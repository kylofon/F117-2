"""S5.9: reference data and checks for `f117 --sound-test` (ctest `sound`).

* The ORIGINAL sound drivers (Game/ASOUND.117 and Game/ASOUND.LOG) run under Unicorn (tools/sound/drvemu.py): snd_init, snd_cmd, snd_poll and
  snd_noise (slots 100-104), the overlay's INT 8 for the logo. A scenario is a script of calls with the ISR rule applied (after every poll that
  returns > 0 the state is "fast" and snd_noise runs 4 times, a negative result ends it); the OPL writes the driver makes, tagged with the index of
  the call, and every snd_poll result are the expected log (NAME.exp). The script of calls goes to NAME.ops; the port replays it
  (`f117 --headless --sound-test DIR`) and writes NAME.out. Both must be identical.
* Scenarios: the PLAYER intro soundtrack (cmd 3), the debrief music (36-39), every single command 0-40, a randomised mix of the commands VGAME
  sends (with the engine drone values of cmd 23), the shell clicks and the debrief mix, and the logo jingle (1500 INT 8).
* The poll rates the port prints (`rate NAME polls ticks`) are checked: START 7000 of 7000 ticks, PLAYER (class 4), END and VGAME 6000 of 7000 (the 7th
  tick skips the poll: 60.08 Hz at 70.09 Hz), PLAYER on a 60 Hz display 7000, START and the logo overlay on the host clock.

usage: python port/notes/s59/sound_ref.py <f117 executable> <game dir> [work dir]
       python port/notes/s59/sound_ref.py --chain <f117 executable> <game dir>      (the no-audio run, ctest `sound_chain`)
exit 77 (ctest SKIP) when the game folder has no ASOUND.117 / ASOUND.LOG or unicorn is missing.
"""
import os, random, struct, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'sound'))


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


def lines(D, idx):
    """Drain the driver's OPL log into 'idx reg val' lines."""
    out = ['%d %02X %02X' % (idx, r, v) for t, r, v in D.log if r < 0x100]
    D.log.clear()
    return out


def gen_game(drv, schedule, polls):
    D = drv('Game/ASOUND.117')
    D.call(0, 0)                                    # snd_init(0)
    D.log.clear()
    ops, exp = ['i'], []
    idx, fast = 0, False
    for k in range(polls):
        for cmd, arg in schedule.get(k, []):
            D.call(1, cmd, arg)
            ops.append('c %d %d' % (cmd, arg))
            exp += lines(D, idx)
            idx += 1
        r = s16(D.call(3))
        ops.append('p')
        exp += lines(D, idx)
        exp.append('%d P %d' % (idx, r))
        idx += 1
        if r > 0:
            fast = True
        elif r < 0:
            fast = False
        if fast:
            for _ in range(4):
                D.call(4)
                ops.append('n')
                exp += lines(D, idx)
                idx += 1
    return ops, exp


def gen_logo(drv, ticks):
    import drvemu
    from unicorn.x86_const import UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SP, UC_X86_REG_CS
    D = drv('Game/ASOUND.LOG')
    uc = D.uc
    uc.mem_write(0x20, struct.pack('<HH', 0, drvemu.STUB))
    D.call(0, 0)
    D.log.clear()
    D.call(1, 2)
    ops, exp = ['L', 'C 2 0'], []
    exp += lines(D, 0)
    for k in range(ticks):
        isr_off, isr_seg = struct.unpack('<HH', bytes(uc.mem_read(0x20, 4)))
        uc.reg_write(UC_X86_REG_SS, drvemu.STK)
        uc.reg_write(UC_X86_REG_DS, drvemu.L)
        uc.reg_write(UC_X86_REG_ES, drvemu.L)
        sp = 0xF000 - 6
        uc.mem_write(drvemu.STK * 16 + sp, struct.pack('<HHH', 0, drvemu.STUB, 0x202))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_CS, isr_seg)
        uc.emu_start(isr_seg * 16 + isr_off, drvemu.STUB * 16, count=2_000_000)
        ops.append('I')
        exp += lines(D, k + 1)
    return ops, exp


def scenarios():
    s = []
    s.append(('cmd03', {0: [(3, 0)]}, 4300))                       # the PLAYER intro soundtrack
    for c in (36, 37, 38, 39):                                      # the debrief music
        s.append(('cmd%02d' % c, {0: [(c, 0)]}, 3000))
    for c in range(0, 41):
        if c not in (3, 36, 37, 38, 39):
            s.append(('cmd%02d' % c, {0: [(c, 0x120 if c == 23 else 0)]}, 500))
    rng = random.Random(59)
    vg = [4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 40]
    sched = {0: [(24, 0), (23, 90)]}
    for _ in range(90):
        k = rng.randrange(0, 3000)
        c = rng.choice(vg + [23, 23, 25, 24])
        sched.setdefault(k, []).append((c, rng.randrange(0, 0x340) if c == 23 else 0))
    s.append(('vgame_mix', sched, 3200))
    sched = {}
    for k in range(0, 2400, 37):
        sched.setdefault(k, []).append((rng.choice([0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0]), 0))
    s.append(('end_shell_mix', sched, 2600))
    return s


def chain(exe, game):
    """The no-audio run: the scripted front-end chain (F117_KEYS answers SETUP, a space ends the PLAYER intro, START quits by its script) with the real sound
    driver on SDL's dummy audio and video drivers and no --headless, so the audio stream is opened, the OPL chip is rendered and the poll runs from the
    tick, all silently (the launcher's Sound checkbox sets SDL_AUDIO_DRIVER=dummy). The trace must show the driver bound, the polls made and the
    normal end of the chain."""
    import re, time
    if not os.path.isfile(os.path.join(game, 'SETUP.EXE')):
        print('no game files')
        return 77
    sys.path.insert(0, os.path.join(ROOT, 'port', 'notes', 's54'))
    import shell_ref, shell_emu
    scratch = shell_ref.scratch_game(game)
    open(os.path.join(scratch, 'ROSTER.FIL'), 'wb').write(shell_emu.make_roster())
    env = dict(os.environ, F117_KEYS='1:31,2:03,11:39,22:01', F117_SHELL_SCRIPT=os.path.join(ROOT, 'port', 'notes', 's54', 'shell_quit.txt'),
               SDL_AUDIO_DRIVER='dummy', SDL_VIDEO_DRIVER='dummy')
    t = time.time()
    p = subprocess.run([exe, '--trace', '--game-dir', scratch], env=env, capture_output=True, text=True, timeout=120)
    secs = time.time() - t
    out = p.stdout
    m = re.search(r'sound: (\d+) polls, shutdown', out)
    ok = p.returncode == 0 and 'sound: ASOUND.117 bound' in out and m and int(m.group(1)) > 500 and 'quit: START returned 0' in out
    if not ok or 'audio unavailable' in p.stderr:
        print('sound_chain FAILED: exit %d, %.1f s' % (p.returncode, secs))
        print(out[-1200:])
        print(p.stderr[-600:])
        return 1
    print('sound_chain OK: the chain ran with SDL_AUDIO_DRIVER=dummy, the driver was polled %s times, %.1f s' % (m.group(1), secs))
    return 0


def main():
    if len(sys.argv) > 3 and sys.argv[1] == '--chain':
        return chain(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3]))
    args = [a for a in sys.argv[1:]]
    exe, game = os.path.abspath(args[0]), os.path.abspath(args[1])
    work = os.path.abspath(args[2]) if len(args) > 2 else os.path.join(os.path.dirname(exe), 'sound_ref')
    if not all(os.path.exists(os.path.join(game, n)) for n in ('ASOUND.117', 'ASOUND.LOG')):
        print('sound_ref: no ASOUND.117 / ASOUND.LOG in', game)
        return 77
    try:
        import unicorn  # noqa: F401
        import drvemu
    except ImportError as e:
        print('sound_ref: unicorn missing:', e)
        return 77
    os.makedirs(work, exist_ok=True)

    def drv(p):
        return drvemu.Drv(os.path.join(game, os.path.basename(p)))

    names, exps = [], {}
    for name, sched, polls in scenarios():
        ops, exp = gen_game(drv, sched, polls)
        names.append(name)
        exps[name] = exp
        open(os.path.join(work, name + '.ops'), 'w').write('\n'.join(ops) + '\n')
        open(os.path.join(work, name + '.exp'), 'w').write('\n'.join(exp) + '\n')
    ops, exp = gen_logo(drv, 1500)
    names.append('logo')
    exps['logo'] = exp
    open(os.path.join(work, 'logo.ops'), 'w').write('\n'.join(ops) + '\n')
    open(os.path.join(work, 'logo.exp'), 'w').write('\n'.join(exp) + '\n')
    open(os.path.join(work, 'list.txt'), 'w').write('\n'.join(names) + '\n')

    r = subprocess.run([exe, '--headless', '--sound-test', work, '--game-dir', game], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode != 0:
        print('sound_ref: f117 --sound-test exit', r.returncode)
        return r.returncode
    bad = 0
    total = 0
    for name in names:
        got = open(os.path.join(work, name + '.out')).read().split('\n')
        got = [g for g in got if g and g.split()[1] != '04']       # the harness hides register 4 (the AdLib timer test)
        exp = exps[name]
        total += len(exp)
        if got != exp:
            bad += 1
            i = next((k for k in range(min(len(got), len(exp))) if got[k] != exp[k]), min(len(got), len(exp)))
            print('sound_ref: %s DIFFERS at line %d: port %r  original %r  (%d vs %d lines)' % (
                name, i, got[i:i + 1], exp[i:i + 1], len(got), len(exp)))
    print('sound_ref: %d scenarios, %d lines compared, %d differences' % (len(names), total, bad))
    # the poll rates
    rates = {}
    for ln in r.stdout.split('\n'):
        p = ln.split()
        if len(p) == 6 and p[0] == 'rate':
            rates[p[1]] = (int(p[3]), int(p[5]))
    hz = 359200 / 25175000.0
    hz = 1.0 / hz                                           # 70.0866 ticks per second
    fails = 0
    for name, want in (('START', 7000), ('PLAYER', 6000), ('PLAYER60', 7000), ('END', 6000), ('VGAME', 6000)):
        polls, ticks = rates.get(name, (-1, 0))
        ok = polls == want and ticks == 7000
        fails += not ok
        print('rate %-9s %5d polls in %d ticks = %.2f polls/s at %.2f ticks/s %s' % (name, polls, ticks, polls / ticks * hz if ticks else 0, hz, 'ok' if ok else 'WRONG (want %d)' % want))
    polls, ticks = rates.get('START_live', (0, 0))
    ok = 128 <= ticks <= 144 and abs(polls - ticks) <= 1
    fails += not ok
    print('rate START_live: %d polls in %d ticks over 2 s (%.1f polls/s) %s' % (polls, ticks, polls / 2.0, 'ok' if ok else 'WRONG'))
    polls, ticks = rates.get('LOGO_live', (0, 0))
    ok = 117 <= polls <= 126
    fails += not ok
    print('rate LOGO_live: %d overlay INT 8 in 2 s (%.1f Hz, want 60.88) %s' % (polls, polls / 2.0, 'ok' if ok else 'WRONG'))
    return 1 if (bad or fails) else 0


if __name__ == '__main__':
    sys.exit(main())
