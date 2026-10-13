# render3d: the 3D renderer of VGAME.EXE (S3.7)

Bit-exact specification of everything VGAME does to turn the world into pixels: scene set-up and camera, terrain and
scenery, placement and culling, the depth-sorted object queue, the 3DX object renderer (transform, lighting flags,
back-face/BSP order, projection, near-plane handling), the 2D edge clipper and polygon assembler, the mode-13h scan
converter and fills, the sky/ground backdrop and the 2D edge tables of segment 1001, the map-MFD terrain renderer, the
scenery collision probe, and the Q15 math library 1433. **146 indexed functions** (+76 labels / index-gap routines, 237
DGROUP data rows) in `render3d_symbols.csv`; `python tools/spec_coverage.py vgame` = 450/450 (platform 74, render3d 146,
simflow 230).

Made from six reader passes (R7a..R7f) that verified and completed the S3.6 drafts (`port/notes/s36/spec_R11a.md`,
`spec_R11b.md`) against the disassembly. Nearly every arithmetic routine was **differential-tested** against the real code
under Unicorn (Python references and harnesses kept in `port/notes/s37/`, section 8). Marks: (V) read in the code,
(T) differential-tested, (U) unclear / needs a runtime or visual check, **[corr]** = correction of an earlier draft or of
`simflow.md`. Where this file and `simflow.md` disagree, this file is right (simflow sections of render3d functions carry
a pointer here).

Formats are not repeated: 3DX objects, terrain `.3DG/.3DT`, flight palette and colour tables = `FORMATS.md`; MGRAPHIC
driver slots (31 line, 37 poly_fill, 40 poly_fill_solid, 22 sprite_scaled, 45 flip_flag, pages) = `video.md`; the sine
table DS:2084 and the `109D` helpers (`mul_q15`, `sin16`, `cos16`) = `simflow.md` Part 2 section 1; the caller side of the
scene (view modes, `2F82`, `bf71` TV camera, `8387` map MFD, `A17E` overlays) = `simflow.md` Parts 3, 9, 10.

## 1 Part map

| Part | reader | range | content |
|---|---|---|---|
| 1 | R7d | 0000: 03CE 04DA 0804 0865 0FB1 C6B8 D76B DAE8..E8B5 BD8F BEE1 1AD6 1B1B 1A85, 1088:0120 | scene entry and set-up, viewport, camera matrices, terrain/scenery walk, placement, cull, queue insert DDFE, queue draw DF08, 2D object sprites, depth shade |
| 2 | R7f | 0000: 9ACA B85D BA13 BA67 C73C C834 | per-frame world-object pass (own ship, units, projectiles, smoke, wrecks, shadows), point projection B85D, TV-camera fire/smoke effects |
| 3 | R7e | 0000: 119C 11F2 1353 13AC 13E3 0FFB 77D9 BC12 BCD7 | map-MFD terrain (orthographic top-down), scenery collision probe, site/unit shape helpers |
| 4 | R7a | 11EB:0000..1040 | 3DX object renderer `11EB:00D3` and its pipeline (camera 000C, parts, planes, BSP, vertices, projection 0906, near bisection, edges, face lists) |
| 5 | R7b | 12EF:0000..06A0, 1359:0000..0DA0 | edge clipper `12EF:0333` (16- and 32-bit paths), polygon assembler, raster back end: edge DDA, walls, fills (solid/ramp/checker/stipple/AND/OR), line |
| 6 | R7c | 1001:0006..0875, 0000:1442, 1433:0000..06FC + tables 14B2 | sky/ground backdrop, 2D edge tables + Cohen-Sutherland clipper of 1001, Q15 math (sin/cos/atan2/asin/hypot, matrices, Euler) |
| 7 | - | - | function index (generated from `render3d_symbols.csv`) |

In the parts, "R7x" or "spec_R7x" refers to the part of that reader; `work/s37/R7x_*.py` scripts are in `port/notes/s37/`.

## 2 The pipeline in one page

```
2F82 view_frame -> 03CE r3d_scene_draw(-heading, pitch, roll, x32, y32, z32, x0, y0, w, h)        [Part 1]
  DAE8 r3d_begin_scene: DB71 viewport/clip/centre/page, DC64 matrices (E26B cull 498E, 1433:03A9 model 49A0, 11EB:000C camera 7AF8),
                        DCA9 origin, C686=0, 11EB:0F9B (no-op in shipped data), wait [262A]==0, 1442 sky/ground [Part 6], queue count 0
  04DA r3d_draw_terrain: level 4 -> 1 (coarse -> fine), cells far -> near (octant tables), records -> E7AC
        E7AC: camera-relative vector, E8B5 cull, flag bit 7 ? DDFE queue : draw now via 11EB:00D3
  9ACA world objects [Part 2]: BD8F -> E7AC, DCC0 -> E8A2 + DDFE (billboards/2D sprites), B85D projection for the HUD side
  DB3E r3d_end_scene: DF08 draws the queue farthest-first, offset_clear, spinner vars, copy 498E -> 49B2
  [7D50]=7D60, A17E overlays (simflow Part 9)
11EB:00D3 r3d_draw_object(obj, x, y, z, mat9, vars)                                                 [Part 4]
  origin*Cam -> LOD -> sphere/pyramid test 02B6 -> parts 031B -> planes 0565 (+BSP 0E5F) -> vertices 06B9/0819 -> project 0906
  -> edges 0A37/0B3F (near bisection 0990, clip 12EF:0333) -> faces 0C7A/0CAC/0CED
     polygon: per edge 12EF:003E (-> 1359:0719 DDA) ; 12EF:010A poly_finish: closing near edge, walls (1359:07D6), 1359:0AF7 fill   [Part 5]
     line:    1359:045D Bresenham
119C map MFD [Part 3]: DAE8 straight-down camera, 11F2 tile walk (5 passes), 13E3 -> 11EB:00D3 in ortho mode [7D5E], DB3E
```
The object polygons do **not** use MGRAPHIC: segments 12EF/1359 are a complete private scan converter writing into the
page segment `[85EA]` (V, no driver call in 11EB/12EF/1359). Only the backdrop/rectangles of 1001 and the 2D sprites use
driver slots 31/32/37/40/22.

### 2.1 Draw order and sorting (painter's algorithm, no z-buffer)
1. Backdrop `1442` (sky bands then ground bands, or the two-colour split at detail < 3).
2. Terrain/scenery `04DA`: levels 4, 3, 2, 1; inside a level the cells in the order of the octant table (far -> near,
   camera cell last; path A visits cells 9..14 again after the camera cell, some twice: original quirk, Part 1);
   inside a cell the records in `.3DT` order. Records whose bank flag byte has bit 7 are not drawn but queued.
3. Sim objects of `9ACA` in its fixed order (Part 2); BD8F/E7AC draw immediately unless the flag byte has bit 7,
   DCC0/C834 always queue.
4. The queue (`DDFE`/`DF08`): up to 50 records, key = view depth C shifted to level-4 units (`sar32` by 2*(4-[E2FC])),
   +0x200000 for class 5 at level 2; descending order, index 0 = farthest = drawn first; a new record goes in front of
   (is drawn before) records with an equal key; when full the current farthest record is dropped (even if the new one is
   farther). Compare = signed hi word, unsigned lo word (T).
5. Inside one model: planes in file order, or in BSP order (`0E5F`, back-to-front by the visibility of each node's plane);
   back-facing planes are skipped. Inside a face list: elements in stream order.
6. Overlays `A17E` (HUD, target boxes) after the 3D scene.

## 3 Conventions

* World: x = east, y = north, z = up; theatre 0x100000 world units square; x/y 32-bit (lo word at the lower address),
  altitude 16-bit signed. 3DX model space: x east, y up, z south, so callers pass `(x, z, -y)` camera-relative to `11EB:00D3`.
* Angles: uint16, 0x10000 = 360 deg. heading 0 = north, clockwise; pitch + = nose up; roll + = right wing down. The scene
  functions receive `-heading` and negate again in DC64.
* View space (E8B5): A = right, B = up, C = forward depth; (11EB) x right, y down-scaled by 3/4 for the 320x200 aspect,
  z depth; the camera matrix at DS:7AF8 has the focal/aspect scale folded in (Part 4, 000C).
* Fixed point: Q15, unity = 0x7FFF. Three different product helpers exist and must not be mixed up:
  `mul_q15` = `109D:0006` `(a*b + 0x4000) >> 15` (rounded; E26B, E3E2, BA67, flight);
  `q15` of 1433 = `(a*b) >> 15` floor (`imul; shl ax,1; rcl dx,1`, take DX) (mat_from_euler, mat_vec16);
  `mat_mul`/`vec_mat32` = full 32-bit sums of the products, shifted once at the end (mat_mul) or not at all (vec_mat32).
  Matrices are 9 words row-major, row-vector convention `p' = p*M` (`p'_k = sum_j p_j*M[3j+k]`).
* Integer semantics: 16-bit two's complement wrap; `sar` = floor shift; `idiv` truncates toward 0; `abs16(-32768) =
  -32768` (`0000:F302`); runtime `F46A` sar32 / `F50E` shr32 / `F45E` shl32 loop CL times (count >= 32 gives sign fill / 0).
  Disassembler note: in this 16-bit code `cdq` printed by capstone = opcode 99h = **cwd**, `cwde` = 98h = **cbw**.
* INT 0: three routines install a private divide-overflow handler around their `idiv`s (`11EB:0975`, `1001:04E3`,
  `1001:0854`); the port must reproduce the saturated results they produce (given per handler), not trap.
* Calling: segment 0 = near cdecl (first arg `[bp+4]`), far segments = far cdecl (`[bp+6]`); near helpers inside a far
  segment use registers (listed per function). DGROUP = 1E23: DS:oooo = image offset 0x1E230+oooo.

## 4 Decisions (scope and ownership)

* **In render3d:** everything listed in section 1, incl. the billboard/object queue `DCC0`/`DDFE`/`DF08` and the point
  projection `B85D` (S3.6 Part 8 already marked them render3d; confirmed: the queue is the only inter-object sort and
  B85D is a pure camera projection whose clients are 9ACA and the HUD overlay A17E). `9ACA` is render3d although it holds
  the ground/air target designator blocks (weapons logic): keep them inside 9ACA in the port (write order matters),
  S3.8 cross-references Part 2.
* The Q15 library `1433` moved from owner `lib` to render3d: all its callers are renderer code, no flight-model caller (V).
  The four `1359` init/alloc rows (`lib` in S3.6) moved as well.
* `0000:E578 mat_mul` stays in `simflow_symbols.csv` (owner lib; only caller `2C6E fm_rotate_apply`, flight model), but
  its bit-exact spec is here (Part 1 section 3, (T)). `14A2:00C5 pal_read_dac` (caller `1088:00D7 pal_save`) is palette
  code: owner changed to simflow. `0D08`/`0F31` (scenery damage) are S3.8; loaders `0994`/`0AF9`/`0C2C` are in simflow Part 1.
* **Other programs:** no other program has 3D code. START contains no `.3DX` reference and no matrix/projection code; its
  ID quiz is PIC-based (`AIR_ID.PIC`) and dead in this build (shell.md section 3); it reads `.3dG/.3dT` only for the
  mission generator (shell.md section 4) and its briefing map is 2D. END, SETUP, PLAYER, MPS_LOGO, DSWAP: no 3D data or
  code (no 3D file names, no renderer-like routines in their indexes).
* Dead code kept short: video modes E/C/T of 1359 (only 'M' is selected), routines without callers (`11EB:0EF2`,
  `12EF:0002`, `12EF:01B1`, `1359:019B`, `1359:023A/027B/02B6/0301`, `1001:02BA`, `1433:00D9 011A 01E0 04D7 0542 0554 058F
  05C6 05D1`), the face-list "points" element (computed, never drawn), `11EB:0DAF` (unreachable), the shared-vertex path
  (`11EB:0F9B` and `nverts < 0`: count 0 in all 11 banks), the plane shade bytes `77D2`/`7D5A` (written, never read).

## 5 S3.6 open items, settled

| item | result |
|---|---|
| 32-bit bisection clip path `12EF:0333` | decoded bit-exact incl. the 0553/055F halving special case and the failure-code rule (85h/8Ah from the LAST midpoint's out-code) (T, 20000 edges) - Part 5 |
| latent near `ret` at `12EF:030F` | reachable only if a window bound is >= 8000h; never with the game's windows (DB71 keeps 0..13Fh/C7h). Port: treat as "edge accepted" + assert - Part 5 |
| part-count limit | <= 7 parts safe; 8 parts overwrite the root eye vector (7B1C+24h*k vs 7C3C). Shipped data: max 2 parts in every bank (one PG.3DX model has 0 parts: object matrix ignored) - Part 4 |
| plane shade byte | computed by `11EB:0678`, copied to 7D5A, never read anywhere in VGAME (V by xref): dead; brightness comes from `[4894]` (Part 1, 1088:0120) via the ramp rule of `1359:01D4` |
| public routines without callers | listed in section 4 (dead); no data-referenced calls found |
| `[262A]` frame wait | set by the frame loop after the flip, cleared by `timer_isr` at `0000:1CB3` (vgxref misses this writer) - Part 1 |
| sky/ground details | 1442 fully specified (T, 10000 runs); mono TV case sky 0x88 / ground 0x7C; hz_draw draws the horizon with slot 31 before filling - Part 6 |
| `.3DT` override bit 7 / 0FB1 0xFF00 quirk | no record of the 9 shipped `.3DT` files has bit 7 (checked with `tools/f3d.py`); bit 7 is set only at run time together with an override entry (0F31), so the quirk is not reachable with original data (port it anyway) |

## 6 Original quirks to port faithfully (pointers; details in the parts)

* Part 1: 04DA path A re-reads the next octant rows (and past the table into DS:03DC for octants 5/7), drawing some cells
  twice; E8B5 wide-mode distance uses the y bound and the signed depth; 0FB1 returns 0xFF00 on an empty table; DF08 sends
  `[98D8]` (bay doors) to var 8 only for id 0x18 and leaves vars 5..14 stale for banks 1/2; DDFE drops the farthest even
  if the new record is farther; BD8F/DCC0/BEE1 test only the low words (`abs16(lo) < 0x7FFF`).
* Part 2: stale `[49C6]` on smoke billboards, stale `[DE92]` before the wreck, B85D leaves 49E8/DE92 unchanged on early
  reject, C73C consumes the game RNG (keep the call order), var 9 (C040) only set for puffs 4-5.
* Part 3: 0FFB never tests the last record of a tile and tests `obj & 0x50` on the object id byte instead of the flag
  byte (part of the CE/KO collision is off); BC12 loses the bank for replacement ids >= 0x80; destroyed scenery vanishes
  from the map.
* Part 4: rotating parts are culled with the eye relative to the pivot without adding the pivot back (Q2); nparts <= 0
  ignores the object matrix; two 16-bit wraps in the 02B6 compare; 0990 counts h = 8000h as "in front"; 01B8 uses a stale
  vars pointer; edges skipped by 0A37 keep stale records.
* Part 5: wall_fill drops single-row ranges and ranges touching only ymin/ymax; fill_M stops at the first untouched row
  ("gap row"); set_colour_M ramp shade can wrap below the ramp base and keeps the wrapped value; 85BE = 3/4 disables the
  closing edge; pixel_M (dead) reads row offsets from DS:[2y].
* Part 6: the 1001 rounded divide is not exact rounding for odd divisors; INT 0 result +-7F00h has an off-by-one when the
  divisor is +1; atan2(0,0) = 8190; asin(-32768) = 0; the hypot table entry 192 is 16383 (formula 16384); 1442 and hz_draw
  use different horizon-offset clamps.

## 7 Open items (need a runtime or visual check; collected in PLAN.md user to-do `U-render3d`)

| # | item | part |
|---|---|---|
| 1 | rotating parts at close range: does the pivot-culling quirk show (missing/flickering faces of radar dishes, props)? | 4 |
| 2 | map MFD bottom edge while scrolling: 1-pixel sliver missing (row orientation vs 1353 bounds)? | 3 |
| 3 | TV camera mono backdrop (`[4360] && [43B2]`): sky 0x88 / ground 0x7C - what is it on screen (which TV option)? | 6 |
| 4 | glow sprites of ids >= 0x25 are always drawn into the TV window (offsets 0xA9/0x6E), also when the record comes from the main view? | 1 |
| 5 | BA67 z-term sign against E8B5 (pitched TV camera: smoke puff sizes) | 2 |
| 6 | fill_M "gap row" reachable in real frames (polygon cut short + stale span)? | 5 |
| 7 | meaning only (no port impact): view 0x8B and `[C06E]`, model 0x23, overlay ids 0x26.., `48A6..48B4` (D76B, no reader), flag bits of 0960/07E0 (bit 4 collision, bit 5 damaged variant, bit 7 solid/on top), physical world unit | 1-3 |

## 8 Differential tests and reference code

Every part starts with its test table (cases, coverage, 0 mismatches). The Python references are executable
specifications of the leaf routines and are the oracle for the port's unit tests (Phase 5):
`port/notes/s37/R7a_ref.py` (000C, 02B6, 0565/0678/0E5F, 0819, 0906+0975, 0990), `R7b_ref.py` (0333 both paths,
003E/010A walls, 0719 DDA, 045D, 0AF7 all fill types, 01D4), `R7c_ref.py` (all of 1433, 1001 clip/edge/walk/fills, 1442),
`R7d_ref.py` (E8B5, E26B, E3E2, E578, 0804, 0865, 1088:0120, DDFE), `R7e_ref.py` (13AC, 1353, 11F2, 13E3, 0FFB, 77D9,
BC12, BCD7), `R7f_ref.py` (B85D, BA13, BA67). Harnesses `*_emu.py` run the real code under Unicorn (`pip install
unicorn`, read `work/unpacked/VGAME.EXE` at run time; no game data is stored in the repo).
Not emulated (they call the object renderer): DF08, 04DA, DCC0, BD8F, 9ACA, 0CED: line-by-line pseudocode, (V).
Whole-frame test for those (golden frames from DOSBox-X dumps around `03CE`, Unicorn replay, then the port's `--render-test`):
PLAN.md Phase 5 Notes, S5.6 render3d test plan.

## 9 Suggested port layout (Phase 5)

`r3d_math.c` (Part 6 C, 109D helpers), `r3d_scene.c` (Part 1 set-up, cull, queue), `r3d_terrain.c` (04DA family, Part 3
map walk), `r3d_object.c` (Part 4), `r3d_clip.c` + `r3d_raster.c` (Part 5), `r3d_sky.c` + `edge2d.c` (Part 6 A/B),
`r3d_world.c` (Part 2). Keep the DGROUP working buffers as one struct with the original offsets in comments: several
routines rely on their adjacency (edge records 5EBA.., part slots 7B1C.. vs 7C3C, L/R tables 89AC/8D6E).


---------------------------------------------------------------------------------------------------------------------

# Part 1: scene set-up, terrain, placement, culling, sorted queue (segment 0, 1088:0120) (R7d)

Range: `0000:03CE 04DA 0804 0865 0FB1 1A85 1AD6 1B1B BD8F BEE1 C6B8 D76B DAE8 DB3E DB71 DC64 DCA9 DCC0 DDFE DF08 E26B
E3E2 E54B E578 E7AC E8A2 E8B5` and `1088:0120`. Sources: `work/s37/R7d_dis_{a,b,c,d,e}.txt` (dsann), `tools/x86dis.py` for
the far helpers. Drafts checked: `port/notes/s36/spec_R11a.md` sections 1-5, `port/spec/simflow.md` Part 3 section 3 and
Part 8 Part 2. Corrections against the drafts are marked **[corr]**. Marks: (V) read in the code, (T) differential-tested,
(U) unclear. All arithmetic is 16-bit two's complement unless a width is given; `sar` = floor shift, `shr` = logical.
`mq(a,b)` = `109D:0006 mul_q15` = `int16(((int32)a*b + 0x4000) >> 15)` (V, (T) through E26B); `sin16/cos16` = `109D:007C/006C`
(simflow Part 2 section 1 / spec_R11a 2.3; core `r = T[a>>8] + (((T[i+1]-T[i])*(a&FF) + 0x80) >> 8)`, (T) through E26B).
`abs16(v)` = `0000:F302` (`cwd; xor; sub`: abs16(-32768) = -32768). Runtime shifts: `F45E` shl32, `F46A` **sar32**,
`F50E` **shr32** (all loop CL times, CH cleared, so a count >= 32 gives 0 / sign fill); `F476/F492` = the same on a long in memory,
result also left in DX:AX.

Differential tests (`port/notes/s37/R7d_ref.py` vs the real code under Unicorn, `port/notes/s37/R7d_emu.py`), all **0 mismatches**:

| routine | cases | notes |
|---|---|---|
| E8A2/E8B5 cull | 60000 | random and real rotation matrices, s16 x/y/z at several scales, class 0..7, [4360] 0/1/2, [294B] 0/1/2, [365E] 0/1/100h, all 4 table pairs (+20h), [48B6] incl. random; checks AX, ZF, 4958..4962, [4978] (and that it is left alone on early culls). ~14000 accepted, ~150 rejected by the final distance test |
| E26B matrix_from_angles | 20000 | also the six sin/cos words 4982..498C (so sin16/cos16/mq are verified too) |
| E3E2 matrix_obj_side | 20000 | 49A0..49B0 + 4982..498C |
| E578 mat_mul | 10000 | random s16 matrices |
| 0804 terr_to_level | 20000 | incl. carries near 0xFFFFFFFF |
| 0865 terr_tile_type | 20000 | random tables in all 5 maps, out-of-range x/y, recursion |
| 1088:0120 depth_shade | 5000 | incl. [497A] > 7 |
| DDFE queue_insert | 19057 inserts | 400 sequences from empty, up to 89 inserts (overflow past 50), E2FC 1..4, class 5 bonus, ties; checks the pointer list, count and the 0x16-byte record |

---------------------------------------------------------------------------------------------------------------------------

## 0. Overview (call order of one 3D view)

```
2F82 view_frame -> 03CE r3d_scene_draw(-heading, pitch, roll, x32, y32, z32, x0, y0, w, h)
   03CE: viewport [0344] (= DS:032E), 04BB frame delay, colour table [7D50], light vars
   DAE8 r3d_begin_scene(vp, a1,a2,a3, 0,0,z, 1, focal)
        DB71 viewport -> centre 0D1E/0D20, clip 85CA.., 2293/2295, page seg 85EA, frustum tables E53E/E544, light 7D54..58
        DC64 -> E26B(498E cull matrix), 1433:03A9(49A0 model matrix), 11EB:000C(final matrix 7AF8, depth scale)
        DCA9 origin 497C..4980 ; [C686]=0 ; [8560]=[B760] ; 11EB:0F9B ; while(byte[262A]) ; 1442 sky/ground ; [49C4]=0
   04DA r3d_draw_terrain: levels 4..1, cells far->near; per scenery record E7AC (draw now or DDFE queue)
   9ACA world objects (R8): BD8F -> E7AC, DCC0 -> E8A2 + DDFE, C834 smoke -> BEE1 -> E7AC, C73C -> E7AC
   DB3E r3d_end_scene: DF08 draws the queue (index 0 = farthest first), offset_clear, spinner vars, copy 498E -> 49B2
   [7D50]=7D60 ; A17E overlays (uses 1A85 explosion sprite) ; [0346] ^= 1
bf71 (TV/target camera, R10) uses the same pieces with viewport [4042] (= DS:402C), [0348]=1 and BEE1/E7AC/DF08 directly.
```
Data flow inside one object: caller sets the origin with `DCA9` -> `E7AC` makes the camera-relative vector `[4964..4968]` ->
`E8B5` computes view vector `A,B,C` (4958..4962) with matrix `498E` and accepts/rejects -> either `11EB:00D3` immediately
(with model matrix 49A0 from `1433:03A9(-a1,-a2,a3)` transposed) or `DDFE` stores a 0x16-byte record keyed by `C` ->
`DF08` replays the records. `[E2FC]` = the "unit level" of the current coordinates (4..1: terrain level; 1 for sim objects).

---------------------------------------------------------------------------------------------------------------------------

## 1. Scene entry and set-up

### 0000:03CE r3d_scene_draw
`void near r3d_scene_draw(int16 a1, int16 a2, int16 a3, int32 x, int32 y, int32 z, int16 x0, int16 y0, int16 w, int16 h)`, cdecl
(`[bp+4]`..`[bp+1C]`). **Verified against simflow.md Part 3 section 3 (line 1369): correct, cite it.** (V) Additions:
* `vp = [0344]`: `vp+0E = y0; vp+10 = y0+h-1; vp+12 = x0; vp+14 = x0+w-1; vp+0 = flip_flag() & 0xFF` (slot 45, `sub ah,ah`);
  `04BB([43B8])`; `vp+4 = 0x88` (the colour word that 1442 pushes as the sky colour; the TextCtx "fg" slot).
* light vars: `C030=C044=C042=9874=0; if ((uint8)[11E2] < 0x14) C044=C042=9874=1` (jae: unsigned). `DS:11E2` has no writer
  (image value 0) -> always 1 (V).
* `focal = ([365E] != 0 && (uint8)[294B] != 0) ? 0x80 : 0x100` (`[365E]` word test, `[294B]` byte test).
* `DAE8([0344], a1, a2, a3, 0, 0, (int16)z_lo, 1, focal)` (only the low word of z); `04DA(a1, a2, x, y, z)` (roll not passed;
  pushes the 8 words a1,a2,xlo,xhi,ylo,yhi,zlo,zhi); `9ACA(); DB3E(); [7D50] = 0x7D60; A17E(); byte[0346] ^= 1`.
* `[0346]` has no other reader or writer (dead toggle) (V, vgxref).

### 0000:DAE8 r3d_begin_scene
`void near r3d_begin_scene(Viewport *vp, int16 a1, int16 a2, int16 a3, int16 ox, int16 oy, int16 oz, int16 backdrop, int16 focal)` (V)
```
DB71(vp); DC64(a1, a2, a3, focal); DCA9(ox, oy, oz);
if (backdrop != 0) {
    [C686] = 0; [8560] = [B760];          // collision flag out; ES bank = theatre .3DX segment
    11EB:0F9B();                           // shared-vertex pre-transform (R7a)
    while ((uint8)[262A] != 0) ;           // byte compare
    1442(vp);                              // sky + ground (R7c)
}
[49C4] = 0;                                // empty the sorted queue
```
`[262A]`: set to 1 by `1BBC frame_loop_body` after the page flip, cleared (`mov byte [262A],0` at 0000:1CB3) by `1C89 timer_isr`
every `[2636]`-th timer tick (simflow Part 1, timer ISR, line ~265). So the backdrop of a new frame is not started until one
ISR "frame tick" has passed since the last flip (V). Port: wait for the timer-tick event. Callers: 03CE, 119C, bf71 (4x).

### 0000:DB71 r3d_set_viewport
`void near r3d_set_viewport(Viewport *vp)` (V)
```
[7D54] = 0xFFDC (-36); [7D56] = [7D58] = 0x24;         // light vector, read by 11EB:04CD (R7a: dead output); image initial 0,40h,0
W = vp->x1 - vp->x0 + 1;  H = vp->y1 - vp->y0 + 1;      // +14,+12 / +10,+0E
[0D1E] = ((W + 1) sar 1) - 1;  [0D20] = ((H + 1) sar 1) - 1;
if (vp->y0 == 0)                                        // full-screen style views
    [0D20] = [365E] ? (((uint8)[294B] == 0) ? 0x34 : 0x3C) : 0x64;   // cmp byte,1; sbb; and al,F8h; add 3Ch
slot65_data_ce_set(W - 1);                              // far call 1E23:025F, arg popped
slot26_offset_set(slot62_pixel_offset(vp->x0, vp->y0)); // all following drawing is viewport-relative
[85CA] = vp->x0; [85CC] = vp->y0; [85CE] = vp->x1; [85D0] = vp->y1;
[85D2] = vp->x0 + [0D1E]; [85D4] = vp->y0 + [0D20];     // absolute screen centre for 11EB edges
[2293] = W - 1; [2295] = H - 1;                         // 1001 clip maxima
[85EA] = DS:85E2[vp->page];                             // word table of page segments (written by 02C2)
[E53E] = 0x48D6; [E544] = 0x48F6;
if ((uint8)[294B] != 0) { [E53E] = 0x48E6; [E544] = 0x4906; }
if ([365E] != 0) [E544] += 0x20;                        // -> 4916 or 4926
```
**[corr]** draft: centre 52 is for `[294B]==0`, 60 for `!=0` (draft had it right; simflow's order of the ternary too). The
table pair 4926 (294B && 365E) was missing from the draft. Values: main view 320x200 -> `[0D1E]=159`, `[0D20]=100`.

### 0000:DC64 r3d_set_camera_matrix
`void near DC64(int16 a1, int16 a2, int16 a3, int16 focal)` (V): `E26B(0x498E, -a1, -a2, -a3); 1433:03A9(-a1, -a2, a3, 0x49A0);
11EB:000C(0x49A0, focal)` (11EB:000C in R7a). Caller DAE8.

### 0000:DCA9 r3d_set_origin
`void near DCA9(int16 x, int16 y, int16 z)`: `[497C]=x; [497E]=y; [4980]=z` (V). Callers 04DA, DAE8, DCC0, BD8F, BEE1.

### 0000:DB3E r3d_end_scene
`void near DB3E(void)` (V)
```
DF08();  slot24_offset_clear();
t = [4EA0] << 7;            // [4EA0] = 6 (constant: no writer; it is the word after the 50-entry pointer list 4E3C)
[C04E] += t << 3;           // += 0x1800 per frame (var 16)
[C04C] += (t << 3) + t;     // += 0x1B00 per frame (var 15)   (16-bit wrap)
memcpy(0x49B2, 0x498E, 0x12);   // 49B2 has no reader (dead copy)
```

### 0000:D76B view_range_setup
`void near D76B(void)` (V). Callers 39C4 (mission init), CBA0 (detail key).
```
for (k = 0; k < 6; k++) DS:48A6[k] = 0x20 << (uint8)(k + [98DE]);      // 16-bit shl
[48B2] = [48AE] + [48B0];
[48B4] = C94E([48B0] * 2, 0x1000, 0x270F);      // fx_clamp_wrap(v,lo,hi): v>hi -> hi; v<lo -> (v <= -0x4000 ? hi : lo); else v
[48B6] = ([98DE] + 1) * 0x0D05;                 // max view depth: 3333, 6666, 9999, 13332 for detail 0..3 (image initial 10000)
```
`48A6..48B4` have no static reader (only `[48B6]` is used, by E8B5) (U: possibly read through a pointer; looks dead).

---------------------------------------------------------------------------------------------------------------------------

## 2. Terrain and scenery

### 0000:0804 terr_to_level  (T)
`int32 near terr_to_level(int16 level, uint32 v)` -> DX:AX, cdecl (`[bp+4]` level, `[bp+6]` lo, `[bp+8]` hi):
```
switch (level) { case 0: return v << 1;  case 1: return v;  case 2: return (v + 2) >> 2;
                 case 3: return (v + 8) >> 4;  case 4: return (v + 0x20) >> 6; }    // 32-bit add with carry, shr32 (unsigned)
default: returns AX = level - 5 (left over by the dec chain), DX = unchanged (garbage); never used.
```

### 0000:0865 terr_tile_type  (T)
`uint16 near terr_tile_type(int16 level, int16 x, int16 y)` -> AX (0..255). Exactly the draft (spec_R11a 4.2) with these details:
level 4 adds 2 to x and y **in the argument slots**; tests in order `x < 0`, `y < 0` (signed), `dim[level] <= x`, `dim[level] <= y`
(signed; `dim` = DS:0506 = 1024,256,64,16,8) -> 0; maps: L4 `byte DS:B00C[y*8+x]`, L3 `DS:9F68[y*16+x]`, L2/L1/L0
`DS:9D10/9B08/9904[p*16 + (y&3)*4 + (x&3)]` with `p = terr_tile_type(level+1, x sar 2, y sar 2)`. The function never returns
0xFFFF (the callers' `-1` test is dead).

### 0000:0FB1 terr_scenery_override  (V)
`uint16 near 0FB1(int16 level, int16 idx, int16 cx, int16 cy)` -> AX.
```
ax = [9902] - 1;  [94DC] = ax;                       // AH stays = high byte of ([9902]-1) for the whole function
for (; (int16)[94DC] >= 0; [94DC]--) {
    e = DS:B772 + 5*[94DC];                           // {u8 level, u8 idx, u8 cx, u8 cy, u8 newobj}
    if (e[0]==(uint8)level && e[1]==(uint8)idx && e[2]==(uint8)cx && e[3]==(uint8)cy) return (AH<<8) | e[4];
}
return AH << 8;                                       // AL = 0
```
**Quirk (port it):** with `[9902] == 0` the result is **0xFF00**, not 0. 0D08 tests only AL, but 04DA tests the whole AX, see
below. With `1 <= [9902] <= 256` AH = 0. Bit 7 of a tile record is set only by 0F31, which always leaves `[9902] >= 1`, so
the quirk needs .3DT data that already has bit 7 set (U: check the shipped .3DT files with tools/f3d.py).

### 0000:C6B8 r3d_scenery_id  (V)
`uint16 near C6B8(uint16 id)`: `if (id & 0x0100) return DS:0510[id & 0x7F] | (id & 0x8000); else return id;` (tests bit 8 only,
so banks 1 and 3 are both mapped). DS:0510 = 128 words filled by `0994` (identity `0x100|i` for the theatre objects, then the
two mission PHOTO objects `0x200|[E2DF]`, `0x200|[E2F1]`, theatre 6 patches word 0x5C2 (idx 0x59) -> 0x20D and 0x51A (idx 5) ->
0x20A; spec_R11a 4.6 is right).

### 0000:04DA r3d_draw_terrain  (V)
`void near 04DA(int16 a1, int16 pitch, int32 x, int32 y, int32 z)`, cdecl `[bp+4]` a1 (= -heading), `[bp+6]` pitch,
`[bp+8/A]` x, `[bp+C/E]` y, `[bp+10/12]` z. Locals: near, oct, i, ox, oy, fx, fy, cx, cy, tile, j, obj.
```
near = ((int16)[98DE] >= 2 || [0348] != 0);
oct  = (uint16)(0x1000 - a1) >> 13;                         // shr: 0..7, 45-degree sectors, 0 = north
for ([E2FC] = 4; (int16)[E2FC] >= 1; [E2FC]--) {
    L = [E2FC];
    if (DS:04FA[L] == 0) continue;                          // 04FA = 1,1,1,1,1,0 (all levels on)
    u = 0804(L, x);  cx = (u >> 12).lo (shr32);  fx = u.lo & 0x0FFF;
    u = 0804(L, y);  cy = (u >> 12).lo;           fy = u.lo & 0x0FFF;
    u = 0804(L, z);  if (u.hi != 0 || u.lo >= 0x7FFF /*unsigned*/) continue;
    [94DA] = (u.lo < 2 /*unsigned*/) ? 2 : u.lo;
    for (i = 0; ; i++) {
        pathA = (L == 4 && (int16)[98DE] >= 2);
        if (pathA) { if (i == 15) break; }
        else {
            if (i == 9) break;
            if (L != 4 && (int16)[98DE] < 2 && i < 4) continue;          // fine levels at low detail: far cells off
        }
        if (!pathA && (int16)pitch < (int16)0xD555) { ox = DS:04CC[i]; oy = DS:04DE[i]; }   // looking down > 60 deg
        else { ox = DS:034C[oct*9 + i]; oy = DS:034C[((oct+2)&7)*9 + i]; }               // word tables, see below
        [94D6] = fx - (ox << 12) - 0x800;  [94D8] = fy - (oy << 12) - 0x800;  // camera rel. to the cell centre (16-bit)
        if (pathA) { [497A] = 7; if (E8A2(-[94D6], -[94D8], -[94DA]) != 0) continue; }  // cell-centre frustum test
        DCA9([94D6], [94D8], [94DA]);
        tile = 0865(L, ox + cx, oy + cy);
        if (tile == 0xFFFF) continue;                                   // dead
        if (i < 4 && !near) {                                           // reachable only in path B, level 4
            if (L == 4) {                                               // (the L test is redundant)
                p = [E44A] = DS:E304[L*32 + tile];  [0504] = 0x400;
                E7AC(0x100 | p->obj /*raw byte, no C6B8, bit 7 kept*/, 0, 0, 0, p->x, p->y, p->z);   // only record 0
            }
            continue;
        }
        [0504] = ([98DE] == 2) ? 0 : (L << 8);
        p = [E44A] = DS:E304[L*32 + tile];                              // first 7-byte record {s16 x,y,z; u8 obj}
        for (j = 0; DS:061E[L*32 + tile] > j /*unsigned*/; j++) {
            [C044] = [C042] = [9874];                                   // lights (var 11, 10)
            if (!(p->obj & 0x80)) obj = p->obj;                         // zero-extended
            else {
                ov = 0FB1(L, j, ox + cx, oy + cy);                      // AX (see quirk)
                if (ov == 0)                    obj = p->obj & 0x7F;
                else if (((uint8)ov ^ p->obj) & 0x7F) obj = ov;         // replaced by another object (whole AX)
                else { [C044] = [C042] = 0; obj = ov | 0x8000; }        // same object: destroyed variant, lights off
            }
            E7AC(C6B8(obj | 0x0100), 0, 0, 0, p->x, p->y, p->z);
            [E44A] += 7;  p = [E44A];  [0504]++;
        }
    }
}   // exit with [E2FC] = 0
```
* `[0504]` is written/incremented only here and never read (dead) (V).
* Path A (level 4, detail >= 2) has no pitch test and no low-detail skip; the cull uses class 7 (largest radius table entry).
* With the quirk of 0FB1 (`[9902]==0`, flagged record): `ov = 0xFF00` -> `obj = 0xFF00` (or `0xFF00|0x8000`) -> C6B8 returns
  `DS:0510[0] | 0x8000` (theatre object 0, destroyed) (V by reading; U whether it happens).
* E7AC is called with the record (x, y, z) and origin `(94D6, 94D8, 94DA)`, so the object vector is `rec - cam_rel_to_centre` in
  level-L units (z: `rec.z - alt`).

Tables (image DS, constant):
* `DS:034C` = 8 rows x 9 s16, row r = octant, entries far -> near; the code uses row `oct` for x and row `(oct+2)&7` for y:
  ```
  r0: -1  1  0 -1  1  0 -1  1  0        r4:  1 -1  0  1 -1  0  1 -1  0
  r1:  2  1  2  0  2  1  0  1  0        r5: -2 -1 -2  0 -2 -1  0 -1  0
  r2:  2  2  2  1  1  1  0  0  0        r6: -2 -2 -2 -1 -1 -1  0  0  0
  r3:  2  2  1  2  0  1  1  0  0        r7: -2 -2 -1 -2  0 -1 -1  0  0
  overread used by path A (DS:03DC.., no other reader): -2  2 -1  1  0 -2 (2 -1 1 0 -2 2 -1 1 0 2 1 ...)
  ```
* Cells (ox,oy) visited per octant (path A visits all 15, path B the first 9; `*` = duplicate of an earlier cell):
  ```
  oct 0: -1,2 1,2 0,2 -1,1 1,1 0,1 -1,0 1,0 0,0 | 2,2 1,2* 2,1 0,2* 2,0 1,1*
  oct 1: 2,2 1,2 2,1 0,2 2,0 1,1 0,1 1,0 0,0 | 2,1* 2,-1 2,0* 1,1* 1,-1 1,0*
  oct 2: 2,1 2,-1 2,0 1,1 1,-1 1,0 0,1 0,-1 0,0 | 2,-2 2,-1* 1,-2 2,0* 0,-2 1,-1*
  oct 3: 2,-2 2,-1 1,-2 2,0 0,-2 1,-1 1,0 0,-1 0,0 | 1,-2* -1,-2 0,-2* 1,-1* -1,-1 0,-1*
  oct 4: 1,-2 -1,-2 0,-2 1,-1 -1,-1 0,-1 1,0 -1,0 0,0 | -2,-2 -1,-2* -2,-1 0,-2* -2,0 -1,-1*
  oct 5: -2,-2 -1,-2 -2,-1 0,-2 -2,0 -1,-1 0,-1 -1,0 0,0 | -2,-2* -2,2 -2,-1* -1,1 -1,0* -1,-2*
  oct 6: -2,-1 -2,1 -2,0 -1,-1 -1,1 -1,0 0,-1 0,1 0,0 | -2,2 -2,1* -1,2 -2,0* 0,2 -1,1*
  oct 7: -2,2 -2,1 -1,2 -2,0 0,2 -1,1 -1,0 0,1 0,0 | -2,2* 2,2 -1,2* 1,1 0,1* -2,1*
  ```
  Original quirk (port it): cells 9..14 re-read the next rows (oct 7 and the y of oct 5 read past the table into DS:03DC), so
  they are drawn **after** the camera cell, several twice; oct 5 draws (-2,2) (behind-left) and oct 7 (2,2).
* `DS:04CC` (x) = `-1 1 1 -1 0 1 0 -1 0`, `DS:04DE` (y) = `1 1 -1 -1 1 0 -1 0 0` (corners, edges, centre; heading independent).
* `DS:0506` dims `1024 256 64 16 8`; `DS:04FA` = `1 1 1 1 1 0`; `DS:04F4` = `0 0x1000 0x2000` (used by 0D08, not here).
* Records: `DS:E304[L*32+type]` = near pointer to the first record (pool DS:A06A, 7 bytes each, written by 0AF9),
  `DS:061E[L*32+type]` = record count (words, from the .3DT).
Callers: 03CE, bf71. Callees: 0804, 0865, 0FB1, C6B8, DCA9, E7AC, E8A2.

---------------------------------------------------------------------------------------------------------------------------

## 3. Matrices

### 0000:E26B r3d_matrix_from_angles  (T)
`void near E26B(int16 *m, int16 A, int16 B, int16 C)`. Stores `[4982]=s1=sin A, [4984]=c1=cos A, [4986]=s2, [4988]=c2,
[498A]=s3, [498C]=c3` (in this order) then (every product rounded by mq, sums 16-bit):
```
m[0] = mq(c1,c3) + mq(s1, mq(s2,s3))     m[1] = mq(s1, mq(s2,c3)) - mq(c1,s3)     m[2] = mq(s1,c2)
m[3] = mq(s3,c2)                         m[4] = mq(c3,c2)                         m[5] = -s2
m[6] = mq(c1, mq(s2,s3)) - mq(s1,c3)     m[7] = mq(c1, mq(s2,c3)) + mq(s3,s1)     m[8] = mq(c1,c2)
```
Callers DC64 (m = 498E, A = -a1 = heading, B = -pitch, C = -roll), 2F82, 2E43.

### 0000:E3E2 r3d_matrix_obj_side  (T)
`void near E3E2(void)`: angles `[496A],[496C],[496E]` -> `4982..498C` = s1,c1,s2,c2,s3,c3 (as E26B), then 49A0..49B0 (stored in
the order 49A0, 49A6, 49AC, 49A2, 49A8, 49AE, 49A4, 49AA, 49B0):
```
49A0 = mq(c1,c3) - mq(s1, mq(s2,s3))     49A2 = -mq(s3,c2)     49A4 = mq(s1,c3) + mq(c1, mq(s2,s3))
49A6 = mq(c1,s3) + mq(s1, mq(s2,c3))     49A8 =  mq(c3,c2)     49AA = mq(s1,s3) - mq(c1, mq(s2,c3))
49AC = -mq(s1,c2)                        49AE =  s2            49B0 = mq(c2,c1)
```
Caller DF08 (type 0x18 only; the rows 49A6.. and 49AC.. are used for the side tests, the matrix is then overwritten).

### 0000:E54B r3d_transpose49A0  (V)
Swaps 49A2<->49A6, 49A4<->49AC, 49AA<->49AE. Callers DF08, 0FFB, 13E3; E7AC has the same code inline.

### 0000:E578 mat_mul  (T)
`void near E578(int16 *A, int16 *B, int16 *C)`: `C[r*3+c] = sum_k mq(A[r*3+k], B[k*3+c])` (16-bit wrap sums; C may not alias A/B:
it is written element by element). Only caller `2C6E fm_rotate_apply` (flight model, R2); listed because it sits in this range.

---------------------------------------------------------------------------------------------------------------------------

## 4. Placement and culling

### 0000:E7AC r3d_place_object  (V)
`void near E7AC(uint16 type, int16 a1, int16 a2, int16 a3, int16 X, int16 Y, int16 Z)`, cdecl, caller pops 0xE.
```
[496A]=a1; [496C]=a2; [496E]=a3; [4956]=type;            // [4956] keeps bit 15 and the bank
dead = (type & 0x8000) ? 1 : 0;
bank = (type & 0x7FFF) >> 8;  idx = type & 0x7F;
flags = byte[ DS:0A60[bank] + idx ];                     // 0A60 = {0760 STFLT, 07E0 theatre, 0860 PHOTO, 08E0 (bank 3, never written)}
[497A] = flags & 7;                                       // size class
[8560] = DS:B75E[bank];                                   // bank segment (written by bd24 / 0994)
[4966] = Y - [497E]; [4968] = Z - [4980]; [4964] = X - [497C];
if (E8B5(BP=[4964], BX=[4966], CX=[4968]) != 0) return;  // culled
if (flags & 0x80) { DDFE(); return; }                     // depth-sorted object
[C030] = dead;  [4894] = (uint8)1088:0120();
1433:03A9(-a1, -a2, a3, 0x49A0);  transpose 49A0 (inline E54B);
11EB:00D3(idx, [4964], [4968], -[4966], 0x49A0, 0xC02E);  // far cdecl, 12 bytes
```
Flag byte (FORMATS.md 3DX): bits 0-2 class, bit 7 sorted. Flag arrays: 0760 (STFLT, written by `bd24 load_stflt_3dx`), 07E0
(theatre, `0994`), 0860 (PHOTO, `0994`), 08E0 (no writer, zero). `0994` also writes a copy at DS:0960 (indexed by the 0510
slot, read by 0FFB/bf71, not by E7AC). Callers 04DA, BD8F, BEE1, bf71 (c455, c535, c76d), C73C.

### 0000:E8A2 r3d_cull_point  (T)
`int16 near E8A2(int16 x, int16 y, int16 z)`: `BP=x, BX=y, CX=z; return E8B5();` (saves BP, DI). AX 0 = visible, ZF = (AX==0).

### 0000:E8B5 r3d_cull_core  (T)
Register interface: in `BP = x (east), BX = y (north), CX = z (up)` (s16), `[497A]` = class 0..7; out `AX` = 0 visible / 1 culled,
**ZF = 1 when visible** (`sub ax,ax` / `mov ax,1; or ax,ax`); clobbers BX, CX, DX, DI, BP; saves SI. Matrix `m[k] = DS:498E+2k`.
```
A = (int32)((m6*y + m3*z + m0*x) << 1)      // three imul16 products, 32-bit add/adc (wraps), then shl/rcl (wraps)
B = (int32)((m7*y + m4*z + m1*x) << 1)
C = (int32)((m8*y + m5*z + m2*x) << 1)
[4958:495A] = A; [495C:495E] = B; [4960:4962] = C;      // lo, hi
Ah = A >> 16, Bh = B >> 16, Ch = C >> 16 (s16)
if (Ch > (int16)[48B6]) return 1;                       // too far (signed jg)
if (Ch < DS:48C6[class]) return 1;                      // behind (signed jl)
if ([4360] != 0) {                                      // wide / map mode (bf71)
    s  = (int16)(abs16(Ch) + DS:4936[class]) sar 3;
    t  = s sar 3;  sx = s + t;  sx += t sar 1;          // s + s>>3 + s>>4 (16-bit)
    if (sx < abs16(Ah)) return 1;                       // signed jl
    s2 = (int16)(abs16(Ch) + DS:4946[class]) sar 2;
    sy = s2 + (s2 sar 3);
    if (!(sy > abs16(Bh))) return 1;                    // signed jg; NOTE: equality culls here
    d = ((int16)(sy + abs16(Bh)) sar 2) + Ch;           // QUIRK: uses sy (not |A|) and the signed Ch (not |Ch|)
} else {
    cl = ((uint8)[294B] ^ 1);                           // 1 for 294B=0, 0 for 294B=1 (286+ masks the count to 5 bits)
    aC = abs16(Ch);
    t  = (int16)(aC + [E53E][class]) sar cl;   sx = (t sar 2) + t;
    if (abs16(Ah) > sx) return 1;                       // signed jg
    u  = (int16)(aC + [E544][class]) sar cl;
    by = ((uint8)[365E] != 0) ? (((u sar 3) + u) sar 1) : u;   // byte test of 365E
    if (abs16(Bh) > by) return 1;
    d = ((int16)(abs16(Ah) + abs16(Bh)) sar 2) + aC;
}
[4978] = d;                                             // written only when both frustum tests pass
if (d > (int16)[48B6]) return 1;
return 0;
```
All the additions are 16-bit and wrap (e.g. `abs16(Ch) + 19751` overflows for |Ch| > 13016 in the wide table, class 7 -> negative
-> culled): port with int16 casts. `[4978]` has no reader besides 0FFB (which writes it) (dead output here).

Tables (s16 x 8, index = class; constant, no writer):

| DS | use | values |
|---|---|---|
| 48C6 | min depth (Ch >= ) | -1 -23 -91 -182 -363 -725 -1450 -2900 |
| 48D6 | x radius, [294B]=0 | 2 44 172 344 685 1368 2736 5472 |
| 48E6 | x radius, [294B]!=0 | 2 30 117 233 465 929 1857 3714 |
| 48F6 | y radius, 294B=0 | 3 50 197 394 786 1570 3139 6278 |
| 4906 | y radius, 294B!=0 | 2 26 101 202 403 805 1609 3217 |
| 4916 | y radius, 294B=0, 365E | 4 83 325 650 1297 2590 5179 10358 |
| 4926 | y radius, 294B!=0, 365E | 2 31 120 240 478 955 1910 3820 |
| 4936 | x radius, wide (4360) | 7 157 620 1239 2472 4938 9875 19751 |
| 4946 | y radius, wide | 6 149 589 1174 2351 4696 9393 18786 |

`[48B6]`: 0x2710 in the image, then `(detail+1)*0xD05` (D76B). Callers: E8A2 (04DA path A, DCC0), E7AC.

### 1088:0120 r3d_depth_shade  (T)
`uint16 far 1088:0120(void)` -> AX (0..15), no stack arguments. (simflow line 487 is right; exact form:)
```
if ((uint8)[7D5E] != 0) return 0;                  // map/ortho mode
v = (int8)byte[4963];  v = (v < 0) ? 0 : v;        // top byte of the 32-bit view depth C (= Ch >> 8)
v >>= 1;                                           // 0..63
h = (uint8)(v - (uint8)[497A]);  if (h & 0x80) h = 0;   // 8-bit subtract, sign test
if ((int8)h > 0x1F) h = 0x1F;
if ((uint8)[43B6] == 0) return 0;                  // Alt-A toggles the haze (image initial 1)
return h >> 1;
```
Callers DDFE (record +4), E7AC (`[4894]`, byte). The value is the colour shift for shaded ramps (FORMATS.md fill rule).

### 0000:BD8F r3d_draw_model  (V)
Signature and body as **simflow.md Part 8 Part 2 "0000:BD8F" (line 4245)**, which is correct, with these exact points:
* `[bp-2] = (byte[4088]==0) ? [3FE2] : [3FFA]` is computed and never used (dead).
* `sh = lod - (((uint8)[294B] == 0) ? 3 : 2)`, stored back into the lod slot; `sh > 0`: `rx <<= sh, ry <<= sh` (32-bit, F476 loop),
  `rz = (int16)(rz << sh)`; `sh < 0`: sar32 by -sh, `rz sar -sh` (count = low byte; 16-bit shifts are masked to 5 bits on 286+).
* Accept: `abs16(rx.lo) < 0x7FFF && abs16(ry.lo) < 0x7FFF` (signed; **only the low words**, so rx = 0x0001_0005 passes and
  rx.lo = 0x8000 passes, abs16 = -32768) **[corr]** (simflow wrote abs32(...).lo).
* Then `DCA9(0, 0, -rz); [E2FC] = 1; E7AC(C6B8(model), -ang1, ang2, ang3, rx.lo, -ry.lo, (alt != 0) ? 1 : 0)`; so the object
  vector is `(rx.lo, -ry.lo, rz + (alt != 0))`.
Caller 9ACA.

### 0000:BEE1 r3d_queue_object  (V)
`void near BEE1(uint16 id, int32 x, int32 y, int16 z, int16 a1, int16 a2, int16 a3)` (`[bp+4]`..`[bp+14]`), the BD8F variant for the
secondary camera of bf71 (no C6B8, no lod shift, no external-camera branch):
```
ry = [B76A:B76C] + y - 0x01000000;  rz = z - [B808];  rx = x - [B766:B768];     // 32-bit add/sub with carry
if (abs16(rx.lo) >= 0x7FFF || abs16(ry.lo) >= 0x7FFF) return;                  // signed, low words only
DCA9(0, 0, -rz);  [E2FC] = 1;
E7AC(id, -a1, a2, a3, rx.lo, -ry.lo, (z != 0) ? 1 : 0);
```
`B766/B76A/B808` are written by bf71 (TV camera position). Caller C834 fx_smoke_draw.

### 0000:DCC0 r3d_queue_billboard  (V)
Algorithm as **simflow.md Part 8 Part 2 "0000:DCC0" (line 4268)**; exact form:
```
rx = x - [BA10:BA12];  ry = y + [C02A:C02C] - 0x01000000;  rz = z - [2DF4];            // 32/32/16 bit
if ((uint8)[C07A] & 0x80) { rx = rx - [B288:B28A] + [BA10:BA12]; ry = ry - [C02A:C02C] + [B752:B754]; rz = rz - [B75C] + [2DF4]; }
n = ((uint8)[294B] == 0) ? 3 : 2;                  // (cmp byte,1; sbb; add -2; neg al)
ry = sar32(ry, n);  rz = rz sar n;  rx = sar32(rx, n);       // in this order
if (abs16(rx.lo) >= 0x7FFF || abs16(ry.lo) >= 0x7FFF) return;   // low words only (as BD8F)
DCA9(0, 0, -rz);  [E2FC] = 1;
[4964] = rx.lo - [497C];  [4966] = -ry.lo - [497E];  [4968] = ((z != 0) ? 1 : 0) - [4980];   // = rx, -ry, rz + (z!=0)
[497A] = 2;
if (E8A2([4964], [4966], [4968]) != 0) return;
[4956] = id;  [496A] = sx;  [496C] = sy;  [496E] = size;  DDFE();
```
Signature `void near DCC0(uint16 id, int32 x, int32 y, int16 z, int16 sx, int16 sy, int16 size)`. The three last words are copied
into the record +10..+14; DF08 interprets them as screen x, y, size only for ids with bit 0x0400 (sprite records). Caller 9ACA.

---------------------------------------------------------------------------------------------------------------------------

## 5. Sorted queue

### 0000:DDFE r3d_queue_insert  (T)
`void near DDFE(void)`; inputs `[4960:4962]` (C from the last E8B5), `[E2FC]`, `[497A]`, `[49C6]`, `[4956]`, `[4964..4968]`,
`[496A..496E]`; queue `ptr[]` = DS:4E3C (50 near pointers), count `[49C4]`, pool DS:49F0 (50 x 0x16).
```
if ([49C4] == 50) {                                  // full: drop the FARTHEST (index 0) and reuse its record
    e = ptr[0];  for (k = 1; k < 50; k++) ptr[k-1] = ptr[k];  [49C4]--;
} else e = 0x49F0 + 0x16 * [49C4];
key = sar32([4960:4962], (uint8)((4 - (uint8)[E2FC]) * 2));      // F46A: arithmetic, CL loop (E2FC 4..1 -> 0,2,4,6)
if ([E2FC] == 2 && [497A] == 5) key += 0x00200000;               // add [bx],0 / adc [bx+2],20h
e->key = key;                                       // +0 lo, +2 hi
e->shade = (uint8)1088:0120();                      // +4 (word, high byte 0)   [corr] simflow called it rand_byte
e->flags = [49C6];  e->type = [4956];               // +6, +8
e->x = [4964]; e->y = [4966]; e->z = [4968];        // +A, +C, +E
e->a1 = [496A]; e->a2 = [496C]; e->a3 = [496E];     // +10, +12, +14
for (i = [49C4] - 1; i > -1; i--) {                 // from the nearest end
    r = ptr[i]->key;
    if ((int16)r.hi < (int16)key.hi) continue;      // signed hi   [corr] simflow said unsigned
    if ((int16)r.hi > (int16)key.hi) break;
    if ((uint16)r.lo <= (uint16)key.lo) continue;   // unsigned lo
    break;                                          // stop at the first r.key > key (signed 32-bit)
}
for (k = [49C4] - 1; k > i; k--) ptr[k+1] = ptr[k];
ptr[i+1] = e;  [49C4]++;
```
Order: descending key, index 0 = farthest, drawn first. **Tie rule:** a new record goes in front of (is drawn before) the existing
records with an equal key. When full, the dropped record is always the current farthest one, even if the new one is farther.
Callers DCC0, E7AC.

### 0000:DF08 r3d_draw_queued  (V)
`void near DF08(void)`; locals `i [bp-6]`, `rec [bp-4]`, `m1 [bp-0E]`, `m2 [bp-0C]`, `alt [bp-0A]`, `extra_first [bp-2]`.
`draw(id)` below = `11EB:00D3(id, [4964], [4968], -[4966], 0x49A0, 0xC02E)`.
```
for (i = 0; i < [49C4] /*signed*/; i++) {
    rec = ptr[i];
    if (rec->type & 0x8000) { [C030] = 1; v = 0; } else { [C030] = 0; v = [9874]; }
    [C044] = [C042] = v;
    t = rec->type;
    if (t & 0x0400) { 1AD6(rec->a1, rec->a2, rec->a3, t & 7); continue; }      // 2D sprite record (any bank)
    byte[4894] = (uint8)rec->shade;
    [4956] = t & 0x7FFF;
    [4964] = rec->x; [4966] = rec->y; [4968] = rec->z; [496A] = rec->a1; [496C] = rec->a2; [496E] = rec->a3;
    bank = byte[4957];  [8560] = DS:B75E[bank];
    if (bank == 0) {
        [C04A] = [C048] = [C046] = [C03E] = [C03C] = [C03A] = [C038] = 0;       // vars 14,13,12,8,7,6,5
        if (rec->flags & 3) [C046] = 1;                                        // var 12
        id = [4956];
        if (id == 1 || id == 0x0A || id == 0x0F) [C04A] = 1;                   // var 14 (dec al / sub 9 / sub 5 chain)
        else if (id == 0x18) {                                                 // player aircraft model (external views)
            if ([C072] == 0) [4956] = 0x1F;                                    // gear up: single model 1F
            else {
                m1 = 0x21; m2 = 0x22; [C038] = 1; [C03A] = [C072] - 1;         // var 5 = 1, var 6 = gear position 0..2
                E3E2();
                alt = [2DF4];
                if (byte DS:B2AB[[DFE0]*16] & 2) alt -= 0x80;                  // site flag bit 1 = carrier deck (16-byte site rec +7)
                if ([B052] != 0 && alt == 0 && (byte[9B04] & 8)) {             // moving, on the ground/deck, brakes bit
                    [C03C] = 1;                                                // var 7
                    s = mq([4966],[49B0]) + (mq([4968],[49AE]) + mq([4964],[49AC]));   // 16-bit
                    extra_first = (s < 0) ? 1 : 0;                             // jns
                }
                s = mq([4966],[49AA]) + (mq([4968],[49A8]) + mq([4964],[49A6]));
                if (s < 0) { m1 = 0x22; m2 = 0x21; }
            }
            if ([98D8] != 0) [C03E] = [98D8];                                  // var 8 = bay doors 1..2, ONLY for id 0x18
        }
        else if ([4968] != 0 && !(rec->flags & 0x20)) [C048] = 1;              // var 13 (all other ids, incl. > 0x18)
    }
    1433:03A9(-[496A], -[496C], [496E], 0x49A0);  E54B();
    if ([4956] == 0x18) {
        if ([C03C] && extra_first) draw(0x23);
        draw(m1); draw(m2);
        if ([C03C] && !extra_first) draw(0x23);
        continue;
    }
    draw((uint8)[4956]);                                                       // low byte (bank 0: whole id)
    if ([4956] >= 0x100) continue;                                             // only bank 0 gets the overlay
    if ([4956] < 0x25) continue;
    e = [7D4E];                                                                // last line edge drawn by 11EB:0CED
    if (e->flags_word(+2) & 0x8080) continue;                                  // edge rejected
    if ([4956] == 0x25) { size = rec->flags >> 8;  cls = rec->flags & 7; }
    else { k = [4956] - 0x25;  size = DS:4EA2[k];  cls = k + 2; }              // 4EA2 = 6,8,12,16,20,24,30,36,...
    1B1B(e->x0(+0) - 0xA9, e->y0(+4) - 0x6E, size, cls);                       // TV window origin (169,110)
}
```
**[corr]** vs the draft: `[98D8] -> [C03E]` is applied only on the id 0x18 path; ids 1/0Ah/0Fh do not get var 13; vars 5..14 are not
reset for banks 1/2 (they keep the values of the last bank-0 record: port it); the 0x23 model order uses the row 49AC..49B0 dot
product and the 0x21/0x22 order the row 49A6..49AA; `extra_first` is read only when `[C03C]` was set in the same record.
* Sizes/classes for ids > 0x25: k=1..5 -> class 3..7, size 8..24; k >= 6 would index the sprite table 91D8 past its 8 entries
  (never seen in data, U).
* The overlay always uses the TV viewport (1B1B) and subtracts its origin, so in the main view these ids would draw wrong; such
  ids appear only in the bf71 scene (U).
* A far-cell record from 04DA may carry obj bit 7 in the low byte (`0x100|obj` raw); DF08 then passes `id & 0xFF` with bit 7 to
  11EB:00D3 whereas E7AC would pass `& 0x7F` (quirk, U whether reachable).
Names of the read globals: `[C072]` gear animation 0..3 (39C4/CBA0), `[B052]` raw speed (1E9E), `[9B04]` bit 3 wheel brakes,
`[2DF4]` altitude, `[DFE0]` nearest airfield site (39C4), `[98D8]` bay-door animation 0..2 (CBA0), `[7D4E]` (11EB:0CED). The
meaning of model 0x23 (drawn while rolling with brakes on) is (U).
Callers DB3E, bf71. Callees 1AD6, 1B1B, E3E2, E54B, 109D:0006, 1433:03A9, 11EB:00D3.

---------------------------------------------------------------------------------------------------------------------------

## 6. 2D sprites

### 0000:1AD6 r3d_sprite_main / 0000:1B1B r3d_sprite_tv / 0000:1A85 fx_explosion_sprite  (V)
`void near f(int16 x, int16 y, int16 size, int16 cls)`; all three:
```
half = size / 2;                                    // truncating (cwd; sub ax,dx; sar 1)
slot22_sprite_scaled(ctx, x - half, y - half, size, size - (size sar 2), tab[cls].off, tab[cls].seg);
```
1AD6: ctx = `[0344]`, tab = DS:91D8; 1B1B: ctx = `[4042]`, tab = DS:91D8; 1A85: ctx = `[0348] ? [4042] : [0344]`, tab = **DS:9188**.
Tables: 8 far pointers (off, seg); image initial 91D8 = offsets 0,1B6h,361h,4F2h,6A7h,848h,9EFh,B82h in seg 1D50h, 9188 =
0,0D5h,229h,54Bh,810h,9F9h,0CDAh,10F4h in seg 1853h (fixed up by the loader, U). Callers: 1AD6, 1B1B DF08; 1A85 A17E.

---------------------------------------------------------------------------------------------------------------------------

## Open items
1. Whether the 0FB1 0xFF00 quirk (empty override table + .3DT record with bit 7) occurs with the shipped data (check .3DT obj bytes).
2. Model id 0x23 (STFLT) meaning; overlay ids 0x26.. usage; DF08 overlay in the main view.
3. `48A6..48B4` (D76B) have no static readers.
4. 1A85/1AD6 sprite pointer tables: who fixes the segments at load (U).
5. Unicorn harness: DF08, 04DA, DCC0, BD8F were not emulated (they call the 11EB renderer); their arithmetic parts are covered
   by the E8B5/DDFE/0804/0865 tests.

## DGROUP globals seen

| DS | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 0344 | w | r3d_vp_main | Viewport* (=032E) | 03CE, 1AD6, 1A85 / - |
| 0346 | b | ds_0346_hint | toggled each 03CE, never read | 03CE |
| 0348 | w | r3d_force_near | 1 in bf71: near terrain path, 1A85 ctx | bf71 W; 04DA, 1A85 R |
| 034C | 8x9 s16 | terr_cell_order | cell offsets per octant (far->near) | 04DA |
| 03DC | s16 | (overread) | read by path A as rows 8.. | 04DA |
| 04CC/04DE | 9 s16 each | terr_cell_down_x/y | look-down cell order | 04DA, 0D08 |
| 04F4 | 3 s16 | terr_half_tab | 0,1000h,2000h | 0D08 |
| 04FA | 6 s16 | terr_level_on | 1,1,1,1,1,0 | 04DA |
| 0504 | w | ds_0504_hint | dead counter | 04DA only |
| 0506 | 6 s16 | terr_dim | 1024,256,64,16,8,0 | 0865 |
| 0510 | 128 w | r3d_scenery_map | 3DT object -> bank<<8|idx | 0994 W; C6B8, 0FFB R |
| 061E | 5x32 w | terr_rec_count | records per (level,type) | 0AF9 W; 04DA, 0D08, 0FFB, 11F2 |
| 0760/07E0/0860/08E0 | 128 b each | r3d_obj_flags_b0..3 | 3DX flag bytes per bank | bd24, 0994 W; E7AC R (via 0A60) |
| 0960 | 128 b | r3d_obj_flags_slot | flag copy by 0510 slot | 0994 W; 0FFB, bf71 R |
| 0A60 | 4 w | r3d_flag_tab | pointers 0760,07E0,0860,08E0 | E7AC, 72A4, BC12 |
| 0D1E/0D20 | w,w | r3d_centre_x/y | viewport-relative centre | DB71 W |
| 11E2 | b | ds_11E2_hint | light threshold, never written (0) | 03CE |
| 2293/2295 | w,w | clip_xmax/ymax | W-1, H-1 | DB71 (+HUD) W |
| 262A | b | timer_frame_flag | 1 after flip, 0 at ISR frame tick | 1BBC, 1C89 W; DAE8 R |
| 4360 | w | r3d_wide_mode | wide cull tables (bf71) | bf71 W; E8B5, 1442 R |
| 43B6 | b | r3d_haze_on | Alt-A, shade enable (initial 1) | CBA0 W; 1088:0120 R |
| 43B8 | w | frame_rate_ticks | 03CE frame delay | D707 W |
| 4894 | b | r3d_shade | shade shift | E7AC, DF08, 119C W |
| 48A6..48B4 | w | ds_48A6_hint | D76B range values, no readers | D76B |
| 48B6 | w | r3d_far | max view depth (detail+1)*0D05h | D76B W; E8B5 R |
| 48C6..4946 | 9x8 s16 | r3d_cull_tabs | see E8B5 table | E8B5 |
| 4956 | w | r3d_obj_type | current object id | E7AC, DCC0, DF08 W; DDFE, DF08 R |
| 4958..4962 | 3 s32 | r3d_view_A/B/C | view vector of last E8B5 | E8B5 W; DDFE, 1088:0120 R |
| 4964/4966/4968 | 3 s16 | r3d_rel_x/y/z | camera-relative object position | E7AC, DCC0, DF08, 0FFB |
| 496A/496C/496E | 3 s16 | r3d_obj_ang | object angles / sprite x,y,size | E7AC, DCC0, DF08 W; DDFE, E3E2, DF08 R |
| 4978 | w | r3d_dist_est | E8B5 distance estimate | E8B5, 0FFB W; no reader |
| 497A | w | r3d_size_class | object class 0..7 | 04DA, DCC0, E7AC W; E8B5, DDFE, 1088:0120 R |
| 497C/497E/4980 | 3 s16 | r3d_origin | origin for E7AC | DCA9, 0FFB W |
| 4982..498C | 6 s16 | r3d_sincos | sin/cos scratch | E26B, E3E2 W |
| 498E | 9 s16 | r3d_cull_mat | camera/cull matrix | E26B (DC64) W; E8B5, 1442 R |
| 49A0 | 9 s16 | r3d_obj_mat | model matrix for 11EB:00D3 | 1433:03A9, E3E2, E54B |
| 49B2 | 9 s16 | ds_49B2_hint | copy of 498E by DB3E, no reader | DB3E |
| 49C4 | w | r3d_queue_n | queue count (max 50) | DAE8, DDFE W; DF08 R |
| 49C6 | w | r3d_obj_flags | record +6 flags | 9ACA, BCD7, C834 W; DDFE R |
| 49F0 | 50x16h | r3d_queue_pool | queue records | DDFE W; DF08 R |
| 4E3C | 50 w | r3d_queue_ptr | sorted record pointers | DDFE, DF08 |
| 4EA0 | w | r3d_spin_rate | 6 (constant) | DB3E |
| 4EA2 | s16 x 8 | r3d_glow_size | 6,8,12,16,20,24,30,36 | DF08 |
| 7D4E | w | r3d_last_line | last line edge record | 11EB:0CED W; DF08 R |
| 7D50 | w | r3d_colour_tab | 7F60/8360 in 03CE, 7D60 after | 03CE |
| 7D54..7D58 | 3 s16 | r3d_light_dir | -36,36,36 | DB71 W; 11EB:04CD R |
| 7D5E | b | r3d_map_mode | ortho map mode (shade 0) | 119C, 11F2 W |
| 8560 | w | r3d_bank_seg | ES of the current 3DX bank | DAE8, E7AC, DF08, 0FFB, 13E3 W |
| 85CA..85D4 | 6 w | r3d_clip_rect | x0,y0,x1,y1, centre abs | DB71 W |
| 85E2 / 85EA | 4 w / w | gfx_page_seg / r3d_page_seg | page segments / current | 02C2 / DB71 W |
| 9188 / 91D8 | 8 far ptr | fx_expl_sprites / r3d_sprites | sprite pointers | 1A85 / 1AD6, 1B1B |
| 94D6/94D8/94DA | 3 w | terr_cam_rel | camera rel. to cell centre, alt | 04DA |
| 94DC | w | terr_ovr_index | last 0FB1 index | 0FB1 W; 0D08, 0F31 R |
| 9874 | w | r3d_lights | lights value for vars 10/11 | 03CE W; 04DA, DF08 R |
| 9902 / B772 | w / 5 b x n | scen_state_n / scen_state_tab | override table | 0F31 W; 0FB1 R |
| 98D8 / C072 | w | bay door / gear animation | DF08 vars 8 / 5,6 | CBA0 / 39C4, CBA0 W |
| 98DE | w | detail | 0..3 | 3AFB, CCD8 |
| B2AB | b (+16n) | site_flags | site record +7 (bit1 deck) | 516D W |
| B75E | 4 w | r3d_bank_segs | .3DX segment per bank (B760 theatre) | bd24, 0994 W |
| B766/B76A/B808 | s32,s32,s16 | tv_cam_pos | bf71 camera x, y', z | bf71 W; BEE1 R |
| C02E | 32 w | r3d_vars | 3DX var table (C030 v1, C038.. v5..) | 03CE, 04DA, DF08, E7AC, DB3E W |
| E2FC | w | r3d_unit_level | level of current coords | 04DA, BD8F, BEE1, DCC0, 11F2 W; DDFE R |
| E304 | 5x32 w | terr_rec_ptr | first record per (level,type) | 0AF9 W |
| E44A | w | terr_rec_cur | current record pointer | 04DA, 0D08, 0FFB, 11F2 |
| E53E / E544 | w, w | r3d_xrad_tab / r3d_yrad_tab | E8B5 table pointers | DB71 W; E8B5 R |

## Structures seen
* **Viewport** (DS:032E main, DS:402C TV; TextCtx layout of video.md): `+0 page (u8 in the low byte)`, `+4 colour word (0x88 =
  sky for 1442)`, `+0E y0`, `+10 y1`, `+12 x0`, `+14 x1` (inclusive). Image: 032E = {0,2,2Fh,0,0,0,1,0,111,0,319,..},
  402C = {1,2,2Fh,0,0,0,1,110,169,169,245,..}.
* **Queue record** (0x16 bytes, DS:49F0 + 0x16n): `+0 s32 key` (sar32(C, 2*(4-E2FC)), +200000h for E2FC=2 class 5), `+4 u16 shade`
  (1088:0120), `+6 flags` ([49C6]: bits 0-1 -> var 12, bit 5 suppresses var 13, high byte = glow size for id 25h, bits 0-2 =
  glow class), `+8 type` ([4956]: bit 15 destroyed, bit 10 sprite record, bits 8-14 bank, low byte id), `+A,+C,+E s16 x,y,z`
  (camera-relative east, north, up), `+10,+12,+14` a1,a2,a3 (angles; sprite records: screen x, y, size).
* **Scenery record** (7 bytes, pool DS:A06A): `s16 x, s16 y, s16 z` (cell-centre relative, level units), `u8 obj` (bit 7 = look up
  the override table).
* **Override entry** (5 bytes, DS:B772): `u8 level, u8 index_in_tile, u8 cellx, u8 celly, u8 new_obj`.
* **Edge record** fields used by DF08: `+0 x0`, `+2 flags` (8080h = rejected), `+4 y0` (simflow Part 12 4.1).


---------------------------------------------------------------------------------------------------------------------

# Part 2: world-object pass 9ACA, projection B85D, TV-camera effects (R7f)

Range (segment 0, all near cdecl, caller pops): `9ACA r3d_draw_world_objects` (0x6B4 B), `B85D r3d_project_point` (0x1B6),
`BA13 r3d_matrix_row_dot` (0x54), `BA67 r3d_view_depth` (0x6A), `C73C fx_fire_puffs` (0xF8), `C834 fx_smoke_draw` (0xC6).
No index gaps in the range (each function ends exactly where the next one starts); no extra labels.

Notation: `mq(a,b)` = `109D:0006 mul_q15` = `int16(((int32)a*b + 0x4000) >> 15)` (floor shift; V: imul, shl/rcl, shl + adc).
`abs16` = `F302` (`cwd; xor; sub`): **abs16(-0x8000) = -0x8000** (stays negative). `lsl32/sar32` = `F45E rtl_aFlshl` / `F46A
rtl_aFlshr` (count in CL, loop of shl/rcl resp. sar/rcr = arithmetic). `ldiv32` = `F392 rtl_aFldiv` (signed 32/32, truncates
toward 0). Record bases: units `U(i) = DS:C140 + 0x24*i`, sites `S(i) = DS:B2A4 + 0x10*i`, smoke puffs `P(i) = DS:39DA + 8*i`,
projectile slots `W(i) = DS:3C0C + 0x1C*i` (field lists in "Structures seen").

## Overview (call order)

```
03CE r3d_scene_draw: DAE8 sky/ground, 04DA terrain, 9ACA (this), DB3E end scene (DF08 draws the queue), A17E overlay
  9ACA: [view 0x8B] own F-117 -> BD8F            (BD8F/DCC0/BEE1/E7AC/DDFE/DF08: Part 1 (R7d))
        ground-target designator (weapons logic, sites)       -> [3D66], [B28E]
        16 smoke puffs  -> B85D -> DCC0 billboard id 0x400+k  (2D sprite record in the queue)
        units C140: air-target candidate (weapons logic) ; B85D cull ; far = 1 pixel (8929/873F) ;
                    day ground shadow model 4 -> BD8F ; unit model 2FDC[type][near/far] -> BD8F
        12 projectile slots 3C0C: B85D cull ; far = 1 pixel ; model 3372[type] -> BD8F
        falling wreck C0A6/C0B8/C602: model 0x0B -> BD8F
        [external views except 0x8B] own F-117 0x18 + day shadow 0x11 -> BD8F
bf71 mfd_view_render (TV/target window, secondary camera B766/B76A/B808):
        C73C fire puffs around a burning air target -> E7AC ; C834 smoke column of the burning site -> BA67 depth -> BEE1(0x25)
B85D: world point (16-bit "u" units) -> screen via BA13 x3 (matrix DS:B156); results in DS:49E0/49E8/DE92 (+9500, dead)
```
`B85D` is also the projector of the HUD overlay `A17E` (the only other caller). `BA67` is the depth-only variant against the
cull matrix `DS:498E` (row C), used only by C834.

## Ownership recommendation (for the S3.7 merge)

| function | owner | reason |
|---|---|---|
| 9ACA r3d_draw_world_objects | render3d | called once per frame by 03CE between terrain and DB3E; its job is submitting every sim object to BD8F/DCC0. The two designator blocks (A) and (C) below are weapons logic embedded in it: keep them in the same function in the port (order of [94FE]/[9502] writes), but spec-cross-reference them from weapons. |
| B85D r3d_project_point | render3d | pure world->screen projection with the camera matrix; HUD (A17E) is a client. |
| BA13 r3d_matrix_row_dot | render3d | camera-matrix row times vector, only caller B85D. |
| BA67 r3d_view_depth | render3d | camera-axis depth with the cull matrix 498E, only caller C834. |
| C73C fx_fire_puffs | render3d | submits 3DX puff models to E7AC; its state DS:3A5C is purely visual (but it consumes the game RNG, see section). |
| C834 fx_smoke_draw | render3d | submission of the 16 smoke puffs as sorted 0x25 objects for the TV camera; no sim state written except [49C6]/[DE92]. |

---------------------------------------------------------------------------------------------------------------------------

### 0000:B85D r3d_project_point  (T)
`void near B85D(int16 x_u, int16 y_u, int16 z)` - out in globals: `[49E0]` sx (`0xFFFF` = not visible), `[49E8]` sy, `[DE92]`
depth, `[9500]` (written, **never read** anywhere: dead). Locals `bp-2` dx, `bp-8` dy, `bp-E` dz, X/Y/Z 32-bit.
simflow.md Part 8 Part 2 "0000:B85D" (line 4200) is correct in outline; the exact form, with three corrections **[corr]**:
```c
dx = (int16)([C0A4] - x);  dy = (int16)(y - [C0B2]);  dz = (int16)(z - [2DF4]) sar 5;       // 16-bit wrap, then sar
if ((uint8)[C07A] & 0x80) {                                   // external camera
    dx = dx - (int16)sar32(([BA10:BA12] - [B288:B28A]), 5);  // 32-bit sub/sbb, low word after the shift
    dy = dy - (int16)sar32(([C02A:C02C] - [B752:B754]), 5);
    dz = dz - (int16)sar32((int32)(int16)[B75C] - (uint32)(uint16)[2DF4], 5);   // cwd; sub; sbb dx,0  [corr: mixed sign ext.]
}
X = BA13(0, dx, dy, dz);  Y = BA13(1, dx, dy, dz);  Z = BA13(2, dx, dy, dz);      // int32 each
if (Z >= 0) goto invisible;                                   // (or dx,dx; jl): [49E8], [DE92] NOT written
if ((uint8)[294B] != 0) { X = sar32(X,1); Y = sar32(Y,1); }   // Z is not halved
if (!(X <= -Z)) goto invisible;                               // -Z = 32-bit neg (neg/adc/neg); signed 32 compares
if (!(Z <= X)) goto invisible;                                //   => |X| <= -Z : 90 deg horizontal field (Y unbounded)
sx = (int16)ldiv32(lsl32(X, 8), Z) + 0xA0;                    // [49E0] stored here first
sy = (int16)ldiv32(lsl32(Y, 8), Z);
sy = sy - (sy sar 2);                                         // x 3/4 (16-bit)
if ([365E] != 0) sy += ((uint8)[294B] == 0) ? 0x34 : 0x3C;    // (cmp byte,1; sbb; and al,F8; add 3C): 52 / 60
else             sy += 0x64;                                  // 100
[49E8] = sy;                                                  // always written once Z<0 and inside the field
[DE92] = (int16)sar32(Z, 3);
if (sx < 0 || sx > 0x13F) { [9500] = sx; sx = 0xFFFF; }       // signed
lim = ([365E] == 0) ? 0xC7 : 0x60;                            // word compare
if (sy < 0 || sy > lim) { [9500] = sx; sx = 0xFFFF; }         // [corr] uses the CURRENT [49E0]: 0xFFFF if x also failed
[49E0] = sx;  return;
invisible: [49E0] = 0xFFFF;                                   // [49E8], [DE92], [9500] untouched (stale)
```
* Screen: centre x = 0xA0 (160); the scale is 256 in x and 192 in y (256 * 3/4); centre y = 52 (cockpit, style 0), 60 (cockpit,
  style 1), 100 (full screen). Visible rows 0..0x60 in cockpit views, 0..0xC7 otherwise; columns 0..0x13F. (V)
* `[294B]` (HUD style byte) halves X and Y only (= half the focal length), Z unchanged. (V)
* No overflow traps: `|X| <= -Z` bounds the x quotient to [-256,256]; `lsl32(Y,8)` and `lsl32(X,8)` wrap mod 2^32 for
  |X|,|Y| >= 2^23 (the division then gives a wrapped value; reproduced by the test). Z = 0x80000000 cannot occur in practice.
* **[corr]** callers are only `9ACA` and `A17E` (vgxref); `99A7`/`90B9` write 49E0/49E8 themselves (their own projector).
  Readers of the results: `[49E0]` 8C10 903F 90B9 99A7 9ACA A17E B339 B77A; `[49E8]` 8C10 903F 9ACA A17E B339 B77A;
  `[DE92]` 8C10 9ACA A17E C834 (also written by 90B9, 9ACA, A17E, BA67, C834). `[9500]` no reader.
* Quirk (port it): when the point is rejected early (behind, outside the field) `[DE92]` keeps the previous value; 9ACA block
  (F) relies on DE92 without checking 49E0 (see there).
Differential-tested (R7f_emu.py vs R7f_ref.py): 60000 cases (random matrices, rotation matrices, random and near-camera
points, all [C07A]/[294B]/[365E] variants incl. byte/word aliasing; 4665 visible), 0 mismatches on 49E0/49E8/DE92/9500.

### 0000:BA13 r3d_matrix_row_dot  (T)
`int32 near BA13(int16 row, int16 dx, int16 dy, int16 dz)` (DX:AX). Matrix: 9 words at `DS:B156` (`view_matrix`, built by E26B
from 2F82). **[corr]** (simflow line 4227 pairs column 1 with dy; the code pairs it with dz):
```c
return (int32)mq(W[B156 + 2*row], dx) + (int32)mq(W[B15C + 2*row], dz) + (int32)mq(W[B162 + 2*row], dy);   // each cwd'd
```
i.e. with `m[k] = W[B156+2k]`: `row r = m[r]*dx + m[3+r]*dz + m[6+r]*dy` - the same element/axis pairing as E8B5 with the cull
matrix 498E (`A = m0*x + m3*z + m6*y`), so B156 has the E26B layout. The sum is 32-bit (no 16-bit wrap). (V)
Differential-tested: 20000 cases (random matrix and vector, edge values), 0 mismatches.

### 0000:BA67 r3d_view_depth  (T)
`void near BA67(int16 px, int16 py, int16 pz, int16 cx, int16 cy, int16 cz)`; writes `[DE92]` (AX = same low word, unused).
Uses the third row of the cull matrix `DS:498E` (`m2 = [4992]`, `m5 = [4998]`, `m8 = [499E]`, written by E26B via DC64 and by
bf71's own E26B call; vgxref lists no direct writer because E26B writes through a pointer).
```c
a = (int16)(py - cy);  b = (int16)(pz - cz) sar 5;  c = (int16)(cx - px);          // 16-bit wraps
[DE92] = (int16)sar32((int32)mq([4992], c) + (int32)mq([4998], b) + (int32)mq([499E], a), 3);
```
simflow line 4821 is right except the multiplier: it is `mq` = Q15 (`+0x4000 >> 15`), not "mul14". **[corr]**
Quirk (U visual effect): compared with E8B5's depth `C = m2*x + m5*z + m8*y` (object minus camera, y north-inverted) the x and
y terms here have the opposite sign (=> -C, negative = in front, the B85D convention) but the z term does not; for a pitched
TV camera the depth (and so the C834 puff size/gate) is slightly wrong. Port as is. Caller: C834 only.
Differential-tested: 20000 cases, 0 mismatches.

### 0000:C73C fx_fire_puffs  (V)
`void near C73C(int16 hdg, int16 pitch, int16 roll, int16 X, int16 Y, int16 Z)`; caller bf71 (`c73c(-a0, a1, a2, rx, -ry, rz)`,
simflow line 5364, inside the "air target, close view, burning" branch after `df08(); [49C4] = 0`).
State: 6 records at `DS:3A5C`, 8 B: `+0 dh, +2 dp, +4 frame, +6 active`. Table `DS:436A` int16[8] =
`0C00 0800 0400 0200 FE00 FC00 F800 F400` (only entries 0..6 are reachable: `rand_n(7)`).
```c
for (i = 0; i < 6; i++) {
    R = DS:3A5C + 8*i;
    if (R.active == 0 || R.frame >= 6) {                        // signed
        R.dh = W[436A + 2*rand_n(7)];                           // RNG call order: dh, dp, frame
        R.dp = W[436A + 2*rand_n(7)];
        R.frame = rand_n(2) + 1;                                 // 1 or 2
        R.active = 1;  continue;                                 // not drawn this call
    }
    h = hdg + R.dh;  p = pitch + R.dp;                           // 16-bit
    if (abs16(p) > 0x4000 || p == (int16)0x8000) { p = 0x8000 - p; h += 0x8000; }   // (add byte [bp-1],80h)
    f = R.frame++;
    if (i < 4) E7AC(0x25 + f, h, p, roll, X, Y, Z);              // models 0x26..0x2A, [C040] NOT written
    else     { [C040] = f;  E7AC(0x24, h, p, roll, X, Y, Z); }   // model 0x24, var[9] (DS:C02E+2*9) = frame
}
```
**[corr]** simflow line 5427 has the [C040] assignment on the wrong puffs (it is set only for puffs 4 and 5). Consequence
(quirk): puffs 0..3 are drawn with the var[9] left by the previous call's puff 5 (only C73C writes C040) - harmless if models
0x26..0x2A do not use var 9 (U). `rand_n` = `cb62` = the game LCG `DS:926E/9270` shared with the simulation: each
re-randomised puff consumes 3 RNG values, so this TV-window effect perturbs the sim random sequence - must be ported in the
same order for bit-exact replays. `e7ac` contract: Part 1 (R7d) "0000:E7AC" (the frame-specific bank-0 models go through its
0x80 sorted flag / DDFE or are drawn immediately).

### 0000:C834 fx_smoke_draw  (V)
`void near C834(int16 cx, int16 cy, int16 cz)`; caller bf71: `c834((int16)sar32([B766:B768],5), 0x8000 - (int16)sar32([B76A:B76C],5), [B808])`
(simflow line 5365: terrain branch of the TV view). Records `P(i)` = smoke puffs of the burning site (`479c fx_smoke_update`).
```c
if ([3D68] == -1) return;                                       // no burning site
for (i = 0; i < 16; i++) {
    P = DS:39DA + 8*i;
    if (P.x == 0) continue;                                      // free slot
    BA67(P.x, P.y, P.z, cx, cy, cz);                             // -> [DE92]
    if (!([DE92] < 0 && [DE92] > -0x100)) continue;              // signed
    size = c94e(c8fa(P.n, abs16((int16)(0x200 / [DE92]))), 4, 0xFF);   // cwd; idiv (trunc): quotient -2..-512
    [49C6] = ((size & 0xFF) << 8) | ((P.n & 0x0E) >> 1);         // high byte = sprite size, low = class 0..7
    BEE1(0x25, (uint32)(uint16)P.x << 5, (uint32)(uint16)P.y << 5, P.z, 0, 0, 0, 1);   // zero-extended (sub dx,dx)
}
```
`c8fa(mode,v)` = scale by 4/8,5/8,6/8,7/8 for mode 0..3 else v (simflow line 4263); `c94e(v,lo,hi)` = clamp with the wrap
quirk (simflow line 705; v >= 2 here, so it is a plain clamp to 4..255). The 8th pushed word (1) is ignored by BEE1 (BEE1
reads only up to `[bp+14]`, V). DF08 later reads `size = flags >> 8, cls = flags & 7` for id 0x25 (Part 1 (R7d) DF08).
`P.n` = word +6 (count of other live puffs at spawn time, written by 479c) used as both the c8fa mode and the class.

### 0000:9ACA r3d_draw_world_objects  (V)
`void near 9ACA(void)`; caller 03CE only. Locals: `bp-0E lim`, `bp-1C shadow_alt`, `bp-1E alt`, `bp-20 cur_d`, `bp-22 i`,
`bp-26 hi` (0/1), `bp-2E best`. simflow.md Part 8 Part 2 "0000:9ACA" (line 4290) has the right flow; this is the complete,
corrected body. `TILE(xu,yu) = byte[DS:B174 + ((uint16)yu >> 11)*16 + ((uint16)xu >> 11)] & 3` (shr: unsigned).
`OWN_Y32 = 0x01000000 - [C02A:C02C]` (32-bit `sub ax,ax; mov dx,100h; sub; sbb`). `cone(v) = abs16((int16)([E556] - [9502] + [2DEE]))`.
```c
/* (1) own aircraft, view 0x8B only (drawn first) */
if ([C07A] == 0x8B) {                                           // word compare
    [49C6] = TILE([C0A4], [C0B2]);
    BD8F(0x18, [BA10:BA12], OWN_Y32, [2DF4] + 0x10, [2DEE], [2DF0], [2DF2], 4);
}
if ([B28E] != 0) [B28E]--;                                      // designator cooldown (frames)

/* (A) ground-target designator - weapons logic */
if (((uint8)[3D66] & 0x80) && ([DFDC] == 0x13 || [DFDC] == 0x19) && [B28E] == 0 && [E2FE] != 1 && !((uint8)[C07A] & 0x80)) {
    best = -1;  lim = 100 << (6 - (uint8)[3658]);               // shl ax,cl: 6400 day, 3200 night
    cur_d = 0;
    if ([3D66] != -1) {                                         // 0x80|k = "select next after k"
        cur_d = site_dist([3D66] - 0x80) - 1;                   // bb86 -> bbb4(S.x,S.y,1): [94FE] dist, [9502] bearing
        if (cone() > 0x2000) cur_d = 0;                         // signed jle keeps it
    }
    for (i = 0; i < [C0B4]; i++) {                              // signed
        site_dist(i);
        if (!(cone() < 0x2000 || ([E2FE] == 0 && cur_d > 0))) continue;   // abs16(0x8000) < 0x2000: directly behind passes (quirk)
        if ((int16)(i - [3D66]) == -0x80) continue;             // the current target k
        fl = W[S(i)+6];
        if (fl & 0x80) continue;                                // low byte bit 7: destroyed/excluded
        if ([94FE] >= lim) continue;                            // signed
        if ([94FE] <= cur_d) continue;                          // cycling: only farther than the current one
        if ([E2FE] != 2 && (int16)(fl & 0x501) <= 1) continue;
        if (i == 0 && (W[B2A4] == 0 || W[B2A6] == 0 || W[B2B0] == 0)) continue;   // site 0 needs x, y, +0C non-zero
        best = i;  lim = [94FE];
    }
    if ((uint8)best & 0x80) {                                   // none (-1); QUIRK: an index >= 0x80 also counts as none
        if ([3D66] == -1) [B28E] = 4;  else [3D66] = 0xFFFF;
    } else [3D66] = best;
}
/* note: 0xFFFF has bit 7 set, so with no ground target and an LGB selected an empty search repeats every 4th frame (B28E = 4, decremented before the test). */

/* (B) smoke puffs as billboards ([49C6] is NOT set: stale value, see quirks) */
for (i = 0; i < 16; i++) {
    P = DS:39DA + 8*i;
    if (P.x == 0) continue;
    B85D(P.x, P.y, P.z);
    if ([49E0] == 0xFFFF || !([DE92] < 0 && [DE92] > -0x100)) continue;    // signed
    size = c8fa(P.n, abs16((int16)(0x100 / [DE92])));                       // cwd; idiv
    DCC0(0x400 + ((P.n & 0x0E) >> 1), (uint32)(uint16)P.x << 5, (uint32)(uint16)P.y << 5, P.z,
         [49E0], [49E8], size);                                              // id 0x400..0x407 (and al; sar; add ah,4)
}
lim = 0x4B << (6 - (uint8)[3658]);                              // 4800 day / 2400 night
hi  = ([365E] != 0 && (uint16)([2DF4] + [98F2]) > 0x5DC) ? 1 : 0;   // unsigned (jbe); 98F2 = distance to nearest airfield

/* (C1) air-target designator setup - weapons logic */
if (((uint8)[3D64] & 0x80) && [3D64] != -1) {
    k = [3D64] - 0x80;
    cur_d = bbb4(U(k).x, U(k).y, 1);                            // obj_dist_bearing
    if (cone() > 0x2000) cur_d = 0;
} else cur_d = 0;
best = -1;

/* (D) units */
for (i = 0; i < [DED0]; i++) {                                  // signed
    [49C6] = 0;
    if (!((uint8)U(i).flags & 2)) continue;                      // +16 bit 1: active
    if (unit_dist(i) >= 0x12C0) continue;                        // bb9e = bbb4(x,y,0): [94FE]; signed
    /* (C2) candidate - weapons logic */
    if ((((uint8)[3D64] & 0x80) || [94FE] < 0x100) && [94FE] < lim && [94FE] > cur_d && !((uint8)[C07A] & 0x80)
        && !((uint8)U(i).flags & 0x20) && U(i).status != 0) {
        bbb4(U(i).x, U(i).y, 1);                                 // same [94FE], sets [9502]
        if (cone() < 0x2000) { lim = [94FE]; best = i; }
    }
    B85D(U(i).x, U(i).y, U(i).z);
    if ([DE92] >= 0 || [49E0] == 0xFFFF) continue;               // (DE92 test first: stale DE92 possible, see B85D)
    [DE92] = [DE92] sar hi;                                      // global modified
    if ([DE92] <= -0x20) { gfx_set_fg(0x0F); hud_line_view([49E0],[49E8],[49E0],[49E8]); continue; }   // 8929, 873F: 1 white pixel
    if (U(i).z < 0x3E7 && [3658] == 0) {                         // signed; day only
        shadow_alt = 0;  s = S([DFE0]);                          // nearest airfield (39c4)
        if ((byte[s+7] & 2) && abs16(U(i).x - W[s+0]) < ([9900] sar 5) && abs16(U(i).y - W[s+2]) < ([9B06] sar 5))
            shadow_alt = 0x80;                                   // over the carrier deck / raised airfield plane
        if (!([2DF4] == 0x80 && shadow_alt != 0x80))
            BD8F(4, U(i).x32, U(i).y32, shadow_alt, U(i).hdg, 0, 0, 4 - sign(hi));   // [49C6] = 0 here
    }
    bcd7(i);                                                     // [49C6] = terrain class (simflow line 4235)
    if ((uint8)U(i).flags & 0x20) [49C6] |= 0x20;                // or byte
    alt = U(i).z;  if (!((uint8)[C07A] & 0x80)) alt -= 0x10;
    far = ([DE92] <= -0x10) ? 1 : 0;
    BD8F(W[2FDC + 0x20*U(i).type + 2*far], U(i).x32, U(i).y32, alt, U(i).hdg, U(i).pitch, U(i).roll, 4 - hi);
}
[49C6] = 0;
/* (C3) air-target commit - weapons logic */
if (best != -1) [3D64] = best;
if ((uint8)[3D64] & 0x80) [3D64] = 0xFFFF;                       // a request that found nothing deselects

/* (E) projectile slots (49C6 = 0) */
for (i = 0; i < 12; i++) {
    W_ = DS:3C0C + 0x1C*i;
    if (W_.active(+0E) == 0) continue;
    B85D(W_.x, W_.y, W_.z);
    if ([49E0] == 0xFFFF) continue;                              // no DE92 test (visible implies DE92 < 0)
    if ([DE92] <= -0x20) { gfx_set_fg(i < 8 ? 0x29 : 0x0D); hud_line_view(sx,sy,sx,sy); continue; }   // DE92 NOT shifted by hi
    BD8F(W[3372 + 0x12*W_.type(+10)], (uint32)(uint16)W_.x << 5, (uint32)(uint16)W_.y << 5, W_.z,
         W_.a(+8), W_.b(+A), W_.c(+C) + 0x2000, 3 - sign(hi));   // (add ah,20h)
}
/* (F) falling wreck of a killed unit (71b4 / 483c) */
if ([C602] > 0) {                                                // signed
    B85D([C0A6], [C0B8], [C602]);
    if ([DE92] < 0 && [DE92] > -0x100)                          // QUIRK: 49E0 not checked; DE92 may be stale from (E)/(D)
        BD8F(0x0B, (uint32)(uint16)[C0A6] << 5, (uint32)(uint16)[C0B8] << 5, [C602], 0, 0, 0, ([B756] > 0) ? 6 : 5);
}
/* (G) own aircraft in the other external views (drawn last) */
if (((uint8)[C07A] & 0x80) && [C07A] != 0x8B && !([2DF4] == 0 && [C06E] != 0)) {
    [49C6] = TILE([C0A4], [C0B2]);
    BD8F(0x18, [BA10:BA12], OWN_Y32, [2DF4] + 0x10, [2DEE], [2DF0], [2DF2], 4);
    if ((uint16)[2DF4] < 0x3E8 && [3658] == 0)                  // unsigned (jae); day only
        BD8F(0x11, [BA10:BA12], OWN_Y32, [C0A2], [2DEE], 0, 0, 4);   // [corr] alt = [C0A2] (ground plane 0/0x80), not 0
}
```
Submitted categories, in this exact order: (1) own F-117 (view 0x8B), (B) smoke billboards, (D) per unit: shadow model 4 then
unit model, or a pixel, (E) projectiles (model or pixel), (F) wreck 0x0B, (G) own F-117 0x18 + own shadow 0x11. No ground
targets (sites) are drawn here (they are terrain scenery, 04DA). Every BD8F/DCC0 goes into the depth queue or is drawn at once
by E7AC (flag 0x80, Part 1 (R7d)); the pixels (873F) are drawn immediately, i.e. **under** every queued object of the frame.

Arguments summary (BD8F `(model, x32, y32, alt, ang1, ang2, ang3, scale)`; `scale - (style?2:3)` = coordinate shift, Part 1 (R7d)):

| object | model | x32, y32 | alt | ang1/2/3 | last arg | [49C6] |
|---|---|---|---|---|---|---|
| own F-117 | 0x18 | BA10:BA12, OWN_Y32 | [2DF4]+0x10 | 2DEE/2DF0/2DF2 | 4 | TILE(own) |
| own shadow (G) | 0x11 | same | [C0A2] | 2DEE/0/0 | 4 | TILE(own) |
| unit shadow | 4 | U+6/+8, U+A/+C | 0 or 0x80 | U.hdg/0/0 | 4-sign(hi) | 0 |
| unit | W[2FDC+0x20*type+2*far] | U+6.., U+A.. | U.z (-0x10 if not external) | U+E/+10/+12 | 4-hi | bcd7 class, \|0x20 |
| projectile | W[3372+0x12*type] | x<<5, y<<5 (zero-ext) | W.z | +8/+A/+C+0x2000 | 3-sign(hi) | 0 |
| wreck | 0x0B | C0A6<<5, C0B8<<5 | C602 | 0/0/0 | B756>0 ? 6 : 5 | 0 |
| smoke billboard (DCC0) | id 0x400+((n&0xE)>>1) | x<<5, y<<5 | P.z | sx,sy,size | - | stale |

`[49C6]` bits written here: bits 0-1 = terrain class (TILE or bcd7), bit 5 = unit flag 0x20 (DF08: bits 0-1 -> var 12, bit 5
suppresses var 13). C834 writes the whole word (size<<8 | class).

Designator writes (weapons, keep exact): `[3D66]` = best site / 0xFFFF; `[B28E]` = 4 on an empty search with no target,
decremented each frame; `[3D64]` = best unit, or 0xFFFF when a request (bit 7) found nothing; side effects `[94FE]`/`[9502]`
(bbb4) are overwritten many times (final values = last unit visited by C2 / the last site of A).

Quirks to port faithfully:
1. (B) particle billboards use the `[49C6]` left over from the previous code (0 from the end of the last 9ACA, TILE(own) after
   (G)/(1), or C834's size/class from bf71 of the previous frame). DF08 ignores record flags for 0x400 sprite records, so
   probably harmless (V for DF08 per Part 1 (R7d)), but DDFE stores it.
2. (F) uses `[DE92]` without the 49E0 check; when B85D rejects the wreck early, DE92 is the value of the last earlier B85D call
   (or the `sar hi` modified unit depth). BD8F culls by itself (E8B5), so the effect is limited to an extra submission.
3. `abs16(0x8000)` is negative, so a site/unit exactly behind the view axis (relative bearing 0x8000) passes the 45 degree cone.
4. Shadow skipped when `[2DF4] == 0x80` exactly unless the unit is in the airfield zone (sitting on a deck at 0x80).
5. Air designator (C2) auto-selects the nearest unit within 0x100 in the cone even without a request (and every frame when
   `[3D64] == 0xFFFF`, which has bit 7): an "auto-lock" that is original behaviour.
6. `best` tested with `test byte, 0x80`: site index >= 0x80 would be treated as "none" (C0B4 is far below that in practice).

Callees: B85D, BB86, BB9E, BBB4, BCD7, BD8F, DCC0, C8FA, CB39, F302, F45E, 8929 gfx_set_fg (colour into both window contexts),
873F hud_line_view (clipped line in the 3D window context, simflow line 4344). Globals read: see vgxref list (2DEE..2DF4, 2FDC,
3372, 3658, 365E, 39DA.., 3C0C.., 3D64, 3D66, 49C6, 49E0, 49E8, 94FE, 9502, 98F2, 9900, 9B06, B174, B28E, B2A4.., B756,
BA10/12, C02A/2C, C06E, C07A, C0A2, C0A4, C0A6, C0B2, C0B4, C0B8, C140.., C602, DE92, DED0, DFDC, DFE0, E2FE, E556).
Written: 3D64, 3D66, 49C6, B28E, DE92 (+ callee writes).

## Open items
| # | Item |
|---|---|
| 1 | (U) meaning of view 0x8B (own aircraft drawn first, no shadow 0x11) and of `[C06E]` (suppresses own model on the ground in external views). |
| 2 | (U) weapon ids 0x13/0x19 in `[DFDC]` = laser-guided bombs (only these run the ground designator); `[E2FE]` modes 0..2. |
| 3 | (U) visual effect of BA67's z-term sign (pitched TV camera): only matters for puff size and the depth gate. |
| 4 | (U) whether models 0x26..0x2A use var 9 (C040 stale for puffs 0..3 in C73C). |
| 5 | 2FDC is the aircraft type table (32-B entries: +0 near model, +2 far model, name at +6; initial image: MiG-25 Foxbat 7/2, ...); 3372 = DS:3362 projectile prototype +0x10 (simflow 4.7). Initial values in the image may be replaced at load (U). |

## DGROUP globals seen
| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 49E0 | w | r3d_sx | projected x, 0xFFFF = invisible | B85D, 90B9, 99A7, A17E w; 8C10 903F 90B9 99A7 9ACA A17E B339 B77A r |
| 49E8 | w | r3d_sy | projected y | B85D, 90B9, 99A7, A17E w; 8C10 903F 9ACA A17E B339 B77A r |
| DE92 | w | r3d_depth | camera depth >> 3, negative = in front | B85D, BA67, 9ACA (sar), C834, 90B9, A17E w; 8C10 9ACA A17E C834 r |
| 9500 | w | r3d_offscreen_x | rejected sx (dead: no reader) | B85D w |
| B156 | 9 w | view_matrix | camera matrix, E26B layout; row r = m[r],m[3+r],m[6+r] for x,z,y | E26B (2F82) w; BA13 r |
| 4992/4998/499E | w | cull_m2/m5/m8 | depth row of the cull matrix 498E | E26B w; BA67, E8B5 r |
| 49C6 | w | r3d_obj_flags | queue record +6 | 9ACA, BCD7, C834 w; DDFE r |
| 3D64 | w | air_target | unit index, 0xFFFF none, bit 7 = request | 9ACA, cba0, a17e (weapons) |
| 3D66 | w | gnd_target | site index, 0xFFFF none, 0x80\|k = select next after k | 9ACA, cba0, a17e (weapons) |
| B28E | w | gnd_tgt_cooldown | frames | 9ACA only (weapons) |
| 94FE / 9502 | w | tgt_dist / tgt_bearing | bbb4 results | bbb4 w; 9ACA r |
| E556 | w | view_yaw | 0/4000/8000/C000 | b3b3, cba0 w; 9ACA r |
| 3658 | w | night | 0 day, 1 night (shift counts use the low byte) | 9ACA r |
| 365E | w | cockpit_view | | B85D, 9ACA r |
| 294B | b | hud_style | halves X/Y in B85D | B85D r |
| 98F2 | w | airfield_dist | | 39c4 w; 9ACA r |
| C0A2 | w | ground_plane | 0 / 0x80 | 39c4 w; 9ACA r (own shadow alt) |
| 9900 / 9B06 | w | zone_half_x/y | airfield zone (x32) | 39c4 w; 9ACA r |
| DFE0 | w | near_airfield | site index | 39c4 w; 9ACA r |
| DFDC | w | sel_weapon | | 9ACA r |
| E2FE | w | (U) master mode 0..2 | | 9ACA r |
| C602 / C0A6 / C0B8 / B756 | w | wreck z / x_u / y_u / vz | falling wreck | 71b4, 483c w; 9ACA r |
| C06E | w | (U) | suppresses own model on the ground | 9ACA r |
| 39DA | 16 x 8 B | smoke_puffs | | 479c w; 9ACA, C834 r |
| 3C0C | 12 x 28 B | proj_tab | | sim w; 9ACA r |
| 3A5C | 6 x 8 B | fire_puffs | | C73C |
| 436A | 8 w | puff_offsets | 0C00 0800 0400 0200 FE00 FC00 F800 F400 | C73C r |
| C040 | w | var[9] | puff frame for model 0x24 | C73C w; model renderer r |
| 2FDC | 0x20 B per type | actype_tab | +0 near model, +2 far model | 9ACA r |
| 3372 | 0x12 B stride | wproto_model | = 3362+0x10 model id | 9ACA r |
| B174 | 256 B | tile_class | 16x16 map, & 3 | 9ACA, BCD7, a17e r |

## Structures seen
* Unit (0x24 B at C140): +0 x_u, +2 y_u, +4 z, +6/+8 x32, +A/+C y32 (north-inverted like OWN_Y32), +E hdg, +10 pitch, +12 roll,
  +14 type (2FDC index), +16 flags (bit 1 active, bit 5 0x20), +18 status (0 = dead). (V offsets)
* Site (0x10 B at B2A4): +0 x_u, +2 y_u, +6 flags word (bit 7 excluded; mask 0x501), +7 bit 1 = carrier/raised airfield, +0C word (site 0 test).
* Smoke puff (8 B at 39DA): +0 x_u (0 = free), +2 y_u, +4 z (altitude/age), +6 n (c8fa mode and class).
* Projectile slot (0x1C B at 3C0C): +0 x_u, +2 y_u, +4 z, +8/+A/+C angles, +E active, +10 type (0x12-B prototype index).
* Fire puff (8 B at 3A5C): +0 dh, +2 dp, +4 frame, +6 active.


---------------------------------------------------------------------------------------------------------------------

# Part 3: map-MFD terrain renderer, scenery collision probe, site/unit helpers (R7e)

Range (segment 0000): `0FFB terr_probe_point`, `119C map_render_terrain`, `11F2 map_draw_tiles`, `1353 map_tile_bounds`,
`13AC map_pixel_to_tile`, `13E3 map_draw_object`, `77D9 terr_probe_cell`, `BC12 obj_wreck_shape`, `BCD7 r3d_unit_terrain_class`.
Sources: `work/s37/R7e_dis_{a,b,c}.txt` (dsann), caller excerpts `R7e_x{5730,671a,2f82}.txt`. Drafts checked: simflow.md
Part 3 sections 8/9 (lines 1446-1503), Part 8 Part 2 (bcd7), spec_R11a 4.x. Corrections are marked **[corr]**. Marks: (V) read
in the code, (T) differential-tested, (U) unclear. 16-bit two's complement unless stated; `sar` = floor, `idiv` = trunc toward 0.

Cited, not respecified here: `0804 terr_to_level`, `0865 terr_tile_type`, `DAE8`, `DB3E`, `DB71`, `E54B` (Part 1 (R7d));
`11EB:00D3 r3d_draw_object`, `11EB:01B8 r3d_probe_object`, ortho mode `7D5E` in `11EB:0906` (Part 4 (R7a)); `1433:03A9 mat_from_euler`
(Part 6 (R7c) C.12). Not owned (just cited): `0AF9 load_3dt_scenery`, `0C2C load_3dg_terrain` (owner loader, real spec in simflow.md
Part 1 lines 327/348), `0D08 terr_find_scenery_at`, `0F31 terr_set_scenery_state` (owner damage -> S3.8, only the index-level
entries of simflow Part 3 s9 exist), `BC77 obj_cls_soft` (owner weapons, simflow_symbols row 224).

Differential tests (`port/notes/s37/R7e_ref.py` vs the real code under Unicorn, `port/notes/s37/R7e_emu.py`, output `R7e_test_out.txt`),
all **0 mismatches**:

| routine | cases | notes |
|---|---|---|
| 13AC map_pixel_to_tile | 20000 | real and random tile sizes (also negative, 1), centres, px/py incl. negative |
| 1353 map_tile_bounds | 20000 | random [2293]/[2295]/[94E0] incl. negative |
| 11F2 map_draw_tiles | 3000 runs, 158571 calls | 0865 real; 13E3 stubbed (ret) and its args + [7D5E],[94E6],[E2FC] compared call by call in order; final globals 94DE..94E8, DECC, E2FC, E44A |
| 13E3 map_draw_object | 5000 | 1433:03A9 + E54B real (identity check of 49A0), 11EB:00D3 stubbed (retf), class filter, [8560] |
| 0FFB terr_probe_point | 20000 (2727 hits) | 0804/0865/03A9/E54B real, 11EB:01B8 stubbed; random tile tables, record pools, flags, 0510, B75E; x/y/z incl. negative and out-of-theatre |
| 77D9 terr_probe_cell | 5000 | 0FFB stubbed: the three 32-bit args |
| BC12 obj_wreck_shape | 20000 | BC77 and C6B8 real; random site records, 0510, flag tables (incl. the bit-15 negative index) |
| BCD7 r3d_unit_terrain_class | 20000 | random unit table, B174 grid, [DED0], [3D62], unit -2..47 |

---------------------------------------------------------------------------------------------------------------------------

## 0. Overview

**Map MFD terrain (left MFD, `mfd_map_redraw 8387`, simflow Part 3 s8).** `8387` computes the map centre `[4084],[4086]` (cells)
and calls `119C(ctx=[402A] (=0x4014), [4084]/2, 0x4000 - [4086]/2, 9 - [4080])`. The centre is thus passed in **level-4 units
(half cells = 64 world units)**, y as terrain north (`0x4000 - y'/2`), and `shift = 9 - zoom` = 0..7 (zoom `[4080]` 2..9).
`119C` opens a 3D scene with a straight-down camera (DAE8 with pitch 0xC000, origin (0,0,0x2000), sky/ground backdrop on, focal
0x100), then `11F2` walks five passes over tile levels {4,3,2,4,3}; for every visible tile it draws each ground record (`z == 0`)
of that tile's scenery list with `13E3` in orthographic map mode (`[7D5E] = DECC + 0x0E`, R7a 0906), and `DB3E` flushes the
queue. Pixel <-> tile maths: `13AC`, visible range `1353`.

**Scenery collision probe.** `77D9(cellx, celly, alt)` converts a cell position to terrain units and calls `0FFB`, which finds
the nearest "solid" (flag 0x10) record of the one level-4 tile containing the point and asks `11EB:01B8` whether the point is
inside that model; the answer is `byte [C686]` (0x80 inside, 0 outside). Callers: `5730` (ground/air units: flag bit 0x2000 of
unit word +0x18) and `671a` (projectile impact).

**Helpers.** `BC12` = model id to draw for a site (destroyed variants), `BCD7` = per-unit shading class `[49C6]` for `9ACA`/`A17E`.

Call tree: `8387 -> 119C -> {DAE8, slot62/slot26, 11F2 -> {1353 -> 13AC x2, 0865, 13E3 -> {1433:03A9, E54B, 11EB:00D3}}, DB3E}`;
`5730/671a -> 77D9 -> 0FFB -> {0804 x3, 0865, F302 abs, 1433:03A9, E54B, 11EB:01B8}`; `72A4/A17E -> BC12 -> {C6B8, BC77}`;
`9ACA/A17E -> BCD7`.

---------------------------------------------------------------------------------------------------------------------------

## 1. Map MFD terrain

### 0000:119C map_render_terrain
`void near map_render_terrain(Viewport *ctx, int16 px, int16 py, int16 shift)`, cdecl `[bp+4..A]` (V). Only caller `8387`.
```c
byte[4894] = 0;                                    // shade offset 0 (no distance darkening of the map objects)
DAE8(ctx, 0, 0xC000, 0, 0, 0, 0x2000, 1, 0x100);   // a1=0, a2(pitch)=0xC000 (-90 deg), a3=0, origin (0,0,0x2000), backdrop=1, focal=0x100
slot26_offset_set(slot62_pixel_offset(ctx->x0 /*+12*/, ctx->y0 /*+0E*/));   // redundant: DB71 (inside DAE8) already did this
11F2(px, py, shift);
DB3E();                                            // draw the queue (empty: 00D3 draws immediately), offset_clear, var 15/16 advance
word[7D5E] = 0;                                    // back to perspective
```
* DAE8 with backdrop=1 also does `[C686]=0`, `[8560]=[B760]`, `11EB:0F9B`, **waits for the timer frame tick** (`[262A]`, R7d) and
  draws sky/ground with `1442` (R7c) using the camera (pitch -90 deg -> the ground fill). It sets the viewport globals from `ctx`
  (DB71): for the map window `0x4014` (x 74..150, y 110..169): `W=77, H=60`, `[0D1E]=38`, `[0D20]=29` (y0 != 0), `[2293]=76`,
  `[2295]=59`. **[corr]** draft 13AC said "window centre (160,100)": those are only the image values of 0D1E/0D20; at map time
  they are 38/29.
* `[7D50]` (colour table) is **not** set here: it is 0x7D60 at map time (image value 0x7D60; both writers 03CE and BF71 restore
  0x7D60 after their scene, V at 03CE / BF71:C5B6). So map objects use the normal colour table with shade 0 (V).
* `[DB3E]` advances `[C04C]/[C04E]` (animation vars 15/16) once per map redraw too (side effect, V by R7d).
* `C686`: in map mode 0565 marks every plane front-facing, so 00D3 never reports "inside": `[C686]` stays 0 (R7a).

### 0000:11F2 map_draw_tiles  (V,T)
`void near map_draw_tiles(int16 px, int16 py, int16 shift)`, cdecl. Locals: x0 `[bp-6]`, x1 `[bp-12]`, y0 `[bp-8]`, y1 `[bp-2]`,
tx `[bp-C]`, ty `[bp-E]`, tile `[bp-10]`, j `[bp-A]`, sx `[bp-14]`, sy `[bp-4]`. All shift counts use CL = low byte (hardware masks to 5 bits).
```c
[94E2] = px sar shift;   [94E4] = py sar shift;                  // map centre in pixels (sar, CL = low byte of shift)
for ([94E8] = 4; (int16)[94E8] >= 0; [94E8]--) {                 // 5 passes
    [94E6] = ((int16)[94E8] <= 1) ? 0x80 : 0;                    // class filter for 13E3
    L = [E2FC] = DS:0CFC[[94E8]];                                // word table {3,4,2,3,4,(1)}: passes give L = 4,3,2,4,3
    [DECC] = (4 - L)*2 + shift;                                  // 16-bit: pixel -> level-L units shift
    T = [94DE] = 0x1000 sar (uint8)[DECC];                       // tile size in pixels
    if ((int16)T <= 0x10) continue;                              // jg: tiles of 16 px or less are skipped
    [94E0] = 4 << (uint8)((4 - (uint8)L) * 2);                   // tiles per side at this level: L4 4, L3 16, L2 64 (byte arithmetic in CL)
    1353(&x0, &x1, &y0, &y1);
    for (ty = y0; !((int16)y1 < ty); ty++)                       // rows outer (signed)
      for (tx = x0; !(tx > (int16)x1); tx++) {                   // columns inner (signed)
        sx = (int16)(tx * T) - [94E2] + (T sar 1);               // imul, low word; tile centre rel. to the map centre, pixels
        sy = (T sar 1) + ((int16)(T * ty) - [94E4]);
        tile = 0865(L, tx, ty);                                  // R7d (level 4: +2 offset, out of range -> 0)
        if (tile == 0xFFFF) continue;                            // dead (0865 never returns FFFF)
        idx = L*32 + tile;
        p = [E44A] = DS:E304[idx];                               // first 7-byte record {s16 x, s16 y, s16 z, u8 obj}
        for (j = 0; DS:061E[idx] > j /*unsigned ja*/; j++, p = [E44A] += 7) {
            if (p->z != 0) continue;                             // only ground-level records are drawn on the map
            [7D5E] = [DECC] + 0x0E;                              // ortho map scale, 0Eh..15h (R7a 0906)
            c = (uint8)[DECC];
            y = (sy << c) + p->y;                                // 16-bit shl / add (pushed first)
            x = (sx << c) + p->x;
            13E3((uint16)p->obj, x, y);                          // obj zero-extended byte, bit 7 kept
        }
      }
}
// exit: [94E8] = 0xFFFF, [E2FC] = 3 (last pass), [94DE]/[94E0]/[DECC] of the last pass that got past the size test
```
Pass table and draw order (V, `DS:0CFC` = `3,4,2,3,4` then `1` (unused 6th word)):

| pass ([94E8]) | L ([E2FC]) | [94E6] | 13E3 class test | drawn when |
|---|---|---|---|---|
| 4 | 4 | 0 | flag bit7 == 0 | always (shift <= 7 -> T >= 0x20) |
| 3 | 3 | 0 | flag bit7 == 0 | shift <= 5 |
| 2 | 2 | 0 | none (L < 3) | shift <= 3 |
| 1 | 4 | 0x80 | flag bit7 == 0x80 | always |
| 0 | 3 | 0x80 | flag bit7 == 0x80 | shift <= 5 |

The flag is the pristine bank-1 flag byte `DS:07E0[obj]` (via `[0A62]`, see 13E3); bit 7 = "solid" object in the f3d.py
classification (`flat` vs `solid`), so flat level-4/3 objects are painted first, level-2 objects in between, solid level-4/3
objects last (on top) (V; meaning of bit 7 from tools/f3d.py). Level 1 is never drawn on the map.

Units: px, py are level-4 units; one pixel = `2^shift` level-4 units = `2^DECC` level-L units, so `x = (sx << DECC) + rec.x` is
the record position relative to the map centre in level-L units (rec.x/y are relative to the tile centre, FORMATS.md Terrain).
The orthographic projection with `7D5E = DECC + 0Eh` scales back by `2^-DECC` (times the fixed factor of R7a 0906).
Quirks (port them):
* **Destroyed / overridden scenery (obj bit 7, set at runtime by 0F31) is passed as `obj | 0x80`** with no 0FB1 lookup and no C6B8
  mapping; 00D3 returns at once when `es:[0] <= obj` (theatre .3DX files have 94..115 objects < 128), so such objects simply
  vanish from the map (V by reading + data: no shipped .3DT record has bit 7, all theatre banks have < 128 objects).
* No C6B8 mapping at all: in theatre 6 (0994 patches ids 5 and 0x59 to PHOTO objects) the map still draws theatre models 5/0x59 (V).
* Records with `z != 0` are skipped (a few per theatre: e.g. LB L4 1 of 19, VN L1 5 of 121; `port/notes/s37/R7e_data.py`).
* `[94E0]` for level 4 is 4, so only level-4 tiles 0..3 are visited (0865 adds 2: the inner 4x4 of the 8x8 B00C table); the
  filler border never shows (V).
* (U) The row bounds of 1353 treat screen rows as increasing ty, while the scene camera shows +ty (north) upwards (`13E3`
  passes `z = -y`, E7AC convention). With the near-symmetric window (rows 0..59, centre 29) the computed range is the mirror
  of the shown one: the south edge range is 38.67 instead of 40 pixel units. At most a 1-pixel sliver of a tile row can be
  missed; port as is. User check: map MFD shows no tile-row gaps at the bottom edge while scrolling.

### 0000:1353 map_tile_bounds  (V,T)
`void near map_tile_bounds(int16 *x0, int16 *x1, int16 *y0, int16 *y1)`, cdecl.
```c
13AC(0, 0, x0, y0);                         if (*x0 < 0) *x0 = 0;   if (*y0 < 0) *y0 = 0;          // signed (jge)
13AC([2293], [2295], x1, y1);               if (*x1 >= [94E0]) *x1 = [94E0]-1;  if (*y1 >= [94E0]) *y1 = [94E0]-1;  // signed (jl)
```
No other clamp (x0 above the max or x1 below 0 just make the loop empty). `[2293]/[2295]` = viewport W-1/H-1 (DB71).

### 0000:13AC map_pixel_to_tile  (V,T)
`void near map_pixel_to_tile(int16 px, int16 py, int16 *tx, int16 *ty)`, cdecl; px/py are viewport pixels (0..W-1, 0..H-1).
```c
*tx = idiv16((int16)([94E2] - [0D1E] + px), [94DE]);                 // cwd; idiv: truncates toward 0
t   = idiv16((int16)((py - [0D20]) << 2), 3);                         // 16-bit shl 2 then /3 (trunc): aspect 4/3
*ty = idiv16((int16)(t + [94E4]), [94DE]);
```
Truncation (not floor) matters only for negative numerators, which 1353 clamps to 0 anyway (both give tile 0 or -1 -> 0).
`[94DE]` > 0x10 when called from 11F2, so no #DE.

### 0000:13E3 map_draw_object  (V,T)
`void near map_draw_object(uint16 obj, int16 x, int16 y)`, cdecl.
```c
if ((int16)[E2FC] >= 3)
    if ((byte[[0A62] + (obj & 0xFF)] & 0x80) != [94E6]) return;   // [0A62] = 0x07E0 (image const): pristine bank-1 flags
[8560] = [B760];                                                   // theatre bank segment, always (no bank from the id)
1433:03A9(0, 0, 0, 0x49A0);                                        // -> 49A0 = diag(7FFE,7FFE,7FFE), cache 916C.. = (0,7FFF,0,7FFF,0,7FFF)
E54B();                                                            // transpose (no-op on the identity)
11EB:00D3(obj & 0xFF, x, 0, -y, 0x49A0, 0xC02E);                    // world vector (x, 0, -y): model +z = south (E7AC convention)
```
* `byte[07E0 + obj]` with `obj >= 0x80` reads into the PHOTO flag array 0860 (only for bit-7 records, which 00D3 rejects anyway).
* `[0A62]` has no writer (V, vgxref): table `DS:0A60 = {0760, 07E0, 0860, 08E0}` (R7d).
* 49A0 is overwritten here: the camera matrix of the scene was already folded into 7AF8 by `11EB:000C` in DC64 (R7d/R7a).

---------------------------------------------------------------------------------------------------------------------------

## 2. Scenery collision probe

### 0000:0FFB terr_probe_point  (V,T)
`void near terr_probe_point(uint32 x, uint32 y, uint32 z)`, cdecl `[bp+4/6]`, `[bp+8/A]`, `[bp+C/E]` (terrain units: x east,
y north, z up; world = cell << 5). Locals: cx `[bp-E]`, fx `[bp-4]`, cy `si`, fy `di`, `[bp-1A]` = 0x800-fx, `[bp-2]` = 0x800-fy,
tile `[bp-18]`, i `[bp-A]`, level `[bp-6]` = 4.
```c
word[C686] = 0;  [BA16] = 0x7FFF;                       // result / best distance (BA16 shared with 0D08's hit record)
u = 0804(4, x);  cx = (u shr32 12).lo;  fx = u.lo & 0x0FFF;   // 0804(4,v) = (v + 0x20) >> 6, uint32 (carry lost)
u = 0804(4, y);  cy = (u shr32 12).lo;  fy = u.lo & 0x0FFF;
[4980] = 0804(4, z).lo;                                 // probe altitude in level-4 units (negative z -> 32-bit wrap, see below)
[497C] = fx - 0x800;  [497E] = fy - 0x800;              // point relative to the tile centre
tile = 0865(4, cx, cy);                                 // outside the theatre (cx/cy beyond -2..5): tile type 0
if (tile != 0xFFFF) {                                   // always true
    p = [E44A] = DS:E404[tile];                         // = DS:E304[4*32 + tile]
    for (i = 1; DS:061E[4*32 + tile] > i /*unsigned ja*/; i++, p = [E44A] += 7) {   // QUIRK: i starts at 1
        if (p->obj & 0x50) continue;                    // tests the object id byte itself (bits 4 and 6), see quirks
        if (!(byte[0x0960 + p->obj] & 0x10)) continue;  // working flag copy, bit 4 = "solid for collision"
        d = abs16(p->x + (0x800 - fx)) + abs16(p->y + (0x800 - fy));   // 16-bit; abs16 = F302 (abs(-8000h) = -8000h)
        [4978] = d;                                     // every candidate (also non-best); no reader
        if ((int16)d < (int16)[BA16]) { [BA20] = p; [BA16] = d; }      // strict, signed: first of equal distances wins
    }
}
if ([BA16] == 0x7FFF) return;                           // nothing found ([BA20], [8560], 4964.. untouched)
p = [E44A] = [BA20];
[4964] = p->x - [497C];  [4966] = p->y - [497E];  [4968] = p->z - [4980];   // object minus point, level-4 units
id = DS:0510[p->obj];                                   // word table (bank << 8 | index), no 0x7F mask
[8560] = DS:B75E[id >> 8];                              // bank segment (byte index from 0511 + 2*obj)
1433:03A9(0, 0, 0, 0x49A0);  E54B();                    // identity orientation (as 13E3)
11EB:01B8(id & 0xFF, [4964], [4968], -[4966], 0x49A0);  // point-in-model test, ORs 0x80 into C686 when inside (R7a)
```
Result: `byte [C686]` = 0x80 if the point is inside the nearest solid model of its level-4 tile, else 0 (the high byte stays 0).
Quirks (port them):
* **Last record skipped**: the loop index starts at 1 against the count, so records `0 .. count-2` are tested; a tile with one
  record never collides (V,T).
* **`p->obj & 0x50`** excludes ids with bit 4 or bit 6 set regardless of the flag. With the shipped data this disables
  collision for many solid level-4 records: CE 18 of 54, KO 29 of 58 probed; LB 0 solid; other theatres all probed
  (`R7e_data.py`). Looks like a bug (the flag byte was probably meant) - port it.
* Only the single level-4 tile containing the point, only level 4 (the big relief/structure objects), only the one nearest
  candidate (Manhattan distance of the centres in level-4 units) gets the model test.
* No 0FB1 / C6B8: an id with bit 7 would index `DS:0960+obj` and `DS:0510+2*obj` past the tables (into 09E0 / 0610..070F), but
  level-4 records never get bit 7 (0F31 only marks hits of 0D08, whose level loop runs 1..2, V at 0D14/0F10), so it does not happen (V by reading).
* Negative z (alt < 0): only the low word of `(uint32)(z + 0x20) >> 6` is kept, i.e. bits 6..21, which equal the low word of
  `(int32)(z + 0x20) sar 6` for any z (z=-1 -> 0, z=-100 -> 0xFFFE); port with uint32 arithmetic and take the low word.
* Writes `49A0` (model matrix) and `8560` outside a scene (called from the sim step): harmless, every scene rebuilds them (V).

### 0000:77D9 terr_probe_cell  (V,T)
`void near terr_probe_cell(int16 cellx, int16 celly, int16 alt)`:
`0FFB((int32)cellx << 5, ((int32)0x8000 - (int32)celly) << 5, (int32)alt)` (cwd sign extension; `0x8000` as the 32-bit
0x00008000; shl32 by 5, wrap). Callers and use of the result (V):
* `5730 ai_update_units` (5D21): for unit u when `((u ^ [3D60]) & 3) == 0` (every 4th step, staggered):
  `77D9(U.x +2, U.y +4, U.z +6)`; `if (byte[C686]) U.flags(+0x18) |= 0x2000 else &= ~0x2000` (byte `C13E+0x24u+0x19` bit 5).
  `9ACA` copies that bit into `[49C6]` bit 5 (Part 2 (R7f)).
* `671a wpn_proj_update_all` (6B86): `byte[C686] = 0` every step; on the staggered step `77D9(P.x, P.y, P.z)` (record 0x1C at
  3C0C: +0,+2,+4); then `if (P.z < 0 || byte[C686]) impact` (6BB3).
* `2f82` reads `C686` into `[9F66]` after `03CE` (the view's own 00D3 calls: camera inside a model); that value comes from
  DAE8/00D3, not from 0FFB.

---------------------------------------------------------------------------------------------------------------------------

## 3. Site and unit helpers

### 0000:BC12 obj_wreck_shape  (V,T)
`uint16 near obj_wreck_shape(int16 o)`; site record `S = DS:B2A2 + 16*o` (+8 flags, +0E shape word).
```c
id = C6B8(S.shape);                                  // bank-1 mapping through 0510 when bit 8 set, bit 15 kept (R7d)
if (!(byte S.flags & 0x80)) return S.shape;          // not destroyed: the raw word (not the C6B8 result)
tb = DS:0A60[(int16)id sar 8];                       // flag array of the bank; bit 15 of id -> negative index (reads 0962..0A5E)
if (byte[tb + (id & 0xFF)] & 0x20) return S.shape | 0x0100;     // model has a built-in destroyed state
b = BC77(o) ? byte[C0AC] : byte[DEA4];               // replacement wreck model (BC77: C604[type]&15 in {9,11,12,13})
return (uint16)((int16)(int8)b + 0x100);             // cbw; add ah,1
```
* Quirk: a replacement id `b >= 0x80` gives `0x00b` (bank 0 = STFLT) instead of `0x01b` (cbw sign-extends) (V,T).
* Flag bit 0x20 = "has damaged variant" (drawn via the 0x8000 path of C6B8 elsewhere); meaning (U). Callers `72A4`, `A17E`.

### 0000:BCD7 r3d_unit_terrain_class  (V,T)
`void near r3d_unit_terrain_class(int16 u)`; unit record `U = DS:C13E + 0x24*u` (16-bit imul) (+2 x cell, +4 y cell).
```c
c = byte DS:B174[((uint16)U.y >> 11) * 16 + ((uint16)U.x >> 11)] & 3;   // 16x16 level-3 cell grid of the .WLD (CAD8 copy)
if (!((int16)([DED0] - 4) > u)) [49C6] = c;        // the last 4 units (airfield objects) and beyond: raw class 0..3
else [49C6] = (c == 0 && [3D62] == 0) ? 1 : 0;     // ordinary units: 1 only over a class-0 cell with no alert running
```
**[corr]** csv note "last 4 units forced 0/1" is inverted: ordinary units get 0/1, the last four get the raw class.
`[49C6]` becomes record `+6` of the sorted queue (DDFE) -> bits 0-1 = var 12 of the model (R7d/R7f). Callers `9ACA` (per unit,
then `|= 0x20` from unit flag bit 13), `A17E`. `[3D62]` = alert countdown (559A/671A).

---------------------------------------------------------------------------------------------------------------------------

## Open items
1. (U) Map row orientation vs 1353 bounds (section 1, last quirk): user check of the map MFD bottom edge while scrolling.
2. (U) Exact ortho scale factor (out = coord_hi * 2^(2-DECC) in R7a) combined with the 7AF8 matrix of the top-down camera and
   the 3/4 vertical aspect that 13AC assumes: owned by R7a/11EB:000C; a port check is "icons (8548, R7) sit on their site
   models at every zoom".
3. 0FFB `p->obj & 0x50` looks like a bug; faithful port keeps it (CE/KO mountains partly non-solid). Candidate for Enhanced.
4. 0D08 / 0F31 have only index-level entries (simflow Part 3 s9); bit-exact spec belongs to S3.8 (damage), together with BC77.
5. Meaning of flag bits: 0960/07E0 bit 4 (collision), bit 7 (solid/on-top in map), bit 5 (has damaged variant, BC12) - names
   from usage, (U) for the designer's meaning.

## DGROUP globals seen

| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 0510 | 128 w | bank1_objid | `bank<<8 | idx` per bank-1 slot (0994) | 0FFB, C6B8 R |
| 0960 | 128 b | r3d_obj_flags_slot | working flag copy (bit 4 collision) | 0994 W; 0FFB, BF71 R |
| 07E0 | 128 b | r3d_obj_flags_b1 | pristine theatre flags (bit 7 solid) | 0994 W; 13E3 R via 0A62 |
| 0A60 | 4 w | r3d_flag_tab | ptrs 0760/07E0/0860/08E0 (no writer) | BC12, E7AC, 72A4; 0A62 by 13E3 |
| 061E | 5x32 w | terr_rec_count | records per (level, type) (0AF9) | 0FFB, 11F2, 04DA |
| 0CFC | 6 w | map_pass_level | `3,4,2,3,4,1` level per pass (index 4..0) | 11F2 |
| 0D1E / 0D20 | w | r3d_cx / r3d_cy | viewport half size - 1 (DB71); map 38 / 29 | 13AC, 1442, 1001 R; DB71 W |
| 2293 / 2295 | w | clip_xmax / clip_ymax | W-1 / H-1 (DB71); map 76 / 59 | 1353 R |
| 4894 | b | r3d_shade_ofs | ramp shade offset; 0 for the map | 119C, DF08, E7AC W; 1359:01D4 R |
| 4964/4966/4968 | 3 w | r3d_objvec | object minus point (level-4 units) | 0FFB W (also DF08 etc.) |
| 4978 | w | r3d_dist_est | candidate distance (no reader) | 0FFB, E8B5 W |
| 497C/497E/4980 | 3 w | r3d_origin | 0FFB: point rel. tile centre / alt | 0FFB, DCA9 W |
| 49A0 | 9 w | r3d_model_mat | identity for map/probe | 13E3, 0FFB W |
| 49C6 | w | r3d_obj_flags | queue record +6 | BCD7, 9ACA, C834 W; DDFE R |
| 7D5E | w | r3d_ortho | 0 / DECC+0Eh (map) | 11F2 W per object, 119C W 0 |
| 8560 | w | r3d_bank_seg | ES bank of the next object | 13E3, 0FFB W |
| 94DE | w | map_tile_px | tile size in pixels `0x1000 sar DECC` | 11F2 W; 11F2, 13AC R |
| 94E0 | w | map_tiles_side | tiles per side of the level (4/16/64) | 11F2 W; 1353 R |
| 94E2 / 94E4 | w | map_centre_px | map centre x / y(north) in pixels | 11F2 W; 11F2, 13AC R |
| 94E6 | w | map_class_filter | 0 / 0x80 required flag bit 7 (L>=3) | 11F2 W; 13E3 R |
| 94E8 | w | map_pass | pass counter 4..0 (ends FFFF) | 11F2 |
| B174 | 256 b | wld_cell_grid | 16x16 level-3 cell classes | BCD7, 0D08, A17E R |
| B2A2 | 16 B x n | site_tab | site records (+8 flags, +0E shape) | BC12 R |
| B75E | 4 w | r3d_bank_segs | bank segments (B760 theatre) | 0FFB, 13E3 (B760) R |
| BA16 | w | scenery_hit_dist | best Manhattan distance (shared with 0D08 hit rec BA14) | 0FFB, 0D08 |
| BA20 | w | scenery_hit_rec_ptr | best record pointer (shared with 0D08) | 0FFB, 0D08 |
| C0AC / DEA4 | b | wreck_id_soft / wreck_id | replacement wreck model ids | BC12, 0F31 R |
| C13E | 0x24 B x [DED0] | unit_tab | units (+2 x, +4 y, +0x18 flags) | BCD7 R; 5730 W bit 13 |
| C686 | w (byte used) | r3d_inside | 0x80 = probe point inside | 0FFB W0; 11EB OR; 5730, 671A, 2F82 R |
| DECC | w | map_shift | `(4-L)*2 + shift` | 11F2 |
| E2FC | w | terr_level | current tile level | 11F2 W; 13E3 R |
| E304 / E404 | 5x32 w | terr_rec_ptr | first record per (level,type); E404 = level 4 | 0AF9 W; 11F2, 04DA, 0FFB R |
| E44A | w | terr_rec_cur | current record pointer | 11F2, 0FFB, 04DA |

## Structures seen
* Viewport (`ctx`, R7d): `+0E y0, +10 y1, +12 x0, +14 x1`; map window `0x4014` = x 74..150, y 110..169.
* Scenery record (7 bytes, pool A06A, 0AF9): `+0 s16 x, +2 s16 y (north), +4 s16 z (up), +6 u8 obj` (bit 7 set at runtime by
  0F31 = changed/destroyed); x/y relative to the tile centre (tile = 0x1000 level units).
* Site record 16 B at B2A2 (simflow Part 5 2.6): `+8 flags (bit 7 destroyed)`, `+0E shape`.
* Unit record 0x24 B at C13E (simflow 6.2): `+2 x, +4 y, +6 z, +0x18 flags (bit 13 = inside scenery, from 0FFB)`.
* Projectile record 0x1C B at 3C0C (671a): `+0 x, +2 y, +4 z` (cells / alt).


---------------------------------------------------------------------------------------------------------------------

# Part 4: 3DX object renderer (segment 11EB) (R7a)

Source: `work/s37/R7a_dis.txt` (dsann of the range), `tools/x86dis.py` for the gaps. Draft checked: section 5 of
`port/notes/s36/spec_R11b.md` (+ its sections 1-4). Corrections against the draft are marked **[corr]**.
Marks: (V) read in the code, (T) differential-tested under Unicorn against `port/notes/s37/R7a_ref.py`
(`R7a_emu.py`, `R7a_emu2.py`), (U) unclear. Disassembler note: in this 16-bit code `cdq` = opcode 99h = **cwd**
(DX = sign of AX) and `cwde` = 98h = **cbw** (AX = sign-extended AL).

Differential tests (all 0 mismatches):

| routine | cases | notes |
|---|---|---|
| 000C r3d_set_camera | 5000 | incl. 7D3A != 0, zoom 0/negative/8000h, also checks the 7B0A transpose |
| 02B6 r3d_frustum_test | 30000 | random s32 x/y/z, radius incl. negative, far 0FFh..7FFFh |
| 0565 r3d_planes (+0658, 0678, 0E5F) | 4000 | random streams with 80pp / 8100 prefixes, map mode, BSP; checks 76D2/77D2/79D2/78D2/7D5C/7D4C/SI |
| 0678 r3d_plane_shade | 10000 | |
| 0819 r3d_xform_one (+0906) | 10000 | also register outputs SI/DI/BP/BX/CX |
| 0906 r3d_project (+0975 INT 0) | 30000 | all z ranges, map-mode bytes, 1804 divide overflows went through the real 0975 handler |
| 0990 r3d_near_bisect | 20000 | |
| 0E5F r3d_bsp_order | 5000 | random trees up to 39 nodes, vis 00/7F/FF, leaf codes 80h..FEh |

Harness note: Unicorn delivers `#DE` to the interrupt hook only once per instance, so the harness checks the
`idiv cx` operands at 0906's four idiv sites itself and vectors through IVT[0] = 11EB:0975 with the return IP =
the idiv (386 behaviour). The handler code itself runs in the emulator.

## 0. Overview of the range

```
caller (13E3 / DF08 / E7AC)  -> 00D3 r3d_draw_object(obj, x, y, z, mat9, vars)
  1433:032E  origin_view(7CA2, 3 x s32) = (x,y,z) * CamMat(7AF8)
  LOD select (|z_hi|), radius test -> 7D5C
  02B6 frustum test (skipped in map mode)                    fail -> no drawing
  031B part matrices / eye / light / part origins (uses 0440, 04CD, 04F9)
  0565 plane visibility 76D2, shade 77D2 (0678, dead), vertex marks 79D2 (0658 clears), BSP order 78D2 (0E5F)
  0281 hook INT 0 -> 0975
  06B9 vertices: 0819 (v*M + origin) -> 0906 project -> D688       (or the shared-vertex path)
  0A37 edges: 0B3F builds the 24-byte record, 0990 near-plane bisection, 12EF:0333 clip
  0C7A faces: 0CAC per visible plane -> 0CED face-list interpreter -> 12EF:003E/010A, 1359:0040/0049
  02A2 unhook INT 0;  C686 |= inside flag
01B8 r3d_probe_object: same up to 0565, no drawing (point-in-model test for 0FFB).
000C r3d_set_camera (from DC64), 0F9B r3d_shared_verts (from DAE8), 0EF2 r3d_object_extent (no callers).
```

Call contract of the 1433 math routines used here (owned by another reader; C convention, far, first arg at [bp+6]):
* `1433:0314 mat_copy(src, dst)`, `1433:02E2 mat_transpose(src, dst)`: 9 words.
* `1433:0286 mat_mul(A, B, C)`: `C = A*B` (Q15). Row-vector convention: a point transforms as `p' = p*M`,
  `p'_k = sum_j p_j*M[3j+k]`; so `C = A*B` means "first A, then B".
* `1433:0219 mat_vec16(v, M)`: v (3 x s16) is replaced in place by `v*M` (Q15, >>15 rounding = that reader's).
* `1433:032E vec_mat32(v16, M, out32)`: `out_k = sum_j v_j*M[3j+k]` as full s32 (not shifted).
* `1433:03A9 mat_from_euler(a, b, c, out)`: three angles, out 9 words.

Layout facts (V): 11EB:0000..000A are the tail of `11CE:012B` (loader: `jle; call 007C; mov [9668],cx; jmp 0132`),
11EB:000B is a 00h pad. The index entry 0F9B (size 165) stops one byte short: its `retf` sits at 11EB:1040 =
**12EF:0000**; 12EF:0001 is a 00h pad. 11EB:0818 is a `nop` pad. 11EB:0CC9 is a second `ret` (exit of 0CED).
All other "gaps" are local labels of the functions listed below.

---

### 11EB:000C r3d_set_camera
`void far r3d_set_camera(const int16 *mat9 /*[bp+6], DS*/, int16 zoom /*[bp+8]*/)` (V,T) - caller DC64 only
(`DC64`: `mat_from_euler(...) -> DS:49A0`, then `000C(49A0, [bp+0Ah])`).
```c
uint16 k = (uint16)abs16((int16)(zoom << 1));      // shl, cwd, xor, sub: abs16(8000h) = 8000h
if (k < 0x100) k = 0x100;                          // unsigned (jae)
far7D2E = (uint16)(0x007FFF00UL / k);              // div cx: unsigned 32/16, never overflows (<= 7FFFh)
mat_copy(mat9, 7AF8);
mat_transpose(7AF8, 7B0A);                         // transpose of the UNSCALED matrix; never read (see below)
int16 *m = 7AF8;   int16 c = -far7D2E;
for i in {2,5,8}:  m[i] = (int16)(((int32)c * m[i] * 2) >> 16);   // imul, shl ax / rcl dx, keep DX: floor((c*m)/2^15)
for i in {0,3,6,1,4,7}: m[i] = m[i] >> 1;          // sar (floor)
for i in {1,4,7}:
    if (word[7D3A] != 0) m[i] = -m[i];
    else { int16 a = -m[i]; m[i] = a - (a >> 2); } // = -(3/4)*(m/2), sar twice (floor of the negated value)
```
Net: column 0 = m/2, column 1 = -(3/8) m (7D3A = 0), column 2 = -m*far/32768. Zoom 100h gives far = 3FFFh, so
column 2 is about -m/2 (same magnitude as column 0). `far7D2E` is also the far limit of 02B6.
DS:7D3A is never written (image value 0) => the `neg` branch is dead in practice (V: only reader 000C).
**7B0A**: written only through the pointer passed to 02E2 here; `0x7b0a` occurs once in the whole image (this
`lea`) and no slot-relative access reaches it (slot+12h addressing only exists for 7B1C+24h*k) => dead (V).
Globals: W 7AF8..7B1B, 7D2E; R 7D3A.

### 11EB:00D3 r3d_draw_object
`void far r3d_draw_object(uint16 obj, int16 x, int16 y, int16 z, int16 *mat9, int16 *vars)` (V)
(args [bp+6..+10h]; x,y,z = object origin minus camera, world axes; mat9 = object orientation matrix; vars = var
table; DF08 pushes C02E (V at E10D); 13E3 pushes `(obj&FF, [bp+6], 0, -[bp+8], 49A0, C02E)` (V at 141F)). ES = word[8560] (bank segment) throughout.
```c
byte[7D5C] = 0;
if (!(es:[0] > obj)) return;                        // unsigned (ja); no INT 0 hook yet
7C9C,7C9E,7CA0 = x,y,z;  7D1C = mat9;  7D1E = vars;
vec_mat32(7C9C, 7AF8, 7CA2);                        // origin in view space, 3 x s32 (7CA2 x, 7CA6 y, 7CAA z)
si = es:[2*obj + 2];
uint16 az = abs16(word[7CAC]);                      // |z_hi|, abs16(8000h) = 8000h
do { md = es:[si]; mdl = es:[si+2]; si += 4; } while (az > md);   // unsigned; last LOD has 8001h
si = mdl;  radius = es:[si]; si += 2;
if (!(radius > az)) byte[7D5C] = 0x80;              // unsigned compare (ja)
if (byte[7D5E] == 0 && !r3d_frustum_test(radius)) { byte[7D5C] = 0x80; goto out; }
cx = es:[si]; si += 2;  r3d_setup_parts(cx);         // 031B
word[7D2C] = es:[si]; si += 2;                      // nverts
r3d_planes();                                       // 0565
if (abs16(word[7D2A]) < 4) byte[7D5C] = 0x80;       // unsigned (jae)
r3d_hook_div0(); r3d_xform_vertices(); r3d_build_edges(); r3d_draw_faces(); r3d_unhook_div0();
out: word[C686] |= (word[7D5C] & 0x80) ^ 0x80;       // "inside" flag
```
Meaning of 7D5C bit 7 = "outside": set by too-far LOD radius test, frustum fail, < 4 planes, or by 0565 when any
plane is front-facing and not var-hidden (0565 ORs FFh). In map mode (7D5E != 0) all planes are FF => never inside.

### 11EB:01B8 r3d_probe_object
`void far r3d_probe_object(uint16 obj, int16 x, int16 y, int16 z, int16 *mat9)` (V) - caller 0FFB (via 1190).
Identical to 00D3 except: 7D1E is NOT written (stale; 031B part angles and 0565 `8100` tests use the previous
pointer - always C02E in practice (U)); the frustum test is always run (no 7D5E check, **[corr]** draft said "same");
no INT 0 hook, no 06B9/0A37/0C7A. Final `C686 |= ...` identical.

### 11EB:0281 r3d_hook_div0 / 11EB:02A2 r3d_unhook_div0
`near`, no args (V). 0281: `5EB6 = word 0000:0000; 5EB8 = word 0000:0002; 0000:0002 = CS (11EB); 0000:0000 = 0975`.
02A2 writes 5EB6/5EB8 back. Both preserve ES, clobber AX, BX. A port replaces this by checked division (see 0906).

### 11EB:02B6 r3d_frustum_test
`near; in: AX = radius (int16); out: AX = 0 and ZF = 1 if visible, AX = FFFFh and ZF = 0 if culled` (V,T).
Clobbers BX, CX, DX, BP. Reads 7CA2..7CAD (s32 x,y,z), 7D2E.
```c
int16 zh = word[7CAC];
if ((uint16)((abs16(zh) + 1) << 1) >= word[7D2E]) return CULL;   // 16-bit wrap: zh = 7FFFh or -8000h gives
                                                                  // 0 resp. 2 => passes (quirk)
int16 t = zh + radius + zh;      // code: bp:bx = ((zh+radius) : 0) + z32; low add never carries, so only the hi words
if (t < 0) return CULL;          // js on the 16-bit sum (wraps: zh = 7FFFh, radius 2 => t = 0)
if (hi16(abs32(x32)) > (uint16)t) return CULL;     // abs32 by not/not/add 1/adc 0: abs32(-2^31) = 80000000h
if (hi16(abs32(y32)) > (uint16)t) return CULL;     // unsigned (ja)
return VISIBLE;
```

### 11EB:031B r3d_setup_parts
`near; in: CX = nparts (int16), ES:SI = after nparts; out: SI after the pivots` (V). Clobbers all but ES.
```c
word[7D28] = nparts;
eye0 = 7C3C: (-word[7C9C], -word[7C9E], -word[7CA0]);          // 16-bit neg
if (nparts <= 0) { word[7D20] = 7AF8; return; }               // signed (jg). See quirk Q1.
// pass 1: matrices
slot = 7B1C;  word[7D20] = 7B1C;
mat_mul(A = [7D1C] ObjMat, B = 7AF8 CamMat, C = 7B1C);  mat_transpose(ObjMat, 7B1C+12h);  word[7D20] += 24h;
for (k = 1; k < nparts; k++) {                                // nparts-1 times
    var = es:[si]; axis = (int8)es:[si+1]; si += 2;
    ang = vars[var];  (word [7D1E] + 2*var, var = full byte 0..255, no mask)
    a = b = c = 0;  if (axis == 1) b = ang; else if (axis < 1) a = ang; else c = ang;   // signed byte compare
    T = word[7D20] + 24h;                                      // = slot k+1, used as temp
    mat_from_euler(a, b, c, T);                                // PartRot
    mat_mul(A = T, B = 7B1C, C = word[7D20]);                  // M_k = PartRot * M_0  (not hierarchical)
    mat_transpose(T, word[7D20] + 12h);                        // PartRot^T
    word[7D20] += 24h;
}
// pass 2: origins, eyes, lights
word[7D20] = 7B1C;  copy 12 bytes 7CA2 -> 7CAE (part 0 origin);  word[7D26] = 7CAE;
mat_vec16(eye0 = 7C3C, 7B2E);          // eye0 := eye0 * ObjMat^T  (in place)
r3d_light_root(DI = 7B2E);             // 04CD
for (k = 1; k < nparts; k++) {         // BX = 7C3C+6k (eye_k), DI = 7B1C+24h*k+12h
    r3d_part_pivot();                  // 0440, ES:SI = pivot k (3 words)
    eye_k = (eye0.x - es:[si], eye0.y - es:[si+2], eye0.z - es:[si+4]);  si += 6;   // 16-bit wrap
    mat_vec16(eye_k, DI);              // eye_k *= PartRot^T
    r3d_light_part(DI);                // 04F9
    word[7D26] += 0Ch;
}
```
Data order consumed (matches f3d.py): `(var,axis)` x (nparts-1), then pivots x (nparts-1).
Shipped data (V, all 11 banks): axis/var pairs only (16,0), (16,1), (17,0) (vars 16/17 = DS:C04E/C050).

**Part limit (V)**: slots are 24h bytes from 7B1C; 7C3C - 7B1C = 120h = exactly 8 slots. The Euler temp for part k is
slot k+1, so the last temp is slot `nparts` (18 bytes). eye 7C3C+6k and light 7C6C+6k hold 8 entries each
(7C6C+30h = 7C9C), part origins 7CAE+0Ch*k hold 8 (7CAE+60h = 7D0E).
=> **max safe nparts = 7**. nparts = 8: the last temp (slot 8 = 7C3C..7C4D) overwrites eye0..eye2 *before* eye0 is
rotated (eye0 corrupt => wrong culling for every part). nparts >= 9: slot 8 itself and origins of part >= 8 overwrite
7C3C.., 7D0E.. and the scratch pointers 7D1A..7D2E (fatal). Shipped data: max nparts = **2** in every bank
(954 models nparts=1, 33 nparts=2, 1 nparts=0) - the limit is never reached.

Q1 (quirk, V): nparts <= 0 => matrix = camera only (object orientation ignored), eye0 = -(x,y,z) in world axes not
rotated, light slots not recomputed (stale; only feeds the dead shade). One shipped model: PG.3DX at 0134h
(objects 0,3,4,7; 1 plane, 4 vertices).
Q2 (U, data convention): eye_k = (eye0 - pivot)*PartRot^T has no "+ pivot", while vertices rotate about the pivot
(see 0440). So plane `d` values of a part are evaluated in a frame centred on the pivot. Port as is.

### 11EB:0440 r3d_part_pivot
`near; in: BX = eye slot k, DI = slot k + 12h, ES:SI = pivot (3 x s16); SI not advanced` (V).
```c
eye_k = pivot;  vec_mat32(eye_k, word[7D20] /* = 7B1C, M_0 */, 7D0E);   // 7D0E: pivot*M0, s32 x3
eye_k = -eye_k (16-bit neg each);  vec_mat32(eye_k, DI-12h /* M_k */, word[7D26]);
for c in 0..2: org_k[c] = org_k[c] + origin[c] (7CA2+4c) + 7D0E[c];       // two 32-bit add/adc each, wrap
```
=> `origin_k = origin_view + pivot*M_0 - pivot*M_k`, so a part vertex v maps to `(v - pivot)*R*M_0 + pivot*M_0 +
origin` (rotation about the pivot). Leaves eye_k = -pivot (overwritten by 031B).

### 11EB:04CD r3d_light_root / 11EB:04F9 r3d_light_part
`near; in: DI = matrix (slot+12h)` (V). 04CD: `7D4C = 7C6C; 7C6C..= word[7D54], [7D56], [7D58]; mat_vec16(7C6C, DI);
7D4C += 6`. 04F9: `[7D4C]..= 7C6C..7C70 (the already rotated root light); mat_vec16([7D4C], DI); 7D4C += 6`.
Light vector 7D54..58 = (-24h, ax, ax) set by DB71 (image initial 0, 40h, 0). Only consumer: 0678 (dead output).
Labels 0521 / 0544 inside 04F9's index range belong to 0565 (below).

### 11EB:0565 r3d_planes
`near; in: ES:SI = nplanes word; out: SI = start of the vertex list` (V,T). Clobbers AX..DI, BP.
```c
np = es:[si]; si += 2; word[7D2A] = np;  if (np == 0) return;
cnt = abs16(np);  di = 0;  eye = 7C3C;  word[7D4C] = 7C6C;
do {                                              // 'loop' over cnt planes
    byte[7D38] = 0xFF;                            // 0587: reset for every plane
  next:                                           // 058D
    w = es:[si]; si += 2;
    if ((w >> 8) == 0x80) {                       // 0544: part switch, does not count as a plane
        pp = w & 0xFF; eye = 7C3C + 6*pp; word[7D4C] = 7C6C + 6*pp; goto next; }   // mask kept
    if (w == 0x8100) {                            // 0521: conditional plane
        v = es:[si] & 0x1F; val = (int16)(int8)es:[si+1]; si += 2;
        if (val != vars[v]) byte[7D38] = 0x7F;  goto next; }   // 16-bit equality after cbw
    int32 s = (int32)(int16)w*eye.x + (int32)(int16)es:[si]*eye.y + (int32)(int16)es:[si+2]*eye.z  // imul 16x16->32
            + (int32)es:[si+4..7];                // d s32; all adds add/adc mod 2^32
    si += 8;
    dl = (s >= 0) ? 0xFF : 0x00;                  // sign of the 32-bit sum's hi word (cwd + not)
    if (byte[7D5E] != 0) dl = 0xFF;               // map mode: no back-face culling
    dl &= byte[7D38];   byte[76D2 + di] = dl;     // FF visible, 7F front-facing but var-hidden, 00 back-facing
    if (dl & 0x80) {                              // sign flag of the AND
        // (dead test: if nx|nz|dlo|dhi == 0 then dl |= 80h - no-op, ny not included)
        byte[7D5C] |= dl;                         // => FFh
        r3d_plane_shade();                        // 0678
    }
    di++;
} while (--cnt);
r3d_clear_vertex_flags();                         // 0658
for (p = 0; p < abs16(np); p++) {                 // per plane: n byte + n vertex bytes
    n = es:[si++];
    if (n == 0) continue;
    if (byte[76D2+p] > 0x7F) while (n--) byte[79D2 + es:[si++]] = 1;   // only FF marks (unsigned ja)
    else si += n;
}
if (np < 0) { r3d_bsp_order(ES:SI, DI = 78D2, BX = 76D2);  si += 2*abs16(np) + 1; }   // root byte + pairs
```
`7D4C` ends as the light slot of the last part selected. 0x80xx is never a legal nx (data: |n| small).

### 11EB:0658 r3d_clear_vertex_flags
`near` (V,T): `rep stosw` of `(uint16)(abs16(word[7D2C]) + 2) >> 1` zero words at DS:79D2 (ES temporarily = DS).
For nverts n it clears `2*((n+2)>>1)` bytes (n+1 or n+2).

### 11EB:0678 r3d_plane_shade
`near; in: SI = after the plane's d, DI = plane index; SI unchanged on exit` (V,T).
```c
L = word[7D4C];  int32 s = nx*L.x + ny*L.y + nz*L.z;   // imul each, add/adc
byte[77D2 + di] = (uint8)(s >> 14);                     // two shl/rcl, keep low byte of the hi word = bits 14..21
```
**Dead output (V)**: the only reader of 77D2 is 0CAC, which copies it to byte DS:7D5A; DS:7D5A has no reader in the
whole image (vgxref g 7D5A: 0 readers; `0x7d5a` occurs once in vgame_all.asm). A port may skip 0678 and 7D5A.

### 11EB:06B9 r3d_xform_vertices
`near; in: ES:SI = vertex list (after the face planes / BSP), BX ignored` (V). Out: SI after the vertex list (not
for the shared path, see below).
```c
bx = word[7D20]; cx = word[7D2C];
if (cx == 0) return;
if (cx < 0) goto shared;                           // 075E
di = C688; bp = D688; word[7D1A] = C688;           // 7D1A: written, never read (dead)
if (word[7D2A] == 0) { do r3d_xform_one(); while (--cx); return; }   // all vertices, part words inside 0819
dx = 0;                                            // 072E: vertex index
loop: (0730)
    w = es:[si];
    if (w >= 0x8000 && w < 0x8010) {               // 06E6: part switch, NO count consumed
        pp = w & 0x0F; bx = word[7D20] + 24h*pp; p = 7CAE + 0Ch*pp; si += 2;
        copy 12 bytes p -> 7CA2;                   // the object origin is overwritten (stays so after 06B9)
        goto loop; }
    if (byte[79D2 + dx] == 0) { bp += 8; di += 16; si += 6; }   // 0751: skipped vertex keeps its slot
    else r3d_xform_one();                           // advances bp, di, si itself
    dx++;  if (--cx) goto loop;  return;
shared: (075E)                                     // nverts < 0: |nverts| byte indices into DS:4EB6
cx = -cx; word[7D1A] = C688;
do { i = es:[si]; src = 4EB6 + 16*i; dst = C688 + 16*i;
     dst.x/y/z (s32) = src.x/y/z + 7CA2/7CA6/7CAA (add/adc);
     r3d_project(DI = dst, BX = D688 + 8*i);  si++; } while (--cx);
```
The shared path is unused: every bank's shared-vertex count (the word at `offs[n]`, last word of the file) is 0 (V,
all 11 banks), and no model has nverts < 0. Note the shared path does not consult 79D2 and has no part words.

### 11EB:0819 r3d_xform_one
`near; in: ES:SI = vertex (or part word), BX = matrix, DI = out3d (s32 x3, 16-byte slot), BP = out2d (8-byte slot);
out: SI += 6, DI += 10h, BP += 8, BX/CX preserved (BX = new matrix after a part word)` (V,T).
```c
push cx, si, bx, bp;
again: (081D)
w = es:[si];
if (w >= 0x8000 && w < 0x8010) {                  // 07CC: part switch (pops bp, bx, si; cx stays pushed)
    pp = w & 0x0F; bx = word[7D20] + 24h*pp; copy 12 bytes (7CAE + 0Ch*pp) -> 7CA2; si += 2;
    push si, bx, bp; goto again; }                // the new SI/BX are what is restored at the end
x = w; y = es:[si+2]; z = es:[si+4];
for k in 0..2: out3d[k] = x*M[k] + y*M[3+k] + z*M[6+k] + origin[k]   // imul s16*s16 -> s32, add/adc, mod 2^32
r3d_project(DI, BX = bp);  pop bp, bx, si, cx;  bp += 8; di += 16; si += 6;
```
Note: in the all-vertices path (nplanes == 0) part words therefore do not consume the vertex counter either.

### 11EB:0906 r3d_project
`near; in: DI = s32 x,y,z (x at +0, y +4, z +8), BX = out (s32 x at +0, s32 y at +4); clobbers AX, CX, DX` (V,T).
```c
if (byte[7D5E] != 0) goto ortho;                         // 08DA
int16 zh = word[di+0Ah];
if (zh >= 0x100) {                                       // signed (jl)
    for v in (x,y): { int32 n = v >> 8;                  // bytes 1..3, sign-extended (cbw of byte 3)
                      out = (int32)(int16)idiv(n, zh); } // trunc toward 0; #DE -> INT 0 (only n = -2^23, zh = 100h
    return; }                                            //   could, and -8000h is legal on 386+)
if (zh >= 1) {
    uint16 d = (uint16)(z >> 8) >> 1;                    // word [di+9] shr 1 = z >> 9 (80h..7FFFh)
    for v in (x,y): { int32 n = v >> 1;                  // sar dx / rcr ax (32-bit floor)
                      q = idiv(n, d);                    // overflow (|q| > 7FFFh) -> INT 0 -> 0975
                      out = no_ovf ? (int32)(int16)q : (int32)(int16)idiv(n >> 8, d) * 256; }
    return; }
word[bx+2] = 0x8000;  return;                            // behind: only x_hi written, x_lo / y untouched
ortho: (08DA)
int8 m = byte[7D5E];
if (m >= 0x10) {                                         // signed byte (jge); 08E5
    c = m & 0x0F;                                        // 'and' clears CF: c = 0 => no rounding
    for v in (x_hi, y_hi): { r = v >> c; if (c && (v >> (c-1)) & 1) r++;   // sar, CF = last bit out, inc on CF
                             out = (int32)(int16)r; } }
else {                                                   // 08BC
    c = (uint8)(0x10 - m);                               // 1..15 for m = 1..0Fh (count masked to 5 bits)
    for v in (x_hi, y_hi): out = (int32)(int16)(uint16)(v << c); }   // shl, 16-bit truncation
```
Both perspective ranges give `out ~ x*256/z`. Results are s32 because the INT 0 path returns up to 2^23.
The behind marker `x_hi == 8000h` cannot be produced otherwise (max |out| <= 2^23).
Map mode values (V): only writers `0000:1283` (`7D5E = [DECC] + 0Eh`, before each `13E3` call in 11F2) and `0000:11EA`
(`7D5E = 0` after 11F2). `DECC = 2*(4 - [E2FC]) + shift`, levels with `0x1000 >> DECC <= 10h` are skipped, so
DECC = 0..7 and 7D5E = 0Eh..15h: overall `out = coord_hi * 2^(2 - DECC)` (DECC 0,1: shl 2,1; 2: no shift;
3..7: sar 1..5 with round-half-up). Only the low byte of 7D5E is tested anywhere in 11EB.

### 11EB:0975 r3d_div0_handler (INT 0)
Real-mode interrupt handler, `iret` (V,T). At the fault DX:AX = dividend, CX = divisor (unchanged by the fault).
```c
[bp+2] (return IP) += 3;      // 386: IP = the 2-byte idiv -> skips idiv + cwd, lands on 'or ax,ax'
                              // 8086: IP = after idiv -> skips cwd + 'or ax,ax'; both resume at 'mov [bx],ax'
q = idiv((int32)(DX:AX) >> 8, CX);       // byte shuffle = arithmetic >>8 of the 32-bit dividend
DX:AX = (int32)(int16)q << 8;            // cwd, then dl=ah, ah=al, al=0
```
A second overflow cannot occur from 0906's operands (|n>>8| <= 2^22, d >= 80h). Flags restored by `iret`.
Port: `q = n / d; if (q < -32768 || q > 32767) out = (int32)(int16)((n >> 8) / d) * 256; else out = (int16)q;`
(C `/` truncates like idiv; `>>` must be arithmetic.)

### 11EB:0990 r3d_near_bisect
`void far r3d_near_bisect(int32 p0x, p0y, p0z, int32 p1x, p1y, p1z)` (12 words at [bp+6..1Dh], lo word first;
p0 = the in-front vertex, p1 = the behind vertex; the args are modified in place; out: DS:7D42 x s32, DS:7D46 y s32)
(V,T). Clobbers AX, CX, DX.
```c
for (;;) {
    for c in x,y,z: m[c] = (int32)((uint32)p0[c] + (uint32)p1[c] + 1) >> 1;  // add/adc, +1, sar/rcr: the 33rd bit
    store m.x -> 7D42, m.y -> 7D46;                                          // is lost (wrap)
    uint16 h = (uint32)m.z >> 16;
    if (h <= 1) return;                     // unsigned (jbe): 0 <= m.z < 20000h
    if ((int16)(h - 1) < 0) p1 = m;         // h = 8001h..FFFFh: mid is behind
    else                    p0 = m;         // h = 2..8000h: mid in front (h = 8000h counts as front, quirk)
}
```
Terminates because p0.z >= 20000h and p1.z < 0 keep the gap > 20000h while it halves (with the real call sites).
Callers: 0B3F (both near paths).

### 11EB:0A37 r3d_build_edges
`near; in: ES:SI = nedges word; out: SI after the edge pairs` (V).
```c
bp = 5EBA; cx = es:[si]; si += 2;  if (cx == 0) return;     // jcxz
do { v0 = es:[si]; v1 = es:[si+1]; si += 2;
     if (word[7D2A] == 0 || (byte[79D2+v0] & byte[79D2+v1]) != 0) r3d_make_edge(SI = BP, DI = 8*v0, BX = 8*v1);
     bp += 18h; } while (--cx);                              // record index = edge index
```
Edges not built keep the record of an earlier object (stale); faces of visible planes only reference built edges
when the data is consistent (U). Edge counts in data <= 252 (record area holds 354).

### 11EB:0B3F r3d_make_edge
`near; in: BP = record (SI := BP), DI = 8*v0, BX = 8*v1; clobbers all` (V).
`P[v] = D688 + 8v` (projected s32 x at +0, y at +4), `V[v] = C688 + 16v` (3D s32 x,y,z). `cx_ = (uint32)(uint16)word[85D2]`,
`cy_ = (uint32)(uint16)word[85D4]` (the centre is zero-extended: `add ax,c; adc dx,0`). All adds 32-bit wrap.
```c
b0 = word[P[v0]+2] == 0x8000;  b1 = word[P[v1]+2] == 0x8000;
if (!b0 && !b1) {                                     // 0B57
    rec.x0 = P[v0].x + cx_; rec.y0 = P[v0].y + cy_;   // +0, +4 (s32)
    rec.x1 = P[v1].x + cx_; rec.y1 = P[v1].y + cy_;   // +8, +0C
    12EF:0333(SI = rec); return; }
if (b0 && b1) { word[rec+2] = 0x8080; return; }      // 0BAE: nothing else written, no clip call
near(front f, behind b):  r3d_near_bisect(V[f], V[b]);           // front point first (p0)
    nx = ((int32)[7D42] >> 8) * 2 + cx_;  ny = ((int32)[7D46] >> 8) * 2 + cy_;  // bytes 1..3 sign-ext, shl/rcl
if (b1) {                                             // 0A81: v0 front
    rec.x0,y0 = P[v0] + centre;  near(v0, v1);  rec.x1 = rec[+10h] = nx;  rec.y1 = rec[+14h] = ny; }
else {                                                // 0BB6: v0 behind
    rec.x1,y1 = P[v1] + centre;  near(v1, v0);  rec.x0 = rec[+10h] = nx;  rec.y0 = rec[+14h] = ny; }
12EF:0333(SI = rec);  word[rec+2] |= 0x4040;          // after the clip: one-behind marker (C0C0h if rejected)
```
The near point is projected as if z = 8000h (x*256/z with z_hi = 1/2), although the bisection stops anywhere in
0 <= z < 20000h - an approximation of the original, port as is.
Record before 12EF:0333 (24 bytes): `+0 x0, +4 y0, +8 x1, +0C y1` (s32 screen coords incl. centre), `+10h nx, +14h ny`
(s32, only written in the one-behind cases; else stale). 12EF:0333 (other reader) reads +0..+0F, copies them to
DS:8568 for its bisection, rewrites the coordinate words as s16 and the flag word +2 (section 4.1 of spec_R11b),
and does not touch +10h..+17h; 12EF:00E0 reads +10h..+16h when flag 40h is set. 11EB writes flags only as above.

### 11EB:0C7A r3d_draw_faces
`near; in: ES:SI = start of the face-list offset table (or of the single list)` (V).
```c
n = word[7D2A];
if (n > 0) for (p = 0; p < n; p++) r3d_draw_face_plane(BX = p);     // file order
else if (n == 0) r3d_face_list();                                    // one list at SI ('inc cx; loop' = once)
else for (q = 78D2; byte[q] != 0xFF; q++) r3d_draw_face_plane(BX = byte[q]);   // BSP order
```

### 11EB:0CAC r3d_draw_face_plane
`near; in: BX = plane, ES:SI = offset table; SI, BX, DI preserved` (V).
`if (byte[76D2+bx] > 0x7F) { byte[7D5A] = byte[77D2+bx] /*dead*/; r3d_face_list(SI = es:[si + 2*bx]); }`
(unsigned: 7F var-hidden and 00 back-facing planes are not drawn; BSP emits 7F nodes, they are skipped here).

### 11EB:0CED r3d_face_list
`near; in: ES:SI = face list; returns at FFh` (V). Element grammar (first byte b):

| b | element | bytes after b | action |
|---|---|---|---|
| FFh | end | - | return (via 0CC9) |
| 80h..FEh | condition (0CCA) | `val` | `v = b & 7Fh` (7 bits, no &1Fh); if `(int16)(int8)val == vars[v]` continue with the next element; else read `l = next byte`: `l == FFh` => return (list end); else `si += (l & 1Fh) + 1` (skips exactly one simple element; a following condition byte would be mis-skipped - never in data) |
| 20h..3Fh, 60h..7Fh | points (0DFE), n = b & 1Fh | `colour, n vertex bytes` | computes screen positions, draws nothing (call removed: args pushed and dropped) |
| 40h..5Fh | lines (0D5F), n = b & 1Fh | `colour, n edge bytes` | `1359:0040(AX = word[word[7D50] + 2*colour])`; per edge e: `r = 5EBA + 18h*e; word[7D4E] = r` (every edge, also rejected ones); `if (!(byte[r+2] & 80h)) 1359:0049(SI = r)` |
| 00h..1Fh | polygon (0D04), n = b | `n edge bytes, colour` | `word[7D3E] = n; word[7D40] = si` (read only by dead code); if `es:[si+n] == FFh`: `si += n+1` (invisible polygon); else per edge `12EF:003E(SI = 5EBA+18h*e)`, then `12EF:010A(word[word[7D50] + 2*colour])` (far, C arg, ES/SI saved) |

Test order: FFh, bit 7, bit 5, bit 6, else polygon (so 60h..7Fh are points). n = 0 polygons/lines would loop
65536 times (`loop` with CX = 0) - none in data (V: 10842 polygons, 13911 lines, max n = 11, 548 with colour FFh,
0 point elements, 0 chained conditions in all banks). Within a list elements are drawn in stream order.
`7D4E` = record of the last line edge processed (DF08 reads it for object type 25h).
Dead code 0DAF..0DFD (V, unreachable: preceded by `jmp 0CED`, no jump targets it): an alternative polygon end that
calls `12EF:010A` and then re-draws the polygon's edges from 7D40/7D3E as lines (1359:0040/0049). Not ported.

### 11EB:0E5F r3d_bsp_order
`near; in: ES:SI = root byte (pairs follow at SI+1, 2 bytes per node: b0, b1), DI = out (78D2), BX = vis (76D2);
out: FFh-terminated byte list at DI; clobbers AX..DX, BP, DI; SI unchanged; uses the CPU stack (1 word per level:
node<<8 | state)` (V,T). Exact equivalent (the iterative state machine 0/1/2/3/4 is this recursion):
```c
void visit(uint8 n) {
    uint8 b0 = pair[n].b0, b1 = pair[n].b1;
    if (vis[n] != 0) {                       // FF or 7F: camera on the front side
        if (b1 != 0xFF) visit(b1);           // back subtree first (no bit-7 test here: 80h..FEh = node index)
        emit(n);
        if (b0 != 0xFF) { if (b0 & 0x80) emit(b0); else visit(b0); }   // front subtree last
    } else {                                 // 00: node itself is not emitted
        if (b0 != 0xFF) { if (b0 & 0x80) emit(b0); else visit(b0); }
        if (b1 != 0xFF) visit(b1);
    }
}
visit(root); emit(0xFF);
```
vis is read when a node is entered. Leaf codes (b0 >= 80h) never occur in data (V); the asymmetric b1 handling is a
quirk of the original (port it).

### 11EB:0EF2 r3d_object_extent
`void far r3d_object_extent(uint16 obj, int16 out[3])` - no callers (V). Preserves ES, SI, DI.
```c
out[0..2] = 0;  if (!(es:[0] > obj)) return;         // ES = word[8560]
si = es:[es:[2*obj+2] + 2];  si += 2 /*radius ignored*/;  cx = es:[si]; si += 2;
r3d_setup_parts(cx);                                  // stale 7C9C/7D1C/7D1E; clobbers the part buffers
word[7D2C] = es:[si]; si += 2;  r3d_planes();         // only to skip the planes (also writes 76D2.., 7D5C)
for (n = (int16)word[7D2C]; n > 0; n--) {             // n <= 0 => nothing
    do { w = es:[si]; si += 2; } while (w >= 0x8000 && w < 0x8010);   // part words skipped, not counted
    out[0] = max_u16(out[0], abs16(w)); out[1] = max_u16(out[1], abs16(es:[si])); out[2] = max_u16(out[2], abs16(es:[si+2]));
    si += 4; }                                         // unsigned max; abs16(8000h) = 8000h
```
Uses LOD 0 only (`es:[si]` max depth read and ignored).

### 11EB:0F9B r3d_shared_verts
`void far r3d_shared_verts(void)` - caller DAE8 with `word[8560] = word[B760]` (V). Preserves DI, SI, ES, BP; ends
with the `retf` at 12EF:0000.
```c
si = es:[2*es:[0] + 2];  cnt = es:[si]; si += 2;      // the word at offs[n] (end of the offset table)
for (k = 0; k < cnt; k++, si += 6)                    // unsigned count ('loop')
    4EB6[16k] (s32 x,y,z) = (x,y,z) * CamMat(7AF8)    // imul s16*s16 sums, add/adc, no origin
```
cnt = 0 in all 11 shipped banks (V) => does nothing; 4EB6 and the 06B9 shared path are dead in practice.

## Open items
1. Q2: part planes are tested against `(eye0 - pivot)*R^T` (no `+ pivot`). Either the 33 two-part models store part
   plane `d` relative to the pivot or culling of rotated parts is slightly off. Port as is; visual check of a
   rotating part (vars 16/17) at close range (U).
2. 01B8 uses the stale 7D1E (vars ptr) - harmless if every 00D3 caller passes C02E; only DF08 (V) and 13E3 were
   checked (V at E10D and 141F; U for the E142..E1C0 and E896 call sites).
3. Edge records skipped by 0A37 stay stale; any face that references an unbuilt edge would draw garbage. Not
   checked against data (U).
4. 12EF:0333 output layout and flag bits: other reader (R7b); this spec only fixes what 11EB writes.

## DGROUP globals seen

| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 4EB6 | 16 x n | r3d_shared_vtx | shared vertices s32 x,y,z + pad | W 0F9B, R 06B9 shared path (dead in data) |
| 5EB6 | 4 | r3d_old_int0 | saved INT 0 vector off,seg | 0281 W, 02A2 R |
| 5EBA | 18h x 354 | r3d_edges | edge records | W 0A37/0B3F/12EF, R 0CED/12EF/1359 |
| 76D2 | 256 | r3d_plane_vis | FF / 7F / 00 | W 0565, R 0565, 0CAC, 0E5F |
| 77D2 | 256 | r3d_plane_shade | shade byte, dead | W 0678, R 0CAC |
| 78D2 | 256 | r3d_bsp_list | BSP order, FF-terminated | W 0E5F, R 0C7A |
| 79D2 | 256 | r3d_vtx_needed | 1 = vertex used by a visible plane | W 0658/0565, R 06B9, 0A37 |
| 7AF8 | 18 | r3d_cammat | scaled camera matrix | W 000C, R 00D3/01B8 (032E), 031B, 06B9 (nparts<=0), 0F9B |
| 7B0A | 18 | r3d_cammat_t | transpose of the unscaled camera matrix, dead | W 000C |
| 7B1C | 24h x 8 | r3d_partmat | slot k: +0 M_k (model->view), +12h PartRot^T (k=0: ObjMat^T); slot nparts = Euler temp | 031B |
| 7C3C | 6 x 8 | r3d_eye | eye in part frame, s16 x3 | 031B/0440 W, 0565 R |
| 7C6C | 6 x 8 | r3d_light | light in part frame | 04CD/04F9 W, 0678 R |
| 7C9C | 6 | r3d_objpos | object offset (x,y,z s16, world axes) | 00D3/01B8 W, 031B R |
| 7CA2 | 12 | r3d_origin | current origin in view space, 3 x s32 (overwritten by part switches) | 00D3, 06B9, 0819, 0440, 02B6 |
| 7CAE | 0Ch x 8 | r3d_part_origin | part origins s32 x3 | 031B/0440 W, 06B9/0819 R |
| 7D0E | 12 | r3d_pivot_m0 | pivot*M_0 (s32 x3) temp | 0440 |
| 7D1A | 2 | r3d_vtx_ptr | = C688, never read (dead) | 06B9 W |
| 7D1C | 2 | r3d_objmat_ptr | object matrix ptr | 00D3/01B8 W, 031B R |
| 7D1E | 2 | r3d_vars_ptr | var table ptr | 00D3 W, 031B/0565/0CED R |
| 7D20 | 2 | r3d_mat_ptr | current/base part slot (7B1C, or 7AF8 if nparts<=0) | 031B, 0440, 06B9, 0819 |
| 7D26 | 2 | r3d_porg_ptr | current part origin ptr | 031B, 0440 |
| 7D28 | 2 | r3d_nparts | nparts | 031B |
| 7D2A | 2 | r3d_nplanes | nplanes (signed; <0 BSP) | 0565 W; 00D3, 06B9, 0A37, 0C7A R |
| 7D2C | 2 | r3d_nverts | nverts (signed; <0 shared) | 00D3/0EF2 W; 0658, 06B9, 0EF2 R |
| 7D2E | 2 | r3d_far | 7FFF00h / max(abs16(2*zoom),100h); image 7FFFh | 000C W, 02B6 R |
| 7D38 | 1 | r3d_plane_mask | FF, 7F after a failed 8100 test | 0565 |
| 7D3A | 2 | r3d_yflip | never written (0) | 000C R |
| 7D3E, 7D40 | 2+2 | r3d_poly_n, r3d_poly_ptr | polygon edge count / start, read only by dead 0DAF | 0CED W |
| 7D42, 7D46 | 4+4 | r3d_near_x, r3d_near_y | bisection result s32 | 0990 W, 0B3F R |
| 7D4A | 2 | r3d_bsp_vis | vis array ptr | 0E5F |
| 7D4C | 2 | r3d_light_ptr | current light slot | 04CD/04F9/0565 W, 0678 R |
| 7D4E | 2 | r3d_last_line | last line edge record | 0CED W, DF08 R |
| 7D50 | 2 | r3d_coltab | colour table base (7D60/7F60/8160/8360) | 03CE/BF71 W, 0CED R |
| 7D54 | 6 | r3d_light_dir | world light vector | DB71 W, 04CD R |
| 7D5A | 1 | r3d_cur_shade | dead | 0CAC W |
| 7D5C | 1 (read as word) | r3d_outside | bit 7 = outside / not inside | 00D3, 01B8, 0565, 0EF2 |
| 7D5E | 2 (low byte used) | r3d_ortho | 0 perspective; 0Eh..15h map scale | 0000:1283/11EA W; 00D3, 0565, 0906, 1088:0120 R |
| 8560 | 2 | r3d_bank_seg | 3DX bank segment | callers W |
| 85D2, 85D4 | 2+2 | r3d_centre_x/y | projection centre (zero-extended), image A0h, 64h | DB71 W, 0B3F/0CED R |
| C02E | 64 | vars | var table | callers |
| C686 | 2 | r3d_inside | bit 7 = probe point inside a model | 00D3/01B8 OR |
| C688 | 16 x 256 | r3d_vtx3d | view-space vertices s32 x,y,z + pad | 06B9/0819 W, 0990 args via 0B3F |
| D688 | 8 x 256 | r3d_vtx2d | projected s32 x,y (x_hi 8000h = behind) | 0906 W, 0B3F, 0CED R |
| 0000:0000 | 4 | IVT[0] | INT 0 vector | 0281/02A2 |

## Structures seen
* Part slot (24h bytes at 7B1C + 24h*k): `int16 M[9]` (model -> view), `int16 T[9]` (k=0: ObjMat^T, k>0: PartRot^T).
* View vertex (16 bytes at C688 + 16v): `int32 x, y, z; uint8 pad[4]`.
* Projected vertex (8 bytes at D688 + 8v): `int32 x, y` (screen units before adding the centre; `x_hi == 8000h` = behind).
* Edge record (24 bytes at 5EBA + 18h*e): see 0B3F (before clip) and spec_R11b 4.1 / R7b (after clip).
* BSP table in the model: `uint8 root; struct { uint8 front /*b0*/, back /*b1*/; } node[|nplanes|]`, FFh = none.
* Face-list grammar: see 0CED table. Plane stream: see 0565 (prefix words 80pp, 8100 var val).


---------------------------------------------------------------------------------------------------------------------

# Part 5: edge clipper and polygon assembler (12EF), raster back end (1359) (R7b)

Range: `12EF:0000..06A0` (19 indexed functions + labels) and `1359:0000..0DA0` (22 indexed + the mode-M gap routines).
Draft verified and corrected: `port/notes/s36/spec_R11b.md` sections 4, 4.1, 6, 7. Marks: (V) verified in the disassembly,
(T) differential-tested under Unicorn against `port/notes/s37/R7b_ref.py` (harness `port/notes/s37/R7b_emu.py`, output `R7b_test_out.txt`),
(U) unclear. DS = DGROUP 1E23. All coordinates are pixels, window = DS:85CA xmin, 85CC ymin, 85CE xmax, 85D0 ymax (s16,
written by `0000:DB71`; image values 0,0,13Fh,C7h).

**Differential tests (T), seed 7, all 0 mismatches:**
* `12EF:0333` clip_edge alone: 20000 random edges (s16 small/large, s32 huge incl. +-2^31, 8000h/FFFFh/10000h/10001h specials,
  20% degenerate edges with equal ends, 5 windows incl. a negative one), compared: the 24-byte record, DS:8568..85C1 (scratch,
  counters, 85C0/85C1). Coverage: fast accept/clip 2789, fast fail end0/end1 (05/0A) 2016, x-side reject 2967, y reject 8166,
  dx/dy overflow -> 32-bit 167, 85C0 -> 32-bit 2 (negative window only), 32-bit trivial reject 9646, one-end bisection 125,
  two-end bisection 641, bisect_in corner fail 3499, bisect_in 32-step fail 10, bisect_out on-border exit 1383, 32-step exit 24,
  halving quirk hit 896 times (sum -1: 142, sum 0: 466, unhalved lo==hi: 288). No INT 0, near-ret label never reached.
* `1359:0719` edge DDA: 4000 cases x 1..4 edges (random, short, horizontal, vertical, single point, y outside the window),
  compared L/R tables DS:89AC..912F and DS:9130.
* `1359:045D` line_M: 1000 random lines (incl. points, 1..3 pixel lines) over a random page, compared the page.
* Whole polygon chain: 2000 random polygons (3..7 vertices, small/large/huge, 25% with 1..3 near-plane points via flag 40h):
  per edge `12EF:0333` + `12EF:003E`, then `12EF:010A(colour)` with random colour words of every fill type (FF solid incl. ramp
  colours >= A0h with DS:4894 in {0,3,10h,25h,F0h}, dither, FE AND, FD OR, FC stipple, FB none); compared the page,
  DS:8588..85D7 and DS:89AC..9141. Covers walls (1718 drawn, 3 single-row skipped), closing edge (426), 85BE = 0/1/3,
  "nothing rasterised, both walls" (201).

## Overview (call order)

```
11EB:0B3F make_edge:   record (s32 x0,y0,x1,y1) -> lcall 12EF:0333 clip_edge -> (rec+2 |= 4040h if one end behind camera)
11EB:0CED face list, polygon:  per edge  lcall 12EF:003E poly_add_edge -> 1359:0719 edge_dda (L/R tables) + wall y ranges
                               then      lcall 12EF:010A poly_finish(colour word)
                                            1359:0040 -> 01D4 set_colour_M (85D6, ES = page)
                                            closing edge along the near plane (8588/8590) -> 0333 + 003E
                                            walls 0185/019B -> 1359:07D6 wall_fill (x = xmax+1 / xmin-1)
                                            1359:0052 -> 0AF7 fill_M (spans from L/R, then reset tables)
                 lines:   1359:0040 colour, per edge with flag bit 7 clear 1359:0049 -> 045D line_M (clipped by 0B3F)
```
Scan conversion = per-scanline min/max x tables: `L[y]` at DS:89AC+2y (idle 7FFFh), `R[y]` at DS:8D6E+2y (idle 8001h),
DS:9130 = smallest row touched since the last fill (idle 7FFFh). The fill walks rows upward from 9130 until the first row with
`L == 7FFFh` and resets every row it visits. Parts of the polygon outside the window on the left/right are represented by
"walls": vertical runs at x = xmin-1 / xmax+1 that extend the spans to the window border (they are clamped away by the fill).

Conventions of both segments (V): near helpers use registers; far entries use SI = record (0333, 003E, 0719, 01F9) or the C
stack (010A, 07D6, 0002, 01B1, 019B, pixel routines). DF is assumed clear (stos/movs forward). No routine saves AX/BX/CX/DX/BP
unless stated; 0719 and 045D save SI and DI; 0333 preserves SI (pushes/pops it around the scratch work) but clobbers BP,DI.

## Edge record (24 bytes; DS:5EBA + 24*i, or DS:8588 for the closing edge)

| off | before 0333 | after 0333 |
|---|---|---|
| +0 | x0 s32 (lo,hi) | +0 x0 s16 clipped |
| +2 | | flag word, lo byte == hi byte (bits below) |
| +4 | y0 s32 | +4 y0 s16 clipped |
| +6 | | y0 (original) clamped to [ymin,ymax] when it had a y out-code, else the unclipped y0 lo word |
| +8 | x1 s32 | +8 x1 s16 clipped |
| +0A | | unchanged (hi word of x1; never read after clipping) |
| +0C | y1 s32 | +0C y1 s16 clipped |
| +0E | | y1 clamped like +6 |
| +10..+17 | near-plane point x s32, y s32 (only if 0B3F set flag 40h) | unchanged |

Note: "+6/+0E written" holds for every path except: trivial accept (fast path: +2 = 0, +6/+0E keep the sign words; 32-bit
path never accepts trivially), plain y-reject 8080h (+6/+0E untouched). Readers use +6/+0E only when the side bits are set,
which implies they were written (V).

Flag byte (V,T): bit0 end0 x>xmax, bit1 end0 x<xmin, bit2 end1 x>xmax, bit3 end1 x<xmin (from the ORIGINAL ends),
bit6 40h near-plane point present (set by 11EB:0B3F after 0333, not by 0333), bit7 80h rejected. Values produced by 0333:
`00` inside, `0..0F` clipped with side bits, `80` rejected (both ends on the same y side, or same y side + anything),
`85` / `8A` rejected right / left (both ends beyond the same x side; or the edge misses the window: see the failure rules).

## Segment 12EF

Out-codes (both widths): `1` y<ymin, `2` y>ymax, `4` x<xmin, `8` x>xmax.

### 12EF:0333 `void far clip_edge(SI = rec)`  (V,T)
```
if (hi(x0)==sext(lo x0) && hi(x1)==sext(lo x1) && hi(y0)==sext(lo y0) && hi(y1)==sext(lo y1))   // order x0,x1,y0,y1
    goto fast16 (0465);
path32 (0359):
    c1 = outcode32(x1,y1);  c0 = outcode32(x0,y0);
    if ((c0|c1)==0) goto L030F;                        // latent bug, see below
    if (c0 & c1) {                                     // trivial reject
        if (c0 & c1 & 3) rec+2 = 8080h;
        else { al = flags32(c0,c1) | 80h; rec+2 = al*101h; }   // = 85h or 8Ah; also writes +6/+0E
        return; }
    copy rec[0..15] to DS:8568 (P0 at 8568, P1 at 8570) and again to DS:8578 (P0' 8578, P1' 8580);
    al = flags32(c0,c1); rec+2 = al*101h;              // writes +6/+0E too
    if (c1==0) { p = bisect_out(in=8570, out=8568); rec+0 = lo(p.x); rec+4 = lo(p.y); return; }
    if (c0==0) { p = bisect_out(in=8568, out=8570); rec+8 = lo(p.x); rec+0C = lo(p.y); return; }
    [85A8] = c0; [85AA] = c1;
    m = bisect_in(8568, 8570);                         // a point inside the window on the segment
    if (fail) { al = (BP & 4) ? 8Ah : 85h; rec+2 = al*101h; return; }
        // QUIRK: BP = out-code of the LAST midpoint computed by bisect_in, not c0
    [8568] = m;
    p = bisect_out(in=8568, out=8578);  rec+0 = lo(p.x); [8568] = p; rec+4 = lo(p.y);
    p = bisect_out(in=8568, out=8580);  rec+8 = lo(p.x); rec+0C = lo(p.y);       // from end0's border point to P1
    return;
fast16 (0465):
    [85C0] = 0;
    c1 = outcode16(x1,y1);  c0 = outcode16(x0,y0);     // 16-bit lo words
    if (c0==0 && c1==0) { rec+2 = 0; return; }
    if (c0 & c1) {
        if (c0 & c1 & 3) { rec+2 = 8080h; return; }
        flags16(c0,c1,y0,y1);  rec+2 = (c0 & 8) ? 8585h : 8A8Ah;  return; }
    flags16(c0,c1,y0,y1);                              // +6, +0E, +2 = side bits
    dx = x1-x0 (16-bit, signed overflow -> L0315);  [85AC] = dx;
    dy = y1-y0 (overflow -> L0315);                    [85AE] = dy;
    [85C1] = c1;
    if ([85C0] & 1) goto L0315;
    if (!intercept(c0, x0, y0 -> x,y)) { rec+2 = (code|80h)*101h; return; }   // code 05 / 0Ah
    rec+0 = x; rec+4 = y;
    if (!intercept([85C1], x1, y1 -> x,y)) { rec+2 = (code|80h)*101h; return; } // end0 stays clipped
    rec+8 = x; rec+0C = y;
L0315: rec+2 = sext(rec+0); rec+6 = sext(rec+4); rec+0A = sext(rec+8); rec+0E = sext(rec+0C); goto path32;
       // restores the hi words overwritten by flags16; the s32 values equal the originals
```
Both intercepts always use the ORIGINAL dx,dy (85AC/85AE) and the original end1 (rec+8/+0C are written last). (V,T)

**L030F, the latent near `ret` (V):** `rec+2 = 0; ret` (near) inside a far routine. Reached only from 037E, i.e. the 32-bit
path with both out-codes 0. With a window satisfying `0 <= xmin <= xmax <= 7FFFh` and the same for y this is impossible:
the 32-bit path is entered only (a) when some coordinate is outside s16, which outcode32 always marks outside (hi word != 0, or
hi == 0 and lo >= 8000h > xmax unsigned), or (b) via L0315, which requires a dx/dy overflow or 85C0 = 1 and therefore an end
beyond the window by more than 32767. It needs a window bound >= 8000h (e.g. xmax >= 8000h lets x = 8000h..xmax be "inside"
in 32 bits). The game's windows come from DB71 (screen coordinates), so it never happens (never hit in 20000 tests). If it
did: the near ret pops only the caller's IP, execution continues at 12EF:<caller IP> with the caller's CS left on the stack
(from 11EB:0BA8 that is 12EF:0BAD = inside 1359:0369 line_E) -> crash. Port: treat as "accepted" (`rec+2 = 0`, return) and assert.

### 12EF:02D1 `outcode16(BX = x, CX = y) -> AL`  (V,T); labels 0301, 0308
```
al = 0;
if (x < xmin) al |= 4;  if (x > xmax) al |= 8;           // signed jl/jg
if (y < ymin) al |= 1;  if (y > ymax) al |= 2;
if (x - xmin overflows s16) [85C0] = 1;                 // 0301: jo after cmp bx,[85CA]
if (y - ymin overflows s16) [85C0] = 1;                 // 0308: jo after cmp cx,[85CC]
```
The overflow only sets the byte flag; the jl after it still evaluates the true signed comparison (SF != OF), so the out-code is
always correct. Compares against xmax/ymax are not overflow-checked. 85C0 is effectively redundant: with xmin, ymin >= 0 an
overflow implies an end < xmin-32768 and the other end not left, so dx overflows first (only a negative window reached the
85C0 branch in the tests). (V,T)

### 12EF:020B `flags16(AL = c0, AH = c1, CX = y0, BP = y1)`, SI = rec  (V,T); preserves BX
`rec+6 = (c0&2) ? ymax : (c0&1) ? ymin : y0;  rec+0E = same with c1,y1;  b = (c0&8?1:0)|(c0&4?2:0)|(c1&8?4:0)|(c1&4?8:0);
rec+2 = b*101h`. (If both y bits were set, ymax wins; impossible with ymin <= ymax.)

### 12EF:0265 `intercept(AL = outcode, BX = x, CX = y) -> BP = x', DX = y', CF = fail, AL = AH = code`  (V,T); label 025F
```
if (al == 0) { bp = bx; dx = cx; CF = 0; return; }      // 025F (also used as "no clip")
if (al & 0Ch) {                                          // x border first
    xb = (al & 4) ? xmin : xmax;
    y = cx + (int16)idiv((int32)(int16)(xb - bx) * dy, dx);       // imul [85AE], idiv [85AC]: truncates toward 0
    if (y < ymin)      yb = ymin;                        // fall to the y border
    else if (y > ymax) yb = ymax;
    else { bp = xb; dx = y; CF = 0; return; }
} else yb = (al & 1) ? ymin : ymax;
x = bx + (int16)idiv((int32)(int16)(yb - cx) * dx, dy);           // imul [85AC], idiv [85AE]
if (x < xmin) { al = ah = 0Ah; CF = 1; return; }         // edge misses the window: "left"
if (x > xmax) { al = ah = 05h; CF = 1; return; }         // "right"
bp = x; dx = yb; CF = 0;
```
All adds/subs 16-bit wrapping (cannot actually wrap: |xb-bx| <= |dx| and |yb-cy| <= |dy| because the other end lies on the
other side). INT 0 (divide by 0 / quotient overflow) is impossible for the same reason (none in tests). (V,T)

### 12EF:0665 `outcode32(DX:AX = x, CX:BX = y) -> BP, ZF = (BP == 0)`  (V,T)
```
bp = 0;
if (xh < 0 || (xh == 0 && xl < xmin)) bp |= 4;            // lo compares UNSIGNED (jb/ja)
else if (xh > 0 || xl > xmax) bp |= 8;                    // exclusive: x<xmin skips the xmax test
if (yh < 0 || (yh == 0 && yl < ymin)) bp |= 1;
else if (yh > 0 || yl > ymax) bp |= 2;
```
### 12EF:063E `on_border(DX:AX, CX:BX) -> ZF = 0 (BP = 1) iff hi words both 0 and (xl == xmin || xl == xmax || yl == ymin || yl == ymax)`  (V,T)
Any of the four borders, not only the one being crossed.

### 12EF:05D9 `flags32(BP = c0, DI = c1) -> AL = AH = side bits`, SI = rec  (V,T)
`rec+6 = (c0&1) ? ymin : (c0&2) ? ymax : rec+4 (lo y0);  rec+0E = same with c1 / rec+0C;` side bits as flags16. (Here ymin wins.)

### 12EF:04EF `bisect_in(SI = P0, DI = P1) -> CF, DX:AX, CX:BX`  (V,T); uses 85A8 = c0, 85AA = c1, counter 85B0
```
[85B0] = 32;  a = *DI;
for (;;) {
    if (--[85B0] < 0) return CF=1;                         // 32 midpoints at most
    m.x = (int32)(a.x + SI->x) >> 1;  m.y = (int32)(a.y + SI->y) >> 1;   // 32-bit add WRAPS, then sar (floor)
    bp = outcode32(m);
    if (bp == 0) return CF=0, m;
    if ([85AA] & bp) { if ([85A8] & bp) return CF=1;       // midpoint outside "both corners": line misses the window
                       *DI = m; a = m; }
    else             { *SI = m; a = *DI; }
}
```
P0/P1 slots are updated in place. 85A8/85AA are the ORIGINAL end codes (never updated). BP on exit = last midpoint's code.

### 12EF:056B `bisect_out(SI = inside point, DI = outside point) -> DX:AX = x, CX:BX = y`  (V,T); labels 0553, 055F
```
[85B0] = 32;  a = *DI;
for (;;) {
    if (--[85B0] < 0) return *SI;                          // last inside point (may be off the border)
    m.x = half(a.x + SI->x);  m.y = half(a.y + SI->y);     // 32-bit wrapping sums
    if (outcode32(m)) { *DI = m; a = m; continue; }
    if (on_border(m)) return m;
    *SI = m; a = *DI;
}
half(s):  if (lo16(s) == hi16(s))  return (lo16(s) == FFFFh) ? 0 : s;     // QUIRK 058D/0553: s == lo|lo<<16
          else return (int32)s >> 1;                                       // floor
```
The special case meant "-1/2 -> 0"; it also returns s **unhalved** for 00010001h, 00020002h, ..., FFFEFFFEh (sum 0 is unaffected).
Hit 288 times in the tests with huge inputs; port it as written. Callers store only lo16(x), lo16(y) (inside => hi = 0).

### 12EF:003E `void far poly_add_edge(SI = rec)`  (V,T)
```
f = rec+2;  [85B2] |= f;                                    // whole word
if (f & 40h) note_zclip_point(rec);                         // 00E0, also for rejected edges
if (f & 80h) {                                              // rejected: only end0's side bits are looked at
    if (f & 3) ((f & 2) ? note_left : note_right)(rec+6, rec+0E);
    return; }
[85BC] |= 4;  edge_dda(rec);                                // 1359:0719
f = rec+2 (re-read, low byte);
if (f & 3)  ((f & 2) ? note_left : note_right)(rec+4, rec+6);    // clipped y0 .. clamped original y0
if (f & 0Ch)((f & 8) ? note_left : note_right)(rec+0C, rec+0E);  // clipped y1 .. clamped original y1
```
For every flag value 0333 produces, a side bit of a rejected edge is always present in end0's bits too (85h/8Ah, or 80h|flags32
with a common x side), so a side flag in 85B2 always comes with a noted y range (V by enumeration of the paths).
### 12EF:00AA `note_left(BX, CX)` / 12EF:00C5 `note_right(BX, CX)`  (V,T)
`lo = min(BX,CX), hi = max(BX,CX)` (signed); `if (hi >= [85B6]) [85B6] = hi; if (lo < [85B4]) [85B4] = lo;` (right: 85BA/85B8).
### 12EF:00E0 `note_zclip_point(SI = rec)`  (V,T)
`dst = ([85BE] == 0) ? 8588 : 8590;  [85BE]++;  copy rec+10h..17h (x s32, y s32) to dst.` A third/fourth point overwrites
8590 and makes 85BE = 3/4, which disables the closing edge (quirk, faithful).
### 12EF:010A `void far poly_finish(int16 colour)`  (V,T)
```
AX = colour; lcall 1359:0040;                 // set colour (pushed copy unused); ES = page, AX clobbered
if ([85BE] == 2) { clip_edge(8588); poly_add_edge(8588); }   // 8588..8597 = (xA,yA,xB,yB) as an edge record
if ([85BC]) {
    if ([85B2] & 5)  wall_right();            // 0185: wall_fill([85BA], [85B8], xmax+1)
    if ([85B2] & 0Ah) wall_left();            // 019B: wall_fill([85B6], [85B4], xmin-1)
    lcall 1359:0052;                          // fill (mode M 0AF7)
} else if (([85B2] & 5) && ([85B2] & 0Ah)) {  // nothing rasterised but the polygon spans past both sides
    wall_right(); wall_left(); lcall 1359:0052;
}                                             // else: nothing drawn, tables untouched
[85B8] = [85B4] = 7FFFh;  [85BA] = [85B6] = 8000h;  [85BC] = [85B2] = [85BE] = 0;
```
Wall semantics with fill_M: a row that has only the right wall gets L = R = xmax+1 > xmax -> skipped; only the left wall:
R = xmin-1 < xmin -> skipped; both walls -> full row [xmin,xmax]; wall + edge -> span from the edge to the border. (T)
If a wall flag were set with the range still at its reset value, wall_fill would get (8000h, 7FFFh) = the whole window height;
unreachable per 003E above.
### 12EF:0185 `wall_right` / 12EF:019B `wall_left`  (V)
Push order `x, lo, hi` -> `1359:07D6(hi, lo, x)` with x = [85CE]+1 / [85CA]-1 (16-bit).
### 12EF:0002 `void far clip_add_edge(int16 x0, y0, x1, y1)`  (V) no callers
Writes sign-extended s32 values to DS:5EBA (+0,+4,+8,+0C), `0333`, `003E`. Saves SI.
### 12EF:01B1 `void far draw_line(int16 x0, y0, x1, y1, colour)`  (V) no callers
`AX = colour; 1359:0040`; record at 5EBA as above; `0333`; `01F9`. ### 12EF:01F9 `line_emit(SI)` (V): far;
`if (!(rec+2 & 80h)) { push es; lcall 1359:0049; pop es; }`. (0208..020A: padding.)

## Segment 1359

Dispatch tables at CS:0064 (6 near words each: init, pixel, colour, line, fill, fill2), selected by DS:85C2 (image value
0064h = E; set to 0070h by 010A):

| mode | table | init | pixel | colour | line | fill | fill2 |
|---|---|---|---|---|---|---|---|
| E | 0064 | 00E7 | 023A | 01B1 | 0369 | 0874 | 0834 |
| M | 0070 | 010A | 027B | 01D4 | 045D | 0AF7 | 0AF7 |
| C | 007C | 0126 | 02B6 | 01F8 | 0505 | 0BB3 | 0BB3 |
| T | 0088 | 0149 | 0301 | 021D | 0607 | 0CA1 | 0CA1 |

### 1359:0000 `void far gfx_mode_init(int16 mode_char)`  (V)
`ax = arg & 0FFh; if (al >= 'a') al -= 20h; [85C4] = ax;` then 'E' -> 00E7, 'M' -> 010A, 'C' -> 0126, 'T' -> 0149, else nothing.
Called once by `0000:0010` with 'M'.
### 1359:010A `rows_M` (near)  (V,T)
`for (i = 0; i < 200; i++) DS:85EC[i] = i*320;  [85C2] = 0070h.` Entries 200..479 keep the image value A000h (never used:
all rows are clipped to <= ymax).
### 1359:0037 / 0040 / 0049 / 0052 / 005B dispatch stubs (far)  (V)
`bx = [85C2]; call cs:[bx+2/4/6/8/0Ah]; retf`. Register interface passes through (AX colour for 0040, SI record for 0049).
BX clobbered. 0037 (pixel) and 005B (fill2) have no callers; 0037 is also broken by design (the pixel routines are far/cdecl).
### 1359:01D4 `set_colour_M(AX = colour word)` (near, gap)  (V,T)
```
[85D6] = ax;
if (ah == FFh && al >= A0h) {             // shaded ramp colour (unsigned compare)
    base = al & F0h;
    c = (uint8)(al - [4894]);             // byte subtract, wraps
    if (c <= base) c = base;              // unsigned; QUIRK: a wrap (al-[4894] < 0) gives a large c > base, kept
    byte [85D6] = c; }
es = ax = [85EA];                         // AX returns the page segment
```
DS:4894 = shade offset byte, written by `0000:DF08` from the queued object (`[obj+4]`), also by 119C, E7AC.
### 1359:045D `line_M(SI = rec)` (near, gap)  (V,T)
Uses rec+0 x0, +4 y0, +8 x1, +0C y1 (s16, already clipped; no clipping here), ES = [85EA], colour c = byte [85D6].
```
ady = |(int16)(y1-y0)|, adx = |(int16)(x1-x0)|   (16-bit)
if (ady <= adx) {                                 // x-major (unsigned compare; adx == ady is x-major)
    if (x1 < x0) swap ends;                       // now x0 <= x1
    ystep = (y1 < y0) ? -320 : 320;
    o = rows[y0] + x0;  n = x1-x0+1;  err = -(n >> 1);
    do { page[o++] = c; err += ady; if (err >= 0) { o += ystep; err -= adx; } } while (--n);
} else {                                          // y-major
    if (y1 < y0) swap ends;                       // now y0 <= y1
    xstep = (x1 < x0) ? -1 : 1;
    o = rows[y0] + x0;  n = y1-y0+1;  err = -(n >> 1);
    do { page[o] = c; o += 320; err += adx; if (err >= 0) { o += xstep; err -= ady; } } while (--n);
}
```
Both endpoints drawn; `err >= 0` tested as the sign of the 16-bit sum (`jns`), so ties step; the error starts at
-(count>>1) with count = major length + 1. Single point = 1 pixel. Saves SI, DI; clobbers AX,BX,CX,DX,BP.
### 1359:0719 `void far edge_dda(SI = rec)`  (V,T)
```
xa = rec+0, ya = rec+4, xb = rec+8, yb = rec+0C (s16);
if (xa > xb) swap (xa,ya) <-> (xb,yb);           // signed; equal x: no swap
row = ya;  dx = xb - xa;  step = +1;
lo = ya, hi = yb;  if (yb < ya) { step = -1; lo = yb; hi = ya; }
if (lo < ymin || lo > ymax || hi < ymin || hi > ymax) return;   // whole edge must be inside vertically; 9130 untouched
if (lo < [9130]) [9130] = lo;
dy = hi - lo;  x = xa;
if (dy <= dx) {                                   // x-major (unsigned compare)
    n = dx + 1;  err = -(n >> 1);
    L[row] = min(L[row], x);
    for (;;) {
        err += dy;
        if (err >= 0) {                           // sign of the 16-bit sum
            R[row] = max(R[row], x);  err -= dx;
            if (--n == 0) return;
            x++;  row += step;  L[row] = min(L[row], x);
        } else {
            x++;
            if (--n == 0) { x--; R[row] = max(R[row], x); return; }
        }
    }
} else {                                          // y-major
    n = dy + 1;  err = -(n >> 1);
    for (;;) {
        L[row] = min(L[row], x);  R[row] = max(R[row], x);
        err += dx;
        if (err >= 0) { if (--n == 0) return;  err -= dy;  x++;  row += step; }
        else          { row += step;  if (--n == 0) return; }
    }
}
```
min/max are signed (`jge`/`jle`). L = DS:89AC+2*row, R = DS:8D6E+2*row (16-bit index arithmetic). Pixel rule: x always runs
from the left end to the right end; in x-major mode each row gets the first x of its run in L and the last x in R; in y-major
mode each row gets its single x in both. Horizontal edge (dy = 0, dx > 0): one row, L = min x, R = max x. Vertical edge: every
row x. Single point (dx = dy = 0): err = 0 -> step branch -> L = R = x. Diagonal (dx = dy) is x-major: one pixel per row.
Saves SI, DI; clobbers AX,BX,CX,DX,BP.
### 1359:07D6 `void far wall_fill(int16 a, int16 b, int16 x)`  (V,T)
```
lo = a, hi = b; if (hi < lo) swap;                // signed
if (hi <= ymin || lo >= ymax) return;             // QUIRK: a range touching only ymin or only ymax is dropped
if (lo < ymin) lo = ymin;  if (hi > ymax) hi = ymax;
if (hi == lo) return;                             // QUIRK: single-row ranges are dropped (jcxz)
if (lo < [9130]) [9130] = lo;
for (row = lo; row <= hi; row++) { L[row] = min(L[row], x); R[row] = max(R[row], x); }
```
Called with (85BA/85B6 = max, 85B8/85B4 = min, x) by 0185/019B.
### 1359:0AF7 `fill_M()` (near, gap; AX ignored, uses [85D6], ES = page from 01D4)  (V,T)
```
lo = byte [85D6], hi = byte [85D7];
hi <  FBh : dither(lo, hi)            (0B29)
hi == FFh : dither(lo, lo) = solid    (ah = al)
hi == FEh : AND  (09F7)   hi == FDh : OR (0A77)   hi == FCh : stipple (098D)   hi == FBh : none (0959)
row loop shared by all (r = [9130]):
    if (r == 7FFFh) { [9130] = 7FFFh; return; }      // stipple: if ((uint16)r >= 7FFFh)
    for (;; r++) {
        if (L[r] == 7FFFh) { [9130] = 7FFFh; return; }   // first untouched row ends the polygon
        if (!(L[r] > xmax || R[r] < xmin)) {               // signed
            xl = max(L[r], xmin); xr = min(R[r], xmax);
            span(r, xl, xr);  }
        L[r] = 7FFFh; R[r] = 8001h;
    }
span, o = rows[r] + x (= r*320 + x):
    dither : page[o] = ((x ^ r) & 1) ? hi : lo           for x in [xl,xr]   ([9140] = lo | hi<<8)
    AND    : page[o] &= lo;   OR : page[o] |= lo           for x in [xl,xr]
    stipple: page[o] = lo  only where ((x ^ r) & 1) == 1   (checkerboard, other pixels unchanged)
    none   : no pixels (0959: if [9130] == 7FFFh return; [9130] = 7FFFh; then reset rows while L != 7FFFh, no window test)
```
The dither is a 1-pixel checkerboard (lo on even x+y, hi on odd x+y), written with byte/word stores (alignment does not change
the result). Rows are filled top to bottom, each span left to right. QUIRK: a row with L == 7FFFh inside the polygon's y range
would end the fill and leave the later rows set for the next polygon; closed polygons + walls do not produce such gaps except
through the wall_fill drops above (U: not seen in tests). Assumes xl <= xr for touched rows (true: every touched row gets L and R).

### Index-level entries (dead in this game: modes E/C/T, or no callers)  (V)
* `1359:0094` alloc (dead, no callers): INT 21h AH=48h BX=FFFFh (query), then returns 0 if `max-400h > request` (inverted test)
  else allocates `request` paragraphs and returns AX.
* `1359:00E7 rows_E` (200 rows x 28h; if [913E] != 0 jumps to label `00B6` = 480 rows x 50h, [9142] = 4Fh, [9144] = -51h;
  913E is never written), `0126 rows_C` (CGA interleave 0/2000h + 50h*k), `0149 rows_T` (Tandy 4 banks x 2000h, A0h per
  group). Each sets 85C2 to its table.
* `1359:0174` (label, no callers): if [85C4] == 'E' reset EGA sequencer/GC registers (3C4h map mask 0F, 3CEh regs 1,8,5,3,4).
* `1359:019B set_page(int16 page)` (far, no callers): `[85C6] = page; [85EA] = DS:85E2[page]`.
* `1359:01B1 / 01F8 / 021D` colour E/C/T: E `85D6 = al`, GC set/reset from DS:9168 table (zero in the image); C/T replicate
  2/4-bit patterns into 914A/914C/914E.
* `1359:023A / 027B / 02B6 / 0301` far `pixel(x, y, c)` E/M/C/T, window-clipped. 027B (M): BUG reads the row offset from
  DS:[2y] instead of DS:85EC[y] (also 02B6, 0301); dead. `0344` EGA address/bit-mask helper (DS:9150 masks 80h..01h).
* Gap labels: `0369` line_E, `0505` line_C, `0607` line_T, `0834`/`0874` fill_E variants (sequencer 3C4h), `0BB3` fill_C,
  `0CA1` fill_T: all only reachable through the E/C/T tables.

## Open items
* (U) Whether the original ever reaches the fill_M "gap row" quirk (a wall dropped by the single-row/border rule leaving an
  untouched row between touched rows). Would show as a polygon cut short and a stale span in the next polygon. Port faithfully.
* Who writes the window (85CA..85D0), 85EA and the page table 85E2: `0000:DB71`, `0000:02C2` (other readers). The near-ret
  impossibility proof assumes their values stay in 0..7FFFh.
* 11EB:0B3F: sets flag 40h by `or [si+2],4040h` after 0333 and writes rec+2 directly when both ends are behind the camera
  (other reader; not re-verified here beyond the grep).

## DGROUP globals seen

| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 4894 | 1 | r3d_shade_ofs | ramp shade subtracted by 01D4 | W DF08/119C/E7AC, R 1359:01D4 |
| 5EBA | 24*n | r3d_edges | edge records (format above) | 11EB, 12EF:0002/01B1 |
| 8568 | 16 | clip_seg_a | s32 P0,P1 copy for bisection | 12EF:0333 |
| 8578 | 16 | clip_seg_b | second copy (original ends) | 12EF:0333 |
| 8588 | 8 | zclip_pt0 | first near-plane point (x s32,y s32) | 00E0 W, 010A R |
| 8590 | 8 | zclip_pt1 | second near-plane point | 00E0 W, 010A R |
| 85A8 | 2 | clip_c0 | out-code end0 (bisect_in) | 0333 W, 04EF R |
| 85AA | 2 | clip_c1 | out-code end1 | 0333 W, 04EF R |
| 85AC | 2 | clip_dx | x1-x0 (fast path) | 0333 W, 0265 R |
| 85AE | 2 | clip_dy | y1-y0 | 0333 W, 0265 R |
| 85B0 | 2 | clip_iter | bisection counter 32..-1 | 04EF, 056B |
| 85B2 | 2 | poly_flags | OR of edge flag words | 003E W, 010A R/reset 0 |
| 85B4,85B6 | 2+2 | wall_l_lo/hi | left wall y range (reset 7FFFh/8000h) | 00AA, 010A, 019B |
| 85B8,85BA | 2+2 | wall_r_lo/hi | right wall y range (reset 7FFFh/8000h) | 00C5, 010A, 0185 |
| 85BC | 2 | poly_drawn | 4 once an edge was rasterised | 003E, 010A |
| 85BE | 2 | zclip_n | near-plane points saved | 00E0, 010A |
| 85C0 | 1 | clip_ovf | 16-bit compare overflow seen | 02D1 W, 0333 R |
| 85C1 | 1 | clip_c1b | out-code end1 for the 2nd intercept | 0333 |
| 85C2 | 2 | gfx_table | CS offset of the dispatch table (image 0064h, M 0070h) | 1359 init W, stubs R |
| 85C4 | 2 | gfx_mode | upper-cased mode char (image FFFFh) | 1359:0000 W, 0174 R |
| 85C6 | 2 | gfx_page_no | page index | 019B |
| 85CA..85D0 | 4x2 | win_xmin,ymin,xmax,ymax | clip window (image 0,0,13Fh,C7h) | DB71 W; 12EF, 1359 R |
| 85D6 | 2 | gfx_colour | colour word: lo colour, hi fill type | 01D4 W, 045D/0AF7 R |
| 85E2 | 2*n | gfx_pages | page segments | 02C2 W, 019B/DB71 R |
| 85EA | 2 | gfx_seg | current draw segment | DB71/019B W, 01D4/045D R |
| 85EC | 480x2 | gfx_rows | row offsets (M: y*320, 200 used) | 1359 init W, 045D/0AF7 R |
| 89AC | 480x2 | edge_L | min x per row, idle 7FFFh | 0719/07D6 W, fill R/reset |
| 8D6E | 480x2 | edge_R | max x per row, idle 8001h | same |
| 9130 | 2 | edge_miny | first touched row, idle 7FFFh | 0719/07D6 W, fill R/reset |
| 9132,9134 | 2+2 | ega_px_x/y | pixel scratch (E) | 023A, 0344 |
| 913E | 2 | ega_480 | E alternate 480-line switch (always 0) | 00E7 |
| 9140 | 2 | fill_dither | lo/hi dither colours | 0B29 |
| 9142,9144 | 2+2 | ega_rowbytes | E 480-line constants | 00B6 |
| 914A..914E | 6 | cga_pat | C/T colour patterns | 01F8/021D |
| 9150 | 8 | ega_bitmask | 80h,40h,..,01h | 0344 |
| 9158 | 8 | ega_lmask | FFh,7Fh,..,01h (E fills) | 0834/0874 (U) |
| 9168 | 16 | ega_coltab | colour plane table (zero) | 01B1/01F8/02B6 |

## Structures seen
* Edge record (24 bytes), see the table at the top. The closing edge reuses 8588..8597 as a record (+10h.. not used: 0333 never
  sets 40h).
* Dispatch table: 6 near code offsets in CS 1359 (table above).


---------------------------------------------------------------------------------------------------------------------

# Part 6: sky/ground backdrop (1442), 2D edge tables (1001), Q15 math library (1433) (R7c)

Sources: `work/s37/R7c_dis1001.txt`, `R7c_dis1433.txt`, `R7c_dis1442.txt` (dsann), raw `x86dis` for the index gaps.
Python reference `port/notes/s37/R7c_ref.py`, Unicorn harness `port/notes/s37/R7c_emu.py` (`python port/notes/s37/R7c_emu.py N [math|1001|fill|1442]`),
table fits `R7c_tables.py`, `R7c_fit2.py`. Results in `R7c_test_*.txt`.

## 0. Overview

* **1001** is a small 2D raster front end that works on the two DGROUP edge tables `L = DS:22A1`, `R = DS:2459`
  (220 words each, 0x1B8 bytes apart, row y at `+2y`) and the dirty row range `[2615]` (ymin, 0xFFFF = empty) /
  `[2617]` (ymax). An edge is passed in the 4-word block `[2299]=x1 [229D]=y1 [229B]=x2 [229F]=y2` and
  **clipped** to the window `0..[2293]` x `0..[2295]` (inclusive maxima, set by the caller: 0x13F/0xC7 full screen,
  `db71 r3d_set_viewport`, `87a9`, `17a3`, ...). Two clippers share one Cohen-Sutherland core:
  `0336` draws the clipped segment with MGRAPHIC slot 31 `line` (HUD lines, horizon line), `06AC` scan-converts it
  into L/R (`05B9` Bresenham walk) and turns the parts outside the left/right border into border columns (`055B`), so
  a polygon that leaves the screen still produces closed spans. The tables are emptied by `0525` and consumed by
  slot 37 `poly_fill` / slot 40 `poly_fill_solid` (see `port/spec/video.md` section 5 and the slot table; not repeated).
* **0000:1442** draws the sky and the ground of a 3D view (called by `dae8 r3d_begin_scene` when its draw-backdrop flag
  is set). Detail `[98DE] < 3`: one two-colour split (`1001:0006` -> `0018`). Otherwise 16 sky bands + 16 ground bands,
  each a 4-point polygon through `1001:01EC`.
* **1433** is the Q15 fixed-point library (sin/cos/atan2/asin/hypot, 3x3 matrices, Euler matrices). Angles are
  uint16, 0x10000 = 360 deg. Tables in segment 14B2 (image 0x14B20). Callers are all render3d (11EB object renderer,
  `dc64`, `df08`, `e7ac`, `0ffb`, `13e3`); there are **no flight-model callers**, and 9 of the 21 routines have no
  caller at all in VGAME (dead library code, listed but kept short).
* Image range note: `14A2:000E`/`14A2:00C5` (image 0x14A2E..0x14B09) lie inside 1433..14B2 but are the video
  palette routines `pal_write_vsync`/`pal_read_dac` (owned by platform/video specs), not covered here.

Call order of one frame's backdrop: `dae8` -> `1442` -> {`1001:0006` -> `0018` -> (`0525`, `0336`, `06AC`/`05B9`, `055B`, slot 37)}
or {32 x `1001:01EC` -> (`0525`, n x `06AC`, slot 37)}.

### Notation (used everywhere below)
`s16/u16/s32` = C `int16_t/uint16_t/int32_t`; all 16-bit arithmetic wraps. `>>` on signed values is arithmetic (sar = floor).
`/` and `%` on signed values are C99 truncating (`idiv`). `q15(a,b) = (int16)(((int32)a*b) >> 15)` (floor; the code is
`imul; shl ax,1; rcl dx,1` and takes DX). `mul15r(a,b) = (int16)(((int32)a*b + 0x4000) >> 15)` = `109D:0006`
(simflow Part 2 section 1). `hi(p) = (int16)(p >> 16)`.
Tags: (V) verified in code, (T) differential-tested against the real code under Unicorn, (U) unclear.

### Differential tests (all 0 mismatches)

| routine | cases | notes |
|---|---|---|
| 1433:0004 sin, 0031 cos, 015E asin | 65536 each (exhaustive) | also `cos(a)==sin(a+0x4000)` for all a |
| 1433:00D9, 011A, 0061 atan2, 018E hypot2, 01E0, 0542, 05C6 | 20000 each | random + edge words (0, +-1, 7FFF, 8000, 4000, C000 ...) |
| 1433:0554 muldiv / 058F div32sat | 20000 / 19375 | cases where the real code raises #DE skipped (0 / 625) |
| 1433:0219, 032E, 0286 (C=new, C=A, C=B), 02E2 (dst new and in place), 0314, 03A9 (+ sin/cos cache), 05D1, 04D7 (random M and M=03A9(a,b,c)) | 20000 each | |
| 1001:032A -> 0336 | 20000 | 6 windows incl. 1x1 and 0x20x0x1C, coords up to +-32768, 298 INT 0 overflows hit |
| 1001:06A0 -> 06AC | 20000 | same, 321 INT 0 overflows; compares L/R tables, 2615/2617, all scratch words |
| 1001:05B9 walk | 20000 | in-window end points, random prior table content |
| 1001:01EC poly_fill | 5000 | 3..6 vertices, tables snapshotted at the slot 37 call; 369 INT 0 |
| 1001:0006 -> 0018 hz_draw | 5000 | random pitch/roll, zoom 0..2, lens 0/1; slot 31/32/37 sequence + table snapshots |
| 1001:024C fill_rows, 051F tables_clear | 3000 each | |
| 0000:1442 | 10000 | 1001:01EC / 0006 hooked: sequence of (colour, 4 points) compared; DS 98C8/98E4/98F8/9F12, C080..C09F |

INT 0 is emulated as on a 286+ (the saved IP is the faulting `idiv`, which both handlers rely on).

---------------------------------------------------------------------------------------------------------------------

## Part A. Segment 1001 (image 0x10010..0x10885)

### A.0 Data used by all of 1001 (DGROUP)
* `[2293]` XMAX, `[2295]` YMAX: inclusive clip maxima, compared **signed**. YMAX must be <= 219 (table size).
* `[2299] x1, [229B] x2, [229D] y1, [229F] y2`: the edge/segment (in), the clipped segment (out, see A.8).
* 0336 scratch: `[228A]` byte outcode of point 1, `[228B]` Dx, `[228D]` Dy, `[228F]` Dx>>1, `[2291]` Dy>>1.
* 06AC scratch: `[261A]` byte outcode, `[261B]` Dx, `[261D]` Dy, `[261F]` Dx>>1, `[2621]` Dy>>1.
* 05B9 scratch: `[2611]` |dx|, `[2613]` |dy|.
* `L[y]` = word `DS:22A1+2y` (min x of row y; empty = 0xFFFF), `R[y]` = word `DS:2459+2y` (max x; empty = 0).
  Compared **unsigned** (jb/ja). `[2615]` ymin / `[2617]` ymax of the rows touched (unsigned compares; 0xFFFF/0 = empty).
* hz_draw: `[0D42]` corner x, `[0D44]` = -sin(roll), `[0D46]` byte colour of the first side, `[0D48]` side toggle 0/0xFFFF.
  Reads `[4986]=sinB [4988]=cosB [498A]=sinC [498C]=cosC` (pitch/roll, written by `e26b`/`e3e2`), `[433A]` byte zoom
  shift, `[294B]` byte lens flag, `[0D1E]`/`[0D20]` screen centre (w2, h2; from `db71`).
* Every routine expects DS = DGROUP 1E23; ES is set to DS (or 0 briefly for the INT 0 vector).

### A.1 `1001:0006 hz_fill_far` - `void far hz_fill_far(int colour)` (V,T)
`[0D46] = (uint8)colour; push si,di; hz_draw(); pop di,si; retf`. Only caller `1442` (detail < 3) with `ctx->[+4]`
(sky colour, 0x88 for the main view).

### A.2 `1001:0018 hz_draw` - `void near hz_draw(void)` (V,T) - two-colour sky/ground split
No arguments; clobbers all registers including BP.
```c
uint16 nsC = -[498A];  [0D44] = nsC;
int16 sB=[4986], cB=[4988], cC=[498C], w2=[0D1E], h2=[0D20];
int16 H = (cB > 0x1F0B) ? (int16)(((int32)sB << 8) / cB)  /* idiv, trunc; cannot overflow */ : 0x3FF;   // signed jg
if ((uint8)[433A]) H <<= (uint8)[433A];          // 16-bit shl (count & 31; >=16 gives 0)
if ((uint8)[294B]) H >>= 1;                      // sar
// 32-bit products, all wrap mod 2^32; RND(v) = (int16)((uint32)v >> 16) + bit15(v)  (= hi word rounded)
int32 P1 = ((int32)H * (int16)nsC) << 1,  P2 = ((int32)(int16)(w2 << 1) * cC) << 1;
[2299] = w2 + RND(P2 - P1);                      // x1
[229B] = w2 - RND(P1 + P2);                      // x2
int32 Q1 = ((int32)H * cC) << 1,          Q2 = ((int32)(int16)(w2 << 1) * (int16)nsC) << 1;
int16 r = RND(Q2 - Q1);  [229F] = h2 + (r - (r >> 2));          // y2
int16 s = RND(Q1 + Q2);  [229D] = h2 - (s - (s >> 2));          // y1
[0D48] = 0;
color_set_r([0D46]);   tables_clear();                          // slot 32, 0525
if (line_clip_draw() == REJECT                                  // 0336: also DRAWS the clipped line (slot 31) in [0D46]
    || ([229D] == [229F] && ([229D] == 0 || [229D] == [2295]))) {   // clipped line lies on the top/bottom border
    if ((int16)[4986] >= 0) color_set_r(0x78);                  // sin B >= 0: ground colour, else keep [0D46]
    edge_column(0, 0, [2295]);  edge_column([2293], 0, [2295]);   // whole window
    poly_fill([2615], 0x22A1, [2617], 0);  return;               // slot 37 mode 0
}
for (;;) {                                                       // 2 passes over the clipped line
    save x1,y1,x2,y2;  edge_to_tables();  restore x1,y1,x2,y2;   // 06AC rasterises the (already clipped) line
    [0D42] = (([0D48] ^ [0D44]) & 0x8000) ? [2293] : 0;          // corner column x of this side
    int16 bx = 0, bp = [2295];  if (([0D48] ^ [498C]) & 0x8000) swap(bx, bp);   // bx = edge row of this side
    for (end point (si,di) in {([2299],[229D]), ([229B],[229F])}) {
        if (di == bx) continue;
        if (di == bp) si = [0D42];                               // end point on the opposite border: corner column
        edge_column(si, di, bx);                                 // only does something if si == 0 or si == XMAX
    }
    poly_fill([2615], 0x22A1, [2617], 0);
    if (([0D48] ^= 0xFFFF) == 0) return;
    color_set_r(0x78);  tables_clear();
}
```
Geometry (for understanding only): the line is centred at `(w2 + H*sinC, h2 - 0.75*H*cosC)` with half-length vector
`(2*w2*cosC, -0.75*2*w2*sinC)` (Q15 sin/cos), so it is the horizon at screen offset H = 256*tan(pitch) (zoom, lens)
rotated by the roll; vertical components are scaled by 3/4 (`v - (v>>2)`). Pass 1 fills the side towards row 0 (row
YMAX when cos C < 0) in `[0D46]`, pass 2 the other side in 0x78. The slot 31 line is fully overdrawn by the fills.
Note the difference to 1442: here `H = 0x3FF` when `cosB <= 0x1F0B` (even for looking down), 1442 clamps cosB to 0x200.
Callers: `0006` only. Callees: slots 31 (via 0336), 32, 37; 0525, 0336, 06AC, 055B.

### A.3 `1001:01EC poly_fill_far` - `void far poly_fill_far(Viewport *vp, int n, int16 *pts, int colour)` (V,T)
cdecl `[bp+6]=vp` (unused), `[bp+8]=n`, `[bp+0A]=pts` (n pairs x,y), `[bp+0C]=colour` (low byte).
```c
color_set_r(colour); tables_clear();
[2299]=pts[0].x; [229D]=pts[0].y;
for (k = 1; k <= (uint16)(n-1); k++) {          // `loop`: n==1 would run 65536 times (never happens)
    [229B]=pts[k].x; [229F]=pts[k].y; edge_to_tables();
    [2299]=pts[k].x; [229D]=pts[k].y;           // restored from the stack (06AC clips the block in place)
}
[229B]=pts[0].x; [229F]=pts[0].y; edge_to_tables();       // closing edge
poly_fill([2615], 0x22A1, [2617], 0);                      // slot 37, mode 0; returns at once if no row (ymin=0xFFFF)
```
Saves ES, DS, SI, DI, BP. Callers: 1442, 17a3 cockpit_draw_adi, 1961, 19ea, 2f82 view_frame.

### A.4 `1001:024C fill_rows_far` - `void far fill_rows_far(Viewport *vp, int xl, int ymin, int xr, int ymax)` (V,T)
```c
old = seg_get();                         // slot 16
page_select_r(vp->page /* word +0 */);   // slot 13
color_set_r(byte vp[+4]);                // slot 32
tables_clear();
[2617] = ymax; [2615] = ymin;
fill words L[ymin..ymax] = xl; R[ymin..ymax] = xr;     // count (uint16)(ymax-ymin+1), rep stosw, ES=DS
poly_fill_solid([2615], 0x22A1, [2617]);              // slot 40 (DX=0 too)
seg_set(old);                                         // slot 15
```
No clipping: the caller guarantees `0 <= ymin <= ymax <= 219`. Callers: 00f0 sim_init, 17a3, 8316, 893f gfx_fill_rect,
8a5e, 8c10, 950e, a17e, bf71 (14 call sites).

### A.5 `1001:02BA fill_rows2_near` (dead)
Identical to 024C but colour `vp[+6]`, no `DX=0` before slot 40, and ends with a **near** `ret` although it builds a far
frame. No callers (raw scan for `call`/`lcall` finds none). Port: omit.

### A.6 `1001:0326` / `1001:032A` far wrappers of 0336 (V)
`0326`: `call 0336; retf` (no register save; caller 10a8:000c hud_draw_front, asm). `032A`: push BP,SI,DI,ES;
`call 0336`; pop; `retf` (callers 02fe, 873f, 87a9). The CF result of 0336 is returned in the flags only.

### A.7 `1001:04C2 clip_outcode` - register `BX=x, BP=y -> AL, ZF` (V,T)
```c
uint8 outcode(int16 x, int16 y) { uint8 al = 0x0F;
  if (x >= 0) al &= ~8;  if (x <= XMAX) al &= ~1;  if (y >= 0) al &= ~4;  if (y <= YMAX) al &= ~2;  return al; }
```
Bits: 8 = x<0, 1 = x>XMAX, 4 = y<0, 2 = y>YMAX (signed compares). ZF=1 when inside.

### A.8 Clip core shared by `0336 line_clip_draw` and `06AC edge_to_tables` (V,T)
Both first save the INT 0 vector (`0:0000`, `0:0002`) on the stack and install `1001:04E3` (0336) or `1001:0854`
(06AC); every exit restores it. Port: no vector handling, use `rdiv` below. Variables `V.*` = 228A.. (0336) or 261A.. (06AC).
```c
// rdiv: the idiv + rounding block (0336: 0432/046D, 06AC: 078F/07C7). num = (int32)(int16)a * (int16)b.
int16 rdiv(int32 num, int16 D, int16 h /* = D>>1 as stored */) {
  uint16 hw = (uint32)num >> 16;   int16 q, r;
  if (D == 0 || num / D < -32768 || num / D > 32767) {     // #DE -> handler 04E3 / 0854
      q = ((hw ^ D) & 0x8000) ? -0x7F00 : 0x7F00;  r = 0;   // the handler xors DX (=hw) with the divisor it re-reads
  } else { q = num / D; r = num % D; }                     // truncating
  if ((hw ^ D) & 0x8000) { r = -r; q -= 1; }               // quotient negative
  r = (int16)(r - h);
  if (((r ^ hw) & 0x8000) == 0) q += 1;
  return q;                                                // ~round(num/D); see quirks
}

int16 cx=[2299], dx=[229D], si=[229B], di=[229F];        // point1 = (cx,dx), point2 = (si,di)
V.oc = outcode(cx,dx);  uint8 al = outcode(si,di);
if (al == 0) {
    if (V.oc == 0) goto ACCEPT;                            // both inside: block unchanged
    swap(cx,si); swap(dx,di);  al = V.oc; V.oc = 0;   // clip the old point 1 instead
    [2299] = cx; [229D] = dx;                              // (new point 1 = old point 2)
}
int16 bp = dx;                                             // reference point (cx,bp)
if (V.oc & al) goto TRIVIAL_REJECT;
// deltas with overflow handling (jo -> cmc; rcr = halve the true 17-bit difference, floor)
int32 tdx = si - cx, tdy = di - bp;                        // true differences
if (tdx in int16) { Dx = tdx; hx = Dx >> 1;
     if (tdy in int16) Dy = tdy; else { Dy = floor(tdy/2); Dx >>= 1; hx >>= 1; } }   // sar on the stored words
else { Dx = floor(tdx/2); hx = Dx >> 1; Dy = floor(tdy/2); }                         // dy always halved here
hy = Dy >> 1;   store V.Dx, V.hx, V.Dy, V.hy;
for (;;) {                                                 // clip point (si,di) with outcode al
    int16 rx, ry;
    if (al & 9) {                                          // x out
        int16 tx = (si < 0) ? 0 : XMAX;
        int16 y = rdiv((int16)(tx - cx) * Dy, Dx, hx) + bp;
        if (y < 0)          { ty = 0; goto YCLIP; }
        if (y <= YMAX)      { rx = tx; ry = y; goto GOT; }
    }
    ty = (di > YMAX) ? YMAX : 0;                           // uses the y of the point being clipped
YCLIP: {
    int16 x = rdiv((int16)(ty - bp) * Dx, Dy, hy) + cx;
    if (x < 0 || x > XMAX) goto LATE_REJECT;              // (06AC passes x and the current si/di/bp along)
    rx = x; ry = ty; }
GOT:
    if (06AC) edge_column(rx, ry, di);                     // part beyond a side border -> border column
    if (V.oc == 0) { [229F] = ry; [229B] = rx; goto ACCEPT; }
    [229D] = ry; [2299] = rx;                              // clipped point becomes point 1 ...
    swap(si,cx); swap(di,bp); al = V.oc; V.oc = 0;         // ... and the other ORIGINAL point is clipped next,
}                                                          // with the same Dx,Dy (not recomputed, not negated)
```
Exits:
* **0336** ACCEPT: restore vector, `line(AX=[2299], BX=[229D], CX=[229B], DX=[229F])` (slot 31), `CF=0`.
  TRIVIAL_REJECT / LATE_REJECT: restore, `CF=1`, nothing drawn.
* **06AC** ACCEPT: restore, `jmp 05B9` (edge_walk of the block).
  TRIVIAL_REJECT: `al &= V.oc`; if `al & 6` (both above or both below) return; else `edge_column_x(bx=cx, ya=di, yb=bp)`
  (both left or both right: border column over the edge's y range).
  LATE_REJECT: `edge_column_x(bx=x, ya=di, yb=bp)` with x the out-of-window intercept.
Result order: when point 1 was outside, the clipped segment comes out with its end points **swapped** (old point 2
in `[2299]/[229D]`). This matters for slot 31 (pixel choice of the line) and is reproduced by the port.
Callers: 0336 via 0326/032A and 0018; 06AC via 06A0 (c6de mfd_view_flir_tint), 0018, 01EC.

Quirks (port them): (1) rdiv is not exact rounding: for |D| odd it rounds up when the remainder equals (|D|-1)/2, and
for `D == +1` the result is one too large in magnitude (`num=0, D=1` gives 1). (2) a divide overflow yields
`+-0x7F00` and then still passes through the rounding (`+-0x7F00 +-1` possible). (3) the 16-bit `tx - cx` / `ty - bp`
and the final `+ bp` / `+ cx` wrap. (4) when both end points are outside, the deltas of the original segment are reused.

### A.9 `1001:04E3` / `1001:0854` INT 0 handlers (V,T)
```
push bp; mov bp,sp; ax = ([bp+2] == 0x0474 /*0854: 0x07CE*/) ? [228D] /*[261D]*/ : [228B] /*[261B]*/;
dx ^= ax;  ax = (dx & 0x8000) ? 0x8100 : 0x7F00;  [bp+2] += 4 /* skip the 4-byte idiv m16 */;  dx = 0; pop bp; iret
```
i.e. the divisor actually used (0474/07CE = the y-clip idiv, else the x-clip one); relies on 286+ fault semantics.

### A.10 `1001:0504 poly_flush_far(int mode)` (V)
`poly_fill(AX=[2615], BX=0x22A1, CX=[2617], DX=mode)` (slot 37); saves SI, DI. Caller c6de (mode = the FLIR tint mode).

### A.11 `1001:051F tables_clear_far` / `1001:0525 tables_clear` (V,T)
```c
void tables_clear(void) { uint16 y0 = [2615]; if (y0 & 0x8000) return;    // empty (0xFFFF)
  uint16 n = [2617] + 1 - y0;  L[y0 .. y0+n-1] = 0xFFFF; [2615] = 0xFFFF;  R[y0 ..] = 0; [2617] = 0; }
```
Only the dirty range is reset; rows outside it are assumed empty. The initial DGROUP image has all L = 0xFFFF,
R = 0, `[2615]=0xFFFF`, `[2617]=0` (V, dumped), so the port initialises them that way.
051F = `push di; call 0525; pop di; retf` (callers bf71, c6de).

### A.12 `1001:055B edge_column` - register `SI=x, DI=ya, DX=yb` (V,T); local entry `1001:05AA edge_column_x (BX=x)`
```c
void edge_column(uint16 x, int16 ya, int16 yb) {
  uint16 *tbl; if (x == 0) tbl = L; else if (x == [2293]) tbl = R; else return;   // interior x: nothing
  int16 hi = ya, lo = yb;  if (hi < lo) swap(hi, lo);      // signed
  if (lo < 0) lo = 0;  if (hi > YMAX) hi = YMAX;
  if ((uint16)hi > [2617]) [2617] = hi;  if ((uint16)lo < [2615]) [2615] = lo;
  tbl[lo .. hi] = x;                                        // count (uint16)(hi-lo+1), rep stosw
}
void edge_column_x(int16 x, ya, yb) { if (x < 0) edge_column(0,ya,yb); else if (x > XMAX) edge_column(XMAX,ya,yb); }
```
Note: the column **overwrites** L/R (no min/max). If both ya,yb were below 0 or above YMAX the count would go negative
and clobber memory; no caller can produce that (06AC always passes one in-range y; hz_draw passes window rows).

### A.13 `1001:05B9 edge_walk` (label, reached by `jmp` from 06AC; near ret) (V,T)
```c
if ([229B] < [2299]) swap the points (x and y);           // unsigned
uint16 DX = [229B] - [2299];  [2611] = DX;
int16 dy = [229F] - [229D]; uint16 yhi = [229F], ylo = [229D]; int step = +2 (bytes);
if (dy < 0) { swap(yhi, ylo); step = -2; dy = -dy; }
[2613] = dy;  if (yhi > [2617]) [2617] = yhi;  if (ylo < [2615]) [2615] = ylo;   // unsigned
uint16 o = 2*[229D], x = [2299];                            // start at the left end point
if ((uint16)dy < DX) {                                      // x-major
    int16 err = -((uint16)(DX + 1) >> 1), n = DX;
    if (x < L[o]) L[o] = x;
    for (;;) { if (--n < 0) { if (x > R[o]) R[o] = x; return; }
               err += dy;
               if (err >= 0) { if (x > R[o]) R[o] = x; x++; err -= DX; o += step; if (x < L[o]) L[o] = x; }
               else x++; }
} else {                                                    // y-major (includes dx == dy, and a single point)
    int16 err = -((uint16)(dy + 1) >> 1), n = dy;
    for (;;) { if (x < L[o]) L[o] = x;  if (x > R[o]) R[o] = x;
               if (--n < 0) return;
               o += step; err += DX; if (err < 0) continue; err -= dy; x++; }
}
```
L/R compares are unsigned. Rows touched: all rows between the end points (x-major: every x of the run is merged
into its row's L/R).

### A.14 `1001:06A0 edge_to_tables_far` (V) push ES,SI,DI,BP; `call 06AC`; pop; `retf`. Caller c6de (2 sites).

---------------------------------------------------------------------------------------------------------------------

## Part B. `0000:1442 r3d_draw_sky_ground` - `void near r3d_draw_sky_ground(Viewport *ctx)` (cdecl `[bp+4]`) (V,T)

Locals: `X1[17] (bp-26h), Y1[17] (bp-4Ah), X2[17] (bp-6Ch), Y2[17] (bp-8Eh), pts[8] (bp-9Eh), H (bp-2), flag (bp-28h)`.
```c
flag = 0;
[98C8] = -[498A]; [98E4] = [498C]; [98F8] = [4986]; [9F12] = [4988];   // nsC, cC, sB, cB cache (read by 17a3, bf71)
if ((int8)[98DE] < 3) { hz_fill_far(ctx->w4 /* word +4, low byte used */); return; }     // A.1, flat split
if ([4360] && [43B2]) flag = 1;                     // TV camera picture (bf71) in mono mode (F6 tv_opt)
for (i = 0; i < 16; i++) [C080+2i] = [0D22+2i];     // sky offsets, see table
int16 cB = (int16)[4988]; if (cB < 0x200) cB = 0x200;          // signed
int16 H = (int16)(((int32)(int16)[4986] << 8) / cB);           // MSC long divide (f392), trunc; |H| <= 16384
if ((uint8)[433A]) H <<= (uint8)[433A];             // 16-bit shl
if ((uint8)[294B]) H >>= 1;                          // sar
// one band boundary at perpendicular offset t (pixels, + = up when cos C > 0); A = (int16)0xA7B8 = -22600
void boundary(int i, int16 t) {
    int16 nsC = [98C8], cC = [498C], w2 = [0D1E], h2 = [0D20];
    int16 a = mul15r(cC, A), b = mul15r(nsC, t);
    X1[i] = w2 - b + a;          X2[i] = w2 - b - a;
    int16 c = mul15r(cC, t), d = mul15r(nsC, A);
    int16 s = d + c;  Y1[i] = (s >> 2) - d - c + h2;           // = h2 - (s - (s>>2))
    int16 u = c - d;  Y2[i] = (u >> 2) - c + h2 + d;           // = h2 - (u - (u>>2))
}
for (i = 0; i <= 16; i++) boundary(i, i < 16 ? (int16)([C080+2i] + H) : 0x5848);       // sky: H .. +22600
for (i = 0; i < 16; i++) poly_fill_far(ctx, 4, {X1[i],Y1[i], X2[i],Y2[i], X2[i+1],Y2[i+1], X1[i+1],Y1[i+1]},
                                       flag ? 0x88 : 0x80 + i);
int16 alt = [4980];                                  // camera origin z (dca9 r3d_set_origin / 0ffb)
[C080] = alt / 0x200;                                // idiv, trunc
for (i = 1; i < 16; i++) [C080+2i] = alt / ((16 - i) << 5) - [C080];   // divisors 480,448,..,32
[C080] = 0;
for (i = 0; i <= 16; i++) boundary(i, i < 16 ? (int16)(H - [C080+2i]) : (int16)0xA7B8);  // ground: H .. -22600
for (i = 0; i < 16; i++) poly_fill_far(ctx, 4, {same 4 points}, flag ? 0x7C : 0x70 + i);
```
* Sky offset table `DS:0D22` (17 words, initialised data, never written): `0,2,3,4,6,8,11,15,20,27,36,48,64,87,119,164,0`
  (only the first 16 are copied). Band i of the sky spans offsets `H+D22[i] .. H+D22[i+1]` (band 15 up to 22600).
* Ground offsets `G[i] = alt/(32*(16-i)) - alt/512` (i=1..15), `G[0]=0`; band i spans `H-G[i] .. H-G[i+1]`
  (band 15 down to -22600). `DS:C080..C09F` is left holding G after the call (only 1442 uses it).
* Geometry: boundary i is the line through `(w2 + t*sinC, h2 - 0.75*t*cosC)` with half-length vector
  `22600*(cosC, -0.75*sinC)` (Q15), X1 at `-cosC` side; band polygons are quadrilaterals far outside the screen
  horizontally, clipped by 06AC (with INT 0 saturation for the huge coordinates). Colour ramp 0x80..0x8F sky
  (horizon first), 0x70..0x7F ground (horizon first); see FORMATS.md "Flight palette" for the RGB values.
* Draw order: sky bands 0..15 (from the horizon upwards), then ground bands 0..15 (from the horizon downwards); each
  band is an independent `poly_fill_far` (tables cleared each time), later bands overwrite shared edge pixels.
* 32-bit/16-bit details: all X/Y/t values are int16 with wrap (e.g. `H - G[i]` at extreme altitude). mul15r rounds
  (+0x4000). The `(int8)[98DE]` compare is a signed byte compare (`cmp byte, 3; jge`).
* Unlike hz_draw there is no explicit line, no 0x3FF clamp, and no "off-screen" special case: off-screen bands just
  produce empty tables (`poly_fill` returns when ymin = 0xFFFF).
Callers: `dae8 r3d_begin_scene` (draw_backdrop != 0). Callees: `1001:0006`, `1001:01EC`, `109D:0006`, `f45e`
(rtl long shl), `f392` (rtl long div).

---------------------------------------------------------------------------------------------------------------------

## Part C. Segment 1433 fixed-point library (image 0x14330..0x14A2D) and tables (segment 14B2)

All routines are `far`, cdecl (first argument at `[bp+6]`), DS = DGROUP, use ES = 0x14B2 for the tables, preserve
SI/DI/BP/ES, return in AX (DX:AX for 32-bit).

### C.0 Tables (`14B2:xxxx` = image 0x14B20 + xxxx), all uint16
| table | 14B2 offset | entries | formula reproducing every entry (IEEE double, verified with Python math) |
|---|---|---|---|
| sine quadrant `TS` | 0000..0FFF | 2048 | `TS[i] = floor(32767*sin(i*pi/4096) + 0.5)` (0 mismatches); TS[0]=0, TS[2047]=32767 |
| atan `TA` | 1000..2FFF | 4096 | `TA[i] = trunc(atan(i/4096) * 32768/pi)` (0 mismatches); TA[4095]=8190 |
| hypot `TH` | 3000..3201 | 257 | `TH[i] = trunc((sqrt(1+(i/256)^2) - 1) * 65536)` except **TH[192] = 16383** (formula gives exactly 16384); TH[256] = 27145 (= the entry at 3200) |
| asin `TQ` | 3202..3A01 | 1024 | `TQ[i] = trunc(asin(i/1023) * 16383/(pi/2))` (0 mismatches); TQ[1023]=16383 |
Port: generate the four arrays offline with these formulas into a C table (or load 0x3A02 bytes from image 0x14B20);
do not compute them at run time with a different libm.
`DS:9178` octant base table (8 words, initialised): `0000 4080 0080 C000 8080 4000 8000 C080` (low byte 0x80 = subtract).

### C.1 `1433:0004 math_sin(uint16 a)` / `1433:0031 math_cos(uint16 a)` (V,T exhaustive)
```c
int16 math_sin(uint16 a){ uint16 i = a >> 3 & 0x7FF; if (a & 0x4000) i = 0x7FF - i; int16 v = TS[i]; return (a & 0x8000) ? -v : v; }
int16 math_cos(uint16 a){ return math_sin(a + 0x4000); }   // the code reverses the index when !(a&4000h), sign by (a+4000h)&8000h; identical for all a
```
(`(a>>2) & 0xFFE` is the byte offset; `not` before the mask gives `0x7FF - i`.) No interpolation; range -32767..32767.
Callers: 03A9, 05D1 only (other code uses the DS:2084 table / 109D helpers).

### C.2 `1433:00D9 math_mul_sin(int16 k, uint16 a)` / `1433:011A math_mul_cos(int16 k, uint16 a)` (V,T) - dead
`return q15(k, math_sin(a))` / `q15(k, math_cos(a))`. The `mov bh,ah ... or dl,1` sequence is a **no-op**: it sets bit 0
of DX to bit 15 of the low product word, which `shl ax,1; rcl dx,1` has already shifted into that bit. No rounding.

### C.3 `1433:0061 math_atan2(int16 y [bp+6], int16 x [bp+8]) -> uint16` (V,T)
```c
uint16 X = x, Y = y, o = 0;              // magnitudes as uint16 (0x8000 stays 0x8000 = 32768)
if ((int16)x < 0) { o += 8; X = -X; }  if ((int16)y < 0) { o += 4; Y = -Y; }
if (X < Y) { o += 2; swap(X, Y); }      // X = larger, Y = smaller (unsigned)
uint16 base = W9178[o/2], t;
if (Y >= X /* equal, incl. 0,0 */) t = TA[4095];                  // 8190, not 8192
else t = TA[(uint16)(((uint32)Y << 16) / X) >> 4];               // div: floor; index floor(ratio/16), 0..4095
return (base & 0x80) ? (base & 0xFF00) - t : (base & 0xFF00) + t;
```
Result: 0x0000 = +x axis, 0x4000 = +y, counter-clockwise. Quirks: `atan2(0,0) = 8190`; exact diagonals give
`k*0x4000 +- 8190`. Truncation everywhere (always rounds the angle towards the octant start). Callers: 04D7 only.

### C.4 `1433:015E math_asin(int16 x) -> int16` (V,T exhaustive)
`uint16 m = |x| (0x8000 stays); int16 v = TQ[(m >> 5) & 0x3FF]; return x < 0 ? -v : v;` Range -16383..16383
(0x4000 = 90 deg). Quirk: `asin(-32768) = 0` (index wraps to 0). Callers: 04D7.

### C.5 `1433:018E math_hypot2(int16 a, int16 b) -> uint16` (V,T)
```c
uint16 A = |a|, B = |b|;                                 // cwd/xor/sub: 0x8000 -> 0x8000
if (A == B) { L = A; t = TH[256]; }
else { L = max(A,B); S = min(A,B); t = TH[(((uint32)S << 16) / L) >> 8]; }   // index = high byte of the Q16 ratio
uint16 r = (uint16)(((uint32)t * L) >> 16) + L;          // mul (unsigned), 16-bit add
return (r < L) ? L : r;                                  // guard for a wrap; cannot trigger (max 46341)
```
Approximation `L*(1 + TH[floor(256*S/L)]/65536)` (piecewise constant, always <= the true value). Callers: 01E0 only.

### C.6 `1433:01E0 math_hypot3(int16 x, int16 y, int16 z) -> int16` (V,T) - dead
`h = hypot2(x,y); if ((int16)h < 0) return h + (|z| >> 1) /* shr, 16-bit wrap */; return hypot2(h, z);`

### C.7 `1433:0219 mat_vec16(int16 v[3] [bp+6], const int16 M[9] [bp+8])` - in place (V,T)
`for k in 0..2: v'[k] = (int16)((hi(x*M[k]) + hi(y*M[3+k]) + hi(z*M[6+k])) << 1)` with x,y,z read before any
write. I.e. the **high words** (floor /65536) of the three products are summed in 16 bits, then shifted left once
(result always even). Row vector times row-major M. Callers: 11eb:031b, 04cd, 04f9 (4 sites).

### C.8 `1433:0286 mat_mul(const int16 A[9], const int16 B[9], int16 C[9])` and helper `1433:02AA mat_row_mul` (V,T)
```c
for (i = 0; i < 3; i++) for (k = 0; k < 3; k++) {        // 02AA: SI = &A[3i], BX = B, DI = &C[3i+k] (advances)
    int32 s = (int32)A[3i]*B[k] + (int32)A[3i+1]*B[3+k] + (int32)A[3i+2]*B[6+k];   // full 32-bit, wraps
    [916A] = (uint16)(s << 1);                            // scratch low word left in DS:916A
    C[3i+k] = (int16)(s >> 15);                           // floor, no rounding
}
```
Element-by-element with immediate stores: if C aliases A or B the later elements read the already written values
(reproduce by writing exactly in this order). 02AA clobbers BP (the far wrapper does not need it afterwards).
Callers: 11eb:031b.

### C.9 `1433:02E2 mat_transpose(const int16 src[9], int16 dst[9])` (V,T)
Reads src sequentially (`lodsw`) and writes `dst[0,3,6,1,4,7,2,5,8] = src[0..8]` in that order; in-place use is
therefore not a true transpose (dst[3] = src[1] is written before src[3] is read). Callers: 11eb:000c, 031b.

### C.10 `1433:0314 mat_copy(const int16 src[9], int16 dst[9])` (V,T) `rep movsw` x9 (forward, DF=0 assumed). Caller 11eb:000c.

### C.11 `1433:032E vec_mat32(const int16 v[3], const int16 M[9], int32 out[3])` (V,T)
`out[k] = (int32)(x*M[k] + y*M[3+k] + z*M[6+k])` with 32-bit wrapping adds (`add/adc`), no shift. v is read before the
writes (out may not alias v anyway). Callers: 11eb:00d3, 01b8, 0440.

### C.12 `1433:03A9 mat_from_euler(uint16 a, uint16 b, uint16 c, int16 out[9])` (V,T)
```c
SA=sin(a) ->[916C], CA=cos(a) ->[916E], SC=sin(c) ->[9174], CC=cos(c) ->[9176], SB=sin(b) ->[9170], CB=cos(b) ->[9172];
out[0] = q15(CC,CA) - q15(q15(SC,SB),SA);      out[1] = q15(q15(CC,SB),SA) + q15(SC,CA);   out[2] = -q15(CB,SA);
out[3] = -q15(SC,CB);                          out[4] = q15(CC,CB);                        out[5] = SB;
out[6] = q15(q15(SC,SB),CA) + q15(CC,SA);      out[7] = q15(SC,SA) - q15(q15(CC,SB),CA);   out[8] = q15(CB,CA);
```
Every product is a separate `q15` (floor >>15, truncated to 16 bits); the triple products are nested `q15(q15(..),..)`;
all sums/negations are 16-bit. There are **no** single 32-bit sums in this routine. Sin/cos via C.1 (1433 table, not
DS:2084). Callers: 0ffb terr_probe_point, 13e3 map_draw_object, dc64 r3d_set_camera_matrix, df08 r3d_draw_queued,
e7ac r3d_place_object, 11eb:031b r3d_setup_parts (all render3d).

### C.13 `1433:04D7 mat_to_euler(const int16 M[9], uint16 *a [bp+8], uint16 *b [bp+0A], uint16 *c [bp+0C])` (V,T) - dead
```c
b = asin(M[5]);
if (|b| > 0x3E80 /* unsigned; 0x8000 counts as large */) { a = atan2(M[6], M[0]); c = 0; }
else { a = atan2(-M[2], M[8]); c = atan2(-M[3], M[4]); }
*c = c; *a = a; *b = b;   // write order
```

### C.14 `1433:0542 math_mulq(int16 a, int16 b)` (V,T) - dead: `AX = q15(a,b), DX = sign extension (cwd)`.
### C.15 `1433:0554 math_muldiv(int16 a, int16 b, int16 c)` (V,T) - dead
```c
uint16 C = |c|; if (C == 0) c = 1 /* the stack word */;   // C stays 0, so the next test always takes branch 2
if (|b| >= C)  { int16 q = b / c; int32 p = (int32)q * a; return AX = low(p), DX = high(p); }   // #DE if b=-32768,c=-1
else           { int16 q = ((int32)a * b) / c; return AX = q, DX = cwd(q); }                     // cannot overflow
```
### C.16 `1433:058F math_div32sat(uint16 lo, int16 hi, int16 d)` (V,T) - dead
```c
uint16 C = |d|; if (C == 0) d = 1 /* stack word, unused afterwards */;
if ((uint16)((|hi| + 1) << 1) > C) return hi < 0 ? -0x7FFF : 0x7FFF;     // saturate, sign of hi only
return (int16)(((int32)hi << 16 | lo) / (int16)C);                         // divides by |d|, sign of d ignored
```
Quirks: `|hi| = 0x7FFF` or `hi = -32768` make the guard wrap (0 / 2) and the `idiv` raises #DE (no handler installed:
DOS "divide overflow"); `d = -32768` divides by -32768 (sign flipped). Dead, so harmless.
### C.17 `1433:05C6 math_imul(int16 a, int16 b) -> int32` (V,T) - dead: `DX:AX = a*b`.
### C.18 `1433:05D1 mat_from_euler_b(uint16 p6, uint16 p8, uint16 pA, int16 out[9])` (V,T) - dead
`S1,C1 = sin/cos(pA) -> [916C],[916E]; S3,C3 = sin/cos(p6) -> [9174],[9176]; S2,C2 = sin/cos(p8) -> [9170],[9172];`
`out = { q15(q15(S1,S2),S3)+q15(C1,C3), q15(C2,S3), q15(q15(C1,S2),S3)-q15(S1,C3), q15(q15(S1,S2),C3)-q15(C1,S3),`
`q15(C2,C3), q15(q15(C1,S2),C3)+q15(S1,S3), q15(S1,C2), -S2, q15(C1,C2) }`. The index size (286) stops at 06EF; the
routine really ends at `06FC retf` (last store `out[8]` at 06F7).

---------------------------------------------------------------------------------------------------------------------

## Open items
1. Nothing open for the arithmetic: every routine of the range is differential-tested with 0 mismatches.
2. (U) Meaning of the `[4360] && [43B2]` mono backdrop (0x88 sky / 0x7C ground): `[4360]` = bf71 TV-camera terrain
   mode, `[43B2]` = F6 tv_opt (simflow). Visual check only.
3. 1442 vs hz_draw horizon offsets differ (1442: `cosB` clamped to 0x200, long divide; hz_draw: 0x3FF when
   `cosB <= 0x1F0B`). Both reproduced; no user check needed unless the low-detail horizon looks wrong.
4. vgxref lists `1001:0018` as a writer of `DS:498C`; it only reads it (`xor si,[498C]`). Tool quirk.
5. The `sky offset` table DS:0D22 is also read by `1961`/`19ea` (MFD sky/ground bands, other reader).

## DGROUP globals seen
| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 0D1E / 0D20 | w,w | r3d_scr_cx / r3d_scr_cy | screen centre w2, h2 of the current viewport | db71 (w); 0018, 1442, 13ac (r) |
| 0D22 | w[17] | hz_sky_offs | sky band offsets 0,2,3,4,6,8,11,15,20,27,36,48,64,87,119,164,0 | 1442, 1961, 19ea (r) |
| 0D42 | w | hz_corner_x | corner column of the current side (0 / XMAX) | 0018 |
| 0D44 | w | hz_nsinC | -sin(roll) | 0018 |
| 0D46 | b | hz_colour | first-side colour | 0006 (w), 0018 (r) |
| 0D48 | w | hz_side | pass toggle 0 / 0xFFFF | 0018 |
| 2293 / 2295 | w,w | clip_xmax / clip_ymax | inclusive clip maxima (signed) | db71, 87a9, 873f, 17a3, 10a8:000c (w); all of 1001 (r) |
| 2299 / 229B / 229D / 229F | w x4 | edge_x1 / x2 / y1 / y2 | segment in/out block | 1001, 87a9, 17a3 ... |
| 228A | b | clipA_oc | 0336 outcode of point 1 | 0336 |
| 228B / 228D / 228F / 2291 | w x4 | clipA_dx / dy / hdx / hdy | 0336 deltas and halves | 0336, 04E3 |
| 22A1 | w[220] | edge_L | left edge table (0xFFFF empty) | 1001, slot 37/40 |
| 2459 | w[220] | edge_R | right edge table (0 empty) | 1001, slot 37/40 |
| 2611 / 2613 | w,w | walk_dx / walk_dy | |dx|, |dy| of the walked edge | 05B9 |
| 2615 / 2617 | w,w | edge_ymin / edge_ymax | dirty row range | 1001; slot 37 args |
| 261A | b | clipB_oc | 06AC outcode | 06AC |
| 261B / 261D / 261F / 2621 | w x4 | clipB_dx / dy / hdx / hdy | 06AC deltas and halves | 06AC, 0854 |
| 294B | b | r3d_lens_half | halves H (lens flag) | many (r) |
| 433A | b | r3d_zoom_shift | H <<= n | 2f82, bf71 (w); 0018, 1442 (r) |
| 4360 | w | view_terrain_mode | with 43B2: mono backdrop | bf71 (w); 1442, e8b5 (r) |
| 43B2 | w | tv_opt | F6 toggle | cba0 (w) |
| 4980 | w | r3d_org_z | camera origin z (altitude) for the ground bands | dca9, 0ffb (w); 1442 (r) |
| 4986 / 4988 / 498A / 498C | w x4 | r3d_sinB / cosB / sinC / cosC | pitch / roll sin, cos (Q15), part of r3d_sc DS:4982 | e26b, e3e2 (w) |
| 98C8 / 98E4 / 98F8 / 9F12 | w x4 | hz_nsC / hz_cC / hz_sB / hz_cB | cache: -sinC, cosC, sinB, cosB | 1442, bf71 (w); 17a3, bf71, 1442 (r) |
| 98DE | b | detail | detail level 0..3 (signed byte compare) | 1442 (r) |
| C080 | w[16] | hz_band_offs | band offsets (sky copy, then ground G[i]) | 1442 |
| 916A | w | mat_scratch | low word of the last mat_mul sum << 1 | 02AA |
| 916C..9176 | w[6] | mat_sc | sin/cos cache of 03A9/05D1 (sin a, cos a, sin b, cos b, sin c, cos c for 03A9) | 03A9, 05D1 |
| 9178 | w[8] | atan_oct_base | octant bases 0000 4080 0080 C000 8080 4000 8000 C080 | 0061 |

## Structures seen
* Viewport (R11a 3.1): 1442 uses only `+4` (word, sky colour for the flat split) and passes the pointer to
  01EC (unused there); 024C uses `+0` page and `+4` colour, 02BA `+6`.
* Edge block `[2299..229F]` (x1, x2, y1, y2 at 2299, 229B, 229D, 229F: note x2 before y1).
* 3x3 matrices: 9 int16, row-major `M[3*row+col]`, Q15; vectors are row vectors (`v*M`).


---------------------------------------------------------------------------------------------------------------------

# Part 7: function index (generated from render3d_symbols.csv)

`kind` func = indexed function (counted by `tools/spec_coverage.py`), label = local entry or index-gap routine. The `[Part n]` prefix of a note gives the part with its section; `[S3.6]` rows come from simflow.md unchanged.

| address | kind | name | signature | note |
|---|---|---|---|---|
| 0000:03ce | func | `r3d_scene_draw` | `void near(int16 a1,int16 a2,int16 a3,int32 x,int32 y,int32 z,int16 x0,int16 y0,int16 w,int16 h)` | [Part 1] main 3D view: viewport 0344, 04BB delay, 7D50 table, lights, DAE8, 04DA, 9ACA, DB3E, A17E (simflow Part 3 s3) |
| 0000:04da | func | `r3d_draw_terrain` | `void near(int16 a1,int16 pitch,int32 x,int32 y,int32 z)` | [Part 1] levels 4..1, cells per octant table 034C (path A 15 cells + E8A2 cull, path B 9), records -> E7AC; 0FB1 overrides |
| 0000:050b | label | `terr_pathA_cell` | `label in 04DA` | [Part 1] path A (level 4 detail>=2): cell offsets + E8A2 class-7 cull |
| 0000:0575 | label | `terr_pathB_cell` | `label in 04DA` | [Part 1] path B: i==9 end, low-detail far-cell skip, look-down table 04CC/04DE if pitch<D555h |
| 0000:05f9 | label | `terr_cell_draw` | `label in 04DA` | [Part 1] DCA9 origin + tile type + record loop |
| 0000:0669 | label | `terr_rec_noovr` | `label in 04DA` | [Part 1] override 0 -> obj & 7Fh |
| 0000:0719 | label | `terr_far_cell` | `label in 04DA` | [Part 1] low detail far cell: level 4 record 0 only, type 100h/obj raw, [0504]=400h |
| 0000:0755 | label | `terr_next_cell` | `label in 04DA` | [Part 1] i++ and path select |
| 0000:0772 | label | `terr_next_level` | `label in 04DA` | [Part 1] [E2FC]-- and level set-up (0804 x/y/z) |
| 0000:0804 | func | `terr_to_level` | `int32 near(int16 level,uint32 v)` | [Part 1] L0 v<<1, L1 v, L2 (v+2)>>2, L3 (v+8)>>4, L4 (v+20h)>>6 (shr32). Diff-tested |
| 0000:0865 | func | `terr_tile_type` | `uint16 near(int16 level,int16 x,int16 y)` | [Part 1] L4 +2 offset, bounds vs DS:0506, maps B00C/9F68, child tables 9D10/9B08/9904 (recursive). Diff-tested |
| 0000:0fb1 | func | `terr_scenery_override` | `uint16 near(int16 level,int16 idx,int16 cx,int16 cy)` | [Part 1] backward search DS:B772 x [9902]; AH = hi(count-1): returns FF00h when count 0 (quirk); [94DC]=index |
| 0000:0ffb | func | `terr_probe_point` | `void near(uint32 x,uint32 y,uint32 z)` | [Part 3] nearest flag-0x10 record of the level-4 tile (records 0..n-2, id&0x50 skipped), 11EB:01B8 inside test -> byte C686=0x80 |
| 0000:119c | func | `map_render_terrain` | `void near(Viewport*ctx,int16 px,int16 py,int16 shift)` | [Part 3] map MFD: 4894=0, DAE8 top-down camera (pitch C000, z 2000h, backdrop), 11F2, DB3E, 7D5E=0; only caller 8387 |
| 0000:11f2 | func | `map_draw_tiles` | `void near(int16 px,int16 py,int16 shift)` | [Part 3] 5 passes L=4,3,2,4,3 (0CFC), filter 94E6; rows then columns of 1353 range; z==0 records -> 13E3 with 7D5E=DECC+0Eh |
| 0000:1353 | func | `map_tile_bounds` | `void near(int16*x0,int16*x1,int16*y0,int16*y1)` | [Part 3] 13AC of (0,0) and ([2293],[2295]); min clamped to 0, max to [94E0]-1 (signed) |
| 0000:13ac | func | `map_pixel_to_tile` | `void near(int16 px,int16 py,int16*tx,int16*ty)` | [Part 3] tx=idiv(94E2-0D1E+px,94DE); ty=idiv(idiv((py-0D20)<<2,3)+94E4,94DE); trunc toward 0 |
| 0000:13e3 | func | `map_draw_object` | `void near(uint16 obj,int16 x,int16 y)` | [Part 3] L>=3: 07E0[obj]&80h must equal 94E6; 8560=B760; identity 49A0; 11EB:00D3(obj&FF,x,0,-y,49A0,C02E) |
| 0000:1442 | func | `r3d_draw_sky_ground` | `void near r3d_draw_sky_ground(Viewport*ctx)` | [Part 6] detail<3: hz_fill_far(ctx+4); else 16 sky bands 80h+i and 16 ground bands 70h+i (mono 88h/7Ch) via 1001:01EC |
| 0000:1a85 | func | `fx_explosion_sprite` | `void near(int16 x,int16 y,int16 size,int16 cls)` | [Part 1] sprite_scaled centred, h=size-size/4, table DS:9188, ctx [0348]?[4042]:[0344]; caller A17E |
| 0000:1ad6 | func | `r3d_sprite_main` | `void near(int16 x,int16 y,int16 size,int16 cls)` | [Part 1] sprite_scaled(ctx [0344], x-size/2, y-size/2, size, size-(size sar 2), DS:91D8[cls]) |
| 0000:1b1b | func | `r3d_sprite_tv` | `void near(int16 x,int16 y,int16 size,int16 cls)` | [Part 1] same as 1AD6 with ctx [4042] |
| 0000:77d9 | func | `terr_probe_cell` | `void near(int16 cellx,int16 celly,int16 alt)` | [Part 3] 0FFB((long)cx<<5,(0x8000-(long)cy)<<5,(long)alt); callers 5730 (unit flag 0x2000), 671a (impact) |
| 0000:9aca | func | `r3d_draw_world_objects` | `void near(void)` | [Part 2] per-frame object pass (03CE): own F-117, smoke billboards, units+shadows, projectiles, wreck 0x0B; embeds ground/air target designator (weapons logic) |
| 0000:b85d | func | `r3d_project_point` | `void near(int16 x_u,int16 y_u,int16 z)` | [Part 2] world->screen via BA13 x3: 49E0 sx (FFFF invisible) 49E8 sy DE92 depth>>3; /X/<=-Z, scale 256, sy*3/4, centre 160/52/60/100 (T) |
| 0000:ba13 | func | `r3d_matrix_row_dot` | `int32 near(int16 row,int16 dx,int16 dy,int16 dz)` | [Part 2] mq(B156[r],dx)+mq(B15C[r],dz)+mq(B162[r],dy), 32-bit sum (T) |
| 0000:ba67 | func | `r3d_view_depth` | `void near(int16 px,int16 py,int16 pz,int16 cx,int16 cy,int16 cz)` | [Part 2] [DE92]=sar32(mq(4992,cx-px)+mq(4998,(pz-cz)>>5)+mq(499E,py-cy),3); z-term sign quirk; caller C834 (T) |
| 0000:bc12 | func | `obj_wreck_shape` | `uint16 near(int16 site)` | [Part 3] site +0E shape; destroyed (+8&80h): shape/100h if flag 20h else cbw(C0AC or DEA4 by BC77)+100h (ids>=80h lose the bank) |
| 0000:bcd7 | func | `r3d_unit_terrain_class` | `void near(int16 unit)` | [Part 3] [49C6]=B174[(y>>11)*16+(x>>11)]&3 for the last 4 units; others 1 if class 0 and [3D62]==0 else 0 |
| 0000:bd8f | func | `r3d_draw_model` | `void near(uint16 model,int32 x,int32 y,int16 alt,int16 a1,int16 a2,int16 a3,int16 lod)` | [Part 1] camera-relative, shift by lod-(294B?2:3), abs16 of LOW words < 7FFFh, DCA9(0,0,-rz), E2FC=1, E7AC(C6B8(model),...) |
| 0000:bee1 | func | `r3d_queue_object` | `void near(uint16 id,int32 x,int32 y,int16 z,int16 a1,int16 a2,int16 a3)` | [Part 1] BD8F variant relative to bf71 camera B766/B76A/B808; no lod shift, no C6B8; caller C834 |
| 0000:c6b8 | func | `r3d_scenery_id` | `uint16 near(uint16 id)` | [Part 1] if id bit 8: DS:0510[id&7F] / (id&8000) else id |
| 0000:c73c | func | `fx_fire_puffs` | `void near(int16 hdg,int16 pitch,int16 roll,int16 X,int16 Y,int16 Z)` | [Part 2] 6 puffs DS:3A5C -> E7AC 0x25+frame (0..3) / 0x24 with C040=frame (4,5); consumes game RNG cb62 |
| 0000:c834 | func | `fx_smoke_draw` | `void near(int16 cx,int16 cy,int16 cz)` | [Part 2] 16 smoke puffs DS:39DA for TV camera: BA67 depth gate, 49C6=size<<8/class, BEE1(0x25) |
| 0000:d76b | func | `view_range_setup` | `void near(void)` | [Part 1] 48A6[k]=20h<<(k+detail) k<6, 48B2, 48B4 (no readers); [48B6]=(detail+1)*0D05h max depth |
| 0000:dae8 | func | `r3d_begin_scene` | `void near(Viewport*vp,int16 a1,int16 a2,int16 a3,int16 ox,int16 oy,int16 oz,int16 backdrop,int16 focal)` | [Part 1] DB71, DC64, DCA9; backdrop: C686=0, 8560=B760, 11EB:0F9B, wait byte[262A]==0, 1442; [49C4]=0 |
| 0000:db3e | func | `r3d_end_scene` | `void near(void)` | [Part 1] DF08, offset_clear, C04E+=1800h C04C+=1B00h ([4EA0]=6), memcpy(49B2,498E,18) |
| 0000:db71 | func | `r3d_set_viewport` | `void near(Viewport*vp)` | [Part 1] light 7D54=-36,36,36; centre 0D1E/0D20 (y0==0: 100 or 52/60), offset_set, 85CA..85D4, 2293/5, 85EA, E53E/E544 tables |
| 0000:dc64 | func | `r3d_set_camera_matrix` | `void near(int16 a1,int16 a2,int16 a3,int16 focal)` | [Part 1] E26B(498E,-a1,-a2,-a3); 1433:03A9(-a1,-a2,a3,49A0); 11EB:000C(49A0,focal) |
| 0000:dca9 | func | `r3d_set_origin` | `void near(int16 x,int16 y,int16 z)` | [Part 1] [497C]=x [497E]=y [4980]=z |
| 0000:dcc0 | func | `r3d_queue_billboard` | `void near(uint16 id,int32 x,int32 y,int16 z,int16 sx,int16 sy,int16 size)` | [Part 1] camera-relative, sar by 294B?2:3, low-word test, class 2 E8A2, DDFE with sx/sy/size in +10..+14; caller 9ACA |
| 0000:ddfe | func | `r3d_queue_insert` | `void near(void)` | [Part 1] full: drop ptr[0] (farthest) and reuse; key=sar32(C,2*(4-E2FC)) (+200000h L2 class5); signed hi/unsigned lo; new before equal. Diff-tested |
| 0000:de3b | label | `r3d_queue_key` | `label in DDFE` | [Part 1] key computation and record fill |
| 0000:deb3 | label | `r3d_queue_search` | `label in DDFE` | [Part 1] insertion search from the nearest end |
| 0000:df08 | func | `r3d_draw_queued` | `void near(void)` | [Part 1] replays queue far->near: sprite recs (400h) 1AD6; bank0 vars 5..14; id 18h gear/side models 21h/22h/23h; ids>=25h glow 1B1B |
| 0000:df16 | label | `r3d_dq_body` | `label in DF08` | [Part 1] record body (lights and sprite test) |
| 0000:dfe9 | label | `r3d_dq_player` | `label in DF08` | [Part 1] id 18h: C072==0 -> 1Fh, else E3E2, side tests, deck alt, model 23h |
| 0000:e0d8 | label | `r3d_dq_draw` | `label in DF08` | [Part 1] 1433:03A9 + E54B then draw(s) |
| 0000:e19d | label | `r3d_dq_var14` | `label in DF08` | [Part 1] ids 1/0Ah/0Fh: var 14 = 1 |
| 0000:e1a6 | label | `r3d_dq_single` | `label in DF08` | [Part 1] single model draw + glow overlay (7D4E edge) |
| 0000:e23f | label | `r3d_dq_loop` | `label in DF08` | [Part 1] loop test i < [49C4] and destroyed/lights |
| 0000:e26b | func | `r3d_matrix_from_angles` | `void near(int16*m,int16 A,int16 B,int16 C)` | [Part 1] Q15 rotation, sin/cos to 4982..498C, products mq rounded. Diff-tested |
| 0000:e3e2 | func | `r3d_matrix_obj_side` | `void near(void)` | [Part 1] Q15 matrix 49A0 from [496A..496E] (other composition); DF08 id 18h side tests. Diff-tested |
| 0000:e54b | func | `r3d_transpose49A0` | `void near(void)` | [Part 1] swap 49A2/49A6 49A4/49AC 49AA/49AE |
| 0000:e7ac | func | `r3d_place_object` | `void near(uint16 type,int16 a1,int16 a2,int16 a3,int16 X,int16 Y,int16 Z)` | [Part 1] flags DS:0A60[bank][idx]: class->497A, bit7 -> DDFE; else shade, 1433:03A9, transpose, 11EB:00D3(idx,x,z,-y) |
| 0000:e82c | label | `r3d_place_draw` | `label in E7AC` | [Part 1] immediate draw branch |
| 0000:e8a2 | func | `r3d_cull_point` | `int16 near(int16 x,int16 y,int16 z)` | [Part 1] BP=x BX=y CX=z -> E8B5; AX 0 visible, ZF |
| 0000:e8b5 | func | `r3d_cull_core` | `near(BP=x,BX=y,CX=z,[497A]) -> AX,ZF` | [Part 1] A,B,C=2*(M*v) s32 wrap; depth 48B6/48C6; wide 4936/4946 or E53E/E544 >>cl; [4978] dist (wide quirk). Diff-tested |
| 0000:e9ad | label | `r3d_cull_normal` | `label in E8B5` | [Part 1] normal frustum (cl=294B^1) |
| 0000:ea09 | label | `r3d_cull_dist` | `label in E8B5` | [Part 1] distance estimate [4978] and final test |
| 0000:ea1f | label | `r3d_cull_reject` | `label in E8B5` | [Part 1] AX=1 ZF=0 |
| 1001:0006 | func | `hz_fill_far` | `void far hz_fill_far(int colour)` | [Part 6] [0D46]=colour, hz_draw (flat two-colour sky/ground split, 1442 when detail<3) |
| 1001:0018 | func | `hz_draw` | `void near hz_draw(void)` | [Part 6] horizon line from pitch/roll (H=256*sinB/cosB or 3FFh, zoom, lens), clip+line 0336, fill side 1 [0D46], side 2 0x78 |
| 1001:01ec | func | `poly_fill_far` | `void far poly_fill_far(Viewport*vp,int n,int16*pts,int colour)` | [Part 6] clear tables, n edges via 06AC (clipped), slot 37 mode 0 |
| 1001:024c | func | `fill_rows_far` | `void far fill_rows_far(Viewport*vp,int xl,int ymin,int xr,int ymax)` | [Part 6] page vp+0, colour vp+4, L=xl R=xr rows ymin..ymax, slot 40, restore segment |
| 1001:02ba | func | `fill_rows2_near` | `void fill_rows2_near(...)` | [Part 6] dead: copy of 024C with colour vp+6 and a near ret; no callers |
| 1001:0326 | func | `line_clip_far_stub` | `void far line_clip_far_stub(void)` | [Part 6] call 0336; retf (caller 10a8:000c) |
| 1001:032a | func | `line_clip_far` | `void far line_clip_far(void)` | [Part 6] saves BP SI DI ES; 0336 |
| 1001:0336 | func | `line_clip_draw` | `bool near line_clip_draw(void) /*CF=1 rejected*/` | [Part 6] Cohen-Sutherland clip of [2299..229F] to 0..[2293]x0..[2295], rounded intercepts, INT0 04E3; slot 31 line |
| 1001:04c2 | func | `clip_outcode` | `uint8 clip_outcode(BX=x,BP=y) -> AL,ZF` | [Part 6] AL bits 8:x<0 1:x>xmax 4:y<0 2:y>ymax (signed) |
| 1001:04e3 | func | `int0_sat_a` | `INT 0 handler (iret)` | [Part 6] 0336 idiv overflow: AX=+-7F00h by sign(DX^divisor), DX=0, IP+=4; divisor by faulting IP (0474 -> [228D]) |
| 1001:0504 | func | `poly_flush_far` | `void far poly_flush_far(int mode)` | [Part 6] slot 37 poly_fill([2615],22A1,[2617],mode) (caller c6de) |
| 1001:051f | func | `tables_clear_far` | `void far tables_clear_far(void)` | [Part 6] call 0525; retf |
| 1001:0525 | func | `tables_clear` | `void near tables_clear(void)` | [Part 6] reset dirty rows: L=FFFFh R=0, [2615]=FFFFh [2617]=0 |
| 1001:055b | func | `edge_column` | `void near edge_column(SI=x,DI=ya,DX=yb)` | [Part 6] x==0 -> L, x==XMAX -> R: overwrite rows min..max (clamped 0..YMAX), update 2615/2617 |
| 1001:05aa | label | `edge_column_x` | `void near edge_column_x(BX=x)` | [Part 6] x<0 -> edge_column(0,..), x>XMAX -> edge_column(XMAX,..), else nothing |
| 1001:05b9 | label | `edge_walk` | `void near edge_walk(void)` | [Part 6] Bresenham of the clipped [2299..229F] into L (min) / R (max), x-major or y-major, updates 2615/2617 |
| 1001:06a0 | func | `edge_to_tables_far` | `void far edge_to_tables_far(void)` | [Part 6] saves ES SI DI BP; 06AC |
| 1001:06ac | func | `edge_to_tables` | `void near edge_to_tables(void)` | [Part 6] clip core of 0336 (vars 261A..2621, INT0 0854); border columns for parts beyond left/right; visible part -> 05B9 |
| 1001:0854 | func | `int0_sat_b` | `INT 0 handler (iret)` | [Part 6] 06AC idiv overflow: as 04E3, IP 07CE -> divisor [261D] else [261B] |
| 1088:0120 | func | `r3d_depth_shade` | `uint16 far(void)` | [Part 1] 0 if [7D5E] or ![43B6]; else clamp((max(0,(s8)[4963])>>1)-[497A],0,31)>>1. Diff-tested |
| 11eb:000c | func | `r3d_set_camera` | `void far(const int16*mat9,int16 zoom)` | [Part 4] camera matrix -> 7AF8 scaled: col0 m>>1, col1 -(3/8)m, col2 floor(-far*m/2^15); far 7D2E=7FFF00h/max(abs16(2zoom),100h); 7B0A transpose dead |
| 11eb:00d3 | func | `r3d_draw_object` | `void far(uint16 obj,int16 x,int16 y,int16 z,int16*mat9,int16*vars)` | [Part 4] draws one 3DX object: LOD by /z_hi/, radius, 02B6 cull, parts, planes, INT0 hook, vertices, edges, faces; C686/=inside |
| 11eb:01b8 | func | `r3d_probe_object` | `void far(uint16 obj,int16 x,int16 y,int16 z,int16*mat9)` | [Part 4] 00D3 without drawing (point-inside-model test for 0FFB); always runs 02B6; 7D1E stale |
| 11eb:0281 | func | `r3d_hook_div0` | `void near()` | [Part 4] saves IVT[0] to DS:5EB6/8 and installs 11EB:0975 |
| 11eb:02a2 | func | `r3d_unhook_div0` | `void near()` | [Part 4] restores IVT[0] from DS:5EB6/8 |
| 11eb:02b6 | func | `r3d_frustum_test` | `near(AX=radius)->AX=0/ZF=1 visible` | [Part 4] (abs16(zh)+1)*2 < far (u16 wrap); t=s16(2zh+r)>=0; hi16(abs32(x)) and hi16(abs32(y)) <= t unsigned. Diff-tested |
| 11eb:031b | func | `r3d_setup_parts` | `void near(CX=nparts,ES:SI)` | [Part 4] M_k=PartRot*M_0 at 7B1C+24h*k, eyes 7C3C, lights 7C6C, origins 7CAE; max safe nparts 7 (data max 2) |
| 11eb:0440 | func | `r3d_part_pivot` | `void near(BX=eye slot,DI=slot+12h,ES:SI=pivot)` | [Part 4] origin_k = origin + pivot*M_0 - pivot*M_k (32-bit add/adc) |
| 11eb:04cd | func | `r3d_light_root` | `void near(DI=mat)` | [Part 4] 7C6C = 7D54 light * ObjMat^T; 7D4C=7C72 |
| 11eb:04f9 | func | `r3d_light_part` | `void near(DI=mat)` | [Part 4] [7D4C] = root light 7C6C * PartRot^T; 7D4C+=6 |
| 11eb:0521 | label | `r3d_plane_vartest` | `label in 0565` | [Part 4] 8100 var val prefix: if (int8)val != vars[var&1F] then 7D38=7Fh |
| 11eb:0544 | label | `r3d_plane_partsw` | `label in 0565` | [Part 4] 80pp prefix: eye=7C3C+6pp, 7D4C=7C6C+6pp; mask kept |
| 11eb:0565 | func | `r3d_planes` | `void near(ES:SI=nplanes)` | [Part 4] s=n.eye+d (imul sums mod 2^32) >=0 -> FF&7D38 into 76D2; map mode FF; marks 79D2; BSP 0E5F. Diff-tested |
| 11eb:0587 | label | `r3d_planes_loop` | `label in 0565` | [Part 4] plane loop head (7D38=FFh) |
| 11eb:0658 | func | `r3d_clear_vertex_flags` | `void near()` | [Part 4] zero (abs16(nverts)+2)>>1 words at DS:79D2 |
| 11eb:0678 | func | `r3d_plane_shade` | `void near(SI after d,DI=plane)` | [Part 4] 77D2[di] = bits 14..21 of n.L; dead (7D5A never read). Diff-tested |
| 11eb:06b9 | func | `r3d_xform_vertices` | `void near(ES:SI=vertices)` | [Part 4] needed vertices -> 0819 (C688 s32, D688 proj); part words 8000-800F not counted; nverts<0 shared path (dead) |
| 11eb:06e6 | label | `r3d_xform_partsw` | `label in 06B9` | [Part 4] part word in plane path: BX=[7D20]+24h*pp, 7CAE+0Ch*pp -> 7CA2 |
| 11eb:072e | label | `r3d_xform_planepath` | `label in 06B9` | [Part 4] plane path start (DX=vertex index 0) |
| 11eb:0730 | label | `r3d_xform_loop` | `label in 06B9` | [Part 4] plane path loop head |
| 11eb:0751 | label | `r3d_xform_skip` | `label in 06B9` | [Part 4] unneeded vertex: bp+=8 di+=16 si+=6 |
| 11eb:075e | label | `r3d_xform_shared` | `label in 06B9` | [Part 4] nverts<0: byte indices into DS:4EB6 + origin, project; unused by data |
| 11eb:07cc | label | `r3d_xform_partsw2` | `label in 0819` | [Part 4] part word met inside 0819 (no-plane path) |
| 11eb:0818 | label | `r3d_pad_0818` | `nop` | [Part 4] pad byte |
| 11eb:0819 | func | `r3d_xform_one` | `void near(ES:SI,BX=mat,DI=out3d,BP=out2d)` | [Part 4] out3d=v*M+origin (s32, no shift), 0906; si+=6 di+=16 bp+=8. Diff-tested |
| 11eb:08bc | label | `r3d_project_ortho_left` | `label in 0906` | [Part 4] map mode 7D5E<10h: (int16)(coord_hi << (10h-m)) |
| 11eb:08da | label | `r3d_project_ortho` | `label in 0906` | [Part 4] map mode dispatch on byte 7D5E (signed >= 10h) |
| 11eb:08e5 | label | `r3d_project_ortho_right` | `label in 0906` | [Part 4] map mode m>=10h: coord_hi sar (m&0F), +1 if last bit out (CF) |
| 11eb:0906 | func | `r3d_project` | `void near(DI=v3d,BX=out2d)` | [Part 4] zh>=100h: (x>>8)/zh; 1<=zh<100h: (x>>1)/(z>>9), #DE -> 0975; zh<1: x_hi=8000h. Diff-tested |
| 11eb:0975 | func | `r3d_div0_handler` | `INT 0 handler (iret)` | [Part 4] IP+=3 (skips idiv+cwd), q=idiv(dividend>>8,cx), DX:AX=(int32)q<<8. Diff-tested |
| 11eb:0990 | func | `r3d_near_bisect` | `void far(int32 p0[3] front,int32 p1[3] behind)` | [Part 4] mid=(p0+p1+1)>>1 (wrap) until mid z_hi<=1 unsigned; x->7D42 y->7D46. Diff-tested |
| 11eb:0a37 | func | `r3d_build_edges` | `void near(ES:SI=nedges)` | [Part 4] edge (v0,v1): if nplanes==0 or 79D2[v0]&79D2[v1] -> 0B3F; record 5EBA+18h*i |
| 11eb:0a81 | label | `r3d_edge_v1_behind` | `label in 0B3F` | [Part 4] v1 behind: p0=proj+centre, near point (mid>>8)*2+centre -> +8/+0C and +10h/+14h, clip, /=4040h |
| 11eb:0b3f | func | `r3d_make_edge` | `void near(BP=rec,DI=8*v0,BX=8*v1)` | [Part 4] edge record: proj + zero-extended centre (s32), near-plane cases, 12EF:0333 clip |
| 11eb:0bae | label | `r3d_edge_reject` | `label in 0B3F` | [Part 4] both ends behind: word rec+2 = 8080h, no clip |
| 11eb:0bb6 | label | `r3d_edge_v0_behind` | `label in 0B3F` | [Part 4] v0 behind (mirror of 0A81): near point -> +0/+4 and +10h/+14h |
| 11eb:0c7a | func | `r3d_draw_faces` | `void near(ES:SI)` | [Part 4] nplanes>0 file order, <0 BSP list 78D2, ==0 one list |
| 11eb:0cac | func | `r3d_draw_face_plane` | `void near(BX=plane)` | [Part 4] if 76D2[bx]>7Fh: 7D5A=77D2[bx] (dead), 0CED(es:[si+2bx]) |
| 11eb:0cc9 | label | `r3d_face_list_ret` | `ret` | [Part 4] return of 0CED (FFh end) |
| 11eb:0cca | label | `r3d_face_cond` | `label in 0CED` | [Part 4] 80h/v val: if vars[v&7F] != (int8)val skip next element ((l&1F)+1 bytes), FFh ends |
| 11eb:0ced | func | `r3d_face_list` | `void near(ES:SI=list)` | [Part 4] face list: 00-1F polygon (12EF:003E/010A), 40-5F lines (1359:0040/0049, 7D4E), 20h points (no draw), 80h cond, FF end |
| 11eb:0d04 | label | `r3d_face_poly` | `label in 0CED` | [Part 4] polygon n edges + colour (FFh = skip) |
| 11eb:0d5f | label | `r3d_face_lines` | `label in 0CED` | [Part 4] line element 40/n: colour, n edges; 7D4E = each record |
| 11eb:0daf | label | `r3d_face_outline_dead` | `dead code in 0CED` | [Part 4] unreachable alternative polygon end (fill + edge lines), only reader of 7D3E/7D40 |
| 11eb:0dfe | label | `r3d_face_points` | `label in 0CED` | [Part 4] point element 20/n: computes screen positions, call removed (no drawing) |
| 11eb:0e5f | func | `r3d_bsp_order` | `void near(ES:SI=root,DI=out,BX=vis)` | [Part 4] vis!=0: b1 subtree, node, b0; vis==0: b0, b1 (node not emitted); b0 80h-FEh emitted verbatim; FF end. Diff-tested |
| 11eb:0ef2 | func | `r3d_object_extent` | `void far(uint16 obj,int16 out[3])` | [Part 4] unsigned max abs16 x,y,z of LOD0 vertices; no callers |
| 11eb:0f9b | func | `r3d_shared_verts` | `void far()` | [Part 4] 4EB6[k] = v*CamMat for the count at offs[n]; count 0 in all banks; retf at 12EF:0000 |
| 12ef:0002 | func | `r3d_clip_add_edge` | `void far(int16 x0,y0,x1,y1)` | [Part 5] sext to record DS:5EBA, 0333 clip, 003E add; no callers |
| 12ef:003e | func | `r3d_poly_add_edge` | `void far(SI=rec)` | [Part 5] 85B2/=flags; 40h->00E0; rejected: wall range of end0 side from +6/+0E; else 85BC/=4, 1359:0719 DDA, wall ranges per end |
| 12ef:00aa | func | `r3d_note_left` | `void near(BX,CX)` | [Part 5] left wall range: 85B6=max(.,hi) if hi>=, 85B4=min(.,lo) |
| 12ef:00c5 | func | `r3d_note_right` | `void near(BX,CX)` | [Part 5] right wall range 85B8/85BA |
| 12ef:00e0 | func | `r3d_note_zclip_point` | `void near(SI=rec)` | [Part 5] copy rec+10h..17h to 8588 (first) else 8590; 85BE++ (3rd point overwrites 8590) |
| 12ef:010a | func | `r3d_poly_finish` | `void far(int16 colour)` | [Part 5] colour via 1359:0040; closing edge if 85BE==2; walls; fill 1359:0052; reset wall ranges 7FFF/8000, 85B2/85BC/85BE=0 |
| 12ef:0185 | func | `r3d_wall_right` | `void near()` | [Part 5] 1359:07D6([85BA],[85B8],xmax+1) |
| 12ef:019b | func | `r3d_wall_left` | `void near()` | [Part 5] 1359:07D6([85B6],[85B4],xmin-1) |
| 12ef:01b1 | func | `r3d_draw_line` | `void far(int16 x0,y0,x1,y1,colour)` | [Part 5] colour, record 5EBA, 0333 clip, 01F9 emit; no callers |
| 12ef:01f9 | func | `r3d_line_emit` | `void far(SI=rec)` | [Part 5] if !(rec+2&80h): push es; 1359:0049 line; pop es |
| 12ef:020b | func | `r3d_clip_flags16` | `void near(AL=c0,AH=c1,CX=y0,BP=y1,SI=rec)` | [Part 5] rec+6/+0E clamped y (ymax wins), rec+2 = side bits*101h; preserves BX |
| 12ef:025f | label | `r3d_clip_noop` | `label in 020B/0265` | [Part 5] BP=BX, DX=CX, CF=0 |
| 12ef:0265 | func | `r3d_clip_intercept` | `near(AL=code,BX=x,CX=y)->BP=x,DX=y,CF` | [Part 5] x border first, imul/idiv (trunc) with 85AC/85AE, y border fallback; fail AL=AH=0Ah (x<xmin) or 05h (x>xmax) |
| 12ef:02d1 | func | `r3d_outcode16` | `near(BX=x CX=y)->AL` | [Part 5] 1 y<ymin 2 y>ymax 4 x<xmin 8 x>xmax, signed; overflow of x-xmin or y-ymin sets byte 85C0=1 |
| 12ef:0301 | label | `r3d_oc16_xovf` | `label in 02D1` | [Part 5] 85C0=1 then jl with the same flags |
| 12ef:0308 | label | `r3d_oc16_yovf` | `label in 02D1` | [Part 5] 85C0=1 then jl with the same flags |
| 12ef:030f | label | `r3d_clip_inside_bug` | `label in 02D1/0333` | [Part 5] rec+2=0 then NEAR ret from far 0333: latent bug, unreachable for windows within 0..7FFFh |
| 12ef:0315 | label | `r3d_clip_sx32` | `label` | [Part 5] re-sign-extend hi words of the 4 coords, jmp 0359 (32-bit path) |
| 12ef:0333 | func | `r3d_clip_edge` | `void far(SI=rec)` | [Part 5] s16 fast path 0465 (Cohen-Sutherland intercepts) or s32 path 0359 (bisection); writes clipped s16 coords, +2 flags, +6/+0E |
| 12ef:0359 | label | `r3d_clip_path32` | `label in 0333` | [Part 5] 32-bit out-codes, trivial accept(bug)/reject, bisection |
| 12ef:0447 | label | `r3d_clip_miss32` | `label in 0333` | [Part 5] bisect_in failed: flags 8A if last midpoint code&4 else 85 |
| 12ef:0457 | label | `r3d_clip_accept` | `label in 0333` | [Part 5] rec+2=0, retf |
| 12ef:045d | label | `r3d_clip_reject` | `label in 0333` | [Part 5] rec+2=8080h, retf |
| 12ef:0465 | label | `r3d_clip_fast16` | `label in 0333` | [Part 5] 16-bit fast path |
| 12ef:04ef | func | `r3d_bisect_in` | `near(SI=P0,DI=P1)->CF,DX:AX,CX:BX` | [Part 5] <=32 floor midpoints to find an inside point; fail if midpoint shares codes with both 85A8 and 85AA |
| 12ef:0553 | label | `r3d_half_x_quirk` | `label in 056B` | [Part 5] x sum with lo==hi: FFFF:FFFF->0 else kept unhalved |
| 12ef:055f | label | `r3d_half_y_quirk` | `label in 056B` | [Part 5] same for y |
| 12ef:056b | func | `r3d_bisect_out` | `near(SI=in,DI=out)->DX:AX,CX:BX` | [Part 5] <=32 midpoints toward the border; returns first inside midpoint on any border, else last inside point |
| 12ef:05d9 | func | `r3d_clip_flags32` | `near(BP=c0,DI=c1,SI=rec)->AL=AH` | [Part 5] rec+6/+0E clamped y (ymin wins), side bits |
| 12ef:063e | func | `r3d_on_border` | `near(DX:AX,CX:BX)->ZF` | [Part 5] ZF=0 iff hi words 0 and x==xmin/xmax or y==ymin/ymax |
| 12ef:0665 | func | `r3d_outcode32` | `near(DX:AX=x,CX:BX=y)->BP,ZF` | [Part 5] hi word sign/nonzero, else unsigned lo compare; x<xmin excludes the xmax test |
| 1359:0000 | func | `gfxlib_mode_init` | `void far(int16 mode_char)` | [Part 5] uppercase (>='a' -20h) to 85C4; E/M/C/T row init; main passes 'M' |
| 1359:0037 | label | `gfxlib_dispatch_pixel` | `label` | [Part 5] call cs:[[85C2]+2]; no callers; inconsistent with far pixel routines |
| 1359:0040 | func | `gfxlib_dispatch_a` | `void far(AX=colour)` | [Part 5] call cs:[[85C2]+4] (M: 01D4 set colour) |
| 1359:0049 | func | `gfxlib_dispatch_b` | `void far(SI=rec)` | [Part 5] call cs:[[85C2]+6] (M: 045D line) |
| 1359:0052 | func | `gfxlib_dispatch_c` | `void far()` | [Part 5] call cs:[[85C2]+8] (M: 0AF7 fill) |
| 1359:005b | label | `gfxlib_dispatch_fill2` | `label` | [Part 5] call cs:[[85C2]+0Ah]; no callers |
| 1359:0064 | label | `gfxlib_mode_tables` | `data in CS` | [Part 5] 4 x 6 near words: E 0064, M 0070, C 007C, T 0088 (init,pixel,colour,line,fill,fill2) |
| 1359:0094 | func | `gfxlib_alloc_dead` | `uint16 far(uint16 paras)` | [Part 5] INT 21/48 query, inverted 400h-reserve test; never called |
| 1359:00b6 | label | `gfxlib_rows_vga480` | `label in 00E7` | [Part 5] [913E]!=0: 480 rows x 50h, 9142=4Fh, 9144=-51h; 913E never written: dead |
| 1359:00e7 | func | `gfxlib_rows_ega` | `void near()` | [Part 5] EGA row table 200 x 28h, 85C2=0064; dead |
| 1359:010a | func | `gfxlib_rows_mcga` | `void near()` | [Part 5] DS:85EC[i]=i*320 for i<200; 85C2=0070 (the only mode used) |
| 1359:0126 | func | `gfxlib_rows_cga` | `void near()` | [Part 5] CGA interleaved rows, 85C2=007C; dead |
| 1359:0149 | func | `gfxlib_rows_tandy` | `void near()` | [Part 5] Tandy 4-bank rows, 85C2=0088; dead |
| 1359:0174 | label | `gfxlib_ega_reset` | `label` | [Part 5] if 85C4=='E' reset sequencer/GC registers; no callers |
| 1359:019b | func | `r3d_set_page` | `void far(int16 page)` | [Part 5] 85C6=page; 85EA=DS:85E2[page]; no callers |
| 1359:01b1 | func | `r3d_setcolor_E` | `void near(AX)` | [Part 5] EGA colour (GC 3CEh from DS:9168); dead |
| 1359:01d4 | label | `r3d_setcolor_M` | `void near(AX=colour word)` | [Part 5] 85D6=ax; if ah==FF and al>=A0: al=max_u8((al-[4894])&FF, al&F0); ES=AX=[85EA] |
| 1359:01f8 | func | `r3d_setcolor_C` | `void near(AX)` | [Part 5] CGA colour pattern 914A/914C; dead |
| 1359:021d | func | `r3d_setcolor_T` | `void near(AX)` | [Part 5] Tandy colour pattern 914E; dead |
| 1359:023a | func | `r3d_pixel_E` | `void far(x,y,c)` | [Part 5] EGA window-clipped pixel; dead |
| 1359:027b | func | `r3d_pixel_M` | `void far(x,y,c)` | [Part 5] window-clipped pixel; BUG row offset read from DS:[2y] not 85EC; no callers |
| 1359:02b6 | func | `r3d_pixel_C` | `void far(x,y,c)` | [Part 5] CGA pixel (same DS:[2y] bug); dead |
| 1359:0301 | func | `r3d_pixel_T` | `void far(x,y,c)` | [Part 5] Tandy pixel (same DS:[2y] bug); dead |
| 1359:0344 | func | `r3d_ega_addr` | `void near()` | [Part 5] EGA addr from 9132/9134, bit mask DS:9150; dead |
| 1359:0369 | label | `r3d_line_E` | `void near(SI=rec)` | [Part 5] EGA line; dead |
| 1359:045d | label | `r3d_line_M` | `void near(SI=rec)` | [Part 5] Bresenham, both ends, err=-(n>>1), n=major+1, step when err>=0; no clipping; colour byte 85D6 |
| 1359:04c5 | label | `r3d_line_M_ymajor` | `label in 045D` | [Part 5] y-major half of line_M |
| 1359:0505 | label | `r3d_line_C` | `void near(SI=rec)` | [Part 5] CGA line; dead |
| 1359:0607 | label | `r3d_line_T` | `void near(SI=rec)` | [Part 5] Tandy line; dead |
| 1359:0719 | func | `r3d_edge_dda` | `void far(SI=rec)` | [Part 5] edge into L=89AC/R=8D6E per row; rejects if an end y is outside the window; x-major runs L=first R=last; 9130=min row |
| 1359:07d6 | func | `r3d_wall_fill` | `void far(int16 a,int16 b,int16 x)` | [Part 5] rows sort(a,b) clamped: drop if hi<=ymin, lo>=ymax or single row; L=min(L,x), R=max(R,x) |
| 1359:0834 | label | `r3d_fill_E2` | `void near()` | [Part 5] EGA fill variant; dead |
| 1359:0874 | label | `r3d_fill_E` | `void near()` | [Part 5] EGA fill; dead |
| 1359:0959 | label | `r3d_fill_none` | `label` | [Part 5] fill type FB: reset tables only |
| 1359:098d | label | `r3d_fill_stipple` | `label` | [Part 5] fill type FC: colour lo where (x^y)&1 |
| 1359:09f7 | label | `r3d_fill_and` | `label` | [Part 5] fill type FE: pixel &= lo |
| 1359:0a77 | label | `r3d_fill_or` | `label` | [Part 5] fill type FD: pixel /= lo |
| 1359:0af7 | label | `r3d_fill_M` | `void near()` | [Part 5] spans from row 9130 until L==7FFF, clamp to [xmin,xmax], reset 7FFF/8001; dispatch on 85D6 hi |
| 1359:0b29 | label | `r3d_fill_dither` | `label` | [Part 5] hi<FB (or FF: hi=lo): pixel = (x^y)&1 ? hi : lo; 9140 = colours |
| 1359:0bb3 | label | `r3d_fill_C` | `void near()` | [Part 5] CGA fill; dead |
| 1359:0ca1 | label | `r3d_fill_T` | `void near()` | [Part 5] Tandy fill; dead |
| 1433:0004 | func | `math_sin` | `int16 far math_sin(uint16 a)` | [Part 6] TS[(a>>3)&7FF] quadrant-reflected, sign by bit 15; no interpolation |
| 1433:0031 | func | `math_cos` | `int16 far math_cos(uint16 a)` | [Part 6] exactly math_sin(a+4000h) |
| 1433:0061 | func | `math_atan2` | `uint16 far math_atan2(int16 y,int16 x)` | [Part 6] octant base DS:9178 +- TA[floor(Q16 ratio)>>4]; equal or (0,0) -> TA[4095]=8190 |
| 1433:00d9 | func | `math_mul_sin` | `int16 far math_mul_sin(int16 k,uint16 a)` | [Part 6] dead: q15(k,sin a); the or dl,1 is a no-op |
| 1433:011a | func | `math_mul_cos` | `int16 far math_mul_cos(int16 k,uint16 a)` | [Part 6] dead: q15(k,cos a) |
| 1433:015e | func | `math_asin` | `int16 far math_asin(int16 x)` | [Part 6] TQ[(/x/>>5)&3FF] signed; asin(-32768)=0 quirk |
| 1433:018e | func | `math_hypot2` | `uint16 far math_hypot2(int16 a,int16 b)` | [Part 6] L + (TH[hi byte of S*65536/L]*L>>16); equal -> TH[256] |
| 1433:01e0 | func | `math_hypot3` | `int16 far math_hypot3(int16 x,int16 y,int16 z)` | [Part 6] dead: hypot2(hypot2(x,y),z); if h>=8000h h+(/z/>>1) |
| 1433:0219 | func | `mat_vec16` | `void far mat_vec16(int16 v[3],const int16 M[9])` | [Part 6] in place v'k = (sum of hi words of v_j*M[3j+k]) << 1 |
| 1433:0286 | func | `mat_mul` | `void far mat_mul(const int16 A[9],const int16 B[9],int16 C[9])` | [Part 6] C=A*B: 32-bit sums >>15 floor, element order stores (aliasing exact) |
| 1433:02aa | func | `mat_row_mul` | `void near mat_row_mul(SI=A row,BX=B,DI=C row)` | [Part 6] one row of mat_mul; scratch [916A]; clobbers BP |
| 1433:02e2 | func | `mat_transpose` | `void far mat_transpose(const int16 src[9],int16 dst[9])` | [Part 6] sequential: dst[0,3,6,1,4,7,2,5,8]=src[0..8] |
| 1433:0314 | func | `mat_copy` | `void far mat_copy(const int16 src[9],int16 dst[9])` | [Part 6] rep movsw 9 words |
| 1433:032e | func | `vec_mat32` | `void far vec_mat32(const int16 v[3],const int16 M[9],int32 out[3])` | [Part 6] out_k = sum v_j*M[3j+k] 32-bit unshifted |
| 1433:03a9 | func | `mat_from_euler` | `void far mat_from_euler(uint16 a,uint16 b,uint16 c,int16 out[9])` | [Part 6] all products separate q15 (nested for triples), 16-bit sums; caches sin/cos in 916C..9176 |
| 1433:04d7 | func | `mat_to_euler` | `void far mat_to_euler(const int16 M[9],uint16*a,uint16*b,uint16*c)` | [Part 6] dead: b=asin(M5); /b/>3E80h: a=atan2(M6,M0),c=0 else a=atan2(-M2,M8),c=atan2(-M3,M4) |
| 1433:0542 | func | `math_mulq` | `int16 far math_mulq(int16 a,int16 b)` | [Part 6] dead: q15(a,b), DX=cwd |
| 1433:0554 | func | `math_muldiv` | `int16 far math_muldiv(int16 a,int16 b,int16 c)` | [Part 6] dead: /b/>=/c/ ? (b/c)*a : a*b/c; c=0 -> 1 |
| 1433:058f | func | `math_div32sat` | `int16 far math_div32sat(uint16 lo,int16 hi,int16 d)` | [Part 6] dead: (hi:lo)//d/, saturate +-7FFFh by sign(hi) if 2(/hi/+1)>/d/; guard wraps for hi=7FFFh/8000h |
| 1433:05c6 | func | `math_imul` | `int32 far math_imul(int16 a,int16 b)` | [Part 6] dead: DX:AX=a*b |
| 1433:05d1 | func | `mat_from_euler_b` | `void far mat_from_euler_b(uint16 p6,uint16 p8,uint16 pA,int16 out[9])` | [Part 6] dead: other Euler order; really ends at 06FC (index size short) |
