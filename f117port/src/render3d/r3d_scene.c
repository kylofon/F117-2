/* The scene layer of render3d (spec port/spec/render3d.md Part 1): the scene entry 03CE, set-up DAE8 / DB71 / DC64 / DCA9, the end DB3E, the object matrices
 * E3E2 / E54B, placement E7AC, the cull E8A2 / E8B5, the depth shade 1088:0120, the sim-object submitters BD8F / BEE1 / DCC0, the sorted queue DDFE / DF08 and the
 * 2D sprites 1AD6 / 1B1B / 1A85. All state is VGAME's DGROUP at the original offsets. */
#include "render3d/r3d.h"
#include "sim/sim.h"
#include "sim/sim_priv.h"

#define LONG(o) ((s32)DSL(o))

static inline s16 abs16(s16 v) { return (s16)(v < 0 ? (u16)-(u16)v : (u16)v); }       /* 0000:F302: abs16(-32768) = -32768 */
static inline s16 mq(s16 a, s16 b) { return (s16)fx_mul15(a, b); }                     /* 109D:0006 */

/* F45E / F46A: the runtime's 32-bit shifts loop CL times (a count >= 32 gives 0 / the sign fill) */
static s32 shl32(s32 v, unsigned n) { n &= 0xFF; return n >= 32 ? 0 : (s32)((u32)v << n); }
static s32 sar32(s32 v, unsigned n) { n &= 0xFF; return n >= 32 ? (v < 0 ? -1 : 0) : v >> n; }

/* ------------------------------------------------------------------------------------------------ set-up */

/* 0000:DCA9 r3d_set_origin */
void r3d_set_origin(s16 x, s16 y, s16 z) { DSW(0x497C) = (u16)x; DSW(0x497E) = (u16)y; DSW(0x4980) = (u16)z; }

/* 0000:DB71 r3d_set_viewport (vp = DS offset of the Viewport: +0 page, +0E y0, +10 y1, +12 x0, +14 x1) */
void r3d_set_viewport(u16 vp)
{
    DSW(0x7D54) = 0xFFDC; DSW(0x7D56) = 0x24; DSW(0x7D58) = 0x24;
    s16 x0 = DSS((u16)(vp + 0x12)), y0 = DSS((u16)(vp + 0x0E)), x1 = DSS((u16)(vp + 0x14)), y1 = DSS((u16)(vp + 0x10));
    s16 W = (s16)(x1 - x0 + 1), H = (s16)(y1 - y0 + 1);
    DSW(0x0D1E) = (u16)(((s16)(W + 1) >> 1) - 1);
    DSW(0x0D20) = (u16)(((s16)(H + 1) >> 1) - 1);
    if (y0 == 0) DSW(0x0D20) = DSW(0x365E) ? (DSB(0x294B) == 0 ? 0x34 : 0x3C) : 0x64;
    data_ce_set(W - 1);
    offset_set(pixel_offset(x0, y0));
    DSW(0x85CA) = (u16)x0; DSW(0x85CC) = (u16)y0; DSW(0x85CE) = (u16)x1; DSW(0x85D0) = (u16)y1;
    DSW(0x85D2) = (u16)(x0 + DSW(0x0D1E)); DSW(0x85D4) = (u16)(y0 + DSW(0x0D20));
    DSW(0x2293) = (u16)(W - 1); DSW(0x2295) = (u16)(H - 1);
    DSW(0x85EA) = DSW((u16)(0x85E2 + 2 * DSW(vp)));
    DSW(0xE53E) = 0x48D6; DSW(0xE544) = 0x48F6;
    if (DSB(0x294B)) { DSW(0xE53E) = 0x48E6; DSW(0xE544) = 0x4906; }
    if (DSW(0x365E)) DSW(0xE544) += 0x20;
}

/* 0000:DC64 r3d_set_camera_matrix: the cull matrix 498E (E26B), the model matrix 49A0 and the scaled camera matrix 7AF8 (11EB:000C) */
void r3d_set_camera_matrix(s16 a1, s16 a2, s16 a3, s16 focal)
{
    sim_matrix_from_angles(0x498E, (s16)-a1, (s16)-a2, (s16)-a3);
    mat_from_euler((u16)-a1, (u16)-a2, (u16)a3, 0x49A0);
    r3d_set_camera(0x49A0, focal);
}

/* 0000:DAE8 r3d_begin_scene */
void r3d_begin_scene(u16 vp, s16 a1, s16 a2, s16 a3, s16 ox, s16 oy, s16 oz, s16 backdrop, s16 focal)
{
    r3d_set_viewport(vp);
    r3d_set_camera_matrix(a1, a2, a3, focal);
    r3d_set_origin(ox, oy, oz);
    if (backdrop) {
        DSW(0xC686) = 0;
        DSW(0x8560) = DSW(0xB760);
        r3d_shared_verts();
        while (DSB(0x262A)) host_pump();                                /* the frame tick of the timer ISR clears it */
        r3d_draw_sky_ground(DSP(vp));
    }
    DSW(0x49C4) = 0;
}

/* 0000:DB3E r3d_end_scene: the queue, offset_clear, the spinner vars 15 / 16, the dead copy 498E -> 49B2 */
void r3d_end_scene(void)
{
    r3d_draw_queued();
    offset_clear();
    u16 t = (u16)(DSW(0x4EA0) << 7);
    DSW(0xC04E) += (u16)(t << 3);
    DSW(0xC04C) += (u16)((t << 3) + t);
    for (int i = 0; i < 0x12; i += 2) DSW((u16)(0x49B2 + i)) = DSW((u16)(0x498E + i));
}

/* ------------------------------------------------------------------------------------------------ matrices */

/* 0000:E3E2 r3d_matrix_obj_side: the object matrix of the angles 496A..496E (the side tests of DF08) */
void r3d_matrix_obj_side(void)
{
    s16 s1 = (s16)fx_sin16(DSW(0x496A)), c1 = (s16)fx_cos16(DSW(0x496A)), s2 = (s16)fx_sin16(DSW(0x496C)), c2 = (s16)fx_cos16(DSW(0x496C));
    s16 s3 = (s16)fx_sin16(DSW(0x496E)), c3 = (s16)fx_cos16(DSW(0x496E));
    DSW(0x4982) = (u16)s1; DSW(0x4984) = (u16)c1; DSW(0x4986) = (u16)s2; DSW(0x4988) = (u16)c2; DSW(0x498A) = (u16)s3; DSW(0x498C) = (u16)c3;
    DSS(0x49A0) = (s16)(mq(c1, c3) - mq(s1, mq(s2, s3)));
    DSS(0x49A6) = (s16)(mq(c1, s3) + mq(s1, mq(s2, c3)));
    DSS(0x49AC) = (s16)-mq(s1, c2);
    DSS(0x49A2) = (s16)-mq(s3, c2);
    DSS(0x49A8) = mq(c3, c2);
    DSS(0x49AE) = s2;
    DSS(0x49A4) = (s16)(mq(s1, c3) + mq(c1, mq(s2, s3)));
    DSS(0x49AA) = (s16)(mq(s1, s3) - mq(c1, mq(s2, c3)));
    DSS(0x49B0) = mq(c2, c1);
}

/* 0000:E54B r3d_transpose49A0 */
void r3d_transpose49A0(void)
{
    static const u16 pr[3][2] = { { 0x49A2, 0x49A6 }, { 0x49A4, 0x49AC }, { 0x49AA, 0x49AE } };
    for (int i = 0; i < 3; i++) { u16 t = DSW(pr[i][0]); DSW(pr[i][0]) = DSW(pr[i][1]); DSW(pr[i][1]) = t; }
}

/* ------------------------------------------------------------------------------------------------ cull, shade, placement */

/* 0000:E8B5 r3d_cull_core (BP = x, BX = y, CX = z, class [497A]): 1 = culled */
int r3d_cull_core(s16 x, s16 y, s16 z)
{
#define M(k) ((s32)DSS(0x498E + 2 * (k)))
    s32 A = (s32)(((u32)(M(6) * y) + (u32)(M(3) * z) + (u32)(M(0) * x)) << 1);
    s32 B = (s32)(((u32)(M(7) * y) + (u32)(M(4) * z) + (u32)(M(1) * x)) << 1);
    s32 C = (s32)(((u32)(M(8) * y) + (u32)(M(5) * z) + (u32)(M(2) * x)) << 1);
#undef M
    DSSL(0x4958) = A; DSSL(0x495C) = B; DSSL(0x4960) = C;
    s16 Ah = (s16)((u32)A >> 16), Bh = (s16)((u32)B >> 16), Ch = (s16)((u32)C >> 16);
    u16 cls = (u16)(2 * DSW(0x497A));
    s16 far_ = DSS(0x48B6), d;
    if (Ch > far_) return 1;
    if (Ch < DSS((u16)(0x48C6 + cls))) return 1;
    if (DSW(0x4360)) {
        s16 s = (s16)((s16)(abs16(Ch) + DSS((u16)(0x4936 + cls))) >> 3);
        s16 t = (s16)(s >> 3);
        s16 sx = (s16)(s + t);
        sx = (s16)(sx + (t >> 1));
        if (sx < abs16(Ah)) return 1;
        s16 s2 = (s16)((s16)(abs16(Ch) + DSS((u16)(0x4946 + cls))) >> 2);
        s16 sy = (s16)(s2 + (s2 >> 3));
        if (!(sy > abs16(Bh))) return 1;
        d = (s16)(((s16)(sy + abs16(Bh)) >> 2) + Ch);
    } else {
        int cl = (DSB(0x294B) ^ 1) & 0x1F;
        s16 aC = abs16(Ch);
        s16 t = (s16)((s16)(aC + DSS((u16)(DSW(0xE53E) + cls))) >> cl);
        s16 sx = (s16)((t >> 2) + t);
        if (abs16(Ah) > sx) return 1;
        s16 u = (s16)((s16)(aC + DSS((u16)(DSW(0xE544) + cls))) >> cl);
        s16 by = DSB(0x365E) ? (s16)((s16)((u >> 3) + u) >> 1) : u;
        if (abs16(Bh) > by) return 1;
        d = (s16)(((s16)(abs16(Ah) + abs16(Bh)) >> 2) + aC);
    }
    DSW(0x4978) = (u16)d;
    return d > far_ ? 1 : 0;
}

/* 0000:E8A2 r3d_cull_point */
int r3d_cull_point(s16 x, s16 y, s16 z) { return r3d_cull_core(x, y, z); }

/* 1088:0120 r3d_depth_shade: the colour shift of the shaded ramps (0..15) */
u16 r3d_depth_shade(void)
{
    if (DSB(0x7D5E)) return 0;
    s8 v = (s8)DSB(0x4963);
    if (v < 0) v = 0;
    v = (s8)(v >> 1);
    u8 h = (u8)((u8)v - DSB(0x497A));
    if (h & 0x80) h = 0;
    if ((s8)h > 0x1F) h = 0x1F;
    if (DSB(0x43B6) == 0) return 0;
    return (u16)(h >> 1);
}

/* 0000:C6B8 r3d_scenery_id: bank-1 ids through the 0510 map (bit 8 only is tested), bit 15 kept */
u16 r3d_scenery_id(u16 id)
{
    if (id & 0x0100) return (u16)(DSW((u16)(0x0510 + 2 * (id & 0x7F))) | (id & 0x8000));
    return id;
}

/* 0000:E7AC r3d_place_object: cull, then queue (flag bit 7) or draw now */
void r3d_place_object(u16 type, s16 a1, s16 a2, s16 a3, s16 X, s16 Y, s16 Z)
{
    DSW(0x496A) = (u16)a1; DSW(0x496C) = (u16)a2; DSW(0x496E) = (u16)a3; DSW(0x4956) = type;
    u16 dead = (type & 0x8000) ? 1 : 0;
    u16 bank = (u16)((type & 0x7FFF) >> 8), idx = type & 0x7F;
    u8 flags = DSB((u16)(DSW((u16)(0x0A60 + 2 * bank)) + idx));
    DSW(0x497A) = flags & 7;
    DSW(0x8560) = DSW((u16)(0xB75E + 2 * bank));
    DSW(0x4966) = (u16)(Y - DSW(0x497E)); DSW(0x4968) = (u16)(Z - DSW(0x4980)); DSW(0x4964) = (u16)(X - DSW(0x497C));
    if (r3d_cull_core(DSS(0x4964), DSS(0x4966), DSS(0x4968))) return;
    if (flags & 0x80) { r3d_queue_insert(); return; }
    DSW(0xC030) = dead;
    DSB(0x4894) = (u8)r3d_depth_shade();
    mat_from_euler((u16)-a1, (u16)-a2, (u16)a3, 0x49A0);
    r3d_transpose49A0();
    r3d_draw_object(idx, DSS(0x4964), DSS(0x4968), (s16)-DSS(0x4966), 0x49A0, 0xC02E);
}

/* 0000:BD8F r3d_draw_model: a sim object at world (x, y, alt) relative to the camera, scaled by the lod shift */
void r3d_draw_model(u16 model, s32 x, s32 y, s16 alt, s16 ang1, s16 ang2, s16 ang3, s16 lod)
{
    u16 mdl = r3d_scenery_id(model);
    s32 rx = (s32)((u32)x - (u32)LONG(0xBA10));
    s32 ry = (s32)((u32)y + (u32)LONG(0xC02A) - 0x01000000u);
    s16 rz = (s16)(alt - DSS(0x2DF4));
    if (DSB(0xC07A) & 0x80) {
        rx = (s32)((u32)rx - (u32)LONG(0xB288) + (u32)LONG(0xBA10));
        ry = (s32)((u32)ry - (u32)LONG(0xC02A) + (u32)LONG(0xB752));
        rz = (s16)(rz - DSS(0xB75C) + DSS(0x2DF4));
    }
    s16 sh = (s16)(lod - (DSB(0x294B) == 0 ? 3 : 2));
    if (sh > 0) { rx = shl32(rx, (unsigned)sh); ry = shl32(ry, (unsigned)sh); rz = (s16)(u16)((u16)rz << (sh & 31)); }
    if (sh < 0) { unsigned n = (unsigned)(u16)-sh; rx = sar32(rx, n); ry = sar32(ry, n); rz = (s16)(rz >> (n & 31)); }
    if (abs16((s16)rx) < 0x7FFF && abs16((s16)ry) < 0x7FFF) {
        r3d_set_origin(0, 0, (s16)-rz);
        DSW(0xE2FC) = 1;
        r3d_place_object(mdl, (s16)-ang1, ang2, ang3, (s16)rx, (s16)-(s16)ry, alt != 0 ? 1 : 0);
    }
}

/* 0000:BEE1 r3d_queue_object: BD8F for the TV camera of bf71 (B766 / B76A / B808), no C6B8, no lod shift */
void r3d_queue_object(u16 id, s32 x, s32 y, s16 z, s16 a1, s16 a2, s16 a3)
{
    s32 ry = (s32)((u32)LONG(0xB76A) + (u32)y - 0x01000000u);
    s16 rz = (s16)(z - DSS(0xB808));
    s32 rx = (s32)((u32)x - (u32)LONG(0xB766));
    if (abs16((s16)rx) >= 0x7FFF || abs16((s16)ry) >= 0x7FFF) return;
    r3d_set_origin(0, 0, (s16)-rz);
    DSW(0xE2FC) = 1;
    r3d_place_object(id, (s16)-a1, a2, a3, (s16)rx, (s16)-(s16)ry, z != 0 ? 1 : 0);
}

/* 0000:DCC0 r3d_queue_billboard: a 2D sprite record (or billboard) in the sorted queue */
void r3d_queue_billboard(u16 id, s32 x, s32 y, s16 z, s16 sx, s16 sy, s16 size)
{
    s32 rx = (s32)((u32)x - (u32)LONG(0xBA10));
    s32 ry = (s32)((u32)y + (u32)LONG(0xC02A) - 0x01000000u);
    s16 rz = (s16)(z - DSS(0x2DF4));
    if (DSB(0xC07A) & 0x80) {
        rx = (s32)((u32)rx - (u32)LONG(0xB288) + (u32)LONG(0xBA10));
        ry = (s32)((u32)ry - (u32)LONG(0xC02A) + (u32)LONG(0xB752));
        rz = (s16)(rz - DSS(0xB75C) + DSS(0x2DF4));
    }
    unsigned n = DSB(0x294B) == 0 ? 3 : 2;
    ry = sar32(ry, n); rz = (s16)(rz >> n); rx = sar32(rx, n);
    if (abs16((s16)rx) >= 0x7FFF || abs16((s16)ry) >= 0x7FFF) return;
    r3d_set_origin(0, 0, (s16)-rz);
    DSW(0xE2FC) = 1;
    DSW(0x4964) = (u16)((s16)rx - DSS(0x497C));
    DSW(0x4966) = (u16)((s16)-(s16)ry - DSS(0x497E));
    DSW(0x4968) = (u16)((z != 0 ? 1 : 0) - DSS(0x4980));
    DSW(0x497A) = 2;
    if (r3d_cull_point(DSS(0x4964), DSS(0x4966), DSS(0x4968))) return;
    DSW(0x4956) = id; DSW(0x496A) = (u16)sx; DSW(0x496C) = (u16)sy; DSW(0x496E) = (u16)size;
    r3d_queue_insert();
}

/* ------------------------------------------------------------------------------------------------ the sorted queue */

#define QPTR(k) DSW((u16)(0x4E3C + 2 * (k)))

/* 0000:DDFE r3d_queue_insert: a 16h-byte record keyed by the view depth; descending order, index 0 = farthest */
void r3d_queue_insert(void)
{
    u16 e;
    if (DSW(0x49C4) == 50) {
        e = QPTR(0);
        for (int k = 1; k < 50; k++) QPTR(k - 1) = QPTR(k);
        DSW(0x49C4)--;
    } else e = (u16)(0x49F0 + 0x16 * DSW(0x49C4));
    s32 key = sar32(DSSL(0x4960), (u8)((4 - DSB(0xE2FC)) * 2));
    if (DSW(0xE2FC) == 2 && DSW(0x497A) == 5) key = (s32)((u32)key + 0x00200000u);
    DSSL(e) = key;
    DSW((u16)(e + 4)) = (u8)r3d_depth_shade();
    DSW((u16)(e + 6)) = DSW(0x49C6); DSW((u16)(e + 8)) = DSW(0x4956);
    DSW((u16)(e + 0xA)) = DSW(0x4964); DSW((u16)(e + 0xC)) = DSW(0x4966); DSW((u16)(e + 0xE)) = DSW(0x4968);
    DSW((u16)(e + 0x10)) = DSW(0x496A); DSW((u16)(e + 0x12)) = DSW(0x496C); DSW((u16)(e + 0x14)) = DSW(0x496E);
    s16 n = DSS(0x49C4), i;
    for (i = (s16)(n - 1); i > -1; i--) {
        u16 r = QPTR(i);
        s16 rh = DSS((u16)(r + 2)), kh = (s16)((u32)key >> 16);
        if (rh < kh) continue;
        if (rh > kh) break;
        if (DSW(r) <= (u16)key) continue;
        break;
    }
    for (s16 k = (s16)(n - 1); k > i; k--) QPTR(k + 1) = QPTR(k);
    QPTR(i + 1) = e;
    DSW(0x49C4)++;
}

/* 0000:1AD6 r3d_sprite_main / 0000:1B1B r3d_sprite_tv / 0000:1A85 fx_explosion_sprite: a scaled 2D sprite centred on (x, y) */
static void sprite(u16 ctx, u16 tab, s16 x, s16 y, s16 size, s16 cls)
{
    s16 dh = (s16)(size - (size >> 2));                                 /* the y offset is half of dh, not of size (cwd on the dh still in AX) */
    sprite_scaled(DSP(ctx), x - size / 2, y - dh / 2, size, dh, DSW((u16)(tab + 4 * cls)), DSW((u16)(tab + 4 * cls + 2)));
}
void r3d_sprite_main(s16 x, s16 y, s16 size, s16 cls) { sprite(DSW(0x0344), 0x91D8, x, y, size, cls); }
void r3d_sprite_tv(s16 x, s16 y, s16 size, s16 cls) { sprite(DSW(0x4042), 0x91D8, x, y, size, cls); }
void fx_explosion_sprite(s16 x, s16 y, s16 size, s16 cls) { sprite(DSW(0x0348) ? DSW(0x4042) : DSW(0x0344), 0x9188, x, y, size, cls); }

static void draw_rec_model(u16 id) { r3d_draw_object(id, DSS(0x4964), DSS(0x4968), (s16)-DSS(0x4966), 0x49A0, 0xC02E); }

/* 0000:DF08 r3d_draw_queued: the queue farthest first; the player's model 0x18 with its parts, the glow overlays of ids >= 25h */
void r3d_draw_queued(void)
{
    u16 m1 = 0x21, m2 = 0x22, extra_first = 0;                          /* locals of the original, kept across records */
    for (s16 i = 0; i < DSS(0x49C4); i++) {
        u16 rec = QPTR(i), v;
        if (DSW((u16)(rec + 8)) & 0x8000) { DSW(0xC030) = 1; v = 0; } else { DSW(0xC030) = 0; v = DSW(0x9874); }
        DSW(0xC044) = v; DSW(0xC042) = v;
        u16 t = DSW((u16)(rec + 8));
        if (t & 0x0400) { r3d_sprite_main(DSS((u16)(rec + 0x10)), DSS((u16)(rec + 0x12)), DSS((u16)(rec + 0x14)), (s16)(t & 7)); continue; }
        DSB(0x4894) = DSB((u16)(rec + 4));
        DSW(0x4956) = t & 0x7FFF;
        DSW(0x4964) = DSW((u16)(rec + 0xA)); DSW(0x4966) = DSW((u16)(rec + 0xC)); DSW(0x4968) = DSW((u16)(rec + 0xE));
        DSW(0x496A) = DSW((u16)(rec + 0x10)); DSW(0x496C) = DSW((u16)(rec + 0x12)); DSW(0x496E) = DSW((u16)(rec + 0x14));
        u8 bank = DSB(0x4957);
        DSW(0x8560) = DSW((u16)(0xB75E + 2 * bank));
        if (bank == 0) {
            DSW(0xC04A) = 0; DSW(0xC048) = 0; DSW(0xC046) = 0; DSW(0xC03E) = 0; DSW(0xC03C) = 0; DSW(0xC03A) = 0; DSW(0xC038) = 0;
            if (DSW((u16)(rec + 6)) & 3) DSW(0xC046) = 1;
            u16 id = DSW(0x4956);
            if (id == 1 || id == 0x0A || id == 0x0F) DSW(0xC04A) = 1;
            else if (id == 0x18) {
                if (DSW(0xC072) == 0) DSW(0x4956) = 0x1F;
                else {
                    m1 = 0x21; m2 = 0x22; DSW(0xC038) = 1; DSW(0xC03A) = (u16)(DSW(0xC072) - 1);
                    r3d_matrix_obj_side();
                    s16 alt = DSS(0x2DF4);
                    if (DSB((u16)(0xB2AB + DSW(0xDFE0) * 16)) & 2) alt = (s16)(alt - 0x80);
                    if (DSW(0xB052) != 0 && alt == 0 && (DSB(0x9B04) & 8)) {
                        DSW(0xC03C) = 1;
                        s16 s = (s16)(mq(DSS(0x4966), DSS(0x49B0)) + (s16)(mq(DSS(0x4968), DSS(0x49AE)) + mq(DSS(0x4964), DSS(0x49AC))));
                        extra_first = s < 0 ? 1 : 0;
                    }
                    s16 s = (s16)(mq(DSS(0x4966), DSS(0x49AA)) + (s16)(mq(DSS(0x4968), DSS(0x49A8)) + mq(DSS(0x4964), DSS(0x49A6))));
                    if (s < 0) { m1 = 0x22; m2 = 0x21; }
                }
                if (DSW(0x98D8) != 0) DSW(0xC03E) = DSW(0x98D8);
            } else if (DSW(0x4968) != 0 && !(DSW((u16)(rec + 6)) & 0x20)) DSW(0xC048) = 1;
        }
        mat_from_euler((u16)-DSW(0x496A), (u16)-DSW(0x496C), DSW(0x496E), 0x49A0);
        r3d_transpose49A0();
        if (DSW(0x4956) == 0x18) {
            if (DSW(0xC03C) && extra_first) draw_rec_model(0x23);
            draw_rec_model(m1);
            draw_rec_model(m2);
            if (DSW(0xC03C) && !extra_first) draw_rec_model(0x23);
            continue;
        }
        draw_rec_model(DSB(0x4956));
        if (DSW(0x4956) >= 0x100 || DSW(0x4956) < 0x25) continue;
        u16 e = DSW(0x7D4E);
        if (DSW((u16)(e + 2)) & 0x8080) continue;
        s16 size, cls;
        if (DSW(0x4956) == 0x25) { size = (s16)(DSW((u16)(rec + 6)) >> 8); cls = (s16)(DSW((u16)(rec + 6)) & 7); }
        else { u16 k = (u16)(DSW(0x4956) - 0x25); size = DSS((u16)(0x4EA2 + 2 * k)); cls = (s16)(k + 2); }
        r3d_sprite_tv((s16)(DSS(e) - 0xA9), (s16)(DSS((u16)(e + 4)) - 0x6E), size, cls);
    }
}

/* ------------------------------------------------------------------------------------------------ the scene entry */

/* 0000:03CE r3d_scene_draw(-heading, pitch, roll, x, y, z, x0, y0, w, h): the main 3D view into the window of the back page.
 * In the sim flow's differential tests (sim_test.active) the emulator hooks 03CE, so the port does nothing either; --render-test runs it in full. */
void r3d_scene_draw(int yaw, int pitch, int roll, s32 x, s32 y, s32 z, int x0, int y0, int w, int h)
{
    if (sim_test.active && !sim_test.render) return;
    u16 vp = DSW(0x0344);
    DSW((u16)(vp + 0x0E)) = (u16)y0; DSW((u16)(vp + 0x10)) = (u16)(y0 + h - 1);
    DSW((u16)(vp + 0x12)) = (u16)x0; DSW((u16)(vp + 0x14)) = (u16)(x0 + w - 1);
    DSW(vp) = (u16)(flip_flag() & 0xFF);
    frame_delay((s16)DSW(0x43B8));
    DSW((u16)(vp + 4)) = 0x88;
    DSW(0x7D50) = DSW(0x98DE) == 3 ? 0x7F60 : 0x8360;
    DSW(0xC030) = 0; DSW(0xC044) = 0; DSW(0xC042) = 0; DSW(0x9874) = 0;
    if (DSB(0x11E2) < 0x14) { DSW(0xC044) = 1; DSW(0xC042) = 1; DSW(0x9874) = 1; }
    s16 focal = (DSW(0x365E) != 0 && DSB(0x294B) != 0) ? 0x80 : 0x100;
    r3d_begin_scene(vp, (s16)yaw, (s16)pitch, (s16)roll, 0, 0, (s16)z, 1, focal);
    r3d_draw_terrain((s16)yaw, (s16)pitch, x, y, z);
    r3d_draw_world_objects();
    r3d_end_scene();
    DSW(0x7D50) = 0x7D60;
    obj_overlay_frame();
    DSB(0x0346) ^= 1;
}
