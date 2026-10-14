"""S5.5: scenario families of the sim step 39C4, the key interpreter cba0 and the small per-frame pieces (see sim_scn.py).
Snapshot s1 = the machine after the first step of the ORIGINAL (mission initialisation), so these start from a running mission."""
import struct

from sim_scn import SCRATCH

# the keys of cba0 table A, the nav keys, the training keys, and a few that mean nothing
KEYS = [0x0231, 0x0332, 0x0433, 0x0534, 0x0635, 0x0736, 0x0837, 0x0938, 0x0A39, 0x0E08, 0x1C0D, 0x1100, 0x1400, 0x1A7B, 0x1B7D, 0x1E00, 0x2000, 0x2100, 0x2300,
        0x2C5A, 0x2C7A, 0x2D58, 0x2D78, 0x2E00, 0x2F00, 0x2F76, 0x3062, 0x316E, 0x326D, 0x332C, 0x342E, 0x352F, 0x324D, 0x333C, 0x343E, 0x353F, 0x3920, 0x3B00, 0x3C00,
        0x3D00, 0x3E00, 0x3F00, 0x4000, 0x4100, 0x4200, 0x4300, 0x4400, 0x5200, 0x5400, 0x5500, 0x5600, 0x5700, 0x5800, 0x5900, 0x5B00, 0x5D00,
        0x1300, 0x1700, 0x2500, 0x2400, 0x2600, 0x3100,
        0x4700, 0x4F00, 0x4800, 0x4838, 0x5000, 0x5032, 0x4B00, 0x4B34, 0x4D00, 0x4D36, 0x4900, 0x4939, 0x5100, 0x5133,
        0x1234, 0x011B, 0x7777]


def state_pokes(rng):
    """a random but plausible cockpit / flight state around the running mission"""
    pos = lambda: rng.randint(0x1000, 0x6000)
    px, py = pos(), pos()
    return ['poke w 0x365E 1', 'poke b 0x4088 1',
            'poke l 0xBA10 %d' % (px << 5), 'poke l 0xC02A %d' % ((0x8000 - py) << 5), 'poke w 0xC0A4 %d' % px, 'poke w 0xC0B2 %d' % py,
            'poke w 0x4084 %d' % (px + rng.randint(-2000, 2000)), 'poke w 0x4086 %d' % (py + rng.randint(-2000, 2000)), 'poke w 0x2DEE %d' % rng.randint(0, 65535), 'poke w 0x2DF2 %d' % rng.randint(0, 65535),
            'poke w 0x2DF0 %d' % rng.randint(-3000, 3000), 'poke w 0x2DF4 %d' % rng.choice([0, 0x80, 300, 4000]), 'poke w 0x2DF6 %d' % rng.randint(0, 5000),
            'poke w 0xB052 %d' % rng.randint(0, 30000), 'poke w 0xB168 %d' % rng.randint(0, 600), 'poke w 0x2DFE %d' % rng.randint(0, 100),
            'poke w 0x9B04 %d' % rng.choice([0, 1, 4, 0x10, 0x20, 0x31, 0x2007]), 'poke b 0x9B05 %d' % rng.choice([0, 1, 2, 4, 0x10, 0x14, 0x30]),
            'poke w 0x3632 %d' % rng.choice([0, 0, 0x08, 0x10, 0x40, 0x58]), 'poke w 0xDFDC %d' % rng.choice([0x13, 0x14, 0x15, 0x18]),
            'poke w 0xDEDA %d' % rng.randint(0, 1), 'poke w 0x3D60 %d' % rng.randint(0, 0xFFF), 'poke w 0x4080 %d' % rng.randint(2, 4),
            'poke w 0x3660 %d' % rng.randint(4, 15), 'poke w 0x3D7A %d' % rng.randint(1, 2), 'poke w 0xE2FE %d' % rng.randint(0, 2)]


def fam_small(T, rng):
    ops = ['poke w 0x365E 1']
    for cmd, prio in ((5, 2), (0x14, 2), (0xF, 1), (4, 0), (7, 3)):
        for lvl in (0, 1, 2):
            for ej in (0, 1):
                ops += ['poke w 0x43C2 %d' % lvl, 'poke w 0xC06E %d' % ej, 'call snd_cue %d %d' % (cmd, prio)]
    T.add('snd', 's1', ops)
    ops = []
    for rate in (4, 7, 15, 16, 20, 40, 100):
        for mode in (1, 2):
            ops += ['poke w 0x3660 %d' % rate, 'poke w 0x3D7A %d' % mode, 'call frame_params_update', 'call frame_rate_halve']
    T.add('frameparams', 's1', ops)
    ops = []
    for d in (0, 1, 2, 3):
        ops += ['poke w 0x98DE %d' % d, 'poke w 0x48B0 %d' % rng.randint(0, 9000), 'poke w 0x48AE %d' % rng.randint(0, 9000), 'call view_range_setup']
    T.add('viewrange', 's1', ops)
    for n in range(6):
        ops = state_pokes(rng) + ['poke w 0xDFE0 %d' % rng.randint(0, 5), 'poke w 0x98C6 %d' % rng.randint(-500, 500)]
        for _ in range(4):
            ops += ['poke w 0x2DF4 %d' % rng.choice([0, 0x80, 500, 8000]), 'poke w 0xC0B2 %d' % rng.randint(0x1000, 0x6000), 'call hud_glideslope_update']
        T.add('glide%d' % n, 's1', ops)
    for n in range(4):
        ops = state_pokes(rng) + ['poke w 0x3D68 %d' % rng.choice([0xFFFF, 0, 1, 2, 3])]
        for k in range(20):
            ops += ['poke w 0x3D60 %d' % (8 * k + rng.randint(0, 1)), 'call fx_smoke_update']
        ops += ['poke w 0xC602 %d' % rng.randint(-5, 500), 'poke w 0xB756 %d' % rng.randint(-30, 10), 'call fx_fall_update', 'call fx_fall_update', 'call fx_fall_update']
        T.add('smoke%d' % n, 's1', ops)
    T.add('cheat', 's1', ['poke w 0x365E 1', 'poke w 0x363E 3', 'poke w 0x3642 20', 'poke w 0x3646 0', 'call mission_cheat_rearm', 'call cockpit_widgets_reset'])
    T.add('names', 's1', ['call mission_name_targets'])
    T.add('names0', 's0', ['call mission_name_targets'])
    for n in range(3):
        T.add('events%d' % n, 's1', ['poke w 0x98E2 %d' % rng.randint(0, 3000), 'poke w 0xC0A4 %d' % rng.randint(0x1000, 0x6000), 'poke w 0xC0B2 %d' % rng.randint(0x1000, 0x6000)] +
              ['call mission_log_event %d %d' % (rng.randint(0, 255), rng.randint(0, 255)) for _ in range(5)] + ['call mission_end %d' % rng.choice([0, 1, 2, 5, 6, 7]),
                                                                                                             'poke w 0xC06E 1', 'call mission_end %d' % rng.choice([0, 1, 7]), 'call mission_world_export'])
    T.add('log_full', 's1', ['poke w 0x94EA 253'] + ['call mission_log_event %d %d' % (i, i) for i in range(1, 6)])
    ops = []
    for t in range(0, 400, 7):
        ops += ['poke w 0x98E2 %d' % t, 'poke w 0x3658 %d' % (t & 1), 'poke w 0x43AE 0', 'poke w 0x365E %d' % ((t >> 1) & 1), 'call pal_time_of_day_step']
    T.add('todstep', 's1', ops)
    T.add('resetclock', 's1', ['poke w 0x3658 1', 'poke b 0xE2E0 5', 'poke b 0xE2F2 9', 'call sim_reset_clock_palette'])
    T.add('f4', 's1', ['poke w 0xDE90 0x3E00', 'call sim_key_and_signature'])
    T.add('f4d', 's1', ['poke w 0x43B0 1', 'call cockpit_daynight_recolor'])
    T.add('f4e', 's1', ['poke w 0x43B0 1', 'call hud_color_init'])
    T.add('f4b', 's1', ['call input_command_key 0x3E00'])
    T.add('f4c', 's1', ['poke w 0x365E 1', 'call input_command_key 0x3E00'])
    T.add('daynight', 's1', ['poke w 0x43B0 1', 'call cockpit_daynight_recolor', 'poke w 0x43B0 0', 'call cockpit_daynight_recolor'])
    T.add('sig', 's1', state_pokes(rng) + ['call sim_key_and_signature', 'poke w 0x3654 3', 'call sim_key_and_signature', 'poke w 0xDE90 0x3E00', 'call sim_key_and_signature'])


def fam_keys(T, rng):
    for n in range(40):
        ops = state_pokes(rng)
        for _ in range(8):
            ops += ['poke w 0xDE90 %d' % rng.choice(KEYS)]
            if rng.random() < 0.3:
                ops += ['poke b 0x9B05 %d' % rng.choice([0x10, 0x14, 0x30, 0x12])]
            ops += ['call input_command_key %d' % rng.choice(KEYS)]
        T.add('keys%d' % n, 's1', ops)
    for k in KEYS:                                     # each key once from the plain state
        T.add('key_%04X' % k, 's1', ['poke w 0x365E 1', 'poke b 0x4088 1', 'poke b 0x9B05 0x10', 'poke w 0xDFDC 0x14', 'poke w 0xB80C 0', 'call input_command_key %d' % k])


def fam_step(T, rng):
    T.add('step_init', 's0', ['poke w 0xC0AE 1', 'poke w 0xBA10 20000', 'poke w 0xC02A 30000', 'call sim_frame_step'])
    T.add('step_init2', 's0', ['poke w 0xC0AE 1', 'poke w 0x3D7C 1', 'call sim_frame_step', 'call sim_frame_step', 'call sim_frame_step'])
    for n in range(40):
        ops = state_pokes(rng) + ['poke w 0xDFE0 %d' % rng.randint(0, 5), 'poke w 0x3D6A %d' % rng.randint(-1, 2), 'poke w 0x3D68 %d' % rng.choice([0xFFFF, 1])]
        for step in range(rng.randint(1, 6)):
            ops += ['poke w 0xDE90 %d' % rng.choice([0] * 4 + KEYS[:20]), 'poke b 0x9F66 0', 'call sim_frame_step']
        T.add('step%d' % n, 's1', ops)
    for n in range(12):                                                  # near airfields, zones and the estimator
        ops = state_pokes(rng)
        site = 0xB2A2 + 16 * rng.randint(0, 3)
        ops += ['poke w 0xDFE0 0', 'poke w 0x3D60 %d' % (rng.randint(0, 100) * 128), 'poke w 0x3D78 %d' % rng.choice([0, 30, 60, 200]), 'poke w 0x2646 %d' % rng.randint(0, 4000)]
        ops += ['poke w 0x2DF4 %d' % rng.choice([0, 0x80]), 'poke w 0xB168 %d' % rng.randint(0, 80)]
        for step in range(rng.randint(2, 20)):
            ops += ['call sim_frame_step']
        T.add('stepzone%d' % n, 's1', ops)


def fam_frames(T, rng):
    """the frame loop body (1BBC) a few times in a row, with keys, ending the mission and exporting the world: the screen pages, the HUD and the MFD state, the comm block and the event log"""
    for n in range(8):
        ops = state_pokes(rng) + ['poke w 0xDFE0 %d' % rng.randint(0, 5), 'poke w 0xC07A %d' % rng.choice([0, 0, 0, 0x42, 0x85]), 'poke w 0x2E48 0', 'poke w 0x3D6A %d' % rng.randint(-1, 2)]
        for frame in range(rng.randint(3, 6)):
            ops += ['call view_frame', 'call hud_overlay_frame', 'call hud_draw_front', 'call hud_side_strips 1', 'call cockpit_draw_adi',
                    'poke w 0xDE90 %d' % rng.choice([0] * 3 + KEYS[:30]), 'call sim_frame_step']
        ops += ['call mission_end %d' % rng.choice([0, 1, 7]), 'call mission_world_export']
        T.add('frames%d' % n, 's1', ops)
