"""S5.6b: golden frames of the whole 3D renderer (ctest `render3d_golden`).

  python r3d_golden.py <f117.exe> <Game dir> <work dir> [--verbose] [--only ID] [--fresh]

The ORIGINAL VGAME.EXE runs under Unicorn from the start-up of a real mission (port/notes/s55/vgame_emu.py, the comm block of the `shell` test or
$F117_COMM), one sim step on top (s1 = a running mission); then the scenes below call the real `r3d_scene_draw` (0000:03CE: set-up, sky, terrain, the
world pass 9ACA, the sorted queue, the object renderer 11EB, the overlay pass), `view_frame` (2F82, which calls 03CE), the TV camera (bf71) and the map
MFD terrain (119C) with the same DGROUP on both sides. The port runs them with `f117 --render-test` (sim/simtest.c: r3d_scene_draw is not skipped).
Compared after every scenario: DGROUP up to the end of the BSS, the three pages row by row (CRC), the DAC and the comm block (sim_ref.compare_rec).
Only the S5.7 / S5.9 stubs (flight, weapons, AI, damage, sound) are hooked to return 0 on both sides, as in the sim tests.
The references are cached in <work dir> (stamp = the scripts, VGAME.EXE, the comm block); --fresh rebuilds them. Exit 77 = no game files.
"""
import hashlib, os, struct, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 's55'))
import sim_ref
import vgame_emu as V

ROOT = V.ROOT
MASK = sim_ref.CALL_MASK + [(0x5EB6, 4)]           # 11EB:0281 saves IVT[0] (the port does a checked division instead, PORT)


def w(v):
    return v & 0xFFFF


def scene(yaw, pitch, roll, x, y, z, h=0x5B):
    """call r3d_scene_draw(-heading, pitch, roll, x32, y32, z32, 0, 0, 320, h)"""
    return 'call r3d_scene_draw %d %d %d %d %d %d %d %d %d 0 0 320 %d' % (w(-yaw), w(pitch), w(roll), x & 0xFFFF, (x >> 16) & 0xFFFF, y & 0xFFFF, (y >> 16) & 0xFFFF,
                                                                          z & 0xFFFF, (z >> 16) & 0xFFFF, h)


def scenarios(T, st):
    """st: values read from s1. Scene list (camera = x32 east, y32 as C02A (north-inverted), altitude)."""
    base = ['poke b 0x262A 0']
    px, py = st['BA10'], st['C02A']
    sites = st['sites']
    # (a) on the runway, the four main headings and the octants, cockpit (365E = 1, h 0x5B) and full screen (365E = 0, h 200)
    for k in range(8):
        T.add('runway%d' % k, 's1', base + [scene(k * 0x2000, 0, 0, px, py, 0x18)])
    T.add('runway_full', 's1', base + ['poke w 0x365E 0', scene(0, 0, 0, px, py, 0x18, 200)])
    T.add('runway_wide', 's1', base + ['poke b 0x294B 1', scene(0x4000, 0, 0, px, py, 0x18)])
    # (b) low over the terrain, every detail level (path A / B of 04DA, the two-colour sky below 3), pitched and rolled
    for dl in range(4):
        for k, (hd, pt, rl) in enumerate(((0x1000, -0x600, 0), (0x9000, -0x300, 0x1800), (0x5000, -0x2800, 0))):
            T.add('low_d%d_%d' % (dl, k), 's1', base + ['poke w 0x98DE %d' % dl, scene(hd, pt, rl, px - 0x3000 * k, py + 0x2000 * k, 300)])
    T.add('lookdown', 's1', base + [scene(0x2345, -0x3800, 0, px, py - 0x4000, 900)])
    # (c) high
    for k in range(3):
        T.add('high%d' % k, 's1', base + [scene(0x3000 + 0x5000 * k, -0x1800, 0x400 * k, px + 0x8000 * k, py, 8000 + 6000 * k)])
    # (d) looking at targets (sites), (e) a polygon crossing the screen edge / very close, (f) up against the models
    for n, i in enumerate(sites[:10]):
        sx, sy = i[0] << 5, 0x100000 - (i[1] << 5)
        T.add('target%d' % n, 's1', base + [scene(0, -0x300, 0, sx, sy + 0x900, 150)])
        T.add('close%d' % n, 's1', base + [scene(0x0800, -0x900, 0x1400, sx - 0x60, sy + 0x140, 40)])
        T.add('inside%d' % n, 's1', base + [scene(0xC000, 0x200, 0, sx + 0x20, sy + 0x30, 12)])
    # (g) external views: own aircraft (DF08's 0x18 parts, gear, shadow 0x11)
    for k, mode in enumerate((0x87, 0x86, 0x8B)):
        cam = ['poke w 0xC07A %d' % mode, 'pokes 0xB288 %s' % struct.pack('<i', px).hex(), 'pokes 0xB752 %s' % struct.pack('<i', py - 0x180).hex(), 'poke w 0xB75C 0x40']
        T.add('ext%d' % k, 's1', base + cam + ['poke w 0xC072 %d' % (k + 1), scene(0, -0x400, 0, px, py - 0x180, 0x40)])
    # (g2) smoke puffs (9ACA block B: DCC0 billboards -> DF08 -> 1AD6 sprites of every size) in front of the player, a whole view_frame
    for k in range(3):
        sg = -1
        puffs = ['pokes 0x%X %s' % (0x39DA + 8 * j, struct.pack('<HHhH', w(st['C0A4'] + 2 * j - 8), w(st['C0B2'] + sg * (0x8 + 4 * j + 12 * k)), 20 + 10 * j, (j + k) & 7).hex()) for j in range(8)]
        T.add('smoke%d' % k, 's1', base + puffs + ['poke w 0x2DEE 0', 'poke w 0x2DF0 0', 'poke w 0x2DF2 0', 'poke w 0xC07A 0', 'call view_frame'])
    # (h) whole frames: view_frame (2F82 -> 03CE) after sim steps; the world pass moves
    T.add('frames', 's1', base + ['call view_frame', 'call sim_frame_step', 'poke b 0x262A 0', 'call view_frame', 'call sim_frame_step', 'poke b 0x262A 0', 'call view_frame'])
    # (i) the map MFD terrain (119C: ortho map mode) at every zoom
    for z in range(2, 10):
        T.add('map%d' % z, 's1', base + ['call map_render_terrain 0x4014 %d %d %d' % (w(st['C0A4'] // 2), w(0x4000 - st['C0B2'] // 2), 9 - z)])
    # (j) the TV camera (bf71) on sites and units
    for n, i in enumerate(sites[:6]):
        T.add('tv%d' % n, 's1', base + ['call mfd_view_render %d %d %d 0 0 0 0 %d 0' % (i[2], i[0], i[1], (1, 3, 4)[n % 3])])
    # (k) the scenery collision probe (0FFB through 77D9)
    for n, i in enumerate(sites[:6]):
        for k, (dx, dy) in enumerate(((0, 0), (8, 3), (-5, 12))):
            T.add('probe%d_%d' % (n, k), 's1', ['call terr_probe_cell %d %d %d' % (i[0] + dx, i[1] + dy, 8 * n)])
    # (l) units (9ACA block D: shadow model 4, the unit model near / far, the 1-pixel far case): the player (BA10 / C02A / C0A4 / C0B2) moved behind each unit
    for n, (ux, uy, uz) in enumerate(st['units'][:6]):
        for k, back in enumerate((0x18, 0x60, 0x600)):
            cy = 0x100000 - ((uy - back) << 5)
            pos = ['poke w 0xC0A4 %d' % ux, 'poke w 0xC0B2 %d' % w(uy - back), 'pokes 0xBA10 %s' % struct.pack('<i', ux << 5).hex(),
                   'pokes 0xC02A %s' % struct.pack('<i', cy).hex(), 'poke w 0x2DF4 %d' % w(uz + 0x20)]
            pose = ['poke w 0x2DEE 0', 'poke w 0x2DF0 %d' % w(-0x200), 'poke w 0x2DF2 0', 'poke w 0xC07A 0']
            T.add('unit%d_%d' % (n, k), 's1', base + pos + pose + ['call view_frame'])


def stamp(comm):
    h = hashlib.sha1()
    for f in [os.path.abspath(__file__), os.path.join(HERE, '..', 's55', 'vgame_emu.py'), os.path.join(HERE, '..', 's55', 'sim_ref.py'),
              os.path.join(ROOT, 'work', 'unpacked', 'VGAME.EXE'), comm]:
        h.update(open(f, 'rb').read())
    return h.hexdigest()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    verbose, fresh = '--verbose' in sys.argv, '--fresh' in sys.argv
    only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
    if only:
        args.remove(only)
    if len(args) < 3:
        print(__doc__)
        return 2
    exe, game, work = [os.path.abspath(a) for a in args[:3]]
    comm = sim_ref.comm_path()
    if not os.path.exists(os.path.join(game, 'VGAME.EXE')) or not os.path.exists(os.path.join(ROOT, 'work', 'unpacked', 'VGAME.EXE')) or not comm:
        print('render3d golden: no game files (or no comm block of the shell test)')
        return 77
    os.makedirs(work, exist_ok=True)
    t0 = time.time()
    m = sim_ref.emu_machine(game, os.path.join(work, 'emu'), comm)
    assert m.run_to(0, 0x0233), 'VGAME did not reach sim_run'
    syms = sim_ref.symbols()
    for seg, off, name in sim_ref.stub_list():
        if name != 'r3d_scene_draw':                    # the TESTSTUB of the sim tests: here 03CE runs in full
            m.hook_stub(seg, off, seg != 0)
    T = sim_ref.CallTests(exe, game, work, verbose)
    m.wb(m.ds, 0xC0AE, struct.pack('<H', 1))
    m.call(0, syms['sim_frame_step'][1], [])
    sim_ref.write_snapshot(m, work, 's1')
    T.snaps['s1'] = m.snapshot()
    rd = lambda o, f='<h', n=2: struct.unpack(f, m.dg(o, n))[0]
    st = {'BA10': rd(0xBA10, '<i', 4), 'C02A': rd(0xC02A, '<i', 4), 'C0A4': rd(0xC0A4, '<H'), 'C0B2': rd(0xC0B2, '<H'), 'sites': [], 'units': []}
    for i in range(rd(0xC0B4)):
        s = 0xB2A4 + 16 * i
        x, y, shape = rd(s, '<H'), rd(s + 2, '<H'), rd(s + 14, '<H')
        if x and y:
            st['sites'].append((x, y, shape))
    for i in range(rd(0xDED0)):
        u = 0xC140 + 0x24 * i
        if m.dg(u + 0x16, 1)[0] & 2:
            st['units'].append((rd(u, '<H'), rd(u + 2, '<H'), rd(u + 4)))
    scenarios(T, st)
    if only:
        T.scns = [s for s in T.scns if s[0] == only]
    with open(os.path.join(work, 'scn.txt'), 'w') as f:
        for sid, snap, ops in T.scns:
            f.write('scn %s %s\n' % (sid, snap))
            for op in ops:
                f.write(' '.join(op) + '\n')
            f.write('end\n')
    key = stamp(comm) + ('' if not only else only)
    ref_file, stamp_file = os.path.join(work, 'refs.bin'), os.path.join(work, 'stamp.txt')
    if not fresh and os.path.exists(stamp_file) and open(stamp_file).read().strip() == key and os.path.exists(ref_file):
        raw = open(ref_file, 'rb').read()
        refs = [(raw[i * sim_ref.REC_SIZE:(i + 1) * sim_ref.REC_SIZE], None) for i in range(len(T.scns))]
        src = 'cached'
    else:
        refs = sim_ref.run_emu_scenarios(m, T.snaps, T.scns, syms)
        open(ref_file, 'wb').write(b''.join(r[0] for r in refs))
        open(stamp_file, 'w').write(key + '\n')
        src = 'ORIGINAL under Unicorn, %.0f s' % (time.time() - t0)
    print('render3d golden: %d scenarios, references: %s' % (len(T.scns), src))
    sys.stdout.flush()
    r = subprocess.run([exe, '--render-test', work, '--game-dir', game], capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        print('render3d golden: the port failed:', r.stdout[-600:], r.stderr[-600:])
        return 1
    raw = open(os.path.join(work, 'scn_port.bin'), 'rb').read()
    if len(raw) != sim_ref.REC_SIZE * len(T.scns):
        print('render3d golden: %d bytes from the port, expected %d' % (len(raw), sim_ref.REC_SIZE * len(T.scns)))
        return 1
    bad = 0
    for i, (sid, snap, ops) in enumerate(T.scns):
        probs = sim_ref.compare_rec(sid, refs[i], raw[i * sim_ref.REC_SIZE:(i + 1) * sim_ref.REC_SIZE], MASK, verbose)
        if probs:
            bad += 1
            if bad <= 30:
                print('  scenario %s: %s' % (sid, '; '.join(p for p in probs)[:600]))
    print('render3d golden: %d scenarios, %d differ%s' % (len(T.scns), bad, '' if bad else ' (pages, DGROUP, DAC, comm byte for byte)'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
