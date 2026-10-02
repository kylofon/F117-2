# F-117A Nighthawk Stealth Fighter 2.0 — SDL3 port plan

Same approach as `../AcesOfThePacific` and the Test Drive ports: a **faithful C port** here
(reverse-engineer → document → port function by function, original data read at run time), then a
separate **F-117A Enhanced** repo built on it. Token efficiency is a first-class goal: small subtasks,
one session each, the cheapest model that can do the job, and the user doing whatever is cheap for a
human and expensive for a model (looking at pictures, playing the game, driving DOSBox).

## Status

- **Done:** S0.1 (repo), Phase 1 S1.1-S1.5 (survey, overlay chain, indexer, Ghidra, shared code).
- **Next:** S2.1 U5 check (PIC/PAL sheets in `work/pic/`), then S2.2. User tasks that can start now: U1, U2, U3.
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
| S2.1 | `.PIC`/`.PAL`/`PALETTES.PAL` → PNG + one contact sheet; **U5 check** | S | S |

_S2.1 status: decoder + PNGs done (`tools/picdec.py`, `tools/pic2png.py`, `work/pic/`); awaiting U5 (`work/pic/sheet.png`, `palettes.png`)._

| S2.2 | `.PAN` animations and `LABSLOGO.SS` → PNG frames + contact sheet; **U5 check** against U3 captures | S | M |

_S2.2 status: decoders done (`tools/lzs.py`, `panlib.py`, `pan2png.py`, `ss2png.py`; output `work/pan/`); all 6 PAN cels and 81 LABSLOGO sprites decode to exact record boundaries. Awaiting U5: `work/pan/*_sheet.png` vs U3 captures 02_title1, 03_title2, 04_title3a, 05_credits, 01_mps_logo (+ cockpit/bunker). Formats in `port/RE_GUIDE.md` §4._
| S2.3 | 3D: `.3DX` objects (+ `STFLT`, `PHOTO`), `.3DG`/`.3DT`, `.WLD` layout → OBJ files + top-down theatre map PNGs; **U5 check** | O | L |
| S2.4 | Sound: `.LOG` music format and driver API (from S1.2); AdLib render via Nuked-OPL3 (copy `../AcesOfThePacificEnhanced/third_party/nuked-opl3`) → `work/sound/*.wav`; `SPEECH.117`; **U5 listen** | S | M |

_S2.4 status: done except U5. `.LOG` = logo-jingle overlay, driver API + speech in RE_GUIDE §5; WAVs in `work/sound/` (`asound_log_logo`, `asound_cmd03/36-39` music, `speech_0-7`). Please listen and report (tempo assumes 300 Hz poll)._

| S2.5 | `ROSTER.FIL`, files written by SETUP/START (config, saves) → `FORMATS.md` | H | done 2026-10-02 |

### Phase 3 — Specs (output: `port/spec/<module>.md`, gate: `tools/spec_coverage.py` 100 %)
Module split is confirmed in S1.5; one session per module. Specs are written so that a Sonnet session
can port a function from its section alone.
| Id | Module | Model | Size |
|---|---|---|---|
| S3.1 | platform: RTL, startup, memory, timer, keyboard, joystick, files, overlay chain | S | M |
| S3.2 | video: `MGRAPHIC` driver API, blitters, PIC/PAN decoding, fonts | S | M |
| S3.3 | sound: driver API, music sequencer, AdLib device | S | M |
| S3.4 | front end: `MPS_LOGO`, `SETUP`, `PLAYER`, `DSWAP`, `MISC`, title/credits | S | M |
| S3.5 | shell (`START`): roster, theatres, briefing, arming, ID quiz | S | L |
| S3.6 | sim flow (`VGAME`): main loop, mission state, HUD, cockpit, MFDs, view modes | S | L |
| S3.7 | render3d: transform, clipping, polygons, terrain, sky, sorting | O | L |
| S3.8 | flight model, controls, weapons, AI, detection (stealth), damage | O | L |
| S3.9 | debrief (`END`): scoring, medals, promotion, death/rescue screens | S | M |
| S3.10 | `HANDOFFS.md`: data passed between programs; coverage gate; symbols merge | H | S |

### Phase 4 — `f117port/` skeleton
| Id | Task | Model | Size |
|---|---|---|---|
| S4.1 | Copy `../AcesOfThePacific/aotpport` skeleton (CMake, SDL3 host, mem, VGA, OPL); drop EMS/VROOMM; chain loop = `F117.COM`; placeholder programs run the whole chain headless | S | M |
| S4.2 | Generated `symbols_*.h`, module headers (`tools/gen_symbols.py`, `gen_module_headers.py`), `PORTING.md` | H | S |

### Phase 5 — Port, module by module (one session per module or half-module)
Order: S5.1 platform → S5.2 video → S5.3 front end/title (**U4 first visible milestone**) → S5.4 shell
→ S5.5 sim flow with a static 3D view → S5.6 render3d → S5.7 flight/weapons/AI → S5.8 debrief →
S5.9 sound. Model: S for 5.1–5.5, 5.8, 5.9; O for 5.6, 5.7. Each ends with a U6 playtest request.

### Phase 6 — Release, then Enhanced
| Id | Task | Model |
|---|---|---|
| S6.1 | Launcher (copy from Aces Enhanced), README, CHANGELOG, v0.1.0 zip | H |
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
6. **GitHub**: create `kylofon/f117a-sdl3` (private?) when you want a remote; default: local only.

## Decisions
(Settled answers from U7 go here, dated.)

## TODO (parked)
- [ ] Roland MT-32 (`RSOUND.117`; Munt, user ROMs), IBM/Tandy sound
