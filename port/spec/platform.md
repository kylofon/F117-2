# Platform: C runtime, start-up, DOS layer, timer, input, overlay chain (S3.1)

**Status: S3.1 delivered.** Symbols: `platform_symbols.csv` (517 rows, all 7 programs; regenerate with
`python tools/platform_syms.py`). Conventions: `port/RE_GUIDE.md` (§2 is the authority for the overlay
chain, comm block and driver slots; this file does not repeat it). Disassembly source: `work/unpacked/*.EXE`
(`python tools/fndis.py END 4dd2 4ebc`).
Confidence: **verified** = read in the disassembly; **likely** = recognised by structure/strings or by
matching another program's copy (`port/shared_matches.csv`), not read line by line.

## Key findings

* The compiler is **Microsoft C 5.x/6, small model** (code and data in one 64 KB segment each: code
  segment 0000 of the unpacked image, DGROUP elsewhere, `main` at `0000:0010` in END). The "library" is
  the MSC runtime (c0, stdio, malloc, long arithmetic, `int86`) plus a **MicroProse system layer**
  linked into every program: DOS file wrappers, joystick, mouse, a vertical-retrace-calibrated timer, a
  Ctrl-Break hook and the overlay-driver binder. In **END** the whole library is one contiguous block
  `0000:3E45..5F63` (read completely for S3.1); the other six programs carry the same routines, linked
  through `shared_matches.csv`.
* **Every error is fatal.** The MicroProse DOS wrappers (`dos_open`, `dos_read` ...) print a message and
  exit to DOS on any error (never return an error code). Game code therefore never tests for a missing
  file; the port may `fatal()` the same way.
* **The timer ISR runs several times per video frame and is calibrated to the monitor.** `timer_calibrate`
  measures the PIT counts of 16 vertical retraces and picks a rate class (ISR interrupts per frame):
  VGA mode 13h at 70 Hz gives class 4 (~280 Hz), 60 Hz gives class 5 (300 Hz); anything outside 4..6
  falls back to class 5 with a fixed reload `4DAEh` (19 886 clocks = 60 Hz frame). That is where the
  "60 Hz / 300 Hz" of the S2.4 sound notes comes from.
* `mem_fill(dst, val, n)` **always fills with zero** (the code clears AL before `rep stosb`); the word
  version `mem_fill_w` does honour `val`. Callers rely on the observable behaviour.
* END's `c0_start` calls two empty stubs (`0000:00C7`, `0000:00C8`: `ret`) where SETUP/PLAYER/DSWAP/MPS_LOGO
  link the real `_setargv`/`_setenvp` (`rtl_setargv`). No program except those four looks at its command
  line; F117.COM passes arguments only to SETUP, MPS_LOGO, PLAYER and DSWAP (RE_GUIDE §2.1).
* The three **mouse** routines exist only in START and END (INT 33h); the **joystick** code (port 201h)
  exists in START, END, SETUP and VGAME. Keyboard input goes through MISC.EXE (slots 90-92) or `INT 16h`
  directly (PLAYER/MPS_LOGO copy `bios_keyboard`).

## 1 Library map (code offsets; segment 0000 unless shown)

| function | VGAME | START | END | SETUP | PLAYER | DSWAP | MPS_LOGO |
|---|---|---|---|---|---|---|---|
| `c0_start` | ebb4 | 8edc | 4dd2 | 1516 | 1096 | 0c7e | (own, 0146:08cc region) |
| `rtl_cinit` | ec9e | 8fc6 | 4ebc | 1600 | 1180 | 0d68 | 0146:010e |
| `exit` (+`_exit`,`_cexit`,`_c_exit` at +7,+F,+19) | ed6c | 9094 | 4f8a | 16ce | 124e | 0e36 | 0146:01d3 |
| `rtl_setargv` | – | – | (stub 00c7/00c8) | 19b6 | 1602 | 117e | 0146:08cc |
| `printf` / `sprintf` / `fprintf` | – | 9430 / 959e / – | 505a / 521c / – | – | 131e / – / 18c4 | 0f42 / 107a / – | 05a0 / – / 0b94 |
| `rtl_output` (printf engine) | – | 9d06 | 5678 | – | 1a7e | 16c6 | 0fe4 |
| `malloc` / `rtl_nmalloc` | – / fe35 | – / a791 | 5daa / 5de1 | 1c22 / 1d69 | 1fe8 / 23b1 | 1c3c / 2097 | – / 1b8b |
| `rtl_open` / `rtl_read` / `rtl_write` | fb02 / f94e / fca4 | a5b4 / a230 / a31a | – / – / 5c6c | – | – / – / 20ec | 1d80 / – / 1f22 | 18ae / – / 15fa |
| `fopen` / `fread` / `fflush` / `lseek` | ef12 / ef26 / f7bc / f8d4 | 923a / 924e / 9c32 / a520 | – / – / 5ba8 / 5096 | – | – / – / 19aa / 2304 | 0f2e / – / 1ca2 / 0f7e | – / 03b6 / 0f0c / 181a |
| `dos_open` / `dos_creat` | 1199:000c / 006a | 8075 / 80cc | 400f / 4066 | 0dc7 / 0e1e | – | 0599 / 05f0 | – |
| `dos_close` / `dos_read` / `dos_read_far` / `dos_write_far` | – | 8123 / 8144 / 816f / 81ff | 40bd / 40de / 4109 / 4199 | 0e75 / 0e97 / 0ec2 / 0f52 | – | 0647 / 0668 / 0693 / 0723 | – |
| `dos_allocmem` / `dos_freemem` | f348 / f384 | 800a / 8030 | 3fa4 / 3fca | – | 14a0 / 14e0 | 04e0 / 0506 | 070c / 0748 |
| `mem_alloc_chk` / `mem_free_chk` | – | 40a4 / 40cf | 2ba0 / 2bcb | – | – | 0494 / 04bf | – |
| `rand` / `srand` / `rand_n` | f322 / f310 / – | 96ce / 96bc / – | 52dc / 52ca / 3e45 | 17b0 / 179e / 0a70 | – | – | – |
| `joy_read_raw` / `joy_scale` / `joy_set_calib` | 10a8:0cb7 / 0cff / – | 8469 / 84b1 / 851a | 43d5 / 441d / 448e | 0237:005c / 00a4 / 0127 | – | – | – |
| `mouse_init` / `mouse_poll` | – | 8573 / 8545 | 44e7 / 44b9 | – | – | – | – |
| `break_hook_install` | 1b60 | – | 4557 | 0c35 | – | 0523 | – |
| `bios_timer` | 1e60 | 8607 | 45a6 | – | – | – | – |
| `ovl_bind` | – (0FFD:0008) | 860e | 45ad | 0ba2 | 0f6c | 085a | – (own loader) |
| `timer_install` / `timer_remove` | – | 8ca8 / 8ce6 | 4b8c / 4bca | 0fc0 / 0ffe | – | – | – |
| `timer_calibrate` / `pit_read` | 1d76 / – | 8dd8 / 8ead | 4cbc / 4da3 | 111a / 11af | 026b:0272 / 026b:0307 | – | – |
| `mem_copy` / `mem_zero` / `mem_fill` | – | 8b6c / 8b3e / 8ba0 | 4b0c / 4ade / 4b40 | – | – | – | – |
| `strcpy` / `strcat` / `strlen` | f046 / f006 / f078 | 94ac / 946c / 94de | 5150 / 5110 / 5182 | – | – | – | 061e / 05de / – |
| `int86` | f108 | 9520 | 519e | – | 13ce | 0ffc | – |

Everything else in the symbols file (rows per exe, `owner = platform`) follows the same pattern; names
there are authoritative. Routines found while reading and handed to **video (S3.2)**: `gfx_line`,
`pal_set_default`, `pal_set`, `pal_write_vsync`, `pal_apply_queue` (END 4206, 42BE, 42D0, 42E4, 4393);
the PIC/LZW reader family END 4648..4A71 (shared rows in `work/s31/unassigned.txt`).

## 2 Start-up, exit, fatal errors (verified on END)

`c0_start` (END 4DD2), the standard MSC sequence:
1. INT 21/30; DOS < 2 → `retf` to the PSP (exit). `DI` = DGROUP (`05FA` in END).
2. Stack: size = min(PSP:[2] − DGROUP, 1000h paragraphs); `SS=DGROUP`, `SP += 724Eh` (end of BSS/stack);
   `[50EC]` = stack limit, near-heap bookkeeping words `[50EE..50F8]` initialised (heap = the block
   between BSS end and the stack). `[PSP:2]` is rewritten and INT 21/4A shrinks the DOS block.
3. BSS `[5438..7250)` zeroed; `[5336]` (init hook) called if set; `_setargv`/`_setenvp` (END: empty stubs).
4. `rtl_cinit` (4EBC): INT 21/35+25 on vector 0 (divide error) → handler `4E8A`; environment scan for
   `_C_FILE_INFO=` (decodes two-letter hex pairs into the handle-flag table at `[5131]`); for handles 0..3
   INT 21/4400 sets bit 6 (`0x40` = device) in the flag table; run the initialiser tables via
   `rtl_initterm` (`5038` near, `5047` far pointers).
5. `main(argc [5145], argv [5147], envp [5149])` = `0000:0010`, then `exit(ax)`.

`exit(code)` (4F8A; `_exit` 4F91, `_cexit` 4F99, `_c_exit` 4FA3 share the body with a flags word in CX):
run the terminator tables (`5038` on `[55CA..]`, `[5354..5356]`), flush all streams, `rtl_nullcheck`
(xor-sum of DS:0..41h must equal 55h, else the banner "R6001 null pointer assignment" path), then
`rtl_ctermsub` (restore INT 0, optional vector saved at `[5153/5154]`, math hook `[5348]`) and
INT 21/4C with the code (`[bp+4]`, forced to 0FFh when the null check failed and the code was 0).
The video mode is **not** reset; the next program starts over the last picture (as in the Aces port).

`rtl_amsg_exit(n)` (END 4E9A, entered from `rtl_chkstk` with n = 0 for stack overflow, from 53FA with 2,
from the divide ISR 4E8A with 3): `rtl_FF_MSGBANNER` (message 252 then hook `[515C]`, message 255),
`rtl_NMSG_WRITE(n)` (INT 21/40 to handle 2 from the table at `[535E]`), then exit(255). Port: print
"run-time error R60xx" to stderr and exit 255.

Fatal tail of the DOS wrappers (END 41CA, inside `dos_write_far`): INT 10h mode 3 (text), then — for
open/create — the file name printed with INT 21/09 (its NUL replaced by `$`) followed by the message
`":File not found"` (DOS errors 2, 3; `DS:1E77`), `":No file buffers available"` (4; `DS:1E87`) or
`":Open error "` + one digit `DOS error + '0'` (`DS:1EA2`); close/read/write errors print only
`"File closing error"` (`1EAF`), `"Read error"` (`1EC2`), `"Write error"` (`1ECD`) plus the digit rule
(`CX = FFFFh` suppresses the digit). Then INT 21/4C00. `mem_alloc_chk` prints
`"Insufficient system memory - AllocBuffer"`, `mem_free_chk` `"Buffer dealloc error"`, and
`dos_freemem_chk` (4606) `"Error releasing overlay memory"` before exiting. The run-time-error strings
(`R6000 stack overflow`, `R6001 null pointer assignment`, `R6002 floating-point support not loaded`,
`R6003 integer divide by 0`, `R6009 not enough space for environment`, banner `run-time error `) sit at
`DS:535E` in END.

## 3 DOS layer and C stdio

### 3.1 MicroProse wrappers (verified, END; same code in START/SETUP/DSWAP)
All take C near-pointer arguments in DGROUP (stack, caller pops). On error they never return.

| Function | Behaviour |
|---|---|
| `dos_open(name, mode)` | INT 21/3D; `[1EDF] = 200h` (disk-buffer read position = empty); returns handle |
| `dos_creat(name, attr)` | INT 21/3C; same |
| `dos_close(fd)` | INT 21/3E |
| `dos_read(fd, n, buf)` | INT 21/3F into DS:buf |
| `dos_read_far(fd, n, off, seg)` | INT 21/3F into seg:off |
| `dos_write_far(fd, n, off, seg, add)` | INT 21/40 from seg:(off+add) |
| `dos_buf_fill()` / `dos_buf_setfd(fd)` | refill the 512-byte decoder buffer `DS:1C77` from handle `[1EE1]` (PIC/PAN LZW reader, S3.2) |
| `dos_allocmem(bytes)` / `dos_freemem(seg)` | INT 21/48 / 49; bytes rounded up to paragraphs; the free returns the DOS error (0 = ok) |
| `mem_alloc_chk(bytes)` / `mem_free_chk(seg)` | the above + printf("Insufficient system memory - A…") / ("Buffer dealloc error") and a quit via the program's exit path (`3F44` in END) |
| `bios_timer()` | INT 1A/00 → low word of the BIOS tick count |

**Port:** `dos_*` become `fopen/fread/fwrite` on the host with the same fatal-on-error behaviour (print the
original message, exit 1). File names are DOS names relative to the game directory; the port must open
them case-insensitively (the data files are upper case).

### 3.2 MSC runtime (verified on END; stdio file layer likely, from VGAME)
Do **not** port function by function; map to the host C library:

| Original | Use |
|---|---|
| `printf/sprintf/fprintf` + `_output`, `_flsbuf`, `_stbuf`, `_ftbuf` | host `printf/snprintf/fprintf`. Format flags used by the game: `%d %u %ld %s %c %02d %x` (check each call during S5); there is **no floating-point printf** in the 7 programs (no `_cfltcvt` found) |
| `malloc`, `rtl_nmalloc`, `rtl_nfree`, `_nh_*`, `rtl_nh_grow/setblock` | host `malloc/free`. Heap is the near heap inside DGROUP (size = DOS block − BSS − stack) — memory-exhaustion behaviour is not reproduced |
| `strcpy strcat strlen memcpy movedata abs` | host equivalents |
| `rand/srand` | **keep the exact LCG**: `seed = seed*0x343FD + 0x269EC3` (32-bit), result = `(seed >> 16) & 0x7FFF`; `rand_n(n) = (rand() * n) >> 15`. The initial seed is the program's own (`srand` call sites in START/VGAME decide determinism). Needed for save/replay-identical behaviour |
| `rtl_aFldiv/aFlmul/aFlshr/aFlshl/aFulshr` | C `long` operators (use `int32_t`/`uint32_t`) |
| `int86(n, in, out)` | the only callers are the BIOS/DOS INTs listed in §4-§5; replace each call site with a host function (video modes via MGRAPHIC, `INT 33h` via SDL mouse, `INT 16h/21h` via SDL keyboard) |
| `lseek/rtl_read/rtl_write/rtl_open/fopen/fread/fflush/unlink`, `_osfile` table | `FILE*`/`fd` on the host. Text-mode CR/LF translation applies to `fopen("r"/"w")` without `b` (see `rtl_write`/`rtl_read`); `ROSTER.FIL`, `*.WLD` etc. are opened binary |
| `rtl_chkstk`, `stackavail`, `_nullcheck`, `_dosmaperr`, `exit` banner | drop (keep `exit(code)` semantics: run nothing special, flush) |

## 4 Timer (verified on END, 4B8C..4DA3 and the ISR at 4BEC..4CBB)

State (END DS): `[50CB]` installed flag, `[50CE:50D0]` 32-bit accumulator, `[50D2]` reload (current),
`[50D4]` reload (applied), `[50D6]` resync counter, `[50D8]` divisor (ISR interrupts per "tick"), `[50DB]`
rate class (4..6), `[50DD]` clocks per retrace, `[50DA]` resync countdown (reset to 20), `[50E1]` resync
enable, `[50E2]` divisor countdown, `[50CC]`/`[50CA]`/`[50CD]` tick/flag bytes used by the main loop.

* `timer_install()`: `[50D8]=[50E2]=1`, accumulator 0, `timer_calibrate()`, save INT 8 (INT 21/35 →
  `cs:4C51`), install the ISR (INT 21/25 with `cs:4BEC`), `[50CB]=1`.
* `timer_calibrate()` (interrupts off): 16 samples of `pit_read()` (one per retrace edge on port 3DAh
  bit 3; timeouts return 0), sum of successive differences → average counts per retrace; `[50DD]` =
  average/2 (PIT clocks per retrace); class = (`[50DD]` × 17/16) / 0F89h; class 4..6 accepted, else
  `[50DD]=4DAEh`, class 5. `[50D8]` stays 1 on the first call; later calls set it to the class;
  `[50D4]=[50D2]=[50DD]/[50D8]` is the PIT channel-0 reload.
* ISR (every interrupt): accumulator += reload; `[50E2]--`; when it reaches 0 it reloads from `[50D8]` and
  runs the "tick": every 20th tick resync to the retrace (`4C55`: compare the 3DAh bit with the expected
  phase, adjust the reload by ±1 for one period when out of step), `[50CA]=0`, then the sound step
  (`4D51`: far call to driver slot 103 `snd_poll`, whose result steers `[50D8]`/`[50D2]` exactly as in
  RE_GUIDE §5) and, when `[50D8] != 1`, driver slot 104. The ISR chains to the BIOS INT 8 only when the
  accumulator wraps past 65 536 PIT clocks (keeps the DOS clock correct), otherwise sends EOI (`out 20h`).
* `timer_remove()`: PIT ch0 = mode 3, count 0 (18.2 Hz) and the saved INT 8 restored.

**Port (S5.1):** drop the PIT entirely. Provide one host timer thread/callback running at the frame rate
(70.09 Hz, VGA mode 13h) × class 4 (≈ 280.4 Hz) that does the tick work above (`[50CC]` frame counter, the
sound step per S3.3, `[50CA]` flag). Retrace-synchronised palette writes (`pal_write_vsync`, `pal_apply_queue`)
become "write at next frame boundary" in the host video layer. Decision needed in S3.3: keep sound at the
original 60 Hz-base (300 Hz when requested) by the S2.4 rule or derive from the 70 Hz base (unresolved, see
Open questions).

## 5 Input

### 5.1 Keyboard (MISC.EXE slots, RE_GUIDE §2.3; verified)
`kbd_empty()` (slot 90, INT 16/01: **0 = key waiting, FFFFh = none**), `kbd_get()` (91, INT 16/00 →
`scan<<8 | ascii`), `getche()` (92, INT 21/01), `joy_button(n)` (93: port 201h bit n+4 inverted → 1 =
pressed), slots 94/95 clear the Caps/Num/Scroll/Insert bits at `0040:0017`. In the port: SDL key events into
a small queue producing `scan<<8|ascii` (extended keys ascii 0 + scan); arrow/keypad scan codes as on
the IBM BIOS. PLAYER and MPS_LOGO use `bios_keyboard` (INT 16h) directly (`f360`, cmd 0/1 = get/peek).

### 5.2 Joystick (verified on END)
`joy_read_raw` (43D5): `out 201h` (trigger), loop `in al,dx` counting up to 65 535 times until the axis
bits fall; X counter = bit 0 of stick A, Y counter = bit 1; **0FFFFh on timeout** (no joystick).
`joy_poll` (43C6) reads then scales both axes. `joy_scale(axis)` (441D): the calibration table
(`DS:23C4`, 40 bytes = 20 words, copied in by `joy_set_calib` from comm block +48h) holds per axis min,
centre, max and the two slopes; output byte `[23F4+axis]`: 0..7Fh left/up, 80..FFh right/down, 7Fh at
centre; it **learns** new min/max while running. Buttons: MISC slot 93. Port: SDL gamepad axes mapped to
0..FF with centre 7F/80; keep `joy_scale` as is if raw values are synthesised, otherwise bypass it.

### 5.3 Mouse (START, END; verified)
`mouse_init` (INT 33h: reset, X range 0..280h, Y range 0..C7h, mickey ratio 8:16, position from
`[7214]` (x/2), `[7216]`), `mouse_poll` (INT 33/03: `[7214] = x/2`, `[7216] = y`, buttons → `[23F8]`
left, `[23F9]` right as 0/1), `mouse_set_pos`, `mouse_button(n)`. Port: SDL mouse scaled to 320×200.

### 5.4 Ctrl-Break and clock
`break_hook_install` saves INT 1Bh (read through the IVT at `0:006C`) and installs `break_isr`, which
only sets the flag `[2608] = 1`; the program's main loop checks it and calls `exit` through the normal path
(`3F44` in END removes the hook first). Port: Ctrl-C / window close sets the same flag.

## 6 Overlay chain and drivers
Specified in RE_GUIDE §2 (F117.COM state machine, loader, header, vector table, comm block). Port design:
* One process; top-level state machine `SETUP → LOGO → [intro] → { START → [title-2] → VGAME → END }` with
  the exit codes of §2.1 as return values of the program entry functions. DSWAP is a no-op returning 1.
* `ovl_bind(seg)` (END 45AD, all programs) becomes a no-op: the driver function tables (MGRAPHIC slots
  0-83, MISC 90-95, ?SOUND 100-104) are static C function-pointer arrays indexed by slot, call sites keep the
  slot numbers (`call far DGROUP:table+5*slot` → `drv[slot](args)`).
* The comm block is a global struct (`RE_GUIDE` §2.4); `0000:04F2/04F4` become two fields of it.
* Allocation of the off-screen page (`MGRAPHIC` slot 0, 0FA00h bytes) happens once at start-up (S3.2).

## 7 Port notes (summary for S5.1)
Implement as `platform/`: `rand.c` (exact LCG), `dosio.c` (fatal-on-error file wrappers incl. the 512-byte
`dos_buf` reader used by the picture decoders), `timer.c`, `input.c` (keyboard queue, joystick scale,
mouse, break flag), `mem.h` (`mem_copy` src,dst order; `mem_fill` zero quirk), `prog.c` (chain state
machine + comm struct). Drop: c0/_cinit/exit machinery, heap, stdio internals, `int86` (callers become host
calls), `rtl_chkstk`, `_nullcheck`.

## 8 Hand-offs
* **To S3.2 (video):** `gfx_line`, palette routines (4 rows `owner = video`), the picture/LZW readers
  (`work/s31/unassigned.txt`), `[55D0+2*page]` page-segment table, row-offset table `DS:2234`, retrace use
  of port 3DAh.
* **To S3.3 (sound):** ISR → `snd_poll` slot 103 and slot 104 in `timer_isr`; poll cadence (see §4).
* **To S3.10:** 59 shared-match rows are still unnamed (`work/s31/unassigned.txt`): picture decoders
  (video), START/END shell code shared by both (`2864`..`3e84`, S3.5), and a few small helpers (`END 0113`,
  `0242`, `0273`, `100D..10C1`) that belong to the shell/front-end modules. `spec_coverage.py` still has the
  Aces layout (`intro/ps/ap`) and must be adapted to the 7 F-117 index files before it can gate.

## 9 Open questions
1. Timer cadence for sound: the S2.4 rule (60 Hz base, 300 Hz on request) vs the calibrated rate (70 Hz →
   class 4 = 280 Hz). Needs a DOSBox-X trace of `[50D8]`/`[50DB]` in START (U4 dump) or a decision to
   keep S2.4's fixed 60/300.
2. What the second driver call from the ISR (slot 104, `[50D8] != 1`) does at run time (RE_GUIDE names it
   `snd_stop`, returning 0).
3. The 4-byte `ovl`-style far-call table of `rtl_initterm_far` (`5047`, called with `[5354..5356]`): which
   terminators are registered (probably none in the game code).
4. Joystick calibration table layout (20 words): min/centre/max slope fields confirmed by use, exact word
   order only inferred from `joy_scale` (441D); the SETUP calibration screen (S3.4) writes it.
5. `fcb_find` (START 804E, SETUP 0C84): INT 21/11 FCB search used as a presence test; which file/FCB at
   `DS:6187` is checked (START: probably the pilot/roster file) — S3.5.
