"""S5.5: the scenario families of ctest `sim` (see sim_ref.py). Each family adds scenarios `T.add(id, snapshot, ops...)`; an op is a line of the scenario
language of f117port/src/sim/simtest.c:  poke b|w|l OFF V  /  pokes OFF HEX  /  call NAME args.  The same lines drive the ORIGINAL (vgame_emu.py) and the port."""
import struct

SCRATCH = 0xF100            # DS offset of free memory for strings and arrays (above the stack the harness uses)


def hexstr(t):
    return t.encode('latin-1').hex() + '00'


def fam_math(T, rng):
    """return values of the fixed-point helpers, one scenario per function (the hash of the return values is compared)"""
    specials = [0, 1, -1, 2, 0x7FFF, -0x7FFF, -0x8000, 0x4000, -0x4000, 0x2000, 255, 256, 0x3FFF, 0x8001, 100, -100]

    def a16():
        return rng.choice(specials) if rng.random() < 0.25 else rng.randint(-0x8000, 0x7FFF)
    two = ('fx_mul15', 'fx_dist', 'fx_heading', 'fx_sin_mul', 'fx_cos_mul')
    three = ('fx_clamp', 'fx_clamp_wrap')
    one = ('fx_sin16', 'fx_cos16', 'fx_sext8', 'fx_isqrt', 'fx_asin', 'fx_acos', 'lib_sign')
    for name in two + three + one + ('fx_ratio_q15', 'lib_scale_eighths'):
        ops = []
        for _ in range(300):
            if name in two:
                ops.append('call %s %d %d' % (name, a16(), a16()))
            elif name in three:
                ops.append('call %s %d %d %d' % (name, a16(), a16(), a16()))
            elif name in one:
                ops.append('call %s %d' % (name, a16()))
            elif name == 'lib_scale_eighths':
                ops.append('call lib_scale_eighths %d %d' % (rng.randint(-1, 4), a16()))
            else:
                x, y = a16(), a16()
                if abs(x) > abs(y):
                    x, y = y, x
                if y != 0:
                    ops.append('call fx_ratio_q15 %d %d' % (x, y))
        T.add('math_' + name, 's0', ops)


def fam_lamps(T, rng):
    for ci, ctx in enumerate((0x3FCC, 0x3FE4, 0x3FFC)):
        ops = ['poke w 0x365E 1']
        for lamp in range(37):
            ops.append('call hud_lamp_recolor %d %d %d' % (ctx, lamp, lamp & 1))
        for lamp in range(37):
            ops.append('call hud_lamp_recolor %d %d %d' % (ctx, lamp, 1 - (lamp & 1)))
        T.add('lamp_ctx%d' % ci, 's0', ops)
    ops = ['poke w 0x365E 1']
    for _ in range(60):
        ops.append('call hud_lamp_update %d %d' % (rng.randint(0, 14), rng.choice([0, 1, 3, 10, 15, 0x29, 0x0A, 5])))
    T.add('lamp_update', 's0', ops)
    ops = ['poke w 0x365E 1']
    for _ in range(60):
        ops.append('call hud_lamp_set %d %d' % (rng.randint(0, 36), rng.randint(0, 1)))
    T.add('lamp_set', 's0', ops)
    T.add('lamp_off', 's0', ['poke w 0x365E 0', 'call hud_lamp_set 3 1', 'call hud_lamp_update 4 15'])
    ops = ['poke w 0x365E 1']
    for _ in range(40):
        ops.append('poke w 0x3D8E %d' % rng.randint(0, 2))
        ops.append('poke w 0x3D90 %d' % rng.randint(0, 2))
        ops.append('poke w 0x3D92 %d' % rng.randint(0, 2))
        ops.append('call hud_lamp_timers')
    T.add('lamp_timers', 's0', ops)


def fam_digits(T, rng):
    ops = ['poke w 0x365E 1']
    for slot in range(5):
        for v in (0, 5, 9, 10, 55, 99, 100, 123, 999, 1000, 9999, -3):
            ops.append('call hud_number_draw %d %d' % (slot, v))
    T.add('digits', 's0', ops)
    ops = ['poke w 0x365E 1']
    for v in (0, 5, 99, 100, 9999, 10000, 12000, -1):
        ops.append('poke w 0x3634 %d' % (v & 0xFFFF))
        ops.append('call hud_draw_fuel')
    for v in (0, 7, 50, 100):
        ops.append('poke w 0x2DFE %d' % v)
        ops.append('call hud_draw_throttle')
    for t in (1, 2, 3):
        for v in (0, 4, 9, 10, 25, 99, 100):
            ops.append('poke w 0x%X %d' % (0x3636 + 2 * t, v))
            ops.append('call hud_draw_store_count %d' % t)
    ops.append('call hud_draw_store_counts')
    T.add('digits2', 's0', ops)
    ops = ['poke w 0x365E 1']
    for page in (0, 1, 2):
        for slot in range(5):
            ops.append('pokes %d %s' % (SCRATCH, hexstr('4096A17')))
            ops.append('call hud_digits_draw %d %d %d' % (page, slot, SCRATCH))
    T.add('digits3', 's0', ops)


def fam_lines(T, rng):
    def c():
        return rng.choice([-50, -1, 0, 5, 100, 319, 320, 400, 90, 91, 199, 200, 160, 45, 255]) if rng.random() < .4 else rng.randint(-100, 450)
    for n in range(8):
        ops = ['poke w 0x365E 1', 'poke b 0x4088 1']
        for _ in range(25):
            ops.append('poke w 0x3FD0 %d' % rng.randint(0, 255))        # ctx0.fg
            ops.append('call hud_line_clip %d %d %d %d %d %d %d %d %d' % (c(), c(), c(), c(), rng.choice([0, 5, 48]), rng.choice([319, 271, 220]),
                                                                          rng.choice([0, 15, 30]), rng.choice([199, 90, 89]), rng.randint(0, 1)))
        T.add('lineclip%d' % n, 's0', ops)
    for n in range(4):
        ops = ['poke b 0x4088 1']
        for _ in range(25):
            ops.append('poke w 0x3FD0 %d' % rng.randint(0, 255))
            ops.append('call hud_line_view %d %d %d %d' % (c(), c(), c(), c()))
            ops.append('call hud_line_win %d %d %d %d' % (c(), c(), c(), c()))
        T.add('lineview%d' % n, 's0', ops)
    ops = ['poke b 0x4088 1']
    for _ in range(12):
        ops.append('call hud_box %d %d %d %d %d' % (rng.randint(0, 300), rng.randint(0, 190), rng.randint(0, 300), rng.randint(0, 190), rng.randint(1, 255)))
        ops.append('call hud_pixel %d %d %d' % (rng.randint(0, 319), rng.randint(0, 199), rng.randint(1, 255)))
    T.add('boxes', 's0', ops)
    for n in range(400):                                               # line_clip_far direct with odd windows, one call per scenario
        ops = ['poke b 0x4088 1', 'call gfx_set_fg 13']
        for _ in range(1):
            ops.append('poke w 0x2293 %d' % rng.choice([0, 1, 39, 100, 319, 0x7FFF, 0x8000]))
            ops.append('poke w 0x2295 %d' % rng.choice([0, 1, 20, 60, 199]))
            for o in (0x2299, 0x229B, 0x229D, 0x229F):
                v = rng.choice([-32768, 32767, -1, 0, 1, 319, 100, 99]) if rng.random() < .3 else rng.randint(-300, 500)
                ops.append('poke w 0x%X %d' % (o, v & 0xFFFF))
            ops.append('call line_clip_far')
        T.add('lineclipfar%d' % n, 's0', ops)


def fam_clipdeg(T, rng):
    for n in range(60):
        ops = ['poke b 0x4088 1', 'call gfx_set_fg 13']
        ops.append('poke w 0x2293 %d' % rng.choice([0, 1, 3]))
        ops.append('poke w 0x2295 %d' % rng.choice([0, 1, 3]))
        for o in (0x2299, 0x229B, 0x229D, 0x229F):
            ops.append('poke w 0x%X %d' % (o, rng.randint(-100, 900) & 0xFFFF))
        ops.append('call line_clip_far')
        T.add('clipdeg%d' % n, 's0', ops)


def fam_fills(T, rng):
    ops = ['poke b 0x4088 1']
    for _ in range(25):
        y0 = rng.randint(0, 150)
        y1 = rng.randint(y0, 199)
        x0 = rng.randint(0, 300)
        x1 = rng.randint(x0, 319)
        ops.append('call gfx_set_fg %d' % rng.randint(0, 255))
        ops.append('call hud_fill_rect %d %d %d %d' % (x0, y0, x1, y1))
        ops.append('call fill_rows_far %d %d %d %d %d' % (rng.choice([0x3FCC, 0x3FE4, 0x3FFC]), x0, y0, x1, y1))
    for w in (1, 2):
        for col in (0, 6, 0x9F, 0x2A, 1):
            ops.append('call mfd_fill %d %d' % (w, col))
    ops.append('call gfx_restore_rect 10 100 50 40')
    T.add('fills', 's0', ops)
    for n in range(6):
        ops = ['poke b 0x4088 1']
        for _ in range(6):
            k = rng.randint(3, 7)
            pts = []
            for _ in range(k):
                pts += [rng.randint(-40, 360), rng.randint(-20, 120)]
            ops.append('pokes %d %s' % (SCRATCH, struct.pack('<%dh' % len(pts), *pts).hex()))
            ops.append('poke w 0x2293 319')
            ops.append('poke w 0x2295 111')
            ops.append('call poly_fill_far 0x3FE4 %d %d %d' % (k, SCRATCH, rng.randint(0, 255)))
            ops.append('call tables_clear')
        T.add('polys%d' % n, 's0', ops)


def fam_text(T, rng):
    ops = ['poke w 0x365E 1', 'poke b 0x4088 1']
    for t in ('Hello 123', 'AIRSPEED', 'low lower 0', 'a'):
        for x, y in ((10, 10), (300, 80), (-5, 40), (160, 195)):
            ops.append('pokes %d %s' % (SCRATCH, hexstr(t)))
            ops.append('call hud_text %d %d %d %d' % (SCRATCH, x, y, rng.randint(1, 255)))
    for v in (0, 5, -7, 123, 32767, -32768):
        ops.append('call hud_num_left %d %d %d %d' % (v, rng.randint(0, 250), rng.randint(0, 150), rng.randint(1, 255)))
    for v in (0, 5, 123, 65535, 40000):
        ops.append('call hud_num_right %d %d %d %d' % (v, rng.randint(80, 300), rng.randint(0, 150), rng.randint(1, 255)))
    ops.append('poke w 0x3660 4')
    ops.append('pokes %d %s' % (SCRATCH, hexstr('Waypoint 2 reached')))
    ops.append('call msg_post %d' % SCRATCH)
    for mode in (0, 1, 2, 3):
        ops.append('pokes %d %s' % (SCRATCH, hexstr('NAV test')))
        ops.append('call mfd_text %d 2 3 %d 15' % (mode, SCRATCH))
    T.add('text', 's0', ops)
    ops = ['poke w 0x365E 1']
    for ctx in (0x3FCC, 0x3FE4, 0x3FFC):
        ops.append('pokes %d %s' % (SCRATCH, hexstr('Direct txt')))
        ops.append('call txt_draw %d %d 20 30 11' % (ctx, SCRATCH))
    T.add('txt', 's0', ops)


def fam_misc(T, rng):
    for night in (0, 1):
        for dbg in (0, 1):
            for clim in (2, 6):
                ops = ['poke w 0x3658 %d' % night, 'poke w 0x43AE %d' % dbg, 'poke b 0xD47 %d' % clim, 'poke w 0x365E %d' % rng.randint(0, 1), 'call pal_flight_load']
                T.add('palflight_%d%d%d' % (night, dbg, clim), 's0', ops)
    T.add('palsave', 's0', ['call pal_save', 'call pal_flight_load', 'call pal_restore'])
    for n in range(4):
        data = bytes(rng.randint(0, 63) for _ in range(60)).hex()
        data2 = bytes(rng.randint(0, 63) for _ in range(60)).hex()
        T.add('fade%d' % n, 's0', ['pokes %d %s' % (SCRATCH, data), 'pokes %d %s' % (SCRATCH + 100, data2),
                                  'call pal_fade_step %d %d 20' % (SCRATCH, SCRATCH + 100), 'call pal_fade_step %d %d 20' % (SCRATCH, SCRATCH + 100)])
    for mode in (0, 1):
        for panel in (0, 1):
            for sel in (0, 1, 2, 3):
                ops = ['poke w 0x463E %d' % panel, 'poke b 0x294B %d' % mode, 'poke w 0x46C2 %d' % sel, 'poke w 0x46C4 %d' % rng.randint(0, 3), 'call hud_side_strips 1']
                T.add('strips_%d%d%d' % (mode, panel, sel), 's0', ops)
    for day in (0, 1):
        for style in (0, 1):
            T.add('hudinit_%d%d' % (day, style), 's0', ['poke w 0x43B0 %d' % day, 'poke b 0x294B %d' % style, 'call hud_color_init'])


ARG_FIXUP = {'pal_fade_step': lambda a, ds: [a[0], ds, a[1], ds, a[2]]}        # far pointers of the original take two words

FAMILIES = [('math', fam_math), ('lamps', fam_lamps), ('digits', fam_digits), ('lines', fam_lines), ('clipdeg', fam_clipdeg), ('fills', fam_fills), ('text', fam_text), ('misc', fam_misc)]

# functions whose result is part of the comparison (simtest.c marks the same ones with ER())
RET_FUNCS = {'input_button', 'mfd_map_sx', 'mfd_map_sy', 'mfd_map_plot', 'mfd_map_to_screen', 'mfd_map_sx', 'mfd_map_sy', 'mfd_map_plot', 'mfd_map_to_screen', 'fx_mul15', 'fx_sin16', 'fx_cos16', 'fx_clamp', 'fx_clamp_wrap', 'fx_dist', 'fx_heading', 'fx_sin_mul', 'fx_cos_mul', 'fx_sext8', 'fx_isqrt',
             'fx_asin', 'fx_acos', 'fx_ratio_q15', 'lib_scale_eighths', 'lib_sign', 'pal_fade_step',
             'wpn_seeker_check', 'wpn_suit', 'obj_find_at', 'obj_cls_ship', 'wpn_site_dist', 'wpn_unit_dist', 'fm_bomb_dive_angle', 'dmg_wobj_destroyable', 'mission_target_done',
             'terr_find_scenery_at'}


import sim_scn_mfd
FAMILIES.append(('mfd', sim_scn_mfd.fam_mfd))

import sim_scn_step
FAMILIES.append(('small', sim_scn_step.fam_small))
FAMILIES.append(('keys', sim_scn_step.fam_keys))
FAMILIES.append(('step', sim_scn_step.fam_step))

import sim_scn_hud
FAMILIES.append(('hud', sim_scn_hud.fam_hud))
FAMILIES.append(('hudfront', sim_scn_hud.fam_hudfront))
FAMILIES.append(('view', sim_scn_hud.fam_view))
FAMILIES.append(('frames', sim_scn_step.fam_frames))
FAMILIES.append(('objovl', sim_scn_hud.fam_objovl))
FAMILIES.append(('objovp', sim_scn_hud.fam_objovp))
FAMILIES.append(('mfdview', sim_scn_hud.fam_mfdview))
FAMILIES.append(('polysingle', sim_scn_hud.fam_polysingle))

import sim_scn_flight
FAMILIES.append(('flight', sim_scn_flight.fam_flight))

import sim_scn_combat
FAMILIES.append(("combat", sim_scn_combat.fam_combat))
FAMILIES.append(("engage", sim_scn_combat.fam_engage))
