/* The sensor picture window of the right MFD (spec simflow.md Part 9 "bf71"): x 169..245, y 110..169 of the screen. The sky / ground backdrop bands, the FLIR tint and
 * the render function itself; the 3D scene calls inside are render3d's stubs (TODO(S5.6)). */
#include <stdio.h>
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"
#include "sim/sim_stubs.h"

#define LONG(o) ((s32)DSL(o))
#define SETL(o, v) (DSL(o) = (u32)(v))

static void ds_itoa(int v, u16 buf) { snprintf((char *)DSP(buf), 12, "%d", (s16)v); }

/* 0000:1961 mfd_view_sky_bands (spec 2.2): up to 16 sky quads, colour 80h+i, above the horizon row, clamped at y = 6Eh */
void mfd_view_sky_bands(int horizon)
{
    s16 q[8];
    for (int i = 0; i < 16; i++) {
        int done = 0;
        s16 top = (s16)(horizon - (s16)DSW((u16)(0x0D24 + 2 * i)));
        if (top <= 0x6E) { top = 0x6E; done = 1; }
        s16 bot = (s16)(horizon - (s16)DSW((u16)(0x0D22 + 2 * i)));
        q[0] = 0xA9; q[1] = top; q[2] = 0xF5; q[3] = top; q[4] = 0xF5; q[5] = bot; q[6] = 0xA9; q[7] = bot;
        poly_fill_far(DSP(DSW(0x0D1C)), 4, q, 0x80 + i);
        if (done) break;
    }
}

/* 0000:19EA mfd_view_ground_bands (spec 2.2): up to 16 ground quads, colour 70h+i (F0h when mode == 1), below the horizon, clamped at y = A9h */
void mfd_view_ground_bands(int horizon, int mode)
{
    s16 q[8];
    int base = mode == 1 ? 0xF0 : 0x70;
    for (int i = 0; i < 16; i++) {
        int done = 0;
        s16 bot = (s16)((s16)DSW((u16)(0x0D24 + 2 * i)) + horizon);
        if (bot >= 0xA9) { bot = 0xA9; done = 1; }
        s16 top = (s16)((s16)DSW((u16)(0x0D22 + 2 * i)) + horizon);
        q[0] = 0xA9; q[1] = bot; q[2] = 0xF5; q[3] = bot; q[4] = 0xF5; q[5] = top; q[6] = 0xA9; q[7] = top;
        poly_fill_far(DSP(DSW(0x0D1C)), 4, q, base + i);
        if (done) break;
    }
}

/* 0000:C6DE mfd_view_flir_tint (spec 2.2): the 77x61 sensor window at offset (x, y) goes through the green MFD ramp */
void mfd_view_flir_tint(int x, int y)
{
    offset_set(pixel_offset(x, y));
    tables_clear();
    DSW(0x2299) = 0; DSW(0x229D) = 0; DSW(0x229B) = 0; DSW(0x229F) = 0x3C;
    edge_to_tables();
    DSW(0x229D) = 0;
    DSW(0x2299) = 0x4C; DSW(0x229B) = 0x4C; DSW(0x229F) = 0x3C;
    edge_to_tables();
    poly_flush_far(3);
    offset_set(0);
}

/* 0000:C6B8 object model: bit 8 set -> the model word of the object map, bit 15 of the object kept */
static u16 obj_model(u16 obj)
{
    if (obj & 0x100) return (u16)(DSW((u16)(0x0510 + 2 * (obj & 0x7F))) | (obj & 0x8000));
    return obj;
}

/* 0000:BF71 mfd_view_render (spec simflow.md 2.1) */
void mfd_view_render(unsigned obj_, int x, int y, int z, int a0, int a1, int a2, int mode, int zshift)
{
    u16 obj = (u16)obj_;
    s16 rx = 0, ry = 0, rz = 0;
    int horizon = 0, g = 0x78;
    u16 ctx = DSW(0x4042);
    DSW(0x9D08) = 1;
    u16 model = obj_model(obj);
    DSW(0x4362) = obj & 0x7F;
    u8 zi = (u8)((obj & 0x100 ? DSB((u16)(0x0960 + (obj & 0x7F))) : DSB((u16)(0x09E0 + (obj & 0x7F)))) & 7);
    u16 zr = DSW((u16)(0x4340 + 2 * zi));
    DSB(0x433C) = (u8)(0x38 - zi);
    if (((y | x) & 0xFF) == 0) zr = (u16)(zr << 1);
    if (mode == 1 && zshift == 1) zr = (u16)((s16)zr >> 1);
    DSW(ctx) = DSB(0x4088) != 0;
    DSW(0xE554) = (u16)mode;

    if (mode == 4) {                                                      /* the weapon seeker: its own camera */
        u16 w = (u16)(0x3C0C + 0x1C * (u16)DSW(0xB04C));
        DSW(0xDED4) = DSW(w + 8);
        DSW(0x9510) = DSW(w + 0xA);
        SETL(0xB766, (u32)DSW(w) << 5);
        SETL(0xB76A, 0x100000 - ((u32)DSW(w + 2) << 5));
        SETL(0xB808, (s32)(s16)DSW(w + 4));
    } else {
        if (mode < 2) {                                                   /* from the player at the object */
            DSW(0x9512) = 0;
            s16 dx = (s16)(x - (s16)DSW(0xC0A4)), dy = (s16)(y - (s16)DSW(0xC0B2)), dz = (s16)((s16)(z - (s16)DSW(0x2DF4)) >> 5);
            u16 brg = fx_heading(dx, (s16)-dy);
            s16 d1 = (s16)fx_dist(dx, dy);
            u16 elev = fx_heading(dz, d1);
            s16 rng = (s16)fx_dist(dz, (s16)fx_dist(dx, dy));
            DSW(0xC576) = (u16)rng;
            DSW(0x950C) = (u16)((rng >> 4) + 0x190);
            s16 num = (s16)(DSW(0x950C) << 5);
            s16 den = (s16)(rng + 1);
            DSW(0x950E) = den != 0 ? (u16)(s16)((s32)num / den) : 0;
            s16 r = (s16)(DSW(0x950C) << 2);
            DSW(0xDED4) = brg;
            DSW(0x9510) = elev;
            s16 h2 = (s16)fx_cos_mul(elev, r);
            DSB(0x433A) = 0;
            if (zshift < 0) { DSB(0x433A) = (u8)(zshift + 2); zshift = 0; }
            rx = (s16)((s16)fx_sin_mul(brg, h2) >> zshift);
            ry = (s16)((s16)-(s16)fx_cos_mul(brg, h2) >> zshift);
            rz = (s16)((s16)fx_sin_mul(elev, r) >> zshift);
        } else {                                                          /* the own orientation of the player */
            rx = (s16)((s16)(x - (s16)DSW(0xC0A4)) << 4);
            ry = (s16)((s16)(y - (s16)DSW(0xC0B2)) << 4);
            rz = (s16)((s16)(z - (s16)DSW(0x2DF4)) >> 1);
            DSW(0xDED4) = DSW(0x2DEE);
            DSW(0x9510) = DSW(0x98CE);
            DSW(0x9512) = DSW(0x2DF2);
            DSW(0x950E) = 0x20;
            DSB(0x433A) = 2;
        }
        /* the horizon row of the window, the ground colour */
        s32 hp = (s32)((s16)DSW(0x9510) >> 2) * (s16)DSW(0x950E);
        horizon = (s16)((s16)(hp >> 5) + 0x8B);
        if (horizon < 0x6E || (s16)DSW(0x9510) < -0x1800) horizon = 0x6E;
        if (horizon > 0xA9 || (s16)DSW(0x9510) > 0x1800) horizon = 0xA9;
        int attr = (s8)DSB((u16)(0xC604 + (obj & 0x7F)));
        if (attr & 0x10) g = 8;
        int lo = attr & 0xF;
        if (lo == 0xC || lo == 0xD || lo == 9 || lo == 0xB) g = 0xFC;
        if (zr > DSW(0xC576) || mode == 3) {                              /* nearer than the zoom range: the natural camera */
            SETL(0xB766, LONG(0xB288));
            SETL(0xB76A, LONG(0xB752));
            SETL(0xB808, (s32)(s16)DSW(0xB75C));
        } else {                                                          /* magnify: the camera zr units before the object */
            u32 y32 = (u32)(u16)y << 5;
            s16 zr5 = (s16)(zr << 5);
            s16 q = (s16)fx_cos_mul(DSW(0x9510), zr5);
            SETL(0xB766, ((u32)(u16)x << 5) - (u32)(s32)(s16)fx_sin_mul(DSW(0xDED4), q));
            SETL(0xB76A, 0x100000 - (u32)(s32)(s16)fx_cos_mul(DSW(0xDED4), q) - y32);
            DSW(0xB808) = (u16)-(s16)((s16)fx_sin_mul(DSW(0x9510), zr5) - z);
            DSW(0xB80A) = 0;
        }
    }
    /* the pole test, the minimum altitude */
    {
        s16 p = (s16)DSW(0x9510);
        if ((s16)(p < 0 ? -p : p) > 0x4000 || DSW(0x9510) == 0x8000) {
            DSW(0x9510) = (u16)(0x8000 - DSW(0x9510));
            DSW(0xDED4) = (u16)(DSW(0xDED4) + 0x8000);
        }
        s16 hi = (s16)DSW(0xB80A);
        u16 lo = DSW(0xB808);
        if (!(hi > 0 || (hi == 0 && lo >= 0x10))) { DSW(0xB808) = 0x10; DSW(0xB80A) = 0; }
    }
    u8 saved294b = DSB(0x294B);
    DSB(0x294B) = 0;
    DSW(0x7D50) = DSW(0x43B2) ? 0x8160 : 0x7F60;
    if ((s16)DSW(0x98DE) < 3) {
        DSW(0x7D50) = 0x8360;
        DSW(ctx + 4) = 0x88;
        fill_rows_far(DSP(ctx), 0xA9, 0x6E, 0xF5, 0xA9);
    }
    u16 yaw_n = (u16)-DSW(0xDED4);
    if (DSW(0x034A) == 0 && mode != 4) {                                  /* the simple path: bands and one object */
        if (mode != 0) {
            DSW(ctx + 4) = 0x88;
            if (horizon != 0x6E) {
                if (DSW(0x98DE) == 3) mfd_view_sky_bands(horizon);
                else fill_rows_far(DSP(ctx), 0xA9, 0x6E, 0xF5, horizon);
            }
            DSW(ctx + 4) = (u16)g;
            if (horizon != 0xA9) {
                if (DSW(0x98DE) == 3 && g != 8 && DSW(0x3658) == 0) mfd_view_ground_bands(horizon, g);
                else fill_rows_far(DSP(ctx), 0xA9, horizon, 0xF5, 0xA9);
            }
        }
        sim_r3d_begin_scene(DSP(ctx), (s16)yaw_n, (s16)DSW(0x9510), (s16)DSW(0x9512), 0, 0, 0, 0, (u16)(0x100 << (DSB(0x433A) & 31)));
        {
            u16 site = (u16)(0xB2A4 + ((DSW(0x3D66) << 4) & 0xFFFF));
            if ((DSB((u16)(site + 6)) & 0x80) && DSW((u16)(site + 0xC)) != (u16)(s16)(s8)DSB(0xC0AC) && DSW((u16)(site + 0xC)) != (u16)(s16)(s8)DSB(0xDEA4)) model |= 0x8000;
        }
        sim_r3d_place_object(model, -a0, a1, a2, rx, -ry, rz);
    } else {                                                              /* the terrain path */
        u16 s98f8 = DSW(0x98F8), s9f12 = DSW(0x9F12), s98c8 = DSW(0x98C8), s98e4 = DSW(0x98E4);
        DSW(0x4360) = 1;
        sim_r3d_begin_scene(DSP(ctx), (s16)yaw_n, (s16)DSW(0x9510), 0, 0, 0, (s16)DSW(0xB75C), 1, (u16)(0x100 << (DSB(0x433A) & 31)));
        u16 s348 = DSW(0x0348);
        DSW(0x0348) = 1;
        sim_r3d_draw_terrain((s16)yaw_n, (s16)DSW(0x9510), LONG(0xB766), LONG(0xB76A), LONG(0xB808));
        if (mode == 1 && zshift != 0) {                                   /* an air target, close view */
            sim_r3d_end_scene();
            sim_r3d_begin_scene(DSP(ctx), (s16)yaw_n, (s16)DSW(0x9510), (s16)DSW(0x9512), 0, 0, 0, 0, DSW((u16)(0x4350 + 2 * zi)));
            sim_r3d_place_object(model, -a0, a1, a2, rx, -ry, rz);
            if (DSB((u16)(0xC156 + 0x24 * (u16)DSW(0x3D64))) & 0x20) {
                sim_r3d_draw_queued();
                DSW(0x49C4) = 0;
                sim_fx_fire_puffs(-a0, a1, a2, rx, -ry, rz);
            }
        } else {
            sim_fx_smoke_draw((s16)(LONG(0xB766) >> 5), (s16)(0x8000 - (s16)(LONG(0xB76A) >> 5)), (s16)DSW(0xB808));
        }
        DSW(0x4360) = 0;
        DSW(0x0348) = s348;
        DSW(0x98F8) = s98f8; DSW(0x9F12) = s9f12; DSW(0x98C8) = s98c8; DSW(0x98E4) = s98e4;
    }
    sim_r3d_end_scene();
    DSW(0x7D50) = 0x7D60;
    if (DSW(0x9D0C) != 0 && DSW(0x9D0E) != 0) {                           /* the overlay picture */
        s16 a = (s16)DSW(0x9D0C);
        u16 e = (u16)(0x91C8 - 4 * (u16)(a < 0 ? -a : a));
        sprite_scaled(DSP(ctx), 13, 10, 50, 40, DSW(e), DSW((u16)(e + 2)));
    }
    if (DSW(0x43B2)) {
        mfd_view_flir_tint(0xA9, 0x6E);
        if (DSW(0x480E) == 0) hud_lamp_recolor(DSP(DSW(0x3FE2)), 0x19, 1);
    } else if (DSW(0x480E)) hud_lamp_recolor(DSP(DSW(0x3FE2)), 0x19, 0);
    tables_clear();
    if (mode == 1) {
        int col = DSW(0x43B2) ? 0 : (s8)DSB(0x2CA0);
        strcpy_fn(ds_str(0x9876), ds_str(0x4364));
        ds_itoa((int)(DSW(0xDED4) / 0xB6), 0xDEAA);
        strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
        hud_text2(ds_str(0x9876), 0xB9, 0xA1, col);
        if (DSW(0x034A)) {
            strcpy_fn(ds_str(0x9876), ds_str(0x433C));
            hud_text2(ds_str(0x9876), 0xEC, 0xA3, col);
        }
    }
    DSB(0x433A) = 0;
    DSB(0x294B) = saved294b;
}
