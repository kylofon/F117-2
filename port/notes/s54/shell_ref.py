"""S5.4: reference data and the test driver of `f117 --shell-test` (ctest `shell`), and the scripted chain run (ctest `shell_chain`).

* The ORIGINAL START.EXE (work/unpacked, from tools/unlzexe.py) runs under Unicorn (shell_emu.py) through two scripts: shell_ground.txt (create
  a pilot, options, orders for a ground strike in the Persian Gulf, briefing with its map modes and the orders pages, arming, launch) and
  shell_quit.txt (a roster written by the first run is read, normalised and saved, then Alt-Q).  Per script step the screen and DAC CRC, and at
  the end the comm block and ROSTER.FIL are written to REFROOT/<scenario>/.
* The port runs the same scripts (shelltest.c) on a scratch copy of the game folder and must produce the same CRCs, the same comm block (the mission
  generated for the fixed seed, byte for byte) and the same ROSTER.FIL.

usage: python shell_ref.py <f117 executable> <game dir> [refroot]          (the test)
       python shell_ref.py --chain <f117 executable> <game dir>            (the scripted run of the whole chain)
       python shell_ref.py --diff <refroot> <scenario> <step>               (pixel difference of a dumped step: original `dump/`, port `portdump/`)
exit 77 (ctest SKIP) when the game folder or work/unpacked/START.EXE is missing.
"""
import os, re, shutil, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)


def scratch_game(game):
    dst = os.path.join(ROOT, 'work', 's54', 'game')
    os.makedirs(dst, exist_ok=True)
    for f in os.listdir(game):
        p = os.path.join(game, f)
        if os.path.isfile(p) and f.upper() != 'ROSTER.FIL':
            q = os.path.join(dst, f)
            if not os.path.exists(q) or os.path.getsize(q) != os.path.getsize(p):
                shutil.copy2(p, q)
    open(os.path.join(dst, 'SCRATCH.MARK'), 'w').write('scratch copy for f117 --shell-test; ROSTER.FIL is overwritten\n')
    return dst


def have_game(game):
    return (os.path.isfile(os.path.join(game, 'START.EXE')) and os.path.isfile(os.path.join(game, 'MGRAPHIC.EXE'))
            and os.path.isfile(os.path.join(ROOT, 'work', 'unpacked', 'START.EXE')))


SCENARIOS = ['ground', 'air', 'airtrain', 'striketrain', 'lockheed']


def stamp(game):
    """A hash of everything the references depend on: the scripts, the emulator, the original programs."""
    import hashlib
    h = hashlib.sha1()
    files = [os.path.join(HERE, f) for f in sorted(os.listdir(HERE)) if f.startswith('shell_') and f.endswith(('.txt', '.py')) and f != 'shell_ref.py']
    files += [os.path.join(HERE, '..', 's53', 'dos_emu.py'), os.path.join(ROOT, 'work', 'unpacked', 'START.EXE')]
    files += [os.path.join(game, n) for n in ('MGRAPHIC.EXE', 'MISC.EXE', 'PALETTES.PAL')]
    for f in files:
        h.update(os.path.basename(f).encode())
        h.update(open(f, 'rb').read())
    return h.hexdigest()


def references(game, refroot, dump=False):
    import shell_emu
    t = time.time()
    st = stamp(game)
    sf = os.path.join(refroot, 'stamp.txt')
    if not dump and os.path.exists(sf) and open(sf).read().strip() == st and os.path.exists(os.path.join(refroot, 'scenarios.txt')):
        return 0.0                                       # unchanged since the last run: the references are still good
    if os.path.exists(sf):
        os.remove(sf)
    for name in SCENARIOS:
        code, m = shell_emu.run(os.path.join(HERE, 'shell_%s.txt' % name), os.path.join(refroot, name), game, dump=dump)
        assert code in (0x0C, 0x0D) and not m.script, '%s scenario: exit code %r, %d steps left' % (name, code, len(m.script))
    roster = open(os.path.join(refroot, 'ground', 'roster_out.fil'), 'rb').read()
    code, m = shell_emu.run(os.path.join(HERE, 'shell_quit.txt'), os.path.join(refroot, 'quit'), game, roster=roster, dump=dump)
    assert code == 0 and not m.script, 'quit scenario: exit code %r' % code
    out = open(os.path.join(refroot, 'quit', 'roster_out.fil'), 'rb').read()
    assert out == roster, 'the ORIGINAL does not rewrite its own ROSTER.FIL unchanged'
    open(os.path.join(refroot, 'scenarios.txt'), 'w').write(chr(10).join(SCENARIOS + ['quit']) + chr(10))
    open(sf, 'w').write(st + chr(10))
    return time.time() - t


def test(exe, game, refroot, dump=False):
    if not have_game(game):
        print('no game files (or work/unpacked) for the shell references')
        return 77
    refroot = refroot or os.path.join(ROOT, 'work', 's54', 'ref')
    secs = references(game, refroot, dump)
    print('shell_ref: original START under Unicorn: ground and quit scenarios (%.0f s) -> %s' % (secs, refroot))
    sys.stdout.flush()
    scratch = scratch_game(game)
    env = dict(os.environ)
    if dump:
        for sc in SCENARIOS + ['quit']:
            os.makedirs(os.path.join(refroot, sc, 'portdump'), exist_ok=True)
        env['F117_SHELL_DUMP_ROOT'] = refroot
    return subprocess.call([exe, '--headless', '--shell-test', refroot, '--game-dir', scratch], env=env)


def one(exe, game, name):
    """Debugging: one scenario on both sides with the screens dumped; prints the pixel / DAC differences of every step that differs."""
    import shell_emu
    refroot = os.path.join(ROOT, 'work', 's54', 'one')
    shutil.rmtree(refroot, ignore_errors=True)
    roster = None
    if name == 'quit':
        roster = open(os.path.join(ROOT, 'work', 's54', 'ref', 'ground', 'roster_out.fil'), 'rb').read()
    code, m = shell_emu.run(os.path.join(HERE, 'shell_%s.txt' % name), os.path.join(refroot, name), game, roster=roster, dump=True)
    open(os.path.join(refroot, 'scenarios.txt'), 'w').write(name + chr(10))
    os.makedirs(os.path.join(refroot, name, 'portdump'), exist_ok=True)
    env = dict(os.environ, F117_SHELL_DUMP_ROOT=refroot)
    rc = subprocess.call([exe, '--headless', '--shell-test', refroot, '--game-dir', scratch_game(game)], env=env)
    d1, d2 = os.path.join(refroot, name, 'dump'), os.path.join(refroot, name, 'portdump')
    for f in sorted(os.listdir(d1)):
        if f in os.listdir(d2) and open(os.path.join(d1, f), 'rb').read() != open(os.path.join(d2, f), 'rb').read():
            print('step', f)
            diff(refroot, name, int(f[1:3]))
    return rc


def chain(exe, game):
    """The scripted run: F117_KEYS answers SETUP ('N' at 1 s, '2' at 2 s), the logo plays, a space at 11 s ends the PLAYER intro; then START runs the
    script of shell_chain.txt (F117_SHELL_SCRIPT): create a pilot, orders, briefing, arming, launch -> PLAYER title-2 ends at once, VGAME
    runs 40 frames and lands, the real END debriefs (end_chain.txt), START again and Alt-Q at the quit prompt."""
    if not have_game(game):
        print('no game files')
        return 77
    scratch = scratch_game(game)
    import shell_emu
    open(os.path.join(scratch, 'ROSTER.FIL'), 'wb').write(shell_emu.make_roster())
    script = os.path.join(HERE, 'shell_chain.txt')
    env = dict(os.environ, F117_KEYS='1:31,2:03,11:39', F117_SHELL_SCRIPT=script, F117_VGAME_FRAMES='40', F117_VGAME_END='0',
               F117_END_SCRIPT=os.path.join(HERE, '..', 's58', 'end_chain.txt'))     # the real END (S5.8) needs its keys; the flight ends with a landing
    t = time.time()
    p = subprocess.run([exe, '--headless', '--trace', '--game-dir', scratch], env=env, capture_output=True, text=True, timeout=500)
    secs = time.time() - t
    out = p.stdout
    want = [r'EXEC SETUP\.EXE', r'SETUP\.EXE exit code 24h', r'EXEC MPS_LOGO\.EXE\s+tail " A"', r'MPS_LOGO\.EXE exit code 00h',
            r'EXEC PLAYER\.EXE\s+tail " title-1 title-2 title-3a credit c-pit bunker"', r'EXEC DSWAP\.EXE\s+tail " 0"',
            r'EXEC START\.EXE', r'START\.EXE exit code 0[CD]h', r'EXEC PLAYER\.EXE\s+tail " title-2"', r'EXEC VGAME\.EXE',
            r'EXEC END\.EXE', r'END\.EXE exit code 23h', r'EXEC DSWAP\.EXE\s+tail " 0"', r'EXEC START\.EXE', r'START\.EXE exit code 00h',
            r'quit: START returned 0']
    pos = 0
    for w in want:
        m = re.compile(w).search(out, pos)
        if not m:
            print('shell_chain FAILED: "%s" not found after offset %d\n%s' % (w, pos, out[-2000:]))
            print(p.stderr[-1000:])
            return 1
        pos = m.end()
    r = open(os.path.join(scratch, 'ROSTER.FIL'), 'rb').read()
    if b'kim' not in r:
        print('shell_chain FAILED: the new pilot is not in ROSTER.FIL')
        return 1
    print('shell_chain OK: SETUP -> MPS_LOGO -> PLAYER -> DSWAP -> START (new pilot, Persian Gulf strike, arming, launch) -> PLAYER title-2 -> '
          'VGAME -> END -> START -> quit, %.0f s' % secs)
    return 0


def diff(refroot, scenario, step):
    """Compares the dumps of the original (refroot/scenario/dump/pNN.scr) and of the port (refroot/scenario/portdump/pNN.scr)."""
    a = open(os.path.join(refroot, scenario, 'dump', 'p%02d.scr' % step), 'rb').read()
    b = open(os.path.join(refroot, scenario, 'portdump', 'p%02d.scr' % step), 'rb').read()
    xs, ys = [], []
    for i in range(64000):
        if a[i] != b[i]:
            xs.append(i % 320)
            ys.append(i // 320)
    print('screen: %d pixels differ' % len(xs), ('bbox x %d..%d y %d..%d' % (min(xs), max(xs), min(ys), max(ys))) if xs else '')
    for k in range(min(8, len(xs))):
        i = ys[k] * 320 + xs[k]
        print('   (%d,%d) original %d port %d' % (xs[k], ys[k], a[i], b[i]))
    d = [i for i in range(256) if a[64000 + 3 * i:64003 + 3 * i] != b[64000 + 3 * i:64003 + 3 * i]]
    print('DAC: %d entries differ' % len(d), d[:16])
    return 0


if __name__ == '__main__':
    if len(sys.argv) > 3 and sys.argv[1] == '--chain':
        sys.exit(chain(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])))
    if len(sys.argv) > 4 and sys.argv[1] == '--diff':
        sys.exit(diff(os.path.abspath(sys.argv[2]), sys.argv[3], int(sys.argv[4])))
    if len(sys.argv) > 4 and sys.argv[1] == '--one':
        sys.exit(one(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3]), sys.argv[4]))
    if len(sys.argv) > 3 and sys.argv[1] == '--dump':
        sys.exit(test(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3]), os.path.abspath(sys.argv[4]) if len(sys.argv) > 4 else None, True))
    sys.exit(test(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None))
