"""S5.5: scenario families of the cockpit panel, the MFDs and the HUD overlay (see sim_scn.py)."""
import struct

from sim_scn import SCRATCH


def fam_mfd(T, rng):
    base = ['poke w 0x365E 1', 'poke b 0x4088 1']

    def pos():
        return rng.randint(0x1000, 0x6000)
    for n in range(10):                                                # map primitives
        ops = list(base)
        ops += ['poke w 0xC0A4 %d' % pos(), 'poke w 0xC0B2 %d' % pos(), 'poke w 0x2DEE %d' % rng.randint(0, 0xFFFF),
                'poke w 0x4080 %d' % rng.randint(2, 9), 'poke w 0xDEDA 0', 'poke w 0xDE94 74', 'poke w 0xE302 150', 'poke w 0xDE96 110', 'poke w 0xE444 169']
        ops += ['call mfd_map_redraw %d %d' % (pos(), pos())]
        for _ in range(6):
            wx, wy = pos(), pos()
            ops += ['poke w 0x4084 %d' % wx, 'poke w 0x4086 %d' % wy]
            ops.append('call mfd_map_sx %d' % (wx + rng.randint(-4000, 4000)))
            ops.append('call mfd_map_sy %d' % (wy + rng.randint(-4000, 4000)))
            ops.append('call mfd_map_plot %d %d %d %d' % (wx + rng.randint(-3000, 3000), wy + rng.randint(-3000, 3000), rng.randint(-1, 20), rng.randint(0, 1)))
            ops.append('call mfd_map_to_screen %d %d %d %d' % (wx + rng.randint(-3000, 3000), wy + rng.randint(-3000, 3000), SCRATCH, SCRATCH + 2))
            ops.append('call mfd_map_line %d %d %d %d' % (wx + rng.randint(-3000, 3000), wy + rng.randint(-3000, 3000), wx + rng.randint(-3000, 3000), wy + rng.randint(-3000, 3000)))
            ops.append('call mfd_map_arc %d %d %d %d %d %d %d' % (wx, wy, rng.randint(100, 3000), rng.randint(1, 30), rng.randint(0, 1), rng.randint(0, 255), rng.randint(0, 255)))
        ops += ['call mfd_map_save_bg', 'call mfd_map_restore_bg', 'poke w 0xDEDA %d' % rng.randint(0, 1), 'call mfd_map_plot 100 100 5 1']
        T.add('map%d' % n, 's0', ops)
    for n in range(4):                                                 # route
        ops = list(base) + ['poke w 0xC0A4 %d' % pos(), 'poke w 0xC0B2 %d' % pos(), 'poke w 0x4080 5']
        wp = b''.join(struct.pack('<hh', rng.choice([0, pos()]), pos()) for _ in range(4)).hex()
        ops += ['pokes 0x2E6C ' + wp, 'poke w 0x2E7C %d' % rng.randint(0, 3), 'call mfd_map_route']
        T.add('route%d' % n, 's0', ops)
    for n in range(4):                                                 # zoom and select
        ops = list(base) + ['poke w 0xC0A4 %d' % pos(), 'poke w 0xC0B2 %d' % pos(), 'call mfd_init_geometry']
        for _ in range(6):
            ops += ['poke w 0xC07A %d' % rng.choice([0, 0, 0x80, 0x88]), 'call mfd_zoom_in', 'call mfd_zoom_out', 'call mfd_zoom_out',
                    'call mfd_left_select %d' % rng.randint(0, 2), 'call mfd_zoom_in', 'poke w 0x3D70 %d' % rng.randint(0, 9)]
        T.add('zoom%d' % n, 's0', ops)
    for n in range(6):
        ops = list(base) + ['poke w 0xC0A4 8192', 'poke w 0xC0B2 8192', 'poke w 0x4080 4', 'pokes 0x2E6C ' + struct.pack('<8h', 9000, 9000, 9500, 8000, 0, 0, 7000, 7000).hex()]
        for page in (0x13, 0x14, 0x15, 0x18, 0x14, 0x13, 0x15, 0x18):
            ops += ['poke w 0xE556 %d' % rng.choice([0, 0x4000, 0x8000, 0xC000, 0x1234]), 'poke w 0x43B2 %d' % rng.randint(0, 1), 'call mfd_right_select %d' % page]
        ops += ['poke w 0x3632 %d' % rng.randint(0, 255), 'call mfd_refresh 22', 'call mfd_refresh 20', 'call mfd_tv_label']
        T.add('rsel%d' % n, 's0', ops)
    for n in range(8):                                                 # navigation page
        ops = list(base) + ['poke w 0xC0A4 %d' % pos(), 'poke w 0xC0B2 %d' % pos(),
                            'poke w 0xB80C %d' % rng.randint(0, 1), 'poke w 0x2E7C %d' % rng.randint(0, 3), 'poke w 0x2E7E %d' % rng.randint(0, 3),
                            'poke w 0xB052 %d' % rng.choice([0, 1500, 2001, 9000, 20000, 40000]), 'poke w 0x2DFE %d' % rng.randint(0, 100),
                            'poke w 0x3632 %d' % rng.choice([0, 0x20, 0x28]), 'poke w 0xC5C8 %d' % rng.randint(0, 9), 'poke w 0x3634 %d' % rng.randint(0, 10000),
                            'poke w 0x98E2 %d' % rng.randint(0, 3000), 'poke w 0x9870 %d' % rng.choice([0, 0x300, 0x1100]), 'poke w 0x3658 %d' % rng.randint(0, 1)]
        wp = b''.join(struct.pack('<hh', pos(), pos()) for _ in range(4)).hex()
        ops += ['pokes 0x2E6C ' + wp, 'call mfd_nav_page']
        T.add('nav%d' % n, 's0', ops)
    for n in range(6):                                                 # weapons and objectives
        ops = list(base) + ['poke w 0x3660 4']
        for i in range(4):
            ops += ['poke w 0x%X %d' % (0x363E + 4 * i, rng.randint(0, 19)), 'poke w 0x%X %d' % (0x3640 + 4 * i, rng.randint(0, 12))]
        ops += ['call mfd_weapons_page']
        for k in range(2):
            ops += ['poke w 0x%X %d' % (0xE2D8 + 0x12 * k, rng.randint(0, 9)), 'poke w 0x%X %d' % (0xE2DA + 0x12 * k, rng.randint(0, 20)),
                    'poke w 0x%X %d' % (0xE2DE + 0x12 * k, rng.choice([0, 2, 0x22]))]
        ops += ['poke w 0xE2EE %d' % rng.randint(0, 20), 'poke w 0x9B04 %d' % rng.choice([0, 0x4000, 0x2000, 0x6000]), 'poke w 0xDE88 %d' % rng.randint(0, 3000),
                'poke w 0x98E2 %d' % rng.randint(0, 3000), 'call mfd_objectives_page']
        T.add('wpn_obj%d' % n, 's0', ops)
    for n in range(6):                                                 # text helpers
        ops = []
        for _ in range(10):
            t = rng.randint(0, 65535)
            ops += ['poke w 0x3658 %d' % rng.randint(0, 1), 'poke w 0x9870 %d' % rng.choice([0, 0x300, 0x500]), 'call fmt_clock_hhmm %d' % t, 'call fmt_clock_hhmmss %d' % t,
                    'call site_name %d' % rng.randint(0, 25), 'call msg_site_label %d' % rng.randint(0, 25), 'call fmt_2digit %d' % rng.randint(-100, 3000),
                    'call nav_copy_xy %d %d' % (rng.randint(0, 3), rng.randint(0, 20))]
        T.add('text2_%d' % n, 's0', ops)
    for n in range(6):                                                 # ILS
        ops = list(base) + ['poke w 0xDFE0 %d' % rng.randint(0, 5), 'poke w 0x2DEE %d' % rng.randint(0, 65535), 'poke w 0x2DF4 %d' % rng.randint(0, 40000), 'poke b 0x294B 0']
        for k in range(5):
            site = 0xB2A2 + 16 * rng.randint(0, 5)
            ops += ['poke w 0x%X %d' % (site + 2, pos()), 'poke w 0x%X %d' % (site + 4, pos())]
        for k in range(4):
            ops += ['poke w 0xC0A4 %d' % pos(), 'poke w 0xC0B2 %d' % pos(), 'call hud_ils']
        T.add('ils%d' % n, 's0', ops)
    ops = list(base)
    for val in (-100, -50, -1, 0, 1, 2, 50, 99, 100):
        ops += ['call hud_threat_bar %d %d 0 10' % (val, rng.randint(1, 255))]
    for v in (0, 100, 134, 135, 5000, 10000, 20000, -5):
        ops += ['call mfd_fuel_bar %d %d' % (v, rng.randint(1, 255))]
    for cx in range(0, 12):
        ops += ['call mfd_icon %d %d %d %d' % (cx, rng.randint(0, 3), rng.randint(60, 160), rng.randint(100, 180))]
    for k in range(5):
        ops += ['call cockpit_keyblit %d %d %d %d %d %d 0' % (rng.randint(0, 250), rng.randint(0, 150), rng.randint(0, 200), rng.randint(0, 150), rng.randint(1, 50), rng.randint(1, 40))]
    T.add('misc2', 's0', ops)
    T.add('refresh_panels', 's0', ['poke w 0x365E 1', 'poke b 0x4088 1', 'poke w 0x46E2 1', 'poke w 0x%X 1' % (0x46E2 + 12 * 3), 'poke w 0x%X 1' % (0x46E2 + 12 * 20),
                                   'poke w 0xDEDA 1', 'poke w 0xDFDC 21', 'call cockpit_refresh_panels'])
