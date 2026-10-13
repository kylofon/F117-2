# S3.6 reader R11a: scene set-up, terrain/scenery, object placement + culling, sorted-object queue, 2D polygon edge tables (1001), sine table

Scope: 0000:04DA 0804 0865 0FB1 1AD6 1B1B C6B8 DAE8 DB3E DB71 DC64 DCA9 DF08 E26B E3E2 E54B E7AC E8A2 E8B5 EA26 and the far segment
1001:0006..0854 (37 functions). Owner of nearly everything here is **render3d** (index-level, S3.7 will go deeper), plus the shared glue:
coordinate conventions, the sine table, the matrix layouts and the object-record layout. `(V)` = verified in the disassembly, `(U)` = unclear.

IMPORTANT correction to BRIEF.md: **DGROUP data in the unpacked image is at image offset 0x1EA50 + DS:oooo, not 0x1E230** (checked: "regn.xxx" at
DS:0042 = image 0x1EA92, "photo.3dx" at DS:0CC5 = 0x1F715, and the sine table below at DS:2084 = 0x20AD4). Code addresses (seg 0 and far segs
SSSS*16+OOOO) are right as stated. Anyone reading initial DGROUP data straight from the image must use 0x1EA50 (0x820 bytes more).

**1001 is NOT 3D math.** Segment 1001 is the 2D polygon edge-table builder/clipper used for the sky/ground backdrop and a few rectangles
(Cohen-Sutherland clipper + scan-edge tables at DS:22A1/2459). The sine/cosine/multiply helpers are in far segment **109D** (not in this scope, but
documented here in section 2 because everything depends on them): `109D:0006 mul_q15`, `109D:006C cos16`, `109D:007C sin16`, `109D:0090` table core.
The matrix builders are `0000:E26B` (this scope) and `1433:03A9`/`1433:0314`/`1433:02E2`, `11EB:000C` (R11b).

---------------------------------------------------------------------------------------------------------------------------------------------

## 1. Overview: how a 3D scene is drawn (data flow)

### 1.1 Frame call chain (main view)
`frame loop 1BBC -> 2F82 (view) -> 03CE r3d_scene` (03CE is R3's; arguments below because they are the entry-point parameters):

```
03CE(heading_neg, pitch, roll, xlo, xhi, ylo, yhi, zlo, zhi, x0, y0, w, h)       // cdecl, [bp+4].. in this order
  vp = (Viewport*)[0344] = DS:032E        // the main viewport record (see 3.1)
  vp->y0=[bp+0x18] vp->y1=y0+h-1 vp->x0=[bp+0x16] vp->x1=x0+w-1   // vp+0xE,+0x10,+0x12,+0x14   (so [bp+0x16]=x0,[bp+0x18]=y0,[bp+0x1A]=w,[bp+0x1C]=h)
  vp->page = flip_flag()  (driver slot 45)  ;  vp->bg = 0x88  (+4)
  04BB([43B8])                                  // frame pacing: spin until the byte tick counter [264A] has advanced by [43B8] (V)
  [7D50] = (detail==3) ? 7F60 : 8360            // colour-byte -> fill word table (day, high / low detail)     (V, [98DE]==3)
  var[1]=[C030]=0 ; var[10]=[C042]=var[11]=[C044]=[9874]=0 ; if byte[11E2] < 0x14 -> all three = 1   (V; lights on, 11E2 = light/time level (U))
  focal = 0x100 ; if [365E] && [294B] focal = 0x80                                                   // 365E = side/rear view, 294B = narrow lens flag (U)
  DAE8(vp, a1=heading_neg, a2=pitch, a3=roll, ox=0, oy=0, oz=zlo, draw_backdrop=1, focal)           // set-up + sky/ground
  04DA(heading_neg, pitch, xlo, xhi, ylo, yhi, zlo, zhi)                                            // terrain + scenery (world position, 32-bit x,y,z)
  9ACA()                                                                                            // sim objects -> culled -> queued/drawn (R8: dcc0)
  DB3E()                                                                                            // flush sorted queue far->near, advance spinner vars
  [7D50] = 7D60 ; A17E() ; [0346] ^= 1                                                              // overlays/MFD 3D, buffer parity (U)
```
The call in 2F82 (V, 3758) passes: `-[E44C]` (heading, 0 = north, clockwise), `[C07C]` (pitch, +up), `[B770]` (roll), x = `[B28A]:[B288]`,
y = `[B754]:[B752]`, z = sign-extended `[B75C]` (altitude, 16 bit), viewport origin (0,0), width 0x140, height 0xC8 / 0xA1 / 0x9D (by view mode).
Angles/positions in DS:E44C, C07C, B770, B75C are written by 2F82 (copies of the plane state adjusted per view mode).

Other callers of the same primitives:
* `bf71` (target camera / TV picture, viewport `[4042]` = DS:402C, window at screen (169,110)): `DAE8(vp4042, -[DED4], [9510], [9512], 0,0,0, 1, 0x100<<[433A])`
  (c40e) or the map-like variant at c49f with `[4360]=1`, then `04DA(...)` (with `[0348]=1` forcing the "near" detail path), `DB3E`, `E7AC` for the target object.
* `119C` (map/overview, R3): `DAE8(vp, a1=0, a2=0xC000, a3=0, 0,0, oz=0x2000, 1, 0x100)` = camera looking straight down (pitch -90 deg) from z=0x2000, then `11F2(...)` (map terrain) and `DB3E`.
* `0FFB` (R? map object drawer) writes the same origin variables [497C..4980] and calls `11EB:01B8` directly.

### 1.2 Inside DAE8 r3d_begin_scene (draw order of the static scene)
1. `DB71(vp)` viewport -> clip window, screen centre, draw offset, cull tables.
2. `DC64(a1,a2,a3,focal)` camera matrices: `E26B(DS:498E, -a1,-a2,-a3)` (cull/transform matrix), `1433:03A9(-a1,-a2,a3, DS:49A0)` (model-side matrix), `11EB:000C(DS:49A0, focal)` (final view matrix at DS:7AF8 + depth scale [7D2E]).
3. `DCA9(ox,oy,oz)` -> [497C],[497E],[4980] (camera origin used by E7AC; per-cell overridden by 04DA).
4. if `draw_backdrop`: `[C686]=0`; `[8560]=[B760]` (theatre .3DX bank segment); `11EB:0F9B` (pre-transform the theatre shared vertex table into DS:4EB6 with the new matrix, R11b);
   spin `while (byte[262A]) ;` ([262A] is set to 1 by the frame loop after the flip, cleared elsewhere (timer ISR, U)): do not draw on the page being shown;
   `1442(vp)` = **sky + ground backdrop** (R? ; calls `1001:0006` flat horizon fill when detail<3, else 16+16 colour bands via `1001:01EC`).
5. `[49C4] = 0` (empty the sorted-object queue).

### 1.3 Order of drawing inside one scene
```
backdrop (1442)  ->  terrain/scenery (04DA, levels 4,3,2,1 = coarse far->fine near, cells far->near inside a level)
                 ->  non-sorted objects are drawn immediately by E7AC (11EB:00D3), flag byte bit7 = 1 objects are only queued (DDFE)
                 ->  9ACA: simulation objects (planes, weapons, ...) via DCC0 -> E8A2 cull -> DDFE queue (R8)
                 ->  DB3E: DF08 draws the queue in order (list sorted by view depth, FARTHEST first, painter's algorithm)
                 ->  A17E overlays
```
Within DF08 a record may also be a 2D scaled sprite (type bit 0x400) or have a 2D flame/glow sprite added after the model (bank 0, type >= 0x25).

### 1.4 Projection set-up (summary; details in 3 and 4)
* screen centre (viewport-relative): `[0D1E]` = (width+1)/2-1 (159 for 320), `[0D20]` = 100 for a viewport starting at y0=0 (special: 60 or 52 if `[365E]`, see DB71), else (height+1)/2-1; absolute copy in `[85D2],[85D4]`.
* focal length in pixels: the last argument of DAE8 (`0x100` normal, `0x100<<[433A]` zoom in bf71, `0x80` for `[365E]&&[294B]`), turned into `[7D2E] = 0x7FFF00 / max(0x100, |2*focal|)` by `11EB:000C` (0x3FFF for focal 0x100).
  The vertical axis is squeezed by 3/4 for non-square pixels (`11EB:000C`, `[7D3A]==0`; same 3/4 factor in 1ad6/1b1b sprite heights and in the horizon routine 1001:0018) (V).
* view matrices: DS:498E (Q15, built by E26B; used for CULLING by E8B5 and by 1442/1001:0018 via its sin/cos copies in DS:4982..498C), DS:49A0 (model-side matrix handed to 11EB:00D3, transposed by E54B), DS:7AF8 (final matrix incl. focal, R11b).
* max view distance: `[48B6]` = 0x0D05 * (detail+1) (written by `D76B`), min depth per size class `[48C6+2*class]` = -1,-23,-91,-182,-363,-725,-1450,-2900.
* detail level `[98DE]` 0..3: from comm+0x32 at mission start (3AFB..3B05 copies the byte), cycled by a key (CCD8: decrement; <0 -> value of driver slot 63 `get_const_3` if it is 3, else 2; message "Detail Level n"), then `D76B` rebuilds the depth tables.
  Effects seen: 3 = colour table 7F60 (instead of 8360) + 16-band gradient ground (1442); >=2 = level-4 terrain uses the 15-cell frustum-culled path (04DA path A); <2 = far cells drawn only as the single ground record, near/other levels use the plain 9-cell path;
  `[48B6]` grows by 0xD05 per detail step.

---------------------------------------------------------------------------------------------------------------------------------------------

## 2. Coordinate conventions, angles and the sine table (needed by S3.7 / S3.8)

### 2.1 World / camera coordinates
* World: **x = east, y = north, z = up**. Theatre = 0x100000 (2^20) world units square (x,y 32-bit values 0..0xFFFFF in the sim; the 16-bit pair is `lo` at the lower address). Altitude z is a **signed 16-bit** value (2F82 sign-extends `[B75C]`). The physical unit is not visible in the code (probably feet, (U)).
* Terrain cell sizes: see 4.1 (level 4 cell = 0x40000, level 3 = 0x10000, level 2 = 0x4000, level 1 = 0x1000 world units; level 0 = 0x800).
* Object records / placement: E7AC and DF08 work with **camera-relative** (east, north, up) = `([4964],[4966],[4968])`. The 3DX model space is (x east, y up, z south), so the call to `11EB:00D3` passes `(x=[4964], y=[4968], z=-[4966])` (V).
* View space produced by E8B5 (32-bit, units = input units, hi word = value): **A = right, B = up, C = forward depth**:
  `A = 2*(m6*N + m3*U + m0*E)`, `B = 2*(m7*N + m4*U + m1*E)`, `C = 2*(m8*N + m5*U + m2*E)` with (E,N,U) = (BP, BX, CX) = (x,y,z) and m0..m8 = the words DS:498E+2k (V).

### 2.2 Angles
16-bit binary angles, **0x10000 = 360 deg, 0x4000 = 90 deg**, wrap-around by 16-bit arithmetic.
* heading `[E44C]`: 0 = north, 0x4000 = east (clockwise) (V from the 04DA octant table: octant = (0x1000 + heading) >> 13, heading 0 -> far cells at +y). The scene functions receive `-heading` (a1 = `-[E44C]`) and negate again inside DC64 (V).
* pitch `[C07C]`: positive = nose up (V from the matrix: C = cos p*(x sin h + y cos h) + z sin p). 04DA treats pitch < 0xD555 (= -60 deg, signed compare) as "looking straight down".
* roll `[B770]`: positive = right wing down (V from the matrix: right = (cos r, -, -sin r) for h=p=0).
* Object angles in queue records (+0x10,+0x12,+0x14) / E7AC args: three angles (a1,a2,a3) handed to `1433:03A9(-a1,-a2,a3)` (R11b); E3E2 reads them as (sin/cos of [496A],[496C],[496E]).

### 2.3 Sine table and trig helpers (segment 109D; DS data) -- COMPLETE
* **Table: DS:2084, 257 signed 16-bit words** (image 0x20AD4). `T[i] = round(32768 * sin(2*pi*i/256))` clamped to +/-32767 (i = 0..256; verified: all 257 entries equal the formula, only i=64 (32767 instead of 32768) and i=192 (-32767) are clamped). One full circle = 256 entries (step 1.40625 deg), entry 256 = 0 is the interpolation guard word.
  Symmetries (verified): `T[128-i] = T[i]`, `T[256-i] = -T[i]`. Q15 scale: **unity = 0x7FFF (32767)**, there is no 0x8000.
* Quarter wave T[0..64]: 0,804,1608,2411,3212,4011,4808,5602,6393,7180,7962,8740,9512,10279,11039,11793,12540,13279,14010,14733,15447,16151,16846,17531,18205,18868,19520,20160,20788,21403,22006,22595,23170,23732,24279,24812,25330,25833,26320,26791,27246,27684,28106,28511,28899,29269,29622,29957,30274,30572,30853,31114,31357,31581,31786,31972,32138,32286,32413,32522,32610,32679,32729,32758,32767.
* `109D:0090` core (BX = angle in, BX = result): `i = a >> 8 (bh)`, `f = a & 255 (bl)`, `r = T[i] + ((T[i+1]-T[i])*f + 128 >> 8)` (the code: imul, take bits 8..23 of the product, add the carry from bit 7 = round half up) (V).
* `109D:007C sin16(angle)` = core(angle); `109D:006C cos16(angle)` = core(angle + 0x4000); both far, cdecl `[bp+6]`, result in AX, Q15 (V). `109D:0088` = register entry of the core.
* `109D:0006 mul_q15(a, b)` = `(a*b + 0x4000) >> 15` (signed, rounded; code: imul, shl/rcl, shl + adc dx,0) (V). Q15 x int gives int (coordinates), Q15 x Q15 gives Q15.
* The only other reader of the table is `0000:2EBE asin_q15(x)` (V): `x == 0x8000 -> 0xC000`; else `i = (|x|>>9)+1` (<= 64), decrement i until `T[i] <= |x|`, `ang = (i<<8) + (((|x|-T[i]) << 8) / (T[i+1]-T[i]))`, negated if x < 0. Input Q15, output 16-bit angle in -0x4000..0x4000. `2F2F acos_q15(x) = 0x4000 - asin_q15(x)` is built on it.
* START has its own copy of the idea (shell.md `ui_sin16` at DS:ABF8), not shared.
* Inverse use: none found in this scope.

### 2.4 Matrix layouts
All matrices are 9 signed 16-bit words, row-major as the code stores them, **Q15 (0x7FFF = 1.0)**, entries `m0..m8` at `base+2k`.
* **DS:498E** (E26B output; "camera/cull matrix"). With (s1,c1) = sin/cos of A, (s2,c2) of B, (s3,c3) of C, where DC64 passes (A,B,C) = (-a1,-a2,-a3) = (heading, -pitch, -roll):
  ```
  m0 = c1*c3 + s1*s2*s3     m1 = s1*s2*c3 - c1*s3     m2 = s1*c2
  m3 = c2*s3                m4 = c2*c3                m5 = -s2
  m6 = c1*s2*s3 - s1*c3     m7 = c1*s2*c3 + s1*s3     m8 = c1*c2        (every product is mul_q15, sums in 16 bit)
  ```
  used as `view.A = m0*E + m6*N + m3*U`, `view.B = m1*E + m7*N + m4*U`, `view.C = m2*E + m8*N + m5*U` (E8B5). (V)
* **DS:49A0** (model matrix): `1433:03A9` (R11b) from (-a1,-a2,a3); transposed by E54B/E7AC before `11EB:00D3`. E3E2 builds an alternative 49A0 (see 5.10).
* **DS:49B2**: 18-byte copy of DS:498E made by DB3E (memcpy(0x49B2, 0x498E, 0x12)); no reader found in the code image (U).
* `E54B` = transpose of DS:49A0 (swaps 49A2<->49A6, 49A4<->49AC, 49AA<->49AE) (V).

---------------------------------------------------------------------------------------------------------------------------------------------

## 3. Common structures

### 3.1 Viewport record (`Viewport`, e.g. DS:032E main, DS:402C target camera; also the TextCtx/sprite ctx layout of video.md)
| off | size | meaning |
|---|---|---|
| +0 | w | page number (03CE: from driver slot 45 `flip_flag`); index into the page-segment table DS:85E2 (db71 -> [85EA]) |
| +4 | w | background (sky) colour: 0x88 for the main view (03CE writes it); 1001:0006/024C use it |
| +6 | w | second colour (used only by the dead near function 1001:02BA) |
| +0xE | w | y0 (top) |
| +0x10 | w | y1 (bottom, inclusive) |
| +0x12 | w | x0 (left) |
| +0x14 | w | x1 (right, inclusive) |

### 3.2 Sorted-object queue (ddfe, R8; documented because DF08 is mine)
`DS:4E3C`: 50 near pointers (sorted list), `[49C4]` count (max 0x32); record pool `DS:49F0` = 50 x 0x16 bytes (record = pool + 0x16*index when not full; when full the farthest one is dropped and its record reused).
Sorted descending by key (farthest first, signed 32-bit compare, ties by low word). Record:
| off | size | meaning |
|---|---|---|
| +0 | s32 | sort key = view depth `[4960]:[4962]` >> (2*(4 - `[E2FC]`)) (so all terrain levels compare in level-4 units; DCC0 sets `[E2FC]`=1) |
| +4 | w | shade value = result of `1088:0120` (distance fade, 0..15; used as `[4894]` colour shift) |
| +6 | w | flags `[49C6]` (set by 9ACA/BCD7/C834): bits 0-1 -> var[12]=1; bit 5 -> suppresses var[13]; rest (U) |
| +8 | w | type: low byte = object index, high byte = bank (0 STFLT, 1 theatre, 2 PHOTO); bit 15 = "destroyed" variant (var[1]); 0x0400 = 2D sprite object |
| +0xA,+0xC,+0xE | s16 x3 | position relative to the camera: east, north, up |
| +0x10,+0x12,+0x14 | s16 x3 | angles a1,a2,a3 (for sprite objects: screen x, screen y, size!) |

### 3.3 3DX variable table `DS:C02E` (the `vars` argument of `11EB:00D3`; word per var number, var & 0x1F)
var[1]=`[C030]` destroyed; var[5]=`[C038]`, var[6]=`[C03A]`, var[7]=`[C03C]`, var[8]=`[C03E]`; var[10]=`[C042]` lights, var[11]=`[C044]` lights; var[12]=`[C046]`, var[13]=`[C048]`, var[14]=`[C04A]`;
var[15]=`[C04C]`, var[16]=`[C04E]` = spinner angles advanced each frame by DB3E. Setting is done by 03CE (lights), 04DA (lights/destroyed), E7AC (destroyed) and DF08 (the rest, bank 0).

---------------------------------------------------------------------------------------------------------------------------------------------

## 4. Terrain and scenery (04DA family)

### 4.1 Tile pyramid (see FORMATS.md; here the code facts)
* `0804 terr_to_level(level, long world)` returns the coordinate in level units: level0: `v*2`; level1: `v`; level2: `(v+2)>>2`; level3: `(v+8)>>4`; level4: `(v+0x20)>>6` (round to nearest; 32-bit in DX:AX). Cell index at a level = `unit >> 12`, offset inside the cell = `unit & 0xFFF` (V).
* `[0506+2*level]` = grid dimension in cells: 1024,256,64,16,8 for levels 0..4 (V, dumped from the image). Level 4 coordinates are offset by +2 (the 8x8 table holds a 4x4 theatre plus a 2-cell border).
* `[04FA+2*level]` = 1,1,1,1,1,0: "level enabled" flags (constants).

### 4.2 `0000:0865 terr_tile_type(level, x, y)` -> tile type 0..255 (0 for out of range)
```c
int terr_tile_type(int level, int x, int y) {
    if (level == 4) { x += 2; y += 2; }
    if (x < 0 || y < 0 || x >= dim[level] || y >= dim[level]) return 0;            // dim at DS:0506
    switch (level) {
    case 4: return byte[0xB00C + y*8 + x];                                          // 8x8 level-4 map
    case 3: return byte[0x9F68 + y*16 + x];                                         // 16x16 .3DG map
    case 2: p = terr_tile_type(3, x>>2, y>>2); return byte[0x9D10 + p*16 + (y&3)*4 + (x&3)];
    case 1: p = terr_tile_type(2, x>>2, y>>2); return byte[0x9B08 + p*16 + (y&3)*4 + (x&3)];
    case 0: p = terr_tile_type(1, x>>2, y>>2); return byte[0x9904 + p*16 + (y&3)*4 + (x&3)];
    }
}
```
(V) addresses decoded from `[bx+si-0x4FF4]`=B00C, `-0x6098`=9F68, `-0x62F0`=9D10, `-0x64F8`=9B08, `-0x66FC`=9904. Original quirk: callers test for -1 but the function can never return it (dead test in 04DA).
Callers: 04DA, 0D08, 0FFB, itself.

### 4.3 `0000:04DA r3d_draw_terrain(heading_neg, pitch, xlo, xhi, ylo, yhi, zlo, zhi)`
cdecl, stack [bp+4]..[bp+0x12] = heading_neg (= -heading), pitch, x lo/hi, y lo/hi, z lo/hi (32-bit world camera position, z needs to be small). Locals: octant, near flag, cell indices, fractions. (V)
```
near = ([98DE] >= 2 || [0348] != 0)                         // [0348]: forced by bf71's target camera
oct  = (0x1000 - heading_neg) >> 13   (16-bit)              // 0..7, 45-degree sectors
for (level = [E2FC] = 4; level >= 1; level--) {
    if (!enabled[level]) continue;
    cx = 0804(level,x)>>12 ; fx = 0804(level,x) & 0xFFF ;  cy,fy likewise ; alt = 0804(level,z)   (32-bit)
    if (alt >> 16 != 0 || (uint16)alt >= 0x7FFF) continue;  // too high for this level -> level skipped (so only coarse levels at altitude)
    alt = max(alt, 2) ; [94DA] = alt
    for (i = 0; ; i++) {
        // PATH A: level 4 and [98DE] >= 2: cells i = 0..14, with a frustum test of the cell centre
        // PATH B: everything else: cells i = 0..8 (9 cells) from the same octant table, no cull
        ox = T[oct*9 + i] ; oy = T[((oct+2)&7)*9 + i]       // DS:034C, 8 rows x 9 words; row = view-direction octant, entries far -> near, last = (0,0)
        (path B, pitch < 0xD555 i.e. looking down > 60 deg): ox = DS:04CC[i], oy = DS:04DE[i]   (9 entries each, heading independent)
        path B skips cells i < 4 when level != 4 && [98DE] < 2 (the 4 far cells are not drawn at low detail on fine levels)
        [94D6] = fx - ox*0x1000 - 0x800 ; [94D8] = fy - oy*0x1000 - 0x800                        // camera relative to the cell centre (level units)
        path A only: [497A] = 7 ; if (E8A2(-[94D6], -[94D8], -[94DA]) != 0) continue;            // culled
        DCA9([94D6],[94D8],[94DA])                                                              // origin for E7AC
        tile = terr_tile_type(level, ox + cx, oy + cy);                                          // (the "-1" test is dead)
        if (i < 4 && !near) {                                                                    // low detail far cell: only level 4, only record 0
            if (level == 4) { [0504] = 0x400; p = recptr[4*32 + tile];  E7AC(0x100|p->obj, 0,0,0, p->x,p->y,p->z); }
            continue;
        }
        [0504] = ([98DE]==2) ? 0 : level << 8 ;  p = [E44A] = DS:E304[level*32 + tile] ; n = DS:061E[level*32 + tile]
        for (j = 0; j < n; j++, p += 7) {                                                        // record = 7 bytes: s16 x,y,z (cell-centre relative), u8 obj
            [C044] = [C042] = [9874];                                                           // lights (var 10/11)
            obj = p->obj;
            if (obj & 0x80) {                                                                    // mission override possible
                ov = 0FB1(level, j, ox+cx, oy+cy);
                if (ov == 0)               obj = obj & 0x7F;
                else if ((ov ^ obj) & 0x7F) obj = ov;                                            // replaced by another scenery object
                else { [C044] = [C042] = 0; obj = ov | 0x8000; }                                 // same object: destroyed variant, lights out
            }
            E7AC(C6B8(obj | 0x100), 0,0,0, p->x, p->y, p->z);
            [0504]++;
        }
    }
}
[E2FC] ends 0 on exit.
```
Details: path A runs i until 0xF (15 cells) although the table rows have 9 entries, so cells 9..14 re-read the NEXT rows' words (rows overlap; for oct 0 the extra cells are (2,2),(1,2),(2,1),(0,2),(2,0),(1,1)); some are already drawn cells (duplicates) -- probably an original quirk (U). Path B stops at i==9.
`[0504]` is only ever written/incremented by 04DA and never read elsewhere (U; maybe an old priority key).
Level loop order is coarse -> fine and cells far -> near, i.e. the terrain is drawn with a painter's algorithm using the "ground" records of each tile; sort-flagged objects (flag bit 7) are deferred into the DDFE queue.
Callers: 03CE, bf71 (c4d3). Callees: 0804, 0865, 0FB1, C6B8, DCA9, E7AC, E8A2.

### 4.4 `0000:0804 terr_to_level(level, lo, hi)` -> DX:AX. See 4.1. Pure function. Callers: 04DA, 0D08, 0FFB, 11F2.
### 4.5 `0000:0FB1 terr_scenery_override(level, index_in_tile, cellx, celly)` -> byte (0 = none)
Linear search **backwards** through the override table `DS:B772` (5-byte entries `{u8 level, u8 index, u8 cellx, u8 celly, u8 new_object}`, count `DS:9902`); returns `new_object` of the last matching entry, else 0. Entries are created by START for mission targets / destroyed buildings (shell.md mission/site records). Writes `[94DC]` (loop counter). Callers: 04DA, 0D08, 0F31.
### 4.6 `0000:C6B8 r3d_scenery_id(id16)` -> word
`if (id16 & 0x0100) return DS:0510[(id16 & 0x7F)] | (id16 & 0x8000); else return id16;` DS:0510 is a 128-word map "3DT object byte -> (bank<<8 | index)", filled by the .3DX loader `0994` (identity `i | 0x100` for the theatre file's N objects `[0610]`; entries N and N+1 = `0x200 | [E2DF]`, `[E2F1]` = the two mission PHOTO.3DX objects; theatre index 6 (pilot rec +0x38 == 6) patches entry 0x5C2->0x20D and 0x51A->0x20A). Bit 15 (destroyed) is preserved. Callers: 04DA, bf71, 72A4, BC12 (0FFB reads DS:0510 directly).

---------------------------------------------------------------------------------------------------------------------------------------------

## 5. Scene set-up, placement, culling, sorted drawing

### 5.1 `0000:DAE8 r3d_begin_scene(Viewport *vp, a1, a2, a3, ox, oy, oz, draw_backdrop, focal)`  (V)
Stack: vp [bp+4], a1 +6, a2 +8, a3 +0xA, ox +0xC, oy +0xE, oz +0x10, flag +0x12, focal +0x14; caller cleans (`add sp,0x12`).
Calls: `DB71(vp)`; `DC64(a1,a2,a3,focal)`; `DCA9(ox,oy,oz)`; if flag: `[C686]=0; [8560]=[B760]; 11EB:0F9B(); while([262A]); 1442(vp)`; `[49C4]=0`. Callers: 03CE, 119C, bf71 (4x).
Angle parameter meaning for the main view: a1 = -heading, a2 = pitch, a3 = roll.

### 5.2 `0000:DB3E r3d_end_scene()`  (V)
`DF08()` (draw sorted queue); `offset_clear` (slot 24: draw offset := 0, undoing DB71's offset_set); then spinner variables: `t = [4EA0] << 7;  [C04E] += t*8;  [C04C] += t*9` (i.e. += [4EA0]*0x400 and *0x480; [4EA0] is a constant 6 in the image, never written -> 0x1800 and 0x1B00 per frame (U: var 15/16 rotating parts such as radar/engine fans)); `memcpy(0x49B2, 0x498E, 18)`. Callers: 03CE, 119C, bf71.

### 5.3 `0000:DB71 r3d_set_viewport(Viewport *vp)`  (V)
```
[7D54] = -36 ; [7D56] = [7D58] = 36                          // (U: limits read by 11EB code)
W = vp->x1 - vp->x0 + 1 ; H = vp->y1 - vp->y0 + 1
[0D1E] = ((W+1) >> 1) - 1                                     // screen centre x, viewport relative (159 for 320)
[0D20] = ((H+1) >> 1) - 1                                     // screen centre y
if (vp->y0 == 0) [0D20] = [365E] ? ([294B] ? 60 : 52) : 100  // full-height views put the horizon centre at 100; side/rear views at 60/52
data_ce_set(W-1)  (slot 65, no effect) ; offset_set(pixel_offset(vp->x0, vp->y0))  (draw offset = y0*320+x0: polygons are viewport-relative)
[85CA]=x0 [85CC]=y0 [85CE]=x1 [85D0]=y1 [85D2]=x0+[0D1E] [85D4]=y0+[0D20]                  // absolute rect + centre for the polygon clipper (1359)
[2293] = W-1 ; [2295] = H-1                                    // clip maxima for the 1001 edge code
[85EA] = DS:85E2[vp->page]                                     // draw segment of the target page
[E53E],[E544] = (294B ? 48E6 : 48D6), (294B ? 4906 : 48F6) ; if [365E] [E544] += 0x20       // frustum radius tables for E8B5
```
### 5.4 `0000:DC64 r3d_set_camera_matrix(a1, a2, a3, focal)`  (V) -- sequence in 1.2 step 2. Callers: DAE8 only.
### 5.5 `0000:DCA9 r3d_set_origin(x, y, z)`  (V) `[497C]=x [497E]=y [4980]=z` (camera origin / per-cell origin for E7AC). Callers: 04DA, DAE8, DCC0, BD8F, BEE1 (0FFB writes the three words directly).

### 5.6 `0000:E26B r3d_matrix_from_angles(int16 m[9], A, B, C)`  (V)
cdecl `[bp+4]=m` (usually 0x498E), `[bp+6]=A`, `[bp+8]=B`, `[bp+0xA]=C`. Fills the temporaries `[4982]=sin A,[4984]=cos A,[4986]=sin B,[4988]=cos B,[498A]=sin C,[498C]=cos C` (Q15) and the 9 words per 2.4. Callers: DC64, 2F82, 2E43.
### 5.7 `0000:E54B r3d_transpose49A0()`  (V) transposes DS:49A0 in place. Callers: DF08, 0FFB, 13E3 (E7AC contains an inline copy).

### 5.8 `0000:E8B5 r3d_cull_core` / `0000:E8A2 r3d_cull_point(x, y, z)`  (V)
`E8A2` (cdecl `[bp+4]=x,[bp+6]=y,[bp+8]=z`, camera-relative east/north/up) loads BP=x, BX=y, CX=z and calls `E8B5`; returns AX (0 = potentially visible, 1 = culled, ZF set accordingly).
`E8B5` (register interface BP=x, BX=y, CX=z) inputs also `[497A]` = size class 0..7:
1. computes the view vector (formula in 2.1) -> `[4958]:[495A]` = A (right), `[495C]:[495E]` = B (up), `[4960]:[4962]` = C (depth), hi words are the values.
2. cull if `C.hi > [48B6]` (too far) or `C.hi < DS:48C6[2*class]` (behind; table -1,-23,-91,-182,-363,-725,-1450,-2900).
3. frustum, two variants:
   * `[4360] != 0` (wide / target-camera mode): `sx = ((|C|+DS:4936[class])>>3)`; `sx += sx>>3 + sx>>4` -> cull if `sx < |A|`; `sy = (|C|+DS:4946[class])>>2; sy += sy>>3` -> cull unless `sy > |B|`.
   * normal: `cl = [294B]^1`; `sx = (|C| + [E53E][class]) >> cl; sx += sx>>2` -> cull if `|A| > sx`; `sy = (|C| + [E544][class]) >> cl` (if `[365E]`: `sy = (sy + sy>>3) >> 1`) -> cull if `|B| > sy`.
   (the radius tables: 48D6 = 2,44,172,344,685,1368,2736,5472; 48E6 = 2,30,117,233,465,929,1857,3714; 48F6 = 3,50,197,394,786,1570,3139,6278; 4906 = 2,26,101,202,403,805,1609,3217; 4936 = 7,157,620,1239,2472,4938,9875,19751; 4946 = 6,149,589,1174,2351,4696,9393,18786; the +0x20 offset (4916) = 4,83,325,650,1297,2590,5179,10358.)
4. `[4978] = ((|A|+|B|)>>2) + |C|` (distance estimate); cull if `> [48B6]`. Returns 0 and `[4978]` set when visible.
Callers: E8A2 (04DA path A, DCC0), E7AC.

### 5.9 `0000:E7AC r3d_place_object(type, a1, a2, a3, X, Y, Z)`  (V)
cdecl: `[bp+4]=type` (bit 15 = destroyed variant, bits 8-14 = bank, low byte = index (low 7 bits used)), a1 `[bp+6]`, a2 `[bp+8]`, a3 `[bp+0xA]`, world/cell-relative X `[bp+0xC]`, Y `[bp+0xE]`, Z `[bp+0x10]`. Caller cleans 14 bytes.
```
[496A]=a1 [496C]=a2 [496E]=a3 ; [4956]=type ; bank=(type&0x7FFF)>>8 ; idx=(type&0x7F)
flags = byte[ DS:0A60[bank] + idx ] ; [497A] = flags & 7 ; [8560] = DS:B75E[bank]            // flag arrays: 0760 STFLT, 07E0 theatre, 0860 PHOTO, 08E0; bank segments B75E+2*bank
[4964]=X-[497C] [4966]=Y-[497E] [4968]=Z-[4980]
if (E8B5(...) != 0) return;                                                                    // culled
if (flags & 0x80) { DDFE(); return; }                                                         // sorted object: queue it (uses [4956],[4964..4968],[496A..496E],[4960:4962])
[C030] = destroyed ; [4894] = 1088:0120() (shade) ;
1433:03A9(-a1, -a2, a3, 0x49A0) ; transpose 49A0 ; 11EB:00D3(idx, [4964], [4968], -[4966], 0x49A0, 0xC02E)   // draw now
```
Callers: 04DA, BD8F, BEE1, bf71 (c455, c535, c76d).

### 5.10 `0000:E3E2 r3d_matrix_obj_side()`  (V)
Builds a 3x3 Q15 matrix at DS:49A0..49B0 from `[496A],[496C],[496E]` (a different angle composition than 1433:03A9). With (s1,c1) = sin/cos `[496A]`, (s2,c2) = `[496C]`, (s3,c3) = `[496E]` (stored to 4982..498C, all products `mul_q15`):
```
49A0 = c1*c3 - s1*s2*s3     49A2 = -(c2*s3)     49A4 = s1*c3 + c1*s2*s3
49A6 = c1*s3 + s1*s2*c3     49A8 =  c2*c3       49AA = s1*s3 - c1*s2*c3
49AC = -(c2*s1)             49AE =  s2          49B0 =  c2*c1
```
(V from e431..e549). Only used by DF08 for the type-0x18 aircraft: it is called first, then the dot products of the position `[4964],[4966],[4968]` with the rows `[49A6],[49A8],[49AA]` and `[49AC],[49AE],[49B0]` decide which of the sub-models 0x21/0x22 is drawn first (and whether the extra model 0x23 goes before/after); the matrix is then overwritten by the normal `1433:03A9` call (e0eb). Caller: DF08.

### 5.11 `0000:DF08 r3d_draw_queued()`  (V for structure, (U) for the meaning of several vars)
```
for (i = 0; i < [49C4]; i++) {                       // far -> near
    rec = DS:4E3C[i]  (see 3.2)
    if (rec.type & 0x8000) { [C030]=1; [C042]=[C044]=0 } else { [C030]=0; [C042]=[C044]=[9874] }
    if (rec.type & 0x0400) {                         // 2D sprite object: x=rec+0x10, y=+0x12, size=+0x14, class=type&7
        1AD6(x, y, size, class);  continue;
    }
    [4894]=rec.shade(+4) ; [4956]=type & 0x7FFF ; [4964..4968]=rec+0xA..0xE ; [496A..496E]=rec+0x10..0x14
    [8560] = DS:B75E[bank]                           // bank = [4957]
    if (bank == 0) {                                 // STFLT.3DX aircraft/weapons: set the var table
        var[12]=[C046] = (rec.flags & 3) != 0 ;  [C04A]=[C048]=[C03E]=[C03C]=[C03A]=[C038]=0
        switch (type) {
          case 1, 0xA, 0xF: [C04A] = 1;                     // var 14
          case 0x18:        player/special aircraft: if ([C072]==0) type = 0x1F; else { models 0x21,0x22 (+0x23) drawn as separate parts,
                               [C038]=1; [C03A]=[C072]-1; E3E2(); side/order decided by the matrix dot products; extra 0x23 if [B052] && ([9B04]&8) ... }
          default:          if ([4968] != 0 && !(rec.flags & 0x20)) [C048] = 1;      // var 13
        }
        if ([98D8]) [C03E] = [98D8];                       // var 8 (damage state, U)
    }
    1433:03A9(-[496A], -[496C], [496E], 0x49A0) ; E54B() (transpose) ;
    11EB:00D3(type & 0xFF, [4964], [4968], -[4966], 0x49A0, 0xC02E)       // the model draw; for 0x18 up to 3 calls with ids 0x21/0x22/0x23
    if (bank == 0 && type >= 0x25) {                                      // flame/glow overlay on the projected centre ([7D4E] -> screen x,y of vertex 0)
        pt = [7D4E] ; if (!(pt.y_hi & 0x80 || pt.x_hi & 0x80)) {            // on screen
          type == 0x25: 1B1B(x - 0xA9, y_stuff - 0x6E, size = rec.flags>>8, class = rec.flags & 7)
          type  > 0x25: 1B1B(x - 0xA9, pt[2] - 0x6E, size = DS:4EA2[type-0x25 (word idx)], class = type - 0x25 + 2) }
    }
}
```
(1B1B draws into the TV viewport `[4042]`, hence the subtraction of its window origin (169,110) (V); in the main viewport these overlay types are not expected.) Exact conditions of the 0x18 branch are at e0d8..e19d, port line by line (S3.7/S3.8).
`[C072]` (read here; written by 39C4 and CBA0) = state of the big special aircraft model (U), `[B052]` set by 1E9E (U).
Callers: DB3E, bf71 (direct). Callees: 1AD6, 1B1B, E3E2, E54B, 1433:03A9, 11EB:00D3, 109D trig (via 1433).

### 5.12 `0000:1AD6 r3d_sprite_main(x, y, size, class)` / `0000:1B1B r3d_sprite_tv(x, y, size, class)`  (V)
cdecl `[bp+4]=x [bp+6]=y [bp+8]=size [bp+0xA]=class(0..7)`. Both call `sprite_scaled` (driver slot 22) as
`sprite_scaled(ctx, x - size/2, y - size/2, size, size - size/4 /*(3/4)*/, DS:91D8[class].off, DS:91D8[class].seg)` with ctx = `[0344]` (main viewport, 1AD6) or `[4042]` (TV viewport, 1B1B). Sprite far pointers: 8 entries of (off, seg) at DS:91D8 (offsets 0,438,865,1266,1703,2120,2543,2946 in segment 0x1D50 in the image = the sprite pool, filled at load). 1AD6 caller: DF08; 1B1B caller: DF08.

### 5.13 `0000:EA26 pal_fade_step(RGB *cur, RGB *target, count)` -> changed  (V)
cdecl with FAR pointers: `[bp+4]:[bp+6]` cur (modified), `[bp+8]:[bp+0xA]` target, `[bp+0xC]` count of RGB triples (3 bytes each; DAC 6-bit values). For each component: if cur != target, step cur by +-1 toward target and count it. Returns (BX->AX) the number of components that changed this call (0 = fade finished). Callers: 4857 (fade palette 104A toward 164A/1C4A, count 256), 4D43 (partial palettes). Not 3D; belongs to the palette code.

---------------------------------------------------------------------------------------------------------------------------------------------

## 6. Segment 1001: 2D polygon edge tables + clipping (sky/ground fill, rectangles) (all (V) structurally; fine constants (U))

Working data (DGROUP): clip maxima `[2293]` = width-1, `[2295]` = height-1; current edge: `[2299]`=x1, `[229D]`=y1, `[229B]`=x2, `[229F]`=y2 (note the register loading order cx=[2299], dx=[229D], si=[229B], di=[229F]);
edge tables: **L table DS:22A1** and **R table DS:2459** (220 words each, 0x1B8 bytes apart, indexed by 2*y), dirty range `[2615]` = ymin (0xFFFF = empty), `[2617]` = ymax; scratch `[228A..2291]` and `[261A..2621]` (deltas, outcode); `[0D44]`=-sinC, `[0D46]` colour byte, `[0D48]` toggle.
The tables are consumed by driver slot 37 `poly_fill(ymin=[2615], tbl=22A1, ymax=[2617], mode)` (video.md section 5: rows ymax..ymin, L=*(tbl+2y), R=*(tbl+0x1B8+2y)).

| addr | name / signature | what it does |
|---|---|---|
| 1001:0006 | `void far hz_fill(int colour)` | `[0D46]=colour` (byte) then near `0018`. Called by 1442 when detail < 3 (flat sky/ground). |
| 1001:0018 | `void hz_draw()` (near) | **Horizon split**: `H = 256*tan(B)` = `([4986]<<8)/[4988]` (sin B / cos B), 0x3FF if cos B <= 0x1F0B; `H <<= [433A]` (zoom), `H >>= 1` if `[294B]`; `[0D44] = -[498A]` (-sin C). Builds the horizon line (end points x1,x2,y1,y2 into 2299..229F) through the screen centre `([0D1E],[0D20])` shifted by H along the roll direction (C), spanning about +-2*[0D1E] px (x1 = w2+2*w2*cosC+H*sinC, x2 = w2-2*w2*cosC+H*sinC, y components scaled by 3/4 with `v - v>>2`), clipped by 0336. If the line misses the screen (clip returns CF): whole viewport filled with colour `[0D46]` if `sin B < 0` (looking up) else colour **0x78** (ground); else fills the polygon below (colour [0D46] first side) and, via the `[0D48]` toggle, the other side with 0x78 (`6AC` edge walk, corner points by `55B`, then slot 37 with mode 0). Uses driver slots 32 (`color_set_r`) and 37. |
| 1001:01EC | `void far poly_fill(Viewport *vp, int n, int *pts, int colour)` | args `[bp+6]=vp (unused), [bp+8]=n, [bp+0xA]=pts (n x (x,y) words), [bp+0xC]=colour` : `color_set_r(colour)`, clears the tables (0525), walks the n edges through `06AC` (clip + Bresenham into L/R), closes the polygon, slot 37 mode 0. Called by 1442 for the 4-point sky/ground bands (colour 0x80+i, or 0x88 when `[bp-0x28]`), see 1442. |
| 1001:024C | `void far fill_rows(Viewport *vp, int xl, int ymin, int xr, int ymax)` | `[bp+6]=vp [bp+8]=xl [bp+0xA]=ymin [bp+0xC]=xr [bp+0xE]=ymax`: selects page (slot 13 `page_select_r` with `vp->page`), colour `vp+4` (slot 32), clears the tables, fills L=xl, R=xr for rows ymin..ymax, slot 40 `poly_fill_solid`, restores the old segment (slots 16/15). Used by bf71 (c3e2) to clear the TV window rectangle (xl=0xA9, xr=0xF5). |
| 1001:02BA | `void fill_rows2(...)` (NEAR ret, never called: dead) | same as 024C with colour `vp+6` and no `dx=0` setup. |
| 1001:0326 | `far` stub | `call 0336; retf`. (caller: 10A8:000C asm HUD) |
| 1001:032A | `far line_clip_draw()` | saves BP/SI/DI/ES, `0336`; used by 02FE/873F/87A9 callers (HUD/line code, other readers). Uses `[2299..229F]`. |
| 1001:0336 | `bool line_clip_draw()` (near) | **Cohen-Sutherland** clip of the segment `[2299,229D]-[229B,229F]` to `0..[2293]` x `0..[2295]` with outcodes (04C2); temporarily hooks INT 0 (divide error) to `04E3` to saturate overflowing `idiv`s; when accepted calls driver slot 31 `line(x1,y1,x2,y2)`; returns CF=0 drawn / CF=1 rejected. |
| 1001:04C2 | `byte outcode(bx=x, bp=y)` | AL bits: 1 = x > xmax, 2 = y > ymax, 4 = y < 0, 8 = x < 0; ZF = inside. |
| 1001:04E3 | INT 0 handler (iret) | divide overflow: result AX = +/-0x7F00 (sign = xor of operands, which operand pair chosen by the faulting IP == 0x474), DX=0; adds 4 to the saved IP to skip the idiv. Installed/removed by 0336. |
| 1001:0504 | `void far poly_flush(int mode)` | `poly_fill([2615], 22A1, [2617], mode)` slot 37 (mode 0 solid, 1 OR, 2 AND, >=3 green ramp). Caller: bf71 (c72c). |
| 1001:051F | `void far tables_clear()` | `call 0525; retf` (called by bf71). |
| 1001:0525 | `void tables_clear()` (near) | if `[2615]` valid: L entries `[ymin..ymax]` := 0xFFFF, `[2615]`=0xFFFF; R entries := 0, `[2617]`=0 (resets only the dirty range). |
| 1001:055B | `void edge_column(si=x, di=y_a, dx=y_b)` (near) | for an edge lying on the clip border: if x == 0 writes x into the L table, if x == xmax into the R table, for rows min..max(y_a,y_b) (clamped to 0..ymax), updating `[2615]/[2617]`; interior x: nothing. Entry `05AA` (bx = x): chooses si = 0 (bx < 0) or xmax (bx > xmax) for an off-screen edge, ret otherwise. |
| 1001:05B9 | (label inside the rasteriser) | **Bresenham edge walk**: sort end points by x, delta registers into `[2611]` (dx) `[2613]` (|dy|), step bp = +-2; x-major and y-major loops writing `L[y] = min(L[y], x)`, `R[y] = max(R[y], x)` and extending `[2615]/[2617]`. Ends with `ret`. |
| 1001:06A0 | `far edge_far()` | saves ES/SI/DI/BP, `06AC`, `retf`. |
| 1001:06AC | `void edge_to_tables()` (near) | clip + rasterise the edge `[2299..229F]` into the L/R tables: same Cohen-Sutherland structure as 0336 (outcode, INT 0 hook = `0854`), out-of-window parts go through `055B`/`05AA` as border columns; the visible part jumps to `05B9`. Used by 0018, 01EC. |
| 1001:0854 | INT 0 handler | like 04E3 for 06AC (IP compare 0x7CE, operands `[261D]`/`[261B]`). |

Edge-table conventions (for S3.7): L[y] initial 0xFFFF, R[y] initial 0, so an untouched row has R < L and `poly_fill` skips it. Polygons are filled with the sky/ground colours using slot 37 mode 0; the AND/OR modes are used by 3D polygons via 1359 (R11b) on the same tables.

---------------------------------------------------------------------------------------------------------------------------------------------

## 7. What the sim side must provide (parameters, summary)
* Scene entry: `03CE(...)` (see 1.1) -- once per frame from 2F82; with the player's world position (x,y: 20-bit within a 32-bit, z: 16-bit), `-heading`, pitch, roll; viewport (x0,y0,w,h).
* Sim object submission (R8): `DCC0(type, xlo, xhi, ylo, yhi, z, a1, a2, a3)` (queue after cull) -> stored in the sorted queue. Everything else (terrain, scenery) comes from the .3DG/.3DT data held in DGROUP (`DS:E304/061E`, `DS:B00C/9F68/9D10/9B08/9904`).
* Mission scenery state: override table `DS:B772` x `DS:9902` (destroyed/replaced objects) read by 04DA via 0FB1.
* Per-object state variables: `DS:C02E..C04E` (3.3).
* Detail level `[98DE]`, lens flag `[294B]`, side-view flag `[365E]`, zoom shift `[433A]`, wide mode `[4360]`, light level `[11E2]`.

## Open items
| item | note |
|---|---|
| 04DA path A i=9..14 | cells read from the neighbouring octant rows (overlap): duplicate cells drawn (U whether intentional). |
| `[0504]` | written/incremented but never read in the code seen (U). |
| `[262A]` clear site | set to 1 by 1BBC after the flip; the clearing write is not in any indexed function (timer ISR 1BA0?). |
| DF08 type 0x18 branch (`e0d8..e19d`) | player/special aircraft built from 2-3 sub-models; the exact meaning of `[C072]`, `[B052]`, `[9B04]` bit 3, `[2DF4]`, `[DFE0]` index (16-byte records at DS:B2AB..) not decoded. Port line by line. |
| DF08 overlay for types >= 0x25 | sprite offsets (0xA9,0x6E) point to the TV window; verify visually in the main view. |
| E3E2 | formulas for 49A0..49B0 not simplified (only used for the 0x18 order decision). |
| 1001:0018 | exact end-point formulas of the horizon line (3/4 aspect factors, sign of H) need a visual check; `0x78` ground colour; sky colour `[0D46]`=`[vp+4]`=0x88. |
| `[7D54..7D58]` = -36,36,36 | written by DB71, consumer unknown (11EB code?). |
| `DS:48A6[6]` = 0x20<<(detail+i), `[48B2]`, `[48B4]` | written by D76B, no reader found (U). |
| DS:49B2 matrix copy | written by DB3E, no reader (U). |
| unit of world coordinates | presumably feet (U). |
| `[0D22]` table | 16 words 0,2,3,4,6,8,11,15,20,27,36,48,64,87,119,164: sky/ground band offsets used by 1442 (copied to DS:C080 each frame) -- owner 1442. |
| BRIEF.md | DGROUP image base is 0x1EA50 (see top). |

## DGROUP globals seen
| offset | size | name | meaning | readers/writers |
|---|---|---|---|---|
| 0344 | w | r3d_main_vp_ptr | pointer to the main Viewport record (= 032E) | 03CE,1AD6,2F82 |
| 0346 | b | r3d_parity | toggled each scene (03CE) | 03CE |
| 0348 | w | r3d_near_flag | forces the detailed terrain path (bf71) | 04DA,bf71 |
| 034C | 8x9 w | terr_cell_order | per view-octant cell offsets (x, and y via octant+2), far -> near | 04DA |
| 04CC / 04DE | 9 w each | terr_cell_order_down | cell offsets when pitch < -60 deg | 04DA |
| 04FA | 6 w | terr_level_on | level enabled flags | 04DA |
| 0504 | w | terr_prio | record counter/priority (unused elsewhere) | 04DA |
| 0506 | 5 w | terr_level_dim | grid dims 1024,256,64,16,8 | 0865 |
| 0510 | 128 w | scenery_id_map | 3DT object byte -> bank<<8\|idx (c6b8) | 0994 (w), C6B8, 0FFB |
| 0610 | w | tdx_count | number of objects in the theatre .3DX | 0994 |
| 061E | 5x32 w | terr_rec_count | number of 7-byte records per (level,type) | 04DA,0AF9,0D08 |
| 0760/07E0/0860/08E0 | 128 b each | bank_flags | 3DX flag byte per object (bank 0 STFLT, 1 theatre, 2 PHOTO, 3 ?) | 0994, E7AC |
| 0A60 | 4 w | bank_flag_ptrs | near pointers to the four flag arrays | E7AC,72A4,BC12 |
| 0960 | 128 b | tdx_flags_copy | theatre flags copy incl. mission extras | 0994, bf71, 0FFB |
| 0D1E / 0D20 | w | r3d_cx / r3d_cy | screen centre (viewport relative) | DB71 (w), 1001:0018, 1442, 13AC |
| 0D22 | 16 w | hz_band_tab | band table for 1442 | 1442 |
| 2084 | 257 w | sin_tab | **sine table Q15** | 109D:0090, 2EBE |
| 2293 / 2295 | w | clip_xmax / clip_ymax | width-1 / height-1 | DB71, 1001, 17A3, ... |
| 2299,229B,229D,229F | w | edge_x1,x2,y1,y2 | current edge for 1001 | 1001 |
| 22A1 / 2459 | 220 w each | edge_L / edge_R | scan-edge tables | 1001, slot 37 |
| 2615 / 2617 | w | edge_ymin / edge_ymax | dirty range | 1001 |
| 262A | b | r3d_frame_wait | 1 after flip, spun on by DAE8 | 1BBC, DAE8 |
| 294B | b | lens_narrow | 0/1 lens flag (halves focal in 03CE/1001/1442) | many |
| 365E | w | view_side | (C07A & C0) != 0 | 2F82 (w) |
| 4042 | w | tv_vp_ptr(=402C) | target-camera viewport | bf71,1B1B |
| 4360 | w | r3d_wide | wide frustum / target camera mode | bf71, E8B5, 1442 |
| 433A | b | zoom_shift | focal shift | 2F82,bf71 |
| 4894 | b | obj_shade | colour shift for shaded fills (11EB/1359) | 119C,DF08,E7AC |
| 48B6 | w | max_depth | = 0x0D05*(detail+1) | D76B |
| 48C6 | 8 w | min_depth_tab | per size class | E8B5 |
| 48D6/48E6/48F6/4906/4916/4936/4946 | 8 w each | frustum_radius tabs | see 5.8 | E8B5, DB71 |
| 4956 | w | cur_type | type being placed | DF08,E7AC,DCC0 |
| 4958/495C/4960 | s32 | view_A/B/C | view-space vector | E8B5 (ddfe reads 4960) |
| 4964/4966/4968 | s16 | rel_e/n/u | camera-relative position | E7AC,DF08,DDFE |
| 496A/496C/496E | s16 | cur_ang1..3 | object angles | E7AC,DF08,E3E2 |
| 4978 | w | cur_dist | distance estimate | E8B5 |
| 497A | w | cur_class | size class 0..7 | E7AC,04DA,DCC0 |
| 497C/497E/4980 | s16 | origin_x/y/z | camera/cell origin | DCA9 |
| 4982..498C | 6 w | sc_tmp | sin/cos temporaries (A,B,C) | E26B,E3E2 (1442,1001 read) |
| 498E | 9 w | cam_matrix | Q15 | E26B, E8B5 |
| 49A0 | 9 w | obj_matrix | Q15 | 1433:03A9,E3E2,E54B |
| 49B2 | 9 w | cam_matrix_copy | copy | DB3E |
| 49C4 | w | queue_count | | DDFE,DAE8,DF08,bf71 |
| 49C6 | w | rec_flags | | 9ACA,BCD7,C834 |
| 49F0 | 50x22 b | queue_pool | | DDFE |
| 4E3C | 50 w | queue_list | sorted record pointers | DDFE,DF08 |
| 4EA0 | w | spin_step | const 6 | DB3E |
| 4EA2 | 8 w | glow_size_tab | 6,8,12,16,20,24,30,36 | DF08 |
| 7D50 | w | colour_tab_ptr | -> 7D60/7F60/8160/8360 | 03CE,bf71,11EB |
| 8560 | w | cur_bank_seg | segment of the current 3DX bank | DAE8,DF08,E7AC,11EB |
| 85CA..85D4 | 6 w | vp_abs | x0,y0,x1,y1,cx,cy absolute | DB71 |
| 85EA | w | draw_seg | | DB71 |
| 91D8 | 8x(w,w) | sprite_ptrs | far ptrs of 2D object sprites | 1AD6,1B1B |
| 94D6/94D8/94DA | w | cell_rel_x/y/alt | camera relative to the current cell (level units) | 04DA |
| 94DC | w | ov_idx | | 0FB1 |
| 9874 | w | lights | 1 at dusk/night | 03CE (w), 04DA, DF08 |
| 98DE | w | detail | 0..3 | 3B05,CCD8,D76B |
| 9902 | w | override_count | | START |
| 9904/9B08/9D10 | 512 b ea. | child tables levels 0,1,2 | | 0865 |
| 9F68 | 256 b | level3_map | | 0865 |
| B00C | 64 b | level4_map | | 0865 |
| B75E | 3 w | bank_segs | 3DX bank segments | BD24 (w), DF08, E7AC |
| B760 | w | bank1_seg | theatre bank segment | 0994 (w), DAE8 |
| B772 | n x 5 b | override_tab | | START, 0FB1 |
| C02E | 32 w | var_tab | 3DX var table | 03CE,04DA,DF08,E7AC,DB3E |
| C686 | w | drawn_flag | any polygon drawn | DAE8,11EB |
| E2FC | w | terr_level | current level | 04DA,DCC0,DDFE |
| E304 | 5x32 w | terr_rec_ptr | near pointer to each (level,type) record list | 0AF9 (w), 04DA |
| E44A | w | terr_rec_cur | current record pointer | 04DA |
| E53E / E544 | w | frustum_tab_ptrs | -> 48D6/48E6, 48F6/4906 | DB71 |

## Structures seen
* **Viewport** (3.1): 0x16 bytes used: +0 page, +4 bg, +6 colour2, +0xE y0, +0x10 y1, +0x12 x0, +0x14 x1. DS:032E (main), DS:402C (TV).
* **Queued object record** 0x16 bytes (3.2), pool DS:49F0 x 50, list DS:4E3C.
* **Terrain scenery record** 7 bytes: s16 x, s16 y, s16 z (cell-centre relative, level units), u8 obj (bit 7 = override-able, low 7 bits = scenery index -> DS:0510); list pointers DS:E304[level*32+type], counts DS:061E.
* **Override entry** 5 bytes: level, index in the tile list, cellx, celly, new object; table DS:B772, count DS:9902 (searched from the end).
* **Matrices** 9 x s16 Q15 (2.4).
* **3DX var table** DS:C02E (3.3).
