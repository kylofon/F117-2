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
| U-player | Run original PLAYER (DOSBox-X as in U2, then `F117`; watch the title animations) and tell me: Only question left: does the first frame fade in from black over about 5 s (and do the frames then follow at the speed you remember)? (The tick rate is settled by the code: 60.08 ticks/s on a 70 Hz display, `frontend.md` 8.3.3.) Reply with what you see. | S3.4 | 3 min |
| U-sound | Listen to `work/sound/asound_cmd03_steered.wav` (PLAYER intro soundtrack, new poll-rate rule: 70 Hz idle, 280 Hz while an effect runs) and `asound_cmd36/37/38/39_steered.wav`; compare with the old `asound_cmd03.wav` / `asound_cmd36..39.wav`. Reply: do the steered ones sound right (tempo, no odd speed-ups)? If you can, check original PLAYER in DOSBox: does the intro music run in jumps of speed? | S3.3 | 5 min |
| U-vgame | Visual/runtime checks collected from the S3.6 readers (one session, DOSBox-X in flight): (1) rear view (key for view 0x42?): is the centre 138x157 of `256rear.pic` pasted opaque over the 3D scene? (2) right MFD sensor window (TV/DLIR, page 0x13): position x169-245 y110-169, `BRG nnn` and zoom labels, FLIR green tint; (3) left map/scope MFD: signal bars/arcs colours; does the map start at zoom 2? (4) HUD: first tick strip colour equals the second; tracer/explosion colours; (5) nav page ETA/units. Reply with what looks different from `port/spec/simflow.md` (Parts 3, 5, 7, 8, 9, 10 `(U)` marks). | S3.6 | 20 min |
| U-render3d | DOSBox-X in flight (as U-vgame), look at the original and reply what you see: (1) radar dishes / rotating parts seen up close: any faces missing or flickering? (2) left map MFD while it scrolls: is the bottom pixel row of the map ever left unpainted? (3) TV camera (F6 options): is there a mode with a flat two-colour sky/ground? which option? (4) burning targets: do glow/flame sprites appear in the main view or only in the TV window? (5) any polygon that looks cut off part-way down (a hole that shows the frame before)? `port/spec/render3d.md` section 7. | S3.7 | 15 min |
| U-flight | DOSBox-X in flight (as U-vgame), reply what you see: (1) take a few hits (SAM/fighter) and tell me the labels of the damage lamps that turn red (cockpit buttons; which label lights for which hit if you can tell); (2) left map MFD with an enemy missile in flight: is its blip a 2x2 dot or a single pixel? (3) keys `4` and `3`: which lamps light (ECM / IR jammer), and does the `3` lamp stay dim for a while after it switches itself off? `port/spec/flight.md` section 11. | S3.8 | 10 min |
| U-debrief | DOSBox-X: finish any mission (land, or eject so that the rescue / captured screen runs) and tell me what you see in the original debrief: (1) on the map replay, are the route line segments visible (the code draws them with colour index 0)? (2) the 48x40 event picture that appears next to each stop: what do the pictures show (takeoff, landing, kill, missile hit, photo...)? (3) the TIME: clock at the bottom: does it start at 06:00:00 (18:00:00 on a night mission) plus an odd offset, and does the hour tens digit stay 0? `port/spec/debrief.md` 6.6-6.10. | S3.9 | 10 min |

## Status

- **Done:** S0.1 (repo), Phase 1 S1.1-S1.5 (survey, overlay chain, indexer, Ghidra, shared code), Phase 2 S2.1-S2.6, S3.1 (platform spec), S3.2 (video spec), S3.3 (sound spec), S3.4 (front-end spec), S3.5 (shell spec), S3.6 (sim flow spec), S3.7 (render3d spec), S3.8 (flight/weapons/AI/detection/damage spec), S3.9 (debrief spec), S3.10 (hand-offs, coverage gate, symbols merge; closing pass: coverage 100 %, all conflicts settled).
- **Next:** S4.1. Your tasks: **User to-do** above.
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
- S3.4 status: DONE (U-player pending). `port/spec/frontend.md` + `frontend_symbols.csv` (56 rows, `python tools/frontend_syms.py`); `tools/dsann.py` = fndis with DGROUP string annotations. SETUP screen/flow and comm fields, MPS_LOGO (SS loader palette base 31, delays in 18.2 Hz ticks, 146 ticks = 8 s, key completes the animation instantly), PLAYER (tick callback, 5 s fade-in, snd_cmd 3/0/2, exit on any input), DSWAP (port = return 1), MISC (6 entries) all verified. Open: PLAYER tick rate 60 vs 70 Hz, fade look (U-player); SETUP quirk comm+30h always 0.
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
| S3.4 | front end: `MPS_LOGO`, `SETUP`, `PLAYER`, `DSWAP`, `MISC`, title/credits | S | done 2026-10-03 · U-player pending |
| S3.5 | shell (`START`): roster, theatres, briefing, arming, ID quiz | S | done 2026-10-03 |
| S3.6 | sim flow (`VGAME`): main loop, mission state, HUD, cockpit, MFDs, view modes | S | done 2026-10-13 · U-vgame pending |
| S3.7 | render3d: transform, clipping, polygons, terrain, sky, sorting | O | done 2026-10-13 · U-render3d pending |
| S3.8 | flight model, controls, weapons, AI, detection (stealth), damage | O | done 2026-10-13 · U-flight pending |
| S3.9 | debrief (`END`): scoring, medals, promotion, death/rescue screens | S | done 2026-10-13 · U-debrief pending |
| S3.10 | `HANDOFFS.md`: data passed between programs; coverage gate; symbols merge | H | done 2026-10-13 · coverage 100% (closing pass) |
| S3.11 | sound re-check: `sound.md` section 3 assumes `snd_poll` at the ISR rate, but VGAME (simflow.md Part 0 D1, Part 1 section 1) and PLAYER (frontend.md 3.2, closing pass) drop every 7th tick (60.08 Hz at 70 Hz); correct the poll-rate rule per program, re-render `work/sound/*_steered.wav`, update U-sound. Needed before S5.9 | S | S |

### Phase 4 — `f117port/` skeleton
| Id | Task | Model | Size |
|---|---|---|---|
| S4.1 | Copy `../AcesOfThePacific/aotpport` skeleton (CMake, SDL3 host, mem, VGA, OPL); drop EMS/VROOMM; chain loop = `F117.COM`; placeholder programs run the whole chain headless | S | M |
| S4.2 | Generated `symbols_*.h`, module headers (`tools/gen_symbols.py`, `gen_module_headers.py`), `PORTING.md` | H | S |

### Phase 5 — Port, module by module (one session per module or half-module)
Order: S5.1 platform → S5.2 video → S5.3 front end/title (**U4 first visible milestone**) → S5.4 shell
→ S5.5 sim flow with a static 3D view → S5.6 render3d → S5.7 flight/weapons/AI → S5.8 debrief →
S5.9 sound. Model: S for 5.1–5.5, 5.8, 5.9; O for 5.6, 5.7. Each ends with a U6 playtest request.

Notes:
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

### Phase 6 — Release, then Enhanced
| Id | Task | Model |
|---|---|---|
| S6.1 | Launcher (copy from Aces Enhanced), README, CHANGELOG, v0.1.0 zip | H | launcher done early 2026-10-06 (`f117port/launcher`, aligned with the TD3 launcher: dialog, Play only); README/CHANGELOG/zip pending |
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

## TODO (parked)
- [ ] Roland MT-32 (`RSOUND.117`; Munt, user ROMs), IBM/Tandy sound
