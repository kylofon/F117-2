# Front end: MPS_LOGO, SETUP, PLAYER, DSWAP, MISC (S3.4)

**Status: S3.4 delivered; S3.10b closing pass added section 8 (every indexed function of the four programs is now covered; `frontend_symbols.csv` has 208 rows, the first 56 from `python tools/frontend_syms.py`).**
Disassembly tools: `python tools/dsann.py EXE DGROUP_SEG FROM TO` (fndis + DGROUP string annotations) and
`python tools/rawdis.py work/unpacked/EXE.EXE SSSS:OOOO LEN`. DGROUP segments: SETUP 024C, PLAYER 02A3,
MPS_LOGO 0332, DSWAP 0225 (RE_GUIDE 2.3). Overlay loader, driver header, comm block, exit codes and the chain:
`port/RE_GUIDE.md` 2.1-2.4 (not repeated). C runtime / MicroProse layer / timer / joystick / DOS wrappers:
`platform.md`. Video driver: `video.md`. PAN / SS file formats: `RE_GUIDE.md` 4, `FORMATS.md`.
Confidence: **verified** = read in the disassembly; *(U)* = needs a visual/aural check.

## 0 What the port must reproduce

| Program | Port entry | Needs from other modules | Visible result |
|---|---|---|---|
| SETUP | `int setup_main(void)` | comm block, MISC, joystick calibration | text-mode questionnaire, fills comm |
| MPS_LOGO | `int logo_main(char drv)` | SS decoder, DAC, BIOS tick, sound driver `.LOG` | LABSLOGO animation + jingle |
| PLAYER | `void player_main(names[])` | PAN decoder, DAC, timer tick, sound cmds 2/3 | title/credit animations with fade-in |
| DSWAP | `int dswap_main(n)` | none | **no-op, returns 1** (hard-disk install assumed) |
| MISC | in the host platform layer | - | keyboard / joystick-button thunks |

Everything is a straight sequential program; there is no state shared with the later programs except `comm`
(SETUP) and the exit codes (RE_GUIDE 2.1). All text below is verified unless marked.

## 1 SETUP (`SETUP.EXE`, image 13 437 B, DGROUP 024C)

Start-up (`0000:0010`):

1. `comm = *(far*)0000:04F0` (kept at DS:FD6/FD8); `[FD0/FD2] = comm:120E` (SETUP's own far pointer, unused).
2. Loads `misc.exe` with the overlay loader (`0A9F`, RE_GUIDE 2.2) and binds it (`0BA2`, slots 90-95 at the
   vector table DS:08A6). Installs the Ctrl-Break hook (`0C35`; handler sets `[0AC8]`; `0503` polls it and
   exits with 0). **Port:** no overlay; MISC is built in (section 5).
3. `fcb_find` (`0C84`, platform.md 3.1) is a floppy test: non-zero -> prints row 22
   `You cannot play F117A off of 720K floppy disks!!` and `exit(0)` (the chain then quits). **Port:** treat as 0.

Screen (text mode 3, 80x25, BIOS teletype writer `14BE(row,col,str)`; a byte `FFh` inside a string means "next byte =
attribute"; the box is CP437 `╔═╗║╚╝`). Attribute 1Fh (white on blue) is set by the row-0 string, 07h by `*** NOTE ***`,
0Eh (yellow) by prompts. Rows (all verified, text in `DS:00C8..0760`):

| row,col | text |
|---|---|
| 1-8, 0 | box: blank / `Night Hawk F-117A Stealth Fighter 2.0 - Version 473.03` / blank / `Program & Audio-visual Copyright (C) 1991 by MicroProse Software,Inc.,` / `All Rights Reserved.` / blank / bottom border (each row 79 chars, text centred) |
| 10,32 | `*** NOTE ***` (attr 07) |
| 12,18 | `F117A works best if played from a hard disk.` |
| 14,21 / 15,21 | `It can be played from 1.2 Mb floppies,` / `but we recommend you use backup disks.` |
| 17,15 / 18,18 | `The game CANNOT be played from 720Kb floppy disks;` / `it must be installed on to your hard drive.` |
| 20,2 | `Instructions for game installation can be found in the Technical Supplement.` |
| 22,0 | prompts (attr 0E): `Do you have a joystick (Y/N) ?`, `Center joystick, then press fire button 1`, `Move joystick to upper left corner, then press fire button 1`, `Now move joystick to lower right corner, then press fire button 2` |
| 13,0 | `Select Sound Driver:` (attr 0E) |
| 15-18, 0 | `1) IBM Sound`, `2) Ad Lib Sound Board`, `3) Roland Sound Board`, `4) No Sound` (indented 24 columns) |
| 16,29 | `One Moment Please...` |

Flow after the banner (comm = the pointer above; `comm+N` as in RE_GUIDE 2.4):

1. Initialise: `comm+22h = 1`, `comm+24h = 0`, `comm+70h = 0`, `comm+32h = FFFFh`, `comm+2Ah = 0`; `joy = 0`.
2. **Joystick question** at row 22: loop `c = getche()` (MISC slot 92). `c == 100h` -> `exit(0)`
   (Ctrl-Break also exits via `0503`). `Y/y` -> `comm+72h = 1`, `N/n` -> `comm+72h = 0`; any other key re-asks.
3. If yes (`comm+72h & 1`): `joy = 1`, `timer_install` (`0FC0`); "Center joystick" -> wait for fire button 1
   (`joy_button(0)` loop = slot 93), `joy_calib_center` (`0237:002C`), `wait_ticks(2)` (`0A4B`: ~2 timer ticks) then wait
   for the button to be released (slot 93 loop on 0 again); "Move to upper left" -> loop `joy_calib_track`
   (`0237:004D`) until button 1 is pressed; "lower right" -> same until button **2** (slot 93 arg 1);
   `joy = comm+24h (byte)`; `joy_calib_get` copies the 28h-byte calibration block to `comm+48h`.
   `timer_remove` is done by `setup_exit`.
   *Quirk (verified):* nothing ever writes `comm+24h` except the `= 0` above, so `joy` ends 0 and `comm+30h = 0` (and START overwrites `comm+30h` anyway: (corrected 2026-10-13, see HANDOFFS C4)) even
   when a joystick was configured. VGAME/START read the joystick through the calibration block and `comm+72h`
   bit 0, not through `comm+30h`. Port: write exactly `0`.
4. `DS:E24/E25 = 80h` (scaled axes reset). `mouse_present` (`0237:0004`: INT 33h vector non-zero and not IRET, INT
   33h/0 = FFFFh) -> `comm+72h |= 2`.
5. `comm+0 = "Mgraphic.exe"` (graphics driver file; the adapter prompt of older versions is gone).
6. **Sound menu** (rows 13-18): `getche()` loop; `100h` -> `exit(0)`; keys `'1'..'4'` copy the file name
   `Isound.117 / Asound.117 / Rsound.117 / Nsound.117` (table at DS:0778, strings at DS:0042/004D/0058/0063) to
   `comm+0Dh` (13 bytes, includes the NUL); any other key re-asks. The routine is guarded by a flag that is always 1
   (constant at `[bp-56h]`), so the menu is always shown.
7. Blank the area, print `One Moment Please...` (row 16,col 29), then finalise: `comm+2Ah = [0776]` (= 0),
   `comm+26h = 1`, `comm+30h = joy`, and `comm+32h` (if still FFFFh) = 3 when `comm+0` starts with `M/m`
   (always) else 2. `exit(24h)`.

`comm` after SETUP: `+0 "Mgraphic.exe"`, `+0Dh` sound driver file, `+22h 1`, `+24h 0`, `+26h 1`, `+2Ah 0`, `+30h 0`,
`+32h 3`, `+48h` calibration block (only if joystick), `+70h 0`, `+72h` bit0 joystick / bit1 mouse.

**Port decision (S4/S5):** `setup_main()` is only the questionnaire. Provide `--setup` to show a text-mode version
(80x25, CP437 8x16 font supplied by the host) and by default fill `comm` as if the user answered *no joystick* and
sound = *AdLib* (`Asound.117`, i.e. the driver letter `A` that F117.COM passes to MPS_LOGO is `comm[0Dh] & 5Fh`). The
mouse bit is set from the host. SETUP's return code is always 24h in that case.

## 2 MPS_LOGO (`MPS_LOGO.EXE`, "LABS.EXE Version 1.32", DGROUP 0332)

### 2.1 Main (`0006:0072`, argc/argv from the EXEC tail `" X"`)

* `argc < 2` -> `logo_fatal("01 Specify sound driver (R/A/T/I/N)")` (prints `LABS.EXE Version 1.32` then
  `ERROR: <msg>`, exit(-1)). `drv = toupper(argv[1][0])`.
* Driver file by letter: `R -> rsound.log`, `A -> asound.log`, `I -> isound.log`, `T -> tsound.log`;
  `N` = no sound (no file); any other letter -> `logo_fatal("02 Specify sound driver (R/A/T/I/N)")`.
  Shipped files: `ASOUND.LOG`, `ISOUND.LOG`, `RSOUND.LOG` (no `TSOUND.LOG`; T therefore dies with message 03).
  Load = overlay loader (`00DA:0352`, 7-byte thunk table at 00DA:0049, slot base 100; messages in RE_GUIDE 2.2); result 0
  with `drv != 'N'` -> `logo_fatal("03 Bad ovrload")`.
* `drv != 'N'`: `ovl_bind_log(seg)`; repeat `snd_init_log(0,0)` (slot 100) **until it returns 0**.
* `text_clear_b800()` (zero 64 000 bytes at B800:0 - harmless in the port), `set_video_mode(13h)`,
  `pal_get(buf[768])` (current DAC), `ss = ss_load("labslogo", 0, buf)` (NULL -> `logo_fatal("04 bad series load")`),
  `pal_set(buf)`.
* `drv != 'N'` -> `snd_cmd_log(2)` (play the jingle; the music runs on the driver's own INT 8 hook).
* Animation: `t = bios_ticks()`, `fast = 1` (flag `[0E76]`), `result = 0`; for `i = 1 .. ss->nframes` (**1-based over
  all 82 file records**, in memory records start at +22h; the 82nd is the 1x1 "hold" record at (0,0) with delay 34 that
  draws nothing): `t += ss->frame[i].delay` (record field `flag` in RE_GUIDE 4 = **delay in BIOS ticks (18.2 Hz)**, values 0..34);
  `while (fast && bios_ticks() < t) ;` then `ss_draw(ss, i, 3E7h, 0, {200,320,0,A000h})` (the frame draws at its own x,y).
  After each draw `kbhit()` (INT 16h AH=1): a key -> `result = getkey()` (AX=scan:ascii) and `fast = 0`: **the remaining
  frames are drawn without waiting** (the picture is completed instantly, no abort).
* Cleanup: `ss_free(&ss)`; if `drv != 'N'`: `snd_cmd_log(0)`, `snd_shutdown_log()`, `ovl_free_log(seg)`.
* Return `result` mapped: no key 0, `011Bh` (Esc) 1, `1000h` (Alt-Q) 2, any other key 3. (RE_GUIDE 2.1: F117.COM quits on 2,
  skips the PLAYER intro on 1 and 3.)

Port: the LOG overlay is the logo jingle sequencer (RE_GUIDE 5, S3.3). The port plays it by whatever S3.3 decides
(emulating the INT 8 hook at 60.9 Hz, PIT 4C90h). `bios_ticks()` -> an 18.2 Hz clock derived from `SDL_GetTicks`.
`logo_main` returns the same four codes. Total play time = 146 ticks = 8.0 s (sum of the 82 delays at 18.2 Hz) unless a key is pressed.

### 2.2 Screen and palette helpers (verified)

`pal_set`/`pal_get`: 256 entries x 3 bytes through ports 3C8/3C9 and 3C7/3C9, 6-bit components, interrupts off.
`set_video_mode`: INT 10h AH=0. `ss_draw` clips nothing (the sprites fit the screen).

### 2.3 `ss_load` and `ss_draw` (`006D:0112`, `006D:05BE`)

The file format and the sprite RLE are in RE_GUIDE 4 and `tools/ss2png.py` (the **reference decoder**, validated in U5).
Added by this task:

* Load order: `name + ".SS"` is opened `"rb"` (strings at DS:0050/0052/0056). The loader checks the signature `MADSS1`
  and the next byte, reads the header, a 14-byte record per frame, the palette block, then the sprite data into a
  DOS-allocated block; record data offsets are turned into far pointers. Errors (`[0C74]`): 1 open, 2 bad signature, 3
  alloc, 5 bad second tag.
* **Palette (`mode` argument = 0, header mode byte M = 4 for LABSLOGO):** the first DAC slot is `base = DS:004A[M]`
  (table `01 51 1F 38 1F 01 2E ...`, entry 4 = **1Fh = 31**). Palette entry k (1-based) = `{r,g,b}` of the file's
  `{r,g,b,slot}` (the 4th byte is ignored) is stored in the 768-byte buffer at DAC index `base + k - 1`
  (6-bit values, written to the DAC by the caller's `pal_set`; the other 256-56 = 200 colours keep the previous DAC).
  Every non-`FCh` sprite byte is rewritten once at load by `+ (base - 1)` (`ss_remap_rows_*`), so a stored pixel `v`
  is DAC index `v + 30`, and `FCh` stays "transparent". LABSLOGO therefore occupies DAC 31..86.
  The port may either keep that remap or draw through a flat palette `pal[v-1]` as `ss2png.py` does; both give the same
  picture (the DAC outside 31..86 is not used by the logo, background colour 0 = black).
* `ss_draw(ss, frame, x, y, scr)`: `x = 3E7h` or `y = 3E7h` -> use the frame's own `x`,`y`; destination =
  `scr.seg:scr.off + y*scr.w + x`; walks the RLE rows; `FCh` = skip (transparent), others stored. Series header byte
  at +9 must be 0 (else assertion `SPRITES.C ser->scaled==no`: scaled series are not supported here).

## 3 PLAYER (`PLAYER.EXE`, DGROUP 02A3)

Command line (from F117.COM, RE_GUIDE 2.1): `" title-1 title-2 title-3a credit c-pit bunker"` (first run) or
`" title-2"`. `argc < 2` -> prints `usage: player pan_file(s)` and exits. For each name: strip a `.` suffix, append
`.PAN`, open (current directory), play. Memory: all free DOS memory (>= 2000h paragraphs) is one ring buffer
(port: irrelevant, decode whole files).

**Per-file state machine (verified):**

1. `pan_play_file` decodes the LZ stream (`tools/lzs.py`) to cels as described in RE_GUIDE 4. First cel starts with
   `'Z', speed, n:u16, n x u16 sizes, 768 B palette` then n cels `'B',0,1,0 + RLE`; `n <= 1024` (assert "cels <= 1024").
2. `tick_callback` (called once per timer tick, see 3.2):
   * `++tick_counter`; if the fade is pending (`[1DEA] != 0` and `[13D4] != 0`) -> `fade_step`.
   * else if `[1DEA]` (frame countdown) is non-zero, decrement it; when it reaches 0 (or is 0): if all cels were consumed
     (`play ptr == end ptr`) -> `++[1DF0]` (idle), otherwise `cel_show(next)`, `++cel_index`, `[1DEA] = [1DEC]`.
   * `[1DEC]` = `60 / speed` ticks per frame (idiv, `speed` = byte at cel offset 1).
3. `cel_show`:
   * First cel (`'Z'`, `[1E72] = 0`): `screen_off` (wait vblank, seq reg1 |= 20h); the palette block located at
     `cel_base - 768` is passed to `pal_load(0,256,pal)`: while `[13D4] != 0` the DAC is **written with zeros** and the
     palette is kept in a RAM table for the fade; then the first image cel is drawn; `screen_on` (reg1 &= ~20h).
   * Every cel must start with bytes `42h,00h,01h,00h` (asserts `cel[0]=='B'`, `cel[1]==0`, `cel[2]==1`, `cel[3]==0`).
   * Drawing: `cel_rle_draw(src+4, A000h, 0)`: **delta onto the existing screen** (RE_GUIDE 4 grammar). No double buffer.
4. **Fade-in:** `fade_init(5)` runs once at start-up: total steps `[13D4] = 5*60 = 300`; each tick `fade_step` writes the
   current slice of the palette scaled by a 16-bit factor that grows linearly from 0 to FFFFh over the 300 steps (the loop
   count per tick adapts to the vblank-measured PIT speed, `[13D2]`, so the real fade finishes in ~5 s on a 60 Hz
   monitor). Port: linear fade of all 256 colours from black to the palette over ~5 s while the first frame is already
   visible; frames advance only after the fade ends (`[1DEA] != 0 && [13D4] != 0` takes the fade branch first). *(U)* fade
   length by eye (U-player).
5. After the **last** cel of a file the program waits for the next file; with several files `pan_play_file` loops the list.
   When the whole list is finished: `player_run`: if `argc > 2` (more than one name) it additionally **waits until sound
   command 2 (voice-active query) returns 0**, polling input; then `player_exit(0)`.

### 3.1 Input and exit

`poll_input` (`0696`, called from the main wait loops): `1E7A` (re-entrancy hold) is initially 1; on the first call it
issues **`snd_cmd(3)`** (the PLAYER cue sequence; RE_GUIDE 5) and clears the hold, which enables `tick_callback`. Then it
polls: joystick buttons 0 and 1 (`comm+72h` bit 0; port 201h via `0F1A`), mouse buttons (bit 1; INT 33h/3 via `0F47`) and the
BIOS keyboard (`1380` = peek, `13A6` = get key). **Any** key/button, or Ctrl-Break (`[2092] != 0`), ends the program:
`player_exit(key)` (`1Bh` when only a button), which does `snd_cmd(0)`, restores INT 8/1Bh/23h, restores the previous
video mode **only if it was not 13h**, frees memory and `exit(code)`. F117.COM ignores the code (RE_GUIDE 2.1). The
screen is left as is (mode 13h, last frame, palette as last written) for the next program.
Video mode: at start `INT 10h/0Fh`; if the mode is not 13h it is set to 13h (MPS_LOGO already did).

### 3.2 Timer

PLAYER uses the platform timer (`026B:000A` install, `026B:0272` calibrate; platform.md 4) with the divisor class left
at 1, i.e. **one tick per vertical retrace**. The speed byte is "a divisor of 60": the intended rate is 60 ticks/s;
on a 70 Hz mode-13h monitor the ISR still runs at 70.09 Hz, but `timer_step` drops every 7th tick (class 4), so the callback
runs at **60.08 Hz** (corrected 2026-10-13, see 8.3.3; this section used to say "17 % faster"). **Port:** tick = 1/60 s. `snd_poll` is not called from PLAYER's ISR separately - the platform ISR does
its sound step (slot 103) as in platform.md.

## 4 DSWAP (`DSWAP.EXE`, DGROUP 0225)

`dswap_main` (`0000:0010`): `argv[1]` = `"0"/"1"/"2"` -> program name table at DS:006A (`start.exe`, `vgame.exe`,
`end.exe`; RE_GUIDE 2.1). `fopen(name,"r")` succeeds -> `exit(1)` immediately. Otherwise: floppy count from
`0040:0010` bits 6-7 (0 -> 1 drive else 2 drives, `[bp-44h]`); candidate drive `A/B` (`'A' + [bp-50h]`); presence test by
INT 13h disk reads (`10F4`, up to 3 tries per drive), then `fopen("%s:\%s")`; if the program is found -> `exit(1)`;
otherwise show `swap_screen_show` (`SWAP.PIC` + `SWAP.PAL` with the PIC decoder shared with START/END, video.md 8) and wait
for a key: `B/b` selects drive B (`[bp-50h] = 1`) and loops, any other key selects A (`0`). `exit(1)` only when found.
Errors: `Insufficient system memory - AllocBuffer`, `Buffer dealloc error`, the DOS-layer messages of platform.md 3.1.
**Port: `dswap_main` is `return 1;` (never shows the picture).** `SWAP.PIC`/`SWAP.PAL` are unused data.

## 5 MISC.EXE (6 entries, slot base 90, code segment image 0070h, 689-byte file) - verified

| Slot | Offset | Code | Meaning |
|---|---|---|---|
| 90 | `0007:000A` | `mov ah,1; int 16h; je...` | `kbd_empty()`: 0 if a key is waiting, FFFFh if not |
| 91 | `0007:0005` | `sub ah,ah; int 16h` | `kbd_get()` -> AX = scan:ASCII |
| 92 | `0007:0000` | `mov ah,1; int 21h` | `getche()` -> AX (echoes the char) |
| 93 | `0007:0029` | `in al,201h; shr al,n+4; and 1; xor 1` | `joy_button(n)`: 1 = pressed, n = 0/1 |
| 94, 95 | `0007:0018` | `0040:0017 &= F0h` | clear Shift/Ctrl/Alt-state lock bits (Caps/Num/Scroll/Insert), keep the low nibble |

SETUP carries its own copy of the joystick code (segment `0237`, 317 B) that START/VGAME use as the `joy_*` set
(`platform.md` `joy_read_raw`, `joy_scale`, `joy_set_calib`); the calibration block layout is DS:0DF4 (28h bytes = 20 words:
min x/y at +00, max +08..., centre, scale; written by SETUP to `comm+48h`).

`getche()` and `0100h`: SETUP compares the whole AX with `0059h` ('Y') and `0100h` yet `INT 21h/01` returns only AL;
whatever AH holds on the shipped DOS 3.3-5 builds, the programs only ever see ASCII keys for Y/N/1-4 (matched exactly,
so AH was 0 there) and `100h` never occurs in normal play. **Port:** `getche()` returns the ASCII code (0 for an extended
key, then the second byte is NOT read); SETUP treats Esc (`1Bh`) like `100h` (quit) - matches RE_GUIDE's "Esc in menus".

## 6 Open questions

1. (tick rate settled by 8.3.3: 60.08 Hz) The exact fade shape/length (3 item 4): visual check **U-player** (compare
   with U3 captures 02_title1/03_title2).
2. (resolved) SS frame loop: 1-based over all 82 records, last = invisible hold of 34 ticks.
3. LOG overlay `slot 105` tick count unused by MPS_LOGO (RE_GUIDE 5); S3.3 owns the jingle.
4. `getche()` AH (5): harmless.

## 7 Port summary (to S4/S5)

| Function | Notes |
|---|---|
| `setup_main()` | defaults + optional `--setup` text UI; writes comm per section 1 |
| `logo_main(drv)` | SS decode via `ss2png`-equivalent C decoder; delay in 18.2 Hz ticks; key completes the animation instantly |
| `player_main(names)` | PAN decode (`lzs.py` in C); 60 Hz ticks; first cel palette fade 5 s; any key/click ends; sound cmd 3 at the start, cmd 0 at the end |
| `dswap_main(n)` | `return 1` |
| `kbd_*`, `getche`, `joy_button` | host input layer |

## 8 Closing pass (S3.10b): the remaining 121 functions (2026-10-13)

`frontend_symbols.csv` now has a row for every indexed function of SETUP, MPS_LOGO, PLAYER and DSWAP. The 121 functions that
the earlier passes (S3.4, S3.10) left uncovered are listed here (the 127 of `HANDOFFS.md` section 9 minus the 6 of START, which
are in `shell.md` Part E). Every address was read in the disassembly (`tools/dsann.py`). Every "dead" verdict was checked
three ways: no direct call or jump in the indexed code, no operand or data word equal to the address (`work/s310b/refs.py`),
and no reachability from the entry point or from an address-taken handler (`work/s310b/reach.py`).

| Class | Meaning | Port |
|---|---|---|
| **live** | reached from the program's entry. Program-specific code is described in full below. MSC RTL and MicroProse library pieces are *mapped* to the function of the same role in `platform.md` (CSV note `(= EXE addr name)`) and not re-specified | RTL: the host C library. Program code: as stated per entry |
| **dead** | linked in but unreachable (library objects the program never calls, debug code). One line each; the CSV note starts with `dead` | not ported |

Counts by class are in 8.6. Conventions: `TextCtx` and `FILE` as in `video.md` / `platform.md`; "far" parameters are `seg:off`
pairs with the offset pushed last (Microsoft C order); `DS:xxxx` is the DGROUP of the program in question.

### 8.1 DSWAP (7 functions)

DSWAP is a no-op in the port (`dswap_main` = `return 1`, section 4), so nothing here is ported. It is documented so that the
exit rule of section 4 is complete and the dead code is accounted for.

| Addr | Name | Class | Notes |
|---|---|---|---|
| `0219` | `dswap_exit` | live | `void dswap_exit(int code)`: MGRAPHIC slot 69 `screen_off`; if `[2BF4] != 0` (segment of the 2000h-paragraph disk buffer) `mem_free_chk([2BF4])`; `break_hook_remove` (`0546`); `exit(code)`. Callers: the end of `dswap_main` (`exit(1)`, `0211`) and the two fatal memory errors, `mem_alloc_chk` `04B4` ("Insufficient system memory - AllocBuffer") and `mem_free_chk` `04D8` ("Buffer dealloc error"), both with code **0** |
| `031F` | `set_text_mode3` | dead | `int86(0x10, r, r)` with `AX = 0003h`. Its only caller is the unindexed `030A` (below), which nobody calls |
| `10D0` | `dos_getdisk` | live | `int dos_getdisk(void)`: INT 21h/19h, returns `AL + 1` (1 = A:). `dswap_main` stores `1 - AL` (`dec, dec, neg`) as its candidate floppy: the *other* drive (current A: -> 1 = B:, current B: -> 0 = A:); on a hard disk the first `fopen` succeeds and this is never used |
| `10D8` | `dos_setdisk` | dead | `int dos_setdisk(int drive1)`: INT 21h/0Eh with `DL = drive1 - 1`, INT 21h/19h; returns 0 when the current drive is now `drive1`, else -1 |
| `10F4` | `bios_disk` | live | `unsigned bios_disk(int cmd, DiskReq *r)`: INT 13h with `AH = cmd`, `DL = r->drive`. When `cmd >= 2`: `DH = r->head` (word at +2), `CH = cylinder low 8 bits`, `CL = ((cyl >> 8) & 3) << 6 \| r->sector` (words at +4 and +6), `AL = r->count` (+8), `ES:BX = r->buf` (far, +0Ah). Returns INT 13h `AX` (status in `AH`); carry is ignored. `DiskReq = { int drive, head, cyl, sector, count; void far *buf; }`. `dswap_main` calls it with `cmd = 0` (reset) and `cmd = 2` (read one sector), up to 3 tries per drive |
| `1120` | `dos_select_disk` | live | `int dos_select_disk(int drive1, int *nlogical)`: INT 21h/0Eh (`DL = drive1 - 1`); `*nlogical = AL` (number of logical drives); returns 0 |
| `1BD0` | `atol` | live | `long atol(char *s)`: skips blanks (20h) and tabs, optional `+`/`-`, then digits while `'0' <= c <= '9'`; `acc = acc*10 + d` in 32 bits, no overflow check; negated for `-`. Result `DX:AX`. `0FF8` is the 3-byte entry `jmp 1BD0` (`atoi`; `dswap_main` pushes a junk word first and calls `atoi(argv[1])`) |

Unindexed: **`030A` `break_check`** (dead): `if ([00C8] != 0) { break_hook_remove(); set_text_mode3(); exit(0); }`. The flag
`[00C8]` is set by `break_isr` (`0563`), but nothing calls this check, so Ctrl-Break does nothing in DSWAP.

### 8.2 SETUP (38 functions)

SETUP links the MicroProse graphics/string library in whole and uses only a few pieces of it: `ovl_load`, three string copies,
a BIOS-interrupt thunk and the RTL. The rest is dead. The driver-slot calls inside the dead text routines (`05C3`, `0633`,
`075C`, ...) go to MGRAPHIC slots 5 and 47, which SETUP never binds (RE_GUIDE 2.3).

**Live (5)**

| Addr | Name | Description |
|---|---|---|
| `0B83` | `ovl_load_tail` | the exit epilogue of `ovl_load` (`0A9F`; jumped to from `0B77`): `DS = ES = 024C`, `AX = [0786]` (the load segment); if `[078E] == 0` (the normal entry `0A9F`) it unwinds the frame (`pop bp, ds, es, si, di; mov sp,bp; pop bp; ret`), else a plain `ret` (the second entry `0AB2` sets `[078E] = 1`). `0B9D` right after it is the loader's shared fatal exit, `INT 21h/4Ch` with `AX = 4C00h`, reached after one of the messages at `DS:078F..085B` (RE_GUIDE 2.2). Port: no overlay loader |
| `1244` | `strcpy` | `char *strcpy(char *dst, char *src)` byte loop (**returns garbage**: `AX` is the last byte copied, not `dst`). Same role as START `94AC`; the caller ignores the result. Used for the local copy of `"Mgraphic.exe"` and the driver name (`032F`, `0458`) |
| `125F` | `str_copy_to_far` | `void str_copy_to_far(char far *dst, char *src)`: copies the NUL-terminated near string to `dst` (`les di,[bp+4]`; `lodsb/stosb`). It writes `"Mgraphic.exe"` to `comm+0` and the sound driver name to `comm+0D` (`comm` is a far pointer). Port: `strcpy` into the comm struct |
| `1387` | `bios_int4` | `int bios_int4(int intno, Regs4 *in, Regs4 *out)`, `Regs4 = {ax, bx, cx, dx}`. Only INT 10h, 21h, 16h and 1Ah are supported (compare chain); any other number returns -1 and calls nothing. Loads `AX..DX` from `*in`, runs the interrupt, stores `AX..DX` to `*out`. SETUP calls it with `in == out == DS:0FC0` for INT 10h AH=00 (mode set at start, `005A`: `DS:0FC0 = 0003h`) and AH=02 (`text_gotoxy`, `0534`: `DH` = row, `DL` = column, `BH` = 0). Port: text-mode helper of the host |
| `1BED` | `rtl_NMSG_WRITE` | head of `rtl_NMSG_WRITE` (= VGAME `F58D`; its first 24 instructions are identical, SETUP's copy ends at the `INT 21h/40h` write to handle 2): `msgno -> rtl_NMSG_TEXT`, `strlen`, `write(2, msg, len)`. Callers: `rtl_FF_MSGBANNER`, `rtl_nullcheck` and `c0_start` |

**Dead (33)**: library objects with no reference.

| Addr | Name | What it is |
|---|---|---|
| `0562` | `ui_text_far` | `void ui_text_far(TextCtx *c, char far *s, int x, int y)`: `c->x = x; c->y = y;` then `05A3` |
| `05A3` | `ui_text_far_buf` | copies the far string into a 200-byte stack buffer (`1276`) and calls MGRAPHIC slot 5 `text_noclip(c, buf)` |
| `05CE` | `ui_text_wrap_far` | word-wrap draw of a far string, an older variant of START `ui_text_wrap` (`3776`): `(c, char far *s, int width, int x, int y, int line_h)`, no centre option. Measures with slot 47, breaks at blanks and `-`, draws each line with `text_noclip` through the copy `1340`, `y += line_h` per line (2 more after a CR) |
| `070A` | `ui_text_wrap` | the same for a near string (copy via `1322`); 3FAh bytes of stack |
| `0811` | `ui_text_width` | near twin of START `373C`: `int ui_text_width(TextCtx *c, char *s)` = sum of slot 47 `font_char_width` over `s` (keeps the string pointer in a local, hence not byte-exact) |
| `0851` | `fmt_long_commas` | `void fmt_long_commas(long v, char *out)`: `-` for negatives, then **six** decimal digits (`18A2` remainder, `17D6` quotient), a comma after the thousands once a non-zero digit was printed; values of 1 000 000 and more lose their high digits (1234567 prints `234,567`); NUL-terminated |
| `0979` | `fmt_int_commas` | the same for a 16-bit `int` (`idiv`) |
| `0D53` | `file_open` | `int file_open(char *name, int mode)` = `dos_open` (`0DC7`) |
| `0D65` | `file_creat` | `file_creat(name, attr)` = `dos_creat` (`0E1E`). It is the only file-creating code in SETUP and it is unreachable (HANDOFFS G1) |
| `0D77` | `file_close` | `file_close(fd)` = `dos_close` (`0E75`) |
| `0D85` | `file_read` | `file_read(fd, buf, n)` = `dos_read` (`0E97`) |
| `0D98` | `file_read_far` | `file_read_far(fd, off, seg, n)` = `dos_read_far` (`0EC2`) |
| `0DAE` | `file_write_far` | `file_write_far(fd, off, seg, n, ...)` = `dos_write_far` (`0F52`). The twins `0CB2`, `0CE5`, `0D1B` mapped in S3.10 call these wrappers and are dead too |
| `11E6` | `outp` | `void outp(int port, int val)`: `out dx, al` |
| `11F4` | `strupr` | in-place `a-z` upper-casing (role of VGAME `F2B8`) |
| `1214` | `str_equal` | `int str_equal(char *a, char *b)`. **Bug**: the loop never advances `a` or `b`, so it returns 1 only for two empty strings, 0 when the first bytes differ, and loops forever when they are equal and non-empty |
| `1276` | `str_copy_far` | `void str_copy_far(char *dst, char far *src)` (role of END `4B72`) |
| `1291` | `strlen` | `unsigned strlen(char *s)` (role of VGAME `F078`) |
| `12AD` | `strcat` | `strcat(char *d, char *s)` (role of VGAME `F006`) |
| `12D5` | `strchr` | `char *strchr(char *s, int c)`: pointer to the first `c` or 0; the terminating NUL is not found |
| `12F8` | `memset_near` | `memset(char *d, int c, unsigned n)` (`rep stosb`, ES assumed = DS) |
| `130C` | `memset_far` | `memset(char far *d, int c, unsigned n)` |
| `1322` | `memcpy_near` | byte loop with `loop`; `n = 0` copies 65 536 bytes |
| `1340` | `memcpy_far_src` | `memcpy(char *d, char far *s, unsigned n)` (`rep movsb`) |
| `135C` | `wordcmp` | `int wordcmp(void *a, void *b, unsigned n)`: 1 when equal. **Bug**: both pointers advance by one byte per word, so overlapping words are compared |
| `13EC` | `itoa_radix` | `int itoa_radix(int v, char *buf, int radix)`: radix 10 (`1429`, signed, returns the digit count), 16 (`1454`, exactly four digits; digits above 9 come out as `:;<=>?` because only `'0'` is added: bug; returns 4), 2 (`149C`, 16 digits MSB first, returns 16); any other radix writes an empty string and returns 0 |
| `1429`, `1454`, `149C` | | the three bodies called by `13EC` (register arguments: `BX` = buffer, the value is `[bp+4]` of `13EC`'s frame) |
| `18A2` | `rtl_aFlrem` | `long aFlrem(long a, long b) /*pascal*/`: signed remainder, the sibling of `rtl_aFldiv` (`17D6`) |
| `194E` | `ldiv_assign` | `void ldiv_assign(long *p, long d) /*pascal*/`: `*p /= d` through `rtl_aFldiv` |
| `1E4F` | | stray far-returning stub that calls `1F51` |
| `1F51` | | 4 bytes `F9 42 5F CF` (`stc; inc dx; pop di; iret`): data, not code |

### 8.3 PLAYER (35 functions)

#### 8.3.1 The PAN stream reader: ring buffer and bit reader (live)

`pan_play_file` (`0244`, section 3) streams a `.PAN` file through a ring buffer in all free DOS memory. The port decodes
whole files (section 3), so none of this is ported. The algorithm is given so the behaviour can be reproduced. Variables
(DS:xxxx): `A = [2082:2084]` write position (32-bit linear address), `H = [2094:2096]` head (start of the first undisplayed
data), `T = [1E6E:1E70]` tail, `S = [1E6A:1E6C]` buffer start, `E = [1E60:1E62]` buffer end, `W = [1E66:1E68]` and
`P = [2086:2088]` previous head, `[1DE8]` = number of cels (0 until the header word has been decoded). A linear address `L`
is `seg = L >> 4`, `off = L & 15`.

* **`03FE` `int ring_make_room(void)`** returns 1 when `0C000h` bytes can be decoded at `A`, else 0 (the caller then runs
  `poll_input` and retries). `lim = A + 0C000h`. If the buffer has wrapped (`T > H`): return `lim < T`. Otherwise, if
  `lim <= E` return 1. Else the unread data has to wrap to the buffer start: `used = (word)(A - H)`;
  `keep = [1DE8] ? max(used, 2000h) : 0`; if `S + 0C000h + keep >= T` return 0; else `huge_copy(A - keep, S, keep)`,
  `P = H`, `A = S + keep`, `H = W = A - used`, return 1.
* **`05F6` `void huge_copy(uint32 src, uint32 dst, unsigned n)`**: forward byte copy between two linear addresses; `n = 0`
  copies nothing. **`0664` `unsigned huge_peek_word(uint32 lin)`** reads the 16-bit word at a linear address.
  `pan_play_file` uses them for the cel count (the word at `A + 2`) and for the cel size table (`2*n` bytes from `A + 4`
  to `[207E:2080]`); `n > 1024` fails the assertion `cels <= 1024`.
* **`0BA2` `int bits_open(char *name)`**: `[11BC] = 0` (bits left), `[11BE] = 0` (end flag), read pointer `[01B6]` := end of the
  4 KB file buffer `DS:01B8..11B7` (so the first read refills), `INT 21h/3Dh` read-only, handle to `[11B8]`. Returns **1 on
  success**, 0 on failure. **`0BCD` `int bits_close(void)`**: `INT 21h/3Eh`, same convention (1 = ok; 0 raises the assertion
  `FAB_close()`). **`0BDC` `int bits_eof(void)`** returns `[11BE]` (set by the chunk decoder `0BE2`: `1` when the chunk ended with
  the control byte 0, 0 when it ended with 1).
* **`0B84` `bits_refill`** (register entry; `SI` = read pointer that passed `11B7h`): copies the byte at `[SI]` to `[SI - 1000h]`
  (the byte already fetched), `SI -= 1000h`, then `INT 21h/3Fh` reads 1000h bytes from `[11B8]` to `DS:01B8`. Errors and short
  reads are not checked (the rest of a short last block keeps stale data).

#### 8.3.2 Fade helpers (live)

* **`0DE4` `fade_measure`** (called by `fade_init` `0E0C` while `[13D2] == 0`): waits for a retrace on `3DAh` bit 3 (rise, then fall),
  then with interrupts off copies bytes from `DS:SI` (the 768-byte palette table `13DA`) to port `3C9h` until the next retrace
  starts, counting them in `CX`; `[13D2] = CX`. The first `out 3C8h` uses the last `3DAh` status byte in `AL` (normally 0 or 1)
  as the DAC index: harmless, because the first real palette write follows. Port: no measurement (use 256 colours per tick).
* **`0E65` `fade_slice`** (called by `fade_init` and `fade_step`): computes the next `[13D0]` colours of the faded palette into
  `DS:16DA`. `idx = ([13CE] + [13D0]) & 0FFh` is stored to `[13CE]`; for each of the `[13D0]` colours it reads the source
  triple at `13DA + 3*idx` and writes `(v * [13D6] + 8000h) >> 16` per component (the `mul; shl ax,1; adc dx,0` pair rounds to nearest).
  After each colour `idx` is incremented as a byte; when it wraps to 0 a full pass is done and the factor moves:
  `[13D6] += [13D8]` (signed), clamped at `FFFFh` (rising) or `0` (falling). With `fade_init(5)`: `steps = [13D4] = 300`,
  `[13D0] = min(256, [13D2] / 20)`, `[13D8] = [13D6] = 65536 / ((steps * [13D0] >> 8) - 1)` (negated for a negative `secs`).
  With 256 colours per tick that is 65536 / 299 = **219**. Port: on tick `t = 1..300` all 256 colours are `(v * f + 8000h) >> 16`
  with `f = min(FFFFh, 219 * t)`; the real slice size depends on the CPU speed (about 1/20 of the bytes that fit into one retrace).

#### 8.3.3 Input, video, break handler, timer (live)

| Addr | Name | Description |
|---|---|---|
| `0520` | `video_get_mode` | `int video_get_mode(void)`: INT 10h AH=0Fh, returns `AL` |
| `0540` | `video_set_mode` | `void video_set_mode(int mode)`: INT 10h AH=00h, `AL = mode` |
| `0560` | `break_isr` | handler for INT 1Bh and INT 23h (installed by `0583`): saves all registers, `DS = 02A3`, **`[2092] = 3`**, `iret`. `poll_input` ends the program when `[2092] != 0`. Same role as `break_isr` in `platform.md` |
| `05D0` | `break_hooks_restore` | `dos_setvect(1Bh, [1E76:1E78]); dos_setvect(23h, [208E:2090])` (the saved vectors; role of `break_hook_remove`) |
| `0F1A` | `joy_button` | `int joy_button(int n)` = `((inp(201h) >> (n + 4)) & 1) ^ 1` (1 = pressed); a local copy of MISC slot 93 |
| `0F47` | `mouse_poll_buttons` | near twin of START `8545 mouse_poll` without the coordinates: INT 33h AX=3, `[19DA] = BX & 1` (left), `[19DB] = (BX >> 1) & 1` (right); `mouse_button(n)` (`0F32`) returns them |
| `026B:000A` | `timer_install` | `if (xchg([1A1C], 1) == 0) { [1A0E] = [1A16] = 1; [1A04] = [1A06] = 0; timer_calibrate(); save INT 8 to cs:020D; set INT 8 = ISR 026B:01A3 (pointer cell cs:019F) }`. The platform timer of `platform.md` 4 |
| `026B:004D` | `timer_remove` | `if (xchg([1A1C], 0) != 0) { PIT channel 0 mode 3, count 0 (18.2 Hz); restore INT 8 }` |
| `026B:0336` | `timer_step` | the ISR's tick work, called by the ISR (`026B:01D7`) once per tick, not by code. **If the rate class `[1A10]` is 4 it counts `[1A1E]` down (7 -> 0) and on every 7th tick returns at once: neither `tick_callback` nor the sound poll runs.** Otherwise: far-call `tick_callback` (`0000:0738`, section 3), then slot 103 `snd_poll`; result 0 = nothing, negative -> `026B:008B` (divisor `[1A0E] := 1`, reload `[1A08] := [1A12]`, `[1A1B] = 1`), positive -> `026B:0073` (divisor := class `[1A10]`, reload := `[1A12] / [1A10]`, `[1A1B] = 1`). This is the "sound step" of `platform.md` 4 |

**PLAYER's animation clock is 60.08 ticks per second** on a 70 Hz mode-13h display (class 4, the DOSBox default), because the
ISR runs once per retrace (70.09 Hz) and 1 tick in 7 is dropped before `tick_callback` runs (6/7 * 70.09 = 60.08). On a 60 Hz
display the class is 5 and there is no skipping (60.0). Section 3.2's "17 % faster on 70 Hz" is wrong (corrected 2026-10-13).
The port runs `tick_callback` at 60 Hz, as decided. The same drop makes the sound sequencer run at 60.08 Hz (`simflow.md` Part 0).

#### 8.3.4 RTL and library pieces (live, mapped)

| Addr | Name | Maps to |
|---|---|---|
| `144C` | `strchr` | `char *strchr(char *s, int c)` via `repne scasb`; `pan_play_file` uses it to cut the name at `'.'`. Returns 0 when absent |
| `1476` | `rtl_assert_fail` | `_assert(expr, file, line)`: `fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", ...)`, `fflush(stderr)`, `abort()`. MPS_LOGO `06DC` is the large-model copy |
| `14B8` | `dos_getvect` | `void far *dos_getvect(int n)`: INT 21h/35h, result `ES:BX` in `DX:AX` |
| `14CA` | `dos_setvect` | `int dos_setvect(int n, void far *h)`: INT 21h/25h (`DS:DX` = `h`), returns 0 |
| `1F54` | `abort` | `rtl_NMSG_WRITE(10)`, `raise(22)` (`SIGABRT`), `rtl_exit_quick(3)`. MPS_LOGO `1756` is the copy. The bytes `1F70..1FCE` right after its `ret` are a separate function that the index did not split: the `ltoa` core `(long v, char *buf, int radix)` that `1376 ltoa` calls |
| `223A` | `raise` | MSC `raise(sig)`: `SIGINT` (2): `INT 23h`, exit unless it returns with carry clear; `SIGFPE` (8) without a handler returns -1; otherwise `257B` finds the table slot of `sig` (-1 if unknown); handler word 1 = `SIG_IGN` returns 0; 0 = `SIG_DFL` jumps through the default-action table at `cs:222E`; any other value is a handler: the slot is cleared (except for `SIGFPE`) and `handler(sig, 8Ch)` is called. MPS_LOGO `1ADC` is the copy |
| `257B` | `rtl_sig_lookup` | `AL` = signal number -> `BX` = slot in the table `DS:1C96` (4 bytes per signal, six signals, compared against `cs:[24DD..]`), carry set if unknown. MPS_LOGO `1D66` is the copy |

**Dead (10)**

| Addr | Name | What it is |
|---|---|---|
| `0908` | `dbg_reset` | `[1E56] = 1DF2` (start of the debug line buffer `DS:1DF2`) |
| `0910` | `dbg_putc` | appends `c` at `[1E56]++` and terminates the string |
| `092A` | `dbg_puts` | appends a near string |
| `0954` | `dbg_pad` | `void dbg_pad(int v, int width)`: while `tbl[width--] > v` append a space (table `DS:0172` = -32768, 0, 10, 100, 1000, 10000): right-justifies `v` to `width` digits |
| `0984` | `dbg_put_dec` | `ltoa(v, [1E56], 10)`, then advance `[1E56]` to the end |
| `09B8` | `dbg_put_hex` | `ltoa(v, [1E56], 16)`, then advance |
| `09EE` | `dbg_put_dec_pad` | `(v, width)`: `width > 0` pads first, `width < 0` pads after the number |
| `0A20` | `dbg_status_line` | builds `time:<[1DEE]> cels:<[1E64]> full:<n>% tail:<hex> head:<hex> size:<hex> skip:<[1DF0]>` from the ring buffer variables (full % = `(H - T [+ P - W]) * 100 / (E - S)`) and stores the characters into the text row passed as a far pointer (every second byte, so the attributes stay). A debug overlay; nothing calls it |
| `24DE` | `signal` | MSC `signal(sig, handler)`; for `SIGINT` it installs the INT 23h handler `cs:25A2` (saving the old vector in `[1C8E:1C90]`). MPS_LOGO `1CB8` is the copy. Nothing calls it |
| `25C7` | `rtl_sig_unwind` | tail of the INT 23h signal handler (rebuilds the frame and `retf`s with carry set). Exact copy of MPS_LOGO `1DB6` |

### 8.4 MPS_LOGO (41 functions)

MPS_LOGO is a large-model program (far calls, `retf`) with the LABS library. Segments: `0000` far-call veneers, `0006` main,
`0045` huge-memory and string helpers, `006D` SS loader, `00DA` overlay manager for the `.LOG` driver, `0146` C runtime.

#### 8.4.1 Live program code

| Addr | Name | Description |
|---|---|---|
| `0045:000E` | `huge_norm` | `void far *huge_norm(unsigned off, unsigned seg)`: if `off > 0Fh` then `seg += off >> 4; off &= 0Fh`; result in `DX:AX`. Used by `huge_fread` (`0045:0116`) |
| `0045:003A` | `strpos` | `int strpos(char *hay, char *needle)`: 1-based index of the first occurrence (via `strstr`), 0 when absent. `ss_load` calls it with the string `"."` (`DS:0050`) to see whether the series name has an extension |
| `0045:008E` | `strcpy_to_seg` | `void strcpy_to_seg(unsigned off, unsigned seg, char *src)`: copies the near string `src` (with its NUL) to `seg:off`. `ss_load` copies the series name into the SS header block with it (`0220`) |
| `006D:0678` | `ss_draw_rle` | the RLE row loop of `ss_draw`, entered by falling through from `05BE` (the index split the function here). Register interface: `DS:SI` = sprite data, `ES:DI` = destination (`seg:off + y*w + x`), `BX` = destination width. Grammar (identical to `tools/ss2png.py decode`): at the start of a row, `FF` = end of data, `FE` = empty row (`DI := row start + width`); any other byte is the **row mode marker**: `FA` = literal mode, every other value = pair mode (the marker itself is discarded). Literal mode: bytes are pixels, `FC` = skip one pixel, `FD n v` = run of `n` pixels of colour `v`, `FE` ends the row. Pair mode: `(count, value)` pairs, `value == FC` skips `count` pixels, a `FE` in the count position ends the row. The subroutine `0678` is the run writer (`AL` = count, `AH` = value): `AH == FC` -> `DI += AL`, else `AL` stores of `AH`. **Quirk:** a count of 0 writes 256 pixels (`dec al; jne`), and skips 0 for `FC`; the shipped data has no zero counts. No clipping |
| `00DA:000E` | `ovl_call` | the overlay call trampoline. Every driver slot is a 7-byte thunk `E8 rel16` (call `000E`) followed by `dw off, seg` (written by `ovl_bind_log`, thunk of slot `s` at `00DA:0049 + 7*s`). `ovl_call` pops the return address (= address of `off,seg`), stores it as the operand of the `ljmp cs:[..]` at `001C` and jumps to the driver function, which returns straight to the thunk's caller. If `[007B] != 0` (never: nothing in MPS_LOGO sets it) it instead saves the caller's far return address in `[007C]`, calls the target, and runs `0636` once per nesting level before returning |
| `00DA:043D` | `ovl_read_file` | reads the overlay file whose name pointer is `[0082]` (0 = skip) into the segment in `BX`: `INT 21h/3D00h` open, one `INT 21h/3Fh` read of up to 8000h bytes to `BX:0000` (if the read returned 8000h, `DS += 800h` before the paragraph arithmetic: files above 32 KB are not supported), `INT 21h/3Eh` close; returns `BX` = first free segment after the image (`seg + (len + 15) / 16`). Errors print the message at `DS:00DD` and exit (`040E`). Called by `ovl_load_log` (`0352`) |
| `00DA:0088`, `00B9`, `0129` | | stretches of the thunk table (slots 9, 10, 20..21) that the index took for functions; data, never executed as code here |

#### 8.4.2 Mouse library (dead)

The LABS library's mouse layer is linked in and never called by MPS_LOGO.

| Addr | Name | What it is |
|---|---|---|
| `00DA:0533` | `mouse_set_hot` | `(int hx, int hy, int shape)`: `[01AC] = hx; [01AE] = hy; [01AA] = shape` |
| `00DA:054A` | `mouse_set_pos` | `(long pos)`: `[01A2:01A4] = pos`; when `[01B1] != 0` INT 33h AX=4 with `CX = x << [01B0]` (video-mode scaling), `DX = y` |
| `00DA:05C1` | `mouse_hide` | if `[01AA] != 0 && [007B] != 0`: `[007B] := 0` (saved in `cs:05C0`) and call thunk slot 9 (`00DA:0088`) |
| `00DA:0602` | `mouse_call` | `int *mouse_call(int fn, int bx, int cx, int dx)`: INT 33h; results stored in `[01B6..01BC]` (`AX,BX,CX,DX`); for `fn <= 6` the x result is `>> [01B0]`; returns a pointer to the result block |
| `00DA:0636` | `ovl_call_after` | the cursor-redraw step of `ovl_call`'s guarded path (calls thunk slots 17 and 27 with the cursor position minus the hot spot). Reached only through that path, which MPS_LOGO never takes |
| `00DA:0668` | `mouse_set_cursor` | `(void far *shape)`: `[01B2:01B4] = shape` |

#### 8.4.3 C runtime (mapped to `platform.md`)

All of these are the Microsoft C large-model builds of routines `platform.md` names for the small-model programs
(`retf` instead of `ret`, `lcall`/`push cs` for calls inside the segment, FILE structures in DGROUP at `0332:027C`). The CSV
rows carry `(= EXE addr name)`.

| Addr | Name | Role |
|---|---|---|
| `0000:0000`, `0000:0030` | `file_copy_w`, `huge_fread_w` | far-call veneers (`chkstk(4)`, then `0045:01FE` / `0045:0116` with the caller's words). Dead |
| `0045:01FE` | `file_copy` | `long file_copy(FILE *src, FILE *dst, long n)`: up to `n` bytes in chunks of 200h through the buffer `DS:0C76` (`fread` then `fwrite` per chunk), stops at a short read, returns the bytes copied. Dead |
| `0146:0016` | `c0_start` | MSC start-up (= END `4DD2`): DOS >= 2 check, SS/SP setup, shrink the block (`INT 21h/4Ah`), clear BSS `0662..0E7F`, run the init table, `rtl_setenvp` (`0A5E`), `rtl_setargv` (`08CC`), `rtl_cinit` (`010E`), then `main` = `lcall 0006:0072`, `exit(AX)`. `00D9..010B` is the `exit` continuation |
| `0146:0296` | `rtl_chkstk` | `AX` = frame size; stack-overflow check against `[0232]`; on failure it jumps to the handler pointer `[022E]` if one is set, else to the fatal exit `00E9` with message 0 |
| `0146:02BA` | `rtl_fclose_core` | `int fclose_core(FILE *f)` (= START `9164`): `fflush`, free the buffer, `close`, and delete the temporary file named `\<n>` for `tmpfile` streams |
| `0146:0374` | `rtl_fsopen` | `FILE *fsopen(name, mode, shflag)` (= START `9212`): `getstream` + `openfile` |
| `0146:03A0` | `fopen` | `fopen(name, mode)` = `fsopen(name, mode, 0)` (= START `923A`) |
| `0146:049A` | `fwrite` | `unsigned fwrite(void *buf, unsigned size, unsigned n, FILE *f)` (= START `932E`, see `shell.md` Part E): buffered; whole blocks go straight to `rtl_write`; returns the number of complete items; sets the error bit `20h` on a short write. Reached only from the dead `file_copy` |
| `0146:0650` | `strcmp` | `int strcmp(char *a, char *b)`: 0 equal, else -1 / +1 (unsigned bytes: `sbb ax,ax; sbb ax,-1`). `ss_load` compares the file signature `"MADSS1"` (`DS:0059`) and the series name |
| `0146:067C` | `strstr` | `char *strstr(char *hay, char *needle)`: pointer or 0; an empty needle returns `hay` |
| `0146:06DC` | `rtl_assert_fail` | `_assert(expr, file, line)` (= PLAYER `1476`); `ss_draw` uses it for `SPRITES.C ser->scaled==no` (`0613`, line 190h) |
| `0146:0822` | `hmemcpy` | `void far *hmemcpy(void far *dst, void far *src, unsigned n)`: `rep movsw/movsb` in runs, adding 1000h to a segment when its offset wraps; returns `dst` |
| `0146:0880` | `rtl_FF_MSGBANNER` | (= START `9800`): message 252, hook `[0260]`, message 255 |
| `0146:08A8` | `rtl_nullcheck` | (= START `9826`): xor-checksum of `DS:0000..0041` against 55h; on mismatch the "null pointer assignment" message |
| `0146:0B07` | `rtl_NMSG_WRITE` | (= START `9873`) |
| `0146:1510` | `rtl_read` | `int read(int fd, void *buf, unsigned n)` with text-mode CR/LF and Ctrl-Z handling (= START `A230`) |
| `0146:1756` | `abort` | (= PLAYER `1F54`) |
| `0146:17B0` | `malloc` | `void *malloc(unsigned n)`: temporarily sets `_amblksiz [0448]` to 400h, calls `1A64`, restores it, fatal exit on failure (= SETUP `1C22`) |
| `0146:1A50` | `stackavail` | `unsigned stackavail(void)`: `SP - [0232]` or 0 (= START `A756`) |
| `0146:1A64` | `nmalloc` | the 5-byte entry `ljmp 0146:1B8B` = `rtl_nmalloc` |
| `0146:1A6A` | `free` | the 5-byte entry `ljmp 0146:1B6A` = `rtl_nfree`. The rest of the indexed block (`1A6F..1ACF`) is the `ltoa` core (see PLAYER `1F70`) |
| `0146:1ADC` | `raise` | (= PLAYER `223A`) |
| `0146:1CB8` | `signal` | (= PLAYER `24DE`). Dead |
| `0146:1D66` | `rtl_sig_lookup` | (= PLAYER `257B`) |
| `0146:1DB6` | `rtl_sig_unwind` | (= PLAYER `25C7`, exact). Dead |

### 8.5 Corrections made by this pass

* Section 3.2 (PLAYER timer): the animation tick is **60.08 / s** on a 70 Hz display, not 70 (see 8.3.3 `timer_step`).
* S3.10 twin rows that were wrong (seven of the eleven 1-6 byte twins): `1C46` (SETUP), `222A` (PLAYER) and `2060` (DSWAP) are the 3-byte `nmalloc`
  entry, not `free`; DSWAP `0FF8` is `atoi`; START `A768` is `nmalloc` (its neighbour `A76C` really is `free`).
* The 1-byte `ret` stubs START `885E` and DSWAP `0AAA` are the PIC decoder's `pic_hook_nop` (called once at the start of every
  `pic_to_*`), not `ui_nop_a` / `stub_ret_02c0`.

### 8.6 Coverage after the closing pass

`python tools/spec_coverage.py` reports 100 % for all seven programs (1 347 of 1 347 indexed functions; dead functions count as
covered through their `dead` rows and are tallied separately). The 127 functions of this pass:

| Program | Left after S3.10 | Specified in full | Mapped to an existing function | Marked dead |
|---|---|---|---|---|
| SETUP | 38 | 3 | 2 | 33 |
| MPS_LOGO | 41 | 9 | 17 | 15 |
| PLAYER | 35 | 13 | 12 | 10 |
| DSWAP | 7 | 5 | 0 | 2 |
| START (`shell.md` Part E) | 6 | 0 | 5 | 1 |
| **Total** | **127** | **30** | **36** | **61** |

Plus 1 unindexed dead function (DSWAP `030A`). The 37 twin rows of S3.10 stay as they are, except the seven corrected in 8.5
(of the eleven that are only 1-6 bytes long, four were right: `rtl_dosmaperr_al` in PLAYER, START and DSWAP, and START `A76C`
`free`).
