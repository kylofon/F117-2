# Flight model, controls, weapons, AI, detection, damage: VGAME.EXE (S3.8)

**Status: S3.8 delivered** (2026-10-13). Symbols: `flight_symbols.csv` (42 functions, 2 labels, 90 data symbols; owners `flight`, `detect`, `ai`, `weapons`,
`damage`; their rows were moved out of `simflow_symbols.csv`). Coverage: `python tools/spec_coverage.py vgame` = 450/450.
Disassembly: `python tools/dsann.py VGAME 1e23 FROM TO`; xrefs `python tools/vgxref.py g|f ...`. DGROUP = 1E23,
`DS:oooo` = unpacked image offset `0x1E230 + oooo` (file offset `0x1EA50 + oooo` in `work/unpacked/VGAME.EXE`).
`(V)` = verified in the disassembly, `(T)` = differential-tested against the real code under Unicorn (section 9), `(U)` = needs a runtime check.

How this document relates to `simflow.md` (S3.6): the S3.6 readers already specified most of these functions in its Parts 2, 4, 5, 6, 9 and 10.
This spec **owns** them now: where a simflow section is right (verified here, mostly by differential tests) it is pointed to and not copied;
where it was index-level (`5730`, `0D08`, `0F31`) or wrong, the full text is here. **Where simflow.md and this file disagree, this file is right**
(every such point is listed in section 8). Conventions shared with all modules: `simflow.md` Part 0 section 3.

## 0 Map

| section | content | functions (owner) |
|---|---|---|
| 1 | conventions, units, helper semantics, the `sim_rate` scaling table (timing decision) | - |
| 2 | player flight model and controls | `1e9e 2c6e 2c9e 2e43 e578 bbed cb72 1181:0008/0058/0072 10a8:0c87/0c95/0ca8` (flight) |
| 3 | radar detection and stealth signature, alert levels, SAM launch | `43ee 4f2a 5460 516d 5588 559a` (detect) |
| 4 | enemy aircraft AI (full) | `5730 62d0 65d4` (ai) |
| 5 | weapons, countermeasures, gun, targeting leaf code | `4512 461c 465b 671a 711f 753f 8bab bad1 bb86 bb9e bbb4 bc77 bcac` (weapons) |
| 6 | damage to the player and to targets, crash and eject, scenery damage | `71b4 72a4 7470 74f6 780a 0d08 0f31` (damage) |
| 7 | step order and who reads whom | - |
| 8 | corrections to simflow.md / render3d.md | - |
| 9 | tests (Python references, harness, results) | `port/notes/s38/` |
| 10 | original quirks and bugs to keep | - |
| 11 | open items (U) | - |
| 12 | function index (generated) | all 42 |

Code that implements part of this subject but stays where it is (it is interleaved with drawing, keep the write order):
`a17e` S2 cannon-round hit tests and S4 weapon lock (`simflow.md` Part 9 sections 1.2, 1.4), `9ACA` ground/air target designators
(`render3d.md` Part 2 "0000:9ACA"), `cba0` key handlers (`simflow.md` Part 10 section 1), `39c4` airfield/landing/terrain end checks
(`simflow.md` Part 4 sections 3.1-3.3), `4926 mission_end` (Part 4 section 4).

## 1 Conventions

### 1.1 Coordinates, units

* **Level units** (16 bit): player `px = [C0A4]`, `py = [C0B2]`; site, unit, projectile, decoy and tracer `x, y` are the same units.
  x grows east, **y grows south**; the playable box is 0x100..0x7E00 x 0x200..0x7D00 (`39c4` step 3). Moving along heading `h` by `d`:
  `x += sin_mul(h, d); y -= cos_mul(h, d)`. The 32-bit player position (`[BA10:BA12]` east, `[C02A:C02C]` north) is level units << 5
  (`px = (BA10 + 16) >> 5`, `py = 0x8000 - ((C02A + 16) >> 5)`); units keep their own `<< 5` copy (`+8`, `+0xC`, y **not** mirrored).
  World-object (scenery) lookups take `(long)x << 5, (long)(0x8000 - y) << 5`.
* **Altitude**: the player's `alt = [2DF6]` (u16, 0..0xEA60 = 60000, feet) and the compressed `alt_s = [2DF4]` (`alt` below 0x2000, then
  slope 1/2 to 0x4000, then 1/4; simflow Part 2 section 2.9). **Unit `z`, projectile `z`, tracer `z`, decoys and every altitude compared with
  `[2DF4]` use the alt_s scale.** In slant/blast formulas z is divided by 32 to be added to a level-unit distance (`|dz| >> 5`), and the
  altitude cone of the radar uses `|dz| >> 10` against `D` (section 3.2): 1 level unit = 32 alt_s units.
* **Distances**: `fx_dist(dx, dy)` (`c987`) = `max + min/2` of the absolute values, capped at 0x7FFF, in level units ("raw").
  `D = fx_dist >> 6` ("distance units") is what radar ranges (`DS:2E80 +8`), weapon ranges (`DS:3362 +8`) and scramble radii use.
* **Angles**: u16, 0x10000 = 360 deg, heading 0 = north, 0x4000 = east. `fx_heading(x, y)` (`c9d6`) = angle of the vector (x east,
  y north), so the bearing from A to B is `fx_heading(B.x - A.x, A.y - B.y)`. Pitch positive = climb, bank positive = right wing down.
* **Unit record** (36 bytes, `DS:C13E + 0x24*u`, count `[DED0]`; START `D3BE`): `+0 site` (home/target/landing site index), `+2 x`, `+4 y`,
  `+6 z`, `+8 x32`, `+0xC y32` (level units << 5, s32), `+0x10 hdg`, `+0x12 pitch`, `+0x14 bank`, `+0x16 type` (row of the aircraft table
  `DS:2FC2`), `+0x18 fl` (low byte) / `+0x19 fl2` (high byte; word tests such as `fl & 0x140` use both), `+0x1A spd` (**speed**, not hit
  points: 0 = parked), `+0x1C endur` (steps until return to base), `+0x1E wpn` (projectile type it fires), `+0x20` (unused by the sim),
  `+0x22 level` (radar track level 0..255).
  `fl`: 0x01 exists, 0x02 airborne (also: candidate for player AAMs), 0x04 returning to base, 0x08 has the player on radar (lock tone given),
  0x10 a player missile is within 0x180 (cleared every step by 5730), 0x20 shot down / falling, 0x40 keep target (no re-targeting; leaves the
  hunt after 50 s), 0x80 scrambles on any alert. `fl2`: 0x01 formation member, 0x02 "on final" announced, 0x04 take-off roll, 0x08 visual ID
  done, 0x10 landed / rolling out, 0x20 terrain ahead (pull up), 0x40 its level passed 0xC0 once.
  Persistent mask after a reset: `fl & 0x1C1` (keeps 0x01, 0x40, 0x80 and formation).
* **Site record** (16 bytes, `DS:B2A2 + 16*i`, count `[C0B4]` incl. scratch record 0; `[E540]` first non-target site; `[98FC]` sites with a
  threat): `simflow.md` Part 5 section 6.2 (V). Field `+6` is the radar type (0 = none; also "alive objective" for HARM), `+8` flags, `+9`
  flags2, `+0xA` level (radar sites) / fighter type based here (airbases), `+0xC` scan countdown, `+0xE` object type (low 7 bits).
* **Projectile record** (28 bytes, `DS:3C0C + 0x1C*i`, 12 slots: 0..7 enemy missiles, 8..11 player stores): `simflow.md` Part 6 section 2.4 (V).
* **Aircraft table** `DS:2FC2`, 29 x 32 bytes: `+0 name[7]`, `+7 nickname[11]`, `+0x12 maxspd`, `+0x14 range`, `+0x16 agility`, `+0x18 radar
  type` (-1 none), `+0x1A` near 3D model id, `+0x1C` far 3D model id (used by 9ACA, `render3d.md` Part 2), `+0x1E` kill counter (dead data).
  Values: `simflow.md` Part 5 section 6.4.

### 1.2 Helper semantics (exact; Python in `port/notes/s36/r2_ref.py`)

`i16`/`u16` = truncate to 16 bits signed/unsigned. All arithmetic is 16-bit unless stated. `tdiv(a, b)` = C `/` (truncate toward zero;
`idiv`, `__aFldiv`); `a % b` from `idiv` has the sign of `a`. `>>` on signed values is arithmetic.
* `abs16(v)` (`f302`, RTL `abs`): `abs(-32768)` stays `-32768` (negative). Every "within the cone" test `abs16(a - b) < k` therefore
  **passes** for a difference of exactly 0x8000.
* `sx8(b)` = sign-extend a byte (`cbw`). `hi(v)` = `v >> 8` as a byte.
* `clamp_w(v, lo, hi)` (`c94e`): `if (hi < v) return hi; if (lo <= v) return v; return (v <= -0x4000) ? hi : lo;` (signed; the third rule is
  the wrap quirk: a value far below `lo` returns `hi`).
* `sin_mul(a, m)` (`caee`) = `mul15(m, sin16(a))`, `cos_mul(a, m)` (`cb07`) = `sin_mul(a + 0x4000, m)`; `mul15(a,b) = (a*b + 0x4000) >> 15`;
  `sin16` = interpolated 257-entry table (simflow Part 2 section 1).
* `rand_n(n)` (`cb62`) = `(rand() * n) >> 15` (32-bit product, arithmetic shift); `rand()` = MSC LCG `seed = seed*0x343FD + 0x269EC3`,
  result `(seed >> 16) & 0x7FFF`, seed = `DS:926E` (32 bit), seeded from the BIOS clock at mission start (`4857`).
* `skill = [3654]` (pilot+3Eh, enemy skill 0 Green .. 3 Elite), `war = [3656]` (pilot+3Ah, 0..2), `night = [3658]`,
  `model = pilot+42h` (0 "MicroProse F117A": gun, 4 bays; 1 "Lockheed F117A": no gun, 2 bays, half radar return).

### 1.3 Time base: what is scaled by `sim_rate` (timing decision of 2026-10-13)

The port emulates the original timing exactly (PLAN.md Decisions; `simflow.md` Part 0 D1): one step per rendered frame, `sim_rate = [3660]`
(4..15) re-estimated every `4*sim_rate` steps by the `39c4` estimator that assumes 60 ticks/s (`simflow.md` Part 4 section 1.2) although the
tick is the 70.09 Hz retrace. `sim_rate` is "steps per game second". `d707` derives `[B16C] = clamp_w(2*rate, 3, 16)` (player gun slots),
`[B286] = 250*rate` (alert duration), `[9F64] = 200*rate` (alert age threshold) and `[43B8]`. Time acceleration (`Z`, `[3D7A] = 2`) halves
`rate`; `d7bb` (`frame_rate_halve`) undoes it and is called when a radar sees the player (5460), on every store release (753f) and on `X`.

| quantity | scaling | where |
|---|---|---|
| every flight-model increment (rotations, speed, spool, brakes, position, vertical speed) | `x / rate` (`tdiv`) | 1e9e (simflow Part 2 sections 2.5-2.9) |
| fuel burn | once every `2*rate` steps | 1e9e |
| mission clock `[98E2]` | +1 every `rate` steps | 39c4 step 17 |
| unit bank, heading, pitch rates | `(rate_b << 2) / rate`, `(bank >> 3) / rate`, `(dp << 2) / rate` | 5730 section 4.2 |
| unit speed along the path | `((w << 2) / rate) >> 2` | 5730 |
| unit take-off acceleration, roll-out deceleration | `0x28 / rate`, `0x3C / rate` per step | 5730 |
| enemy cannon round speed | `0xE0 / rate` per step | 5730 |
| unit radar scan | once every `16*rate` steps per unit (staggered) | 5730 |
| unit endurance | `(range * rate << 11) / maxspd` steps | 65d4 |
| site radar scan countdown | `(rate << 8) / ((level >> 2) + 16) + i` steps | 4f2a |
| alert network timer `[3D62]` | set to `250*rate`, -1 per step; "fresh alert" = `> 200*rate` (first 50 s) | 559a, 39c4, 5730, 671a |
| SAM flight time | `(range * rate << 4) / 1` steps | 516d |
| enemy AAM flight time | `(range * rate << 5) / (speed64 >> 6)` steps | 62d0 |
| player store flight time | `(range * rate << 5) / ((speed64 >> 6) + 1) + 6` (999 if <= 6) | 753f |
| projectile turn, pitch, gravity, motion | `(dh << 2) / rate`, `(pe << 2) / rate`, `0x800 / rate`, `cos*spd*4 / rate`, `sin*(spd << 7) / rate` | 671a |
| proximity fuse distance | `(spd << 3) / rate` | 671a |
| burst life | `2*rate`; enemy missile detonates only while `life > 4*rate` | 671a |
| player gun: ammo per round, round speed, round slots | `-40 / rate`, `0x70 / rate`, `[B16C]` | 465b |
| flare/chaff/decoy life | `(9 - 2*skill) * rate` (decoy `<< 3`) steps, but see the serial countdown quirk (5.6) | 4512, 461c |
| lamp timers: radar warning, flare, chaff | `2*rate`, `3*rate`, `3*rate` | 5588, 4512 |
| IR jammer duty limit | on for at most `20*rate` steps | cba0 tail |
| messages | `3*rate` drawn frames | 8b93 |
| target-destroyed indications `[DED8]`, `[9D0A]` | `4*rate` | 72a4 |

**Not scaled** (per step, so their real-time speed follows the frame rate): projectile acceleration (+1 speed on odd steps), seeker and
countermeasure tests, the terrain probes (every 4th step per slot/unit), site/unit level build-up per scan (but the scans themselves are
`rate`-scaled), the unit combat table, falling-unit smoke (every 2nd step), the airfield scan (every 8th step), the explosion animation of
`a17e` (per rendered frame = per step).

## 2 Player flight model and controls

### 2.1 `0000:1e9e sim_input_flight` (owner flight) - specified in `simflow.md` Part 2 section 2 (T)

The S3.6 transcription is bit-exact: `python port/notes/s36/r2_test_fm.py 600` was re-run in S3.8: **0 mismatching of 600** (whole DGROUP,
ordered sound/lamp/far-call logs, keyboard and joystick stick, ground/air, crash sequence, every key of the function). Read Part 2 section 2
with the corrections of section 8 below (`sim_rate` is emulated, not fixed; `[E2FE]` = master mode; `[9B04]` bit names). Summary of the model
for orientation (all exact formulas are in Part 2):
* inputs: one BIOS key per frame (`[DE90]`, the rest is discarded), stick bytes from the joystick (`10a8:0ca8`) or the keypad hook (`[2CD6/7]`),
  autopilot altitude hold `[3652]`, throttle command `[2DFE]` 0..100;
* stick -> `roll_in` (curve `-2r(|r|+2)`) and `pitch_in` (`6p`, nose-down halved), dead zone, autopilot override, buffet (crash realism,
  gear down, flight-control damage), flare assist;
* engine: spool up `(cmd - thr)/4/rate` (min +1), spool down instant, engine damage caps the throttle at `(0x19 - hits) * 4`, fuel burn
  `cmd^2/1000 + 2 (+ hits with a fuel leak)` every `2*rate` steps;
* G load from the bank table `DS:2D14` plus pull, 8 G limit; speed target from thrust, density, fuel weight, induced drag, gear/`'9'` drag;
  lift `[98C6]`, turn rate `g*sin(bank)/V`; brakes; incremental rotation matrices (roll, pitch post-multiplied, yaw pre-multiplied),
  matrix -> Euler (`2c9e`), stall nose drop, ground constraints, re-orthogonalisation every 8 rotations (`2c6e`, `2e43`);
* position / altitude / touch-down: hard landing -> `4926(5)`; 16-entry history ring; cockpit sway in master mode 1.

### 2.2 Orientation helpers (owner flight) - (T in S3.6)

`2c6e fm_rotate_apply`, `2c9e fm_mat_to_euler`, `2e43 fm_mat_from_euler`: `simflow.md` Part 2 section 3. `e578 mat_mul` (3x3 Q15,
`C = A x B`, `mul15` products, 16-bit wrapping sums): bit-exact in `render3d.md` Part 1 (E578) and `r2_ref.mat_mul`; it lives in
`flight_symbols.csv` because the flight model is its main user.

### 2.3 `0000:bbed fm_bomb_dive_angle(void) -> u16` (owner flight) (V, used by 753f for free-fall bombs)
`return u16( tdiv( (long)i16(0x4000 - abs16([2DF0])) << 12, (long)(u16)([2DF4] + 0x1000) ) - 0x4000 )` (32-bit dividend, divisor zero-extended,
low word of the quotient). 753f stores `result - 0x400` as the bomb's minimum pitch (`proj +0x16`).

### 2.4 Controls

| input | path | spec |
|---|---|---|
| keypad / arrow keys as stick (no joystick, `comm[72h] & 1 == 0`) | INT 9 hook `1181:0072` -> `[2CD6]` X, `[2CD7]` Y bytes (0x80 centre, 0x80-+0x5A first press, 0x80-+0x7F second press within 5 BIOS ticks); scaled by `([43BA]+1)/3` in 1e9e (Insert = sensitivity 1/2/3) | `simflow.md` Part 1 section 7 (V), Part 2 section 2.2 (T) |
| joystick | `10a8:0ca8 joy_poll` -> raw counts -> `joy_scale` -> `[2CD2]`/`[2CD3]`; `10a8:0c87` re-centre (Alt-S, 40-frame debounce); calibration `DS:2CA2` = comm+48h | `simflow.md` Part 2 section 6 (V, scale formula T) |
| buttons | `cb72 input_button(n)` = `(comm[72h] & 1 ? joy_button(n) /*slot 93*/ : 0) + [43BE + 2n]`; latches `[43BE]` (Backspace, model 0 only) = gun, `[43C0]` (Enter) = weapon release; both cleared at the start of every `cba0` call | Part 2 section 1 (V), Part 10 section 1.1 |
| throttle, brakes/catapult, HUD style, quit/pause/boss, re-centre | keys of `1e9e` | Part 2 section 2.1 (T) |
| gear `6`, altitude hold `7`, bay doors `8`, devices `9`, flare `1`, chaff `2`, IR jammer `3`, ECM `4`, decoy `5`, weapon select Space, fire Enter, gun Backspace, target cycle `b`, master mode F2, eject Shift-F10 | `cba0` table A and its tail (bay door / gear animations, IR jammer duty cycle, fire debounce 4 steps) | Part 10 sections 1.1, 1.4 (V) |

`[9B04]` (word) bits as used by this module: 0x0001 gear up and locked, 0x0002 key `9` devices (flaps/air brake: -25 % speed reference,
-1/16 thrust, half buffet), 0x0004 bay doors fully open, 0x0008 wheel brakes, **0x0010 ECM jammer on (key `4`)**, **0x0020 IR jammer on
(key `3`, at most `20*rate` steps, then recharges)**, 0x2000/0x4000 secondary/primary objective done (the same bits are `[9B05]` 0x20/0x40),
0x1000 (`[9B05]` 0x10) training mode. (S3.6 parts call 0x10/0x20 "chaff/flare mode"; they are the two jammers: 0x10 adds 16 to the signature,
halves every radar's build-up and defeats radar seekers; 0x20 defeats IR seekers. Flares and chaff are the dropped stores of `4512`.)

## 3 Detection: stealth signature, radars, alert levels

### 3.1 `0000:43ee sim_key_and_signature(void)` (owner detect) (T)
Runs last in the step (`39c4` step 19), so the signature is used by the **next** step's `4f2a`/`5730`.
```c
cba0([DE90]);                                         /* key commands first (simflow Part 10) */
t = abs16(sx8(hi([2DF2])));  if (t >= 0x40) t = 0x80 - t;     /* bank in 1/256 turn folded to 0..0x40 (0x40 = 90 deg) */
if (model != 0) {                                     /* Lockheed */
    v = sx8(hi(fx_heading((skill + 1) << 7, 0x800)));        /* 2, 4, 7, 9 for skill 0..3 */
    sig = v + ([B052] >> 10) + (t >> 1);
    if ([9B04] == 0) sig += 0x14;                      /* WORD compare: gear down and every other bit clear (incl. 9B05) */
} else {                                              /* MicroProse */
    v = sx8(hi(fx_heading(u16((u16)[2DF4] / 3 * (skill + 1)), 0x800)));   /* unsigned div and mul; grows with altitude */
    sig = v + ([B052] >> 10) + (t >> 1);
}
if ([9B04] & 4)    sig += 0x18;                       /* bay doors open */
if ([9B04] & 0x10) sig += 0x10;                       /* ECM jammer emitting */
[3D5C] = sig;                                         /* 16-bit, no clamp */
```
Radar cross-section summary: speed (`[B052] >> 10`, raw speed = 27 per knot: +1 per ~38 kt), bank (+1 per 2/256 turn up to +32 at 90 deg),
bay doors +24, ECM +16, and in the MicroProse model altitude (`alt_s/3*(skill+1)` through an arctangent: 0 on the deck, at 60000 ft +53 (Green) .. +61 (Elite)).
**The landing gear has no effect in the MicroProse model**; in the Lockheed model +20 only when the whole `[9B04]` word is 0 (gear down,
no brakes, no jammers, no devices, no objective done, not training). The aspect towards each radar is added per radar in 5460.

### 3.2 `0000:5460 det_radar_signal(x, y, z, rtype, *bearing, *dist) -> int` (owner detect) (T)
`simflow.md` Part 5 section 1 is right, with these exact details (all T):
* step 7: `a = sx8(hi(abs16(bearing - [2DEE])))`; a difference of exactly 0x8000 gives `a = -128` (not folded; the signal becomes
  `((-128 + 0x20) >> 1) * (sig0 >> 1) >> 4`, i.e. negative for a positive `sig0`).
* step 8 (Doppler radars, flag bit 0): `f = u16((0x60 - a) * ([B052] >> 5)) >> 9` - the product is truncated to 16 bits (`mul`, low word)
  before the shift, so above raw speed ~21850 (~810 kt) the Doppler term wraps.
* step 5 divides with `__aFldiv` (truncate toward zero); `sig0` keeps the low 16 bits.
* step 10: `(u16)(abs16(z - [2DF4]) >> 10) > D` (unsigned) -> 0.
* `rtype == 0 || rtype == -1`: returns 0 **without writing `*bearing` and `*dist`**: the callers then use uninitialised stack locals
  (section 10, Q-A1).
* step 11 compares `i16(sig + [3D5C]) > 100` before the Lockheed halving of step 13.

### 3.3 `0000:516d det_site_radar_update(site)` (owner detect) (T)
`simflow.md` Part 5 section 1 is right except: the arc bounds are **sign-extended** high bytes, `[C13A] = sx8(hi(bearing)) - 0x20`,
`[C13C] = sx8(hi(bearing)) + 0x20` (words). Order of side effects (keep it): signal, `[3D6C]`, bar `8316`, ring `[C070] = (sig+sig_p)*D/100`
(32-bit, truncating), `[3D6A] = rate`, `[C07E] = site`, `[E54C]`, arc, `[9872]`, first-contact dot + "<site> detected"; then if
`sig + [3D5C] > 100`: `5588`, level `+= ((war + skill + 1) << 4) >> (ECM ? 1 : 0)` capped at 0xFF, alert network when the site is not
neutral, no decoy is alive (`[39B2] == 0`) and level > 0x7F; firing gate `skill >= [B054]` (fighters engaged), level > 0xC0, type != 0x15
(OTH), `[98F2] > 0x500` (player not near an airfield); then either "Radar ID" (level >= 0xFA, once) or a SAM into slot `site & 7` if free and
`range > D` ("<site> firing <SAM>", `comm+2Ch` += 1). Else flag 0x10 cleared and level -= 0x10 (floor 0).

### 3.4 `0000:4f2a hud_threat_update(void)` (owner detect) - `simflow.md` Part 4 section 4 (V)
The site scheduler: a site with a radar (`+6 != 0`, not destroyed) is serviced when `2*war + ([C600] >= 0x80 ? 3 : 1) >= (-((st >> 10) * i)) & 7`
or it already has a level or flag2 bit 0; its countdown reload `(rate << 8)/((level >> 2) + 16) + i` makes tracked sites scan faster.
`516d` runs when the countdown equals 4 and no ring animation is running (`[3D6A] < 0`). **`[C600]`** is the alert pulse: `559a` writes
`[C0B6] = 0xFF`; every 8th step the airfield scan of `39c4` latches `[C600] = [C0B6]; [C0B6] = 0`, so for 8 steps after an alert every site's
service probability rises (`+2`). Part A draws the stealth bars from `[3D5C]`; Part C the threat ring.

### 3.5 `0000:5588 det_radar_warning`, `0000:559a det_alert_network` (owner detect) (T)
`simflow.md` Part 5 section 1 (both right). `559a` = "the whole network learns where the player is": `[3D62] = 250*rate`, last known
position `([B2A0], [B75A], [B76E] = [2DF4])` = the player's, or the decoy's x/y when a decoy is alive, `[98F0] = heading` (no reader),
`[C0B6] = 0xFF`, and every site with a radar gets `level = clamp_w(level, (war + skill - 1) << 4, 0xFF)` (a floor; for `war + skill = 0` the
floor is -16 and the clamp keeps the level).

### 3.6 Alert state at a glance

| state | meaning | raised by | decays / read by |
|---|---|---|---|
| `[3D5C]` | own radar signature (section 3.1) | 43ee every step | 4f2a bars, 5460, 516d, 62d0 |
| site `+0xA` level | track quality of one radar site | 516d `+ (war+skill+1)*16` per seen scan (halved by ECM) | -16 per unseen scan; > 0x7F alert network, > 0xC0 may fire, >= 0xFA Radar ID |
| unit `+0x22` level | same for one fighter | 62d0 `+ (war+skill+2)*16` (halved by ECM) | -32 per unseen scan; > 0xC0 alert network, visual ID, AAM launch |
| `[3D62]` | network alert timer, `250*rate` steps | 559a, ground missile hits (671a) | -1 per step (39c4); `> [9F64]` (first 50 s) = fresh: scrambles, enemy guns, hunting by 0x40/formation units; `== 0`: fighters patrol |
| `[B2A0] [B75A] [B76E]` | last known player (or decoy, or last ground explosion) position | 559a, 671a | 5730 mode 1 target, scramble radius |
| `[C0B6]` -> `[C600]` | alert pulse (8 steps) | 559a | 4f2a scan rate |
| `[DED6]` | locks this step (sites + units > 0xC0) | 516d, 62d0; recounted in 39c4 every `4*rate` steps | lamp 8 blinking (671a) |
| `[DED2]` / `[DECE]` | Radar ID / visual ID counters | 516d / 62d0 | decremented when that site/unit is destroyed |
| `[B054]` | fighters that were attacking (mode 1, `D < 0x400`) last step | 5730 | 516d fires only if `skill >= [B054]`; 62d0 only if `2*skill >= [B054]`; 5730 speed bonus / break-off |

SAM sites and radar types: `simflow.md` Part 5 section 6.1 (table `DS:2E80`, V). There is **no AAA** in VGAME: the only guns are the
player's (465b) and the fighters' (5730), both in the tracer table `DS:E44E`.

## 4 Enemy aircraft AI

### 4.1 `0000:5730 ai_update_units(void)` (owner ai) (T, 0 mismatches, every line covered)
Called once per step by `39c4` (step 6). `st = [3D60]` (step counter), `rate = [3660]`. Locals: `mode`, target `(tx, ty, tz)`, `brg`, `D`
(raw `fx_dist`), `pt` (pitch target), `bt` (bank target), `rb` (bank rate).
```c
if (!(st & 1) && [3D68] == -1) W[39DA + 8*((st & 0x1E) >> 1)] = 0;        /* free one smoke record (x = 0) */
W[E44E + 12*(((st & 0xC) >> 2) + [B16C])] = 0;                           /* free one of the 4 enemy tracer slots (x = 0) */
[B054] = [94F2];  [94F2] = 0;                                            /* attackers of the last step */
for (u = 0; u < [DED0]; u++) {
    U = unit[u];  fl = U.fl (word);
    if (!(fl & 1)) continue;
    [94F0] = U.type;  AC = DS:2FC2 + 32*[94F0];
    if (!(fl & 2) || U.spd == 0) { scramble(u); continue; }               /* section 4.3 */
    /* ---- target selection ---- */
    if (fl & 4) {                                                        /* MODE 2: return to base and land */
        mode = 2;  S = site[U.site];  tx = S.x;
        if (U.fl2 & 2) {                                                 /* on final: line up on the runway point */
            ty = S.y;  d = U.x - tx;  tx = tx - 2*d;                     /* lateral over-correction (16-bit) */
            tz = ((u16)(abs16(d) + W[S+8]) & 0x200) ? 0x8C : 0x0C;       /* deck height on a carrier (flag2 bit 1) unless |d| carries into bit 9 */
        } else {                                                         /* approach point 640 units off the field, glide slope */
            ty = [B16E]*0x280 + S.y;
            tz = fx_dist(U.x - tx, U.y - ty) + 0x7D0;
        }
    } else if ([3D62] == 0 || ((fl & 0x140) && [3D62] <= [9F64])) {      /* MODE 3: patrol / own target */
        mode = 3;
        if ((U.fl2 & 1) && [3D6E] != -1) {                               /* formation on the leader [3D6E] (WLD "special unit") */
            L = unit[[3D6E]];  k = u & 7;  a = L.hdg + (k << 11) - 0x1800;
            tx = L.x + sin_mul(a, L.spd);  ty = L.y - cos_mul(a, L.spd);  tz = L.z + (k << 6);
        } else {
            if (((u8)((u << 3) + [98E2]) & 0xBF) == 0 && !(U.fl & 0x40)) {   /* re-target: every step of 2 seconds out of 256 */
                best = 0x7FFF;  bp = fx_heading(px - U.x, U.y - py);         /* bearing unit -> player */
                for (n = 0; n < 8; n++) {
                    s = rand_n([C0B4]) + 1;                              /* 1..[C0B4] (one past the table possible: quirk) */
                    if (site[s].fl2 & 4) continue;                       /* friendly */
                    d = bp - fx_heading(site[s].x - U.x, U.y - site[s].y);
                    if (abs16(d) >= best) continue;
                    best = abs16(d);  U.site = s;                         /* site most in line with the player */
                    if (((4 - skill) << 12) > best) break;
                }
                if ((fx_dist(px - U.x, py - U.y) & 0xFFC0) > 0x5780 && u != 0) { U.fl &= 0x1C1; U.endur = 0; }   /* too far: park (will re-scramble) */
            }
            tx = site[U.site].x;  ty = site[U.site].y;  tz = clamp_w([2DF4] + 1000, 5000, 20000);
        }
    } else {                                                             /* MODE 1: hunt the player */
        mode = 1;  tx = [B2A0];  ty = [B75A];  tz = [B76E];
        if ([39B2] != 0) { tx = [39AA]; ty = [39AC]; tz = clamp_w([2DF4], 1000, 30000); }   /* decoy alive: chase it */
    }
    steer(u);                                                            /* section 4.2 */
}
```

### 4.2 Steering, guns, integration (the body of 5730 after the target selection)
```c
if (mode == 3 && (U.fl & 8)) { tx = px; ty = py; tz = U.z; }              /* patroller that sees the player turns at him, level */
dx = tx - U.x;  dy = ty - U.y;
brg = fx_heading(dx, -dy);  D = fx_dist(dx, dy);
pt  = clamp_w(fx_heading((tz - U.z) >> 5, D), 0xE000, 0x1000);         /* climb at most 0x1000, dive at most 0x2000 */
if (mode == 1 && D < 0x400) {                                            /* dogfight range */
    [94F2]++;
    if (D < 0x200 && !(st & 3) && [3D62] > [9F64] && abs16(U.hdg - brg) < 0x400 && abs16(U.pitch - pt) < 0x400) {
        k = ((st & 0xC) >> 2) + [B16C];  T = tracer[k];                  /* cannon burst into enemy slot k */
        sp = 0xE0 / rate;
        T.dz = sin_mul(-U.pitch, sp) << 5;                               /* NB: sign inverted (original bug, keep) */
        h = cos_mul(U.pitch, sp);  T.dx = sin_mul(U.hdg, h);  T.dy = -cos_mul(U.hdg, h);
        T.x = U.x;  T.y = U.y;  T.z = U.z;
    }
    row = clamp_w((u & 3) + skill, 0, 2);  if (u == 0) row = 1;
    h = U.hdg;  if (abs16(U.bank) < 0x4000) h += U.bank >> 2;
    b = ((int16)(brg - h) >> 13) & 7;                                    /* octant of the target off the (banked) nose */
    h2 = [2DEE];  if (abs16([2DF2]) < 0x4000) h2 += [2DF2] >> 1;
    a = (((int16)(U.hdg - h2) >> 13) + 4) & 7;                           /* aspect octant vs the player's (banked) heading */
    cmd = TBL[(row*8 + a)*8 + b];                                        /* DS:3A8C, 3 x 8 x 8 signed words, values below */
    bt = cmd << 12;
    if (cmd == 0x100) { pt = 0x6000; bt = (st & 0x800) ? 0x4000 : -0x4000; }        /* loop, direction from step bit 11 */
    if (cmd == 0x200) { pt = 0xA000; bt = (st & 0x800) ? -0x4000 : 0x4000; }        /* split-S */
    if (pt == (int16)0xA000 && 3000 - (U.pitch >> 3) > U.z) pt = U.pitch + 0x1000;  /* too low for a split-S: pull up instead */
    if (abs16(U.bank) > 0x4000) { bt = 0; pt = 0; }                       /* inverted: roll out and level */
} else {
    bt = clamp_w(brg - U.hdg, 0xE000, 0x2000) << 1;                      /* turn toward the target */
    if (mode == 1 && 2*skill < [B054]) bt = 0x3000;                      /* too many attackers: circle away */
}
if (mode == 1 && (site[[DFE0]].fl2 & 4) && [98F2] < 0x780) bt = 0x3000;  /* player near a friendly airfield: break off */
lim  = AC.agil << 12;            bt = clamp_w(bt, -lim, lim);
lim2 = (AC.agil & 0xFF) << 8;    rb = clamp_w(bt - U.bank, -lim2, lim2);
if (U.fl2 & 4) {                                                         /* take-off roll */
    if (U.spd < 0x96) U.pitch = 0; else U.pitch += 0x100;                 /* rotate above 150 */
    rb = 0;
    if (U.spd < AC.maxspd) U.spd += 0x28 / rate;
    else if (U.z > 0x12C) U.fl2 &= ~4;
}
if (U.fl & 0x30) rb = 0x400;                                             /* damaged/falling: roll */
if (((u ^ st) & 3) == 0) { terr_probe_cell(U.x, U.y, U.z);  if ([C686]) U.fl2 |= 0x20; else U.fl2 &= ~0x20; }   /* 77d9, render3d Part 3 */
if (U.fl2 & 0x20) pt = 0x3000;                                           /* terrain ahead: climb */
U.bank += (int16)(rb << 2) / rate;
U.hdg  += (U.bank >> 3) / rate;
dp = pt - U.pitch;
if (U.fl & 0x20) {                                                       /* shot down */
    dp = -0x200;
    if (!(st & 1)) {                                                     /* smoke trail record j */
        j = (st & 0x1E) >> 1;  P = DS:39DA + 8*j;  P = {U.x, U.y, U.z, 0};  [3A5A] = j;
        for (n = 0; n < 16; n++) if (n != j && W[39DA + 8*n] != 0) W[39DA + 8*n + 6]++;   /* age the others */
    }
}
if (U.pitch < 0 && U.z < 200 - sin_mul(U.pitch, 2000) && !(U.fl & 0x220)) dp = 0x400;   /* diving too low: pull (not if falling / on final) */
dp = clamp_w(dp, -0x400, 0x400);
U.pitch += (int16)(dp << 2) / rate;
if (abs16(U.pitch) > 0x4000) { U.hdg += 0x8000; U.bank += 0x8000; U.pitch = 0x8000 - U.pitch; }   /* over the top (byte adds to the high bytes) */
U.fl &= ~0x10;
v = (int16)(((long)U.spd * (u16)(0x8000 - U.pitch)) >> 15);              /* slower climbing, faster diving (x0.5 .. x1.5) */
w = v - (abs16(sin_mul(U.bank, v)) >> 1);                                /* turning costs speed */
if (mode == 1 && skill >= [B054]) w += w >> 1;                           /* attack dash x1.5 */
w = ((int16)(w << 2) / rate) >> 2;
h = cos_mul(U.pitch, w);
U.x32 += (long)sin_mul(U.hdg, h);  U.y32 -= (long)cos_mul(U.hdg, h);  U.z += sin_mul(U.pitch, w);
U.x = (int16)(U.x32 >> 5);  U.y = (int16)(U.y32 >> 5);
if (U.z > 30000) U.pitch = 0;
if (U.z < 0) {                                                           /* hit the ground */
    U.fl &= (u >= 1) ? 0x1C1 : 0;                                       /* unit 0 (the mission's air target) vanishes */
    [C0A0] = U.x; [C0AA] = U.y; [C0B0] = U.z;  [9D0C] = 0xFFF0;          /* explosion for a17e */
    if ([3D64] == u) [3D64] = -1;
}
if (D < 0x10 && mode == 2) {                                             /* reached the approach/runway point */
    msg_site_label(U.site); strcat(9876, " - "); strcat(9876, AC.name);
    if (U.fl2 & 2) { U.fl2 |= 0x10; strcat(9876, " landing"); } else { U.fl2 |= 2; strcat(9876, " on final"); }
    if (u < [DED0] - 4) msg_post(9876);                                  /* the last four (airfield traffic) are silent */
}
if (U.fl2 & 0x10) {                                                      /* rolling out */
    U.pitch = U.bank = 0;  U.hdg = ([B16E] == 1) ? 0 : 0x8000;
    U.z = (site[[DFE0]].fl2 & 2) ? 0x8C : 0x0C;                          /* NB: the PLAYER's nearest airfield, not U.site */
    if (U.spd > 0) U.spd -= 0x3C / rate;
    else { U.fl &= 0x1C1; if (u == 0 && [E2D8] >= 5) U.fl = 0; }        /* parked (spd <= 0 only after it went negative) */
    if (u >= [DED0] - 4 && U.spd < 100) U.fl = (U.fl & 0x1C1) | 0x406;   /* airfield traffic: touch-and-go, takes off again */
}
if (--U.endur == 0) {                                                    /* fuel out: go home */
    U.fl |= 4;  best = 0x7FFF;
    for (i = 3; i < [E540]; i++) if ((W[site[i]+8] & 0x101) == 1) {     /* airbase, not neutral (friendly bit not tested) */
        d = fx_dist(U.x - site[i].x, U.y - site[i].y);  if (d < best) { U.site = i; best = d; } }
}
if (U.fl & 2) {
    c = (u == 0 && [E2D8] >= 5 && (st & 1)) ? 0 : 0x29;                  /* the mission's air target blinks */
    if ([98DE] != 0 || u == 0) mfd_map_plot(U.x, U.y, c, 1);
    if ((int16)st % (rate << 4) == (((u & 8) >> 3) + 2*(u & 7)) * rate && !(U.fl & 0x20)) ai_unit_radar(u);   /* 62d0 */
}
```
Combat table `DS:3A8C` (`TBL[row][a][b]`, rows `a` = aspect octant 0..7, columns `b` = target octant 0..7; `4` = 0x4000 bank, `256` = loop, `512` = split-S):
```
row 0: a0  3  3  2  1  0 -1 -2 -1 | a1  3  2  2  2  1  1  1  2 | a2  3  3  3  2  2  2  3  3 | a3  3  3  3  3  3 -3 -3 -3
       a4  3  3  3 -3 -3 -3 -3 -3 | a5 -3 -3 -2 -2 -2 -3 -3 -3 | a6 -2 -1 -1 -1 -2 -2 -2 -3 | a7  1  2  1  0 -1 -2 -3 -3
row 1: a0  2  3  2  2  1  1  1 -2 | a1  4  4  3  2  1  4  4  4 | a2  4  4  4  4 -2  4  4  4 | a3  4  4 -4 -4  4  4  4  4
       a4 -4 -4 -4 -4  4  4 -4 -4 | a5 -4 -4 -4  2 -4 -4 -4 -4 | a6 -4 -4 -4 -1 -3 -4 -4 -4 | a7  2 -1 -1 -1 -2 -2 -3 -1
row 2: a0  2  3  2  2  1  1 -1 -2 | a1  4  4  3  2  1  4 -4  4 | a2  4  4  4  4 256 4 256 256 | a3 256 4 -4 -4 512 4 256 256
       a4 256 256 -4 512 4 4 -4 256 | a5 256 256 -4 4 -4 -4 -4 -4 | a6 -4  4 256 -1 -3 -4 -4 -4 | a7  2  1 -1 -1 -2 -2 -3 -1
```
(the port may also read the table from the image; verified identical to the dump above.)

### 4.3 Scramble (ground units; part of 5730)
```c
scramble(u):    /* reached when !(fl & 2) || U.spd == 0 */
if ((((int16)[98E2] >> (war + 4)) ^ u) & 7) return;                      /* this unit's window: 1 of 8 slots of 16/32/64 s */
if (u >= [DED0] - 4 || u == 0) return;
r = rand_n([E540]);
if (!([3D62] > [9F64] || (U.fl & 0x80))) return;                        /* fresh alert, or an "any alert" unit */
if ((W[site[r]+8] & 0x181) != 1 || site[r].level /*fighter type based there*/ != U.type) return;
if (AC.range > (u16)(fx_dist([B2A0] - site[r].x, [B75A] - site[r].y) >> 6)) ai_unit_takeoff(u, r);   /* 65d4 */
```
`rand_n` is called before the alert test, so the random stream advances in every window step (keep the order).

### 4.4 `0000:62d0 ai_unit_radar(u)` (owner ai) (T)
`simflow.md` Part 5 section 2 is right, with: the nose-cone arc is `[C13A] = sx8(hi(U.hdg)) - 0x30`, `[C13C] = sx8(hi(U.hdg)) + 0x30`; the
nose test is `sx8(hi(abs16(brg - U.hdg))) > 0x30` (signed byte: a target exactly behind passes); the locked arc uses `sx8(hi(brg)) -+ 0x20`.
Aircraft without radar (`AC.radar` 0 or -1, e.g. An-72, F-18) get `sig = 0` and **uninitialised `brg`, `D`** (Q-A1); with `[3D5C] > 100`
they still raise their level and may "Visual ID" / fire with those values.

### 4.5 `0000:65d4 ai_unit_takeoff(u, site)` (owner ai) (T)
`simflow.md` Part 5 section 2 is right (`+0x1C endur = ((long)AC.range * rate << 11) / AC.maxspd`, 32-bit, truncating).

## 5 Weapons and countermeasures

### 5.1 End to end
1. START writes 4 bays (`comm+38h+2i` store id, `comm+40h+2i` quantity); `39c4` copies them to `DS:363E + 4i {id, qty}` (Part 6 section 5).
   Stores table `DS:3662` (20 x 26 B: names, projectile type, icon), projectile prototypes `DS:3362` (40 x 18 B: name, range, speed*64, seeker
   class, turn rate, 3D model), effectiveness `DS:386A` (20 stores x 16 target classes): `simflow.md` Part 6 section 2 (V, tables dumped).
2. Space selects the bay `[364E]` (0..3; model 1: 0..1), `8` opens the bay doors (2 steps; `[9B04]` bit 2), F2 the master mode `[E2FE]`
   (0 none, 1 air-to-air, 2 air-to-ground), `b` asks `9ACA` for the next target (bit 7 of `[3D64]` / `[3D66]`), the sensor page (keys m , . /) shows
   the designated object.
3. Each frame `a17e` S4 decides whether the selected weapon may lock the designated target (`[E55C]`, lock lamps; `simflow.md` Part 9
   section 1.4); `9ACA` keeps or cycles the designations (`render3d.md` Part 2; the ground designator runs only while the right MFD shows the
   TV sensor page `[DFDC] == 0x13`; 0x19 is never a page).
4. Enter (or joystick button 1) -> `cba0` tail -> `753f wpn_release` (4-step debounce) -> slot 8..11 (`[B04C]` = slot for the weapon camera).
5. `671a` moves every projectile each step (seeker, steering, gravity, ground test, impact, proximity) -> `71b4` / `72a4` / `7470` / `74f6`.
6. Results: message line, event log (`499e`), object/unit flags in the world blob exported to END.

### 5.2 `0000:753f wpn_release(void)` (owner weapons) (T) - `simflow.md` Part 6 section 3 is right
Exact points (T): the "Bay closed" and "Insufficient Altitude" paths call `snd_cmd(0x20)` directly (slot 101) and return **without** the
`829d(0x15)` refresh; the fuel tank path returns without logging; life is a 32-bit `((long)range*rate << 5) / ((speed64 >> 6) + 1)`, low word,
`+ 6`; the target is taken only when `[3D66] >= 0` and `[E55C] != 0` (class 6 any object, class 4 needs `site[t].+6 != 0`, class 5 needs a ship
class 12/13), and `[E55C] == 0` writes `target = -1`; the **last** free slot of 8..11 is used.

### 5.3 `0000:671a wpn_proj_update_all(void)` (owner weapons) (T) - `simflow.md` Part 6 section 3 is right, with:
* the player AAM loop skips units with `!(fl & 2)` or **speed** (`+0x1A`) == 0 (Part 6 calls the field "hp");
* player branch order: (cls 7 unit search) -> acceleration -> (cls 4/5/6 object) -> (LGB `[3D66]`). The acceleration (`(speed64 >> 6) > spd`
  and odd step: `spd++`) sets `brg = proj.heading`, which **cancels the heading correction of a cls-7 missile in that step** (pitch steering
  still runs); enemy missiles accelerate without touching `brg`;
* `brg == 0` (target exactly north) means "no steering" (the test is `brg != 0 && lock`);
* `tz` (`[bp-32h]`) and the player target index (`[bp-12h]`) are function locals that **persist across the 12 iterations** (and are
  uninitialised before the first one): enemy slots set `tz = [2DF4]`; a player slot without a lock keeps the previous slot's values (only
  harmless because the proximity block needs `lock`);
* `dist` (`[bp-0Eh]`) of an enemy slot is `[94F4]` as left by the last successful `711f` (stale on a failed check); the countermeasure
  distance test and the incoming-missile tone use it;
* the enemy-missile map blip is `8548(x, y, 0x11)` with only three arguments: the fourth (`big`) is 671a's saved `SI` = the caller's `SI`,
  which in the game is the memory-top segment from the C start-up (non-zero) -> a 2x2 dot. Port: `big = 1`;
* the "Equipment Delivered / Bad Drop" block is unreachable (Part 6 Q1, confirmed: `type == 0x26` is tested inside the bomb branch).

### 5.4 `0000:711f wpn_seeker_check`, `0000:8bab wpn_suit`, `0000:bad1 obj_find_at`, `0000:bb86/bb9e/bbb4`, `0000:bc77/bcac` (owner weapons) (V / T)
`711f` (T): `simflow.md` Part 6 section 3. `8bab wpn_suit(store, o)` (V): `cls = byte[C604 + (byte[site[o]+0xE] & 0x7F)] & 0xF;
return sx8(byte[386A + (u16)(store << 4) + cls])`. `bad1` (V): Part 6 section 1 table (note: x is sign-extended here, zero-extended in 72a4;
same for x <= 0x7FFF). `bbb4/bb86/bb9e` (V): `simflow.md` Part 8 Part 2 (`[9502]` = bearing player -> object, only with `want_bearing`;
`[94FE]` = distance). `bc77 obj_cls_soft` (class 9, 0xB, 0xC, 0xD) and `bcac obj_cls_ship` (0xC, 0xD) (V): Part 6 section 1.

### 5.5 Countermeasures
* **Dropped stores** (`4512 wpn_release_countermeasure(a)`, keys 1 flare / 2 chaff / 5 decoy) (T): `simflow.md` Part 4 section 4 is right.
  Counts `[3638] [363A] [363C]` start at 12 / 18 / 3 (image). Records `DS:39AA + 12*k {x, y, +4 unused, type, timer, +10 unused}`:
  slot 0 = decoy (refused while damage bit 6), slots 1..3 = flares/chaff (last free one).
* **`461c wpn_cm_tick` (T), corrected:** only the **first** busy slot (lowest index) is decremented per step; the function returns right
  after it (and after the decoy lamp `898b(7, timer >= 1 ? 0x29 : 0)` when that slot is a decoy). So while the decoy lives, flares and chaff
  do not age, and several flares age one after the other:
  ```c
  for (k = 0; k < 4; k++) { R = rec[k]; if (R.timer == 0) continue; R.timer--; if (R.type == 3) hud_lamp_update(7, R.timer >= 1 ? 0x29 : 0); return; }
  ```
* **What they do** (all in 671a for missiles, 5730/516d/559a for the AI): a flare record (type 1) is a target candidate for IR seekers
  (`cls <= 0`), chaff (type 2) for classes 1, 2, and 3 at beam aspect; a captured missile flies straight (lock 0). A **decoy alive**
  (`[39B2]` = decoy timer != 0) moves the enemy missiles' aim point to the decoy, makes hunting fighters chase it (altitude `alt_s` clamped
  1000..30000), suppresses the alert network from SAM sites, the missile warnings and the "Hit by" damage.
* **Jammers** (keys 3 / 4, `[9B04]` 0x20 / 0x10; both switched off by damage bit 6): ECM (0x10) halves every level build-up, adds 16 to
  the signature and makes radar missiles (`cls > 0`) lose their lock beyond `skill*256 + 0x180` (classes 1 and -1 always); IR jammer (0x20)
  does the same for IR missiles (`cls <= 0`), limited to `20*rate` steps on, then recharging (lamp 6 shows 0x2A while recharging).
  A third "countermeasure" is time: with the alert older than 50 s (`[9F64] > [3D62]`) every enemy missile except the SA-14 (type 0x21) is
  subject to the same lose-lock test.

### 5.6 Guns
* Player gun `465b wpn_gun_update` (T): `simflow.md` Part 4 section 4 is right. Model 0 only; one round per 2 steps into slot
  `(st >> 1) % [B16C]` while button 0 is held, ammo `[3650]` (image 650) `+= -40/rate`, clamped 0..1000; rounds move by their velocity
  every step (all `[B16C] + 4` slots).
* Enemy cannon: spawned by 5730 (section 4.2) into the 4 slots after the player's, freed one per step by 5730.
* Hits are tested in `a17e` S2 **only for rounds that project into the current view** (`simflow.md` Part 9 section 1.2): player rounds vs
  every airborne unit (`dist < (378/isqrt((rate+2)<<2)) / (skill+1)`, kills, one round can kill several), enemy rounds vs the player
  (`d < 0x20`: "Hit by gunfire", damage if `d < 0x20/(4-skill)`), rounds below z 0 vs ground objects (effect row 18).

## 6 Damage, crash and eject

### 6.1 Damage to the player
Sources, all through `74f6 dmg_player_blast` (T; `simflow.md` Part 6 section 3 is right): enemy missile proximity burst (671a, unless a decoy
is alive), own bomb bursting within `skill*32` of the player (671a; never at skill 0), enemy cannon (a17e). Training mode (`[9B05] & 0x10`)
is immune. Each call sets `skill + 1` random bits `rand_n(8)` in `[3632]` and adds as many to `[C5C8]` (initial 2), refreshes the damage
lamps (`829d(0x16)`), sets the HUD hit flash `[98EE]` and plays sound 4.

| `[3632]` bit | lamp (898b idx -> button) | effect | where |
|---|---|---|---|
| 0x01 | 10 -> 5 | missile warning lamps and incoming tone suppressed | 671a |
| 0x02 | 11 -> 6 | engine: throttle limited to `(0x19 - [C5C8]) * 4` | 1e9e |
| 0x04 | 12 -> 7 | flight controls: buffet `+ clamp_w([C5C8] << 2, 0, 0x20)` | 1e9e |
| 0x08 | 13 -> 0xC | navigation: altitude hold forced off, HUD waypoint caret hidden | cba0 tail, 7898 |
| 0x10 | 14 -> 0xB | bay doors jammed open (`8` ignored) | cba0 |
| 0x20 | 15 -> 9 | fuel leak: `- [C5C8]` per burn tick; nav-page fuel estimate | 1e9e, 9208 |
| 0x40 | 16 -> 8 | jammers off, decoy cannot be launched | cba0 tail, 4512 |
| 0x80 | none | none | - |

`[C5C8]` also: stall nose-drop doubled when > 8 (1e9e), "stranded" end when > 4 on the ground outside an airfield (39c4), exported to
`comm+36h` (`[3632]` to `comm+34h`).

### 6.2 Damage to targets (owner damage) - `simflow.md` Part 6 section 3 is right for `71b4`, `72a4`, `7470`, `780a` (V, re-read)
Points to keep: `71b4` decrements `[DECE]` when `fl2 & 8` (visual ID'd) and logs `3 | 0x83` with `type + (fl2 & 0x40 ? 0x80 : 0)`; a unit
with **speed 0** is reset to `fl & 0x1C1` instead of staying dead (a parked fighter destroyed on its base can scramble again).
`72a4` decrements `[DED2]` when `fl2 & 0x10` (Radar ID'd), counts unregistered kills in the byte table `C0BA[wobj.id]` (index = the full
id word), and calls `559a` when `skill < 2`. Gun hits use effect row 18 (`a17e`).

### 6.3 `0000:0d08 terr_find_scenery_at(long x, long y') -> hit*` (owner damage) (V)
Nearest destroyable scenery object around a world point (`x = (long)lx << 5`, `y' = (long)(0x8000 - ly) << 5`). Callers: `39c4`, `4b57`,
`72a4`, `bad1`. Uses render3d's terrain tables: `0804 terr_to_level`, `0865 terr_tile_type`, `0fb1 terr_scenery_override`, record lists
`DS:E304[32*L + type]` (7-byte records `{s16 x, s16 y, s16 z, u8 id}`) with counts `DS:061E[32*L + type]` (`render3d.md` Part 1).
```c
[BA16] = 0x7FFF;
for (L = 1; L <= 2; L++)
  for (n = 0; n < 9; n++) {                                  /* 3x3 tiles, order (dx,dy): (-1,1)(1,1)(1,-1)(-1,-1)(0,1)(1,0)(0,-1)(-1,0)(0,0) */
    lx = terr_to_level(L, x);  cx = (u16)(lx >> 12) (unsigned);  fx = (u16)lx & 0x0FFF;
    ly = terr_to_level(L, y'); cy = (u16)(ly >> 12);             fy = (u16)ly & 0x0FFF;
    ox = W[04CC + 2n];  oy = W[04DE + 2n];                   /* -1, 0, 1 */
    offx = ox*0x1000 - fx + 0x800;  offy = oy*0x1000 - fy + 0x800;     /* W[04F4 + 2*o] = o*0x1000 */
    cx += ox;  cy += oy;
    T = terr_tile_type(L, cx, cy);  if (T == -1) continue;
    p = [E44A] = W[E304 + 2*(32*L + T)];
    for (k = 0; k < W[061E + 2*(32*L + T)] /*unsigned*/; k++, p = [E44A] += 7) {
        if (byte[C604 + (p[6] & 0x7F)] == 0) continue;       /* object class 0: not a target */
        rx = W[p] + offx;  ry = W[p+2] + offy;  d = abs16(ry) + abs16(rx);
        if (L == 1) d >>= 2; else { rx <<= 2; ry <<= 2; }    /* d in level-2 units, rx/ry in level-1 (= argument) units */
        flag = 0;  id = p[6];                                /* byte incl. bit 7 */
        if ((p[6] & 0x80) && (u8)terr_scenery_override(L, k, cx, cy) != 0) {
            id = byte[B776 + 5*[94DC]];                      /* overridden id */
            if (byte[07E0 + (p[6] & 0x7F)] == 0) flag = 1;
        }
        if (d < [BA16] && (byte[07E0 + (p[6] & 0x7F)] != 0 || [3D76] != 0 || flag)) {
            [BA22] = L; [BA23] = k; [BA24] = cx; [BA25] = cy;    /* bytes: the override key */
            [BA20] = p;  [BA14] = id;  [BA16] = d;
            [BA18:BA1A] = (long)rx + x;  [BA1C:BA1E] = (long)ry + y';     /* object position, 32-bit */
        }
    }
  }
return ([BA16] != 0x7FFF) ? 0xBA14 : 0;
```
Hit record `DS:BA14`: `+0 id` (word), `+2 dist`, `+4 x32`, `+8 y32'`, `+0xC` tile record pointer, `+0xE..+0x11` key `{L, k, cx, cy}`, `+0x12`
new id (written by 0f31). `byte[07E0 + id]` = theatre object flags (`render3d.md` Part 1, flag arrays); `[3D76]` = 1 in the image, cleared
by `4b57` after the mission-start naming pass, so at mission start any object with a non-zero class is found. `[E44A]` is the shared record
cursor of the terrain walkers (also written by 04DA, 0FFB, 11F2).

### 6.4 `0000:0f31 terr_set_scenery_state(hit*, u8 newid)` (owner damage) (V)
```c
hit[0x12] = newid;
if ((u8)terr_scenery_override(hit[0xE], hit[0xF], hit[0x10], hit[0x11]) != 0) {     /* key already in the table; [94DC] = its index */
    if (newid == sx8([DEA4]) || newid == sx8([C0AC]))  byte[B776 + 5*[94DC]] = newid;   /* wreck ids replace the table entry */
    else                                             ((u8*)W[hit+0xC])[6] = newid;   /* other ids overwrite the tile record */
} else
    memcpy(DS:B772 + 5*[9902]++, hit + 0xE, 5);                                    /* append {key[4], newid} */
((u8*)W[hit+0xC])[6] |= 0x80;                                                       /* the tile record now has an override */
```
The compare is `sx8(byte)` vs the zero-extended `newid`: a wreck id >= 0x80 never matches (it then overwrites the tile record). The table
`DS:B772` (5-byte records, count `[9902]`) is part of nothing exported; END sees destroyed objects through the site flags and `C0BA`.

### 6.5 Crash, eject and the mission-ending checks

| event | condition | effect | spec |
|---|---|---|---|
| **eject** (Shift-F10, `cba0` cfd2, ignored while `[C06E] != 0`) | always | `snd 5`, `snd 0x14`; `lim = rand_n(300) + 500`; `v = (abs16([2DF2]) >> 5) + (abs16([2DF0]) >> 5) + [B168]` (knots); `v > lim` -> `4926(6)` (killed: too fast / inverted), else `comm+26h = 2` (ejected); then `[C06E] = 1`, chute anchor `[DEA2] = px`, `[DEC6] = py`, `[DECA] = [2DF4] + 8` | V, here |
| ejection sequence | `[C06E] != 0` | 1e9e: aircraft tumbles and dives (explodes at `crash` 16..19 or at sea level), pilot height `[DECA]` rises then falls (Esc drops it to 0), at 0 -> `4926(0)` on a step with `([98E2] & 7) == 0` (comm+26h stays 2) | simflow Part 2 section 2.5 (T) |
| hard landing | touch-down sink rate `cm*(-vs) > 0x80` (0x100 on a deck), or with crash mode: gear up or bank above `(0x18/cm) << 8` | `snd 4`, wait 60 ticks, `4926(5)` | Part 2 section 2.9 (T) |
| terrain collision | `[9F66]` (camera under terrain) and not external view | crash mode on and `alt != 0`: `snd 4`, screen off, wait 0x78, `4926(2)`; else bounce `+500` | Part 4 section 3.3 (V) |
| stranded | on the ground outside every airfield zone, `kts > 50`, and (crash mode or `[C5C8] > 4` or no fuel) | `4926(1)` | Part 4 section 3.2 (V) |
| landed | stopped in a friendly/neutral zone `16/rate` checks | "Safe Landing", `4926(0)` with comm+26h = 3 | Part 4 section 3.2 (V) |
| quit | Alt-Q + `Y` | `4926(7)`, exit code 0 | Part 2 section 2.10 |
`4926` ignores codes other than 0 and 7 while `[C06E] != 0` (after an ejection only the end of the sequence or a quit can end the mission).

## 7 Step order (who sees whose results)

```
frame: 2f82 view + 03ce scene (9ACA designators; a17e: gun hits, explosion, weapon lock E55C) ; 7898 HUD ; flip
       1e9e  player flight model (reads [3D64] for the sway, [98EA] gun firing; writes attitude, position, alt, speed)
       39c4: px/py ; first-step init ; map ; 4f2a site radars (516d) ; 5730 fighters (62d0, 65d4) ; 671a projectiles ;
             461c decoy timers ; 465b gun ; ... airfield / landing / terrain / clock / fps estimator ;
             43ee: cba0 keys (fire -> 753f, countermeasures -> 4512, eject) then the signature [3D5C]
```
Consequences: radars see the signature of the previous step; a weapon released in `cba0` first moves in the next step's `671a`; gun hits
of a round spawned in step n are tested in the scene pass of frame n+1 after the round moved once more in `465b`.

## 8 Corrections to simflow.md / render3d.md (this file wins)

| where | was | is |
|---|---|---|
| Part 2 section 0 "sim_rate" | "a port that renders at a fixed rate keeps sim_rate constant (4)" | the port emulates the estimator (Decision 2026-10-13, section 1.3) |
| Part 2 section 2.9 / Open items | `[E2FE]` "view/camera mode" | master mode 0 none, 1 air-to-air, 2 air-to-ground (F2) |
| Part 4 section 4 `461c` | every busy slot ticks | only the first busy slot ticks (section 5.5) |
| Part 4 4.0 / Part 6 2.8 / Part 10 1.1 | `[9B04]` 0x10 "chaff mode" / "radar emitting", 0x20 "flares active" | 0x10 ECM jammer, 0x20 IR jammer (keys 4 / 3) |
| Part 6 sections 2.5, 3 (`71b4`, `671a`) | unit `+0x1A` "hp" | speed |
| Part 6 2.8 | `[39B2]` "player's own alias (ejection?)" | timer of decoy record 0: a decoy is alive |
| Part 5 section 1 `516d`, section 2 `62d0` | `hi_byte(bearing) - 0x20` | sign-extended high byte (sections 3.3, 4.4) |
| Part 5 section 2 `5730` step 3 | formation offset "(u&7)<<11 rotated", "leader's altitude" | polar offset `L.spd` at `L.hdg + (u&7)*0x800 - 0x1800`, altitude `L.z + 64*(u&7)` |
| Part 5 step 4 | carrier approach chosen by `site.flags2 & 2` | chosen by the unit's "on final" bit `fl2 & 2` (section 4.1) |
| Part 5 step 11 | touch-and-go "if `u < [DED0]-4`" | for the last four units `u >= [DED0]-4` |
| Part 5 step 14 | radar every `16*rate` "staggered" | `st % (16*rate) == ((u&8)>>3 + 2*(u&7)) * rate` (signed remainder) |
| Part 5 section 6.3 fl `0x10/0x20` "damaged-falling" | | 0x10 = player missile within 0x180 (cleared each step), 0x20 = shot down |
| Part 9 4.3 (line "Player launch") | "first free of 8..11" | last free slot |
| render3d.md 9ACA open item 2 | `[DFDC]` 0x13/0x19 "laser-guided bomb ids" | right-MFD page: 0x13 = TV sensor page; 0x19 is never selected |
| render3d.md section 6 `DS:04F4 = 0 0x1000 0x2000` | | `DS:04F2..04F6 = -0x1000, 0, 0x1000`, indexed by the cell offset -1..1 (section 6.3) |
| simflow.md Part 4 section 1.2 (`[DED6]` recount, S5.7) | the number of sites plus the number of units above 0xC0 | each loop stops at its first hit (the code falls through into the next loop): `[DED6]` = (any site) + (any unit), 0..2 |
| simflow.md Part 2 section 1 `cb62` (S5.7) | `rand_n` = the RTL helper | VGAME's `cb62` multiplies with `imul` (signed n); the other programs' `rand_n` zero-extends n |

## 9 Tests

`port/notes/s38/` (see its README): `s38_ref.py` (Python reference of every function marked (T) in sections 3-6 plus `d707`/`d7bb`),
`s38_test.py` (Unicorn harness: the real code runs from `work/unpacked/VGAME.EXE`, drawing/sound/text/terrain-probe/world-lookup callees are
stubbed identically on both sides; comparison = the whole DGROUP below the stack, the pilot and comm blocks, the ordered call log, return
values), `cov.py` (line coverage of the reference during a run). `python port/notes/s38/s38_test.py 1000` runs all scenarios.

| scenario | function | cases | mismatches |
|---|---|---|---|
| radar | 5460 | 1000 | 0 |
| sig | 43ee (cba0 stubbed) | 1000 | 0 |
| site | 516d (+5460, 5588, 559a real) | 1000 | 0 |
| unit | 62d0 (+5460, 5588, 559a real) | 1000 | 0 |
| takeoff | 65d4 | 1000 | 0 |
| ai, aifocus | 5730 (62d0, 65d4 stubbed; aifocus forces gun attacks, landings, roll-outs, scrambles) | 1000 + 1000 | 0 |
| cm, cmtick | 4512, 461c | 1000 + 1000 | 0 |
| gun | 465b | 1000 | 0 |
| seeker | 711f | 1000 | 0 |
| blast | 74f6 | 1000 | 0 |
| proj, projfocus | 671a (+711f, 8bab real; bad1/72a4/71b4/74f6 stubbed; projfocus forces ground-missile hits and close bomb bursts) | 1000 + 1000 | 0 |
| release, relfocus | 753f (+d7bb, bbed, bcac real) | 1000 + 1000 | 0 |
| (S3.6) | 1e9e via `port/notes/s36/r2_test_fm.py 600` | 600 | 0 |

Line coverage of the reference over `ai`+`aifocus` and `proj`+`projfocus`+`release`+`relfocus`: every line of 5730, 671a and 753f except
753f's `[E55C] == 0` target reset. Not differential-tested (read only): `0d08`, `0f31`, `72a4`, `71b4`, `7470`, `780a`, `bad1`, `4f2a`
(need terrain tables / world objects; `0fb1`, `0804`, `0865` themselves are T in S3.7).

## 10 Original quirks and bugs (keep them in the faithful port)

* **Q-A1** `5460` leaves `*bearing`/`*dist` unwritten for radar type 0/-1; `62d0` then reads its uninitialised locals `[bp-0Ah]`/`[bp-10h]`.
  Port: keep the values of the previous `62d0` call in static variables (closest to the stack reuse), initial 0.
* **Q-A2** `abs16(0x8000)` passes every cone test (seeker 711f, 9ACA, 62d0 nose, 5730 gun) and gives `a = -128` in 5460.
* **Q-A3** enemy cannon rounds get the vertical velocity of `-pitch` (5730).
* **Q-A4** patrol re-targeting may pick site index `[C0B4]` (one past the table).
* **Q-A5** rolling-out units take the deck height of the **player's** nearest airfield `[DFE0]`.
* **Q-A6** a parked unit killed on the ground (speed 0) is reset, not killed (71b4).
* **Q-W1** `461c` ticks only one countermeasure per step.
* **Q-W2** accelerating cls-7 missiles do not turn in that step; `brg == 0` disables steering; `tz`/target locals persist across slots (671a).
* **Q-W3** decoy capture stores 1 as the bearing (Part 6 Q2); all Part 6 quirks Q1-Q9 stand.
* **Q-W4** gun hits are tested only for rounds visible in the current view (a17e).
* **Q-D1** `0f31` sign-extends the wreck ids for the compare.
* **Q-T1** projectile acceleration, seeker tests and many timers are per step, not per second (section 1.3, "Not scaled").

## 11 Open items (U)

Collected in the PLAN.md User to-do row **U-flight** (DOSBox-X in flight):
* labels of the seven damage lamps (cockpit buttons 5, 6, 7, 0xC, 0xB, 9, 8 = `[3632]` bits 0..6, section 6.1) - naming only;
* enemy missile blips on the left map MFD: 2x2 dots (expected, section 5.3) or single pixels;
* lamps 5 / 6 light with keys `4` / `3` (ECM / IR jammer naming, section 2.4), lamp 6 dim while the IR jammer recharges.
Not needing the user: unit flags 0x40 / 0x80 and the friendly aircraft rows come from START/the WLD (data, no port impact).

## 12 Function index (generated from flight_symbols.csv)

| address | name | owner | signature | note |
|---|---|---|---|---|
| 0000:0d08 | `terr_find_scenery_at` | damage | `hit_t *terr_find_scenery_at(long x,long y_inv)` | [flight.md 6.3] nearest destroyable scenery object around a world point: levels 1-2, 3x3 tiles, Manhattan, override table, hit record DS:... |
| 0000:0f31 | `terr_set_scenery_state` | damage | `void terr_set_scenery_state(hit_t *h,byte newid)` | [flight.md 6.4] store a new id for the hit scenery object: override table DS:B772 (wreck ids) or tile record byte +6; sets bit 7 |
| 0000:1e9e | `sim_input_flight` | flight | `void(void)` | [flight.md 2.1] per-frame: key read+dispatch, stick, player aircraft model (attitude/speed/alt/fuel/G/eject), history ring |
| 0000:2c6e | `fm_rotate_apply` | flight | `void(int*A,int*B)` | [flight.md 2.2] fm_mat = A x B via 2DDC; every 8th sets dirty |
| 0000:2c9e | `fm_mat_to_euler` | flight | `void(void)` | [flight.md 2.2] matrix -> yaw/pitch/roll |
| 0000:2e43 | `fm_mat_from_euler` | flight | `void(void)` | [flight.md 2.2] e26b(2D94,yaw,pitch,roll); clear dirty |
| 0000:43ee | `sim_key_and_signature` | detect | `void(void)` | [flight.md 3.1] cba0(key) then the radar signature [3D5C]: speed, bank, bay +24, ECM +16, MicroProse altitude term / Lockheed +20 when [9... |
| 0000:4512 | `wpn_release_countermeasure` | weapons | `void(int type)` | [flight.md 5.5] Release flare/chaff/decoy: decrement 3636[type], fill slot R[] at 39AA, timers, message "<name> released", sound 0xF |
| 0000:461c | `wpn_cm_tick` | weapons | `void(void)` | [flight.md 5.5] tick ONLY the first busy countermeasure slot (then return); decoy lamp 7 |
| 0000:465b | `wpn_gun_update` | weapons | `void(void)` | [flight.md 5.6] Move gun bullets (E44E, 12 B) and spawn on fire button: ammo 3650, vel from heading/pitch, sound 6 |
| 0000:4f2a | `hud_threat_update` | detect | `void(void)` | [flight.md 3.4] Stealth bars from 3D5C, staggered site scheduler calling 516d, threat arrow 8548/863c; 3D6A-- |
| 0000:516d | `det_site_radar_update` | detect | `void(int site)` | [flight.md 3.3] Per-site radar scan: 5460 signal, bars, "detected" msg, level build-up, "Radar ID" msg, SAM launch into DS:3C0C slot site&7 |
| 0000:5460 | `det_radar_signal` | detect | `int(int x,int y,int z,int rtype,int*bearing,int*dist)` | [flight.md 3.2] Radar formula: (maxr-D)*cell*(2*skill+factor+1)/maxr, aspect/Doppler factor, altitude cone, halved for Lockheed model; D=... |
| 0000:5588 | `det_radar_warning` | detect | `void(void)` | [flight.md 3.5] [3D92]=2*[3660] warning lamp timer; snd_cmd 8 via d6bf(8,1) |
| 0000:559a | `det_alert_network` | detect | `void(void)` | [flight.md 3.5] Alert: [3D62]=[B286], last known player pos/alt/heading, raise every radar level to floor 16*(war+skill-1) |
| 0000:5730 | `ai_update_units` | ai | `void(void)` | [flight.md 4.1] per-step AI of all units: modes hunt(1)/RTB+land(2)/patrol+formation(3), combat table DS:3A8C, cannon, terrain pull-up, i... |
| 0000:62d0 | `ai_unit_radar` | ai | `void(int u)` | [flight.md 4.4] Aircraft radar via 5460, level build-up, "Visual ID - X" msg, air-to-air missile launch "W fired by X", lock tone snd 7 |
| 0000:65d4 | `ai_unit_takeoff` | ai | `void(int u,int site)` | [flight.md 4.5] Place unit on airbase (carrier z=0x8C), flags/=0x403, endurance, msg "<site> - <type> taking off" |
| 0000:671a | `wpn_proj_update_all` | weapons | `void(void)` | [flight.md 5.3] per-step update of the 12 projectile slots (0-7 enemy, 8-11 player): seeker/decoys, steering, move, impact, proximity bur... |
| 0000:711f | `wpn_seeker_check` | weapons | `int(int slot,int tx,int ty,int tz,int cls)` | [flight.md 5.4] 22.5 deg cone / rear-aspect test of projectile vs point; sets [94F4] dist [94F6] bearing; tz unused |
| 0000:71b4 | `dmg_unit_kill` | damage | `void(int u)` | [flight.md 6.2] marks unit u dead (flags 0x20), logs event 3/0x83, name to 9876, sound 5, mission_target_done(0) for primary air target |
| 0000:72a4 | `dmg_object_destroy` | damage | `void(int o)` | [flight.md 6.2] destroys ground object o: flag 0x80, wreck shape, mission target k (kind 2) done, event log 1/0xC/2, sound 5 |
| 0000:7470 | `mission_target_done` | damage | `int(int j)` | [flight.md 6.2] primary/secondary target achieved: sets 9B05 0x40/0x20 and 2E7C 1/2/3; 9876 = Primary target or Second. target; logs 0x8B... |
| 0000:74f6 | `dmg_player_blast` | damage | `void(void)` | [flight.md 6.1] skill+1 random bits into [3632] damage mask and [C5C8]++; lamps 10..16 refresh; [98EE]=1; sound 4; no-op in training |
| 0000:753f | `wpn_release` | weapons | `void(void)` | [flight.md 5.2] fire key: checks roll/bay/altitude, takes a store from the selected bay, fills a free projectile slot 8-11 or transfers f... |
| 0000:780a | `dmg_wobj_destroyable` | damage | `int(int o)` | [flight.md 6.2] 1 if the world object found in [9F10] is not yet the wreck shape (C0AC or DEA4 by class) |
| 0000:8bab | `wpn_suit` | weapons | `int wpn_suit(int wpn,int site)` | [flight.md 5.4] weapon suitability DS:386A[wpn*16 + class[site type]] |
| 0000:bad1 | `obj_find_at` | weapons | `int(int x,int y)` | [flight.md 5.4] terrain object at world point -> site index (>=1) / 0 = scratch site 0 filled / -1 none (R6 name obj_find_at) |
| 0000:bb86 | `wpn_site_dist` | weapons | `int(int site)` | [flight.md 5.4] bbb4 with site record DS:B2A4+16*site (x,y), bearing wanted |
| 0000:bb9e | `wpn_unit_dist` | weapons | `int(int unit)` | [flight.md 5.4] bbb4 with unit record DS:C140+36*unit (x,y), no bearing |
| 0000:bbb4 | `obj_dist_bearing` | weapons | `int(int x_u,int y_u,int want_bearing)` | [flight.md 5.4] [94FE]=dist2d(c987), [9502]=atan2(c9d6) if flag; relative to player [C0A4],[C0B2]; returns dist |
| 0000:bbed | `fm_bomb_dive_angle` | flight | `int(void)` | [flight.md 2.3] (((0x4000-/[2DF0]/)<<12)/(([2DF4]+0x1000)&0xFFFF))-0x4000 ; FFBOMB dive floor |
| 0000:bc77 | `obj_cls_soft` | weapons | `int(int o)` | [flight.md 5.4] C604[type]&15 in {9,11,12,13} |
| 0000:bcac | `obj_cls_ship` | weapons | `int(int o)` | [flight.md 5.4] C604[type]&15 in {12,13} |
| 0000:cb72 | `input_button` | flight | `int(int n)` | [flight.md 2.4] key pulse 43BE+2n plus joy_button(n) |
| 0000:e578 | `mat_mul` | flight | `void(A,B,C)` | [flight.md 2.2] 3x3 Q15 C=AxB; bit-exact spec: render3d.md Part 1 (E578) |
| 10a8:0c87 | `joy_recenter` | flight | `void(void)` | [flight.md 2.4] raw read, min=max=centre=raw |
| 10a8:0c95 | `joy_set_centre_axis` | flight | `SI axis` | [flight.md 2.4] centre/min/max[si]=raw[si] |
| 10a8:0ca8 | `joy_poll` | flight | `void(void)` | [flight.md 2.4] raw read + joy_scale X,Y -> 2CD2/2CD3 |
| 1181:0008 | `keypad_stick_install` | flight | `void(void)` | [flight.md 2.4] NumLock off, clear state, 2CD6/2CD7=80h, save INT 9, install keypad_stick_isr |
| 1181:0058 | `keypad_stick_remove` | flight | `void(void)` | [flight.md 2.4] restore INT 9 from cs:0187 |
| 1181:0072 | `keypad_stick_isr` | flight | `void interrupt(void)` | [flight.md 2.4] INT 9: keypad/arrow keys -> virtual stick bytes 2CD6/2CD7 (80h centre; 5Ah tap, 7Fh double tap), drops repeated identical... |
