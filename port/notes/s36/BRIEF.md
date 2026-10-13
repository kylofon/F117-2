# S3.6 brief (shared by all readers)
Task: spec of VGAME.EXE (the flight simulation) so that a Sonnet session can port ANY function from its section
alone: data layouts with offsets, constants, call order, edge cases, original bugs. You own an address range (below).
Overall S3.6 scope = VGAME main loop, frame timing, mission state + lifecycle, HUD, cockpit, MFDs, view modes, messages,
menus, input, entry from START and hand-off to END. Rendering internals (3D transform/clip/polygons/terrain/sky/sorting) are
S3.7 and flight model/weapons/AI/detection/damage are S3.8: for such functions write a SHORT entry (what, args, globals
and structure fields read/written, callers/callees, ~3-8 lines) - enough to find them again - and mark owner render3d / flight.
For everything else (sim flow, input, HUD, cockpit, MFD, views, messages/text, menus, mission state, timing, loading,
palettes, sound calls) write the full algorithm.

## Where things are
* VGAME.EXE unpacked: `work/unpacked/VGAME.EXE` (flat image; code seg 0000 = image offset; far code segments SSSS:OOOO =
  image offset SSSS*16+OOOO). DGROUP = 1E23 (DS:oooo = image 0x1E230+oooo; = file offset 0x1EA50+oooo because the MZ header is 0x820 bytes). Compiler MSC 5/6, small model, near calls,
  `enter/leave` frames, args pushed right to left (first arg at [bp+4]); far segments 1001..14A2 are hand-written or
  separately linked modules, called with `lcall seg, off`.
* Annotated disassembly (DO NOT dump it whole; read in chunks with Read offset/limit or sed -n, or run it for just your range):
  `python tools/dsann.py VGAME 1e23 FROM TO > work/s36/<yourfile>.txt` then read that file in chunks. FROM/TO are hex offsets in
  segment 0 (`1e9e 2f82`) or `SSSS:OOOO SSSS:OOOO` for a far segment (`10a8:0000 10a8:0d90`). It prints one block per
  indexed function (`== off size ...`) with DGROUP strings, resolved call targets (names from the symbol csv files) and
  MGRAPHIC/sound driver slots (`lcall 0x1e23, 0x1ec ; slot 42 blit_rect`; `lcall 0x1d50, 0xeXX` is the same slot table via
  another segment value, also annotated). Jump targets after `jmp`/`jcc` are offsets in the same segment.
  A function can contain code that the index lists as a gap (a few exist: 1c3a-1d76, 5616-5730 ...): if a `jmp` leaves the
  block, disassemble the target (`python tools/x86dis.py work/unpacked/VGAME.EXE dis 0000:OOOO LEN`).
* Cross references: `python tools/vgxref.py g E548 C07A` (which functions read/write a DGROUP offset), `python tools/vgxref.py f 1e9e`
  (callers, callees, DS offsets read/written), `python tools/vgxref.py name PATTERN`. Other readers' symbols appear in
  `work/s36/syms_*.csv` as they get written (vgxref reads them) - use their names when you meet their functions.
* Full function list with sizes/callers/strings: `work/s36/funclist.txt`. DGROUP strings: `work/s36/dgroup_strings.txt`
  (`regn.xxx lb.xxx pg.xxx nc.xxx ce.xxx vn.xxx me.xxx ku.xxx cu.xxx ko.xxx` at DS:0042.. are theatre file stems:
  the disassembly shows `"xxx"` for immediates that merely look like addresses - check the instruction before trusting an annotation).
* Existing knowledge: search (grep, never dump) `port/RE_GUIDE.md` (comm block, driver slots, chain), `port/spec/platform.md`
  (RTL, timer, keyboard, joystick, rand/srand LCG), `port/spec/video.md` (every MGRAPHIC slot: draw segment/offset, pages, TextCtx
  at section 3, HUD tick strip slot 11, explosion/shake slots 46/79, palette), `port/spec/sound.md` (snd_cmd numbers), `port/spec/shell.md`
  (comm fields, pilot record at comm+1272h, mission/site/unit/WLD records created by START, mission generator), `FORMATS.md`
  (flight palette, 3DX objects, terrain, WLD). Do not re-derive what they state; cite them and add what VGAME does with it.
* Already named (skip, they have rows): `port/symbols.csv` and `port/spec/platform_symbols.csv` rows with exe=vgame (RTL, DOS, timer
  calibrate, joystick raw read 10a8:0cb7/0cff, break hook ...), video_symbols (14a2:000E set_dac). The disassembly shows their names.

## Facts already established (verify if you rely on them)
* `0000:ebb4` = C start-up, calls `main` = `0010`. `0010`: comm = far ptr from the word at 0000:04F0 -> `DS:E548/E54A`
  (comm:0000); pilot record far ptr `DS:98F4/98F6` = comm+1272h; binds drivers (0ffd:0008 with comm+1A/1E/1C), installs
  break hook, joystick calibration from comm+48h (if comm+72h bit0), page segments, loads 256pit.PIC etc. (00f0), runs the sim `0233`,
  exits with code `DS:008D` (RE_GUIDE: 0 = quit, else END debrief).
* `0233` -> `1bfc` (timer: INT 8 handler installed, calibrates retrace rate 1d76), then `1bb0` -> `1bbc` = THE FRAME LOOP:
  `2f82` (view + 3D scene) ; `7898` (HUD/overlays) ; if `[C07A]==0` also `10a8:0000` (asm HUD), `d9a3`, `17a3` ; `flip` (slot 44, 0xA0*[2E34] words)
  ; `1e9e` (keyboard / controls / quit requester) ; `39c4` (simulation step + messages) ; repeat until `[E552]!=0`; then `0233` tail: sound off,
  `4c29`, frees, `jmp 1c3a` (timer restore).
* `[C07A]` = view mode (0 = front cockpit; others = rear/left/right/external ...: see `2f82` jump chain). DGROUP 3658 = comm[33h] (night).
* Pages: `[85E2]` = A000, `[85E4]`/`[85E6]` = page 1/2 segments (page_seg_set_b slot 75).
* Original fatal-on-error behaviour: file/alloc helpers print a message and exit.

## Output (write ONLY your own files in work/s36/)
`spec_<ID>.md` and `syms_<ID>.csv` (ID = your reader id, e.g. R3).
spec: per function (or tight group): `### SSSS:OOOO` heading with address (seg 0 = `0000:OOOO`), proposed C name + signature, what it does
as an ALGORITHM a porter can implement (control flow, formulas with exact constants, tables with all values if small, string
text verbatim with row/col, order of side effects), globals touched (`DS:xxxx` + meaning + size), structure fields (`obj+NN`),
driver slots called (number + meaning), sounds (snd_cmd numbers), files read/written, callers, callees, original bugs/quirks.
Mark claims `(V)` verified in the disassembly or `(U)` unclear / needs a runtime or visual check. Write a short
overview at the top of your part (how your functions fit together, call order) and at the end: `## Open items`, a table
`## DGROUP globals seen` (offset | size | name | meaning | readers/writers), and `## Structures seen` (record layouts with
every field offset you decoded, record size, array base and count, who allocates it). Coordinates are 320x200 pixels.
When a global is shared with other ranges, still give your best meaning; the merge will reconcile. Use names with
this prefix scheme: `sim_` (main loop/state), `frame_`, `timer_`, `load_`, `key_`/`input_`, `view_`/`cam_`, `cockpit_`, `hud_`, `mfd_`, `msg_`
(on-screen text messages), `menu_`, `mission_`, `snd_`, `pal_`, and for the other owners `r3d_` (render3d), `fm_` (flight model), `wpn_` (weapons),
`ai_`, `det_` (detection/stealth), `dmg_` (damage), `obj_` (world object list), `terr_`. Unknown but important data: `ds_XXXX_hint`.

`syms_<ID>.csv`, header exactly `exe,kind,address,name,type,owner,other_names,notes`: exe=vgame; kind=func (address `0000:oooo` or
`SSSS:OOOO`, EVERY function in your range incl. 1-instruction stubs and function-local entry labels you find) or data
(address `DS:oooo`); owner = one of `simflow, render3d, flight, weapons, ai, detect, damage, loader, lib`; notes <= 200 chars, quote with "" if it has commas.
Give every function of your range a row (the coverage gate counts them), even those you only index.

## Rules
Read only your range (plus callees you must understand). Chunked reads. Do not view images. No test pages. Do not edit files outside
work/s36/. Keep the chat reply <= 10 lines: counts of functions done, biggest open items. Be thorough in the files, terse in chat.
Depth: simflow functions get the full algorithm; render3d/flight/weapons/ai/detect/damage get index-level entries (still every
function a row; but include the data structure layouts you meet - object records, mission records, weapon tables - they are the glue).
