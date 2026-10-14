# Porting rules — F-117A Nighthawk Stealth Fighter 2.0 → SDL3

Faithful reimplementation of the seven programs of version 2.0 (SETUP, MPS_LOGO, PLAYER, START, VGAME, END, DSWAP)
in one executable (`f117`), VGA mode 13h with AdLib sound. Behaviour, integer arithmetic and visible quirks must match
the original, including the timing (PLAN.md Decisions 2026-10-13: one tick per retrace at 70.09 Hz, the frame-rate
estimator, the sound poll that skips every 7th tick). The game's data and executables are read from the game folder at
run time: the port never ships game data.

Specs: `../port/spec/*.md` (read `../port/RE_GUIDE.md` first; `HANDOFFS.md` for everything that crosses a program
boundary, `platform.md` §6 for this skeleton's design). Formats: `../FORMATS.md`, `../port/formats/*.md`. Ground truth
when a spec is unclear: `python tools/x86dis.py Game/<EXE> dis SSSS:OOOO LEN` (run on `work/unpacked/` for the seven
LZEXE-packed programs, `python tools/unlzexe.py`).

## Architecture

```
main.c            command line, start-up, the chain test and the other developer modes        (coordinator)
prog.h/.c         F117.COM as a function: comm block allocation, the chain loop and the exit-code
                  dispatch (HANDOFFS §1), program table, trace log                              (coordinator)
comm.h            the 5120-byte comm block as a packed struct over mem[], offsets in comments   (coordinator)
mem.h/.c          real-mode address space mem[], rd/wr and DGROUP accessors (DSW(off)), far
                  pointers, DOS memory arena (INT 21h 48h/49h/4Ah), DGROUP stack model            (coordinator)
host.h/.c         SDL3: window/present (4:3 or 1:1), 70.09 Hz tick, XT key bytes, mouse, gamepad,
                  OPL2 on the audio thread (Nuked-OPL3), game file lookup, headless mode          (coordinator)
platform/vga.*    VGA model: 256 KB planar memory as linear pages, CRTC start/offset, pel panning,
                  DAC, 70.09 Hz retrace on port 3DAh                                              (coordinator)
platform/timer.*  the tick source: timer_ticks and a hook for the ported ISR body                 (platform)
programs/         PHASE 4 ONLY: placeholder.c has a stand-in for each program                     (replaced in S5)
launcher/         the wxWidgets launcher "F-117A Nighthawk" (-DF117_LAUNCHER=ON, launcher/README.md)
../third_party/nuked-opl3   the OPL2 emulator (LGPL-2.1), a separate static library
```

Only `host.c` includes SDL; everything else talks to the host through `host.h`. Other modules (S5) get a directory
under `src/` named after their spec (`video`, `sound`, `frontend`, `shell`, `simflow`, `render3d`, `flight`,
`debrief`) and are added to `F117_MODULES` in `CMakeLists.txt` (one glob per directory). S4.2 generates their headers
and the symbol tables.

## The chain (`prog.c`)

F117.COM's loop is `chain_run()`: `SETUP → MPS_LOGO → [PLAYER intro] → { DSWAP 0 → START → DSWAP 1 → [PLAYER
title-2] → VGAME → DSWAP 2 → END }`, with the exit codes of HANDOFFS §1 (`prog.h`). A program is `int <name>_main(const
char *tail)`; its `exit(n)` is `return n`. The comm block lives in `mem[]` at the segment stored in `0000:04F0`;
`comm_get()` is the C view. The drivers (MGRAPHIC 0-83, MISC 90-95, sound 100-104) are static function tables indexed by
slot, so `ovl_bind` is a no-op (platform.md §6). DSWAP always returns 1.

## Rules

* **One module per spec.** Do not restructure another module's files; declare what you export in your own header.
* **DGROUP offsets in comments.** Every global the original keeps at `DS:xxxx` is read and written in `mem[]` at that
  offset (`DSW(0xE096) /* comm far ptr */`), named from the generated `symbols_*.h` (S4.2); never shadow game state in C
  variables that outlive a function. Code-segment tables are read in their segment.
* **Bugs stay as specified.** Keep the quirks the specs mark as faithful (e.g. `mem_fill` always zero-fills, the comm
  word at `+32h` overlapping the night byte at `+33h`). Mark deliberate deviations `/* PORT: ... */` and doubts
  `/* TODO(verify): ... */`.
* **Integer widths exactly as the original** (`u8/s8/u16/s16/u32/s32`, casts where the CPU truncates). A divide
  overflow or division by zero ends the program as the MSC runtime does (R6003, platform.md §3.2).
* Every busy-wait loop of the original (tick waits, key polls, port 3DAh polls) calls `host_pump()` once per iteration;
  3DAh polls become `vga_wait_vretrace_*()`.
* Your files must compile without warnings under `-Wall -Wextra`.

## Timing, input, sound

* `host_pump()` delivers one tick per retrace (`VGA_FRAME_NS_NUM/DEN`, exact rational, no drift) to the handler that
  `platform/timer.c` installs; the per-program ISR body (frame counter, 7th-tick skip, `[3660]` estimator, sound poll) is
  S5.1/S5.9 and goes in through `timer_set_hook()`. The sound poll runs on the main thread inside the tick; its OPL
  writes take the audio lock (`host_opl_write`).
* Keyboard: SDL events → XT set-1 bytes (E0/E1 prefixes) → `host_set_kbd_handler`. Mouse: INT 33h model (START and END);
  joystick: first gamepad on the port 201h model. Alt+Enter toggles full screen.
* Sound off is `SDL_AUDIO_DRIVER=dummy` (the launcher sets it).

## Build and run

From the repository root with MSYS2 mingw64 first on `PATH` (`export PATH=/c/msys64/mingw64/bin:$PATH`):

```
cmake -S f117port -B f117port/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DF117_LAUNCHER=ON
cmake --build f117port/build && ctest --test-dir f117port/build
f117port/build/f117 --game-dir Game          # placeholders: window with test patterns, Esc or close to exit
```

Needs SDL3 (`mingw-w64-x86_64-sdl3`); the launcher also wxWidgets 3.2 (`mingw-w64-x86_64-wxwidgets3.2-msw`), leave
`-DF117_LAUNCHER` off without it. On Windows the build copies `SDL3.dll` beside `f117.exe` (and the launcher's DLLs beside
it); keep them together in a release.

Options: `--game-dir DIR`, `--scale N`, `--aspect 4:3|1:1`, `--fullscreen` (what the launcher passes), `--headless` (no window or
audio), `--trace`, `--check` (are the files of the chain in the game folder?), `--chain-test`, `--tick-test [MS]`.
ctest runs the chain with the placeholders (`chain`), counts ticks over a second (`tick_rate`) and the missing-file
report (`check_missing_files`). Developer aids (environment, `host.h`): `F117_SNAPSHOT_DIR` (+ `F117_SNAPSHOT_MS`) saves
presented frames as PNG, `F117_KEYS` scripts XT key presses, `F117_TRACE=1` logs program switches. Throwaway
verification programs go in your scratch area; do not create HTML pages.

## Platform layer (`src/platform`, S5.1)

Spec `port/spec/platform.md`; prototypes in `gen/module_platform.h`, the port's own API in `platform/platform.h`.

```
exeload.c    LZEXE 0.91 / MZ reader and the DOS loader: PSP, image at PSP+10h, relocations, DGROUP (the `mov di,imm16` of the
             c0 start-up, NOT the EXE header's SS), BSS zero. Blocks are owned by the program and freed at exit
startup.c    platform_exec() = INT 21h/4Bh + c0_start + exit(): exit_fn() anywhere ends the program with its code (longjmp);
             per-program DGROUP offsets of the library state (ProgDs: rand seed, break flag, joystick table, mouse, decoder
             buffer); the near heap in DGROUP (offsets, rtl_malloc_ds); run-time error messages R60xx
rtl.c        mem_*/str*/movedata (forward copies, mem_fill zero quirk), rand/srand/rand_n (exact LCG), the long helpers,
             dist_approx, the 8086 DIV rules (rtl_div32_16 -> R6003)
dosio.c      INT 21h file/memory wrappers (fatal on error, case-insensitive names under --game-dir), the 512-byte decoder
             buffer, platform_load_file_paras (simflow.md 2 chunk bug), low-level open/read/write with text mode
kbd.c        BIOS INT 9 / INT 16h model on the XT byte stream; kbd_set_int9_hook() = a program's own INT 9 handler
input.c      joystick (gamepad -> synthetic 201h counts -> the original joy_scale incl. its learning), mouse, Ctrl-Break
timer.c      tick source, BIOS clock 0040:006C, timer_set_hook/timer_install/timer_remove
selftest.c   --platform-test (scratch --game-dir!) and --exe-test (reads Game/); vectors from the ORIGINAL code under
             Unicorn: `python port/notes/s51/platform_emu.py` writes selftest_vectors.h
```

Rules for the modules that follow:

* Pointer arguments of the platform routines are host pointers into `mem[]` (`DSP(off)`, `mp(seg, off)`); `ds_off(p)` goes back.
* A program body is `int <name>_main(const char *tail)`; `exit_fn(n)` from any depth ends it (do not call `exit()`).
  Programs install what the original installs and remove it: `timer_set_hook(body); timer_install(); ... timer_remove();`,
  `break_hook_install()/remove()`, `kbd_set_int9_hook(h)` (VGAME's keypad-as-stick ISR, flight.md 2.4: reads `raw`, the BIOS
  part runs after it). The chain removes them at program exit anyway.
* The keyboard words are `scan << 8 | ascii`; `bios_keyboard()` returns a 16-bit `int` (F-keys above 8000h are negative, as in the
  original). The MISC slots 90/91 are `kbd_key_waiting()` / `kbd_key_get()`, slot 93 is `joy_port_201()`.
* Not ported (platform.md section 7): the MSC stdio/printf internals (`rtl_output*`, `rtl_stbuf`, `rtl_flsbuf` ...: use libc
  `snprintf`), `int86` (every call site becomes a host call), `rtl_nh_*`/`rtl_chkstk`/`rtl_initterm*`. `fcb_find` belongs to the
  shell (S5.4).

## Symbol pipeline (`src/gen/`)

The names and addresses of the original come from the specs, not from memory. Pipeline:

1. Spec symbol CSVs, `port/spec/<module>_symbols.csv` (hand-written, one per spec).
2. `python tools/merge_symbols.py` writes `port/symbols.csv` (all specs, one table).
3. `python tools/gen_symbols.py` writes `src/gen/symbols_<image>.h`: setup, mps_logo, player, start, vgame, end, dswap,
   and the driver images asound117, mgraphic, misc, asound_log.
4. `python tools/gen_module_headers.py` writes `src/gen/module_<spec>.h`: platform, video, sound, frontend, shell,
   simflow, render3d, flight, debrief.

The generated headers are committed. Re-run steps 2-4 after any change to a spec symbols CSV (or to `port/symbols.csv`)
and commit the output with it. `ctest` runs `gen_symbols_current` and `gen_module_headers_current`, which regenerate in
memory and fail when a committed header is stale; `python tools/gen_symbols.py --check` says the same without a build.

Naming rules:

* DGROUP globals are `<IMAGE>_DS_<name>` (the DS offset; use with `DSB/DSW/DSS/DSL`). Code-segment variables are
  `<IMAGE>_CS_<name>` (offset) and `<IMAGE>_CSSEG_<name>` (segment). `<IMAGE>` is the image name in capitals.
* Functions have no macro: the symbols header has an index line (address, C name, owner, spec section); the prototype
  is in the module header under the same C name.
* A name used at several addresses in one image gets the address as a suffix: `_OOOO` for a DS offset, `_SSSS_OOOO` for
  a segment:offset (`mat_mul_0000_E578`, `edge_L_22A1`). The generator prints every rename.
* A name that is not a C identifier gets a leading underscore (`3dt_records` becomes `_3dt_records`). A function whose
  name is a C keyword or a common libc name gets `_fn` (`exit` becomes `exit_fn`), so it cannot clash with a system
  header.
* A name that several modules document (each spec describes its own copy of a routine, e.g. `dos_read`) is declared once,
  in the first module in the generator's order with a plain C signature. The other modules' headers say so in a comment.
* Rows the spec marks `dead` (in the notes or the type column) are not ported: they are DEAD in the index and listed at
  the end of their module header.
* A prototype whose type column is not plain C is `void name(void); /* TODO signature: ... */`. Fix the type column in
  the spec's symbols CSV, then re-run the pipeline. Do not edit a generated header by hand.

Every port function's comment cites its spec section and its original address, for example
`/* 0000:E578 mat_mul: spec simflow.md "0000:e578 mat_mul" */`. The index line in `symbols_<image>.h` has the same two
facts, so `grep` on the address finds both.

This port runs one image at a time, so the Aces shared-library layout (`symbols_layout.h`) is not generated.
