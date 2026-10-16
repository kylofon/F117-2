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
programs/         PHASE 4 ONLY: placeholder.c has a stand-in for VGAME and END (S5.5 .. S5.8 replace them); the stand-ins of the
                  programs that read game files (*_stub, START too) are used only by --chain-test, which needs no game files
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
audio), `--trace`, `--check` (are the files of the chain in the game folder?), `--chain-test`, `--tick-test [MS]`, `--frontend-test DIR`, `--keys NAME=CODE,...` and `--keymap-test` (see "Key bindings").
ctest runs the chain with the placeholders (`chain`), counts ticks over a second (`tick_rate`) and the missing-file
report (`check_missing_files`). Developer aids (environment, `host.h`): `F117_SNAPSHOT_DIR` (+ `F117_SNAPSHOT_MS`) saves
presented frames as PNG, `F117_KEYS` scripts XT key presses, `F117_TRACE=1` logs program switches. Throwaway
verification programs go in your scratch area; do not create HTML pages.
Errors the player cannot see (the launcher hides stderr) go to stderr and `f117.log` beside the program (`host_log_error`): every
`host_fatal`, a crash (`host_install_crash_log`: the exception code and the program that ran) and a quit of the chain that the player did
not ask for (`prog.c f117_quit`: a DSWAP or END code the original never gives, an abort without a closed window). The chain quits only on
the programs' exit codes or a closed window; the Esc handler of the placeholders is installed for `--chain-test` only (before 2026-10-16
an Esc anywhere, e.g. after a crash in flight, ended the game when that program finished).

## Key bindings (`src/keymap.c`, launcher `keys.cpp`)

`--keys "name=code,..."` (what the launcher passes for the actions not on their default keys) remaps host keys before the
game's keyboard layer sees them (PORT: marked, the game code stays faithful). `host.c key_event` asks `keymap_map()` for
each XT byte; with no `--keys` it returns -1 at once, so the byte stream is untouched. A code is the XT set-1 make code
with `0x100` = E0 prefix (arrows, Insert...) and `0x1000` Shift, `0x2000` Ctrl, `0x4000` Alt held with it; `0` = no key.
Only while VGAME runs (`prog_current_id()`): a key bound to an action is sent as the action's own key (the keys the
original reads: `simflow.md` Part 2 section 2.1 for 1E9E, Part 10 for CBA0, the arrows of the keypad-as-stick hook), the
modifiers it needs pressed around it; the default key of an action bound elsewhere is dropped; every other key passes.
The 56 actions (flying, weapons and countermeasures, views, displays and game) are listed identically in `keymap.c` and
`launcher/keys.cpp`. Fixed: the keypad stick, Esc, Alt+Q / Alt+B / Alt+S, Alt+Enter. `f117 --keymap-test` (ctest `keymap`)
checks the parser and the key streams. The launcher keeps the keys in `settings.ini` `[Keys]`.

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

## Video module (`src/video`, S5.2)

Spec `port/spec/video.md` (section 14 = what the port found); prototypes in `gen/module_video.h`, the structures and helpers in
`video/video.h`. The MGRAPHIC slots are plain C functions (names = the slot table); struct arguments are host pointers into `mem[]`
(`TextCtx`, `BlitCtx` in `video.h` are packed overlays); a "page" is a segment, `seg_ptr(seg)` gives the host pointer (A000..AFFF =
the VGA model, everything else `mem[]`).

```
mgraphic.c   the driver: pages/draw segment/offset/colour, text (7 fonts from MGRAPHIC.EXE's image), lines, span fills, blits,
             RLE sprite pool and scaled sprites, flip, effects. State = C statics, reset by video_init() (once, in main before the
             chain; fonts are read from the game folder, silently none without it). gfx_alloc(2) is wired into prog.c (comm+20h)
proglib.c    what START/END/DSWAP carry themselves: gfx_line, gfx_fill_rect, pal_write_vsync/pal_set/pal_set_default,
             pal_apply_queue. Their tables are in the program's DGROUP at different offsets: `vprog[]` (START, END, DSWAP; VGAME has
             its own 14A2:000E set, simflow). The fades (pal_fade*, shell/debrief) and the palette queue push stay with their modules
pic.c        .PIC: lzw_reset/lzw_next/rle90_fill/pic_begin/pic_row + pic_to_page/seg_a/seg_b/seg_c (DOS handle or picture in memory),
             pic_decode_file (tests/tools), pal_read_file
pan.c        .PAN whole-file forms: lz_decode (PLAYER 0BE2), pan_parse, pan_apply_cel (PLAYER 1006). PLAYER's streaming ring buffer is S5.3
videotest.c  --decoder-test, --video-test;  vselftest.c  --video-selftest (+ video_vectors.h, generated)
```

Rules for the modules that follow:

* Call `page_seg_set(n, seg)` for the off-screen pages the program uses (page 0 = A000 is built in); text, sprites and the blits take
  page NUMBERS, `line`/`poly_fill`/`hud_tick_strip` the draw segment (`page_select`/`seg_set`) plus the draw offset.
* Row offsets are y*320 (the original's table is only valid for y < 200: do not draw rows outside 0..199 through the row-table calls).
* Palette writes: `dac_set` (no wait) or `pal_write_vsync` (waits for one retrace, grey-scale path honoured); a palette is 768
  6-bit bytes. `pal6_to_8` is the host's expansion.
* The grey-scale / `pal_apply_queue` details: the queue routine spins on the program's tick flag (START DS:ADFC, END 50CC), so the
  program's ISR body (S5.4) must set it; a program that is not in `vprog[]` makes `gfx_line`/`pal_*` fatal ("not part of ...").
* .SS sprites (MPS_LOGO 006D:0112) and VGAME's `pal_flight_load`/`pal_read_dac` are not here (frontend / simflow).
* Tests: `ctest` `video_selftest` (532 scenarios hashed from the ORIGINAL driver and START's library under Unicorn:
  `python port/notes/s52/video_emu.py` rewrites `video_vectors.h`; needs `Game/` and `work/unpacked/`), `video_decoders`
  (`port/notes/s52/decoder_ref.py`: every PIC/PAL/PAN/font against `tools/picdec.py`, `panlib.py`, `mgfont.py`'s parse; skips with
  77 without `Game/`), `video_screen` (`f117 --video-test`, PNGs in `work/s52/screen`).

## Front end (`src/frontend`, S5.3)

Spec `port/spec/frontend.md` (section 8 = the closing pass); prototypes in `gen/module_frontend.h`, the DGROUP offsets, the sound call points
and the test hooks in `frontend/fe.h`. SETUP, MPS_LOGO, PLAYER and DSWAP are real now (`prog_table`); VGAME and END are still the
placeholders (START is S5.4, below); `--chain-test` swaps the five programs that need game files for their `*_stub` stand-ins (`prog_use_stubs`) because it runs without game files.

```
textmode.c   BIOS text mode 03h on B800:0000 (INT 10h 00/02/09/0E, INT 21h/09): cursor, teletype with wrap and scroll
misc.c       MISC.EXE slots 90-95 (kbd_empty_misc, kbd_get_misc, getche_misc, joy_button_misc, kbd_locks_clear)
setup.c      SETUP: banner, joystick question + calibration, sound menu, comm block; exit 24h. Strings and layout are read from the loaded
             image's DGROUP (DSP(0x4BA) ...), the DOS console is textmode.c
mpslogo.c    MPS_LOGO: the .SS loader (ss_load, palette merge into the DAC slots, remap), ss_draw (the RLE grammar of 006D:05BE), the
             BIOS-clock paced animation, exit 0 / 1 / 2 / 3
player.c     PLAYER: tick ISR body (timer_step, 6 of 7 retraces), tick_callback, cel_show, the fade, the PAN ring buffer
             (ring_make_room, pan_decode_chunk reading the 4 KB file buffer, pan_play_file), poll_input, player_exit
dswap.c      DSWAP: exit 1 (the floppy swap prompt is never shown), atoi / atol
sndstub.c    the sound call points (fe_snd_*, snd_*_log): no-ops marked TODO(S5.9)
fetest.c     --frontend-test (below)
platform/vgatext.c  the text screen on the window (640 x 400, 8 x 16 cells from an 8 x 8 font, CGA colours, no cursor)
```

* PLAYER's variables live in its DGROUP at the original offsets (`PLAYER_DS_*`), linear addresses are `u32`; the ring buffer is the largest
  free DOS block. `fade_measure` is not ported (PORT: 256 colours per tick). The timer ISR body is installed with `timer_set_hook(timer_step)`;
  PLAYER does not use `pal_apply_queue` (no tick flag needed). Names shared with MGRAPHIC (`screen_off`, `screen_on`) are the video module's.
* Sound: `fe_snd_cmd(3)` at the first `poll_input`, `fe_snd_cmd(0)` in `player_exit`, `fe_snd_cmd(2)` polled before the exit when several files played,
  `fe_snd_poll()` in `timer_step`, `snd_init_log / snd_cmd_log(2) / snd_cmd_log(0) / snd_shutdown_log` in MPS_LOGO. S5.9 fills them in.
* `FeTest fe_test` (fe.h) holds the hooks of the tests (scripted getche answers, logo without waiting, PLAYER without the fade and with a small
  ring, per-frame callbacks); all idle in a normal run.
* Tests: `ctest` `frontend` (`python port/notes/s53/frontend_ref.py f117 Game`): the ORIGINAL SETUP and MPS_LOGO run under Unicorn on a small
  DOS/BIOS machine (`port/notes/s53/dos_emu.py`; needs `work/unpacked/`): the port's text screens and comm block, and the screen + DAC CRC of all
  82 logo frames, must equal them; every cel of the 6 .PAN files through PLAYER's ring buffer (a 160 KB ring that wraps) equals the Phase 2
  decoder; the fade formula, the 60.08 Hz clock and the logo pacing are measured. `frontend_chain` is the scripted run
  (`F117_KEYS`), SETUP -> MPS_LOGO -> PLAYER -> DSWAP -> START placeholder -> ... -> quit. Both skip (77) without `Game/`.

## Shell (`src/shell`, S5.4)

Spec `port/spec/shell.md` (Part F = what the port found); prototypes in `gen/module_shell.h`, accessors and the test hooks in `shell/shell.h`. START is
real now; `--chain-test` swaps it for `start_stub` (no game files). The ID quiz (057E, 0703, 0786) is dead code in this version (the call is three
NOPs), so it is not ported and there is no `--skip-id`.

```
ui.c         the toolkit: text (ui_text, _wrap, _shadow, _centered), popups, frames, dialogs, the menu loop and ui_poll_input (cursor, arrow-key jumps,
             joystick/mouse), the waits, the button-highlight animation, the palette queue and fades, ui_show_screen, the quit prompt (Alt-Q)
screens.c    ready room, home card, hangar and launch, arming, the briefing map (route, rings, site icons, buttons), the map helpers (ring, route line,
             labels) and the sine routines they use
orders.c     the pilot-orders screen (ordrscrn) and the folder pages (brief_orders_screen, objective texts, the campaign date)
roster.c     ROSTER.FIL load/save, the roster screen with hover info, name entry, the options panel, roster_startup_sync
mission.c    mission_generate (4239, P0..P13 of the spec), the WLD reader/export, sites, units, grid references, clock strings, mission_site_setup (40F0)
terrain.c    the .3DG / .3DT tile maps and object records, map_nearest_object and map_pick_object
start.c      start-up (shell_init/comm/gfx/sprites), ui_main, shell_exit, START's ISR body (DS:ADFC++ for pal_apply_queue, then the sound poll)
shelltest.c  --shell-test and F117_SHELL_SCRIPT (below)
```

* The loaded image keeps the strings, tables, dialog descriptors, button tables and colour triples at their DS offsets: the code reads them with
  `DSB/DSW/DSP` and the generated `START_DS_*` names (a near pointer stored in a table is a DS offset). The callbacks stored in menu tables are code
  offsets: `menu_cb()` in ui.c maps them to the port's functions. Struct arguments are host pointers into `mem[]`.
* Every busy wait is `pal_apply_queue()` (it pumps the host and waits for the ISR flag) or a `kbd_empty_misc()` loop; the fades do one `pal_set` per
  retrace (15 per speed unit) instead of calibrating on the BIOS clock.
* Sound call points are `sh_snd(cmd)` (= `fe_snd_cmd`, `TODO(S5.9)`); the commands are those of the spec. The ISR body calls `fe_snd_poll()`.
* The mission and the world go to VGAME in the comm block exactly as HANDOFFS C6 says (`wld_export_to_comm`: comm+7Ah, layout in the function);
  the exit code is 0Ch, or 0Dh when the departure site has bit 200h of its flags (no hangar), 0 for a quit.
* Tests: `ctest` `shell` runs the ORIGINAL START.EXE under Unicorn (`port/notes/s54/shell_emu.py`, drivers loaded as overlays, stub sound driver,
  keyboard queue, timer interrupts injected) through `shell_ground.txt` (create pilot, options, orders, briefing modes, orders pages, arming, launch)
  and `shell_quit.txt` (roster read and saved again) and compares the screen + DAC CRC at every step, the comm block (the generated mission for the
  fixed seed) and ROSTER.FIL. The port reads the same scripts (`shelltest.c`; steps run at the 3rd `ui_poll_input` after the previous one).
  `shell_ref.py --dump f117 Game REFROOT` also writes the screens of both sides, `shell_ref.py --diff REFROOT ground STEP` shows the pixel difference.
  `shell_chain` is the whole chain with `F117_KEYS` (SETUP, intro) and `F117_SHELL_SCRIPT` (START): new pilot, strike mission, launch, VGAME (real since S5.5, a few frames) and END
  placeholder, START again, quit. Dev aids: `F117_SHELL_DUMP=dir` saves the screen + DAC of every script step, `F117_SHELL_POLLS=1` prints the cursor at every poll,
  `F117_TRACE=1` prints the steps. Both tests run on a scratch copy of the game folder (`work/s54/game`, marked SCRATCH.MARK): START writes ROSTER.FIL.

## Sim flow (`src/sim`, S5.5)

Spec `port/spec/simflow.md`; prototypes in `gen/module_simflow.h`, the DGROUP names in `gen/symbols_vgame.h`, the port's own additions in `sim/sim.h`
(accessors, the test hooks, the sound call points), `sim/sim_priv.h` (what the files share) and `sim/sim_stubs.h`. VGAME is real now (frame loop, HUD,
MFDs, keys); `--chain-test` still swaps it for `vgame_stub`. The 3D scene is render3d's (S5.6, below), the flight model and the weapons /
AI / detection / damage functions are empty stubs until S5.7.

```
sim_main.c    VGAME start-up (vgame_start), sim_init (world from comm+7Ah, files, palettes), the frame loop (1BBC), shutdown and the export back to the comm
              block (mission_world_export, mission_end, mission_log_event), the timer ISR body (vgame_tick: 70.09 Hz tick, sound poll skipped on every 7th),
              the .3DG/.3DX/.3DT/.WLD loaders, pic_show, frame_delay
sim_step.c    the per-frame step 39C4 (clock, fps estimator with its 60 ticks/s assumption, mission set-up, airfield objects, zones, estimator)
sim_keys.c    cba0, the key interpreter (the full table, the debug and nav keys, the tail with the lamps, doors and gear)
sim_input.c   the keypad-as-stick INT 9 hook, the Alt-Q / Alt-P / Alt-B screens, the key fetch of 1E9E (the rest of 1E9E is the flight model, S5.7)
sim_view.c    2F82 view_frame (the 13 view modes, matrix B156 via E26B, picture reloads, the scene call, the pieces pasted over the window), 03CE r3d_scene_draw
              (now in render3d/r3d_scene.c; the TESTSTUB marker stays here for the sim tests)
sim_overlay.c 7898 hud_overlay_frame: the text overlay of the frame (messages, tapes in the small styles, ILS, fps, ...)
sim_hudasm.c  10A8 hud_draw_front (tapes, heading, frame, reticle, pitch ladder), hud_layout_init / hud_color_init from the original immediates
sim_cockpit.c cockpit_keyblit, mfd_icon, panel refresh, 17A3 cockpit_draw_adi
sim_mfd.c     the left MFD (map, zoom, route), the right MFD pages and their selection, fuel bar; sim_pages.c the nav / weapons / objectives pages and the ILS;
              sim_scope.c the situation scope
sim_mfdview.c bf71 mfd_view_render (the sensor window) with the sky / ground bands and the FLIR tint
sim_objovl.c  a17e obj_overlay_frame (projectile markers, tracers and their hit tests, the explosion, the target symbology, the photo shutter, the bomb
              cues, the radio beacon), the helpers b339..b7f8, and b85d r3d_project_point (render3d's, needed here)
sim_hud.c     the HUD primitives (lines, boxes, text, digits, lamps); gfx2d.c the 1001 edge tables, the clipped line and the polygon / rectangle fills
sim_pal.c     palettes (flight, day/night, fades, DAC read)    sim_lib.c the 109D fixed-point library, the C runtime pieces    sim_misc.c small per-frame pieces
sim_stubs.c   the stubs (each marked `STUB ssss:oooo name`; the test harness makes the original return 0 at the same address)
simtest.c     --sim-test: start-up dump, the ISR counters (ticks mode), the scenario runner
```

* DGROUP is VGAME's (`cur_dgroup`); the initial image comes from the real EXE, so strings and tables sit at their DS offsets. A near pointer in a table is a DS
  offset; a pointer argument of a function is a host pointer (`DSP(off)`, `ds_str(off)`). `uN` / `sN` casts mark the 16-bit wrap of the original.
* Timing: one host tick per retrace. `vgame_tick` clears the frame-sync flag [262A], counts [2646] / [264A] / [2648], skips the sound poll on every 7th tick
  (6/7 of 70.09 Hz = 60.08 Hz), lets the poll result switch the divisor and calls `sim_snd_noise` class times; a closed window ends the mission (PORT).
  `frame_delay` and the sync wait in `r3d_scene_draw` busy-wait on those counters; the test harness skips both.
* The deviations from the original that tests accept: the garbage row table for y < 0 or y >= 200 in the HUD line draws (the port uses y * 320); the stale
  `[bp-2Eh]` / `[bp-30h]` locals of a17e read by the bomb cue when the pipper block was skipped (the port starts them at 0); `sim_matrix_from_angles`
  (E26B) and `r3d_project_point` (B85D) are render3d's functions, written here because the view and the overlay need them.
* Tests (`ctest sim_vgame`, `port/notes/s55/sim_ref.py`, skip 77 without Game/; it needs the comm block of the `shell` test or `F117_COMM`): `startup` compares
  the whole DGROUP, the three pages and the DAC at the entry of `sim_run` with the original VGAME under Unicorn (`vgame_emu.py`, segment-valued words masked);
  `ticks` runs the ISR body 7000 times (counters, 6000 polls = 60.08 /s, the rate switch, snd_noise); `calls` runs 1262 scenarios (`sim_scn*.py`) of single
  functions and of frame-loop bodies (view, HUD, ADI, MFDs, overlay, step, keys, export) on both sides from a snapshot of the machine and compares DGROUP, the
  pages row by row, the DAC and the comm block (so the exported world and the event log byte for byte). The stubbed functions return 0 on both sides.
  `--only calls:FAMILY`, `--bisect ID`, `--dump ID` (pixel crop of the first difference), `--trace ID`, `PROBEFN=name:nargs` (arguments of an original near function).
  `shell_chain` runs the real VGAME for `F117_VGAME_FRAMES` frames (headless) between START and END. `debrief_chain_sea` (end_ref.py --chain-sea)
  flies a Libya strike into the sea: `F117_VGAME_DIVE=N` puts the aircraft in a dive off Tripoli after N frames and presses Esc; the crash
  (mission_end(1)), END (killed in action) and START follow. The `fm_sea*` scenarios are the same dive against the original 39C4 byte for byte.

## Render3d (`src/render3d`, S5.6)

Spec `port/spec/render3d.md`; prototypes in `gen/module_render3d.h` (typed from the `type` column of `render3d_symbols.csv`), the work buffers and the
register-interface routines in `render3d/r3d.h`. Every routine works on VGAME's DGROUP at the original offsets; pointer arguments are DS offsets (ES offsets
in the 3DX bank for 11EB), so the buffers keep their adjacency (part slots 7B1C.. vs the eyes 7C3C, edge records 5EBA.., the 12EF / 1359 row tables).

```
r3d_scene.c    03CE r3d_scene_draw, DAE8 / DB71 / DC64 / DCA9 / DB3E set-up and end, E3E2 / E54B, E7AC placement, E8A2 / E8B5 cull, 1088:0120 depth shade,
               C6B8, BD8F / BEE1 / DCC0 submitters, DDFE / DF08 sorted queue, 1AD6 / 1B1B / 1A85 sprites
r3d_terrain.c  0804 / 0865 / 0FB1, 04DA terrain walk (path A quirk), 119C / 11F2 / 1353 / 13AC / 13E3 map MFD, 0FFB / 77D9 collision probe, BC12 / BC77 / BCD7
r3d_world.c    9ACA world pass (designators of the weapons logic kept inside), BA67, C73C, C834; bbb4 / bb86 / bb9e (owner weapons, here until S5.7)
r3d_object.c   segment 11EB: 000C camera, 00D3 / 01B8 objects, 02B6, parts 031B / 0440 / 04CD / 04F9, planes 0565 / 0658 / 0678, BSP 0E5F, vertices 06B9 / 0819,
               projection 0906 (+0975 as a checked division), 0990, edges 0A37 / 0B3F, faces 0C7A / 0CAC / 0CED, 0F9B
r3d_clip.c, r3d_raster.c, edge2d.c, r3d_sky.c, r3d_math.c   the leaf routines of S5.6a (12EF, 1359, 1001, 1442, 1433)
r3dtest.c      --r3d-test: the leaf routines against the Python references (ctest render3d_leaf)
```

* B85D / BA13 (point projection) and E26B (`sim_matrix_from_angles`) stay in `src/sim` (S5.5). The sim code calls the scene functions through the
  `sim_*` wrappers of `sim_stubs.c` (host-pointer viewports become DS offsets there).
* PORT: 11EB:0281 only saves IVT[0] into DS:5EB6 (the port divides with a check, 0906); the raster writes to `seg_ptr(DS:85EA)` (page 2 is in mem[]).
* Tests: `render3d_leaf` (S5.6a) and `render3d_golden` (`port/notes/s56/r3d_golden.py`): the ORIGINAL 03CE / 2F82 / bf71 / 119C / 77D9 under Unicorn and
  `f117 --render-test` (the sim scenario runner with r3d_scene_draw enabled) from the same running-mission snapshot; DGROUP, pages, DAC and comm block byte for byte.
  `--only ID` runs one scene, `--fresh` rebuilds the cached references. The sim tests (`sim_vgame`) keep 03CE hooked (TESTSTUB) but now run the real
  bf71 / 119C / BC12 / BCD7 / 1A85 on both sides.

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
