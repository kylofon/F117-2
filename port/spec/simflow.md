# Sim flow: VGAME.EXE (S3.6)

**Status: S3.6 delivered** (2026-10-13). Symbols: `simflow_symbols.csv` (merged from twelve reader passes; 376 functions of VGAME, the
159 labels inside them and 530 DGROUP data symbols). Disassembly: `python tools/dsann.py VGAME 1e23 FROM TO` (FROM/TO = hex offsets in segment 0 or
`SSSS:OOOO` for a far segment; resolves call names, driver slots and DGROUP strings); cross references: `python tools/vgxref.py g|f|name ...`.
Coverage: `python tools/spec_coverage.py vgame` (74 functions are named in `platform_symbols.csv`/`seed_symbols.csv`, the other 376 here).
Compiler/ABI/DGROUP: MSC small model, near calls, args pushed right to left, DGROUP = 1E23 (**DS:oooo = unpacked image offset 0x1E230+oooo; = file
offset 0x1EA50+oooo in `work/unpacked/VGAME.EXE`, the MZ header is 0x820 bytes**). Claims are marked `(V)` verified in the disassembly or `(U)`
unclear / needs a runtime or visual check.

How the document is built: Part 0 (this part, written at the merge) gives the map, the lifecycle, the cross-part decisions and the reconciled names.
Parts 1-11 are the reader passes (each self-contained: algorithm per function, constants, globals, structures). **Where two parts disagree, Part 0
section 5 decides.** Rendering internals (S3.7) and the flight model / weapons / AI / detection / damage internals (S3.8) are specified in `render3d.md` (S3.7; Part 12
points there) and `flight.md` (S3.8: owns 42 functions of Parts 2-6, 8, 9 and corrects several statements there, see its section 8); the player flight model `1e9e` is specified in full in Part 2 because it is also the
input handler (S3.8 starts from there and from `port/notes/s36/r2_*.py`, an executable differential-tested reference).

## 1 Part map

| Part | Reader | Content | Functions |
|---|---|---|---|
| 1 | R1 | lifecycle (start-up order, frame order, shutdown), timer/ISR, data loading, mission world import/export (comm+7Ah), drivers, palettes, keypad-as-stick, leftovers | `0010 00f0 0233 02c2 02fe 097a..0ce7 1bb0..1e0b 4ad0..4d2f bd24 ea7c`, far `0ffd 1088 1181 1199 11b9 11ce 1359:00xx 14a2` |
| 2 | R2 | `1e9e` input + player flight model (bit-exact reference), fixed-point and orientation helpers, lamp/digit/screen helpers, sound gate, sim_rate helpers, joystick glue, cba0 key table appendix | `1e9e 2c6e..2f40 38ab..3939 8929..8bd3 c94e..cb72 d6bf..d9a3 e578`, `109d`, `10a8:0c87` |
| 3 | R3 | views (`[C07A]` modes), camera, cockpit overlays, `03ce` scene entry, horizon/sky, map MFD terrain, collision probes | `2f82 03ce 3968 8387 91a4 0d08 0f31 0ffb 77d9 119c..13e3 1442 17a3 04bb` |
| 4 | R4 | `39c4` simulation step (ordered call list), mission initialisation, end-of-mission codes, airfield/landing logic, event log, message queue users | `39c4 43ee..4f2a 4b57 4926 499e` |
| 5 | R5 | radar/visual detection, SAM and fighter events, event messages, name tables, moving-map primitives | `516d 5460 5588 559a 5730 62d0 65d4 8316..8970 94a5 973c 9770` |
| 6 | R6 | weapon release, impact and damage bookkeeping, objectives hit, scoring counters, store tables | `671a 711f 71b4 72a4 7470 74f6 753f 780a 784a` |
| 7 | R7 | HUD overlay (C part), left/right MFD pages, scope, ILS, nav page, objective text, text/number helpers | `7898 80a0 8113 81f6 829d 82e9 8498 84d6 873f..8bab 8c10 903f..9154 9208..97a4 99a7 4a73` |
| 8 | R8 | asm HUD (`10a8:000C`: tapes, ladder, reticle), `hud_color_init`/`hud_layout_init`, `9aca` object pass and target selector, projection `b85d` | `10a8:0000..0a22 9aca b85d ba13 bb86..bd8f c8fa cb39 dcc0 ddfe` |
| 9 | R9 | `a17e` per-frame overlay pass (tracers, explosion, target boxes, photo, beacon), object/site/projectile structure layouts | `a17e 1a85 b339..bee1 f492` |
| 10 | R10 | `cba0` key command interpreter (authoritative key table), `bf71` sensor window renderer | `cba0 bf71 1961 19ea c6de c73c c834` |
| 11 | - | function index (generated from `simflow_symbols.csv`) | all 376 |
| 12 | R11a/R11b | render3d: pointer table into `render3d.md` (S3.7 replaced the R11a/R11b interface extracts) | the S3.6 passes are kept in `port/notes/s36/spec_R11a.md`, `spec_R11b.md` as history |

## 2 Program map and lifecycle in one page

VGAME code = segment 0000 `0010..FFFF` (game code up to `ea7c`, MSC runtime above), then far code segments in the image: `0FFD` driver bind, `1001` 2D polygon
edge tables/clip (used for the sky/ground fill and rectangle fills), `1088` palette/DAC glue, `109D` sin/cos/multiply helpers, `10A8` hand-assembled HUD +
joystick, `1181` keypad-as-stick keyboard hook, `1199`/`11B9`/`11CE` DOS file + PIC decoder, `11EB` 3D object renderer, `12EF` polygon clipper,
`1359` raster back end and graphics-library glue, `1433` fixed-point maths, `14A2` DAC writer. Data segments `14B2/1853/1D50` and DGROUP `1E23`.

Lifecycle (details Part 1 section 0):
1. F117.COM EXECs VGAME after START (exit code of START 0Ch/0Dh). `main` (0010) binds the three drivers from the comm block, installs the break hook,
   joystick calibration, three pages, then `sim_init` (00F0): world import from `comm+7Ah`, `STFLT.3DX`, theatre `.3dG/.3DX/.3dT`, `speech.117`, flight palette,
   cockpit pictures into page 2.
2. `sim_run` (0233): timer ISR install (retrace calibrated), keypad stick hook, **frame loop** until `[E552] != 0`, sound off, world export to comm, restore timer.
3. Exit code `DS:008D` (initial 81h = go to END; 0 only after the quit requester "Y": text mode 3 and back to DOS/START loop).
4. Frame order (every frame, one sim step per frame): `2F82` view + 3D scene (`03CE`: sky/ground `DAE8`, terrain `04DA`, objects `9ACA`, `DB3E`, overlay `A17E`) ;
   `7898` HUD/messages ; if front cockpit (`[C07A]==0`): asm HUD `10A8:000C`, `D9A3`, `17A3` horizon ; `flip` ; `1E9E` input + player flight model ;
   `39C4` simulation step (mission init on the first call, map markers, `4F2A` site radar, `5730` aircraft, `671A` weapons, airfield/landing logic, mission clock,
   fps estimator, `43EE` -> `CBA0` key commands).
5. The mission ends only through `4926(code)` (sets `[E552]`); exit codes and the result fields written for END are in Part 4 section 2.2/2.3 and below.

Hand-off fields (comm = far pointer `DS:E548`, the 5120-byte block of F117.COM; pilot = `DS:98F4` = comm+1272h, **never written by VGAME**):

| comm | dir | meaning |
|---|---|---|
| 1Ah 1Ch 1Eh 20h | R | MGRAPHIC / sound / MISC driver segments; off-screen page segment (page 2, `[E558]`) |
| 26h | W | mission state: START 3, mission init 1 (flying), eject 2, normal end 3 |
| 28h | W | end code (`4926`): 0 landed, 1 stranded, 2 terrain collision, 5 hard landing, 6 fatal crash, 7 abort (no result written) |
| 2Ch 30h | W | kills counter (ground targets + aircraft); bit0 training/cheat flag |
| 32h | R | low byte -> `[98DE]` range-scale shift (threat range table `d76b`, scope zoom) |
| 33h | R | night flag -> `[3658]`, `[43B0] = day` |
| 34h 36h | W | damage bit mask `[3632]` / damage points `[C5C8]` |
| 38h..3Eh 40h..46h | R | loadout words (weapon id / quantity per bay) copied to `DS:363E+4i` |
| 48h (20 words) | R/W | joystick calibration (loaded into `DS:2CA2` at start, saved back at exit) when `comm[72h] & 1` |
| 72h | R | bit0 = joystick in use |
| 74h 76h | W | final player position (x, y level units) |
| 7Ah.. | R/W | mission world blob (sites, units, tables, names, grid, waypoints, mission slots) + 0x600-byte event log appended on export |
| pilot +38h theatre, +3Ah war level, +3Ch type, +3Eh enemy skill, +42h flight model | R | selects theatre files, difficulty scalers (`[3654]` skill, `[3656]` war) |

## 3 Conventions shared by all parts (reconciled at the merge)

* **Angles** are 16-bit, 0x10000 = 360 degrees (0x4000 = 90). Heading 0 = north, clockwise; pitch positive = nose up; roll positive = right wing down.
  Sine table `DS:2084`: 257 signed words, Q15 (Part 12). `caee(a,r) = r*sin(a)`, `cb07(a,r) = r*cos(a)` (`(table*r) >> 14`), `c9d6` = 16-bit atan2, `c987` =
  `max + min/2` distance capped at 0x7FFF, `c94e(v,lo,hi)` clamp (with the wrap quirk of Part 2), `cb62(n)` = `rand()*n >> 15`.
* **Pages**: 0 = A000 (`ctx0` DS:3FCC), 1 = back buffer (`ctx1` DS:3FE4, everything is drawn here, `flip` slot 44 copies `[2E34]` rows), 2 = pristine cockpit panel (`ctx2`
  DS:3FFC). `[4088]` = flip flag (page being drawn). MFD boxes (74,110,77x60) and (169,110,77x60) are copied between pages by `2F82` every frame.
* **Time**: `[3660]` = **`sim_rate`** (R4 calls it `fps`, R7 "frame-rate divider"): integer 4..15, initial 4, every per-frame increment of the flight model and every
  "n seconds" duration is `n * sim_rate` frames (messages `3*sim_rate`). Re-estimated every `4*sim_rate` steps from the tick counter `[2646]` (estimator in Part 4
  section 1.2). `[3D7A]` = "FrameRate" setting (1/2). One timer tick per video frame (`timer_tick_work`), see Part 1 section 1.
* **Messages**: `msg_post` = `8B93` (copies the string to `DS:C578`, `[409E] = 3*sim_rate`, drawn by `7898` at the top centre). Lamps: `898B` (`lamp_set`) -> `D83E`
  (button recolour through the table at `DS:46D8`).
* **HUD style** `[294B]` (R8 `hud_style`, R3 `fov_zoom`, R4 `hud_off`, R9 "reduced view window"): key `C` toggles 0/1; style 1 = compact layout and half-scale projection;
  `2F82`/`BF71` zero it temporarily for non-cockpit views. Canonical name `hud_style`.
* **Key tables**: `CBA0` Table A in Part 10 section 1.1 is authoritative for key meanings. Part 2 Appendix A is the mechanical code-to-handler list (same table),
  Part 7 "Key to MFD mapping" is a view of the MFD keys. `1E9E` keys (Esc, 0, - _ + =, Alt-Q/P/S/B, c) are in Part 2 section 2.1.
* **Owners** (column `owner` of `simflow_symbols.csv`): `simflow` (specified here), `render3d` (S3.7), `flight`, `weapons`, `ai`, `detect`, `damage` (S3.8), `loader`, `lib`.
  Names of other readers' functions are cited by address; `898B` = `hud_lamp_update` (R2) = `lamp_set`; `4F05` = `hud_lamp_set`.

## 4 Decisions and open questions that need the user (not settled by the spec)

* **D1 Timing base (PLAN.md U7 item 7).** The timer ISR gives exactly one tick per video retrace (70.09 Hz in mode 13h, calibrated; 60 Hz fallback `4DAEh`), while the
  fps estimator in `39C4` (`[3660]`, Part 4 section 1.2) assumes 60 ticks per second. Sound is polled once per tick but skipped on every 7th tick (60.08 Hz at 70 Hz) and
  `snd_noise` runs `class` times per frame in fast mode. **Decided 2026-10-13 (PLAN.md Decisions):** the port emulates this exactly: a 70.09 Hz retrace tick, the
  60-ticks/s estimator and the 7th-tick sound skip, bugs included. (The Enhanced repo will use its own 60 fps timing.) **S3.3 re-check needed**: `sound.md` section 3 assumes `snd_poll` at the ISR rate with no 7th-tick skip; Part 1 section 1 shows VGAME differs.
* **D2 Visual / runtime checks** are collected in the single User to-do row `U-vgame` of PLAN.md: rear-view centre piece, sensor window (position, labels, FLIR tint), moving-map
  signal bars / arcs colours, HUD tick-strip colour, tracer/explosion colour indices, nav-page ETA units, map zoom at start.
* **Other open items** are listed at the end of each part. The meaning of a few globals stays (U): `[9B04]` bit 1, `[3D6A]`, `[E2FE]`, `[3D74]`, `[C0A2]`, `[4088]` in the shutter test.
* **Original bugs to keep or fix in the port** (decide in phase 5; the parts list them with `bug`/`quirk`): `780A` called with a stale stack word, ground-impact test with a stale
  explosion position, invisible tracers skip hit tests, `B04C > 8` ignores slot 8, `ea7c` reads at most 2x65535 bytes, ladder label index -1 reads outside the table.

## 5 Resolved disagreements between parts

| item | decision |
|---|---|
| `[3660]` | `sim_rate` (Part 2/4 formulas); "fps" in Part 4/5/7 is the same variable |
| `[294B]` | `hud_style` (see section 3) |
| `[98DE]` | `comm+32h` low byte = range-scale shift, not an adapter code (RE_GUIDE wording is wrong for VGAME) |
| `cba0` | key command interpreter, not a menu (the "Detail Level / Sounds" lines are status messages); one key table (Part 10) |
| `a17e` | per-frame overlay pass of the 3D scene (tracers, explosion, target symbology), not an object update loop; units/sites/missiles are moved by `5730/65D4/62D0/671A/465B` |
| `1e9e` | input handler **and** the player aircraft model (S3.8 starts from Part 2) |
| `09CE hud_color_init` | falls through into `0A22 hud_layout_init` (no `ret`), so the HUD layout is initialised at start-up by `01BF` |
| `898B`, `D83E` names | `hud_lamp_update`, `hud_lamp_recolor` (not `hud_lamp_set`, which is `4F05`) |
| `1359`, `11EB` | the renderer has its own scan converter (segment 1359); no MGRAPHIC poly slots are used by 11EB/12EF/1359 |
| DGROUP offset base | image offset `0x1E230`, file offset `0x1EA50` (both statements in the parts are the same thing) |


---
# Part 1: lifecycle, timer, loading, world import/export, palettes (R1)

*Reader pass R1, original title: S3.6 reader R1 - VGAME lifecycle: start-up, loading, timer, hand-off, palettes, library glue*

Scope: segment 0 `0010..0cd1`, `1bb0..1e0b` (+ ISR code in index gaps), `4ad0..4d2f`, `bd24`, `ea7c`, RTL leftovers, and the far
segments 0ffd, 1088, 1181, 1199, 11b9, 11ce, 1359, 14a2:00C5, 10a8:0D70/0D82.
Notation: `(V)` verified in the disassembly, `(U)` unclear / needs a runtime check. DS = DGROUP = 1E23 (`DS:oooo`). Code segment
0 is written `0000:oooo` or plain `oooo`. All arguments are listed in C order (the compiler pushes right to left; first arg at `[bp+4]`).
Image offset of a far address = `SSSS*16+OOOO` in `work/unpacked/VGAME.EXE` minus the 0x820-byte EXE header. Initial values of DGROUP
data quoted below were read from the file image (`DS:oooo` = file offset `0x820 + 0x1E230 + oooo`; offsets beyond the file image are BSS = 0).

## 0. VGAME lifecycle (the numbered sequence)

Entry: F117.COM EXECs VGAME after START (START exits with 0Ch/0Dh, `shell.md` `76D1`). The C start-up `ebb4` (platform.md) calls `main`.

### 0.1 `main` (0010) - start-up order (V)
1. `comm = (far)MK_FP(*(u16*)0x04F0, 0)` -> `DS:E54A:E548`; pilot record far ptr `DS:98F6:98F4 = seg:1272h` (= comm+1272h).
2. Bind the three driver tables with `drv_bind(seg)` (`0FFD:0008`): MGRAPHIC `comm[1Ah]`, MISC `comm[1Eh]`, ?SOUND `comm[1Ch]` (this order). After this
   every `lcall 1E23:011A+5*slot` is live.
3. `break_hook_install` (1b60; INT 1Bh, platform.md).
4. Joystick: if `comm[72h] & 1` (answered Y in SETUP) -> `joy_set_calib(far comm+48h)` (10A8:0D82: copy 20 words to `DS:2CA2`);
   else `DS:2CD2 = DS:2CD3 = 80h` (both stick axes centred).
5. `gfx_pages_init` (02c2): page 0 = `gfx_alloc(0)` = A000h -> `[85E2]`,`[85EA]`; page 1 = `gfx_alloc(1)` -> `[85E4]`, `page_seg_set_b(seg,1)`;
   page 2 = `comm[20h]` -> `[85E6]`, `page_seg_set_b(comm[20h],2)`.
6. `page_select_1` (slot 12): draw segment = page 1. `gfxlib_mode_init('M')` (1359:0000, row table for MCGA, see 1359).
7. `[E558] = comm[20h]` (off-screen page segment, used by the asm HUD 10A8 and 9154/91A4).
8. `sim_init` (00f0) - all loading, section 0.2.
9. `0000:44A9` (weapon quantity labels: for i=1..3 `44C4(i)` prints `[3636+2i]` into the HUD; sim/cockpit owner).
10. `sim_run` (0233) - section 0.3; returns when `[E552] != 0`.
11. If `comm[72h] & 1`: `joy_get_calib(far comm+48h)` (10A8:0D70: DS:2CA2 -> comm+48h, so calibration changes learnt in flight are kept).
12. `break_hook_remove` (1b83).
13. If `DS:008D == 0` (the player confirmed "quit"): `regs.ah=0; regs.al=3; int86(0x10,...)` = BIOS text mode 3. (Not done for the debrief path: END expects graphics.)
14. `exit(DS:008D)`. **`DS:008D` has the initial value 81h** and is set to 0 only by the quit requester (`1e9e` at 20C1, answer "Y"). So the process exit
    code is **81h = go to END (debrief)** or **0 = quit to DOS/START loop** (RE_GUIDE: "VGAME: 0 = quit, else debrief"). (V)

### 0.2 `sim_init` (00f0) - load order (V)
1. `mission_world_import` (4ad0): unpack the mission world from `comm+7Ah` into DGROUP (section 4), build the name pointer table, compute the start
   position `DS:BA10/BA12` and `DS:C02A/C02C` from the anchor site `comm slot0.anchor`.
2. `load_stflt_3dx` (bd24): `STFLT.3DX` -> object bank 0.
3. `strcpy(DS:[008E]=DS:0042 stem buffer, DS:[0090 + 2*pilot.theatre])`: stem string of the theatre (`lb.xxx pg.xxx nc.xxx ce.xxx vn.xxx me.xxx ku.xxx cu.xxx ko.xxx`).
   `pilot.theatre = pilot[+38h]` (0..8).
4. `load_theatre_data` (097a): `.3dG`, `.3DX`, `.3dT` of the stem (sections 2.x).
5. `file_load_paras("speech.117")` (ea7c, name DS:00F7): `[008A]` = segment (0 = file missing, silently ignored). If non-zero:
   `snd_cmd(40, seg, size_lo=[C074], size_hi=[C076])` (slot 101). Speech is thus resident for the whole mission (26 408 bytes).
6. `screen_off` (slot 69): the display stays blank until `screen_on` at 3B65 (first mission frame).
7. `[3658] = (u16)comm[33h]` (night flag 0/1). `[0D47] = climate_tbl[pilot.theatre]` (DS:00EE: `06 06 02 02 02 06 06 02 02`).
   `[43B0] = ([3658] < 1)` i.e. **1 = day**, 0 = night. `[008C] = 1` (write-only flag).
8. `pal_flight_load` (1088:0006): build the 256-colour flight palette in `DS:104A` and program the DAC (section 6).
9. `8929(0)`: text colour 0 in both page text contexts (`[3FE2]`->ctx 3FCC (page 0), `[3FFA]`->ctx 3FE4 (page 1)).
10. Twice `1001:024C(ctx, x0=0, y0=0, x1=319, y1=91)` (fill rectangle with the ctx colour 0): clears the 3D window rows 0..91 on page 0 and page 1.
11. `pic_show("256pit.PIC", page 2)` (d7f4): the front cockpit picture into the off-screen page 2.
12. `ordnance_panel_build` (02fe): weapon name strips, section 3.
13. `8BD3(0, 0x5B, 0x140, 0x6D)`: `blit_rect(2, 0,0x5B, 0, 0,0x5B, 320,109)` then `blit_rect(2,0,0x5B, 1, 0,0x5B, 320,109)`:
    the lower cockpit (rows 91..199) from page 2 onto page 0 (A000) and page 1.
14. falls into `cockpit_daynight_recolor` (01bf): `10A8:0004` (asm HUD descriptor init, 10A8:09CE) then the three recolour rectangles (section 3).

### 0.3 `sim_run` (0233) (V)
1. `DS:98E6:98E8 = 0000:0440` (BIOS data 0040:0040, floppy motor-off counter); if `*(u8*)ptr > 1` then `= 1` (motor stops after one BIOS tick). Nothing else uses it.
2. `timer_install` (1bfc), section 1.
3. If `!(comm[72h] & 1)` (no joystick): `keypad_stick_install` (1181:0008).
4. `frame_loop` (1bb0 -> 1bbc) - section 0.4. Returns when `[E552] != 0`.
5. Shutdown: `snd_cmd(23, 0)` (engine off, `eng_value = 0`), `snd_cmd(0)` (reset all voices).
6. `mission_world_export` (4c29): pack the (modified) world + event log back to `comm+7Ah` (section 4).
7. If `[BA0E] != 0` `dos_freemem([BA0E])` (ordnance sprite). If `[008A] != 0` `snd_cmd(40, 0,0,0)` (detach speech).
8. If `!(comm[72h]&1)`: `keypad_stick_remove` (1181:0058).
9. `shake_set(1)` (slot 79), `timer_wait_ticks(2)` (04BB: wait 2 ISR ticks so slot 46 resets the CRTC start address).
10. `jmp 1C3A` = `timer_remove` (restore PIT ch.0 to 18.2 Hz, INT 8 vector); its `ret` returns to `main` (0233 has no frame; it is called from main).
    Note the pages, palette and DAC are **not** restored; the next program (END) loads its own.

### 0.4 Frame order (`frame_loop_body` 1bbc, loops until `[E552] != 0`) (V)
```
repeat {
  2F82()                       // view selection + 3D scene (S3.7 parts) into the back page
  7898()                       // HUD / overlays / messages
  if ([C07A] == 0) {           // front view only
      10A8:0000()              // asm HUD (10A8:000C)
      D9A3([4088] & 0xFF)      // ([4088] = which text-ctx/page is the back buffer)
      17A3()
  }
  flip(slot 44, words = [2E34]*160)   // copy page 1 rows 0..[2E34]-1 to A000 ([2E34] = visible 3D rows, initial 91)
  [262A] = 1                   // "frame presented"; cleared by the next timer tick
  1E9E()                       // keyboard / joystick / quit requester (first call also does one-time input init, [C0AE] 0->1)
  39C4()                       // simulation step + messages (first call also does the mission init, [C0AE] 1->2)
} until ([E552] != 0)
```
`[C0AE]` (word, BSS 0) is the start-up phase: 0 = nothing initialised (1E9E runs its one-time init: clears the control accumulators `B052 2DF6 2DF4 B170 2DFE 2DF2 2DF0`, `[2DEE] = ([3D7C+2*theatre] > 0) ? 0 : 8000h`, `byte[2DEF] += 4` if the anchor site has flag 0200h, then `2E43`, `3926`, and sets 1); 1 = 39C4 runs the mission init block (3A03..3B65: `4B57`, clears ~30 state words,
`[2646]=0Ch`, `[3660]=4`, copies comm loadout `comm[38h..]`/`[40h..]` to `DS:363E+4i`, `screen_on`, sets 2); 2 = normal running. Page flip is **not** a vsync wait; the
only synchronisation is `[262A]` (section 1.5): code in the render chain (`DAE8`) spin-waits `while ([262A])`.

### 0.5 Quit / end of mission - what ends the loop and what END receives (V)
Everything funnels into `mission_end(code)` (`0000:4926`, owner simflow; sets `[E552]=1`):
* `code == 7`: only `[E552]=1` (nothing written; the quit requester also sets `[008D]=0`).
* otherwise (and only if `[C06E]==0` (not ejected) or `code==0`): `[E552]=1`; `comm+28h = code`; if `code==0 && [C06E]==0`: `comm+26h = 3`; then
  `comm+74h = [C0A4]` (final x in level units), `comm+76h = [C0B2]` (final y in level units, y runs south), `comm+34h = [3632]`, `comm+36h = [C5C8]`;
  finally `mission_log(type=8, arg=0)` (499E) appends the "end" record to the event log.
* `comm+26h`: START writes 3; mission init (39C4:3B0F) writes 1 (flying); the eject code (`D028`) writes 2 and sets `[C06E]=1`; normal end writes 3. (U: END's use.)
* `comm+2Ch` is incremented per destroyed ground target (`5433`) and per destroyed aircraft (`6586`), `comm+30h |= 1` when the training/cheat toggle `[9B05]^=10h` ends up on (`CE08`, U).
* `comm+34h` (= `[3632]`) is a 16-bit damage bit mask (74F6 sets one random bit 0..7 per hit event), `comm+36h` (= `[C5C8]`, initial 2) the damage count (U: both consumed by END).
* Callers and codes: 20BD `7` (quit requester, "Y"); 23FF `0`; 414E `0` after the "Safe Landing" message ("Cargo delivered" is the other message, strings 2E4E/2E5E) when the landing
  dwell counter `[3D9A]` exceeds `16/[3660]`; 41B6 `1` (after 0x78 ticks and a full-screen text); 41F8 `2` (screen off, 0x78 ticks); 2B65 `5` (after 0x3C ticks); D01E `6` (eject branch). Meaning of
  1/2/5/6 is the simflow reader's (U here).
* VGAME **never writes** the pilot record (`comm+1272h`): all `les ..,[98F4]` uses are reads (`+38h theatre, +3Ah, +3Ch, +3Eh, +40h, +42h`). Score, medals and roster update are END's job.
* The world blob at `comm+7Ah` is rewritten by `4C29` with the final site/unit state followed by the 0x600-byte event log (section 4).

## 1. Timer / frame pacing (0000:1bfc .. 1e0b, ISR code in index gaps) (V)

Same design as the START/END timer (platform.md section 4, which has the PIT arithmetic) with a VGAME-specific tick body. Symbols:

| DS | size | name | meaning |
|---|---|---|---|
| 262A | b | `timer_frame_flag` | set to 1 by the frame loop after `flip`; cleared by the ISR tick. `DAE8` spins on it. |
| 262B | b | `timer_installed` | 1 after `timer_install`, 0 after `timer_remove` |
| 262C | d | `timer_clock32` | 32-bit PIT clock accumulator: `+= [2632]` per ISR, `+=` measured sum in calibrate. Only its high word is tested (BIOS chain). |
| 2630 | w | `timer_reload_nom` | nominal PIT reload per ISR = `[263B]/[2636]` |
| 2632 | w | `timer_reload_cur` | reload actually programmed (copied from 2630 at resync) |
| 2634 | w | `timer_resync_count` | resync counter (write-only) |
| 2636 | w | `timer_isr_div` | ISR calls per tick: 1 idle, class 4..6 when sound is fast (see tick body) |
| 2638 | b | `timer_resync_cd` | tick countdown to the next retrace resync (reset to 20) |
| 2639 | w | `timer_class` | rate class 4..6 (= ISR calls per frame in fast mode) |
| 263B | w | `timer_clocks_frame` | PIT clocks per video frame (17024 at 70.09 Hz, fallback 4DAEh = 19886 = 60 Hz) |
| 263D | w | (scratch) | high word of the calibration sum |
| 263F | b | `timer_resync_on` | 1 = phase resync enabled |
| 2640 | w | `timer_isr_cd` | ISR countdown to the next tick |
| 2642 | w | `timer_phase_err` | last resync phase error (write-only) |
| 2646 | w | `tick_count` | **ticks since reset**; 39C4/1E9E/3939 read and reset it (frame-time scaling: `[3660]`, `[E54E]`) |
| 2648 | w | `tick_count2` | incremented, never read |
| 264A | b | `tick_byte` | read by `04BB timer_wait_ticks(n)` (busy-wait n ticks) |
| 264B | b | `snd_skip_cnt` | modulo-7 counter for the sound poll |
| 2624 | b | `break_flag` | set to FFh by the INT 1Bh handler; **never read** in VGAME |
| 2625 | d | `old_int1b` | saved INT 1Bh |
| 1CEB | d (cs) | `old_int8` | saved INT 8 vector, stored into the operand of the `ljmp 0:0` at 1CEA |

### 0000:1BFC `timer_install(void)`
`[2636]=1; [2640]=1; [262C]=[262E]=0; timer_calibrate(); (cs:1CEB,1CED) = INT 8 vector (INT 21/35); INT 8 := cs:1C89 (INT 21/25, the far pointer at cs:1C85 = offset 1C89 +
segment patched by the EXE relocation); [262B]=1`. The PIT is not reprogrammed here: it still runs at 18.2 Hz until the first tick resync (the resync countdown
`[2638]` is 1 after calibration, so the first tick resyncs and loads the real reload).

### 0000:1D76 `timer_calibrate` (named in platform_symbols) (V)
IF=0. `[2638]=1; [263F]=1; sum=0`. `p = pit_read_retrace()`; 16 times: `q = pit_read_retrace(); sum += (u16)(p - q); p = q`. `[262C:262E] += sum;`
`avg = sum/16; [263B] = avg>>1` (mode-3 counter decrements by 2 per clock); `class = (([263B] + [263B]>>4) / 0F89h)`. If `4 <= class <= 6`: `[2639]=class` else
`[263F]=0; [263B]=4DAEh; [2639]=5`. `if ([2636] != 1) [2636]=class` (unchanged on the first call); `[2632]=[2630]=[263B]/[2636]`.

### 0000:1E0B `pit_read_retrace(void) -> u16` (V)
IF=0. Port = `*(u16*)0040:0063 + 6` (3DAh colour / 3BAh mono). Colour: wait (max 65 536 polls, then return 0) until status bit 3 (vertical retrace) is **clear**, then until it is **set** (start of
retrace); mono: same with bit 7. Then `out 43h,0` (latch ch.0), read lo, hi -> return `hi:lo`.

### 0000:1C89 `timer_isr` (INT 8, V)
```
sti; push all; DS = 1E23;
[262C:262E] += [2632];                       // 32-bit clock
if (--[2640] == 0) {
    [2640] = [2636];
    timer_resync();                          // 1CEF, only every 20th tick
    [262A] = 0;                              // frame flag
    timer_tick_work();                       // 1E67
}
if ([2636] != 1) snd_noise();                // slot 104, every ISR call in fast mode
if ([262E] == 0) { out 20h,20h; pop all; iret }   // EOI
else { [262E]--; pop all; cli; jmp far old_int8 }  // BIOS handler (sends EOI itself) once per 65 536 clocks
```
### 0000:1E67 `timer_tick_work` (V)
```
[2646]++ (w); [264A]++ (b); [2648]++ (w);
explosion_fx_tick();                         // slot 46 (video.md: DAC[13] flicker + CRTC shake)
if (++[264B] >= 7) { [264B] = 0; r = 0; }    // every 7th tick the sound poll is SKIPPED
else r = snd_poll();                         // slot 103
if (r > 0)      jmp 1C5B  (timer_rate_fast)  // [2636]=[2639]; [2630]=[263B]/[2636]; [2638]=1
else if (r < 0) jmp 1C73  (timer_rate_idle)  // [2636]=1;       [2630]=[263B];       [2638]=1
```
Effect: a tick happens once per `[2636]` ISR calls, and `[2636]` is the same as the ISR rate multiplier, so **exactly one tick per video frame** in both modes. In idle mode (`[2636]=1`, one ISR per frame) every ISR is a tick;
in fast mode (poll result > 0, `[2636]=class` 4..6) the PIT reload is `frame/class`, the ISR runs `class` times per frame, only every `class`-th call is a tick, and **`snd_noise` (slot 104) runs on every ISR call**
(`class` x per frame) while `snd_poll` still runs once per frame. The poll is skipped on every 7th tick, so the sequencer clock is 6/7 of the frame rate = **60.08 Hz at 70.09 Hz** (the "60 Hz" of S2.4).
(V; START's ISR `8D1E..8D5E` has the same structure but no 7-tick skip. This differs from sound.md section 3, which assumes `snd_poll` at the ISR rate in fast mode - see Open items 6.)

### 0000:1CEF `timer_resync` (called from the ISR once per tick; acts every 20th tick) (V)
`if (--[2638] != 0) return; [2638]=20;` if `[263F]` (resync on): spin (interrupts toggled) on the status port: if bit 3 (retrace) is already set -> fall to the adjust; else loop up to 65 536 display-enable
toggles (`bl` tracks bit 0) until retrace appears; if it never appears, `[263F]=0` (disable forever) and return. Adjust (1D42): `if ([2632] != [2630]) [2632]=[2630]`; **reprogram PIT ch.0** (`out 43h,36h`, lo, hi of `[2632]`) which restarts
the countdown at the retrace edge; `[2634]++; [2642] = -cx` (phase error); `if (cx != 0) [2640]++` (delay the next ISR by one call).
**Port:** drop all of this. A host frame callback replaces the ISR: per frame do `timer_tick_work` (counters, slot 46, 6-of-7 sound poll) and clear `timer_frame_flag`; in fast sound mode call
`snd_noise` `class` times per frame (class 4 at 70 Hz) in addition (the poll itself stays once per frame). `[2646]` must count frames since the sim last reset it.

### 0000:1C3A `timer_remove` (entered by `jmp` from 0233) (V)
`out 43h,36h; out 40h,0; out 40h,0` (18.2 Hz default), INT 8 := `cs:[1CEB]` (INT 21/25), `[262B]=0`, `ret`.

### 0000:04BB `timer_wait_ticks(n)` (not in R1's list; used by 0233): if n>0: `target = n + [264A]` (byte); spin until `[264A] == target`.

## 2. Data loading

### 0000:EA7C `file_load_paras(const char *name) -> u16 seg` (V)
`[C074:C076] = 0` (size out). `fp = fopen(name, "r+b")` (mode string DS:4EB2; **opens read/write, a read-only file fails**; the port should open read-only); 0 -> return 0.
`size = filelength(fp->fd)` (f186; FILE.fd is at `fp+7`); -1 -> return 0. `free = mem_paras_free()` (slot 50); `need = (size+15)>>4`; if `need > free` -> close, return 0.
`dos_allocmem(need)` (f348; INT 21/48, returns the segment) - non-zero error -> return 0. `[C074:C076] = size`. Read loop with `int86x(21h)` AH=3Fh (regs at DS:C5CA: BX=handle, DS:DX = seg:offset):
while `size > 0xFFFF`: read 0xFFFF bytes at offset `off` (starts 0, then `off -= 1` each pass, i.e. 0, FFFF, FFFE ...), `size -= 0xFFFF`. Then read the rest at offset `off`. Close; return the segment.
**Original bug:** the second and later chunks use `seg:FFFF`, `seg:FFFE`, ... instead of advancing the segment, so files larger than 2x65 535 bytes would be corrupted (no shipped file is that large; the largest read this way are the .3DX banks). The file is loaded whole at offset 0 of a fresh DOS block; the block is never freed, except `[BA0E]`.
Errors never exit here (returns 0); callers decide: `0994`/`bd24` call `load_error_msg`; `speech.117` ignores it.
Callers: 00F0, 0994, BD24. Port: `load_file(name, &size)` returning a malloc'd buffer; segments become indices into an arena.

### 0000:0CD1 `load_error_msg(const char *msg)` (V)
`8AA2(msg, x=0, y=0x60, colour=0x0F)` (HUD text on the current back page), then `getch()` (f0e0, waits for a key), then returns **without exiting**. Callers continue with an unloaded bank (stale segment variable) - in the original this is a crash; the port should `fatal()`.
Messages: DS:0CB1 `"Load Error on *.3DX"`, DS:0CCF `"Load Error on PHOTO.3DX"`, DS:4322 `"Load Error on STFLT.3DX"`.

### 0000:0CE7 `set_file_ext(char *name, const char *ext)` (V)
Scans `name` for the first `'.'` or NUL and `strcpy`s `ext` there. Used to turn the stem `"lb.xxx"` into `"lb.3DX"`, `".3dT"`, `".3dG"` (ext strings DS:0CAC `.3DX`, 0CE7 `.3dT`, 0CEF `.3dG`).
The stem string is **modified in place** (so after `097A` the stem buffer holds the last name `xx.3dT`; this is why `0994` and `0AF9` receive the pointer again and re-set the extension each time).

### 0000:097A `load_theatre_data(void)` (V)
`load_3dg_terrain(); load_3dx_banks(DS:[008E]); load_3dt_scenery(DS:[008E]); [0CA8] = 0` (0CA8: word, purpose unknown, `ds_0CA8_hint`, reset only here).

### 0000:0C2C `load_3dg_terrain(void)` (V)
`set_file_ext([008E], ".3dG"); fp = fopen([008E], "rb")` (DS:0CF4 = `"rb"`); then `fread(buf, size, count, fp)`:
`(75E, 2, 1)` magic word (`'22'`, DS:075E); `(9F68, 1, 16)` (16 bytes, overwritten by the next read); `(9F68, 1, 256)` = **level-3 16x16 type map**;
`(9D10, 1, 512)` = level-2 child table; `(9B08, 1, 512)` = level-1; `(9904, 1, 512)` = level-0. `fclose`. Finally
`memcpy(DS:B00C, DS:0A68 + 64*pilot.theatre, 64)` = level-4 8x8 tile table of the theatre (FORMATS.md "Terrain"). No error checking (a missing file crashes in the RTL).

### 0000:0994 `load_3dx_banks(char *stem)` (V)
1. `set_file_ext(stem, ".3DX"); seg = file_load_paras(stem)`; 0 -> `load_error_msg("Load Error on *.3DX")` and return.
   `[B760] = seg` (bank 1). At `seg:0`: `n = word; [0610] = n`; offsets table `n+1` words; then `n` flag bytes at `seg:2+2*(n+1)`.
   `for i<n: DS:0960[i] = DS:07E0[i] = flag[i]` (07E0 = pristine copy, 0960 = working copy modified in flight, e.g. the "destroyed" bit); `DS:0510[2*i] = 0x0100 | i` (object id table, bank 1).
2. `seg = file_load_paras("photo.3dx")` (DS:0CC5); 0 -> `load_error_msg("Load Error on PHOTO.3DX")`, return (the extra entries of step 3 are skipped).
   `[B762] = seg` (bank 2). `m = word at seg:0; for j<m: DS:0860[j] = photo flag j` (flags at `seg:2+2*(m+1)`).
3. Mission target props: for the two mission slots (`slot0 flags hi byte = DS:E2DF`, `slot1 = DS:E2F1`; each nonzero byte `t` is a PHOTO.3DX object index):
   append at index `i = n (first) / n+1 (second)`: `DS:0960[i] = DS:0860[t]; DS:0510[2*i] = 0x0200 | t` (id word with bank 2). `[0610]` is **not** incremented (the count `n` stays the file's count).
   `4B57` later uses the same indices `n + k` (k = running count of non-zero slots), so both agree.
4. If `pilot.theatre == 6` (KU = Kuwait): remap two bank-1 entries onto PHOTO objects: `DS:05C2 = 0x020D; DS:09B9 = DS:086D` (index 0x59 -> photo 0x0D), `DS:051A = 0x020A; DS:0965 = DS:086A` (index 5 -> photo 0x0A). (V)

### 0000:BD24 `load_stflt_3dx(void)` (V)
`seg = file_load_paras("stflt.3dx")` (DS:4318); 0 -> `load_error_msg("Load Error on STFLT.3DX")`. `[B75E] = seg` (bank 0). `n = word at seg:0`; flags at `seg:2+2*(n+1)`:
`for i<n: DS:0760[i] = flag[i]; DS:09E0[i] = 6` (09E0 = per-object byte, set to 6 for every bank-0 object; use unknown: `ds_09E0_hint`).

### 0000:0AF9 `load_3dt_scenery(char *stem)` (V)
`set_file_ext(stem, ".3dT"); fp = fopen(stem, "rb")`. `fread(612, 2, 1)` magic; `fread(614, 2, 5)` = `type_count[5]` (DS:0614, = 32 each);
`for L<5: fread(61E + 64*L, 2, type_count[L])` = **record count per (level L, type t)** at `DS:061E + 64*L + 2*t` (u16).
Then records, level-major then type-major: `p = A06A` (cursor); for `L<5, t<type_count[L]`: `DS:E304[2*(32*L+t)] = p` (**near pointer to the first record of that (level,type)**, the table
at `E304` is 5x32 words); then `count[L][t]` times: `fread(p+0,2,1)` x, `fread(p+2,2,1)` y, `fread(p+4,2,1)` z, `fread(&w,2,1)`; `*(u8*)(p+6) = (u8)w` (object id **low byte only**); `p += 7`.
In-memory record = 7 bytes `{s16 x, s16 y, s16 z, u8 obj}` (file record = 4 words). `fclose(fp)`. Table `E304` is read by the terrain/scenery code (`0865`, `04DA`, S3.7). No size check on the A06A area.

## 3. Cockpit / ordnance panel set-up

### 0000:02FE `ordnance_panel_build(void)` (V)
Builds the weapon name list (77x15 px strips) of the cockpit weapons panel into a sprite kept in `[BA0E]`.
1. `y = 0x6E`. `pic_show("ordnance.pic", page 1)` (DS:010D).
2. `N = (pilot[+42h] == 0) ? 4 : 2` (flight model: 0 = MicroProse = 4 weapon bays, 1 = Lockheed = 2 bays, shell.md).
3. For `i < N`: `w = comm[38h + 2*i]` (loadout id, DS record base `3678 + 26*w`, 26 bytes); `spr = *(u16*)(0x367A + 26*w)`;
   `blit_rect(src page 1, sx = *(u16*)(0xA2+4*spr), sy = *(u16*)(0xA4+4*spr), dst page 2, dx = 0xA9, dy = y, w = 0x4D (77), h = 0x0F (15))`; `y += 0x0F`.
   (Source table DS:00A2 = `{x,y}` per sprite index: `(9,25h) (9,40h) (9,59h) (9,75h) (9,8Eh) (9,ACh) (68h,25h) (68h,40h) (69h,59h) (6Ah,75h) (6Ah,8Fh) (6Ah,ACh) ...`.)
4. `color_set(0x2F)` (slot 33); `page_select(2)`; `DS:2299 = 0xA9; DS:229D = DS:229F = 0x6E; DS:229B = 0xF5; 1001:032A()` = draw the line (169,110)-(245,110) colour 2Fh on page 2 (line slot 31) = top rule of the list; `page_select(1)`.
5. `pool_init()` (slot 76). If it returns non-zero: `[BA0E] = sprite_grab(page 2, 0xA9, 0x6E, 0x4D, 0x3C)` (slot 77: 77x60 RLE sprite of the list); if the grab succeeded call `pool_free` (slot 78: shrink pool to what was used).
   `[BA0E]` is read by `2F82` (draws it) and freed with `dos_freemem` at shutdown. If `pool_init` fails `[BA0E]` stays 0 and the panel is not drawn.

### 0000:01BF `cockpit_daynight_recolor(void)` and 0000:01B7 `cockpit_palette_refresh(void)` (V)
`01B7` = `pal_flight_load()` then fall into `01BF`. `01BF`: `10A8:0004()` (asm HUD init `10A8:09CE`: copies `[E558]` into four descriptors, and sets HUD colours by `[43B0]`: day -> `[2850]=[2866]=[2892]=0Fh, [28BD]=[28DB]=[2CA0]=0Fh, [287C]=[2CA1]=07h`; night -> `0Ah` and `2Fh` - V).
`(from,to) = [43B0] ? (0Fh,0Ah) : (0Ah,0Fh)`. Three `rect_recolor(page ctx [4012] (= page 2 ctx 3FFC), x0,y0,x1,y1, from, to)` (slot 41): `(0,0,140,29)`, `(0,30,64,37)`, `(38,75,91,87)` (these are the picture's instrument areas). `01B7` is called from `cba0` (day/night or palette change in flight).

### 0000:02C2 `gfx_pages_init(void)` and stubs (V)
See 0.1 step 5. `02C0`, `02C1` are single `ret` stubs (never called).

## 4. Mission world import / export (comm+7Ah) (V)

START `wld_export_to_comm` (5625, shell.md) writes the world into `comm+7Ah`; VGAME reads it back with the same field order and writes it back at the end. VGAME does **not** read `TEMP.WLD` and has no `.WLD` string.

### 0000:4D2F `wld_blob_rewind(void)` -> returns 0 (V)
`DS:94EC:94EE = comm + 7Ah` (cursor).

### 0000:4CEF `wld_blob_copy(void *p, u16 n)` (V)
`if ([DE8E] != 0) movedata(cursor -> DS:p, n) (import) else movedata(DS:p -> cursor, n) (export); cursor += n` (offset only; the blob is < 64 KB).

### 0000:4C41 `wld_blob_fields(void)` (V) - the blob layout, in order
| order | DS | size | START name | meaning |
|---|---|---|---|---|
| 1 | DEA4 | 1 | D88C lo | `wld_obj_a`: theatre special object index A (first byte of the .WLD file: 49 CE/CU/KO/VN, 4E KU/ME/PG, 4A LB, 42 NC) compared in `0F31` |
| 2 | C0AC | 1 | D88C hi | `wld_obj_b` (second byte: 5B/58/4E) |
| 3 | C0B4 | 2 | D33C | `site_count` |
| 4 | 98FC | 2 | CACE | number of leading sites with a threat level |
| 5 | E540 | 2 | E084 | first non-target site index |
| 6 | B2A2 | 16*site_count | CBDE | site records (16 B) |
| 7 | DED0 | 2 | D890 | `unit_count` |
| 8 | C13E | 36*unit_count | D3BE | unit records (36 B) |
| 9 | C604 | 128 | D7F6 | object-class table (indexed by object type, 7 bits) |
| 10 | C0BA | 128 | D33E | table 2 |
| 11 | DFEA | 750 | DD70 | NUL-separated names |
| 12 | B174 | 256 | CAD8 | 16x16 level-3 grid |
| 13 | DE88 | 2 | D876 | flight-time estimate (ticks) |
| 14 | 3D6E | 2 | CABC | special unit index (-1 none) |
| 15 | 2E6C | 16 | 15B2 | waypoints (4 x 2 words) |
| 16 | E2D8 | 36 | E05E | two 18-byte mission slots (see shell.md; slot0 `E2D8`: +0 kind, +2 target site `E2DA`, +4 anchor/departure site `E2DC`, +6 flags (`+7` = PHOTO.3DX target prop index `E2DF`), +8 template, +A grid ref string) |
Export (4C29) additionally appends **17**: `DS:BA2A`, 0x600 bytes = the mission event log (section 4.3). Note START's D2A6 table is not exported.

### 0000:4AD0 `mission_world_import(void)` (V)
```
wld_blob_rewind(); [DE8E] = 1; wld_blob_fields();           // unpack
DS:DEDC[0] = DFEA; j = 1;                                    // name pointer table (128 words), entry 0 = the empty string
for (i = 0; i < 0x2EE; i++) if (DFEA[i] == 0 && j < 128) DEDC[j++] = &DFEA[i+1];
s = [E2DC] (slot0 anchor site);  X = (u16)site[s].x  (DS:B2A4+16*s);  Y = (u16)site[s].y (DS:B2A6+16*s)
[BA10:BA12] = ((u32)X << 5) + 2;        // player start x, 1/32 level units
[C02A:C02C] = ((s32)(0x8000 - Y)) << 5; // player start z (north-up terrain y); sign from the borrow
```
(The `+2` is in the original; `39C4` then adds `0x10` and shifts to get `[C0A4]=(x+16)>>5`, `[C0B2]=0x8000-((z+16)>>5)` as the level-unit position.) 39C4's first-frame block adjusts the start: if the site flag `+9 & 2` (airfield, flag 0x200): `x -= [B16E]<<7` and `[9B04] |= 8`, else `z -= [B16E]*0x708`. Callers: 00F0 only. Writes `DS:DE8E` (left at 1).

### 0000:4C29 `mission_world_export(void)` (V)
`wld_blob_rewind(); [DE8E] = 0; wld_blob_fields(); wld_blob_copy(DS:BA2A, 0x600)` - always called at the end of `sim_run`, **including the quit path**.

### 4.3 Event log `DS:BA2A` (0x600 bytes = 256 six-byte records), count `DS:94EA` (V) - `0000:499E mission_log(u8 type, u8 arg)`
`if (count < 255) { r = BA2A + 6*count; r.w0 = [98E2] (mission tick); r.b2 = [C0A4]>>7; r.b3 = [C0B2]>>7; r.b4 = type; r.b5 = arg; count++; BA2A[6*count + 4] = 0 (terminator type 0) }`.
Types seen: 4 (arg = weapon slot `[364E]` fired, from 75F7), 8 (mission end, from 4926). Others are written by other readers' code (grep `call 0x499e`). END reads the log from the blob (S3.9).

### 0000:4B57 `mission_target_objects_setup(void)` (V; called once from 39C4 mission init)
`k = [0610]` (first free object index = file's bank-1 count); for slot `i = 0, 1` (slot base `E2D8 + 18*i`): if `slot.flags_hi (E2DF + 18i) != 0`:
`site = slot.target (E2DA + 18i)`; `rec = tile_object_at(x = (s32)site.x<<5, z = (s32)(0x8000 - site.y)<<5)` (`0D08`, returns a pointer to the scenery record or 0);
if `rec != 0`: `cls = rec[0] & 0x7F; DS:C604[k] = DS:C604[cls]` (copy the object-class byte); `strcpy(DEDC[k], DEDC[cls])` (copy the class name string over name slot k) and `DEDC[k+1] = DEDC[k] + strlen + 1`; `0F31(rec, 0x100 + k)` (re-register the scenery record under the new bank-1 object id; also the mission-override table `B772`, 5 bytes per entry, count `9902`).
In all cases (also when `rec == 0`): `site.objid (B2A2+16*site+0Eh) = 0x100 + k; k++` only for non-zero slots. At the end `[3D76] = 0`.
Effect: the primary targets get private copies of their scenery object so they can be shown/destroyed individually, and use the PHOTO.3DX prop (index `slot.flags_hi`) through the `0994` step 3 mapping. (U: the name overwrite assumes spare name slots; the START generator must reserve them.)

## 5. Drivers, DOS file layer, graphics library glue

### 0FFD:0008 `drv_bind(u16 seg)` (V) - called 3x from main
Driver header at `seg:0000`: `[18h]` = driver code segment, `[1Ch]` = first slot number, `[22h]` = slot count, `[24h..]` = near offsets. For `k < count`:
`DS:011A + 5*(first+k) + 1 = offset[k]; DS:011A + 5*(first+k) + 3 = codeseg`. The slot stubs (5 bytes, opcode `EAh` far jmp, pre-filled in the image) are then `lcall 1E23:011A+5*slot`. Slot numbering: MGRAPHIC 0..83, MISC 90..95, ?SOUND 100..104.
Port: static function-pointer array indexed by slot; this function disappears.

### 1199:xxxx DOS file layer (V; fatal-on-error, platform.md "Every error is fatal")
* `00C8 dos_close(int handle)` INT 21/3E; error -> fatal `"File closing error"` (msg at DS:4600, code CX=FFFF).
* `00F0 dos_read(int handle, u16 count, void *buf)` INT 21/3F into DS(=SS) data; error -> `"Read error"`. **Unused by VGAME code.**
* `0122 dos_read_far(int handle, u16 count, u16 off, u16 seg)` INT 21/3F; unused.
* `0157 dos_buf_fill` (named): read 0x200 bytes of handle `[4632]` into `DS:43C8`.
* `0178 dos_buf_open(int handle)`: `[4632]=handle; dos_buf_fill()`. Unused (the PIC front ends set `[4632]` themselves; the decoder calls `0157` when its buffer runs out).
* `0199 dos_write_far(int handle, u16 count, u16 off, u16 seg, u16 off2)` INT 21/40 at `seg:(off+off2)`; error `"Write error"`. Unused.
* `01D1`/`01EC` fatal tail: `01D1`: set text mode 3 (INT 10/0003), NUL-terminate the string at BX with `$`, print it (INT 21/09), then falls to `01EC`; `01EC`: print DX (the `$` string already set by the caller), if `CX != -1` print the digit `CX+'0'`, then `INT 21/4C00` (exit code 0). Port: `fatal(msg)`.

### 11B9 / 11CE picture decoder (V; format in video.md section 9 and FORMATS.md)
The code at `11CE:0009` is the same bytes as `11B9:0159` (overlapping segment bases). Only `11B9:000E` is called (from `d7f4 pic_show`); the other two front ends are dead code.
* `11B9:000E pic_to_page(int handle, int page)`: `[4632]=handle; [4636]=page; pic_begin; page_es(si=page)` (slot 56) `clear_es` (slot 59); for `row = 0..199`: `di = row_offset(row)` (slot 58, row*320); `pic_row()` -> `scanline_put` (slot 51) from `SS:BP = DS:9516` (320 bytes) to `ES:DI`; slot 53 nop. Loop exits when `[4634]` (pixels left, starts FA00h) reaches 0.
* `11B9:0083 pic_to_seg_a(int handle, u16 seg)` (ES = seg; slots 51/55) and `11B9:00EE pic_to_seg_b(int handle, u16 seg)` (slots 52/54): same, never called.
* `11CE:000A pic_row()`: `si = [4630] + 43C8` (read cursor into the 512-byte buffer `DS:43C8`); `if (di>>1 == 0) pic_begin()` (first row); `[965A] = 320; rle90_fill(di = 9516)`; `[4630] = si - 43C8`.
* `11CE:0034 pic_begin()`: buffer end `[9656]=43C8+200h`; LZW stack pointer area `[9658]=986B`; run state `[965C]=[965D]=0`; refill if `si >= end` (`dos_buf_fill`); first word -> `[9664]` bit accumulator, `[9666]=8` bits left; `byte0 = low byte`: if bit 7 is clear -> `[9667]=1` (4-bit pixels, nibble pairs) else `[9667]=0`, `byte0 = -byte0` (8-bit pixels); `[965F] = max code bits = byte0`; `[965E] = 9` (current code size), `[9660]=1FFh` (mask), `[9662]=100h` (next code). Table `DS:C688` (3-byte entries x 2048: `+0 u16 prefix (FFFFh = none)`, `+2 u8 char`) prefix cleared to FFFFh, first 256 chars set to their index.
* `11CE:00B2 rle90_fill()`: expands `[965A]` pixels into `ES:DI`; byte `90h` = RLE escape (next byte count; 0 -> literal 90h, else repeat `[965D]` count-1 more times); in 4-bit mode each byte gives two pixels (`lo` then `hi`, stored with `stosw`), `[965A] >>= 1`. Uses `lzw_next` through a private stack (`sp` swapped with `[9658]`).
* `11CE:012B lzw_next()`: standard variable-width LZW (9..`[965F]` bits, code 100h = table reset in the lzw header sense per video.md); reads 16-bit words from `DS:43C8` and refills from the file with `dos_buf_fill` when `si` reaches `[9656]`; pops the string of the code from the private stack, adds new entries `(prefix=[9668], char=[966A])` at `[9662]`, widens `[965E]` when `[9662] > [9660]`.
Port: `pic_decode(FILE*, uint8_t out[64000])` as in video.md; `tools/picdec.py` is the reference. All picture loads in VGAME go through `d7f4 pic_show(name, page)` = `dos_open(name,0)` (d81d) + `pic_to_page(handle,page)` + `dos_close` (d82f). Callers of `d7f4`: 00F0 (`256pit.PIC`->2), 02FE (`ordnance.pic`->1), 1E9E (`adv.pic`, quit requester), 2F82 (`256left/right/rear.pic` view changes).

### 1359:xxxx graphics-library glue (MicroProse "MGRAPHIC-lib" video-mode table) (V)
`1359:0000 gfxlib_mode_init(char mode)` (main passes `'M'` = 4Dh): `al = mode` (lower-case -> upper-case by `-20h`), `[85C4] = al`; dispatch: `'E'` -> `00E7` (EGA), `'M'` -> `010A`, `'C'` -> `0126` (CGA), `'T'` -> `0149` (Tandy); others return.
The mode routine fills the row-offset table `DS:85EC` (words) and sets `[85C2]` = base (in 1359's code segment) of a table of near function pointers `cs:[base+4/6/8]`:
`010A` (MCGA, **the only one used**): 200 words `row*0140h` at `85EC`; `[85C2] = 0070h`; `cs:0070 = {010A, 027B, 01D4, 045D, 0AF7, 0AF7}`. `00E7` EGA: 200 words `row*28h` (`[85C2]=64h`) or, if `[913E]!=0`, `00B6`: 480 words `row*50h` + `[9142]=4Fh, [9144]=-0A1h`; `0126` CGA: 100 pairs `(row*50h, +2000h)`, `[85C2]=7Ch`; `0149` Tandy: 50 quads, `[85C2]=88h`.
`1359:0040`, `0049`, `0052` are 3 dispatch stubs: `call cs:[ [85C2] + 4 ]`, `+6`, `+8` respectively. For MCGA they reach `01D4`, `045D`, `0AF7` (`0AF7` = the polygon fill of FORMATS.md; called from the 3D code 11EB/12EF) - owner render3d.
`1359:0094` is a dead DOS allocator (INT 21/48 with a reserve of 400h paragraphs; its test is inverted) - never called.

### 14A2:00C5 `pal_read_dac(void far *dst)` (V)
IF=0; wait until bit 3 (vertical retrace) of port 3DAh is set; `out 3C7h,0`; read 300h bytes from 3C9h into `dst` (6-bit values, entry 0..255). Used with `1088:00D7`.
`14A2:000E pal_write_vsync(void far *rgb, int first, int count)` is documented in video.md section 9 (grey-scale path when BIOS byte `0040:0089` (linear 0489h) bit 1 is set: `gray = (R*4CCDh + G*970Ah + B*1C29h)*2 >> 16` replicated on all three DAC bytes).

## 6. Palettes (1088:xxxx; FORMATS.md "Flight palette" has the data)

### 1088:0006 `pal_flight_load(void)` (V)
```
if (DS:0D47 != 2) {                  // climate byte != 2 (desert/arid), see DS:00EE
    memcpy(179A, 1F4A, 48); memcpy(182A, 1FDA, 48);      // palette A entries 70h..7Fh and A0h..AFh
    memcpy(1A9A, 1F7A, 48); memcpy(1B2A, 200A, 48);      // palette B
    memcpy(1D9A, 1FAA, 48); memcpy(1E2A, 203A, 48);      // palette C
}
src = ([43AE] == 0) ? ([3658] ? B(194A) : C(1C4A)) : ([3658] ? C(1C4A) : A(164A));   // [43AE]=1 is the debug palette toggle
memcpy(104A, src, 0x300);
pal_write_vsync(first=0, count=256, far 1E23:104A);
if ([365E] != 0) pal_mfd_ramp_green();   // 1088:00A3
```
(`[3658]` = night, `[365E]` = `([C07A] & C0h) != 0`, set by 2F82.) Called from 00F0, 01B7 and elsewhere (cba0 toggles).

### 1088:00A3 `pal_mfd_ramp_green()` / 00BD `pal_mfd_ramp_rt()`
`pal_write_vsync(first=98h, count=8, rgb = DS:206A)` / `rgb = DS:1212` (the latter is filled at run time by the MFD code).
### 1088:00D7 `pal_save()`: `pal_read_dac(DS:0D4A)` (768 bytes). `1088:00EE pal_restore()`: `pal_write_vsync(0,256, DS:0D4A)`. `1088:0107 pal_set_adv()`: `pal_write_vsync(0,256, DS:134A)` (= ADV.PAL, the quit requester screen). Used by 1E9E (206A save, 2089 set, 20E7 restore).
### 1088:0120 `r3d_depth_shade(void) -> u8` (owner render3d; V)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 1; where they differ, render3d.md is right.
`if ([7D5E] != 0 || [43B6] == 0) return 0; v = max(0, (s8)[4963]) >> 1; v -= (u8)[497A]; v = clamp(v, 0, 31); return v >> 1;` The result lands in `[4894]` (shade offset subtracted from ramp colours >= A0h, FORMATS.md). Callers DE6A, E830. The code after it (1088:0156..) are fixed-point multiply/divide helpers (other readers).

## 7. Keypad-as-stick keyboard hook (1181:xxxx) (V)

Installed only when no joystick is configured (`comm[72h]&1 == 0`); it turns the numeric keypad (and the separate arrow keys) into a virtual stick and filters the BIOS key buffer.
### 1181:0008 `keypad_stick_install()`
> **S3.8:** owned by `flight.md` section 2.4 (verified there; where they differ, flight.md is right).
`*(u8*)0040:0017 &= ~20h` (NumLock **off**); `[2CD8]=0; [2CD9]=0; [2CDB]=[2CDC]=[2CDD]=0; [2CD6]=[2CD7]=80h`; save INT 9 (IVT 0:0024) in `cs:0187/0189` (operand of the `ljmp` at 1181:0186); install INT 9 := `1181:0072`.
### 1181:0058 `keypad_stick_remove()`: restore INT 9 from `cs:0187/0189`.
### 1181:0072 `keypad_stick_isr` (INT 9)
```
sti; save regs; DS=1E23; ES=0040
if ([2CDD]) { [2CDD]--; goto dedupe; }               // skip bytes of E1-prefixed sequences
al = in(60h); prev = [2CDB]; [2CDB] = al;
if (prev != E0h) {                                    // normal (non-extended) byte
    if (al == E0h) { goto dedupe }                    // prefix itself
    if (al == E1h) { [2CDD] = 2; goto dedupe }        // Pause: skip next two bytes
    f = 0; if (es:[17h] & 20h /*NumLock on*/) f ^= 1; if (es:[17h] & 3 /*Shift*/) f ^= 1;
    if (f) goto dedupe;                               // keypad is in numeric mode: not a stick
}                                                     // after E0 (arrow block) the NumLock/Shift test is skipped
code = al & 7Fh;  if (code > 51h || code < 29h) goto dedupe;
m = xlat[code - 29h];            // DS:2CDE, 41 bytes
if (m == 0) goto dedupe;
if (!(al & 80h)) {               // make
    if ([2CD8]) goto dedupe;     // another stick key still held
    [2CD8] = m;  same = ([2CDC] == m); [2CDC] = m;
    mag = (same && (es:[006Ch] - [2CD9]) < 5) ? 7Fh : 5Ah;   // second press within 4 BIOS ticks (~0.22 s) = full deflection
    lo = 80h - mag; hi = 80h + mag;
    if (m & 1) [2CD7] = lo; if (m & 2) [2CD7] = hi;     // Y: 1 = up/pull?, 2 = down  (axis value 80h centre)
    if (m & 4) [2CD6] = lo; if (m & 8) [2CD6] = hi;     // X: 4 = left, 8 = right
    [2CD9] = es:[006Ch];
} else if ([2CD8] == m) { [2CD8] = 0; [2CD6] = [2CD7] = 80h; }   // break of the held key = centre
dedupe: bx = es:[1Ah] (BIOS buffer head); while (head != tail) { ax = es:[bx]; bx = next(bx) (wrap 80h..82h); if (bx == tail) break; if (ax != es:[bx]) break; es:[1Ah] = bx; }  // drop repeated identical keys
pop regs; popf; cli; jmp far old INT 9
```
xlat table `DS:2CDE` (scan code -> mask), non-zero entries: `29h:01` (the backquote key = up), `2Bh:04` (backslash = left) (odd, probably debug leftovers, U), keypad `47h:05` (up-left), `48h:01` (up), `49h:09` (up-right), `4Ah:02` (grey minus = down), `4Bh:04` left, `4Ch:80` (keypad 5, sets only the held-key state), `4Dh:08` right, `4Eh:08` (grey plus = right), `4Fh:06` (down-left), `50h:02` down, `51h:0A` (down-right).
Outputs: `DS:2CD6` = stick X byte, `DS:2CD7` = stick Y byte (80h = centre, 80h-5Ah / 80h-7Fh left/up). Both are read only by `1E9E` (which merges them with the joystick bytes `DS:2CD2/2CD3`, joy_scale output).
Port: keyboard state -> the same two bytes; the tap/double-tap magnitude (5Ah then 7Fh) is original behaviour.

## 8. RTL / library leftovers (rows only; platform.md names the neighbours)
| addr | name | notes |
|---|---|---|
| 0000:F0B0 | `ltoa(long v, char *buf, int radix)` | entry with `bl=1` (signed) into the shared number conversion at FA38; `f094` is the unsigned head; radix 10 prints '-' for negatives. Callers: `7898`, `8B5C` (HUD numbers) and `44C4`. |
| 0000:F186 | `filelength(int fd)` | `-1` if fd invalid (`fd >= [923F]`, errno 9); otherwise `lseek(fd,0,SEEK_CUR)`, `lseek(fd,0,SEEK_END)`, restore; returns the size (dx:ax). |
| 0000:F206 | `int86x(int intno, union REGS *in, union REGS *out, struct SREGS *s)` | builds `CD nn CB` on the stack; loads ES/DS from `s`; returns `out.ax`, sets `out.cflag`; on CF calls `rtl_dosmaperr_al(AL)`. Used by `ea7c` (INT 21h). |
| 0000:F2B8 | `strupr(char *s)` | upper-cases in place (a-z only), returns `s`. |
| 0000:F4AE | `rtl_aFuldiv(u32 a, u32 b)` | unsigned 32/32 -> 32 divide (compiler helper), `(a, b)` at `[bp+4..6]`, `[bp+8..A]`. |
| 0000:F5E4 | `rtl_dosmaperr_al(al)` | `ah=0` then falls into `rtl_dosmaperr` (F5EA). |
| 0000:FDE2 | `free` | `jmp rtl_nfree` (FE14). |
| 0000:FE10 | `malloc` | `jmp rtl_nmalloc` (FE35). |
| 0000:FFB7 | `rtl_nh_last_free` | near-heap helper in `rtl_nh_grow`: walks the heap list from the heap header (`[bx+8]`..`[bx+6]`), stepping by `word & FFFEh`, until the sentinel `FFFEh`; returns SI = last free block. |
| 0000:F0E0 | `getch` | named `rtl_dosret_ah` in platform_symbols; is really `getch()` (INT 21/08 with the `_ungetch` buffer `[926C]`). Used by `load_error_msg`. |

## 9. Structures and conventions seen
(see the table at the end)

## Open items
| # | item |
|---|---|
| 1 | Values of `comm+28h` (1, 2, 5, 6) and `comm+26h` as consumed by END: needs the END reader (S3.9). |
| 2 | `DS:09E0[i] = 6` for bank-0 objects and `DS:0CA8`: unknown consumers (render3d / terrain readers). |
| 3 | `4B57`: assumption that the START generator reserves spare name slots (names 0x59..) for the copied target objects; check against a dump of `comm+7Ah` after START. |
| 4 | `ea7c` 128 KB bug (see 2): confirm no shipped file exceeds 2 x 65 535 bytes (largest .3DX?). |
| 5 | Keypad table entries `29h`/`2Bh` (backquote = up, backslash = left): intentional? (U) |
| 6 | Sound clock: VGAME polls `snd_poll` once per frame, 6 of 7 frames (60.08 Hz at 70 Hz), and `snd_noise` `class` x per frame in fast mode. sound.md section 3 assumed poll at the ISR rate (4x faster during effect units) and no skip. START's ISR also polls only on ticks (no skip). Needs S3.3 re-check; the port should follow the code here for VGAME. At a 60 Hz refresh the sequencer would run at 51.4 Hz in the original. |
| 7 | Exit code 81h: confirm with `F117.COM` that any non-zero value is accepted (RE_GUIDE: "VGAME: 0 = quit else END"). |

## DGROUP globals seen
| offset | size | name | meaning | readers/writers |
|---|---|---|---|---|
| 0008 | str | rtl_banner | "MS Run-Time Library ..." | RTL |
| 0042 | char[9] | stem_buf | theatre stem buffer, becomes `xx.3dG/.3DX/.3dT` (`regn.xxx` in the image) | 00F0, 097A.. |
| 004B..0083 | str | stem strings | `lb pg nc ce vn me ku cu ko` + `.xxx` (7 bytes each) | 00F0 |
| 008A | w | speech_seg | segment of SPEECH.117 (0 = none) | 00F0, 0233 |
| 008C | b | vgame_run_flag | set to 1, never read | 00F0 |
| 008D | b | exit_code | initial 81h; 0 = quit | 0010, 1E9E |
| 008E | w | stem_ptr | = 0042 (initial) | 00F0, 097A, 0C2C |
| 0090 | w[9] | stem_tbl | pointers to the stem strings, indexed by theatre | 00F0 |
| 00A2 | 4*n | ordnance_xy | `{x,y}` of weapon name strips in `ordnance.pic` | 02FE |
| 00EE | b[9] | climate_tbl | `06 06 02 02 02 06 06 02 02` | 00F0 |
| 0D47 | b | climate | copy of climate_tbl[theatre] | 00F0, 1088:0006 |
| 0D4A | 768 | pal_saved | saved DAC | 1088:00D7/00EE |
| 0510 | w[..] | bank1_objid | object id table: `bank<<8 | index` per bank-1 slot (extra entries `0x200|photo`) | 0994 |
| 0610 | w | bank1_count | number of bank-1 objects in the file (extras not counted) | 0994, 4B57 |
| 0612/0614/061E | w / w[5] / w[5][32] | 3dt_magic / type_count / rec_count | .3dT header | 0AF9 |
| 075E | w | 3dg_magic | `'22'` | 0C2C |
| 0760 / 07E0 / 0860 / 0960 / 09E0 | b[] | bank0_flags / bank1_flags0 / photo_flags / bank1_flags / bank0_attr | object flag bytes | BD24, 0994 |
| 0A68 | 64*9 | l4_tile_tbl | level-4 8x8 type tables per theatre | 0C2C |
| 011A | 5*106 | drv_slots | far-jmp stubs bound by `drv_bind` | 0FFD:0008 |
| 104A | 768 | pal_active | active flight palette | 1088:0006 |
| 164A / 194A / 1C4A | 768 | pal_A / pal_B / pal_C | palettes | 1088:0006 |
| 1F4A..203A | 48*6 | pal_ramps | desert ramps | 1088:0006 |
| 206A / 1212 | 24 | mfd_ramp | MFD green ramp / runtime ramp | 1088:00A3/00BD |
| 134A | 768 | pal_adv | ADV.PAL | 1088:0107 |
| 2624 | b | break_flag | INT 1B flag | 1BA0 |
| 2625 | d | old_int1b | | 1B60, 1B83 |
| 2630..2648 | | timer state | see section 1 | |
| 264A | b | tick_byte | | ISR, 04BB |
| 2CA2 | w[20] | joy_calib | joystick calibration (copy of comm+48h) | 10A8:0D70/0D82, joy_scale |
| 2CD2 / 2CD3 | b / b | joy_x / joy_y | joystick axis (80h centre) | 0010, joy_scale, 1E9E |
| 2CD6 / 2CD7 | b / b | pad_x / pad_y | keypad virtual stick | 1181:0072, 1E9E |
| 2CD8..2CDD | | keypad state | held mask, last tick, last scan, last mask, skip | 1181:0072 |
| 2CDE | b[41] | pad_xlat | scan 29h..51h -> mask | 1181:0072 |
| 2E34 | w | flip_rows | rows copied by `flip` (91 = 3D window) | 2F82, 1BBC |
| 3636 | w[4] | wpn_qty_disp | weapon quantities shown (44C4) | 44C4 |
| 363E | 4*4 | wpn_slots | `{id, qty}` from comm[38h..46h] | 39C4, 7570.. |
| 3658 | w | night | comm[33h] | 00F0, ... |
| 365E | w | mfd_flag | `([C07A] & C0h) != 0` | 2F82 |
| 3678 | 26*n | wpn_table | weapon records: `+2` ordnance sprite index | 02FE, 753F |
| 3D6E | w | special_unit | CABC | 4C41 |
| 3FCC / 3FE4 / 3FFC | 24 | textctx p0/p1/p2 | TextCtx for pages 0, 1, 2 | |
| 3FE2 / 3FFA / 4012 / 4042 | w | textctx ptrs | page-0, page-1, page-2, ordnance ctx pointers | |
| 4088 | b | back_page | which of ctx 3FE2/3FFA is the back buffer | |
| 43AE | w | pal_debug | debug palette toggle | 1088:0006 |
| 43B0 | w | day_flag | 1 = day (`comm[33h]==0`) | 00F0, 01BF |
| 43C8 | b[512] | dos_buf | PIC reader buffer | 1199:0157, 11CE |
| 4630..463C | | pic state | cursor, handle, pixels left, arg, row offset, row | 11B9/11CE |
| 5C2 / 51A / 9B9 / 965 | | KU remaps | see 0994 step 4 | 0994 |
| 85C2 / 85C4 | w | gfxlib dispatch / mode | | 1359 |
| 85E2 / 85E4 / 85E6 / 85EA | w | page0 seg / page1 seg / page2 seg / current seg | | 02C2, 1359 |
| 85EC | w[200] | row_offsets | `row*320` (MCGA) | 1359:010A |
| 94EA | w | log_count | event log records | 499E |
| 94EC/94EE | d | blob_cursor | | 4CEF, 4D2F |
| 9516 | b[320] | pic_row_buf | | 11CE |
| 9656..9668 | | LZW state | | 11CE |
| 98E6/98E8 | d | bios_motor_ptr | 0000:0440 | 0233 |
| 98F4/98F6 | d | pilot_ptr | comm+1272h | 0010 |
| 9F68 / 9D10 / 9B08 / 9904 | 256/512/512/512 | terr L3 map / L2 / L1 / L0 child | | 0C2C |
| A06A | 7*n | 3dt_records | | 0AF9 |
| B00C | b[64] | l4_tiles | | 0C2C |
| B75E / B760 / B762 | w | bank0/1/2 seg | | BD24, 0994 |
| BA0E | w | ordnance_sprite | | 02FE, 0233 |
| BA10/BA12, C02A/C02C | d | start x, start z | | 4AD0, 39C4 |
| BA2A | 0x600 | event_log | | 499E, 4C29 |
| C074/C076 | w | file_size | | EA7C |
| C0AE | w | start_phase | 0/1/2 | 1E9E, 39C4 |
| C5CA..C5D0 | | regs | int86x regs | EA7C |
| C688 | 3*2048 | lzw_table | | 11CE |
| DE8E | w | blob_dir | 1 import / 0 export | 4AD0, 4C29 |
| DEDC | w[128] | name_ptrs | | 4AD0, 4B57 |
| E304 | w[160] | 3dt_ptrs | record pointers per (level,type) | 0AF9 |
| E548/E54A | d | comm_ptr | | 0010 |
| E552 | b | quit_flag | loop exit | 1BBC, 4926 |
| E558 | w | offscreen_seg | = comm[20h] | 0010 |
| (blob) | | see section 4 | | |

## Structures seen
* **Driver slot stub** (DS:011A + 5*slot): `{u8 0xEA; u16 off; u16 seg}`.
* **.3DX bank file**: `{u16 n; u16 offs[n+1]; u8 flags[n]; ...}`; flag byte tables as above. See FORMATS.md.
* **.3dT record in memory** (A06A.., 7 bytes): `{s16 x, s16 y, s16 z, u8 obj}`; pointer table `E304[32*level + type]`; counts `061E[32*level + type]`.
* **Site** (16 B at B2A2, count C0B4) / **unit** (36 B at C13E, count DED0): see shell.md; VGAME field `site+0Eh` (object id word) is overwritten by 4B57.
* **Event log record** (6 B at BA2A): `{u16 tick; u8 x>>7; u8 y>>7; u8 type; u8 arg}`, terminator type 0.
* **Mission slot** (18 B at E2D8 and E2EA): `+0 kind, +2 target site, +4 anchor/departure site, +6 flags (+7 = PHOTO prop index), +8 template, +A grid ref`.
* **comm fields touched by VGAME**: `1Ah 1Ch 1Eh` driver segs (r), `20h` page seg (r), `26h` mission state (w), `28h` end code (w), `2Ch` kill/target counter (w, inc), `30h` bit0 (w), `32h` (r -> [98DE]), `33h` night (r), `34h/36h` damage (w), `38h..3Eh` loadout ids (r), `40h..46h` loadout qty (r), `48h` joystick calibration (r/w 40 B), `72h` bit0 joystick (r), `74h/76h` final position (w), `7Ah` world blob (r then w).
* **TextCtx** per video.md section 3 (24 bytes): `3FCC` page 0 (clip rows 0..90), `3FE4` page 1, `3FFC` page 2 (rows 0..199), `402C` ordnance ctx (clip 0xA9..0xF5, rows from 6E).



---
# Part 2: input and player flight model, fixed-point and helper library (R2)

*Reader pass R2, original title: S3.6 reader R2 - input, player flight integration, fixed-point library, lamp/digit helpers*

Scope: 47 functions (list in `scope_R2.txt`). Image offsets: segment 0000 = image offset, far segments `SSSS:OOOO` = `SSSS*16+OOOO`,
DGROUP = `1E23`, **DS:oooo = unpacked image offset 0x1E230+oooo counted from the end of the EXE header (the `img` of dsann.py; in the raw
`work/unpacked/VGAME.EXE` file add 0x820)**. All arithmetic below is 16-bit two's complement unless a type says otherwise; `i16`/`u16` mean
"truncate to 16 bits, signed/unsigned". `tdiv(a,b)` = C `/` (truncate toward zero), which is what `idiv` and the RTL `__aFldiv` do.

**What was verified how.** `1e9e` is NOT only a keyboard handler: it is the per-frame *player aircraft model* (stick -> attitude ->
speed/altitude/position, fuel, G-load, crash/touch-down checks). Everything in sections 2-4 was transcribed into Python
(`port/notes/s36/r2_ref.py` helpers, `port/notes/s36/r2_fm.py` = whole `1e9e` except the Alt-Q / Alt-P / Alt-B screens) and **differential-tested against the real
machine code under Unicorn** (`port/notes/s36/r2_emu.py`, `r2_test_fm.py`): thousands of randomised frames (keyboard stick, joystick bytes, ground/air,
crash states, each of the throttle/brake/`c`/Esc/Alt-S keys, first-frame init), compared byte for byte over the whole 60 KB data segment plus the
ordered list of sound commands, lamp recolours and far calls: **0 mismatches** (V). The Python files are an executable reference the porter can
read or run (`python port/notes/s36/r2_test_fm.py 600`). Every helper in section 2 (clamp, distance, heading, sin/cos, asin, isqrt, matrix multiply,
matrix->Euler, Euler->matrix) was tested the same way.

## 0. Overview: who calls what, and the frame order

```
frame loop 1bbc:  2f82 (view+3D)  7898 (HUD)  [10a8:0000 asm HUD, d9a3(page), 17a3 if view==0]  flip  1e9e  39c4  (until [E552])
1e9e = sim_input_flight():            once per rendered frame, AFTER the flip and BEFORE 39c4 (the rest of the sim step)
   P0  first-call init (c0ae)              P6  turbulence / crash-sequence / weather throttle cap / engine spool / fuel
   P1  read ONE key (rest of buffer discarded)   P7  G load, G string, speed + lift model
   P2  key dispatch (own keys; every other key is consumed later by cba0 via [DE90])   P8  incremental rotations -> matrix -> Euler angles
   P3  joystick poll OR keyboard stick scaling    P9  stall / ground constraints
   P4  stick -> roll/pitch commands (dead zone, curve)   P10 position, altitude, touch-down / crash test
   P5  autopilot override                          P11 16-entry history ring + cockpit sway
```
Helper layers (all pure functions of DGROUP unless noted): fixed-point maths `109d:*`, `c94e..cb72`; orientation `2c6e 2c9e 2e43 2e64 2ebe 2f2f 2f40 e578`
(+ `e26b` = Euler->matrix, outside R2); lamps/digits `d83e 898b 38ab 38f8 3926 d92d`; sound gate `d6bf d6e5`; frame-rate parameters `d707 d76b d7bb d7d0`;
picture loader `d7f4 d81d d82f`; screen helpers `8929 893f 8bd3`; joystick glue `10a8:0c87 0c95 0ca8` (+ already named `0cb7` raw read, `0cff` scale).

Sim-rate convention used everywhere: **`[3660]` = `sim_rate`**, an integer 4..15 (4 at start, `3A63`; clamp in `d707`): every per-frame increment of the flight
model is divided by it (`x / sim_rate`), i.e. the model is written per 1/`sim_rate` of a nominal unit and the game picks `sim_rate` from the
measured frame time (`39c4` / `cba0` FrameRate menu; owner R4/R10). A port that renders at a fixed rate keeps `sim_rate` constant (4 = original start value).

---

## 1. Fixed-point conventions and the sine table

* Angles are **u16 turns**: `0x10000 = 360 deg`, `0x4000 = 90 deg`. Values are Q15: `0x7FFF` ~ 1.0, `-0x8000` = -1.0 (only as an asin argument).
* **Sine table** `DS:2084`, 257 words (+1 spare zero): `T[i] = clamp(round(32768*sin(2*pi*i/256)), -32767, 32767)`, i = 0..256 (T[64] = 32767, T[128] = 0, T[192] = -32767 (!, not -32768), T[256] = 0).
  Generated and compared against the image: identical (V). The port should generate it (no EXE dependency).
* `mul15(a,b) = (int16)((a*b + 0x4000) >> 15)` with a 32-bit signed product; arithmetic shift; result truncated to 16 bits (`109d:0006`, see below). All Q15 products in the game use it.

```c
int16_t  sin16(uint16_t a){ int idx=a>>8, fr=a&255; int d=(int16_t)(T[idx+1]-T[idx]); int p=d*fr;
                            return (int16_t)(T[idx] + (p>>8) + ((p>>7)&1)); }      /* 109d:0090 core, rounds half up */
int16_t  cos16(uint16_t a){ return sin16(a+0x4000); }                              /* 109d:006c */
```

### 109d:0006 `fx_mul15(int a, int b) -> int`  (far, retf; stack args `[sp+4]`,`[sp+6]`)
`imul`, then `shl ax,1; rcl dx,1; shl ax,1; adc dx,0` = `floor(P/2^15) + bit14(P)` = `(P+0x4000)>>15`. Callers: everywhere (e578, e26b, caee, 1e9e). (V)
### 109d:006c `fx_cos16(int a)`, 109d:007c `fx_sin16(int a)`, 109d:0088 `fx_sin16_reg` (BX = angle -> BX; the register entry of `0090`), 109d:0090 `fx_sin_interp` (BX = angle, returns BX)
`006c` adds `0x4000` to the argument then falls into the same code as `007c`. `0090` is the interpolator in the C block above: `ax = T[idx+1]-T[idx]` (16-bit
sub), `imul` by the fraction byte (DX = fr), the 24-bit product is shifted right 8 via `mov dh,dl; mov dl,ah`, bit 7 of the low byte (`shl al,1`) is added with `adc` (rounding). (V)

### 0000:c94e `fx_clamp_wrap(int v, int lo, int hi) -> int` (cdecl; first arg at [bp+4])
```c
int fx_clamp_wrap(int v,int lo,int hi){ if (hi < v) return hi; if (lo <= v) return v; return (v <= -0x4000) ? hi : lo; }   // signed compares
```
The last line is an original quirk (a value that wrapped far below `lo` is taken to be a wrapped-high value and returns `hi`). The most used clamp (19 callers). Typical: `(throttle-10, 0, 100)`. (V)
### 0000:c96d `fx_clamp(int v,int lo,int hi)` - plain signed clamp (`hi<v ? hi : lo>v ? lo : v`); only caller 1e9e (autopilot). (V)
### 0000:c987 `fx_dist(int a,int b)` - cheap length `|a|,|b|`: `max + (min>>1)`, capped at 0x7FFF. Callers 12 (2f82 39c4 5460 5730 671a 711f 7898 9208 a17e bbb4 bf71 1e9e). (V)
### 0000:c9d6 `fx_heading(int x,int y) -> u16`  heading angle of the vector (x = east, y = north): 0 = +y, 0x4000 = +x, 0x8000 = -y, 0xC000 = -x
```c
if (x==0) return y>0 ? 0 : 0x8000;   if (y==0) return x>0 ? 0x4000 : 0xC000;
ax=|x|, ay=|y|;  if (ay<ax){ t=(ay<<14)/ax; f=1;} else { t=(ax<<14)/ay; f=0; }      // t = 0..0x4000, long arithmetic
e=(|0x1333 - t| * 0xB00) >> 14;   r=(t*(0x2800 - e)) >> 14;                             // ~ atan(t)*(0x8000/pi) polynomial, r(0x4000)=0x204D
quadrant: x>0,y>0: f? 0x4000-r : r        x>0,y<=0: f? r+0x4000 : 0x8000-r
          x<=0,y>0: f? r-0x4000 : -r      x<=0,y<=0: f? 0xC000-r : r+0x8000
```
(V, 1500 random vectors). Callers: 2f82 43ee 4e0f 5460 5730 671a 711f 7898 99a7 a17e bbb4 bf71.
### 0000:caee `fx_sin_mul(u16 a, int m) -> int` = `mul15(m, sin16(a))`;  0000:cb07 `fx_cos_mul(a,m)` = `fx_sin_mul(a+0x4000, m)` (V)
### 0000:cb1b `fx_sext8(int x) -> int` = `(int)(int8_t)(x & 0xFF)`; only caller 43ee (reads `[2DF3]` = high byte of roll). (V by reading)
### 0000:cb56 `rand_seed_from_timer()`: `seed = bios_timer(); [9514] = seed; srand(seed)` (RTL LCG, see platform.md). Only caller 4857. (V by reading)
### 0000:cb62 `rand_n(int n) -> int` = `(rand() * n) >> 15` (32-bit product, arithmetic shift): uniform 0..n-1 for n>0. Callers 1e9e 39c4 4857 5730 671a 74f6 c73c cba0. LCG state at `DS:926E/9270` (V)
### 0000:cb72 `input_button(int n) -> int`  button n (0 or 1): `(comm[0x72]&1 ? joy_button(n) /*slot 93, 1 = pressed*/ : 0) + [43BE + 2n]`
> **S3.8:** owned by `flight.md` section 2.4 (verified there; where they differ, flight.md is right).
`[43BE]` / `[43C0]` are one-frame key pulses: `cba0` zeroes both at entry and sets `[43BE] = 1` for Backspace (`0E08`, only when pilot flight model
`+42h` = 0) and `[43C0] = 1` for Enter (`1C0D`). Callers 465b a17e cba0 (fire / release). (V by reading)
(Neighbour, R8: `cb39` = sign(x).)

### 0000:2f40 `fx_isqrt(int n) -> int`  Babylonian integer square root
```c
n=abs(n); if (n<4) return 1; g=n>>2; for(;;){ q=n/g; g=(q+g)>>1; if (abs(g-q) <= 1) return g; }   // signed idiv; may be off by one
```
Callers 1e9e (`isqrt(g*4)` below), a17e. (V)
### 0000:2ebe `fx_asin(int v) -> u16`  inverse of the sine table; v Q15
```c
if (v==-0x8000) return 0xC000;  a=abs(v); i=(a>>9)+1; while(i>=0 && T[i]>a) i--;           // T at DS:2084
ang = (i<<8) + (((long)(a-T[i])<<8) / (int16)(T[i+1]-T[i]));  return v<0 ? -ang : ang;      // result in -0x4000..0x4000
```
(V). `0000:2f2f fx_acos(int v)` = `0x4000 - fx_asin(v)` (V).
### 0000:2e64 `fx_ratio(int x,int y) -> int` = `sign(x)*sign(y) * (((long)|x|<<16)/|y|) >> 1`  (x/y in Q15; caller guarantees |x|<=|y|; |x|==|y| gives 0x8000) (V). Only caller 2c9e.

### 0000:e578 `mat_mul(const int *A, const int *B, int *C)`  3x3 Q15, row-major (word offset = 2*(3*row+col))
> **S3.8:** owned by `flight.md` section 2.2 (verified there; where they differ, flight.md is right).
`C[3i+j] = i16( mul15(A[3i],B[j]) + mul15(A[3i+1],B[3+j]) + mul15(A[3i+2],B[6+j]) )`. `C` must not alias A or B (callers use scratch `DS:2DDC`). Only caller 2c6e. (V)
### 0000:e26b (not R2, but needed) `mat_from_euler(int *M, u16 yaw, int pitch, u16 roll)`
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 1; where they differ, render3d.md is right.
`sa,ca = sin16/cos16(yaw)` ... : `M0=ca*cc+sa*sb*sc; M1=sa*sb*cc-ca*sc; M2=sa*cb; M3=cb*sc; M4=cb*cc; M5=-sb; M6=ca*sb*sc-sa*cc; M7=ca*sb*cc+sc*sa; M8=cb*ca` (each product = mul15, nested products
rounded at every step; sums 16-bit). It also leaves `sin/cos` of yaw, pitch, roll in `DS:4982 4984 4986 4988 498A 498C`. (V by differential test)

---

## 2. 0000:1e9e `sim_input_flight(void)` - input + player aircraft model, once per frame (3536 bytes, caller 1bbc only)
> **S3.8:** owned by `flight.md` section 2.1 (verified there; where they differ, flight.md is right). Read `sim_rate` as emulated (not fixed), `[E2FE]` = master mode, `[9B04]` 0x10/0x20 = ECM / IR jammer (flight.md section 8).

Names used below (DGROUP offsets in the globals table at the end): `rate = [3660]`, `fm_yaw [2DEE]`, `fm_pitch [2DF0]`, `fm_roll [2DF2]`, `fm_mat [2D94]` (9 words),
`thr_cmd [2DFE]` (0..100), `thr [B170]` (engine, 0..100), `spd [B052]` (u16 raw speed, 27 raw = 1 knot), `kts [B168] = spd/27`, `alt [2DF6]` (u16), `alt_s [2DF4]`
(scaled alt, section 2.7), `gnd [C0A2]` (ground / deck height under the aircraft, same unit as `alt`; 0 = sea-level airfield, 0x80 = carrier deck), `fuel [3634]`,
`roll_in [DE8A]`, `pitch_in [E446]` (the stick after the curve, signed), `g16 [98DC]` (load factor x16), `crash [C06E]` (crash sequence counter, 0 = flying),
`cm = pilot[+40h]` (crash mode 0 none / 1 easy / 2 realistic, `[98F4:98F6]` = pilot record = comm+1272h), `comm = [E548:E54A]`.
"Airborne" always means `gnd < alt_s` (unsigned), "on the ground" means `alt_s == gnd`.

### 2.1 Key table of 1e9e (INT 16h AH=0 codes `scan:ascii` = `AX`) - complete for this function (V)
The frame reads **at most one key**: `if (kbhit) { [DE90] = bios_keyboard(0); while (kbhit) bios_keyboard(0); }` - the remaining buffered keys are thrown away
(`[DE90] = 0` otherwise). `[DE90]` is also read by `43EE` -> `cba0` (every other key, appendix A). Chain order = the order tested; all other codes fall through.

| code | key | action (flow) |
|---|---|---|
| 011B | Esc | `if (crash != 0) [DECA] = 0` (shortens the ejection sequence, section 2.5); no effect while flying |
| 0B30 | `0` (main row; NOT keypad) | wheel brakes toggle: `[9B04] ^= 8; lamp(0x23, [9B04]&8)`. When they were just **released**: `if (gnd != 0 && thr_cmd == 100) { spd = 0x546; snd_cue(0x11,prio 2) }` = **catapult shot on a carrier deck** (50 knots, sound cmd 17); needs `gnd != 0` (deck). Brakes ON: nothing else |
| 0C2D | `-` | `thr_cmd = clamp_wrap(thr_cmd - 10, 0, 100)`; redraw throttle digits (`3926`) |
| 0C5F | `_` (shift -) | `thr_cmd = 0; snd_cue(0x0C, prio 0)`; redraw digits |
| 0D2B | `+` (shift =) | `thr_cmd = 100`; redraw digits |
| 0D3D | `=` | `thr_cmd = clamp_wrap(thr_cmd + (thr_cmd < 10 ? 5 : 10), 0, 100)`; redraw digits |
| 1000 | Alt-Q | quit requester (2.9) |
| 1900 | Alt-P | pause (`3939`, 2.9) |
| 1F00 | Alt-S | joystick re-centre: `if ([2E02]==0) { 10A8:0C87(); [2E02] = 0x28 }` (40-frame debounce, `[2E02]` counts down once per frame at the join) |
| 2E63 | `c` | HUD style toggle `[294B] = ([294B]==0)`; then `10A8:0008()` (asm HUD re-init, `10A8:0A22`) |
| 3000 | Alt-B | boss screen (2.9) |

After the switch (all paths, join `1F89`): `if ([2E02]) [2E02]--;  if (thr_cmd != 0 && thr == 0) snd_cue(0x0B, prio 2);` (engine start sound, cmd 11 = `set_engine(1)` + start rumble).
Throttle digit redraw `3926` does nothing while the instrument panel is hidden (`[365E] == 0`).

### 2.2 Stick input (P3/P4) (V)
```c
if (comm[0x72] & 1) { 10A8:0CA8();            /* joystick: raw read + joy_scale -> [2CD2]=X, [2CD3]=Y (bytes: 0..0x7F left/up, 0x7F/0x80 centre, 0x80..0xFF right/down) */ }
else { k = [43BA] + 1;                           /* keyboard "sensitivity" 0/1 (cba0 menu) -> k = 1 or 2 */
       [2CD2] = (uint8)(tdiv(i16(([2CD6] - 0x80) * k), 3) + 0x80);   /* [2CD6]/[2CD7] = virtual stick bytes written by the INT 9 hook 1181:0072 (R1) */
       [2CD3] = (uint8)(tdiv(i16(([2CD7] - 0x80) * k), 3) + 0x80); }
pitch_in = (([2CD3] >> 4) - 8);  roll_in = (([2CD2] >> 4) - 8);          /* -8..7 */
if (roll_in < 0) roll_in++;  if (pitch_in < 0) pitch_in++;               /* -7..7: stick bytes 0x70..0x8F (+-12 % of travel) are the dead zone (value 0) */
roll_in  = i16( -(|roll_in| + 2) * roll_in * 2 );                        /* roll-rate curve -2*r*(|r|+2): r=1 -> -6, 2 -> -16, 3 -> -30, 5 -> -70, 7 -> -126 (stick right = positive byte = negative roll_in) */
pitch_in = i16(6*pitch_in);  if (pitch_in < 0) pitch_in = (pitch_in + 1) >> 1;      /* pitch-rate 6*p; nose-down (negative) is halved: -3, -6, -9 ... -21 */
if (alt_s == gnd && pitch_in < 0 && fm_pitch <= 0) pitch_in = 0;         /* on the ground the nose cannot be pushed down */
if (roll_in != 0 || pitch_in != 0) ap_alt [3652] = 0;                    /* any stick motion switches the autopilot off (key '7' engages it, cba0) */
hud_lamp_set(4, ap_alt ? 15 : 0);                                         /* 898B: autopilot lamp (no-op while the panel is hidden) */
```
Keyboard deflection (the INT 9 hook stores 0x26 / 0xDA on the first press of an arrow key and 0x01 / 0xFF when the same key repeats within 5 BIOS ticks, 0x80 on release;
`1181:0072`, R1): after the `k/3` scaling and the dead zone the stick step `r` is **+-1 (first press) / +-2 (repeat) at sensitivity 0, +-3 / +-5 at sensitivity 1**
(e.g. 0x26: `(-90*k/3) = -30 / -60 -> 0x62 / 0x44 -> 6-8+1 = -1 / 4-8+1 = -3`). Joystick: `r` = `(byte>>4)-8` plus the dead zone, so full deflection gives +-7.
Conventional reading: stick byte high (back / down) = positive `pitch_in` = nose up (U for the physical sign; the formulas are exact).

### 2.3 Autopilot override (P5) (V)
`ap_alt = [3652]` (target `alt_s`, set by cba0 key `7`: `max(alt_s, 500)`), `[C078]` = bearing to the selected waypoint (written by 7898).
```c
if (ap_alt) {
   err_h  = fx_clamp((int)([C078] - fm_yaw), -0x1400, 0x1400) * 2;                       /* c96d */
   roll_in  = -fx_clamp_wrap(((err_h - fm_roll) >> 6), -0x18, 0x18);                      /* wing-levelling toward the heading: bank command */
   err_v  = fx_clamp((((ap_alt - alt_s) << 4) - [98C6]), -0x1400, 0xC00);                /* uses last frame's lift term */
   pitch_in = fx_clamp_wrap(((err_v - fm_pitch) >> 7), -8, 8);
}
```
(all 16-bit; keep the wrap behaviour, `fx_clamp_wrap` has the quirk.)

### 2.4 Turbulence / buffet and landing flare assist (V)
```c
b = cm ? (int)(((long)(500 - alt_s) * kts) >> 13) : 0;                   /* strong near the ground, only if crashes enabled */
if (!([9B04] & 1)) b += fx_clamp_wrap((kts - 200) >> 4, 0, 0x20);        /* gear DOWN buffet at speed (9B04 bit0 = gear retracted) */
if ([9B04] & 2)    b >>= 1;                                              /* bit1 (key '9', lamp 0x21): halves it - flaps/airbrake (U) */
if (dmg_flags [3632] & 4) b += fx_clamp_wrap(dmg_level [C5C8] << 2, 0, 0x20);   /* flight-control damage */
if (b > 0 && gnd < alt_s) { roll_in += rand_n(b) - (b>>1);  pitch_in += (rand_n(b) - (b>>1)) >> 1; }   /* two rand calls, roll first */
if (([9B04]&1) && pitch_in <= 0 && spd > [B00A] && cm < 2 && abs(fm_roll) < 0x3000 && [98EA] == 0) {     /* gear up, speed above target: flare assist */
    t = (((([98C6] - fm_pitch) >> 3) - alt_s + 0xC8) >> 3);  if (t > 0) pitch_in = fx_clamp_wrap(t, 0, 0x20); }
```
(`abs()` is the RTL abs, so `abs(0x8000)` stays negative and passes the `< 0x3000` test.)

### 2.5 Crash sequence, weather/damage throttle cap, engine spool, fuel (P6) (V)
```c
if (crash != 0) {                       /* EJECTION sequence: cba0 key Shift-F10 (5D00, handler D02E) sets crash = 1 (and [DEA2],[DEC6] = position, [DECA] = alt_s + 8, comm+26h = 2 survived / end_mission(6) too violent); counts frames */
    roll_in = 0x40;  pitch_in = (abs(fm_roll) > 0x4000) ? 0x10 : -8;       /* tumble */
    crash++;  [DECA] += fx_clamp_wrap(0x20 - crash, -96/rate, 128/rate);    /* [DECA] = height of the ejected pilot: rises while crash < 32, then falls; Esc zeroes it (only useful in the falling phase) */
    if ((int)[DECA] < 0) { [DECA] = 0; if (([98E2] & 7) == 0) end_mission(0); }   /* [98E2] = frame counter; 4926(0) = "killed" -> sets E552, comm+28h = 0 */
    if (alt_s == 0 && [3D68] == -1) {            /* crash dive reached sea level */
        spd = 0; thr_cmd = 0; [C0B0] = 0; [3D68] = 0;  [C0A0] = [B2A4] = [C0A4]; [C0AA] = [B2A6] = [C0B2];  [9D0C] = 0xFFF0;  snd_cue(5, 2); }
    if ((crash & 0xFF00) == 0 && (crash & 0xFC) == 0x10 && ([3D60] & 3) == 1) {      /* crash value 16..19 on a frame with (3D60 & 3) == 1: the abandoned aircraft dives and explodes */
        [3D68] = -1; i = [3D60] & 15; [3A5A] = i; [C0A0] = [C0A4]; [C0AA] = [C0B2]; [C0B0] = alt_s;
        crashpos[i] = { [C0A4], [C0B2], alt_s };            /* DS:39DA + 8*i, three words */
        [9D0C] = 0xFFF0;  snd_cue(4, 2);  fm_pitch = 0xC000 (nose straight down);  [2DFB] = 1; }
}
if (dmg_flags & 2) { m = (0x19 - dmg_level) << 2; if (m < thr_cmd) { thr_cmd = max(m, 0); draw_throttle(); } }  /* engine damage caps the throttle */
thr += tdiv(tdiv(thr_cmd - thr, 4), rate);  if (thr < thr_cmd) thr++;  if (thr_cmd < thr) thr = thr_cmd;      /* spool-UP gradual (+25%/rate, min +1), spool-DOWN instantaneous */
if (([3D60] % (2*rate)) == 0 && thr_cmd != 0) fuel += tdiv(thr_cmd*thr_cmd, -1000) - ((dmg_flags & 0x20) ? dmg_level : 0) - 2;   /* 0x20 = fuel leak */
if ((int)fuel <= 0) { thr = 0; fuel = 0; }               /* flame-out */
```
`[3D60]` is the global frame counter (39C4). `end_mission(n)` = `4926(n)`: sets `[E552] = 1` (leave the frame loop) and `comm+28h = n` (n = 0 killed, 5 crash landing, 7 quit without result) - R4 owns it.

### 2.6 G load and the G string (P7) (V)
```c
g16 = Gtab[(abs(fm_roll) & 0x7F00) >> 8];                                 /* DS:2D14, 128 bytes: 16/cos(bank) shape: 0x10 at level, 0x20 at 59 deg, 0xD0 at 90 deg, symmetric */
if (gnd < alt_s) g16 += pitch_in;                                          /* pulling adds load */
if (g16 > 0x80) { g16 = 0x80; pitch_in = fx_clamp_wrap(0x80 - Gtab[idx], 0, pitch_in); }   /* 8 G limit: pitch authority is cut to the remaining 8G - bank load */
sprintf(G_text /*DS:98D0*/, "%d.%dG", tdiv(g16,16), (abs(g16) & 0xE) >> 1);      /* NB: second digit counts EIGHTHS (0..7), not tenths: 1.5 G shows "1.4G" */
```
G_text is drawn by the asm HUD (`10A8:000C`, `bx = 98D0`).

### 2.7 Speed, lift and turn model (P7) (V - all constants)
```c
A    = fx_sin_mul(fm_pitch, 120);                                          /* climb penalty */
t2c  = tdiv((thr - A) * 720, 100);                                         /* net thrust, 32-bit product */
e448 = ((((uint16)alt >> 6) + 0x400) * 120) >> 10;                         /* density term: 120 at sea level, ~229 at 60000 */
t2c  = (int16)((t2c * (((uint16)alt_s >> 7) + 0x400)) >> 10);              /* 32-bit multiply */
t2c  = (int16)tdiv((long)(int16)t2c * (100 - (fuel >> 9)), 90);            /* heavier with fuel */
t2c  = (int16)(((long)(int16)t2c * (0x80 - g16)) >> 7);                    /* induced drag */
e448 = abs((e448 * fx_isqrt(g16 << 2)) >> 3);                              /* speed reference scales with sqrt(G): x1 at 1 G (isqrt(64)=8) */
if ([9B04] & 2)    { e448 -= e448 >> 2;  t2c -= t2c >> 4; }              /* bit1 devices: -25% reference, -1/16 thrust */
if (!([9B04] & 1))   t2c -= t2c >> 3;                                      /* gear down drag */
[B00A] = e448 * 27;                       /* target / limit speed, raw units (16-bit product) */
t18  = fx_clamp_wrap(t2c, 0, 0x383) * 27;                                  /* thrust-limited speed */
spd += tdiv(tdiv((long)(int16)t18 - (long)(int16)spd, 16), rate);         /* NB: spd is sign-extended here; fine below 32768 */
t16  = fx_clamp_wrap((t18 - spd) >> 2, 0, [B00A]);
[B758] = min(tdiv((int16)([B00A] - t16) * 0xC00, abs(spd) + 1), 0x2000);   /* turn/lift limit, unsigned compare */
[98C6] = fx_cos_mul(fm_roll, [B758] - 0x200);                              /* lift component, also the autopilot's pitch term */
if ([9B04] & 8) {                                                          /* wheel brakes */
    if (alt_s == gnd) { spd -= ((4 - cm) * 0xD8) / rate;  if (gnd != 0 && spd < 0x1B0) spd = 0; }   /* 216/rate per frame (cm 0), 144 (cm 2); on a deck below 16 kt: stop */
    else               spd -= (spd >> 4) / rate;                           /* air-brake effect in flight */ }
if (spd > 0xAFC8) spd = 0;                                                 /* underflow guard */
hspd = fx_cos_mul(fm_pitch, spd);   kts = spd / 27;   snd_cmd(0x17, kts);  /* engine pitch/volume cue every frame */
turn = fx_cos_mul(fm_pitch, tdiv(fx_sin_mul(fm_roll, g16<<4) << 7, (spd>>8) + 0x20));   /* yaw increment: g*sin(bank)/V */
if (alt_s == gnd) {                       /* ground handling */
    turn = -(roll_in << 6);  roll_in = 0;                                  /* roll stick steers (nose wheel) */
    if (kts < e448) pitch_in = 0;                                         /* no rotation below rotation speed */ }
if ([98E0]) { pitch_in = 0xFC00 - fm_pitch; spd = 0; thr_cmd = 0; }       /* [98E0] = arrested by the carrier wire (set by 39C4 when caught, snd 15) */
```
Constants: sound cue after the wire / catapult are produced elsewhere (`39C4`).

### 2.8 Attitude integration and ground/stall constraints (P8/P9) (V)
```c
dz = tdiv((long)roll_in << 7, rate);                  /* roll angle step */
if (dz) { R = {c,s: cos16(dz), sin16(dz)}; DS:2DCA matrix: [0]=[4]=c, [1]=s, [3]=-s (the rest is preset: [8]=32767 ...); fm_mat = fm_mat x R; }
dx = tdiv((int16)(pitch_in << 7), rate);              /* NB 16-bit shift here, 32-bit for roll */
if (dx) { DS:2DB8 matrix: [4]=[8]=c, [7]=s, [5]=-s; fm_mat = fm_mat x R; }
dy = tdiv(turn, rate);
if (dy) { DS:2DA6 matrix: [0]=[8]=c, [2]=s, [6]=-s; fm_mat = R x fm_mat; }          /* yaw is a PRE-multiply */
mat_to_euler();                                        /* 2C9E: fm_yaw, fm_pitch, fm_roll from fm_mat */
if (spd < [B00A] && gnd < alt_s) {                     /* below target speed in the air: nose drops (stall mush) */
    n = (cm == 2 || dmg_level > 8) ? 1 : 2;  [2DFB] = 1;
    fm_pitch -= (uint16)([B00A] - spd) >> n;
    if ((int)fm_pitch < 0 || alt_s < 0xC8) snd_cue(0x0E, prio 1); }      /* stall warning (cmd 14), also whenever below 200 */
if (alt_s == gnd) {                                    /* on the ground: wings level, nose cannot move */
    if (fm_roll) { fm_roll = 0; [2DFB] = 1; }
    if (fm_pitch < 0 || (fm_pitch > 0 && kts < e448)) { if ([98E0] == 0) fm_pitch = 0; [2DFB] = 1; } }
[98E0] = 0;
[E542] = (comm-mode [E2FE] != 0) ? (abs(fm_pitch) - abs(fm_roll)/2 > 0x1000) : 1;   /* byte read by the asm HUD: draw the pitch ladder */
if ([2DFB]) mat_from_euler();                         /* 2E43: rebuild fm_mat from the Euler angles, [2DFB] = 0, [2DF8] = 0 */
```
`2C6E` (the multiply wrapper) increments `[2DF8]` and sets `[2DFB]=1` on every 8th rotation: the matrix is re-orthogonalised from the Euler angles at least every 8 frames.

### 2.9 Position, altitude, touch-down (P10/P11) (V)
```c
old = alt;
vs  = [E55A] = mul15(spd >> 4, sin16(fm_pitch - [98C6]));                 /* vertical speed (positive = climb) */
alt += tdiv(vs, rate);
[BA10:BA12] += tdiv(tdiv(mul15(hspd, sin16(fm_yaw)), 16), rate);          /* 32-bit east position (sign-extended step) */
[C02A:C02C] += tdiv(tdiv(mul15(hspd, cos16(fm_yaw)), 16), rate);          /* north position */
if (alt > 0xF230 || gnd > alt) alt = gnd;   if (alt > 0xEA60) alt = 0xEA60;
alt_s = alt < 0x2000 ? alt : alt < 0x4000 ? ((alt-0x2000)>>1)+0x2000 : ((alt-0x4000)>>2)+0x3000;     /* compressed altitude scale */
if (alt_s == gnd) {
   if (old > gnd /*signed*/ && [3D74] /*landing check on*/) {
       if (crash == 0) snd_cue(0x0A, prio 2);                               /* touch-down chirp (cmd 10) */
       lim = (siteflags[[DFE0]] & 2) ? 0x100 : 0x80;                         /* DS:B2AB + 16*[DFE0]: runway record flags; bit1 = carrier deck */
       bad = (uint16)(lim) < (uint16)(-(cm * [E55A]))                        /* sink rate x crash mode */
          || (cm != 0 && ( ([9B04] & 1) /*gear up*/ || ((0x18/cm & 0xFF) << 8) < (uint16)abs(fm_roll) ));
       if (bad) { snd_cue(4, 2); wait_ticks(0x3C); end_mission(5); } }       /* 04BB busy-waits 60 timer ticks (~1 s), 4926(5) = crash landing */
   [E55A] = 0; }
hist = &ring[[3D60] & 15];   /* DS:B056 + 16*i */ hist->x=[BA10:BA12]; hist->y=[C02A:C02C]; hist->alt_s=alt_s; hist->yaw,pitch,roll = fm_*;     /* 16 x 16 bytes */
if ([E2FE] == 1) {                                                              /* cockpit-sway for the external-view mode */
    n = ([3D64] >= 0) ? clamp_wrap((fx_dist([C0A4]-obj[[3D64]].x, [C0B2]-obj[..].y) * rate) >> 7, 0, 12) : rate - 1;     /* obj = DS:C140 + 0x24*i, x at +0, y at +2 */
    r = &ring[([3D60]-n) & 15];   dy = fm_yaw - r->yaw;  dp = fm_pitch - r->pitch;
    [E546] = fx_sin_mul(fm_roll, dp>>2) + fx_cos_mul(fm_roll, (-dy)>>2);
    [E550] = fx_sin_mul(fm_roll, dy>>2) + fx_cos_mul(fm_roll, dp>>1); }         /* read by 2F82 as view shake / lag (R3) */
```
`[C0A4]`/`[C0B2]` (written by 39C4) = tile position: `C0A4 = (BA10:BA12 + 16) >> 5`, `C0B2 = 0x8000 - ((C02A:C02C + 16) >> 5)`.
The three `Euler/matrix` writes at the end of P8 and the first-call init: `if ([C0AE]==0) { b052=alt=alt_s=thr=thr_cmd=roll=pitch=0; fm_yaw = (Dir[theatre] > 0) ? 0 : 0x8000 (DS:3D7C, one signed word per pilot[+38h] theatre: -1 1 -1 1 -1 1); if (siteflags[[E2DC]] & 2) yaw += 0x0400; mat_from_euler(); draw_throttle(); [C0AE] = 1; }`.

### 2.10 Screens driven from the key switch (not in the differential test, read from the disassembly)
* **Alt-Q quit requester** (`205F`): `save = [2646]` (frame-time estimator, so the wait does not disturb the adaptive rate); `slot69 screen_off`; `1088:00D7` = read the 256-entry DAC into `DS:0D4A` (768 bytes);
  `blit_rect(page0 -> page1, 0,0, 320x200)` (backup of the visible frame); `1088:0107` = set the DAC from `DS:134A` (= a built-in copy of `ADV.PAL`, verified identical) via `pal_write_vsync`; `d7f4("adv.pic" /*DS:2D08*/, page 0)` (decode the picture to the screen);
  `slot70 screen_on`; `snd_cmd(0x19)` (engine drone off); `while (kbd_empty() != 0) ;` (wait for a key; slot 90 returns 0 when a key is waiting); `k = kbd_get(); if ((k & 0x5F) == 'Y') { end_mission(7); exit_code[8D] = 0; }`
  `else { screen_off; blit_rect(page1 -> page0, whole screen); 1088:00EE (restore the DAC from DS:0D4A with vsync); screen_on; }` then `d6e5()` (re-evaluate the engine drone) and `[2646] = save`.
  The question text is in the picture; the answer is any `Y`/`y` (also `y` with scan code, only the low byte counts), every other key (including Esc) cancels. 'Y' does not wait for the screen to be restored: the program is leaving.
* **Alt-P pause** (`3939`): `snd_cmd(0x19)`; `save=[2646]`; `do { while(!kbhit); k = bios_keyboard(0); } while (k == 0x1900);` (Alt-P itself does NOT resume, any other key does); `d6e5(); [2646] = save`. Nothing is drawn: the frame stays frozen on screen.
* **Alt-B boss screen** (`2110`): `if ([365E]) blit_rect(page ctx[3FE2] (=0) -> page ctx[4012] (=2), rect x0,y91, 320x109)` (save the lower instrument panel into the clean page 2);
  `gfx_set_fg(0); gfx_fill_rect(0,0,319,199)` (both display pages black); `3939()` (pause until a non-Alt-P key); `if ([365E]) gfx_restore_rect(0,91,320,109)` (page 2 -> pages 0 and 1) and redraw the throttle digits.

---

## 3. Orientation helpers

### 0000:2c6e `fm_rotate_apply(const int *A, const int *B)`  (cdecl: A = [bp+4], B = [bp+6]) (V)
> **S3.8:** owned by `flight.md` section 2.2 (verified there; where they differ, flight.md is right).
`[2DF8]++; if (([2DF8] & 7) == 0) [2DFB] = 1; mat_mul(A, B, DS:2DDC); memcpy(DS:2D94, DS:2DDC, 18);` - i.e. `fm_mat = A x B` always lands in `fm_mat`. Calls in 1e9e:
roll `(2D94, 2DCA)`, pitch `(2D94, 2DB8)` (post-multiply), yaw `(2DA6, 2D94)` (pre-multiply). The three increment matrices are static words in DGROUP; 1e9e writes only
cos/sin/-sin words (see 2.8) and relies on the preset constants (`32767` on the fixed diagonal element, 0 elsewhere).
### 0000:2c9e `fm_mat_to_euler()` (V, 400 random matrices incl. gimbal cases)
```c
pitch = asin(-M5);  c = cos16(pitch);
if (c != 0) {
   yaw  = (|M2| < 0x5A81) ? asin(|ratio(M2,c)|) : acos(|ratio(M8,c)|);
   if (M2<=0 && M8<0) yaw += 0x8000;  if (M2>0 && M8<0) yaw = 0x8000 - yaw;  if (M2<0 && M8>0) yaw = -yaw;
   roll = (|M3| < 0x5A81) ? asin(|ratio(M3,c)|) : acos(|ratio(M4,c)|);
   if (M3<=0 && M4<0) roll += 0x8000; if (M3>0 && M4<0) roll = 0x8000 - roll; if (M3<0 && M4>0) roll = -roll;
} else { roll = 0; yaw = asin(M1); same three corrections as roll but written to yaw }
fm_yaw=yaw; fm_pitch=pitch; fm_roll=roll;                    // [2DEE] [2DF0] [2DF2]
if (pitch > 0x38E3 && pitch < 0x4001) || (pitch < (int16)0xC71D && pitch > (int16)0xBFFF) [2DFB] = 1;   // within ~5 deg of a pole: rebuild from angles
if ([2DFA] != 0 && roll == 0) [2DFB] = 1;
```
`Mi` = `fm_mat[i]` row-major (M2 = DS:2D98, M3 = 2D9A, M4 = 2D9C, M5 = 2D9E, M8 = 2DA4, M1 = 2D96); `ratio`, `asin`, `acos` of section 1. `[2DFA]` is never written in the image (always 0 (V)).
### 0000:2e43 `fm_mat_from_euler()`: `mat_from_euler(2D94, [2DEE], [2DF0], [2DF2]); [2DFB] = 0; [2DF8] = 0` (V). Callers 1e9e, and via the dirty flag only.
### 0000:2e64 / 2ebe / 2f2f / 2f40 : see section 1.

## 4. HUD digit / lamp / screen helpers (they draw only when the instrument panel is visible: `[365E] != 0`)

### 0000:d83e `hud_lamp_recolor(TextCtx *ctx, int lamp, int on)`  (V: logic differential-tested for lamps 0, 5, 33, 35)
Lamp table `DS:46D8`, 12 bytes per lamp, 37 lamps (0..36): `+0 x, +2 y, +4 w, +6 h, +8 colour-set (0..4), +10 state (0/1)`. Colour table `DS:4672`, 8 words per set:
`row[set] = {A0,A1, B0,B1, C0,C1, D0,D1}`; set 0 = 45,47,44,46; set 1 = 38,41,55,42; set 2 = 54,41,55,42; set 3 = 16,41,43,42; set 4 = 16,40,43,39,54,41,55,42 (sets 0-3 use only A and B).
```c
(from,to) = on ? (0,1) : (1,0);
rect_recolor(&ctx->page, x, y, x+w, y+h, row[A+from], row[A+to]);         // slot 41; A = words 0/1
rect_recolor(&ctx->page, x, y, x+w, y+h, row[B+from], row[B+to]);         // B = words 2/3
if (set == 4) { same with C (words 4/5) and D (words 6/7); }
state = to;   // stored at +10
```
Lamp rectangles (x,y,w,h,set): 0 (119,94,18,8,0) 1 (139,94,18,8,0) 2 (159,94,18,8,0) 3 (184,94,21,8,2) 4 (38,105,24,8,3) 5 (4,117,18,8,4) 6 (24,117,18,8,2) 7 (44,117,18,8,2) 8 (4,127,18,8,2) 9 (24,127,18,8,2) 10 (44,127,18,8,2) 11 (4,137,18,8,2) 12 (24,137,18,8,2) 13 (44,137,18,8,2)
14 (51,149,11,6,2) 15 (51,159,11,6,2) 16 (51,169,11,6,2) 17 (4,179,28,8,0) 18 (36,179,26,8,0) 19 (4,191,18,8,2) 20 (24,191,18,8,2) 21 (44,191,18,8,2) 22 (80,173,18,8,0) 23 (100,173,18,8,0) 24 (142,193,14,5,1) 25 (232,184,17,8,2)
26 (190,181,18,8,0) 27 (190,191,38,8,0) 28 (170,181,18,8,0) 29 (210,181,18,8,0) 30 (170,191,18,8,0) 31 (257,103,25,8,3) 32 (257,113,25,8,2) 33 (284,113,25,8,4) 34 (257,123,25,8,2) 35 (284,123,25,8,2) 36 (302,179,13,7,2).
Used here: lamp 35 (0x23) = wheel brakes, 33 (0x21) = key 9 device, 32 (0x20) = autopilot.
### 0000:898b `hud_lamp_update(int idx, int state)`  (no-op if `[365E]==0` or state unchanged) (V)
Table `DS:3F02`, 6 bytes per entry `{lamp_id, on_value, cur}`: 0:(19,0,0) 1:(20,0,0) 2:(31,0,0) 3:(34,0,0) 4:(32,0,0) 5:(17,0,0) 6:(18,0,0) 7:(16,0,0) 8:(21,3,3) 9:(36,3,3) 10:(5,10,10) 11:(6,10,10) 12:(7,10,10) 13:(12,10,10) 14:(11,10,10).
`if (cur[idx] != state) { hud_lamp_recolor(ctx[3FE2], lamp_id, state != on_value); if (idx == 2) hud_lamp_recolor(ctx[4012], lamp_id, state != on_value) /* also on the clean page 2 */; cur[idx] = state; }`
So "on" (recolour state0->state1) is when `state != on_value`. 1e9e uses idx 4 with 15 / 0 (autopilot).
### 0000:d92d `hud_digits_draw(int page, int slot, const char *s)`
Slot table `DS:4654`, 6 bytes: `{x, y, ndigits}`: 0 (35,149,2) 1 (35,159,2) 2 (35,169,2) 3 (261,179,3) 4 (261,191,3). For each of the first `ndigits` characters: digit = c-'0' if '0'..'9' else 0;
`blit_rect(page2 -> page, sx = DS:4640[digit], sy = 31, dx = x, dy = y, w = 4, h = 7); x += 5`. `DS:4640` = 67,72,77,82,87,92,97,102,107,112 (digit glyph strip in the cockpit picture). (V by reading)
### 0000:38ab `hud_number_draw(int slot, int value)`: `s = ltoa(value)`; value 0..99 is zero-padded to three characters ("0dd"/"00d", buffer `DS:2E4A` pre-filled "000"), >= 100 uses the plain string; `hud_digits_draw(0, slot, s)`.
`38f8 hud_draw_fuel()`: if `[365E]`: `hud_number_draw(3, min([3634], 9999) / 10)` (fuel/10, slot 3 at (261,179)). `3926 hud_draw_throttle()`: if `[365E]`: `hud_number_draw(4, thr_cmd)` (slot 4 at (261,191)).
### 0000:8929 `gfx_set_fg(int c)`: `ctx[3FE2].fg = ctx[3FFA].fg = c` (TextCtx +4, pages 0 and 1). 0000:893f `gfx_fill_rect(x0,y0,x1,y1)`: `1001:024C(ctx[3FE2],...)` then the same on ctx[3FFA] (solid poly-fill via slot 40 using the colour in ctx+4, both display pages).
### 0000:8bd3 `gfx_restore_rect(x, y, w, h)`: `blit_rect(page2 -> page0, x,y,w,h); blit_rect(page2 -> page1, x,y,w,h)` (clean cockpit picture back onto both pages). Callers 00f0, 1e9e, 2f82.
### 0000:d9a3 `hud_side_strips(int page)` (frame loop, only in view 0 after the asm HUD; page = `[4088]` the back page) (V mechanics, U purpose: HUD/panel side decorations)
`if ([463E] == 0) return;` (`[463E]` toggled by a cba0 key). Mode `[294B] == 0`: `blit_rect(2 -> page, sx 264 sy 33 -> dx 43 dy 33, 6 x 58)` and `(sx 271 sy 33 -> dx 272 dy 33, 6 x 58)`; mode 1: first `91A4(0x21,0,0x8D,0,0x36,0x5B,0)` and `91A4(0xE9,0,0xC4,0,0x36,0x5B,0)` (cockpit_keyblit),
then the strips to `(95,43)` h 48 and `(219,43)`... (`dx` 0x5F / 0xDB, dy 0x2B, h 0x30). Then the weapon-select highlight: `[46C2]` = selected store (0..3), `[46C4]` = the previously drawn one;
if different: `rect_recolor(page2, 252, y-3, 259, y+h-3, old = rec.colour, new = 0x1E)` for the old one and `old=0x1E, new=rec.colour` for the new one, with `rec = DS:46C6 + 6*(sel-1)` = `{y,h,colour}`: (72,4,41) (77,7,47) (85,4,14); `[46C4] = [46C2]`.
Finally `blit_rect(2 -> page, sx 251 sy 67 -> dx = 33 (mode 0) / 85 (mode 1), dy 70, 10 x 21)` (selected-store icon).

## 5. Sound gate, frame-rate parameters, misc

### 0000:d6bf `snd_cue(int cmd, int prio)` (cdecl: cmd [bp+4], prio [bp+6]) (V)
`if (prio >= [43C2] && ([C06E] == 0 || prio > 1)) snd_cmd(cmd); d6e5();`  `[43C2]` = sound level setting 0..3 (menu Alt-V shows `3 - [43C2]`; 0 = everything). Cues used by R2 code (cmd/prio -> ASOUND track, sound.md 8): 0x0A/2 touch-down (v5<-F94), 0x0B/2 engine start, 0x0C/0 throttle cut, 0x0E/1 stall warning (v5<-F8A), 0x11/2 catapult (v1<-F68), 4/2 explosion/crash, 5/2 crash impact; `snd_cmd(0x17, kts)` engine pitch; `0x18 / 0x19` drone on/off.
### 0000:d6e5 `snd_engine_refresh()`: `snd_cmd( ([43C2]==0 && thr_cmd != 0 && [C06E]==0) ? 0x18 : 0x19 )` (engine drone only at the highest sound level, with throttle, not while ejecting). (V)
### 0000:d707 `frame_params_update()` (callers 39C4, cba0, d7bb) (V by reading)
`[43B8] = (rate > 15) ? fx_clamp_wrap((-120/rate + 9) >> 1, 1, 4) : 0;  rate = fx_clamp_wrap(rate, 4 - [3D7A], 15);  [B16C] = fx_clamp_wrap(rate*2, 3, 16);  [B286] = rate*250;  [9F64] = rate*200;`
(`[43B8]` read by 03CE 39C4 cba0; `[B16C]` by 465B 5730 A17E; `[B286]` by 559A 671A; `[9F64]` by 5730 671A: per-rate timing constants of weapons / AI.) `[3D7A]` = frame-rate setting (0..2).
### 0000:d7bb `frame_rate_halve()`: `if ([3D7A] == 2) { [3D7A] = 1; rate <<= 1; d707(); }` (callers 5460 753F cba0: time-acceleration cancel).
### 0000:d76b `view_range_setup()` (owner render3d; callers 39C4 cba0) (V by reading)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 1; where they differ, render3d.md is right.
`for i in 0..5: [48A6 + 2i] = 0x20 << (i + (uint8)[98DE]);  [48B2] = [48AE] + [48B0];  [48B4] = fx_clamp_wrap([48B0]*2, 0x1000, 0x270F);  [48B6] = ([98DE] + 1) * 0xD05;` `[98DE]` = detail / view-range level (read by 03CE 04DA 1442 8C10 ...); `[48B6]` is read by `E8B5` (cull).
### 0000:d7d0 `nav_copy_xy(int dst, int src)`: `[2E6C + 4*dst] = word at B2A4 + 16*src; [2E6E + 4*dst] = word at B2A6 + 16*src` (copy the x,y of site record `src` to a 4-byte destination slot; callers: cba0 only). Site/runway records: 16 bytes from `DS:B2A4`, `+0 x, +2 y, +7 flags` (bit1 = carrier deck, bit3 = short field (U)).
### 0000:d7f4 `pic_show(const char *name, int page)`: `h = dos_open(name, 0) [d81d]; 11B9:000E(h, page); dos_close(h) [d82f]` - open + decode a PIC to `page` + close (fatal on error through the RTL). `d81d(name, mode)` = `1199:000C dos_open`; `d82f(h)` = `1199:00C8 dos_close`. Callers: Alt-Q (page 0, "adv.pic").

## 6. Joystick glue (asm module 10A8) (V by reading; scaling formula differential-tested in r2_ref.joy_scale design)
> **S3.8:** owned by `flight.md` section 2.4 (verified there; where they differ, flight.md is right).
Data (DS): calibration table 40 bytes at `2CA2` = copy of comm+48h (min `2CA2[2]`, max `2CAA[2]`, centre `2CB2[2]`, low range `2CBA[2]`, high range `2CC2[2]`, each array 4 words of which 2 are used = X,Y); raw counts `2CCA` (X) `2CCC` (Y); scaled output bytes `2CD2` (X) `2CD3` (Y).
* `10A8:0CB7 joy_read_raw` (already named): trigger port 201h, count loops (X = bit0, Y = bit1) up to 65535, **timeout = 0xFFFF both**.
* `10A8:0CA8 joy_poll()` (far): `joy_read_raw(); joy_scale(X); joy_scale(Y)`. Used by 1e9e when `comm[0x72] & 1`.
* `10A8:0C87 joy_recenter()` (far): `joy_read_raw(); for axis in {X,Y}: 0C95: min = max = centre = raw` (so subsequent motion re-learns the range). Alt-S.
* `10A8:0C95` local helper of the above (SI = 0 / 2 axis index): `centre[si] = min[si] = max[si] = raw[si]`.
* `10A8:0CFF joy_scale(axis)` (named earlier): `d = raw - centre; if (d == 0) out = 0x7F; else if (raw < centre) { if (raw > min) out = (0xFFFF - ((d'<<16)/lo_range)) >> 9 (d' = centre-raw) ; else { min = raw; lo_range = centre - raw; out = 0; } } else { if (raw < max) out = 0x80 + (((d<<16)/hi_range) >> 9); else { max = raw; hi_range = d; out = 0xFF; } }` (unsigned 16-bit; self-calibrating).

---

## Appendix A. cba0 key table (mechanically extracted by simulating the compare chain at `CBB9`/`D133` over all 65535 codes; handler address = entry of the case; semantics belong to R10 (`input_command_key`), see its spec)
`code -> handler`: 0231 '1' CC3E, 0332 '2' CC45, 0433 '3' CC37, 0534 '4' CC1A, 0635 '5' CC49, 0736 '6' CC4D (gear), 0837 '7' CE45 (autopilot toggle: alt_s max 500), 0938 '8' CC84, 0A39 '9' CC69, 0E08 Backspace CE8C, 1100 Alt-W CCA4, 1400 Alt-T CDF5, 1B5D ']' / 1B7D '}' CF1A,
1A5B '[' / 1A7B '{' CF61, 1C0D Enter CEA3, 1E00 Alt-A CC94, 2000 Alt-D CCD8, 2100 Alt-F CD5C, 2300 Alt-H CC9C, 2C5A 'Z' CD95, 2C7A 'z' CC10, 2D58 'X' CDB2, 2D78 'x' CC15, 2E00 Alt-C CCAC, 2F00 Alt-V CDB8, 2F76 'v' CE76, 3062 'b' CE63,
316E 'n' CBBC, 324D 'M' CED0, 326D 'm' CED9, 332C ',' CEE7, 333C '<' CEC7, 342E '.' CEEE, 343E '>' CEBE, 352F '/' CEE0, 353F '?' CEB5, 3920 Space CE10, F1 3B00 CEAC, F2 3C00 D04C, F3 3D00 D086, F4 3E00 D099, F5 3F00 D0A4, F6 4000 D0A9, F7 4100 D0B7, F8 4200 D0C1,
F9 4300 D123, F10 4400 D12B, Shift-F1 5400 CFAE, Shift-F2 5500 CF9C, Shift-F3 5600 CFA5, Shift-F4 5700 CFB7, Shift-F5 5800 CFC0, Shift-F6 5900 CFC9, Shift-F8 5B00 D0CF, Shift-F10 5D00 CFD2 (eject, sets C06E).
(5400..5D00 are Shift-F1..F10 by BIOS code.) Keys not listed do nothing. Keys of 1e9e (section 2.1) are not in this chain.

## Open items
| item | note |
|---|---|
| Alt-Q / Alt-P / Alt-B screens | read from the disassembly only, not in the differential test (need video/DAC stubs) |
| exact meaning of `[9B04]` bit1 (key `9`, lamp 33) | model effect = -25 % speed reference, -1/16 thrust, half buffet: flaps/speed-brake like; name by R10 |
| `[2DFA]` | read by 2C9E, never written (always 0) |
| `[E2FE]` (view/camera mode flag) | selects `[E542]` and the sway block; owner R3/R4 |
| `[3D74]` | landing-check enable (written 39C4) |
| `[C0A2]` | ground height under the aircraft (written 39C4); 0x80 on carrier decks |
| sim_rate semantics | all rates divide by it; keep 4 for a fixed 70 Hz port? (needs a timing decision at merge: original adapts 4..15 from measured frame time) |
| `d9a3` purpose | mechanics exact, visual role not confirmed (U) |

## DGROUP globals seen
| offset | size | name | meaning | users |
|---|---|---|---|---|
| 2084 | 257w | sin_tab | sine table, section 1 | 109d:0090 2ebe |
| 2D14 | 128b | gload_tab | 16/cos(bank) load table | 1e9e |
| 2D94 | 9w | fm_mat | aircraft orientation matrix Q15 | 1e9e 2c6e 2c9e 2e43 e26b |
| 2DA6 / 2DB8 / 2DCA | 9w each | fm_rot_yaw / pitch / roll | per-frame increment matrices | 1e9e |
| 2DDC | 9w | fm_mat_tmp | product scratch | 2c6e |
| 2DEE 2DF0 2DF2 | w | fm_yaw fm_pitch fm_roll | Euler angles u16 turns | 1e9e 2c9e 2e43 + render |
| 2DF4 / 2DF6 | w | fm_alt_s / fm_alt | compressed / raw altitude | 1e9e |
| 2DF8 / 2DFA / 2DFB | w,b,b | fm_mat_ops / fm_flag / fm_mat_dirty | rebuild bookkeeping | 2c6e 2c9e 1e9e |
| 2DFE | w | thr_cmd | throttle 0..100 | 1e9e d6e5 3926 |
| 2E02 | w | joy_recenter_cd | Alt-S debounce | 1e9e |
| 2CD2 2CD3 / 2CD6 2CD7 | b | stick_x y / key_stick_x y | scaled stick bytes / INT9 virtual stick | 1e9e 1181:0072 |
| 294A 294B | b | hud_mode / hud_style | `c` toggles 294B | 1e9e 10A8 |
| 2646 | w | frame_time_est | saved/restored around Alt-Q, pause | 1e9e 3939 39C4 |
| 3652 | w | ap_alt | autopilot target alt_s | 1e9e cba0 |
| 3634 | w | fuel | | 1e9e 38F8 |
| 3660 | w | sim_rate | | everywhere |
| 3D7C | 6w | theatre_dir | -1 1 -1 1 -1 1 start heading selector | 1e9e |
| 43B8 43C2 43BA 43BE 43C0 | w | frame_skip / snd_level / kbd_sens / btn0 / btn1 | | d707 d6bf 1e9e cb72 |
| 46D8 / 4672 / 3F02 / 4654 / 4640 / 46C2 46C4 46C6 | tables | lamp rects / lamp colours / lamp state / digit slots / digit strip / store highlight | section 4 | d83e 898b d92d d9a3 |
| 98C6 98DC 98E0 98EA | w | fm_lift / g16 / arrested / fm_flag_98ea | | 1e9e |
| 98D0 | str | g_text | "n.dG" | 1e9e 10A8 |
| B00A B052 B168 B170 B758 E448 E446 DE8A E55A | w | fm_speed_target / spd / kts / thr / turn_lim / vref / pitch_in / roll_in / vs | | 1e9e |
| B056 | 16x16b | fm_history | position+attitude ring indexed by [3D60]&15 | 1e9e (cockpit sway, others) |
| BA10:BA12 C02A:C02C | 2 x long | fm_x / fm_y | world position | 1e9e 39C4 |
| C06E DECA 3D68 3A5A 39DA | | eject_state / eject_height / crash_dive / crashpos | | 1e9e |
| 9B04 3632 C5C8 | | sys_flags / dmg_mask / hit_count | | 1e9e |
| DE90 | w | key_code | key read this frame | 1e9e 43EE |
| C0AE | w | fm_init_done | | 1e9e |
| E542 E546 E550 | | hud_ladder / sway_x / sway_y | | 1e9e |

## Structures seen
* pilot record (comm+1272h): `+38h theatre, +3Ah war level, +3Ch mission type, +3Eh enemy skill, +40h crash mode (0/1/2), +42h flight model`.
* comm: `+26h`, `+28h` result codes (mission_end), `+72h` bit0 joystick enabled, `+48h` joystick calibration (40 bytes).
* lamp record 12 bytes, lamp-state entry 6 bytes, digit slot 6 bytes, store-highlight record 6 bytes (section 4).
* history ring entry (16 bytes, 16 entries from B056): `+0 x(long) +4 y(long) +8 alt_s +10 yaw +12 pitch +14 roll`.
* site record 16 bytes from B2A4 (`+0 x +2 y +7 flags`), object record 0x24 bytes from C140 (`+0 x, +2 y`).



---
# Part 3: views, camera, cockpit overlays, scene entry (R3)

*Reader pass R3, original title: S3.6 reader R3 - view / cockpit dispatcher, scene entry, map MFD, terrain queries*

Range: `2f82` (view + cockpit frame), `3968`, `91a4`, `17a3`, `8387`, `03ce`, `04bb`, and (index level)
`0d08 0f31 0ffb 77d9 119c 11f2 1353 13ac 13e3 1442`. All addresses are segment 0000 (image offsets) unless
written `SSSS:OOOO`. `(V)` = verified in the disassembly, `(U)` = unclear / needs a runtime or visual check.
Conventions used below: angles are 16-bit binary (0x10000 = 360 deg, 0x4000 = 90 deg), heading 0 = +Y (north),
increasing towards +X (east); `sin16(a,m)` = `caee(a,m)` = sin(a)*m, `cos16(a,m)` = `cb07(a,m)` = cos(a)*m (R2 owns
them; `cb07(a,m) = caee(a+0x4000,m)`), `atan2(a,b)` = `c9d6(a,b)` (a = sin-like component, b = cos-like), `hyp(a,b)` = `c987`
(= max+min/2 approximate length, clamped 7FFF), `clamp(v,lo,hi)` = `c94e(v,lo,hi)` (returns hi if v>hi, v if v>=lo, else hi if
v <= -0x4000 (wrap case), else lo). World units: X east / Y north / Z up all in the same unit = 1/32 cell; one cell = 32 units;
the map is 0x100000 units (32768 cells) wide. Tables of objects store cell coordinates as words with the **inverted
y** `y' = 0x8000 - ycell`; the player globals are `BA10:BA12` (X long), `C02A:C02C` (Y long, not inverted), `2DF4` (altitude word),
`2DEE/2DF0/2DF2` (yaw/pitch/roll). `C0A4 = (BA10+0x10)>>5` (clamped 0x100..0x7E00) and `C0B2 = 0x8000 - ((C02A+0x10)>>5)` (clamped
0x200..0x7D00) are the player's cell coordinates (written by 39c4).

## 0 Overview and call order

Frame loop (`1bbc`, R1): `2f82` -> `7898` (R7) -> if `[C07A]==0`: `10a8:0000`, `d9a3`, `17a3` -> `flip(0xA0*[2E34])` -> `1e9e` -> `39c4`.

`2f82` = `view_frame()` is the *whole view layer* of one frame:
1. builds the camera from the view mode `[C07A]` (11 modes, section 1) into `E44C C07C B770 / B288 B752 B75C`;
2. builds the 3x3 view matrix `B156`;
3. on a change of cockpit/external class or of cockpit picture: reloads the picture of the cockpit from disk, resizes the text clip windows, re-lights the lamps;
4. calls the scene renderer `03ce` (viewport rectangle + camera) which draws sky, ground, objects into page 1 (the back page);
5. pastes the cockpit art over the 3D window (MFD copies to the screen page, canopy corners, rear/left/right frame pictures, front triangle);
6. stores the viewport height in `[2E34]`; the frame loop then flips exactly that many rows of page 1 to A000.

Pages (see video.md): page 0 = A000 (screen), page 1 = back buffer `[85E4]` (all 3D, HUD, MFD drawn here; `[4088]` = back page index
= result of slot 45 `flip_flag`, 1 in normal operation), page 2 = `comm[20h]` segment = `[E558]`, the **picture page** (the cockpit PIC is decoded
here and used as the source of every cockpit overlay). The rows below the viewport (the instrument panel) live directly on A000 and are
refreshed by explicit page 1 -> page 0 copies (MFDs, ADI), not by the flip.

---------------------------------------------------------------------------------------------------------

## 1 View modes (`[C07A]`, word)

Initial value 0 (BSS). Only `cba0` (R10, command dispatcher, called from `43ee`) writes it. The key table was derived from the
compare chain at `cba0:d133..d2ed`; key codes are BIOS `scancode<<8 | ascii`.

| C07A | key (code) | name | camera (section 2) | viewport H | picture loaded into page 2 | extra overlay after the 3D scene |
|---|---|---|---|---|---|---|
| 0x00 | F1 (3B00) | `VIEW_FRONT_PIT` front cockpit | aircraft pose | 91 (107 if `[2DFC]`) | `256pit.pic` | MFD copies, ADI, canopy corners if `[2DFC]` |
| 0x41 | Shift+`>` (343E) | `VIEW_REAR_PIT` rear cockpit | yaw+180, pitch=-pitch, roll=-roll | 157 | `256rear.pic` | centre 138x157 pasted, two 91x157 keyed frames |
| 0x42 | Shift+`M` (324D) | `VIEW_LEFT_PIT` left | yaw-90 | 161 | `256left.pic` | full 320x161 keyed frame |
| 0x43 | Shift+`<` (333C) | `VIEW_RIGHT_PIT` right | yaw+90 | 161 | `256right.pic` | full 320x161 keyed frame |
| 0x44 | Shift+`?` (353F) | `VIEW_FRONT_CLEAN` front, no cockpit | aircraft pose (same as 0) | 200 | none (`1088:00BD`) | small grey triangle (nose) |
| 0x84 | Shift+F2 (5500) | `VIEW_DELAYED` | pose recorded `[3660]` frames ago (ring buffer) | 200 | none | none |
| 0x85 | Shift+F3 (5600) | `VIEW_SIDE_R` external, right of the aircraft | orbit offset D at heading+90, looks back | 200 | none | none |
| 0x86 | - (no key, dead code) | `VIEW_FIXED_N` fixed-azimuth external | camera at Y+D looking yaw 0x8000 | 200 | none | none |
| 0x87 | Shift+F1 (5400) | `VIEW_CHASE` chase view, level | D behind, 4<<z above, yaw=heading, pitch=roll=0 | 200 | none | none |
| 0x88 | Shift+F5 (5800) | `VIEW_TGT_SHOULDER` look at selected target from behind the own aircraft | see 2.7 | 200 | none | none |
| 0x89 | Shift+F4 (5700) | `VIEW_WPN` chase camera on the last launched weapon (`[B04C]`) | see 2.7 | 200 | none | none |
| 0x8B | Shift+F6 (5900) | `VIEW_TGT_FAR` camera behind the target looking back at the own aircraft | see 2.7 | 200 | none | none |
| 0x8C | none: forced by `cba0` end while `[C06E]!=0` | `VIEW_EJECT` ejection / death camera | see 2.8 | 200 | none | none |

* "External" = bit 7 of `C07A` set (0x84-0x8C). Other readers test `C07A & 0x80` (39c4:41c9 collision flag use, 7898:78d9, 8498/84d6 zoom,
  9b57, 9e06, 9f53, a0cd, b881, bde9, dcff) or compare the value (`7898:8037 ==0x42`, `9aca:9acf ==0x8b`, `a17e:a0d4 ==0x8b`).
* `[2DFC]` is toggled by Alt+C (key 2E00, `cba0:ccac`); switching it off paints `blit_rect(2, 0,0x5B, 1-[4088], 0,0x5B, 320,16)` (picture rows 91..106 onto the screen page).
* Value 0x8A and every value in 1..0x40, 0x45..0x83 hit the default branch (`3042`: stale `E44C/C07C/B770`, camera = aircraft position) - unreachable.
* Mode switch keys that are **not** view changes (cba0 `m , . /` unshifted = 326D 332C 342E 352F): set `[E556]` = 0xC000 / 0x4000 / 0x8000 / 0
  (sensor look azimuth), clear `[3D64]=[3D66]=-1`, `[9D0E]=0`, `[43B4]=1`, call `8113(0x13)` (display mode 0x13). R10/R7 own this.
* `[C06E]` (eject state, set to 1 by Shift+F10 = 5D00 at cba0:cfd2..d049 and counted up by 1e9e) forces mode 0x8C at the end of every `cba0`
  call that processed a key (cba0:d438; the early exit for key code 0 skips it - (V)). At eject `cba0` stores `DEA2 = C0A4`, `DEC6 = C0B2`, `DECA = 2DF4+8`
  (the camera position of 0x8C); `1e9e:23ea` then moves `DECA` (R2).
* Zoom keys (`8498` = zoom in, `84d6` = zoom out, R7): if `C07A & 0x80` they do `--[3D70]` / `++[3D70]` (camera distance exponent, clamped to 2..8 by `2f82`),
  otherwise they change the map zoom `[4080]` (2..9) and redraw the map with `8387`.

---------------------------------------------------------------------------------------------------------

## 2 `0000:2f82` view_frame(void)  (owner simflow, 2345 bytes)

Locals: `[bp-2]` = viewport height (initial 0x5B); `[bp-4]` = zoom `z`; `[bp-6]` = saved zoom; `[bp-0xA]` = horizontal radius; `[bp-0xC]` = saved `[294B]`;
`[bp-0xE]`,`[bp-0x14]` = target deltas; `[bp-8]` = distance; `[bp-0x16]` = old `[365E]`.

### 2.1 Camera state variables (all word unless noted)

| DS | name | meaning |
|---|---|---|
| E44C | `cam_yaw` | camera yaw (heading); *passed negated* to `03ce` |
| C07C | `cam_pitch` | camera pitch (+ up) |
| B770 | `cam_roll` | camera roll |
| B288:B28A | `cam_x` | camera X (long, units) |
| B752:B754 | `cam_y` | camera Y (long, units, **not inverted**) |
| B75C | `cam_z` | camera altitude (word; min 0x10; sign-extended when passed) |
| DE98:DE9A / DE9E:DEA0 / DEA6 | `tgt_x / tgt_y' / tgt_z` | look-at point (x long, y' long = 0x100000-Y, z word). Default = aircraft; set per view |
| DEA8 | `view_tgt_id` | selected object id for views 0x88/0x89/0x8B, see 2.7 |
| 3D70 | `cam_zoom` | camera distance exponent, clamped each frame to 2..8; distance `D = 0x18 << z` (96..6144 units) |
| B156 (18 bytes) | `view_matrix` | 3x3 word matrix of the camera orientation; consumed by the HUD/world->screen projectors (`b85d` via `ba13`, R-hud) |
| 2D94 (18 bytes) | `ac_matrix` | aircraft orientation matrix built by `2e43` (= `e26b(0x2D94, 2DEE, 2DF0, 2DF2)`) |
| B056 (16 x 16 B) | `pose_ring` | ring buffer of the aircraft pose, entry = `{long x; long y; word z; word yaw; word pitch; word roll}`; entry `[3D60]&15` is written every frame by `1e9e:2b69` (R2) |
| 3D60 (byte used) | `frame_cnt` | frame counter (`39c4:423d inc`) |
| 3660 | `frames_per_tick` | auto-tuned divisor (initial 4, `39c4:4387`); mode 0x84 reads the pose that many frames old |

### 2.2 Prologue (every frame)

```
h = 0x5B;                                        // viewport height, overwritten later
DE98:DE9A = B288:B28A = BA10:BA12;               // camera x = aircraft x
B752:B754 = C02A:C02C;                           // camera y = aircraft y
DE9E:DEA0 = 0x100000 - C02A:C02C;                // target y' (inverted)
B75C = 2DF4 + 0x18;   DEA6 = 2DF4;               // camera 24 units above the aircraft
z = 3D70 = clamp(3D70, 2, 8);                    // push 8,2,3D70 -> c94e
switch (C07A) { ... }                            // 2.3 - 2.8
```

### 2.3 Cockpit-class modes (0, 0x44, 0x41, 0x42, 0x43)  (V)

```
case 0, 0x44:  E44C = 2DEE;                 C07C = 2DF0;           B770 = 2DF2;
case 0x41:     E44C = 2DEE + 0x8000;        C07C = -2DF0;          B770 = -2DF2;
case 0x42:     E44C = 2DEE - 0x4000;        C07C = 2DF2;           B770 = -2DF0;
case 0x43:     E44C = 2DEE + 0x4000;        C07C = -2DF2;          B770 = 2DF0;
```
Camera position stays the default (aircraft x, y, alt+0x18). Then the common tail 2.9.

### 2.4 Mode 0x84 `VIEW_DELAYED`  (V)
```
i = (byte)(3D60 - byte 3660) & 15;  r = pose_ring[i];      // table B056 + 16*i
E44C = r.yaw; C07C = r.pitch; B770 = r.roll;
B288 = r.x; B752 = r.y; B75C = r.z;                         // note: no +0x18 here
```
Purpose (U): a camera `3660` frames behind the aircraft. The write index is `3D60&15` and 3D60 grows by one per frame, so with
`3660 = 4` the slots 4 frames old are used.

### 2.5 External orbit modes 0x85, 0x86, 0x87  (V)
```
D = 0x18 << z;
case 0x85:  E44C = 2DEE - 0x4000; C07C = 0; B770 = 0;
            B288 = BA10 + sin16(2DEE+0x4000, D);  B752 = C02A + cos16(2DEE+0x4000, D);   // B75C stays 2DF4+0x18
case 0x86:  E44C = 0x8000; C07C = 0; B770 = 0;  B752 = C02A + D;                        // B288 stays BA10 (dead mode)
case 0x87:  E44C = 2DEE;   C07C = 0; B770 = 0;
            B288 = BA10 + sin16(2DEE+0x8000, D);  B752 = C02A + cos16(2DEE+0x8000, D);
            B75C = 2DF4 + (4 << z);
```

### 2.6 Common tail 2.9 and 0x8C  (V)
```
case 0x8C:  C07C = 0xF400;  B770 = 0;                    // E44C is NOT written: it keeps its last value (frozen heading)
            B288 = (long)(int)DEA2 << 5;                 // sign extension (cdq) then shl 5
            B752 = (long)(0x8000 - (int)DEC6) << 5;
            B75C = DECA;
```
`DEA2/DEC6` = aircraft cell x / y' at eject, `DECA` = camera altitude (2DF4+8, then changed by the eject animation).

### 2.7 Target / weapon views 0x88, 0x89, 0x8B  (V)

Select the target record (`DEA8`):
```
if (C07A != 0x89) {
    if (E2FE == 1) { if (!(byte[3D64] & 0x80)) DEA8 = 3D64 + 0x80;  }     // air target index (A/A), marks kind B
    else           { if (!(byte[3D66] & 0x80)) DEA8 = 3D66 + 0x100; }     // ground target index, marks kind A
} else DEA8 = B04C;                                                      // last launched weapon slot, no flag, kind C
// if the selected value is "none" (bit 7 of the low byte set, e.g. -1) DEA8 keeps its OLD value (stale target, original quirk)
```
Load the look-at point from the record (`k = z` saved to `[bp-6]` first; the kind constants 3/5/7 stored to `[bp-4]` are dead stores, restored at 3362):

| kind | test | record | DE98:DE9A | DE9E:DEA0 | DEA6 |
|---|---|---|---|---|---|
| A ground site | `DEA9&1` (bit 8) | `B2A4 + 16*(DEA8&0x7F)` | `(ulong)word[+0] << 5` | `(ulong)word[+2] << 5` | 0x32 |
| B air object | `DEA8&0x80` | base `C140 + 0x24*(DEA8&0x7F)` | `long[+6]` | `long[+0xA]` | `word[+4]` |
| C weapon | neither | base `3C0C + 0x1C*DEA8` | `(ulong)word[+0] << 5` | `(ulong)word[+2] << 5` | `word[+4]` |
(weapon record: `+0 x cell, +2 y' cell, +4 alt, +8 yaw(3C14), +0xA pitch(3C16), +0xE active(3C1A)`. If it is inactive (`3C1A==0`) the code instead stores `3C14 = 2DEE`,
`3C16 = 2DF0` into the record and keeps the default look-at point = the aircraft.)

Then for **all three kinds** (before the camera is placed):
```
dx = (long)(DE98:DE9A >> 5) - C0A4;   dy = (DE9E:DEA0 >> 5) - C0B2;   dist = hyp(dx, dy);
E44C = atan2(dx, -dy);                 C07C = -atan2((DEA6 - 2DF4) >> 5, dist);     B770 = 0;
z = saved zoom;   D = 0x18 << z;   hD = cos16(C07C, D);
```
Then per mode:
```
if (kind A or B)                       // (DEA8 & 0x180) != 0
  if (C07A == 0x88) {                  // camera behind the aircraft, looking at the target
      B288 = BA10 + sin16(E44C+0x8000, hD);   B752 = C02A + cos16(E44C+0x8000, hD);
      B75C = (4<<z) + sin16(C07C, D) + 2DF4;  C07C = -C07C;
  } else {                              // 0x89 and 0x8B: camera on the far side of the target, looking back at the aircraft
      B288 = DE98 + sin16(E44C, hD);          B752 = (0x100000 - DE9E) + cos16(E44C, hD);
      B75C = (4<<z) - sin16(C07C, D) + DEA6;  E44C += 0x8000;           // pitch C07C unchanged
  }
else (kind C weapon chase, any of the three modes, normally 0x89)
  E44C = rec.yaw;  C07C = rec.pitch - 0x400;  R = 0x10 << z;  hR = cos16(C07C, R);
  B288 = DE98 - sin16(E44C, hR);   B752 = (0x100000 - DE9E) - cos16(E44C, hR);   B75C = DEA6 - sin16(C07C, R);
```
(In the kind C branch the values computed from the aircraft vector above are overwritten.) Then the common tail.

### 2.8 Common tail (every mode)  (V)

```
// 3042: keep pitch inside +-90 deg
if (abs(C07C) > 0x4000 || C07C == 0x8000) { C07C = 0x8000 - C07C; E44C += 0x8000; B770 = 0x8000 - B770; }
// view matrix
if (C07A == 0) memcpy(B156, 2D94, 18); else e26b(B156, E44C, C07C, B770);   // e26b also leaves sin/cos of yaw/pitch/roll in 4982..498C
if (B75C < 0x10) B75C = 0x10;
```

### 2.9 Cockpit class change  (V)
```
old = 365E;   365E = (C07A & 0xC0) == 0;           // 1 only for mode 0 (the HUD / panel cockpit); 0 for every other mode incl. 0x41-0x44
if (365E != old) {
    y2 = 365E ? 0x5A : 0xC7;  *(int*)(3FE2->+0x10) = y2;  *(int*)(3FFA->+0x10) = y2;   // ymax of the two text/HUD page contexts (page 0 and page 1: ymin 0, xmax 319)
    screen_off();                                    // slot 69
    if (365E) {                                      // entering the front cockpit
        cockpit_copy_panel(0, 0x5B, 320, 109);       // 8bd3: page2 rows 91..199 -> page 0 AND page 1
        3926();                                      // draws [2DFE] (R2)
        if (DEDA == 0) mfd_map_redraw(C0A4, C0B2);   // 8387
        if (DFDC == 0x13) { 3D64 = 3D66 = 0xFFFF; 8a5e(2, 0x9F); 9D0E = 0; }
    } else {                                         // leaving it
        blit_rect(page(ctx 4012)=2, 0,0x96, page(ctx 3FE2)=0, 0,0x96, 33, 50);
        blit_rect(page(ctx 3FE2)=0, 0,0x5B, page(ctx 4012)=2, 0,0x5B, 320, 109);   // save the live panel into the picture page
    }
    screen_on();                                     // slot 70
}
```
Text contexts: `[3FE2] -> 3FCC` (page 0), `[3FFA] -> 3FE4` (page 1), `[4012] -> 3FFC` (page 2, ymax 199); initial fields `{page, flags=2, fg=0x2F, bg=0, x, y, font=1 (font 0 for ctx 3FFC), ymin=0, ymax=0x5A, xmin=0, xmax=0x13F}`.

### 2.10 Cockpit picture change  (V)
```
if (C07A != 2E48) {                  // 2E48 = mode whose picture is currently loaded (initial 0: the pit picture is loaded by 00f0 at start-up)
    screen_off();
    switch (C07A) {
      case 0:    pic = "256pit.pic";                       // d7f4(name, 2) = open + PIC decode into page 2 + close
                 d7f4(pic, 2); cockpit_copy_panel(0,0x5B,320,109);
                 if (BA0E) sprite_scaled(ctx 4012, 0xA9, 0x6E, 0x4D, 0x3C, off 0, seg BA0E);   // slot 22: re-paste the stored ordnance MFD sprite (77x60) on page 2
                 3968();            // redraw lit lamps, mode displays
                 01bf();            // R1: pit recolour by [43B0]
                 1088:00A3();       // DAC 98h..9Fh <- 206A (green MFD ramp)
                 46C2 = 0;
      case 0x41: d7f4("256rear.pic", 2);  blit_rect(2, 0,0, 0, 0,0, 320,200);
      case 0x42: d7f4("256left.pic", 2);  blit_rect(2, 0,0, 0, 0,0, 320,200);
      case 0x43: d7f4("256right.pic", 2); blit_rect(2, 0,0, 0, 0,0, 320,200);
      default:   1088:00BD();                               // DAC 98h..9Fh <- 1212 (no picture)
    }
    screen_on();  2E48 = C07A;
}
```
For modes 0x41-0x43 the whole picture goes straight to the visible page 0 while the display is blanked; the 3D scene later covers the window area through the flip. Name strings at DS:2E04 `256pit.pic`, 2E0F `256left.pic`, 2E1B `256right.pic`, 2E28 `256rear.pic`. (The d7f4 call passes 2 args but `d7f4` forwards a third word read from the caller frame - harmless garbage - (U).)

### 2.11 Viewport height and the 3D call  (V)
```
C686 = 0 (byte);
if (365E)          h = 0x5B, or 0x6B if [2DFC] != 0;       // [2DFC] initial 1 -> 107 rows; written only by cba0 (view-size toggle)
else switch (C07A) { case 0x41: h = 0x9D; case 0x42,0x43: h = 0xA1; default: h = 0xC8; }
saved = 294B;  if (C07A != 0) 294B = 0;                    // zoom/narrow-FOV flag only active in the front cockpit
03ce(-E44C, C07C, B770, B288:B28A, B752:B754, (long)(int)B75C, 0, 0, 320, h);
433A = 0;                                                  // byte, FOV shift that bf71 may set (read by 1442)
if (2E00) --2E00; else 9F66 = C686;                         // 2E00 initial 2: the first two frames' result is ignored. 9F66 is read by 39c4:41c2 as a "player is inside scenery" collision flag
294B = saved;
```

### 2.12 Overlays on the freshly drawn back page (page `[4088]`)  (V)
```
if (365E) {                                      // front cockpit only
    blit_rect([4088], 0x4A,0x6E, 1-[4088], 0x4A,0x6E, 77, 60);      // left MFD window back page -> screen
    blit_rect([4088], 0xA9,0x6E, 1-[4088], 0xA9,0x6E, 77, 60);      // right MFD window
    if (2DFC) {                                  // viewport is 107 rows: re-paste the panel edge
        blit_rect(2, 0x20,0x5B, [4088], 0x20,0x5B, 256, 16);       // 256x16 strip of the pit picture
        cockpit_keyblit(0,    0x5B, 0,    0x4A, 32, 16, 0);         // 91a4: corner pieces from picture rows 74..89
        cockpit_keyblit(0x120,0x5B, 0x120,0x4A, 32, 16, 0);
    }
}
switch (C07A) {
  case 0x41: blit_rect(2, 0x5B,0, [4088], 0x5B,0, 138,157);        // opaque centre
             cockpit_keyblit(0,0, 0,0, 0x5B,0x9D, 0);  cockpit_keyblit(0xE5,0, 0xE5,0, 0x5B,0x9D, 0);   // 91 wide keyed side pieces (x 0..90, 229..319)
  case 0x42, 0x43: cockpit_keyblit(0,0, 0,0, 320,161, 0);          // keyed full frame
  case 0x44: poly_fill(ctx [344], 4 vertices at DS:2E38, colour 5)   // 1001:01EC; vertices (160,185) (230,199) (90,199) (160,185)
}
2E34 = h;                                        // rows flipped by the frame loop
```
`blit_rect(sp,sx,sy,dp,dx,dy,w,h)` is slot 42. In the front cockpit the 3D window therefore covers rows 0..h-1 of the screen only through the flip;
everything below it is static art + the explicit copies above + whatever `7898`/`17a3` draw.

### 2.13 Interface to the 3D renderer (what `2f82` leaves set up)

* Arguments of `03ce` (section 3) = camera pose + viewport rectangle.
* Globals read by the renderer or HUD projectors after `2f82`: `B156` (view matrix, 18 bytes), `B288/B752/B75C` (camera position), `E44C/C07C/B770` (angles), `C07A`, `365E`, `294B`
  (0 = normal FOV, !=0 only in the front cockpit: `03ce` passes FOV scale 0x80 instead of 0x100), `433A`, `98DE` (detail level), `C686` (collision flag out), `9F66`.
* `3D64/3D66` (selected targets), `B04C` (weapon slot), `E2FE` (1 = air-to-air mode), `3C0C` (weapon table, 28 B), `B2A4` (site table, 16 B, `C0B4` entries), `C140` (air object table, 36 B).

---------------------------------------------------------------------------------------------------------

## 3 `0000:03ce` r3d_scene_draw  (owner render3d, 237 bytes)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 1; where they differ, render3d.md is right.

`void r3d_scene_draw(int yaw_neg, int pitch, int roll, long x, long y, long z, int x0, int y0, int w, int h)` - stack layout (V):
`[bp+4] = -E44C, [bp+6] = C07C, [bp+8] = B770, [bp+0xA..0xC] = B288:B28A, [bp+0xE..0x10] = B752:B754, [bp+0x12..0x14] = z (sign-extended B75C),
[bp+0x16] = x0 (0), [bp+0x18] = y0 (0), [bp+0x1A] = w (0x140), [bp+0x1C] = h`.
1. Fill the 3D window context `[344]` (initial `0x32E`, `{page,flags=2,fg=0x2F,bg,x,y,font=1,ymin,ymax,xmin,xmax}`): `ymin=y0`, `ymax=y0+h-1`, `xmin=x0`, `xmax=x0+w-1`, `page = flip_flag()` (slot 45; 1 = draw on the back page), `fg [+4] = 0x88`.
2. `04bb([43B8])` (frame delay, section 4).
3. `[7D50] = (98DE==3) ? 0x7F60 : 0x8360` (renderer table pointer, selected by detail level; restored to `0x7D60` at the end).
4. `C030 = C044 = C042 = 9874 = 0`, then (the byte `DS:11E2` is never written and is 0 in the image) `C044 = C042 = 9874 = 1` - (V) effectively always 1.
5. `fov = 0x100`; if `365E && 294B != 0` then `fov = 0x80`.
6. `dae8(ctx=344, yaw_neg, pitch, roll, 0, 0, z_lo, 1 (draw sky/ground), fov)`: camera setup + `db71/dc64/dca9` and, because flag=1, waits for the frame-sync flag `[262A]` (set to 1 by the frame loop after the flip, cleared by the timer ISR on every full tick) and draws sky+ground (`1442`).
7. `04da(yaw_neg, pitch, x, y, z)` = terrain + scenery (R11a; roll is not passed), `9aca()` (R8), `db3e()` (scene end, R11a), `[7D50] = 0x7D60`, `a17e()` (R9), then `[346] ^= 1` (toggle, meaning unknown (U)). The order 04da -> 9aca -> db3e -> a17e is (V).

---------------------------------------------------------------------------------------------------------

## 4 `0000:04bb` frame_delay(int ticks)

`if (ticks > 0) { t = (byte)(ticks + [264A]); while ((byte)[264A] != t) ; }` (V) - busy wait for `ticks` increments of the byte counter `DS:264A`, which the timer
ISR path (`1e67`, R1) increments once per logical timer tick. `ticks = [43B8]` (the "FrameRate" menu value, written only by `d707`, R10; `39c4:4323` reads it to tune `[3660]`).
The delay starts at the call, it is not a limiter against the frame start. Also called with 0x3C at ejection (`1e9e:2b5d`), 2 at exit (`0233`).
Port: sleep/spin for `ticks` timer ticks (70 Hz class, see R1 timer spec).

---------------------------------------------------------------------------------------------------------

## 5 `0000:3968` cockpit_refresh_panels(void)  (owner simflow, 92 bytes)

```
for (i = 0; i < 0x25; i++) if (lamp[i].on) { d83e(ctx[3FE2], i, 1); if (i < 4) d83e(ctx[4012], i, 1); }   // re-light lit lamps on page 0 (and the first 4 also on the picture page)
80a0([DEDA]);   // MFD page redraw (R7)
8113([DFDC]);   // display-mode redraw (R7)
38f8();         // draws min([3634],9999)/10 via 38ab (R2)
3926();         // draws [2DFE] (R2), only when 365E
44a9();         // R4
```
Only caller: `2f82` (after the pit picture is reloaded).

Lamp table `DS:46D8`, 37 entries x 12 bytes, `{int x, y, w, h, scheme, on}` (the `on` word is 0 initially). `d83e(ctx, idx, on)` (R2) recolours the rectangle twice with
`rect_recolor` (slot 41) using the colour pairs in `DS:4672 [scheme*8 + on/off]` (scheme 4 recolours twice more) and stores `on`.
The rectangles (x,y,w,h,scheme): 0:(119,94,18,8,0) 1:(139,94,18,8,0) 2:(159,94,18,8,0) 3:(184,94,21,8,2) 4:(38,105,24,8,3) 5:(4,117,18,8,4)
6:(24,117,18,8,2) 7:(44,117,18,8,2) 8:(4,127,18,8,2) 9:(24,127,18,8,2) 10:(44,127,18,8,2) 11:(4,137,18,8,2) 12:(24,137,18,8,2) 13:(44,137,18,8,2)
14:(51,149,11,6,2) 15:(51,159,11,6,2) 16:(51,169,11,6,2) 17:(4,179,28,8,0) 18:(36,179,26,8,0) 19:(4,191,18,8,2) 20:(24,191,18,8,2) 21:(44,191,18,8,2)
22:(80,173,18,8,0) 23:(100,173,18,8,0) 24:(142,193,14,5,1) 25:(232,184,17,8,2) 26:(190,181,18,8,0) 27:(190,191,38,8,0) 28:(170,181,18,8,0) 29:(210,181,18,8,0)
30:(170,191,18,8,0) 31:(257,103,25,8,3) 32:(257,113,25,8,2) 33:(284,113,25,8,4) 34:(257,123,25,8,2) 35:(284,123,25,8,2) 36:(302,179,13,7,2).

---------------------------------------------------------------------------------------------------------

## 6 `0000:91a4` cockpit_keyblit(dx, dy, sx, sy, w, h, clip)  (owner simflow, 100 bytes)

Key-colour blit (colour 0 transparent) from the picture page to the back page. Builds the `BlitCtx` at `DS:4062` (video.md section 8):
`seg [4062] = [E558]` (page-2 segment = comm[20h]), `sx [4064] = arg3`, `sy [4066] = arg4`, `dpage [4068] = ([4088] != 0)`, `dx [406A] = arg1`, `dy [406C] = arg2`,
`w [406E] = arg5`, `h [4070] = arg6`, `[407B] = (byte)clip`. Then `clip ? blit_key_clip (slot 71, `[407A]=1`) : blit_key (slot 73, `[407A]=0x10`)` with `&4062`.
(`407A/407B` are not part of video.md's BlitCtx; (U) harmless - the driver reads only 0x10 bytes; the clip rectangle at 4072..4078 is never filled by this code.)
All callers pass `clip = 0`. Callers: `2f82` (corner pieces, frames), `17a3` (ADI bezel), `39c4` (R4).

---------------------------------------------------------------------------------------------------------

## 7 `0000:17a3` cockpit_draw_adi(void)  (owner simflow, 446 bytes)

Artificial horizon / attitude ball at screen (265,140), 32x28 pixels. Only called from `1bbc` when `C07A==0`, **after** `10a8:0000` and `d9a3([4088])`.
```
offset_set(pixel_offset(0x109, 0x8C));              // slots 62/26: draw offset = window origin (265,140)
clip right/bottom [2293]=0x20, [2295]=0x1C;
ctx = *(ctx**)0x0D1C  (-> 0x0D06 {page, 0, fg, bg ...});  ctx->page = [4088]; ctx->fg = 9;
1001:024C(ctx, 0,0, 0x20, 0x1C);                    // clear window to colour 9
// horizon polygon (4 vertices) from the cached sin/cos of the camera (set by e26b/e3e2): 4986 sin(pitch) 4988 cos(pitch) 498A sin(roll) 498C cos(roll)
//   98C8 = -[498A], 98E4 = [498C], 98F8 = [4986], 9F12 = [4988] copied by 1442
t = (98F8 << 4) / max(9F12, 0x20);                  // pitch offset
two vertex pairs computed with the 109d:0006 mul-trig helper from (98E4, 98C8) and angle 0xA7B8 (two end points of the horizon line, rotated by roll, shifted by t);
polygon filled with colour 1 via 1001:01EC(ctx, 4, pts, 1) (ground half)
[2293]=0x13F; [2295]=0xC7; offset_set(0);
cockpit_keyblit(0x109, 0x8A, 0x11C, 0x21, 0x21, 0x21, 0);       // 33x33 bezel from the picture (284,33)
blit_rect([4088], 0x109,0x8C, 1-[4088], 0x109,0x8C, 32, 28);      // back page -> screen
```
The exact vertex arithmetic is the same as in `1442` (see section 9); (U) for the pixel-exact reproduction - port it from `17a3:1817..18c8` if the horizon must match.

---------------------------------------------------------------------------------------------------------

## 8 `0000:8387` mfd_map_redraw(int cx, int cy)  (owner simflow, 273 bytes)

Redraws the left MFD map (window context `[402A] -> 0x4014 {page 1, flags 2, fg 0x2F, bg 0, font 1, ymin 110, ymax 169, xmin 74, xmax 150}`).
Args: `cx = C0A4`, `cy = C0B2` (the player cell, see header). Callers: `2f82` (entering cockpit), `8498/84d6` (map zoom keys), `72a4`, `39c4`.
```
DEDA = 0;  if (!365E) return;  C044 = C042 = 0;
8a4e(1, 6);                                                    // R7 (map colour/clear)
z = [4080];  s = 9 - z;                                        // map zoom 2..9 (initial 8); [4082] = second MFD page value (initial 1, 0..7)
lim = 0x4D << s;      ahead = 0x8000 >> z;
[4084] = clamp(cx + sin16(2DEE, ahead), lim, 0x7FFF - lim);    // map centre x in cells, 'ahead' of the aircraft
limy = ((0x3C << s) / 3) * 4;
[4086] = clamp(cy - cos16(2DEE, ahead), limy, 0x7FFF - limy);
119C(ctx 402A, [4084] / 2 (trunc), 0x4000 - [4086] / 2, 9 - z);   // terrain/object map render, section 9
for (i = 1; i < [C0B4]; i++) {                                 // site table B2A4, 16-byte records, [C0B4] = count (never written by VGAME code, loaded)
    rec = B2A4 + 16*i;   if (rec.word[+4] != 0 && (rec.word[+6] & 0x84) == 4)
        8548(rec.x [+0], rec.y [+2], (rec.word[+6] & 2) ? 0 : 8, 1);     // icon (R7)
}
973C();                                                        // R7
```
Map projection used by the icon routines (`8514`, `852B`, R7): `px = ((x - [4084]) >> (10 - z)) + 0x70`, `py = ((((y - [4086]) >> (10 - z)) * 3) >> 2) + 0x8B`
(centre of the 77x60 window = (112,139)). Site record (16 B, shared with R-ai): `+0 x cell, +2 y' cell, +4 active, +6 flags (bit1 colour, mask 0x84 == 4 -> drawn)`.

---------------------------------------------------------------------------------------------------------

## 9 Index-level entries (terrain / horizon / map renderer)  (owner render3d unless noted)

### 0000:119C map_render_terrain(ctx, px, py, shift)  (86 B)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 3; where they differ, render3d.md is right.
`[4894]=0; dae8(ctx, 0, 0xC000, 0, 0, 0, 0x2000, 1, 0x100)` (top-down camera, pitch -90 deg, altitude 0x2000, draw ground fill);
`offset_set(pixel_offset(ctx->xmin, ctx->ymin))`; `11F2(px, py, shift)`; `db3e()`; `[7D5E] = 0`. Only caller: `8387`.

### 0000:11F2 map_draw_tiles(px, py, shift)  (353 B)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 3; where they differ, render3d.md is right.
`[94E2] = px >> shift; [94E4] = py >> shift;` for tile level `L = 4 .. 0` (`[94E8]`): `[94E6] = (L <= 1) ? 0x80 : 0` (object class filter), `[E2FC] = word[0xCFC + 2L]` (`3,4,2,3,4`),
`[DECC] = (4 - [E2FC])*2 + shift`, tile size in pixels `[94DE] = 0x1000 >> [DECC]` (level skipped when <= 0x10), tiles per side `[94E0] = 4 << ((4 - [E2FC])*2)`;
`1353` gives the visible tile range; for every tile `865(level, tx, ty)` (R11a) returns the tile record (-1 none); its object list (entries of 7 bytes `{int dx, dy; byte? z; byte id}` at `[tile_list + ...]`, count at `word[0x61E + (level*32+tile)*2]`)
is walked and `13E3(id, x_screen, y_screen)` is called for entries with `[+4]==0`; `[7D5E] = [DECC] + 0xE` before each. Tables: count `word[0x61E + 2*(E2FC*32 + tile)]`, list pointer `word[0xE304 + 2*(E2FC*32 + tile)]` (U for the inner loop details). `0xD22` = 17-word table `0,2,3,4,6,8,11,15,20,27,36,48,64,87,119,164,0` (used by `1442`).
### 0000:1353 map_tile_bounds(&x0,&x1,&y0,&y1)  (89 B) / 0000:13AC map_pixel_to_tile(px, py, &tx, &ty)  (55 B)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 3; where they differ, render3d.md is right.
`13AC`: `tx = ([94E2] - word[0xD1E=160] + px) / [94DE]`, `ty = ((py - word[0xD20=100])*4/3 + [94E4]) / [94DE]` (window centre = (160,100)). `1353` calls it for the window
corners (`[2293]`,`[2295]` = clip size), clamps min to 0 and max to `[94E0]-1`.
### 0000:13E3 map_draw_object(id, x, y)  (94 B)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 3; where they differ, render3d.md is right.
If `[E2FC] >= 3` the class byte `ptr[0xA62][id&0xFF] & 0x80` must equal `[94E6]`; then `[8560] = [B760]`, `1433:03A9`, `E54B()`, and `11EB:00D3(id, x, 0, -y, 0x49A0, 0xC02E)` (3DX object draw, R11a; argument order from the pushes at `13E3:141F..1436`).
### 0000:1442 r3d_draw_sky_ground(ctx)  (865 B)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 6; where they differ, render3d.md is right.
Called by `dae8` after the frame sync. Copies the sin/cos cache (`4986 -> 98F8`, `4988 -> 9F12`, `498A -> 98C8 (negated)`, `498C -> 98E4`).
If `98DE < 3` just calls `1001:0006(ctx->fg)` (flat fill) and returns. Otherwise two sets of 16 horizon bands: set 1 (sky) with colour `0x80+i` (or `0x88` when `[4360] && [43B2]`), set 2 (ground) with colour `0x70+i` (or `0x7C` when the same flag), each a 4-vertex polygon via `1001:01EC(ctx, 4, pts, colour)`; the band edges are scaled
by `bp-2 = (4986<<8)/max(4988, 0x200)`, shifted left by `[433A]` and halved when `[294B]` is set, with the table `0xD22` / `[4980]` for the ground bands, trig through `109d:0006`. (U) which set is sky: 0x80..0x8F vs the 0x60..0x6F ramp of FORMATS.md.

### 0000:0D08 terr_find_scenery_at(long x, long y') -> hit* or 0  (553 B, owner damage/render3d)
> **S3.8:** owned by `flight.md` section 6.3 (verified there; where they differ, flight.md is right). The full pseudocode of 0D08 and 0F31 is there.
Args `x = cellx << 5`, `y' = (0x8000 - celly) << 5` (callers `39c4`, `4b57`, `72a4`, `baf8`). Scans levels 1 and 2 and the neighbouring tiles (`804/865` tile lookups) for the nearest scenery object entry (Manhattan metric `|dx|+|dy|` in `[BA16]`, initial 0x7FFF) whose class flags allow it; fills the hit record
`DS:BA14`: `+0 word id (BA14; replaced by the id stored in the destroyed-object table when a record with the same key exists), +2 distance (BA16), +4 long x (BA18), +8 long y (BA1C), +0xC ptr to the 7-byte tile entry (BA20), +0xE..0x11 four key bytes (BA22..BA25: level, list index, tile coords)`. Returns `0xBA14` (found) or 0.
### 0000:0F31 terr_set_scenery_state(hit*, byte newid)  (128 B)
Stores `newid` at `hit+0x12`, then looks the key (`hit+0xE..0x11`) up in the destroyed-object table `DS:B772` (5-byte records `{key[4], id}`, count `[9902]`, search from the newest by `0FB1`, which also leaves the found index in `[94DC]`). Found: if `newid == byte[DEA4]` or `newid == byte[C0AC]` the table record's id byte is replaced, else the tile entry byte `[+6]` is overwritten with `newid`. Not found: the 5 bytes at `hit+0xE` are appended (`memcpy(B772 + 5*[9902]++, hit+0xE, 5)`). In all cases `tile_entry[+6] |= 0x80` at the end (`DEA4`/`C0AC` = special ids, (U)).
### 0000:0FFB terr_probe_point(long x, long y', long z)  (417 B)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 3; where they differ, render3d.md is right.
Scenery collision probe at a world point: `[C686]=0; [BA16]=0x7FFF`; locates the tile (`804(4, ...)`), walks its objects whose class flag `[0x960+id] & 0x10` is set and whose `[+6] & 0x50 == 0`, keeps the nearest (Manhattan) in `[BA20]`; if found builds the relative position into `4964..4968`,
`[8560] = ...`, `1433:03A9`, `E54B()` and calls `11EB:01B8(model, x, y, z, 0x49A0)` which sets `[C686]` when the point is inside the object. Result in the byte `C686` (also written by dae8/671a/11eb).
### 0000:77D9 terr_probe_cell(int cellx, int celly, int alt)  (49 B)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 3; where they differ, render3d.md is right.
`0FFB((long)cellx << 5, (long)(0x8000 - celly) << 5, (long)alt)`. Callers: `5730`, `671a` (air object and weapon flight code, sets bit 5 of the object flags from `C686`).

---------------------------------------------------------------------------------------------------------

## 10 Original quirks / bugs

* `DEA8` keeps a stale target when `3D64/3D66` hold "none" (bit 7 set): the target view then follows the previously selected object.
* Mode 0x8C does not set `E44C`; the camera heading is whatever it was (frozen heading) - and the pitch normalisation may flip it.
* Mode 0x86 is unreachable (no key). `[C07A]` values 0x8A and the unlisted ones fall to the default branch.
* `d7f4` is passed a stray third argument by `2f82`.
* `DS:11E2` (read by `03ce`) is never written: `C042/C044/9874` are always 1.
* The panel is saved to the picture page only when leaving the front cockpit; it is lost again as soon as a `256*.pic` is loaded, which happens for every view that has a picture.
* `04bb` delays from the call, not from the previous frame.

## Open items

| id | item |
|---|---|
| O1 | purpose of the delayed pose view 0x84 and of `3660` as the delay (U) |
| O2 | exact horizon polygon arithmetic of `17a3`/`1442` (port from the disassembly) |
| O3 | which of the two `1442` band sets is the sky; palette indexes 0x80-0x8F vs FORMATS 0x60-0x6F |
| O4 | `[346]` toggle in `03ce`, `[407A]/[407B]` in `91a4`, `[2DFC]` meaning (viewport extension toggle key in `cba0`) |
| O5 | `d7f4` third argument, `1088:00BD` palette source `1212` |
| O6 | pixel-exact check of rear view: opaque 138x157 centre from `256rear.pic` over the 3D scene (V in code, visual check wanted) |

## DGROUP globals seen

| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| C07A | 2 | view_mode | view mode, section 1 | w: cba0; r: 1bbc 2f82 39c4 7898 8498 84d6 9aca a17e b85d bd8f dcc0 |
| E44C | 2 | cam_yaw | camera yaw (negated when passed to 03ce) | w/r: 2f82 |
| C07C | 2 | cam_pitch | camera pitch | 2f82 |
| B770 | 2 | cam_roll | camera roll | 2f82 |
| B288 | 4 | cam_x | camera x long | w 2f82; r b85d bd8f bf71 dcc0 |
| B752 | 4 | cam_y | camera y long | same |
| B75C | 2 | cam_z | camera z | same |
| B156 | 18 | view_matrix | camera orientation matrix | w 2f82 (memcpy / e26b); r ba13 |
| 2D94 | 18 | ac_matrix | aircraft matrix (2e43) | r 2f82 |
| DE98/DE9E/DEA6 | 4/4/2 | tgt_x/tgt_y'/tgt_z | look-at point | 2f82 |
| DEA8 | 2 | view_tgt_id | selected object (flags 0x80 air, 0x100 ground) | 2f82 |
| 3D70 | 2 | cam_zoom | camera distance exponent 2..8 | w 2f82 8498 84d6 |
| 3D60 | 1 | frame_cnt | frame counter, pose ring index | 39c4 4857; 2f82 1e9e |
| 3660 | 2 | frames_per_tick | frames per sim tick | 39c4 etc; 2f82 |
| B056 | 256 | pose_ring | 16 pose records | w 1e9e; r 2f82 |
| 365E | 2 | cockpit_active | 1 only in mode 0 | w 2f82 |
| 2E48 | 2 | view_pic_mode | mode whose picture is loaded | 2f82 |
| 2E34 | 2 | flip_rows | rows flipped per frame | w 2f82; r 1bbc |
| 2DFC | 2 | view_tall | 1 (initial) -> pit window 107 rows; toggled by Alt+C (2E00) in cba0:ccac, which on switching off copies picture rows 91..106 to the screen | w cba0; r 2f82 7898 |
| 2E00 | 2 | view_warmup | 2 frames before 9F66 is updated | 2f82 |
| 9F66 | 1 | scenery_hit | copy of C686 after the scene | w 2f82; r 39c4 |
| C686 | 1 | scenery_hit_raw | renderer collision flag | w 0ffb 2f82 671a dae8 11eb; r 5730 671a |
| 294B | 1 | fov_zoom | narrow FOV flag (front cockpit only) | w 1e9e 2f82 39c4 bf71 |
| 433A | 1 | fov_shift | FOV shift for 1442 | w 2f82 bf71 |
| 344 | 2 | view_win_ptr | -> 3D window context (0x32E) | 03ce 2f82 |
| 3FE2 / 3FFA / 4012 | 2 | ctx_page0 / ctx_page1 / ctx_page2 | near ptrs to text contexts 3FCC / 3FE4 / 3FFC | many |
| E558 | 2 | page2_seg | comm[20h] picture page segment | w 0010; r 91a4 |
| 4088 | 1 | back_page | slot 45 result at frame start | w 7898 87a9; r many |
| BA0E | 2 | ord_sprite_seg | sprite-pool segment, 77x60 ordnance MFD | w 02fe; r 0233 2f82 |
| 46D8 | 444 | lamp_tab | 37 x 12 B lamp rectangles | 3968 d83e |
| DEDA | 2 | mfd_page | MFD page selector (0 = map) | 80a0 82e9 8387; r 2f82 3968 |
| DFDC | 2 | disp_mode | display mode (0x13 look, 0x15, 0x18) | 784a 8113; r 2f82 3968 |
| 4080 / 4082 / 4084 / 4086 | 2 each | map_zoom / mfd2_val / map_cx / map_cy | map state | 8387 8498 84d6 |
| 402A | 2 | map_ctx_ptr | -> 0x4014 map window ctx | 8387 |
| 0D1C | 2 | adi_ctx_ptr | -> 0x0D06 ADI ctx | 17a3 |
| 4062 | 0x1A | keyblit_ctx | BlitCtx for 91a4 | 91a4 |
| C0A4 / C0B2 | 2 | pl_cellx / pl_celly' | player cell | w 39c4 |
| DEA2 / DEC6 / DECA | 2 | eject_cam_x / y' / z | eject camera | w cba0 1e9e |
| C06E | 2 | eject_state | 0 = none | w cba0 1e9e |
| 43B8 | 2 | frame_delay | ticks for 04bb | w d707 39c4 |
| 264A | 1 | tick_byte | timer tick byte | w 1e67 (ISR path) |
| 262A | 1 | frame_sync | 1 after flip, 0 at ISR tick | w 1bbc 1cb3; r dae8 |
| 3C0C | 28 x n | wpn_tab | weapon records | see section 2.7 |
| B2A4 | 16 x C0B4 | site_tab | ground site records | |
| C140 | 36 x n | air_tab | air object records | |
| BA14.. BA26 | 0x13 | scenery_hit_rec | hit record of 0d08 | 0d08 0f31 |
| B772 | 5 x [9902] | scen_state_tab | destroyed/changed scenery table | 0f31 0fb1 |
| 94xx 4964-4980 | - | map/probe scratch | | 11f2 0ffb |

## Structures seen

* **Text/window context** (video.md TextCtx, 24 bytes): `{int page, flags, fg, bg, x, y, font, ymin, ymax, xmin, xmax, self_ptr}` at `3FCC` (page 0), `3FE4` (page 1), `3FFC` (page 2), `032E` (3D window, page = flip_flag at run time), `4014` (map), `0D06` (ADI). Fields offsets +0 .. +0x14, +0x16 = self pointer.
* **Pose record** (16 B, `pose_ring`): `+0 long x, +4 long y, +8 word z, +A yaw, +C pitch, +E roll`.
* **Weapon record** (28 B, base `3C0C`): `+0 x cell, +2 y' cell, +4 alt, +8 yaw, +A pitch, +E active counter` (other fields unknown here).
* **Site record** (16 B, base `B2A4`, count `C0B4`): `+0 x cell, +2 y' cell, +4 active, +6 flags`.
* **Air object record** (36 B, base `C140`): `+0 x cell, +2 y' cell, +4 alt, +6 long x, +0xA long y', ... +0x12 flags`.
* **Lamp record** (12 B, `46D8`, 37 entries): `{x, y, w, h, scheme, on}`.
* **Hit record** `BA14` (section 9) and **destroyed-object entry** (5 B: 4 key bytes + id).
* **Tile object entry** (7 B): `+0 int dx, +2 int dy, +4 int/byte z-or-flag, +6 byte id` (bit 7 = modified).



---
# Part 4: the simulation step 39C4, mission state and end (R4)

*Reader pass R4, original title: S3.6 reader R4 - per-frame simulation step `39c4` and its helpers (VGAME.EXE)*

Range: `39c4` (2602 B) and 19 helpers `43ee .. 4f2a` (all segment 0000, near calls, DS = 1E23).
Marks: (V) verified in the disassembly, (U) unclear / needs a runtime or visual check.
Naming used in the prose: `fps` = DS:3660 (see section 1.2), `T[i]` = site record (section 6.1), `O[k]` = world object (6.2),
`comm` = far ptr DS:E548, `pilot` = far ptr DS:98F4 (= comm+1272h), `px,py` = DS:C0A4 / DS:C0B2 (player cell).

## 0. Overview

`39c4` (`sim_frame_step`) is called once per iteration of the frame loop `1bbc` (after `flip` and `1e9e`), i.e. exactly one
"simulation step" per displayed frame. It is NOT the flight model: the flight model, controls and the position integration
are in `1e9e` (it runs before 39c4 in the same iteration and has already updated the 32-bit world position `ba10/c02a`,
airspeed `b168`, AGL `2df4`, attitude `2dee/2df0/2df2`). `39c4` does, in order: grid position + world clamp, first-call mission
initialisation, moving-map markers, calls the world/weapon/threat updaters (`4f2a`, `5730`, `671a`), small effect timers,
the nearest-airfield logic (airfield objects, take-off line, landing/stop detection, cargo delivery), the ground/terrain failure
checks (these end the mission through `4926`), the 1 Hz mission clock, the frame-rate estimator and finally `43ee`
(keyboard command dispatch `cba0` + stealth signature). It contains **no pause test** and **no state switch**: pause
(`P`) and the quit requester are inside `1e9e` (blocking loops, `3939`), so `39c4` is simply not reached while they run (U: confirm
in R2's section). Mission end is requested only through `4926` which sets `[E552]=1`; the frame loop stops on `[E552]!=0`.

### 0.1 THE ORDERED CALL LIST of `39c4` (steady state, `[C0AE]==2`)

| # | addr | action | condition |
|---|---|---|---|
| 1 | 39ca | `px=(ba10:ba12+0x10)>>5` (32-bit arithmetic shift), `py=0x8000-((c02a:c02c+0x10)>>5)` -> DS:C0A4 / DS:C0B2 | always |
| 2 | 39f9 | **first call only**: init block, section 2.1, ends with `[C0AE]=2`, `38f8()`, `screen_on` (slot 70) | `[C0AE]==1` |
| 3 | 3b6a | clamp `px` to 0x100..0x7E00 and `py` to 0x200..0x7D00 (`c94e`); on change rewrite the world coordinate: `ba10:ba12 = px<<5`, `c02a:c02c = (0x8000-py)<<5` | always |
| 4 | 3bc3 | map markers: if `85ed(px,py,&sx,&sy)` the player is inside the map window -> `9770()` else `8387(px,py)` (re-centre map); for i=0,1 and `!(9b04 & (0x4000>>i))`: waypoint = `T[slot_i.target]`; if `85ed` hits: `91a4(sx-1,sy-1,0x104,0,4,4,0)`; if `[DFDC]==0x14` `94a5()` (route lines) | `[365E]!=0 && [DEDA]==0` |
| 5 | 3c66 | `4f2a()` threat scheduler + stealth bar | always |
| 6 | 3c69 | `5730()` world objects/sites update (owner R5) | always |
| 7 | 3c6c | `671a()` weapons/missiles/AI update (owner R6) | always |
| 8 | 3c6f | player marker on the map: `91a4(sx-1,sy-1,0x100|(((hdg+0x1000)>>11)&0x1C),4,4,4,0)` (hdg=`[2DEE]`; uses the `sx,sy` of step 4) | `[365E]!=0 && [DEDA]==0` |
| 9 | 3ca3 | `461c()` countermeasure timers, `465b()` gun, `479c()` smoke puffs, `483c()` falling object, `4e0f()` glide-slope, `38f8()` fuel digits, `4ebc()` lamp timers | always |
| 10 | 3cb8 | `[3D62]--` (if >0); `[DED8]--` and at 0 `snd_cmd(0x29)`; `[9D0A]--` and at 0 `snd_cmd(0x2A)` (set by `72a4`, owner R6) | always |
| 11 | 3ced | **airfield scan** (section 3.1) - nearest friendly/neutral airfield `[DFE0]`, `[98F2]`, airfield objects, traffic spawn, 128-frame counter rotation | `([3D60] & 7)==0` |
| 12 | 3f7c | fuel/landing speech `snd_cmd(0x2D)` once | always |
| 13 | 3fa7 | **airfield zone / take-off line / cargo / landing** (section 3.2) | always |
| 14 | 415a | **ground failure** `4926(1)` (section 3.3) | `[3D74]==0` |
| 15 | 41c2 | **terrain collision** `4926(2)` or lift +500 (section 3.4) | `[9F66]!=0 && !([C07A]&0x80)` |
| 16 | 420a | strip beacon phase `[DFDE]` | always |
| 17 | 423d | `[3D60]++`; once every `fps` steps: `[98E2]++`, speech 0x2C on the first second, `4d43()`, deadline event, event log every 32 s | `[3D60] % fps == 0` |
| 18 | 4301 | every `4*fps` steps: frame-rate estimator (section 1.2) and `[DED6]` recount | `++[3D78] >= 4*fps` |
| 19 | 43e7 | `43ee()` = `cba0([DE90])` key dispatcher, then stealth signature `[3D5C]` | always |

Whatever the branch, the function returns to `1bbc`, which tests `[E552]`.

## 1. Timing

Speed variables used below (from `1e9e`, V): `[B052]` thrust/speed accumulator (0..0xAFC8), `[B168] = [B052]/27` airspeed, `[2DF4]` AGL, `[2DF6]` MSL altitude, `[2DEE/2DF0/2DF2]` heading/pitch/roll as 16-bit angles (0x10000 = 360 deg).

### 1.1 Counters

* `[3D60]` (word) = step counter. Starts at `rand(0x1000) & 0x7FF8` (so a multiple of 8, 0..0xFF8; `4857`), incremented at step 17
  (`3d60++` happens AFTER the `&7` and `&0x7F` tests of the same step). Everyone uses its low bits for time slicing:
  `&1` gun spawn parity (465b), `&7` airfield scan, `&0xF` stopped-in-zone test, `&0x78>>3` smoke slot (479c), `&0x7F` traffic/counter
  rotation, `&0x80` which airfield object, `>>10` in the site scheduler (4f2a).
* `[98E2]` (word) = mission clock in seconds of game time: `++` when `[3D60] % fps == 0`. Reset to 0 by `4857`. It is NOT real
  time: it advances once per `fps` steps, `fps` being the current estimate of steps per real second (1.2).
  Seconds are used by: palette drift (`4d43`), event log time stamp (`499e`), deadline `[DE88]`.
* `[3660]` = `fps` (word, 4..15), steps per second estimate. All "per second" durations are written `n*fps` steps:
  message `3*fps`, flare/chaff indicator `3*fps`, decoy lifetime `(9-2*skill)*fps*8`, gun `0x70/fps`, ammo `-40/fps`,
  strip beacon `(b16e<<8)/fps`. The flight model (`1e9e`) divides its integrations by `fps` too.
  So the game is NOT frame-rate independent by any clock: it is "speed-corrected" by a measured integer fps. A port with a
  fixed 70 Hz display must pick fps once (e.g. the original's default 4..15 range) or emulate the estimator (1.2).
* `[3D7A]` (word, 1 or 2, static init 1) = "FrameRate" menu setting (key in `cba0`): 2 halves `fps`
  (`cba0`: fps/=2, then `d707`), `d7bb` goes back 2->1 with `fps*=2`. Lower bound of fps is `4-[3D7A]`.
* `[3D78]` = steps since the last estimate; `[2646]` = timer ticks since the last estimate (incremented by the INT 8
  "tick" at `1e67`: `[2646]++, [264A]++ (byte), [2648]++`; `[264A]` is what `04bb(n)` busy-waits on). The estimator assumes
  **60 ticks per second**; the real tick of the VGAME timer is one per video retrace x rate class / divisor (platform.md
  section 4, sound.md section 3: 70.09 Hz in mode 13h) (U: effect on the original's fps estimate, ~17 % high ticks => fps
  under-estimated; the port must define its tick rate and keep `960 = 240*4` consistent).

### 1.2 Frame-rate estimator (step 18, V)

```
if (4*fps > (uint16)(++[3D78])) goto step19;            /* jbe: runs when 4*fps <= [3D78] (unsigned) */
[E54E] = [2646] / fps;                                   /* unsigned div; stored, never read */
[2646] += (1 - 2*[43B8]) * fps * 2;                      /* [43B8]==0 in practice -> += 2*fps (fudge) */
if ([2646] < 4) [2646] = 4;                              /* unsigned compare */
a = clamp((fps*960) / ([2646]*[3D7A]), 1, 255);          /* 16-bit: imul ax,[3660],0x3C0 ; div (2646*3D7A) */
[2646] = 0; [3D78] = 0;
if (abs(4*fps - a) > 3) { fps = (a+2) >> 2; d707(); }   /* d707 clamps fps to [4-[3D7A] , 15] and recomputes derived values */
/* recount of active threats */
[DED6] = #{ i=3..[98FC]-1 : T[i].alert > 0xC0 && !(T[i].flags & 0x80) } + #{ k=0..[DED0]-1 : O[k].+34 > 0xC0 && (O[k].flags & 2) };
```
Note: the loop starts at `bp-0xe = 3` and the first `jmp` re-enters through `439a`; the second loop is `k=0..ded0-1`. `a` is "4 x measured fps"
if 60 ticks/s. `d707` (owner R2): `[43B8] = (fps>15) ? clamp((-120/fps + 9)>>1, 1, 4) : 0`; `fps = clamp(fps, 4-[3D7A], 15)`;
`[B16C] = clamp(2*fps, 3, 16)`; `[B286] = 250*fps`; `[9F64] = 200*fps`.

## 2. Mission lifecycle and exit state

States of `[C0AE]` (word): 0 = not started (set by `1e9e` at 1ea3..1ef0 when the first frame is prepared), 1 = initialise on the next
`39c4` call, 2 = running. `[E552]` (byte) = "leave frame loop" flag (init 0), written only by `4926`. `[008D]` (byte, DS image
value 0x81) = process exit code: `main` does `exit([8D])` after the loop; `1e9e` sets it to 0 (and `main` then switches to text
mode 3 first) when the user confirms the quit requester with `Y` and calls `4926(7)`. Every other end leaves 0x81 (START/END batch
chain decides what comes next; END reads the result words below).

### 2.1 First-call initialisation (`[C0AE]==1`, V; offsets 3a03..3b65)

1. `4b57()` name targets (below). 2. Zero: `[B75A] [B2A0]` (5730 state), byte `[294B]=0` (HUD-off toggle), `[2E7E] [2E7C]` (route
count/index), `[3652] [364E] [3632]` (3632 = damage bitmask, 364E ?), `[9B04]` (aircraft flags), `[43BC] [B76E] [3D62] [DE8C] [C600]
[C0B6] [DFE0] [E2FE]`. Set `-1`: `[98FE] [3D66] [3D64] [3D68] [3D72]`. `[3634]=0x2710` (fuel 10000), `[C5C8]=2` (damage
points, starts at 2 (V; U why)), `[2646]=0xC`, `[3660]=4` then `d707()`. `[3D6A]=[A068]=[4082]=[4080]=1` (3D6A = threat
blink/"first frame" counter, A068 = gear-lever, 4080 = map zoom, 4082 = ?), `[C072]=3` (gear position 0..3).
3. `[B16E] = DS:3D7C[2*pilot.theatre]` (word table, theatre 0..8: -1,1,-1,1,-1,1,1,-1,1; sign = runway direction / which side
of the airfield the start is on). 4. Start position from the departure site `s = T[[E2DC]]` (`[E2DC]` = mission slot 0 +4 = departure base):
   * if `s.flags_hi & 2` (no-hangar / strip start): `ba10:ba12 -= (long)([B16E]<<7)`; `[9B04] |= 8` (brakes); `d83e([3FE2], 0x23, 1)` (brake lamp);
   * else `c02a:c02c -= (long)[B16E] * 0x708` (starts 1800 world units behind the airfield centre; the position itself was set by `4ad0`: `ba10 = (s.x<<5)+2`, `c02a = (0x8000-s.y)<<5`).
5. `4857()` (random step counter, start clock, palette), `499e(8,0)` (event "start"), `82e9()` (map defaults: `[DEDA]=0; [DE94]=0x4A; [E302]=0x96;
   [DE96]=0x6E; [E444]=0xA9; [98CA]=0x4D; [98CC]=0x3C` then jumps into `8498`), `898b(3,10)` (lamp 3 value 10), `97a4()` (objective text screen, owner R7).
6. `[3656]=pilot+3A` (war level 0..2), `[3654]=pilot+3E` (enemy skill 0..3), `[98DE]=(uint8)comm[32h]` (range-scale shift, U: START field), `d76b()` (threat range table
   `[48A6+2i]=0x20<<(i+[98DE])`, i=0..5, `[48B2]=[48AE]+[48B0]`, `[48B4]=clamp(2*[48B0],0x1000,9999)`, `[48B6]=([98DE]+1)*0xD05`).
7. `comm+26h = 1` (mission running; see 2.3). If `pilot+3C & 2` (training mission type 2/3): `[9B05] |= 0x10` (training flag: no damage, cheats allowed).
8. Weapon bay copy for i=0..3: `[363E+4i] = comm.word[38h+2i]` (weapon id), `[3640+4i] = comm.word[40h+2i]` (quantity/state).
9. `[C0AE]=2`, `38f8()`, `screen_on` (slot 70 = the loader left the screen off). Execution then falls into the normal step at 3b6a.

### 2.2 Ending the mission - `4926(code)` and who calls it

`mission_end(code)`: see section 4 (`4926`). Exit codes `comm+28h` and their sources (V unless noted):

| code | meaning (best name) | raised by |
|---|---|---|
| 0 | normal end: landed and stopped inside a friendly airfield zone for the wait time; also the crash-sequence end | `39c4` 414e; `1e9e` 23ff (`c06e>0`, `(98E2&7)==0`, `[DECA]` ran out) |
| 1 | stranded: on the ground (AGL 0) outside any airfield, speed > 50, and (crash mode>0 or damage>4 or fuel==0) | `39c4` 41b6 (after sound, white flash, 120 ticks wait) |
| 2 | terrain collision with crash mode != 0 and MSL altitude != 0 | `39c4` 41f8 |
| 5 | hard/unsafe landing or gear failure at touch-down | `1e9e` 2b65 |
| 6 | fatal crash (impact severity) | `cba0` d01e |
| 7 | user abort: no result written, `[008D]=0` | `1e9e` 20bd |

`4926` ignores every code except 0 and 7 while `[C06E] != 0` (crash sequence running; `[C06E]` is 0 normally, set to 1 by `cba0` at ground
impact and incremented by `1e9e` each step; if the impact was survivable `cba0` sets `comm+26h=2`).

### 2.3 Result words written to comm (`4926`, V)

`comm+28h = code`; `comm+26h = 3` only when `code==0 && C06E==0` (landed safely); otherwise it keeps 1 (running/lost) or 2 (set by
`cba0` for survivable crash). `comm+74h = px`, `comm+76h = py` (final grid position), `comm+34h = [3632]` (damage bitmask),
`comm+36h = [C5C8]` (damage points). Also, at program exit `0233` calls `4c29` which copies the persistent mission state
(sites, objects, event log...) back into comm+7Ah.. (R1). END reads these (S3.9).

## 3. Mission flow logic inside `39c4`

### 3.1 Airfield scan (every 8th step: `([3D60]&7)==0`, V, 3ced..3f7b)

```
[3D72] = [DFE0];  [98F2] = 0x7FFF;
for i in 0 .. [C0B4]-1:                               /* all sites */
    if ((T[i].flags & 0x0201) && (T[i].flags_hi & 5)) {      /* airbase-of-interest or strip site, AND neutral(0x100) or friendly(0x400) */
        d = dist8(py - T[i].y, px - T[i].x);                 /* c987: max(|a|,|b|) + min(|a|,|b|)/2, cap 0x7FFF; units = grid cells */
        if (d < [98F2]) { [98F2] = d; [DFE0] = i; }
    }
if ([DFE0] != [3D72] && !(T[[DFE0]].flags_hi & 8)) {          /* nearest airfield changed (and not hidden) */
    s = T[[DFE0]];
    for k = 1..2:                                             /* O[ded0-1], O[ded0-2] : "airfield buildings" */
        o = &O[[DED0]-k];  o.flags &= ~2;                     /* inactive */
        if (!(s.flags_hi & 4)) o.type(+22) = 0;               /* hostile: none */
        else o.type = (k==1) ? 0x15 : 0x1C;                   /* friendly */
        if (s.flags_hi & 2) o.type = 8;                       /* strip site */
        if (s.flags_hi & 1) o.type = 0x18 + ((s.flags & 0x200) ? 0 : 3);   /* neutral: 0x18 (strip) or 0x1B */
        o.+0 = [DFE0];
    for k = 3..4:                                             /* O[ded0-3], O[ded0-4] : two parked aircraft/objects */
        o = &O[[DED0]-k];  o.flags |= 2;  o.x = s.x;  o.y = s.y;
        if (s.flags_hi & 2) { o.x += 5*[B16E]; o.y += ((k&1)*[B16E]) << 4; o.z(+6) = 0x80; }
        else                { o.x += 10;      o.y += (((uint8)[DFE0]+k)&3) << 4; o.z = 0; }
        o.X32 = (long)o.x<<5;  o.Y32 = (long)o.y<<5;         /* +8 / +12 */
        o.+16 = -rand(0x4000);                                 /* heading */
        o.type = ((s.flags & 0x400) ? 0 : 1) + 0xA;           /* 0xA friendly, 0xB otherwise */
        if (s.flags_hi & 1) o.type = 0xA;                      /* neutral: 0xA */
}
if (([3D60] & 0x7F) == 0) {                                   /* every 128 steps */
    if (!(T[[DFE0]].flags_hi & 8)) {                          /* not hidden */
        k = ([3D60] & 0x80) ? [DED0]-1 : [DED0]-2;
        o = &O[k];
        if (!(o.flags & 2)) {                                  /* traffic object not yet active */
            65d4(k, [DFE0]);                                   /* init airfield traffic aircraft (owner R5) */
            o.flags = 0x207;  o.z(+6) = 0x3E8;  o.+26 = 0xFA;
            o.Y32 += (long)[B16E] * 0x3000;
        }
    }
    [DE8C] = [C600];  [C600] = [C0B6];  [C0B6] = 0;           /* rotate the 128-step window counter (C0B6 incremented by 559a) */
}
```
`[DE8C]` is never read (V); `[C600]` is read by `4f2a` (>=0x80 widens the scan window) and `4f2a` only. (U) what the objects with types
0x15/0x1C/8/0x18/0x1B are (airfield dressing: hangar/tower/sheds from the 3DX list); `o.flags 0x207` = active + moving + ...
bit meaning is owner R5/R3.

### 3.2 Airfield zone, take-off line, cargo, landing (every step, V, 3f7c..415a)

```
/* speech: low fuel while on final to the landing base */
if ([3D94]==0 && [DFE0]==[E2EE] && [3634] < 0x1388 && [46C2]==2) { snd_cmd(0x2D); [3D94]++; }   /* E2EE = mission slot1 +4 = landing base site */

if ([98F2] >= 0x200 && [C0A2] != [2DF4]) { [3D74] = 0; goto after_zone; }       /* far from every airfield and not on its ground plane */
[C0A2] = 0;  [9900] = 0xA0;  [9B06] = 0x800;
s = T[[DFE0]];
if (s.flags_hi & 8) [9B06] = 0x400;                           /* hidden: shorter zone */
if (s.flags_hi & 2) {                                         /* strip site: ground plane at AGL 0x80, wider/shorter zone */
    [C0A2] = 0x80;  [9900] = 0x100;  [9B06] = 0x3C0;
    if ([2DF4]==0x80 && [B168] > 0x50 &&
        (uint16)((py - s.y) * [B16E]) in [0x10,0x14] &&
        abs((([B16E]-1)<<14) + [2DEE]) < 0x2000) {            /* just past the strip end, > 80 kt, aligned with the strip */
        [98E0] = 1;  d6bf(0xF, 2);                             /* "take-off done" flag (read by 1e9e) + sound 0xF */
    }
}
if (pilot.crash_mode == 1) { [9900] += 0x80; [9B06] += 0x100; }   /* Easy: zone half sizes +4 and +8 cells */
if (abs(px - s.x) > ([9900]>>5) || abs(py - s.y) > ([9B06]>>5)) { [C0A2] = 0; [3D74] = 0; goto after_zone; }  /* outside */
[3D74] = 1;                                                   /* inside the airfield zone */
if ([B168] > 1 || ([3D60] & 0xF)) goto after_zone;            /* only when stopped, every 16th step */
for i = 0..1:                                                 /* mission slots at DS:E2D8 + 0x12*i */
    if (slot_i.target(+2)==[DFE0] && slot_i.kind(+0)==4 && !([98E2] >= [DE88])) {   /* kind 4 = cargo delivery; deadline DE88 in seconds */
        7470(i);                                               /* mark objective i complete (9B05 bit 6/5) - owner R6 */
        msg_post("Cargo delivered");                           /* DS:2E4E */
        [3640 + 4*i] = 0;                                      /* bay i emptied */
    }
if ((s.flags_hi & 5) && [3D9A] != 0 && !(s.flags_hi & 8)) {   /* friendly or neutral, not hidden */
    old = [3D9A]++;
    if (old == 1) { msg_post("Safe Landing"); snd_cmd(0x2B); }  /* DS:2E5E */
    if (16/fps < [3D9A]) mission_end(0);                       /* integer division; ends the mission */
}
after_zone:
if ([3D74]==0) {                                               /* 4161: outside every zone */
    if ([2DF4] != 0) [3D9A] = 1;                               /* airborne: arm the landing counter (never reset) */
    else if (pilot.crash_mode != 0 || [C5C8] > 4 || [3634]==0) {   /* on the ground */
        if ([C06E]==0 && [B168] > 0x32) {
            d6bf(4,2); 8929(0); 893f(0,0,0x13F,0xC7); 04bb(0x78); mission_end(1);
        }
    }
}
```
Notes (V): `3d9a` increments only every 16th step while stopped in a friendly zone; since the wait is `16/fps` increments, the time to the end is
about `16*(16/fps)` steps. `8929(0)`+`893f(0,0,0x13F,0xC7)` fill both page records ([3FE2],[3FFA]) with colour 0 over the whole
320x200 area (black "end" flash) (U: colour arg order). In the stranded branch the very first test of 4161 is
"AGL==0" (`cmp [2DF4],0; jne 41bc`), the following conditions (4168..417f) are: `pilot+40h==0 && [C5C8]<=4 && [3634]!=0` => skip.

### 3.3 Terrain collision (step 15, V)

`[9F66]` (byte, set by `2f82` when the scene finds the camera under the terrain) `&& !([C07A] & 0x80)`:
```
if (pilot.crash_mode != 0 && [2DF6] != 0) { d6bf(4,2); screen_off(slot 69); 04bb(0x78); mission_end(2); }
else { [2DF6] += 0x1F4; [3652] = 0; }        /* "No Crashes": bounce the aircraft up by 500 units */
```
(2DF6 = altitude MSL, 2DF4 = AGL; `1e9e` limits MSL to 0xEA60/0xF230.)

### 3.4 Strip beacon phase (step 16, V)

`if (T[[DFE0]].flags_hi & 2 && [98F2] < 0x500) [DFDE] = ([DFDE] + ((int16)([B16E]<<8) / fps)) & 0x0FFF; else [DFDE] = 0;`
(`[DFDE]` is a 12-bit rotation phase of the strip lights, read by the scene code (U).)

### 3.5 Once-per-second block (step 17, V)

```
[98E2]++;  if ([98E2]==1) snd_cmd(0x2C);          /* speech clip 44 at the first second */
4d43();                                            /* palette drift */
for i = 0..1:                                      /* timed target event */
    slot = E2D8 + 0x12*i;
    if ((slot.flags_lo & 0x20) && [98E2] >= [DE88] && [C0A2] != [2DF4] && !(T[slot.target].flags & 0x80)) {
        t = T[slot.target];
        cell = 0d08((long)t.x<<5, (long)(0x8000 - t.y)<<5);        /* world cell record at the site (owner R3) */
        if (cell) 0f31(cell, (uint8)[DEA4] + 0x100);               /* put object (bank 1, type DEA4) there */
        T[slot.target].flags |= 0x80;                              /* now flagged: hidden from further spawn / suppressed in the scheduler */
    }
if (([98E2] & 0x1F)==0) 499e(9, 0);                /* position sample into the event log every 32 s */
```
(U) the exact object meaning (mobile target appears at its deadline).

## 4. Function entries

### 0000:39c4 `sim_frame_step` void sim_frame_step(void) (V)
See 0.1 and sections 2-3 for the complete algorithm. Locals: bp-2/bp-4 = map sx/sy of the player, bp-0xA/bp-0xC = sx/sy of a waypoint,
bp-0xE = loop index. Called by `1bbc` only. Callees (order of first use): 4b57, 4857, 499e, 82e9, 898b, 97a4, d76b, 38f8, c94e, 85ed, 9770,
8387, 91a4, 94a5, 4f2a, 5730, 671a, 461c, 465b, 479c, 483c, 4e0f, 4ebc, d83e, c987, 65d4, 7470, 8b93, d6bf, 4926, 8929, 893f, 04bb, 0d08, 0f31, 4d43, d707, 43ee,
cb62, f302, rtl shifts. Quirks: (1) the marker position `sx,sy` used at step 8 is stale/uninitialised if step 4 did not run or the player was off the
map. (2) `[3D72]` is overwritten with the old `[DFE0]` before the comparison, so the "-1" initial value is never used and the first scan does not
rebuild the airfield objects when the nearest site is index 0. (3) 4926(1) path uses `pilot+40h` (crash mode) as "any crash mode".

### 0000:43ee `sim_key_and_signature` void sim_key_and_signature(void) (V)
> **S3.8:** owned by `flight.md` section 3.1 (verified there; where they differ, flight.md is right).
1. `cba0([DE90])` - key command dispatcher (owner R10; `[DE90]` = pending key code (scan<<8|ascii) written by `1e9e`; 0 = none; not cleared here, cba0 clears the button words `43BE/43C0` on entry).
2. Radar signature `[3D5C]` (higher = more visible; drawn as bars by `4f2a`, used by `516d/62d0/5460`):
   `t = |(int8)([2DF2]>>8)|` (`cb1b` = sign-extend byte; bank angle in 1/256 turns, 0..0x80); `if (t>=0x40) t = 0x80-t`.
   `sk = [3654]` (enemy skill 0..3).
   * `pilot+42h != 0` (Lockheed model): `v = atan2((sk+1)<<7, 0x800)>>8` (c9d6 returns a 16-bit angle, 0x4000 = 90 deg; `>>8` = high byte);
     `[3D5C] = v + ([B052]>>10) + (t>>1)`, `+0x14` if `[9B04]==0` (gear down, bay closed, nothing toggled).
   * else (MicroProse model): `v = atan2(([2DF4]/3)*(sk+1), 0x800)>>8` (unsigned div and mul); `[3D5C] = v + ([B052]>>10) + (t>>1)`.
   Then in both branches: `+0x18` if `[9B04]&4` (bay doors open), `+0x10` if `[9B04]&0x10` (radar emitting). The `+0x14` (gear down and nothing else set) exists only in the `pilot+42h != 0` branch (V: 4457).
   The result is a 16-bit sum, no clamp (4f2a clamps for display, 516d adds it to the detection roll).
Callers: 39c4. Owner of the signature part: detect.

### 0000:44a9 `hud_draw_store_counts` void(void) (V)
`for a=1..3: 44c4(a)`. Callers: `0010` (init), `3968` (return to the front cockpit view).

### 0000:44c4 `hud_draw_store_count` void hud_draw_store_count(int a) (V)
If `[365E]!=0`: `n=[3636+2a]` (a=1 Flare, 2 Chaff, 3 Decoy; static initial words 12, 18, 3); `ltoa(n, DEB4, 10)`; text = `DEB4` if n>9 else `"0"+DEB4[0]` (buffer DS:3DA4 = "00", digit copied to `[3DA5]`); `d92d(0, [3D9C+2a], text)` draws two digit sprites in panel field `[3D9C+2a]` = (0,1,0,2)[a]:
a=1 -> field 1 (35,159), a=2 -> field 0 (35,149), a=3 -> field 2 (35,169). Digits are 4x7 sprites from page 2 (x offsets DS:4640[digit] = 67+5*digit, y=0x1F), step 5 px (d92d, owner R2). No bound check (n>99 prints 3 digits).

### 0000:4512 `wpn_release_countermeasure` void wpn_release_countermeasure(int a) (V)
> **S3.8:** owned by `flight.md` section 5.5 (verified there; where they differ, flight.md is right).
a: 1 flare, 2 chaff, 3 decoy (called by the three keys in `cba0` at cc40; nothing else).
```
old = [3636+2a]--;
if (old <= 0) { [3636+2a] = 0; msg_post("Stores exhausted"); return; }        /* DS:3DA7 */
slot = -1;
if (a == 3) { if (R[0].timer == 0 && !([3632] & 0x40)) slot = 0; }            /* decoy: slot 0 only, not if damage bit 6 */
else for (s = 1; s < 4; s++) if (R[s].timer == 0) slot = s;                   /* LAST free slot among 1..3 */
if (slot != -1) {
    R[slot].x = px; R[slot].y = py; R[slot].type = a;
    R[slot].timer = (9 - 2*[3654]) * fps;                                     /* skill 0..3 -> 9,7,5,3 s */
    switch (a) { case 1: s9876 = "Flare"; [3D90] = 3*fps; break;              /* lamp 0xF timer */
                 case 2: s9876 = "Chaff"; [3D8E] = 3*fps; break;              /* lamp 0xE timer */
                 case 3: s9876 = "Decoy"; R[slot].timer <<= 3; break; }
    strcat(s9876, " released");  msg_post(s9876);
}
44c4(a);  d6bf(0xF, 2);                    /* redraw counter, sound 0xF priority 2 (also when no slot was free: the item is lost) */
```
`R` = array of 4 records x 12 bytes at DS:39AA (6.3). The counter is decremented before the slot test (quirk: with all slots busy the store is used up without effect and the message is not shown).

### 0000:461c `wpn_cm_tick` void(void) (V)
> **S3.8:** owned by `flight.md` section 5.5 (verified there; where they differ, flight.md is right). **Correction:** only the first busy slot is decremented per call (the loop returns after it).
For s=0..3: if `R[s].timer!=0`: `timer--`; if `R[s].type==3`: `898b(7, timer>=1 ? 0x29 : 0)` (decoy lamp 7: on while alive).

### 0000:465b `wpn_gun_update` void(void) (V)  (owner weapons)
> **S3.8:** owned by `flight.md` section 5.6 (verified there; where they differ, flight.md is right).
Only if `[B16C]!=0`. (1) Advance bullets: for i=0..`[B16C]+3`: record at DS:E44E+12i `{x,y,z,dx,dy,dz}` words; if `x!=0`: `x+=dx; y+=dy; z+=dz`.
(2) Spawn: only when `([3D60]&1)` and `pilot+42h==0` (flight model 0): `i = ([3D60]>>1) % [B16C]`; if `cb72(0)` (fire button 0 = joystick button 0 or key flag `[43BE]`) and `[3650]>0` and `[C06E]==0`:
`[3650] = clamp([3650] + (-40)/fps, 0, 1000)` (ammo), `d6bf(6,2)`, `sp = 0x70/fps`, `dz = caee([2DF0],sp)<<5` (sin(pitch)*sp), `h = cb07([2DF0],sp)` (cos), `dx = caee([2DEE],h)`, `dy = -cb07([2DEE],h)`;
bullet i: `x = px+dx; y = py+dy; z = [2DF4]+dz-2; (dx,dy,dz stored as dx,dy,dz)`; `[98EA]=1` (firing flag read by `1e9e`). Else (no fire/ammo/crash): bullet i `x=0`, `[98EA]=0`.
`caee(a,m)` = sin-table(a) * m (fixed point via 109D:7C and 109D:6); `cb07` = same with a+0x4000. (V: the bullet slot written is i, but only slots <`[B16C]` get spawned.) Consumers: `5730` (hit tests), `a17e` (draw).

### 0000:479c `fx_smoke_update` void(void) (V)
If `[3D68] != -1` (index of a burning/destroyed site): for i=0..15 on the 16 records x 8 B at DS:39DA `{x,y,age,count}`: `age += 4; y += age>>9` (arithmetic). Every 8th step (`([3D60]&7)==0`): `j = ([3D60]&0x78)>>3`; record j = `{T[3D68].x, T[3D68].y, 0x40, 0}` ; `[3A5A]=j`; `record[j].count` = number of other records with `x != 0`. (U) consumer = `5730`/`9aca` smoke drawing. `[3D68]` is set by `71b4/72a4/a17e/bad1` and by `1e9e` (R2).

### 0000:483c `fx_fall_update` void(void) (V)
`if ([C602] > 0) { if ([B756] > -16) [B756] -= 12; [C602] += [B756]; }` - a falling body (altitude `C602`, vertical speed `B756`, set by `71b4`); drawn by `9aca` (R8).

### 0000:4857 `sim_reset_clock_palette` void(void) (V)
`cb56()` (`[9514]=bios_timer(); srand([9514])`), `784a()` (reset cockpit lamps 0x16, 0x1D, 0 on both pages, `[409C]=0`, `[DFDC]=0x18` instrument page), `[3D60] = (rand*0x1000>>15) & 0x7FF8`.
`h = ([E2E0] + [E2F2]) & 0xF` (low bytes of the two mission slots' template ids = start clock index, same formula START used), `[9870] = h<<8` (mission start time, read by the clock text 955c/95c4), `[98E2]=0`.
Palette: target = `[3658] ? DS:1C4A : DS:164A` (night/day 256-colour tables, 3 bytes/colour, 6-bit DAC); repeat `2*h` times `ea26(DS:104A, target, 0x100)`
(pal_step: moves each of the 3*count components of the working palette one unit toward the target, returns the number changed); then `pal_write_vsync(0, 0x100, DS:104A)` (14A2:000E), `lcall 1088:00A3` (R1).
Callers: 39c4 (init). Note `ea26`: `int pal_step(uint8 *cur, const uint8 *tgt, int ncolours)`.

### 0000:48dd `mission_cheat_rearm` void(void) (V)
Training cheat (cba0 key Alt-R, allowed only when `[9B05]&0x10`): `[3632]=0; [C5C8]=0; for i<4: [3640+4i] = ([363E+4i] < 0x10) ? 9 : 1; [3650]=1000; [3634]=5000` (fuel is only set to 5000 although the start value is 10000 (V)).

### 0000:4926 `mission_end` void mission_end(int code) (V)
```
if (code == 7) { [E552]=1; return; }
if ([C06E] != 0 && code != 0) return;
[E552] = 1;
comm[28h] = code;
if (code == 0 && [C06E] == 0) comm[26h] = 3;
comm[74h] = px; comm[76h] = py; comm[34h] = [3632]; comm[36h] = [C5C8];
499e(8, 0);
```
Callers: 39c4 (0,1,2), 1e9e (0,5,7), cba0 (6). The result of a code != 0 end leaves comm+26h at 1 (or 2 if cba0 set it).

### 0000:499e `mission_log_event` void mission_log_event(int code, int aux) (V)
Appends a 6-byte record to the debrief log at DS:BA2A (max 255 records; `[94EA]` = count): `{word [98E2]; byte px>>7; byte py>>7; byte code; byte aux}`; then `[94EA]++` and the next record's `code` byte is cleared (terminator; the table is saved to comm by `4c29`, 0x600 bytes). If count>=255 nothing is written.
Event codes (code, aux) pushed by the callers (V from the push sequences): (8,0) mission start (39c4) and end (4926); (9,0) 32-second position sample (39c4); (7,i) `516d` at 537e, aux = site index (site raised to "Radar ID"/locked, U); (6,[94F0]) `62d0` at 647f; (5,v) `671a` at 6e56;
(2,[bx]) at 73b6 (R6 area); (4,[364E]) at 75fd (R6); (0x4A,v) and (0x8A,v) `a17e` at aa8a/aa66 (aux = bp-0x34); `72a4` pushes computed codes (7256, 7389). Meanings belong to those readers; these records form the timeline END prints.

### 0000:49e7 `msg_site_label` void msg_site_label(int i) (V)
Builds in DS:9876 the label "Noun at Place" for site i: `noun = strtbl[T[i].+14 & 0x7F]`, `place = strtbl[T[i].+0]` (string pointer table DS:DEDC, 1 word per name index, built by `4ad0` from the pool at DS:DFEA); `strcpy(9876, noun); if (strlen(place)) { if (strlen(noun)) strcat(" at "); strcat(place); }` (note: uses `strlen(strtbl[T[i].+0])` first, then the noun test reads `T.+14` again);
if `strlen(9876) > 25` then `9876[0x18] = '.'`, `9876[0x19] = 0` (truncate to 24 chars + "."). Callers: 516d, 5730, 65d4, 72a4 (they then `msg_post(9876)`). The shorter place-only variant is `4a73` (R7, limit 18/19 chars).

### 0000:4b57 `mission_name_targets` void(void) (V)
For slots i=0,1 (DS:E2D8 + 0x12*i) with `slot.+7 != 0` (flags hi): `t = T[slot.target]`; `cell = 0d08(t.x<<5, (0x8000-t.y)<<5)` (world cell, owner R3); if found: `cls = *(uint8*)cell & 0x7F`;
`C604[n] = C604[cls]` (n = next free name index, local, starts at `[0610]` = 1, static); `strcpy(strtbl[n], strtbl[cls])`; `strtbl[n+1] = strtbl[n] + strlen + 1`; `0f31(cell, n + 0x100)` (re-class the cell object with its own class/name); then always `T[slot.target].+14 = n + 0x100` (word) and `n++`.
Finally `[3D76] = 0`. Purpose: each primary/secondary target object gets a private name entry so that "Noun at Place" labels work for it. Called once, from the init block. (U) exact object-class semantics; `0610` is not written back.

### 0000:4d43 `pal_time_of_day_step` void(void) (V)
Called once per second from step 17. `t = [98E2] & 0x3F`; returns unless `(t&0xF)==0 && [43AE]==0` (`43AE` = 1 after the manual day/night toggle key). Select the colour range:

| t | first colour | count | extra condition |
|---|---|---|---|
| 0x00 | 0x10 | 0x40 | only if `[98E2]&0x40` (every 128 s) |
| 0x10 | 0x60 | 0x40 | upload count 0x38 only if `[365E]!=0` |
| 0x20 | 0xA0 | 0x30 | |
| 0x30 | 0xD0 | 0x30 | |

`ea26(DS:104A+3*first, target+3*first, count)` with target = `[3658] ? 1C4A : 164A`, then `pal_write_vsync(first, count', DS:104A+3*first)`. Net: each range moves one DAC unit per 64 s (the sky/terrain ramps drift toward the day/night table).

### 0000:4e0f `hud_glideslope_update` void(void) (V)
Sets `[46C2]` (0 none, 1, 2 on slope, 3; consumers: 39c4 speech test `==2`, `d9a3`/`2f82` indicator). If `[9B04]&1` (gear up) -> `[46C2]=0`, return. Else:
```
dev = [2DF0] - [98C6];                         /* pitch minus [98C6]; 98C6 = cos(roll)*(lift-0x200), the vertical lift term computed in 1e9e at 276e (V) */
s   = T[[DFE0]];  strip = s.flags_hi & 2;
dy  = |s.y - py| - (strip ? 0x18 : 0x38);      /* cells to the touchdown point along y */
h   = [2DF4] - (strip ? 0x80 : 0);              /* AGL over the runway plane */
ang = c9d6(-(h >> 5), dy) - dev;                /* c9d6(y,x) = atan2 -> 16 bit angle, 0x4000 = 90 deg */
[46C2] = 2;
if (h == 0) return;
if (ang < 0) { if (-ang > 0x5B0) [46C2] = 1; } else if (ang > 0x5B0) [46C2] = 3;
```
Tolerance 0x5B0 = 1456 = 7.98 deg. (U: which of 1/3 is "too high" - 1 = path steeper than the ideal descent? read in d9a3.)

### 0000:4ebc `hud_lamp_timers` void(void) (V)
For (timer, lamp) in ([3D8E] chaff, 0xE), ([3D90] flare, 0xF), ([3D92], 0x18): `if (timer) { timer--; on=1 } else on=0; 4f05(lamp, on)`. `3D92` is set by `5588` (R5, U: warning lamp).

### 0000:4f05 `hud_lamp_set` void hud_lamp_set(int lamp, int on) (V)
`if ([365E] && DS:46E2[12*lamp] != on) d83e([3FE2], lamp, on)` - sets a cockpit lamp sprite on the page record 3FE2; the table at DS:46E2 has 12-byte records, word +0 = current state (written by d83e). Callers: 4ebc.

### 0000:4f2a `hud_threat_update` void(void) (V; owner detect for the scheduler, 8316/863c/9208/516d R5/R7)
> **S3.8:** owned by `flight.md` section 3.4 (verified there; where they differ, flight.md is right).
Part A (stealth bars, when redraw needed: `[3D6A]==0 || [DEC8]!=[3D5C]`, and `[365E]!=0`): `[DEC8]=[3D5C]`; `8316(clamp(100-[3D5C],0,99), 1, 0, 10)`; `8316(-clamp([3D5C],0,99), 0x2A, 0, 10)`
(green/dark part = stealth margin, colour 0x2A part = signature; 8316 = bar-segment draw, owner R5); then if `[3D6A]==0 && [DEDA]==0`: if `[DFDC]==0x14` `9208()`; `[C13A]=0; [C13C]=0x100` (threat pointer reset).
Part B (site scheduler): for i = 0 .. `[98FC]`-1 with `T[i].+6 != 0 && !(T[i].flags&0x80)`:
`c = 2*[3656] + (([C600] >= 0x80) ? 3 : 1)`; `ph = (-(([3D60]>>10) * i)) & 7`; if (`c >= ph` || `T[i].alert != 0` || `(T[i].flags_hi & 1)`): { `T[i].timer(+12)--`; if `<=0`: `timer = (int16)(fps<<8) / ((T[i].alert>>2) + 0x10) + i`;
if `timer == 4 && [3D6A] < 0`: `516d(i)`; `T[i].flags |= 2` } else `T[i].flags &= ~2`. Sites that fail the first test: `flags &= ~2`. (`T[i].alert` = site word +10 = detection level 0..255, raised by 516d, decays by 0x10, see 6.1.)
Part C (alert marker, when `[DEDA]==0 && [3D6A]>0 && [365E]!=0 && [C070]>1`): `str = ([98DE]!=0) ? ((clamp(fps-[3D6A],1,fps) * [C070]) / fps) << 6 : ([C070]<<6, [C070]=0)`;
threat = `[C07E]`: if >=0 `(x,y) = T[[C07E]].(x,y)` else `(x,y) = O[-[C07E]-1]`'s (x,y) (words +2,+4; `imul bx,[C07E],-0x24` indexing, U: -1 -> O[0]); `8548(x, y, ([3D6A]&1) ? 0xF : 0, 1)` then `863c(x, y, str, [9872], [E54C], [C13A], [C13C])` (threat arrow, owner R5).
Finally `[3D6A]--` (so it counts 1,0,-1,... and stays negative unless `516d/62d0` re-arm it with a positive blink counter).

## 5. The on-screen message queue (`msg_*`)

There is no queue: a message is a single slot (V).
* Buffer `DS:C578`: NUL-terminated string, up to 80 bytes (the neighbour `C5C8` starts after; callers never exceed ~45 chars; `strcpy` unchecked).
* Timer `DS:409E` (word) in steps.
* Post: `8B93 msg_post(const char *s)`: `strcpy(C578, s); [409E] = 3*[3660]` (3 s of game time). A new message overwrites the old and restarts the timer; nothing is queued. Helper string builders use DS:9876 (scratch, ~40 bytes) then `msg_post(9876)`.
* Draw/expire: in `7898` (HUD), every frame: `if ([409E] != 0 && (([C07A]==0 && [294B]==0) || [365A]!=0)) { x = 160 - 2*(strlen & ~1); 8ad3(C578, x, 20, [2CA0]); [409E]--; }`. So the text is centred on x=160 at y=20 (font 4 px/char), colour `[2CA0]` (byte, 15 = white by default, set by 10A8:09CE), and the timer only counts down while it is drawn (front view, HUD visible); `[365A]` is never written (static 0) (V).
* Users in this range: "Cargo delivered" (DS:2E4E), "Safe Landing" (DS:2E5E), "Stores exhausted" (DS:3DA7), "Flare/Chaff/Decoy released" (DS:3DB8/3DBE/3DC4 + 3DCA " released"). Strings not in this range: R5 ("<site> detected - Radar ID" via `49e7`, DS:3DE4), R6, R9.
* Sound cues that accompany the flow (snd_cmd numbers, sound.md): 0x13 gear (cba0), 0x29 / 0x2A (speech 41 / 42 from timers `DED8` / `9D0A`), 0x2B (speech 43, "Safe Landing"), 0x2C (speech 44, first second of the mission), 0x2D (speech 45, low fuel on final); effects through `d6bf(cmd, prio)` = `if (prio >= [43C2] && ([C06E]==0 || prio > 1)) snd_cmd(cmd); d6e5()`: 0xF (store released, take-off line), 4 (crash/terrain/stranded), 6 (gun), 0xC, 0x14.

## Open items
| # | item |
|---|---|
| 1 | `[3D6A]`: exact producer semantics in `516d/62d0` (blink counter value); `[C070]`, `[C07E]`, `[9872]`, `[E54C]`. |
| 2 | meaning of `9B04` bits 1 and 5 (0x02 lamp 0x21 toggle, 0x20 lamp 6), bit 3 = brakes (V by the lamp 0x23 use). |
| 3 | `[98DE]` = comm+32h low byte; documented as adapter in RE_GUIDE but used as a range/zoom shift by `d76b`. |
| 4 | Real-time base of the fps estimator: 60 ticks/s assumed vs 70.09 Hz tick. |
| 5 | Whether `1e9e` or something else resets `[98E0]`, `[3D9A]`; the stranded branch uses crash mode "!= 0" (also Easy). |
| 6 | Types 0x15/0x1C/8/0x18/0x1B/0xA/0xB of the airfield objects and flags 0x207. |
| 7 | `4e0f`: [98C6] is a lift term (V), so `dev` is not a plain angle; why it is subtracted from the slope angle is unclear (U). |
| 8 | Event code table (499e) complete only from the other readers' sections. |

## DGROUP globals seen
| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 008D | b | exit_code | process exit code, image value 0x81, 0 = abort to DOS | main, 1e9e |
| 0610 | w | name_next_free | first free name-table index (static 1) | 4b57 |
| 2646 | w | tick_acc | INT 8 tick accumulator for the fps estimate | ISR 1e67, 39c4, 1e9e, 3939 |
| 264A | b | tick_byte | tick counter used by 04bb waits | ISR, 04bb |
| 294B | b | hud_off | HUD hidden toggle (0 at init) | 39c4, 1e9e, 7898 ... |
| 2CA0 | b | text_colour | HUD/message text colour (15) | 7898, 10a8 |
| 2E4E | str | s_cargo_delivered | "Cargo delivered" | 39c4 |
| 2E5E | str | s_safe_landing | "Safe Landing" | 39c4 |
| 3632 | w | damage_mask | bit per damaged subsystem (bit 4 bays stuck, 3 ?, 6 decoy launcher) | 74f6, 39c4, 4926, 4512, cba0 |
| 3634 | w | fuel | 0x2710 at start, 5000 after cheat | 1e9e, 38f8, 39c4, 48dd |
| 3636 | w[4] | store_count | [1] flares 12, [2] chaff 18, [3] decoys 3 | 4512, 44c4 |
| 363E | 4 x (w id, w qty) | bay | weapon id / quantity per bay (copied from comm+38h, +40h) | 39c4, 48dd, cba0 |
| 3650 | w | gun_ammo | 0..1000 | 465b, 48dd, 7898 |
| 3654 | w | skill | pilot+3E enemy skill 0..3 | 39c4 |
| 3656 | w | war_level | pilot+3A | 39c4 |
| 3658 | w | night | comm+33h | 00f0 |
| 365E | w | cockpit_visible | 1 when the front cockpit panels are drawn | 2f82 |
| 3660 | w | fps | steps per second estimate | many |
| 3D5C | w | signature | radar signature (stealth) | 43ee, 4f2a, 516d |
| 3D60 | w | step_counter | see 1.1 | 39c4, 4857 |
| 3D62 | w | timer_3d62 | decremented per step (set by 559a/671a) | 39c4 |
| 3D68 | w | burning_site | site index or -1 | 479c |
| 3D6A | w | alert_blink | threat marker counter (1 at start) | 4f2a, 516d |
| 3D74 | w | in_airfield_zone | 1 inside the zone this step | 39c4, 1e9e |
| 3D78 | w | est_steps | steps since estimate | 39c4 |
| 3D7A | w | fps_mode | 1/2 frame-rate menu | cba0, d707 |
| 3D7C | w[9] | dir_by_theatre | runway direction sign | 39c4 |
| 3D8E/3D90/3D92 | w | lamp_timer_chaff/flare/x | indicator on-time | 4512, 4ebc |
| 3D94 | w | fuel_speech_done | | 39c4 |
| 3D9A | w | landing_count | 0 before first flight | 39c4 |
| 3D9C | w[4] | store_field | panel digit field per store type | 44c4 |
| 39AA | 4 x 12 B | cm_slot | released countermeasures | 4512, 461c, 5730, 671a |
| 39DA | 16 x 8 B | smoke | smoke puffs | 479c |
| 46C2 | w | glide_state | 0..3 | 4e0f, 39c4 |
| 46E2 | 12 B records | lamp_state | cockpit lamp states | d83e, 4f05 |
| 98DE | w | range_shift | comm+32h low byte | 39c4, d76b |
| 98E0 | w | takeoff_done | | 39c4, 1e9e |
| 98E2 | w | mission_seconds | | 39c4 and many |
| 98EA | w | gun_firing | | 465b, 1e9e |
| 98F2 | w | airfield_dist | distance in cells to nearest friendly/neutral airfield | 39c4 |
| 98FC | w | n_threat_sites | sites with threat level | static, 4f2a |
| 9870 | w | start_clock | start time << 8 | 4857, 955c |
| 9876 | 40 B | text_scratch | label buffer | 49e7, 4a73, 4512 |
| 9900 / 9B06 | w | zone_half_x / zone_half_y (x32 cells) | airfield zone | 39c4, 9aca |
| 9B04/9B05 | w | aircraft_flags | b0 gear up, b2 bay open, b3 brakes, b4 radar on, b14/b13 objective 0/1 done (9B05 bit 6/5), 9B05 bit 4 training | many |
| 9F66 | b | terrain_hit | camera under terrain | 2f82, 39c4 |
| B16C | w | gun_slots | clamp(2*fps,3,16) | d707, 465b |
| B16E | w | dir_sign | +-1 | 39c4 |
| BA2A | 6 x 255 | event_log | | 499e |
| C0A2 | w | ground_plane | 0 / 0x80 | 39c4 |
| C0A4 / C0B2 | w | px / py | player cell | 39c4 |
| C0AE | w | sim_state | 0/1/2 | 1e9e, 39c4 |
| C13A / C13C | w | threat_ptr | pointer state before O[0] | 4f2a, 516d |
| C5C8 | w | damage_points | | 39c4, 74f6, 4926 |
| C600 / C0B6 / DE8C | w | window counters | 128-step windows | 39c4, 559a |
| C602 / B756 | w | fall_alt / fall_vel | falling body | 483c, 71b4 |
| C578 | 80 B | msg_text | message | 8b93, 7898 |
| 409E | w | msg_timer | | 8b93, 7898 |
| DE88 | w | deadline_s | mission deadline seconds | 39c4 (read) |
| DED0 | w | n_objects | count of O | 4c41, 39c4 |
| DED6 | w | n_active_threats | | 39c4, 516d |
| DEDA | w | map_busy | | 8387, 82e9 |
| DFDE | w | beacon_phase | | 39c4 |
| DFE0 | w | near_airfield | index of nearest friendly/neutral airfield site | 39c4 |
| E2D8 | 2 x 18 B | mission_slot | | comm copy |
| E2DC / E2EE | w | dep_base / land_base | slot0+4 / slot1+4 | 39c4 |
| E552 | b | quit_flag | | 4926, 1bbc |
| E44E | 12 x (b16c+4) | bullets | | 465b |

## Structures seen
### 6.1 Site record `T[i]` - 16 B at DS:B2A2, count `[C0B4]`, loaded from comm by `4ad0/4c41` (same layout as START's site table, shell.md section 5)
`+0 w` place-name index (string table DS:DEDC), `+2 w` x (cell), `+4 w` y (cell, north = smaller after the `0x8000-` inversion), `+6 w` threat level (0 = none/gone),
`+8 w` flags: lo bits 0 (airbase of interest), 1 (scheduler active, set/cleared by 4f2a), 4 (0x10: "tracking", set by 516d), 5/6/7 (0x20 timed event, 0x40, 0x80 done/suppressed); hi byte: 0x100 neutral, 0x200 strip (no hangar) site, 0x400 friendly, 0x800 hidden.
`+10 w` detection (alert) level 0..255 (516d adds `((war+skill+1)*16) >> (radar_on?0:1)`, clamps 255; decays by 0x10; >0xC0 counts as "tracking", >=0xFA sets 0x1000 flag and a "Radar ID" message), `+12 w` scheduler countdown, `+14 w` (low 7 bits = noun name index, `n+0x100` after 4b57).
### 6.2 World object `O[k]` - 36 B (0x24) at DS:C13E, count `[DED0]` (the last four are the airfield objects)
`+0` site ref / kind, `+2` x, `+4` y, `+6` z, `+8` long X32, `+12` long Y32, `+16` heading, `+22` type, `+24` flags word (bit1 active, 0x207 = active|traffic), `+26` w, `+34` w (>0xC0 counted by DED6 when active).
### 6.3 Countermeasure slot - 12 B x 4 at DS:39AA
`+0 x(cell)`, `+2 y(cell)`, `+4 (unused)`, `+6 type (1 flare 2 chaff 3 decoy)`, `+8 remaining steps`; slot 0 = decoy only.
### 6.4 Event record - 6 B at DS:BA2A: `{w seconds; b x>>7; b y>>7; b code; b aux}`
### 6.5 Gun bullet - 12 B at DS:E44E: `{w x,y,z,dx,dy,dz}` (x==0: free)
### 6.6 Smoke puff - 8 B x 16 at DS:39DA: `{w x,y,age,count}`
### 6.7 Mission slot - 18 B x 2 at DS:E2D8 (as START E05E/E070): `+0 kind (4 = cargo delivery)`, `+2 target site`, `+4 base site (slot0 departure, slot1 landing)`, `+6 w flags (0x20 timed event)`, `+8 template idx`, `+10 grid ref`.



---
# Part 5: detection, threat events, moving map primitives (R5)

*Reader pass R5, original title: S3.6 spec, reader R5: radar / visual detection, SAM and fighter events, on-screen event messages, moving-map primitives*

Range: VGAME.EXE segment 0000 functions `516d 5460 5588 559a 5730 62d0 65d4 8316 8514 852b 8548 85ed 863c 86e5 871f 87a9 8888 8970 94a5 973c 9770`.
Notation: `DS:xxxx` = DGROUP offset (DGROUP 1E23). `(V)` verified in the disassembly, `(U)` unclear / needs a runtime or visual check.
Args are listed in C order (`[bp+4]` = first). `rand`, `clamp` etc. are named below in "Callees outside my range".

## 0. Overview

Everything here belongs to the *threat* part of the sim step `39c4` -> `4f2a` (sites) and `39c4` -> `5730` (aircraft):

```
sim step 39c4
  4f2a  (R4)  for every radar site i < [98FC]: scan countdown; when it hits 4 -> 516d(i)  [site radar + SAM launch]
  5730  ai_update_units()   one pass over all enemy aircraft units u < [DED0]
          5730 -> 62d0(u)  [aircraft radar / visual ID / air-to-air missile launch]  (staggered, once per 16*[3660] ticks per unit)
          5730 -> 65d4(u,site) [scramble: unit takes off from an airbase]
  both 516d and 62d0 -> 5460 (the one radar signal formula), 5588 (warning lamp + tone), 559a (alert the whole network)
  both 516d/62d0/5730/65d4 print one-line event messages with msg_post (8b93, R7) into DS:C578
  8316 hud_threat_bar: the green/red signal bars under the cockpit (x 86..136, y 187..197)
  8514 852b 8548 85ed 863c 86e5 94a5 973c 9770: world -> moving-map drawing (the 77x60 map window at x 74..150, y 110..169)
  871f 87a9 8888 8970: clipped line / pixel primitives on the double-buffered HUD pages, used by the map code
```

The detection model: the player has a running "own signature" `[3D5C]` (computed in `43ee`, R4; roughly 0..100+).
A radar adds `sig` (from 5460) to it; if `sig + [3D5C] > 100` the radar "sees" the player and its per-site/per-unit
*level* (0..255) grows; thresholds on the level give "Radar ID" (>= 0xFA), lock (> 0xC0) and missile launch.
The player-facing consequences are: (1) text messages, (2) snd_cmd 7 / 8, (3) the bars and the arcs on the moving map,
(4) SAM / air-to-air missiles in the slot table at DS:3C0C, (5) statistics counters, (6) the mission event log (499e).

Global scale factors that appear everywhere:
* `[3654]` = enemy skill 0..3 (pilot+3Eh: Green, Regular, Veteran, Elite). Loaded in 39c4 at 3af8 (V).
* `[3656]` = war level 0..2 (pilot+3Ah: Cold War, Limited War, Conventional War) loaded at 3af1 (V).
* `[3660]` = sim tick scale (1.. 16, init 4, doubled by `d7bb` when time acceleration is dropped; `d707` derives `[B16C]`, `[B286]`, `[9F64]` from it) - "ticks per sim step".
* `[3658]` = night flag (comm+33h). `[98DE]` = map/ring assist option (U; makes the radar arcs animate, see 4f2a). `[9B04]` = player switch bits: `0x10` = jammer/ECM switch (toggled at `cc1a`, lamp 5), `0x20` second jammer (lamp 6; both cleared when `[3632]&0x40`), `0x04`/`0x01`/`0x02`/`0x08` other cockpit switches (R2).
* world coordinates are 16-bit; `D` ("distance units") = `dist2d(dx,dy) >> 6`. `dist2d` = `c987`, `max + min/2` saturated to 0x7FFF (V).

---

## 1. Radar signal and site radar

### 0000:5460 `int det_radar_signal(int x, int y, int z, int rtype, int *bearing, int *dist)` (V)
> **S3.8:** owned by `flight.md` section 3.2 (verified there; where they differ, flight.md is right).
Args `[bp+4]=x [bp+6]=y [bp+8]=z [bp+0xA]=rtype [bp+0xC]=&bearing [bp+0xE]=&dist`. Returns `sig` (<= 0 means "not seen").
Radar type table `R = DS:2E80 + 14*rtype` (see section 6.1): `R.maxr = word +8`, `R.factor = word +10`, `R.flags = byte +12` (bit0 Doppler, bit1 "radar looks only along the nose" - used by 62d0).
1. `if (rtype == 0 || rtype == -1) return 0;`
2. `dx = [C0A4] - x; dy = [C0B2] - y;` (player X = `[C0A4]`, Y = `[C0B2]`).
3. `D = dist2d(dx, dy) >> 6` (unsigned shift). This is `*dist`.
4. Terrain/coverage factor `cell = byte DS:[B174 + (([C0B2] >> 11) << 4) + ([C0A4] >> 11)] & 0x0C` (0, 4, 8 or 12; DS:B174 is a 16x16 byte map of 2048-unit cells **at the player's position**, not the radar's; bits 2-3 = how well radars see this cell, 0 = nothing seen; bits 0-1 are used by other code, R9). (V for the formula, (U) for the semantic of the map)
5. `sig0 = ((long)(R.maxr - D) * (cell * (2*[3654] + R.factor + 1))) / R.maxr` (all signed 32-bit via aFlmul/aFldiv; negative if `D > maxr`; truncated to 16 bits when stored) (V). A site with `D >= maxr` therefore gives `sig <= 0`.
6. `bearing = atan2_16(dx, -dy)` (`c9d6`; 0x0000 when dx=0 and -dy>0, 0x4000 when dy=0 and dx>0). `*bearing = bearing`.
7. Aspect (radar cross section of the F-117): `a = abs(bearing - [2DEE]) >> 8` (byte 0..0x80; `[2DEE]` = player heading); `if (a > 0x40) a = 0x80 - a;` -> 0 = nose/tail on, 0x40 = beam on.
8. `f = a;` if `R.flags & 1` (Doppler): `f = ((0x60 - a) * ([B052] >> 5)) >> 9` (`[B052]` = player speed, written by 1e9e; unsigned mul, 16-bit).
9. `sig = (((f + 0x20) >> 1) * (sig0 >> 1)) >> 4` (signed 16-bit arithmetic; `imul` result's low word is used) (V).
10. Altitude cone: `if ((abs(z - [2DF4]) >> 10) > D (unsigned)) sig = 0;` (`[2DF4]` = player altitude).
11. `if (sig + [3D5C] > 100) det_time_accel_off();` = `d7bb`: if `[3D7A] == 2` { `[3D7A] = 1; [3660] <<= 1; d707();` } (cancel time acceleration when any radar sees the player).
12. `*bearing = bearing; *dist = D;`
13. `if (pilot->flight_model != 0) sig >>= 1;` (pilot far ptr `DS:98F4` + 0x42; 0 = "MicroProse F117A", 1 = "Lockheed F117A"; the Lockheed model is half as visible).
14. return `sig`.
Callers: 516d, 62d0. Callees: c987 (dist2d), c9d6 (atan2), f302 (abs), d7bb.

### 0000:516d `void det_site_radar_update(int site)` (V; owner detect)
> **S3.8:** owned by `flight.md` section 3.3 (verified there; where they differ, flight.md is right).
Called by 4f2a for site `i` when its scan countdown `rec.+0xC` reaches 4 and `[3D6A] < 0` (ring animation idle); 4f2a then sets `rec.flags |= 2`.
`rec = DS:B2A2 + 16*site` (site record, section 6.2). `t = rec.type (+6)`.
```
sig = det_radar_signal(rec.x, rec.y, 0, t, &bearing, &D);       // z = 0 for ground sites
[3D6C] = 0                                                      // bar colour, 0 = nothing
if (sig > 0) {
    shown = min(sig, 99)
    [3D6C] = 0x2A                                               // palette index
    if ([3D5C] + sig > 50)  [3D6C] = 0x29
    if ([3D5C] + sig > 100) [3D6C] = 0x11
    hud_threat_bar(shown, [3D6C], 1, 4)                          // 8316: bar rows y=188..191
    [C070] = ((long)([3D5C] + sig) * D) / 100                    // ring radius scale used by 4f2a/863c
    [3D6A] = [3660]                                              // ring animation timer
    [C07E] = site                                                // last contact index (>=0 site, <0 = -1-unit)
    [E54C] = R[t].flags & 1                                      // Doppler -> solid arc, Pulse -> dotted arc
    if (rec.level (+0xA) != 0) { [C13A] = hi_byte(bearing) - 0x20; [C13C] = hi_byte(bearing) + 0x20 }   // arc sector on the map (bytes: 1/256 turn)
    [9872] = [3D6C]                                              // ring colour
    if (!(rec.flags(+8) & 4)) {                                  // first contact with this site
        if ([DEDA]==0 && [365E]!=0) {                            // cockpit active
            9770();                                              // restore clean map background
            8548(rec.x, rec.y, 0, 1);                            // permanent 2x2 dot (colour 0) at the site on the moving map
            973c();                                              // save background again
            if ([DFDC] == 0x14) 94a5();                          // redraw the route polyline when that MFD page is up
        }
        rec.flags(+8) |= 4
        msg = site_name(site) + " detected"                      // 49e7 + strcat(DS:3DDA)   -> msg_post (8b93)
    }
}
if ([3D5C] + sig > 100) {                                        // radar sees the player
    5588();                                                      // warning lamp + snd 8
    rec.level (+0xA) += ((([3656] + [3654] + 1) << 4) >> ([9B04] & 0x10 ? 1 : 0))   // jammer halves the build-up
    rec.level = min(rec.level, 0xFF)
    if (!(rec.flags2(+9) & 1) && [39B2] == 0 && rec.level > 0x7F)  559a();   // non-neutral site, no decoy: alert network
    if ([3654] >= [B054]) {                                      // fratricide guard: skip firing if more fighters are engaged than the skill value
        if (rec.level > 0xC0 && t != 0x15 && [98F2] > 0x500) {   // 0x15 = OTH radar (no weapon); [98F2] = distance to nearest friendly/neutral airbase (39c4)
            ++[DED6];                                            // statistic: lock events
            if (!(rec.flags2(+9) & 0x10) && rec.level >= 0xFA) { // radar ID
                rec.flags2 |= 0x10; ++[DED2];
                msg = site_name(site) + " - Radar ID"            // strcat(DS:3DE4)
                d6bf(8, 1)                                       // snd_cmd 8, priority 1
                499e(7, site)                                    // event log code 7, arg = site
            } else {
                k = site & 7                                     // SAM slot
                m = &DS:3C0C[0x1C*k]                             // section 6.6
                if (m->timer(+0xE) == 0 && wpn_table[t].range (DS:3362+0x12*t+8) > D) {
                    m->x = rec.x + 8; m->y = rec.y; m->z = 0; m->speed(+6) = 1;
                    m->heading(+8) = bearing; m->pitch(+0xA) = 0x4000;       // launches straight up
                    m->timer(+0xE) = ((long)wpn_table[t].range * [3660] << 4) / m->speed;
                    m->type(+0x10) = t; m->owner(+0x16) = site;
                    msg = site_name(site) + " firing " + wpn_table[t].name   // strcat(DS:3DF0) , name at DS:3362+0x12*t
                    ++comm->stat_sam_fired                                   // far ptr DS:E548 + 0x2C (word)
                }
            }
        }
    }
    rec.flags(+8) |= 0x10                                        // "tracking" lamp bit
} else {
    rec.flags(+8) &= ~0x10
    rec.level -= 0x10;  if (rec.level < 0) rec.level = 0
}
```
Details / quirks:
* The flow is exactly as listed: the level-increment block and the `559a` call are inside the `sig + [3D5C] > 100` branch; the "fire" block is skipped (`jmp 5437`, only the flag 0x10 is set) when `[3654] < [B054]`, `rec.level <= 0xC0`, `t == 0x15`, or `[98F2] <= 0x500` (V). If a message for ID is produced in this scan, no missile is launched in the same scan (V).
* `site_name(i)` = `49e7(i)` builds the text into DS:9876 (R4): `name[rec.+0xE & 0x7F]` + (if `name[rec.+0]` is non-empty and the first is non-empty: `" at "`) + `name[rec.+0]`, strings from the near-pointer table DS:DEDC (`DEDC + 2*idx`); if longer than 25 chars it is cut: `[988E] = '.'; [988F] = 0` (i.e. 24 chars + '.').
* `msg_post` = `8b93(char *s)`: `strcpy(DS:C578, s); [409E] = 3 * [3660];` the text is drawn by 7898 (see section 5).
* `[C07E]`, `[C070]`, `[3D6A]`, `[E54C]`, `[C13A]`, `[C13C]`, `[9872]` are the input of the ring/arc animation in 4f2a (R4), which calls `8548` and `863c` (sections 3-4).
* The weapon table index equals the radar type for rows 1..15 (SA-2 .. Seacat), so a site's SAM is `wpn_table[t]` (V by names).

### 0000:5588 `void det_radar_warning(void)` (V; owner detect)
> **S3.8:** owned by `flight.md` section 3.5 (verified there; where they differ, flight.md is right).
`[3D92] = 2 * [3660]` (radar-warning lamp countdown, consumed in 4ebc, R4) and `d6bf(8, 1)`, i.e. `snd_cmd(8)` (track `v3 <- F4A`, see sound.md section 8) if sound priority allows. Called by 516d and 62d0 every scan in which the player is seen.

### 0000:559a `void det_alert_network(void)` (V; owner detect)
Makes every enemy agent aware of where the player is:
```
[3D62] = [B286]                          // alert timer = 250 * [3660]  (d707)
if ([39B2] != 0) { [B2A0] = [39AA]; [B75A] = [39AC] }          // a decoy/flare/chaff object [39AA,39AC] attracts the AI instead
else             { [B2A0] = [C0A4];      [B75A] = [C0B2] }
[B76E] = [2DF4]                          // last known altitude
[98F0] = [2DEE]                          // last known heading
[C0B6] = 0xFF
for (i = 0; i < [E540]; ++i)             // [E540] = number of site records
    if (site[i].type != 0)
        site[i].level (+0xA) = clamp(site[i].level, (([3656] + [3654] - 1) << 4), 0xFF)    // c94e(val, lo, hi): raises every radar to a floor of 16*(war+skill-1)
```
Callers: 516d (site level > 0x7F, non-neutral, no decoy), 62d0 (unit level > 0xC0). `[3D62]`/`[B2A0]`/`[B75A]`/`[B76E]` are what 5730 uses as "last known player position" (mode 1). (V)

---

## 2. Enemy aircraft (units): AI step, radar, scramble

### Unit record (36 bytes) at `DS:C13E + 0x24*u` ( = START's D3BE table, loaded from the WLD; `[DED0]` = unit count)
See section 6.3 for the table. Key facts used below: `+0` home/target site, `+2 +4 +6` = x y z (word), `+8/+0xA`, `+0xC/+0xE` = 32-bit x/y << 5 (sub-pixel position), `+0x10` heading, `+0x12` pitch, `+0x14` bank, `+0x16` aircraft type, `+0x18` flags (low byte `+0x18`, high byte `+0x19`), `+0x1A` speed, `+0x1C` endurance countdown, `+0x1E` weapon type (index into the DS:3362 table), `+0x22` radar level 0..255.

### 0000:5730 `void ai_update_units(void)` (V flow, owner ai; index-level)
> **S3.8:** owned by `flight.md` section 4.1 (verified there; where they differ, flight.md is right). **Superseded:** the full, differential-tested algorithm of 5730 is in flight.md sections 4.1-4.3; several statements below are wrong (flight.md section 8).
Single pass over all units, called once per sim step from 39c4. Housekeeping first:
* `if (!([3D60] & 1) && [3D68] == -1) DS:[39DA + 4*([3D60] & 0x1E)] = 0;` (frees one slot of the 16-entry smoke/crash trail list DS:39DA, entries 8 bytes: x, y, z, age).
* `E44E[12 * ([B16C] + (([3D60] & 0xC) >> 2))].x = 0;` frees one enemy tracer slot (section 6.7). `[3D60]` = frame counter.
* `[B054] = [94F2]; [94F2] = 0;` -> `[B054]` = number of enemy fighters that were in mode 1 *and* within 0x400 units of their target in the previous pass (used by 516d/62d0 to hold fire: SAMs/air-to-air fire only if `skill >= [B054]` resp. `2*skill >= [B054]`).
Per unit `u` (loop `u < [DED0]`):
1. `fl = unit.flags; if (!(fl & 1)) continue;` `[94F0] = unit.type` (aircraft index into DS:2FC2, kept in a global for the rest of the pass).
2. If `!(fl & 2)` (on the ground) or `unit.speed == 0` -> section "scramble" (a). Otherwise `mode = 0` and:
   * `fl & 4` (returning to base) -> section (b), mode 2.
   * `[3D62] == 0` (no alert) or (`fl & 0x140` and `[3D62] <= [9F64]`) -> mode 3 (patrol / go to own target).
   * else mode 1 (hunt the player): target = last known player `([B2A0], [B75A], [B76E])`, or the decoy `([39AA],[39AC])` with altitude `clamp([2DF4], 1000, 30000)` when `[39B2] != 0`.
3. Mode 3 (target selection, index-level (U)): wingmen (`flags2 & 1` and leader `[3D6E] != -1`) fly formation on the leader at offset `(u & 7) << 11` (rotated by the leader heading - 0x1800) and the leader's altitude; others, once every 64 ticks (`((u<<3) + [98E2]) & 0xBF == 0` and `!(fl & 0x40)`), pick among 8 random sites (`rand([C0B4])`) the one with the smallest bearing difference to the current line to the player that is not friendly (`site.flags2 & 4`) within `(4 - skill) << 12`; store as `unit.+0`; target altitude `clamp([2DF4]+1000, 5000, 20000)`; if the unit is far from the player (`dist2d & 0xC0.. > 0x5780`, not unit 0) its flags are masked `&= 0x1C1` and `+0x1C = 0` (despawn-ish).
4. Mode 2 (landing, `fl & 4`): target = home airbase `unit.+0` position; carrier sites (`site.flags2 & 2`) approach on a fixed glide using `0x8C` / `0x10C` deck height (selected by `abs(dx) + site.+8 & 0x200`), others `y = site.y + [B16E] * 0x280`, altitude `dist2d(...) + 0x7D0`.
5. Steering: yaw rate target from the bearing difference (`c9d6`), pitch target from the altitude difference (`c9d6(dz>>5, dist)`), plus a table-driven combat manoeuvre when `mode == 1` and `D < 0x400`: `++[94F2]` and the table `DS:3A8C` (word, indexed `((skill_or_u&3 clamp)*8 + aspect_idx)*8 + angle_idx`) gives a roll command; if `D < 0x200` and `!([3D60] & 3)` and `[3D62] > [9F64]` and the target is within 0x400 in yaw and pitch, a cannon round is spawned in the tracer table DS:E44E (section 6.7). Roll limit = `AC.+0x16 << 12`; max speed `AC.+0x12`; takeoff acceleration `0x28 / [3660]` per step while `flags2 & 4` (takeoff roll) until `unit.speed >= AC.+0x12` and `z > 0x12C` clears `flags2 & 4`.
6. Terrain check every 4th unit tick: `77d9(x,y,z)` (R3) -> `[C686]` -> `flags2 |= 0x20` (pull up: pitch target 0x3000) else clear.
7. Damaged / falling (`fl & 0x20`): bank rate -0x200, pitch rate down; every other tick a smoke-trail record is written to `DS:[39DA + 8*((3D60 & 0x1E) >> 1)] = (x, y, z, 0)`; older records age (`+6`).
   Combat manoeuvre table `DS:3A8C` (192 signed words = `[3][8][8]`, V): `row = clamp(skill + (u & 3), 0, 2)` (unit 0 always row 1); `b = ((bearing_to_player - (hdg + bank/4)) >> 13) & 7` (octant of the target relative to the nose; bank/4 only if `|bank| < 0x4000`); `a = (((hdg - (pl_hdg + pl_bank/2)) >> 13) - 4) & 7` (aspect octant of the player's heading relative to the unit); `cmd = tbl[(row*8 + a)*8 + b]`; roll target = `cmd << 12` (units of 1/16 turn bank, sign = direction), except `0x100` (pitch target 0x6000 and a bank command derived from the frame counter byte `[3D61]`: `((([3D61] & 0xF8) << 4) - 0x40) << 8` style, i.e. a loop / reversal) and `0x200` (pitch target 0xA000, mirrored bank command); the pitch target is otherwise the angle to the target altitude (`c9d6(dz>>5, dist)` clamped to +-0x2000 / 0xE000). Values: row0: a0 `3 3 2 1 0 -1 -2 -1`, a1 `3 2 2 2 1 1 1 2`, a2 `3 3 3 2 2 2 3 3`, a3 `3 3 3 3 3 -3 -3 -3`, a4 `3 3 3 -3 -3 -3 -3 -3`, a5 `-3 -3 -2 -2 -2 -3 -3 -3`, a6 `-2 -1 -1 -1 -2 -2 -2 -3`, a7 `1 2 1 0 -1 -2 -3 -3`; row1: a0 `2 3 2 2 1 1 1 -2`, a1 `4 4 3 2 1 4 4 4`, a2 `4 4 4 4 -2 4 4 4`, a3 `4 4 -4 -4 4 4 4 4`, a4 `-4 -4 -4 -4 4 4 -4 -4`, a5 `-4 -4 -4 2 -4 -4 -4 -4`, a6 `-4 -4 -4 -1 -3 -4 -4 -4`, a7 `2 -1 -1 -1 -2 -2 -3 -1`; row2: a0 `2 3 2 2 1 1 -1 -2`, a1 `4 4 3 2 1 4 -4 4`, a2 `4 4 4 4 256 4 256 256`, a3 `256 4 -4 -4 512 4 256 256`, a4 `256 256 -4 512 4 4 -4 256`, a5 `256 256 -4 4 -4 -4 -4 -4`, a6 `-4 4 256 -1 -3 -4 -4 -4`, a7 `2 1 -1 -1 -2 -2 -3 -1`.
8. Position integration: `heading += bank >> 3 / [3660]`, `bank += rate*4/[3660]`, `pitch += clamp(rate, -0x400, 0x400)*4/[3660]`; 32-bit positions `+8/+0xC` updated with `sin/cos(heading) * (speed*(0x8000 - pitch)>>15)` and `y -= ...`; `x = pos32 >> 5; y = pos32 >> 5` (writes unit `+2 +4`). `z += sin(pitch)*...`; `z > 0x7530 -> pitch = 0`.
9. Crash: `z < 0`: `flags &= (u == 0) ? 0 : 0x1C1` (unit 0 vanishes entirely), explosion at `[C0A0]/[C0AA]/[C0B0]` = (x, y, z), `[9D0C] = 0xFFF0`; if `[3D64] == u` then `[3D64] = 0xFFFF` (player target cleared).
10. **Landing messages** (mode 2 and `D < 0x10`, where `D = dist2d(target) >> 6`... computed as `[bp-0x18]` = dist2d in raw units): 
    `msg = site_name(unit.+0) + " - " + AC[unit.type].name + (flags2 & 2 ? " landing" : " on final")`. First time: sets `flags2 |= 2` and text `" on final"` (DS:3E0D); second time: sets `flags2 |= 0x10` and text `" landing"` (DS:3E04). The separator `" - "` is DS:3E00. The message is posted only when `u < [DED0] - 4` (the last four units are silent). (V)
11. `flags2 & 0x10` (landed/rolling out): pitch = bank = 0, heading = 0 or 0x8000 depending on `[B16E]` (runway direction), `z = 0x8C (+0x80 if nearest base `[DFE0]` has flags2 bit1 clear)`, speed -= `0x3C / [3660]`; when speed <= 0: `flags &= 0x1C1` (and, for u == 0 with `[E2D8] >= 5`, flags = 0). If `u < [DED0]-4` and speed < 100: `flags &= 0x1C1; flags |= 0x406`.
12. Endurance: `--unit.+0x1C; if == 0` -> `flags |= 4` (return to base) and choose as new home the nearest site `i` in `3 .. [E540]-1` with `(site.flags16 & 0x101) == 1` (airbase, not neutral; the friendly bit is not tested) by `dist2d` -> `unit.+0 = i` (sites 0..2 are never used).
13. Map blip: if `flags & 2`: `8548(unit.x, unit.y, colour, 1)` with colour `0x29` (0 for unit 0 when `[E2D8] >= 5` and `[3D60] & 1`, i.e. blinking) if `[98DE] != 0 || u == 0`.
14. Detection step: every `16*[3660]` ticks, staggered `((u & 8) >> 3) + 2*(u & 7)` (see 6153..617B): if `!(flags & 0x20)` -> `62d0(u)`.
(a) Scramble (ground units, `u != 0`, `u < [DED0]-4`): when `([98E2] >> ([3656]+4)) ^ u` has `(& 7) == 0`: `r = rand([E540])`; require (`[3D62] > [9F64]` or `unit.flags & 0x80`), site `r` with `(site.flags16 & 0x181) == 1` (airbase, not out of service, not neutral) and `site.+0xA == unit.type`; `D = dist2d(([B2A0],[B75A]) - site) >> 6`; if `AC.+0x14 > D` -> `65d4(u, r)`.
Callers: 39c4. Callees: 62d0, 65d4, 49e7, 8b93, 8548, 77d9, c9d6, c987, c94e, caee/cb07, cb62, f302.

### 0000:62d0 `void ai_unit_radar(int u)` (V; owner ai/detect)
> **S3.8:** owned by `flight.md` section 4.4 (verified there; where they differ, flight.md is right).
Airborne enemy aircraft radar, visual ID and missile launch. Inputs: `unit = DS:C13E + 0x24*u`, `[94F0]` = unit type (set by 5730), `AC = DS:2FC2 + 32*[94F0]` (aircraft table), radar type `AC.+0x18` (0xFFFF = no radar).
```
sig = det_radar_signal(unit.x, unit.y, unit.z, AC.radar, &bearing, &D)
[3D6C] = 0x2A
if (sig > 0) {
    [C070] = ((long)(sig + [3D5C]) * D) / 100
    if (!(R[AC.radar].flags & 2)) {                            // forward looking radar
        [C13A] = hi(unit.heading) - 0x30;  [C13C] = hi(unit.heading) + 0x30       // nose cone shown on the map
        if (hi(abs(bearing - unit.heading)) > 0x30) sig = 0                         // target outside +-0x30/256 turn of the nose
    }
    if (unit.flags(+0x18) & 8) { [C13A] = hi(bearing) - 0x20; [C13C] = hi(bearing) + 0x20 }   // lock tone already given: arc centred on the bearing, +-0x20
    [3D6C] = (sig + [3D5C] > 100) ? 0x0F : 0x0B
    hud_threat_bar(min(sig,99), [3D6C], 6, 9)                    // second bar, rows y = 193..196
    [3D6A] = [3660];  [C07E] = -1 - u;  [9872] = [3D6C];  [E54C] = R.flags & 1
}
if (sig + [3D5C] > 100) {
    5588();                                                     // lamp + snd 8
    unit.level(+0x22) += (([3656] + [3654] + 2) << 4) >> ([9B04] & 0x10 ? 1 : 0)
    if (unit.level > 0xC0) {
        ++[DED6]; unit.flags2 |= 0x40; 559a();
        if (!(unit.flags2 & 8) && (8 >> [3658]) > D) {          // visual identification: 8 units by day, 4 at night
            unit.flags2 |= 8; ++[DECE];
            msg = "Visual ID - " + AC.name                       // strcpy(DS:3E17); strcat(DS:2FC2 + 32*[94F0]); posted unconditionally
            d6bf(7, 1)                                          // snd_cmd 7
            499e(6, [94F0])                                      // event log code 6, arg = aircraft type
        }
        // missile launch test (every failing test jumps to the tone tail "T" below)
        k = u & 7;  m = &DS:3C0C[0x1C*k];  w = unit.weapon(+0x1E)
        if (2*[3654] >= [B054] && m->timer == 0 && abs(bearing - unit.heading) < 0x1800 &&
            wpn_table[w].range > D && (4 - [3654])*2 < D && w != 0) {
            m->x = unit.x; m->y = unit.y; m->z = unit.z - 0x19;
            m->speed = wpn_table[w].speed(+10) >> 6;
            m->heading = unit.heading; m->pitch = unit.pitch; m->bank(+0xC) = unit.bank;
            m->timer = ((long)wpn_table[w].range * [3660] << 5) / m->speed;
            m->type = w; m->owner(+0x16) = -u;
            msg = wpn_table[w].name + " fired by " + AC.name         // strcpy(name), strcat(DS:3E24), strcat(DS:2FC2+32*type); posted unconditionally
            ++comm->stat_missiles_at_player                          // DS:E548 far ptr + 0x2C
        }
    }
    // tail "T" (reached from every path inside the "level > 0xC0" test and also when level <= 0xC0)
    if (!(unit.flags(+0x18) & 8)) d6bf(7, 1);                  // snd_cmd 7 the first time this unit locks on
    unit.flags(+0x18) |= 8;
} else {
    unit.flags &= ~8;  unit.level -= 0x20
}
unit.level = clamp(unit.level, 0, 0xFF)
```
(V) for every branch. The tone/`flags |= 8` tail runs whenever `sig + [3D5C] > 100` (also when the level is still <= 0xC0). Note the missile start z is `unit.z - 0x19`; the weapon speed for the slot is `wpn.+10 >> 6` (`sar`); the SAM version uses speed 1.
Callers: 5730. Callees: 5460, 8316, 5588, 559a, 49e7 not used here (no site), 8b93, d6bf, 499e, c94e.

### 0000:65d4 `void ai_unit_takeoff(int u, int site)` (V; owner ai)
> **S3.8:** owned by `flight.md` section 4.5 (verified there; where they differ, flight.md is right).
Places unit `u` on airbase `site` and starts the take-off roll.
```
T = unit.type (+0x16)
unit.heading(+0x10) = ([B16E] == 1) ? 0 : 0x8000               // [B16E] = runway direction flag (1 or -1) (U)
rec = DS:B2A2 + 16*site
if (rec.flags2(+9) & 2) {                                       // carrier / no-hangar site
    unit.x = rec.x + 3*[B16E];  unit.y = -(12*[B16E] - rec.y);  unit.z = 0x8C;  unit.speed(+0x1A) = 0x64;  unit.heading -= 0x400
} else {
    unit.x = rec.x;  unit.y = rec.y + 30*[B16E];  unit.z = 0x0C;  unit.speed = 0x0A
}
unit.pos32x(+8/+0xA) = x << 5;  unit.pos32y(+0xC/+0xE) = y << 5       // zero-extended (sub-pixel position)
unit.pitch(+0x12) = unit.bank(+0x14) = 0
unit.flags |= 0x403;  unit.+0 = site
unit.endurance(+0x1C) = ((long)AC[T].+0x14 * [3660] << 11) / AC[T].+0x12      // range / max speed
if ([3D6E] == -1) unit.flags2 &= ~1                              // no formation leader -> not a wingman
msg = site_name(site) + " - " + AC[T].name + " taking off"      // DS:3E2F " - ", DS:3E33 " taking off"
if (u < [DED0] - 4) msg_post(msg)
```
(V). Callers: 5730 (scramble), 39c4. The `AC[T].+0x14 = 560` for MiG-23 etc.

---

## 3. HUD bars and message text

### 0000:8316 `void hud_threat_bar(int val, int color, int y0, int y1)` (V; owner simflow/hud)
`if ([365E] == 0) return;` `ctx = [3FE2]` (near ptr to a TextCtx, see 6.8); `ctx->fg(+4) = color;` 
`val > 0`: `1001:024C(ctx, 0x56, 0xBB + y0, 0x56 + val/2, 0xBB + y1)` (rectangle fill, (U) 1001:024C is R11's). `val < 0`: `1001:024C(ctx, 0x88 + val/2, 0xBB + y0, 0x88, 0xBB + y1)` (fills leftwards from x=0x88). `val == 0`: nothing. (`/2` is `(val - (val>>15)) >> 1`, i.e. trunc towards 0.)
Users: 516d (bar rows 1..4, colour 0x2A / 0x29 / 0x11 by level), 62d0 (rows 6..9, colour 0x0B / 0x0F), 4f2a (the player-signature meter: `8316(clamp(100 - [3D5C],0,99), 1, 0, 10)` then `8316(-clamp([3D5C],0,99), 0x2A, 0, 10)`; i.e. an empty part at x 86..136 and a filled part growing leftwards from x = 136).

### Event messages (all built in DS:9876 by `49e7` + strcat, then `msg_post`)
Strings (DS offsets verified from `dgroup_strings.txt`):

| DS | text | used by |
|---|---|---|
| 3DD4 | `" at "` | 49e7 (site name join) |
| 3DDA | `" detected"` | 516d (first sight of a site) |
| 3DE4 | `" - Radar ID"` | 516d (level >= 0xFA) |
| 3DF0 | `" firing "` | 516d (SAM launch) |
| 3DF9 | `" fired"` | not in my range (other readers) |
| 3E00 | `" - "` | 5730 (landing / on final) |
| 3E04 | `" landing"` | 5730 |
| 3E0D | `" on final"` | 5730 |
| 3E17 | `"Visual ID - "` | 62d0 |
| 3E24 | `" fired by "` | 62d0 |
| 3E2F | `" - "` | 65d4 |
| 3E33 | `" taking off"` | 65d4 |

Message formats (X = site_name(i) = text from 49e7, AC = aircraft name = the first (NUL terminated) string of the 32-byte record at `DS:2FC2 + 32*type`, e.g. `MIG-29`; the NATO nickname `" Fulcrum"` starts at record +7 and is NOT used by these messages; W = weapon name = `DS:3362 + 0x12*idx`, e.g. `SA-10`, `AA-7`):

| event | text | extra |
|---|---|---|
| radar first sees player | `X detected` | map dot, `[C07E]`... |
| radar ID | `X - Radar ID` | snd_cmd 8 (prio 1), event log (7, site) |
| SAM launch | `X firing W` | `comm+2Ch` word ++ |
| aircraft visual ID | `Visual ID - AC` | snd_cmd 7, event log (6, type), `[DECE]++` |
| air-to-air launch | `W fired by AC` | `comm+2Ch` word ++ |
| unit take-off | `X - AC taking off` | only if `u < [DED0]-4` |
| unit on final | `X - AC on final` | only if `u < [DED0]-4` |
| unit landing | `X - AC landing` | only if `u < [DED0]-4` |

* Display (7898, R7): when `[409E] != 0` and (`[C07A] == 0 && [294B] == 0` or `[365A] != 0`): text `DS:C578` centred: `x = 160 - 2*(strlen & ~1)` (font 0, 4 px chars), `y = 0x14`, colour `byte [2CA0]` (= 0x0F), through `8AD3` (draws on the current back page), then `--[409E]`. A new message simply overwrites C578 and restarts the timer (`3*[3660]` ticks). No queue.
* `[9876]` buffer is shared with 955c/95c4 (other readers).

---

## 4. Moving-map primitives (cockpit map window x 74..150, y 110..169)

Map state (set by 8387 / 8498 / 84D6 / 82E9, R3): `[4080]` zoom (world units per pixel = `2^(10 - zoom)`), `[4084]`/`[4086]` = world X/Y at the window centre, `[DE94] [DE96] [E302] [E444]` = window clip rectangle (xmin, ymin, xmax, ymax; the sprite/ctx values `0x4A,0x6E,0x96,0xA9` are the window), `[DEDA]` = map hidden flag (when non-zero all map drawing returns immediately), `[365E]` = cockpit/panel drawing allowed.

### 0000:8514 `int mfd_map_sx(int wx)` (V)
`return ((wx - [4084]) >> (10 - [4080])) + 0x70;` (arithmetic shift; 0x70 = 112 = window centre x).
### 0000:852B `int mfd_map_sy(int wy)` (V)
`return ((((wy - [4086]) >> (10 - [4080])) * 3) >> 2) + 0x8B;` (0x8B = 139 = window centre y; the 3/4 factor is the pixel aspect).

### 0000:8548 `int mfd_map_plot(int wx, int wy, int color, int big)` (V)
```
if ([DEDA] != 0 || [365E] == 0) return 0;
sx = mfd_map_sx(wx); sy = mfd_map_sy(wy);
if (sx < [DE94] || sx >= [E302]-1 || sy < [DE96] || sy >= [E444]-1) return 1;     // off the map window
if (color == -1) return 0;
hud_pixel(sx, sy, color);
if (big) { hud_pixel(sx+1, sy, color); hud_pixel(sx, sy+1, color); hud_pixel(sx+1, sy+1, color); }
return 0;
```
Callers: 4f2a (ring blips, `big = 1`), 516d (site dot, colour 0), 5730 (unit blip, colour 0x29), 863c (arc points, `big = 0`), 9678 (R7).

### 0000:85ED `int mfd_map_to_screen(int wx, int wy, int *sx, int *sy)` (V)
`if ([365E] == 0) return 0;` `*sx = mfd_map_sx(wx); *sy = mfd_map_sy(wy);` returns 1 iff `*sx > [DE94] && *sx < [E302]-1 && *sy > [DE96] && *sy < [E444]-1` (strict `>` on the minimum side, unlike 8548). Caller 39c4.

### 0000:863C `void mfd_map_arc(int cx, int cy, int r, int color, int solid, int a0, int a1)` (V)
Arc of a circle in world units: angles in 1/256 turn. `a0 = [bp+0xE]`, `a1 = [bp+0x10]`; `if (a1 < a0) a1 += 0x100`; `hud_set_color(color)`; for `a = a0; a <= a1; a += 0x10` (16 steps per turn): `ang = (a & 0xFF) << 8`; `px = cx + sin16(ang, r)` (caee); `py = cy - cos16(ang, r)` (cb07, minus); `if (px > 0xC000) px = 0; if (py > 0xC000) py = 0` (unsigned compare, clamps negatives); if `solid && a != a0`: `mfd_map_line(prev_x, prev_y, px, py)` else `mfd_map_plot(px, py, color, 0)`; `prev = (px, py)`.
`solid` = `[E54C]` (Doppler radar -> solid polyline, Pulse -> dots). Only caller: 4f2a with `r = ([C070] << 6)` (or the animated radius when `[98DE] != 0`), `color = [9872]`, `a0 = [C13A]`, `a1 = [C13C]`. Quirk: after the 86E5 call the code jumps into the middle of the `8548` call sequence (`8669: add sp, 8`) to share the stack cleanup; harmless in a port.

### 0000:86E5 `void mfd_map_line(int wx0, int wy0, int wx1, int wy1)` (V)
`hud_line_clip(sx(wx0), sy(wy0), sx(wx1), sy(wy1), [DE94], [E302], [DE96], [E444], 1)` (87a9 with `both_pages = 1`). Callers 863c, 94a5.

### 0000:94A5 `void mfd_map_route(void)` (V)
`if ([DEDA]) return;` `p = (C0A4, C0B2)` (player); colour 0x0F (`8929(0xF)`); for `i = [2E7C]; i < 4; ++i`: `w = DS:2E6C + 4*i` (`x = w[0]`, `y = w[2]`); if `w.x != 0`: `mfd_map_line(p.x, p.y, w.x, w.y)`; `p = w`; colour 0x0A (`8929(0xA)`). So the leg player -> current waypoint is white (15), the remaining legs green (10). DS:2E6C = 4 waypoint (x,y) pairs filled by `d7d0(idx, site)` (R2), `[2E7C]` = current waypoint index. Callers: 39c4, 516d.

### 0000:973C `void mfd_map_save_bg(void)` / 0000:9770 `void mfd_map_restore_bg(void)` (V)
`page = *(word*)(([4088] ? [3FFA] : [3FE2]))` = page number of the current draw page TextCtx (double buffer; `[4088]` = `flip_flag` slot 45 result from 87a9).
* save (973C): `blit_rect(page, 0x4A, 0x6E, *(word*)[4012], 0x4A, 0x6E, 0x4D, 0x3C)` (slot 42), i.e. map window (74,110) 77x60 -> backup page (ctx at `[4012]`).
* restore (9770): `blit_rect(*(word*)[4012], 0x4A, 0x6E, page, 0x4A, 0x6E, 0x4D, 0x3C)`.
Callers of save: 516d, 8387. Of restore: 39c4, 516d.

### Pixel / line primitives (used by the map; the two sets of draw contexts `[3FE2]` and `[3FFA]` are the two display pages)
* **0000:8970 `void hud_pixel(int x, int y, int color)`** (V): `8929(color); 871f(x, y, x, y)`.
* **0000:871F `void hud_line_full(int x0, int y0, int x1, int y1)`** (V): `87a9(x0, y0, x1, y1, 0, 0x13F, 0, 0xC7, 0)`.
* **0000:8888 `void hud_line_full2(int x0, int y0, int x1, int y1)`** (V): identical to 871F (callers 88a8, 903f, 99a7).
* **0000:87A9 `void hud_line_clip(int x0, int y0, int x1, int y1, int xmin, int xmax, int ymin, int ymax, int both_pages)`** (V): sets the draw window (`pixel_offset(xmin,ymin)` slot 62 -> `offset_set` slot 26), `DS:2293 = xmax - xmin`, `DS:2295 = ymax - ymin`, colour `color_set(ctx[3FE2]->fg)` (slot 33), `DS:2299 = x0 - xmin`, `DS:229D = y0 - ymin`, `DS:229B = x1 - xmin`, `DS:229F = y1 - ymin`, calls the clipping line routine `1001:032A` (R11). If `both_pages`: `flip = flip_flag()` (slot 45) -> `[4088]`; `page_select(flip == 0)` (the other page), colour set again, same globals, `1001:032A` again, then `page_select([4088] != 0)`. Finally restores `2293 = 0x13F`, `2295 = 0xC7`, `offset_set(0)`. Note the line colour comes from the first ctx only; `8929` keeps both ctx colours equal.

---

## 5. Callees outside my range (names used above)

| addr | meaning |
|---|---|
| `0000:c987` | `dist2d(dx,dy)` = `max(|dx|,|dy|) + min/2`, saturated 0x7FFF |
| `0000:c9d6` | `atan2_16(x, y)` 16-bit turn angle (0 = +y axis, 0x4000 = +x) |
| `0000:c94e` | `clamp(val, lo, hi)`; quirk: if `val < lo` and `val <= -0x4000` (signed) it returns `hi` (wrap handling), else `lo` |
| `0000:caee / cb07` | `r*sin(ang)` / `r*cos(ang)` (`cb07` = `caee(ang + 0x4000)`), args `(ang, r)` |
| `0000:cb62` | `rand(n)` |
| `0000:d6bf(cmd, prio)` | `if (prio >= [43C2] && ([C06E]==0 || prio > 1)) snd_cmd(cmd); d6e5();` (d6e5 = engine tone cmd 0x18/0x19) |
| `0000:d7bb` | cancel time acceleration (see 5460 step 11) |
| `0000:8929(color)` | sets `ctx[3FE2]->fg = ctx[3FFA]->fg = color` |
| `0000:8b93(s)` | `msg_post` (see 516d notes) |
| `0000:49e7(site)` | `site_name` into DS:9876 |
| `0000:499e(code,arg)` | mission event log (entries of 6 bytes at DS:BA2A: word `[98E2]` tick, byte `x>>7`, byte `y>>7`, byte code, byte arg, max 255, `[94EA]` count) |
| `0000:77d9(x,y,z)` | terrain probe -> `[C686]` (R3) |

---

## 6. Structures seen

### 6.1 Radar type table `DS:2E80`, 14 bytes x 23 (V)
`+0 char name[8]` ("None", "SA-2", "SA-5", "SA-8B", "SA-10", "SA-11", "SA-12", "SA-13", "SA-N-4", "SA-N-5", "SA-N-6", "SA-N-7", "Hawk", "Rapier", "Tiger", "Seacat", "IL76", then 6 unnamed: 17..20, 22, and "OTH" at 21), `+8 word maxr`, `+10 word factor`, `+12 byte flags` (bit0 Doppler, bit1 omni-directional / not nose-only for aircraft use), `+13` pad. Same table as START's DS:15C6. Values (maxr, factor, flags):

| t | name | maxr | factor | flags | t | name | maxr | factor | flags |
|---|---|---|---|---|---|---|---|---|---|
| 0 | None | 0 | 0 | 0 | 12 | Hawk | 175 | 6 | 1 |
| 1 | SA-2 | 200 | 3 | 0 | 13 | Rapier | 75 | 8 | 0 |
| 2 | SA-5 | 350 | 2 | 0 | 14 | Tiger | 65 | 4 | 0 |
| 3 | SA-8B | 125 | 5 | 0 | 15 | Seacat | 200 | 2 | 0 |
| 4 | SA-10 | 320 | 7 | 1 | 16 | IL76 (AWACS) | 200 | 8 | 3 |
| 5 | SA-11 | 200 | 5 | 0 | 17 | (MiG-23 class) | 50 | 5 | 0 |
| 6 | SA-12 | 250 | 5 | 1 | 18 | (MiG-25 class) | 70 | 6 | 0 |
| 7 | SA-13 | 125 | 3 | 0 | 19 | (MiG-29 class) | 80 | 7 | 1 |
| 8 | SA-N-4 | 200 | 4 | 1 | 20 | (MiG-31 class) | 100 | 8 | 1 |
| 9 | SA-N-5 | 150 | 3 | 0 | 21 | OTH | 500 | 5 | 1 |
| 10 | SA-N-6 | 320 | 6 | 1 | 22 | (F-5 / MiG-21 class) | 40 | 3 | 0 |
| 11 | SA-N-7 | 200 | 5 | 0 | | | | | |

Unnamed rows have an empty name string. `maxr` is in "distance units" (`dist2d >> 6`).

### 6.2 Site (radar/airbase) record, 16 bytes at `DS:B2A2 + 16*i` (count `[E540]` = all sites, `[98FC]` = leading sites that carry a threat; identical to START's `CBDE` record, loaded from the WLD)
| off | size | meaning |
|---|---|---|
| +0 | word | location-name index into the near-pointer table DS:DEDC (0 = none) |
| +2 | word | world X |
| +4 | word | world Y |
| +6 | word | type (index into the radar table 6.1; 0 = plain airbase; also the SAM row in the weapon table 6.5 for 1..15) |
| +8 | byte | flags: bit0 airbase (0x01), bit1 radar scanned/active this period (set by 4f2a), bit2 "detected" announced (516d), bit4 tracking (516d), bit7 out of action (4f2a skips) |
| +9 | byte | flags2: bit0 neutral, bit1 carrier / no-hangar site, bit2 friendly, bit3 hidden, bit4 radar-ID announced (516d), bit6 ring shown (START UI) (the START meaning of bits 0..3 is confirmed by use) |
| +A | word | radar level 0..255 (516d/559a); START: fighter type that starts here (matched with `unit.type` in 5730 scramble) |
| +C | word | scan countdown (4f2a: reloaded with `([3660]<<8) / ((level>>2) + 0x10) + i`); START: squadron count |
| +E | byte | name index (low 7 bits) into DS:DEDC |
| +F | byte | unused |

### 6.3 Unit (enemy aircraft) record, 36 bytes at `DS:C13E + 0x24*u` (START `D3BE`, count `[DED0]`)
| off | size | meaning |
|---|---|---|
| +0 | word | home / target site index |
| +2 | word | x |
| +4 | word | y |
| +6 | word | z (altitude; 0x8C on a deck, 0x0C on a runway) |
| +8 | dword | x << 5 (sub-pixel position) |
| +C | dword | y << 5 |
| +10 | word | heading (0x10000 = 360 degrees) |
| +12 | word | pitch |
| +14 | word | bank |
| +16 | word | aircraft type (index into 6.4) |
| +18 | byte | flags lo: 0x01 active, 0x02 airborne, 0x04 returning to base / landing, 0x08 radar lock tone given, 0x10/0x20 damaged-falling bits (0x20 = trail + dive; both -> roll 0x400), 0x40 keep target (no retarget), 0x80 scramble on any alert; persistent mask 0x1C1 |
| +19 | byte | flags hi: 0x01 formation member, 0x02 "on final" announced, 0x04 take-off roll (ends at z > 0x12C), 0x08 visual ID done, 0x10 landed / rolling out, 0x20 terrain ahead (climb), 0x40 level > 0xC0 reached |
| +1A | word | speed (start = `AC.+0x12` at spawn in START, 0x64 / 0x0A in 65d4) |
| +1C | word | endurance countdown until return to base |
| +1E | word | weapon type (index into 6.5) |
| +20 | word | not touched by R5 code |
| +22 | word | radar level 0..255 |

### 6.4 Aircraft table `DS:2FC2`, 32 bytes x 29 (V; START `1708`)
`+0 char name[7]` (e.g. `MIG-23`), `+7 char nickname[11]` (NATO name with a leading space, e.g. `" Flogger"`), `+0x12 word max speed`, `+0x14 word range (scramble radius in distance units and endurance base)`, `+0x16 word agility 1..5 (roll limit = value << 12)`, `+0x18 word radar type (0xFFFF = none)`, `+0x1A word (U, 3DX object / weapon index?)`, `+0x1C word (always 2)`, `+0x1E word (0)`.
Full list (type: name nickname, speed, range, agility, radar):
0 MIG-23 Flogger 740 560 3 17 | 1 MIG-25 Foxbat 570 700 2 18 | 2 MIG-29 Fulcrum 700 400 5 19 | 3 MIG-31 Foxhound 790 930 3 20 | 4 Su-27 Flanker 725 715 4 19 | 5 IL-76 Mainstay 400 4000 1 16 | 6 F-4E Phantom 800 520 4 18 | 7 F-14 Tomcat 800 800 4 19 | 8 F-18 Hornet 660 461 5 -1 | 9 An-72 Coaler 350 620 2 0 | 10 F-18 Hornet 660 461 5 -1 | 11 MIG-23 Flogger 740 560 3 0 | 12 F-14 Tomcat 800 800 4 -1 | 13 F-4E Phantom 800 520 4 -1 | 14 Yak-38 Forger 550 300 3 17 | 15 Tu-95 Bear 410 5100 1 0 | 16 Mi-24 Hind 200 300 1 17 | 17 F-5 Tiger 500 250 3 22 | 18 767 Boeing 400 1000 1 -1 | 19 MIG-17 Fresco 570 560 2 17 | 20 F-1 Mirage 790 560 4 20 | 21 F-15 Eagle 800 800 5 -1 | 22 F-16 Falcon 800 800 4 -1 | 23 UH-60 Blackhawk 200 200 1 0 | 24 A-6 Intruder 600 800 3 -1 | 25 MIG-21 Fishbed 750 400 2 22 | 26 EF-111 Aardvark 800 3000 3 -1 | 27 C-5 Galaxy 400 1000 1 0 | 28 A-10 Tbolt II 400 300 2 -1. (Types 8, 10, 12, 13, 21.. are friendly / neutral designs with no radar.)

### 6.5 Weapon table `DS:3362`, 18 bytes x 40 (V for the fields I use; owner weapons)
`+0 char name[8]`, `+8 word range (distance units)`, `+10 word speed` (missile speed = `>> 6`), `+12 word (U)`, `+14 word (U)`, `+16 word (U)`. Rows: 0 None, 1 SA-2, 2 SA-5, 3 SA-8B, 4 SA-10, 5 SA-11, 6 SA-12, 7 SA-13, 8 SA-N-4, 9 SA-N-5, 10 SA-N-6, 11 SA-N-7, 12 Hawk, 13 Rapier, 14 Tiger, 15 Seacat, 16 AA-2, 17 AA-8, 18 AA-6, 19 AA-7, 20 AA-9, 21 AA-10, 22 AIM120, 23 AIM-9, 24 HARM, 25 Penguin, 26 Harpoon, 27 AGM-65, 28 LGBOMB, 29 RTBOMB, 30 FFBOMB, 31 AIM-7W, 32 AIM-9W, 33 SA-14, 34 AA-6, 35 AA-7, 36 AA-9, 37 AA-10, 38 Equip., 39 SA-9. (range, speed): SA-2 125/2000, SA-5 150/1800, SA-8B 65/1200, SA-10 125/1800, SA-11 100/1500, SA-12 125/2000, SA-13 65/900, SA-N-4 30/1200, SA-N-5 30/900, SA-N-6 125/1800, SA-N-7 100/1500, Hawk 125/900, Rapier 65/1200, Tiger 30/900, Seacat 30/900, AA-2 14/1500, AA-8 12/1800, AA-6 50/2400, AA-7 34/1800, AA-9 82/2000, AA-10 64/2000.

### 6.6 Missile / SAM slot table `DS:3C0C`, 28 bytes x 8 (owner weapons; fields I write)
`+0 x, +2 y, +4 z, +6 speed, +8 heading, +0xA pitch, +0xC bank (62d0), +0xE timer (non-zero = slot busy; flight time left), +0x10 type (weapon row), +0x16 owner (>= 0 site, -u unit)`. Slot = `site & 7` / `u & 7`, so at most 8 active enemy missiles, one per (index mod 8).
SAM timer = `(range * [3660] << 4) / speed(=1)`, air-to-air timer = `(range * [3660] << 5) / (wpn.speed >> 6)`.

### 6.7 Tracer slots `DS:E44E`, 12 bytes (cannon rounds from fighters; 4 consecutive slots starting at `[B16C]` rotate each 4 ticks)
`+0 x, +2 y, +4 z, +6 vx, +8 vy, +0xA vz` (U for use; written in 5730 around 5A61..5AE0; `x = 0` frees the slot).

### 6.8 Draw contexts
`[3FE2]`, `[3FFA]`, `[4012]` are near pointers to TextCtx structures (video.md section 3; 24-byte stride: `+0 page, +2 flags, +4 fg, +6 bg, +8 x, +0xA y, +0xC font, +0xE ymin, +0x10 ymax, +0x12 xmin, +0x14 xmax`): the two display pages and the map backup page. `[4088]` = which of the first two is currently being drawn into.

---

## 7. DGROUP globals seen

| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 2E80 | 14B x 23 | det_radar_types | radar type table | 5460, 516d, 62d0 |
| 2FC2 | 32B x 29 | ai_aircraft_types | enemy aircraft table (names at +0, NATO at +7) | 5730, 62d0, 65d4 |
| 3362 | 18B x 40 | wpn_table | weapon table | 516d, 62d0 |
| 3C0C | 28B x 8 | wpn_enemy_slots | enemy missile slots | 516d, 62d0 |
| 3A8C | word[?] | ai_manoeuvre_tbl | roll command table indexed `((skill*8)+aspect)*8+angle` | 5730 |
| 39DA | 8B x 16 | dmg_trail_list | smoke / crash trail records | 5730 |
| E44E | 12B x ? | wpn_tracers | cannon tracer records | 5730 |
| B2A2 | 16B x [E540] | obj_site_list | site records | 516d 559a 5730 62d0 65d4 49e7 |
| C13E | 36B x [DED0] | obj_unit_list | unit records | 5730 62d0 65d4 |
| B174 | 256 B | terr_radar_mask | 16x16 byte map, cell 2048 units, bits 2-3 radar visibility, other bits used by R9 | 5460 |
| 3654 | word | sim_skill | enemy skill 0..3 (pilot+3Eh) | 516d 5460 559a 5730 62d0 |
| 3656 | word | sim_war_level | war level 0..2 (pilot+3Ah) | 516d 559a 5730 62d0 |
| 3658 | word | sim_night | night flag (comm+33h) | 62d0 |
| 3660 | word | sim_tick_scale | ticks per sim step (init 4) | everywhere |
| 3D5C | word | det_player_signature | own signature (set by 43ee), threshold 100 | 516d 5460 62d0 4f2a |
| 3D62 | word | ai_alert_timer | alert countdown set to `[B286]` by 559a (R4 decrements) | 559a 5730 |
| B286 | word | ai_alert_time | `250 * [3660]` (d707) | 559a |
| 9F64 | word | ai_alert_half | `200 * [3660]` (d707) | 5730 |
| B2A0 / B75A / B76E | word | ai_last_x / ai_last_y / ai_last_z | last known player X, Y, altitude | 559a 5730 |
| 98F0 | word | ai_last_heading | last known player heading | 559a |
| 39AA / 39AC / 39B2 | word | decoy_x / decoy_y / decoy_active | decoy attracts AI and blocks 559a from site radars | 516d 559a 5730 |
| 94F0 | word | ai_cur_type | current unit's aircraft type during the pass | 5730 62d0 |
| 94F2 / B054 | word | ai_engaged_now / ai_engaged_prev | fighters in mode 1 within 0x400 (this / previous pass) | 5730 516d 62d0 |
| 3D60 | word | sim_frame_ctr | sim step counter | 5730 4f2a |
| 98E2 | word | sim_tick | tick counter (event log time) | 5730 499e |
| 98F2 | word | det_dist_base | distance to nearest friendly/neutral airbase (39c4) | 516d 5730 |
| DFE0 | word | det_nearest_base | its site index | 5730 |
| C0A4 / C0B2 | word | pl_x / pl_y | player X / Y | 5460 559a 5730 |
| 2DEE / 2DF4 | word | pl_heading / pl_alt | player heading / altitude | 5460 559a 5730 |
| B052 | word | pl_speed | player speed | 5460 |
| 9B04 | byte | pl_switches | cockpit switches (0x10 jammer) | 516d 62d0 43ee |
| C070 | word | det_ring_scale | `(sig + [3D5C]) * D / 100` | 516d 62d0 4f2a |
| C07E | word | det_last_contact | last contact (site >= 0, unit = -1-u) | 516d 62d0 4f2a |
| 3D6A | word | det_ring_timer | ring animation timer | 516d 62d0 4f2a |
| 3D6C | word | det_bar_color | bar colour | 516d 62d0 |
| 9872 | word | det_ring_color | ring colour | 516d 62d0 4f2a |
| E54C | word | det_ring_solid | 1 = Doppler (solid arc) | 516d 62d0 4f2a |
| C13A / C13C | word | det_arc_a0 / det_arc_a1 | arc start / end angle (1/256 turn) | 516d 62d0 4f2a |
| 3D92 | word | det_warn_timer | radar warning lamp countdown | 5588 4ebc |
| DED6 | word | stat_locks | radar lock events | 516d 62d0 |
| DED2 | word | stat_radar_ids | "Radar ID" count | 516d 72a4 |
| DECE | word | stat_visual_ids | "Visual ID" count | 62d0 |
| 9876 | char[26+] | msg_buf | message build buffer | 49e7, 516d... |
| C578 | char[] | msg_text | current on-screen message | 8b93, 7898 |
| 409E | word | msg_timer | message lifetime (`3*[3660]`) | 8b93 7898 |
| 2CA0 | byte | msg_color | 0x0F | 7898 |
| 3DD4..3E33 | strings | msg_str_* | see section 3 | |
| DEDC | word[128] | name_ptrs | location name pointer table | 49e7 |
| 4080 / 4084 / 4086 | word | map_zoom / map_cx / map_cy | map scale and centre | 8514 852b |
| DE94 / DE96 / E302 / E444 | word | map_xmin / ymin / xmax / ymax | map clip window | 8548 85ed 86e5 |
| DEDA | word | map_off | map hidden | 8548 94a5 |
| 365E | word | cockpit_draw_ok | panel drawing allowed | 8316 8548 85ed |
| 3FE2 / 3FFA / 4012 | word | ctx_page_a / ctx_page_b / ctx_map_bg | TextCtx near pointers | 8316 8929 973c 9770 |
| 4088 | byte | draw_flip | current draw page selector | 87a9 973c 9770 |
| 2293 / 2295 / 2299 / 229B / 229D / 229F | word | line_w / line_h / x0 / x1 / y0 / y1 | argument block for 1001:032A | 87a9 |
| 2E6C | 4B x 4 | mission_waypoints | route points (x,y) | 94a5 d7d0 |
| 2E7C | word | mission_cur_wp | current waypoint | 94a5 |
| DFDC | word | mfd_page | MFD page id (0x14 = route page) | 516d 5460.. |

---

## 8. Open items

* Meaning of `DS:B174` bit fields beyond bits 2-3; why the radar visibility is looked up at the **player's** cell (probably a "radar coverage" overlay of the WLD).
* Exact palette colours: 0x2A / 0x29 / 0x11 (516d bars), 0x0B / 0x0F (62d0 bars), 0x29 (unit blip), 0x0F / 0x0A (route). Need a visual check (flight palette in FORMATS.md).
* Meaning of the aircraft table fields `+0x1A` (3DX object? weapon?), and unit `+0x20`; weapon table `+12/+14/+16`.
* Unit flag bits `0x10`, `0x40`, `0x80` (low byte) are inferred from usage; unit 0 and units `>= [DED0]-4` are special ("quiet"); reason unknown (probably scenery / ambient traffic).
* 5730 sections 3, 5 and 8 (formation, manoeuvre table `DS:3A8C`, tracer spawn arithmetic) are index-level only; a flight/AI reader (S3.8) must go through `5730` bytes `5A61..5C46` and `577B..5992` for exact arithmetic.
* Bearing convention: `bearing = atan2_16(player.x - radar.x, radar.y - player.y)` = compass bearing radar -> player (0 = north = decreasing world Y, 0x4000 = east), compared directly with heading fields (`[2DEE]`, `unit.+0x10`), so heading uses the same convention (consistent in all uses, (V) by symmetry of the aspect fold).
* `snd_cmd 7` / `8` track roles (v3 <- F52 / F4A) are known from sound.md, their audible meaning is not.
* 62d0: when `2*skill < [B054]` the code path jumps to the "tone" tail; I did not separate whether `unit.flags |= 8` is set in that case (it is, the shared tail at 658A-65A7) (V).



---
# Part 6: weapon release, impacts, objectives, damage bookkeeping (R6)

*Reader pass R6, original title: S3.6 spec R6 - weapons, projectile update, impact/damage, mission-target bookkeeping*

VGAME.EXE range `0000:671A .. 7897` minus `77D9` (R3's): `671a 711f 71b4 72a4 7470 74f6 753f 780a 784a` (9 functions).
All are in code segment 0000, near calls, `enter/leave` frames, cdecl (args pushed right to left, first arg at `[bp+4]`).
`(V)` = verified in the disassembly, `(U)` = unclear / needs a runtime check.

## 0. Overview and call order

```
cba0 (keyboard, fire key)         39c4 (sim step, once per step)              a17e (gun task, bullets)
   |                                  |                                           |
   v                                  v                                           v
753f wpn_release --fills 8..11--> 671a wpn_proj_update_all ---+---> 71b4 dmg_unit_kill      <---- a17e
62d0 (AI launch)  --fills 0..7--> (12 projectile slots)       +---> 72a4 dmg_object_destroy <---- a17e
                                                              +---> 711f wpn_seeker_check
                                  72a4 --> 780a dmg_wobj_destroyable, 7470 (target bookkeeping), 559a
                                  71b4 --> 7470 (primary air target)
                                  671a --> 74f6 dmg_player_blast
```

* Slots 0..7 of the projectile table `DS:3C0C` are **enemy** missiles (SAM sites, enemy fighters - 62d0 launches them into slot
  `unit & 7`, 5730/516d launch the SAMs) aimed at the player. Slots 8..11 are the **player's** missiles and bombs: 753f only ever
  allocates 8..11. (V)
* A projectile record (28 bytes) has a `type` (index of the 40-record **projectile prototype table** `DS:3362`) and a `store` id (index of
  the 20-record **store table** `DS:3662`, = the START weapon id 0..19). (V)
* Results of an impact are expressed as: (a) text built in the scratch buffer `DS:9876` and shown with `msg_show` (8B93 -> `DS:C578`,
  3 s), (b) event records in the flight log `DS:BA2A` (`log_event`, 499E), (c) flag/hit-point changes in the object table `DS:B2A2`,
  the unit table `DS:C13E` and the world object (`wobj_set_shape`), (d) mission flags `DS:9B04/9B05`, `DS:2E7C`, (e) sounds
  (`d6bf`) and cockpit lamps (`898b`). The state blob written by `4C29` at the end of the sim (objects, units, mission targets,
  C0BA kill counters, the 0x600-byte log; see 6.5) is what END reads.

## 1. Conventions and helper semantics used below

* DS base: `DS:oooo` = flat image offset `0x1E230 + oooo`; in the file `work/unpacked/VGAME.EXE` there is a 0x820-byte header, so
  **file offset = 0x1EA50 + DS offset**. (V - the tables below were dumped that way)
* Angles are 16-bit turns (0x10000 = 360 deg, 0x4000 = 90 deg). Heading 0 = north, x grows east, **y grows south**:
  `x += sin(h)*d; y -= cos(h)*d`. For the world-object lookup y is mirrored (`0x8000 - y`) and x, y are shifted `<< 5` into 32-bit.
  The altitude `z` of a projectile is in 1/32 of an x/y unit (slant range uses `|dz| >> 5`; z moves with `speed<<7`, x/y with `speed*4`).
* `rate` = `[3660]` = sim steps per second (initial 4; recomputed by `d707`: `clamp([3660], 4-[3D7A], 15)`; doubled while time acceleration
  `[3D7A]==2` is cancelled by `d7bb`). Nearly every per-step quantity is `... * 4 / rate` or `... << k / rate`.
  `[3D60]` = sim step counter (`++` per step in 39c4; `3D60 & 1`, `& 3` are 2- and 4-step phases); `[98E2]` = mission seconds
  (incremented when `3D60 % rate == 0`).
* `skill` = `[3654]` = enemy skill 0..3 (pilot+3Eh: Green, Regular, Veteran, Elite; shell.md). `[3656]` = pilot+3Ah (war level). (V: 3AF1)
* Helpers called from this range (proposed names; other readers own them; listed so the algorithms below can be read alone):

| addr | name / signature | semantics |
|---|---|---|
| c94e | `int clamp(int v, int lo, int hi)` | pushed `hi, lo, v` (`v=[bp+4] lo=[bp+6] hi=[bp+8]`). `if (hi < v) return hi; if (lo > v) { if (v <= (short)0xC000) return hi; return lo; } return v;` Quirk: a value below -16384 that is also below `lo` returns `hi` (wrap guard). (V) |
| c987 | `int hypot_approx(int dx, int dy)` | `a=abs(dx); b=abs(dy); r = max(a,b) + (min(a,b) >> 1)` computed in 32 bit, `min(r,0x7FFF)`. (V) |
| c9d6 | `int atan2_ang(int dx, int dy)` | angle of the vector, `dy` is the "north" axis: (dx=0,dy>0)->0, (dx=0,dy<=0)->0x8000, (dy=0,dx>0)->0x4000, (dy=0,dx<0)->0xC000, otherwise octant division (flight owner). Callers pass `(dx, -dy_world)` to get a compass heading. (V special cases) |
| caee / cb07 | `int sin_mul(a, m)` / `int cos_mul(a, m)` | `m*sin(a)` / `m*cos(a)` via lib `109d:7c` (sine) and `109d:6` (fixed multiply); `cos_mul(a,m) = sin_mul(a+0x4000, m)`. (V structure; rounding U) |
| cb39 | `int sign(int v)` | -1 / 0 / +1 |
| cb62 | `int rand_n(int n)` | `rand() * n >> 15`, 0..n-1 (V) |
| 8b93 | `void msg_show(const char *s)` | `strcpy(DS:C578, s); [409E] = rate * 3;` 7898 (HUD) draws `DS:C578` horizontally centred (`x = 160 - 2*(strlen & ~1)`) at y = 20, colour `[2CA0]`, while `[409E] != 0`, and decrements it once per frame **in which the text is drawn** (drawn when (`[C07A]==0` and `[294B]==0`) or `[365A] != 0`); about 3 s of drawn frames. (V; R4/R8 own the display) |
| 499e | `void log_event(int code, int arg)` | appends a 6-byte record to the flight log at `DS:BA2A + 6*n`, `n=[94EA]` (only while `n < 0xFF`): `{ short time=[98E2]; byte x=[C0A4]>>7; byte y=[C0B2]>>7; byte code; byte arg }`, then writes a terminator record with `code=0` after it and `[94EA]++`. x/y are the **player's** position at the time, not the event's. Pushed as `(arg, code)`: `code=[bp+4]`. (V) |
| d6bf | `void snd_play(int cmd, int prio)` | `if (prio >= [43C2] && ([C06E]==0 || prio <= 1)) snd_cmd(cmd); d6e5();` called `push prio; push cmd`. cmd numbers: sound.md section 8. (V) |
| 898b | `void hud_lamp_set(int idx, int val)` | annunciator lamp `idx` (6-byte entries at DS:3F02: `+0` frame, `+2` off value, `+4` cached value); only when `[365E] != 0` (2D panel shown) and `val != cached`; redraws through `d83e([3FE2], frame, val != off)`. Lamps used here: 0 (radar/SAM lock, val 0x11), 1 (IR missile, val 0x29), 8 (threat, 0x11 / 3), 10..16 (damage; val 0x29 = failed, 0x0A = ok). (V calls; lamp names U) |
| 49e7 | `void obj_name_build(int o)` | builds `DS:9876` = `DEDC[(obj[o].+0E & 0x7F)*2]` (object type name); if `strlen(DEDC[obj[o].+0*2]) != 0`: add `" at "` (only if the type name is non-empty) + the place name. If `strlen(9876) > 0x19`: `9876[0x18]='.'; 9876[0x19]=0`. `DS:DEDC` = table of near pointers to type names / place names (filled by 4AD0). (V) |
| bad1 | `int obj_find_at(int x, int y)` | `[9F10] = wobj_find(x<<5, (0x8000-y)<<5)`. If 0: return -1. Else take `x' = wobj.+4>>5`, `y' = 0x8000-(wobj.+8>>5)`; search `i = 1 .. [C0B4]-1` for `obj[i].x==x' && obj[i].y==y'`: return i. No match: fill the **scratch record 0** (`obj[0].x=x', obj[0].y=y', obj[0].+0E = wobj.+0 + 0x100`), `if ([3D68]==0) [3D68]=-1`, return 0. (V) |
| 8bab | `int wpn_effect(int store, int o)` | `cls = (char)DS:C604[obj[o].+0E & 0x7F] & 0xF; return (signed char)DS:386A[store*16 + cls]` (table 2.3). (V) |
| bc77 / bcac | `int obj_cls_soft(o)` / `int obj_cls_ship(o)` | same `cls`; bc77 true for cls in {9, 0xB, 0xC, 0xD}, bcac true for {0xC, 0xD}. (V) |
| c6b8 | `unsigned shape_map(unsigned v)` | `if (v & 0x100) return DS:0510[(v & 0x7F)] | (v & 0x8000); else return v;` (word table). (V) |
| bc12 | `unsigned obj_wreck_shape(int o)` | if `obj[o].flags & 0x80` is clear return `obj[o].+0E`; else `v = shape_map(obj[o].+0E); if ((DS:[A60 + (v>>8)*2])[v & 0xFF] & 0x20) return obj[o].+0E | 0x100; return ((obj_cls_soft(o) ? (signed char)[C0AC] : (signed char)[DEA4]) + 0x100)`. `[C0AC]`,`[DEA4]` = generic wreck shape ids loaded with the world; `DS:0A60` = 4 near pointers (0760,07E0,0860,08E0) to per-shape flag tables. (V) |
| 0d08 / 0f31 | `void *wobj_find(long x32, long ymirror32)` / `void wobj_set_shape(w, shape)` | world (terrain) object at a position, pointer or 0; result also kept in `[9F10]`. Fields used: `+0` shape word, `+4/+6` x (32 bit), `+8/+A` y (32 bit), `+C` pointer to a parent whose byte `+6` (bit 7 + low 7 bits) is a shape id. R3/render owns. (V fields) |
| 77d9 -> 0ffb | `void wpn_ground_test(x, y, z)` | `0FFB(x<<5, (0x8000-y)<<5, (long)z)`; the world collision test writes `[C686] = 1` when the point is inside terrain / a solid object. R3's range. (V call) |
| 8548 / 8387 | `int map_plot(x, y, colour)` / `void map_refresh(px, py)` | tactical map blip / full redraw of the map blips (R3/R8). |
| 81f6 | `void view_label_draw()` | redraws the "Ahead / Rear / Right / Left" view label (strings 40CA..40E2); it uses `DS:9876` as a scratch buffer (side effect: 9876 is overwritten). 671a calls it right before it builds its own text. (V) |
| 559a | `void ai_alert_reset()` | `[3D62]=[B286]` (=250*rate); `[B2A0]=` player x (or `[39AA]` when `[39B2]`), `[B75A]=` player y, `[B76E]=[2DF4]`, `[98F0]=[2DEE]`, `[C0B6]=0xFF`; for every ground object with `+6 != 0`: `obj.+0A = clamp(obj.+0A, ([3656]+skill-1)<<4, 255)`. 72a4 calls it when skill < 2. (V; meaning U) |

## 2. Data tables

### 2.1 Projectile prototype table `wpn_proto_tab`, DS:3362, 40 records x 0x12 bytes (V; static data)

Record: `+0 char name[8]` (NUL padded; shown in messages), `+8 short range` (flight-time factor), `+A short speed64` (cruise speed * 64),
`+C short class` (guidance class), `+E short turn` (turn-rate index), `+10 short u` (read only by 9aca: probably a display colour; U).
Same data as shell.md `1AA8` (START copy). Index = `proj.type`.

| id | DS addr | name | +8 range | +A spd*64 | +C class | +E turn | +10 u |
|--:|--|--|--:|--:|--:|--:|--:|
| 0 (0x00) | 3362 | None | 0 | 0 | 0 | 1 | 15 |
| 1 (0x01) | 3374 | SA-2 | 125 | 2000 | 1 | 4 | 15 |
| 2 (0x02) | 3386 | SA-5 | 150 | 1800 | 1 | 1 | 15 |
| 3 (0x03) | 3398 | SA-8B | 65 | 1200 | 2 | 3 | 15 |
| 4 (0x04) | 33AA | SA-10 | 125 | 1800 | 3 | 2 | 15 |
| 5 (0x05) | 33BC | SA-11 | 100 | 1500 | 2 | 3 | 15 |
| 6 (0x06) | 33CE | SA-12 | 125 | 2000 | 3 | 2 | 15 |
| 7 (0x07) | 33E0 | SA-13 | 65 | 900 | 0 | 4 | 15 |
| 8 (0x08) | 33F2 | SA-N-4 | 30 | 1200 | 2 | 3 | 15 |
| 9 (0x09) | 3404 | SA-N-5 | 30 | 900 | -1 | 4 | 15 |
| 10 (0x0a) | 3416 | SA-N-6 | 125 | 1800 | 3 | 2 | 15 |
| 11 (0x0b) | 3428 | SA-N-7 | 100 | 1500 | 2 | 3 | 15 |
| 12 (0x0c) | 343A | Hawk | 125 | 900 | 2 | 3 | 15 |
| 13 (0x0d) | 344C | Rapier | 65 | 1200 | 2 | 4 | 15 |
| 14 (0x0e) | 345E | Tiger | 30 | 900 | 1 | 3 | 15 |
| 15 (0x0f) | 3470 | Seacat | 30 | 900 | 1 | 3 | 15 |
| 16 (0x10) | 3482 | AA-2 | 14 | 1500 | -1 | 4 | 15 |
| 17 (0x11) | 3494 | AA-8 | 12 | 1800 | 0 | 5 | 15 |
| 18 (0x12) | 34A6 | AA-6 | 50 | 2400 | 2 | 2 | 15 |
| 19 (0x13) | 34B8 | AA-7 | 34 | 1800 | 2 | 2 | 15 |
| 20 (0x14) | 34CA | AA-9 | 82 | 2000 | 2 | 3 | 15 |
| 21 (0x15) | 34DC | AA-10 | 64 | 2000 | 3 | 4 | 15 |
| 22 (0x16) | 34EE | AIM120 | 32 | 2400 | 7 | 4 | 1 |
| 23 (0x17) | 3500 | AIM-9 | 17 | 2000 | 7 | 8 | 1 |
| 24 (0x18) | 3512 | HARM | 20 | 1200 | 4 | 2 | 1 |
| 25 (0x19) | 3524 | Penguin | 32 | 500 | 5 | 2 | 15 |
| 26 (0x1a) | 3536 | Harpoon | 60 | 500 | 5 | 2 | 15 |
| 27 (0x1b) | 3548 | AGM-65 | 32 | 800 | 6 | 2 | 10 |
| 28 (0x1c) | 355A | LGBOMB | 10 | 0 | 28 | 2 | 12 |
| 29 (0x1d) | 356C | RTBOMB | 0 | 0 | 29 | 2 | 12 |
| 30 (0x1e) | 357E | FFBOMB | 0 | 0 | 30 | 2 | 12 |
| 31 (0x1f) | 3590 | AIM-7W | 44 | 2400 | 2 | 4 | 1 |
| 32 (0x20) | 35A2 | AIM-9W | 12 | 2000 | 0 | 5 | 1 |
| 33 (0x21) | 35B4 | SA-14 | 16 | 900 | 0 | 5 | 1 |
| 34 (0x22) | 35C6 | AA-6 | 50 | 2400 | -1 | 2 | 15 |
| 35 (0x23) | 35D8 | AA-7 | 34 | 1800 | -1 | 2 | 15 |
| 36 (0x24) | 35EA | AA-9 | 82 | 2000 | -1 | 3 | 15 |
| 37 (0x25) | 35FC | AA-10 | 64 | 2000 | 0 | 4 | 15 |
| 38 (0x26) | 360E | Equip. | 0 | 0 | 29 | 0 | 11 |
| 39 (0x27) | 3620 | SA-9 | 30 | 1000 | 0 | 4 | 1 |

Fields as used by code (V):
* `+C class` (`cls`) drives the seeker logic: `cls <= 0` (-1, 0) = **IR**-seeker (defeated by flares, `[9B04]&0x20`); `cls > 0` = **radar** (defeated by chaff `[9B04]&0x10`).
  -1 and 1 = old seekers that are *always* lost to a decoy once the countermeasure test applies; 0 = rear-aspect IR (extra 45 deg check); 3 = all-aspect
  (no 22.5 deg cone check); 2 = semi-active radar; **7** = player air-to-air (target = an enemy *unit*); **4/5/6** = player ground-guided
  (HARM: needs an emitting site, Penguin/Harpoon: ships, AGM-65: any registered object); 28/29/30 = bombs (also `proj.type` special-cased: 0x1C LGBOMB, 0x1D RTBOMB, 0x1E FFBOMB, 0x26 Equip.).
* `+8 range`, `+A speed64`: launch life (753f) `life = ((range*rate) << 5) / ((speed64 >> 6) + 1) + 6`, cruise speed `speed64 >> 6`.
* `+E turn`: per-step turn limit is `turn << 7` for heading and `turn << 8` for pitch (before the `/rate` scaling).
* `+10 u`: only read by 9aca (map/target display), U.

### 2.2 Store table `wpn_store_tab`, DS:3662, 20 records x 0x1A bytes, index = START weapon id (V; static data)

Record: `+0 char short_name[10]`, `+0A char long_name[12]` (used in messages: `"<long name> fired"`, `"... misses "`, `"... destroyed by <long name>"`),
`+16 short proj_type` (index into 2.1; **-1 = camera, -2 = extra fuel tank, 0 = guns (no projectile)**), `+18 short u` (read only by 02FE at load; picture/bay icon index, U).
START ids (shell.md): 0 short-range IR AAM ... 17 fuel tank, 18 special equipment, 19 empty bay (shown as `Guns`).

| id | DS addr | short name (+0) | long name (+0A) | +16 proj type | +18 |
|--:|--|--|--|--:|--:|
| 0 | 3662 | AIM-9M | Sidewinder | 23 | 0 |
| 1 | 367C | AIM-120 | AMRAAM | 22 | 3 |
| 2 | 3696 | AGM-88A | HARM | 24 | 7 |
| 3 | 36B0 | P3 ASM | Penguin | 25 | 10 |
| 4 | 36CA | AGM-86A | Harpoon | 26 | 12 |
| 5 | 36E4 | AGM-65D | Maverick | 27 | 16 |
| 6 | 36FE | GBU-12 | Paveway | 28 | 1 |
| 7 | 3718 | Mk 20 | Rockeye | 29 | 4 |
| 8 | 3732 | Dndl | Durandal | 29 | 8 |
| 9 | 374C | Mk 82-0 | Slick | 30 | 11 |
| 10 | 3766 | Mk 82-1 | Snakeye | 29 | 13 |
| 11 | 3780 | Mk 20 | Rockeye II | 28 | 17 |
| 12 | 379A | Mk 122 | Fireeye | 30 | 2 |
| 13 | 37B4 | CBU-72 | Fuel-Air | 28 | 6 |
| 14 | 37CE | Mk 35 | IN Cluster | 29 | 9 |
| 15 | 37E8 | ISC B-1 | Minelets | 29 | 14 |
| 16 | 3802 | 135 mm | Camera | -1 | 15 |
| 17 | 381C | 1900lbs | Extra Fuel | -2 | 18 |
| 18 | 3836 | Special | Equip | 38 | 5 |
| 19 | 3850 | 20 mm | Guns | 0 | 5 |

### 2.3 Weapon-effectiveness table `wpn_effect_tab`, DS:386A, 20 stores x 16 target classes (bytes) (V; static data)

`wpn_effect(store, o) = DS:386A[store*16 + cls]`, `cls = DS:C604[obj[o].+0E & 0x7F] & 0xF` (DS:C604 = 128-byte object-type -> class table, loaded with the
world by 4B57; low nibble = target class 0..15). The value is the **lethality 0..8** of that store against that class. Use: bombs
(`miss radius = value*64/(skill+2)`, see 671a), ground-guided missiles (`hit if value > rand_n(4)`). Rows (store id, long name):

| store | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|--|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| 0 Sidewinder | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1 AMRAAM | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2 HARM | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 8 | 0 | 0 | 0 | 5 | 0 | 0 | 0 |
| 3 Penguin | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6 | 6 | 0 | 0 |
| 4 Harpoon | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 8 | 7 | 0 | 0 |
| 5 Maverick | 2 | 0 | 2 | 3 | 4 | 4 | 3 | 3 | 4 | 4 | 4 | 0 | 4 | 0 | 6 | 0 |
| 6 Paveway | 4 | 1 | 2 | 4 | 5 | 5 | 4 | 4 | 5 | 4 | 4 | 0 | 3 | 4 | 4 | 0 |
| 7 Rockeye | 0 | 0 | 4 | 4 | 0 | 0 | 5 | 5 | 4 | 4 | 5 | 0 | 4 | 0 | 6 | 0 |
| 8 Durandal | 0 | 8 | 0 | 0 | 2 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 9 Slick | 3 | 1 | 3 | 4 | 1 | 1 | 4 | 2 | 3 | 3 | 4 | 0 | 3 | 0 | 0 | 0 |
| 10 Snakeye | 0 | 1 | 4 | 4 | 0 | 0 | 4 | 0 | 2 | 0 | 4 | 0 | 0 | 0 | 0 | 0 |
| 11 Rockeye II | 0 | 0 | 4 | 4 | 0 | 0 | 4 | 4 | 4 | 4 | 4 | 0 | 3 | 0 | 4 | 0 |
| 12 Fireeye | 2 | 0 | 4 | 4 | 0 | 2 | 5 | 3 | 3 | 2 | 5 | 1 | 3 | 3 | 0 | 0 |
| 13 Fuel-Air | 4 | 0 | 0 | 6 | 0 | 5 | 6 | 2 | 2 | 4 | 6 | 5 | 2 | 3 | 3 | 0 |
| 14 IN Cluster | 1 | 0 | 6 | 4 | 0 | 0 | 6 | 6 | 3 | 6 | 6 | 0 | 3 | 0 | 2 | 0 |
| 15 Minelets | 0 | 6 | 0 | 3 | 0 | 0 | 2 | 3 | 0 | 0 | 3 | 0 | 0 | 0 | 2 | 0 |
| 16 Camera | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 17 Extra Fuel | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 18 Equip | 2 | 0 | 3 | 2 | 0 | 1 | 2 | 3 | 2 | 2 | 3 | 0 | 1 | 0 | 2 | 0 |
| 19 Guns | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

(Class 8 = radar / SAM site, 12/13 = ships by the ship test; the other class meanings come from the object-type loader, not decoded here (U).)

### 2.4 Projectile record `proj[12]`, DS:3C0C, 12 x 0x1C bytes (V)

| off | abs (slot 0) | field | meaning |
|--:|--|--|--|
| +00 | 3C0C | x | world x (short) |
| +02 | 3C0E | y | world y (south = +) |
| +04 | 3C10 | z | altitude in 1/32 xy units; `z < 0` (signed) = impact |
| +06 | 3C12 | speed | steps: horizontal move per step = `cos(pitch)*speed*4/rate`; accelerates +1 per odd step up to `speed64 >> 6` |
| +08 | 3C14 | heading | 16-bit turn |
| +0A | 3C16 | pitch | 16-bit turn, + = climbing |
| +0C | 3C18 | bank | enemy/guided: `2*clamped heading change` (for drawing); at launch = the player's roll `[2DF2]` |
| +0E | 3C1A | life | steps left; **0 = slot free** |
| +10 | 3C1C | type | index into 2.1 |
| +12 | 3C1E | store | index into 2.2 (name for messages; effect table row) |
| +14 | 3C20 | target | player missiles: unit index (cls 7) or ground object index (cls 4,5,6; 0 = scratch record); -1 = none |
| +16 | 3C22 | aux | enemy missiles (written by 62d0/516d): **launcher** - `<= 0`: `-unit index` (unit table), `> 2`: ground object index (1,2 unused); player FFBOMB (0x1E): the minimum pitch ("terminal dive floor", `bbed() - 0x400`) |
| +18 | 3C24 | tgt_x | last known target x (cls 4/5 re-registration through `bad1`) |
| +1A | 3C26 | tgt_y | last known target y |

Other readers of the table: 2F82 (3D models), 8C10 (map), 9ACA, A17E, BF71 (all outside R6).

### 2.5 Unit record `unit[]` (enemy aircraft etc.), base DS:C13E, `[DED0]` records x 0x24 (V fields used here)

The state blob (4C92) saves `[DED0]*0x24` bytes starting at `DS:C13E`, so the record **starts at C13E**; `x` is at +2 (absolute C140 + 0x24*j).

| off | abs (unit 0) | field | meaning |
|--:|--|--|--|
| +00 | C13E | ? | written by 39C4 (U) |
| +02/+04/+06 | C140/C142/C144 | x, y, z | position (z in the same scale as projectile z) |
| +10 | C14E | heading | used by 62D0 |
| +12 | C150 | pitch | |
| +14 | C152 | roll | |
| +16 | C154 | type | index into the unit-type table `DS:2FC2` (name at `2FC2 + 32*type`, 29 names MIG-23 ... A-10) |
| +18 | C156 | flags | `0x02` active (target candidate for cls-7 missiles), `0x08` the unit is still guiding its missile (cleared => the missile loses guidance), `0x10` "missile inbound" (set by 671a when an own missile is < 0x180 away), `0x20` dead, `0x800` counted in `[DECE]` (decremented on kill), `0x4000` -> event arg bit 0x80; `71b4` keeps `0x1C1` when hp == 0 |
| +1A | C158 | hp | |
| +1E | C15C | weapon | proj type the unit fires (62D0) |

Unit-type table `DS:2FC2`: 32 bytes per type = `char name[0x1E]; short kills` (`+0x1E` = DS:2FE0 for type 0); 71b4 increments `kills` per kill (the counters are never read again in VGAME: dead data, U).

### 2.6 Ground object record `obj[]`, base DS:B2A2, `[C0B4]` records x 16 bytes (V fields used here)

State blob saves `[C0B4]*16` bytes from `DS:B2A2`. **Record 0 is a scratch record** (671a saves it at entry to `DS:B290..B29F` and restores it at exit; bad1 writes it
when a hit object is not registered).

| off | abs (obj 0) | field | meaning |
|--:|--|--|--|
| +00 | B2A2 | name | index into the place-name pointer table `DS:DEDC` (0 = no place) |
| +02 | B2A4 | x | |
| +04 | B2A6 | y | |
| +06 | B2A8 | active | non-zero = emitting / counts as "alive objective" (HARM lock needs `!= 0`; 72A4 clears it; 72A4 uses event code 0xC when it is already 0) |
| +08 | B2AA | flags | `0x04` shown on the tactical map (with `!0x80`), `0x02` friendly(?) colour select, `0x10` site still guiding its missiles (cleared => missile loses guidance), `0x80` destroyed, `0x200` airfield (high byte bit 1), `0x1000` counted in `[DED2]` (decremented on kill) |
| +0A | B2AC | alert | threat/alert level (559A clamps it) |
| +0E | B2B0 | shape | low 7 bits = object type (index into `DEDC` type names and `C604` class table), bit 8 = "already replaced by wreck", bit 15 kept by `shape_map` |

### 2.7 Mission target records `mt[2]`, DS:E2D8, 2 x 0x12 bytes (V fields used here; blob saves 0x24 bytes from E2D8)

`mt[0]` = primary, `mt[1]` = secondary target. Created by START/the mission generator (shell.md "mission record"); not written by VGAME code.

| off | abs (mt0) | field | meaning |
|--:|--|--|--|
| +00 | E2D8 | kind | 1 Photograph enemy, 2 Destroy enemy (ground object), 3 Drop supplies over, 4 Land supplies at, 5 Hit AN72 flying to, 6 Plane returning to, 7 Hit AN72 leaving, 8 Destroy plane over (jump table at 983B in 97A4; texts 413D..41B7) |
| +02 | E2DA | obj | ground object index (kind 2/3/4 and photo); for kinds >= 5 the target is unit 0 |
| +04 | E2DC | base | airfield object (3AA0 tests its flag `0x200`) |
| +06 | E2DE | flags | `0x02` has a deadline (97A4 prints "Before"), `0x20` deadline enforced (39C4 at 426F) |
| +08.. | | | deadline etc. - R7/R? (`[DE88]` = deadline in mission seconds) |

### 2.8 Flag words and per-mission state touched here (V unless marked)

* `[9B04]` (word): `0x0004` weapon bay doors open (753f), `0x0008` set at start on an airfield mission (3AAC, U), `0x0010` chaff active (radar countermeasure),
  `0x0020` flares active (IR countermeasure) (V: 6FBD..6FD1), `0x2000` secondary target achieved, `0x4000` primary target achieved. High byte `[9B05]`: `0x10` = **training
  mission** (copied from pilot+3Ch bit 1 at 3B19: no blast damage, no missile-kill log), `0x20` secondary target achieved, `0x40` primary target achieved.
  (`7470` tests `[9B04] & (0x4000 >> j)` = already achieved, but sets `[9B05]` bits - same bits; 39C4 at 3C0D skips the map marker of targets whose bit is set.)
* `[2E7C]` = which objective(s) were just achieved for the HUD/debrief: 1 secondary, 2 primary, 3 both (7470).
* `[3632]` (word) = damaged-subsystem bitmask (bit k = damage lamp 10+k, k = 0..6; bit 7 can be set too and has no lamp); `[C5C8]` = number of hits taken (initial 2). 4926 copies them to comm+34h / comm+36h at the end.
* `[3D64]` = currently selected air target (unit index), `[3D66]` = selected / designated ground object index (-1 = none), `[3D68]` = last destroyed object (-1), `[3D6E]` = locked unit (-1),
  `[E2FE]` = master mode (1 = air-to-air, 2 = air-to-ground); `[9D0E]` is set to 1 when the selected target (`3D64` unit with mode 1, `3D66` object with mode 2) is destroyed
  (cleared/consumed by 2F82/BF71).
* `[3D62]` = AI alert timer, set to `[B286] = 250*rate` by 559A and by a ground-missile hit; read in 671a as `[9F64] (=200*rate) > [3D62]` (more than 50 s since the last alert).
* `[9D0A]`, `[DED8]` = display timers `4*rate` set when the secondary resp. primary target object is destroyed (read by 39C4, U meaning: "TARGET DESTROYED" indication).
* Explosion hand-over: `[C0A0],[C0AA],[C0B0]` = position of a projectile explosion, `[9D0C]` = explosion kind (0xFFFA ground bomb burst, 0x10 direct hit/proximity; read by A17E/BF71); `[C0A6],[C0B8],[C602]` = unit explosion position, `[B756] = 0x80` (71B4).
* `[B04C]` = slot of the last launched projectile (set by 753f; cockpit external camera follows it, U).
* `[3634]` = fuel (lbs, initial 10000); `[43B4]` = AGM-65 TV-lock state (753f clears it).
* Decoys (`DS:39AA`, 12-byte records): `[39AA + 12*j]` x, `[39AC + 12*j]` y, `[39B0 + 12*j]` type (1 flare, 2 chaff) for j = 1..3; record 0 is
  the player's own alias: when `[39B2] != 0` the *enemy missiles aim at (`[39AA]`, `[39AC]`)* instead of the player (set by 4512/461C, U what that is: probably ejection/external view).

### 2.9 Event log codes written from this range (`log_event(code, arg)`, record at DS:BA2A + 6n)

| code | arg | writer | meaning |
|--|--|--|--|
| 0x02 | world object type (word [wobj]) | 72A4 (obj 0) | unregistered world object destroyed |
| 0x01 / 0x0C (+ 0x80 for primary, 0x40 for secondary target) | object index | 72A4 | registered object destroyed (0x0C if its `+6` was already 0); `+0x80` if `mt[0]` (kind 2) is this object, `+0x40` if `mt[1]` |
| 0x03 | unit type (+0x80 if unit flag 0x4000) | 71B4 | unit destroyed |
| 0x83 | same | 71B4 | same, when `unit == 0` and `mt[0].kind >= 5` (primary air target) |
| 0x04 | bay index 0..3 | 753F | store released |
| 0x05 | projectile type | 671A (enemy missile detonation near the player, not in a training mission) | player hit by a missile |
| 0x06 | unit type | 62D0 | missile launched at the player |
| 0x08 | 0 | 4926 | end of mission |
| 0x8B / 0x4B | 0 | 7470 (when `mt[j].kind` is 3 or 4) | primary / secondary objective achieved |
| 0x8A / 0x4A | object index | A17E | photo objective achieved |

## 3. Functions

### 0000:671A `wpn_proj_update_all` - void wpn_proj_update_all(void) (V)
> **S3.8:** owned by `flight.md` section 5.3 (verified there; where they differ, flight.md is right).
Owner weapons (index-level for the ballistics, full detail for the impact / messages / bookkeeping). Caller: 39C4 (once per sim step).
Callees: 499E 711F 71B4 72A4 7470 74F6 77D9 81F6 8548 898B 8B93 8BAB BAD1 C94E C987 C9D6 CAEE CB07 CB39 CB62 D6BF F006 F046 F302 F46A.

Entry:
1. Copy 16 bytes `DS:B2A2 -> DS:B290` (save the scratch object record 0).
2. `hud_lamp_set(0, 0)`; `hud_lamp_set(1, 0)`; `hud_lamp_set(8, ([DED6] != 0 && (3D60 & 1)) ? 0x11 : 3)` (`[DED6]` = number of threats this step, reset in 39C4).
3. Aim point of the enemy missiles `(px, py)` = `[39B2] ? ([39AA],[39AC]) : ([C0A4],[C0B2])` (player x,y).
4. `for (i = 0; i < 12; i++) { if (proj[i].life == 0) continue; ... }` with
   `lock = 0` (`[bp-4]`), `bearing = 0` (`[bp-6]`), `type = proj[i].type`, `cls = proto[type].class` (+C).

**Enemy projectile (i < 8), steering input (6F96..70FB):**
* `tz = [2DF4]` (player altitude). `lock = wpn_seeker_check(i, px, py, tz, cls)`; `dist = [94F4]`, `bearing = [94F6]` (valid even when `lock == 0`).
* Countermeasure test `cm`: `cm = ([9B04] & 0x10 && cls > 0)` (chaff vs radar) `|| ([9B04] & 0x20 && cls <= 0)` (flares vs IR) `|| ([9F64] > [3D62] && type != 0x21)`
  (the last term: the AI alert timer `[3D62]` is below `200*rate`, i.e. more than 50 s after the last alert; SA-14 (0x21) is exempt). (V)
  If `cm`: **lose lock** (`bearing = proj.heading; lock = 0`) when `dist > skill*256 + 0x180` (unsigned compare `jb`) **or** `cls == 1` **or** `cls == -1`. (V)
* Decoys: for `j = 1..3` (records at `DS:39AA + 12*j`): let `t = decoy[j].type`;
  * `t == 1` (flare) and `cls <= 0` -> candidate;
  * `t == 2` (chaff) and (`cls == 1` or `cls == 2` or (`cls == 3` and `abs(abs((signed char)((bearing - [2DEE]) >> 8)) - 0x40) < 0x40 - skill*12`)) -> candidate (the cls-3 case is a beam-aspect doppler notch: the bearing is +-90 deg off the player's heading, window width shrinks with skill; for other classes chaff never captures).
  * candidate: `r = wpn_seeker_check(i, decoy[j].x, decoy[j].y, [2DF4], cls)`; if `r != 0`: `bearing = r` (**bug**: the original stores the *return value* 1, not `[94F6]`), `lock = 0` - the missile then flies straight (no steering happens when `lock == 0`). Later decoys can override earlier ones (the loop does not break).
* If `dist > 0x200`: if `proj.aux > 2` and `!(obj[aux].flags & 0x10)` -> `lock = 0`; if `proj.aux <= 0` and `!(unit[-aux].flags & 0x08)` -> `lock = 0` (launcher stopped guiding: the missile continues unguided).
* Acceleration: `if (proto.speed64 >> 6 > proj.speed && (3D60 & 1)) proj.speed++`.
* Jump to the common part (690E).

**Player projectile (i >= 8), target acquisition (678B..68C1):** `best = 0x7FFF`, `tz` unset.
* cls == 7: for each unit `j` in `0..[DED0]-1` with `(flags & 2) && hp != 0`: `r = wpn_seeker_check(i, unit.x, unit.y, unit.z, cls)`;
  if `r != 0 && best > [94F4]` (unsigned): `bearing = [94F6]; best = dist = [94F4]; target = j; tz = unit.z; lock = 1; if (dist < 0x180) unit.flags |= 0x10`.
  (note: the `|= 0x10` is inside the "best" block, so only the nearest candidate that beats the previous best is marked.)
* Acceleration as above (and `bearing = proj.heading` when it accelerated, otherwise bearing stays 0).
* cls in {4,5,6} and `proj.target != -1`: `t = proj.target`; if `t == 0` (scratch object) and `(obj[0].x != proj.tgt_x || obj[0].y != proj.tgt_y)`: `bad1(proj.tgt_x, proj.tgt_y); [B2AA] = 0`.
  `r = wpn_seeker_check(i, obj[t].x, obj[t].y, 0, cls)`; if `r`: `bearing = [94F6]; dist = [94F4]; target = t; tz = 0; lock = 1`.
* type == 0x1C (LGBOMB) and `[3D66] != -1`: same with `obj[[3D66]]`; target = `[3D66]`.

**Common steering and warning (690E..6AB8):**
* Warnings (enemy missiles, `lock != 0`, `i < 8`): if `|[94F6] - proj.heading| < 0x1000` (nose on to the player) and `!([3632] & 1)` and `[39B2] == 0`:
  `cls <= 0 && (3D60 & 2)` -> `hud_lamp_set(1, 0x29)`; `cls != 0 && !(3D60 & 2)` -> `hud_lamp_set(0, 0x11)`;
  if `!(3D60 & 3)` and `(proj.speed << 4) > dist`: `snd_play(9, 1)` (incoming-missile tone).
* If `bearing != 0 && lock != 0`:
  * `dh = bearing - proj.heading`; if `i < 8`: `dh = clamp(dh, (0xFF-skill)<<8 (as short), (skill+1)<<8)`; `dh = clamp(dh, -(proto.turn << 7), proto.turn << 7)`;
    `proj.heading += (dh << 2) / rate` (the `<<2` is a 16-bit shift); `proj.bank = dh << 1`.
  * Target pitch: `if (i < 8 && dist < 0x400) pa = atan2_ang((tz - proj.z) >> 4, abs(dist)); else pa = atan2_ang(((tz - proj.z) >> 5) + (abs(dist) > 0x140 ? abs(dist) >> 3 : 0), abs(dist))` (far targets get a lob offset `dist/8`).
  * `pe = pa - proj.pitch`; if `i < 8`: `pe = clamp(pe, -((skill+1)<<9), (skill+1)<<9)`; `pe = clamp(pe, -((proto.turn & 0xFF) << 8), (proto.turn & 0xFF) << 8)`; `proj.pitch += (pe << 2) / rate`.
* else (no steering): `if (proj.pitch > 0 && type != 0x1E) proj.pitch -= (sign(proj.pitch) << 11) / rate`.
* Pitch limits: `type == 0x1C && proj.pitch > (short)0xF800` -> `proj.pitch = 0xF800` (LGBOMB never flatter than -11.25 deg). `type == 0x1E || proj.z == 1`: `proj.pitch -= 0x800 / rate; if (proj.aux > proj.pitch) proj.pitch = proj.aux` (free-fall bomb gravity arc down to its dive floor).

**Move (6B2C..6B86):**
```
h  = cos_mul(proj.pitch, proj.speed) * 4 / rate          (16-bit *4)
proj.x += sin_mul(proj.heading, h);  proj.y -= cos_mul(proj.heading, h);
proj.z += sin_mul(proj.pitch, (proj.speed << 7) / rate)
proj.life--;   [C686] = 0
if (((i ^ [3D60]) & 3) == 0) wpn_ground_test(x, y, z);     // 4-step phase per slot, may set [C686]
```

**Impact (6BA8): `if (z < 0 (signed) || [C686] != 0)`:**
* explosion position `[C0A0]=x, [C0AA]=y, [C0B0]=z`, `[9D0C] = 0xFFFA`, `proj.life = 0`, `view_label_draw()`, `strcpy(9876, store.long_name)`.
* **not** a bomb (type not 0x1C/0x1D/0x1E): enemy (i<8): nothing more. Player missile (i >= 8): `strcat(9876, " ground impact"); msg_show(9876)`. (This is the path for AIM-9/120, HARM, Penguin, Harpoon, AGM-65 and **also type 0x26 Equip.**: see quirk Q1.)
* bomb (0x1C/0x1D/0x1E):
  1. `snd_play(5, 2)`; `strcat(9876, " misses ")`; `o = bad1([C0A0], [C0AA])`.
  2. If `o != -1` and `!(obj[o].flags & 0x80)`: `wo = [9F10]` (world object); object centre `ox = wo.+4 >> 5`, `oy = 0x8000 - (wo.+8 >> 5)`; `miss = hypot_approx(C0A0 - ox, C0AA - oy)`;
     `if ((wpn_effect(proj.store, o) << 6) / (skill + 2) > miss)` (unsigned compare, `jbe` skips): **hit**: `dmg_object_destroy(o)` (it builds the object name in 9876), `strcat(9876, " destroyed by ")`, `strcat(9876, store.long_name)`, `[9D0C] = 0x10`, `[C0B0] = 0`.
  3. `msg_show(9876)`: on a miss this is `"<store long name> misses "` (note the trailing blank), on a hit `"<object name> destroyed by <store long name>"`
     (the object name can be `"Primary target"` / `"Second. target"` because 7470 overwrites 9876, see 7470).
  4. Player-blast check: `d = hypot_approx(C0A4 - proj.x, C0B2 - proj.y) + ([2DF4] >> 5)`; if `(skill << 5) > d` (unsigned): `dmg_player_blast()` and `strcpy(9876, "Blast damage")` - **no `msg_show` is called on this path**, so the text is never displayed (the miss message of step 3 was already shown; U whether anything reads 9876 later - nothing in VGAME does).
  (Quirk Q1: the original also contains an "Equipment Delivered / Bad Drop" block (6D28) reached only from inside the bomb branch with `type == 0x26`, which can never be true there - unreachable code.)
  The Equipment block, for completeness: for `k = 0..1`: `o = mt[k].obj`; `d = hypot_approx(proj.x - obj[o].x, proj.y - obj[o].y)`; if `d < 0x100 && mt[k].kind == 3 && [DE88] > [98E2]` -> `msg_show("Equipment Delivered"); mission_target_done(k); done = 1`. After the loop `if (!done) msg_show("Bad Drop")`.

**Proximity / direct hit (6DAF..6F35), executed for every live projectile after the impact code (also in the step in which the projectile impacted):**
`slant = (abs(tz - proj.z) >> 5) + dist`. `if (((proj.speed << 3) / rate) > slant && lock)`:
* **i < 8 (enemy missile detonates near the player)**: `if (proj.life > 4*rate)`: explosion at the missile position (`C0A0/C0AA/C0B0`), `[9D0C] = 0x10`, `proj.life = 2*rate`; if `[39B2] == 0`: `strcpy(9876, "Hit by ")`, `strcat(9876, proto.name)`, `msg_show(9876)`, `dmg_player_blast()`, and unless `[9B05] & 0x10` (training): `log_event(5, type)`. (The whole block runs once because `life` drops to <= 4*rate; with `[39B2] != 0` the explosion happens but no damage/message.)
* **i >= 8, cls == 7 (air-to-air)**: unit `u = target`: `proj.x/y/z = [C0A0]/[C0AA]/[C0B0] = unit.x/y/z`, `[9D0C] = 0x10`, `proj.life = 2*rate`, `dmg_unit_kill(u)` (leaves the unit name in 9876), then text `" hit by "` + `proto.name` appended, `msg_show` -> `"MIG-29 hit by AIM120"`. **No probability roll: a proximity burst always kills.**
* **i >= 8, cls 4/5/6/other (ground)**: `o = target` (maybe 0): `view_label_draw()`; explosion at `(obj[o].x, obj[o].y, 0)`, `proj.life = 0`, `[9D0C]=0x10`; `r = rand_n(4)`; `if (wpn_effect(proj.store, o) > r) dmg_object_destroy(o) else strcpy(9876, "Ineffective")`; `[3D62] = [B286]`; `[B2A0] = C0A0`; `[B75A] = C0AA`; `[B76E] = 3000` (AI "last explosion" alert); then (also for unit kills) `strcat(9876, " hit by ")`, `strcat(9876, proto.name)`, `msg_show` -> `"Primary target hit by HARM"`, `"Ineffective hit by AGM-65"`.
After the block: `if (i < 8 && life != 0 && (3D60 & 1)) map_plot(x, y, 0x11)` (enemy missile blip, unreachable for dead slots). Loop end: restore the 16 bytes `DS:B290 -> DS:B2A2`.

### 0000:711F `wpn_seeker_check` - int wpn_seeker_check(int slot, int tx, int ty, int tz, int cls) (V)
> **S3.8:** owned by `flight.md` section 5.4 (verified there; where they differ, flight.md is right).
Args: slot `[bp+4]`, tx `[bp+6]`, ty `[bp+8]`, tz `[bp+10]` (**unused**), cls `[bp+12]`. Returns 1 if the projectile can see the point.
```
dx = tx - p.x; dy = ty - p.y;  dist = hypot_approx(dx, dy);
[94F6] = bearing = atan2_ang(dx, -dy);
a = abs(bearing - p.heading);                    // abs of 0x8000 stays negative -> counts as inside
if (a > 0x1000 && cls != 3) {                    // outside the 22.5 deg cone (all-aspect cls 3 skips this)
    if (a > 0x6000 && slot < 8 && p.speed < dist) p.life = 0;     // enemy missile flew past: self-destruct
    return 0;
}
if (cls == 0 && abs(p.heading - [2DEE]) > 0x2000) return 0;       // rear-aspect IR: must be within 45 deg of the player's heading
[94F4] = dist; return 1;
```
Side effects: `[94F6]` always set, `[94F4]` only on success, `p.life` possibly 0. Caller: 671A.

### 0000:71B4 `dmg_unit_kill` - void dmg_unit_kill(int u) (V)
> **S3.8:** owned by `flight.md` section 6.2 (verified there; where they differ, flight.md is right). The field called "hp" (+0x1A) is the unit speed.
Marks unit `u` destroyed (called by 671A, A17E). Owner damage.
```
[98FE] = u;                                   // last killed unit
t = unit[u].type; f = unit[u].flags;
if (!(f & 0x20)) {
    unit_type[t].kills++;                      // DS:2FE0 + 32*t (dead data)
    if (f & 0x800) [DECE]--;
    if ([3D6E] == u) [3D6E] = -1;
    unit[u].flags |= 0x20;   [3D68] = -1;
    [C0A6] = unit.x; [C0B8] = unit.y; [C602] = unit.z;  [B756] = 0x80;     // wreck / explosion position
    code = 3;
    if ([E2D8] >= 5 && u == 0) { mission_target_done(0); code = 0x83; }       // primary air target
    log_event(code, t + ((unit.flags & 0x4000) ? 0x80 : 0));
    if (unit[u].hp == 0) unit[u].flags &= 0x1C1;                       // hp==0 units are reset instead (quirk, U)
}
strcpy(9876, unit_type[unit[u].type].name);                            // DS:2FC2 + 32*type
snd_play(5, 2);
if ([E2FE] == 1 && [3D64] == u) [9D0E] = 1;                           // selected air target gone
```
(`mission_target_done` overwrites 9876 with "Primary target" but the `strcpy` above restores the unit name afterwards. A second kill of an already dead unit only repeats the strcpy / sound.)

### 0000:72A4 `dmg_object_destroy` - void dmg_object_destroy(int o) (V)
> **S3.8:** owned by `flight.md` section 6.2 (verified there; where they differ, flight.md is right).
Destroys ground object `o` (called by 671A, A17E). Owner damage; this is the core of the mission-objective bookkeeping.
```
obj_name_build(o);                                   // 9876 = "<type> at <place>"
code = 1;
if (!(obj[o].flags & 0x80)) {
    if (obj[o].flags & 0x1000) [DED2]--;
    w = wobj_find((long)obj[o].x << 5, (long)(0x8000 - obj[o].y) << 5);    // [9F10]
    if (o == 0) {                                    // unregistered world object (scratch record)
        if (!dmg_wobj_destroyable(0)) return;        // already a wreck: nothing happens at all (no sound, no map refresh)
        DS:C0BA[w.shape]++;                          // kills per world-object type (byte table, 128 entries)
        log_event(2, w.shape /* word at [9F10] */);
        v = shape_map(obj[0].+0E);
        if (DS:[A60 + (v>>8)*2][v & 0xFF] & 0x20) new = obj[0].+0E;
        else new = (obj_cls_soft(0) ? (signed char)[C0AC] : (signed char)[DEA4]);
        new |= 0x100;  obj[0].+0E = new;
    } else {                                         // registered object
        if (obj[o].+6 == 0) code = 0x0C;
        obj[o].flags |= 0x80;  obj[o].+6 = 0;
        for (k = 0; k < 2; k++) {
            if (mt[k].kind != 2 || mt[k].obj != o) continue;
            mission_target_done(k);
            code |= (k == 0) ? 0x80 : 0x40;
            snd_play(4, 2);
            if (k == 0) [DED8] = rate*4; else [9D0A] = rate*4;
        }
        log_event(code, o);
        new = obj_wreck_shape(o);
    }
    if (w) wobj_set_shape(w, new);                   // replace the model
}
[3D68] = o;                                          // (also on the already-destroyed path)
snd_play(5, 2);
if ([E2FE] == 2 && [3D66] == o) [9D0E] = 1;          // designated target destroyed
if ([DEDA] == 0) map_refresh([C0A4], [C0B2]);
if (skill < 2) ai_alert_reset();
```
Notes: (a) the *name* in 9876 is the object's own text; `mission_target_done` replaces it by "Primary target"/"Second. target" when `o` is a mission target (the caller 671A appends `" destroyed by <weapon>"` or `" hit by <weapon>"`); (b) per matching target `k`: `k == 0` sets `[DED8]`, `k == 1` sets `[9D0A]`, both `rate*4`; (c) already destroyed objects (flag 0x80) skip everything except `[3D68]`, sound 5, the target-destroyed flag and the map refresh; (d) the 0x80 flag + `+6 = 0` + wreck shape are what END reads from the blob; (e) `ded2` bit 0x1000 objects are counted objectives (e.g. SAM sites still alive) - the counter is read by 516D (AI).

### 0000:7470 `mission_target_done` - int mission_target_done(int j) (V)
> **S3.8:** owned by `flight.md` section 6.2 (verified there; where they differ, flight.md is right).
Marks mission target `j` (0 primary, 1 secondary) achieved. Callers: 39C4 (kind 4 landing), 671A, 71B4, 72A4, A17E (photographs).
```
bit = 0x4000 >> j;  if ([9B04] & bit) return 0;           // already done -> no new message, no log
if (mt[j].kind == 4 || mt[j].kind == 3) log_event(0x0B | (j == 0 ? 0x80 : 0x40), 0);   // 0x8B / 0x4B
if (j != 0) { strcpy(9876, "Second. target"); [2E7C] = 1; [9B05] |= 0x20; }
else        { strcpy(9876, "Primary target"); [2E7C] = 2; [9B05] |= 0x40; }
if (([9B05] & 0x60) == 0x60) [2E7C] = 3;
return 1;
```
It does not display anything itself; the caller shows 9876. For kinds 1/2/5..8 the achievement is logged by the caller (72A4: `0x81/0x41/0x8C/0x4C` + object; 71B4: `0x83`; A17E: `0x8A/0x4A`). END derives the objective result from these log codes and from the saved object/mt records (the `[9B04]` bits are not part of the blob, U).

### 0000:74F6 `dmg_player_blast` - void dmg_player_blast(void) (V)
> **S3.8:** owned by `flight.md` section 6.1 (verified there; where they differ, flight.md is right).
Random subsystem damage to the player from a nearby blast.
```
if ([9B05] & 0x10) return;                       // training mission: invulnerable
for (n = 0; n <= skill; n++) { b = rand_n(8); [3632] |= 1 << b; [C5C8]++; }   // skill+1 random hits (bit 7 has no lamp)
lamps_refresh(0x16);                             // 829D(0x16): lamp 10+k = (3632 >> k & 1) ? 0x29 : 0x0A for k = 0..6; then if [DFDC]==0x16 8113(0x16)
[98EE] = 1;                                      // "damaged" flag read/cleared by 7898 (HUD)
snd_play(4, 2);
```
Called by 671A (bomb too close, enemy missile proximity) and A17E (gunfire hits).

### 0000:753F `wpn_release` - void wpn_release(void) (V)
> **S3.8:** owned by `flight.md` section 5.2 (verified there; where they differ, flight.md is right).
Fire key handler body (called by CBA0). Releases the currently selected store.
```
if (abs([2DF2]) > 0x3000) return;                        // roll limit (about 67 deg)
b = [364E];                                              // selected bay 0..3
if (bay[b].qty == 0) return;                             // bay[b] = DS:363E + 4*b {short store_id; short qty}
store = bay[b].store_id;  type = store_tab[store].proj_type;     // DS:3678 + 0x1A*store
if (type == 0 || type == -1) return;                     // guns / camera are not handled here
if (!([9B04] & 4) && type != -2) { snd_cmd(0x20) /*direct*/; msg_show("Bay closed"); return; }
if ([2DF6] == 0 || (obj[[DFE0]].flags_hi & 2 && [2DF6] == 0x80)) { snd_cmd(0x20); msg_show("Insufficient Altitude"); return; }
bay[b].qty--;
if (type == -2) {                                        // extra fuel tank
    [3634] += 0x76C;  msg_show("Fuel transfer");
    if ([DFDC] == 0x14) fuel_panel_draw();               // 9208
    return;
}
log_event(4, b);  sim_accel_cancel();                    // D7BB: if [3D7A]==2 { [3D7A]=1; [3660] <<= 1; d707(); }
s = -1; for (k = 8; k < 12; k++) if (proj[k].life == 0) s = k;          // LAST free slot of 8..11 wins
if (s != -1) {
    proj[s].x = [C0A4]; .y = [C0B2]; .z = [2DF4] - 0x14; .speed = [B052] >> 10;
    .heading = [2DEE]; .pitch = [2DF0]; .bank = [2DF2];
    proj[s].life = ((range*rate) << 5) / ((speed64 >> 6) + 1) + 6    // 32-bit divide, low word; (range, speed64) from proto[type]
    if (proj[s].life <= 6) proj[s].life = 999;                        // 0x3E7
    proj[s].type = type; .store = store; .target = .tgt_x = .tgt_y = -1;
    if (type == 0x1B) [43B4] = 0;
    if (type in {0x1E, 0x1D, 0x1C, 0x26}) proj[s].speed >>= 1;
    if (type == 0x1E) proj[s].aux = bbed() - 0x400;                   // dive floor; bbed = (((0x4000-|[2DF0]|) << 12) / (([2DF4]+0x1000) & 0xFFFF)) - 0x4000
    if ([3D66] >= 0) {                                                // a designated ground object
        if ([E55C] != 0) {
            c = proto[type].class;
            if (c == 6 || (c == 4 && obj[[3D66]].+6 != 0) || (c == 5 && obj_cls_ship([3D66]))) {
                proj[s].target = [3D66]; proj[s].tgt_x = obj.x; proj[s].tgt_y = obj.y;      // lock
            }
        } else proj[s].target = -1;
    }
    if (type == 0x1D) { proj[s].pitch = 0xC000; proj[s].speed = 1; }  // retarded bomb drops straight down
    [B04C] = s;
    strcpy(9876, store.long_name); strcat(9876, " fired"); msg_show(9876);
    snd_play(0x0D + (proto[type].range == 0 ? 3 : 0), 2);           // missiles: cmd 13, bombs (range 0): cmd 16
}
if ([DFDC] == 0x15) lamps_refresh(0x15);                              // 829D(0x15) -> 8113(0x15) when current panel is 0x15
```
Notes: the quantity is decremented and the event 4 logged **before** the free-slot search, so with no free slot the weapon is lost silently (V). The bays (`DS:363E`) are copied at mission start by 39C4 (3B25..3B5A): `bay[i].store_id = comm[0x38 + 2*i]`, `bay[i].qty = comm[0x40 + 2*i]` for i = 0..3 (`comm` = far ptr `[E548]`; START writes id 0x13 = empty/Guns and qty 0 for unused bays, flight_model 1 = only 2 bays). `[364E]` is the selected bay (changed by CBA0; 39C4 resets it to 0). There are no separate pylons: the 4 (or 2) bays are the only stores stations.

### 0000:780A `dmg_wobj_destroyable` - int dmg_wobj_destroyable(int o) (V)
> **S3.8:** owned by `flight.md` section 6.2 (verified there; where they differ, flight.md is right).
Whether the world object last found (`[9F10]`) can still be turned into a wreck: `wreck = obj_cls_soft(o) ? (signed char)[C0AC] : (signed char)[DEA4]`;
`if (wobj.+0 == wreck) return 0; p = wobj.+0C; if ((p.byte6 & 0x80) && (p.byte6 & 0x7F) == wobj.+0) return 0; return 1;` Callers: 72A4 (o == 0 path), A17E.

### 0000:784A `cockpit_widgets_reset` - void cockpit_widgets_reset(void) (V)
Called once by 4857 (mission start, right after `srand(bios timer)`): `8A4E(2, 0)`; `d83e([3FE2], 0x16, 1)`, `d83e([3FE2], 0x1D, 1)`, `d83e([3FE2], 0, 1)`, `d83e([4012], 0, 1)` (reset the initial state of four panel widgets, R8's range: d83e = widget blit); `[409C] = 0`; `[DFDC] = 0x18` (current MFD/panel id after start). Owner simflow.

## 4. Message texts (verbatim) and when they are queued

All messages go through `msg_show` (one string slot `DS:C578`, a new message replaces the old one, 3 s). Where a text is built in `DS:9876` the final text is the concatenation shown.

| DS addr | text | built / shown by |
|--|--|--|
| 3E24 | `" fired by "` | 62D0: `<missile name> fired by <unit name>` (outside R6) |
| 3E40 | `" misses "` | 671A bomb impact: `<store long name> misses ` (trailing blank) |
| 3E49 | `" destroyed by "` | 671A bomb hit: `<object name> destroyed by <store long name>` |
| 3E58 | `"Blast damage"` | 671A, written to 9876 but **never shown** (no msg_show on that path) |
| 3E65 | `"Equipment Delivered"` | 671A, unreachable block (Q1) |
| 3E79 | `"Bad Drop"` | 671A, unreachable block (Q1) |
| 3E82 | `" ground impact"` | 671A player missile (or Equip.) hits the ground: `<store long name> ground impact` |
| 3E91 | `"Hit by "` | 671A enemy missile proximity: `Hit by <proto name>` |
| 3E99 | `"Ineffective"` | 671A ground missile failed the effect roll (then `" hit by <proto name>"` is appended) |
| 3EA5 | `" hit by "` | 671A air-to-air kill (`<unit name> hit by AIM120`) and ground missile result (`<object name> hit by HARM`) |
| 3EAE | `"Second. target"` | 7470 (j != 0), placed in 9876; part of `Second. target destroyed by/hit by ...` |
| 3EBD | `"Primary target"` | 7470 (j == 0) likewise |
| 3ECC | `"Bay closed"` | 753F (with snd_cmd 0x20) |
| 3ED7 | `"Insufficient Altitude"` | 753F (with snd_cmd 0x20) |
| 3EED | `"Fuel transfer"` | 753F |
| 3EFB | `" fired"` | 753F: `<store long name> fired` (e.g. `Sidewinder fired`) |
| 3DD4 | `" at "` | 49E7 object name |

## 5. How the START loadout becomes in-flight stores (end to end)

1. START writes `comm+38h + 2i` = weapon id (0..19; 0x13 = empty) and `comm+40h + 2i` = quantity, i = 0..3 (shell.md section 4); pilot record `comm+1272h`: `+3Eh` skill, `+3Ch` mission type (bit1 = training), `+42h` flight model (bays), `+3Ah` war level.
2. 39C4 start-up (3AF1..3B5A): `[3656] = pilot.+3A; [3654] = pilot.+3E; [98DE] = comm.+32; if (pilot.+3C & 2) [9B05] |= 0x10; for i in 0..3 { DS:363E[4i] = comm[38+2i]; DS:3640[4i] = comm[40+2i] }`.
3. Selecting a bay: CBA0 (`[364E]`); the HUD (7898) prints the selected store's names/quantity from `DS:3678`/`363E`/`3640` (R8).
4. Fire key -> 753F (section 3) -> projectile slot 8..11 -> 671A each step -> impact/proximity -> 72A4/71B4/7470 -> messages, log, flags.
5. At the end 4926 writes the reason and counters, 4C29 writes the blob (objects incl. flags 0x80, `C0BA[128]` kills per object type, units with flags, `mt[]`, the 0x600-byte log) after `comm+0x7A`; END reads it.

## 6. Quirks and original bugs (for a faithful port)

* **Q1** The "Equipment Delivered"/"Bad Drop" code in 671A is unreachable (`type == 0x26` tested inside the branch that already requires type 0x1C/0x1D/0x1E). A dropped equipment container (store 18, proj type 0x26, speed halved, no gravity, life 999) just flies on and ends with `"Equip ground impact"`. mt kind 3 ("Drop supplies over") therefore can only be completed by A17E/another path (U).
* **Q2** Decoy capture stores the return value `1` into the bearing variable (harmless: lock is cleared too).
* **Q3** `abs(0x8000)` stays negative, so a target exactly 180 deg behind passes the 22.5 deg cone test in 711F.
* **Q4** 753F: quantity is consumed and event 4 logged even when no free slot exists; the last free slot of 8..11 is used.
* **Q5** Blast damage text is stored in 9876 but not displayed; blast damage happens only when `skill*32 > distance` (skill 0 never).
* **Q6** After a bomb impact the proximity block can run in the same step (LGBOMB with lock): the object may be processed twice (`72A4` again; second pass is a no-op apart from messages).
* **Q7** All turn/pitch arithmetic is 16-bit (`dh << 2`, `speed << 3/4`); with the table values no overflow occurs, but keep 16-bit semantics.
* **Q8** `71B4` on a unit with `hp == 0` clears the dead flag again (`flags &= 0x1C1`).
* **Q9** `view_label_draw()` (81F6) overwrites `DS:9876`; 671A calls it before every `strcpy(9876, ...)`, so the order (81F6 first, then the strcpy) must be kept in a port that shares the buffer.

## Open items

| item | note |
|--|--|
| `[2DF6]` meaning | gates the release ("Insufficient Altitude"): updated by 1E9E with limits 0xF230/0xEA60/0x2000 and the terrain height `[C0A2]`, so it is the aircraft altitude word; 0 = on the ground, 0x80 at the airfield test. (U exact units) |
| class table `DS:C604` values | which nibble means what (8 radar site, 12/13 ships known from effect table + bcac) - owner of 4B57. |
| `[9D0C]` semantics | -6 / 0x10: explosion size or phase, consumed by A17E/BF71 (U). |
| lamp meanings 0, 1, 8 | 898B/HUD owner. |
| `[39B2]` / decoy record 0 | what activates the alias (ejection? external view?) - 4512/461C. |
| wreck shape ids `[C0AC]`,`[DEA4]` | loaded by the world loader; meaning of the 0x20 flag in the A60 tables. |
| `[3D62]`, `[9F64]`, `[B286]` | AI alert timer logic is in 39C4/5730. |
| DS:3372 (+10 u) and DS:367A (+18 u) | consumers 9ACA / 02FE. |
| Equipment drop path (kind 3) | see Q1. |
| `unit[+0]` word, `unit[+1E]` meaning | 39C4 / 62D0 owners. |

## DGROUP globals seen

| offset | size | name | meaning | readers / writers |
|--|--|--|--|--|
| 3362 | 40x18 | wpn_proto_tab | projectile prototype table (2.1) | 671A 753F (reads) |
| 3662 | 20x26 | wpn_store_tab | store table (2.2), proj type at +16 = DS:3678 | 753F 7898 A17E 02FE |
| 386A | 20x16 | wpn_effect_tab | effectiveness table (2.3) | 8BAB |
| 3C0C | 12x28 | proj_tab | projectiles (2.4) | 671A 753F 62D0 (516D) 2F82 8C10 9ACA A17E BF71 |
| 363E | 4x4 | bay_tab | `{store_id, qty}` per bay | 39C4 (init) 753F 48DD 7898 9678 A17E |
| 364E | 2 | bay_sel | selected bay | 39C4 CBA0 753F 7898 |
| 3654 | 2 | skill | enemy skill 0..3 (pilot+3E) | many |
| 3656 | 2 | war_level | pilot+3A | 39C4 559A |
| 3660 | 2 | rate | sim steps per second | many |
| 3D60 | 2 | step_counter | sim step counter | 39C4 4857 671A ... |
| 3632 | 2 | dmg_mask | damaged-subsystem bitmask | 74F6 829D 7898 4926 |
| C5C8 | 2 | hit_count | hits taken | 74F6 4926 |
| 9B04/9B05 | 2 | mission_flags | see 2.8 | 39C4 7470 753F 671A |
| 2E7C | 2 | obj_done | 1 sec / 2 prim / 3 both | 7470 7898 9208 94A5 CBA0 |
| B2A2 | 16 x [C0B4] | obj_tab | ground objects (2.6) | 72A4 671A BAD1 8387 A17E 4C29 |
| C0B4 | 2 | obj_count | ground object count (incl. record 0) | 671A BAD1 8387 |
| C13E | 0x24 x [DED0] | unit_tab | units (2.5) | 671A 71B4 62D0 5730 A17E |
| DED0 | 2 | unit_count | | 671A |
| 2FC2 | 29 x 32 | unit_type_tab | unit type names + kill counter | 71B4 |
| E2D8 | 2 x 0x12 | mt_tab | mission targets (2.7) | 671A 71B4 72A4 7470 97A4 39C4 |
| C0BA | 128 | kill_by_wobj_type | kills per world-object type (byte) | 72A4 4C29 |
| BA2A | 0x600 | event_log | flight log, 256 x 6 | 499E 4C29 |
| 94EA | 2 | event_count | | 499E |
| 9876 | 0x20+ | msg_scratch | scratch text | many |
| C578 | | msg_text | current HUD message | 8B93 7898 |
| 409E | 2 | msg_timer | | 8B93 7898 |
| 3D64 3D66 3D68 3D6E | 2 each | sel_air, sel_gnd, last_dead, lock_unit | target selection | 2F82 5730 71B4 72A4 A17E CBA0 |
| E2FE | 2 | master_mode | 1 A/A, 2 A/G | 71B4 72A4 CBA0 39C4 |
| 9D0E | 2 | target_destroyed_flag | | 71B4 72A4 2F82 BF71 |
| 9D0C | 2 | explosion_kind | | 671A A17E BF71 1E9E 5730 |
| 9D0A DED8 | 2 | tgt_timers | | 72A4 39C4 |
| 3D62 9F64 B286 | 2 | alert timers | | 559A 671A d707 39C4 |
| 94F4 94F6 | 2 | seeker_dist / seeker_bearing | | 711F 671A |
| C0A0 C0AA C0B0 | 2 | boom_x/y/z | | 671A |
| C0A6 C0B8 C602 B756 | 2 | unit_boom_x/y/z, size | | 71B4 |
| B04C | 2 | last_proj_slot | | 753F |
| 39AA | 12 x 4 | decoy_tab | decoys (2.8) | 671A 4512 559A 5730 8C10 |
| 9F10 | 2 | wobj_ptr | last world object found | 72A4 BAD1 780A BC12 |
| C686 | 1 | ground_hit_flag | | 671A 0FFB 2F82 |
| 2DF2 / 2DF4 / 2DF6 | 2 | roll / z / altitude | | 753F 671A |
| 3634 | 2 | fuel | lbs | 753F 39C4 |
| DE88 98E2 | 2 | deadline / mission_seconds | | 671A 39C4 |

## Structures seen

Section 2.4 (projectile, 0x1C, 12 at DS:3C0C), 2.5 (unit, 0x24, DS:C13E, `[DED0]`), 2.6 (ground object, 0x10, DS:B2A2, `[C0B4]`), 2.7 (mission target, 0x12, 2 at DS:E2D8),
2.1 (proto, 0x12, 40), 2.2 (store, 0x1A, 20), 2.3 (effect 20 x 16), log record (6 bytes), bay (4 bytes), decoy (12 bytes). Allocation: all static DGROUP; objects/units/mission targets are loaded by the mission/world loader, the others are initialised data.



---
# Part 7: HUD overlay, MFD pages, objective text (R7)

*Reader pass R7, original title: S3.6 spec, reader R7: HUD overlay (C part), MFD pages, text/number helpers, objective text*

Scope: 0000:4a73, 7898, 80a0, 8113, 81f6, 829d, 82e9, 8498, 84d6, 873f, 88a8, 88f6, 89fe, 8a4e, 8a5e, 8aa2, 8ad3, 8b04, 8b3b,
8b5c, 8b93, 8bab, 8c10, 903f, 90b9, 9154, 9208, 950e, 955c, 95c4, 9641, 9678, 97a4, 99a7 (34 functions, all VGAME seg 0000).
Method: annotated disassembly (`tools/dsann.py`, files work/s36/ann_R7_*.txt), data dumped from `work/unpacked/VGAME.EXE`
(helper `work/s36/ds7.py`). `(V)` = read in the disassembly, `(U)` = meaning inferred / needs a runtime or visual check.
Calls into functions owned by other readers are cited by address with the semantics I verified (8929, 893f, 898b, 8888, 87a9,
d83e, c94e, c987, c9d6, caee, cb07, cb39, f046/f006/f078/f094/f0b0/f2b8, 91a4, 8387, 8548, 973c, 9770, 94a5, 4512, 74f6, 7470).

## 0. Overview

Everything here runs once per frame from `1bbc` (`2f82` 3D view -> **`7898`** -> asm HUD `10a8:0000` -> `flip` -> `1e9e` keys -> `39c4`
sim step) or on demand from the key handler `cba0` (called by `1e9e`/`39c4` with the key code, and every frame with 0 = poll).

**Screen layout of the cockpit (320x200, panel present = `[365E]==1`, which `2f82` sets to `((C07A & 0xC0) == 0)`):**

| Rect (x0,y0)-(x1,y1) incl. | What |
|---|---|
| (0,0)-(319,90) | 3D view + HUD + overlay text (`ctx0/ctx1.ymax = 0x5A`; without panel `0xC7`) |
| (0,91)-(319,106) | 16-row strip above the MFDs; flipped only when `[2DFC]!=0` (Alt-C toggles it), then `[2E34]=0x6B` rows are flipped instead of `0x5B`; views without panel flip 0xC8 / 0xA1 / 0x9D rows (set at the end of 2f82) |
| (74,110)-(150,169) 77x60 | **left MFD**: `[DEDA]` 0 = moving map, 1 = radar/situation scope |
| (169,110)-(245,169) 77x60 | **right MFD**: `[DFDC]` 0x13 TV/camera, 0x14 navigation, 0x15 weapons, 0x18 objectives |
| (80,173) (100,173) 18x8 | left MFD mode lamps/buttons (lamp 22 = map, 23 = scope) |
| (170,181) (190,181) (210,181) (170,191) (190,191) | right MFD page lamps 28 (nav), 26 (weapons), 29 (objectives), 30 (TV), 27 = TV label box |
| (119,94) (139,94) (159,94) 18x8 | lamps 0,1,2 = `[E2FE]` target-mode indicator |

**Three text/line contexts** (a `TextCtx` of video.md section 3 is 0x16 bytes; VGAME appends a near self pointer, so each takes 0x18 bytes):
`ctx0` at DS:3FCC (page 0 = A000), `ctx1` at 3FE4 (page 1 = back buffer), `ctx2` at 3FFC (page 2 = pristine panel copy). The words
`DS:3FE2`, `DS:3FFA`, `DS:4012` hold the near pointers to them (`ctx0 = [3FE2]`, `ctx1 = [3FFA]`, `ctx2 = [4012]`; V initial image:
page 0/1/2, flags 2, fg 0x2F, bg 0, font 1 for 0/1 and 0 for ctx2; clip (xmin,xmax,ymin,ymax) = (0,319,0,90) for ctx0/ctx1 (ymax
rewritten by `2f82` to 90 or 199) and (0,319,0,199) for ctx2).
`[4088]` (byte) = `flip_flag()` (slot 45) sampled at the top of `7898` and by `87a9`: 1 = flipping enabled (always 1 in practice).
**Convention used by all text helpers: `cur_ctx = [4088] ? ctx1 : ctx0`**, i.e. HUD text and MFD text are drawn on the back buffer
page 1 (which `flip` copies to A000 for the flipped rows). The MFD boxes are not in the flipped rows; instead `2f82` (V,
2f82 at 378d..37ca) copies **both MFD rectangles from page `[4088]` to page `1-[4088]`** (blit_rect(1,74,110 -> 0,74,110,77,60) and the
same at x=169) **every frame before `7898`**, so MFD content drawn once on page 1 persists and reaches A000; the scope (`8c10`) is
repainted into page 1 every frame after that copy (one-frame latency). Lamps (`d83e`) are recoloured directly on page 0 (A000) and, for
lamps 0..3 and 4, also on page 2.
The 3D view is drawn with the draw segment on page 1 and `87a9` can also draw the same line on the other page (flag).

Palette (flight palette A, 6-bit RGB, from DS:164A; B/C night variants are the same for these indices except 44/45/159):
0 black, 1 blue, 3 cyan, 7 light grey (42,42,42), 8 dark grey, 0x0A light green (21,63,21), 0x0B light cyan, 0x0D light red, 0x0F white,
0x11 yellow (60,60,20), 0x29 red (51,0,0), 0x2A dark red (36,0,0), 0x2C..0x2F greens (0,16,0) (0,18,0) (0,27,0) (0,38,0), 0x98..0x9F
dark blue-grey MFD ramp (0x9F = (5,7,10)).

HUD colours: `[2CA0]` (byte) = HUD primary colour, `[2CA1]` = HUD secondary; written by `10a8:09ce` from `[43B0]` (F4 toggle):
`[43B0]!=0` -> 0x0A / 0x2F, else 0x0F / 0x07 (V).

### Key to MFD mapping (V, from the `cba0` dispatch table; key = (scan<<8)|ascii, cross-check with R2/R10 when their specs exist)

| Key | Code | Effect |
|---|---|---|
| F2 | 3C00 | `[E2FE]` 0->1->2->0 (target mode); entering 1 sets `[3D64]=[3D66]=-1`, entering 2 sets `[3D66]=-1` (V) |
| F3 | 3D00 | `80a0(DEDA==1 ? 0 : 1)` toggles the left MFD map/scope |
| F4 | 3E00 | `[43B0]^=1; 01bf()` HUD colour set toggle |
| F5 | 3F00 | `8113(0x15)` weapons page |
| F6 | 4000 | `[43B2]^=1`, lamp 25 (0x19) on ctx0 follows `[43B2]` (TV option) |
| F7 | 4100 | `[B80C]=1; 8113(0x14)` navigation page, waypoint **edit** mode |
| F8 | 4200 | `[B80C]=0; [2E7E]=[2E7C]; 8113(0x14)` navigation page, normal mode |
| F9 | 4300 | `[9B05]^=4` (ILS overlay `99a7`) |
| F10 | 4400 | `8113(0x18)` objectives page |
| z / x | 2C7A / 2D78 | `8498()` / `84d6()` zoom in/out of the active left MFD (or camera range when `C07A&0x80`) |
| m , . / | 326D 332C 342E 352F | TV camera direction `[E556]` = 0xC000 (Left), 0x4000 (Right), 0x8000 (Rear), 0 (Ahead) and `8113(0x13)` (only when `[9B05]&2==0`); also `[43B4]=1,[9D0E]=0,[3D64]=[3D66]=-1` |
| M < > ? | 324D 333C 343E 353F | view `[C07A]` = 0x42 / 0x43 / 0x41 / 0x44 (external views, no panel) |
| v | 2F76 | `[294A]` 0->1->2->0 (HUD style) |
| c | 2E63 | `[294B]^=1` (HUD "small view" mode; handled in `1e9e`) |
| n | 316E | `[9B05]^=1` (target acquire request) |
| Alt-T | 1400 | `[9B05]^=0x10` training/cheat flag, sets comm+30h bit 0 |
| Alt-C | 2E00 | `[2DFC]^=1` panel strip |
| Space (weapon select) | 3920 | `[364E]` next weapon slot (`^=1` if pilot+42, else `(+1)&3`), redraw `81f6` if `[9B05]&2`, `9678` if `[DFDC]==0x15` |

## 1. Text, number and drawing helpers

### 0000:8aa2 `hud_text(char *s, int x, int y, int col)` (V) and 0000:8ad3 `hud_text2` (identical copy)
`ctx = [4088]==0 ? [3FE2] : [3FFA]; txt_draw(ctx, s, x, y, col)`. 8ad3 is byte-for-byte the same logic (second copy; callers: the 7898 message line, 99a7 "ILS", 9678 weapon names/quantities).

### 0000:8b04 `txt_draw(TextCtx *c, char *s, int x, int y, int col)` (V)
`c->font = 0; c->x = x; c->y = y; c->fg = col; strupr(s) /* f2b8, in place, ASCII a-z only */; text_noclip(c, s)` (slot 5, driver draws,
font 0 = 3 px glyphs fixed width + 1 px spacing = **4 px per character**, height 5). `c->flags/bg` are NOT touched (flags 2 = transparent; `81f6` sets 1 temporarily for an opaque
box). The string is upper-cased in the caller's buffer: `9678` passes pointers into the weapon table so the weapon name table (`DS:366C`) is permanently upper-cased after the first
draw (original quirk). The call `f2b8(s, strlen)` passes an extra ignored argument.

### 0000:8b3b `hud_num_left(int v, int x, int y, int col)` (V)
`itoa(v, DS:C5D8, 10)` (signed int, f094) then `hud_text(DS:C5D8, x, y, col)`: left aligned at x.
### 0000:8b5c `hud_num_right(unsigned v, int xr, int y, int col)` (V)
`ltoa((long)(unsigned short)v, DS:C5D8, 10)` (f0b0 with hi word 0); `x = xr - 4*strlen`; `hud_text(DS:C5D8, x, y, col)`: right aligned ending at x = xr.
### 0000:8b93 `msg_post(char *text)` (V)
`strcpy(DS:C578, text); [409E] = [3660] * 3`. One slot only: a new message overwrites the old one and restarts the timer. `[3660]` = frame/time
scale (4 at mission start, see Open items); `7898` shows the message while `[409E]` counts down once per displayed frame.
Callers: 39c4 (2x), 4512 (countermeasure texts / "Stores exhausted"), 516d, 7898 ("Waypoint N reached").
### 0000:89fe `mfd_text(int mode, int col, int row, char *s, int colour)` (V)
If `[365E]`: mode 1 (left MFD): `hud_text(s, col*4 + 0x4A, row*6 + 0x6E, colour)`; mode 2 (right MFD): `x = col*4 + 0xA9`. Other modes draw nothing. Character cell = 4x6 px,
the MFD text area is 19 columns x 10 rows (77x60 px). The text lands on page `[4088]` (see section 0).
### 0000:8a4e `mfd_clear(int which, int colour)` -> 0000:8a5e `mfd_fill(int which, int colour)` (V)
`8a4e` just forwards both args. `8a5e`: `set_color(colour)` (8929: writes fg of ctx0 and ctx1), then fill_rect (1001:024C through `cur_ctx`) over
`which==1` -> (74,110)-(150,169) (left MFD), any other value -> (169,110)-(245,169) (right MFD). Used values: map background colour 6 (`8387`), scope/clear black 0,
TV page `0x9F`, nav page `[B80C]?1:0x2A`, objectives 0 and `2f82` calls `8a5e(2, 0x9F)` when it resets the TV page (V).
### 0000:873f `hud_line_view(int x0, int y0, int x1, int y1)` (V)
Draws a line of colour `ctx0.fg` on the **draw segment** (page 1 during the HUD) clipped to the ctx0 window (0,0)-(319,90): `offset_set(pixel_offset(ctx0.xmin, ctx0.ymin))`,
`[2293]=xmax-xmin, [2295]=ymax-ymin`, `color_set(ctx0.fg)`, `[2299]=x0, [229B]=x1, [229D]=y0, [229F]=y1`, then `1001:032A` (clipped line, R11a). Leaves
offset/window set (offset = 0 because the window origin is 0,0). Used for the crosshair, the tape bars, waypoint caret and the AAM circle.
### 0000:88a8 `hud_box(int x0, int y0, int x1, int y1, int col)` (V)
`set_color(col)`; four `8888` lines: (x0,y0)-(x1,y0), (x0,y1)-(x1,y1), (x0,y0)-(x0,y1), (x1,y0)-(x1,y1): outline rectangle. (8888 = `87a9` with clip window (0,319,0,199), no second page.)
### 0000:88f6 `hud_line_win(x0,y0,x1,y1)` (V)
`87a9(x0,y0,x1,y1, clip)` with clip (xmin,xmax,ymin,ymax) = (0x30,0x10F,0x0F,0x5A) = (48,271,15,90), or if `[294B]!=0` (99,220,30,90); both-pages flag 0. Callers a17e, b339, b3e6 (R9, HUD target brackets).
### 0000:8bab `wpn_suit(int wpn, int site)` (V)
> **S3.8:** owned by `flight.md` section 5.4 (verified there; where they differ, flight.md is right).
`type = (B2A2+16*site+0xE) & 0x7F` (site object type, byte at B2B0+16*site); `cls = byte [C604 + type] & 0xF` (target class); returns signed byte `[0x386A + 16*wpn + cls]` (weapon
suitability table 16x16, same as DS:1D86 in START). Callers 671a, a17e.
### 0000:9641 `fmt_2digit(int n)` (V)
`n = n % 60 (signed idiv)`; if `n < 10` `strcat(DS:9876, "0")` (DS:4113); `strcat(DS:9876, itoa(n, DS:DEAA, 10))`. (Negative n prints "0-5"; unused.)
### 0000:955c `fmt_clock_hhmm(unsigned t)` and 0000:95c4 `fmt_clock_hhmmss(unsigned t)` (V)
Clock time of the mission from the tick counter `t` in **units of 2 s** (1800 units = 1 h, 30 = 1 min; same as START `clock_string`): `t += [9870]` (start-time offset,
`((E2E0+E2F2)&0xF)<<8`, set by 4857).
- `955c`: `strcpy(9876,""); fmt_2digit(t/1800)` -> "HH" (hours count `t/1800 % 60`, no wrap at 24), then **`9876[0] += [3658]; 9876[1] += night ? 8 : 6`** (day starts at 06:xx, night at 18:xx;
  `[3658]` = night flag = comm+33h), carry `if 9876[1] > '9' { 9876[1] -= 10; 9876[0]++ }`; `strcat(":")` (DS:410C); `fmt_2digit(t/30)` -> minutes. Result "HH:MM".
- `95c4`: same hour part, `strcat(":")` (410F), minutes `fmt_2digit(t/30)`, `strcat(":")` (4111), `fmt_2digit(t*2)` seconds (`(2t) % 60`). Result "HH:MM:SS".
Buffer is DS:9876 (shared scratch string).
### 0000:4a73 `site_name(int site)` (V)
Builds the display name of a site into DS:9876: `9876[0]=0`; `p = DEDC[ site.name_idx*2 ]` (name pointer table at DS:DEDC; `site.name_idx` = word at B2A2+16*site); if `strlen(p) != 0` use
index `site.name_idx` else index `site.type & 0x7F` (byte B2B0+16*site); `strcpy(9876, DEDC[idx*2])`; if `strlen > 0x12` truncate: `9876[0x12] = '.'; 9876[0x13] = 0` (name max 18 chars + '.').
Callers: 97a4 (target place name and landing base name).
### 0000:9154 `mfd_icon(int cx, int cy, int x, int y)` (V)
Draws a 7x7 colour-keyed (0 = transparent) icon centred on (x,y) from the panel picture page: BlitCtx at DS:4044 = {seg `[E558]`, sx = `cx*8+1`, sy = `cy*8+0x29`, dpage = `[4088]!=0`,
dx = x-3, dy = y-3, w = 7, h = 7, clip ymin=110, ymax=169, xmin=74, xmax=150} -> `blit_key_clip` (slot 71). So icons are clipped to the **left MFD box**. Icon sheet in the panel PIC:
row 0..2 (sy=41,49,57) x 16 columns (sx=1+8*dir) = aircraft symbols by direction, row 3 (sy=65): col 0 own ship, 1 SAM/threat site, 2 flare, 3 chaff, 5 generic site, 6 decoy, 7 site with flag 8 (U), 8..11 airfield/target symbol in 4 orientations.
### 0000:903f `mfd_target_box(int x, int y, int col)` (V)
`set_color(col)`; 4 calls of `8888` drawing the rectangle outline (x-4,y-3)-(x+4,y-3), (x+4,y-3)-(x+4,y+3), (x+4,y+3)-(x-4,y+3), (x-4,y+3)-(x-4,y-3): a 9x7 selection frame around an icon.
### 0000:950e `mfd_fuel_bar(int v, int col)` (V)
`v = clamp(v, 0, 10000)` (c94e); if `v > 134`: `set_color(col)`; `fill_rect(cur_ctx, 170, 164, 170 + v/135, 167)` (ctx1 if `[4088]!=0`): a bar 4 px tall at y=164..167 from x=170, 74 px wide for v = 10000 (full tank = 10000).
Nothing is drawn for `v <= 134`.
### 0000:90b9 `mfd_w2s(int wx, int wy)` (V) -- world to radar scope
Sets `[DE92] = 0`; `s = 7 - [4082]`; `dx = (wx - [C0A4]) >> s; dy = ([C0B2] - wy) >> s` (arithmetic; C0A4/C0B2 = ownship x,y; y grows south so dy = north offset);
`h = [2DEE]` (heading, angle 0 = north, 0x4000 = east):
`X = dx*cos(h) - dy*sin(h)` (`cb07(h,dx) - caee(h,dy)`, caee = r*sin, cb07 = r*cos);
`Y = dx*sin(h) + dy*cos(h)` (`caee(h,dx) + cb07(h,dy)`);
`[49E0] = X + 0x70` (112); `[49E8] = 0x98 - Y` (152 - Y). Ownship is therefore at (112,152), heading-up.
Visibility: `[DE92] = -1` if `[49E0] < 0x4E || > 0x93` (78..147) or `[49E8] < 0x71 || > 0xA6` (113..166); otherwise 0. Output in DS:49E0 (x), 49E8 (y), DE92 (outside flag).

## 2. Left MFD

### 0000:80a0 `mfd_left_select(int mode)` (V)
```
if (!panel) return;                                  /* [365E] */
if (mode != [DEDA]) lamp_set(ctx0, [408A + 2*[DEDA]], 0);   /* old button off */
switch (mode) { case 0: mfd_map_redraw(C0A4, C0B2) /* 8387 (R3), sets DEDA=0 */; break;
                case 1: mfd_clear(1, 0); break; }    /* black; the scope is repainted every frame by 7898 -> 8c10 */
[DEDA] = mode;
if (rectstate[ [408A + 2*mode] ] == 0) lamp_set(ctx0, [408A + 2*mode], 1);   /* d83e(ctx0, lamp, 1) */
```
`DS:408A` lamp table: mode 0 -> lamp 22, mode 1 -> lamp 23 (also 13 13 29 for non-existing later entries). Callers: cba0 (F3), 3968 (restore after panel redraw).
### 0000:82e9 `mfd_init_geometry` (V)
`[DEDA]=0; [DE94]=0x4A; [E302]=0x96; [DE96]=0x6E; [E444]=0xA9; [98CA]=0x4D; [98CC]=0x3C` = left MFD x0=74, x1=150, y0=110, y1=169, width 77, height 60 (read by 8548/85ed/86e5,
the map icon code; 98CA/98CC unused). **Then `jmp 8498`** (tail jump, no `ret` of its own): it behaves as one `mfd_zoom_in()` call, so at mission start `[4080]` goes 1 -> 2 and `8387` redraws the map. Called once from 39c4 at
mission initialisation (after 4857 and 499e).
### 0000:8498 `mfd_zoom_in()` / 0000:84d6 `mfd_zoom_out()` (V)
```
zoom_in:  if (C07A & 0x80) { [3D70]--; return; }
          if (DEDA==0 && [4080] < 9) { [4080]++; mfd_map_redraw(C0A4, C0B2); }
          if (DEDA==1 && [4082] < 7) [4082]++;
zoom_out: if (C07A & 0x80) { [3D70]++; return; }
          if (DEDA==0 && [4080] > 2) { [4080]--; mfd_map_redraw(...); }
          if (DEDA==1 && [4082] != 0) [4082]--;
```
`[4080]` map zoom 2..9 (init 1, forced to 2 by 82e9; the data image has 8), `[4082]` scope range index 0..7 (init 1; scope uses shift `7-[4082]`, so a larger value = more zoomed in; one pixel = 2^(7-s) world units). `[3D70]` (R3) = camera distance in external views.
Note the scope zoom-in test uses `<7`, the zoom-out test `!=0` (no lower clamp at 1).
### 0000:8387 `mfd_map_redraw(int x, int y)` (R3 owns; facts I verified)
Sets `[DEDA]=0`, `[C042]=[C044]=0`, `mfd_clear(1, 6)` (left MFD filled with colour 6 first), computes the map window `[4084]`,`[4086]` from `[2DEE]` and the ownship position with zoom `[4080]`,
calls `119c` (terrain map render into page `[402A]`), then draws site icons through `8548` for sites with `flags & 0x84 == 4`, finally `973c` (save). Details are in R3's section.

### 0000:8c10 `mfd_scope_draw(int flip)` (V) -- left MFD mode 1 (situation / radar scope), called every frame from 7898 when `[DEDA]==1`
Arg: `[4088]`. All drawing is clipped to the MFD box (74,150)x(110,169) by `87a9(…, 0x4A, 0x96, 0x6E, 0xA9, 0)`; icon visibility window is 78..147 x 113..166 (see 90b9).
1. `set_color(0)`; `fill_rect` via `flip ? ctx1 : ctx0` the rect (73,109)-(151,170) (1 px larger than the box).
2. `set_color(8)` (dark grey) **grid** at 1024-world-unit spacing in world space (rotated by the heading by 90b9): `n = 1; if ([4082] < 2 && [98DE] != 0) n = (1 << (2-[4082])) + 1`
   (s=0: 5, s=1: 3). `gx = [C0A4] & 0xFC00`, `gy = [C0B2] & 0xFC00`. Vertical lines for `i = 1-n .. n`: from `(gx+(i<<10), gy+0x1400)` to `(gx+(i<<10), gy-0x1000)`;
   then horizontal lines for `i = 1-n .. n`: from `(gx+0x1400, gy+(i<<10))` to `(gx-0x1000, gy+(i<<10))`; each end converted with `90b9`, drawn with `87a9` (clipped).
   (`[98DE]` = comm+32h byte, set from START; extra grid lines at high zoom (U).)
3. **Aircraft/units** `i = 0 .. [DED0]-1`, record = 36 bytes at DS:C140+36*i (fields used: `+0 x`, `+2 y`, `+4 alt`, `+0xE heading`, `+0x16 flags`, `+0x18 state word`): skipped unless `(flags & 2)` and `state != 0`;
   `90b9(x,y)`; skipped if `[DE92]==-1`. Then:
   - if `[E2FE]==1 && [3D64]==i`: `mfd_target_box(X,Y,7)` (selected unit);
   - if `[3D6A] > 0 && (0xFFFF - i) == [C07E]`: `mfd_target_box(X,Y,[9872])` (locked target; ids of units are negative, `C07E = -1-i`);
   - icon: `dir = (((rec.heading - [2DEE]) + 0x800) >> 12) & 0xF`; `cls = 0` if `|alt diff| <= 1000`, 1 if `rec.alt - [2DF4] < -1000`, 2 if `> 1000`; `mfd_icon(dir, cls, X, Y)`.
4. **Missile/projectile tracks** `i = 0..11`, 28-byte records at DS:3C0C (`+0 x`, `+2 y`, `+8 heading`, `+0xE active`, `+0x10 type index` -> `0x12*type + 0x336E` (word at +0xC of the weapon-type record, >0 = guided type)):
   if active (`+0xE != 0`): `90b9(x,y)`; skip if outside; colour = (`type_rec[+0xC] > 0`) ? `0x11` : `0x29`; then if `i >= 8` colour `0x0F` (overrides); draw one line from (X,Y) to
   `(X - caee(a, r), Y + cb07(a, r))` with `a = rec.heading - [2DEE]`, `r = [4082] + 1` (1..8 px), clipped to the MFD box (`87a9`). (U: why slots 8..11 are white; probably enemy vs own weapons.)
5. **Sites** `i = 0 .. [C0B4]-1` (16-byte records at DS:B2A2+16*i, see Structures): skip if `(flags & 0x80) && !(flags & 0x200)`; `90b9(x,y)`; skip if outside;
   `[E2FE]==2 && [3D66]==i` -> frame colour 7; `[3D6A]>0 && [C07E]==i` -> frame colour `[9872]`; icon column `c = 5`; if `flags & 0x201` then `c = 8 + (((0x1000 - [2DEE]) & 0x6000) >> 13)` (8..11, orientation relative to heading);
   if `site.+6 != 0` `c = 1`; if `flags & 8` `c = 7`; `mfd_icon(c, 3, X, Y)`.
6. **Ownship**: `90b9(C0A4, C0B2)`; if inside (always, it is at (112,152)): `mfd_icon(0, 3, X, Y)`.
7. **Countermeasure markers** `i = 0..3`, records 12 bytes at DS:39AA (`+0 x`, `+2 y`, `+6 kind`, `+8 life`; written by 4512): if `life != 0`: `90b9`; skip if outside; kind 1 (flare) `mfd_icon(2,3)`, 2 (chaff) `mfd_icon(3,3)`, 3 (decoy) `mfd_icon(6,3)`.
Order of drawing = order above (later icons on top). No text is drawn on the scope.

## 3. Right MFD

### 0000:8113 `mfd_right_select(int page)` (V)
```
if (!panel) return;
if (page != [DFDC]) {                                   /* leaving the old page */
    lamp_set(ctx0, [4068 + 2*[DFDC]], 0);               /* old button off */
    if ([DFDC] == 0x13) { lamp_set(ctx0, 0x1B, 0); if ([480E]) lamp_set(ctx0, 0x19, 0); }
}
switch (page) { case 0x13: mfd_tv_label();  mfd_clear(2, 0x9F); break;
                case 0x14: mfd_nav_page();    break;
                case 0x15: mfd_weapons_page(); break;
                case 0x18: mfd_objectives_page(); break; }
[DFDC] = page;
if (rectstate[ [4068 + 2*page] ] == 0) {                /* new button on */
    lamp_set(ctx0, [4068 + 2*page], 1);
    if (page == 0x13) { lamp_set(ctx0, 0x1B, 1);
        if ([43B2] && [480E]==0) lamp_set(ctx0, 0x19, 1); }
}
```
Page -> lamp table `DS:4068` (word index = page): 0x13 -> lamp 30, 0x14 -> 28, 0x15 -> 26, 0x16 -> 13, 0x17 -> 13, 0x18 -> 29. (`[480E]` is never written; always 0.)
Page 0x16 is not a page: `829d(0x16)` only refreshes the damage lamps. `[DFDC]` initial value 0x18 (written by 784a at mission start; 39c4 calls `97a4` directly there).
Callers: cba0 (F5/F7/F8/F10, m , . /), 829d (refresh), 3968 (re-draw after panel restore).
### 0000:829d `mfd_refresh(int page)` (V)
```
if (page == 0x16) for (i = 0; i < 7; i++) lamp898b(10 + i, ([3632] >> i) & 1 ? 0x29 : 0x0A);   /* damage lamps, 898b entries 10..16 = lamps 5,6,7,12,11,9,8; value 0x0A = off */
if (page == [DFDC]) mfd_right_select(page);
```
Callers: 74f6 (after damage, page 0x16), cba0 (page 0x14 every 32nd sim frame while the nav page is shown: `([3D60] & 0x1F)==0`; page 0x16 after the Alt-R repair cheat in training mode, which calls 48dd first).
### Refresh rules of the right MFD pages (V from cba0 d46b, d5ec, ce2b, d630)
* nav (0x14): redrawn by `829d(0x14)` when `[DFDC]==0x14 && ([3D60] & 0x1F)==0`; also after the waypoint edit keys and `d11d` (waypoint list change).
* weapons (0x15): `9678()` after each fire command (`753f`, with 4-frame debounce `[43BC]`), after the weapon select key, on F5.
* TV (0x13): label `81f6()` after weapon select (when `[9B05]&2`) and when the lock ends; page content is rendered by the 3D code (`a17e`, R9/R10).
* objectives (0x18): only on page selection (F10) and at mission start.

### 0000:81f6 `mfd_tv_label()` (V) -- text under the TV page
Active only if `[365E]`. `fg = ([4826]==1) ? 0x2F : 0x2D; bg = ([4826]==1) ? 0x2E : 0x2C` (`[4826]` is never written, so 0x2D/0x2C: dark greens). Text by `[E556]`
(TV camera azimuth): 0 `" Ahead "`, 0x4000 `" Right "`, 0x8000 `" Rear  "`, 0xC000 `" Left  "` (DS:40CA/40DA/40D2/40E2, 7 chars; copied to DS:9876). Draws on **ctx0 = [3FE2] directly** (page 0):
temporarily `ctx0.flags = 1` (opaque), `ctx0.bg = bg`, then `txt_draw(ctx0, 9876, 197, 193, fg)` (font 0, 4 px/char -> x 197..224, y 193..197, inside lamp box 27 at (190,191) 38x8), then restores flags and bg.

### 0000:9208 `mfd_nav_page()` (V) -- right MFD page 0x14 (F7 edit / F8 normal)
Layout (x = 169 + 4*col, y = 110 + 6*row, font 0, `mfd_text(2, col, row, s, colour)`):
```
mfd_clear(2, [B80C] ? 1 : 0x2A);              /* background: blue 1 in edit mode, dark red 0x2A normal */
t = [98E2];                                    /* mission tick, 2 s units */
mfd_text(2, 2, 0, "TIME", 15);                 /* DS:40EA, col 2 -> x=177, y=110 */
fmt_clock_hhmmss(t);  mfd_text(2, 8, 0, DS:9876, 15);        /* x=201 */
row = 1;
for (i = 0; i < 4; i++) {                      /* waypoint legs, 2 rows each */
    cur = [2E7C];  /* current waypoint 0..3 */
    wp = (X=[2E6C+4i], Y=[2E6E+4i]);
    if (i <= cur) { prev = ownship (C0A4,C0B2); col = 0 /*black*/; if (i==cur) col = [B80C] ? 15 : banded; }
    else          { prev = (X,Y)[2E68+4i], [2E6A+4i] = waypoint i-1; col = banded; }
    banded = (i & 1) ? 3 : 11;                 /* odd rows cyan(3), even light cyan(0xB); written (i&1)==0 ? 11 : 3 */
    if (![B80C] && i == [2E7E]) col = 15;      /* selected waypoint white in normal mode */
    s = " Waypoint " + itoa(i+1) + "-" + itoa(dist >> 6) + " Km";   /* DS:40EF, 40FA, 40FC ; dist = c987(prevx-X, prevy-Y) */
    mfd_text(2, 0, row++, s, col);
    spd = (B052 <= 2000) ? 10800 : B052;  eta = dist / (spd >> 6);         /* U units: spd>>6 divisor */
    fuel[i] = 5 * eta * ( [2DFE]*[2DFE]/1000 + (([3632]&0x20) ? [C5C8] : 0) );   /* fuel burn estimate for the leg, 16-bit signed */
    s = itoa(eta >> 2) + " MIN. ETA";           /* DS:4100 "" , 4101 " MIN. ETA" */
    mfd_text(2, 1, row, s, col);               /* same row as the clock below */
    if (i >= [2E7C]) t += eta << 3;            /* running arrival time (U unit: eta<<3 in 2 s units) */
    fmt_clock_hhmm(t);  mfd_text(2, 13, row++, DS:9876, col);   /* x = 221 */
}
/* fuel bars at y 164..167 (950e), x from 170, scale 135 fuel units per pixel */
mfd_fuel_bar(10000, 0);                        /* clears the track in black */
f = [3634];                                    /* fuel on board, 10000 = full */
for (i = 0; i < 4; i++) {
    if (i < [2E7C]) continue;                  /* passed waypoints: no overlay and no subtraction */
    mfd_fuel_bar(f, i == [2E7C] ? 15 : ((i&1) ? 3 : 11));
    f -= fuel[i];
}
mfd_fuel_bar(f, 0x2F);                         /* reserve left after the last leg, green */
```
Details to keep exact: the colour of line pairs uses the leg index i (not the waypoint's own state) as described; for `i<cur` the colour is 0 (black text on the background, effectively invisible). Fuel bar overlays shrink progressively: leg 0's overlay has the longest length; each later one the remaining fuel. `fuel[]` is a local 4-word array. `eta` for the clock and for the text are the same value `bp1a`.
Edit mode (`[B80C]=1`, F7): `cba0` arrow keys move waypoint `[2E7E]` by `delta >> [4080]` map pixels and re-call `8113(0x14)`; F8 resets `[2E7E]=[2E7C]`.

### 0000:9678 `mfd_weapons_page()` (V) -- right MFD page 0x15 (F5)
```
blit_rect(2, 169,110, 1, 169,110, 77, 60);     /* restore the pristine right MFD box from page 2 into page 1 (NOT through [4088]) */
n = (pilot.+42 == 0) ? 4 : 2;                  /* MicroProse F117A model has 4 weapon slots, Lockheed 2 */
y = 112;
for (i = 0; i < n; i++, y += 15) {
    s = "0" + itoa([3640 + 4*i]);              /* DS:4115 "0": quantity, "03" etc. (a quantity of 10+ prints "010") */
    hud_text2(s, 172, y + 6, 0x2F);            /* via 8ad3 -> 8b04 */
    s = "" + weapon_name(  [363E + 4*i] );     /* 0x366C + 26*weapon_id, uppercased in place */
    hud_text2(s, 172, y, 0x2F);
}
```
Name line at y = 112 + 15*i, quantity line 6 px below (x=172 for both), colour 47 (green); weapon id 0..19 (Sidewinder, AMRAAM, HARM, Penguin, Harpoon, Maverick, Paveway, Rockeye, Durandal, Slick, Snakeye, Rockeye II, Fireeye, Fuel-Air, IN Cluster, Minelets, Camera, Extra Fuel, Equip, Guns).
The selected weapon `[364E]` is NOT highlighted here (the HUD shows it on the main view line, see 7898). `[3640+4i]` is decremented by the weapon code.

### 0000:97a4 `mfd_objectives_page()` (V) -- right MFD page 0x18 (F10, initial page)
Text builder for the mission orders. State: `[9B04]` bit `0x4000>>k` set = objective k completed (set by 7470 `objective_completed(k)`); mission slot records of 18 bytes at DS:E2D8+0x12*k, k = 0 primary, 1 secondary:
`+0 kind (1..8)`, `+2 target site`, `+4 base site`, `+6 flags (bit 1 = has deadline)`, `+8 template`; `[E2EE]` = landing base site; `[DE88]` = deadline tick (2 s units); `[98E2]` = current tick.
```
mfd_clear(2, 0);   row = 0;
for (k = 0; k < 2; k++) {
    if (([9B04] & (0x4000 >> k)) != 0) {            /* completed */
        s = (k == 0 ? "PRIMARY" : "SECONDARY") + " Mission";        /* DS:4122 / 4118 + 412A */
        mfd_text(2, 1, row++, s, 15);
        mfd_text(2, 1, row++, "Completed", 15);     /* DS:4133 */
        continue;
    }
    switch (kind = rec.kind) {                        /* verb phrase, jump table at cs:983B */
      1: "Photograph enemy"   (DS:413D)     2: "Destroy enemy"       (DS:4172)
      3: "Drop supplies over" (DS:415F)     4: "Land supplies at"    (DS:414E)
      5: "Hit AN72 flying to" (DS:4180)     6: "Plane returning to"  (DS:41A4)
      7: "Hit AN72 leaving"   (DS:4193)     8: "Destroy plane over"  (DS:41B7)
      default (0 or >8): 9876 keeps its old content (no strcpy): leftover text of the previous line (U, not reachable normally) }
    col = (k == 0) ? 0x11 : 0x29;                   /* primary yellow, secondary red */
    mfd_text(2, 1, row++, DS:9876, col);
    site = rec.target;
    s = DEDC[ site.type*2 ]        /* noun name, type = byte B2B0+16*site & 0x7F */   (strcpy to 9876)
    if (strlen( DEDC[ site.name_idx*2 ] ) != 0) {  /* the site has a place name */
        s += " at ";                                 /* DS:41CA */
        mfd_text(2, 1, row++, s, col);
        site_name(site);                              /* 4a73: the place name (max 18 chars) */
        mfd_text(2, 1, row++, DS:9876, col);
    } else {
        mfd_text(2, 1, row++, s, col);                /* colour recomputed the same: k==0 ? 0x11 : 0x29 */
    }
    if (rec.flags & 2) {                              /* deadline */
        fmt_clock_hhmmss([DE88]);
        buf = "Before " + DS:9876;                    /* DS:41CF */
        if ([98E2] > [DE88]) buf = "Time expired";    /* DS:41D7 ; signed compare, deadline passed */
        mfd_text(2, 1, row++, buf, 7);
    }
}
mfd_text(2, 1, row++, "Return to", 10);                /* DS:41E4 */
site_name([E2EE]);  mfd_text(2, 1, row++, DS:9876, 10);
```
Colours: 15 white, 0x11 yellow, 0x29 red, 7 grey, 10 light green. Rows are 6 px apart starting at y=110; 10 rows fit (the maximum possible text is 2x (verb, noun, place, deadline) + 2 = 10 rows).
Note the colour test in the "completed" branch: the `jne 9961` on the bit test means **bit set = completed** (39c4 uses the same bit to stop drawing the objective blip).
The "kind" -> text table is indexed `kind-1` with the jump table order `1,2,3,4,5,6,7,8 -> 97FD, 9814, 980F, 980A, 9819, 9823, 981E, 9828` giving the texts above (V; note the table is not in string order).

## 4. HUD overlay: 0000:7898 `hud_overlay_frame()` (V) -- per frame, after the 3D view
Locals: `alt_disp = ([2DF6] >= 100) ? ([2DF6] / 5) * 5 : [2DF6]` (altitude shown rounded down to 5).
All coordinates absolute screen pixels; text via `hud_text` (font 0, uppercase) on page `[4088]`; lines via `hud_line_view (873f)` (colour = ctx0.fg set by `8929`) or the lamps.
```
[4088] = flip_flag();  compute alt_disp
if (![365E]) goto messages;
/* 1. hit flash */
if ([98EE]) { [98EE] = 0;
    if (!([C07A] & 0x80)) { set_color(13); fill_rect_both_pages(0,0,319,89);  /* 893f on ctx0 and ctx1 */ shake_set(60); } }
/* 2. target-mode lamps 0..2 */
if ([E2FE] != [409C]) {
    lamp_set(ctx2, [409C], 0);  lamp_set(ctx2, [E2FE], 1);                  /* d83e on page 2 */
    blit_rect(2, 119,94, 0, 119,94, 59, 9);                                   /* strip to A000 */
    if ([2DFC]) blit_rect(2, 119,94, 1, 119,94, 59, 9);                      /* and to page 1 */
    [409C] = [E2FE]; }
[2956] = 1;                                                                   /* flag read by the asm HUD 10A8:000C */
if ([C07A] != 0) goto messages;                                               /* everything below is the front cockpit view only */
/* 3. stall lamp 4 */
if ([B168] < [E448] && [2DF4] != [C0A2]) v = [3D60] & 1;    /* blink while airborne below the minimum speed */
else if ([4712] != 0) v = 0; else skip;                                       /* 4712 never written: dead */
lamp_set(ctx0, 4, v); lamp_set(ctx1, 4, v); lamp_set(ctx2, 4, v);
/* 4. weapon line */
s = itoa([3640 + 4*[364E]]) + " " + weaponname([363E + 4*[364E]])   ;  /* strcpy "" (40A0), strcat itoa, strcat " " (40A1), strcat name */
hud_text(s, x = [294B]==0 ? 53 : 106, y = 85, [2CA0]);
if (pilot.+42 == 0) { s = "GUN " + itoa([3650]);  hud_text(s, x = [294B]==0 ? 239 : 188, 85, [2CA0]); }
/* 5. flight-path marker (not when [E2FE]==1) */
if ([E2FE] == 0 || [E2FE] == 2) { set_color([2CA1]);
    [C684] = ([294B]==0 ? 52 : 60) + ([98C6] >> 6);            /* aoa-based y of the marker */
    if ([294B]==0 && [C684] > 10 && [C684] < 93) 91a4(154, [C684]-4, 55, 77, 11, 7, [2CA0]);   /* 11x7 sprite from the panel sheet, key colour 0, clipped to rows 0..90 */ }
/* 6. fuel lamp 9 (898b entry 9 = lamp 36 at (302,179)) */
if (([3D60] & 1) && (c987(C0A4 - site[E2EE].x, C0B2 - site[E2EE].y) >> 4) > [3634]) v = 0x11; else v = 3;
lamp898b(9, v);
/* 7. left MFD scope */
if ([DEDA] == 1) mfd_scope_draw([4088]);
if ([294B] != 0) goto small_view;
/* 8. designator crosshair */
if ([9B05] & 2) { show = 1; if ([B28C]) show = [3D60] & 1;                  /* blinks while acquiring */
    if (show) { set_color([2CA0]); hud_line_view(156,85,164,85); hud_line_view(160,82,160,88); } }
if ([294A] == 0) {                                                              /* full HUD style */
    set_color([3658] ? 0x2A : 0);                                               /* black by day, dark red at night */
    n = clamp(((E448 - B168) * 2) / 5 + 29, 0, 57);                              /* left speed-margin bar */
    if (n) hud_line_view(72, 81 - n, 72, 81);
    hud_line_view(247, 52, 247, clamp(52 - ([E55A] >> 4), 20, 81));            /* right vertical-speed bar */
    if (!([9B04]&1) && ([3D60]&1) && pilot.+40 != 0 && [E55A] < 0) {           /* gear not up, descending, landing realism on */
        lim = ((site[DFE0].flags>>8 & 2) ? 0x100 : 0x80) / pilot.+40;  y = 52 + (lim >> 4);
        set_color([2CA0]); hud_line_view(242, y-2, 244, y); hud_line_view(242, y+2, 244, y); }   /* sink-rate limit tick */
}
if ([9B05] & 4) hud_ils();                                                     /* 99a7, F9 */
if ([E2FE] == 1) {                                                              /* air-to-air targeting cursor */
    s = [294B] + 4  (= 4 here);  X = ([E546] >> s) + 159;  Y = ([E550] >> s) + 52;
    if (X > 10 && X < 309 && Y > 8 && Y < 86) 91a4(X-6, Y-5, 38, 76, 13, 11, 15);    /* 13x11 reticle sprite */
    if (type_rec( weapon_rec([363E + 4*[364E]]).+12 ).+0xC == 7) {              /* AIM-9 / AIM-120 selected */
        set_color([2CA1]);
        for (a = 0; a <= 0x100; a += 0x10) {                                    /* 17 points, angle = (a & 0xFF) << 8 = k*0x1000 */
            px = 159 + sin16(ang) * 40;  py = 52 - cos16(ang) * 35;        /* caee(ang,0x28)+0x9F ; 0x34 - cb07(ang,0x23) */
            if (a) hud_line_view(prevx, prevy, px, py);  prev = p; } } }       /* seeker circle 40x35 around (159,52) */
/* 9. numbers */
hud_num_left([B168], [294A]==0 ? 80 : 58, 50, [2CA0]);                          /* airspeed */
hud_num_right(alt_disp, [294A]==0 ? 240 : 264, 50, [2CA0]);                    /* altitude, right aligned */
if ([294A] != 0) {
    if ([294A] == 2) { hud_num_left([2DEE] / 182, 154, 8, [2CA0]);  hud_box(151,5,167,15,[2CA0]); }   /* heading 0..359 in a box */
    hud_box(55,47,79,57,[2CA0]);  hud_box(241,47,265,57,[2CA0]);                /* boxes round speed and altitude */ }
if ([3D7A] > 1)  hud_text("ACCEL", 110, 85, [2CA0]);                            /* DS:40A8, time acceleration active */
if ([9B05] & 0x10) hud_text("TRAINING", 182, 85, [2CA0]);                        /* DS:40AE */
/* 10. waypoint caret (skipped when nav damaged: [3632] & 8) */
if (!([3632] & 8)) {
    wp = [2E6C + 4*[2E7C]], [2E6E + 4*[2E7C]];  dx = wp.x - C0A4;  dy = wp.y - C0B2;
    if (c987(dx,dy) < 0x200 && [2E7C] < 3) { [2E7C]++;  msg_post("Waypoint " + [2E7C] + " reached"); }   /* DS:40B7, 40C1 */
    [C078] = bearing = c9d6(dx, -dy);                                          /* angle: 0 north, 0x4000 east */
    xc = clamp((((bearing - [2DEE]) >> 6) / 3) + 159, 0x59, 0xE5);
    set_color(0x0B);
    if ([294A]==2)  { hud_line_view(xc,17,xc,17); hud_line_view(xc-1,18,xc+1,18); }                         /* small mark under the heading box */
    else { hud_line_view(xc-2,11,xc,13); hud_line_view(xc,13,xc+2,11); hud_line_view(xc-2,11,xc+2,11); }  /* V caret */ }
goto messages;

small_view:  /* [294B] != 0 : reduced HUD */
if (!([3632] & 8)) { bearing as above (no waypoint advance, no message); xc = clamp(((bearing - [2DEE]) >> 6)/7 + 159, 0x82, 0xBC);
    set_color(0x0B); hud_line_view(xc,27,xc,27); hud_line_view(xc-1,28,xc+1,28); }
if ([294A] != 0) { hud_num_left([B168], 103, 58, [2CA0]); hud_num_right(alt_disp, 218, 58, [2CA0]);
    if ([294A]==2) hud_num_left([2DEE]/182, 154, 20, [2CA0]); }
```
```
messages:
if ([409E] != 0 && ( ([C07A]==0 && [294B]==0) || [365A] != 0 )) {
    hud_text2(DS:C578, 160 - 2*(strlen & ~1), 20, [2CA0]);   /* centred, 4 px per char */
    [409E]--; }
if ([C07A] == 0x42) {                                              /* external view M: altitude in the status strip */
    blit_rect(2, 196,185, [4088], 196,185, 30, 5);                  /* restore the strip from the pristine page */
    ltoa(alt_disp, DS:C5D8); hud_text(DS:C5D8, 200, 185, 0x29);
    blit_rect([4088], 196,185, 1-[4088], 196,185, 30, 5); }          /* copy to the other page */
```
Remarks: (a) the message timer only counts down while the message is allowed to be shown. (b) `[C684]` is also read by a17e (gun/pipper). (c) `[E2FE]==1` branch uses `[9B05]&2` crosshair at (160,85) in parallel with the cursor. (d) In `[C07A] != 0` views only the hit-flash, lamps 0..2 and the message/altitude code run. (e) The waypoint "reached" test happens only in the full view (not in the small view) and only when nav is not damaged.

### Object/lamp primitives used (owned by R2; semantics I verified)
* `8929 set_color(c)`: `ctx0.fg = ctx1.fg = c`. `893f fill_rect(x0,y0,x1,y1)`: fills with ctx fg on **both** ctx0 and ctx1 (1001:024C(ctx, x0, y0, x1, y1), inclusive).
* `8888 line_full(x0,y0,x1,y1)` = `87a9` with clip window (0,319,0,199), second-page flag 0. `87a9(x0,y0,x1,y1,xmin,xmax,ymin,ymax,both)`: sets draw offset to the clip window origin, `[2293]=xmax-xmin, [2295]=ymax-ymin`, colour ctx0.fg, calls 1001:032A with `[2299]=x0-xmin,[229B]=x1-xmin,[229D]=y0-ymin,[229F]=y1-ymin`; if `both!=0` draws the same line again on the other page (`page_select(flip_flag==0)`) then restores `page_select([4088]!=0)`; finally `[2293]=0x13F,[2295]=0xC7`, offset 0.
* `d83e lamp_set(ctx, idx, on)`: lamp rectangle table `DS:46D8`, 37 entries of 12 bytes {x, y, w, h, type, state}; colours from table `DS:4672` (16 bytes per type, words): `on`: recolour `tbl[t][0]->tbl[t][1]` and `tbl[t][2]->tbl[t][3]` (types 4 also `[4]->[5]`, `[6]->[7]`) inside rect (x,y)-(x+w,y+h) of the page of `ctx` with `rect_recolor`; off: the reverse; `state = on`. Types: 0: 45<->47, 44<->46; 1: 38<->41, 55<->42; 2: 54<->41, 55<->42; 3: 16<->41, 43<->42; 4: 16<->40, 43<->39, 54<->41, 55<->42.
  Lamp rectangles (x,y,w,h,type): 0 (119,94,18,8,0) 1 (139,94,18,8,0) 2 (159,94,18,8,0) 3 (184,94,21,8,2) 4 (38,105,24,8,3) 5 (4,117,18,8,4) 6 (24,117,18,8,2) 7 (44,117,18,8,2) 8 (4,127) 9 (24,127) 10 (44,127) 11 (4,137) 12 (24,137) 13 (44,137) all 18x8 type 2; 14 (51,149,11,6,2) 15 (51,159) 16 (51,169); 17 (4,179,28,8,0) 18 (36,179,26,8,0) 19 (4,191,18,8,2) 20 (24,191) 21 (44,191) 22 (80,173,18,8,0) 23 (100,173,18,8,0) 24 (142,193,14,5,1) 25 (232,184,17,8,2) 26 (190,181,18,8,0) 27 (190,191,38,8,0) 28 (170,181,18,8,0) 29 (210,181,18,8,0) 30 (170,191,18,8,0) 31 (257,103,25,8,3) 32 (257,113,25,8,2) 33 (284,113,25,8,4) 34 (257,123,25,8,2) 35 (284,123,25,8,2) 36 (302,179,13,7,2).
* `898b lamp898b(idx, value)`: table `DS:3F00` 6 bytes per entry {?, lamp, off_value, current}: `if ([365E] && value != cur) { lamp_set(ctx0, lamp, value != off_value); if (idx == 2) lamp_set(ctx2, lamp, same); cur = value; }`. Entries (lamp, off value): 0 (19,0) 1 (20,0) 2 (31,0) 3 (34,0) 4 (32,0) 5 (17,0) 6 (18,0) 7 (16,0) 8 (21,3) 9 (36,3) 10 (5,10) 11 (6,10) 12 (7,10) 13 (12,10) 14 (11,10) 15 (9,10) 16 (8,10) 17 (13,10) 18 (13,10) 19 (4,2).

## 5. ILS: 0000:99a7 `hud_ils()` (V) -- F9 overlay, called by 7898 when `[9B05]&4` and `[294B]==0`
```
if (![365E] || [294B] || (site[DFE0].flags>>8 & 8)) return;           /* DFE0 = landing site; flag 0x800 = hidden */
dx = site.x - C0A4; dy = site.y - C0B2;
ang = c9d6(dx, sgn(dy) * |dx| - dy);                                   /* (U exact geometry, half-angle form) */
if (|dy| < 0x40 || |dy| > 0xA00) return;                              /* only within 64..2560 units along Y */
set_color([2CA0]);
[49E0] = x = clamp((int8)((ang - [2DEE]) >> 8) + 159, 139, 181);       /* high byte of the angle difference, sign extended */
line_full(x, 39, x, 73);                                               /* localiser needle */
hud_text2("ILS", x - 6, 33, [2CA0]);                                    /* DS:41EE */
if (site.flags>>8 & 2) { d = (|dy| - 0x18) >> 5; alt = [2DF4] - 0x80; } else { d = (|dy| - 0x38) >> 6; alt = [2DF4]; }
[49E8] = y = clamp((alt >> 7) - d, -16, 16) + 56;
line_full(140, y, 180, y);                                              /* glide-slope needle */
```
Uses `cb39` = sign function, `c9d6` = atan2(x, y) (0 = +y, 0x4000 = +x), `c94e(v,lo,hi)` clamp.

## Open items
1. `[3660]`: used as the time scale for message duration (`3*[3660]`), `[3660]*0x14`, countermeasure life; initial 4 (set in 39c4 init); R4 should name it (frames per sim tick or frame-rate divider).
2. Units of the nav page ETA (`eta`, `eta>>2` minutes, `eta<<3` added to the 2-second clock): arithmetic reproduced, physical units (U); fuel-burn constants as above (U).
3. The 0x9F fill of the TV page (`mfd_clear(2,0x9F)`) is probably a key colour for the TV/camera render in `a17e` (U); `[4826]`, `[480E]`, `[4712]` are never written (dead branches).
4. `[98DE]` (comm+32h) meaning (extra grid lines in the scope at zoom 0/1) (U). `[9872]` lock colour written by 516d/62d0/a17e.
5. `82e9` falls into `8498` (zoom in once) at mission start; confirm against a runtime trace that the map starts at zoom 2.
6. Misc flag meanings: `[3632]` damage bitmap (bit3 = nav/HUD caret and autopilot, bit5 = engine/fuel leak, bit6 = lamps 5,6), `[9B05]` bits: 1 acquire request, 2 locked/tracking, 4 ILS, 0x10 training, 0x20/0x40 primary/secondary objective done; `[9B04]` bit0 gear up, bit1/4/5 lamps, bit3 night nav?, 0x4000/0x2000 objective completed.
7. Which view code uses `[C07A]` 0x41..0x44 (names) -- R3.

## DGROUP globals seen
| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 3FCC/3FE4/3FFC | 24 B each | txt_ctx0/1/2 | TextCtx for page 0, 1, 2 (+0x16 self pointer) | all drawing helpers |
| 3FE2/3FFA/4012 | w | txt_ctx0_ptr / ctx1 / ctx2 | pointers (data-initialised, never written) | everything |
| 4088 | b | hud_flip_flag | `flip_flag()` result sampled each frame; selects ctx1 vs ctx0 | 7898, 87a9 writers; all text |
| 4044 | 22 B | icon_blitctx | BlitCtx for 9154 (seg E558, clip 110,169,74,150) | 9154 |
| 4062 | 22 B | spr_blitctx | BlitCtx for 91a4 (clip rows 0..90) | 91a4 (R3) |
| E558 | w | panel_sheet_seg | segment of the panel picture / sprite sheet | 0010 writes, 9154, 91a4 |
| 2CA0 / 2CA1 | b | hud_col1 / hud_col2 | HUD primary/secondary colour (0xA/0x2F or 0xF/7) | 10a8:09ce writes; 7898, 99a7, a17e... |
| 43B0 | w | hud_colour_alt | F4 toggle | cba0, 01bf, 10a8:09ce |
| 294A | b | hud_style | 0/1/2 (key v) | cba0 writes; 7898, 10a8:000C |
| 294B | b | hud_small | 0/1 (key c) | 1e9e, 2f82, 39c4, bf71 write; 7898 etc. |
| 2956 | b | hud_drawn | set to 1 each cockpit frame | 7898 writes, 10a8:000C |
| 365E | w | panel_present | `(C07A & 0xC0)==0`, set in 2f82 | many |
| 365A | w | msg_all_views | allows messages outside the front view (never seen set) | 7898 |
| C07A | w | view_mode | 0 front; 0x41..0x44, 0x80+ external | many |
| DEDA | w | mfd_left_mode | 0 map, 1 scope | 80a0, 82e9, 8387 write |
| DFDC | w | mfd_right_page | 0x13/0x14/0x15/0x18 | 784a, 8113 write |
| 408A | 8 w | mfd_left_lamp_tbl | left mode -> lamp (22,23,...) | 80a0 |
| 4068 | w[] | mfd_right_lamp_tbl | right page -> lamp | 8113 |
| 4080 | w | map_zoom | 2..9 | 8498, 84d6, 8387, 39c4 |
| 4082 | w | scope_zoom | 0..7 | 8498, 84d6, 8c10, 90b9 |
| 4084/4086 | w | map_org_x/y | map window origin (8387) | 8387, 8514 |
| 49E0/49E8 | w | scope_x / scope_y | result of 90b9 / ILS | 90b9, 99a7, 903f, a17e... |
| DE92 | w | scope_clip | -1 = outside | 90b9, 9aca, a17e |
| DE94/E302/DE96/E444 | w | mfd_x0/x1/y0/y1 | left MFD geometry 74,150,110,169 | 82e9 writes; 8548, 85ed, 86e5 |
| 98CA/98CC | w | mfd_w/mfd_h | 77, 60 (unused) | 82e9 |
| 409C | w | hud_mode_lamp | last `[E2FE]` shown on lamps 0..2 | 784a, 7898 |
| E2FE | w | tgt_mode | F2: 0/1/2 | cba0, 39c4 write |
| 3D64 / 3D66 | w | tgt_unit / tgt_site | selected unit index / site index (-1 none) | cba0, 39c4, 2f82... |
| 3D6A | w | lock_state | >0 target locked | 39c4, cba0, 4f2a, 516d, 62d0 |
| C07E | w | lock_id | locked target id (site index, units = -1-i) | 516d, 62d0 |
| 9872 | w | lock_col | colour of the lock frame | 516d, 62d0, a17e |
| 98EE | w | hit_flash | set by 74f6, cleared by 7898 | |
| 409E | w | msg_timer | frames left | 7898, 8b93 |
| C578 | char[96] | msg_text | current message | 8b93, 7898 |
| C5D8 | char[] | num_buf | number scratch | 8b3b, 8b5c, 7898 |
| 9876 | char[] | str_buf | shared scratch string | all text builders |
| DEAA | char[] | itoa_buf | itoa scratch | 7898, 9208, 9641... |
| 3D60 | w | sim_frame | sim frame counter (blink = bit 0, 32-frame refresh = low 5 bits); incremented in 39c4 | 7898, cba0, 8c10... |
| 3D7A | w | time_accel | >1 shows "ACCEL" | 7898 |
| 9B04 | w | cockpit_flags | bit0 gear up, 0x4000/0x2000 objective done, ... | 7898, 97a4, cba0 |
| 9B05 | b | tgt_flags | see Open item 6 | 7898, cba0, a17e, 7470 |
| 3632 | w | dmg_bits | damage bitmap | 7898, 829d, 74f6 |
| B168 | w | airspeed_kt | `B052/27` | 7898 |
| E448 | w | min_speed | speed threshold (stall lamp, left bar) | 7898 |
| 2DF6 | w | alt_ft | altitude (display) | 7898 |
| 2DF4 | w | alt_world | compressed altitude (renderer) | 7898, 8c10 |
| C0A2 | w | ground_alt | terrain height under ship | 7898 |
| 2DEE | w | heading | 16-bit angle, 0 north | many |
| 98C6 | w | aoa | angle of attack (marker offset) | 7898 |
| E55A | w | vspeed | vertical speed (VSI bar) | 7898 |
| E546/E550 | w | stick_x/y | rotated stick cursor offsets | 1e9e, 7898 |
| C684 | w | fpm_y | flight-path marker y | 7898, a17e |
| C078 | w | wp_bearing | bearing to waypoint | 7898 |
| 2E6C | 4 x {x,y} | wp_list | waypoints (x at 2E6C+4i, y at 2E6E+4i) | cba0, d7d0; 7898, 9208, 94a5 |
| 2E7C / 2E7E | w | wp_cur / wp_sel | current / selected waypoint | 7898, 39c4, 7470, cba0 |
| B80C | w | wp_edit | F7 edit mode | cba0, 9208 |
| 3634 | w | fuel | 10000 = full | 7898, 9208 |
| 2DFE | w | throttle | percent | 9208, 1e9e |
| B052 | w | speed_raw | airspeed fixed | 9208 |
| C5C8 | w | dmg_pts | damage points (fuel leak factor) | 9208, 74f6 |
| 364E | w | wpn_sel | selected weapon slot 0..3 | 7898 |
| 363E / 3640 | 4 x {w id, w qty} | wpn_slots | weapon id / quantity (4 bytes stride) | 7898, 9678, 4512, 39c4 |
| 366C | 26 B x n | wpn_table | weapon records, name[12] first, +12 = type index | 7898, 9678 |
| 3362 | 18 B x 41 | wpn_type_table | SAM/missile type records (name[8], +8 range, +0xC class, ...) | 7898, 8c10 |
| 3650 | w | gun_rounds | | 7898, 465b |
| 98E2 | w | mission_tick | 2 s units | 9208, 97a4, 39c4 |
| 9870 | w | clock_offset | start offset (E2E0+E2F2 & 0xF)<<8 | 955c, 95c4, 4857 |
| 3658 | w | night | comm+33h | 955c, 95c4, 7898 |
| DE88 | w | deadline_tick | objective deadline | 97a4 |
| E2D8 | 18 B x 2 | mission_slots | mission objective records | 97a4, 7470 |
| E2DC / E2EE | w | base_site / land_site | departure / landing site index (= slot0+4 / slot1+4) | 7898, 97a4, 1e9e, 39c4, cba0 |
| DFE0 | w | land_site_sel | current landing site index | 7898, 99a7 |
| B2A2 | 16 B x [C0B4] | sites | site records | 8c10, 97a4, 4a73, 99a7 |
| C0B4 | w | site_count | | 8c10 |
| DEDC | w[] | name_ptrs | site/noun name pointer table | 4a73, 97a4 |
| C140 | 36 B x [DED0] | air_units | aircraft records | 8c10 |
| DED0 | w | air_unit_count | | 8c10 |
| 3C0C | 28 B x 12 | tracks | missile/projectile records | 8c10 |
| 39AA | 12 B x 4 | cm_marks | countermeasure markers | 8c10, 4512 |
| 98DE | w | scope_detail | comm+32h | 8c10 |
| C604 | b[] | obj_class | object type -> target class | 8bab |
| 386A | 16x16 b | wpn_suit_tbl | weapon suitability | 8bab |
| 46D8 | 12 B x 37 | lamp_rects | see section 4 | d83e, 3968 |
| 4672 | 16 B x 5 | lamp_cols | | d83e |
| 3F00 | 6 B x 20 | lamp_map | | 898b |
| 3660 | w | time_scale | | many |
| 43B2 | w | tv_opt | F6 | cba0, 8113 |
| E556 | w | tv_azimuth | 0/0x4000/0x8000/0xC000 | 81f6, cba0, b3b3 |
| 2DFC | w | panel_strip | Alt-C | 2f82, 7898, cba0 |
| 2E34 | w | flip_rows | rows flipped | 2f82, 1bbc |
| B28C | w | acquiring | lock-on in progress | a17e, 7898 |

## Structures seen
* **TextCtx** (24 B, video.md): `+0 page, +2 flags, +4 fg, +6 bg, +8 x, +A y, +C font, +E ymin, +10 ymax, +12 xmin, +14 xmax, +16 self pointer (near)`.
* **Site record** (16 B at DS:B2A2 + 16*i, count `[C0B4]`; = START `CBDE` record): `+0 name idx (word, DEDC pointer table; 0 = none)`, `+2 x`, `+4 y`, `+6 threat/alive word`, `+8 flags (0x001 valid target, 0x008, 0x080 hidden, 0x100, 0x200 airfield / no hangar, 0x400, 0x800 hidden at ILS...)`, `+A unit type`, `+C count`, `+E object id (low 7 bits = type; name via DEDC)`.
* **Mission slot** (18 B at DS:E2D8 + 18*k): `+0 kind 0..8`, `+2 target site`, `+4 base/anchor site`, `+6 flags (bit 1 deadline)`, `+8 template`, `+A grid string`.
* **Air unit** (36 B at DS:C140 + 36*i): `+0 x`, `+2 y`, `+4 altitude`, `+0xE heading`, `+0x16 flags (bit 1 shown)`, `+0x18 state` (others R4/R5).
* **Missile track** (28 B at DS:3C0C + 28*i): `+0 x`, `+2 y`, `+8 heading`, `+0xE active`, `+0x10 type index into the 18-byte type table`.
* **Countermeasure marker** (12 B at DS:39AA + 12*i): `+0 x`, `+2 y`, `+6 kind (1 flare, 2 chaff, 3 decoy)`, `+8 life`.
* **Weapon record** (26 B at DS:366C + 26*w): `+0 name[12]`, `+12 type index into the 18-byte table at DS:3362` (Sidewinder 23, AMRAAM 22, HARM 24, Penguin 25, Harpoon 26, Maverick 27, Paveway 28, Rockeye/Durandal/Snakeye 29, Slick 30, ...).
* **Weapon type record** (18 B at DS:3362 + 18*t): `+0 name[8]`, `+8 range`, `+10 speed?`, `+0xC class (7 = AIM-9/AIM-120 style)`, `+0xE`, `+0x10`.
* **Lamp rect** (12 B x 37 at DS:46D8) {x, y, w, h, type, state}; **lamp map** (6 B x 20 at DS:3F00) {?, lamp, off value, current}.



---
# Part 8: assembly HUD, object pass 9ACA, projection (R8)

*Reader pass R8, original title: S3.6 spec part R8: front-cockpit HUD (segment 10A8) and world-object draw (9ACA + helpers)*

Reader R8. Address notation: `0000:OOOO` = segment 0 (image offset), `10A8:OOOO` = far segment (image offset 0x10A80+O).
`(V)` = verified in the disassembly, `(U)` = unclear / needs a runtime or visual check.

**Note on the brief**: DGROUP data is at *file* offset `0x1EA50 + DS_off` of `work/unpacked/VGAME.EXE` (the 0x820-byte EXE header is part of the file;
image offset of DS:0 is 0x1E230 as the brief says). Anything above DS:986C (e.g. DS:98D0) is BSS and absent from the file.

## Overview

* `10A8:0000` (`hud_draw_far`) is called once per frame by `1bbc` (the frame loop) **only when `[C07A]==0`** (front cockpit), right after `7898` (overlays),
  before `d9a3`/`17a3`/flip. It is a thunk to `10A8:000C` `hud_draw_front`.
* `10A8:0004` -> `09CE` `hud_color_init` (called from `01BF` = cockpit panel recolour, once per cockpit (re)draw). **09CE has no `ret`: both branches end in/jump to `0A22`, so `hud_color_init` FALLS THROUGH into `hud_layout_init`** (merge check S3.6: `0a1f mov [2ca1],al` is the last instruction, next function starts at 0a22; `09ff jmp 0a22`). Hence the layout is initialised at start-up by `01BF` (called from `00F0`) - no divide error; the `C` key only re-runs 0A22 after the toggle.
  `10A8:0008` -> `0A22` `hud_layout_init` (called only from the `C` key handler in `1e9e`, 0000:203F).
* `hud_draw_front` draws the **head-up display of the front view** straight into the draw page (page 1) with MGRAPHIC slots (text, line, blit_key,
  hud_tick_strip). Elements, in drawing order: airspeed tape, reference marks, altitude tape, heading tape, HUD glass frame lines, aiming reticle,
  G-load text, pitch ladder with numeric labels. It is parameterised by two persistent switches and a layout block that `hud_layout_init` fills:
  * `[294B]` = **HUD style** 0/1 (key `C`, scancode 2E, `1e9e` 0000:2033: `[294B] = ([294B]<1)`; reset to 0 by `39c4` at mission start; `2f82`/`bf71` save, zero and restore it
    around drawing in non-front views). Style 0 = wide layout (ladder window 160x68 at (80,16), tapes at x=49/255, 1 px per degree of heading).
    Style 1 = compact layout (ladder window 66x60 at (128,28), tapes at x=102/204, 0.4 px per degree). The same switch also changes the 3D object LOD and the
    projection scale (b85d, bd8f, dcc0 below).
  * `[294A]` = **declutter level** 0/1/2 (key handler in `cba0` 0000:CE76: `inc; if >2 then 0`; initial 0). Level 0: tapes + heading + everything. Level 1: heading
    tape and its centre tick, no airspeed/altitude tape, no tape reference marks. Level 2: no tapes and no heading; frame, reticle, G text and ladder remain.
* `9aca` is called by the scene draw `03ce` (every frame, every view) and is **the world-object renderer and target designator**: own aircraft (external views),
  ground-target (site) acquisition, the 16 particle slots, the unit list (`C140`), the 12 weapon slots (`3C0C`), a special marker object and the shadows.
  It never draws pixels itself: it calls `bd8f` (model draw -> `e7ac`), `dcc0` (billboard into a depth-sorted queue) and `873f`+`8929` (single pixels for far objects).
  Helpers `b85d/ba13/bbb4` are the world-to-screen projection and range/bearing math.

---------------------------------------------------------------------------------------------------------------------------

# Part 1: HUD (segment 10A8)

## Conventions used by the HUD code

*flip flag* = result of `flip_flag()` (slot 45): 1 once a `flip` copied, 0 before the first frame is flipped (page 0 = A000 = the screen; on the very first frame the HUD text
therefore goes to the screen directly - a port should always use the draw page 1). `line(x1,y1,x2,y2)` (slot 31; AX,BX,CX,DX) and `hud_tick_strip` (slot 11) draw on the **draw segment
+ draw offset** (the code assumes: draw segment = back page, draw offset = 0 outside the ladder; the 3D code clears the offset when it is done (U)). Text slots and `blit_key` use the page
numbers stored in their context structs and ignore the draw offset. Colours are flight-palette indexes: 0x0A, 0x0F, 0x2F, 0x07, 0x08.

## Context structures (DS words)

`TextCtx` (video.md section 3, 0x16 bytes) and `BlitCtx` (video.md section 8, 0x18 bytes; the instances are 0x1E apart: the 6 extra bytes at +0x18..+0x1D are `01, colour, 7, 0, 7, 0`, never read).

| Name | Address | Purpose |
|---|---|---|
| `hud_tx_tape` T0 | 284C | label text of the airspeed tape and of the altitude tape (x/y rewritten per label) |
| `hud_tx_head` T1 | 2862 | heading tape labels (x rewritten, y fixed) |
| `hud_tx_lad` T2 | 2878 | pitch ladder number labels |
| `hud_tx_g` T3 | 288E | G text |
| `hud_bl_head` B0 | 28A4 | heading tick bitmap (blit_key) |
| `hud_bl_ret` B1 | 28C2 | aiming reticle bitmap (blit_key) |
| B2, B3 | 28E0, 28FE | initialised (seg, page) but **never drawn** by this routine (sx=184, sy=16, 24x16 at (159,30)/(167,30); maybe used by 7898 (U)) |

Step 1 of every frame writes the flip flag into the `page` word of T0..T3 (284C, 2862, 2878, 288E) and B0..B3 (28AA, 28C8, 28E6, 2904).
Static fields: flags (word +2) = 2 in T0..T3 (bit0 clear: transparent, no extra rows), bg 0; T0 xmin/xmax = 0/320, T1 ymin/ymax = 0/200, T2 ymax = 89; blit contexts ymin 0, ymax 199, xmin 0, xmax 310/319
(clip fields unused: plain `blit_key`, not the clipped variant).

**Source of all blit bitmaps** = segment `[E558]` (= comm[20h], RE_GUIDE; hud_color_init copies it into B0..B3.seg): the **cockpit panel picture `256PIT.PIC` in the off-screen page**. I decoded it (tools/picdec.py):
its top-left corner holds the HUD resources: rows 0..29 x cols 0..140 = 10 phases x 3 rows of tick marks (colour 0x0F; period 10 px; rows 2 major tick, 1 row minor ticks every 5 px), rows 30..37 x cols 0..58 = 4 phases x
2 rows (period 4 px), the reticle for style 0 at (69,73) 25x15 (four 3-px ticks around a gap: a cross at the 12th column / 7th row) and the reticle for style 1 at (54,76) 13x9 (a small aircraft symbol).
`01BF` recolours these regions of the picture page `[4012]` with `rect_recolor`: (0,0)-(140,29), (0,30)-(64,37), (38,75)-(91,87): old->new = 0x0F->0x0A when `[43B0]!=0` (day), else 0x0A->0x0F (night; no visible change).
So by day tick bitmaps and reticle are green (0x0A), at night white (0x0F). The port must keep the picture page and apply the same recolour.

## 10A8:09CE  `void hud_color_init(void)`   (V)   [thunk 10A8:0004]

```c
B0.seg = B1.seg = B2.seg = B3.seg = [E558];             /* 28A4, 28C2, 28E0, 28FE */
if ([43B0] != 0) {   /* [43B0] = (comm[33h]==0): 1 = day (set in 00F0: cmp [3658],1 / sbb / neg) */
    T0.fg = T1.fg = T3.fg = 0x0A;  [28BD] = [28DB] = [2CA0] = 0x0A;
    T2.fg = 0x2F;                  [2CA1] = 0x2F;
} else {            /* night */
    T0.fg = T1.fg = T3.fg = 0x0F;  [28BD] = [28DB] = [2CA0] = 0x0F;
    T2.fg = 0x07;                  [2CA1] = 0x07;
}
```
T0.fg = DS:2850, T1.fg = 2866, T3.fg = 2892, T2.fg = 287C. `[2CA0]` = colour of tape ticks and reference marks, `[2CA1]` = colour of the pitch-ladder lines. Caller: `01BF`
(`lcall 10A8:0004`, then the three rect_recolors above); `01BF` is called from `2f82` and `cba0`.

## 10A8:0A22  `void hud_layout_init(void)`   (V)   [thunk 10A8:0008]

Fills the layout block for the current `[294B]` with `mov` immediates (style 1 at 0A2C..0B3F, style 0 at 0B40..0C55).
**There is no call at program start**: the only caller is the `C` key handler, so right after loading the block holds a static preset (`[292B]=0`, `[2922]=0` ...) that would divide by zero
in the heading code. The start-up initialiser is the fall-through from `09CE hud_color_init` (see above); a port calls `hud_layout_init()` after `hud_color_init()` at start and after every `C` toggle.

| DS | name | style 0 (`[294B]==0`) | style 1 | meaning |
|---|---|---|---|---|
| 291D | tape_y0 | 0x5A (90) | 0x4E (78) | y of label q when the remainder is 0 (top of text) |
| 291F | strip_dy | 0x11 (17) | 8 | tick-strip start row = y0 - strip_dy |
| 2920 | lab_step | 0x14 (20) | 0x0A (10) | label spacing (px) on both tapes |
| 2922 | rem_shift | 1 | 0 | remainder shift (style 0 = 2 px per 5 units) |
| 2923 | spd_x | 0x31 (49) | 0x66 (102) | x of airspeed labels |
| 2925 | alt_x | 0xFF (255) | 0xCC (204) | x of altitude labels |
| 291C | head_x0 | 0x6D (109) | 0x88 (136) | x (byte) of heading label 0 when px=0 |
| 2927 | head_step | 0x2D (45) | 0x12 (18) | px per 45 degrees = label spacing |
| 2929 | head_lim | 0xF8 (248) | 0xAF (175) | 4th heading label skipped if x of the 3rd >= this (never true) |
| 292B | tick_per | 0x0A (10) | 4 | tick bitmap period in px (= 10 degrees) |
| 292D | tick_odd | 5 | 2 | phase shift for odd 45-degree sectors |
| 292F | lad_xl | -50 | -23 | rung outer left x (relative to the ladder centre) |
| 2931 | lad_xi_l | -12 | -8 | rung inner left x |
| 2933 | lad_xi_r | 13 | 9 | rung inner right x |
| 2935 | lad_xr | 50 | 23 | rung outer right x |
| 2937 | lad_step | 0x34 (52) | 0x1A (26) | ladder px per 10 degrees of pitch |
| 2939 | lad_y0 | 0x68 (104) | 0x34 (52) | ladder base row (= 2 * lad_step) |
| 293A | lad_cx | 0x4F (79) | 0x1F (31) | ladder centre x inside the ladder window |
| 293C | lad_cy | 0x24 (36) | 0x20 (32) | ladder centre y inside the ladder window |
| 293E | lad_sy | 0x34 (52) | 0x3C (60) | **screen** y of the ladder centre (label placement) |
| 2940 | lad_sx | 0x9F (159) | 0x9F (159) | **screen** x of the ladder centre |
| 2942 | lad_ofs | slot 29 = 0x1450 (row 16, col 80) | 0x2380 (row 28, col 128) | draw offset of the ladder window |
| 2944 | lad_w | 0xA0 (160) | 0x42 (66) | ladder clip width (line clip x in 0..w inclusive) |
| 2946 | lad_h | 0x44 (68) | 0x3C (60) | ladder clip height (y in 0..h inclusive) |
| 2948 | g_x | 0x3C (60) | 0x6C (108) | G text x |
| 2898 | T3.y | 0x10 (16) | 0x16 (22) | G text y |
| 289A | T3.font | 0 | 2 | |
| 2858 | T0.font | 0 | 2 | tape label font |
| 285A / 285C | T0.ymin / ymax | 0x16 / 0x52 (22/82) | 0x22 / 0x4A (34/74) | tape label vertical clip |
| 286E | T1.font | 0 | 2 | heading label font |
| 286C | T1.y | 6 | 0x14 (20) | heading label y |
| 2874 / 2876 | T1.xmin / xmax | 0x5A / 0xE6 (90/230) | 0x82 / 0xBC (130/188) | heading label clip |
| 2884 | T2.font | 0 | 2 | ladder label font |
| 2886 | T2.ymin | 0x14 (20) | 0x1C (28) | ladder label clip |
| 288A / 288C | T2.xmin / xmax | 0x4E / 0xF1 (78/241) | 0x7E / 0xC3 (126/195) | |
| 28AC,28AE,28B0,28B2 | B0.dx,dy,w,h | 90,12,141,3 | 130,25,59,2 | heading tick bitmap destination |
| 28C4,28C6 | B1.sx,sy | 69,73 | 54,76 | reticle bitmap source |
| 28CA,28CC,28CE,28D0 | B1.dx,dy,w,h | 147,44,25,15 | 153,55,13,9 | reticle destination; its centre is (159,52) / (159,60) |

Fonts (video.md section 3): 0 = fixed 3x5 (+1 extra), 2 = fixed 3x4. The ladder centre on screen is (80+79, 16+36) = (159,52) / (128+31, 28+32) = (159,60); `b85d` projects a point straight ahead to the same place
(`sy += 52` / `60`).

## 10A8:000C  `void hud_draw_front(void)`   (V)

Pseudo-C of the whole routine; integer widths and shifts are the code's. Working words are DS variables (listed under "Globals written").

```c
void hud_draw_front(void) {
    /* 1. pages */
    uint8 ff = flip_flag();                         /* slot 45 */
    T0.page=T1.page=T2.page=T3.page=B0.page=B1.page=B2.page=B3.page = ff;
    level = [294A];  style = [294B];
    if (level == 2) goto frame;                     /* no tapes, no marks, no heading */

    /* ---------- 2. AIRSPEED TAPE (level 0 only) ---------- */
    if (level == 0) {
        v  = (uint16)[B168];                        /* airspeed, 50 per label (units U) */
        q  = v / 50;  r = v % 50;                   /* div dh (byte divide): #DE if q > 255 */
        dl = (uint8)(q - 1);                        /* 0xFF when q==0 */
        px = ((r << [2922]) / 5) & 0xFF;            /* 0..19 (style 0), 0..9 (style 1) */
        y  = (uint8)(px + [291D]);   T0.y = y;      /* byte add; the high byte of T0.y stays 0 */
        [2971] = dl;
        strip_row = (uint8)(y - [291F]);            /* [2952] */
        di  = (uint8)(dl << 1);                     /* byte offset into the label tables */
        cnt = 5;                                    /* [2955] */
        for (;;) {                                  /* 0078 */
            T0.y = (uint8)(T0.y - [2920]);          /* byte subtract: wraps mod 256, hidden by the y clip */
            T0.x = [2923];
            if (di >= 0x28) {                       /* unsigned: also catches 0xFE (q==0) */
                di = 0;
                if (--cnt == 0) break;
                strip_row -= [2920];                /* word subtract */
                continue;                           /* slot skipped; y keeps moving up */
            }
            [296C] = word[297E + di];  [296E] = word[29CE + di];   /* 4-char label + NUL at 2970, e.g. " 1"+"00" */
            text_clip_y_r(&T0, (char*)0x296C);      /* slot 1 */
            di += 2;
            if (--cnt == 0) break;
        }
        hud_tick_strip(0, strip_row, style, [2971]);/* slot 11; drawn in the colour current at that moment (U) */
    }

    /* ---------- 3. REFERENCE MARKS (levels 0,1) ---------- */
    color_set_r([2CA0]);                            /* slot 32 */
    if (style == 1) {
        if (level == 0) { line(122,60,124,60); line(196,60,198,60); }
        line(159,29,159,30);
    } else {
        if (level == 0) { line(73,52,76,52);  line(243,52,246,52); }
        line(159,16,159,18);
    }
    if (level == 1) goto heading;

    /* ---------- 4. ALTITUDE TAPE (level 0) ---------- */
    a  = (uint16)[2DF6];                            /* altitude, feet, 0..60000 */
    q  = a / 1000;  r = a % 1000;  [294C] = r;      /* word divide */
    px = ((r << [2922]) / 100);                     /* word divide: 0..19 / 0..9 */
    di = (int16)((q - 1) * 2);
    cnt = 5;
    if (di >= 0) {                                  /* q >= 1: thousands labels */
        T0.y = px + [291D];  [2971] = 2;
        strip_row = (uint8)(T0.y - [291F]);
    } else {                                        /* q == 0: hundreds labels "000".."900" */
        h  = r / 100;  f = r % 100;                 /* byte divides */
        dl = (uint8)(h - 1);
        px = ((uint8)(f << [2922])) / 10;           /* byte divide */
        T0.y = (uint8)(px + [291D]);  [2971] = dl;
        strip_row = (uint8)(T0.y - [291F]);
        di = (uint8)(dl << 1);
        for (;;) {                                  /* 01CC */
            T0.y = (uint8)(T0.y - [2920]);  T0.x = [2925];
            if (di >= 0x28) { di = 0; cnt--; strip_row -= [2920]; continue; }   /* NOTE: no test of cnt here */
            [2976] = byte[2A1F + di];               /* units digit of the 2-char number; [2977..2978]="00" -> "d00" */
            text_clip_y_r(&T0, (char*)0x2976);
            di += 2;
            if (--cnt == 0) goto strip2;
            if (di < 0x14) continue;
            di = 2; break;                          /* after "900" continue with " 1K", " 2K" ... */
        }
    }
    for (;;) {                                      /* 0224: thousands labels */
        T0.y = (uint8)(T0.y - [2920]);  T0.x = [2925];
        if (di == 0) text = (char*)0x297A;          /* "900" : q==1, slot below 1K */
        else { [2972] = word[2A1E + di]; text = (char*)0x2972; }   /* 2 digits then 'K' (DS:2974) then NUL */
        text_clip_y_r(&T0, text);
        di += 2;
        if (--cnt == 0) break;
    }
strip2:
    hud_tick_strip(2, strip_row, style, [2971]);    /* now in colour [2CA0] */

    /* ---------- 5. HEADING TAPE (levels 0,1) ---------- */
heading:
    H  = (uint16)[2DEE];                            /* heading, 0x10000 = 360 deg */
    t  = (uint16)(H - 0x2000);
    u  = (t >> 7) & 0x3F;                           /* position inside the 45-degree sector, 0..63 */
    px = (u * [2927]) >> 6;  [2954] = (uint8)px;    /* [2927] used as the 8-bit multiplier */
    x  = (uint8)[291C] - px;
    k8 = (t >> 10) & 0x38;                          /* sector*8 : (t>>8) >> 2 & 0x38 */
    T1.x = x;  [2950] = x;
    text_clip_xl_r(&T1, 0x2AAA + k8);               /* slot 2: label of sector k, left clip T1.xmin */
    k8 = (k8 + 8) & 0x3F;   x += [2927];  T1.x = x; text_noclip_r(&T1, 0x2AAA + k8);   /* slot 4 */
    k8 = (k8 + 8) & 0x3F;   x += [2927];  T1.x = x; text_noclip_r(&T1, 0x2AAA + k8);
    if (x < [2929]) {                               /* always true in practice */
        k8 = (k8 + 8) & 0x3F;
        memcpy(0x2AAA + k8 + 4, 0x2AAA + k8, 4);    /* the blank entry after a label takes its text: no visible effect */
        x += [2927];  T1.x = x;
        text_clip_xr_r(&T1, 0x2AAA + k8 + 4);       /* slot 3: right clip T1.xmax */
        ph = (uint8)[2954] % (uint8)[292B];         /* div dl */
        if ([2DEE] & 0x2000) ph += [292D];
        if (ph >= [292B]) ph -= [292B];             /* signed compare */
        B0.sy = (style==1) ? tab2AFE[ph] : tab2AEA[ph];    /* B0.sx = 0 */
        blit_key_r(&B0);                            /* slot 18: B0.w x B0.h from page [E558] to B0.page at (B0.dx,B0.dy), colour 0 transparent */
    }

    /* ---------- 6. HUD GLASS FRAME (all levels) ---------- */
frame:
    color_set_r(0x0F);
    if (style == 0) {
        line(46,90,46,14);  line(274,90,274,14);
        color_set_r(0x08);
        line(45,90,45,14);  line(275,90,275,14);
        line(275,14,232,2); line(232,2,90,2);  line(45,14,89,2);
    } else {
        line(98,90,98,30);  line(221,90,221,30);
        color_set_r(0x08);
        line(97,90,97,30);  line(222,90,222,30);
        line(222,30,209,17); line(209,17,110,17); line(110,17,97,30);
    }

    /* ---------- 7. RETICLE ---------- */
    if ([2956]) blit_key_r(&B1);                    /* [2956] set to 1 by 7898 and never cleared */

    /* ---------- 8. G TEXT ---------- */
    T3.x = [2948];  text_noclip_r(&T3, (char*)0x98D0);   /* slot 4 */

    /* ---------- 9. PITCH LADDER ---------- */
    ladder();   /* below */
}
```

### Notes on sections 2-5

* **Label strings.** `297E` = 40 words `" 0"," 0"," 1"," 1" ... "19","19"` (only the first 20 words are reachable because `di < 0x28`): entry i = decimal(i/2) padded to 2 chars. `29CE` = 40 words `"00","50","00","50"...`.
  Label i = both words = " 000"," 050"," 100" .. " 950". So the airspeed tape shows a label every 50 units and wraps at 1000. `2A1E` = 70 words `" 0"," 1".."69"` (the altitude labels use the word followed by `K` at DS:2974);
  `2A1F` is the same table read one byte off = the **units digit** (hundreds labels "d00": digit in 2976, "00" in 2977/2978). `297A` = "900".
  Heading labels at `2AAA`: 16 entries x 4 bytes: "000","   ","045","   ","090","   ","135","   ","180","   ","225","   ","270","   ","315","   ". Sector k = (t>>13)&7 selects entry 2k (byte offset 8k).
* **Label rows.** The first label drawn has index `q-1` at `y = y0 - lab_step` (y0 = px + tape_y0); the next ones are drawn `lab_step` higher. 5 slots are visited (`[2955]`); a slot whose index is >= 20 or < 0 is skipped (counter decremented,
  strip start row moved up by one label step), so at q==0 only 4 labels are drawn. Labels are clipped to rows T0.ymin..ymax (text_clip_y). y is a **byte** (wraps 246.. below 0 and is clipped away).
  Worked example, style 0, airspeed 0: " 000"@(49,50), " 050"@(49,30), " 100"@(49,10), " 150"@(49,246) clipped; reference mark at y=52. Altitude 0: "000"@(255,50), "100"@(255,30), "200"@(255,10), "300" clipped.
* **Altitude below 1000 ft**: hundreds labels "000".."900" (digit + "00"), then " 1K".. For 1000..1999 the lowest label (index 0) is replaced by "900" (`di==0`). For 2000+ the labels are " qK" from table 2A1E (up to 69K).
* **hud_tick_strip arguments**: `bx = strip_row`, `cl = [294B]`, `dl = [2971]` (tape A: `q-1` as a signed byte; tape B: 2, or `h-1` when q==0). In the driver `dl>=1` (signed) adds 20 to the start row (video.md section 6; columns 71/248 for style 0 [si=0,2],
  120/200 for style 1 [si=4,6]; visible rows 22..82 / 34..74; 10-iteration period). The first strip is drawn **before** `color_set_r([2CA0])`, i.e. in whatever colour was last set (U); the second after.
* **Heading**: 45 degrees = `[2927]` px (style 0: 1 px per degree). Labels are the 8 compass points every 45 degrees, always 3 digits (no letters). The 10/5-degree ticks only exist in the tick bitmap (10 phases x 3 rows for style 0 taken from source
  rows `tab2AEA[ph]` = 27,24,21,18,15,12,9,6,3,0; 4 phases x 2 rows for style 1 `tab2AFE[ph]` = 32,30,36,34).
* **G text string** `DS:98D0` (BSS) is built every frame in `1e9e` 0000:25B4..2605: `ltoa([98DC]/16)` (idiv: truncation to zero) + "." (DS:2D10) + `ltoa((|[98DC]| & 0xE) >> 1)` + "G" (DS:2D12).
  `[98DC]` = G estimate in 1/16 g, clamped to 0x80 (8 G). Quirk: the digit after the point is in **eighths** (0..7), not tenths.

### Pitch ladder (section 9)   (V)

The geometry is always computed; the drawing/labels are skipped when `[E542]==0`. `[E542]` is written by `1e9e` (0000:29C6..29F2): `[E542] = ([E2FE]==0) || (|pitch| - |roll|/2 > 0x1000)`; `[E2FE]` is a master/weapon mode word 0..2 (U).

```c
P  = (int16)[2DF0];                       /* pitch: 0x4000 = 90 degrees */
A  = (uint16)((P<0 ? -P : P) >> 6);       /* neg ax ; shr 6 */
t  = (A * 0x168) >> 8;                    /* quarter degrees: 90 deg -> 360  (dx:ax >> 8) */
q  = t / 40;  rem = t % 40;               /* 10-degree rung index, remainder (div bl) */
f  = (rem * [2937]) / 40;                 /* px inside the current 10-degree step */
if (P < 0) f = [2937] - f;
y0 = (uint8)(f + [2939]);                 /* byte add */
ch = q;  if (P < 0) ch = -ch - 1;         /* neg ch; dec ch  (int8) */
k0 = ch - 2;  [2C2C] = k0;                /* rung number of the first rung */
```
Five rungs `r = k0 .. k0+4` are generated; rung i is at `y = y0 - i*[2937]` (y downward: higher pitch = smaller y). The rung at `r = ch` lies `f` px **below** the ladder centre (positive pitch) - the centre is (0,0) in this frame.
Arrays: x at `DS:2B12`, y at `DS:2B82` (int16, indexed by byte offset 2*point), group line counts in bytes `DS:2BF2[2*i]`, `[2*i+1]` (g0, g1), group anchor point offsets in bytes `DS:2C0E[2*i]`, `2C0F[2*i]`
(stored by overlapping word writes: effectively a byte array), total line count `[2C2B]` (the `ch` accumulator). L=[292F], Mi=[2931], Ni=[2933], R=[2935], S=[2937]. Dispatch order of the code: r==9, r>9, r==-9, r<-9, r==0, r>0, else.

| r | points (x,y) | g0 | g1 | anchors | note |
|---|---|---|---|---|---|
| r == 0 or r == -9 | P0(L,y) P1(Mi,y) P2(Ni,y) P3(R,y) | 1 line (P0-P1) | 1 (P2-P3) | P0, P3 | horizon with a centre gap; **-90 degrees uses the horizon shape** |
| 1..8 | P0(L,y+5) P1(L,y) P2(R,y) P3(R,y+5) | 3 lines (P0-P1-P2-P3) | 0 | P1, P2 | solid, end ticks point down |
| r == 9 | P0(L,y) P1(R,y) | 1 | 0 | P0, P1 | zenith: plain solid line |
| r >= 10 | as 1..8 but legs at y-5 | 3 | 0 | P1, P2 | beyond the zenith |
| -8..-1 | P0(L,y-5) P1(L,y) P2(Mi,y) P3(Ni,y) P4(R,y) P5(R,y-5) | 2 (P0-P1-P2) | 2 (P3-P4-P5) | P1, P4 | dashed, legs point up |
| r < -9 | as -8..-1 but legs at y+5 | 2 | 2 | P1, P4 | beyond the nadir |

Per-rung stride in the arrays: 4 points for horizon/solid, 2 for r==9, 6 for dashed; the line total grows by 2, 3, 1, 3, 4, 4 respectively (rows in table order).

**Roll rotation.** `c = sin16(0x4000 - roll)`, `s = sin16(-roll)` (`109D:0088`: table interpolation, 16-bit angle, result ~+-0x7FFF); `roll = [2DF2]`. For every point (last to first):
```c
nx = ((c*x) >> 15) - ((s*y) >> 15);           /* hi16 of (2*P): shl ax,1 / rcl dx,1 */
ny = ((3*(c*y)) >> 17) + ((3*(s*x)) >> 17);   /* 3/4 aspect scaling, each product shifted separately (32-bit signed) */
```
**Lines** (only if `[E542]`): `color_set_r([2CA1])`; `offset_set_r([2942])` (slot 27); save `[2293]`,`[2295]`; `[2293]=[2944]`, `[2295]=[2946]` (clip window, inclusive); for every group, for every segment:
`clipline(x1+[293A], y1+[293C], x2+[293A], y2+[293C])` = `1001:0326` (Cohen-Sutherland against 0..[2293] x 0..[2295], then slot 31 `line` with the draw offset; the intersection rounding belongs to the 1001 reader (U)).
Group walk: `for g in groups: for n in count: draw(P[j],P[j+1]); j++; end; j++` (the extra `j++` skips the last point of the group); a zero count in the second group of a rung is skipped.

**Labels** (only if `[E542]`) - still inside the offset window, but text ignores the offset and uses absolute screen coordinates:
```c
idx = k0 + 11;                                        /* [2C2C] += 0xB ; label table DS:2C30, 4 bytes per entry */
a = (uint16)roll >> 10;  b = (uint16)(roll + 0x8000) >> 10;        /* 0..63 */
[2C98]=T264C[a]+2; [2C9A]=T26CC[a]; [2C9C]=T274C[a]+2; [2C9E]=T27CC[a];   /* "second (right-end) label" offsets */
[2C90]=T264C[b]-1; [2C92]=T26CC[b]; [2C94]=T274C[b]-1; [2C96]=T27CC[b];   /* "first (left-end) label" offsets  */
for (i = 0; i < 5; i++, idx++) {
    copy the 4 bytes at DS:2C30 + 4*idx to DS:2C8C;       /* NUL-terminated text */
    neg = ((int16)(4*idx) < 0x2C);                         /* rung < 0 */
    p = anchor0(i);                                        /* byte offset into the point arrays */
    T2.x = rx[p] + (neg ? [2C94] : [2C90]) + [2940];
    T2.y = ry[p] + ((int16)p < 0x2C ? [2C96] : [2C92]) + [293E];   /* BUG: compares the point BYTE offset with 0x2C, not the label index */
    text_clip_all_r(&T2, 0x2C8C);                          /* slot 6 */
    p = anchor1(i);
    T2.x = rx[p] + (neg ? [2C9C] : [2C98]) + [2940];
    T2.y = ry[p] + ((int16)p < 0x2C ? [2C9E] : [2C9A]) + [293E];
    text_clip_all_r(&T2, 0x2C8C);
}
offset_clear();  [2293]/[2295] restored;
```
`rx/ry` are the *rotated* points before the `[293A]/[293C]` translation (the labels use the absolute screen centre `[2940]/[293E]` instead). Both ends of a rung carry the same text.
Label table `2C30`, entry = rung r + 11 (r = -11..11): "-70","-80","-90","-80","-70","-60","-50","-40","-30","-20","-10","00","10","20","30","40","50","60","70","80","90","80","70" (mirrored beyond +-90).
Entry -1 (pitch = -90 exactly, first rung -12) reads bytes before the table (U, edge).
Offset tables (64 words each; index = roll>>10; the x tables are 264C and 274C, the y tables 26CC and 27CC; **26CC == 27CC**):
```
264C: 2,2,2,2,2,1,1,1,0,0,0,0,-1,-1,-2,-2,-3,-4,-4,-5,-5,-6,-6,-7,-7,-8,-8,-8,-9,-9,-9,-9,-9,-9,-9,-9,-9,-8,-8,-8,-7,-7,-6,-6,-5,-5,-4,-4,-3,-2,-2,-1,-1,0,0,0,0,1,1,1,2,2,2,2
26CC: -1,-2,-2,-3,-3,-4,-4,-5,-5,-6,-6,-6,-7,-7,-7,-7,-7,-7,-7,-7,-7,-6,-6,-6,-5,-5,-4,-4,-3,-3,-2,-2,-1,0,0,0,0,1,1,2,2,3,3,3,4,4,4,4,4,4,4,4,4,3,3,3,2,2,1,1,0,0,0,0
274C: 0,0,0,0,0,0,0,0,-1,-1,-2,-2,-3,-3,-4,-4,-5,-6,-6,-7,-7,-8,-8,-9,-9,-10,-10,-10,-11,-11,-11,-11,-11,-11,-11,-11,-11,-10,-10,-10,-9,-9,-8,-8,-7,-7,-6,-6,-5,-4,-4,-3,-3,-2,-2,-1,-1,0,0,0,0,0,0,0
27CC: = 26CC
```
(They keep the number just outside the rung end whatever the roll; the table for negative rungs is wider because of the minus sign.)

**Exit state**: `offset_clear()` (slot 25), `[2293]/[2295]` restored; the current colour is left at `[2CA1]` (or 0x0F when the ladder is off).

### Globals written by `hud_draw_front`
284C..2904 page words; 2854/2856 (T0.x,y), 286A (T1.x), 2880/2882 (T2.x,y), 2896 (T3.x), 28A8 (B0.sy), 2950 (heading x), 2952 (strip row), 2954 (heading px), 2955 (label counter), 2971 (strip selector), 294C (altitude remainder),
294E (label offset), 296C..296F/2972/2976 (label buffers), 2B12.. 2B82.. 2BF2.. 2C0E.. (ladder points/groups/anchors), 2C2A/2C2B/2C2C/2C2E (ladder counters), 2C8C..2C9E (label buffer, offsets), 2299/229B/229D/229F (line args), 2293/2295 (clip, restored).

---------------------------------------------------------------------------------------------------------------------------

# Part 2: world-object renderer and helpers (render3d / targeting)

Coordinate conventions seen (U for exact axes; owned by S3.7): "u" = 16-bit world units (1/32 of the 32-bit world values). Player: x32 `[BA10:BA12]`, y32 `[C02A:C02C]` (north is inverted: callers pass
`0x0100:0000 - y`), altitude `[2DF4]` (feet); `[C0A4]` = x in u, `[C0B2]` = y in u (both computed in 39c4); angles `[2DEE]` heading, `[2DF0]` pitch, `[2DF2]` roll. External camera (`[C07A]&0x80`): x32 `[B288:B28A]`, y32 `[B752:B754]`, z `[B75C]`
(set at the top of 2f82). `[3658]` = comm[33h] (night flag, 0 = day), `[365E]` = 1 for cockpit views (`([C07A]&0xC0)==0`, 2f82 0000:353A), `[294B]` = HUD style (also selects the 3D scale).

## 0000:B85D  `void r3d_project_point(int x_u, int y_u, int z)`   (V) - owner render3d
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 2; where they differ, render3d.md is right.

World point -> screen. Results in globals: `[49E0]` = sx (`0xFFFF` = not visible), `[49E8]` = sy, `[DE92]` = depth (negative in front), `[9500]` = unclipped sx of a rejected point.
```c
dx = [C0A4] - x;  dy = y - [C0B2];  dz = (z - [2DF4]) >> 5;                 /* sar */
if ([C07A] & 0x80) {                                  /* external camera */
    dx -= (int16)(([BA10:BA12] - [B288:B28A]) >> 5);
    dy -= (int16)(([C02A:C02C] - [B752:B754]) >> 5);
    dz -= (int16)(([B75C] - [2DF4]) >> 5);
}
X = ba13(0,dx,dy,dz);  Y = ba13(1,dx,dy,dz);  Z = ba13(2,dx,dy,dz);        /* 32-bit */
if (Z >= 0) goto invisible;
if ([294B]) { X >>= 1; Y >>= 1; }                      /* style 1: half scale (sar/rcr) */
if (!(Z <= X && X <= -Z)) goto invisible;              /* |X| <= -Z : 90 degree horizontal field */
sx = (X << 8) / Z + 0xA0;                              /* rtl_aFldiv */
sy = (Y << 8) / Z;  sy -= sy >> 2;                      /* x 3/4 */
sy += [365E] ? ([294B] ? 60 : 52) : 100;
[DE92] = Z >> 3;
if (sx < 0 || sx > 0x13F) { [9500] = sx; sx = -1; }
if (sy < 0 || sy > ([365E] ? 0x60 : 0xC7)) { [9500] = sx_original; sx = -1; }   /* window rows: 96 in cockpit views, 199 otherwise */
[49E0] = sx; [49E8] = sy;
return;  invisible: [49E0] = 0xFFFF;
```
(Z negative in front, so the X sign is mirrored; the formula is exact.) Callers: 9aca (x10), a17e, 99a7/90b9.

## 0000:BA13  `long r3d_matrix_row_dot(int row, int dx, int dy, int dz)`   (V)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 2; where they differ, render3d.md is right.

`return fmul(M[row][0],dx) + fmul(M[row][1],dy) + fmul(M[row][2],dz)` with the 3x3 matrix in DS from `B156`: element (row,col) at `B156 + 6*col + 2*row` (column-major), and `fmul(a,b)` = `109D:0006` = `(a*b) >> 14` with rounding
(imul, shl/rcl, shl/adc 0). The matrix is built by `e26b` (called from 2f82 with the view angles; owner render3d).

## 0000:BB86  `int site_dist(int site)`; 0000:BB9E  `int unit_dist(int unit)`; 0000:BBB4  `int obj_dist_bearing(int x_u, int y_u, int want_bearing)`   (V) - owner weapons (targeting leaf math)
> **S3.8:** owned by `flight.md` section 5.4 (verified there; where they differ, flight.md is right).

* `bbb4`: `dx = [C0A4]-x; dy = [C0B2]-y; if (want_bearing) [9502] = atan2_16(-dx, dy)` (`c9d6`); `[94FE] = dist2d(dx,dy)` (`c987`); returns the distance in AX. `[9502]` is **not** touched when `want_bearing==0`.
* `bb86(site)`: `bbb4(S.x, S.y, 1)` with the site record `DS:B2A4 + 16*site` (+0 x_u, +2 y_u). `bb9e(unit)`: `bbb4(U.x, U.y, 0)` with the unit record `DS:C140 + 36*unit`.

## 0000:BCD7  `void unit_terrain_class(int unit)`   (V)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 3; where they differ, render3d.md is right.

Sets `[49C6]` (terrain/shade class forwarded to the model/billboard queue):
```c
cls = TILE[(U.y >> 11) * 16 + (U.x >> 11)] & 3;       /* TILE = DS:B174 (si = y>>11 << 4, bx = x>>11) */
if (unit < [DED0] - 4) [49C6] = cls;
else                   [49C6] = (cls == 0 && [3D62] == 0) ? 1 : 0;      /* the last 4 units */
```
(U) the class meaning 0..3.

## 0000:BD8F  `void r3d_draw_model(int model, long x, long y, int alt, int ang1, int ang2, int ang3, int lod)`   (V) - owner render3d
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 1; where they differ, render3d.md is right.

Stack args: `[bp+4]` model id, `[bp+6..8]` x32, `[bp+A..C]` y32, `[bp+E]` altitude, `[bp+10]` ang1 (heading; negated), `[bp+12]` ang2 (pitch), `[bp+14]` ang3 (roll), `[bp+16]` LOD.
```c
mp = c6b8(model);                                      /* model id -> model pointer */
rx = x - [BA10:BA12];   ry = y + [C02A:C02C] - 0x01000000;   rz = alt - [2DF4];
if ([C07A]&0x80) { rx = rx - [B288:B28A] + [BA10:BA12];  ry = ry - [C02A:C02C] + [B752:B754];  rz = rz - [B75C] + [2DF4]; }
sh = lod - (([294B]==0) + 2);                          /* style 0: lod-3, style 1: lod-2 */
if (sh > 0) { rx <<= sh; ry <<= sh; rz <<= sh; }       /* rtl_lshl_mem on the 32-bit values (rz as 16-bit) */
if (sh < 0) { rx >>= -sh; ry >>= -sh; rz >>= -sh; }    /* rtl at F492 */
if (abs32(rx).lo < 0x7FFF && abs32(ry).lo < 0x7FFF) {  /* abs() on the long returns the low word compared here (U) */
    dca9(0, 0, -rz);                                   /* sets the camera z in 497C/497E/4980 */
    [E2FC] = 1;
    e7ac(mp, -ang1, ang2, ang3, rx, -ry, alt != 0);    /* model submission (render3d S3.7) */
}
```
Callers: 9aca (many), a17e, bee1, bf71.

## 0000:C8FA  `int scale_by_mode(int mode, int v)`; 0000:CB39  `int sign(int v)`   (V) - owner lib

* `c8fa`: `switch(mode){0: v>>1; 1: (v>>1)+(v>>3); 2: (v>>1)+(v>>2); 3: (v>>1)+(v>>2)+(v>>3); default: v}` (arithmetic shifts; 0.5, 0.625, 0.75, 0.875).
* `cb39`: `return v==0 ? 0 : v>0 ? 1 : -1`.

## 0000:DCC0  `void r3d_queue_billboard(int id, long x, long y, int z, int sx, int sy, int size)`   (V) - owner render3d
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 1; where they differ, render3d.md is right.

Camera-relative like bd8f (`dx = x - [BA10:BA12]`, `dy = y + [C02A:C02C] - 0x01000000`, `dz = z - [2DF4]`, external-camera variant identical to bd8f), then **`sar` by 3 (style 0) or 2 (style 1)**
(`cl = 2 + (style==0)`, dx,dy 32-bit, dz 16-bit). If the low words of |dx|,|dy| are < 0x7FFF: `dca9(0,0,-dz); [E2FC]=1;` rel = (dx, -dy, z != 0) minus `[497C]/[497E]/[4980]` stored to `[4964]/[4966]/[4968]`; `[497A]=2`;
`e8a2(rel...)` (projection/accept test: nonzero = reject). If accepted: `[4956]=id; [496A]=sx; [496C]=sy; [496E]=size; ddfe()`. `(sx,sy,size)` are the already computed screen values. Caller: 9aca (particles).

## 0000:DDFE  `void r3d_queue_insert(void)`   (V) - owner render3d
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 1; where they differ, render3d.md is right.

Billboard display list: entries of 0x16 bytes at `DS:49F0 + 0x16*n` (`n = [49C4]`, max 50) and an array of entry pointers sorted in `DS:4E3C` (2 bytes each).
```c
if (n == 50) { e = ptr[0]; ptr[0..48] = ptr[1..49]; n--; } else e = 0x49F0 + 0x16*n;      /* the farthest entry is dropped when full */
key = ([4960:4962]) >> (2*(4 - [E2FC]));   if ([E2FC]==2 && [497A]==5) key.hi += 0x20;
e->key_lo,key_hi = key;      /* +0,+2 : depth key, larger = farther */
e->rnd = rand_byte();        /* +4 : lcall 1088:0120 */
e->cls = [49C6];             /* +6 */
e->id = [4956]; e->rx=[4964]; e->ry=[4966]; e->rz=[4968];     /* +8,+A,+C,+E */
e->sx=[496A]; e->sy=[496C]; e->size=[496E];                    /* +10,+12,+14 */
i = n - 1; while (i >= 0 && !(ptr[i]->key > e->key)) i--;      /* unsigned 32-bit compare (hi, then lo) */
ptr[i+2 .. n] = ptr[i+1 .. n-1];  ptr[i+1] = e;  [49C4] = n + 1;          /* descending: index 0 = farthest = drawn first */
```
The queue is consumed by the render3d code (S3.7).

## 0000:9ACA  `void r3d_draw_world_objects(void)`   (V flow, U names) - owner render3d (contains the target designator = weapons)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 2; where they differ, render3d.md is right.

Called every frame by `03ce` after terrain and sky, no arguments. Locals: `bp-2E` best candidate (init -1), `bp-0E` best distance/limit, `bp-20` current-target distance, `bp-22` loop index, `bp-26` high-altitude flag, `bp-1C` shadow height.
Tables: sites `B2A4` (16 B, `[C0B4]`), units `C140` (36 B, `[DED0]`), particles `39DA` (8 B x16), weapon slots `3C0C` (28 B x12), tile map `B174`, unit model table `2FDC`, weapon model table `3372`.

1. **Own aircraft in view 0x8B**: if `[C07A]==0x8B`: `[49C6] = TILE[(C0B2>>11)*16 + (C0A4>>11)] & 3; r3d_draw_model(0x18, [BA10:BA12], 0x0100:0000 - [C02A:C02C], [2DF4]+0x10, [2DEE], [2DF0], [2DF2], 4)`. Model 0x18 = the player's F-117.
2. `if ([B28E]) [B28E]--` (target cycle cooldown).
3. **Ground-target acquisition** (9B31..9C6A) only if `([3D66] & 0x80)` (bit 7 = "select next"; `0xFFFF` = none and also has bit 7; the request is made by `or [3D66],0x80` in the cba0 key handler 0000:CE69) and `([DFDC]==0x13 || [DFDC]==0x19)` (selected weapon, (U) laser-guided bombs),
   `[B28E]==0`, `[E2FE]!=1` and not an external view:
```c
best = -1;  lim = 100 << (6 - [3658]);                 /* 6400 by day, 3200 at night */
cur_d = 0;
if ([3D66] != -1) { i0 = [3D66] - 0x80;  cur_d = site_dist(i0) - 1;  if (abs([E556] - [9502] + [2DEE]) > 0x2000) cur_d = 0; }
for (i = 0; i < [C0B4]; i++) {
    site_dist(i);                                      /* [94FE] distance, [9502] bearing; [E556] = view yaw 0/4000/8000/C000 (set by b3b3, cba0) */
    if (!( abs([E556] - [9502] + [2DEE]) < 0x2000 || ([E2FE]==0 && cur_d > 0) )) continue;   /* +-45 degrees of the view axis */
    if (i - [3D66] == -0x80) continue;                 /* the current target */
    fl = site[i].word6;  if (fl & 0x80) continue;
    if ([94FE] >= lim) continue;                       /* nearer than the best so far */
    if ([94FE] <= cur_d) continue;                     /* farther than the current one: cycling */
    if ([E2FE] != 2 && (fl & 0x501) <= 1) continue;
    if (i == 0 && !([B2A4] && [B2A6] && [B2B0])) continue;
    best = i;  lim = [94FE];
}
if (best & 0x80 /* -1 */) { if ([3D66] == -1) [B28E] = 4; else [3D66] = -1; }
else [3D66] = best;
```
4. **Particles** (9C6D..9D04): for `i` in 0..15, `P = DS:39DA + 8*i` (+0 x_u, +2 y_u, +4 z, +6 type); if `P.x != 0`: `b85d(P.x,P.y,P.z)`; if `[49E0] != -1 && -0x100 < [DE92] < 0`:
   `size = c8fa(P.type, abs(0x100/[DE92]))`; `dcc0(0x400 + ((P.type&0xE)>>1), P.x<<5, P.y<<5, P.z, [49E0], [49E8], size)` (smoke/explosion sprites, ids 0x400..0x407, (U)).
5. `[bp-26] = ([365E] && [2DF4]+[98F2] > 0x5DC)` (high above ground in a cockpit view: halves the depth and selects coarser LOD), `bp-0E = 0x4B << (6-[3658])` (4800 / 2400: nearest-target limit for step 7).
6. **Air-target setup**: if `([3D64]&0x80) && [3D64] != -1`: `bp-20 = obj_dist_bearing(unit(3D64-0x80).x, .y, 1)` but 0 if `abs([E556]-[9502]+[2DEE]) > 0x2000`; else `bp-20 = 0`. `bp-2E = -1`.
7. **Unit loop** `for i in 0..[DED0)-1`, `U = DS:C140 + 36*i` (layout below):
```c
[49C6] = 0;
if (!(U.flags & 2)) continue;                            /* +16h bit1 = active */
if (unit_dist(i) >= 0x12C0) continue;                    /* draw range 4800 u */
if ((([3D64]&0x80) || [94FE] < 0x100) && [94FE] < bp-0E && [94FE] > bp-20 && !([C07A]&0x80) &&
    !(U.flags & 0x20) && U.status != 0 && (obj_dist_bearing(U.x,U.y,1), abs([E556]-[9502]+[2DEE]) < 0x2000))
        { bp-0E = [94FE]; bp-2E = i; }                   /* air-target candidate (nearest, within 45 degrees) */
b85d(U.x, U.y, U.z);
if ([DE92] >= 0 || [49E0] == -1) continue;               /* behind or off screen: not drawn at all */
[DE92] >>= [bp-26];
if ([DE92] <= -0x20) { 8929(0x0F); 873f(sx,sy,sx,sy); continue; }       /* far: one white pixel */
if (U.z < 0x3E7 && [3658] == 0) {                        /* day shadow below 999 ft */
    bp-1C = 0;  s = site[[DFE0]] (nearest airfield);
    if ((s.hi_flags & 2) && abs(U.x - s.x) < ([9900]>>5) && abs(U.y - s.y) < ([9B06]>>5)) bp-1C = 0x80;
    if (!([2DF4] == 0x80 && bp-1C != 0x80))
        r3d_draw_model(4, U.x32, U.y32, bp-1C, U.hdg, 0, 0, 4 - sign(bp-26));      /* model 4 = ground shadow */
}
unit_terrain_class(i);  if (U.flags & 0x20) [49C6] |= 0x20;
alt = U.z - (([C07A]&0x80) ? 0 : 0x10);
variant = ([DE92] <= -0x10) ? 1 : 0;                      /* 1 = far LOD */
r3d_draw_model(WORD[2FDC + 32*U.type + 2*variant], U.x32, U.y32, alt, U.hdg, U.pitch, U.roll, 4 - [bp-26]);
```
   `873f(x1,y1,x2,y2)` = clipped line in the 3D view window context `[3FE2]` (`offset_set(pixel_offset(win.ymin,win.xmin))`, `[2293]=w-1`, `[2295]=h-1`, colour `ctx+4`, then `1001:032A`); `8929(c)` stores the colour into both window contexts (`[3FE2]+4`, `[3FFA]+4`).
8. After the loop: `if (bp-2E != -1) [3D64] = bp-2E; if ([3D64]&0x80) [3D64] = -1;` (a pending request that found nothing clears the air target).
9. **Weapon slots** (9FB8..A06B): `for i in 0..11`, `W = DS:3C0C + 28*i`; if `W.active (+0E)`: `b85d(W.x,W.y,W.z)`; if visible: if `[DE92] <= -0x20` `8929(i<8 ? 0x29 : 0x0D); 873f(pixel)` else
   `r3d_draw_model(WORD[3372 + 0x12*W.type(+10)], W.x<<5, W.y<<5, W.z, W.a(+8), W.b(+A), W.c(+C)+0x2000, 3 - sign(bp-26))`. (Slots 0..7: SAM/missile slots filled by `det_site_radar_update`; 8..11 other projectiles (U).)
10. **Special object** (A06B..A0CD): if `[C602] > 0`: `b85d([C0A6],[C0B8],[C602])`; if `-0x100 < [DE92] < 0`: `r3d_draw_model(0x0B, [C0A6]<<5, [C0B8]<<5, [C602], 0,0,0, [B756]>0 ? 6 : 5)` (a marker; (U)).
11. **Own aircraft in external views** (A0CD..A17B): if `([C07A]&0x80) && [C07A]!=0x8B` and not (`[2DF4]==0 && [C06E]`): set `[49C6]` from the tile and `r3d_draw_model(0x18, own x32, own y32, [2DF4]+0x10, [2DEE],[2DF0],[2DF2], 4)`;
    if also `[2DF4] < 0x3E8 && [3658]==0`: `r3d_draw_model(0x11, own x32, own y32, 0, [2DEE], 0, 0, 4)` (shadow model 0x11).
Writes: `[3D64] [3D66] [49C6] [B28E] [DE92] [49E0] [49E8]` plus what the callees write.

---------------------------------------------------------------------------------------------------------------------------

## Open items

| # | Item |
|---|---|
| 1 | RESOLVED by the merge: `09CE hud_color_init` falls through into `0A22 hud_layout_init`; `01BF` -> `10A8:0004` -> 09CE therefore initialises the layout at start-up (the DGROUP preset with `[292B]=0` is never used). |
| 2 | Colour of the first tick strip: `hud_tick_strip` is called before `color_set_r([2CA0])`, so it uses the previously set colour (end of 7898 or last frame's 0x2F/0x0F). Check visually that it matches the second strip. |
| 3 | Rounding of the Cohen-Sutherland clip in `1001:0326` (used by the ladder) belongs to the 1001 reader; needed to match ladder pixels exactly. |
| 4 | Units of `[B168]` (airspeed, 50 per label) and the difference between `[2DF6]` (tape altitude, feet) and `[2DF4]` (3D altitude). |
| 5 | `[E2FE]` meaning (master mode 0..2), `[DFDC]` weapon ids 0x13/0x19, `[49C6]` classes, particle `type` word, slot 0..7 vs 8..11 colours, model 0x0B marker, meaning of the site record fields. |
| 6 | The tick/reticle bitmaps are **in the panel picture** (top-left corner) - the port must keep 256PIT.PIC in the off-screen page and apply the 01BF recolour. |
| 7 | Declutter key: `cba0` 0000:CE76 (key code not decoded here); style key `C` (1e9e 0000:2033). |
| 8 | B2/B3 (28E0, 28FE) initialised but unused here; possibly used by 7898. |
| 9 | Ladder label index -1 (pitch exactly -90 degrees) reads outside the label table. |

## DGROUP globals seen

| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 294A | b | hud_level | declutter level 0..2 | cba0 w; 7898, 000C r |
| 294B | b | hud_style | HUD style 0/1 | 1e9e, 2f82, 39c4, bf71 w; many r |
| 2956 | b | hud_ret_on | reticle enable | 7898 w; 000C r |
| 2CA0 | b | hud_col_a | tape/mark colour | 09CE w; 000C, 7898, 99a7, a17e, b339, b4f3, b5af, bf71 r |
| 2CA1 | b | hud_col_lad | ladder colour | 09CE w; 000C, 7898 r |
| 284C..2904 | w | hud_ctx | TextCtx T0..T3, BlitCtx B0..B3 | table above |
| 291C..2948 | w | hud_layout | layout block | 0A22 w; 000C r |
| 2952,2954,2955,2971 | b/w | hud_tmp | tape temporaries | 000C |
| 296C..2976 | b | hud_lblbuf | tape label buffers | 000C |
| 297A | s | str_900 | "900" | static |
| 297E, 29CE, 2A1E | tab | hud_lab_* | label tables (tens; 00/50; 2-char numbers) | static |
| 2AAA | tab | hud_head_lbl | 16 x 4-byte heading labels | static (blank slots overwritten) |
| 2AEA, 2AFE | tab | hud_tick_phase | tick bitmap source rows | static |
| 264C, 26CC, 274C, 27CC | tab | hud_lad_off | ladder label offsets by roll | static |
| 2C30 | tab | hud_lad_lbl | ladder label strings | static |
| 2B12, 2B82, 2BF2, 2C0E | tab | hud_lad_pts | ladder points/groups/anchors | 000C |
| E542 | b | hud_ladder_on | pitch ladder enable | 1e9e w |
| 98D0 | s | hud_gtext | G text "x.yG" | 1e9e w (BSS) |
| 98DC | w | g_load | G in 1/16 g | 1e9e |
| E558 | w | seg_panel | panel picture segment (comm[20h]) | 0010 w; 09CE, 9154, 91a4 r |
| 43B0 | w | is_day | 1 if comm[33h]==0 | 00F0 w; 01bf, cba0, 09CE r |
| 3658 | w | night | comm[33h] | 00F0, cba0 w |
| 365E | w | cockpit_view | 1 in cockpit views | 2f82 w |
| 49E0, 49E8, DE92 | w | proj_sx, proj_sy, proj_depth | b85d results | |
| 94FE, 9502 | w | tgt_dist, tgt_bearing | bbb4 results | |
| E556 | w | view_yaw | view azimuth 0/0x4000/0x8000/0xC000 | b3b3, cba0 w |
| 3D64, 3D66 | w | air_target, gnd_target | 0xFFFF none, bit7 = select-next request | cba0, 9aca |
| B28E | w | tgt_cooldown | | 9aca |
| 49C4, 49F0, 4E3C | | bb_queue | count, entries, sorted pointers | ddfe |
| 4956..4980 | | bb_tmp | billboard/model temporaries | dcc0, e7ac |
| 2FDC | tab | unit_model_tab | model ids [type][16] | static |
| 3372 | tab | wpn_model_tab | model id per weapon type (0x12 bytes per entry) | static |
| B174 | tab | tile_class | terrain class map | loader |
| B156 | 18 B | cam_matrix | 3x3 rotation, column-major | e26b |

## Structures seen

* **Unit record** (36 = 0x24 bytes, base `DS:C140`, count `[DED0]`; updated by `5730 ai_update_units`): +0 x_u, +2 y_u, +4 z (feet), +6/+8 x32 (lo/hi), +A/+C y32, +E heading, +10 pitch, +12 roll, +14 type (index into `2FDC`), +16 flags (bit1 active, bit5 0x20 static/on ground), +18 status (0 = dead), +1A..+23 not touched here.
* **Site record** (16 bytes, base `DS:B2A4`, count `[C0B4]`): +0 x_u, +2 y_u, +6 flags word (0x80 excluded, 0x501 mask for validity, high-byte bit1 = airfield), +8.. unknown; record 0 needs `[B2A4],[B2A6],[B2B0]` non-zero.
* **Particle** (8 bytes x16, base `DS:39DA`): +0 x_u (0 = unused), +2 y_u, +4 z, +6 type.
* **Weapon slot** (28 = 0x1C bytes x12, base `DS:3C0C`): +0 x_u, +2 y_u, +4 z, +8 a, +A b, +C c (angles), +E active, +10 type (index into the 0x12-byte entries at `3372`).
* **Billboard queue entry** (0x16 bytes x50, base `DS:49F0`) - see `r3d_queue_insert`; pointer array `DS:4E3C`.
* **HUD contexts** TextCtx/BlitCtx as in video.md; layout block `DS:291C..2948`.



---
# Part 9: scene overlay pass A17E and object structures (R9)

*Reader pass R9, original title: S3.6 spec R9 - world-object overlay (`a17e`), HUD target symbology helpers, object-model glue*

Range: `0000:a17e` (4539 B) + its helpers `1a85 b339 b3b3 b3e6 b4f3 b5af b6ae b77a b7f8 ba67 bad1 bbed bc12 bc77 bcac bee1 f492` (18 functions).
VGAME.EXE unpacked, DS = 1E23 (file offset = DS offset + 0x1EA50). Marks: (V) verified in the disassembly, (U) unclear / inferred.
Names of other readers are used where they exist (`obj_find_at`, `obj_wreck_shape`, `dmg_unit_kill` = 71b4, `dmg_object_destroy` = 72a4, `msg_show` = 8b93, `log_event` = 499e,
`snd_play` = d6bf, `button_light` = d83e, `wpn_effect` = 8bab, `fps` = DS:3660, `skill` = 3654, `war_level` = 3656, `step_counter` = 3D60, `cockpit_visible` = 365E).

Name map to the symbols published by other readers (vgxref): `499e = mission_log_event` (= my `log_event`), `8b93 = msg_post` (= `msg_show`), `8bab = wpn_suit` (= `wpn_effect`), `74f6 = dmg_player_blast` (= `player_hit`),
`bb86 = wpn_site_dist` (= `obj_dist`), `7470 = mission_target_done`, `bcd7 = r3d_unit_terrain_class`, `bf71 = mfd_view_render` (3D target preview), `b85d = r3d_project_point`, `1001:024c = fill_rows_far` (called here as a rectangle fill
`(ctx, 0xA9, 0x6E, 0xF5, 0xA9)` = x 169..245, y 110..169), `8a5e = mfd_fill`, `873f = hud_line_view`, `88f6 = hud_line_win`, `8ad3 = hud_text2` (page-aware text), `8b04 = txt_draw` (explicit ctx).

## 0. Overview - what `a17e` is

`a17e` is NOT a world-object update loop (units/sites/missiles are moved elsewhere: `5730`, `65d4`, `62d0`, `671a`, `465b`). It is the **per-frame overlay pass of the 3D scene**:
the only caller is `03ce` (`r3d_scene_draw`): order inside 03ce = `dae8` sky/ground -> `04da` terrain -> `9aca` objects -> `db3e` -> **`a17e`** (R3 section 3). It does five jobs, in this order:

| # | job | code | side effects on the simulation |
|---|---|---|---|
| S1 | one small box marker per live projectile slot (`DS:3C0C`) | a194..a1e2 | none (draw only) |
| S2 | **cannon tracers** (`DS:E44E`): project, draw the streak, test hits against units / the player / the ground | a1e2..a4d9 | **kills units** (`71b4`), **destroys ground objects** (`72a4`), **damages the player** (`74f6`), bullet freed, starts the explosion, posts messages |
| S3 | explosion sprite + countdown (`9D0C`) | a4d9..a576 | counter; `[98FE]=-1`, `[9D0E]=0` |
| S4 | target-designation symbology (only if `[365E]`): ground-target box + weapon eligibility/lock decision (`[E55C]`, lock flag), floating site/unit name "ping", **auto designation under the boresight**, target info panel + 3D target preview (`bf71`), **photo-reconnaissance shutter**, air-target box + readout, bomb-drop cue, radio-beacon marker | a576..b316 | writes `3D66` (selected site), `[E55C]`, `[9B05]` bits, `[9D08] [B28C] [41F2] [41F4]`, objective completion (`7470`), event log (`499e`), cockpit lamp 3 |
| S5 | clear the right MFD panel when photo mode ended | b316..b335 | draw only |

Everything is drawn through the HUD primitives of R7 (`8929` set colour, `873f` line, `88f6` clipped line, `871f` full-screen line, `8ad3` text, `8b04` text with explicit ctx).
`b85d(x,y,z)` (R8) = world point -> screen: sets `DS:49E0/49E8` (`0xFFFF` in 49E0 = not visible) and `DS:DE92` (camera depth>>3, negative = in front).
Coordinates 320x200. The projector only accepts points with depth < 0, `0 <= sx <= 0x13F`, `0 <= sy <= 0x60` (`0xC7` when `[365E]==0`) (V, b9d0..ba05).

Frame-state notation: `px = [C0A4]`, `py = [C0B2]` (player position in site units; = `(ba10+0x10)>>5` and `0x8000-((c02a+0x10)>>5)` computed in 39c4), `alt = [2DF4]`, `hdg = [2DEE]`, `pitch = [2DF0]`, `roll = [2DF2]`
(16-bit angles, 0x4000 = 90 deg; sin/cos helpers `caee(a,m) = sin(a)*m`, `cb07(a,m) = cos(a)*m`, both `(table*m)>>14`), `hud_colour = (int8)[2CA0]` (15 white at start),
`mono = [43B2]` (when set all readout text/boxes use colour 0), `fps = [3660]`.

---------------------------------------------------------------------------------------------------------

## 1. `0000:a17e` `obj_overlay_frame(void)` (owner simflow; also weapons/damage side effects)

### 1.0 Prologue
```
hud_photo_prev = hud_photo_mode;  hud_photo_mode = 0;          /* [9504] = [9D08]; [9D08] = 0 */
```

### 1.1 S1 projectile markers (V)
```
for (i = 0; i < 12; i++) {
    if (proj[i].life == 0) continue;                           /* [3C1A + 28*i] */
    b85d(proj[i].x, proj[i].y, proj[i].z);                     /* +0,+2,+4 */
    if (r3d_sx == 0xFFFF) continue;
    hud_target_box(r3d_sx, r3d_sy, 6, 0, i < 8 ? 0x11 : 0x0A);  /* slots 0..7 enemy missiles, 8..11 player weapons */
}
```
(`hud_target_box` is a no-op when `[365E]==0`.)

### 1.2 S2 cannon tracers
`hit_scale = 378 / isqrt((fps + 2) << 2)` (`2f40` = integer square root by Newton iteration, returns 1 for n < 4). With fps = 4 -> isqrt(24)=4 -> 94.
Bullet slots `bullet[0 .. B16C+3]` (12-byte records, `B16C = gun_slots = clamp(2*fps, 3, 16)`): slots `0 .. B16C-1` = **the player's rounds** (spawned by `465b`), slots `B16C .. B16C+3` = **enemy rounds** (4 consecutive slots, spawned by `5730`).
Record: `+0 x, +2 y, +4 z, +6 vx, +8 vy, +0xA vz` (words, site units; `x == 0` = free). They are advanced by `465b` (`pos += vel` every simulation step), not here.
```
for (i = 0; i < gun_slots + 4; i++) {
    b = &bullet[i];  if (b->x == 0) continue;
    b85d(b->x, b->y, b->z);                       p0 = (r3d_sx, r3d_sy)           /* kept in bp-0x38 / bp-0x3E */
    b85d(b->x + (b->vx>>1), b->y + (b->vy>>1), b->z + (b->vz>>1));   p1 = (r3d_sx, r3d_sy)   /* streak end = position + half a step */
    if (p1.x == 0xFFFF || p0.x == 0xFFFF) continue;     /* !! invisible bullets skip the WHOLE rest of the iteration, hit tests included (V) */
    set_colour(i >= gun_slots ? 0x29 : 0x0D);            /* 8929: 0x0D for the player's rounds, 0x29 for enemy rounds (DAC indices; look (U)) */
    line(p1.x, p1.y, p0.x, p0.y);                         /* 873f (clipped to the 3D window) */
    hit = 0;
    if (i < gun_slots) {                                  /* ---- player round vs units ---- */
        for (j = 0; j < n_units; j++) {                   /* n_units = [DED0], includes the 4 airfield objects */
            u = &unit[j];  if (!(u->flags & 2)) continue; /* flags word at +0x18 */
            d = abs(abs(b->x-u->x) + abs(b->y-u->y) + (abs(b->z-u->z) >> 5));   /* 16-bit, z is 1/32 scale */
            if (hit_scale / (skill + 1) <= d) continue;   /* (idiv; skill 0..3) */
            u->flags |= 0x10;                              /* low byte */
            dmg_unit_kill(j);                              /* 71b4: sets [98FE]=j, kills++, flag 0x20, event 3/0x83, strcpy(9876, type name), snd 5 */
            strcat(9876, " destroyed by gunfire");         /* DS:41F6 */
            msg_show(9876);                                /* 8b93 */
            b->x = 0;                                      /* bullet consumed but the unit loop CONTINUES with the same (now x==0) bullet (V quirk: one round can kill several units) */
            fx_expl_x = u->x; fx_expl_y = u->y; fx_expl_z = u->z;     /* [C0A0] [C0AA] [C0B0] from unit +2,+4,+6 */
            fx_expl_timer = 0x10;
        }
    } else {                                              /* ---- enemy round vs the player ---- */
        d = abs(abs(b->x-px) + abs(b->y-py) + (abs(b->z-alt) >> 5));
        if (d < 0x20) {
            hit = 1;
            msg_show("Hit by gunfire");                    /* DS:420C */
            if (d < 0x20 / (4 - skill)) player_hit();      /* 74f6 (R6): damage bit(s); skill is 0..3 so no div-by-zero; skill==3 -> threshold 32 = always */
        }
    }
    if (hit) { fx_expl_x = b->x; fx_expl_y = b->y; fx_expl_z = b->z; fx_expl_timer = -1; b->x = 0; }   /* a34a..a375 */
    if (b->z < 0) {                                         /* below ground: impact, runs for every class of round, even if b->x was just zeroed */
        if (fx_expl_timer <= 0) { fx_expl = b position (b->x may already be 0!); fx_expl_timer = -1; }   /* a383..a39f */
        b->x = 0;
        o = obj_find_at(fx_expl_x, fx_expl_y);               /* bad1 -- uses the EXPLOSION position, which is stale when a bigger explosion (timer>0) is running */
        if (o != -1 && !(obj[o].flags & 0x80)) {
            if (dmg_wobj_destroyable(<stack garbage = [C0A0]>)) {   /* 780a is called WITHOUT arguments: its [bp+4] is the left-over stack word = [C0A0] (V); only picks which wreck id is compared */
                wx = wobj.+4 >> 5;  wy = 0x8000 - (wobj.+8 >> 5);   /* [9F10] wobj ptr returned by bad1 */
                dist = c987(fx_expl_x - wx, fx_expl_y - wy);        /* octagonal distance */
                if ((wpn_effect(0x12, o) * 6) / (skill + 2) > dist) {   /* store 18 row of DS:386A is used as the cannon's lethality table (U naming) */
                    dmg_object_destroy(o);                           /* 72a4 */
                    strcat(9876, " destroyed by gunfire");           /* DS:421B, 72a4 left the object name in 9876 */
                    msg_show(9876);
                    fx_expl_timer = 0x10;  fx_expl_z = 0;            /* no sprite (z==0) but the timer runs */
                }
            }
        }
    }
}
```
Notes: (1) the unit loop and the ground test use `b->x` as the "alive" marker only for the next frame; (2) `b->z < 0` means the round has gone below ground level (z is the same
scale as the aircraft altitude; ground = 0 here, terrain relief is NOT consulted); (3) both message strings are identical texts stored twice (41F6 / 421B).

### 1.3 S3 explosion animation (V)
```
if (fx_expl_timer == 0) { fx_target_down = 0; goto S4; }          /* [9D0E] = 0 */
if (fx_expl_z > 0 && fx_expl_unit >= 0)                            /* [98FE] = unit that was just killed (set by 71b4): the fireball follows it while it falls */
    { fx_expl_x = unit[fx_expl_unit].x; fx_expl_y = .y; fx_expl_z = .z; }
b85d(fx_expl_x, fx_expl_y, fx_expl_z);
if (r3d_sx != 0xFFFF) {
    size = abs(0x100 / r3d_depth);                                 /* de92 != 0 for any accepted point */
    if (fx_expl_z > 0) fx_explosion_sprite(r3d_sx, r3d_sy, size, 0x10 - abs(fx_expl_timer));   /* frames 0..15; timer -1 shows frame 15 once */
}
fx_expl_timer -= sign(fx_expl_timer);                              /* cb39 */
if (fx_expl_timer == 0) fx_expl_unit = -1;
```
The timer is decremented once per call (= once per rendered frame, not per simulation step).

### 1.4 S4 target symbology (all guarded by `[365E] != 0`; otherwise the function returns here)
Common: `lock = 0` (bp-0xC), `[E55C] = 0` ("target inside seeker cone"). Weapon selection used by all sub-blocks:
`st = [364E]` (bay 0..3), `wid = bay[st].id` (`[363E+4*st]`), `qty = bay[st].qty` (`[3640+4*st]`), `kind = store[wid].proj` (`[3678+26*wid]`: -1 camera, -2 fuel tank, 0 guns,
else projectile prototype index), `cls = proto[kind].cls` (`[336E+18*kind]`: 4 HARM, 5 Penguin/Harpoon, 6 AGM-65, 7 air-to-air, 28/29/30 bombs), `proto[kind].range` = `[336A+18*kind]`.

**S4a - ground-target box** (condition `tgt_mode == 2 && view == 0 ([C07A]) && sel_site >= 0`) (V a592..a74a):
```
b85d(site.x, site.y, 0);   colour = hud_colour;  box_mode = 0;
cap = wpn_effect(wid, sel_site);                                  /* 8bab: lethality 0..8 of this store vs the site's class */
ok  = (cap != 0 && qty != 0);
if (ok && cls == 4 && site.+6 == 0) ok = 0;                        /* HARM needs an emitting site */
if (ok && cls == 5 && !obj_cls_ship(sel_site)) ok = 0;             /* Penguin/Harpoon need a ship class */
if (!ok) { if (kind != -1) colour = 6; }                           /* red when not eligible (camera keeps HUD colour) */
else if (proto[kind].range > (-r3d_depth) / 7) {                   /* idiv by -7 of de92: within envelope */
    if (kind == 0x1C) {                                            /* LGBOMB: altitude based */
        rng = obj_dist(sel_site);                                  /* bb86 */
        lim = (alt >> 5) * 5;
        if (lim > rng && r3d_depth < 0) { hud_in_cone = 1; box_mode = 1; if (lim/2 > rng) { lock = 1; colour = 0x29; } }
    } else if (cls != 7) {                                         /* guided: reticle test, centre (0xA0, 0x34) */
        if (c987(sx - 0xA0, sy - 0x34) < 0x30) {
            hud_in_cone = 1; box_mode = 1;
            if (proto[kind].range > ((-r3d_depth) >> 2)) { lock = 1; colour = 0x29; }
        }
    }
}
if (r3d_sx != 0xFFFF) hud_target_box(sx, sy, cap ? cap + 5 : 9, box_mode, colour);     /* a6d6 */
if (kind == 0x1C) {                                                 /* a703: drop-point diamond */
    b85d(site.x + caee(hdg,0x80), site.y - cb07(hdg,0x80), alt);   /* 128 units beyond the site along the aircraft heading at flight altitude */
    if (r3d_sx != 0xFFFF) hud_draw_diamond();
}
```
(The region test uses `r3d_depth` left by the `b85d` of the site; the `kind==0x1C` branch re-projects only at the end.)

**S4b - floating name "ping"** (`announce_timer [3D6A] > 0 && announce_idx [C07E] >= 0`; a74a..a792): `b85d(site[idx].x, .y, 0); hud_ping_label(DEDC[site[idx].name], [9872], fps - [3D6A])`.
Afterwards unconditionally: `[9B05] &= ~2`; `[3FE2]+2 = 4; [3FFA]+2 = 4` (TextCtx flags).

**S4c - ground/nav readouts** (condition `[DFDC] == 0x13 && (tgt_mode == 2 || tgt_mode == 0)`; a7b3..ad29):
1. Designation request: if `[9B05] & 1` (toggled by a key command in `cba0` at `cbbc`, R2; the same command sets nothing else here):
```
p = pitch;  dist = (p < 0) ? cb07(p, alt) / (caee(-p, 0x20) + 1) : 0x280;     /* horizontal distance where the boresight meets z=0 */
gx = caee(hdg, dist) + px;  gy = py - cb07(hdg, dist);
s = obj_find_at(gx, gy);                                                          /* bad1; may register scratch site 0 */
if (s != -1 && (tgt_mode != 0 || (site[s].flags & 0x501) > 1)) {                  /* mode 0 only accepts neutral(0x100)/friendly(0x400) sites */
    sel_site = s;  if (last_dead_site [3D68] == 0) [3D68] = -1;
    [9B05] &= ~1;                                                                  /* request is cleared ONLY on success -> retried every frame */
}
```
2. `if ([41F4]) [41F4]--;`
3. `if (sel_site == -1) { hud_lock_indicator(1, lock); goto S4d; }`   (note: `[E55C]` is still 0 -> lamp 3 off)
4. `sel = (uint8)sel_site & 0x7F` (bp-0x34) -- NB 3D66 may transiently hold `0x80|k` set by 9aca's auto-acquire search; elsewhere the full word is used (U).
5. **Photo mode** if `kind == -1` (camera) **and** `[9B04] & 4` (bay doors open): 
```
[9B05] |= 2;  hud_photo_mode = 1;  hud_photo_inframe = 0;
for (k = 0; k < 2; k++) {                                       /* the two mission slots mt[k] at E2D8 + 0x12*k */
    if (mt[k].kind == 1 /*photograph*/ && mt[k].target == sel && !(site[sel].flags & 0x80)) {
        d  = obj_dist(sel);                                      /* bb86 -> also [9502] = bearing */
        h  = abs([9502] - hdg) >> 4;
        e  = c9d6(-(alt >> 5), d);                               /* elevation angle of the site seen from the aircraft */
        pe = abs(e - pitch + 0x6EF) >> 4;                        /* camera is mounted 0x6EF below the nose line */
        if (h + pe + d < (4 - skill) << 7) hud_photo_inframe |= k + 1;
    }
}
if (hud_shutter_cd == 0 && (byte)[4088] != 0 && (cb72(1) != 0 || input_fire_prev != 0)) {   /* shutter */
    hud_shutter_cd = 4;
    set_colour(0); fill_rect(ctx 3FFA, 0xA9, 0x6E, 0xF5, 0xA9);        /* 1001:024C: blank the right MFD panel on page 2 */
    strcpy(9876, "Frame "); strcat(9876, ltoa(++hud_photo_count, DEAA, 10));  text(9876, 0xD1, 0x8E, colour 15);
    snd_play(0x12, 2);
}
if (hud_shutter_cd == 0) {                                          /* a9c5 */
    [98CE] = pitch - 0x6EF;
    bf71(obj_wreck_shape(sel), site.x, site.y, 0, 0, 0, 0, 3, 0);    /* camera preview of the site model in the right panel */
    [3FE2]+6 = 0x2E; [3FE2]+2 = 1;                                    /* opaque, bg colour 0x2E */
    text_ctx(3FE2, (step_counter&1) ? "      " : "Camera", 0xC5, 0xC1, colour 0x2F);        /* 8b04: blinking label bottom-left of the right panel */
}
if (hud_shutter_cd == 3 && hud_photo_inframe) {                        /* one frame after the shot */
    msg_show("Target photographed.");                                 /* DS:4246 */
    if ((inframe & 1) && !([9B05] & 0x40)) { 7470(0); log_event(0x8A, sel); }      /* primary objective */
    if ((inframe & 2) && !([9B05] & 0x20)) { 7470(1); log_event(0x4A, sel); }      /* secondary objective */
}
goto S4d;   /* aa8d jumps to a885 only to reuse a 2-word stack clean-up; hud_lock_indicator is NOT called in photo mode (V) */
```
Caveats (V): the shutter test needs `[4088] != 0` (the draw target is the second page), so a shot is taken only on frames drawn into page 2; 41F4 is decremented once per call but the `== 3` test fires only in the frame where it reads 3.
6. **AGM-65 TV view** if not photo mode, and `[43B4] == 0 && [B04C] > 8 && proj[B04C].life != 0 && proj[B04C].type == 0x1B`:
```
bf71(obj_wreck_shape(sel), site.x, site.y, 0, 0,0,0, 4, -1);  hud_lock_indicator(0, lock);
text_ctx(3FE2 flags=1 bg=0x2E, (step&1) ? "       " : "Missile", 0xC5, 0xC1, 0x2F);   /* hud label blinks */
```
(`B04C > 8` excludes slot 8 itself: a Maverick launched into slot 8 is never tracked - original off-by-one, V).
7. **Normal target panel**:
```
bf71(obj_wreck_shape(sel), site.x, site.y, 0, 0,0,0, 1, -1);
hud_lock_indicator(1, lock);  hud_update_aspect_label();
hud_format_range(obj_dist(sel));  text(9876, 0xB5, 0x9B, mono ? 0 : hud_colour);                      /* "Range n.n km" */
strcpy(9876, DEDC[site.objtype & 0x7F]);  text(9876, 205 - 2*strlen, 0x70, col);                       /* type name, right aligned at x=205 */
s2 = DEDC[site.name];  strcpy(9876, strlen(s2) ? " at " : "");  strcat(9876, s2);  text(9876, 205 - 2*strlen, 0x76, col);   /* " at <place>" */
if (tgt_mode == 0) { b85d(site.x, site.y, 0); hud_target_box(sx, sy, 8, 0, hud_colour); }    /* no -1 check */
else if (sel_site == mt[0].target && mt[0].kind < 5) text("Primary Target",   179, 0x7C, col);       /* DS:4271 */
else if (sel_site == mt[1].target)                   text("Secondary Target", 173, 0x7C, col);       /* DS:4280 */
else if (!(step_counter & 1)) {                                                                         /* "No Target": forbidden object */
    forbidden = (war_level < 2 && (C604[objtype] & 0xC0) != 0) || (site.flags & 0x500) || (B174[(site.x>>11) + 16*(site.y>>11)] & 1);
    if (forbidden) text("No Target", 189, 0x7C, col);                                                    /* DS:4291 */
}
```
(`col = mono ? 0 : hud_colour`; B174 = the 16x16 level-3 cell grid, `>>11` = 2048 site units per cell.) The A-G / mode 0 readouts are drawn on the page the caller is currently rendering; text() = `8ad3(str, x, y, colour)`.

**S4d** `ad29`: `input_fire_prev = cb72(1)` (button 1 = trigger/shutter; `cb72(i) = [43BE+2*i] + (joystick enabled ? joy_button(i) : 0)`).

**S4e - air target** (`tgt_mode == 1`) (V ad32..af69):
```
if (view == 0 && (sel_unit & 0x80) == 0) {                          /* sel_unit valid (-1 or >=0x80 fail the test); NB (uint8) test */
    u = &unit[sel_unit];  b85d(u->x, u->y, u->z);
    if (r3d_sx != 0xFFFF) {
        colour = 6; box_mode = 0;
        if (qty != 0 && cls == 7) {                                 /* air-to-air missile */
            colour = hud_colour;
            if (c987(sx - 0xA0, sy - 0x38) < 0x30 && proto[kind].range > ((-r3d_depth) >> 3)) {
                hud_in_cone = 1; box_mode = 1;
                if (proto[kind].range > ((-r3d_depth) >> 2)) { lock = 1; colour = 0x29; }
            }
        }
        hud_target_box(sx, sy, 9, box_mode, colour);
    }
}
```
In this block `qty` = `bay[st].qty` and the test is `qty != 0` (V ad88). (`kind` was fetched from the selected bay as in S4a.) For the UNIT case the cone test is **stricter** than the ground case: `range > depth/8` is required for `E55C`.
If `[DFDC] == 0x13 && tgt_mode == 1` (ae06..af69) the air readout:
```
if (sel_unit == -1) { hud_lock_indicator(1, lock); goto S4f; }
sel = (uint8)sel_unit & 0x7F;  obj_depth_class(sel);                        /* bcd7: [49C6] */
bf71(actype[unit.type].model (+0x1A), u.x, u.y, u.z, u.heading (+0x10), u.pitch (+0x12), u.roll (+0x14), 1, 1);
hud_lock_indicator(1, lock);  hud_update_aspect_label();
hud_format_range(c987(py - u.y, px - u.x));  text(9876, 0xB5, 0x9B, col);
strcpy(9876, actype[type].name); strcat(9876, actype[type].name + 7 /*nickname*/);  text(9876, 0xB9, 0x74, col);   /* "MIG-23 Flogger" left aligned */
if (sel == 0 && mt[0].kind >= 5) text("Primary Target", 179, 0x7C, col);                                  /* DS:429B */
if ([98DE] != 0 && (step_counter & 1)) [9506] = ...;                         /* dead store, 9506 is never read (V) */
```
`[3FE2]+2 = 2; [3FFA]+2 = 2` at af69 (TextCtx flags restored).

**S4f - delayed unit name ping** (`announce_timer > 0 && announce_idx < 0`, af7b..afc9): `u = ~announce_idx; b85d(unit[u].x,.y,.z); hud_ping_label(actype[unit[u].type].name, [9872], fps - [3D6A])`.

**S4g - free-fall bomb cue** (`tgt_mode == 2 && view == 0`, afc9..b1f3; weapon = selected bay):
```
if (kind == 0x1E (FFBOMB) && abs(roll) < 0x2000) {                          /* impact-point pipper, shown before the target is chosen */
    a  = fm_bomb_dive_angle();                                              /* bbed */
    r  = cb07(a, alt) / (caee(-a, 0x20) + 1);
    gx = caee(hdg, r) + px;  gy = py - cb07(hdg, r);                         /* bp-0x2E/-0x30 */
    b85d(gx, gy, 0);
    if (r3d_sx == 0xFFFF) { r3d_sx = 0xA0 + (caee(roll, 0x5A - [C684]) * 4) / 3; r3d_sy = 0x5A; }   /* off-screen: pin to the bottom edge, shifted by roll */
    else hud_target_box(sx, sy, 5, 1, 0x29);
    set_colour(hud_colour);  88f6(0xA0, [C684], sx, sy);                    /* line from the screen reference point (160, C684) */
}
if ((kind == 0x1E || kind == 0x1D) && sel_site >= 0) {                     /* RTBOMB/FFBOMB: range bar + release-point diamond (b0c2..b1f0) */
    b85d(site.x + caee(hdg,0x80), site.y - cb07(hdg,0x80), alt);        /* 128 units beyond the site along the heading, at flight altitude */
    if (r3d_sx != 0xFFFF) {
        hi = 0x3A + ([294B] == 0 ? 6 : 0);                                   /* cmp byte [294B],1 / sbb / and 6 / add 0x3A */
        if (kind == 0x1E) {                                                  /* FFBOMB: distance pipper -> site (gx,gy = bp-2E/-30 of the pipper block above; STALE if that block was skipped because |roll| >= 0x2000) */
            bar = clamp(c987(gx - site.x, gy - site.y) >> 3, 0, hi);
            too_low = ((unsigned)((0x5DC - [B168]) * 2) > (unsigned)alt);
        } else {                                                             /* RTBOMB */
            bar = clamp(obj_dist(sel_site) >> 3, 0, hi);                      /* bb86([3D66]) */
            too_low = ((unsigned)[B168] > (unsigned)alt);
        }
        [DE92] = bar;                                                        /* the depth variable is reused as the bar half-width */
        if (!too_low || (step_counter & 1)) {                                /* blinks on odd steps when too low */
            set_colour(0x29);
            line(0x9F-bar, 0x21, 0x9F-bar, 0x1E);                            /* 873f: left tick  (y 33 -> 30) */
            line(bar+0xA0, 0x21, bar+0xA0, 0x1E);                            /* right tick */
            line(0x9F-bar, 0x1E, bar+0xA0, 0x1E);                            /* bar at y = 30 */
        }
        hud_draw_diamond();                                                  /* b339 (colour = hud_colour) at the displaced point; always drawn */
    }
}
```
(`c94e(v,lo,hi)` is the clamp; `[B168]` = a minimum release altitude set by 1e9e (U), `0x5DC` = 1500.)

**S4h - radio beacon (supply drop)** (b1f3..b316), for the two mission slots, executed independently of the HUD page:
```
for (k = 0; k < 2; k++) {
    m = &mt[k];  if (!(m->flags(+6) & 0x10)) continue;                /* "beacon active" */
    if (!(deadline [DE88] > mission_seconds [98E2])) continue;
    s = m->target;  d = obj_dist(s);  if (d >= 0xC80) continue;      /* 3200 units */
    if (s == sel_site) { b85d(site.x + caee(hdg,0x80), site.y - cb07(hdg,0x80), alt); if (visible) hud_draw_diamond(); }
    b85d(site.x, site.y, 0);  [9872] = 0x0A;
    hud_ping_label("Radio Beacon", 0x0A, step_counter % fps);          /* DS:42AA */
    if (step_counter % (fps * 8) == 0) {
        strcpy(9876, "Radio beacon bearing "); strcat(9876, ltoa(c9d6(site.x - px, py - site.y) / 0xB6, DEAA, 10));  /* DS:42B7, degrees */
        msg_show(9876);
        if (d < 0x100 && bay[0].qty != 0) msg_show("Drop supplies now");     /* DS:42CD -- tests bay 0 only (V) */
    }
}
```
Condition note: this loop runs only when `tgt_mode`/`DFDC` allow? No: the `b1f3` entry is reached from every path of S4 (it is unconditional inside the `[365E]` branch) (V).

### 1.5 S5 (b316)
`if ([DFDC] == 0x13 && hud_photo_prev != 0 && hud_photo_mode == 0) 8a5e(2, 0x9F);` - clears the right MFD panel (169..245 x 110..169 on the current page) with colour 0x9F when camera mode has just ended.

### 1.6 Order and ownership summary
Called every rendered frame (not every sim step). It never moves objects; position integration is `465b` (bullets), `671a` (projectiles), `5730` (units).
The only cross-frame state it owns: `fx_expl_*`, `hud_photo_*`, `hud_shutter_cd`, `hud_photo_count`, `input_fire_prev`, `[9872]`, `[42E0]`.

---------------------------------------------------------------------------------------------------------

## 2. Helper functions (all small; owner simflow unless stated)

### 0000:b3e6 `hud_target_box(x, y, size, hex, colour)`  (V)
`if (!cockpit_visible) return; if ([294B]) size >>= 1; set_colour(colour);` Edges: `L = x-size, R = x+size, t = size - (size>>2)` (3/4 size, arithmetic), `T = y-t, B = y+t`.
`hex == 0`: rectangle outline with the 4 lines of `88f6` (clipped): L side, bottom, R side, top.
`hex != 0`: hexagon with `q = t>>1`: vertices `(x,T) (R,y-q) (R,y+q) (x,B) (L,y+q) (L,y-q)` closed (6 `88f6` lines). Clipping window of `88f6` (passes `xmin,xmax,ymin,ymax` after the line args to `87a9`, same order as the full-screen `871f` call): `[294B] ? x 99..220, y 30..90 : x 48..271, y 15..90`.

### 0000:b339 `hud_draw_diamond()`  (V)
`set_colour(hud_colour)` (`8929([2CA0])`), then four `88f6` lines: `(x-4,y)-(x,y-4)`, `(x,y-4)-(x+4,y)`, `(x+4,y)-(x,y+4)`, `(x,y+4)-(x-4,y)` with `(x,y) = (r3d_sx, r3d_sy)` (no visibility test).

### 0000:b3b3 `hud_update_aspect_label()`  (V)
`q = ((hdg - [DED4]) + 0x2000) & 0xC000; if (q == 0x4000 || q == 0xC000) q ^= 0x8000;` (swaps left/right); `if (q != [E556]) { [E556] = q; 81f6(); }` (81f6 redraws ' Ahead '/' Rear '/' Right '/' Left ' at (197,193); [DED4] is written by `bf71` = heading of the previewed target).

### 0000:b4f3 `hud_seeker_cross(animate)`  (V) AGM-65 TV seeker reticle
`if (!cockpit_visible) return; set_colour(mono ? 0 : hud_colour); g = 0; if (animate) { g = word[42E2 + 2*[42E0]]; if (++[42E0] > 5) [42E0] = 0; }` Table DS:42E2 = {0,2,4,6,4,2}.
Four ticks with `871f` (unclipped full-screen line) around (207,140): horizontal `(200-g,140)-(206-g,140)` and `(208+g,140)-(214+g,140)`; vertical `(207,133-g)-(207,139-g)` and `(207,141+g)-(207,147+g)`.

### 0000:b5af `hud_lock_bracket(animate)`  (V) converging square (right panel)
`if (!cockpit_visible) return; set_colour(mono?0:hud_colour); s = [42F2] (=12); t = s - (s>>2) ...` precisely: `s = 12; ty = s - (s>>2)`; if `animate`: `s = word[42F2 + 2*[42E0]]; if (++[42E0] > 6) [42E0] = 0; ty = s - (s>>2)`. Table DS:42F2 = {12,10,8,6,4,2,0}.
`xr = c94e([B04E] + s, 0xA9, 0xF5); xl = c94e([B04E] - s, 0xA9, 0xF5); yb = c94e([B050] + ty, 0x6E, 0xA9); yt = c94e([B050] - ty, 0x6E, 0xA9)` (`c94e(v,lo,hi)` = clamp with a wrap quirk) then 4 `871f` lines (left, bottom, right, top).
Quirk (V): b4f3 and b5af share the single index `[42E0]` but wrap at different limits (5 / 6), so alternating use desynchronises the animations.

### 0000:b6ae `hud_lock_indicator(mode, lock)`  (V)
```
if (!cockpit_visible) return;
[B04E] = 207; [B050] = 140;                                         /* panel centre */
if ([43B4] == 0 && [B04C] > 8 && proj[[B04C]].life != 0 && proj[[B04C]].type == 0x1B && [E554] == 4) hud_seeker_cross(1);   /* live Maverick in flight, TV preview active */
if (hud_in_cone) {                                                  /* [E55C] */
    if (mode) hud_lock_bracket(lock ? 1 : 0);
    if (lock == 0) { on = (step_counter & 1); }                      /* blink */
    else           { if ([4706] != 0) return; on = 1; }             /* steady, only if not already lit */
    button_light(3FE2, 3, on); button_light(4012, 3, on);
} else if ([4706] != 0) { button_light(3FE2, 3, 0); button_light(4012, 3, 0); }     /* [4706] = state word of cockpit lamp 3 */
```
(`d83e(ctx, 3, on)` = lamp 3 at (184,94) 21x8, `DS:46D8 + 36`, state at `DS:4706`; this is the "target locked" lamp.)

### 0000:b77a `hud_ping_label(s, colour, dy)`  (V)
`if (r3d_sx == 0xFFFF) return;` Box: `if (dy < sx && 0x13F - dy > sx && dy < sy && 0x5C - dy > sy) hud_target_box(sx, sy, dy, 1, colour)` (expanding hexagon, nothing when `dy <= 0` fails the margin tests only for tiny sizes).
Text: `if (0x14 < sx < 0x118 && 0 < sy < 0x4C) text(s, sx - 2*strlen(s), sy + 5, colour=[9872])` (note: the text colour is the global `[9872]`, which is BSS 0 until the beacon code stores 0x0A - original quirk).

### 0000:b7f8 `hud_format_range(dist)`  (V)
`9876 = "Range " + ltoa(dist >> 6) + "." + ltoa((((uint8)dist & 0x3F) * 2) / 13) + " km"` (strings DS:430A "Range ", 4311 ".", 4313 " km"; ltoa radix 10 into DS:DEAA). 64 site units = 1 km; the decimal is 0..9.

### 0000:1a85 `fx_explosion_sprite(x, y, size, frame)`  (V) (owner render3d)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 1; where they differ, render3d.md is right.
`ctx = ([0348] ? [4042] : [0344])` (3D window ctx or alternate); `sprite_scaled(ctx, x - size/2, y - size/2, size, size - (size>>2), off = word[9188 + 4*frame], seg = word[918A + 4*frame])` (driver slot 22; `size/2` rounds toward zero).
Table DS:9188 = 18 far pointers (off, seg 0x1853, a statically relocated data segment of the EXE): offsets {0, 213, 553, 1355, 2064, 2553, 3290, 4340, 5906, 7610, 9742, 11945, 14242, 16208, 17492, 18509, 19201, 19620} (frames 0..17; a17e uses 0..15).
The sibling `1ad6` (R11a) is the same with table DS:91D8.. (not in this range).

### 0000:bad1 `obj_find_at(x, y)` -> site index  (V)
> **S3.8:** owned by `flight.md` section 5.4 (verified there; where they differ, flight.md is right).
```
wobj = [9F10] = 0d08((long)x << 5, (long)(0x8000 - y) << 5);      /* terrain-object query (R3); returns pointer to DS:BA14 struct or 0 */
if (wobj == 0) return -1;
x2 = (wobj.+4:+6) >> 5;  y2 = 0x8000 - ((wobj.+8:+A) >> 5);       /* object centre in site units (low words) */
for (i = 1; i < n_sites; i++) if (site[i].x == x2 && site[i].y == y2) return i;
site[0].x = x2; site[0].y = y2; site[0].+0E = wobj.+0 + 0x100;      /* register the unknown object in the scratch record 0 (flags/name/alert of record 0 are NOT cleared) */
if (sel_last_dead [3D68] == 0) [3D68] = -1;
return 0;
```
Callers: a17e (S2 ground impact, S4c designation), 671a, 753f via proj.target (R6). Record 0 therefore represents "an anonymous world object last picked".

### 0000:bc12 `obj_wreck_shape(o)` / 0000:bc77 `obj_cls_soft(o)` / 0000:bcac `obj_cls_ship(o)`  (V)
> **S3.8:** owned by `flight.md` section 5.4 (verified there; where they differ, flight.md is right). (bc12 itself is render3d.)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 3; where they differ, render3d.md is right.
```
cls(o)        = C604[site[o].+0E & 0x7F] & 0x0F;                  /* object-type -> target class table loaded from the WLD */
obj_cls_soft  = cls in {9, 0x0B, 0x0C, 0x0D};     obj_cls_ship = cls in {0x0C, 0x0D};
obj_wreck_shape(o):
    if (!(site[o].flags & 0x80)) return site[o].+0E;                              /* intact: stored id */
    v = shape_map(site[o].+0E)  /* c6b8: if (id & 0x100) DS:0510[(id&0x7F)] | (id & 0x8000) else id */;
    if (byte_table[ A60[(v>>8)*2] ][v & 0xFF] & 0x20) return site[o].+0E | 0x100;  /* this shape has its own destroyed variant (bank+1) */
    return (obj_cls_soft(o) ? (int8)C0AC : (int8)DEA4) + 0x100;                     /* generic wreck from the WLD header ('N' / 'B') */
```
Used by the target-preview windows (`bf71`) and, via `9aca`/terrain code, to pick the model of a destroyed site.

### 0000:b85d-dependent: `ba67`, `bee1` (owner render3d, callers `c834` R10)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 2; where they differ, render3d.md is right.
* `ba67(a, b, c, d, e, f)` (V): `dx = b - e; dy = (c - f) >> 5; dz = d - a;` `[DE92] = (mul14([4992], dz) + mul14([4998], dy) + mul14([499E], dx)) >> 3` where `mul14(a,b) = (a*b + 0x2000) >> 14` (lcall 109D:0006) and DS:4992/4998/499E = the camera forward vector components (U who fills them: e8b5 reads them). Result = depth of the point along the camera axis; negative = in front.
* `bee1(obj, x32, y32, z, a, b, c)` (V): queue a 3D object relative to the camera: `v1 = (long)[B76A:B76C] + y32 - 0x01000000; dz = z - [B808]; v0 = x32 - (long)[B766:B768]`; if `abs(v0) < 0x7FFF && abs(v1) < 0x7FFF`: `dca9(0, 0, -dz)` (origin), `[E2FC] = 1`, `e7ac(obj, -c, ?, v0, -v1, b, a, (a != 1) ? ...)`: exact pushes: `e7ac(obj, -[bp+0x10], a_hi:lo = [bp+0x12..0x14], v0_lo, -v1_lo, flag = ([bp+0xE] == 0 ? 1 : 0))` (see disassembly bee1: pushes `(bp+0xe==0)`, `-v1`, `v0`, `[bp+0x14]`, `[bp+0x12]`, `-[bp+0x10]`, `[bp+4]`). Sibling of `bd8f` (R8) which adds the rear-view branch (`[C07A] & 0x80`) and the LOD shift argument `[bp+0x16]`.

### 0000:bbed `fm_bomb_dive_angle()`  (V) (owner flight)
> **S3.8:** owned by `flight.md` section 2.3 (verified there; where they differ, flight.md is right).
`t = (alt + 0x1000) & 0xFFFF; return (((0x4000 - abs(pitch)) << 12) / t) - 0x4000;` (32-bit shift/divide). FFBOMB launch stores `bbed() - 0x400` in `proj.+0x16` (R6).

### 0000:f492 `rtl_aFlshr_mem(long *p, int n)`  (V) (owner lib)
`*p = rtl_aFlshr(*p, n)` arithmetic right shift in place (callers `bd8f`, `dcc0`). Its sibling `f476` is the left shift.

---------------------------------------------------------------------------------------------------------

## 3. The world-object model as seen from A17E (glue between START data, simulation and rendering)

### 3.1 The five object populations
| population | array (DS) | record | count | who creates | who moves/updates | destroyed by | drawn by |
|---|---|---|---|---|---|---|---|
| ground objects ("sites") | `B2A2` | 16 B | `[C0B4]` (record 0 = scratch) | START (.WLD) -> comm+7Ah -> `4c41`; record 0 also by `bad1` at runtime | flags/alert by 4f2a/516d/559a | `72a4` (flag 0x80, wreck model via `bc12`) | `04da/9aca` (terrain objects through `0d08`); markers by a17e |
| units (aircraft/ships/vehicles) | `C13E` | 36 B | `[DED0]` (last 4 = airfield objects) | START (.WLD units) -> comm -> `4c41`; the last 4 re-seeded by `39c4` when the nearest airfield `[DFE0]` changes; traffic aircraft by `65d4` | `5730` (every step) | `71b4` (flag 0x20) | `9aca` via `bd8f` |
| projectiles (missiles, bombs) | `3C0C` | 28 B | 12 (0..7 enemy, 8..11 player) | `62d0`/`516d` (enemy), `753f` (player) | `671a` | life reaches 0 (`671a`) | `9aca` via `bd8f`; marker box in a17e S1 |
| cannon tracers | `E44E` | 12 B | `[B16C]+4` | `465b` (player), `5730` (enemy) | `465b` | `x := 0` by a17e on hit/ground impact, or by age (5730 clears the rotating slot) | a17e S2 (line only) |
| explosion | `C0A0/C0AA/C0B0`, `9D0C`, `98FE`; unit explosion `C0A6/C0B8/C602` + `B756` | scalars | 1 + 1 | `671a`, `71b4`, a17e | a17e S3 (timer) | timer 0 | sprite by a17e; `9aca` draws 3DX model 0x0B at `C0A6/C0B8/C602` while `C602 > 0` (lod arg 6 if `[B756] > 0` else 5) |

### 3.2 From START to runtime objects
START exports the world into the shared buffer comm+7Ah (`5625`, R START). VGAME `4ad0` (R1) sets `[DE8E] = 1`, resets the cursor to comm+7Ah (`4d2f`) and calls `4c41`, which copies with `4cef(dst, size)` (movedata far -> DS; with `[DE8E]==0` the same code copies DS -> far, used by `4c29` at exit). Order and destinations (V, 4c41):

| order | size | VGAME destination | START source | meaning |
|---|---|---|---|---|
| 1 | 1 | `DEA4` | D88C byte 0 | generic wreck shape id for ordinary objects (the file starts with 'B','N' = 0x42,0x4E) |
| 2 | 1 | `C0AC` | D88C byte 1 | wreck shape id for "soft/surface" classes 9,11,12,13 |
| 3 | 2 | `C0B4` | D33C | number of sites `n` (records 0..n-1; START's index 0..2 are scratch) |
| 4 | 2 | `98FC` | CACE | number of leading sites that carry a threat level |
| 5 | 2 | `E540` | E084 | first site index of the non-target block |
| 6 | 16*n | `B2A2` | CBDE | sites |
| 7 | 2 | `DED0` | D890 | number of units |
| 8 | 36*units | `C13E` | D3BE | units |
| 9 | 128 | `C604` | D7F6 | object type -> class table (low nibble = weapon target class; bits 0x40/0x80 = off-limits in Cold/Limited war) |
| 10 | 128 | `C0BA` | D33E | second type table (U, not read by R9) |
| 11 | 750 | `DFEA` | DD70 | NUL separated names pool |
| 12 | 256 | `B174` | CAD8 | 16x16 level-3 cell grid; bit0 = "no target" cell (a17e S4c), bits 0|1 = excluded in 0d08 |
| 13 | 2 | `DE88` | D876 | mission deadline |
| 14 | 2 | `3D6E` | CABC | special unit (locked/leader) index, -1 none |
| 15 | 16 | `2E6C` | 15B2 | 4 waypoints (x,y) |
| 16 | 36 | `E2D8` | E05E | two mission slots (primary / secondary) |

At the end `4c29` writes the same blocks back (DS -> comm) plus `0x600` bytes from `BA2A` (event log) so that END can derive the debrief. `4ad0` afterwards rebuilds the name pointer table `DS:DEDC` from `DFEA` and the type names.
Runtime post-processing of units: none for units 0..DED0-5 (they are used exactly as START generated them: `+0` home site, `+2/+4` position, `+6` altitude, `+8:+0A` = x<<5 and `+0C:+0E` = y<<5 as 32-bit, `+10` heading ...). The last four records are re-initialised by 39c4 (R4 section 3).

### 3.3 Culling / visibility decisions that feed the renderer (from the code read in this range; the full object draw is R8's `9aca`)
* every a17e primitive goes through `b85d`; a point is visible only if depth < 0 (in front), `0 <= sx <= 319`, `0 <= sy <= 0x60` (0xC7 without cockpit); invisible -> `[49E0] = 0xFFFF` and `[9500] = sx`.
* units drawn by `9aca` only if `flags & 2`, `bb9e(u) < 0x12C0` (distance in site units, octagonal), `de92 < 0`, projected visible; `de92 >>= lod_shift`; `de92 <= -0x20` -> a single pixel (colour 15), else model `actype[type].+0x1A` when `de92 > -0x10` and the far model `+0x1C` otherwise (`bd8f`, R8) (V from the portion read).
* sites are drawn through terrain-object lookup (`0d08`); their model id comes from `obj_wreck_shape` (`bc12`): undamaged -> the stored id, destroyed -> either the same id with bank bit (`| 0x100`) if the object's flag byte has 0x20 (own wreck model exists), else the generic wreck `DEA4`/`C0AC` (+0x100).
* the a17e markers of S1 use no distance limit; S2 bullets are simulated/hit-tested **only while visible** (see 1.2).

### 3.4 Messages, sounds, score side effects originating in A17E
| trigger | message / effect | sound | log |
|---|---|---|---|
| player round kills a unit | `<type name> destroyed by gunfire` (name from 71b4 in 9876) | 71b4: `snd_play(5,2)` | 71b4: event 3 (0x83 unit 0 of an air mission; +0x80 if unit flag 0x4000) |
| player round destroys a site | `<object text from 72a4> destroyed by gunfire` | (72a4) | 72a4: 0x01/0x0C (+0x80 primary, +0x40 secondary) |
| enemy round within 32 units | `Hit by gunfire` | 74f6: snd 4 (`d6bf(4,2)`) when `d < 32/(4-skill)` | none |
| photo taken | `Target photographed.`, shutter `snd_play(0x12,2)` + text `Frame N` at (209,142) | 0x12 | 0x8A / 0x4A + site index; objective via `7470` |
| radio beacon | `Radio beacon bearing NNN` every `8*fps` steps, `Drop supplies now` within 256 units | - | - |
Score/debrief: a17e writes no counters itself; kills are counted in `71b4` (`actype[type].+0x1E`++) and in the event log.

---------------------------------------------------------------------------------------------------------

## Open items
| # | item |
|---|---|
| 1 | The exact meaning of `[4088]` as used in the shutter test (`!= 0` required): 7898/87a9 toggle it per frame; it is probably "second page is being drawn", so only half of the frames can take a shot (U). |
| 2 | `[294B]` is named `hud_off` by R4 but is used here (b3e6, b85d, 88f6) as "reduced view window / half scale" flag; sort out at merge (U). |
| 3 | `780a` is called with a stale stack word (`[C0A0]`) as argument (original bug). A port that does not emulate DGROUP must pick `obj_cls_soft(?)`; the safest equivalent is "use `DEA4`" (U which branch the original takes in practice). |
| 4 | Gunfire vs ground objects uses `wpn_effect(0x12, o)`, i.e. the row of store 18 ("Equip") of DS:386A; plausible as a hidden gun row but unconfirmed (U). |
| 5 | Meaning of kinds 0x1D/0x1E cue (`[B168]`, the 0x5DC test) - owner flight/weapons (S3.8). |
| 6 | `[3D66]`/`[3D64]` bit 7: the target-cycle key in `cba0` (cd63..ce73) does `[3D66] |= 0x80; [3D64] |= 0x80` (and `[9D0E]=0`); `9aca` (runs before a17e in the same frame) then searches the next nearest candidate and stores a clean index (value -1 = none also has bit 7 set, so 9aca keeps auto-acquiring while nothing is selected). a17e therefore sees clean indices; its `& 0x7F` is a leftover guard (V reading of cba0/9aca). |
| 7 | Sound 0x12 = `cmd 18` (`v5 <- F80`) is the shutter; sound numbers of the 71b4/74f6 calls are R6's. |
| 8 | `[E554]` (written by bf71) = preview mode that is currently displayed (4 = AGM-65 view); `[DED4]` heading of the preview (R10). |
| 9 | Colours: 0x0D/0x29/0x11/0x0A/0x6/0x9F/0x2E/0x2F are literal DAC indices of the flight palette; their on-screen meaning (tracer colours etc.) needs a visual check (U). |

## DGROUP globals seen
| offset | size | name | meaning | readers/writers |
|---|---|---|---|---|
| 0344 / 0348 / 4042 | w | scene_ctx, scene_alt_flag, alt ctx | sprite window used by `1a85` | 03ce, 1a85, bf71 |
| 2CA0 | b | hud_colour | HUD text/line colour (15) | a17e, b339, b4f3, b5af |
| 294B | b | hud_off / view_small | see open item 2 | b3e6, b85d, 88f6 |
| 2DEE/2DF0/2DF2/2DF4 | w | hdg/pitch/roll/alt | player attitude and altitude | many |
| 2FC2 | 29 x 32 B | actype_tab | aircraft type table (section 4.5) | a17e, 5730, 71b4 |
| 3362 | 40 x 18 B | wproto_tab | projectile prototypes (4.7) | a17e, 753f, 9aca |
| 3632..3640 | | damage_mask, fuel, store_count, bay | R4 names | |
| 363E / 3640 | 4 x (w id, w qty) | bay | store station table | a17e, 753f, 7898 |
| 364E | w | bay_sel | selected bay | a17e, cba0 |
| 3654 / 3656 / 3658 | w | skill / war_level / night | | a17e (3654, 3656) |
| 365E | w | cockpit_visible | gates all HUD code here | many |
| 3660 | w | fps | | many |
| 3662 | 20 x 26 B | wstore_tab | store catalogue | a17e, 753f |
| 3678 | w | wstore_tab[0].proj | (inside 3662 record) | a17e |
| 386A | 20 x 16 | wpn_effect_tab | lethality | 8bab |
| 3C0C | 12 x 28 B | proj_tab | projectiles | a17e, 671a, 753f, 62d0, 9aca |
| 3D60 | w | step_counter | | many |
| 3D64 / 3D66 / 3D68 | w | sel_unit / sel_site / last_dead_site | | a17e, 9aca, 1e9e |
| 3D6A / C07E | w | announce_timer / announce_idx | | a17e, 4f2a, 516d, 62d0 |
| 3FE2 / 3FFA / 4012 | w | text ctx pointers (page0, page1, picture page) | | many |
| 41F2 / 41F4 | w | hud_photo_count / hud_shutter_cd | | a17e |
| 4246.. | str | s_target_photographed etc. | strings 41F6 " destroyed by gunfire", 420C "Hit by gunfire", 421B " destroyed by gunfire", 4231 "Frame ", 4238 6 spaces, 423F "Camera", 4246 "Target photographed.", 425B 7 spaces, 4263 "Missile", 426B " at ", 4270 "", 4271 "Primary Target", 4280 "Secondary Target", 4291 "No Target", 429B "Primary Target", 42AA "Radio Beacon", 42B7 "Radio beacon bearing ", 42CD "Drop supplies now", 430A "Range ", 4311 ".", 4313 " km" | a17e, b7f8 |
| 42E0 / 42E2 / 42F2 | w / w[6] / w[7] | hud_anim_idx / seeker gap table {0,2,4,6,4,2} / bracket table {12,10,8,6,4,2,0} | | b4f3, b5af |
| 43B2 | w | mono | text colour 0 override | a17e, b4f3, b5af, bf71 |
| 43B4 | w | seeker_off | 0 while an AGM-65 flight is being tracked (753f clears it when launching kind 0x1B) | a17e, b6ae, 753f |
| 4706 | w | lamp3_state | state of cockpit lamp 3 (`DS:46D8 + 36 + 10`) | b6ae, d83e |
| 4992/4998/499E | w | camera forward vector | | ba67, e8b5 |
| 49C6 | w | obj_cell_class | set by `bcd7` | 9aca, ddfe, bcd7 |
| 49E0 / 49E8 | w | r3d_sx / r3d_sy | | b85d, a17e |
| 9500 | w | r3d_offscreen_x | | b85d |
| 94FE / 9502 | w | obj_dist / obj_bearing | | bbb4 |
| 9504 / 9506 / 9508 | w | hud_photo_prev / dead store / input_fire_prev | | a17e |
| 9872 | w | hud_label_colour | BSS 0; b293 stores 0x0A | a17e, b77a text |
| 9876 | 40 B | str_scratch | text buffer | many |
| 98CE | w | hud_cam_pitch | | a17e -> bf71 |
| 98DE | w | detail/realism byte comm+32h | | a17e (dead use) |
| 98E2 | w | mission_seconds | | a17e S4h |
| 98FE | w | fx_expl_unit | | a17e, 71b4, 39c4 |
| 9B04 / 9B05 | w / b | aircraft_flags | bit2 bay open; 9B05: 1 designate request, 2 photo, 0x10 training, 0x20/0x40 secondary/primary done | a17e |
| 9D08 | w | hud_photo_mode | | a17e, bf71 |
| 9D0C / 9D0E | w | fx_expl_timer / fx_target_down | | a17e, 1e9e, 5730, 671a, 71b4, 72a4 |
| 9F10 | w | wobj_ptr | result of 0d08 for bad1 / a17e | bad1, 780a, a17e |
| B04C / B04E / B050 | w | proj_last_slot / panel_cx / panel_cy | | 753f, b6ae, b5af |
| B16C | w | gun_slots | clamp(2*fps,3,16) | d707, 465b, a17e |
| B168 | w | min_release_alt (U) | | 1e9e, a17e |
| B28C | w | hud_photo_inframe | | a17e, 7898 |
| B2A2 | 16 x [C0B4] | site_tab | | many |
| B174 | 256 | cell_grid | | a17e, bcd7, 9aca |
| C0A0/C0AA/C0B0 | w | fx_expl_x/y/z | | a17e, 671a |
| C0A4 / C0B2 | w | px / py | player position (site units) | many |
| C0B4 | w | n_sites | | many |
| C604 | 128 B | type_class_tab | | 8bab, bc77, bcac, a17e |
| C684 | w | hud_ref_y | HUD reference y (written by 7898) | a17e, 7898 |
| DE92 | w | r3d_depth | | b85d, ba67, a17e |
| DED0 | w | n_units | | many |
| DEDC | w[128] | name_ptr_tab | place/type names | a17e |
| DFDC | w | hud_page | 0x13 = target page | a17e, 8113 |
| E2D8 | 2 x 18 B | mission_slot | | a17e |
| E2FE | w | tgt_mode | | a17e, cba0 |
| E44E | 12 B x (B16C+4) | bullet_tab | | a17e, 465b, 5730 |
| E55C | w | hud_in_cone | | a17e, b6ae, 753f |
| E556 | w | hud_aspect_quadrant | | b3b3, 81f6 |

## Structures seen
### 4.1 Unit record, 36 B at `DS:C13E + 0x24*u`, count `[DED0]` (START `D3BE`; V fields used here, cross-checked with R4/R5/R6)
| off | size | meaning |
|---|---|---|
| +00 | w | home / current waypoint site index (5730 retargets it) |
| +02 | w | x (site units = x32>>5) |
| +04 | w | y |
| +06 | w | z altitude (a17e compares with the player's `[2DF4]`; hits use `abs(dz)>>5`) |
| +08 | long | x32 = x<<5 (render position, `bd8f` argument) |
| +0C | long | y32 = y<<5 |
| +10 | w | heading (0x10000 = 360 deg) |
| +12 | w | pitch |
| +14 | w | roll |
| +16 | w | type = index into `actype_tab` (4.5); special values 0x15/0x1C (friendly airfield objects), 8, 0x18..0x1B, 0x0A/0x0B (parked/ambient) are used by 39c4 for the last four records |
| +18 | w | flags: bit1 (0x02) active/alive (tested by gunfire and by every draw), 0x08 guiding its missile, 0x10 hit this frame, 0x20 dead, 0x40, 0x80 (U), 0x100 formation member, 0x200 / 0x400 / 0x800 (counted in `[DECE]`), 0x4000 (event arg +0x80); 71b4 keeps mask 0x1C1 when speed is 0 |
| +1A | w | speed (accelerates to `actype.+0x12`); 0 and dead -> stops being a target |
| +1C | w | endurance countdown (5730) |
| +1E | w | weapon type = `wproto_tab` index it fires (62d0) |
| +20 | w | (not touched by R9) |
| +22 | w | radar level / type (R5) |
Units `0 .. DED0-5` = mission units from START; units `DED0-4 .. DED0-1` = airfield objects re-seeded by 39c4 (unit 0 is the primary air target for mission kinds >= 5).

### 4.2 Site (ground object) record, 16 B at `DS:B2A2 + 16*i`, count `[C0B4]`; record 0 = scratch (V)
`+0 w` place-name index (DEDC), `+2 w` x, `+4 w` y (south = larger), `+6 w` radar/threat type (0 = none/gone; HARM needs != 0), `+8 w` flags (bit0 valid, bit1 scanning, bit2 detected, bit4 tracking, bit7 (0x80) destroyed; hi byte 0x100 neutral, 0x200 strip/carrier, 0x400 friendly, 0x800 hidden), `+A w` alert level, `+C w` countdown, `+E w` shape id (low 7 bits object type -> DEDC type names and `C604` class; bit 8 set by `bad1` = bank bit; `bc12` adds 0x100 for the wreck).

### 4.3 Projectile record, 28 B at `DS:3C0C + 28*s`, 12 slots (V, matches R6 section 2.4)
`+0 x, +2 y, +4 z, +6 speed, +8 heading, +A pitch, +C bank, +E life (0 = free), +10 proto type (3362), +12 store id (3662), +14 target (unit/site index or -1), +16 aux (launcher -unit / site; FFBOMB dive floor), +18 tgt_x, +1A tgt_y`.
Player launch (753f, V): slot = first free of 8..11 (none free: store is lost); `x,y = px,py; z = alt-0x14; speed = [B052]>>10; heading/pitch/roll = hdg/pitch/roll; life = ((proto.range*fps)<<5)/((proto.speed64>>6)+1)+6` (999 if the result is <= 6); speed halved (`>>1`) for kinds 0x1E/0x1D/0x1C/0x26; kind 0x1D: pitch 0xC000, speed 1; kind 0x1E: aux = bbed()-0x400; `target = sel_site` (and tgt_x/tgt_y = site x/y) only if `sel_site >= 0 && [E55C]` and (cls 6, or cls 4 with threat != 0, or cls 5 with obj_cls_ship), else -1; kind 0x1B clears `[43B4]`; `[B04C] = slot`.

### 4.4 Cannon tracer, 12 B at `DS:E44E + 12*i`, `i < [B16C]+4`
`+0 x, +2 y, +4 z, +6 vx, +8 vy, +A vz` (words; `x == 0` free). Player gun (465b, V): first every live slot (`x != 0`, `i < B16C+4`) adds its velocity; then, only on odd `step_counter` and if `pilot+0x42 == 0`: slot `s = (step_counter>>1) % B16C`; if trigger `cb72(0)` and `[3650] > 0` and `[C06E] == 0`: `[3650] = clamp([3650] - 40/fps, 0, 1000)`, `snd_play(6,2)`, create the round (below), `[98EA] = 1`; else `bullet[s].x = 0; [98EA] = 0`. Round: `m = 0x70/fps`; `vz = caee(pitch, m) << 5`; `m2 = cb07(pitch, m)`; `vx = caee(hdg, m2)`; `vy = -cb07(hdg, m2)`; start position = player position + one velocity step (`x = px+vx, y = py+vy, z = alt+vz-2`). All live slots add their velocity every step.

### 4.5 Aircraft type table `DS:2FC2`, 29 x 32 B (V; note: the name comes FIRST in VGAME)
`+0 char name[7+11]` ("MIG-23" NUL " Flogger": name at +0, nickname at +7), `+0x12 w speed`, `+0x14 w range`, `+0x16 w agility (roll rate)`, `+0x18 w radar type (0xFFFF none)`, `+0x1A w near 3DX model id`, `+0x1C w far model id (2)`, `+0x1E w kills counter (incremented by 71b4)`.
Model ids (`+0x1A`) for types 0..28: 7,0,16,0,22,9,8,5,26,6,3,3,5,8,13,14,15,20,14,18,19,21,23,25,27,28,29,30,32. START's `AC` description (shell.md: w0..wC then name) is the same stream seen one record later.

### 4.6 Store catalogue `DS:3662`, 20 x 26 B and weapon-effect table `DS:386A` 20 x 16: as documented by R6 sections 2.2/2.3 (V; the `+0x16` field is read here as `[3678 + 26*id]`).

### 4.7 Projectile prototype `DS:3362`, 40 x 18 B (R6 section 2.1): extra finding from 9aca/a17e: **the last word (`+0x10`, R6 "u") is the 3DX model id passed to `bd8f` when a projectile is drawn** (9aca a059: `push [3372 + 18*type]`): 15 for SAM/AA missiles, 1 for AAM/HARM, 10 AGM-65, 12 bombs, 11 equipment, 16 for the last entry.

### 4.8 Mission slot, 2 x 18 B at `DS:E2D8` (R6 2.7): fields used here `+0 kind` (1 = photograph, 5..8 = air targets), `+2 target`, `+6 flags` (0x10 = radio beacon active).

### 4.9 Other small structures
* World-object hit struct `DS:BA14` (result of `0d08`, returned as pointer; R3/R6 name `wobj`): `+0 w shape id`, `+2 w distance`, `+4 long x32`, `+8 long y32 (mirrored: site y = 0x8000 - (y32>>5))`, `+C w pointer to parent record (byte +6: bit7 + 7-bit shape)`, `+E` four bytes L, cx, cy, j (grid cell/lookup indices) (V from bad1/780a/0d08).
* Cockpit lamp/button table `DS:46D8`, 37 x 12 B `{x, y, w, h, scheme, state}`; lamp 3 = (184,94,21,8,2) is the "lock" lamp driven by `b6ae` (R3/R10 section lists all 37).
* Event record (6 B at DS:BA2A, `499e`): `{w mission_seconds, b px>>7, b py>>7, b code, b arg}`; a17e writes codes 0x8A/0x4A.
* Explosion state: `fx_expl_timer [9D0C]` (0x10 hit, -1 ground puff, 0 off), `[98FE]` followed unit, `(C0A0,C0AA,C0B0)` position; unit-kill explosion `(C0A6,C0B8,C602)` + `[B756]` drawn by 9aca.



---
# Part 10: key command interpreter CBA0 and sensor window BF71 (R10)

*Reader pass R10, original title: S3.6 reader R10 - `cba0` (in-flight key command dispatcher) and `bf71` (sensor-view window renderer) + helpers*

Functions: `0000:cba0` (2847 B), `0000:bf71` (1863 B), `0000:1961`, `0000:19ea`, `0000:c6de`, `0000:c73c`, `0000:c834`.
(`c6b8` / `c8fa` belong to other readers; they are summarised where `bf71` / `c834` need them.)

## 0. Overview and correction of the topic hint

* **`cba0` is NOT a menu.** The strings `Detail Level `, `Kybd Sensitivity`, `FrameRate `, `Sounds ` are one-line *status
  messages* posted by key commands (see msg_post below). There is no modal state, no saved screen rectangle, no timer while a
  message is shown: the frame loop keeps running and the text is simply drawn by `7898` for `3*[3660]` frames at
  top centre. The only modal UI in VGAME is the quit requester in `1e9e` (R2). There is no in-flight options menu.
* `cba0(key)` is the **per-frame keyboard command interpreter** for everything that is not flight control. It is called
  once per frame from `43ee` (`43ee` <- `39c4` sim step) as `cba0([DE90])`. `[DE90]` is the single BIOS key word
  (`AH=scan, AL=ascii`, INT 16h/0 via `bios_keyboard`) read by `1e9e` in the same frame (`1e9e` drains the BIOS buffer and keeps
  the first key only; `[DE90]=0` = no key). `1e9e` handles its own keys (Esc quit requester, flight keys) from the same word;
  `cba0` just ignores every key not in its tables.
* Frame order (V): `1bbc` loop: `2f82` view, `7898` HUD, ..., `flip`, `1e9e` (reads key -> `[DE90]`, flight controls),
  `39c4` (sim step) -> ... -> `43ee` -> `cba0([DE90])`.
* `cba0` has three parts: (A) **key switch** (jump table for `'1'..'8'`, compare chain otherwise); (B) the two **post-switch key
  groups** that every call runs for any non-zero key (debug/cheat keys when `[9B05]&0x10`; nav-map keys when `[DFDC]==0x14`);
  (C) the **tail** (`d445`) that runs on every call, also when `key==0`: weapon button, flare-mode timer, bay door and gear
  animation, cockpit lamps.
* `bf71` (called only from the target/weapon logic in `a17e`) renders the **sensor picture window** (DLIR/TV/missile seeker
  view) of the right MFD, x 169..245, y 110..169 of the 320x200 screen (button `0x13` / MFD page `[DFDC]==0x13`): background
  sky/ground bands, 3D scene (render3d `04da`/`dae8`), the object, flames, FLIR tint, `BRG nnn` and zoom labels. It is a
  HUD/MFD overlay (full detail); the 3D internals it calls are render3d (S3.7).
* `1961`/`19ea` (sky/ground colour bands) and `c6de` (FLIR green tint) are helpers of `bf71`. `c73c` (fire puffs) and `c834`
  (effect list) are index-level (render3d / effects).

Shared helper conventions (all R2/R11 own the bodies; given here so this section is self-contained):

| Fn | Meaning (V) |
|---|---|
| `c94e(v,lo,hi)` | clamp: `if (hi<v) return hi; if (lo>v) return (v<=-0x4000)? hi : lo; return v;` (signed words; a value below `lo` by more than 0x4000 *wraps* to `hi`) |
| `cb62(n)` | `rand()*n >> 15` (0..n-1) |
| `caee(a,r)` / `cb07(a,r)` | `r*sin(a)` / `r*cos(a)` (a: 16-bit angle, 0x4000 = 90 deg; `cb07(a,r)=caee(a+0x4000,r)`) |
| `c9d6(a,b)` | 16-bit atan2: 0 when `b>0,a=0`; 0x4000 when `a>0,b=0` (bearing of vector (a=east, b=north)) |
| `c987(a,b)` | approx hypot: `max + min/2` of `|a|,|b|`, capped at 0x7FFF |
| `8b93(str)` = **msg_post** | `strcpy(DS:C578,str); [409E] = 3*[3660];` `7898` draws `C578` at y=20, x = 160 - ((strlen&~1)<<1), colour `[2CA0]`, decrementing `[409E]` each frame, only if `[C07A]==0 && [294B]==0` or `[365A]!=0` (see R8) |
| `898b(i,v)` = **lamp_set** | if `[365E]` and `v != cur[i]`: `d83e([3FE2], lamp[i].btn, v != lamp[i].off)`; for i==2 also `d83e([4012],...)`; `cur[i]=v` (table `DS:3F02`, 6 B/entry: `btn, off, cur`) |
| `d83e(ctx,btn,on)` = **button_light** | recolours the rectangle of cockpit button `btn` (table `DS:46D8`, 12 B: `x,y,w,h,kind,state`) with 2 (4 for kind 4) `rect_recolor` calls (slot 41) via colour table `DS:4672`; stores `state=on` at `rec+10` |
| `cb72(i)` = **button(i)** | `(joystick && comm[72h]&1 ? joy_button(i) : 0) + latch[i]`, latch = `[43BE+2i]` (`[43BE]` Backspace, `[43C0]` Enter; both zeroed at the top of every `cba0`) |
| `d6bf(snd,lvl)` | `if (lvl >= [43C2]) { if (![C06E] || lvl<=1) snd_cmd(snd); } d6e5();` |
| `d6e5()` | `snd_cmd( ([43C2]==0 && [2DFE] && ![C06E]) ? 0x19 : 0x18 )` (cmd 25 = engine drone off / 24 = on, see sound.md) (U polarity) |
| `d707()` | frame-period derived constants: `[43B8] = ([3660]>15) ? c94e((-0x78)/[3660]+9 >>1, 1, 4) : 0` (quirk: `0xFF88 = -120`), `[3660]=c94e([3660],4-[3D7A],15)`, `[B16C]=c94e(2*[3660],3,16)`, `[B286]=[3660]*250`, `[9F64]=[3660]*200` |
| `d76b()` | applies Detail Level: `for i<6: [48A6+2i] = 0x20 << (i+[98DE])`; `[48B2]=[48AE]+[48B0]`; `[48B4]=c94e(2*[48B0],0x1000,0x270F)`; `[48B6]=([98DE]+1)*0xD05` |
| `d7bb()` | `if ([3D7A]==2){[3D7A]=1; [3660]<<=1; d707();}` |
| `d7d0(slot,obj)` | `[2E6C+4*slot]=W[obj*16+0xB2A4]; [2E6E+4*slot]=W[obj*16+0xB2A6]` (copy unit x,y into nav waypoint `slot`) |
| `8113(m)` | select right-MFD page `m` (`[DFDC]`): un-light old button, run page redraw (`0x13`->`81f6` view-name label, `0x14`->`9208` NAV, `0x15`->`9678` weapons, `0x18`->`97a4`), light the new button `[4068+2m]` |
| `80a0(p)` | select left-MFD page `p` (`[DEDA]`; 0 = map via `8387(c0b2,c0a4)`, 1 = `8a4e(1,0)`), buttons `[408A+2p]` = 22/23 |
| `829d(m)` | `m==0x16`: refresh the 7 damage lamps; then `if ([DFDC]==m) 8113(m)` |
| `4926(code)` | end-of-mission request: `[E552]=1` (frame loop exit), comm+28h=code (0=quit), comm+26h=3 when code==0 and not ejected, saves position/damage to comm+74h/76h/34h/36h; `code==7` only sets `[E552]` |
| `48dd()` | repair-all: `[3632]=[C5C8]=0`, `[3640+4i]` = 1 if `[363E+4i]>=0x10` else 9 (i<4), `[3650]=1000`, `[3634]=5000` |

## 1. `0000:cba0` `void input_command_key(unsigned key)` (V unless marked)

Frame: `enter 0Eh` (locals: `[bp-2]` arrow step, `[bp-4]` zoom-table base, `[bp-8]` look azimuth, `[bp-0Ah/0Ch]` temps).
Callers: `43ee`. Callees: `01b7 01bf 4512 48dd 4926 753f 80a0 8113 81f6 829d 8498 84d6 898b 8b93 9208 9678 c94e cb62 cb72 d6bf d6e5 d707
d76b d7bb d7d0 d83e`, RTL strcpy/strcat/ltoa/abs/aFlshr, slots 101 snd_cmd, 63 get_const_3, 93 joy_button.

```
cba0(key):
  [43C0] = 0; [43BE] = 0;                       // keyboard button latches (cleared every frame)
  if (key == 0) goto TAIL;                      // cbb3 -> d445
  switch (key) { ...table A... }  -> each case then goes to GATE (cbd0)
GATE (cbd0):
  if ([9B05] & 0x10) switch (key) { ...table B (debug keys)... }
d2fe: if ([DFDC] == 0x14) { step = ([B80C]==0) ? 0x1000 : 0; switch(key){ ...table C... } }  else goto d438
d400 (only when DFDC==0x14): clamp the active nav waypoint:
        i=[2E7E]; [2E6C+4i] = c94e([2E6C+4i], 0, 0x7FFF); [2E6E+4i] = c94e([2E6E+4i], 0, 0x7FFF);
d438: if ([C06E] != 0) [C07A] = 0x8C;           // ejected: force eject-camera view on every key press
TAIL (d445): ...see 1.4...
```
Note (V): when `key==0` the jump goes straight to `TAIL`, skipping GATE, the nav clamp and `d438`.
For case labels that end `jmp cc65` the code does `pop bx` first (stack balance only) and then continues at GATE.

### 1.1 Table A - main key switch (key = BIOS word `scan<<8|ascii`)

First test (V, `d133`): `key == 0x0A39` -> case `'9'`. If `key > 0x0A39` -> compare chain (second table). Else
`k=key-0x231; if (k <= 0x707 && k % 0x101 == 0) goto JT[k/0x101]` (digits `'1'..'8'`), otherwise GATE.
Jump table `JT` at `0000:D15B` = `cc3e cc45 cc37 cc1a cc49 cc4d ce45 cc84`.

| Key (word) | Name | Handler | Action |
|---|---|---|---|
| 0231 | `1` | cc3e | `4512(1)` drop **Flare** (R4: inventory `[3638]`, msg "Flare released" / "Stores exhausted") |
| 0332 | `2` | cc45 | `4512(2)` drop **Chaff** (`[363A]`) |
| 0433 | `3` | cc37 | `[9B04] ^= 0x20` toggle **flare mode** (bit5; R4 `mission_flags` 0x20 = flares; time limited). Lamp refreshed by the tail (1.4) |
| 0534 | `4` | cc1a | `[9B04] ^= 0x10` toggle **chaff mode** (bit4; R4 0x10 = chaff); `lamp_set(5, (b04&0x10) ? 0x0A : 0)` immediately |
| 0635 | `5` | cc49 | `4512(3)` drop **Decoy** (`[363C]`) |
| 0736 | `6` | cc4d | **landing gear lever**: if `[C0A2] == [2DF4]` (on the ground: terrain height == altitude) do nothing; else `[A068] ^= 1; snd_cmd(0x13)` (cmd 19). Gear animation/lamp in the tail |
| 0837 | `7` | ce45 | **altitude hold**: `if ([3652] != 0) [3652] = 0; else [3652] = max([2DF4], 500)` (compiler reuses `dx==0` from the table `div`). Cleared elsewhere when damaged (tail) or on stick input (1e9e); lamp 4 and the controller are in `1e9e` |
| 0938 | `8` | cc84 | **weapon-bay doors**: if `[3632]&0x10` (bay damage) ignore; else `[E55E] ^= 1; snd_cmd(0x1F)` (cmd 31). Animation in the tail (`[98D8]`, `[9B04]` bit2) |
| 0A39 | `9` | cc69 | `[9B04] ^= 2; button_light([3FE2], 0x21, [9B04]&2)` (cockpit button 0x21 at (284,113); bit1 halves a term in `1e9e` flight code at 22E8, U: flaps/air-brake) |
| 0E08 | Backspace | ce8c | if `pilot[0x42] == 0` (MicroProse flight model): `[43BE] = 1` (button 0 latch = gun trigger, read by `465b` via `cb72(0)`); Lockheed model (`pilot+42h != 0`): no gun, ignored |
| 1C0D | Enter | cea3 | `[43C0] = 1` (button 1 latch = weapon release; consumed in the tail of the *same call*) |
| 1100 | Alt-W | cca4 | `[034A] ^= 1`: sensor view shows real terrain (`04da`) instead of flat sky/ground bands (see `bf71`) and prints the zoom label |
| 1400 | Alt-T | cdf5 | `[9B05] ^= 0x10` (= bit 0x1000 of word `[9B04]`: **training mode: invulnerable + debug keys**; also set at mission start when `pilot[3Ch]&2`, 39c4:3B20); if now set: `comm[30h] |= 1` (flags the mission as training/non-scoring) |
| 1A7B | `{` | cf61 | zoom **out** of the last sensor object: only if `[DFDC]==0x13`; table `T = ([E2FE]==1) ? 0x9E0 : 0x960`; `v = DS[T+[4362]]; if ((v&7) < 7) DS[T+[4362]] = (v&0xF8) | ((v&7)+1)` (zoom index 0..7, higher = farther range, smaller label digit) |
| 1B7D | `}` | cf1a | zoom **in**: same but `if ((v&7) != 0) -> (v&7)-1` |
| 1E00 | Alt-A | cc94 | `[43B6] ^= 1` (read by far module `1088:0120`; U: HUD/gauge variant) |
| 2000 | Alt-D | ccd8 | **Detail Level**: `if (--[98DE] < 0) [98DE] = (get_const_3() == 3) ? 3 : 2;` (slot 63 returns 3 for MCGA driver, so 3->2->1->0->3); `msg_post("Detail Level " + itoa([98DE]))`; `d76b()` |
| 2100 | Alt-F | cd5c | message `"FrameRate " + itoa([3660]) + " " + itoa([43B8])` (info only; `[3660]` = current frame period/time step in ticks, `[43B8]` = detail penalty) |
| 2300 | Alt-H | cc9c | `[463E] ^= 1` (gates `d9a3`, cockpit side overlay, R2) |
| 2C5A | `Z` | cd95 | `if ([3D7A]==1) { [3D7A]=2; [3660] /= 2 (signed); d707(); }` |
| 2C7A | `z` | cc10 | `8498()`: zoom in. If `[C07A]&0x80` (external view) `[3D70]--` (camera distance); else left-MFD map zoom `[4080]++` (max 9) + `8387` redraw when `[DEDA]==0`, or `[4082]++` (max 7) when `[DEDA]==1` |
| 2D58 | `X` | cdb2 | `d7bb()` (undo `Z`) |
| 2D78 | `x` | cc15 | `84d6()`: zoom out (external: `[3D70]++`; map `[4080]--` min 2; page1 `[4082]--` min 0) |
| 2E00 | Alt-C | ccac | `[2DFC] ^= 1` (tall 3D window: 0x5B rows normal, 0x6B rows when set, see `2f82`); when it becomes 0 the 320x16 strip at y=0x5B..0x6A is restored: `blit_rect(page 2,(0,0x5B) -> page (1-[4088]),(0,0x5B), 320, 16)` (slot 42) |
| 2F00 | Alt-V | cdb8 | **Sounds**: `[43C2] = ([43C2]+1)&3`; `msg_post("Sounds " + itoa(3-[43C2]))`; `d6e5()` |
| 2F76 | `v` | ce76 | `[294A]++; if ([294A] > 2) [294A] = 0` (HUD mode 0..2, read by `7898` and asm HUD `10a8:000c`) |
| 3062 | `b` | ce63 | `[9D0E]=0; [3D66] |= 0x80; [3D64] |= 0x80` (flag current targets "dropped"; indices keep low 7 bits) |
| 316E | `n` | cbbc | `[9B05] ^= 1`; then if `[DFDC]==0x13` run LOOK(0) (below) else nothing |
| 326D / 332C / 342E / 352F | `m` `,` `.` `/` | ced9 cee7 ceee cee0 | **sensor look direction** Left / Right / Rear / Ahead: azimuth `[bp-8]` = `0xC000 / 0x4000 / 0x8000 / 0x0000` then LOOK |
| 324D / 333C / 343E / 353F | `M` `<` `>` `?` | ced0 cec7 cebe ceb5 | full-screen out-of-cockpit look: `[C07A] = 0x42 / 0x43 / 0x41 / 0x44` (left/right/rear/ahead; see R3 `2f82`) |
| 3920 | Space | ce10 | **weapon select**: if `pilot[0x42]!=0` `[364E] ^= 1` else `[364E] = ([364E]+1)&3`; `if ([9B05]&2) 81f6()`; `if ([DFDC]==0x15) 9678()` |
| 3B00 | F1 | ceac | `[C07A] = 0` (front cockpit view) |
| 3C00 | F2 | d04c | **master mode** cycle `[E2FE]` (R4 `master_mode`): 0->1 air-to-air (`[3D64]=[3D66]=-1`), 1->2 air-to-ground (`[3D66]=-1`), 2->0 |
| 3D00 | F3 | d086 | left MFD page toggle: `80a0([DEDA]==1 ? 0 : 1)` |
| 3E00 | F4 | d099 | `[43B0] ^= 1; 01bf()` (cockpit lamp/panel colour swap 0x0F<->0x0A, `rect_recolor` of 3 panel rectangles on page `[4012]`) |
| 3F00 | F5 | d0a4 | `8113(0x15)` right MFD = weapons/targets |
| 4000 | F6 | d0a9 | `[43B2] ^= 1; button_light([3FE2], 0x19, [43B2])` (FLIR/IR tint of the sensor window, see `bf71`) |
| 4100 | F7 | d0b7 | `[B80C] = 1; 8113(0x14)` NAV page, waypoint *view* mode |
| 4200 | F8 | d0c1 | `[B80C] = 0; [2E7E] = [2E7C]; 8113(0x14)` NAV page, waypoint *edit* mode |
| 4300 | F9 | d123 | `[9B05] ^= 4` (read by HUD `7898`) |
| 4400 | F10 | d12b | `8113(0x18)` right MFD page 0x18 (`97a4`) |
| 5200 | Insert | cd1e | **Keyboard sensitivity**: `[43BA] = ([43BA]+1) > 2 ? 0 : [43BA]+1`; `msg_post("Kybd Sensitivity" + itoa([43BA]+1))` (no space in the string, message e.g. `Kybd Sensitivity2`). `1e9e:2173` uses it: `axis = ((raw-0x80)*([43BA]+1))/3 + 0x80` |
| 5400 | Shift-F1 | cfae | `[C07A] = 0x87` |
| 5500 | Shift-F2 | cf9c | `[C07A] = 0x84` |
| 5600 | Shift-F3 | cfa5 | `[C07A] = 0x85` |
| 5700 | Shift-F4 | cfb7 | `[C07A] = 0x89` |
| 5800 | Shift-F5 | cfc0 | `[C07A] = 0x88` |
| 5900 | Shift-F6 | cfc9 | `[C07A] = 0x8B` |
| 5B00 | Shift-F8 | d0cf | reset the 4 nav waypoints from units: `d7d0(0,[E2DC]); d7d0(1,[E2DA]); d7d0(2,[E2EC]); d7d0(3,[E2EE]); [2E6C]=([2E70]+[2E6C])>>1; [2E6E]=([2E6E]+[2E72])>>1;` then `if ([DFDC]==0x14) 9208()` |
| 5D00 | Shift-F10 | cfd2 | **EJECT** (ignored if `[C06E]`): `d6bf(5,2); d6bf(0x14,2); lim = cb62(300)+500; v = (abs([2DF2])>>5) + (abs([2DF0])>>5) + [B168]; if (v > lim) 4926(6) /* killed */ else comm[26h] = 2; [C06E]=1; [DEA2]=[C0A4]; [DEC6]=[C0B2]; [DECA]=[2DF4]+8;` (eject-camera anchor read by `2f82`) |

View values `0x84..0x8B` = external/trail views (R3). `[C07A]` meaning (V, from `2f82`/`365E` code): `0` front cockpit with panel
(`[365E]=((C07A&0xC0)==0)`, i.e. only `C07A==0` shows the cockpit panel), `0x41` rear, `0x42` left, `0x43` right, `0x44` ahead
(no panel), `>=0x80` external.

**LOOK(az)** (`cef3`, entered with `[bp-8]=az`):
```
if ([9B05] & 2) return;                // seeker locked: keep the view
[9D0E] = 0; [43B4] = 1; [E556] = az;   // az: 0 Ahead, 0x4000 Right, 0x8000 Rear, 0xC000 Left (labels " Ahead "," Rear  "," Right "," Left  " via 81f6)
[3D64] = [3D66] = 0xFFFF;              // drop both targets
8113(0x13);                            // right MFD = sensor view
```
(`'n'` runs LOOK(0) only if `[DFDC]==0x13`; the four direction keys run it unconditionally.)

Keys not in any table: ignored.

### 1.2 Table B - debug keys (only while `[9B05] & 0x10` = training mode, enabled by Alt-T or by training missions)

Reached at `cbd0` for every key (including keys already handled in table A):

| Key | Action |
|---|---|
| Alt-R 1300 | `48dd(); 829d(0x16)`: repair all damage, relight damage lamps |
| Alt-I 1700 | `[C02A:C02C] += 0x20000 >> [4080]` (long) - move player along y (sign of "north" U, see open item 4) |
| Alt-K 2500 | `[C02A:C02C] -= 0x20000 >> [4080]` |
| Alt-J 2400 | `[BA10:BA12] -= 0x20000 >> [4080]` - move player along x (west) |
| Alt-L 2600 | `[BA10:BA12] += 0x20000 >> [4080]` |
| Alt-N 3100 | `[43AE]=1; [3658] ^= 1; 01b7()` toggle night and reload palette (`1088:0006`, then `01bf`) |

(The step is `0x20000 >> [4080]` where `[4080]` is the map zoom 2..9.) After the switch execution continues at `d2fe`.

### 1.3 Table C - NAV map keys (only while `[DFDC]==0x14`)

`step = ([B80C]==0) ? 0x1000 : 0`; `i = [2E7E]` (edit waypoint). Arrow keys act only when a joystick is present
(`comm[72h]&1`; without a joystick the arrows are flight controls) - keypad digit keys (ascii `8 2 4 6 9 3`) always act.

| Key word | Action |
|---|---|
| 4700 Home | `[98E2] -= 0x3C` (mission clock back 60 ticks) -> clamp |
| 4F00 End | `[98E2] += 0x3C` -> clamp |
| 4800 Up (joy only) / 4838 keypad `8` | `[2E6E+4i] -= step >> [4080]` ; `8113(0x14)` ; clamp |
| 5000 Down (joy only) / 5032 keypad `2` | `[2E6E+4i] += step >> [4080]` ; redraw ; clamp |
| 4B00 Left (joy only) / 4B34 keypad `4` | `[2E6C+4i] -= step >> [4080]` ; redraw ; clamp |
| 4D00 Right (joy only) / 4D36 keypad `6` | `[2E6C+4i] += step >> [4080]` ; redraw ; clamp |
| 4900 PgUp (joy only) / 4939 keypad `9` | previous waypoint: if `[B80C]` then `[2E7C]--` (mod 4) else `[2E7E]--` (mod 4); redraw ; clamp |
| 5100 PgDn (joy only) / 5133 keypad `3` | next waypoint: `[2E7C]++` (mod 4) if `[B80C]` else `[2E7E]++` (mod 4); redraw ; clamp |

(`[B80C]==1` after F7: step is 0 so the arrows only redraw and PgUp/PgDn move the viewed waypoint `[2E7C]`; after F8 the arrows
move waypoint `[2E7E]` by `0x1000>>[4080]` map units and PgUp/PgDn select it.) Waypoint k: x=`[2E6C+4k]`, y=`[2E6E+4k]`, 4 slots.
Other keys on the NAV page reach the clamp (`d400`) unchanged.

### 1.4 TAIL (`d445`), runs on every call

```
if ([43BC] > 0) [43BC]--;                                   // fire debounce
if (cb72(1) != 0 && [43BC] == 0 && [C06E] == 0) {          // button 1: Enter / joystick fire
    753f();                                                 // weapon release (R4, "Bay closed" etc.)
    if ([DFDC] == 0x15 && [365E]) 9678();                   // refresh weapon page
    [43BC] = 4;                                             // 4-frame debounce (set after the call)
}
if (([3632] & 0x40) != 0) {                                 // dispenser damage: chaff & flare modes off
    [9B04] &= 0xCF; lamp_set(5, 0); lamp_set(6, 0);
}
// flare mode (bit5) duty-cycle timer [43C4]
if ([9B04] & 0x20) { [43C4]++; if (20*[3660] < [43C4]) [9B04] &= ~0x20; }   // max on-time 20 frame periods
else if ([43C4] > 0) [43C4]--;                              // recharge
lamp_set(6, ([9B04]&0x20) ? 0x0A : ([43C4] != 0 ? 0x2A : 0));
if ([3632] & 0x10) { [E55E] = 1; [98D8] = 2; }              // bay-door damage: doors forced open
if ([3632] & 0x08) [3652] = 0;                              // altitude hold off
if ([DFDC] == 0x14 && ([3D60] & 0x1F) == 0 && [365E]) 829d(0x14);   // NAV page redraw every 32 frames
// bay doors: [E55E] command, [98D8] animation 0..2, [9B04] bit2 = fully open
if ([E55E]) { if ([98D8] == 2) [9B04] |= 4; else [98D8]++; }
else if ([98D8] == 0) { if ([9B05] & 2) 81f6(); [9B04] &= ~4; } else [98D8]--;
lamp_set(2, (([9B04]&4) && ([3D60]&1)) ? 0x11 : 0);        // blinks while open
// landing gear: [A068] lever (1 = down), [C072] animation 0..3, [9B04] bit0 = up-and-locked
if ([A068]) { if ([C072] == 3) [9B04] &= ~1; else [C072]++; }
else if ([C072] == 0) [9B04] |= 1; else [C072]--;
v = ([9B04]&1) ? 0 : (([B168] < 250 || ([3D60]&1)) ? 0x0A : 0);   // steady below speed 250, blinks above
lamp_set(3, v);
```
Lamp numbers (`DS:3F02`, V): 2 -> cockpit button 0x1F (bay), 3 -> 0x22 (gear), 4 -> 0x20 (altitude hold, `1e9e`),
5 -> 0x11 (chaff), 6 -> 0x12 (flares), 10..16 -> buttons 5,6,7,0xC,0xB,9,8 (damage lamps, off value 10, damaged 41).

`[9B04]` bit map (V from all users): bit0 gear up&locked; bit1 `'9'` toggle (1e9e halves a term); bit2 bay doors fully open;
bit3 set by `1e9e`/`39c4` (button 0x23 toggle, `1e9e:1FF1`); bit4 chaff mode (`'4'`); bit5 flare mode (`'3'`, timed). `671a` (detection)
tests bit4 for threats with sign `>0` and bit5 for `<=0` (U: radar-guided vs IR-guided; matches chaff/flares). `43ee` (cockpit bob/sprite offset `[3D5C]`) adds 0x14 when
`pilot+42h!=0 && [9B04]==0`, 0x18 when bit2, 0x10 when bit4 (R6).
`[9B05]` bit map: bit0 `'n'` toggle (HUD, `a17e`); bit1 seeker/target locked mode (set by `a17e`, blocks LOOK); bit2 F9 toggle (HUD);
bit4 debug keys; bits 5,6 set by `7470`, read by `a17e`.

### 1.5 Messages posted by `cba0`

| String (DS) | Where | Resulting text |
|---|---|---|
| `437A` `"Detail Level "` | Alt-D | `Detail Level 3` .. `0` |
| `4388` `"Kybd Sensitivity"` | Insert | `Kybd Sensitivity1..3` |
| `4399` `"FrameRate "` + `43A4` `" "` | Alt-F | `FrameRate <3660> <43B8>` |
| `43A6` `"Sounds "` | Alt-V | `Sounds 3` (all) .. `Sounds 0` |
Built in `DS:9876` with `strcpy/strcat/ltoa(v,DS:DEAA,10)`, then `8b93(DS:9876)`.

### 1.6 Quirks (V)

* Alt-F jumps *into the middle of the Insert handler* (`cd44`) to share the number-append tail.
* Messages and `[9876]` share a scratch buffer with other code; message timer is `3*[3660]` frames.
* Shift-F10 eject test uses unsigned-looking `abs` sums of pitch/roll words `>>5` plus speed; success only sets `comm[26h]=2` and
  `[C06E]=1`, the mission ends later in the main loop.
* Home/End change the mission clock `[98E2]` by 60 on the NAV page without the debug gate.
* Arrow-key map control requires a joystick; keyboard-only players must use the keypad.
* `d438` (force view 0x8C after ejection) runs only for non-zero keys.

## 2. `0000:bf71` `void mfd_view_render(unsigned obj, int x, int y, int z, int a0, int a1, int a2, int mode, int zshift)`

Args (cdecl, first at `[bp+4]`): `obj` = object word (`bits0..6` object/model index, `bit8` class flag, `bit15` damage flag);
`x,y` world position (16-bit), `z` altitude; `a0,a1,a2` = object orientation words (`[bp+0Ch/0Eh/10h]`); `mode` `[bp+12h]`;
`zshift` `[bp+14h]` (-1, 0, 1). Locals: `bp-0x42` size. Only caller `a17e` (4 sites, V):

| Site | mode | zshift | Args | Meaning |
|---|---|---|---|---|
| a9f7 | 3 | 0 | `obj=bc12(idx)`, `x,y` = ground-site record (16 B @`B2A4`+16*idx).x/.y, `z=a0=a1=a2=0` | direct look at the designated ground site (`[3D66]`) when the release button fires (`[41F4]` 4-frame hold, label `Frame n`) |
| aad3 | 4 | -1 | same | **missile seeker camera** (label `Missile`); uses weapon record `[B04C]` |
| ab39 | 1 | -1 | same | target camera on the designated ground site `[3D66]` (A/G mode) |
| ae5f | 1 | 1 | **air** record (36 B @`C140`+0x24*`[3D64]`): `x=+0 y=+2 z=+4 a0=+0Eh a1=+10h a2=+12h`, `obj=W[0x2FDC + (rec+14h<<5)]` | air-to-air target camera (`[DFDC]==0x13 && [E2FE]==1`), closer zoom (`zshift==1` halves zoom range) |

Window: **x 169..245 (xl=0xA9, xr=0xF5), y 110..169 (yt=0x6E, yb=0xA9)**, drawn on page selected by `[4088]` through context `[4042]`
(`[4042]->+0 = ([4088]!=0)`, `+4` colour). Helper `1001:024c(ctx,xl,yt,xr,yb)` = fill that rectangle with `ctx+4` colour
(inclusive both ends); `1001:01ec(unused,n,pts,color)` = polygon fill; `1001:051f()` resets polygon edge tables.

### 2.1 Algorithm

```
[9D08] = 1;                                   // "sensor view active" (read by a17e)
model  = c6b8(obj);                           // bit8 set: model = W[0x510 + 2*(obj&0x7F)] | (obj & 0x8000); else model = obj
[4362] = obj & 0x7F;                          // last object (used by '{' '}' zoom keys)
tbl    = (obj & 0x100) ? 0x960 : 0x9E0;       // per-object zoom bytes (runtime tables, 128 B each)
zi     = DS[tbl + (obj&0x7F)] & 7;            // zoom index 0..7
zr     = W[0x4340 + 2*zi];                    // zoom range  10,30,50,80,100,150,200,250
[433C] = 0x38 - zi;                           // zoom label "8X".."1X" ([433D]='X', [433E]=0)
if (((y|x) & 0xFF) == 0) zr <<= 1;            // both low bytes zero -> double range (quirk, U)
if (mode == 1 && zshift == 1) zr >>= 1;       // arithmetic
[4042]->page = ([4088] != 0);                 // 0 or 1
[E554] = mode;
```
Camera set-up, three cases:

**mode 4 (weapon seeker)**: `w = &DS[0x3C0C + 0x1C*[B04C]]` (28-B in-flight weapon record):
`[DED4]=w[+8](3C14)` camera heading, `[9510]=w[+0xA](3C16)` pitch, `[B766:B768] = (long)w[+0](3C0C) << 5`,
`[B76A:B76C] = 0x100000 - ((long)w[+2](3C0E) << 5)`, `[B808:B80A] = sext(w[+4](3C10))`; continue at COMMON.

**mode 0/1** (look from the player at the object):
```
[9512] = 0;
dx = x - [C0A4]; dy = y - [C0B2]; dz = (z - [2DF4]) >> 5;
brg  = c9d6(dx, -dy);                      // [DED4]  (bearing, 0 = north)
D    = c987(dy, dx);
elev = c9d6(dz, D);                        // [9510]
rng  = c987(D, dz); [C576] = rng;
[950C] = (rng >> 4) + 0x190;               // pseudo distance
[950E] = ([950C] << 5) / (rng + 1);        // signed idiv (perspective scale)
r  = [950C] << 2;
h2 = cb07(elev, r);
[433A] = 0; if (zshift < 0) { [433A] = zshift + 2; zshift = 0; }
rx =  caee(brg, h2) >> zshift;
ry = -cb07(brg, h2) >> zshift;             // note the explicit negate
rz =  caee(elev, r) >> zshift;
```
**mode >= 2 (2,3)** (direct, player orientation):
```
rx = (x - [C0A4]) << 4;  ry = (y - [C0B2]) << 4;  rz = (z - [2DF4]) >> 1;
[DED4] = [2DEE]; [9510] = [98CE]; [9512] = [2DF2]; [950E] = 0x20; [433A] = 2;
```
COMMON:
```
// horizon row of the window
h = (((int)[9510] >> 2) * [950E] >> 5) + 0x8B;            // imul, 32-bit shift right 5
if (h < 0x6E || (int)[9510] < -0x1800) h = 0x6E;
if (h > 0xA9 || (int)[9510] > 0x1800) h = 0xA9;
// ground colour from the per-type attribute byte DS[0xC604 + (obj&0x7F)]
g = 0x78; a = attr; if (a & 0x10) g = 8; if ((a & 0xF) in {9,0xB,0xC,0xD}) g = 0xFC;
// camera position (units 1/32 world unit; Y is flipped: 0x100000 - y*32)
if (zr > [C576] /*unsigned*/ || mode == 3) {                 // object nearer than zoom range: natural camera
    [B766:B768] = [B288:B28A]; [B76A:B76C] = [B752:B754]; zc = [B75C]; [B808:B80A] = sext(zc);   // player pos, alt+0x18
} else {                                                       // magnify: put camera zr units before the object
    q = cb07([9510], zr<<5);                                   // horizontal part
    [B766:B768] = ((long)x<<5) - caee([DED4], q);
    [B76A:B76C] = 0x100000 - cb07([DED4], q) - ((long)y<<5);
    [B808] = -(caee([9510], zr<<5) - z); [B80A] = 0;
}
// (mode 4 enters here with its own camera)
if (|[9510]| > 0x4000 || [9510] == 0x8000) { [9510] = 0x8000 - [9510]; [DED5] += 0x80; }   // pitch over the pole: flip
if ([B80A:B808] < 0x10) [B808:B80A] = 0x10;                    // minimum camera altitude 16
save = [294B]; [294B] = 0;                                     // hide HUD flag while drawing
[7D50] = [43B2] ? 0x8160 : 0x7F60;                             // colour-shade table base for the terrain renderer
if ([98DE] < 3) { [7D50] = 0x8360; ctx->+4 = 0x88; fill(ctx,0xA9,0x6E,0xF5,0xA9); }   // low detail: flat sky colour everywhere first
if ([034A] == 0 && mode != 4) {                                // "simple" path
    if (mode != 0) {
        ctx->+4 = 0x88;                                        // sky
        if (h != 0x6E) { if ([98DE]==3) 1961(h); else fill(ctx,0xA9,0x6E,0xF5,h); }
        ctx->+4 = g;                                           // ground
        if (h != 0xA9) { if ([98DE]==3 && g != 8 && [3658]==0) 19ea(h, g); else fill(ctx,0xA9,h,0xF5,0xA9); }
    }
    s = 0x100 << [433A];
    dae8(ctx, -[DED4], [9510], [9512], 0,0,0,0, s);            // camera/rotation set-up (R11a)
    // if site rec(3D66<<4 @B2A4)+6 has bit7 and rec+0Ch != (byte)[C0AC] and != (byte)[DEA4]: model |= 0x8000 (U meaning)
    e7ac(model, -a0, a1, a2, rx, -ry, rz);                     // draw the object instance (R11a)
} else {                                                       // terrain path ([034A] set or mode 4)
    save [98F8],[9F12],[98C8],[98E4],[0348]; [4360] = 1; [0348] = 1;
    dae8(ctx, -[DED4], [9510], 0,0,0, [B75C], 1, 0x100 << [433A]);
    04da(-[DED4], [9510], [B766:B768], [B76A:B76C], [B808:B80A]);           // terrain/world render (R11a)
    if (mode == 1 && zshift != 0) {                            // air target (site 4), close view
        db3e(); dae8(ctx, -[DED4], [9510], [9512], 0,0,0,0, W[0x4350+2*zi]);   // scale 1024,896,..,128
        e7ac(model, -a0, a1, a2, rx, -ry, rz);
        if (DS[0xC156 + 0x24*[3D64]] & 0x20) {                 // air rec +16h bit5: burning (U)
            df08(); [49C4] = 0;  c73c(-a0, a1, a2, rx, -ry, rz); }
    } else c834((int)([B766:B768] >> 5), 0x8000 - (int)([B76A:B76C] >> 5), [B808]);   // effect list (see below)
    [4360] = 0; restore the five saved words;
}
db3e();                                                        // end of 3D pass (R11a)
[7D50] = 0x7D60;
if ([9D0C] != 0 && [9D0E] != 0)                                // overlay picture
    sprite_scaled(ctx, 13, 10, 50, 40, W[0x91C8 - 4*abs([9D0C])], W[0x91C8 - 4*abs([9D0C]) + 2]);   // slot 22
if ([43B2]) { c6de(0xA9, 0x6E); if ([480E]==0) button_light([3FE2],0x19,1); }       // FLIR tint + keep F6 button lit
else if ([480E]) button_light([3FE2],0x19,0);
1001:051F();
if (mode == 1) {
    s = "BRG " + itoa(([DED4] / 0xB6) unsigned);                // bearing in degrees
    text(s, x=0xB9, y=0xA1, colour = [43B2] ? 0 : [2CA0]);      // 8ad3 -> 8b04(ctx,..)
    if ([034A]) text(zoomlabel DS:433C ("8X".."1X"), x=0xEC, y=0xA3, same colour);
}
[433A] = 0; [294B] = save;
```
(`fill` = `1001:024c`; the sky fill uses y range 0x6E..h inclusive, the ground h..0xA9 inclusive.)
Note: the `c834` call (terrain branch when NOT `mode==1 && zshift!=0`) is `c834(camX>>5, 0x8000-(camY>>5), [B808])`.

Quirks (V): `[C576]` is stale in modes >=2 (only mode 0/1 sets it) so the near/far test uses the previous range; mode 2 is never used by
the callers; mode 0 draws no background; `[9D08]` is write-only here; the BRG degree conversion divides by 182 (0xB6) with unsigned
`div`; `zr` doubling when `(x|y)&0xFF==0`.

### 2.2 Sub-functions

#### `0000:1961` `void view_sky_bands(int horizon)`  (V)
Sixteen horizontal sky bands above the horizon. `W[]` = `DS:0D22` words `0,2,3,4,6,8,11,15,20,27,36,48,64,87,119,164` (then `0,0`).
```
for (i = 0; i < 16; i++) {
    top = horizon - W[i+1]; done = 0;  if (top <= 0x6E) { top = 0x6E; done = 1; }
    bot = horizon - W[i];
    quad = {(0xA9,top),(0xF5,top),(0xF5,bot),(0xA9,bot)};
    1001:01ec(DS:[0D1C] /*unused*/, 4, quad, 0x80 + i);       // colour 0x80..0x8F, i=0 nearest the horizon
    if (done) break;
}
```
(arg `[bp+4]` = horizon row; `[bp-6]` copies; callers pass `h`.)

#### `0000:19ea` `void view_ground_bands(int horizon, int mode)`  (V)
```
base = (mode == 1) ? 0xF0 : 0x70;
for (i = 0; i < 16; i++) {
    bot = horizon + W[i+1]; done = 0; if (bot >= 0xA9) { bot = 0xA9; done = 1; }
    top = horizon + W[i];
    quad = {(0xA9,bot),(0xF5,bot),(0xF5,top),(0xA9,top)};  // vertex order as in the code
    1001:01ec(DS:[0D1C], 4, quad, base + i);                  // 70h..7Fh ground ramp (horizon haze -> near); F0h water ramp
    if (done) break;
}
```
`bf71` passes `g` (0x78 / 0xFC) as second arg, so the `mode==1` test never succeeds in practice (original quirk): bands always use 0x70+i.

#### `0000:c6de` `void view_flir_tint(int x, int y)`  (V)
```
offset_set(pixel_offset(x, y))        // slots 62, 26: draw offset = window origin (0xA9,0x6E)
1001:051F();                          // reset edge tables
line (0,0)-(0,0x3C)  and line (0x4C,0)-(0x4C,0x3C)   // 1001:06A0 twice -> left/right edge of a 77 x 61 block
1001:0504(3);                         // poly_fill mode 3: pixel = ((old & 0x0E) >> 1) | 0x98 (video.md §5: MFD green ramp 98h-9Fh)
offset_set(0);
```
Applied after the scene is drawn: converts the whole window (77x61 px, one row more than the 60-row fill) to the green MFD ramp = FLIR look.

#### `0000:c73c` `void fx_fire_puffs(a0, a1, a2, rx, ry, rz)`  (index-level, owner render3d)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 2; where they differ, render3d.md is right.
Six puffs, state table `DS:3A5C`, 8 B each: `+0 dHeading, +2 dPitch, +4 frame (0..5), +6 active`. For `i<6`: if inactive or `frame>=6`
re-randomise: `d = W[0x436A + 2*cb62(7)]` for both deltas (table `0C00 0800 0400 0200 FE00 FC00 F800 F400`), `frame = 1 + cb62(2)`,
`active = 1`, no draw this call. Otherwise `(h,p) = (a0 + dH, a1 + dP)` with pole normalisation (`|p|>0x4000 || p==0x8000` ->
`p = 0x8000 - p; h hi-byte += 0x80`); puffs 0..3: `[C040]=frame; e7ac(0x25+frame_old, h, p, a2, rx, ry, rz); frame++`;
puffs 4,5: `[C040]=frame_old; e7ac(0x24, ...)`. Caller: `bf71` (burning ground target in close view).

#### `0000:c834` `void fx_draw_effect_list(int camx, int camy, int camz)`  (index-level, owner render3d/effects)
> **S3.7:** superseded by the bit-exact spec in `render3d.md` Part 2; where they differ, render3d.md is right.
Draws the smoke column of the burning site `[3D68]` (does nothing if `[3D68] == -1`). For the 16 puff records at `DS:39DA`
(8 B: `+0 x (0 = free), +2 y, +4 altitude/age, +6 rank`; maintained by `479c`):
`ba67(x,y,alt,camx,camy,camz)` (R9; sets `[DE92]` = camera-space depth); if `-256 < [DE92] < 0`:
`size = c94e(c8fa(rank, abs(0x200/[DE92])), 4, 255)` (`c8fa(sel,v)` scales v by 0.5/0.625/0.75/0.875 for sel 0..3, else unchanged),
`[49C6] = (size<<8) | ((rank & 0xE) >> 1)`, then `bee1(0x25, (long)x<<5, (long)y<<5, alt, 0, 0, 0, 1)` (R9, queue sprite-object 0x25).
Callers: `bf71`.

## 3. Open items

| # | Item | Why |
|---|---|---|
| 1 | Exact meaning of `[43B6]` (Alt-A), `[463E]` (Alt-H), `[9B04]` bit1 (`'9'`) | only far modules `1088:0120`, `d9a3`, `1e9e:22E8` use them |
| 2 | what `671a` does with the chaff/flare mode bits | owned by detection readers; names from R4 `mission_flags` |
| 3 | polarity of `d6e5` (engine drone on/off) and meaning of `[2DFE]` | cmd 24/25 mapping |
| 4 | camera Y convention in `bf71` (far case uses `[B752:B754]`, near case `0x100000 - ...`) | confirm with `2f82` |
| 5 | `[9D0C]` sprite table `DS:91C8` and what pictures it holds; who sets `[9D0E]` | runtime-populated |
| 6 | Visual check (user): sensor window position, BRG/zoom text, FLIR tint, sky/ground band look, keys `m , . /` labels | U-tasks |
| 7 | `[E2DC],[E2DA],[E2EC],[E2EE]` initial values (unit indices for waypoints) | never written by code seen |
| 8 | `bf71` mode 2 and mode 0 unused by callers - port can ignore | |

## 4. DGROUP globals seen

| Offset | Size | Name | Meaning | Readers / writers |
|---|---|---|---|---|
| 0344 | w | `ctx_alt` | alternate draw context | bf71(1a85) |
| 0348 | w | `view_ctx_sel` | when nonzero `1a85` draws to `[4042]`; set to 1 in terrain path | 04da, 1a85, bf71 |
| 034A | w | `view_sensor_terrain` | Alt-W toggle | cba0 w, bf71 |
| 0510 | w[128] | `obj_model_map` | model id per object index | c6b8 (runtime filled) |
| 0960 / 09E0 | b[128] | `view_zoom_tbl` | per object zoom byte (low 3 bits = zoom index, rest preserved) | bf71, cba0 |
| 0D22 | w[16+] | `view_band_w` | `0,2,3,4,6,8,11,15,20,27,36,48,64,87,119,164` | 1961, 19ea |
| 2299 229B 229D 229F | w | `poly_line_x0 x1 y0 y1` | polygon line endpoint registers | c6de, 1001:* |
| 294A | b | `hud_mode` | `v` key, 0..2 | cba0, 7898, 10a8:000c |
| 294B | b | `hud_hide` | zeroed while sensor view draws | bf71, d9a3, 7898 |
| 2CA0 | b | `hud_color` | text colour | bf71, 7898 |
| 2DEE 2DF0 2DF2 | w | `plr_heading pitch roll` | 16-bit angles | many |
| 2DF4 | w | `plr_alt` | altitude | many |
| 2DFC | w | `view_tall` | Alt-C, 3D window 0x5B/0x6B rows | cba0, 2f82, 7898 |
| 2DFE | w | `snd_engine_flag` (U) | read by d6e5 | 1e9e w |
| 2E6C 2E6E (+4k) | w | `nav_wp_x / nav_wp_y` | 4 waypoints, stride 4 bytes | cba0, d7d0, 9208 |
| 2E7C 2E7E | w | `nav_wp_view / nav_wp_edit` | waypoint index 0..3 | cba0, 39c4, 7470, 9208 |
| 3632 | w | `dmg_flags` | bit3 autopilot, bit4 bay doors, bit6 chaff/flare dispensers; others R8 | cba0, 48dd, 4512 ... |
| 3652 | w | `ap_alt_hold` | altitude hold target (0 = off) | cba0, 1e9e |
| 3658 | b | `night` | comm[33h] | cba0(Alt-N) |
| 365E | w | `cockpit_panel` | `(C07A & 0xC0)==0` | 2f82 w |
| 3660 | w | `sim_frame_ticks` | adaptive frame period / time step | 39c4 (adaptive), cba0 (Z/X), d707 |
| 364E | w | `wpn_select` | Space | cba0, 753f, 7898, a17e |
| 3D60 | w | `frame_counter` | | many |
| 3D64 / 3D66 | w | `sel_air_target / sel_ground_target` (R4 names) | selected target index (low 7 bits; 0xFFFF none; bit7 'dropped' flag set by `b`) | cba0, bf71, a17e |
| 3D70 | w | `view_ext_dist` | external camera distance 2..8 | 8498, 84d6, 2f82 |
| 3D7A | w | `sim_speed_mode` | 1/2 (Z/X) | cba0, d707, d7bb |
| 3F02 | 6B x N | `cockpit_lamp[]` | `btn,off,cur` | 898b |
| 3FE2 3FFA 4012 4042 | w | `ctx_page0 ctx_page1 ctx_panel_copy ctx_mfd` | draw contexts | many |
| 4068 | w[] | `mfd_mode_button` | right MFD mode -> button id | 8113 |
| 4080 4082 | w | `map_zoom0 / map_zoom1` | 2..9 / 0..7, defaults 8 / 1 | 8498, 84d6, 8387 |
| 4088 | b | `draw_page` | 0/1 | 17a3, 7898, ... |
| 408A | w[2] | `left_mfd_button` | 22, 23 | 80a0 |
| 409E | w | `msg_timer` | | 8b93, 7898 |
| 433A | b | `view_scale_shift` | | bf71, 2f82, 1442 |
| 433C | 3B | `view_zoom_label` | "8X" | bf71 |
| 4340 | w[8] | `view_zoom_range` | 10 30 50 80 100 150 200 250 | bf71 |
| 4350 | w[8] | `view_zoom_scale` | 1024 896 768 640 512 384 256 128 | bf71 |
| 4360 | w | `view_terrain_mode` | | bf71, 1442, e8b5 |
| 4362 | w | `view_last_obj` | | bf71, cba0 |
| 4364 | str | `"BRG "` | | bf71 |
| 436A | w[8] | `puff_offsets` | 0C00 0800 0400 0200 FE00 FC00 F800 F400 | c73c |
| 43AE | w | `pal_changed` | | cba0, 4d43, 1088:0006 |
| 43B0 | w | `panel_light_alt` | F4 | cba0, 01bf, 10a8:09ce |
| 43B2 | w | `sensor_flir` | F6 | cba0, 8113, a17e, bf71, ... |
| 43B4 | w | `view_dir_selected` | | cba0, 753f, a17e, b6ae |
| 43B6 | b | `alt_a_flag` | Alt-A | cba0, 1088:0120 |
| 43B8 | w | `fps_penalty` | | d707, 39c4 |
| 43BA | w | `kbd_sens` | 0..2 | cba0, 1e9e |
| 43BC | w | `fire_debounce` | | cba0, 39c4 |
| 43BE 43C0 | w | `key_button_latch[2]` | | cba0, cb72 |
| 43C2 | w | `snd_level` | 0..3 | cba0, d6bf, d6e5 |
| 43C4 | w | `flare_mode_timer` | | cba0 |
| 463E | b | `cockpit_overlay` | Alt-H | cba0, d9a3 |
| 46D8 | 12B x 37 | `cockpit_button[]` | `x,y,w,h,kind,state` | d83e |
| 480E | w | (button 0x19 state) | | d83e, bf71 |
| 98CE | w | `view_pitch_alt` | pitch used by mode 2/3 | a17e w, bf71 |
| 98D8 | w | `bay_anim` | 0..2 | cba0 |
| 98DE | w | `detail_level` | 0..3, init comm[32h] | cba0, 39c4, d76b, bf71 |
| 98E2 | w | `mission_clock` | | 39c4, cba0, 9208 |
| 9876 | str | `msg_buf` | | many |
| 9B04 | b | `sys_flags` | see 1.4 | cba0, 1e9e, 39c4, ... |
| 9B05 | b | `sys_flags2` | see 1.4 | cba0, 39c4, 7898, a17e |
| 9D08 9D0C 9D0E | w | `sensor_active`, `overlay_idx`, `overlay_on` | | bf71, a17e |
| A068 | w | `gear_lever` | | cba0, 39c4 |
| B04C | w | `wpn_flight_idx` | | 753f, bf71, a17e |
| B168 | w | `airspeed` | | 1e9e, cba0 |
| B80C | w | `nav_mode` | F7=1 / F8=0 | cba0, 9208 |
| BA10 / C02A | long | `plr_x / plr_y` | | many |
| C06E | w | `ejected` | | cba0, 1e9e |
| C07A | w | `view_mode` | | cba0 w, 2f82 ... |
| C0A2 C0A4 C0B2 | w | `ground_elev, plr_x16, plr_y16` | | many |
| C576 | w | `view_range` | | bf71 |
| C578 | str | `msg_text` | | 8b93, 7898 |
| DE90 | w | `key_word` | | 1e9e w, 43ee |
| DED4 / 9510 / 9512 | w | `cam_heading / cam_pitch / cam_roll` | | bf71, 2f82 |
| DEDA | w | `left_mfd_page` | | 80a0 |
| DFDC | w | `right_mfd_mode` | 0x13/0x14/0x15/0x18 | 8113 |
| E2FE | w | `master_mode` | F2: 0 nav, 1 air-to-air, 2 air-to-ground (R4) | cba0, 39c4, a17e |
| E55E | w | `bay_cmd` | | cba0 |
| E556 | w | `look_azimuth` | | cba0, 81f6 |
| E548 | far | `comm` | | everywhere |
| 98F4 | far | `pilot` | | everywhere |

## 5. Structures seen

* **Cockpit lamp entry** (`DS:3F02`, 6 B, index = lamp id): `+0 button id, +2 off value, +4 current value`. Entries 0..7 -> buttons
  `13 14 1F 22 20 11 12 10`; 8 -> 15 (off 3), 9 -> 24 (off 3), 10..18 -> `5 6 7 C B 9 8 D D` (off 10), 19 -> 4 (off 2, cur 30).
* **Cockpit button** (`DS:46D8`, 12 B x 0x25): `+0 x, +2 y, +4 w, +6 h, +8 kind (0..4), +10 state`. Buttons used here: 0x19 (232,184,17x8) FLIR,
  0x1F bay (257,103), 0x20 alt-hold (257,113), 0x21 (284,113), 0x22 gear (257,123), 0x23 (284,123), 0x11/0x12 chaff/flare lamps (4,179)/(36,179),
  0x13..0x15 (4/24/44,191), 0x16/0x17 left MFD pages, 0x1A/0x1C/0x1D/0x1E right MFD pages.
* **Ground site record** (`DS:B2A4`, 16 B, R4 `site_tab`): `+0 x, +2 y (inverted), +4 active, +6 flags (bit7 tested by bf71), +0Ch byte compared with [C0AC]/[DEA4]` (others R4/R9).
* **Air object record** (`DS:C140`, 0x24 B, R4 `air_tab`): `+0 x, +2 y, +4 alt, +0Eh/+10h/+12h angles (heading, pitch, roll), +14h model slot (index into 32-B entries at DS:2FDC), +16h flags (bit5 burning)`.
* **In-flight weapon record** (`DS:3C0C`, 0x1C B): `+0 x, +2 y, +4 z, +8 heading, +0Ah pitch, +0Eh active, +10h type (0x1B = missile)`.
* **Fire-puff state** (`DS:3A5C`, 8 B x 6; c73c) and **smoke puff record** (`DS:39DA`, 8 B x 16, filled by `479c fx_smoke_update`: `+0 x, +2 y (drifts by age>>9), +4 altitude/age (starts 0x40, +4 per frame), +6 rank`).
* **Nav waypoints**: 4 x `{x,y}` words at `DS:2E6C` (stride 4).



---
# Part 11: function index (generated from simflow_symbols.csv)

> **S3.7:** the render3d rows (146 functions + labels) moved to `render3d_symbols.csv`; their index is `render3d.md` Part 7.

> **S3.8:** the flight / detect / ai / weapons / damage rows (42 functions, 2 labels, 84 data; plus 6 new data rows) moved to `flight_symbols.csv`; their index is `flight.md` section 12.

| address | name | owner | signature | note |
|---|---|---|---|---|
| 0000:0010 | `main` | simflow | `void(void)` | VGAME entry from c0_start: comm/pilot ptrs, bind drivers, pages, gfxlib init, sim_init, sim_run, joystick cali |
| 0000:00f0 | `sim_init` | loader | `void(void)` | all loading: world import, STFLT.3DX, theatre files, speech.117, palette, 256pit.PIC, ordnance panel, cockpit  |
| 0000:01b7 | `cockpit_palette_refresh` | simflow | `void(void)` | pal_flight_load() then falls into cockpit_daynight_recolor; called from cba0 |
| 0000:01bf | `cockpit_daynight_recolor` | simflow | `void(void)` | 10A8:0004 asm HUD init then 3 rect_recolor (day 0F->0A, night 0A->0F) on page-2 ctx 4012 |
| 0000:0233 | `sim_run` | simflow | `void(void)` | timer_install, keypad hook, frame_loop, sound off, world export, frees, jmp timer_remove |
| 0000:02c0 | `stub_ret_02c0` | lib | `void(void)` | single ret (unused) |
| 0000:02c1 | `stub_ret_02c1` | lib | `void(void)` | single ret (unused) |
| 0000:02c2 | `gfx_pages_init` | simflow | `void(void)` | page0=A000 -> 85E2/85EA, page1=gfx_alloc(1) -> 85E4, page2=comm[20h] -> 85E6; page_seg_set_b for 1 and 2 |
| 0000:02fe | `ordnance_panel_build` | simflow | `void(void)` | ordnance.pic -> page 1; 2 or 4 weapon strips 77x15 to page 2 at (169,110+15i); sprite_grab 77x60 -> BA0E |
| 0000:04bb | `frame_delay` | simflow | `void frame_delay(int ticks)` | Busy-wait ticks increments of byte DS:264A (timer tick); arg = DS:43B8 frame-rate menu value |
| 0000:097a | `load_theatre_data` | loader | `void(void)` | load_3dg_terrain, load_3dx_banks(stem), load_3dt_scenery(stem), [0CA8]=0 |
| 0000:0994 | `load_3dx_banks` | loader | `void(char *stem)` | theatre .3DX -> bank1 (B760), PHOTO.3DX -> bank2 (B762), flag tables, id table 0510, mission prop entries, KU  |
| 0000:0af9 | `load_3dt_scenery` | loader | `void(char *stem)` | .3dT: type counts 0614, rec counts 061E, 7-byte records at A06A, ptr table E304[32*level+type] |
| 0000:0c2c | `load_3dg_terrain` | loader | `void(void)` | .3dG: L3 map 9F68, L2 9D10, L1 9B08, L0 9904; copies 64-byte L4 theatre table 0A68+64*t to B00C |
| 0000:0cd1 | `load_error_msg` | loader | `void(const char *msg)` | 8AA2(msg,0,0x60,0xF) then getch; returns (no exit) |
| 0000:0ce7 | `set_file_ext` | lib | `void(char *name,const char *ext)` | replace text from first . (or NUL) with ext; used on the stem buffer |
| 0000:17a3 | `cockpit_draw_adi` | simflow | `void cockpit_draw_adi(void)` | Attitude indicator 32x28 at (265,140): horizon polygon col 1 on col 9, bezel 91a4, copy back page to screen; o |
| 0000:1961 | `mfd_view_sky_bands` | simflow | `void(int horizon)` | up to 16 sky quads colours 80h+i above horizon row, clamped at y=6Eh; table DS:0D22 |
| 0000:19ea | `mfd_view_ground_bands` | simflow | `void(int horizon,int mode)` | up to 16 ground quads colours 70h+i (F0h if mode==1) below horizon, clamped at y=A9h |
| 0000:1bb0 | `frame_loop` | simflow | `void(void)` | saves bp si di es and calls frame_loop_body |
| 0000:1bbc | `frame_loop_body` | simflow | `void(void)` | THE frame loop: 2F82,7898,[C07A]==0:(10A8:0000,D9A3,17A3),flip,[262A]=1,1E9E,39C4 until [E552] |
| 0000:1bfc | `timer_install` | simflow | `void(void)` | [2636]=[2640]=1, clock=0, timer_calibrate, save INT 8 in cs:1CEB, install timer_isr, [262B]=1 |
| 0000:1e0b | `pit_read_retrace` | lib | `u16(void)` | wait retrace start on 3DAh (3BAh mono) then latch and read PIT ch0; 0 on timeout |
| 0000:2e64 | `fx_ratio_q15` | lib | `int(int x,int y)` | x/y in Q15 with signs |
| 0000:2ebe | `fx_asin` | lib | `u16(int q15)` | inverse sine via table 2084 |
| 0000:2f2f | `fx_acos` | lib | `u16(int q15)` | 0x4000-asin |
| 0000:2f40 | `fx_isqrt` | lib | `int(int n)` | Babylonian isqrt |
| 0000:2f82 | `view_frame` | simflow | `void view_frame(void)` | View+cockpit frame: camera per C07A mode, matrix B156, picture reload, calls 03ce, cockpit overlays, sets 2E34 |
| 0000:38ab | `hud_number_draw` | simflow | `void(int slot,int v)` | 3-digit zero padded number via d92d |
| 0000:38f8 | `hud_draw_fuel` | simflow | `void(void)` | slot 3 = min(fuel,9999)/10 |
| 0000:3926 | `hud_draw_throttle` | simflow | `void(void)` | slot 4 = throttle |
| 0000:3939 | `sim_pause_wait` | simflow | `void(void)` | snd 25, wait key != Alt-P, restore 2646 |
| 0000:3968 | `cockpit_refresh_panels` | simflow | `void cockpit_refresh_panels(void)` | After pit picture reload: re-light lit lamps (tab 46D8), 80a0(DEDA), 8113(DFDC), 38f8, 3926, 44a9 |
| 0000:39c4 | `sim_frame_step` | simflow | `void(void)` | Per-frame simulation step (once per 1bbc iteration): grid pos, init, map markers, updaters, airfield/landing/g |
| 0000:44a9 | `hud_draw_store_counts` | simflow | `void(void)` | Redraw flare/chaff/decoy counters (a=1..3 via 44c4) |
| 0000:44c4 | `hud_draw_store_count` | simflow | `void(int type)` | Draw 2-digit store count of type 1 flare 2 chaff 3 decoy in panel field 3D9C[type] via d92d |
| 0000:479c | `fx_smoke_update` | simflow | `void(void)` | Smoke puffs (39DA x16 x8 B) over burning site 3D68: age+=4, y drift, spawn every 8 steps |
| 0000:483c | `fx_fall_update` | simflow | `void(void)` | Falling body C602 altitude, B756 velocity (gravity -12/step, min -16) |
| 0000:4857 | `sim_reset_clock_palette` | simflow | `void(void)` | Seed rand, reset cockpit (784a), 3D60 random, start clock 9870, 98E2=0, palette pre-steps toward day/night tab |
| 0000:48dd | `mission_cheat_rearm` | simflow | `void(void)` | Training cheat (Alt-R): clear damage, bays 9/1, gun 1000, fuel 5000 |
| 0000:4926 | `mission_end` | simflow | `void(int code)` | Set E552=1, write comm+28/26/74/76/34/36, log event 8; code 7 aborts; non-zero codes ignored while C06E!=0 |
| 0000:499e | `mission_log_event` | simflow | `void(int code,int aux)` | Append 6-byte record {98E2,px>>7,py>>7,code,aux} to DS:BA2A (max 255), terminator code=0 |
| 0000:49e7 | `msg_site_label` | simflow | `void(int site)` | Build "Noun at Place" label for site in DS:9876 (max 24 chars + ".") |
| 0000:4a73 | `site_name` | simflow | `void site_name(int site)` | builds site place/noun name into DS:9876 (max 18 chars + ".") |
| 0000:4ad0 | `mission_world_import` | simflow | `void(void)` | unpack comm+7Ah blob, build name pointer table DEDC, start position BA10/BA12 and C02A/C02C from slot0 anchor  |
| 0000:4b57 | `mission_name_targets` | simflow | `void(void)` | Give each mission target object its own name entry (clone class attr + string, re-class world cell, T.+14) |
| 0000:4c29 | `mission_world_export` | simflow | `void(void)` | blob fields to comm+7Ah plus 0x600-byte event log BA2A; called at the end of sim_run (also on quit) |
| 0000:4c41 | `wld_blob_fields` | simflow | `void(void)` | ordered list of 16 DGROUP fields copied to/from the world blob via wld_blob_copy |
| 0000:4cef | `wld_blob_copy` | simflow | `void(void *p,u16 n)` | movedata between DS:p and blob cursor 94EC (dir by DE8E: 1 import, 0 export); cursor += n |
| 0000:4d2f | `wld_blob_rewind` | simflow | `int(void)` | cursor 94EC:94EE = comm+7Ah; returns 0 |
| 0000:4d43 | `pal_time_of_day_step` | simflow | `void(void)` | Once per second: every 16 s step one palette colour range 1 unit toward day/night table and upload |
| 0000:4e0f | `hud_glideslope_update` | simflow | `void(void)` | Compute glide-slope state 46C2 (0 gear up,1,2 ok,3) from slope angle vs 7.98 deg tolerance |
| 0000:4ebc | `hud_lamp_timers` | simflow | `void(void)` | Count down chaff/flare/aux indicator timers and set lamps 0xE/0xF/0x18 via 4f05 |
| 0000:4f05 | `hud_lamp_set` | simflow | `void(int lamp,int on)` | If cockpit visible and lamp state differs call d83e(3FE2,lamp,on) |
| 0000:784a | `cockpit_widgets_reset` | simflow | `void(void)` | mission-start reset of panel widgets (d83e x4), [409C]=0, [DFDC]=0x18; called from 4857 |
| 0000:7898 | `hud_overlay_frame` | simflow | `void hud_overlay_frame(void)` | per-frame HUD C part: flash, lamps 0-2, stall lamp, weapon/gun line, crosshair, tapes, AAM circle, speed/alt n |
| 0000:80a0 | `mfd_left_select` | simflow | `void mfd_left_select(int mode)` | left MFD mode 0 map / 1 scope; lamp 22/23 via d83e; F3 |
| 0000:8113 | `mfd_right_select` | simflow | `void mfd_right_select(int page)` | right MFD page 0x13 TV 0x14 nav 0x15 weapons 0x18 objectives; lamp via DS:4068 |
| 0000:81f6 | `mfd_tv_label` | simflow | `void mfd_tv_label(void)` | Ahead/Right/Rear/Left label at (197,193) on ctx0 from DS:E556 |
| 0000:829d | `mfd_refresh` | simflow | `void mfd_refresh(int page)` | page 0x16 = update 7 damage lamps (898b 10..16); redraw right MFD if page == DFDC |
| 0000:82e9 | `mfd_init_geometry` | simflow | `void mfd_init_geometry(void)` | sets DEDA=0 and left MFD rect vars then tail-jumps into 8498 (one zoom-in) |
| 0000:8316 | `hud_threat_bar` | simflow | `void(int val,int color,int y0,int y1)` | Signal bar: x 0x56..0x56+val/2 (val>0) or 0x88+val/2..0x88 (val<0), rows 0xBB+y0..0xBB+y1 via 1001:024C on ctx |
| 0000:8387 | `mfd_map_redraw` | simflow | `void mfd_map_redraw(int cellx,int celly)` | Left MFD map: centre (4084,4086) clamped ahead of aircraft, 119c terrain, site icons 8548, 973c; DEDA=0 |
| 0000:8498 | `mfd_zoom_in` | simflow | `void mfd_zoom_in(void)` | z key: map zoom DS:4080 <9 / scope zoom DS:4082 <7 / camera range 3D70-- in views with bit 0x80 |
| 0000:84d6 | `mfd_zoom_out` | simflow | `void mfd_zoom_out(void)` | x key: map zoom >2 / scope zoom !=0 / 3D70++ |
| 0000:8514 | `mfd_map_sx` | simflow | `int(int wx)` | ((wx-[4084])>>(10-[4080]))+0x70 |
| 0000:852b | `mfd_map_sy` | simflow | `int(int wy)` | ((((wy-[4086])>>(10-[4080]))*3)>>2)+0x8B |
| 0000:8548 | `mfd_map_plot` | simflow | `int(int wx,int wy,int color,int big)` | World point on moving map; 1 if off window, 0 else; big = 2x2 dot; skips if [DEDA] or !cockpit |
| 0000:85ed | `mfd_map_to_screen` | simflow | `int(int wx,int wy,int*sx,int*sy)` | World->map screen coords, returns 1 if strictly inside the map window |
| 0000:863c | `mfd_map_arc` | simflow | `void(int cx,int cy,int r,int color,int solid,int a0,int a1)` | Arc a0..a1 (1/256 turn, step 16) radius r in world units; dots or solid polyline (Doppler) |
| 0000:86e5 | `mfd_map_line` | simflow | `void(int wx0,int wy0,int wx1,int wy1)` | World line on map via 87a9 clipped to map window, both pages |
| 0000:871f | `hud_line_full` | simflow | `void(int x0,int y0,int x1,int y1)` | 87a9 with full screen clip 0..319,0..199, one page |
| 0000:873f | `hud_line_view` | simflow | `void hud_line_view(int x0,int y0,int x1,int y1)` | line in ctx0 colour clipped to ctx0 window (0,0)-(319,90) via 1001:032A |
| 0000:87a9 | `hud_line_clip` | simflow | `void(int x0,int y0,int x1,int y1,int xmin,int xmax,int ymin,int ymax,int both)` | Set clip window/offset, args into DS:2293..229F, call 1001:032A; optionally also on the other page |
| 0000:8888 | `hud_line_full2` | simflow | `void(int x0,int y0,int x1,int y1)` | Duplicate of 871f |
| 0000:88a8 | `hud_box` | simflow | `void hud_box(int x0,int y0,int x1,int y1,int col)` | outline rectangle with four 8888 lines |
| 0000:88f6 | `hud_line_win` | simflow | `void hud_line_win(int x0,int y0,int x1,int y1)` | 87a9 line clipped to HUD window (48..271 x 15..90) or (99..220 x 30..90) if 294B |
| 0000:8929 | `gfx_set_fg` | simflow | `void(int c)` | fg of ctx 3FE2 and 3FFA |
| 0000:893f | `gfx_fill_rect` | simflow | `void(x0,y0,x1,y1)` | 1001:024C on page0 and page1 ctx |
| 0000:8970 | `hud_pixel` | simflow | `void(int x,int y,int color)` | 8929(color); 871f(x,y,x,y) |
| 0000:898b | `hud_lamp_update` | simflow | `void(int idx,int state)` | lamp state table 3F02, drawn via d83e |
| 0000:89fe | `mfd_text` | simflow | `void mfd_text(int mode,int col,int row,char *s,int colour)` | MFD text cell x=col*4+4A/A9 (mode 1/2) y=row*6+6E via 8aa2 |
| 0000:8a4e | `mfd_clear` | simflow | `void mfd_clear(int which,int colour)` | forwards to 8a5e |
| 0000:8a5e | `mfd_fill` | simflow | `void mfd_fill(int which,int colour)` | set_color then fill left (74,110)-(150,169) or right (169,110)-(245,169) MFD |
| 0000:8aa2 | `hud_text` | simflow | `void hud_text(char *s,int x,int y,int col)` | text on ctx0 or ctx1 chosen by DS:4088 via 8b04 |
| 0000:8ad3 | `hud_text2` | simflow | `void hud_text2(char *s,int x,int y,int col)` | duplicate of 8aa2 |
| 0000:8b04 | `txt_draw` | simflow | `void txt_draw(TextCtx *c,char *s,int x,int y,int col)` | font 0, strupr in place, text_noclip (4 px per char) |
| 0000:8b3b | `hud_num_left` | simflow | `void hud_num_left(int v,int x,int y,int col)` | itoa to DS:C5D8 and draw left aligned |
| 0000:8b5c | `hud_num_right` | simflow | `void hud_num_right(unsigned v,int xr,int y,int col)` | ltoa unsigned, right aligned x = xr - 4*len |
| 0000:8b93 | `msg_post` | simflow | `void msg_post(char *text)` | strcpy(DS:C578,text); DS:409E = 3*DS:3660 frames |
| 0000:8bd3 | `gfx_restore_rect` | simflow | `void(x,y,w,h)` | page2 -> page0 and page1 |
| 0000:8c10 | `mfd_scope_draw` | simflow | `void mfd_scope_draw(int flip)` | left MFD mode 1 scope: grid, air units, missile tracks, sites, ownship, countermeasure marks |
| 0000:903f | `mfd_target_box` | simflow | `void mfd_target_box(int x,int y,int col)` | 9x7 selection frame around a scope icon |
| 0000:90b9 | `mfd_w2s` | simflow | `void mfd_w2s(int wx,int wy)` | world to scope: DS:49E0/49E8, outside flag DS:DE92; ownship at (112,152) heading up |
| 0000:9154 | `mfd_icon` | simflow | `void mfd_icon(int cx,int cy,int x,int y)` | 7x7 keyed icon from panel sheet (cx*8+1, cy*8+0x29) clipped to left MFD |
| 0000:91a4 | `cockpit_keyblit` | simflow | `void cockpit_keyblit(int dx,int dy,int sx,int sy,int w,int h,int clip)` | Key-colour blit from picture page E558 to back page 4088 using BlitCtx at DS:4062 |
| 0000:9208 | `mfd_nav_page` | simflow | `void mfd_nav_page(void)` | right MFD page 0x14 route list: TIME, 4 waypoint legs, ETA, arrival clock, fuel bars |
| 0000:94a5 | `mfd_map_route` | simflow | `void(void)` | Draws player -> waypoints DS:2E6C (4 x,y) from index [2E7C]; colour 15 first leg then 10 |
| 0000:950e | `mfd_fuel_bar` | simflow | `void mfd_fuel_bar(int v,int col)` | bar y 164..167 from x=170 width v/135 (v clamped 0..10000) |
| 0000:955c | `fmt_clock_hhmm` | simflow | `void fmt_clock_hhmm(unsigned t)` | DS:9876 = "HH:MM" mission clock (t in 2 s units; +6 h day / +18 h night) |
| 0000:95c4 | `fmt_clock_hhmmss` | simflow | `void fmt_clock_hhmmss(unsigned t)` | DS:9876 = "HH:MM:SS" |
| 0000:9641 | `fmt_2digit` | simflow | `void fmt_2digit(int n)` | append n%60 as two digits to DS:9876 |
| 0000:9678 | `mfd_weapons_page` | simflow | `void mfd_weapons_page(void)` | right MFD page 0x15: per slot name line and quantity line, colour 0x2F |
| 0000:973c | `mfd_map_save_bg` | simflow | `void(void)` | blit_rect map window (74,110) 77x60 from draw page to backup page ctx [4012] |
| 0000:9770 | `mfd_map_restore_bg` | simflow | `void(void)` | Reverse of 973c: backup page -> draw page |
| 0000:97a4 | `mfd_objectives_page` | simflow | `void mfd_objectives_page(void)` | right MFD page 0x18 mission orders text builder (PRIMARY/SECONDARY Mission, verb phrase, noun at place, Before |
| 0000:99a7 | `hud_ils` | simflow | `void hud_ils(void)` | F9 ILS needles: localiser x 139..181, glide slope y 40..72, label "ILS" |
| 0000:a17e | `obj_overlay_frame` | simflow | `void(void)` | Per-frame (from 03ce after db3e): projectile markers, cannon tracers+hit tests, explosion sprite, ground/air t |
| 0000:b339 | `hud_draw_diamond` | simflow | `void(void)` | 4-line diamond +-4 px around (DS:49E0,49E8) in HUD colour [2CA0] |
| 0000:b3b3 | `hud_update_aspect_label` | simflow | `void(void)` | quadrant of (2DEE-DED4+0x2000)&0xC000 (left/right swapped); on change [E556]=q and 81F6() redraws Ahead/Rear/R |
| 0000:b3e6 | `hud_target_box` | simflow | `void(int x,int y,int size,int hex,int colour)` | rect (hex=0) or hexagon (hex!=0) target bracket half-width size (halved if [294B]) height 3/4 size; no-op if [ |
| 0000:b4f3 | `hud_seeker_cross` | simflow | `void(int animate)` | AGM-65 TV seeker cross: 4 ticks around (207;140) gap from anim table DS:42E2 (0 2 4 6 4 2) index shared [42E0] |
| 0000:b5af | `hud_lock_bracket` | simflow | `void(int animate)` | converging square in right MFD panel (169..245 x 110..169) half-size from DS:42F2 (12 10 8 6 4 2 0) index [42E |
| 0000:b6ae | `hud_lock_indicator` | simflow | `void(int mode,int lock)` | sets (b04E,b050)=(207;140); Maverick seeker cross; lock bracket; cockpit lamp 3 on (steady if lock else blinki |
| 0000:b77a | `hud_ping_label` | simflow | `void(char *s,int colour,int dy)` | expanding hexagon (half-size dy) + centred name text at projected point DS:49E0/49E8 (text colour DS:9872) |
| 0000:b7f8 | `hud_format_range` | simflow | `void(int dist)` | DS:9876 = "Range " + (dist>>6) + "." + ((dist&63)*2/13) + " km" |
| 0000:bd24 | `load_stflt_3dx` | loader | `void(void)` | stflt.3dx -> bank 0 (B75E); flags to 0760; 09E0[i]=6 |
| 0000:bf71 | `mfd_view_render` | simflow | `void(unsigned obj,int x,int y,int z,int a0,int a1,int a2,int mode,int zshift)` | Sensor/seeker view window x169-245 y110-169: camera, sky/ground bands, 3D object, FLIR tint, BRG and zoom labe |
| 0000:c6de | `mfd_view_flir_tint` | simflow | `void(int x,int y)` | Shade-fill (poly_fill mode 3, ramp 98h-9Fh) of the 77x61 sensor window at offset (x,y) = FLIR green look |
| 0000:c8fa | `lib_scale_eighths` | lib | `int(int mode,int v)` | mode 0..3 -> v*0.5/0.625/0.75/0.875 (sar), else v |
| 0000:c94e | `fx_clamp_wrap` | lib | `int(v,lo,hi)` | clamp with wrap quirk (v<=-0x4000 -> hi) |
| 0000:c96d | `fx_clamp` | lib | `int(v,lo,hi)` | plain clamp |
| 0000:c987 | `fx_dist` | lib | `int(a,b)` | max+min/2 capped 7FFF |
| 0000:c9d6 | `fx_heading` | lib | `u16(x,y)` | angle from +y, polynomial atan |
| 0000:caee | `fx_sin_mul` | lib | `int(a,m)` | mul15(m,sin16(a)) |
| 0000:cb07 | `fx_cos_mul` | lib | `int(a,m)` | fx_sin_mul(a+0x4000,m) |
| 0000:cb1b | `fx_sext8` | lib | `int(int x)` | sign-extend low byte |
| 0000:cb39 | `lib_sign` | lib | `int(int v)` | signum |
| 0000:cb56 | `rand_seed_timer` | lib | `void(void)` | srand(bios_timer) |
| 0000:cb62 | `rand_n` | lib | `int(int n)` | rand()*n>>15 |
| 0000:cba0 | `input_command_key` | simflow | `void(unsigned key)` | Per-frame key command interpreter called from 43ee with [DE90]: 1-9, F1-F10, Alt/Shift keys, debug keys, nav m |
| 0000:d6bf | `snd_cue` | simflow | `void(cmd,prio)` | snd_cmd gated by snd_level 43C2 and eject state |
| 0000:d6e5 | `snd_engine_refresh` | simflow | `void(void)` | snd_cmd 24 or 25 |
| 0000:d707 | `frame_params_update` | simflow | `void(void)` | clamp rate 4-3D7A..15; 43B8 B16C B286 9F64 |
| 0000:d7bb | `frame_rate_halve` | simflow | `void(void)` | 3D7A 2->1, rate<<=1 |
| 0000:d7d0 | `nav_copy_xy` | simflow | `void(dst,src)` | copy site xy to 2E6C+4*dst |
| 0000:d7f4 | `pic_show` | loader | `void(name,page)` | open + 11B9:000E decode + close |
| 0000:d81d | `file_open_ro` | loader | `int(name,mode)` | 1199:000C dos_open |
| 0000:d82f | `file_close` | loader | `void(h)` | 1199:00C8 dos_close |
| 0000:d83e | `hud_lamp_recolor` | simflow | `void(ctx,lamp,on)` | rect_recolor lamp rect, table 46D8/4672 |
| 0000:d92d | `hud_digits_draw` | simflow | `void(page,slot,str)` | blit 4x7 digits from page2 strip |
| 0000:d9a3 | `hud_side_strips` | simflow | `void(int page)` | side bars + store highlight 46C2 |
| 0000:ea26 | `pal_fade_step` | lib | `int pal_fade_step(RGB far*cur,RGB far*target,int count)` | moves each component of cur one step toward target, returns number changed (callers 4857,4D43) |
| 0000:ea7c | `file_load_paras` | loader | `u16(const char *name)` | fopen r+b, filelength, check free paragraphs, alloc, int86x read loop (64K chunk bug); returns segment or 0; s |
| 0000:f0b0 | `ltoa` | lib | `char *(long v,char *buf,int radix)` | signed entry (bl=1) of the number conversion at FA38; f094 is the unsigned head |
| 0000:f186 | `filelength` | lib | `long(int fd)` | -1 if bad fd; lseek cur/end/restore |
| 0000:f206 | `int86x` | lib | `int(int n,REGS *in,REGS *out,SREGS *s)` | CD nn CB on stack; loads ES/DS from s; ax result, cflag; CF -> dosmaperr |
| 0000:f2b8 | `strupr` | lib | `char *(char *s)` | in-place a-z upper-case, returns s |
| 0000:f492 | `rtl_aFlshr_mem` | lib | `void(long *p,int n)` | *p = rtl_aFlshr(*p,n) (in-place arithmetic shift right); partner of f476 |
| 0000:f4ae | `rtl_aFuldiv` | lib | `u32(u32 a,u32 b)` | unsigned 32/32 divide helper |
| 0000:f5e4 | `rtl_dosmaperr_al` | lib | `void(unsigned char al)` | ah=0 then rtl_dosmaperr (f5ea) |
| 0000:fde2 | `free` | lib | `void(void *p)` | jmp rtl_nfree (fe14) |
| 0000:fe10 | `malloc` | lib | `void *(unsigned n)` | jmp rtl_nmalloc (fe35) |
| 0000:ffb7 | `rtl_nh_last_free` | lib | `internal` | near-heap list walk to the last free block (sentinel FFFEh), used by rtl_nh_grow |
| 0ffd:0008 | `drv_bind` | loader | `void(u16 seg)` | driver header seg:0: [18h] code seg, [1Ch] first slot, [22h] count, [24h..] offsets -> DS:011A+5*(first+k)+1/+ |
| 1088:0006 | `pal_flight_load` | simflow | `void(void)` | climate ramps (DS:0D47!=2) into A/B/C, pick A/B/C by [43AE]/[3658], copy 768 B to 104A, set DAC 256, MFD ramp  |
| 1088:00a3 | `pal_mfd_ramp_green` | simflow | `void(void)` | set DAC 98h..9Fh from DS:206A (green MFD ramp) |
| 1088:00bd | `pal_mfd_ramp_rt` | simflow | `void(void)` | set DAC 98h..9Fh from DS:1212 (runtime ramp) |
| 1088:00d7 | `pal_save` | simflow | `void(void)` | pal_read_dac -> DS:0D4A (768 B) |
| 1088:00ee | `pal_restore` | simflow | `void(void)` | set DAC 0..255 from DS:0D4A |
| 1088:0107 | `pal_set_adv` | simflow | `void(void)` | set DAC 0..255 from DS:134A (ADV.PAL, quit requester) |
| 109d:0006 | `fx_mul15` | lib | `int(a,b)` | (a*b+0x4000)>>15 |
| 109d:006c | `fx_cos16` | lib | `int(a)` | sin16(a+0x4000) |
| 109d:007c | `fx_sin16` | lib | `int(a)` | table interpolation 2084 |
| 109d:0088 | `fx_sin16_reg` | lib | `reg BX` | register entry of 0090 |
| 109d:0090 | `fx_sin_interp` | lib | `reg BX` | T[i]+round(d*frac/256) |
| 10a8:0000 | `hud_draw_far` | simflow | `void(void)` | Far thunk to hud_draw_front; called by frame loop 1bbc when [C07A]==0 |
| 10a8:0004 | `hud_color_init_far` | simflow | `void(void)` | Far thunk to hud_color_init; called from 01bf (cockpit recolour) |
| 10a8:0008 | `hud_layout_init_far` | simflow | `void(void)` | Far thunk to hud_layout_init; called by the C-key handler in 1e9e (start-up init is the fall-through of 09CE) |
| 10a8:000c | `hud_draw_front` | simflow | `void(void)` | Front-cockpit HUD: airspeed/altitude/heading tapes, frame, reticle, G text, pitch ladder (style [294B], level  |
| 10a8:09ce | `hud_color_init` | simflow | `void(void)` | Sets panel seg [E558] in blit ctx B0..B3 and day/night HUD colours (0A/2F day, 0F/07 night) |
| 10a8:0a22 | `hud_layout_init` | simflow | `void(void)` | Fills layout block DS:291C..2948 + text/blit ctx fields for HUD style [294B] (0 wide, 1 compact) |
| 10a8:0d70 | `joy_get_calib` | lib | `void(void far *dst)` | copy 20 words DS:2CA2 -> far dst (comm+48h) |
| 10a8:0d82 | `joy_set_calib` | lib | `void(void far *src)` | copy 20 words far src (comm+48h) -> DS:2CA2 |
| 1199:00c8 | `dos_close` | lib | `void(int h)` | INT 21/3E, fatal on error |
| 1199:00f0 | `dos_read` | lib | `int(int h,u16 n,void *buf)` | INT 21/3F into DS buffer, fatal on error (unused) |
| 1199:0122 | `dos_read_far` | lib | `int(int h,u16 n,u16 off,u16 seg)` | INT 21/3F into seg:off (unused) |
| 1199:0178 | `dos_buf_open` | lib | `void(int h)` | [4632]=h then dos_buf_fill (unused) |
| 1199:0199 | `dos_write_far` | lib | `int(int h,u16 n,u16 off,u16 seg,u16 off2)` | INT 21/40 from seg:(off+off2), fatal on error (unused) |
| 11b9:000e | `pic_to_page` | loader | `void(int handle,int page)` | decode a .PIC into a page (slot 56 page_es, clear, row by row scanline_put slot 51); the only front end used ( |
| 11b9:0083 | `pic_to_seg_a` | loader | `void(int handle,u16 seg)` | same into a segment, slots 51/55 (never called) |
| 11b9:00ee | `pic_to_seg_b` | loader | `void(int handle,u16 seg)` | same into a segment, slots 52/54 (never called) |
| 11ce:0009 | `pic_hook_nop` | lib | `void(void)` | ret (same bytes as 11B9:0159) |
| 11ce:000a | `pic_row` | loader | `void(void)` | one 320-pixel row into DS:9516 (calls pic_begin on first row, rle90_fill) |
| 11ce:0034 | `pic_begin` | loader | `void(void)` | read header word: bit7 -> 8-bit pixels else 4-bit; max code bits; init LZW table C688 (2048*3 B) |
| 11ce:00b2 | `pic_rle90_fill` | loader | `void(void)` | expand [965A] pixels with RLE escape 90h; 4-bit mode emits 2 px per byte |
| 11ce:012b | `pic_lzw_next` | loader | `void(void)` | variable-width LZW decoder step using a private stack and the 512 B file buffer 43C8 |
| 14a2:00c5 | `pal_read_dac` | simflow | `void(void far *dst)` | wait retrace, read 768 bytes of DAC from index 0 into dst (6-bit) |


---
# Part 12: render3d interface (moved to render3d.md, S3.7)

The renderer is specified bit-exact in `port/spec/render3d.md` (S3.7), which replaces the R11a/R11b extracts that stood here
(several of their details were corrected there). Where to find what the sim side needs:

| topic | render3d.md |
|---|---|
| scene entry `03CE` (arguments from `2F82`), `DAE8`/`DB3E` set-up and end, viewport `DB71`, camera matrices | Part 1 section 1 |
| coordinates, angles, Q15 helpers, matrix conventions | section 3 (conventions), Part 6 C (1433), simflow Part 2 section 1 (sine table, 109D) |
| draw order and sorting (terrain walk, object queue 50 entries farthest-first) | section 2.1, Part 1 sections 2 and 5 |
| sim-object submission `BD8F`, `DCC0`, `BEE1`, `E7AC`; queue record layout (0x16 bytes) | Part 1 sections 4-5 |
| per-frame object pass `9ACA`, projection `B85D` (49E0/49E8/DE92) | Part 2 |
| object renderer `11EB:00D3` call contract, var table DS:C02E, inside flag DS:C686, working buffers | Part 4 |
| map MFD terrain `119C`, collision probe `77D9`/`0FFB` | Part 3 |
| sky/ground `1442`, 2D edge tables / clipper of 1001 used by the HUD and cockpit lines | Part 6 |
