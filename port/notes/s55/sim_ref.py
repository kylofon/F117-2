"""S5.5: references and comparison for ctest `sim` (the sim flow, f117port/src/sim).

  python sim_ref.py <f117.exe> <Game dir> <work dir> [--only startup|calls|ticks] [--verbose]

The ORIGINAL VGAME.EXE runs under Unicorn (vgame_emu.py); the port (f117 --sim-test) runs the same things and the two are compared byte for byte:
  startup  DGROUP (up to the end of the BSS), the three pages and the DAC at the entry of sim_run, from a fixed comm block (the one START leaves for the
           first scenario of the shell test, f117port/build/shell_ref/ground/comm.bin or $F117_COMM)
  calls    functions called from a snapshot of the machine, with scripted pokes and arguments; DGROUP, the pages, the DAC and the return value compared
  ticks    the ISR body's counters (simtest.c)
Exit code 77 = no game files.
"""
import sim_scn
import csv, os, re, struct, subprocess, sys, shutil, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vgame_emu as V

ROOT = V.ROOT
BSS_END = 0xE55E

def reloc_mask():
    """DGROUP words that the EXE loader relocates (segment values in the initial data): they carry the load segment, which differs between the two sides."""
    d = open(os.path.join(ROOT, 'work', 'unpacked', 'VGAME.EXE'), 'rb').read()
    h = struct.unpack_from('<14H', d, 0)
    out = []
    for i in range(h[3]):
        off, seg = struct.unpack_from('<HH', d, h[12] + 4 * i)
        pos = seg * 16 + off
        if 0x1E230 <= pos < 0x1E230 + BSS_END:
            out.append((pos - 0x1E230, 2))
    return out


# DGROUP words/ranges that legitimately differ in the START-UP comparison: segments (the port allocates its own), the C library's state, the PIC decoder's scratch
START_MASK = reloc_mask() + [
    (0x011A, 530),                                                # driver slot stubs (far jumps into the drivers)
    (0x008A, 2), (0x85E2, 6), (0x85EA, 2),                        # speech seg, page segments
    (0xB75E, 6), (0xBA0E, 2), (0xE54A, 2), (0x98F6, 2), (0xE558, 2),  # 3DX banks, ordnance sprite, comm segments, panel sheet
    (0x2624, 6),                                                  # INT 1Bh save / flag
    (0x43C8, 0x200 + 0x1A0), (0x9516, 0x9870 - 0x9516 - 6),        # PIC decoder: file buffer, state, row buffer, LZW stack
    (0xC688, 0xDE88 - 0xC688),                                    # LZW table
    (0xC5CA, 14),                                                 # int86x registers
    (0x28A4, 2), (0x28C2, 2), (0x28E0, 2), (0x28FE, 2),            # blit contexts: the panel segment
    (0x91E8, 0x1E8), (0x98EC, 2), (0xDFE8, 2),                    # the C library's FILE table and handle tables (stdio state)
    (0x94EE, 2), (0x986A, 1),                                     # blob cursor segment, LZW stack
    (0xC074, 4),                                                  # file size words (identical when the speech file is read, kept for clarity)
]


def load_names():
    names = []
    try:
        for r in csv.DictReader(open(os.path.join(ROOT, 'port', 'symbols.csv'))):
            if r['exe'] == 'vgame' and r['kind'] == 'global' and r['address'].upper().startswith('DS:'):
                names.append((int(r['address'][3:], 16), r['name']))
    except OSError:
        pass
    names.sort()
    return names


NAMES = load_names()


def name_of(off):
    best = None
    for o, n in NAMES:
        if o <= off:
            best = (o, n)
        else:
            break
    return '%s+%d' % (best[1], off - best[0]) if best else '?'


def masked(buf_ref, buf_port, mask):
    a = bytearray(buf_ref)
    b = bytearray(buf_port)
    for off, n in mask:
        for i in range(off, min(off + n, len(a))):
            a[i] = b[i] = 0
    return a, b


def ranges(a, b, limit=40):
    out = []
    i = 0
    while i < len(a) and len(out) < limit:
        if a[i] != b[i]:
            j = i
            while j < len(a) and a[j] != b[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def compare_state(tag, ref, port, mask, verbose):
    """ref/port = (dgroup, a000, page1, page2, dac) tuples of bytes. Returns the number of differing regions."""
    bad = 0
    a, b = masked(ref[0], port[0], mask)
    rs = ranges(a, b)
    for lo, hi in rs:
        bad += 1
        if verbose or bad <= 25:
            print('  %s DGROUP %04X..%04X (%s) ref %s port %s' % (tag, lo, hi, name_of(lo), bytes(a[lo:min(hi, lo + 12)]).hex(), bytes(b[lo:min(hi, lo + 12)]).hex()))
    for k, nm in ((1, 'page0 A000'), (2, 'page1'), (3, 'page2')):
        if ref[k] != port[k]:
            bad += 1
            d = [i for i in range(64000) if ref[k][i] != port[k][i]]
            print('  %s %s: %d pixels differ, first at row %d col %d (ref %d port %d), rows %d..%d' % (
                tag, nm, len(d), d[0] // 320, d[0] % 320, ref[k][d[0]], port[k][d[0]], d[0] // 320, d[-1] // 320))
    if ref[4] != port[4]:
        bad += 1
        d = [i for i in range(768) if ref[4][i] != port[4][i]]
        print('  %s DAC: %d bytes differ, first colour %d (ref %s port %s)' % (tag, len(d), d[0] // 3, ref[4][d[0] // 3 * 3:d[0] // 3 * 3 + 3].hex(), port[4][d[0] // 3 * 3:d[0] // 3 * 3 + 3].hex()))
    return bad


def parse_dump(raw):
    p = 0
    out = []
    for n in (BSS_END, 64000, 64000, 64000, 768):
        out.append(raw[p:p + n])
        p += n
    return out


def comm_path():
    for p in (os.environ.get('F117_COMM'), os.path.join(ROOT, 'f117port', 'build', 'shell_ref', 'ground', 'comm.bin')):
        if p and os.path.exists(p):
            return p
    return None


def emu_machine(game, wd, comm):
    shutil.rmtree(wd, ignore_errors=True)
    return V.VG(game, wd, open(comm, 'rb').read())


def emu_dump(m):
    return [m.dg(0, BSS_END), m.rb(V.A000, 0, 64000), m.rb(m.dw(0x85E4), 0, 64000), m.rb(m.dw(0x85E6), 0, 64000), bytes(m.dac)]


def test_startup(exe, game, work, verbose):
    comm = comm_path()
    if not comm:
        print('sim: no comm block (run the shell test first or set F117_COMM)')
        return 77
    os.makedirs(work, exist_ok=True)
    shutil.copy(comm, os.path.join(work, 'comm.bin'))
    m = emu_machine(game, os.path.join(work, 'emu'), comm)
    assert m.run_to(0, 0x0233), 'VGAME did not reach sim_run'
    ref = emu_dump(m)
    r = subprocess.run([exe, '--sim-test', work, '--game-dir', game], capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        print('sim startup: port failed:', r.stdout[-400:], r.stderr[-400:])
        return 1
    port = parse_dump(open(os.path.join(work, 'startup_port.bin'), 'rb').read())
    bad = compare_state('startup', ref, port, START_MASK, verbose)
    print('sim startup: %s (%d differing regions)' % ('OK' if not bad else 'FAILED', bad))
    return 0 if not bad else 1


def test_ticks(exe, game, work, verbose):
    """The timer ISR body (tick counters, the sound poll skipped on every 7th tick = 60.08 Hz at 70.09 Hz, the rate switch of the poll result, snd_noise) on the start-up state."""
    comm = comm_path()
    if not comm:
        print('sim: no comm block (run the shell test first or set F117_COMM)')
        return 77
    os.makedirs(work, exist_ok=True)
    shutil.copy(comm, os.path.join(work, 'comm.bin'))
    open(os.path.join(work, 'ticks.req'), 'w').write('ticks\n')
    tf = os.path.join(work, 'ticks.txt')
    if os.path.exists(tf):
        os.remove(tf)
    r = subprocess.run([exe, '--sim-test', work, '--game-dir', game], capture_output=True, text=True, timeout=300)
    os.remove(os.path.join(work, 'ticks.req'))
    if not os.path.exists(tf):
        print('sim ticks: no ticks.txt:', r.stdout[-300:], r.stderr[-300:])
        return 1
    lines = [ln.split() for ln in open(tf).read().splitlines()]
    d = {k: int(v) for k, v in zip(lines[0][1::2], lines[0][2::2])}
    bad = []
    n = 7000
    if d['tick_count'] != n or d['tick_byte'] != n & 0xFF or d['tick_count2'] != n:
        bad.append('counters %s' % d)
    if d['polls'] != n * 6 // 7:
        bad.append('sound polls %d, want %d' % (d['polls'], n * 6 // 7))
    rate = d['polls'] / (n / 70.09)                       # the polls per second of the 70.09 Hz tick
    if abs(rate - 60.08) > 0.02:
        bad.append('poll rate %.3f/s, want 60.08' % rate)
    if d['noise'] != 0:
        bad.append('snd_noise ran with the idle divisor')
    p = {k: int(v) for k, v in zip(lines[1][1::2], lines[1][2::2])}          # poll result +3: fast
    q = {k: int(v) for k, v in zip(lines[2][1::2], lines[2][2::2])}          # poll result -1: idle
    if (p['polls'], p['class'], p['clocks'], p['flag']) != (12, 4, 17024 // 4, 1):
        bad.append('fast switch %s' % p)
    if p['noise'] != 4 * 14:
        bad.append('snd_noise calls with the fast divisor: %d, want %d' % (p['noise'], 4 * 14))
    if (q['polls'], q['class'], q['clocks'], q['flag'], q['noise']) != (12, 1, 17024, 1, 0):
        bad.append('idle switch %s' % q)
    print('sim ticks: %s (%d ticks: %d polls = %.2f/s)' % ('OK' if not bad else 'FAILED ' + '; '.join(bad), n, d['polls'], rate))
    return 1 if bad else 0


# ------------------------------------------------------------------------------------------------------------------ function-call scenarios

CALL_MASK = [
    (0x43C8, 0x200 + 0x1A0), (0x9516, 0x9870 - 0x9516 - 6), (0xC688, 0xDE88 - 0xC688),          # PIC decoder scratch
    (0x91E8, 0x1E8), (0x98EC, 2), (0xDFE8, 2), (0xC5CA, 14), (0x986A, 1),                        # stdio state, int86x regs, LZW stack
    (0x2624, 6),
]
REC_HEAD = 8


def symbols():
    """name -> (segment, offset, far) of the functions of VGAME (port/symbols.csv)."""
    out = {}
    for r in csv.DictReader(open(os.path.join(ROOT, 'port', 'symbols.csv'))):
        if r['exe'] == 'vgame' and r['kind'] == 'func':
            seg, off = [int(x, 16) for x in r['address'].split(':')]
            out.setdefault(r['name'], (seg, off, seg != 0))
    for alias, real in (('tables_clear', 'tables_clear_far'), ('edge_to_tables', 'edge_to_tables_far'), ('hud_color_init', 'hud_color_init_far'),
                        ('hud_layout_init', 'hud_layout_init_far'), ('hud_draw_front', 'hud_draw_far')):     # the far wrappers of the near routines
        out[alias] = out[real]
    return out


def stub_list():
    """The functions the port leaves as stubs: `/* STUB ssss:oooo name */` markers in src/sim; the original gets them hooked to return 0."""
    out = []
    d = os.path.join(ROOT, 'f117port', 'src', 'sim')
    for fn in sorted(os.listdir(d)):
        if fn.endswith('.c'):
            for ln in open(os.path.join(d, fn), encoding='utf-8', errors='replace'):
                m = re.match(r'\s*/\* (?:TEST)?STUB ([0-9a-fA-F]{4}):([0-9a-fA-F]{4}) (\w+)', ln)
                if m:
                    out.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    return out


def write_snapshot(m, work, name):
    mem, a000, dac = m.snapshot()
    open(os.path.join(work, name + '.mem'), 'wb').write(mem)
    open(os.path.join(work, name + '.a000'), 'wb').write(a000)
    open(os.path.join(work, name + '.dac'), 'wb').write(dac)
    mgcode = struct.unpack_from('<H', mem, V.SEG_MG * 16 + 0x18)[0]
    open(os.path.join(work, name + '.meta'), 'w').write('dgroup %X mgdata %X mgcode %X\n' % (m.ds, V.SEG_MG, mgcode))


def parse_ops(lines):
    return [ln.split() for ln in lines]


def emu_rec(m, ret):
    """The record the port writes at `end` (simtest.c)."""
    ax, dx = ret
    out = bytearray(b'SCN1')
    out += struct.pack('<HH', ax & 0xFFFF, dx & 0xFFFF)
    out += m.dg(0, BSS_END)
    pages = [m.rb(V.A000, 0, 64000), m.rb(m.dw(0x85E4), 0, 64000), m.rb(m.dw(0x85E6), 0, 64000)]
    for pg in pages:
        for y in range(200):
            out += struct.pack('<I', zlib.crc32(pg[320 * y:320 * y + 320]) & 0xFFFFFFFF)
    out += bytes(m.dac)
    out += m.rb(V.COMM_SEG, 0, 5120)
    return bytes(out), pages


REC_SIZE = 8 + BSS_END + 3 * 200 * 4 + 768 + 5120


def run_emu_scenarios(m, snaps, scns, syms):
    """snaps: name -> snapshot; scns: list of (id, snapname, [ops]). Returns the records and the pages (for debugging)."""
    recs = []
    for sid, snap, ops in scns:
        m.restore(snaps[snap])
        h, nc = 0, 0
        for op in ops:
            if op[0] == 'poke':
                off, v = int(op[2], 0), int(op[3], 0)
                n = {'b': 1, 'w': 2, 'l': 4}[op[1]]
                m.wb(m.ds, off, (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'little'))
            elif op[0] == 'pokes':
                m.wb(m.ds, int(op[1], 0), bytes.fromhex(op[2]))
            elif op[0] == 'call':
                seg, off, far = syms[op[1]]
                try:
                    args = [int(x, 0) & 0xFFFF for x in op[2:]]
                    if op[1] in sim_scn.ARG_FIXUP:
                        args = sim_scn.ARG_FIXUP[op[1]](args, m.ds)
                    r = m.call(seg, off, args, far=far)
                except Exception as e:
                    raise RuntimeError('scenario %s: %s: %s' % (sid, ' '.join(op), e))
                if op[1] in sim_scn.RET_FUNCS:
                    h = (h * 31 + (r[0] & 0xFFFF)) & 0xFFFF
                nc += 1
        recs.append(emu_rec(m, (h, nc)))
    return recs


def trace_scenario(T, m, syms, sid, exe, work, game):
    """--trace ID: the return value of every call of scenario ID on both sides, the first difference marked"""
    sc = [x for x in T.scns if x[0] == sid]
    if not sc:
        print('no scenario', sid)
        return
    _, snap, ops = sc[0]
    m.restore(T.snaps[snap])
    ref = []
    for op in ops:
        if op[0] == 'poke':
            n = {'b': 1, 'w': 2, 'l': 4}[op[1]]
            m.wb(m.ds, int(op[2], 0), (int(op[3], 0) & ((1 << (8 * n)) - 1)).to_bytes(n, 'little'))
        elif op[0] == 'pokes':
            m.wb(m.ds, int(op[1], 0), bytes.fromhex(op[2]))
        elif op[0] == 'call':
            seg, off, far = syms[op[1]]
            args = [int(x, 0) & 0xFFFF for x in op[2:]]
            if op[1] in sim_scn.ARG_FIXUP:
                args = sim_scn.ARG_FIXUP[op[1]](args, m.ds)
            ref.append((op[1], args, m.call(seg, off, args, far=far)[0]))
    env = dict(os.environ, F117_SIM_TRACE=sid)
    r = subprocess.run([exe, '--sim-test', work, '--game-dir', game], capture_output=True, text=True, env=env, timeout=900)
    port = [ln.split() for ln in r.stdout.splitlines() if ln.startswith('T ')]
    bad = 0
    for i, (nm, args, v) in enumerate(ref):
        pv = int(port[i][2]) if i < len(port) else None
        if pv != v:
            bad += 1
            if bad <= 15:
                print('  call %d %s %s: ref %04X port %s' % (i, nm, [x if x < 0x8000 else x - 0x10000 for x in args], v, '%04X' % pv if pv is not None else '-'))
    print('trace %s: %d calls, %d differ' % (sid, len(ref), bad))


def dump_scenario(T, m, syms, sid, exe, work, game):
    """--dump ID: pixel differences of the pages of scenario ID"""
    sc = [x for x in T.scns if x[0] == sid]
    refs = run_emu_scenarios(m, T.snaps, sc, syms)
    pages_ref = emu_rec(m, (0, 0))[1]
    env = dict(os.environ, F117_SIM_DUMP=sid)
    subprocess.run([exe, '--sim-test', work, '--game-dir', game], capture_output=True, text=True, env=env, timeout=900)
    for k in range(3):
        port = open(os.path.join(work, 'dump_page%d.bin' % k), 'rb').read()
        d = [i for i in range(64000) if port[i] != pages_ref[k][i]]
        print('page %d: %d pixels differ' % (k, len(d)))
        for i in d[:30]:
            print('   x=%d y=%d ref %d port %d' % (i % 320, i // 320, pages_ref[k][i], port[i]))
        if d:
            fx, fy = d[0] % 320, d[0] // 320
            for nm, pg in (('ref', pages_ref[k]), ('port', port)):
                print('   crop %s around (%d,%d):' % (nm, fx, fy))
                for yy in range(max(0, fy - 3), min(200, fy + 14)):
                    print('   %3d ' % yy + ''.join('%02x ' % pg[yy * 320 + xx] if pg[yy * 320 + xx] else ' . ' for xx in range(max(0, fx - 6), min(320, fx + 8))))


def bisect_scenario(T, m, syms, sid, exe, work, game):
    """--bisect ID: the shortest prefix of the calls of scenario ID whose result already differs, and that call"""
    sc = [x for x in T.scns if x[0] == sid]
    if not sc:
        print('no scenario', sid)
        return
    _, snap, ops = sc[0]
    ncalls = sum(1 for o in ops if o[0] == 'call')

    def differs(k):
        cut, n = [], 0
        for o in ops:
            if o[0] == 'call':
                if n >= k:
                    continue
                n += 1
            cut.append(o)
        scns = [(sid, snap, cut)]
        refs = run_emu_scenarios(m, T.snaps, scns, syms)
        with open(os.path.join(work, 'scn.txt'), 'w') as f:
            f.write('scn %s %s\n' % (sid, snap))
            for o in cut:
                f.write(' '.join(o) + '\n')
            f.write('end\n')
        subprocess.run([exe, '--sim-test', work, '--game-dir', game], capture_output=True, text=True, timeout=300)
        raw = open(os.path.join(work, 'scn_port.bin'), 'rb').read()
        return compare_rec(sid, refs[0], raw[:REC_SIZE], CALL_MASK, False), cut
    lo, hi = 0, ncalls
    while lo < hi:
        mid = (lo + hi) // 2
        if differs(mid)[0]:
            hi = mid
        else:
            lo = mid + 1
    probs, cut = differs(lo)
    calls = [o for o in cut if o[0] == 'call']
    print('first differing call: #%d: %s' % (lo, ' '.join(calls[-1]) if calls else '-'))
    print('  last pokes before it:', [' '.join(o) for o in cut if o[0] != 'call'][-6:])
    for p in probs:
        print('     ' + p)


def compare_rec(sid, ref, port, mask, verbose):
    """Returns a list of problem strings."""
    probs = []
    rref, pref = ref
    if port[:4] != b'SCN1':
        return ['no record from the port']
    if rref[4:8] != port[4:8]:
        probs.append('return value ref %04X:%04X port %04X:%04X' % (struct.unpack_from('<H', rref, 6)[0], struct.unpack_from('<H', rref, 4)[0],
                                                                  struct.unpack_from('<H', port, 6)[0], struct.unpack_from('<H', port, 4)[0]))
    a, b = masked(rref[8:8 + BSS_END], port[8:8 + BSS_END], mask)
    for lo, hi in ranges(a, b, 12):
        probs.append('DGROUP %04X..%04X (%s) ref %s port %s' % (lo, hi, name_of(lo), bytes(a[lo:min(hi, lo + 10)]).hex(), bytes(b[lo:min(hi, lo + 10)]).hex()))
    o = 8 + BSS_END
    for k, nm in enumerate(('page0', 'page1', 'page2')):
        ra, pa = struct.unpack_from('<200I', rref, o + 800 * k), struct.unpack_from('<200I', port, o + 800 * k)
        rows = [y for y in range(200) if ra[y] != pa[y]]
        if rows:
            probs.append('%s: %d rows differ (first %d, last %d)' % (nm, len(rows), rows[0], rows[-1]))
    o += 2400
    if rref[o:o + 768] != port[o:o + 768]:
        d = [i // 3 for i in range(768) if rref[o + i] != port[o + i]]
        probs.append('DAC: colours differ from %d (%d bytes)' % (d[0], len(d)))
    o += 768
    if rref[o:o + 5120] != port[o:o + 5120]:
        d = [i for i in range(5120) if rref[o + i] != port[o + i]]
        probs.append('comm block: %d bytes differ, first at +%03X ref %02X port %02X' % (len(d), d[0], rref[o + d[0]], port[o + d[0]]))
    return probs


class CallTests:
    def __init__(self, exe, game, work, verbose):
        self.exe, self.game, self.work, self.verbose = exe, game, work, verbose
        self.scns = []
        self.snaps = {}

    def add(self, sid, snap, *ops):
        lines = []
        for op in ops:
            lines.extend(op if isinstance(op, list) else [op])
        self.scns.append((sid, snap, parse_ops(lines)))

    def run(self, m, syms):
        os.makedirs(self.work, exist_ok=True)
        with open(os.path.join(self.work, 'scn.txt'), 'w') as f:
            for sid, snap, ops in self.scns:
                f.write('scn %s %s\n' % (sid, snap))
                for op in ops:
                    f.write(' '.join(op) + '\n')
                f.write('end\n')
        refs = run_emu_scenarios(m, self.snaps, self.scns, syms)
        r = subprocess.run([self.exe, '--sim-test', self.work, '--game-dir', self.game], capture_output=True, text=True, timeout=900)
        if r.returncode != 0:
            print('sim calls: the port failed:', r.stdout[-600:], r.stderr[-600:])
            return 1
        raw = open(os.path.join(self.work, 'scn_port.bin'), 'rb').read()
        if len(raw) != REC_SIZE * len(self.scns):
            print('sim calls: %d bytes from the port, expected %d' % (len(raw), REC_SIZE * len(self.scns)))
            return 1
        bad = 0
        for i, (sid, snap, ops) in enumerate(self.scns):
            probs = compare_rec(sid, refs[i], raw[i * REC_SIZE:(i + 1) * REC_SIZE], CALL_MASK, self.verbose)
            if probs:
                bad += 1
                if bad <= 25:
                    print('  scenario %s (%s): %s' % (sid, snap, '; '.join(' '.join(op) for op in ops if op[0] == 'call')[:120]))
                    for p in probs:
                        print('      ' + p)
        print('sim calls: %d scenarios, %d differ' % (len(self.scns), bad))
        return 1 if bad else 0


TRACE = None
DUMP = None
BISECT = None


def test_calls(exe, game, work, verbose, only_fam=None):
    import random
    import sim_scn
    comm = comm_path()
    if not comm:
        return 77
    os.makedirs(work, exist_ok=True)
    m = emu_machine(game, os.path.join(work, 'emu'), comm)
    assert m.run_to(0, 0x0233), 'VGAME did not reach sim_run'
    syms = symbols()
    for seg, off, name in stub_list():
        m.hook_stub(seg, off, seg != 0)
    if os.environ.get('PROBEFN'):                      # PROBEFN=name:nargs (near functions): print the arguments of every call
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_SP, UC_X86_REG_SS
        fname, nargs = os.environ['PROBEFN'].split(':')
        fseg, foff = syms[fname][:2]

        def probefn(uc, addr, size, _):
            sp = uc.reg_read(UC_X86_REG_SP)
            ss = uc.reg_read(UC_X86_REG_SS)
            a = struct.unpack('<%dh' % int(nargs), m.rb(ss, sp + 2, 2 * int(nargs)))
            print('PROBEFN %s %s' % (fname, a))
        m.uc.hook_add(UC_HOOK_CODE, probefn, None, m.lin(fseg, foff), m.lin(fseg, foff))
    if os.environ.get('PROBE'):
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_SP, UC_X86_REG_SS
        pseg, poff = syms['poly_fill_far'][:2]

        def probe(uc, addr, size, _):
            sp = uc.reg_read(UC_X86_REG_SP)
            ss = uc.reg_read(UC_X86_REG_SS)
            vp, n, pts, col = struct.unpack('<HHHH', m.rb(ss, sp + 4, 8))
            w = struct.unpack('<8h', m.rb(m.ds, pts, 16))
            print('PROBE poly n=%d col=%d pts=%s' % (n, col, w))
        m.uc.hook_add(UC_HOOK_CODE, probe, None, m.lin(pseg, poff), m.lin(pseg, poff))
    T = CallTests(exe, game, work, verbose)
    write_snapshot(m, work, 's0')
    T.snaps['s0'] = m.snapshot()
    # s1: the first step of the ORIGINAL (the mission initialisation) on top of s0 = a running mission
    m.wb(m.ds, 0xC0AE, struct.pack('<H', 1))
    m.call(0, syms['sim_frame_step'][1], [])
    write_snapshot(m, work, 's1')
    T.snaps['s1'] = m.snapshot()
    m.restore(T.snaps['s0'])
    rng = random.Random(55)
    for name, fn in sim_scn.FAMILIES:
        if only_fam in (None, name):
            fn(T, rng)
    if BISECT:
        T.scns = [x for x in T.scns if x[0] == BISECT]
        T.run(m, syms)
        bisect_scenario(T, m, syms, BISECT, exe, work, game)
        return 0
    if DUMP:
        T.scns = [x for x in T.scns if x[0] == DUMP]
        T.run(m, syms)
        dump_scenario(T, m, syms, DUMP, exe, work, game)
        return 0
    if TRACE:
        T.run(m, syms)
        trace_scenario(T, m, syms, TRACE, exe, work, game)
        return 0
    return T.run(m, syms)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    verbose = '--verbose' in sys.argv
    global TRACE, DUMP, BISECT
    BISECT = sys.argv[sys.argv.index('--bisect') + 1] if '--bisect' in sys.argv else None
    DUMP = sys.argv[sys.argv.index('--dump') + 1] if '--dump' in sys.argv else None
    TRACE = sys.argv[sys.argv.index('--trace') + 1] if '--trace' in sys.argv else None
    only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
    if len(args) < 3:
        print(__doc__)
        return 2
    exe, game, work = [os.path.abspath(a) for a in args[:3]]
    if not os.path.exists(os.path.join(game, 'VGAME.EXE')) or not os.path.exists(os.path.join(ROOT, 'work', 'unpacked', 'VGAME.EXE')):
        print('sim: no game files')
        return 77
    rc = 0
    if only in (None, 'startup'):
        r = test_startup(exe, game, os.path.join(work, 'startup'), verbose)
        if r == 77:
            return 77
        rc |= r
    if only in (None, 'ticks'):
        r = test_ticks(exe, game, os.path.join(work, 'ticks'), verbose)
        if r == 77:
            return 77
        rc |= r
    if only is None or only.startswith('calls'):
        r = test_calls(exe, game, os.path.join(work, 'calls'), verbose, only.split(':')[1] if only and ':' in only else None)
        if r == 77:
            return 77
        rc |= r
    return rc


if __name__ == '__main__':
    sys.exit(main())
