# Front end: MPS_LOGO, SETUP, PLAYER, DSWAP, MISC (S3.4)

**Status: S3.4 delivered.** Symbols: `frontend_symbols.csv` (56 rows, `python tools/frontend_syms.py`).
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
   *Quirk (verified):* nothing ever writes `comm+24h` except the `= 0` above, so `joy` ends 0 and `comm+30h = 0` even
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
on a 70 Hz mode-13h monitor the original runs 17 % faster. **Port:** tick = 1/60 s (decision; *(U)* compare against the
U3 captures), 70 Hz is the alternative. `snd_poll` is not called from PLAYER's ISR separately - the platform ISR does
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

1. PLAYER tick rate 60 vs 70 Hz (3.2) and the exact fade shape/length (3 item 4): visual check **U-player** (compare
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
