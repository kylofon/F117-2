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
**Parked (no single palette fits, probably runtime-composed): ORDNANCE (loaded by VGAME, likely tied to the
cockpit palette), REQUESTR (shown with ADV.PAL as placeholder).**
**256LEFT/PIT/REAR/RIGHT (cockpit): none of the 21 palettes fits** - VGAME has no palette write of its own,
so the flight palette is probably composed at run time (colour ramps in VGAME near image 0x1FD9A); later task.

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
  dither lo/hi. VGAME never writes the DAC (it inherits START's palette); renders use `ADV.PAL`
  (ramps A0-FF match the shading; to confirm). The ground backdrop colour is not found yet (drawn via
  MGRAPHIC), so AND/OR polygons are shown in fixed legend colours. Backdrop (bit 7 clear) = land, OR 80h
  = water (U5, CE).

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
