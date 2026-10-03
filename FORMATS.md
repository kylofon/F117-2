# F-117A 2.0 file formats

## .PIC (320x200, 8-bit indexed) — decoder `tools/picdec.py` (END.EXE 0000:4874-49CF, 48AA)
Header byte0: bit7 set = 8-bit pixels, maxbits = 256-byte0 (all shipped files: F5 -> 11); else 4-bit
pixels (two per byte, low nibble first, maxbits = byte0; none shipped). Then LSB-first LZW from bit 8:
code width 9..maxbits, **no clear/EOI code**, free entry starts at 256 and one entry is added after every
code (the first is a dummy), width grows when free > mask; when width would exceed maxbits the table is
cleared (width 9, free 256, previous code kept). Output goes through RLE-90 (`90 n` = repeat previous
byte n-1 more times, `90 00` = literal 90). Result is exactly 64000 pixels. 52/52 files decode.

## .PAL / PALETTES.PAL
768 bytes = 256 x RGB, VGA 6-bit (0-63). `PALETTES.PAL` = 9 palettes (START.EXE 0000:826C copies slot
min(n,8)). Slots 0-8 follow START's screen table (DS:0CBA): 0 AIR_ID, 1 ARMSSCRN, 2 BREFSCRN, 3 FOLDER1,
4 HANGSCRN, 5 HOMESCRN, 6 ORDRSCRN, 7 ROSTSCRN, 8 BREFICON. Confirmed in U5: theatre maps -> slot 2,
PLANES1-5 -> slot 0, CLIMBIN -> slot 8, WEAPONS1-3 -> slot 1 (slots 1 and 4 differ only in UI colours; 1 = ARMSSCRN chosen), own-named .PAL files (ADV, AWARDS, BREFSCRN,
CEMETARY, DESK, ENDHANGR, FLAG, FLIGHT, LANDED1 (also LANDED2-4), RESCUE, SWAP, TASS), ORDRSPRT -> slot 6.
Also confirmed: FOLDER2 (slot 3), ROSTSPRT (slot 7), HANGSPRT (slot 4), MEDALS (AWARDS.PAL).
Cockpit 256LEFT/PIT/REAR/RIGHT and ORDNANCE (sprite page loaded by VGAME 0000:02FE) use the **flight
palette** below; REQUESTR is a START sprite page loaded while ARMSSCRN is shown (START 0000:0963) -> slot 1
(confirmed in U5).

## Flight palette (VGAME, S2.6)
VGAME writes the DAC itself: `set_dac(first, count, far rgb)` 14A2:000E (waits for retrace; a
grey-scale path when BIOS 0489h bit 1), `get_dac(far buf)` 14A2:00C5. Data in DGROUP 1E23:
- Three 256-entry palettes: **A** 164A, **B** 194A, **C** 1C4A. 1088:0006 copies one to 104A and
  sets all 256: `[43AE]`=0 (normal) → B if `[3658]` else C; `[43AE]`=1 (debug key 0000:D2F0,
  which also toggles `[3658]`) → C if `[3658]` else A. `[3658]` = comm[33h], set by START 0000:50AB:
  1 if a target type is 4, else random (`[caca]+42h` set: 90 %, else 70 %); START also uses it for
  the mission clock (hour + 18/06). B and C are dark (night), A is bright.
- Climate byte DS:00EE[theatre] (LB PG NC CE VN ME KU CU KO = 6 6 2 2 2 6 6 2 2): ≠ 2 (desert) →
  48-byte ramps over entries 70h-7Fh and A0h-AFh: A ← 1F4A, 1FDA; B ← 1F7A, 200A; C ← 1FAA, 203A.
- `[365E]` = (`[C07A]` & C0h) ≠ 0 → entries 98h-9Fh ← 206A (green ramp, 8 colours; 1088:00A3);
  1088:00BD sets them from 1212 (filled at run time). 0000:4857 also rebuilds 104A from C (`[3658]`)
  or A through the near helper 0000:EA26 (not traced).
- Quit requester (0000:1E9E): saves the DAC to 0D4A, sets 134A (= `ADV.PAL` exactly), shows `adv.pic`,
  restores 0D4A.
- Layout: 00-0F EGA colours, 14h-26h and 40h-4Fh cockpit greys, 60h-6Fh sky ramp, **70h-7Fh ground
  ramp** (70h = horizon haze → 7Fh near), 98h-9Fh MFD, A0h-FFh 16-colour shading ramps, F0h-FFh water.

## 3D objects: .3DX — `tools/f3d.py` (VGAME 11EB:00D3 renderer, loader 0000:0994)
Three banks; object id word = `bank<<8 | index`, bank segment at DS:B75E+2·bank: 0 = `STFLT.3DX`
(aircraft, weapons), 1 = `<TH>.3DX` (theatre scenery), 2 = `PHOTO.3DX`. All 11 files parse with 100 %
byte coverage (2 trailing zero bytes).
- File: `w n`; `n+1` word offsets (the last = end of data); `n` flag bytes (bank 1 → DS:0960/07E0,
  PHOTO → DS:0860): bit 7 = depth-sorted object (queued by 0000:DDFE, max 50, drawn after the ground),
  bits 0-2 = class. Object = LOD list of `(w maxdepth, w model offset)`, the last with bit 15 set
  (8001h); the first entry with maxdepth ≥ |view-space z high word| is drawn. Models are shared.
- Model (offsets absolute, the file sits at seg:0000): `w radius` (cull), `w nparts`;
  (nparts−1)×`(b var, b axis 0/1/2)` rotating parts (angle = var table[var]), (nparts−1)×`3w pivot`;
  `w nverts`; `w nplanes` (< 0 = BSP); planes; per plane `b n` + n vertex indices (only vertices of
  visible planes are transformed); if nplanes < 0: `b root` + |nplanes|×`(b front, b back)` (FFh none);
  vertices; `w nedges` + nedges×`(b v0, b v1)`; |nplanes| face-list offsets (none if nplanes = 0: one
  list follows); face lists.
- Plane = `3×s16 normal (|n| = 4000h), s32 d`, visible when n·eye + d ≥ 0 (n = d = 0: always). Prefix
  words: `80pp` = following planes belong to part pp; `8100 b var b val` = next plane hidden unless
  var[var&1F] = val.
- Vertex = `3×s16 (x east, y up, z south)` (right-handed), prefix words 8000h-800Fh select the part. nverts < 0 = |n| bytes
  indexing the shared table DS:4EB6 (unused by the shipped data). Parts at rest: vertices are plain
  model coordinates; part planes are pivot-relative.
- Face list until FFh: `n<20h` polygon of n edge indices + colour (FFh = skip; the edge set is
  scan-converted, all faces chain into one loop); `40h|n` colour + n edges drawn as lines; `20h|n` colour
  + n vertex indices (points: computed, never drawn); `80h|v b val` = next element only if var[v] = val
  (when skipping: FFh ends the list, else skip (b&1Fh)+1 bytes). Vars seen: 1 = destroyed, 10 = lights
  (night), 2, 5-9, 11-14 rare. Exports use all vars = 0.
- Order: planes in file order; BSP (0E5F): visible node → back subtree, self, front; hidden → front, back.
- Colour byte → word through table [7D50] (VGAME DGROUP 7D60, 7F60 = day, 8160, 8360 = low detail).
  Fill (1359:0AF7): hi FF solid lo (lo ≥ A0h shaded: lo − [4894], clamped to its 16-colour ramp);
  FB no fill; FC 50 % stipple; FE AND lo onto the backdrop (7Fh, 6Fh); FD OR lo (80h); other hi = 2×2
  dither lo/hi. Palette: see Flight palette. **Ground backdrop** (0000:1600): 16 four-point bands in
  colours 70h+i (horizon → near), or one flat colour 7Ch (0000:179A); AND/OR polygons recolour those
  pixels: AND 7Fh keeps the ground ramp, AND 6Fh → 60h-6Fh, OR 80h → F0h-FFh (water, U5 CE). Renders
  use band 7Ch.

## Terrain: .3DG / .3DT — `tools/f3d.py` (VGAME 0000:0C2C, 0AF9; lookup 0865; scenery 04DA, E7AC)
Tile quadtree, 5 levels. A level-L cell is 1000h level units; world = level units × 64/16/4/1 for levels
4/3/2/1 (0000:0804; level 0 = ×½, never has objects). The theatre is 100000h world units square
(16×16 level-3 cells), x east, y **north**: terrain t = (8000h − WLD y) × 32, x = WLD x × 32 (terrain
  query 0000:77D9; `.WLD` y runs south).
- Tile type: level 4 = 8×8 table in VGAME DS:0A68+40h·theatre (theatre = START order LB PG NC CE VN ME
  KU CU KO), index = cell + 2 (inner 4×4 = the theatre, 10h/11h = fillers); level 3 = `.3DG` map;
  levels 2/1/0 = child table `[parent·16 + (y&3)·4 + (x&3)]`.
- `.3DG` (1810 bytes): `w '22'`; 16 bytes (overwritten); 16×16 level-3 map (row = t, row 0 = south); 3×512 child
  tables for levels 2, 1, 0 (32 types × 4×4).
- `.3DT`: `w '11'`; 5 `w` type counts (32); 5×32 `w` record counts per (level, type); records
  level-major then type-major, `4w (x, y, z, obj)`: x/y relative to the cell centre (+800h), z up,
  obj low byte = bank-1 object (bit 7: mission override lookup 0FB1 in DS:B772, 5-byte entries).
- Placement (E7AC): model (x, y, z) = (rec x, rec z, −rec y) relative to the camera, so model +z = south.

## World layout: .WLD — `tools/f3d.py` (START 0000:54BB load, 5625 copy to the far buffer [E096]+7Ah)
`w 'BN'`, `w nSites`, `w ?` (13h), `w ?` (24h); nSites × 16-byte sites; `w nUnits`; nUnits × 36 bytes;
3 × 128-byte tables (DS:D7F6, D33E, D2A6); 256-byte 16×16 grid (DS:CAD8, level-3 cells); up to 750
bytes of NUL-separated names (index 0 empty). Site = `w name, w x, w y` (×32 = world, VGAME 0000:32C1),
`w ?, w ?, w ?, w ?, w object id` (bank<<8 | index). Units and tables: S3 (mission generator).
