# S3.6 spec, reader R11b: the 3D model renderer, polygon clipper, raster back end and fixed-point math (far segments 11EB, 12EF, 1359, 1433)

Scope: 86 functions in four far code segments of VGAME.EXE (77 listed in `scope_R11b.txt`; the other 9 of segment 1359 -
`0000 0040 0049 0052 0094 00E7 010A 0126 0149` - are R1's and are only mentioned). Owner = render3d (11EB, 12EF, 1359) and lib
(1433, a shared math library). INDEX LEVEL for S3.7 (rendering internals) - but everything below was read from the disassembly,
so a porter can already implement the whole object pipeline from it. `(V)` = verified in the disassembly, `(U)` = inferred / needs
a runtime or visual check. Addresses `SSSS:OOOO` are far, image offset = SSSS*16+OOOO (the .EXE header is 0x820 bytes when you
use a hex editor on the file). DGROUP = 1E23.

IMPORTANT CORRECTION to the task text: this renderer does **not** use MGRAPHIC slots 37/40 (`poly_fill`). It has its own complete
scan-converter (segment 1359, "mode M" = VGA mode 13h) with its own edge tables at DS:89AC/8D6E. No `lcall 0x1e23/0x1d50` appears
anywhere in the four segments (V, grep). The only MGRAPHIC interaction is indirect: `db71` (viewport setup, R11a) stores
the window/centre/page that this code reads. Sorting between objects is NOT here either (depth queue = `0000:DDFE`, R11a); only the
in-model BSP order (`11EB:0E5F`) is.

## 1. Pipeline overview (one object, one call of `11EB:00D3`)

```
sim side (df08 world objects / e7ac terrain scenery / 13e3 / 0ffb)
   DS:8560 <- segment of the 3DX bank (DS:B75E + 2*bank)
   mat9 (DS:49A0)  <- 1433:03A9 mat_from_euler(-a1,-a2,+a3) then transposed by 0000:E54B
   11EB:00D3 r3d_draw_object(obj, x, y, z, mat9, vars)           (vars = DS:C02E)
      |  0281 hook INT 0 (divide overflow -> 0975)                 ; restored at the end by 02A2
      |  1433:032E  cam-space position = (x,y,z)*mat9              -> DS:7CA2 (3 x s32)
      |  LOD select: first LOD entry whose maxdepth >= |zhi|        (model offset)
      |  02B6 frustum/sphere cull  (radius, far limit DS:7D2E)     ; fail -> return
      |  031B build per-part matrices (1433:0286 mat_mul, 03A9 euler, 02E2 transpose, 0219 mat*vec16)
      |  0565 PLANES : per plane  n.eye+d >= 0  -> DS:76D2[plane] (FF visible / 7F var-hidden / 00 back-facing)
      |                          shade byte DS:77D2[plane]  (0678, apparently unused)
      |                          mark needed vertices DS:79D2[v]=1 ; BSP walk 0E5F -> draw order DS:78D2
      |  06B9 VERTICES: for needed vertices   v*M_part + part origin  -> DS:C688 (s32 x,y,z per vertex)
      |                 0906 project  x/z, y/z                       -> DS:D688 (s32 x,y; x_hi==8000h => behind camera)
      |  0A37 EDGES   : for each edge (v0,v1) both needed -> 0B3F builds 24-byte edge record at DS:5EBA+24*i
      |                 (adds screen centre DS:85D2/85D4; if one end is behind the camera: 0990 bisection to z~1)
      |                 12EF:0333 clips the edge to the window (DS:85CA..85D0) -> flags in rec+2
      |  0C7A FACE LISTS: planes in file order, or BSP order, or the single list of a plane-less model
      |       0CAC visible plane -> 0CED face-list interpreter:
      |          polygon : for each edge  12EF:003E (-> 1359:0719 per-scanline min/max DDA into the L/R tables)
      |                    then colour byte -> word via table DS:[7D50] -> 12EF:010A poly_finish
      |                         -> 1359:0040 set colour, wall fills 12EF:0185/019B (-> 1359:07D6), 1359:0052 = fill (mode M: 1359:0AF7)
      |          lines   : 1359:0040 colour, then per edge 1359:0049 (mode M: 1359:045D Bresenham)
      |          points  : computed, never drawn
      v  pixels in the page segment DS:85EA (A000h or an off-screen page)
```

Calling relationships (V, from the function index): `00D3` is called by 13E3, DF08, E7AC; `01B8` by 0FFB; `000C` by DC64;
`0F9B` by DAE8; `1433:03A9` by 0FFB, 13E3, DC64, DF08, E7AC; `0EF2`, `12EF:0002`, `12EF:01B1`, `1359:019B`, `1359:023A..0301` have **no**
direct callers in the image (public API of the module that the game does not use; (U) except via data).

## 2. Conventions used by all four segments

* Far calls (`lcall seg,off`), C calling convention: args pushed right to left, first argument at `[bp+6]` (return CS:IP + saved BP),
  caller cleans (`add sp,N`). Helpers called with `call` (near) inside a segment use registers (listed per function).
* Angles: 16-bit, 65536 = 360 degrees, 4000h = 90 degrees. Positive = counter-clockwise in `atan2(y,x)` terms (V, see 1433:0061).
* Matrices: 3x3 of s16, row-major, 9 words = 18 bytes (0x12), **Q15** (7FFFh ~ 1.0; products `(a*b)>>15`). Vectors are
  row vectors: `out_k = sum_j v_j * M[j*3+k]`. So `view = model * ObjMat * CamMat`.
* "s32 pair": lo word at +0, hi word at +2 (little endian). The hi word is the integer part for the projection (z "hi" is what is compared with LOD depths).
* Coordinates in model space: x east, y up, z south (FORMATS.md); the sim passes world offsets as (east, up, -north).
* The camera matrix is stored with scale folded in (11EB:000C): x/y columns halved, z column scaled by the zoom, y column additionally x0.75 (flag DS:7D3A, always 0 => see 11EB:000C).
* No clipping to a 3D frustum except the coarse sphere test 02B6; real clipping is 2D (12EF) plus the near-plane bisection (0990).

## 3. Input of the renderer: what the sim must provide

### 3.1 Call interface

```c
void far r3d_draw_object(uint16 obj,        // object index inside the bank (low 8 bits of obj record type), must be < word count at bank:0000
                         int16 x, int16 y, int16 z, // object origin relative to the camera, WORLD axes: x = east, y = up, z = south(-north)
                         const int16 *mat9, // DS ptr: object orientation matrix (Q15, 9 words). Callers use the transpose of 1433:03A9(-a1,-a2,+a3)
                         const int16 *vars);// DS ptr: the "var table" DS:C02E, 32 words (see 3.3)
```
Prerequisites in DGROUP (all set by the callers / view setup `dae8`, R11a/R10):

| global | set by | meaning |
|---|---|---|
| DS:8560 | caller (`mov ax,[bx-48A2h]` = `DS:B75E+2*bank`; 13E3 uses `[B760]`; DAE8 sets `[B760]` for the shared-vertex call) | segment of the 3DX bank file (see FORMATS.md: word count, offset table) |
| DS:7AF8..7B09 | `11EB:000C` via `DC64` | camera matrix (scaled) |
| DS:7D2E | `11EB:000C` | far limit = 7FFF00h / max(|2*zoom|,100h) |
| DS:7D50 | 0000:03CE, 0000:BF71 | pointer to the colour table (colour byte -> fill word): 7D60 / 7F60 / 8160 / 8360 (FORMATS.md) |
| DS:4894 | DF08/E7AC/119C | shade/brightness offset subtracted from ramp colours (colour lo >= A0h) by `1359:01D4` |
| DS:85CA,85CC,85CE,85D0 | `0000:DB71` | clip window xmin,ymin,xmax,ymax (inclusive, from rect +12h,+0Eh,+14h,+10h) |
| DS:85D2,85D4 | `0000:DB71` | projection centre x,y = xmin+((w+1)/2-1), ymin+((h+1)/2-1)  (defaults 0A0h,64h); special case in DB71 when ymin==0 |
| DS:85EA | `0000:DB71` | draw segment (page) = `[85E2 + 2*page]` |
| DS:7D54,7D56,7D58 | `0000:DB71` | light direction (-36,+36,+36) (V) |
| DS:7D5E | `0000:119C/11F2` | 0 = perspective; nonzero = "map mode" orthographic projection (see 0906) and no culling |
| DS:7D3A | never written (0) | y-axis scale switch of `000C`: 0 -> y column x(-0.75) (V, constant 0 in the image) |
| DS:C686 | cleared by `DAE8`, `2F82`, ... | bit 7 set by 00D3/01B8 (see 3.4) |

### 3.2 What the model data must look like (read order in `00D3`)

`00D3` reads exactly the 3DX layout of FORMATS.md; the order in which it consumes the stream (V):

1. `es:0000` word = number of objects; the call is ignored if `obj >= that` (`cmp ax,[bp+6]; ja`).
2. `es:[2*obj+2]` = object offset; LOD list = pairs `(maxdepth,model offset)`; loop `do { dx=|zhi| ; ax=pair.maxdepth ; si+=4 } while (|zhi| > maxdepth)` (unsigned compare, last entry has 8001h so it always ends). `si = model offset`.
3. Model header: `radius` (word, used only by the cull `02B6` and the "camera inside" test), `nparts` (word; `31B`), then the `(var,axis)` list of `nparts-1` entries, then `nparts-1` pivots of 3 words each (also consumed by `31B`).
4. `nverts` (word -> DS:7D2C, signed: negative = |n| byte indices into the shared table DS:4EB6), `nplanes` (word -> DS:7D2A, signed, negative = BSP).
5. Planes: per plane (prefix words `80pp` / `8100 var val` allowed) `nx,ny,nz` s16 + `d` s32 (14 bytes of payload); then per plane `n` + n vertex indices; then (BSP) root byte + `|nplanes|` `(front,back)` byte pairs.
6. Vertices (nverts x 3 words, prefix words 8000h..800Fh select the part whose matrix/origin is used from then on); `nedges` word + `(v0,v1)` byte pairs; `|nplanes|` face-list offsets (absent if nplanes==0: one list follows); face lists.

### 3.3 The var table DS:C02E (32 words)

Index `v` -> DS:C02E+2v. Used by (a) rotating parts: `var` selects an angle word (16-bit angle units), (b) conditional planes `8100 v val` and conditional face elements `80|v val` (compared as signed value with the word). Writers seen by the cross-reference (R10/R11a own the semantics): v1 = DS:C030 (destroyed; 03CE, DF08, E7AC), v5..v9 = C038,C03A,C03C,C03E,C040 (DF08, C73C; DF08 sets C038 =1 and C03A = [C072]-1 for object type 18h, C03C = 1 for ...), v10..v14 = C042,C044,C046,C048,C04A (03CE/04DA/8387/DF08), v15 = C04C, v16 = C04E (lights/night, DB3E). FORMATS.md: "Vars seen: 1 = destroyed, 10 = lights (night)" (note: FORMATS numbering is the same: var 10h = DS:C04E).

### 3.4 Output of the renderer

* Pixels in the page segment DS:85EA. `00D3` returns nothing in registers.
* DS:7D4E = pointer to the last *line* edge record drawn (used by DF08 for object type 25h to read the on-screen endpoints: `[bx]` x, `[bx+4]` y of both ends).
* DS:C686 bit 7 (`or word [C686],80h`): "the probe point (= the camera of this view, or the query point of `0000:77D9`) is INSIDE a solid model": set when the point is within the bounding radius of the model (`radius > |zhi|`, so 7D5C was not pre-set) and NO plane is front-facing from it (all planes back-facing => inside a closed hull; models with fewer than 4 planes set 7D5C=80h and never raise the flag). Cleared by `DAE8`, `2F82` (before the view, `[C686]=0` at 36AB; copied to DS:9F66 after drawing, read by 39C4 line 41C2: player camera inside an object), `0000:671A` (before the ground/scenery query `77D9` of a moving object, then `C686 != 0` => impact). `01B8` is the draw-less version used by `0FFB` (called from `77D9`) to implement that point-in-object test.

## 4. Working buffers in DGROUP (all addresses are DS offsets)

| range | size | content | users |
|---|---|---|---|
| 4EB6..5EB5 | 0x1000 | shared vertex table (`0F9B`), 16 bytes (3 x s32 + pad) each, <= 256 | 0F9B writes, 06B9 path "nverts<0" reads |
| 5EB6,5EB8 | 4 | saved INT 0 vector (offset, segment) | 0281 / 02A2 |
| 5EBA..7AF7 | 0x213E | edge records, 24 (0x18) bytes each, index = edge index, <= 354 | 0A37/0B3F write, 12EF reads, 0CED indexes (`idx*24`) |
| 76D2..77D1 | 256 | plane visibility byte (FF visible / 7F hidden by var / 00 back-facing) | 0565 write, 0CAC, 0E5F read |
| 77D2..78D1 | 256 | plane shade byte `(n.L)>>14` | 0678 write; 0CAC copies to 7D5A; no reader found => dead (U) |
| 78D2..79D1 | 256 | BSP draw order (plane indices, FFh-terminated) | 0E5F write, 0C7A read |
| 79D2..7AD1 | 256 | vertex "needed" byte (1 if the vertex belongs to a front-facing plane) | 0565 write, 06B9 / 0A37 read |
| 7AF8 | 18 | camera matrix (scaled) | 000C write; 031B,0F9B read |
| 7B0A | 18 | transpose of the *unscaled* camera matrix (never read, (U) dead) | 000C |
| 7B1C | 0x24 x (nparts+1) | per-part matrix slots: +0 `M_part` (model->view, 9 words), +12h its pivot/object-orientation transpose (for eye/light vectors); one extra slot used as temp. Room for <= 7 parts before overlapping 7C3C (U) | 031B |
| 7C3C | 6 x 8 | eye (camera) position in each part's model frame (3 words/part) | 031B/0440 write, 0565 read |
| 7C6C | 6 x 8 | light vector in each part's frame | 04CD/04F9 write, 0678 read |
| 7C9C | 6 | object offset (x,y,z s16) relative to camera, world axes | 00D3 |
| 7CA2..7CAD | 12 | object origin in view space (x,y,z as s32 pairs); 7CAC = z hi used by LOD and cull | 00D3 via 032E |
| 7CAE.. | 12 x 8 | origin of each part in view space (s32 x3) | 031B/0440 write, 06B9 read |
| 7D0E..7D19 | 12 | temp pivot vector (s32 x3) | 0440 |
| 7D1A,7D1C,7D1E,7D20,7D26,7D28,7D2A,7D2C,7D2E | words | scratch ptrs/counters: 7D1A current 3D vertex ptr; 7D1C object matrix ptr; 7D1E var table ptr; 7D20 current part-matrix slot; 7D26 current part-origin ptr; 7D28 nparts; 7D2A nplanes; 7D2C nverts; 7D2E far limit | |
| 7D38 | byte | per-plane "hidden by var" mask: FFh, set to 7Fh by a failed `8100` test, reset for every plane | 0565 |
| 7D3A | word | y scale switch (0) | 000C |
| 7D3E,7D40 | words | current polygon: edge count, start ptr | 0CED |
| 7D42..7D49 | 8 | bisection midpoint x lo,hi / y lo,hi | 0990 |
| 7D4A | word | BSP: visibility array ptr | 0E5F |
| 7D4C | word | current light-vector slot ptr (7C6C+6*part) | 0565/04CD/04F9/0678 |
| 7D4E | word | last line edge record drawn | 0CED |
| 7D50 | word | colour table base | 03CE/BF71 write, 0CED read |
| 7D54 | 6 | light vector | DB71 |
| 7D5A | byte | last drawn plane's shade | 0CAC (write only) |
| 7D5C | byte | working "visible/inside" flags (bit 7) | 00D3/01B8/0565 |
| 7D5E | word | orthographic (map) mode | 119C/11F2 write |
| 7D60..8560 | 4 x 0x200 | colour tables | |
| 8568..8587 | 32 | scratch copy of an edge's endpoints for the bisection clipper | 12EF:0333 |
| 8588..8597 | 16 | up to 2 saved near-plane clip points (4 words each) | 12EF:00E0 |
| 85A8,85AA,85AC,85AE,85B0 | words | clip scratch: out-code masks of the two ends, dx, dy of the edge, bisection counter (starts at 32) | 12EF |
| 85B2 | word | OR of all edge flags of the polygon being built | 12EF:003E |
| 85B4,85B6 | words | left-wall y range (min,max), reset to 7FFFh,8000h... | 12EF |
| 85B8,85BA | words | right-wall y range | 12EF |
| 85BC | word | count/flag: at least one edge was rasterised (set to 4) | |
| 85BE | word | number of saved near-plane clip points (0..2) | |
| 85C0 | byte | overflow seen in the 16-bit out-code compare | 12EF:02D1 |
| 85C2 | word | mode dispatch table offset (cs-relative): 0064 E, 0070 M, 007C C, 0088 T | 1359:0000.. |
| 85C4 | word | upper-cased mode char ('M' passed by `0010`) | |
| 85C6 | word | page number | 1359:019B |
| 85CA..85D0 | 4 words | clip window xmin,ymin,xmax,ymax | DB71 |
| 85D2,85D4 | words | projection centre | DB71 |
| 85D6 | word | current fill colour word (hi = fill mode, lo = colour) | 1359:01D4 |
| 85E2,85E4,85E6 | words | page segments (A000h, page 1, page 2) | (R-other) |
| 85EA | word | current draw segment | DB71 / 1359:019B |
| 85EC | 480 x 2 | row start offset table: mode M `y*320` (200 used) | 1359:010A |
| 89AC | 480 x 2 | LEFT edge table (min x per scanline), idle value 7FFFh | 1359:0719 |
| 8D6E | 480 x 2 | RIGHT edge table (max x per scanline), idle value 8001h | |
| 9130 | word | min scanline touched (7FFFh = nothing) | |
| 9132,9134 | words | pixel scratch (EGA mode) | |
| 9140 | word | fill colour word copy | 1359:0AF7 |
| 9150.. | | EGA/CGA bit mask tables (not used in mode M) | |
| 9168 | 16 words | EGA/CGA colour plane table (zero in the image: those modes are dead) | |
| 9170..9177 | | scratch: sin/cos of the 3 Euler angles (916C,916E,9170,9172,9174,9176) | 1433 |
| 9178 | 8 words | atan2 octant base angles `0000,4080,0080,C000,8080,4000,8000,C080` (low byte 80h = subtract) (V) | 1433:0061 |
| C688..D687 | 0x1000 | transformed (view-space) vertices, 16 bytes each (x s32, y s32, z s32, pad), <= 256 (also used as a read buffer by loader 11CE) | 06B9 |
| D688..DE87 | 0x800 | projected vertices, 8 bytes each (x s32, y s32; x hi == 8000h marks "behind camera") | 06B9 |
| seg 14B2 | 0x3A02 | math tables, see 1433 | |

### 4.1 Edge record (24 bytes at DS:5EBA + 24*i), built by 11EB:0B3F

Before clipping: `+0 x0 (s32)`, `+4 y0 (s32)`, `+8 x1 (s32)`, `+0C y1 (s32)` (screen coordinates incl. centre offset); `+10h x', +14h y'` (s32 pair)
only for edges with one end behind the camera = the intersection point with the near plane, used to close the polygon.
After `12EF:0333`: `+0 x0, +4 y0, +8 x1, +0C y1` are s16 (clipped to the window), `+2` = flag word (low byte = high byte), `+6` = y0 clamped to the window
(before clipping) and `+0E` = y1 clamped, `+0Ah,+0Eh,..` as above. Flag bits (V from 12EF:020B/0333/003E):

| bit | meaning |
|---|---|
| 0 | end 0 was to the right of xmax |
| 1 | end 0 was left of xmin |
| 2 | end 1 right of xmax |
| 3 | end 1 left of xmin |
| 6 (40h) | one end was behind the camera; +10h..+16h hold the near-plane point |
| 7 (80h) | edge rejected (outside, or both behind). 85h / 8Ah = rejected but both ends right / left (used for the vertical walls); 80h plain |

`flags == 0`: edge completely inside the window.

## 5. Segment 11EB - object transform / projection / face lists

### 11EB:000C  `void r3d_set_camera(const int16 *mat9, int16 zoom)`   (V)
Called only by DC64 (camera setup of `dae8`). Arguments `[bp+6]` matrix ptr (DS), `[bp+8]` zoom (0x100 = 1.0; used as `scale`).
1. `k = |2*zoom|` clamped to >= 100h; `DS:7D2E = 7FFF00h / k` (16-bit result).
2. copy `mat9` -> DS:7AF8 (1433:0314), transpose 7AF8 -> DS:7B0A (1433:02E2) (the transpose is never read, (U)).
3. column 2 (elements `+4,+0A,+10`) `= -((7D2E * m) >> 15)`; columns 0 and 1 `>> 1`; column 1 (`+2,+8,+0E`): if DS:7D3A != 0 it is negated, else `-m - (-m>>2)` (= -0.75 m, in original units -0.375). Net effect: x = m/2, y = -0.375 m (or -m/2), z = -(zoom factor).
Zoom 100h makes the z column equal to the x column (neutral); larger zoom compresses depth => magnifies. Far limit follows: visible if `2*(|zhi|+1) < 7D2E`.
Globals: writes 7AF8..7B1B, 7D2E; reads 7D3A.

### 11EB:00D3  `void far r3d_draw_object(obj, x, y, z, mat9, vars)`   (V)  owner render3d
See 3.1. Algorithm:
1. `7D5C=0`. `es=[8560]`; return if `obj >= es:[0]`.
2. `[7C9C]=x,[7C9E]=y,[7CA0]=z; [7D1C]=mat9; [7D1E]=vars`. `1433:032E([7C9C], 7AF8, 7CA2)` => view-space origin (3 x s32) (position *   camera matrix; the object matrix is not involved).
3. LOD: as 3.2 (uses |hi word of z| = DS:7CAC). Read `radius`.
4. `if (radius <= |zhi|) 7D5C = 80h` (far objects never set the "inside" flag).
5. If `7D5E==0` and `02B6(radius)` fails (ZF clear): `7D5C=80h` and skip all drawing.
6. Else: `cx=nparts; 031B(cx)`; `[7D2C]=nverts` (read from the stream); `0565` planes; if `|[7D2A]| < 4` then `7D5C=80h`; then `0281` hook INT 0; `06B9` vertices+projection; `0A37` edges; `0C7A` faces; `02A2` unhook.
7. `bx = (7D5C & 80h) ^ 80h; [C686] |= bx`.
The INT 0 hook (`0281` / handler `0975`) is installed per object (writes the real-mode vector at 0000:0000) and restored at the end; an early-out path (steps 5) returns without ever installing it.
Callers: 13E3 (bank 1 object, identity matrix: 03A9(0,0,0)+E54B; args `(obj&FF, x=[bp+6], 0, -[bp+8], 49A0, C02E)`), DF08 (world object list), E7AC (terrain scenery).

### 11EB:01B8  `void far r3d_probe_object(obj, x, y, z, mat9)`   (V)
Same prologue as 00D3 (no vars pointer: 7D1E is stale), LOD select, radius, cull `02B6`, `031B`, `0565`; no `06B9..0C7A`. Sets `[C686] bit 7` by the same rule as 00D3 (step 7). Used by `0FFB` (point/camera-in-object test for the ground query).

### 11EB:0281 `r3d_hook_div0` / 11EB:02A2 `r3d_unhook_div0`   (V)
Save/restore the real-mode INT 0 vector (0000:0000/0002) in DS:5EB6/5EB8 and install CS:0975 (cs = 11EB). `ret` near (called with `call`).

### 11EB:0975  `INT 0 handler r3d_div0_handler`   (V)
Divide-overflow recovery for the `idiv cx` + `cdq` pairs in 0906: advances the return IP by 3 (skips the 2-byte idiv and the cdq), recomputes the quotient from `(dx:ax >> 8) / cx` (24-bit arithmetic shift of the dividend, `idiv cx`) and returns it `<< 8` in dx:ax (`dl=ah,ah=al,al=0`, dx sign from `cdq`) - a saturating/less precise result instead of a crash. `iret`.

### 11EB:02B6  `bool r3d_frustum_test(radius in AX)` -> ZF=1 visible, AX=0; else AX=FFFFh   (V)
Inputs: AX = model radius, view-space origin 7CA2..7CAD, DS:7D2E.
`if ((|zhi|+1)*2 >= 7D2E) cull;  t = zhi + radius + zhi + carry(zlo)` i.e. `t = 2*zhi + radius` (bp:bx = z32 + (zhi+radius):0); `if t < 0 cull;` `|xhi| > t -> cull;` `|yhi| > t -> cull`. (x/y use the hi words of s32 x/y = 7CA4 / 7CA8.) => 90-degree-ish pyramid with the sphere radius added; x,y are already halved by 000C.

### 11EB:031B  `r3d_setup_parts` (CX = nparts; ES:SI after the header's first word)   (V)
Builds the per-part matrices, eye vectors, light vectors and part origins (see buffers). `7D28=cx`; `eye = -(7C9C,7C9E,7CA0)` stored at 7C3C.. as words; if `nparts<=0`: `7D20=7AF8` and return (matrix = camera only).
1. Slot 0: `mat_mul(ObjMat=[7D1C], Cam=7AF8, dest=7B1C)` (`1433:0286`) and `7B1C+12h = transpose(ObjMat)` (`1433:02E2`). `7D20=7B1C`.
2. For each further part (`nparts-1` times, reading `(var, axis)` byte pairs from ES:SI): `angle = vars[var]`, axis 0/1/2 selects which of the three Euler arguments gets the angle (others 0); build `PartRot = 1433:03A9(a0,a1,a2)` into the temp slot (`slot+24h`); then slot(k) `= PartRot * slot0(M)` and slot(k)+12h `= transpose(PartRot)`; `7D20 += 24h`.
3. `7D20=7B1C`; copy the object origin 7CA2..7CAD to 7CAE (part 0 origin); `7D26=7CAE`; eye vector rotated into the object frame: `1433:0219(7C3C, 7B2E)` (eye * ObjMat^T); light of the root via `04CD`.
4. For each further part: `0440` (pivot handling, below); eye of the part `= eye - pivot`, rotated by slot+12h (`0219`); light vector of the part via `04F9`; advance pointers (`7D26 += 0Ch`, eye ptr `bx += 6`, slot `di += 24h`).
Pivot order: all pivots (3 words each) follow the `(var,axis)` list in the model header.

### 11EB:0440  `r3d_part_pivot` (BX = eye slot, DI = slot+12h, ES:SI = pivot)   (V)
`eye_slot = pivot` (3 words); `7D0E..7D19 = pivot * slot0 matrix` (`1433:032E`); `eye_slot = -pivot`; `part origin (7D26, s32 x3) = (−pivot)*M_part (032E with slot DI-12h) + origin(7CA2) + 7D0E..`. Net: `origin_part = origin_obj + pivot*M_root - pivot*M_part` (rotation about the pivot). Advances nothing itself.

### 11EB:04CD `r3d_light_root` / 11EB:04F9 `r3d_light_part`   (V)
Copy the world light vector (DS:7D54, 3 words) to DS:7C6C (root; 04CD sets `7D4C=7C6C`) or `[7D4C]` (04F9) and rotate it with `1433:0219` by the matrix at DI (`7B2E` root / slot+12h part); then `7D4C += 6`.
Inside 04F9's range, `11EB:0521` and `11EB:0544` are *labels of the plane loop of 0565* (jumped to from there), not separate functions: `0521` is the `8100 var val` test (below), `0544` the `80pp` part switch.

### 11EB:0565  `r3d_planes` (ES:SI at nplanes word)   (V)
Reads `nplanes` -> `[7D2A]`; returns if 0. `cx=|nplanes|`; clears `di` (plane index), `bx = 7C3C` (eye of part 0), `7D4C = 7C6C`.
Loop head `0587`: `7D38 = FFh`. Next stream word `ax`:
* `ah == 80h` (`80pp`): switch part -> `bx = 7C3C + 6*pp` (eye), `7D4C = 7C6C + 6*pp` (light) (local label `0544`), then read the next word;
* `ax == 8100h`: `bl = es:[si]&1F` = var, `al = es:[si+1]` = value; if `vars[var] != (int8)value` `7D38 = 7Fh` (local label `0521`);
* otherwise `ax` is `nx` of a plane: `v = nx*eye.x + ny*eye.y + nz*eye.z + d` (32-bit, d = next 2 words); `dl = (v >= 0) ? FFh : 00h` (sign of the high word); if `7D5E != 0` then FFh (no culling in map mode); `dl &= 7D38`; `76D2[di] = dl`.
  If `dl` has bit 7 (front-facing): `7D5C |= dl` (also OR 80h if nx|nz|d==0, no-op), and `0678` computes the shade byte. `di++`; `loop 0587` (cx plane counter).
Then `0658` clears `79D2[0 .. |nverts|+1]` (word-wise); then for every plane (`cx = |nplanes|`, byte `n` + n vertex bytes): if `76D2[plane] > 7Fh` mark `79D2[v]=1` for its vertices else skip n bytes.
If `nplanes < 0` (BSP): `0E5F(si, di=78D2, bx=76D2)` generates the draw order, then `si += 2*|n|+1` (skips root byte + pairs).
Result: `si` = start of the vertex list; `7D2A/7D2C` set; arrays filled.

### 11EB:0658 `r3d_clear_vertex_flags`, 11EB:0678 `r3d_plane_shade`   (V)
0658: zero `((|[7D2C]|+2)>>1)` words at DS:79D2. 0678: `shade = (((nx*Lx + ny*Ly + nz*Lz) << 2) >> 16) & FF` using the light slot `[7D4C]`; stored at `77D2[di]`; skips the plane's `d` (si += 4). The shade is only copied to DS:7D5A and never read again (U: dead code or reserved for lighting); the visible brightness comes from `4894` instead.

### 11EB:06B9  `r3d_xform_vertices` (ES:SI at the vertex list)   (V)
`bx=[7D20]` (matrix slot), `cx=[7D2C]`: `cx==0` -> return; `cx>0`: `di=C688`, `bp=D688`, `7D1A=di`; if `[7D2A]==0` (no planes) transform ALL vertices (`0819` per vertex) with part switching handled inside `0819`; else (`72E`): `dx` = vertex index; word `8000..800F` => switch part: `bx = 7D20 + 36*pp`, `di`-ptr part origin `7CAE+12*pp` copied to `7CA2..7CAD`; skip (`si+=2`) and continue **without consuming a vertex count**; for each real vertex: if `79D2[dx]==0` skip (advance `bp+=8, di+=10h, si+=6`) else `0819`.
`cx<0` (`75E`): vertices come from the shared table `4EB6 + 16*idx` (byte indices) offset by the object origin (7CA2); used for no model in the shipped data.
Local labels in this function range: `06E6` (part switch in the plane path), `072E`, `0730` (loop), `0751` (skip), `075E`, `07CC` (part switch inside `0819`).

### 11EB:0819 `r3d_xform_one` (ES:SI vertex x,y,z s16; BX matrix; DI out 3D (s32 x3); BP out projected)   (V)
`out3d = (x*M[0]+y*M[3]+z*M[6], x*M[1]+y*M[4]+z*M[7], x*M[2]+y*M[5]+z*M[8])` (no shift, s32) `+ origin(7CA2..7CAD)` (add with carry); then `0906` with BX = BP (projected output); then `bp+=8; di+=16; si+=6`. If the word is 8000..800F it is a part switch (`07CC`): loads the part's matrix/origin and restarts (the vertex counter `cx` is untouched).

### 11EB:0906 `r3d_project` (DI = 3D vertex (s32 x,y,z), BX = out (s32 x,y))   (V)
* `7D5E==0` (perspective): let `zh = hi(z)`. `zh >= 100h`: `out.x = ((x>>8)) / zh`, `out.y = (y>>8)/zh` (24-bit dividends from bytes 1..3 of x,y; `idiv`, overflow handled by INT 0); `1 <= zh < 100h`: `out.x = (x>>1) / (z>>9)` (z middle word `[di+9]>>1`), same for y; `zh < 1`: `out.x_hi = 8000h` (behind camera marker, x/y otherwise undefined). Results sign-extended with `cdq` into s32.
* `7D5E != 0` (map): `cl=7D5E`; if `cl >= 10h`: `out = round((x_hi) >> (cl&0F))` (labels `08DA`/`08E5`), else `out = x_hi << (16-cl)` (`08BC`) - orthographic projection with power-of-two scale selected by 7D5E (only x,y; no z division).
Local labels: `08BC`, `08DA`, `08E5`.

### 11EB:0990  `void far r3d_near_bisect(p0, p1)` (stack: two 3D points as 6 words each)   (V)
Iteratively bisects the segment p0-p1 (each point `x lo,x hi,y lo,y hi,z lo,z hi`): `mid = (p0+p1+1)>>1` per component into 7D42..7D49 (x,y) and z (hi in cx). Stops when `zhi(mid)` is 0 or 1 (unsigned <=1); if `zhi-1 < 0` (mid behind) `p1 = mid` else `p0 = mid`. Results: DS:7D42 (x s32), DS:7D46 (y s32). The caller turns it into a screen coordinate by `(v>>8)*2 + centre`. Caller: `0A81`, `0BB6` through 0A37/0B3F.

### 11EB:0A37  `r3d_build_edges` (ES:SI at `nedges`)   (V)
`bp = 5EBA`; for each of `nedges` pairs `(v0,v1)`: if `nplanes != 0` skip when `79D2[v0] & 79D2[v1] == 0`; else `v*8`, `0B3F`; `bp += 18h` always (record index = edge index).

### 11EB:0B3F  `r3d_make_edge` (SI=BP=record, DI=v0*8, BX=v1*8)   (V)
Reads the projection markers `D68A[v0*8]`/`D68A[v1*8]` (x hi). Cases:
* both valid: `rec.p0 = proj[v0] + (85D2,85D4)`, `rec.p1 = proj[v1] + centre` (32-bit adds with carry; the centre is treated as unsigned), `12EF:0333` clip.
* v0 behind, v1 valid (`0BB6`): `rec.p1 = proj[v1]+centre`; near point from `0990`; `rec.p0 = rec[+10h] = ((mid.x>>8)*2) + 85D2, ((mid.y>>8)*2) + 85D4`; clip; `rec.flags |= 4040h`.
* v1 behind, v0 valid (`0A81`): mirror image; `rec.p1/+10h` is the near point.
* both behind (`0BAE`): `rec.flags = 8080h`.

### 11EB:0C7A / 0CAC / 0CED  face dispatch   (V)
* `0C7A r3d_draw_faces`: `cx=[7D2A]`; `cx>0`: for plane i = 0..cx-1 `0CAC(i)`; `cx==0`: one `0CED` (single list); `cx<0`: walk the byte list at DS:78D2 until FFh, `0CAC(idx)`.
* `0CAC r3d_draw_face_plane` (BX=plane): if `76D2[bx] > 7Fh`: `7D5A = 77D2[bx]`; `si = es:[si + 2*bx]` (face-list offset table), `0CED`; restores si/bx/di.
* `0CED r3d_face_list` (ES:SI at the list; loops until FFh): element byte `b`:
  * `FFh`: end. 
  * `b & 80h` (`0CCA`): conditional element: `v = b & 7F`; next byte `val`; if `vars[v] != (int8)val`: read next byte `l`; if `FFh` end of list else skip `(l & 1F)+1` bytes; continue.
  * `b & 20h` (`0DFE`, points): `n=b&1F`, a colour byte, n vertex indices: computes projected positions; nothing is drawn (dead).
  * `b & 40h` (lines, `0D5F`): `n=b&1F`, colour byte `c`; `AX = [7D50 + 2c]`; `1359:0040` (set colour); then n edge indices e: `si_e=5EBA+24e`, `7D4E=si_e`; if `rec.flags` bit 7 clear -> `1359:0049` (mode M: `045D` Bresenham with the edge record). (`0DAF` is unreachable code (U).)
  * else polygon (`b = n < 20h`): `7D3E=n`, `7D40=si`; if the byte after the n edge bytes is FFh skip the whole polygon; else for each edge index e: `12EF:003E(5EBA+24e)`; then the colour byte c: `ax=[7D50+2c]`; `12EF:010A(ax)`.
  Colour word semantic (hi byte = fill type FF solid / FB none / FC stipple / FD OR / FE AND / other dither; lo = colour) - FORMATS.md.

### 11EB:0E5F  `r3d_bsp_order` (ES:SI = BSP table, DI = out list, BX = visibility array)   (V)
Iterative traversal of the `(front,back)` byte-pair tree: stack of `(node,state)` words (push `cl` index + state `dl`). Node visible (`vis[n] != 0`, i.e. FFh or 7Fh): state 1: visit back child, emit the node index byte, visit front child; node not visible: front then back. Child byte FFh = none. A child byte with bit 7 set (not FFh) is emitted verbatim (U, never seen in data). Output terminated by FFh. Returns with `si` at the end of the pair table? No: `si` is restored by the caller (`push si ... pop si` around the call) and then advanced by `2*|nplanes|+1`.

### 11EB:0EF2  `void far r3d_object_extent(obj, int16 out[3])`   (V)  no direct callers
Zeroes `out[0..2]`; for the FIRST LOD model of `obj` (bank in DS:8560): reads header, calls `031B`(stale matrices; harmless) and `0565` just to skip the planes, then for every vertex (prefix words skipped) stores the maximum `|x|`, `|y|`, `|z|` into `out`. Result: bounding extents in raw model units.

### 11EB:0F9B  `r3d_shared_verts()`  (V)  caller DAE8
`es=[8560]`; `si = es:[es:[0]*2+2]` (= the "end of data" offset); `cx = es:[si]`; if 0 return (always 0 in the shipped data); else `4EB6[16*k] = (x,y,z)*Cam(7AF8)` as s32. Called once per view setup with `DS:8560 = [B760]`. Dead in practice.

## 6. Segment 12EF - 2D polygon-edge clipper and polygon assembler

Edge record format: section 4.1. Window = DS:85CA..85D0 (xmin,ymin,xmax,ymax). Out-codes: `1` y<ymin, `2` y>ymax, `4` x<xmin, `8` x>xmax.

### 12EF:0002 `r3d_clip_line_queue(x0,y0,x1,y1)`  (V) no callers
Builds an edge record at 5EBA from four s16 args (sign extended), `0333` clip, `003E` add. Public helper (U: unused).
### 12EF:01B1 `r3d_draw_line(x0,y0,x1,y1,colour)`  (V) no callers
`1359:0040(colour)`, build record 5EBA, clip `0333`, then `01F9`.  `12EF:01F9`: if flag bit 7 clear -> `1359:0049` (draw the clipped line).
### 12EF:003E `r3d_poly_add_edge` (SI = edge record)  (V)
`85B2 |= flags`; if bit 6: `00E0` saves the near-plane point; if bit 7 clear: `85BC |= 4`, `1359:0719(rec)` (DDA into the L/R tables); then side handling: for end 0 flags (bits 0/1) `0C5`/`0AA` accumulate the y range `[rec+4, rec+6]` into the right (85B8/85BA) / left (85B4/85B6) wall range; same for end 1 with `[+0C,+0E]`. Rejected edges (bit 7) only do the side handling with `[+6,+0E]` (so a polygon passing outside the window still fills correctly).
### 12EF:00AA `r3d_note_left` / 12EF:00C5 `r3d_note_right` (BX,CX = two y)  (V)
`lo=min, hi=max`; `85B6/85BA = max(.,hi)`, `85B4/85B8 = min(.,lo)`.
### 12EF:00E0 `r3d_note_zclip_point` (SI = record)  (V)
Copies `rec+10h..16h` (4 words) into `8588` (first) or `8590` (second), `85BE++`.
### 12EF:010A `void far r3d_poly_finish(colour_word)`  (V)
`1359:0040` (set colour); if `85BE==2` add an edge between the two saved near points (`8588`, via `0333`+`003E`) to close the polygon along the near plane; if `85BC != 0`: if `85B2 & 5` `0185` (right wall: vertical fill at x=xmax+1 over [85B8,85BA] via `1359:07D6`); if `85B2 & 0A` `019B` (left wall at x=xmin-1 over [85B4,85B6]); `else` (nothing rasterised): if the polygon has both side flags the two walls are still drawn (`0172`); `1359:0052` = fill the L/R tables; reset: `85B8=85B4=7FFF`, `85BA=85B6=8000`, `85BC=85B2=85BE=0`.
### 12EF:0185 `r3d_wall_right`, 12EF:019B `r3d_wall_left`  (V)
`1359:07D6(x = xmax+1 / xmin-1, ya, yb)` with the accumulated range.
### 12EF:020B `r3d_clip_flags16` (AL/AH = out-codes of end 0/end 1, CX,BP = y0,y1)  (V)
Writes `rec+6` = y0 clamped to [ymin,ymax] if outcode has y bits, `rec+0E` = y1 clamped, and `rec+2 = bl|bl<<8` with bl bit0 = AL&8, bit1 = AL&4, bit2 = AH&8, bit3 = AH&4.
### 12EF:0265 `r3d_clip_intercept` (AL = outcode mask, BX,CX = point, uses 85AC/85AE = dx,dy of the edge)  (V)
Computes the intersection of the edge with the window border selected by AL (y borders with `imul/idiv`; x borders); returns CF=1 if the intercept is off the window (AL=5 or 0Ah style codes). Local entry `025F` = "no clipping needed" (`BP=BX, DX=CX, CF=0`).
### 12EF:02D1 `r3d_outcode16` (BX = x, CX = y -> AL)  (V)
Cohen-Sutherland out-code with 16-bit compares; signed overflow of a compare sets `85C0=1` (`0301`/`0308`). Local labels `030F` (sets flags 0 and does a **near `ret`** - latent original bug, entered only from `0333` at `037E`; unreachable because that path is taken only when a coordinate does not fit 16 bits), `0315` (sign-extend all four coordinates and fall into the 32-bit path).
### 12EF:0333 `void far r3d_clip_edge` (SI = edge record)  (V)
1. If all four values already fit s16 (hi word == sign of lo word): fast path `0465`: out-codes of both ends `02D1`; both zero -> `rec+2=0` (inside); same y-side or both outside same x side -> reject (80h / 85h / 8Ah via `020B`); else `020B` side flags, then clip with `0265` (twice: first end, then the second) writing back x0,y0,x1,y1; failure -> `or al,80h`.
2. Otherwise (`0359`) 32-bit path: `0665` out-codes of the s32 ends; trivial accept/reject; else `04EF`/`056B` bisection (32 iterations, counter `85B0`) moves the outside end onto the border; results stored back as s16.
Out: `rec+2` flags (section 4.1), clipped coordinates.
### 12EF:04EF `r3d_bisect_in`, 12EF:056B `r3d_bisect_out`, 12EF:05D9 `r3d_clip_flags32`, 12EF:063E `r3d_on_border`, 12EF:0665 `r3d_outcode32`  (V)
Helpers of the 32-bit path: `0665` out-code of a 32-bit point (DX:AX x, CX:BX y -> BP); `063E` ZF=0 iff the point lies exactly on a window border (hi words zero and equal to xmin/xmax/ymin/ymax); `04EF`/`056B` midpoint loops (with `0553`/`055F` special cases for FFFFh) narrowing the outside point; `05D9` builds `rec+6`/`rec+0E` and flags from the 32-bit out-codes (BP, DI).

## 7. Segment 1359 - raster back end (mode dispatch; the shipped game uses mode 'M')

`1359:0000` (R1) is called once by `0010` with `'M'` (4Dh); it selects a cs-relative table: E (EGA planar) `0064`, M (VGA 13h) `0070`, C (CGA) `007C`, T (Tandy) `0088`; DS:85C2 holds it. Each table = 6 words: `[+0] init, [+2] pixel, [+4] set colour, [+6] line, [+8] fill, [+0A] fill (2nd)`:

| mode | init | pixel | colour | line | fill | fill2 |
|---|---|---|---|---|---|---|
| E | 00E7 | 023A | 01B1 | 0369 | 0874 | 0834 |
| M | 010A | 027B | 01D4 | 045D | 0AF7 | 0AF7 |
| C | 0126 | 02B6 | 01F8 | 0505 | 0BB3 | 0BB3 |
| T | 0149 | 0301 | 021D | 0607 | 0CA1 | 0CA1 |

The entry stubs `1359:0040` (colour, `call cs:[bx+4]`, **AX = colour word, the pushed copy is ignored**), `1359:0049` (line, SI = edge record), `1359:0052` (fill), `1359:005B` (fill2) dispatch through DS:85C2. Only M matters for the port; E/C/T are the original 1991 multi-adapter back ends (E/C/T never run here: DS:913E, 9168 are zero).

### 1359:019B  `void far r3d_set_page(int page)`  (V) no callers
`85C6 = page`, `85EA = [85E2 + 2*page]` (DB71 does the same inline).
### 1359:01B1 / 01F8 / 021D  mode colour setters (E / C / T)  (V)
EGA: AL colour -> `85D6`, programs GC regs 3CEh (set/reset) from table 9168; C/T: build the 2-bit/4-bit replicated pattern in 914A/914C/914E. Dead for mode M.
### 1359:023A / 027B / 02B6 / 0301  `pixel(x,y,colour)` (E / M / C / T)  (V)
Window-clipped single pixel. M: `85D6 = colour; es:[85EC[y]+x] = colour` (027B). No callers.
### 1359:0344 `ega_pixel_addr` (V) helper of 023A.
### 1359:0719  `void far r3d_edge_dda` (SI = edge record, s16 x0@0, y0@4, x1@8, y1@0C)  (V)
Swaps so x0 <= x1; row index `di = 2*y0`, row step `bp = +2` (y1 >= y0) or `-2`; `bx=min y, si=max y`, `dx = x1-x0`; rejects edges with y outside [ymin,ymax]; `9130 = min(9130, miny)`.
`dy = maxy-miny`. x-major (`dy <= dx`): runs of x per row, Bresenham with error `bx = -(n/2)`; at the start of a run `L[row] = min(L[row], x)`, at the end `R[row] = max(R[row], x)`; y-major: every step updates both tables with the current x. Tables: `L = DS:89AC + 2y` (idle 7FFFh), `R = DS:8D6E + 2y` (idle 8001h).
### 1359:07D6  `void far r3d_wall_fill(x, ya, yb)`  (V)
Sorts `ya,yb`, clamps to [ymin,ymax]; for each row `L = min(L, x)`, `R = max(R, x)`; updates `9130`.
### Mode M routines inside the gaps of the index (not indexed; all V)
* `1359:01D4 set_colour_M(AX)`: `85D6 = ax`; if `ah == FFh` and `al >= A0h` (ramp colour): `al' = al - [4894]` (byte), `ah' = al & F0`; if `al' <= ah'` then `al' = ah'` (floor of the 16-colour ramp); `85D6.lo = al'`; `es = [85EA]`.
* `1359:045D line_M(SI=edge record)`: plain Bresenham (no clipping) between (x0,y0),(x1,y1) with row offsets `85EC[y]` (ie `y*320`) in colour `85D6.lo`.
* `1359:0AF7 fill_M`: `ax=[85D6]`; `ah` selects: `FF` solid (`ah=al`), `FE` -> `09F7` AND `al` onto existing pixels, `FD` -> `0A77` OR, `FC` -> `098D` 50% stipple (checker), `FB` -> `0959` no fill (only clears the tables); otherwise 2x2 dither of `al/ah` (`0B29`). Loop: from row `9130` upward while `L[row] != 7FFF`: clamp `[L,R]` to `[xmin,xmax]`, fill the span, reset `L=7FFF, R=8001`; finally `9130 = 7FFF`. Dither pattern: `ah/al` swapped by row parity (`si&2`, si = 2*row) and column parity.
* Other: `00B6` EGA alt init (DOS `1359:0094`, ints 21h/48h = allocate paragraphs), `0369/0505/0607` line E/C/T, `0834/0874` EGA fill with sequencer 3C4h, `0BB3/0CA1` fill C/T.

## 8. Segment 1433 - fixed-point math library (tables in segment 14B2)

Tables at 14B2:0000 (all word): `0000..0FFF` sine, 2048 entries = one quadrant, `sin(i/2048 * 90deg) * 7FFF` (0,25,50,75.., 7FFF,7FFF); `1000..2FFF` atan, 4096 entries `atan(i/4096)` in angle units (0,2,5.. 1FFE); `3000..31FF` hypot: 256 entries `(sqrt(1+(i/256)^2)-1)*65536`; `3200` the entry for ratio 1.0 (6A09); `3202..3A01` asin, 1024 entries (idx = |x|>>5, 0..3FFF).
All functions are `far`, C convention (first arg at `[bp+6]`), return AX (and DX for 32-bit).

| addr | name | semantics |
|---|---|---|
| 1433:0004 | `int math_sin(uint16 a)` | `i = a>>2; if (a&4000h) i = ~i; i &= 0FFEh; v = tab14B2[i]; if (a&8000h) v = -v; return v` (Q15) |
| 1433:0031 | `int math_cos(uint16 a)` | same with the quadrant bits adjusted (`cos a = sin(a+4000h)`) |
| 1433:0061 | `uint16 math_atan2(int y [bp+6], int x [bp+8])` | octant flags bl: +8 x<0, +4 y<0, +2 |x|<|y| (swap); `ratio = small/large` (Q16, DIV); idx = ratio>>4; `ang = base9178[bl] +/- atan[idx]` (sign by base low byte 80h); equal/zero case returns base +/- 1FFEh; result 0..FFFFh, 4000h = +90deg (counter-clockwise from +x) |
| 1433:00D9 | `int math_mul_sin(int k [bp+6], uint16 a [bp+8])` | `(k * sin a) >> 15` (also `or dl,1` rounding bit) |
| 1433:011A | `int math_mul_cos(int k, uint16 a)` | `(k * cos a) >> 15` |
| 1433:015E | `uint16 math_asin(int x)` | `idx = (|x|>>4) & 7FEh; v = tab[3202+idx]; sign from x` => angle -4000h..4000h |
| 1433:018E | `uint16 math_hypot2(int a [bp+6], int b [bp+8])` | `L=max(|a|,|b|), S=min; r = L + ((S/L as Q16 via 256-entry table 3000) * L >> 16)`; at least L (table max error ~0.06%) |
| 1433:01E0 | `int math_hypot3(int x, int y, int z)` | `h = hypot2(x,y); if (h < 0) return h + (|z| >> 1); return hypot2(h, z)` |
| 1433:0219 | `void mat_vec16(int16 v[3] [bp+6] (in/out), const int16 M[9] [bp+8])` | `v' = v*M >> 15` (Q15; sums of hi-word products shifted left once) |
| 1433:0286 | `void mat_mul(A [bp+6], B [bp+8], C [bp+0A])` | `C = A*B` Q15, `C[i][k] = sum_j A[i][j]*B[j][k] >> 15`; via helper `02AA` (one row) |
| 1433:02AA | `mat_row_mul` (near) | SI = A row, BX = B, DI = C row (3 outputs), BX restored by -6 |
| 1433:02E2 | `void mat_transpose(src [bp+6], dst [bp+8])` | 3x3 transpose |
| 1433:0314 | `void mat_copy(src [bp+6], dst [bp+8])` | 9 words |
| 1433:032E | `void vec_mat32(const int16 v[3] [bp+6], const int16 M[9] [bp+8], int32 out[3] [bp+0A])` | `out_k = sum_j v_j * M[3j+k]` full 32-bit, NOT shifted (Q15 result in a s32; hi word = value in model units) |
| 1433:03A9 | `void mat_from_euler(uint16 a [bp+6], uint16 b [bp+8], uint16 c [bp+0A], int16 out[9] [bp+0C])` | with `SA,CA=sin/cos a; SB,CB; SC,CC` and `q(x,y) = x*y>>15`: m0 = CC*CA - q(q(SC,SB),SA); m1 = q(q(CC,SB),SA) + SC*CA; m2 = -CB*SA; m3 = -SC*CB; m4 = CC*CB; m5 = SB; m6 = q(q(SC,SB),CA) + CC*SA; m7 = SC*SA - q(q(CC,SB),CA); m8 = CB*CA (V) |
| 1433:04D7 | `void mat_to_euler(const int16 M[9] [bp+6], uint16 *a [bp+8], uint16 *b [bp+0A], uint16 *c [bp+0C])` | inverse of 03A9: `b = asin(m5)`; if `|b| > 3E80h` (gimbal) `a = atan2(m6, m0), c = 0`; else `a = atan2(-m2, m8)`, `c = atan2(-m3, m4)` (V) |
| 1433:0542 | `int math_mulq(int a, int b)` | `(a*b)>>15` signed, sign-extended |
| 1433:0554 | `int math_muldiv(int a [bp+6], int b [bp+8], int c [bp+0A])` | `a*b/c` (c==0 -> 1); if |b|<|c| `a*b/c` else `(b/c)*a` |
| 1433:058F | `int math_div32sat(int lo [bp+6], int hi [bp+8], int d [bp+0A])` | `(hi:lo) / |d|` (divisor taken as absolute value!, (U) bug or intended); if `(|hi|+1)*2 > |d|` returns 7FFFh^sign (saturate) |
| 1433:05C6 | `long math_imul(int a, int b)` | 32-bit product in DX:AX (just `imul`) |
| 1433:05D1 | `void mat_from_euler_b(int c [bp+6], int b [bp+8], int a [bp+0A], int16 out[9] [bp+0C])` | alternative Euler order, no callers: with `S1,C1 = a`, `S2,C2 = b`, `S3,C3 = c`: m0 = q(q(S1,S2),S3) + C1*C3; m1 = C2*S3; m2 = q(q(C1,S2),S3) - S1*C3; m3 = q(q(S1,S2),C3) - C1*S3; m4 = C2*C3; m5 = q(q(C1,S2),C3) + S1*S3; m6 = S1*C2; m7 = -S2; m8 = C1*C2 (V) |

Sin/cos of the three angles are cached in DS:916C..9176 (`916C,916E` = sin/cos of the first, `9170,9172` of the second, `9174,9176` of the third argument of 03A9 order: a, c? see the table: in 03A9 `916C/916E` <- [bp+6], `9174/9176` <- [bp+0A], `9170/9172` <- [bp+8]).

## 9. Open items

| # | item |
|---|---|
| 1 | DS:C686 bit 7 = point inside a solid scenery model (see 3.4). Readers 2F82/39C4/671A/5730 describe the consequence. Which object banks 0FFB scans is R11a's (77D9). |
| 2 | Plane shade byte (77D2 -> 7D5A) is computed but I found no reader; brightness seems to come from `[4894]` only. Confirm by a runtime watch on 7D5A. |
| 3 | Meaning of `zoom` argument of `000C` / `dae8` last parameter (0x100 = neutral); who passes what in 03CE (R3). |
| 4 | `DS:7D3A` (y scale 0.75) is never written; probably an unused aspect switch. Real pixel aspect is handled by the 0.75 factor in the camera matrix (V). |
| 5 | Part count limit: the matrix slots 7B1C+24h*k collide with the eye array at 7C3C beyond 7 parts. Real data has <= 4? (check f3d.py). |
| 6 | `12EF:0333` general (32-bit) path details (04EF/056B/05D9 bit-exact bisection) - needed for exact clipping at extreme zoom; port can use any robust clipper only if pixel-exactness is not required. |
| 7 | Latent near-`ret` bug at `12EF:030F` (see 02D1 entry) is not reachable (U). |
| 8 | Points (20h) element: loop at `0DFE` computes screen positions but never draws (dead). `0DAF` unreachable. |
| 9 | `0EF2`, `12EF:0002/01B1`, `1359:019B/023A/027B/02B6/0301`, `1433:05D1` have no callers in the image (public API unused). Check data-referenced calls (U). |
| 10 | BSP child bytes with bit 7 set (emit verbatim) never occur in the data (U). |

## 10. DGROUP globals seen (summary; see section 4 for the buffer map)

| offset | size | name | meaning | readers / writers |
|---|---|---|---|---|
| 7C9C | 6 | r3d_objpos | object offset rel. camera (x,y,z s16) | 00D3,01B8 w; 031B r |
| 7CA2 | 12 | r3d_viewpos | object origin in view space, s32 x3 | 032E w; 02B6,06B9,0819 r |
| 7CAE | 96 | r3d_partpos | part origins, s32 x3 each | 031B/0440 w; 06B9 r |
| 7AF8 | 18 | r3d_cammat | camera matrix | 000C w |
| 7B1C | 0x24*8 | r3d_partmat | per-part matrices | 031B |
| 7C3C | 48 | r3d_eye | eye in part frames | 031B |
| 7C6C | 48 | r3d_light | light in part frames | 04CD/04F9 |
| 7D1C | 2 | r3d_objmat_ptr | | 00D3,01B8 w; 031B r |
| 7D1E | 2 | r3d_vars_ptr | | 00D3 w |
| 7D20 | 2 | r3d_cur_slot | | 031B |
| 7D26 | 2 | r3d_cur_origin | | 031B/0440 |
| 7D28 | 2 | r3d_nparts | | |
| 7D2A | 2 | r3d_nplanes | signed | 0565 w |
| 7D2C | 2 | r3d_nverts | signed | 00D3 w |
| 7D2E | 2 | r3d_far | 7FFF00h/max(2*zoom,100h) | 000C w; 02B6 r |
| 7D38 | 1 | r3d_planemask | | 0565 |
| 7D3A | 2 | r3d_yscale_sw | always 0 | 000C r |
| 7D4C | 2 | r3d_light_ptr | | |
| 7D4E | 2 | r3d_last_line | pointer to last drawn line edge | 0CED w, DF08 r |
| 7D50 | 2 | r3d_coltab | colour byte->word table | 03CE,BF71 w; 0CED r |
| 7D54 | 6 | r3d_lightvec | (-36,36,36) | DB71 w |
| 7D5A | 1 | r3d_shade | | 0CAC w |
| 7D5C | 1 | r3d_flags | | |
| 7D5E | 2 | r3d_ortho | map mode | 119C,11F2 w |
| 85B2..85C0 | | r3d_clip_state | polygon clip bookkeeping | 12EF |
| 85C2 | 2 | r3d_vmode_tab | dispatch table | 1359 |
| 85CA..85D4 | 6 words | r3d_win / r3d_centre | window, centre | DB71 w |
| 85D6 | 2 | r3d_color | current fill word | |
| 85EA | 2 | r3d_dseg | draw segment | DB71 w |
| 85EC | 960 | r3d_rowtab | row offsets | |
| 89AC / 8D6E | 960 each | r3d_edge_L / r3d_edge_R | scanline edge tables | 1359:0719,07D6 w; fill r |
| 9130 | 2 | r3d_miny | | |
| 9178 | 16 | atan2_base | octant bases | 1433:0061 |
| C688 / D688 | 4096 / 2048 | r3d_v3d / r3d_vproj | vertices | 06B9 w |
| 5EBA | 8510 | r3d_edges | edge records | |
| 76D2 / 77D2 / 78D2 / 79D2 | 256 each | r3d_vis / r3d_shadeb / r3d_bsporder / r3d_vneed | | |

## 11. Structures seen

* **Edge record**: 24 bytes, section 4.1. Array at DS:5EBA, indexed by the model's edge index (<= 354).
* **Projected vertex**: 8 bytes {s32 x, s32 y} at DS:D688+8*v; `x_hi == 8000h` = behind the camera.
* **View-space vertex**: 16 bytes {s32 x, s32 y, s32 z, 4 pad} at DS:C688+16*v.
* **Part matrix slot**: 36 bytes at DS:7B1C+36*k {9 words M (model->view), 9 words transposed orientation}.
* **Camera/object matrix**: 18 bytes, 9 words Q15, row-major.
* **Viewport rect (read via DB71, owner R11a)**: +0 page, +0Eh ymin, +10h ymax, +12h xmin, +14h xmax -> DS:85CA..85D4.
* **3DX model/object**: see FORMATS.md; consumption order in 3.2. LOD entry = (word maxdepth, word model offset); plane = 3 s16 + s32; vertex = 3 s16; edge = 2 bytes; element codes in the face list as in 0CED.
* **Sim-side world object record (as consumed by DF08 -> 00D3; owner R-other)**: `+4` byte shade -> DS:4894; `+6` flags; `+8` word type (low 15 bits: object index in bits 0-7, bank in bits 8-14: `[4956]`/`[4957]`); `+0Ah,+0Ch,+0Eh` position words (-> 4964, 4966, 4968: x east, z north, altitude); `+10h,+12h,+14h` three Euler angles (-> 496A, 496C, 496E). DF08 calls `03A9(-[496A], -[496C], +[496E])`, transposes with `0000:E54B`, then `00D3(obj, [4964], [4968], -[4966], 49A0, C02E)`.
