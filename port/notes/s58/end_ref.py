"""S5.8: reference data and the test driver of `f117 --debrief-test` (ctest `debrief`), and the scripted chain run with a real END (ctest `debrief_chain`).

* The ORIGINAL END.EXE (work/unpacked, from tools/unlzexe.py) runs under Unicorn (end_emu.py) once per mission outcome. The comm block of a scenario is the one the
  S5.4 `shell` test left for the ground-strike mission (the world: sites, grid, name pool, mission slots) with the result fields VGAME writes (comm+26h/28h/2Ch/2Eh/
  30h/33h/34h/36h/74h/76h), an event log written here and a pilot record. Per script step (wait_input entries, every 3rd poll of the map menu) the screen and DAC CRCs,
  and at the exit the comm block (with the pilot record END updated) and the exit code are written to REFROOT/<scenario>/.
* The port runs the same scripts (endtest.c) and must give the same CRCs at every step, the same comm block byte for byte and the same exit code 23h.

usage: python end_ref.py <f117 executable> <game dir> [refroot]          (the test)
       python end_ref.py --chain <f117 executable> <game dir>            (the scripted run of the whole chain: a mission ends in VGAME, END runs, START saves the roster)
       python end_ref.py --one <f117 executable> <game dir> <scenario>   (debugging: one scenario on both sides with the screens dumped)
exit 77 (ctest SKIP) when the game folder, work/unpacked/END.EXE or the `shell` comm block is missing.
"""
import hashlib, os, re, shutil, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 's54'))

SEED = 12345


def base_comm():
    for p in (os.environ.get('F117_COMM'), os.path.join(ROOT, 'f117port', 'build', 'shell_ref', 'ground', 'comm.bin')):
        if p and os.path.isfile(p):
            return open(p, 'rb').read()
    return None


def have_game(game):
    return (os.path.isfile(os.path.join(game, 'END.EXE')) and os.path.isfile(os.path.join(game, 'MGRAPHIC.EXE'))
            and os.path.isfile(os.path.join(ROOT, 'work', 'unpacked', 'END.EXE')) and base_comm() is not None)


def scratch_game(game):
    dst = os.path.join(ROOT, 'work', 's58', 'game')
    os.makedirs(dst, exist_ok=True)
    for f in os.listdir(game):
        p = os.path.join(game, f)
        if os.path.isfile(p) and f.upper() != 'ROSTER.FIL':
            q = os.path.join(dst, f)
            if not os.path.exists(q) or os.path.getsize(q) != os.path.getsize(p):
                shutil.copy2(p, q)
    open(os.path.join(dst, 'SCRATCH.MARK'), 'w').write('scratch copy for f117 --debrief-test\n')
    return dst


# ---------------------------------------------------------------------------------------------------------------------- the scenarios

def u16(d, o): return struct.unpack_from('<H', d, o)[0]


def layout(d):
    """offsets inside the comm block of the blob parts END reads (spec debrief.md 1.3)"""
    o = 0x7A + 8
    sc = u16(d, 0x7A + 2)
    sites = o
    o += 16 * sc
    uc = u16(d, o)
    o += 2 + 36 * uc
    cls = o
    o += 128 + 128 + 750
    grid = o
    o += 256 + 4 + 16
    slots = o
    o += 36
    return dict(sites=sites, nsites=sc, cls=cls, grid=grid, slots=slots, log=o)


class Mission:
    """a comm block under construction"""
    def __init__(s, base):
        s.d = bytearray(base)
        s.L = layout(base)
        s.events = []
        s.pos = (0, 0)
        s.tick = 0

    def site_xy(s, i):
        o = s.L['sites'] + 16 * i
        return u16(s.d, o + 2), u16(s.d, o + 4)

    def cell_xy(s, friendly):
        for c in range(256):
            if (s.d[s.L['grid'] + c] & 3 != 0) == friendly and c not in (0, 255):
                return (c % 16) * 2048 + 1000, (c // 16) * 2048 + 1000
        raise RuntimeError('no such grid cell')

    def ev(s, t, arg, tick_step=40):
        s.tick += tick_step
        s.events.append(struct.pack('<HBBBB', s.tick & 0xFFFF, (s.pos[0] >> 7) & 0xFF, (s.pos[1] >> 7) & 0xFF, t, arg))

    def fly(s, to, samples=4):
        """position samples along the leg to the point `to` (a site index or an (x, y) pair), then stay there"""
        x1, y1 = s.site_xy(to) if isinstance(to, int) else to
        x0, y0 = s.pos
        for k in range(1, samples + 1):
            s.pos = (x0 + (x1 - x0) * k // (samples + 1), y0 + (y1 - y0) * k // (samples + 1))
            s.ev(9, 0, 30)
        s.pos = (x1, y1)

    def run(s, plan):
        for item in plan:
            if item[0] == 'at':
                s.pos = s.site_xy(item[1]) if isinstance(item[1], int) else item[1]
            elif item[0] == 'fly':
                s.fly(item[1])
            else:
                s.ev(item[0], item[1])
        # the final position of the flight: the last point of the route
        struct.pack_into('<HH', s.d, 0x74, s.pos[0] & 0xFFFF, s.pos[1] & 0xFFFF)
        log = b''.join(s.events) + bytes(6)
        assert len(log) <= 1536
        s.d[s.L['log']:s.L['log'] + len(log)] = log

    def comm(s, **kw):
        for k, v in kw.items():
            off = {'c26': 0x26, 'c28': 0x28, 'c2c': 0x2C, 'c2e': 0x2E, 'c30': 0x30, 'c34': 0x34, 'c36': 0x36}[k]
            struct.pack_into('<H', s.d, off, v)

    def night(s, n): s.d[0x33] = n

    def pilot(s, **kw):
        names = dict(rank=0x20, ph=0x22, am=0x24, dfc=0x26, ss=0x28, afc=0x2A, cmh=0x2C, best=0x2E, last=0x30, missions=0x36, theatre=0x38, war=0x3A, mtype=0x3C,
                     skill=0x3E, crash=0x40, model=0x42, crr=0x44, short=0x46, long=0x48, lev=0x4A, gold=0x4C, status=0x4E)
        for k, v in kw.items():
            if k == 'career':
                struct.pack_into('<I', s.d, 0x1272 + 0x32, v)
            else:
                struct.pack_into('<H', s.d, 0x1272 + names[k], v)


def sc_landed(base):
    m = Mission(base)
    m.night(1)
    m.comm(c26=3, c28=0, c2c=5, c2e=8000, c30=0, c34=0, c36=2)
    m.pilot(rank=1, am=1, best=150, last=120, career=2500, missions=4, theatre=1, war=1, mtype=0, skill=1, crash=1, model=1, short=0)
    m.run([('at', 63), (8, 0), ('fly', 13), (7, 13), (6, 5), (3, 5 | 0x80), (5, 2), (3, 6), ('fly', 1), (0x81, 1), (4, 0), (1, 7), ('fly', 2), (0x41, 2),
           (2, 9), (3, 12 | 0x80), (10, 4), (11, 0), (0x8B, 0), ('fly', 33), (2, 12), (1, 3), (3, 20), ('fly', 63), (8, 0)])
    return m, dict(skipmarks=False, presses=None)


def sc_rescued(base):
    m = Mission(base)
    m.comm(c26=2, c28=0, c2c=1, c2e=10000, c30=0, c34=0, c36=3)
    m.pilot(rank=2, best=300, last=200, career=7000, missions=12, theatre=1, war=0, mtype=1, skill=2, crash=2, model=0, long=1)
    m.run([('at', 63), (8, 0), ('fly', 10), (7, 10), (2, 40), (1, 5), ('fly', 3), (0x81, 1), (3, 8), ('fly', m.cell_xy(True)), (8, 0)])
    return m, dict(skipmarks=True, presses=None)


def sc_captured(base):
    m = Mission(base)
    m.comm(c26=2, c28=0, c2c=4, c2e=12000, c30=0, c34=0xC, c36=8)
    m.pilot(rank=1, best=900, last=600, career=300000 // 1000 * 1000, missions=20, theatre=1, war=2, mtype=2, skill=3, crash=2, model=0)
    m.run([('at', 63), (8, 0), ('fly', 14), (6, 3), (7, 14), (3, 10 | 0x80), (2, 100), (2, 20), (1, 6), (5, 1), (4, 2), ('fly', 9), (1, 9),
           (0x41, 2), ('fly', m.cell_xy(False)), (8, 0)])
    return m, dict(skipmarks=False, presses=None)


def sc_crashed(base):
    m = Mission(base)
    m.comm(c26=1, c28=2, c2c=6, c2e=7000, c30=0, c34=0, c36=4)
    m.pilot(rank=0, best=50, last=40, career=90, missions=1, theatre=1, war=1, mtype=0, skill=0, crash=0, model=0)
    m.run([('at', 63), (8, 0), ('fly', 6), (7, 6), (6, 9), (3, 9), (5, 4), ('fly', 7), (1, 3), (8, 0)])
    return m, dict(skipmarks=False, presses=None)


def sc_promoted(base):
    """a promotion with a Purple Heart (stored, not shown), the Distinguished Flying Cross and the long tour ribbon"""
    m = Mission(base)
    m.comm(c26=3, c28=0, c2c=2, c2e=9500, c30=0, c34=0xC, c36=9)
    m.pilot(rank=0, best=400, last=350, career=280, missions=9, theatre=2, war=0, mtype=0, skill=2, crash=1, model=0, short=1)
    plan = [('at', 63), (8, 0), ('fly', 13), (7, 13)]
    for i in range(6):
        plan += [(3, 5), (0x81, 1), (1, 3)]
    plan += [(0x41, 2), (11, 0), ('fly', 63), (8, 0)]
    m.run(plan)
    return m, dict(skipmarks=False, presses=None)


def sc_training(base):
    """a training flight with the primary objective done: the Combat Readiness Ribbon only"""
    m = Mission(base)
    m.comm(c26=3, c28=0, c2c=0, c2e=7500, c30=1, c34=0, c36=2)
    m.pilot(rank=0, best=10, last=10, career=20, missions=0, theatre=1, war=1, mtype=2, skill=1, crash=1, model=0)
    m.run([('at', 63), (8, 0), ('fly', 1), (0x81, 1), (1, 7), ('fly', 63), (8, 0)])
    return m, dict(skipmarks=False, presses=None)


def sc_general(base):
    """the 99th mission of a Colonel who is killed after the ejection failed: retired, then the brigadier general promotion on the flight deck is not reached"""
    m = Mission(base)
    m.comm(c26=1, c28=6, c2c=3, c2e=11800, c30=0, c34=0, c36=2)
    m.pilot(rank=5, ph=1, am=3, dfc=2, ss=1, afc=1, cmh=1, best=1500, last=900, career=62000, missions=98, theatre=3, war=2, mtype=3, skill=3, crash=2, model=1,
            crr=1, long=1, lev=1, gold=0)
    m.run([('at', 63), (8, 0), ('fly', 12), (7, 12), (3, 4 | 0x80), (3, 7), (5, 3), ('fly', 5), (0x81, 1), (4, 1), (8, 0)])
    return m, dict(skipmarks=False, presses=None)


def sc_retire(base):
    """the 99th mission of a Colonel who lands: promotion to Brigadier General, the retirement text, the closing scene on the flight deck"""
    m = Mission(base)
    m.comm(c26=3, c28=0, c2c=1, c2e=9200, c30=0, c34=0, c36=2)
    m.pilot(rank=5, best=1500, last=900, career=65500, missions=98, theatre=0, war=0, mtype=1, skill=2, crash=1, model=0, long=1, lev=1)
    m.run([('at', 63), (8, 0), ('fly', 13), (3, 3 | 0x80), (3, 4), (2, 7), (1, 1), ('fly', 63), (8, 0)])
    return m, dict(skipmarks=False, presses=None)


SCENARIOS = {'landed': sc_landed, 'rescued': sc_rescued, 'captured': sc_captured, 'crashed': sc_crashed, 'promoted': sc_promoted, 'training': sc_training,
             'general': sc_general, 'retire': sc_retire}


def stops(m, war):
    """the number of events a press of 'next' stops at (type 9 never; types 6 and 7 not in war 2)"""
    n = 0
    war = struct.unpack_from('<H', m.d, 0x1272 + 0x3A)[0]
    for e in m.events:
        t = e[4] & 0x3F
        if t == 9 or (war == 2 and t in (6, 7)):
            continue
        n += 1
    return n


def script_text(m, opt):
    war = struct.unpack_from('<H', m.d, 0x1272 + 0x3A)[0]
    n = stops(m, war)
    lines = ['seed %d' % SEED]
    if opt['skipmarks']:
        lines.append('skipmarks')
    lines.append('270 66')                       # the cursor on the second button: map_hover pushes its colour
    lines.append('272 56')                       # and back on the first
    lines += ['- - 1C0D'] * (n + 2)               # next: every event, the end of the log (the clean map) and the restart
    lines.append('- - 1000 314E')                # Alt-Q, then N: the quit prompt is cancelled
    lines.append('- - 011B')                     # Esc: the second button's meaning
    return '\n'.join(lines) + '\n'


def parse_script(text):
    steps, skip = [], False
    for ln in text.splitlines():
        f = ln.split('#')[0].split()
        if not f or f[0] == 'seed':
            continue
        if f[0] == 'skipmarks':
            skip = True
            continue
        steps.append((None if f[0] == '-' else int(f[0]), None if f[1] == '-' else int(f[1]), [int(k, 16) for k in f[2:]]))
    return steps, skip


# ---------------------------------------------------------------------------------------------------------------------------- references

def stamp(game):
    h = hashlib.sha1()
    files = [os.path.join(HERE, 'end_emu.py'), os.path.join(HERE, 'end_ref.py'), os.path.join(HERE, '..', 's53', 'dos_emu.py'), os.path.join(HERE, '..', 's54', 'shell_emu.py'),
             os.path.join(ROOT, 'work', 'unpacked', 'END.EXE')] + [os.path.join(game, n) for n in ('MGRAPHIC.EXE', 'MISC.EXE')]
    for f in files:
        h.update(os.path.basename(f).encode())
        h.update(open(f, 'rb').read())
    h.update(base_comm())
    return h.hexdigest()


def write_scenario(name, outdir, base):
    m, opt = SCENARIOS[name](base)
    text = script_text(m, opt)
    steps, skip = parse_script(text)
    import end_emu
    code, em = end_emu.run(bytes(m.d), steps, skip, outdir, dump=False)
    open(os.path.join(outdir, 'script.txt'), 'w').write(text)
    assert code == 0x23 and not em.steps, '%s scenario: exit code %r, %d steps left' % (name, code, len(em.steps))
    return em


def references(game, refroot, only=None):
    t = time.time()
    st = stamp(game)
    sf = os.path.join(refroot, 'stamp.txt')
    names = [only] if only else list(SCENARIOS)
    if not only and os.path.exists(sf) and open(sf).read().strip() == st and os.path.exists(os.path.join(refroot, 'scenarios.txt')):
        return 0.0
    if os.path.exists(sf):
        os.remove(sf)
    base = base_comm()
    for name in names:
        t0 = time.time()
        write_scenario(name, os.path.join(refroot, name), base)
        print('  reference %-9s %.0f s' % (name, time.time() - t0))
        sys.stdout.flush()
    if not only:
        open(os.path.join(refroot, 'scenarios.txt'), 'w').write('\n'.join(names) + '\n')
        open(sf, 'w').write(st + '\n')
    return time.time() - t


def test(exe, game, refroot):
    if not have_game(game):
        print('no game files, work/unpacked/END.EXE or shell comm block for the debrief references')
        return 77
    refroot = refroot or os.path.join(ROOT, 'work', 's58', 'ref')
    os.makedirs(refroot, exist_ok=True)
    secs = references(game, refroot)
    print('end_ref: original END under Unicorn: %d scenarios (%.0f s) -> %s' % (len(SCENARIOS), secs, refroot))
    sys.stdout.flush()
    return subprocess.call([exe, '--headless', '--debrief-test', refroot, '--game-dir', scratch_game(game)])


def one(exe, game, name):
    refroot = os.path.join(ROOT, 'work', 's58', 'one')
    shutil.rmtree(refroot, ignore_errors=True)
    os.makedirs(refroot)
    import end_emu
    m, opt = SCENARIOS[name](base_comm())
    text = script_text(m, opt)
    steps, skip = parse_script(text)
    outdir = os.path.join(refroot, name)
    code, em = end_emu.run(bytes(m.d), steps, skip, outdir, dump=True)
    open(os.path.join(outdir, 'script.txt'), 'w').write(text)
    open(os.path.join(refroot, 'scenarios.txt'), 'w').write(name + '\n')
    os.makedirs(os.path.join(outdir, 'portdump'), exist_ok=True)
    env = dict(os.environ, F117_END_DUMP_ROOT=refroot)
    rc = subprocess.call([exe, '--headless', '--debrief-test', refroot, '--game-dir', scratch_game(game)], env=env)
    d1, d2 = os.path.join(outdir, 'dump'), os.path.join(outdir, 'portdump')
    for f in sorted(os.listdir(d1)):
        if f in os.listdir(d2) and open(os.path.join(d1, f), 'rb').read() != open(os.path.join(d2, f), 'rb').read():
            print('step', f)
            diff(refroot, name, int(f[1:3]))
    return rc


def diff(refroot, scenario, step):
    a = open(os.path.join(refroot, scenario, 'dump', 'p%02d.scr' % step), 'rb').read()
    b = open(os.path.join(refroot, scenario, 'portdump', 'p%02d.scr' % step), 'rb').read()
    xs, ys = [], []
    for i in range(64000):
        if a[i] != b[i]:
            xs.append(i % 320)
            ys.append(i // 320)
    print('screen: %d pixels differ' % len(xs), ('bbox x %d..%d y %d..%d' % (min(xs), max(xs), min(ys), max(ys))) if xs else '')
    d = [i for i in range(256) if a[64000 + 3 * i:64003 + 3 * i] != b[64000 + 3 * i:64003 + 3 * i]]
    print('DAC: %d entries differ' % len(d), d[:16])


# -------------------------------------------------------------------------------------------------------------------------------- chain

def chain(exe, game):
    """The scripted run of the whole chain: START (new pilot, strike mission, launch) -> VGAME ends the flight with a landing after a few frames
    (F117_VGAME_FRAMES, F117_VGAME_END=0) -> END for real (F117_END_SCRIPT answers its prompts and walks the replay) -> START again saves the roster -> quit.
    The roster START saved must hold the pilot record END wrote (the mission counted, the score, the career)."""
    if not os.path.isfile(os.path.join(game, 'END.EXE')) or not os.path.isfile(os.path.join(ROOT, 'work', 'unpacked', 'START.EXE')):
        print('no game files')
        return 77
    import shell_ref, shell_emu
    scratch = shell_ref.scratch_game(game)
    open(os.path.join(scratch, 'ROSTER.FIL'), 'wb').write(shell_emu.make_roster())
    env = dict(os.environ, F117_KEYS='1:31,2:03,11:39', F117_SHELL_SCRIPT=os.path.join(HERE, '..', 's54', 'shell_chain.txt'), F117_VGAME_FRAMES='40',
               F117_VGAME_END='0', F117_END_SCRIPT=os.path.join(HERE, 'end_chain.txt'))
    t = time.time()
    p = subprocess.run([exe, '--headless', '--trace', '--game-dir', scratch], env=env, capture_output=True, text=True, timeout=900)
    secs = time.time() - t
    out = p.stdout
    want = [r'EXEC START\.EXE', r'START\.EXE exit code 0[CD]h', r'EXEC VGAME\.EXE', r'EXEC DSWAP\.EXE\s+tail " 2"', r'EXEC END\.EXE', r'\[END\] pilot record: [^\n]*',
            r'END\.EXE exit code 23h', r'EXEC DSWAP\.EXE\s+tail " 0"', r'EXEC START\.EXE', r'START\.EXE exit code 00h', r'quit: START returned 0']
    pos = 0
    rec = None
    for w in want:
        m = re.compile(w).search(out, pos)
        if not m:
            print('debrief_chain FAILED: "%s" not found after offset %d\n%s' % (w, pos, out[-2500:]))
            print(p.stderr[-1000:])
            return 1
        if w.startswith(r'\[END'):
            rec = m.group(0)
        pos = m.end()
    mm = re.search(r'missions=(\d+) last=(\d+) best=(\d+) career=(\d+) status=(\d+) rank=(\d+)', rec)
    end_rec = tuple(int(x) for x in mm.groups())                  # missions, last, best, career, status, rank
    r = open(os.path.join(scratch, 'ROSTER.FIL'), 'rb').read()
    i = r.find(b'kim', 2)
    if i < 0:
        print('debrief_chain FAILED: the new pilot is not in ROSTER.FIL')
        return 1
    o = i - 2                                                     # the pilot record: name at +2
    best, last = struct.unpack_from('<HH', r, o + 0x2E)
    career, = struct.unpack_from('<I', r, o + 0x32)
    missions, = struct.unpack_from('<H', r, o + 0x36)
    status, = struct.unpack_from('<H', r, o + 0x4E)
    rank, = struct.unpack_from('<H', r, o + 0x20)
    got = (missions, last, best, career, status, rank)
    if end_rec[0] != 1 or got != end_rec:
        print('debrief_chain FAILED: END wrote (missions, last, best, career, status, rank) = %r; ROSTER.FIL has %r' % (end_rec, got))
        return 1
    print('debrief_chain OK: START -> VGAME (landing) -> END (debrief, score %d, career %d) -> START saved the roster with the same record, %.0f s' % (last, career, secs))
    return 0


if __name__ == '__main__':
    if len(sys.argv) > 3 and sys.argv[1] == '--chain':
        sys.exit(chain(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])))
    if len(sys.argv) > 4 and sys.argv[1] == '--one':
        sys.exit(one(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3]), sys.argv[4]))
    sys.exit(test(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None))
