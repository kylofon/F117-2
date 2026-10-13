# S3.7 brief (render3d spec readers)

Task: the **render3d spec** of VGAME.EXE (F-117A 2.0, 1991): 3D transform, projection, clipping, polygon scan conversion,
terrain, sky/ground, object sorting. A later Sonnet session must be able to port each function **bit-exact** from its
section alone. You own an address range (given in your prompt). Write ONLY your files `work/s37/spec_<ID>.md` and
`work/s37/syms_<ID>.csv` (plus scratch files `work/s37/<ID>_*.txt|py`). Never edit anything outside `work/s37/`.

## Starting material (a DRAFT: verify every claim against the disassembly, correct it, complete it)
* `port/notes/s36/spec_R11a.md` (0000 scene/terrain/queue + segment 1001) and `port/notes/s36/spec_R11b.md` (11EB, 12EF,
  1359, 1433) with `syms_R11a.csv`, `syms_R11b.csv`. These are index-level passes; several formulas are approximate.
* `port/spec/simflow.md` Part 12 (lines ~5941-6268: interface, buffer map, edge record) and Part 8 Part 2 (lines ~4194-4420:
  b85d, ba13, bd8f, dcc0, ddfe, 9aca), Part 3 sections 3 and 9 (lines ~1369-1504). Search with Grep, read with offset/limit.
* `FORMATS.md` sections "3D objects: .3DX", "Terrain", "Flight palette": data formats. Cite, do not repeat.
* `port/spec/video.md` (MGRAPHIC slots, pages), `port/spec/simflow.md` Part 2 section 1 (sine table DS:2084, 109D helpers).

## Tools (outputs into work/s37/, read them in chunks; never print a whole listing into the chat)
* `python tools/dsann.py VGAME 1e23 SSSS:OOOO SSSS:OOOO > work/s37/<ID>_dis.txt` - annotated disassembly of the indexed
  functions in a range (seg 0 ranges as bare hex `04da 0804`). Then `Read` with offset/limit or `grep -n`.
* Ground truth / code in index gaps: `python tools/x86dis.py work/unpacked/VGAME.EXE dis SSSS:OOOO LEN` (hex LEN).
* `python tools/vgxref.py f SSSS:OOOO` (callers/callees/DS offsets), `python tools/vgxref.py g 85CA` (who reads/writes a DS offset).
* Image: `work/unpacked/VGAME.EXE`; image = file minus the MZ header (header paragraphs at file offset 8). Far segment
  SSSS:OOOO = image offset SSSS*16+OOOO. **DGROUP = 1E23: DS:oooo = image offset 0x1E230+oooo** (= file offset 0x1EA50+oooo).
  Dump initial tables with a few lines of Python (struct.unpack_from on the image) - give small tables in full in the spec.
* Differential testing (strongly recommended for arithmetic-heavy leaf routines: projection, DDA, clipper, intercepts,
  matrix builders): write a Python reference `work/s37/<ID>_ref.py` and compare it with the real code run under Unicorn
  (`pip` package `unicorn` is installed; harness example `port/notes/s36/r2_emu.py`: image at linear 0, DS=SS=0x1E23; for a
  far routine set CS=SSSS, IP=OOOO, push a far return address and stop there). Random + edge-case inputs; report the count
  of cases and mismatches in the spec ("differential-tested: 20000 cases, 0 mismatches"). If something cannot be matched,
  say exactly where it differs.

## What every function section must contain
`### SSSS:OOOO name` (+ C signature with exact int types: int16/uint16/int32, near/far, register interface if not cdecl),
then the algorithm as C-like pseudocode that is bit-exact: operand widths, signed vs unsigned compares (jl/jb), `sar` (floor)
vs `idiv` (truncates toward 0) vs `shr`, carries in 32-bit adds, rounding constants, saturations, overflow handlers (INT 0),
order of side effects, loop bounds and termination, table base addresses as `DS:xxxx` with element size and count, all
constants in hex. Then: globals read/written, callers/callees, edge cases, original bugs/quirks (port them faithfully, mark
them). Mark claims (V) verified in the code, (T) differential-tested, (U) unclear/needs runtime check. Dead code (no callers,
unused video modes) gets a short entry only (what it is, why dead).

At the top: a short overview of your range (how the pieces fit, call order). At the end: `## Open items`,
`## DGROUP globals seen` (offset | size | name | meaning | readers/writers), `## Structures seen`.
Name prefixes: `r3d_` (renderer), `terr_` (terrain), `hz_` (horizon/sky), `edge_`/`clip_`/`fill_` (2D raster), `math_`/`mat_`
(fixed point). Keep existing names from the draft csvs unless wrong.

## syms_<ID>.csv
Header exactly `exe,kind,address,name,type,owner,other_names,notes`; exe=vgame; kind=func for every function of the index in
your range (list: `port/vgame_functions.csv`), kind=label for code entry points that are not in the index (gap routines,
local entries), kind=data for `DS:oooo`; owner=render3d; notes <= 200 chars, quoted if they contain commas.

## Rules
Read only your range plus the callees you must understand. Chunked reads, grep first. Do not view images. No test pages.
Chat reply when done <= 12 lines: functions covered, what was differential-tested, biggest open items.
