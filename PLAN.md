# F-117A Nighthawk Stealth Fighter 2.0 — SDL3 port plan

Same approach as `../AcesOfThePacific` and the Test Drive ports: a **faithful C port** here
(reverse-engineer → document → port function by function, original data read at run time), then a
separate **F-117A Enhanced** repo built on it. Token efficiency is a first-class goal: small subtasks,
one session each, the cheapest model that can do the job, and the user doing whatever is cheap for a
human and expensive for a model (looking at pictures, playing the game, driving DOSBox).

## User to-do

What the agents need from you now, most urgent first. Agents add a row when they hand you a task and
remove it when it is done; subtasks marked `U<n> pending` are added automatically on the plan page.

| Task | What to do | For | Time |
|---|---|---|---|
| U-front | **First visible milestone (U4).** Run the launcher's Play (or `f117port/build/f117 --game-dir Game`) next to the original in DOSBox-X (as U2, then `F117`) and compare: (1) SETUP: the questionnaire in text mode. The port draws the 80x25 text with an 8x8 font; only the layout, the colours (white on blue banner, yellow prompts) and the leftover `.` at the end of row 20 matter. Answer N, then 2 (keys 1-4 pick the sound driver, Esc quits). (2) MPS_LOGO: the logo animation: length (about 8 s), colours, smoothness; Esc or any key must finish the picture at once and skip the PLAYER intro. (3) PLAYER intro (title-1, title-2, title-3a, credit, c-pit, bunker in turn; silent until S5.9): does the first frame fade in from black over about 5 s as in the original (the old U-player question), and do the frames then run at the speed you remember (20 frames/s)? Colours right, no flicker, any key / mouse click / joystick button ends it. (4) After the intro the port reaches the START placeholder (a test pattern): that is expected. Reply with what looks different. | S5.3 | 15 min |
| U-shell | **Playtest of the shell next to the original (U6).** Run the port (`f117port\build\f117 --game-dir Game`, or the launcher's Play) and the original in DOSBox-X (U2, then `F117`) and compare START from the roster on, same keys in both: (1) roster screen: the ten rows (rank, name, career score, missions, status), the info panel under the hovered row (medals, best / last mission, options), Delete = name entry with the `_` cursor (type, Backspace, Enter, Esc), a click on a pilot = the options panel (typed title, markers, hover colours, Done / Esc), the retired / killed dialogs; (2) home screen: the card texts and the rating number, the five hotspots, Alt-Q (adv picture: Y quits, another key returns); (3) orders: theatre / war level / mission type, the tooltip panel animation, markers, Fly; (4) briefing map: the route animation (Space skips it), the pop-up texts, the site buttons and their data boxes, range rings (radar / SAM / hostile, Space = max / average), the folder pages (button 6), arming (weapon picture and text, bays, fuel line), the hangar and the launch; (5) the generated mission for the same choices (the dice differ, so compare the structure: objective wording, time format, place names, grid references, suggested weapons, fuel); (6) ROSTER.FIL after leaving the roster (same size, your new pilot). Reply with what looks different (screen, text, keys, mouse; sound is silent until S5.9). | S5.4 | 30 min |
| U-sim | **Playtest of the cockpit next to the original (U7).** Run the port (`f117port\build\f117 --game-dir Game`, or the launcher's Play) and the original in DOSBox-X (U2, then `F117`), arm and launch the same mission and compare the cockpit, same keys in both. The 3D window is a flat sky / ground backdrop until S5.6, the aircraft does not fly until S5.7 and there is no sound until S5.9, so look at: (1) the HUD in the front view: airspeed / altitude / heading tapes, frame, reticle, pitch ladder (key `v` cycles the HUD level, `c` the style), colours of tapes and text; the first tick strip must have the same colour as the second (**U-vgame 4, first half**); (2) the left MFD (F3 = map / scope): the map starts at zoom 2, `z` / `x` zoom, the scope grid, unit icons, signal bars and arcs colours (**U-vgame 3**); (3) the right MFD pages: F5 weapons, F7 / F8 navigation (**U-vgame 5**: ETA and units), F10, and the sensor window with `m , . /` (**U-vgame 2**): window at x 169-245, y 110-169, `BRG nnn` and the zoom label, F6 = FLIR green tint, the sky / ground bands (Alt-D changes the detail level; the object itself is missing until S5.6); (4) the ADI at the lower right and the lamps (keys `1`..`9`, Enter, Alt-H, Alt-C); (5) the views: F1, Shift+`<` `>` `M` `?`, Shift+F1..F6: in the rear view is the centre 138x157 of `256rear.pic` pasted opaque over the scene, and the frames of the side views (**U-vgame 1**); (6) Alt-Q (requester: Y ends the flight, END placeholder), Alt-P pause, Esc. Reply with what looks different (screen, text, keys). | S5.5 | 40 min |
| U-sound | **Sound of the port next to DOSBox-X (S5.9, AdLib in both).** Run the port (`f117portuild117 --game-dir Game`, SETUP answer 2 = AdLib; the launcher's Sound box must be on) and the original (U2, then `F117`) and listen: (1) the logo jingle; (2) the PLAYER intro music (about 90 s; any key ends it): tempo and pitch, no odd speed-ups when effects play; (3) the shell clicks and button sounds in START; (4) in flight: engine drone rising with the throttle, missile and gun launches, warnings (SAM / lock), explosions, gear and flaps; (5) the debrief music (landing, crash, promotion). Listen for tempo, pitch and missing sounds. The sequencer is proven register for register against the original driver (`ctest sound`), so a difference would be the OPL emulation (Nuked-OPL3 vs DOSBox-X), the timing (PLAYER, VGAME, END poll at 60.08 Hz, START at 70.09 Hz, the logo at 60.88 Hz) or the volume. Speech clips (SB only) and the Roland / speaker drivers are not ported (they play on AdLib or stay silent). Optional: the WAVs `work/sound/asound_cmd03_steered.wav` (PLAYER intro, 90 s) and `asound_cmd36..39_steered.wav` (debrief) are the same music rendered with the corrected rule, as a reference. Reply with what differs. | S5.9 | 20 min |
| U-render3d | **The 3D view of the port next to the original (S5.6b).** (A) Fly: run the port (`f117port\build\f117 --game-dir Game`) and the original in DOSBox-X (U2, then `F117`), same mission, and compare the 3D window (the aircraft does not fly in the port until S5.7, so look from the runway, then use the external views F2..F4 / Shift-F1.., the TV camera of the right MFD (F6 options) and the left map MFD at every zoom `z`/`x`): terrain and scenery models, their colours and distance haze (Alt-A), the sky bands (Alt-D detail 0..3: two-colour split below 3), the draw order of overlapping models, your own F-117 in the external views (gear, bay doors), the smoke billboards, the TV picture, the map tiles. In the original, also tell me: (1) radar dishes / rotating parts up close: faces missing or flickering? (2) left map MFD while it scrolls: is the bottom pixel row of the map ever left unpainted? (3) TV camera: is there a mode with a flat two-colour sky/ground, which option? (4) burning targets: glow/flame sprites in the main view or only in the TV window? (5) any polygon cut off part-way down (a hole that shows the old frame)? (B) Optional extra check (the golden-frame test already compares the original under Unicorn byte for byte): DOSBox-X dumps of 5 scenes in flight, (a) day, low over the airfield, (b) night, low over scenery, (c) a target in the TV window, (d) a large polygon crossing the screen edge, (e) close to a rotating radar dish. For each: in flight press Alt+Pause, read DS in the debugger, SSSS = DS - 1E23h (VGAME load segment = PSP+10h); `BP SSSS:3758` (the call of r3d_scene_draw 0000:03CE in view_frame), F5, then `MEMDUMPBIN 0000:0000 B0000` -> `work\ref\dumps\<scene>_in.bin`; `BP SSSS:375B` (after the call), F5, the same dump -> `<scene>_out.bin` (U4). Reply with what looks different (and "dumped" if you did B). | S5.6b | 30 min (+20 min for B) |\r
| U-fly | **Fly a full mission in the port next to DOSBox-X (U6, S5.7).** Run the port (`f117port\build\f117 --game-dir Game`, or the launcher's Play) and the original (U2, then `F117`), same pilot and mission, same keys, no joystick (keypad as stick: arrows / keypad, Insert = sensitivity). Compare: (1) take-off: `+` full throttle, `0` brakes, rotation speed, `6` gear, climb; handling and roll / pitch rates, the stall (slow flight: nose drop, stall tone), speed and altitude tapes, the G text; `7` altitude hold; (2) `8` bay doors (lamp, the signature bars below the left MFD rise), Space weapon select, Enter release ("Bay closed" with the doors shut), bombs and guided weapons on a designated target (`b`), the hits and "destroyed by" messages, Backspace gun (only in the realistic model); (3) radars: "detected" / "Radar ID" messages, the threat ring and arrow on the map MFD, SAM launches ("<site> SAM launched"), the missile warning lamps and tone, `1` flare / `2` chaff / `5` decoy (store counts), `4` ECM and `3` IR jammer; (4) enemy fighters: take-offs ("taking off"), intercepts, their cannon and missiles, "Visual ID"; (5) damage: take a few hits and tell me the labels of the damage lamps that turn red and what fails (engine power, controls, nav, bay doors, fuel leak, jammers); (6) left map MFD with an enemy missile in flight: is its blip a 2x2 dot or a single pixel? (7) keys `4` / `3`: which lamps light, does the `3` lamp stay dim for a while after it switches itself off? (8) the tracer and explosion colours (`simflow.md` Parts 9, 10 `(U)` marks); (9) crash (fly into the ground with crash mode on), eject (Shift-F10: the tumbling aircraft, Esc), a landing with the gear down and a hard landing. The mission then goes to END, still a placeholder until S5.8. Reply with what looks or feels different. | S5.7 | 60 min |
| U-debrief | **The debrief of the port next to the original (S5.8).** Fly one mission in the port (`f117portuild117 --game-dir Game`) and the same one in DOSBox-X (U2, then `F117`: same pilot, mission and keys; the outcome will differ in detail, so aim for the same kind of ending), and compare END from the outcome scene to the roster: (1) the outcome scene: landing (the four-step landing animation and the blinking light), eject (rescue, or captured when you come down over hostile ground) or a crash (cemetery text by crash cause); try at least two. (2) The summary paragraph on the flag picture: wording, the rating, kills per kind, rules-of-engagement lines. (3) The map replay: the route line (drawn with colour index 0: is it visible in both?), the site dots appearing one by one (only Space skips them), one 48x40 event picture per stop (takeoff, landing, kill, missile hit, photo ...: same picture for the same event?), the event text box and the `TIME:` clock (starts at 06:00:00 / 18:00:00 at night plus an odd offset, the hour tens digit never advances) and the running `SCORE:`, the two buttons (hover colour, `Next`, `Done`). (4) The awards screen: promotion text with the rank board, the medal picture and citation, ribbons strip; a Purple Heart together with a promotion is stored but not shown. (5) The closing scene with a quip (desk / hangar / flight deck). (6) Back in START: the roster row of the pilot shows the new missions flown and career score. Reply with differences only (screenshots if you can); the logic is already proven byte for byte against the original END (`ctest debrief`), so look for look-and-feel (fonts, colours, timing, sound once S5.9 is done). `port/spec/debrief.md`. | S5.8 | 20 min |
| U-crash-speed | **Re-test the two bugs of 2026-10-16.** (1) Fly a mission with crashes on (Realistic), press Esc during the flight and crash into the sea (or anywhere): the debrief (END) must follow, then the roster; the game must never close by itself. If it ever does, send `f117.log` from the folder of `f117.exe`. (2) In the launcher, Options, **Game speed**: at the default 7 (10 frames a second), fly near a radar site and look at the dish: does it turn at a believable speed? Try 0 (the original pacing, about 17 frames a second: the fast spin you saw), 5 and 9: does the aircraft still feel right, and which value do you like best? | bugs | 15 min |
| U-keys | **Key bindings of the launcher (S6.1).** Open the launcher (`f117port\build\launcher\F-117A Nighthawk.exe`, or wherever the build put it): (1) Key Bindings... opens the dialog with four tabs (Flying, Weapons and countermeasures, Views, Displays and game); (2) click the Landing gear button and press G, then Reset to defaults: it goes back to 6 and the bold type goes; (3) rebind gear to G again, OK, reopen the dialog: G is still there (it is in `settings.ini` under `[Keys]`); (4) Play, fly a mission and confirm G lowers the gear and 6 does nothing; try one with Shift (Full throttle on J). Menus, the quit prompt and the keypad stick are unchanged. | you | 10 min |

## Status

- **Done:** S0.1 (repo), Phase 1 S1.1-S1.5 (survey, overlay chain, indexer, Ghidra, shared code), Phase 2 S2.1-S2.6, S3.1 (platform spec), S3.2 (video spec), S3.3 (sound spec), S3.4 (front-end spec), S3.5 (shell spec), S3.6 (sim flow spec), S3.7 (render3d spec), S3.8 (flight/weapons/AI/detection/damage spec), S3.9 (debrief spec), S3.10 (hand-offs, coverage gate, symbols merge; closing pass: coverage 100 %, all conflicts settled), S4.1 (`f117port/` skeleton: CMake, SDL3 host, mem, VGA, OPL, chain loop with placeholder programs), S4.2 (generated symbol and module headers in `f117port/src/gen/`, symbol pipeline in `PORTING.md`), S5.1 (platform layer in `f117port/src/platform`: loader, RTL, DOS files, keyboard/joystick/mouse, timer hook), S5.2 (video: MGRAPHIC driver, program-side line/palette routines, PIC/PAN/PAL readers in `f117port/src/video`), S5.3 (front end in `f117port/src/frontend`: SETUP, MPS_LOGO with its .SS sprites, PLAYER with the PAN ring buffer and fade, DSWAP, the MISC slots, the text-mode screen; the first visible milestone), S5.4 (shell in `f117port/src/shell`: START with the roster, ready room, pilot orders, briefing map and folder, arming, hangar, the mission generator and the world hand-off to VGAME), S5.5 (sim flow in `f117port/src/sim`: VGAME start-up, frame loop, view modes, HUD, ADI, MFDs, key interpreter, step, overlay pass), S5.6 (render3d in `f117port/src/render3d`: the whole 3D renderer, golden frames byte for byte against the original), S5.7 (flight, weapons, AI, detection, damage in `f117port/src/flight`: the aircraft flies), S5.8 (debrief in `f117port/src/debrief`: END with the score, promotion, medals, map replay and ending scenes, byte for byte against the original)., S3.11 (sound poll-rate re-check: sound.md section 3), S5.9 (sound in `f117port/src/sound`: the AdLib driver and the logo overlay, bit-exact against the original drivers).
- **Next:** S6.1 (release: README, CHANGELOG, v0.1.0 zip). Your tasks: **User to-do** above.
- Escalation rule: a subtask that fails twice on Sonnet → new Opus session with a 5-line note
  (symptom, file, what was tried). Never carry an old transcript over.

## Model guide

| Model (app model picker) | Effort | Use for | Relative cost |
|---|---|---|---|
| **Haiku 4.5** | low–medium | Running and adapting existing tools, regenerating outputs, symbol merges, copying files, coverage checks, packaging, FORMATS/PLAN bookkeeping | 1× |
| **Sonnet 5.5** | medium | Default: indexers, decoders for simple/known formats, specs and ports of shell/UI/RTL/driver code, most C porting | ~3× |
| **Opus 5.5** | high | Unknown protocols and formats (overlay chain, 3D objects/world), the 3D renderer, flight model, AI, cross-module bugs, escalations | ~5× |

## How to run a subtask (user, every time)

1. Code tab → **New session** in `C:\Coding\F117-2` (never continue an old session for a new subtask:
   the old context is re-billed on every turn).
2. Pick the model from the table below in the model picker; set the effort.
3. Send exactly: `Do S1.1 from PLAN.md.` (with the subtask id). `CLAUDE.md` tells the agent what to read.
4. When the agent says it is done: glance at the commit (`git log -1 --stat`), do any U-check it asks
   for, then close the session.

## What we have (recon, 2026-10-02)

`Game/` is a junction to `F-117ANi/` (eXoDOS install, git-ignored). `_tools/` is a junction to
`../TestDrive1987/_tools` (Ghidra 12.1.3, JDK 21). Root `NC.WLD` equals `orig/NC.WLD` (unhacked);
`hack/NC.WLD` unlocks the three secret missions (eXoDOS `run.bat` copies one or the other).

| File(s) | Size | Notes |
|---|---|---|
| `F117.COM` | 1 722 | Chain loader. Loads `SETUP MPS_LOGO PLAYER START VGAME END DSWAP MISC` as overlays (INT 21h 4B03, "Overlay load failed"), allocates 1 MB(?) of memory; strings `title-1 title-2 title-3a credit c-pit bunker` (the `.PAN` files) |
| `START.EXE` | 47 100 | Shell? (roster, theatre, briefing, arming) — to confirm |
| `VGAME.EXE` | 95 634 | The simulator (VGA build) |
| `END.EXE` | 24 203 | Debrief / awards / ending? |
| `MPS_LOGO.EXE`, `PLAYER.EXE`, `SETUP.EXE`, `DSWAP.EXE`, `MISC.EXE` | 0.7–11 K | Logo, ?, config, disk swap, ? |
| `MGRAPHIC.EXE` | 11 950 | MCGA/VGA graphics driver (only video driver shipped) |
| `ASOUND/RSOUND/ISOUND/NSOUND.117` + `*.LOG` | 0.6–15 K | Sound drivers (AdLib / Roland / IBM-Tandy? / none-speaker?) + music data |
| `SPEECH.117` | 26 408 | Speech / digitised sound? |
| `*.PIC`, `*.PAL`, `PALETTES.PAL` | | MicroProse PIC images (LZW, publicly documented) and palettes |
| `*.PAN` | 12–114 K | Animations (title, credits, cockpit, bunker) |
| `LABSLOGO.SS` | 12 348 | Logo animation / sprite sheet? |
| `{CE,CU,KO,KU,LB,ME,NC,PG,VN}.{3DG,3DT,3DX,WLD}` | | Nine theatres: 3D objects, ground/terrain, world layout |
| `STFLT.3DX`, `PHOTO.3DX` | 45–48 K | Shared 3D objects (stealth fighter? recon photo?) |
| `ROSTER.FIL` | 802 | Pilot roster |
| `AIR_ID.PIC`, `PLANES1-5.PIC` | | Aircraft identification quiz = copy protection |

Total code ≈ 230 KB (Aces: 900 KB), so the whole port should be about a quarter of the Aces effort.

## Subtasks

Model: H = Haiku 4.5, S = Sonnet 5.5, O = Opus 5.5. Size: rough session length (S/M/L).

### Phase 0 — Repository
| Id | Task | Model | Status |
|---|---|---|---|
| S0.1 | Repo, junctions, tools copied from Aces, PLAN/CLAUDE/README | O | done 2026-10-02 |

### Phase 1 — Executable map (output: `port/RE_GUIDE.md`, `port/*_functions.csv`, `port/symbols.csv`)
| Id | Task | Model | Size |
|---|---|---|---|
| S1.1 | `tools/survey.py`: MZ headers, relocs, packer signatures (EXEPACK/LZEXE/PKLITE), compiler RTL strings, INT/port usage counts for every EXE/COM/.117 → `work/survey.txt`; unpack if packed; 20-line summary into `RE_GUIDE.md` §1 | H | done 2026-10-02 |
| S1.2 | Overlay chain and driver protocol: how `F117.COM` loads/chains programs, shared memory area, exit/next-program codes, `MGRAPHIC.EXE` and `*.117` entry tables (all small files) → `RE_GUIDE.md` §2 | O | done 2026-10-02 |
| S1.3 | Indexer `tools/f117index.py` from `tools/apindex.py`/`td3index.py` (compiler found in S1.1): segments, functions, DGROUP, strings, ints, ports → `port/<exe>_functions.csv` | S | done 2026-10-02 |
| S1.4 | Ghidra: adapt `tools/ghidra/aotp_*.sh` → `f117_*.sh`, run headless in the background, decompiles to `port/decomp/` (ignored). Print only counts | H | done 2026-10-02 |
| S1.5 | Shared code: compiler RTL + MicroProse library matched across the EXEs (`tools/apmatch.py`), first names → `port/symbols.csv` (`tools/merge_symbols.py`) | S | done 2026-10-02 |

### Phase 2 — File formats (output: `FORMATS.md`, decoders in `tools/`, PNG/WAV in `work/`)
| Id | Task | Model | Size |
|---|---|---|---|
| S2.1 | `.PIC`/`.PAL`/`PALETTES.PAL` → PNG + one contact sheet; **U5 check** | S | done 2026-10-02 |
| S2.2 | `.PAN` animations and `LABSLOGO.SS` → PNG frames + contact sheet; **U5 check** against U3 captures | S | done 2026-10-03 |
| S2.3 | 3D: `.3DX` objects (+ `STFLT`, `PHOTO`), `.3DG`/`.3DT`, `.WLD` layout → OBJ files + top-down theatre map PNGs; **U5 check** | O | done 2026-10-02 (colours → S2.6) |
| S2.4 | Sound: `.LOG` music format and driver API (from S1.2); AdLib render via Nuked-OPL3 (copy `../AcesOfThePacificEnhanced/third_party/nuked-opl3`) → `work/sound/*.wav`; `SPEECH.117`; **U5 listen** | S | done 2026-10-03 |
| S2.5 | `ROSTER.FIL`, files written by SETUP/START (config, saves) → `FORMATS.md` | H | done 2026-10-02 |
| S2.6 | Flight palette and ground backdrop: how VGAME/START compose the in-flight palette (VGAME never writes the DAC; colour ramps near VGAME image 1FD9Ah; time of day/theatre?) and which colour the ground backdrop under the AND 7Fh/6Fh / OR 80h polygons gets; then fix `tools/f3d.py` (`palette`, `colour_map`) and the parked cockpit/ORDNANCE/REQUESTR PICs; **U5 check** sheets + cockpit vs U3 in-flight captures | O | done 2026-10-03 |

Notes:
- S2.1 status: DONE. PIC/PAL decoded; palettes confirmed in U5. Parked for the 3D/flight tasks: 256LEFT/PIT/REAR/RIGHT, ORDNANCE, REQUESTR (runtime-composed palettes; see FORMATS.md).
- S2.2 status: decoders done (`tools/lzs.py`, `panlib.py`, `pan2png.py`, `ss2png.py`; output `work/pan/`); all 6 PAN cels and 81 LABSLOGO sprites decode to exact record boundaries. U5 OK 2026-10-03: `work/pan/*_sheet.png` vs U3 captures 02_title1, 03_title2, 04_title3a, 05_credits, 01_mps_logo (+ cockpit/bunker). Formats in `port/RE_GUIDE.md` §4.
- S2.3 status: DONE (geometry; maps confirmed in U5). `python tools/f3d.py all` → `work/3d/`: 989 OBJ (`obj/<BANK>/oNNN.obj`, LOD 0, + `.mtl`), `sheet_<BANK>.png`, `map_<TH>.png` + `maps_sheet.png` (north up). Formats in `FORMATS.md` (3D objects / Terrain / World layout); all 11 `.3DX` parse with 100 % byte coverage. Colours are wrong (`ADV.PAL` guess, legend colours for ground ops) → S2.6.
- S2.6 status: DONE (U5 OK 2026-10-03). VGAME does write the DAC (14A2:000E): three flight palettes A/B/C in DGROUP (B/C = night, picked by comm[33h]; A = debug), desert ramp patch per theatre, ground backdrop = bands 70h-7Fh; FORMATS.md "Flight palette". `tools/f3d.py` (`palette(th, which)`, `colour_map` on backdrop 7Ch; `pal=B`/`pal=C` arg, default A) and `tools/pic2png.py` (cockpit/ORDNANCE → flight B, + `_A`/`_C` files; REQUESTR → PALETTES[1]) regenerated; all confirmed in U5.
- S2.4: done. `.LOG` = logo-jingle overlay; driver API, speech, poll-rate rule in RE_GUIDE §5. WAVs in `work/sound/` (U5 listened: logo, 36-39, speech OK; cmd 3 = PLAYER intro cue sequence rendered at a fixed 120 Hz poll, user-chosen).
- S3.3 status: DONE (U-sound pending). `port/spec/sound.md` + `sound_symbols.csv` (~120 rows, hand-written). The AdLib sequencer of `ASOUND.117` (state 0) and of `ASOUND.LOG` is fully decoded and re-implemented in `tools/sound/asnd.py` (`Game`, `Logo`): 40/40 commands and the logo jingle are write-for-write identical to the real drivers under Unicorn (`asnd.py verify|verifylog`). Corrections to RE_GUIDE §5: slot 104 is `snd_noise` (not stop); the in-game poll rate is the ISR rule (1 poll per video frame = 70 Hz idle, 4x while effect units run), not 60 Hz. Not specified: ISOUND/RSOUND/NSOUND, SB/PAS states, speech via SB. `python tools/sound/asnd.py steer CMD` renders the steered wavs.
- S3.6 status: DONE (U-vgame pending). `port/spec/simflow.md` (Part 0 map/lifecycle/decisions, Parts 1-10 = twelve reader passes over all 376 VGAME functions not named by platform, Part 11 index, Part 12 render3d interface) + `simflow_symbols.csv` (376 funcs, 159 labels, 530 data symbols). New tools: `tools/vgxref.py` (DS-offset and call xrefs), `dsann.py` now resolves call names/driver slots/far segments, `fndis.py` takes `SSSS:OOOO`, `spec_coverage.py vgame|start|...` works for the F-117 programs (vgame 450/450). Findings: `1e9e` is input AND the whole player flight model (differentially tested Python reference `port/notes/s36/r2_*.py`; render3d passes R11a/R11b for S3.7 also in `port/notes/s36/`); `cba0` is the key command interpreter (no menu); `a17e` is the 3D overlay pass (tracers, target boxes); `39c4` = one sim step per frame, mission end only via `4926(code)`; the mission world arrives in `comm+7Ah` (no TEMP.WLD read) and is written back with a 0x600-byte event log; VGAME never writes the pilot record; exit code 81h = END. Open: timing base (U7 item 7: 60 vs 70 Hz estimator; sound poll skips every 7th tick, so re-check S3.3 `sound.md` section 3), (U) marks in the parts, original bugs listed in Part 0 section 4.
- S3.7 status: DONE (U-render3d pending). `port/spec/render3d.md` (sections 1-9: part map, pipeline, draw order/sorting, conventions, ownership decisions, settled S3.6 items, quirks, open items, tests; Parts 1-6 = six reader passes R7d/R7f/R7e/R7a/R7b/R7c, Part 7 index) + `render3d_symbols.csv` (146 funcs, 76 labels, 237 data; moved out of `simflow_symbols.csv`, which now points there; coverage vgame 450/450 = platform 74 + render3d 146 + simflow 230). Almost every arithmetic routine differential-tested under Unicorn, 0 mismatches; Python references + harnesses in `port/notes/s37/` (oracle for Phase 5 unit tests). Settled: 32-bit clip path 12EF:0333 bit-exact, near-ret 12EF:030F unreachable, part limit 7 (data max 2), shade byte dead, 1433 math has no flight callers (now render3d), object polygons use a private scan converter (12EF/1359), queue = 50 records farthest-first. No other program has 3D code (START ID quiz is PIC-based and dead). Open: section 7 (U-render3d).
- S3.9 status: DONE (U-debrief pending). `port/spec/debrief.md` (sections 0-12 + appendix A texts/tables) + `debrief_symbols.csv` (96 funcs + 115 data; `spec_coverage.py end` 196/196 = platform 100 + debrief 96). Findings: END dispatches on `comm+26h` (1 crash, 2 eject -> rescue or captured by the grid class of the final position, 3 landed), `comm+28h` = crash text and "pilot is dead" (settles simflow open item 1, pointer added there); the whole scoring (`3021`: per-event values by war level, 6 final multipliers, objective bookkeeping), promotion (`2AFA`: score / missions / average tables, one rank per mission), medals (`2476`: Purple Heart, AM/DFC/SS/AFC from the last score in steps of 100/300/600/900 per earned count, CMH > 1200) and ribbons (missions 5/10/30/60, Combat Readiness for training) live in END; END writes only the pilot record (`+20 +22..+2C +2E +30 +32 +36 +44..+4C +4E`), no file, exit 23h; about 40 toolkit functions are START copies (mapped in section 10). Differential tests `port/notes/s39/` (Python reference + Unicorn harness): 6 x 1000 numeric and 14 x 300 screen cases, 0 mismatches. Open: U-debrief; unit flag 4000h and object-class bits (U).
- S3.8 status: DONE (U-flight pending). `port/spec/flight.md` (sections 0-12: conventions + the emulated `sim_rate` scaling table, player flight model/controls, detection/stealth signature/alert levels, enemy AI, weapons/countermeasures/guns, damage/crash/eject/scenery damage, step order, corrections to simflow/render3d, tests, quirks) + `flight_symbols.csv` (42 funcs, 2 labels, 90 data; moved out of `simflow_symbols.csv`; coverage vgame 450/450 = flight 42 + platform 74 + render3d 146 + simflow 188). New full specs: `5730` AI (modes, combat table, cannon, landing/roll-out, scramble), `0D08`/`0F31` scenery lookup/damage. Differential tests `port/notes/s38/` (Python reference + Unicorn harness): 5460 43ee 516d 62d0 65d4 5730 4512 461c 465b 711f 74f6 671a 753f, 16 scenarios x 1000 cases, 0 mismatches; 1e9e re-run 600/600. Findings: `[9B04]` 0x10/0x20 are the ECM / IR jammers (not chaff/flare modes); `461c` ticks only the first busy countermeasure; gear has no radar effect in the MicroProse model; unit +1Ah is speed; no AAA exists. Open: section 11 (U-flight).
- S3.5 status: DONE. `port/spec/shell.md` (4 reader parts + 20D6) + `shell_symbols.csv` (321 rows, merged from `work/s35/syms_*.csv`). Corrects RE_GUIDE 2.4: comm = DS:E096, pilot = DS:CACA, DS:CBD8 -> 04F2. ID quiz is dead code (call NOPed). Mission generator = `0000:4239`. Open (U in spec): site flags 0x40/0x80, comm+22h/30h meaning, 82 mission templates at DS:11DA not tabulated, unit placement P9 (4C3D-4E0B) to port from disasm; original bugs listed in part D/A (brief_draw_hostile loop counters, double alloc, volume-label check -> always allow roster save).
- S3.4 status: DONE (U-player folded into U-front, S5.3). `port/spec/frontend.md` + `frontend_symbols.csv` (56 rows, `python tools/frontend_syms.py`); `tools/dsann.py` = fndis with DGROUP string annotations. SETUP screen/flow and comm fields, MPS_LOGO (SS loader palette base 31, delays in 18.2 Hz ticks, 146 ticks = 8 s, key completes the animation instantly), PLAYER (tick callback, 5 s fade-in, snd_cmd 3/0/2, exit on any input), DSWAP (port = return 1), MISC (6 entries) all verified. Open: PLAYER tick rate 60 vs 70 Hz, fade look (U-player); SETUP quirk comm+30h always 0.
- S3.2 status: DONE (fonts confirmed by user 2026-10-06). `port/spec/video.md` + `video_symbols.csv` (94 rows, `python tools/video_syms.py`); `tools/mgfont.py` decodes the 7 fonts embedded in MGRAPHIC. All 84 driver slots read; text/sprite/poly/line algorithms specified; program-side PIC decoder mapped (START 86A8-8A31 = END 47FF.. = DSWAP 0AAB.. = VGAME 11CE). Open: shake unit, DAC-13 flash (runtime look).
- S3.1 status: DONE. `port/spec/platform.md` + `platform_symbols.csv` (517 rows over all 7 programs, `python tools/platform_syms.py`; `tools/fndis.py` = per-function disassembly). Library = MSC 5/6 small-model RTL + MicroProse layer (fatal-on-error DOS wrappers, joystick/mouse, retrace-calibrated PIT timer, Ctrl-Break hook); END holds it all at `0000:3E45..5F63` (read in full). Open: sound cadence 60 Hz vs calibrated 70 Hz class 4 (spec §4/§9). Hand-offs: 59 shared rows unnamed in `work/s31/unassigned.txt` (video/shell); `tools/spec_coverage.py` still has the Aces layout -> S3.10.
- S3.10 status: DONE (127 functions left). `port/spec/HANDOFFS.md` (sections 1-9: chain and exit codes, comm block, files, world blob, VGAME to END, END to START; CONFLICT C1-C10, GAP G1-G3). `tools/twin_map.py --apply`: 37 twin rows (frontend 30, shell 7); `spec_coverage.py` 1220/1347 (was 1183). `tools/merge_symbols.py` writes `port/symbols.csv` (2544 rows, new `spec` column) and `work/coverage/symbol_clashes.txt` (25 clash lines, 134 duplicate keys).
- S3.10 closing pass (2026-10-13, Sonnet): coverage gate **1347/1347** on all seven programs. The 127 left functions: 30 specified in full, 36 mapped to an existing function (RTL / library role), 61 marked `dead` (index-level rows; `tools/spec_coverage.py` now prints the dead count per program). Sections: `frontend.md` 8 (SETUP 38, MPS_LOGO 41, PLAYER 35, DSWAP 7) and `shell.md` Part E (START 6). Dead code: SETUP's graphics/string library (33), PLAYER's debug overlay and signal() (10), MPS_LOGO's mouse layer and file copy (15), DSWAP `031F`/`10D8` (+ the unindexed `030A`), START `8BD2`. Of the 11 tiny S3.10 twin rows 4 were right and 7 were fixed (`nmalloc` x4, `atoi`, `pic_hook_nop` x2); two larger DSWAP rows corrected (`pic_to_seg_c`, `load_pic_seg_c`). **All conflicts C1-C10 and gaps G1-G3 settled from the code** (HANDOFFS sections 7-8; wrong text corrected in RE_GUIDE 2.4, simflow, debrief, shell, frontend): `comm+26h` = mission state (1 stays on any crash path), `4926(0)` = landed or ejected pilot on the ground (not "killed"), `comm+30h` = non-scoring flag (START overwrites SETUP's 0), VGAME never writes the pilot record, START never writes TEMP.WLD, DSWAP returns 0 only after a fatal memory error, pilot `+3E` is 0..3, `comm+2Eh` = estimated fuel lbs, `comm+2Ch` = enemy missile launches (not kills), `comm+22h` = clearance (always 1), `comm+70h` unused, no preferences file. **A quit in END discards that mission's pilot changes** (only START writes ROSTER.FIL): original behaviour, the port keeps it. Also found: PLAYER's animation tick is 60.08/s on a 70 Hz display (every 7th tick dropped before `tick_callback`), which settles U-player question 1. Symbols: the 12 DS-address name clashes resolved (render3d names win; START duplicates removed), `port/symbols.csv` regenerated (2672 rows). Nothing left for the user from this pass.

### Phase 3 — Specs (output: `port/spec/<module>.md`, gate: `tools/spec_coverage.py` 100 %)
Module split is confirmed in S1.5; one session per module. Specs are written so that a Sonnet session
can port a function from its section alone.
| Id | Module | Model | Size |
|---|---|---|---|
| S3.1 | platform: RTL, startup, memory, timer, keyboard, joystick, files, overlay chain | S | done 2026-10-03 |
| S3.2 | video: `MGRAPHIC` driver API, blitters, PIC/PAN decoding, fonts | S | done 2026-10-03 |
| S3.3 | sound: driver API, music sequencer, AdLib device | S | done 2026-10-03 (U-sound pending) |
| S3.4 | front end: `MPS_LOGO`, `SETUP`, `PLAYER`, `DSWAP`, `MISC`, title/credits | S | done 2026-10-03 · U-player folded into U-front (S5.3) |
| S3.5 | shell (`START`): roster, theatres, briefing, arming, ID quiz | S | done 2026-10-03 |
| S3.6 | sim flow (`VGAME`): main loop, mission state, HUD, cockpit, MFDs, view modes | S | done 2026-10-13 · U-vgame pending |
| S3.7 | render3d: transform, clipping, polygons, terrain, sky, sorting | O | done 2026-10-13 · U-render3d pending |
| S3.8 | flight model, controls, weapons, AI, detection (stealth), damage | O | done 2026-10-13 · U-flight pending |
| S3.9 | debrief (`END`): scoring, medals, promotion, death/rescue screens | S | done 2026-10-13 · U-debrief pending |
| S3.10 | `HANDOFFS.md`: data passed between programs; coverage gate; symbols merge | H | done 2026-10-13 · coverage 100% (closing pass) |
| S3.11 | sound re-check: `sound.md` section 3 assumes `snd_poll` at the ISR rate, but VGAME (simflow.md Part 0 D1, Part 1 section 1) and PLAYER (frontend.md 3.2, closing pass) drop every 7th tick (60.08 Hz at 70 Hz); correct the poll-rate rule per program, re-render `work/sound/*_steered.wav`, update U-sound. Needed before S5.9 | S | done 2026-10-16 |

### Phase 4 — `f117port/` skeleton
| Id | Task | Model | Size |
|---|---|---|---|
| S4.1 | Copy `../AcesOfThePacific/aotpport` skeleton (CMake, SDL3 host, mem, VGA, OPL); drop EMS/VROOMM; chain loop = `F117.COM`; placeholder programs run the whole chain headless | S | M | done 2026-10-13 · U-skeleton OK 2026-10-14 |
| S4.2 | Generated `symbols_*.h`, module headers (`tools/gen_symbols.py`, `gen_module_headers.py`), `PORTING.md` | H | S | done 2026-10-13 |

Notes:
- S4.1 status (2026-10-13): `f117port/` builds with no warnings (`-Wall -Wextra`) and `ctest` passes 3/3 (`chain`, `tick_rate`,
  `check_missing_files`). Copied from `aotpport` and trimmed: `host.c/h` (SDL3 window, XT keys, mouse, gamepad, OPL2 via Nuked-OPL3
  from `third_party/`), `platform/vga.c/h`, `mem.c/h` (arena, DS accessors, far pointers; no EXE loader, EMS, VROOMM, layout or Dynamix
  divide helpers). Written new: `prog.c` (F117.COM chain loop, HANDOFFS section 1), `comm.h` (5120-byte comm block, packed struct with static
  offset asserts; `+32h` word and `+33h` night byte overlap in a union as in the original), `platform/timer.c` (70.09 Hz tick source and
  `timer_set_hook()` for the S5.1/S5.9 ISR body), `programs/placeholder.c` (one stand-in per program writing plausible comm fields and exit
  codes; START passes: 0Ch then 0Dh then 0, so START-VGAME-END-START runs twice), `main.c`. Options: `--game-dir --scale --aspect
  --fullscreen --headless --trace --check --chain-test --tick-test`. Two changes against Aces worth knowing: the tick and the 3DAh model share one
  exact rational period (`VGA_FRAME_NS_NUM/DEN`, avoids the 64-bit overflow after 12 minutes that `t * 25175000` would have), and the sound poll
  is NOT on the audio thread (S3.3: it is part of the timer ISR, so it runs from the tick on the main thread). Not done here (S5.1): unpacking and
  loading the program images into `mem[]`, DGROUP set-up, the keyboard ISR, the timer ISR body.
- S4.2 status (2026-10-13): `f117port/src/gen/` holds 11 generated `symbols_<image>.h` (2672 CSV rows: 7 programs plus the
  driver images asound117, mgraphic, misc, asound_log; 122 rows marked dead; 0 rows skipped; 118 identifiers renamed, mostly
  libc names `_fn`, plus the 7 names used at several addresses) and 9 `module_<spec>.h` (964 prototypes, of which 554 are
  TODO signatures: the type column is not plain C; 53 names that several specs document are declared once). `comm.h` includes
  `symbols_start.h` and asserts `START_DS_ptr_comm == 0xE096`. CMake: `src/gen` on the include path; ctest 5/5 (new:
  `gen_symbols_current`, `gen_module_headers_current`, which fail on a stale header). Build: no warnings. `PORTING.md` has the
  "Symbol pipeline" section. Known gaps: the spec section is found for about 80% of functions (asound117 only 15 of 89;
  its spec names them by offset in other text); the TODO signatures are the S5 owners' work, fixed in the spec CSVs.

### Phase 5 — Port, module by module (one session per module or half-module)
Order: S5.1 platform → S5.2 video → S5.3 front end/title (**U4 first visible milestone**) → S5.4 shell
→ S5.5 sim flow with a static 3D view → S5.6 render3d → S5.7 flight/weapons/AI → S5.8 debrief →
S5.9 sound. Model: S for 5.1–5.5, 5.8, 5.9; O for 5.6, 5.7. Each ends with a U6 playtest request.

| Id | Task | Model | Size |
|---|---|---|---|
| S5.1 | Platform layer: RTL pieces, start-up/exit and program loader, memory model, timer hook, keyboard, joystick, mouse, DOS files, overlay chain wiring | S | L | done 2026-10-14 |
| S5.2 | Video: MGRAPHIC drivers (slots 0-83), palette, picture decoders, text and sprites | S | L | done 2026-10-14 · U-video OK 2026-10-14 |
| S5.3 | Front end / title screens (SETUP, MPS_LOGO, PLAYER) - **U4 first visible milestone** | S | L | done 2026-10-14 · U-front pending |
| S5.4 | Shell (START: menus, roster, mission planning; END moved to S5.8, DSWAP was done in S5.3) | S | L | done 2026-10-14 · U-shell pending |
| S5.5 | Sim flow with a static 3D view (VGAME frame loop, HUD, MFDs) | S | L | done 2026-10-14 · U-sim pending |
| S5.6a | Render3d leaf routines (clipper, raster, edge tables, sky, 1433 math) from the Python references | S | M | done 2026-10-15 |
| S5.6b | Render3d top half (scene set-up, terrain, sorted queue, object renderer, world pass) + golden frames | O | L | done 2026-10-15 · U-render3d pending |
| S5.7 | Flight, weapons, AI, detection, damage | O | L | done 2026-10-15 · U-fly pending |
| S5.8 | Debrief | S | M | done 2026-10-16 · U-debrief pending |
| S5.9 | Sound (AdLib driver, sound poll in the tick) | S | M | done 2026-10-16 · U-sound pending |

Notes:
- S5.1 status (2026-10-14): `f117port/src/platform` has the platform layer; `ctest` 7/7 (new: `platform`, 326 checks; `exe_load`, 31
  checks on the real programs, skipped with code 77 when `Game/` is absent), no warnings under `-Wall -Wextra`. 85 of the 119
  platform-owner prototypes are ported (the rest is libc, see PORTING.md "Platform layer"). **What it does:** `platform_exec()` is
  the INT 21h/4Bh of F117.COM (wired into `prog_run`): unpacks the LZEXE image from the game folder, relocates it into `mem[]` at
  PSP+10h, sets `cur_dgroup`, the stack/near-heap model and the per-program offsets of the library state (rand seed, break flag,
  joystick table, mouse words, decoder buffer: `ProgDs` in `startup.c`), runs `<name>_main`, and `exit_fn(n)` from any depth ends the
  program (longjmp); at exit the program's DOS blocks and handles are freed, timer hook and INT 9 hook removed. Without game files
  (the placeholder chain test) it runs without an image. **Bit-exact** against the original code under Unicorn
  (`port/notes/s51/platform_emu.py` -> `selftest_vectors.h`): rand/srand/rand_n (6 seeds), dist_approx (51 cases), `rtl_aFldiv/aFlmul/aFlshl/aFlshr/aFulshr`,
  `joy_scale` incl. the learning of min/max/slope. Loader check: the xor-sum of DS:0..41h is 55h for all seven programs.
  **Findings:** (1) DGROUP is NOT the EXE header's SS (that is a separate stack segment after the image); it is the `mov di,imm16`
  at the entry point (END 5FA, SETUP 24C, MPS_LOGO 332, PLAYER 2A3, START A95, VGAME 1E23, DSWAP 225). (2) VGAME's rand seed is DS:926E
  (platform.md quoted END's 5158); the other seeds: SETUP 0EB4, START AE8C. (3) `rand_n` zero-extends its argument (negative n = 65536+n).
  (4) `file_load_paras` chunk bug: only the THIRD and later 0FFFFh chunks are misplaced (chunk 2 at seg:FFFF happens to be contiguous);
  the test keeps it. (5) The break-flag byte is 1 in END/START and FFh in SETUP/DSWAP/VGAME. **Hand-offs:** keypad_stick_*
  (flight, S5.7) use `kbd_set_int9_hook()`; MISC slots 90/91/93 (frontend) are `kbd_key_waiting/kbd_key_get/joy_port_201`;
  `file_load_paras` (simflow) calls `platform_load_file_paras`; per-program ISR bodies (frame counter, 7th-tick skip, `[3660]`
  estimator, sound poll) go in via `timer_set_hook(); timer_install();` and call their sub-ticks themselves (class 4 = 4 per retrace);
  `fcb_find` is shell (S5.4); `gfx_line`/`pal_*` are video (S5.2). Deviations marked `PORT:` in the code: joystick counts are
  synthesised from the gamepad (500..1500, no gamepad = FFFFh timeout), `joy_scale` skips the divide by an uncalibrated slope, files
  open read-only where the original used "r+b". No in-window check yet: keys and gamepad can first be seen in S5.3 (U4).
- S5.2 status (2026-10-14): `f117port/src/video` has the video module; `ctest` 10/10 (new: `video_selftest`, `video_decoders`, `video_screen`), no warnings
  under `-Wall -Wextra`. **Ported:** all 84 MGRAPHIC slots (text in the 7 fonts read from MGRAPHIC.EXE, blits, line, span fills, tick strip,
  RLE sprite pool + scaled/mirrored sprites, flip and flag, effects, screen off/on; slot 21 is dead, it is the draw half of 22), the
  programs' own `gfx_line`, `gfx_fill_rect`, `pal_write_vsync`, `pal_set`, `pal_set_default`, `pal_apply_queue` (START/END/DSWAP tables in
  `vprog[]`), the .PIC decoder with its layers and four front ends, `.PAL` reader, `.PAN` LZ decoder + cel player. **Left:** VGAME's own
  `pal_read_dac`/flight palettes (simflow S5.5), `pal_fade*`/`pal_queue_push` (shell/debrief), MPS_LOGO's .SS sprites and PLAYER's streaming
  ring buffer (frontend S5.3), `pic_hook_nop`. **Tests:** (1) `video_selftest`: 532 scenarios whose hashes come from running the ORIGINAL MGRAPHIC.EXE
  (and START's `gfx_line`/`pal_write_vsync`) under Unicorn (`port/notes/s52/video_emu.py` -> `src/video/video_vectors.h`): lines, polygon
  modes, rect copy/recolour, blits (plain and clipped), tick strips, 160 text scenarios over all fonts/variants/clip windows, 24 sprite
  scenarios (grab, scaled/mirrored/flipped/clipped draw, pool exhaustion), flip/misc/constants, explosion FX, 48 program-side lines, 14 palette
  writes; all match bit for bit; plus hand checks of `gfx_fill_rect`, `pal_set_default`, `pal_apply_queue`. (2) `video_decoders`
  (`port/notes/s52/decoder_ref.py`): 52 PIC (also through the four front ends), 13 PAL, 6 PAN (1666 frames, CRC32 per frame), 7 fonts (800
  glyphs) equal the Phase 2 Python decoders. (3) `video_screen`: `f117 --video-test` composes a screen and writes PNGs to `work/s52/screen`.
  **Spec corrections** (video.md section 14): text writes the last colour back to `ctx->fg`, sets the driver colour, clips vertically on bytes
  with a too-tall bottom clip; the tick strip draws a pixel on every row; `line` compares x unsigned, `poly_fill` is a do-while; `flip` takes AX,
  its flag starts at 1; `page_es` returns ES; the C slots need DS=SS; program-side `gfx_line` is a different (signed, per-pixel clipped) routine; the
  grey-scale DAC path of `pal_write_vsync` has an index-255 carry bug and an unbalanced stack. **Open:** shake unit and the DAC-13 flash look
  (`explosion_fx_tick`, U-video cannot show it; needs VGAME); `pal_write_vsync` waits for one retrace per call, the original spreads long writes
  over several frames (the fade calibration adapts). Wired: `video_init()` in `main`, `gfx_alloc(2)` in `prog.c` (comm+20h).
- S5.3 status (2026-10-14): `f117port/src/frontend` has the front end; `ctest` 12/12 (new: `frontend`, `frontend_chain`; 2239 checks), no warnings under `-Wall -Wextra`.
  **Ported:** SETUP (banner, joystick question with the calibration steps, sound menu, comm block, exit 24h; strings and layout read from the loaded image's
  DGROUP), MPS_LOGO (`.SS` loader with the palette merge into DAC 31..86 and the pixel remap, `ss_draw` RLE, BIOS-clock pacing, key = finish the picture, exit
  0/1/2/3), PLAYER (timer ISR body at 60.08 Hz, the streaming ring buffer with `pan_decode_chunk` from the 4 KB file buffer, `cel_show`, `pal_load`, the fade, `poll_input`,
  `player_exit`), DSWAP (exit 1), the MISC slots 90-95, the text mode model (`textmode.c`) and its window rendering (`platform/vgatext.c`: 640x400, 8x16 cells,
  `vga.c` switches the frame size with the mode). `pan_rle_draw(src, pos, end, scr)` was added to `video/pan.c` (the ring buffer is in mem[]). **Stubbed:** the sound
  call points (`sndstub.c`, marked `TODO(S5.9)`: PLAYER cue 3 / 0 / 2 / poll, the logo's `.LOG` calls); not ported: the overlay loader, `fade_measure` (256 colours per
  tick), the floppy swap of DSWAP, `ss_remap_rows_b` (mode byte 1, no shipped series). START, VGAME, END stay placeholders (`--chain-test` still runs stand-ins of
  the front end, `*_stub`, because it needs no game files). **Tests:** (1) the ORIGINAL SETUP and MPS_LOGO under Unicorn (`port/notes/s53/dos_emu.py` + `frontend_ref.py`):
  the text screens at the first question, the sound menu and at exit, and the comm block are equal (only the mouse bit differs, PORT), and the screen + DAC CRC of
  all 82 logo frames are equal; (2) every cel of the 6 `.PAN` files through the ring buffer (160 KB: it wraps) equals the Phase 2 decoder, the final palette too;
  (3) timing: 336 tick callbacks between frame 1 and frame 13 of TITLE-1 (5 s fade + 12 frames of 3 ticks), 60.08 callbacks/s and 70.09 ISR ticks/s, fade 4.99 s; the logo
  frames 1..82 take 7.743 s (the delays of the file); (4) `frontend_chain`: the scripted run (`F117_KEYS="1:31,2:03,11:39,14:39"`) goes SETUP -> MPS_LOGO ->
  PLAYER -> DSWAP -> START placeholder -> ... -> quit. **Spec corrections** (frontend.md section 9): `bios_ticks` returns the sign-extended low word; the fade's first tick is
  black; the tick-drop counter starts at 0; PLAYER ends when the last cel is shown; ss_load header layout; the blank line leaves a `.` on row 20. **Open:** the text
  font is an 8x8 font with doubled rows (not the VGA ROM font); the mouse is always "present"; PLAYER and the logo are silent until S5.9; U-front (the first window check of the
  key handling, the mode switches and the fade by eye).
- S5.4 status (2026-10-14): `f117port/src/shell` has START; `ctest` 14/14 (new: `shell`, `shell_chain`; 55 checks in `shell`), no warnings under `-Wall -Wextra`.
  **Ported:** the whole of START: the UI toolkit (text and wrapping, popups, frames, dialogs, the menu loop with the software cursor, the arrow-key jumps, mouse
  and joystick, the waits, the highlight animation, the palette queue and fades, the Alt-Q prompt), the ready room and home card, the hangar, arming, the briefing map
  (route animation, rings, icons, site boxes), the pilot-orders screen and the folder pages, the roster (screen, hover panel, name entry, options panel,
  `ROSTER.FIL` load and save), `mission_generate` with the WLD reader, the .3DG / .3DT tile maps and `wld_export_to_comm`, the exit codes 0Ch / 0Dh / 0, START's ISR body
  (`DS:ADFC++` + sound poll). **Not ported / stubbed:** the ID quiz (dead code in this version: no `--skip-id` is needed), the sound calls are `sh_snd()` =
  `fe_snd_cmd` (`TODO(S5.9)`), the C library copies (fwrite, memset, ctype, ltoa ...: libc), `fcb_find` always says "not the original disk". **Tests:** the ORIGINAL START.EXE
  under Unicorn (`port/notes/s54/shell_emu.py`: MGRAPHIC and MISC loaded as overlays, a stub sound driver, a key queue, injected timer interrupts) runs six scripts
  (pilot creation, options, orders, briefing modes, folder, arming, launch: a ground strike, an air combat mission, both training missions, a Lockheed pilot, and a roster
  read and saved again); the port runs the same scripts and gives the same screen + DAC CRC at every step (94 in all), the same comm block (the mission generated for the fixed
  seed, byte for byte, 5120 bytes), the same exit code and the same `ROSTER.FIL` (after a create-pilot and save, and the round trip of a roster written by the original).
  `shell_chain` is the scripted run of the whole chain (`F117_KEYS` for SETUP and the intro, `F117_SHELL_SCRIPT` for START: new pilot, strike mission, launch, VGAME and END
  placeholders, START again, Alt-Q). `frontend_chain` now ends with START quitting by its script. The references are cached in the build folder (a stamp of the scripts
  and the original programs); `shell` takes about 5 min the first time. **Spec corrections** (`shell.md` Part F): the quiz is dead; `arm_roll_unavailable` sets `D878[j]`;
  hotspots 13 and 15 (not 14) are disabled for the Lockheed; `ui_fixmul_q14` rounds `a*b/32768`; `mission_clamp_hi` wraps; the second site on the first one's position is
  rejected; two `rand()` per template slot; `ui_wait_input(0)` does not wait 65536 ticks; 20D6 uses the departure site's name test for the landing name. **Open:** U-shell
  (the whole shell by eye next to DOSBox-X, mouse and joystick: the tests run with the keyboard only); a repeated orders click that does not change the choice erases a
  marker at a stale stack index in the original (the port erases and redraws the same marker); START never installs Ctrl-Break (a closed window ends it in the ISR body, PORT).
- S5.5 status (2026-10-14): `f117port/src/sim` has VGAME; `ctest` 15/15 (new: `sim_vgame`; `shell_chain` now runs the real VGAME for a few frames), no warnings under `-Wall -Wextra`.
  **Ported (every code function of `simflow_symbols.csv` with owner simflow):** start-up and shutdown (world from comm+7Ah, files, palettes, the world and the 0x600-byte event
  log back to the comm block, `mission_end`), the frame loop, the timer ISR body (70.09 Hz tick, sound poll skipped on every 7th tick = 60.08 /s, the rate switch, `snd_noise`), the sim step
  39C4 with the fps estimator, the key interpreter `cba0` and the keypad-as-stick hook, `view_frame` (all 13 view modes, matrix B156, picture reloads, the pieces pasted over the window),
  the HUD (overlay 7898, asm HUD 10A8 with the ladder, ILS, messages, lamps), the ADI 17A3, the left MFD (map, scope), the right MFD pages (TV, nav, weapons, objectives), the sensor window
  `bf71` with its bands and FLIR tint, the overlay pass `a17e` (markers, tracers with hit tests, explosion, target symbology, photo shutter, bomb cues, radio beacon) and the 1001 edge tables /
  clipped lines / fills the HUD draws through. **Written here for others:** `sim_matrix_from_angles` (E26B) and `r3d_project_point` (B85D, render3d.md Part 2) because the view and the overlay need
  them (S5.6 takes them over), `gfxlib_mode_init` (1359:0000). **Stubs** (each marked `STUB ssss:oooo name`; the test harness hooks the original at that address to return 0): render3d
  `r3d_scene_draw` (03CE: a flat sky / ground backdrop, the DAE8 frame-sync wait, `frame_delay`, then a17e), `r3d_begin_scene`, `r3d_draw_terrain`, `r3d_place_object`, `r3d_end_scene`,
  `r3d_draw_queued`, `fx_smoke_draw`, `fx_fire_puffs`, `fx_explosion_sprite`, `obj_wreck_shape`, `r3d_unit_terrain_class`, `map_render_terrain` (TODO(S5.6)); the player flight model of 1E9E (only its
  key fetch, the first-call set-up and the Alt-Q / Alt-P / Alt-B screens are ported), `ai_update_units`, `ai_unit_takeoff`, `wpn_proj_update_all`, `wpn_cm_tick`, `wpn_gun_update`, `wpn_release`,
  `wpn_release_countermeasure`, `wpn_suit`, `wpn_site_dist`, `obj_find_at`, `obj_cls_ship`, `fm_bomb_dive_angle`, `hud_threat_update`, `terr_find_scenery_at`, `terr_set_scenery_state`,
  `mission_target_done`, `dmg_unit_kill`, `dmg_player_blast`, `dmg_wobj_destroyable`, `dmg_object_destroy` (TODO(S5.7)); the sound driver calls (TODO(S5.9)). **Tests** (`sim_ref.py`, the ORIGINAL VGAME.EXE under
  Unicorn, `vgame_emu.py`): `startup` (the whole DGROUP, the three pages and the DAC at the entry of `sim_run` equal, segment words masked), `ticks` (7000 ISR ticks: 6000 polls = 60.08 /s, the
  divisor switch, snd_noise) and `calls`: 1262 scenarios of single functions and of frame-loop bodies (view modes, HUD, ADI, MFDs, overlay pass, step, keys, export of the world and the event log)
  from a snapshot, comparing DGROUP, every page row, the DAC and the comm block; 0 differences; mutation checks showed that every text and line of the overlay pass is hit. **Spec corrections**
  (`simflow.md`): `17A3` uses the literal 0A7B8h for `si` of both vertex pairs (`si = mul15(A7B8, 98E4)`, `di = mul15(v, 98C8)`), and `t = (98F8 << 4) / max(9F12, 20h)` (dividend first);
  `c94e` is the wrapping clamp `fx_clamp_wrap`; in `a17e` the " at <place>" line is drawn only when the place name is not empty (its prefix " at " only when the type name is not empty);
  in `bf71` the magnified camera stores `[B80A] = 0` (no sign extension); mode 0x8A of `2F82` falls to the default (a stale camera). Port note: `poly_fill` takes the dirty rows as signed words (0FFFFh = empty). **Open:** U-sim (the cockpit by eye next to DOSBox-X); the y < 0 / y >= 200 HUD line draws read a garbage row table in the original (the port
  uses y * 320); a17e's bomb cue reads two stale stack locals when its pipper block is skipped (the port starts them at 0); `Game/ROSTER.FIL` has the modification time of the S5.4 incident
  (20:18, before the S5.4 commit); nothing in `Game/` was written by S5.5.
- S5.6a status (2026-10-15): `f117port/src/render3d` has the leaf routines; `ctest` adds `render3d_leaf` (`port/notes/s56/r3d_leaf_ref.py` writes ~90000 random cases from the Python references of S3.7, `f117 --r3d-test` runs the C routines on them; 0 mismatches, mutation checks bite).
  **Ported:** `r3d_clip.c` (12EF: outcode16/32, on_border, flags16/32, intercept, bisect_in/out with the half() quirk, clip_edge 16-bit and 32-bit paths, note_left/right/zclip, poly_add_edge, wall_left/right, line_emit, poly_finish), `r3d_raster.c` (1359 mode M: gfxlib_mode_init + rows_M, set_colour, edge_dda, wall_fill, line_M, fill_M with all fill types and the gap-row stop, the dispatch stubs), `edge2d.c` (segment 1001, moved from `sim/gfx2d.c`, plus `hz_draw` / `hz_fill_far`), `r3d_sky.c` (1442), `r3d_math.c` (1433: sin, cos, atan2, asin, hypot2, mat_vec16, mat_mul, mat_row_mul, mat_transpose, mat_copy, vec_mat32, mat_from_euler; its tables are read from segment 14B2 of the loaded image = DS - 1E23h + 14B2h). The 109D helpers and `r3d_project_point` / `sim_matrix_from_angles` stay in `src/sim` (S5.5). The DGROUP work buffers are one struct `R3dWork` (DS:8568..9145, offsets asserted) in `r3d.h`.
  Not ported (dead or no callers): `clip_add_edge`, `draw_line`, `set_page`, the E/C/T modes, `mul_sin`, `mul_cos`, `hypot3`, `mat_to_euler`, `mulq`, `muldiv`, `div32sat`, `imul`, `mat_from_euler_b`. The INT 0 divide-overflow of the 16-bit intercept is impossible (spec) and maps to 0. The `r3d_trace` hook reports the MGRAPHIC calls of segment 1001 to the unit test instead of making them; `r3d_test_page` redirects the raster page. `r3d_scene_draw` (sim_view.c) now draws the real sky / ground backdrop (DB71 centre and clip maxima, E26B cache, DCA9 height, 1442) for windows at (0,0); other windows keep the flat S5.5 backdrop.
  **For S5.6b:** the rest of render3d.md Parts 1-4 (scene set-up DAE8/DB71/DC64, terrain 04DA, placement/cull, sorted queue DDFE/DF08, object renderer 11EB, world pass 9ACA, billboards, map terrain 119C) and the golden-frame tests; the 12EF/1359 routines are called by 11EB (`r3d_clip_edge`, `r3d_poly_add_edge`, `r3d_poly_finish`, `r3d_line_emit`).
- S5.6 render3d test plan (golden frames, 2026-10-13). The leaf routines are already bit-exact against the original
  (`port/notes/s37/R7*_emu.py`, 0 mismatches, re-run by the user); the six routines that call the object renderer
  (DF08, 04DA, DCC0, BD8F, 9ACA, 0CED) are only read. S5.6 closes that gap with whole-frame comparisons:
  1. The S5.6 agent works out the run-time address of `r3d_scene_draw` (`0000:03CE`, VGAME load segment = PSP+10h)
     and asks for U4 dumps: `BP` at its entry -> `MEMDUMPBIN 0000:0000 B0000` (`<scene>_in.bin`, includes A000), then
     step over the call -> the same dump (`<scene>_out.bin`). Scenes: (a) day, low over the airfield, (b) night, low
     over scenery, (c) target in the TV camera window, (d) a large polygon crossing the screen edge, (e) close to a
     rotating radar dish. 5 scenes x 2 dumps, about 20 min of U4.
  2. Before porting: run `03CE` itself under Unicorn from each `_in` dump (extend `port/notes/s37/R7*_emu.py`; stub
     the INT and driver calls) and check it reproduces the `_out` off-screen page. That validates the dumps and the
     harness, and the untested routines can then be diffed function by function.
  3. After porting: `f117 --render-test <scene>_in.bin` loads DGROUP from the dump, calls the port's `r3d_scene_draw`
     with the same arguments and compares the off-screen page byte for byte with `_out` (a ctest per scene).
- S5.6b status (2026-10-15): `f117port/src/render3d` has the whole renderer; `ctest` 17/17 (new: `render3d_golden`), no warnings. **Ported:** `r3d_object.c` (11EB: 000C camera, 00D3 / 01B8,
  02B6, parts 031B / 0440 / 04CD / 04F9, planes 0565 / 0658 / 0678, BSP 0E5F, vertices 06B9 / 0819, projection 0906 with 0975 as a checked division, 0990, edges 0A37 / 0B3F, faces 0C7A / 0CAC / 0CED, 0F9B),
  `r3d_scene.c` (03CE, DAE8 / DB71 incl. the pixel offset of windows not at (0,0) / DC64 / DCA9 / DB3E, E3E2, E54B, E7AC, E8A2 / E8B5, 1088:0120, C6B8, BD8F, BEE1, DCC0, DDFE, DF08, 1AD6 / 1B1B / 1A85),
  `r3d_terrain.c` (0804, 0865, 0FB1, 04DA, 119C / 11F2 / 1353 / 13AC / 13E3, 0FFB / 77D9, BC12 / BC77 / BCD7), `r3d_world.c` (9ACA, BA67, C73C, C834; bbb4 / bb86 / bb9e of the weapons
  logic, here until S5.7). The S5.5 stubs of render3d are gone (the sim calls go through the `sim_*` wrappers of `sim_stubs.c`); `render3d_symbols.csv` type column fixed for every ported
  function. **Tests:** golden frames without DOSBox dumps: `port/notes/s56/r3d_golden.py` runs the ORIGINAL 03CE / 2F82 / bf71 / 119C / 77D9 under Unicorn (S5.5 harness, now with the
  11EB idiv hooks) and `f117 --render-test` on 110 scenes from a running mission (runway in 8 headings, cockpit / full screen / HUD style 1, low over terrain at detail 0..3, look-down,
  high, 10 targets at three distances incl. inside the models, external views with the own F-117, smoke billboards, units, whole `view_frame`s after sim steps, the map MFD at every
  zoom, the TV camera, the collision probe): DGROUP, pages, DAC and comm block byte for byte, 0 differences; 11 mutation checks all bite. `sim_vgame` now runs the real bf71 / 119C /
  BC12 / BCD7 / 1A85 on both sides (1262 scenarios, 0 differences). **Spec correction:** 1AD6 / 1B1B / 1A85 offset y by `dh / 2` (not `size / 2`). **Open:** U-render3d (by eye next to
  DOSBox-X, optional dumps); C686 "inside" is never reached by the probe scenes (the 0FFB result path is covered only up to the model test); the 0975 divide-overflow path is hit by one scene only. PORT: 11EB:0281 only saves IVT[0] (masked in the test).
- S5.7 status (2026-10-15): `f117port/src/flight` has the flight module; `ctest` 17/17, no warnings; `sim_vgame` now has 1497 scenarios (new families `flight`, `combat`, `engage`), 0 differences against the ORIGINAL VGAME under Unicorn (DGROUP, pages, DAC, comm block), with no flight / weapons / AI / detection / damage stub left on either side. **Ported:** `fl_flight.c` (1E9E sim_input_flight whole: keys, stick, autopilot, buffet, flare assist, ejection sequence, throttle cap, spool, fuel, G text, speed / lift / turn, the three incremental rotations, stall, ground handling, position, touch-down and hard landing, history ring, sway; 2C6E, 2C9E, 2E43, E578, 10A8:0C87, BBED), `fl_detect.c` (5460, 516D, 5588, 559A, 4F2A), `fl_ai.c` (5730 with the scramble and the steering, 62D0, 65D4), `fl_weapons.c` (753F, 671A, 711F, 4512, 461C, 465B, 8BAB, BAD1, BBB4 / BB86 / BB9E moved from `render3d/r3d_world.c`, BCAC), `fl_damage.c` (74F6, 71B4, 72A4, 7470, 780A, 0D08, 0F31). The keypad-as-stick INT 9 hook stays in `sim/sim_input.c` (now `keypad_stick_isr`), and so do the Alt-Q / Alt-P / Alt-B screens. `flight_symbols.csv` types fixed (fm_rotate_apply, mat_mul E578, keypad_stick_isr, wpn_suit; 71B4's note no longer says "dead", which had hidden its prototype). **Tests:** scripted multi-frame flights (take-off, climb, turn, level-off, air work in the three crash modes with damage and autopilot, landings incl. decks and hard landings, ground handling and the catapult, ejection with Esc), detection near every radar (scans, levels, ECM, bay doors), SAM launch and flight to the player, enemy missiles with flares / chaff / decoy / jammers, fighters (combat table, cannon, radar, AAM launches, formation, RTB), take-offs and scrambles, the player's stores over targets (bombs, guided, AAM), the gun, direct damage / target / scenery calls, the mission end and the export to END; mutation checks in every file bite. The harness got `key` (BIOS key), `pokec` (comm block) and SI = 1 before each original call (671A passes its caller's SI as the blip size of 8548). Not done: a separate C vector test of the s36 / s38 Python references (the whole-machine comparison against the original covers the same functions). **Fixes in earlier ports:** 1E9E's first-call carrier test reads DS:B2AB (S5.5 had B2A9); the [DED6] recount of 39C4 leaves each of its two loops at the first hit (an original quirk: DED6 = 0..2; S5.5 counted all). **Spec corrections** (flight.md section 8): the DED6 recount quirk (simflow.md Part 4 section 1.2); VGAME's rand_n (CB62) multiplies signed (imul), unlike the RTL rand_n of the other programs. **PORT:** for radar type 0 / -1, 516D and 62D0 keep the previous call's bearing / distance (flight.md Q-A1); 671A's map blips are 2x2 (the original passes stale SI, not 0 in the game; U-fly item 6). **Open:** U-fly; the sound calls stay stubs until S5.9; END is still the placeholder until S5.8.
- S5.8 status (2026-10-16): `f117port/src/debrief` has END; `ctest` 19/19 (new: `debrief`, `debrief_chain`; `shell_chain` now runs the real END), no warnings under `-Wall -Wextra`. **Ported (every live function of `debrief_symbols.csv`):** `score.c` (3021 event-log score with its quirks, 2AFA promotion, 2476 medals / ribbons, 05CA icon placement, 0EF3 clock, 00A1 landing class, 0ECD / 0EE0), `end_map.c` (02A4 map screen, route replay 0714 with the event picture 0880 / text 0A98 / status 0DA3, site dots 04D2, hover 046E, 03D9 menu and the score into the record), `end_awards.c` (2265, ribbons, medal / promotion texts, rank board), `end_screens.c` (2BEC summary paragraph, crash / rescue / captured / landing scenes, 39D0 quips), `end_main.c` (start-up, the mission blob 0113 / 00C9, main), `end_ui.c` (the toolkit pieces with END's DGROUP offsets). **Reused from the shell (state-free START toolkit):** `ui_text`, `ui_text_width`, `ui_text_wrap`, `ui_text_shadow`, `ui_text_centered`, `ui_sprite_draw`, `ui_wait_ticks`, `ui_load_file_far/near` (nine of the ~40 twins; the CSV rows were renamed to the shell names). **Own C because the state is at END's offsets:** cursor / `end_poll_input` / `menu_loop` / `hit_test` / `wait_input` (not START's: 0FFFFh never counts down, sounds inside) / `prompt_continue` / `end_pal_queue_push` / `end_pal_fade` / fades / `show_screen_pic` / `quit_prompt` / `end_exit` (the START copies read START's DGROUP); `debrief_symbols.csv` types fixed (`end_main` 0010 is now `debrief_main`, `poll_input` is `end_poll_input`, `pal_fade*` / `pal_queue_push` got `end_` names), generators re-run. **Tests:** `port/notes/s58/end_emu.py` runs the ORIGINAL END under Unicorn (S5.4 machine: MGRAPHIC / MISC overlays, key queue, timer interrupts) on eight comm blocks built on the `shell` test's mission (`end_ref.py`: result fields, event log, pilot record): landed at night (war 1, kills, both objectives, short tour ribbon), ejected and rescued, ejected and captured (war 2, ghost medal), crashed (KIA), promoted with the Distinguished Flying Cross / Purple Heart / long tour ribbon, a training flight with the Combat Readiness Ribbon, the 99th mission lost (KIA) and won (Brigadier General, retirement, flight deck, career-score carry). The port (`f117 --debrief-test`, `endtest.c`) runs the same scripts: screen + DAC CRC at every `wait_input` entry and every 3rd menu poll (20-65 per scenario, 380 in all), the pilot record in the comm block byte for byte, exit 23h: 0 differences (mutation checks bite). References cached in the build tree (about 100 s the first time, the port run about 90 s). `debrief_chain`: START (new pilot, strike) -> VGAME (`F117_VGAME_FRAMES=40`, new `F117_VGAME_END=0` lands instead of aborting) -> the real END (`F117_END_SCRIPT=port/notes/s58/end_chain.txt`) -> START saves the roster; the `ROSTER.FIL` record of the pilot equals the record END logged. **Spec corrections** (debrief.md 6.3, "Port notes"): the site dots are skipped only by Space (`wait_input` returns 1 for Space alone; Enter, Esc and clicks return 0). **PORT:** the leaked first 35000-byte buffer of `end_init` is not allocated, no ISR juggling in the fades, constant fade calibration; `crash_cb` (codes 0, > 6) and the `26h = 1, 28h = 0` summary draw nothing where the original prints stack garbage. **Open:** U-debrief (the port next to DOSBox-X, look and feel); sound calls are `TODO(S5.9)`; mouse and joystick paths of `wait_input` / `end_poll_input` are as START's (the tests use the keyboard only).
- S5.9 status (2026-10-16): `f117port/src/sound` has the sound module; `ctest` 21/21 (new: `sound`, `sound_chain`; the other 19 unchanged), no warnings. **S3.11 first:** the poll is part of the tick work, so it runs once per video frame in both modes (the old 4x rule was wrong; only `snd_noise` runs class x per frame in fast mode); START polls at 70.09 Hz, PLAYER (class 4), VGAME and END at 60.08 Hz (END has the 7th-tick skip too, `4D51`: the old specs said it had none); the logo overlay has its own clock (60.88 Hz). `sound.md` section 3 rewritten (dated), `tools/sound/asnd.py steer` takes the program, the steered WAVs re-rendered. **Ported:** `snd_engine.c` (OPL writes with the shadow, voice primitives, instruments, events, effect units, noise, per-voice tick; two layouts: ASOUND.117 and ASOUND.LOG), `snd_game.c` (loader, snd_init / snd_cmd (46 commands) / snd_shutdown / snd_poll / snd_noise, engine drone, `snd_isr_step` = the sound step of the START / END / PLAYER ISR bodies), `snd_logo.c` (the overlay: reset, cmds 0-2, slow / fast INT 8 modelled on a clock against the host time, driven from the tick through the new `timer_set_aux`). Output goes through `host_opl_write` (audio mutex); the poll runs on the main thread from the tick; the chip is also reset in `--headless` so writes are safe. Wiring: `prog.c` loads `ASOUND.117` by `comm+0Dh` after MPS_LOGO and shuts it down at quit; MPS_LOGO loads `ASOUND.LOG`; every `TODO(S5.9)` stub is gone (`sndstub.c` adapters, `sim_stubs.c` real driver unless `sim_test.active`, END's skip counter `DS:50CD`). `sound_symbols.csv` types fixed, speech / SB / logo duplicates marked dead, generators re-run. **Tests:** `sound` replays 44 call scripts (every command 0-40, cmd 3, 36-39, VGAME / shell / END mixes with the engine drone, the logo jingle) recorded from the ORIGINAL drivers under Unicorn: 141 989 OPL writes and poll results, 0 differences; the poll counts per 7000 ticks (START 7000, PLAYER / END / VGAME 6000 = 60.07 Hz, PLAYER at class 5 7000), START on the host clock (70 polls/s), the logo clock (61 Hz); `sound_chain` runs the front-end chain with SDL dummy audio and video, not headless. **PORT:** Roland / IBM-Tandy-speaker choices play AdLib (decision U7 item 1), N is silent; speech commands 41-45 (Sound Blaster) are silent; the SB / PAS states are not ported; the cmd-2 wait of PLAYER now really waits for the music (any key ends it). **Open:** U-sound (listening).

### Phase 6 — Release, then Enhanced
| Id | Task | Model |
|---|---|---|
| S6.1 | Launcher (copy from Aces Enhanced), README, CHANGELOG, v0.1.0 zip | H | launcher done early 2026-10-06 (`f117port/launcher`, aligned with the TD3 launcher: dialog, Play only); key bindings added 2026-10-16 (Key Bindings... dialog, 56 actions, `--keys`, `src/keymap.c`, `keymap` ctest, U-keys); README/CHANGELOG/zip pending |
| S6.2 | New `F117Enhanced` repo: native-resolution / widescreen / 60 fps 3D view, longer view distance | O plan, S port |

## User tasks (cheap for you, expensive for a model)

### U1 — Manual and docs (now, 10 min)
The manual answers game-rule questions that would otherwise cost reverse-engineering sessions.
1. Find the F-117A manual and technical supplement / keyboard card PDFs (eXoDOS:
   `eXoDOS\Manuals\MS-DOS\F-117A Nighthawk Stealth Fighter 2.0 (1991).pdf`, or a scan from the
   internet archive).
2. Save them as `C:\Coding\F117-2\docs\manual.pdf` (and `keys.pdf` if separate). PDFs and their
   text are git-ignored.
3. If you have Poppler (`pdftotext`), also run in PowerShell:
   `pdftotext -layout docs\manual.pdf docs\manual.txt` — agents then grep the text instead of reading
   the PDF. If not, skip; an agent will do it once.

### U2 — DOSBox-X reference setup (now, 15 min)
1. Download DOSBox-X (portable zip, Windows 64-bit, SDL2 build) from https://dosbox-x.com and unzip it
   to `C:\Coding\F117-2\DOSBOX\` (git-ignored).
2. Create `C:\Coding\F117-2\DOSBOX\f117.conf` with:
   ```ini
   [sdl]
   output=opengl
   [dosbox]
   machine=svga_s3
   memsize=4
   captures=C:\Coding\F117-2\work\ref
   [cpu]
   core=normal
   cputype=386
   cycles=fixed 12000
   [sblaster]
   sbtype=sb16
   oplmode=opl2
   [autoexec]
   mount c C:\Coding\F117-2\F-117ANi
   c:
   ```
3. Create the folder `C:\Coding\F117-2\work\ref`.
4. Run `DOSBOX\dosbox-x.exe -conf DOSBOX\f117.conf`, then `SETUP` once (VGA/MCGA, AdLib, your
   controls), then `F117` to play. Do **not** use `run.bat` (it overwrites `NC.WLD`).
5. If the game runs too fast or slow, change `cycles` and note the value in a reply.

### U3 — Reference captures (whenever, 30 min; more on request)
Screenshot hotkey: **Ctrl+F5** (if nothing appears in `work\ref`, check the hotkey in the DOSBox-X
mapper, Ctrl+F1). Then rename the files to `NN_name.png`:
`01_mps_logo`, `02_title1`, `03_title2`, `04_title3a`, `05_credits`, `06_roster`, `07_theatre`,
`08_briefing`, `09_arming`, `10_id_quiz`, `11_cockpit_runway`, `12_cockpit_flight`, `13_external_view`,
`14_map`, `15_target_view`, `16_landing`, `17_debrief`, `18_awards`. Also one AdLib music sample:
**Ctrl+Alt+F7** starts/stops a raw OPL capture (`.dro`) during the title music → rename `title.dro`.
These are the ground truth for U5 and the agents only open them when a check fails.

### U4 — Memory dumps (only when an agent asks)
An agent will give you a program, a moment ("on the briefing screen") and optionally a breakpoint.
1. Run DOSBox-X as in U2 and reach that moment.
2. Open the debugger: menu **Debug → Debugger** (or Alt+Pause).
3. If a breakpoint was given, type `BP SSSS:OOOO`, Enter, then F5 and play until it stops.
4. Type `MEMDUMPBIN 0000:0000 A0000` and Enter. It writes `MEMDUMP.BIN` in the DOSBox-X folder.
5. Move it to `work\ref\dumps\<name the agent gave>.bin` and reply "dumped".
One dump replaces many sessions of guessing values.

### U5 — Visual / audio checks (when an agent asks)
Open the file it names (PNG, contact sheet, WAV) next to your U3 capture and reply in one line:
`OK`, or what is wrong and where (e.g. "PIC 7 colours shifted, red where blue", "frame 3 offset
8 px right", "music too fast"). Agents will not look at images themselves.

### U6 — Playtests and bug reports (from phase 5)
Build: `cmake --build f117port\build --config Release`, run as the agent says. Report bugs with:
```
Build: <commit hash>     Program: START / VGAME / END
Steps: <what you did, from launch>
DOSBox does: <expected, or capture name>
Port does: <what happened; crash message if any>
Files: work\ref\bugs\<n>.png, ROSTER.FIL copy if relevant
```
Put several bugs in one message; one session then fixes them in a batch.

### U7 — Decisions (answer when convenient; defaults apply otherwise)
1. **Sound**: AdLib through Nuked-OPL3 (as Aces); Roland MT-32 later. Default: yes.
2. **Packaging**: one executable; the `F117.COM` chain becomes a loop in `main`. Default: yes.
3. **Graphics**: VGA/MCGA only (the only driver shipped). Default: yes.
4. **Copy protection (aircraft ID quiz)**: keep as in the original, with a `--skip-id` switch?
   Default: keep, add the switch.
5. **Secret missions**: the port reads whatever `NC.WLD` is in `Game/`, so both builds work. Default:
   nothing to do.
6. **GitHub**: done 2026-10-13: private `kylofon/F117-2`, branch `main`.
7. **Simulation timing (found in S3.6)**: decided 2026-10-13, see Decisions.

## Decisions
(Settled answers from U7 go here, dated.)
- 2026-10-13, U7 item 7 (simulation timing): the faithful port **emulates the original timing exactly**: one tick per
  retrace at 70.09 Hz, the `[3660]` frame-rate estimator with its 60-ticks/s assumption and `sim_rate` scaling, and the
  sound poll skipping every 7th tick (`simflow.md` Part 0 D1, Part 1 section 1, Part 4 section 1). The Enhanced repo (S6.2)
  will likely change this, aiming for 60 fps; decide it there.
- 2026-10-16, game speed (the user's "radar dish spins at an extreme speed"): faithful behaviour, not a port bug. The dish turns by a fixed
  33.75 degrees per scene (`DB3E`, `[C04E] += 1800h`; again per map-MFD redraw), and the original paces a frame only by `frame_delay([43B8])`
  and the tick wait, so on any fast PC (the port, DOSBox at high cycles) it runs at the cap of its own estimator: measured 4 ticks a frame =
  17.5 frames/s, `sim_rate` 15, `[43B8]` 4; a 1991 PC drew a handful. **The port's choice:** `--frame-ticks N` (launcher "Game speed"),
  at least N retrace ticks a frame on top of the original's pacing (`sim_main.c frame_floor`, PORT). **Default 7** = 10.0 frames/s
  (measured; `sim_rate` settles at 8, the middle of the estimator's 4..15, the flight model keeps its speed), the dish about 0.9 turns/s.
  0 = the original pacing exactly (the tests run without it). The user can change it in the launcher (0..20) or with `--frame-ticks`.

## TODO (parked)
- [ ] Roland MT-32 (`RSOUND.117`; Munt, user ROMs), IBM/Tandy sound
