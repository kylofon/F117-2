"""S5.3: reference data for `f117 --frontend-test` (ctest `frontend`).

* SETUP: the ORIGINAL SETUP.EXE (work/unpacked, from tools/unlzexe.py) runs under Unicorn on the DOS/BIOS machine of dos_emu.py with the
  answers 'N' (no joystick) and '2' (AdLib); the 80 x 25 text screen (char, attribute) at the first and second INT 21h/01 and at exit,
  and the comm block it fills, are written to setup1.bin, setup2.bin, setup3.bin and setup_comm.bin.
* MPS_LOGO: the ORIGINAL MPS_LOGO.EXE with driver letter N (no sound) draws its 82 frames; after every ss_draw the CRC32 of the 64000 screen
  bytes (A000:0) and of the 768 DAC bytes are written to logo.bin (u32 pairs). The BIOS tick is bumped on every read so nothing waits.
* PLAYER: the Phase 2 decoders (tools/panlib.py): per .PAN speed, cel count, the 6-bit palette and the CRC32 of the screen after each
  cel -> NAME.pn3 (same data as S5.2's decoder_ref, kept separate because the test needs the 6-bit palette for the fade check).

usage: python port/notes/s53/frontend_ref.py <f117 executable> <game dir> [ref dir]
       python port/notes/s53/frontend_ref.py --chain <f117 executable> <game dir>      (the scripted run, ctest `frontend_chain`)
exit 77 (ctest SKIP) when the game folder has no SETUP.EXE or work/unpacked is missing.
"""
import os, struct, subprocess, sys, zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.dirname(__file__))

COMM_SEG = 0x5000


def setup_ref(game, ref):
    from dos_emu import Machine
    m = Machine('SETUP', game=game, keys=[ord('N'), ord('2')])
    m.uc.mem_write(0x4F0, struct.pack('<H', COMM_SEG))
    m.uc.mem_write(COMM_SEG * 16, bytes(5120))

    def snap(mach, n):
        mach.snaps.append(bytes(mach.text))
    m.on_getche = snap
    code = m.run()
    assert code == 0x24, 'SETUP exit code %r (unhandled interrupts: %r)' % (code, m.int_log[:5])
    assert len(m.snaps) == 2, len(m.snaps)
    for i, s in enumerate(m.snaps):
        open(os.path.join(ref, 'setup%d.bin' % (i + 1)), 'wb').write(s)
    open(os.path.join(ref, 'setup3.bin'), 'wb').write(bytes(m.text))
    open(os.path.join(ref, 'setup_comm.bin'), 'wb').write(m.rb(COMM_SEG, 0, 0x80))
    return m


def logo_ref(game, ref):
    from dos_emu import Machine, A000
    m = Machine('MPS_LOGO', game=game, tail=b' N')
    env = 0x0700
    m.uc.mem_write(env * 16, b'\0\0\1\0C:\\MPS_LOGO.EXE\0')
    m.uc.mem_write(0x0800 * 16 + 0x2C, struct.pack('<H', env))
    m.uc.mem_write(0x46C, struct.pack('<I', 0))

    def tick(mach):                                         # 0045:0062 bios_ticks: the clock runs ahead, nothing waits
        v = struct.unpack('<I', mach.rb(0, 0x46C, 4))[0] + 1
        mach.wb(0, 0x46C, struct.pack('<I', v))
    m.hook_call(0x45, 0x62, tick)
    frames = []

    def after_draw(mach):                                   # 0006:02A5, right after ss_draw returned
        frames.append((zlib.crc32(mach.rb(A000, 0, 64000)) & 0xFFFFFFFF, zlib.crc32(bytes(mach.dac)) & 0xFFFFFFFF))
    m.hook_call(0x06, 0x2A5, after_draw)
    code = m.run()
    assert code == 0, 'MPS_LOGO exit code %r (unhandled: %r)' % (code, m.int_log[:5])
    assert len(frames) == 82, len(frames)
    open(os.path.join(ref, 'logo.bin'), 'wb').write(b''.join(struct.pack('<II', a, b) for a, b in frames))


def pan_ref(game, ref):
    import panlib
    n = 0
    for f in sorted(os.listdir(game), key=str.upper):
        if not f.upper().endswith('.PAN'):
            continue
        pan = panlib.load_pan(os.path.join(game, f))
        pal6 = bytes(v >> 2 for v in pan['pal'])
        rec = struct.pack('<HH', pan['speed'], pan['n']) + pal6
        for i, scr in panlib.frames(pan):
            rec += struct.pack('<I', zlib.crc32(scr) & 0xFFFFFFFF)
        open(os.path.join(ref, os.path.splitext(f)[0].upper() + '.pn3'), 'wb').write(rec)
        n += 1
    return n


def chain(exe, game):
    """The scripted run: F117_KEYS answers SETUP ('N' = no joystick at 1 s, '2' = AdLib at 2 s), the logo plays for its 7.7 s, a space at 11 s
    ends the PLAYER intro; START (S5.4) is quit at once by its script (Alt-Q, y: port/notes/s54/shell_quit.txt) on a scratch game folder.
    The trace has to show SETUP, MPS_LOGO, PLAYER, DSWAP and START in order."""
    import re, time
    if not os.path.isfile(os.path.join(game, 'SETUP.EXE')):
        print('no game files')
        return 77
    sys.path.insert(0, os.path.join(ROOT, 'port', 'notes', 's54'))
    import shell_ref, shell_emu
    scratch = shell_ref.scratch_game(game)
    open(os.path.join(scratch, 'ROSTER.FIL'), 'wb').write(shell_emu.make_roster())
    env = dict(os.environ, F117_KEYS='1:31,2:03,11:39', F117_SHELL_SCRIPT=os.path.join(ROOT, 'port', 'notes', 's54', 'shell_quit.txt'))
    t = time.time()
    p = subprocess.run([exe, '--headless', '--trace', '--game-dir', scratch], env=env, capture_output=True, text=True, timeout=120)
    secs = time.time() - t
    out = p.stdout
    want = [r'EXEC SETUP\.EXE', r'SETUP\.EXE exit code 24h', r'EXEC MPS_LOGO\.EXE\s+tail " A"', r'MPS_LOGO\.EXE exit code 00h',
            r'EXEC PLAYER\.EXE\s+tail " title-1 title-2 title-3a credit c-pit bunker"', r'PLAYER\.EXE exit code 20h', r'EXEC DSWAP\.EXE\s+tail " 0"',
            r'EXEC START\.EXE', r'START\.EXE exit code 00h', r'quit: START returned 0']
    pos = 0
    for w in want:
        m = re.compile(w).search(out, pos)
        if not m:
            print('frontend_chain FAILED: "%s" not found after offset %d\n%s' % (w, pos, out[-1500:]))
            return 1
        pos = m.end()
    if p.returncode != 0 or secs < 9:
        print('frontend_chain FAILED: exit code %d after %.1f s' % (p.returncode, secs))
        return 1
    print('frontend_chain OK: SETUP -> MPS_LOGO -> PLAYER -> DSWAP -> START (quit by its script), %.1f s' % secs)
    return 0


def main():
    if len(sys.argv) > 3 and sys.argv[1] == '--chain':
        return chain(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3]))
    exe, game = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    ref = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else os.path.join(ROOT, 'work', 's53', 'ref')
    if not os.path.isfile(os.path.join(game, 'SETUP.EXE')) or not os.path.isfile(os.path.join(ROOT, 'work', 'unpacked', 'SETUP.EXE')):
        print('no game files (or work/unpacked) for the front-end references')
        return 77
    os.makedirs(ref, exist_ok=True)
    setup_ref(game, ref)
    logo_ref(game, ref)
    n = pan_ref(game, ref)
    print('frontend_ref: SETUP screens, 82 logo frames, %d PAN files -> %s' % (n, ref))
    sys.stdout.flush()
    return subprocess.call([exe, '--headless', '--frontend-test', ref, '--game-dir', game])


if __name__ == '__main__':
    sys.exit(main())
