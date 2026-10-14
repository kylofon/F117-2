"""S5.5: scenario families of the HUD overlay (7898), the scope (8C10) and later the asm HUD, the ADI and the view (see sim_scn.py)."""
import math
import struct

from sim_scn import SCRATCH, hexstr


def hud_state(rng, style=None, level=None, view=None):
    pos = lambda: rng.randint(0x1000, 0x6000)
    px, py = pos(), pos()
    ops = ['poke w 0x365E %d' % (1 if (view in (None, 0)) else rng.randint(0, 1)), 'poke b 0x4088 1',
           'poke w 0xC0A4 %d' % px, 'poke w 0xC0B2 %d' % py, 'poke w 0x2DEE %d' % rng.randint(0, 65535), 'poke w 0x2DF4 %d' % rng.choice([0, 0x80, 200, 4000]),
           'poke w 0x2DF6 %d' % rng.choice([0, 50, 99, 100, 1234, 40000]), 'poke w 0xB168 %d' % rng.randint(0, 700), 'poke w 0xE448 %d' % rng.randint(100, 300),
           'poke w 0xE55A %d' % rng.randint(-800, 800), 'poke w 0x98C6 %d' % rng.randint(-2000, 2000), 'poke w 0x3D60 %d' % rng.randint(0, 0xFFFF),
           'poke b 0x294B %d' % (rng.randint(0, 1) if style is None else style), 'poke b 0x294A %d' % (rng.randint(0, 2) if level is None else level),
           'poke w 0xC07A %d' % (rng.choice([0, 0, 0, 0, 0x42, 0x44, 0x88]) if view is None else view),
           'poke w 0xE2FE %d' % rng.randint(0, 2), 'poke b 0x9B05 %d' % rng.choice([0, 2, 4, 6, 0x10, 0x14, 0x12]), 'poke w 0x9B04 %d' % rng.choice([0, 1, 4]),
           'poke w 0xDEDA %d' % rng.randint(0, 1), 'poke w 0x3632 %d' % rng.choice([0, 0, 8]), 'poke w 0x3D7A %d' % rng.randint(1, 2), 'poke w 0xB28C %d' % rng.randint(0, 1),
           'poke w 0x98EE %d' % rng.randint(0, 1), 'poke w 0x2DFC %d' % rng.randint(0, 1), 'poke w 0x409C %d' % rng.randint(0, 2), 'poke w 0xDFE0 %d' % rng.randint(0, 5),
           'poke w 0xE2EE %d' % rng.randint(0, 5), 'poke w 0x3634 %d' % rng.randint(0, 10000), 'poke w 0x3658 %d' % rng.randint(0, 1), 'poke w 0x4082 %d' % rng.randint(0, 7),
           'poke w 0x98DE %d' % rng.randint(0, 3), 'poke w 0xE546 %d' % rng.randint(-600, 600), 'poke w 0xE550 %d' % rng.randint(-300, 300), 'poke w 0x364E %d' % rng.randint(0, 3),
           'poke w 0x3660 %d' % rng.randint(4, 15), 'poke w 0x3D6A %d' % rng.randint(-1, 2), 'poke w 0xC07E %d' % rng.randint(0, 5),
           'poke w 0x2E7C %d' % rng.randint(0, 3), 'poke w 0x2DFC %d' % rng.randint(0, 1)]
    for i in range(4):
        ops += ['poke w 0x%X %d' % (0x363E + 4 * i, rng.randint(0, 19)), 'poke w 0x%X %d' % (0x3640 + 4 * i, rng.randint(0, 12))]
    wp = b''.join(struct.pack('<hh', px + rng.randint(-3000, 3000), py + rng.randint(-3000, 3000)) for _ in range(4)).hex()
    ops += ['pokes 0x2E6C ' + wp]
    for u in range(6):                                  # a few air units around the player
        o = 0xC13E + 0x24 * u
        ops += ['poke w 0x%X %d' % (o + 2, px + rng.randint(-2500, 2500)), 'poke w 0x%X %d' % (o + 4, py + rng.randint(-2500, 2500)), 'poke w 0x%X %d' % (o + 6, rng.randint(0, 6000)),
                'poke w 0x%X %d' % (o + 0x10, rng.randint(0, 65535)), 'poke w 0x%X %d' % (o + 0x18, rng.choice([2, 0, 0x207])), 'poke w 0x%X %d' % (o + 0x1A, rng.randint(0, 3))]
    for t in range(12):                                 # missile tracks
        o = 0x3C0C + 0x1C * t
        ops += ['poke w 0x%X %d' % (o, px + rng.randint(-2500, 2500)), 'poke w 0x%X %d' % (o + 2, py + rng.randint(-2500, 2500)), 'poke w 0x%X %d' % (o + 8, rng.randint(0, 65535)),
                'poke w 0x%X %d' % (o + 0xE, rng.randint(0, 1)), 'poke w 0x%X %d' % (o + 0x10, rng.randint(0, 20))]
    for k in range(6):                                  # sites
        o = 0xB2A2 + 16 * k
        ops += ['poke w 0x%X %d' % (o + 2, px + rng.randint(-3000, 3000)), 'poke w 0x%X %d' % (o + 4, py + rng.randint(-3000, 3000)), 'poke w 0x%X %d' % (o + 8, rng.choice([0, 1, 8, 0x80, 0x201, 0x280, 0x401, 0x802]))]
    for c in range(4):                                  # countermeasure marks
        o = 0x39AA + 12 * c
        ops += ['poke w 0x%X %d' % (o, px + rng.randint(-2000, 2000)), 'poke w 0x%X %d' % (o + 2, py + rng.randint(-2000, 2000)), 'poke w 0x%X %d' % (o + 6, rng.randint(1, 3)),
                'poke w 0x%X %d' % (o + 8, rng.randint(0, 1))]
    ops += ['pokes 0xC578 ' + hexstr('Waypoint 2 reached'), 'poke w 0x409E %d' % rng.randint(0, 3), 'poke w 0x365A %d' % rng.randint(0, 1)]
    return ops


def fam_hud(T, rng):
    for n in range(60):
        ops = hud_state(rng)
        ops += ['call hud_overlay_frame', 'poke w 0x3D60 %d' % rng.randint(0, 0xFFFF), 'call hud_overlay_frame']
        T.add('overlay%d' % n, 's1', ops)
    for n in range(20):
        ops = hud_state(rng, view=0)
        ops += ['call mfd_scope_draw %d' % rng.randint(0, 1)]
        T.add('scope%d' % n, 's1', ops)
    ops = hud_state(rng, view=0)
    for _ in range(20):
        ops += ['call mfd_w2s %d %d' % (rng.randint(0x1000, 0x6000), rng.randint(0x1000, 0x6000)), 'call mfd_target_box %d %d %d' % (rng.randint(60, 160), rng.randint(110, 170), rng.randint(1, 255))]
    T.add('w2s', 's1', ops)


def fam_hudfront(T, rng):
    for n in range(80):
        style = n % 2
        ops = hud_state(rng, style=style, level=(n // 2) % 3)
        ops += ['poke w 0x2DF0 %d' % rng.randint(-0x4000, 0x4000), 'poke w 0x2DF2 %d' % rng.randint(0, 65535), 'poke b 0xE542 %d' % rng.randint(0, 1),
                'poke b 0x2956 %d' % rng.randint(0, 1), 'poke w 0x2DF6 %d' % rng.choice([0, 50, 99, 100, 999, 1000, 1234, 2500, 4999, 9000, 30000]),
                'poke w 0xB168 %d' % rng.randint(0, 700), 'poke w 0x43B0 %d' % rng.randint(0, 1), 'call hud_color_init', 'call hud_draw_front']
        T.add('front%d' % n, 's1', ops)


def fam_view(T, rng):
    modes = [0, 0x44, 0x41, 0x42, 0x43, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8B, 0x8C, 0x8A, 0x45]
    for n in range(45):
        mode = modes[n % len(modes)]
        ops = hud_state(rng, view=0)
        ops += ['poke w 0xC07A %d' % mode, 'poke w 0x2DEE %d' % rng.randint(0, 65535), 'poke w 0x2DF0 %d' % rng.randint(-0x5000, 0x5000), 'poke w 0x2DF2 %d' % rng.randint(0, 65535),
                'poke w 0x2DF4 %d' % rng.randint(0, 9000), 'poke w 0x3D70 %d' % rng.randint(0, 10), 'poke w 0x2DFC %d' % rng.randint(0, 1), 'poke w 0x2E48 %d' % rng.choice([mode, mode, 0x99]),
                'poke w 0x365E %d' % rng.randint(0, 1), 'poke w 0x3D64 %d' % rng.choice([0xFFFF, 1, 3]), 'poke w 0x3D66 %d' % rng.choice([0xFFFF, 0, 2]), 'poke w 0xB04C %d' % rng.randint(0, 11),
                'poke w 0xDEA2 %d' % rng.randint(0, 65535), 'poke w 0xDEC6 %d' % rng.randint(0, 65535), 'poke w 0xDECA %d' % rng.randint(0, 5000), 'poke w 0x3660 %d' % rng.randint(0, 15),
                'poke w 0x3D60 %d' % rng.randint(0, 255), 'poke w 0x2E00 %d' % rng.randint(0, 2), 'poke w 0xDEDA %d' % rng.randint(0, 1), 'poke w 0xDFDC %d' % rng.choice([0, 0x13])]
        for i in range(16):
            ops += ['pokes 0x%X %s' % (0xB056 + 16 * i, struct.pack('<iiHHHH', rng.randint(0, 0x300000), rng.randint(0, 0x300000), rng.randint(0, 5000), rng.randint(0, 65535), rng.randint(0, 65535), rng.randint(0, 65535)).hex())]
        ops += ['call view_frame']
        T.add('view%d' % n, 's1', ops)
    for n in range(20):
        ops = hud_state(rng, view=0)
        th, ph = rng.uniform(0, 6.2832), rng.uniform(-1.5, 1.5)
        ops += ['poke w 0x98F8 %d' % int(32767 * math.sin(ph)), 'poke w 0x9F12 %d' % int(32767 * math.cos(ph)), 'poke w 0x98E4 %d' % int(32767 * math.cos(th)),
                'poke w 0x98C8 %d' % int(-32767 * math.sin(th)), 'call cockpit_draw_adi']
        T.add('adi%d' % n, 's1', ops)


def fam_polysingle(T, rng):
    from sim_scn import SCRATCH
    T.add('nullscn', 's0', ['poke b 0x4088 1', 'call tables_clear'])
    for n in range(40):
        r = 3000 if n % 2 else 25000
        pts = [rng.randint(-r, r) for _ in range(8)]
        ops = ['poke b 0x4088 1', 'pokes %d %s' % (SCRATCH, struct.pack('<8h', *pts).hex()), 'poke w 0x2293 319', 'poke w 0x2295 111', 'call poly_fill_far 0x3FE4 4 %d 1' % SCRATCH]
        T.add('ps%d' % n, 's0', ops)


def fam_mfdview(T, rng):
    for n in range(8):
        ops = hud_state(rng, view=0) + ['poke b 0x4088 1']
        for h in (0x6E, 0x70, 0x90, 0xA9, 0xB0, 0x40):
            ops += ['call mfd_view_sky_bands %d' % h, 'call mfd_view_ground_bands %d %d' % (h, rng.choice([0, 1, 8, 0xFC]))]
        T.add('bands%d' % n, 's1', ops)
    for n in range(4):
        ops = hud_state(rng, view=0) + ['poke b 0x4088 1', 'call mfd_view_flir_tint %d %d' % (rng.choice([0xA9, 10, 100]), rng.choice([0x6E, 20, 80]))]
        T.add('tint%d' % n, 's1', ops)
    for n in range(80):
        ops = hud_state(rng, view=0)
        ops += ['poke b 0x4088 %d' % rng.randint(0, 1), 'poke w 0x34A %d' % rng.randint(0, 1), 'poke w 0x43B2 %d' % rng.randint(0, 1), 'poke w 0x98DE %d' % rng.randint(0, 3),
                'poke w 0x3658 %d' % rng.randint(0, 1), 'poke w 0x480E %d' % rng.randint(0, 1), 'poke w 0x9D0C %d' % rng.choice([0, 0, 1, -2]), 'poke w 0x9D0E %d' % rng.randint(0, 1),
                'poke w 0xB04C %d' % rng.randint(0, 11), 'poke w 0xC576 %d' % rng.choice([0, 5, 100, 1000, 30000]), 'poke w 0x3D66 %d' % rng.randint(0, 5), 'poke w 0x3D64 %d' % rng.randint(0, 5),
                'poke w 0xB288 %d' % rng.randint(0, 65535), 'poke w 0xB28A %d' % rng.randint(0, 15), 'poke w 0xB752 %d' % rng.randint(0, 65535), 'poke w 0xB754 %d' % rng.randint(0, 15),
                'poke w 0xB75C %d' % rng.randint(0, 6000), 'poke w 0x98CE %d' % rng.randint(0, 65535), 'poke w 0xDEA4 %d' % rng.randint(0, 255), 'poke w 0xC0AC %d' % rng.randint(0, 255)]
        for i in range(0, 128, 7):
            ops += ['poke b %d %d' % (0x960 + i, rng.randint(0, 255)), 'poke b %d %d' % (0x9E0 + i, rng.randint(0, 255)), 'poke b %d %d' % (0xC604 + i, rng.randint(0, 255)),
                    'poke w %d %d' % (0x510 + 2 * i, rng.randint(0, 100))]
        obj = rng.choice([0x100, 0x105, 0x8103, 3, 0x40, 0x8002, 0x17E])
        mode = rng.choice([0, 1, 1, 1, 3, 4, 2])
        zs = rng.choice([-1, 0, 1])
        x, y, z = rng.randint(0, 65535), rng.randint(0, 65535), rng.randint(0, 6000)
        if rng.random() < 0.3:
            x, y = x & 0xFF00, y & 0xFF00
        ops += ['call mfd_view_render %d %d %d %d %d %d %d %d %d' % (obj, x, y, z, rng.randint(0, 65535), rng.randint(0, 65535), rng.randint(0, 65535), mode, zs)]
        T.add('mview%d' % n, 's1', ops)


def objovl_state(rng):
    ops = hud_state(rng, view=0)
    ops += ['poke w 0x365E %d' % rng.choice([1, 1, 1, 0]), 'poke b 0x4088 %d' % rng.randint(0, 1), 'poke w 0xC07A %d' % rng.choice([0, 0, 0, 0x44]),
            'poke w 0xE2FE %d' % rng.randint(0, 2), 'poke w 0xDFDC %d' % rng.choice([0x13, 0x13, 0x14, 0x15]), 'poke w 0x3654 %d' % rng.randint(0, 3), 'poke w 0x3656 %d' % rng.randint(0, 3),
            'poke w 0x3660 %d' % rng.randint(4, 15), 'poke w 0xB16C %d' % rng.randint(3, 16), 'poke w 0x3D60 %d' % rng.randint(0, 255), 'poke w 0x3D66 %d' % rng.choice([-1, 0, 1, 2, 3, 5, 0x82]),
            'poke w 0x3D64 %d' % rng.choice([-1, 0, 1, 2, 5, 0x82]), 'poke w 0x3D68 %d' % rng.choice([0, 0xFFFF]), 'poke w 0x364E %d' % rng.randint(0, 3), 'poke w 0x3D6A %d' % rng.choice([0, 3, 6]),
            'poke w 0xC07E %d' % rng.choice([-1, -3, 0, 2, 5]), 'poke w 0x9872 %d' % rng.randint(0, 255), 'poke b 0x9B04 %d' % rng.choice([0, 4, 5, 0x24]), 'poke b 0x9B05 %d' % rng.choice([0, 1, 0x40, 0x20, 3]),
            'poke w 0x9D0C %d' % rng.choice([0, 0, 5, -1, 16]), 'poke w 0x9D0E %d' % rng.randint(0, 1), 'poke w 0x98FE %d' % rng.choice([-1, 0, 3]), 'poke w 0xC0B0 %d' % rng.choice([0, 100, -4]),
            'poke w 0xC0A0 %d' % rng.randint(0x1000, 0x6000), 'poke w 0xC0AA %d' % rng.randint(0x1000, 0x6000), 'poke w 0x41F4 %d' % rng.choice([0, 0, 1, 4]), 'poke w 0x41F2 %d' % rng.randint(0, 9),
            'poke w 0x9508 %d' % rng.randint(0, 1), 'poke w 0x4706 %d' % rng.randint(0, 1), 'poke w 0xE554 %d' % rng.choice([0, 4]), 'poke w 0xB04C %d' % rng.randint(0, 11), 'poke w 0xE556 %d' % rng.choice([0, 0x4000, 0x8000]),
            'poke w 0x43B4 %d' % rng.randint(0, 1), 'poke w 0x3640 %d' % rng.randint(0, 3), 'poke w 0xDE88 %d' % rng.randint(0, 100), 'poke w 0x98E2 %d' % rng.randint(0, 100), 'poke w 0xC684 %d' % rng.randint(10, 80),
            'poke w 0xB168 %d' % rng.randint(0, 1700), 'poke w 0x2DF4 %d' % rng.randint(0, 9000), 'poke w 0x2DF2 %d' % rng.choice([0, 0x1000, 0x3000, 0xC000]), 'poke w 0x2DF0 %d' % rng.randint(-0x4000, 0x4000),
            'poke w 0x9502 %d' % rng.randint(0, 65535), 'poke w 0xDED4 %d' % rng.randint(0, 65535), 'poke w 0xDE92 %d' % rng.randint(-400, 0), 'poke w 0x98DE %d' % rng.randint(0, 3), 'poke w 0xDED0 %d' % rng.randint(0, 6),
            'poke w 0xC0B4 %d' % rng.randint(1, 6), 'poke w 0x294B %d' % rng.randint(0, 1)]
    ops = [o.replace('poke w 0x294B', 'poke b 0x294B') for o in ops]
    for i in range(20):                                      # the stores and the projectile prototypes
        ops += ['poke w %d %d' % (0x3662 + 26 * i + 0x16, rng.choice([-1, -2, 0, 4, 5, 6, 7, 0x1B, 0x1C, 0x1D, 0x1E, 12, 30, 39]))]
    for i in range(40):
        ops += ['poke w %d %d' % (0x3362 + 18 * i + 8, rng.choice([5, 40, 100, 500, 3000, 20000])), 'poke w %d %d' % (0x3362 + 18 * i + 0xC, rng.choice([4, 5, 6, 7, 28, 29, 30, 0, 12]))]
    for i in range(4):
        ops += ['poke w %d %d' % (0x363E + 4 * i, rng.randint(0, 19)), 'poke w %d %d' % (0x3640 + 4 * i, rng.randint(0, 3))]
    for i in range(20):                                      # the bullets
        base = 0xE44E + 12 * i
        px, py = 0x3000, 0x3000
        ops += ['pokes %d %s' % (base, struct.pack('<6h', rng.choice([0, px + rng.randint(-300, 300)]), py + rng.randint(-300, 300), rng.choice([-5, 10, 300, 2000]),
                                                   rng.randint(-60, 60), rng.randint(-60, 60), rng.randint(-60, 60)).hex())]
    for k in range(2):                                       # the mission slots
        ops += ['pokes %d %s' % (0xE2D8 + 0x12 * k, struct.pack('<hhhhhh', rng.choice([1, 1, 5, 2]), rng.randint(0, 5), 0, rng.choice([0, 0x10, 0x10]), rng.randint(0, 3), 0).hex())]
    ops += ['poke w 0xC0A4 %d' % 0x3000, 'poke w 0xC0B2 %d' % 0x3000]
    for u in range(6):
        o = 0xC13E + 0x24 * u
        ops += ['poke w 0x%X %d' % (o + 0x16, rng.randint(0, 28)), 'poke w 0x%X %d' % (o + 2, 0x3000 + rng.randint(-400, 400)), 'poke w 0x%X %d' % (o + 4, 0x3000 + rng.randint(-400, 400)),
                'poke w 0x%X %d' % (o + 6, rng.randint(0, 2000))]
    for k in range(6):
        o = 0xB2A2 + 16 * k
        ops += ['poke w 0x%X %d' % (o + 2, 0x3000 + rng.randint(-600, 600)), 'poke w 0x%X %d' % (o + 4, 0x3000 + rng.randint(-600, 600)), 'poke w 0x%X %d' % (o + 0xE, rng.randint(0, 255)),
                'poke w 0x%X %d' % (o, rng.randint(0, 60))]
    for i in range(0, 128):
        ops += ['poke w %d %d' % (0xDEDC + 2 * i, 0x4270 + rng.choice([0, 1, 0x20, 0x31, 0x1F]))]
    # a valid view matrix
    th, ph = rng.uniform(0, 6.28), rng.uniform(-0.5, 0.5)
    m = [int(32767 * v) for v in (math.cos(th), 0, math.sin(th), 0, 1.0 if False else math.cos(ph), 0, -math.sin(th), 0, math.cos(th))]
    ops += ['pokes 0xB156 ' + struct.pack('<9h', *m).hex()]
    return ops


def fam_objovl(T, rng):
    for n in range(30):
        ops = objovl_state(rng)
        ops += ['call obj_overlay_frame']
        if n % 3 == 0:
            ops += ['poke w 0x3D60 %d' % rng.randint(0, 255), 'poke b 0x4088 1', 'call obj_overlay_frame']
        T.add('ovl%d' % n, 's1', ops)
    for n in range(10):
        ops = objovl_state(rng)
        for _ in range(8):
            ops += ['call r3d_project_point %d %d %d' % (rng.randint(0x2000, 0x4000), rng.randint(0x2000, 0x4000), rng.randint(0, 3000))]
            ops += ['call hud_target_box %d %d %d %d %d' % (rng.randint(0, 319), rng.randint(0, 120), rng.randint(0, 80), rng.randint(0, 1), rng.randint(0, 255)),
                    'call hud_draw_diamond', 'call hud_update_aspect_label', 'call hud_seeker_cross %d' % rng.randint(0, 1), 'call hud_lock_bracket %d' % rng.randint(0, 1),
                    'call hud_lock_indicator %d %d' % (rng.randint(0, 1), rng.randint(0, 1)), 'call hud_ping_label %d %d %d' % (0x42AA, rng.randint(0, 255), rng.randint(0, 120)),
                    'call hud_format_range %d' % rng.randint(-2000, 30000)]
        T.add('ovh%d' % n, 's1', ops)


def fam_objovp(T, rng):
    """targeted configurations of the overlay pass: everything placed ahead of the nose so that the projections are visible"""
    for n in range(60):
        ops = objovl_state(rng)
        hdg = rng.randint(0, 65535)
        yaw = hdg * 2 * math.pi / 65536
        px, py = 0x3000, 0x3000

        def ahead(r, spread=40):
            return (int(px + r * math.sin(yaw) + rng.randint(-spread, spread)), int(py - r * math.cos(yaw) + rng.randint(-spread, spread)))
        cfg = n % 6
        ops += ['poke w 0x2DEE %d' % hdg, 'poke w 0x2DF0 %d' % rng.randint(-0x300, 0x300), 'poke w 0x2DF2 %d' % rng.choice([0, 0x300, 0xFD00]), 'poke w 0x2DF4 %d' % rng.randint(60, 2500),
                'poke w 0xC0A4 %d' % px, 'poke w 0xC0B2 %d' % py, 'poke w 0xC07A 0x44', 'poke w 0x2E48 0x44', 'call view_frame', 'poke w 0xC07A 0', 'poke w 0x365E 1', 'poke w 0x2E48 0', 'poke b 0x294B 0',
                'poke w 0x2E00 2', 'poke b 0x4088 1', 'poke w 0xDFDC 0x13', 'poke w 0xDED0 6']
        for k in range(6):                                     # sites ahead of the nose
            o = 0xB2A2 + 16 * k
            x, y = ahead(rng.randint(60, 700), 120)
            ops += ['poke w 0x%X %d' % (o + 2, x), 'poke w 0x%X %d' % (o + 4, y), 'poke w 0x%X %d' % (o + 8, rng.choice([1, 0x101, 0x401, 0x501, 0x201]))]
        for u in range(6):
            o = 0xC13E + 0x24 * u
            x, y = ahead(rng.randint(60, 700), 120)
            ops += ['poke w 0x%X %d' % (o + 2, x), 'poke w 0x%X %d' % (o + 4, y), 'poke w 0x%X %d' % (o + 6, rng.randint(0, 2500)), 'poke w 0x%X %d' % (o + 0x18, rng.choice([2, 2, 0]))]
        for i in range(20):
            x, y = ahead(rng.randint(40, 600), 20)
            ops += ['pokes %d %s' % (0xE44E + 12 * i, struct.pack('<6h', rng.choice([x, x, 0]), y, rng.choice([-5, 100, 1000]), rng.randint(-60, 60), rng.randint(-60, 60), rng.randint(-60, 60)).hex())]
        for t in range(12):
            x, y = ahead(rng.randint(40, 600), 20)
            ops += ['poke w 0x%X %d' % (0x3C0C + 0x1C * t, x), 'poke w 0x%X %d' % (0x3C0E + 0x1C * t, y), 'poke w 0x%X %d' % (0x3C10 + 0x1C * t, rng.randint(0, 2000)), 'poke w 0x%X %d' % (0x3C1A + 0x1C * t, 1)]
        kinds = [-1, 0x1E, 0x1D, 0x1C, 6, 7, 4, 5]
        ops += ['poke w 0x364E 0', 'poke w 0x363E %d' % rng.randint(0, 19), 'poke w 0x3640 %d' % rng.randint(1, 3), 'poke w 0x3D66 %d' % rng.randint(0, 5), 'poke w 0x3D64 %d' % rng.randint(0, 5)]
        ops += ['poke w 0x%X %d' % (0x3662 + 26 * i + 0x16, rng.choice(kinds)) for i in range(20)]
        ops += ['poke b 0x9B04 %d' % rng.choice([4, 0, 4]), 'poke w 0x41F4 0', 'poke w 0x9508 %d' % rng.randint(0, 1), 'poke w 0x3D6A %d' % rng.choice([0, 4]), 'poke w 0xC07E %d' % rng.choice([-2, 1])]
        ops += ['poke w 0xE2FE %d' % [2, 2, 2, 1, 0, 2][cfg], 'poke w 0x3640 %d' % rng.randint(1, 3), 'poke b 0x9B05 %d' % rng.choice([0, 1, 0])]
        ops += ['poke w 0xE2DA %d' % rng.randint(0, 5), 'poke w 0xE2D8 %d' % rng.choice([1, 5, 2, 6]), 'poke w 0xE2EC %d' % rng.randint(0, 5)]
        if n % 7 == 0:                                       # a live Maverick: the TV view
            slot = rng.randint(9, 11)
            ops += ['poke w 0xB04C %d' % slot, 'poke w 0x%X 1' % (0x3C0C + 0x1C * slot + 0xE), 'poke w 0x%X 0x1B' % (0x3C0C + 0x1C * slot + 0x10), 'poke w 0x43B4 0', 'poke b 0x9B04 0', 'poke w 0xE2FE 2',
                    'poke w 0xE554 4']
        if n % 5 == 0:
            ops += ['poke w 0x3D64 0', 'poke w 0xE2FE 1', 'poke w 0xE2D8 5']
        for r in range(3):
            ops += ['call obj_overlay_frame', 'poke w 0x3D60 %d' % rng.randint(0, 255), 'poke b 0x4088 %d' % rng.randint(0, 1)]
        T.add('ovp%d' % n, 's1', ops)
