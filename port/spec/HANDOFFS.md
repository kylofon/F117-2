# Hand-offs between the F-117A programs (S3.10)

Delivered 2026-10-13. Collected from the specs only: `platform.md`, `frontend.md`, `shell.md`, `simflow.md`, `render3d.md`,
`flight.md`, `debrief.md`, `video.md`, `sound.md`, plus `RE_GUIDE.md` 2.1 and 2.4. Nothing was re-disassembled.
Citations are `file:line` into `port/spec/` unless another file is named. Section numbers are given where a spec has them.

Legend
* **CONFLICT Cn**: two specs (or a spec and RE_GUIDE) said different things. All ten are now `SETTLED Cn` (section 7).
* **GAP Gn**: no spec said it. All three are now `SETTLED Gn` (section 8).
* Each program has its own DGROUP, so the same comm pointer sits at a different DS address in each program. The 5120-byte
  block itself is one shared structure at `[0000:04F0]` (`RE_GUIDE.md` 2.1 step 1).

## 1 The chain and exit codes

`F117.COM` runs the programs in turn with `INT 21h/4Bh` and reads each return code (`RE_GUIDE.md` 2.1 table). The port keeps
the same state machine (`platform.md` 6, `prog.c`).

| Program | Tail argument | Exit code and meaning | What F117.COM does next | Cite |
|---|---|---|---|---|
| SETUP | F117's own tail | 24h = configured. Any other value quits | MPS_LOGO | frontend.md:75; RE_GUIDE 2.1 row 1 |
| MPS_LOGO | `" X"`, X = `comm[0Dh] & 5Fh` (sound letter) | 2 = Alt-Q (quit); 0 = no key; 1 = Esc; 3 = other key | 0: PLAYER intro; 1 and 3: skip intro; 2: quit | frontend.md:108, :113; RE_GUIDE 2.1 row 2 |
| PLAYER (intro) | `" title-1 title-2 title-3a credit c-pit bunker"` | ignored (SS:SP not reloaded) | DSWAP `" 0"` | RE_GUIDE 2.1 row 4; frontend.md:180 |
| DSWAP `n` | `" 0"`, `" 1"`, `" 2"` (= start.exe, vgame.exe, end.exe) | 1 = program found (continue). DSWAP returns 0 only after a fatal memory error (**SETTLED C7**) | runs the program named by `n` | frontend.md:195-201, :237; RE_GUIDE 2.1 |
| START | empty | 0 = quit (Y at the quit prompt, or fatal error). 0Ch = fly, with PLAYER `" title-2"` first. 0Dh = fly, no title-2 (site flag 0200h, "no hangar" start) | 0Ch: DSWAP `" 1"`, then PLAYER `" title-2"`, then VGAME. 0Dh: DSWAP `" 1"`, then VGAME | shell.md:76, :143; RE_GUIDE 2.1 |
| VGAME | empty | 0 = quit (includes code 7 abort). Anything else = debrief | DSWAP `" 2"`, END | RE_GUIDE 2.1; simflow.md:1733-1738; debrief.md:81 |
| END | empty | 23h = back to START through DSWAP `" 0"`. 0 = quit (quit prompt, Ctrl-Break) | DSWAP `" 0"`, START | debrief.md:36, :192; RE_GUIDE 2.1 |

Quit = far call to the sound driver's shutdown entry, then `INT 21h/4C00h` (RE_GUIDE 2.1). The comm block and the overlays
stay resident across all EXECs, so every hand-off in sections 2-6 is in memory, never through the EXE image.

## 2 The comm block (5120 bytes, shared)

`shell.md:6` lists the corrections to RE_GUIDE 2.4 (the pointers and BIOS words below use the corrected values). BIOS
words and the pointer:

| Address | Writer | Readers | Meaning | Cite |
|---|---|---|---|---|
| `0000:04F0` | F117.COM (allocates the block) | every program at start-up | segment of the comm block; each program keeps a far pointer to it (SETUP `FD6/FD8`, START `DS:E096`, VGAME `DS:E548`, END `DS:7222/7224`) | RE_GUIDE 2.1 step 1, 2.4; frontend.md:28; shell.md:1054, :1241; simflow.md:53; debrief.md:15 |
| `0000:04F2` | F117.COM = 1; START clears it (shell.md:1089, far pointer `CBD8` in the table at :1377) | START | first-run word. START goes to the roster screen first while it is non-zero | RE_GUIDE 2.4; shell.md:102, :116, :1089, :1377 |
| `0000:04F4` | F117.COM = 0; START sets 1 after MGRAPHIC slots 69/60 | START | one-time initialisation flag | RE_GUIDE 2.4; shell.md:1241 |

Block fields (offset in hex). "w" = 16-bit word, "b" = byte. Where a writer is "START" the write is in the program's ui_main
or the screen named in the cite.

| Off | Size | Written by | Read by | Meaning | Cite |
|---|---|---|---|---|---|
| 00 | char[13] | SETUP | F117.COM (graphics driver load) | graphics driver file name `"Mgraphic.exe"` | frontend.md:68; RE_GUIDE 2.4 |
| 0D | char[13] | SETUP (menu 1-4) | F117.COM (sound driver), MPS_LOGO (letter `& 5Fh`) | sound driver name, e.g. `Asound.117`. MPS_LOGO and PLAYER map the letter to a `.LOG` file (see section 3) | frontend.md:69-71, :82, :91-93; RE_GUIDE 2.1 row 2 |
| 1A, 1C, 1E | w | F117.COM | every program | segments of MGRAPHIC, sound driver, MISC | RE_GUIDE 2.1 row 3, 2.4 |
| 20 | w | F117.COM | VGAME (read) | off-screen page segment (MGRAPHIC slot 0) | RE_GUIDE 2.4; simflow.md:642 |
| 22 | w | SETUP = 1 (frontend.md:54, :77); the dead quiz (`070D`, `073E`, `074F`) | START `5B3B`, `5C85`, `6328`, `6D91` | clearance flag, always 1 in a real run (0 would force theatre 0 and `pilot+3Ch \|= 2`). **SETTLED G3** | HANDOFFS section 8 |
| 24 | b | SETUP = 0 (never written otherwise, frontend.md:63-65) | START passes it to MGRAPHIC slot 60 | joystick calibration byte | RE_GUIDE 2.4; frontend.md:61-65 |
| 26 | w | SETUP = 1 (`04BF`, unused); START = 3 (`0035`); VGAME: 1 at flight start (`39C4:3B0F`), 2 on eject (`D028`), 3 on a normal landing (`4961`) | END (`0032`, `007D`, `08FC`, ...) | **mission state**: 1 = the flight ended any way other than landing or eject (crash/killed), 2 = ejected, 3 = landed. **SETTLED C1, C2** | RE_GUIDE 2.4 row 0026 (corrected) |
| 28 | w | VGAME `mission_end` = `4926(code)` (`494F`); START = 0 (`003F`) | END | end code: 0 landed (or the ejected pilot reached the ground), 1 stranded, 2 terrain, 5 hard landing, 6 fatal crash; 7 writes nothing. **SETTLED C3** | simflow.md:1730-1746 |
| 2C | w | START = 0 (`0049`); VGAME increments (`5433` SAM firing, `6586` air-to-air launch) | END (`< 3` = quiet mission quips) | **enemy missiles launched at the player**, not kills. **SETTLED Q3** | flight.md:214; simflow.md:2429 |
| 30 | w | START (`005E` / `006A`: `pilot+3C > 1`); VGAME bit 0 = Alt-T training toggle (`CE08`); SETUP's 0 (`04CE`) is overwritten | END (`0010`, `0084`, ...) | **non-scoring flag**: 0 = the mission counts (`pilot.missions_flown++`). **SETTLED C4** | |
| 32 | w | SETUP: FFFFh = unset, 3 = MCGA, else 2 (frontend.md:74) | VGAME `[98DE]` (simflow.md:642) | adapter type | RE_GUIDE 2.4 row 0032 |
| 33 | b | START (debrief.md:53) | VGAME (simflow.md:642), END clock (debrief.md:53) | 1 = night mission | debrief.md:53 |
| 34, 36 | w | VGAME `mission_end` (simflow.md:642; debrief.md:53) | END (debrief.md:53) | damage bit mask (34) and hits taken (36) | debrief.md:53 |
| 38, 3A, 3C, 3E | 4 w | START (weapon ids per bay, shell.md:184; flight.md:426) | VGAME (simflow.md:642), END event 4 | weapon id of bays 0-3 | shell.md:184; flight.md:426; debrief.md:53 |
| 40, 42, 44, 46 | 4 w | START (quantity per bay, shell.md:184; flight.md:426) | VGAME (simflow.md:642) | quantity per bay | shell.md:184; flight.md:426 |
| 2E | w | START (`4F51`: `12 x route score`) | START briefing (`242A`) and arming screen (`0904`); END (`361B`: bands 7000 / 9000 / 11500) | **estimated fuel in lbs** (not payload, not a cost). **SETTLED C10** | shell.md:770, :857 |
| 48 | 40 B | SETUP (joystick calibration, frontend.md:61) | VGAME r/w (simflow.md:642), END (`joy_set_calib`, debrief.md:206) | joystick calibration block | RE_GUIDE 2.4 row 0048 |
| 70 | w | SETUP = 0 (`01A6`) | nobody | unused. **SETTLED G2** | frontend.md:54 |
| 72 | w | SETUP: bit 0 = joystick (answered Y), bit 1 = mouse (frontend.md:56) | VGAME bit 0 (simflow.md:642, :1087), START (shell.md:771), END (debrief.md:206) | input devices | RE_GUIDE 2.4 row 0072 |
| 74, 76 | w | VGAME `mission_end` (final x, y; simflow.md:5121) | END (grid cell = `>> 11`; landing class, debrief.md 1.6) | final position in level units | debrief.md:53; simflow.md:5121 |
| 7A.. | blob | START `wld_export_to_comm` (shell.md:879); VGAME `mission_world_export` `4C29` writes it back with the event log (simflow.md:181, :221) | VGAME `4AD0`/`4C41` (simflow.md section 4); END `0113` (debrief.md:83-96) | the mission world plus the 0x600-byte event log. See section 4 | |
| 1272 | 50 h | START (roster screen, options and mission setup edit it, shell.md:1058-1073) | VGAME (read, simflow.md:53), END read and write (debrief.md:128-150) | active pilot record. **SETTLED C5**: VGAME only reads (all 26 accesses through `DS:98F4` are loads); START and END write | shell.md:1058-1073 |

## 3 Files on disk

| File | Written by | Read by | Notes | Cite |
|---|---|---|---|---|
| Driver files named in comm 00h and 0Dh | not written | F117.COM (the sound driver named in comm+0Dh, RE_GUIDE 2.1 row 3), MPS_LOGO (`*.LOG` chosen by letter: R, A, I, T, N) | Shipped: `ASOUND.LOG`, `ISOUND.LOG`, `RSOUND.LOG`. No `TSOUND.LOG`, so T fails with message 03 | frontend.md:82-93; RE_GUIDE 2.1, 2.4 |
| `ROSTER.FIL` (coded `"Roster.Fil"`) | **START only**, `roster_save` (`7272`) | START `roster_load` (`7218`) at start-up | 802 bytes: a word (selected index, `DS:B39C`) + 10 records of 50h. Matches the RE_GUIDE 2.4 open point. Save callers: roster screen finish `623E` (shows errors), options exit `6DB8`, `c.txt 5DFE`, silent sync `6D07` at every START run, and the Fly button (sel 17) | shell.md:1177-1184 (1.8), :921, :1175 (1.7), :1058 |
| `<prefix>.wld`, `<prefix>.3dG`, `<prefix>.3dT` (theatre `lb pg nc ce vn me ku cu ko`) | not written | START `wld_load_theatre` (`5466`), `map_load_3dg` (`7D71`), `map_load_3dt` (`7E88`); VGAME (`.3dG`, `.3DX`, `.3dT`) | Prefix table `DS:0D6A`. VGAME does not read a `.wld` (simflow.md:377) | shell.md:756, :877-878; simflow.md:321-349 |
| `TEMP.WLD` | **never written.** START `5625` pushes the name (`DS:0042`) but never reads it; it only copies into `comm+7Ah` (`588D` = `movedata`) | nobody (VGAME does not read it, simflow.md:377) | **SETTLED C6** | shell.md:879 |
| Preferences file | none: SETUP cannot create files (its only `dos_creat` is behind the dead wrapper `0D65`) and F117.COM has no create opcode | none | **SETTLED G1**: SETUP asks its questions on every run and keeps the answers only in the comm block | frontend.md:55-71, 8.2 |
| `SWAP.PIC`, `SWAP.PAL` | not written | DSWAP picture (not shown in the port) | "unused data" in the port | frontend.md:198, :201 |
| Event log | none (no file) | END (`DS:5E38`) | 256 records x 6 bytes = 0x600. It is the tail of the `comm+7Ah` blob | debrief.md:105; simflow.md:5666 |
| `PAN`, `SS`, `PIC`, `LOG` assets | not written | MPS_LOGO, PLAYER, START, VGAME, END (read-only) | not hand-off data | frontend.md §2-3; shell.md; debrief.md |

Writers of files in the chain: START writes `ROSTER.FIL`. END writes no file (debrief.md:5-6; its file writer `10C1` is never
called, debrief.md section 10). SETUP writes no file. VGAME's `file_load_paras` opens with `"r+b"` (simflow.md:308) but only
reads, so the port should open read-only (simflow.md:308).

## 4 START to VGAME: the world blob at `comm+7Ah`

START `wld_export_to_comm` (`5625`, shell.md:879, verified) writes this in order, with `wld_pack_put(src,size,count)`
(`588D`):

1. `D88C` 2 bytes: theatre object ids. simflow.md section 4 splits this into `DEA4` and `C0AC` (1 byte each, same two bytes)
2. `D33C` 2: site count (`nSites`)
3. `CACE` 2: leading sites with a threat level
4. `E084` 2: first non-target site index
5. `CBDE` 16 x nSites: site records (x, y, threat, flags, unit type, count, object id)
6. `D890` 2: unit count (`nUnits`)
7. `D3BE` 36 x nUnits: unit records
8. `D7F6` 128, `D33E` 128, `DD70` 750, `CAD8` 256, `D876` 2, `CABC` 2, `15B2` 4 x 4, `E05E` 2 x 18

`D2A6` is not exported (shell.md:879). VGAME's `4C41` reads the same order back (simflow.md section 4; the blob starts with the
same theatre ids and site count). END's `0113` reads every field in the same order (it has to, to reach the event log) and
keeps only the sites and the log; the unit fields are read and unused (debrief.md:83-96). The event log (`DS:5E38`, 256 x 6 bytes, a record with type 0 ends it) is appended by VGAME `4C29` at
export (simflow.md:5666; debrief.md:105).

## 5 VGAME to END: the mission result

END reads the result from the comm block and the blob. Dispatch table settled in debrief.md 1.2 (rows are `comm+26h`, `comm+28h`):

| 26h | 28h | Raised by (VGAME) | END screen |
|---|---|---|---|
| 3 | 0 | normal landing (`4926(0)`, not ejected) | landed safely |
| 2 | 0 | eject survived (`cba0` sets 2, then `4926(0)`) | rescue if the final grid cell is friendly, else captured |
| 1 | 1 | stranded outside an airfield (`39c4` 41b6) | cemetery: crashed spectacularly |
| 1 | 2 | terrain collision (`39c4` 41f8) | exploded on a hill |
| 1 | 5 | hard landing / gear failure (`1e9e` 2b65) | crashed on the runway |
| 1 | 6 | fatal crash, eject failed (`cba0` D01E) | eject attempt failed |
| 1 | 3, 4 | never raised by VGAME | texts exist |
| 1 | 0 | impossible in a normal run | (B) prints garbage |

Other VGAME to END fields: `comm+2Ch` missile-launch counter; `comm+34h/36h` damage mask and hits; `comm+74h/76h` final x, y; the blob
with the event log. VGAME's per-type kill counters (`DS:6448`, 128 bytes) are not read by END (debrief.md:98, "unused"), so
only `comm+2Ch` crosses. `comm+26h = 1` means "did not end in a landing or an eject" (SETTLED C2), and `comm+28h = 0` is "landed" (SETTLED C3);
`comm+2Ch` counts enemy missile launches, not kills (SETTLED Q3).

## 6 END to START: the pilot record and exit 23h

END returns 23h (debrief.md:36, :192). The chain then runs DSWAP `" 0"` and START (section 1). END writes the active pilot
record in the comm block, at `comm+1272h` (debrief.md 1.5, table at :128-150):

| Pilot offset | END writes | Meaning |
|---|---|---|
| +20 | `++` on promotion | rank 0-6 (2nd Lt ... Brig Gen) |
| +22 | = 1 | Purple Heart |
| +24, +26, +28, +2A | `++` | Airman's Medal (max 9), DFC, Silver Star, Air Force Cross |
| +2C | = 1 | Medal of Honor |
| +2E | max | best mission score |
| +30 | = score (0 if `comm+30h` says training) | last mission score |
| +32 (dword) | `+=` score | career score |
| +36 | `++` | missions flown |
| +44 | = 1 | Combat Readiness Ribbon |
| +46, +48, +4A, +4C | = 1 / 0 | OverSeas short and long, Longevity, Longevity gold cluster |
| +4E | = 1 or 2 | 1 retired (mission 99), 2 KIA. START opens the roster screen when non-zero |

END reads (does not write) +38 theatre, +3A war level, +3C mission type, +3E opponent level (C8), +40 landing realism (crash
mode in START's naming, shell.md:88), +42 aircraft model. START's copy: `6D07` copies the active record into its roster slot
and calls `roster_save(0)` at every START run (shell.md 1.7, :1170-1175), so END's changes reach `ROSTER.FIL` on the next START
run. The ROSTER screen does not show +44 to +4C (debrief.md:149).

## 7 CONFLICT marks (all settled 2026-10-13, S3.10 closing pass)

Every conflict was settled from the disassembly (addresses below); the wrong spec text or `RE_GUIDE.md` row carries a
"(corrected 2026-10-13, see HANDOFFS Cn)" note. Scripts that found the accesses: `work/s310b/` (a full `fndis` listing of
SETUP, START, VGAME, END grepped for `es:[bx+N]`).

| Mark | Topic | Verdict (code evidence) | Fixed in |
|---|---|---|---|
| C1 | Name of `comm+26h` | **SETTLED C1: mission state.** Writers: SETUP `04BF` = 1 (an initial value nobody reads), START `0035` = 3, VGAME `39C4:3B0F` = 1 (first frame of a flight), VGAME `D028` = 2 (eject), VGAME `4961` = 3. Only END reads it (`0032`, `007D`, `08FC`, `090B`, ...). It is not an options word | RE_GUIDE 2.4 |
| C2 | Meaning of `comm+26h = 1` | **SETTLED C2: both are right.** VGAME sets 1 when the flight starts, and every crash path (`4926(1/2/5/6)`) leaves it at 1; only `4926(0)` with no eject writes 3 (`4955..4961`) and the eject handler writes 2. So 1 means "the flight did not end in a landing or an eject" and END reads it as crashed/killed | simflow.md:58; debrief.md:49 |
| C3 | Meaning of `comm+28h = 0` | **SETTLED C3: debrief is right.** `4926(0)` has two callers: `414E` (the landing timer `[3D9A]` ran out: landed) and `23FF` (the ejected pilot's height reached 0). With `[C06E] == 0` it also writes `comm+26h = 3` (landed); after an eject `26h` stays 2. (26h = 1, 28h = 0) never reaches END: code 7 (the Y answer of the quit prompt, `20BD`) writes nothing and VGAME exits with 0, which quits the chain | simflow.md:835, :848 |
| C4 | Owner and meaning of `comm+30h` | **SETTLED C4: non-scoring flag.** START main writes it on every run (`005E` = 1 when `pilot+3C > 1`, else `006A` = 0), overwriting SETUP's 0 (`04CE`, the always-0 joystick flag). VGAME `CE08` ORs 1 (Alt-T). END `0010..001F` increments `pilot.missions_flown` only when it is 0. Nothing reads it as a joystick flag | RE_GUIDE 2.4; frontend.md:63 |
| C5 | Does VGAME write the pilot record? | **SETTLED C5: no.** VGAME holds the pilot pointer in `DS:98F4` (set to `comm+1272h` at `0033`); all 26 accesses through it are loads. START and END write the record | RE_GUIDE 2.4 |
| C6 | Does START write `TEMP.WLD`? | **SETTLED C6: no.** `5625` has no stack frame and never reads its argument `DS:0042` (`"temp.wld"`, pushed at `0029`); it only calls `588D`, which is `movedata` into `comm+7Ah`. No `fopen`/`creat` of that name exists in START | shell.md:63, :74, :347 |
| C7 | Can DSWAP return 0? | **SETTLED C7: only after a fatal memory error.** `dswap_exit(0)` is called from the `mem_alloc_chk` failure (`04B4`) and the `mem_free_chk` failure (`04D8`). The Ctrl-Break check `030A` is never called, and the key loop only ever exits with 1. On a working machine DSWAP returns 1 | RE_GUIDE 2.1 |
| C8 | Range of pilot `+3E` | **SETTLED C8: 0..3.** The only writer of `pilot+3E` is the options screen: `6FEB` stores `button - 1` for the four skill buttons (`6FB4..6FDB`: hotspots 1..4). debrief.md is right; the `rand(5)+cabe` and `cabe-5` in shell.md:766 are expressions inside `4239`, not the range of the field | shell.md:766 |
| C9 | Symbol names at the same address | **SETTLED C9** (section 9.3): one name per address | the CSVs |
| C10 | Meaning of `comm+2Eh` | **SETTLED C10: estimated fuel in lbs** (`12 x route score`, START `4F51`; `> 10000` also sets `comm+3Eh = 11h`). The briefing prints it as "Est. Fuel required: %d lbs." (`242A`, string `DS:07CC`) and the arming screen prints the same number with `%d lbs.` (`0904`). It is neither payload nor a cost. END compares it with 7000, 9000 and 11500 (`361B..3651`) | shell.md:184, :192, :335; RE_GUIDE 2.4 |

Not a conflict, but checked: the blob's first field is 2 bytes in shell.md:879 and 1 + 1 in simflow.md section 4 (same bytes).
`comm+0Dh` holds `Asound.117` (SETUP) and MPS_LOGO reads the letter for `.LOG` (frontend.md:82, :91-93). These are two
different file types, not a conflict.

## 8 GAP marks and open hand-off questions (all answered 2026-10-13)

* **SETTLED G1: there is no preferences file.** SETUP cannot create a file: its only `INT 21h/3Ch` (`dos_creat`, `0E1E`) is
  reached only through the dead wrapper `0D65` (`frontend.md` 8.2), it has no `fopen`, and `F117.COM` contains no
  create/open-for-write opcodes. The joystick, sound and graphics answers live only in the comm block and are asked again on
  every run. Port: keep it that way (or add a config file as an Enhanced feature).
* **SETTLED G2: `comm+70h` is dead.** SETUP writes 0 (`01A6`); no program (START, VGAME, END; full-listing grep for `+ 0x70]`
  on `es:`) reads it. Port: ignore.
* **SETTLED G3: `comm+22h` = clearance flag.** SETUP writes 1 (`0195`); only the dead ID quiz (`0703`, writes at `070D`,
  `073E`, `074F`) ever clears it. START reads it at `5B3B` (`ui_orders_draw`, swaps two hotspot tables), `5C85` (`ui_orders_screen`), `6328`
  (`roster_screen_run`, new pilot) and `6D91` (`roster_startup_sync`): a value of 0 sets `pilot+38h = 0` (theatre 0) and
  `pilot+3Ch |= 2` (training only) at the last two. In a real run it is always 1. Port: constant 1.

Open hand-off questions:

1. **END quit loses the pilot update. SETTLED: yes, and the port keeps it (original behaviour).** END's quit prompt (`3E61`, Alt-Q
   then `Y`) and the Ctrl-Break path call `end_exit(0)` (`3F44`), which calls `exit(0)`: F117.COM quits the whole chain. The
   debrief edits the pilot record only in the comm block in memory. `ROSTER.FIL` is written by START alone (`roster_save`
   `7272`, called by `roster_startup_sync` `6D07` at every START run); the string `Roster.Fil` occurs in no other program, and END's
   file writer is dead. So the pilot update of that mission (rank, medals, score, missions flown, retirement) is discarded
   when the player quits from the debrief. The port must not save on quit.
2. **Which program reads `comm+30h` as the joystick flag? None** (C4). START does not even read it; END and VGAME use it as the
   non-scoring flag.
3. **Which kill counter is `comm+2Ch`? None: it counts enemy missile launches at the player.** The two increments are in
   `det_site_radar_update` (`516D`, `5433`, after building the message "<site> firing <SAM>" and filling a SAM slot) and
   `ai_unit_radar` (`62D0`, `6586`, after "<weapon> fired by <aircraft>"). START clears it (`0049`). END's `< 3` test therefore means
   "fewer than 3 missiles were fired at you". Corrected in simflow.md and debrief.md.
4. **Does DSWAP ever exit 0?** Only after a fatal memory error (C7). The RE_GUIDE rows keep `0 -> quit` as F117.COM's rule; a note was added.
5. **Does the original START write `TEMP.WLD`? No** (C6).
6. **Pilot `+3E` range: 0..3** (C8).
7. **RE_GUIDE 2.4 corrected:** rows 0022, 0026-002E (now one row per field), 0030, 0070 and 1272 were rewritten with the notes above.

## 9 Coverage gate

**Closing pass 2026-10-13: every program is now at 100 % (1 347 / 1 347; see 9.3).** The tables and the lists of missing functions below describe the state after S3.10, before the closing pass.

Command: `python tools/spec_coverage.py` (default is now the seven F-117A programs; the Aces names were removed, as
`platform.md` 8 asked).

| Program | Before | After | Mapped twins | Still missing |
|---|---|---|---|---|
| SETUP | 60 / 105 | **67 / 105** | 7 (6 exact, 1 near) | 38 |
| MPS_LOGO | 76 / 117 | 76 / 117 | 0 | 41 |
| PLAYER | 73 / 111 | **76 / 111** | 3 (exact) | 35 |
| START | 254 / 267 | **261 / 267** | 7 (exact) | 6 |
| VGAME | 450 / 450 | 450 / 450 | - | 0 |
| END | 196 / 196 | 196 / 196 | - | 0 |
| DSWAP | 74 / 101 | **94 / 101** | 20 (exact) | 7 |
| Total | 1 183 / 1 347 | 1 220 / 1 347 | 37 | 127 |

Method (`tools/twin_map.py`, run with `--apply`): every immediate, displacement and branch target is masked, and the
instruction sequences are compared. An exact twin has an equal masked sequence. A near twin has a difflib ratio of at least
0.9 among candidates that share at least half of their 6-instruction windows and are within 15 % in length. Partners must be
covered by a real spec row (never the phase-1 seed names). Accepted twins are appended as `func` rows to
`frontend_symbols.csv` (SETUP, MPS_LOGO, PLAYER, DSWAP) or `shell_symbols.csv` (START), with the partner's name, signature,
and a note `(= EXE SEG:OFF, ...)`. This follows `debrief_symbols.csv` (END's START-toolkit copies). Twins are shape matches,
so 11 of the 37 are 1 to 6 bytes (`ret` stubs, `free` and `ltoa` thunks); their names are the partner's, not verified.
The per-function list is `work/coverage/twins.csv`.

Notable twins: SETUP `0000:0582` = START `ui_text` (`3567`); DSWAP's PIC and LZW decoders (`08F4` to `0BCF`) are copies of
START's video helpers (`pic_to_page` `86A8`, `lzw_next` `8983`, and others); DSWAP's `0FF8` and `2060` are copies of VGAME's
`free` (`FDE2`).

Still missing, after mapping (`work/coverage/remaining.txt`: address, size, first direct callers, and the uncovered function
with the same code if there is one). 59 of the 127 have no direct call site in their own program, so they are reached through
function tables or the driver interface.

* **SETUP, 38 functions, 2 363 bytes**, all in segment 0000. No twin and no pair anywhere. This is SETUP's own code, outside
  `frontend.md` section 1.
* **MPS_LOGO, 41 functions, 2 923 bytes**: 0000 (2), 0045 (4), 006D (1), 00DA (11), 0146 (23). Three are the same code as
  uncovered functions in other programs: `0146:049A` = START `932E` (near 0.977), `0146:1D66` = PLAYER `257B` (near 0.950),
  `0146:1DB6` = PLAYER `25C7` (exact).
* **PLAYER, 35 functions, 2 307 bytes**: 0000 (32), 026B (3). `257B` and `25C7` are the MPS_LOGO pairs above.
* **START, 6 functions, 579 bytes**: 0000 (6). `932E` is the MPS_LOGO pair above.
* **DSWAP, 7 functions, 252 bytes**: 0000 (7). No pair.

A Sonnet pass on the 127 functions (two or three sessions, the MPS_LOGO and PLAYER pairs in one session each) would close
the gate. This is the decision asked of the user in PLAN.md.

### 9.2 Symbols merge (`port/symbols.csv`)

`tools/merge_symbols.py` (already in the repo from S1.5, extended in S3.10) merges every `port/spec/*_symbols.csv` into
`port/symbols.csv`. Columns: `exe,kind,address,name,type,owner,other_names,notes` plus **`spec`** (the file that won) and the
key is `(exe, kind, address)`. Priority: platform, video, sound, render3d, flight, simflow, shell, frontend, debrief, then the
phase-1 seed names (last, so a seed name never beats a spec). Kinds: `func`, `global`, and `label` (a code address inside a function). MGRAPHIC `slot` rows become
`func`. The sound drivers `asound117` and `asound.log` were added as exes.

Result: **2 544 rows** (`vgame` func 450, global 618, label 203; `end` func 196, global 115; `start` func 263, global 184;
`dswap` 94; `setup` 67; `player` 76; `mps_logo` 76; `mgraphic` 77; `asound117` func 89, global 12; `asound.log` 19;
`misc` 5). 0 rows skipped.

Clashes, in `work/coverage/symbol_clashes.txt` (nothing is picked silently; the losing names are in `other_names`):

* 25 clash lines: 12 `DS:` addresses with two names (the C9 list), and 13 names used at several addresses in one exe.
  The 13 include the twins' copied names (`free` in DSWAP and START, `load_pic_seg_a`, `pic_to_seg_a`, and 9 MGRAPHIC slots
  named `nop`). These are real duplicate functions, and gen_symbols.py disambiguates them when it writes the header.
* 134 duplicate keys: 114 are the phase-1 seed rows superseded by a spec, 11 are the render3d/simflow disagreements above,
  2 are the two shell `DS:` addresses, and 7 are rows repeated inside `video_symbols.csv`.

The S4.2 generator (`tools/gen_symbols.py`) reads these columns (`exe`, `kind`, `address`, `name`, `type`, `owner`) and ignores
`label` rows. It is still written for the Aces exes (INTRO, PS, AP), so its exe list must change for the F-117A port (S4.2). The old `port/symbol_conflicts.txt` was removed (its content is now in
`work/coverage/symbol_clashes.txt`).

### 9.3 Closing pass (2026-10-13): 100 % and the symbol clashes

`python tools/spec_coverage.py` now reports **1 347 / 1 347** for all seven programs (the tool also prints, per program, how many
of the covered functions are index-level `dead` rows; it counts them as covered). The 127 functions left by S3.10 were
resolved in one pass:

| Program | Left | Full section | Mapped to an existing function | Dead |
|---|---|---|---|---|
| SETUP | 38 | 3 | 2 | 33 |
| MPS_LOGO | 41 | 9 | 17 | 15 |
| PLAYER | 35 | 13 | 12 | 10 |
| DSWAP | 7 | 5 | 0 | 2 |
| START | 6 | 0 | 5 | 1 |
| **Total** | **127** | **30** | **36** | **61** |

Where: `frontend.md` section 8 (SETUP, MPS_LOGO, PLAYER, DSWAP) and `shell.md` Part E (START). Reachability was checked with the
direct call graph, operand and data-word scans, and a BFS from the entry point (`work/s310b/refs.py`, `reach.py`). Besides the
dead code: DSWAP `030A` (an unindexed dead Ctrl-Break check) has a row too.

**Twin rows verified.** Of the 37 S3.10 twin rows, the eleven that are only 1-6 bytes long were read: `rtl_dosmaperr_al` (PLAYER
`1890`, START `98CA`, DSWAP `140C`) and START `A76C` `free` are right. Seven were wrong and are corrected in
`frontend_symbols.csv` / `shell_symbols.csv`: `1C46` (SETUP), `222A` (PLAYER), `2060` (DSWAP) and START `A768` are `nmalloc`
(3-byte `jmp rtl_nmalloc`), not `free`; DSWAP `0FF8` is `atoi`; the 1-byte stubs START `885E` and DSWAP `0AAA` are
`pic_hook_nop`. Two larger rows were also wrong because the matcher took the first of several masked-identical partners: DSWAP
`0A42` is `pic_to_seg_c` (driver slots 52/54, START `87F6`), and `03FB` is `load_pic_seg_c` (END `1061`).

**Symbol clashes (C9, settled).** The 12 DS addresses that had two names now have one:

| Address | Name kept | Dropped (now in `other_names`) | Why |
|---|---|---|---|
| VGAME `DS:0510` | `r3d_scenery_map` | `bank1_objid` | index is `id & 7F` of the scenery id; read by `r3d_scenery_id` |
| `DS:061E` | `terr_rec_count` | `3dt_rec_count` | the second is not a legal C identifier |
| `DS:0D22` | `hz_sky_offs` | `view_band_w` | read by `r3d_draw_sky_ground` and the MFD sky/ground bands |
| `DS:43B6` | `r3d_haze_on` | `alt_a_flag` | the Alt-A key sets it, the depth shading reads it |
| `DS:49E0`, `49E8` | `r3d_sx`, `r3d_sy` | `scope_x`, `scope_y` | result of the projection `B85D`; read by 3D, HUD, scope and ILS code alike |
| `DS:85E2`, `85EA` | `gfx_pages`, `gfx_seg` | `page0_seg`, `cur_seg` | page segment array (element 0 = A000h) and current draw segment |
| `DS:C688` | `r3d_vtx3d` | `lzw_table` | flight use; the same RAM is the PIC decoder's LZW dictionary while a picture loads (noted in the row) |
| `DS:DE92` | `r3d_depth` | `scope_outside` | written by `B85D`, `90B9`, `BA67`; the scope only reads a flag derived from it |
| START `DS:0CBA` | `tbl_pic_names` | `screen_name_tbl` | duplicate row removed; `shell.md` text now says `tbl_pic_names` |
| START `DS:0D6A` | `theatre_prefix_ptrs` | `theatre_suffix_tbl` | the strings are file-name prefixes (`lb`, `pg`, ...); duplicate row removed |

The rule used: the render3d name wins, because render3d is the main reader (for most also the writer) of those VGAME words;
the data rows keep the old name in `other_names`. `port/symbols.csv` was regenerated by
`python tools/merge_symbols.py` (2 672 rows, 0 skipped).

**Multi-address names (now 7 lines).** The 13 names of S3.10 reduce to: the nine empty MGRAPHIC slots named `nop` (identical
`retf` stubs), and in VGAME `mat_mul` (near copy `E578`, far copy `1433:0286`), `edge_L` / `edge_R` (render3d Part 6 tables of 220
words at `22A1` / `2459` against the Part 5 tables of 480 words at `89AC` / `8D6E`), `r3d_far` and `r3d_origin` (Part 1 against
Part 4 variables) and `r3d_plane_shade` (function `11EB:0678` against the data `DS:77D2`). They are different objects, so
`gen_symbols.py` (S4.2) must suffix them; no spec row needed changing. The per-program copies of the SETUP / DSWAP / START
twins disappeared with the corrections above (`free`, `load_pic_seg_a`, `pic_to_seg_a`). Two real mistakes were fixed on the way:
`DS:294A` is `hud_mode` (it was also called `hud_style`, the name of `DS:294B`), and `DS:3678` is `wpn_table_type` (a field of the
`wpn_table` rows at `366C`).
