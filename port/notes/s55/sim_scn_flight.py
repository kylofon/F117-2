"""S5.7: scenario families of the flight module (flight model 1E9E, weapons, AI, detection, damage; see sim_scn.py).
Multi-frame flights from snapshot s1 (a running mission): every frame is 1E9E (`sim_input_flight`, with scripted BIOS keys and stick bytes) and the sim
step 39C4, which runs the radars, the fighters, the projectiles, the countermeasures, the gun and cba0 (fire, release, countermeasures, eject)."""
import random

from sim_scn_step import state_pokes, KEYS

PILOT = 0x1272                       # pilot record in the comm block (DS:98F4 -> comm+1272h)
FRAME = ['call sim_input_flight', 'call sim_frame_step']
FM_KEYS = [0x011B, 0x0B30, 0x0C2D, 0x0C5F, 0x0D2B, 0x0D3D, 0x2E63]
COCKPIT_KEYS = [0x0231, 0x0332, 0x0433, 0x0534, 0x0635, 0x0736, 0x0837, 0x0938, 0x0A39, 0x1C0D, 0x0E08, 0x3920, 0x3C00, 0x3062]


def stick(rng):
    return ['poke b 0x2CD6 %d' % rng.choice([0x80, 0x80, 0x26, 0xDA, 0x01, 0xFF, rng.randint(0, 255)]),
            'poke b 0x2CD7 %d' % rng.choice([0x80, 0x80, 0x26, 0xDA, 0x01, 0xFF, rng.randint(0, 255)])]


def air(rng, alt=None):
    """airborne at a plausible speed and attitude"""
    a = alt if alt is not None else rng.randint(800, 20000)
    return ['poke w 0x2DF6 %d' % a, 'poke w 0x2DF4 %d' % (a if a < 0x2000 else 0x2000 + ((a - 0x2000) >> 1)), 'poke w 0xC0A2 0',
            'poke w 0xB052 %d' % rng.randint(6000, 16000), 'poke w 0xB170 %d' % rng.randint(40, 100), 'poke w 0x2DFE %d' % rng.randint(40, 100),
            'poke w 0x2DF0 %d' % rng.randint(-2000, 2000), 'poke w 0x2DF2 %d' % rng.choice([0, rng.randint(-0x3000, 0x3000)]),
            'poke w 0x9B04 %d' % rng.choice([1, 1, 5, 0x11, 0x21, 3, 0]), 'poke w 0x3634 %d' % rng.randint(500, 9000), 'poke b 0x2DFB 1']


def fam_flight(T, rng):
    # take-off: full throttle on the runway, rotate, gear up, climb, turn, level off; then the mission end and the export
    ops = ['poke w 0x365E 1', 'pokec %d 1' % (PILOT + 0x40), 'key 0x0D2B'] + FRAME
    for f in range(70):
        if f == 40:
            ops += ['poke b 0x2CD7 0xDA']
        if f == 55:
            ops += ['key 0x0736', 'poke b 0x2CD7 0x80']
        ops += FRAME
    ops += ['poke b 0x2CD6 0x26'] + FRAME * 12 + ['poke b 0x2CD6 0x80', 'poke b 0x2CD7 0x26'] + FRAME * 8 + ['poke b 0x2CD7 0x80'] + FRAME * 6
    ops += ['call mission_end 7', 'call mission_world_export']
    T.add('fm_takeoff', 's1', ops)
    # keyboard sensitivity, autopilot, stall, buffet, damage, crash mode
    for n in range(36):
        ops = state_pokes(rng) + air(rng) + ['pokec %d %d' % (PILOT + 0x40, rng.randint(0, 2)), 'poke w 0x43BA %d' % rng.randint(0, 1),
                                             'poke w 0x3632 %d' % rng.choice([0, 2, 4, 0x20, 0x26, 0x7F]), 'poke w 0xC5C8 %d' % rng.randint(0, 12),
                                             'poke w 0x3652 %d' % rng.choice([0, 0, 600, 3000]), 'poke w 0xC078 %d' % rng.randint(0, 65535),
                                             'poke w 0xE2FE %d' % rng.randint(0, 2), 'poke w 0x3D64 %d' % rng.choice([0xFFFF, 0, 1]), 'poke w 0x98E0 %d' % rng.choice([0, 0, 0, 1])]
        if n % 6 == 5:
            ops += ['poke w 0xB052 %d' % rng.randint(0, 3000)]           # below the target speed: the stall nose drop
        for f in range(rng.randint(6, 18)):
            ops += stick(rng)
            if rng.random() < 0.3:
                ops += ['key %d' % rng.choice(FM_KEYS + COCKPIT_KEYS)]
            ops += FRAME
        T.add('fm_air%d' % n, 's1', ops)
    # landing approaches: touch-down in the three crash modes, gear up / down, banked, on a deck
    for n in range(18):
        cm = n % 3
        ops = state_pokes(rng) + air(rng, rng.randint(40, 300)) + ['pokec %d %d' % (PILOT + 0x40, cm), 'poke w 0x3D74 1', 'poke w 0x2DF0 %d' % rng.randint(-1500, 200),
                                                                    'poke w 0x9B04 %d' % rng.choice([0, 0, 1, 8]), 'poke w 0xDFE0 %d' % rng.randint(0, 3),
                                                                    'poke w 0xC0A2 %d' % rng.choice([0, 0, 0x80]), 'poke w 0x2DF2 %d' % rng.choice([0, 0, 0x800, -0x2000])]
        for f in range(rng.randint(10, 30)):
            ops += FRAME
        ops += ['key 0x0B30'] + FRAME * 4 + ['call mission_world_export']
        T.add('fm_land%d' % n, 's1', ops)
    # on the ground: brakes, throttle, the catapult shot on a deck, nose-wheel steering
    for n in range(8):
        ops = state_pokes(rng) + ['poke w 0x2DF6 %d' % (0x80 if n & 1 else 0), 'poke w 0x2DF4 %d' % (0x80 if n & 1 else 0), 'poke w 0xC0A2 %d' % (0x80 if n & 1 else 0),
                                  'poke w 0xB052 %d' % rng.randint(0, 3000), 'poke w 0x9B04 8', 'poke w 0x2DF0 0', 'poke w 0x2DF2 0', 'key 0x0D2B'] + FRAME
        ops += ['key 0x0B30'] + FRAME
        for f in range(12):
            ops += stick(rng) + FRAME
        T.add('fm_ground%d' % n, 's1', ops)
    # ejection: Shift-F10, the tumbling aircraft, the pilot's height, Esc, the end of the sequence
    for n in range(6):
        ops = state_pokes(rng) + air(rng) + ['poke w 0x98E2 %d' % (8 * rng.randint(0, 100)), 'key 0x5D00'] + FRAME
        for f in range(rng.randint(30, 60)):
            if rng.random() < 0.05:
                ops += ['key 0x011B']
            ops += FRAME
        ops += ['call mission_world_export']
        T.add('fm_eject%d' % n, 's1', ops)
    # into the sea (2026-10-16, the user's crash off Tripoli): a dive outside any airfield zone down to sea level (AGL 0), crash mode Easy /
    # Realistic, the stick held forward: 39C4 ends the mission with code 1 (stranded at AGL 0 with crashes on), comm+28h = 1; Esc in the BIOS keys is
    # harmless in flight
    rng = random.Random(20261016)                                       # its own generator: the families after this one keep their scenarios
    for n in range(6):
        ops = state_pokes(rng) + air(rng, rng.randint(150, 900)) + ['pokec %d %d' % (PILOT + 0x40, 1 + n % 2), 'poke w 0x3D74 0', 'poke w 0x9B04 1',
                                                                   'poke w 0x2DF0 %d' % rng.randint(-0x3000, -0x1800), 'poke w 0xB052 %d' % rng.randint(9000, 16000),
                                                                   'poke w 0xC06E 0', 'poke w 0x3634 5000']
        for f in range(60):
            if f in (3, 12):
                ops += ['key 0x011B']
            ops += ['poke b 0x2CD6 0x80', 'poke b 0x2CD7 0x26'] + FRAME           # the stick held forward
        ops += ['call mission_world_export']
        T.add('fm_sea%d' % n, 's1', ops)
