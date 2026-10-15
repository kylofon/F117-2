"""S5.7: the combat scenario families (see sim_scn.py): detection and SAM launches, enemy missiles in flight with countermeasures, fighters (hunt, combat table,
cannon, radar, AAMs, formation, landing), parked units taking off, the player's stores and gun, damage and the scenery bookkeeping, the export to END.
All start from snapshot s1 (a running mission); the world positions are read from that snapshot."""
import os

from sim_scn_flight import FRAME, PILOT, stick

SITE, UNIT, PROJ = 0xB2A2, 0xC13E, 0x3C0C
RADAR_SITES = list(range(3, 15)) + [15, 17, 22, 23]       # the sites of snapshot s1 with a radar (+6 != 0)
CM_KEYS = [0x0231, 0x0332, 0x0433, 0x0534, 0x0635]


def near(rng, x, y, r):
    return (x + rng.randint(-r, r)) & 0xFFFF, (y + rng.randint(-r, r)) & 0xFFFF


def at(px, py, alt, rng):
    """the player at (px, py), airborne at `alt`"""
    a = alt if alt < 0x2000 else 0x2000 + ((alt - 0x2000) >> 1)
    return ['poke l 0xBA10 %d' % (px << 5), 'poke l 0xC02A %d' % (((0x8000 - py) << 5) & 0xFFFFFFFF), 'poke w 0xC0A4 %d' % px, 'poke w 0xC0B2 %d' % py,
            'poke w 0x2DF6 %d' % alt, 'poke w 0x2DF4 %d' % a, 'poke w 0xC0A2 0', 'poke w 0xB052 %d' % rng.randint(6000, 14000), 'poke w 0xB170 80',
            'poke w 0x2DFE 80', 'poke w 0x2DF0 %d' % rng.randint(-800, 800), 'poke w 0x2DF2 0', 'poke w 0x2DEE %d' % rng.randint(0, 65535), 'poke b 0x2DFB 1',
            'poke w 0x3634 5000', 'poke w 0x365E 1', 'poke b 0x4088 1']


def world(T):
    mem = T.snaps['s1'][0]
    meta = open(os.path.join(T.work, 's1.meta')).read().split()
    ds = int(meta[1], 16) * 16
    return lambda o: mem[ds + o] | (mem[ds + o + 1] << 8)


def fam_combat(T, rng):
    w = world(T)
    sxy = lambda i: (w(SITE + 16 * i + 2), w(SITE + 16 * i + 4))
    # detection: near a radar, the scheduler about to scan, the levels, ECM, bay doors; SAM launches
    for n in range(24):
        s = rng.choice(RADAR_SITES)
        x, y = near(rng, *sxy(s), 0x900)
        ops = at(x, y, rng.choice([150, 600, 3000, 9000]), rng) + [
            'pokec %d %d' % (PILOT + 0x40, rng.randint(0, 2)), 'poke w 0x3D6A 0xFFFF', 'poke w 0x9B04 %d' % rng.choice([1, 5, 0x15, 0x25, 4]), 'poke w 0xB054 0',
            'poke w 0x98F2 %d' % rng.randint(0x400, 0x900), 'poke w 0x3D62 %d' % rng.choice([0, 0, 500, 2000]), 'poke w 0x3654 %d' % rng.randint(0, 3),
            'poke w 0x3656 %d' % rng.randint(0, 2), 'poke w 0xC600 %d' % rng.choice([0, 0xFF]), 'poke w 0xDEDA %d' % rng.randint(0, 1),
            'poke w 0xDFDC %d' % rng.choice([0x13, 0x14, 0x15])]
        for k in RADAR_SITES:
            ops += ['poke w 0x%X %d' % (SITE + 16 * k + 12, rng.randint(1, 8)), 'poke w 0x%X %d' % (SITE + 16 * k + 10, rng.choice([0, 0x40, 0xC8, 0xFC]))]
        ops += ['call det_site_radar_update %d' % s, 'call hud_threat_update']
        for f in range(rng.randint(10, 30)):
            if rng.random() < 0.15:
                ops += ['key %d' % rng.choice(CM_KEYS)]
            ops += FRAME
        T.add('det%d' % n, 's1', ops)
    # enemy missiles in flight at the player: homing, flares / chaff / decoy / jammers, the proximity burst, the damage lamps
    for n in range(24):
        px, py = rng.randint(0x2000, 0x6000), rng.randint(0x2000, 0x6000)
        alt = rng.choice([300, 2000, 6000])
        ops = at(px, py, alt, rng) + ['poke w 0x3654 %d' % rng.randint(0, 3), 'poke b 0x9B05 %d' % rng.choice([0, 0, 0x10]), 'poke w 0x3632 %d' % rng.choice([0, 0, 1]),
                                      'poke w 0x9B04 %d' % rng.choice([1, 0x11, 0x21, 1]), 'poke w 0x3D62 %d' % rng.choice([0, 3000])]
        for k in range(rng.randint(1, 3)):
            m = PROJ + 0x1C * rng.randint(0, 7)
            mx, my = near(rng, px, py, 0x300)
            typ = rng.choice([0xC, 0xD, 0xE, 0xF, 0x1B, 0x21, 0x11, 0x13])
            ops += ['poke w 0x%X %d' % (m, mx), 'poke w 0x%X %d' % (m + 2, my), 'poke w 0x%X %d' % (m + 4, rng.randint(0, 2 * alt)),
                    'poke w 0x%X %d' % (m + 6, rng.randint(8, 50)), 'poke w 0x%X %d' % (m + 8, rng.randint(0, 65535)), 'poke w 0x%X 0' % (m + 0xA),
                    'poke w 0x%X %d' % (m + 0xE, rng.randint(40, 300)), 'poke w 0x%X %d' % (m + 0x10, typ), 'poke w 0x%X %d' % (m + 0x16, rng.choice([3, 5, 0, 0xFFFF]))]
        for f in range(rng.randint(15, 40)):
            if rng.random() < 0.2:
                ops += ['key %d' % rng.choice(CM_KEYS)]
            ops += FRAME
        T.add('missile%d' % n, 's1', ops)
    # fighters: hunting with a fresh alert, the combat table and the cannon, their radar and AAM launches, formation, RTB, landing
    for n in range(24):
        px, py = rng.randint(0x2000, 0x6000), rng.randint(0x2000, 0x6000)
        alt = rng.choice([500, 3000, 8000])
        ops = at(px, py, alt, rng) + ['poke w 0x3D62 %d' % rng.choice([3000, 3000, 500, 0]), 'poke w 0xB2A0 %d' % px, 'poke w 0xB75A %d' % py, 'poke w 0xB76E %d' % alt,
                                      'poke w 0x3654 %d' % rng.randint(0, 3), 'poke w 0x3D6E %d' % rng.choice([0xFFFF, 1, 4]), 'poke w 0x3D60 %d' % rng.randint(0, 0xFFF)]
        for u in rng.sample([1, 4, 5, 8, 9], rng.randint(1, 3)):
            r = UNIT + 0x24 * u
            ux, uy = near(rng, px, py, rng.choice([0x100, 0x300, 0x1000]))
            ops += ['poke w 0x%X %d' % (r + 2, ux), 'poke w 0x%X %d' % (r + 4, uy), 'poke l 0x%X %d' % (r + 8, ux << 5), 'poke l 0x%X %d' % (r + 0xC, uy << 5),
                    'poke w 0x%X %d' % (r + 6, alt + rng.randint(-300, 300)), 'poke w 0x%X %d' % (r + 0x10, rng.randint(0, 65535)),
                    'poke w 0x%X %d' % (r + 0x18, rng.choice([0x483, 0x403, 0x487, 0x4C3, 0x683, 0x283])), 'poke w 0x%X %d' % (r + 0x1A, rng.randint(200, 600)),
                    'poke w 0x%X %d' % (r + 0x1C, rng.choice([2, 500, 4000])), 'poke w 0x%X %d' % (r + 0x22, rng.choice([0, 0xB0, 0xF0]))]
        for f in range(rng.randint(10, 30)):
            ops += stick(rng) + FRAME
        T.add('fighter%d' % n, 's1', ops)
    # parked units: take-off on the airbases, the scramble with an alert
    for n in range(6):
        ops = ['poke w 0x3D62 3000', 'poke w 0xB2A0 %d' % rng.randint(0x2000, 0x6000), 'poke w 0xB75A %d' % rng.randint(0x2000, 0x6000),
               'poke w 0xB16E %d' % rng.choice([1, 0xFFFF])]
        for u in (2, 3, 6, 7):
            ops += ['call ai_unit_takeoff %d %d' % (u, rng.choice([24, 25, 26, 30, 31]))]
        ops += ['poke w 0x98E2 %d' % rng.randint(0, 4000)] + ['call ai_update_units'] * 12 + FRAME * 10
        T.add('takeoff%d' % n, 's1', ops)
    # the player's stores: release over targets (bombs, guided weapons on the designated target, missiles), impacts, objects destroyed, the gun
    for n in range(30):
        tgt = rng.randint(1, 41)
        x, y = near(rng, *sxy(tgt), 0x200)
        ops = at(x, y, rng.choice([200, 800, 2000]), rng) + [
            'poke w 0x9B04 %d' % rng.choice([5, 5, 1, 0x2005]), 'poke w 0x364E %d' % rng.randint(0, 3), 'poke w 0x3D66 %d' % rng.choice([tgt, tgt, 0xFFFF]),
            'poke w 0xE55C %d' % rng.randint(0, 1), 'poke w 0x3654 %d' % rng.randint(0, 3), 'poke w 0xE2FE %d' % rng.randint(0, 2),
            'poke w 0xDFDC %d' % rng.choice([0x13, 0x14, 0x15])]
        ops += ['call wpn_release'] + FRAME * 3 + ['call wpn_release']
        if n % 5 == 0:                                                   # an air-to-air missile next to a unit
            u = rng.choice([1, 4, 5])
            p = PROJ + 0x1C * 8
            ops += ['poke w 0x%X %d' % (p, (w(UNIT + 0x24 * u + 2) - 0x40) & 0xFFFF), 'poke w 0x%X %d' % (p + 2, w(UNIT + 0x24 * u + 4)),
                    'poke w 0x%X %d' % (p + 4, w(UNIT + 0x24 * u + 6)), 'poke w 0x%X 40' % (p + 6), 'poke w 0x%X 0x4000' % (p + 8),
                    'poke w 0x%X 100' % (p + 0xE), 'poke w 0x%X %d' % (p + 0x10, rng.choice([0x16, 0x17, 0x18]))]
        if n % 3 == 0:
            ops += ['poke w 0x43BE 1']                                   # the gun button held
        for f in range(rng.randint(20, 50)):
            ops += FRAME
        ops += ['call mission_end %d' % rng.choice([0, 7]), 'call mission_world_export']
        T.add('release%d' % n, 's1', ops)
    # damage and the bookkeeping called directly; the scenery lookup and state change; the export to END
    for n in range(12):
        ops = ['poke w 0x3654 %d' % rng.randint(0, 3), 'poke b 0x9B05 %d' % rng.choice([0, 0, 0x10]), 'poke w 0x365E 1', 'poke w 0xDFDC %d' % rng.choice([0x15, 0x16]),
               'poke w 0xE2D8 %d' % rng.choice([0, 2, 3, 4, 5]), 'poke w 0xE2DA %d' % rng.randint(1, 20), 'poke w 0xE2EA %d' % rng.choice([2, 4]),
               'poke w 0xE2EC %d' % rng.randint(1, 20)]
        for k in range(6):
            o = rng.randint(0, 41)
            x, y = sxy(o)
            ops += ['call dmg_player_blast', 'call dmg_unit_kill %d' % rng.randint(0, 11), 'call obj_find_at %d %d' % (x, y), 'call dmg_object_destroy %d' % o,
                    'call mission_target_done %d' % rng.randint(0, 1), 'call dmg_wobj_destroyable %d' % o]
            X, Y = (x << 5) & 0xFFFFFFFF, ((0x8000 - y) << 5) & 0xFFFFFFFF
            ops += ['call terr_find_scenery_at %d %d %d %d' % (X & 0xFFFF, X >> 16, Y & 0xFFFF, Y >> 16)]
            if rng.random() < 0.5:
                ops += ['call terr_set_scenery_state 0xBA14 %d' % rng.choice([0x10, 0x81, w(0xDEA4) & 0xFF, w(0xC0AC) & 0xFF])]
        ops += ['call mission_end %d' % rng.choice([0, 1, 5, 7]), 'call mission_world_export']
        T.add('dmg%d' % n, 's1', ops)


def fam_engage(T, rng):
    """targeted engagements: SAM launches, fighter cannon and AAM launches, the gun, unregistered scenery destroyed"""
    import sys
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 's36'))
    from r2_ref import heading
    w = world(T)
    sxy = lambda i: (w(SITE + 16 * i + 2), w(SITE + 16 * i + 4))
    for n in range(16):                                                 # SAM: Radar ID first, then the launch, then the missile flies at the player
        s = rng.choice(RADAR_SITES)
        sx, sy = sxy(s)
        x, y = (sx + rng.choice([-1, 1]) * rng.randint(0x100, 0x400)) & 0xFFFF, (sy + rng.choice([-1, 1]) * rng.randint(0x100, 0x400)) & 0xFFFF
        ops = at(x, y, rng.choice([100, 300, 800]), rng) + ['poke w 0x3D5C 95', 'poke w 0x9B04 5', 'poke w 0x%X 252' % (SITE + 16 * s + 10),
                                                         'poke b 0x%X %d' % (SITE + 16 * s + 9, rng.choice([0, 0x10])), 'poke w 0xB054 0', 'poke w 0x98F2 0x800',
                                                         'poke w 0x3654 %d' % rng.randint(1, 3), 'poke w 0x%X 0' % (PROJ + 0x1C * (s & 7) + 0xE)]
        ops += ['call det_site_radar_update %d' % s] * 3
        for f in range(rng.randint(20, 40)):
            if rng.random() < 0.1:
                ops += ['key %d' % rng.choice(CM_KEYS)]
            ops += FRAME
        T.add('sam%d' % n, 's1', ops)
    for n in range(16):                                                 # fighters at close range: the radar, the AAM, the cannon
        px, py = rng.randint(0x2000, 0x6000), rng.randint(0x2000, 0x6000)
        alt = rng.choice([800, 3000])
        u = rng.choice([1, 4, 5])
        r = UNIT + 0x24 * u
        d = rng.choice([0x80, 0x150, 0x300, 0x500])
        ux, uy = (px + rng.randint(-d, d)) & 0xFFFF, (py + rng.randint(-d, d)) & 0xFFFF
        hdg = heading((px - ux) & 0xFFFF, (uy - py) & 0xFFFF)
        ops = at(px, py, alt, rng) + ['poke w 0x3D62 3000', 'poke w 0xB2A0 %d' % px, 'poke w 0xB75A %d' % py, 'poke w 0xB76E %d' % alt, 'poke w 0x3654 %d' % rng.randint(0, 3),
                                      'poke w 0x3D5C 95', 'poke w 0xB054 0', 'poke w 0x94F0 %d' % w(r + 0x16),
                                      'poke w 0x%X %d' % (r + 2, ux), 'poke w 0x%X %d' % (r + 4, uy), 'poke l 0x%X %d' % (r + 8, ux << 5), 'poke l 0x%X %d' % (r + 0xC, uy << 5),
                                      'poke w 0x%X %d' % (r + 6, alt), 'poke w 0x%X %d' % (r + 0x10, hdg), 'poke w 0x%X 0' % (r + 0x12), 'poke w 0x%X 0' % (r + 0x14),
                                      'poke w 0x%X 0x483' % (r + 0x18), 'poke w 0x%X 400' % (r + 0x1A), 'poke w 0x%X 3000' % (r + 0x1C), 'poke w 0x%X 0xF0' % (r + 0x22),
                                      'poke w 0x%X 0' % (PROJ + 0x1C * (u & 7) + 0xE)]
        ops += ['call ai_unit_radar %d' % u, 'call ai_unit_radar %d' % u]
        for f in range(rng.randint(12, 30)):
            ops += FRAME
        T.add('engage%d' % n, 's1', ops)
    for n in range(6):                                                  # the gun: button 0 on odd steps, rounds moving
        ops = at(0x4000, 0x4000, 1000, rng) + ['pokec %d 0' % (PILOT + 0x42), 'poke w 0x3650 %d' % rng.choice([650, 30])]
        for f in range(8):
            ops += ['poke w 0x3D60 %d' % (2 * f + 1), 'poke w 0x43BE 1', 'call wpn_gun_update', 'call wpn_gun_update'] + FRAME
        T.add('gun%d' % n, 's1', ops)
    for n in range(8):                                                  # scenery that is not a registered site: the scratch record 0, its wreck, the kill table
        ops = ['poke w 0x3654 %d' % rng.randint(0, 3), 'poke w 0x365E 1', 'poke w 0x3D76 %d' % rng.randint(0, 1)]
        for k in range(8):
            x, y = rng.randint(0x800, 0x7800), rng.randint(0x800, 0x7800)
            ops += ['call obj_find_at %d %d' % (x, y), 'call dmg_wobj_destroyable 0', 'call dmg_object_destroy 0']
        T.add('scenery%d' % n, 's1', ops)
