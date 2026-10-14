/* The 3D scene's overlay pass (spec simflow.md Part 9 "a17e" and its helpers, section 2): the projectile markers, the cannon tracers with their hit tests, the explosion
 * sprite, the target symbology of the HUD (boxes, the lock lamp, the name pings, the photo shutter, the bomb cues, the radio beacon). Everything is drawn through the
 * HUD primitives; the hit consequences (units killed, objects destroyed, the player hit) are the damage module's (stubs, TODO(S5.7)). */
#include <stdio.h>
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"
#include "sim/sim_stubs.h"

#define LONG(o) ((s32)DSL(o))

static void ds_itoa(int v, u16 buf) { snprintf((char *)DSP(buf), 12, "%d", (s16)v); }
static int abs16(int v) { s16 s = (s16)v; return (s16)(s < 0 ? -s : s); }

#define SITE(i) ((u16)(0xB2A2 + ((u16)(i) << 4)))
#define UNIT(i) ((u16)(0xC13E + 0x24 * (u16)(i)))
#define BULLET(i) ((u16)(0xE44E + 12 * (u16)(i)))
#define PROJ(i) ((u16)(0x3C0C + 0x1C * (u16)(i)))
#define MT(k) ((u16)(0xE2D8 + 0x12 * (k)))
#define NAMEP(i) DSW((u16)(0xDEDC + 2 * (u16)(i)))
#define HUDCOL ((int)(s8)DSB(0x2CA0))
#define MONOCOL (DSW(0x43B2) ? 0 : HUDCOL)

/* ------------------------------------------------------------------------------------------------------------------------- the point projector */

/* 0000:BA13 r3d_matrix_row_dot (render3d.md Part 2): row `row` of the view matrix B156 times the vector, 32 bits */
static s32 row_dot(int row, int dx, int dy, int dz)
{
    s16 m0 = (s16)DSW((u16)(0xB156 + 2 * row)), m1 = (s16)DSW((u16)(0xB15C + 2 * row)), m2 = (s16)DSW((u16)(0xB162 + 2 * row));
    return (s32)(s16)fx_mul15(m0, dx) + (s32)(s16)fx_mul15(m1, dz) + (s32)(s16)fx_mul15(m2, dy);
}

/* 0000:B85D r3d_project_point (render3d.md Part 2; owner render3d, S5.6 takes it over): a world point (site units) to the screen: [49E0] sx (FFFFh = not visible), [49E8] sy,
 * [DE92] the depth */
void r3d_project_point(int x_u, int y_u, int z)
{
    s16 dx = (s16)((s16)DSW(0xC0A4) - (s16)x_u);
    s16 dy = (s16)((s16)y_u - (s16)DSW(0xC0B2));
    s16 dz = (s16)((s16)((s16)z - (s16)DSW(0x2DF4)) >> 5);
    if (DSB(0xC07A) & 0x80) {                                             /* an external camera */
        dx = (s16)(dx - (s16)((LONG(0xBA10) - LONG(0xB288)) >> 5));
        dy = (s16)(dy - (s16)((LONG(0xC02A) - LONG(0xB752)) >> 5));
        dz = (s16)(dz - (s16)(((s32)(s16)DSW(0xB75C) - (s32)(u32)DSW(0x2DF4)) >> 5));
    }
    s32 X = row_dot(0, dx, dy, dz), Y = row_dot(1, dx, dy, dz), Z = row_dot(2, dx, dy, dz);
    if (Z >= 0) { DSW(0x49E0) = 0xFFFF; return; }
    if (DSB(0x294B) != 0) { X >>= 1; Y >>= 1; }
    s32 nz = (s32)(0u - (u32)Z);
    if (!(X <= nz) || !(Z <= X)) { DSW(0x49E0) = 0xFFFF; return; }
    s16 sx = (s16)((s16)(s32)(((s64)(s32)((u32)X << 8)) / Z) + 0xA0);
    s16 sy = (s16)(s32)(((s64)(s32)((u32)Y << 8)) / Z);
    sy = (s16)(sy - (sy >> 2));
    if (DSW(0x365E) != 0) sy = (s16)(sy + (DSB(0x294B) == 0 ? 0x34 : 0x3C));
    else sy = (s16)(sy + 0x64);
    DSW(0x49E8) = (u16)sy;
    DSW(0xDE92) = (u16)(s16)(Z >> 3);
    if (sx < 0 || sx > 0x13F) { DSW(0x9500) = (u16)sx; sx = (s16)0xFFFF; }
    int lim = DSW(0x365E) == 0 ? 0xC7 : 0x60;
    if (sy < 0 || sy > lim) { DSW(0x9500) = (u16)sx; sx = (s16)0xFFFF; }
    DSW(0x49E0) = (u16)sx;
}

/* ------------------------------------------------------------------------------------------------------------------------- HUD symbology helpers */

/* 0000:B3E6 hud_target_box (spec 2): a rectangle (hex == 0) or a hexagon of half size `size` around (x, y) */
void hud_target_box(int x, int y, int size, int hex, int colour)
{
    if (DSW(0x365E) == 0) return;
    s16 sz = (s16)size;
    if (DSB(0x294B) != 0) sz = (s16)(sz >> 1);
    s16 R = (s16)(sz + (s16)x), L = (s16)((s16)x - sz);
    s16 t = (s16)(sz - (s16)(sz >> 2));
    s16 B = (s16)(t + (s16)y), T = (s16)((s16)y - t);
    gfx_set_fg(colour);
    if (hex == 0) {
        hud_line_win(L, T, L, B);
        hud_line_win(L, B, R, B);
        hud_line_win(R, B, R, T);
        hud_line_win(R, T, L, T);
    } else {
        s16 q = (s16)(t >> 1);
        s16 si = (s16)((s16)y - q), di = (s16)(q + (s16)y);
        hud_line_win(x, T, R, si);
        hud_line_win(R, si, R, di);
        hud_line_win(R, di, x, B);
        hud_line_win(x, B, L, di);
        hud_line_win(L, di, L, si);
        hud_line_win(L, si, x, T);
    }
}

/* 0000:B339 hud_draw_diamond (spec 2): a small diamond at the projected point */
void hud_draw_diamond(void)
{
    s16 sx = (s16)DSW(0x49E0), sy = (s16)DSW(0x49E8);
    gfx_set_fg(HUDCOL);
    hud_line_win(sx - 4, sy, sx, sy - 4);
    hud_line_win(sx, sy - 4, sx + 4, sy);
    hud_line_win(sx + 4, sy, sx, sy + 4);
    hud_line_win(sx, sy + 4, sx - 4, sy);
}

/* 0000:B3B3 hud_update_aspect_label (spec 2): the looking direction label follows the heading of the previewed target */
void hud_update_aspect_label(void)
{
    u16 q = (u16)(((u16)(DSW(0x2DEE) - DSW(0xDED4)) + 0x2000) & 0xC000);
    if (q == 0x4000 || q == 0xC000) q ^= 0x8000;
    if (DSW(0xE556) != q) { DSW(0xE556) = q; mfd_tv_label(); }
}

/* 0000:B4F3 hud_seeker_cross (spec 2): the AGM-65 seeker reticle around (207, 140) */
void hud_seeker_cross(int animate)
{
    if (DSW(0x365E) == 0) return;
    gfx_set_fg(MONOCOL);
    int cx = 0xCF, cy = 0x8C, g = 0;
    if (animate) {
        g = (s16)DSW((u16)(0x42E2 + 2 * DSW(0x42E0)));
        DSW(0x42E0) = (u16)(DSW(0x42E0) + 1);
        if ((s16)DSW(0x42E0) > 5) DSW(0x42E0) = 0;
    }
    hud_line_full(cx - g - 7, cy, cx - g - 1, cy);
    hud_line_full(cx + g + 1, cy, cx + g + 7, cy);
    hud_line_full(cx, cy - g - 7, cx, cy - g - 1);
    hud_line_full(cx, cy + g + 1, cx, cy + g + 7);
}

/* 0000:B5AF hud_lock_bracket (spec 2): the converging square of a locked target in the right panel */
void hud_lock_bracket(int animate)
{
    if (DSW(0x365E) == 0) return;
    gfx_set_fg(MONOCOL);
    s16 s = (s16)DSW(0x42F2);
    s16 ty = (s16)(s - (s16)(s >> 2));
    if (animate) {
        s = (s16)DSW((u16)(0x42F2 + 2 * DSW(0x42E0)));
        DSW(0x42E0) = (u16)(DSW(0x42E0) + 1);
        if ((s16)DSW(0x42E0) > 6) DSW(0x42E0) = 0;
        ty = (s16)(s - (s16)(s >> 2));
    }
    s16 xr = (s16)fx_clamp_wrap((s16)((s16)DSW(0xB04E) + s), 0xA9, 0xF5);
    s16 xl = (s16)fx_clamp_wrap((s16)((s16)DSW(0xB04E) - s), 0xA9, 0xF5);
    s16 yb = (s16)fx_clamp_wrap((s16)((s16)DSW(0xB050) + ty), 0x6E, 0xA9);
    s16 yt = (s16)fx_clamp_wrap((s16)((s16)DSW(0xB050) - ty), 0x6E, 0xA9);
    hud_line_full(xl, yt, xl, yb);
    hud_line_full(xl, yb, xr, yb);
    hud_line_full(xr, yb, xr, yt);
    hud_line_full(xr, yt, xl, yt);
}

/* 0000:B6AE hud_lock_indicator (spec 2): the right panel's seeker reticle / lock bracket and the lock lamp */
void hud_lock_indicator(int mode, int lock)
{
    if (DSW(0x365E) == 0) return;
    DSW(0xB04E) = 0xCF;
    DSW(0xB050) = 0x8C;
    if (DSW(0x43B4) == 0 && (s16)DSW(0xB04C) > 8 && DSW((u16)(PROJ(DSW(0xB04C)) + 0xE)) != 0 && DSW((u16)(PROJ(DSW(0xB04C)) + 0x10)) == 0x1B && DSW(0xE554) == 4) hud_seeker_cross(1);
    if (DSW(0xE55C) != 0) {
        int on;
        if (mode != 0) hud_lock_bracket(lock != 0 ? 1 : 0);
        if (lock == 0) on = DSW(0x3D60) & 1;
        else { if (DSW(0x4706) != 0) return; on = 1; }
        hud_lamp_recolor(DSP(DSW(0x3FE2)), 3, on);
        hud_lamp_recolor(DSP(DSW(0x4012)), 3, on);
    } else if (DSW(0x4706) != 0) {
        hud_lamp_recolor(DSP(DSW(0x3FE2)), 3, 0);
        hud_lamp_recolor(DSP(DSW(0x4012)), 3, 0);
    }
}

/* 0000:B77A hud_ping_label (spec 2): the expanding hexagon and the name at the projected point */
void hud_ping_label(char *s, int colour, int dy)
{
    s16 sx = (s16)DSW(0x49E0), sy = (s16)DSW(0x49E8);
    if (DSW(0x49E0) == 0xFFFF) return;
    if ((s16)dy < sx && 0x13F - (s16)dy > sx && (s16)dy < sy && 0x5C - (s16)dy > sy) hud_target_box(sx, sy, dy, 1, colour);
    if (sx > 0x14 && sx < 0x118 && sy > 0 && sy < 0x4C) hud_text2(s, sx - 2 * (int)strlen(s), sy + 5, (s16)DSW(0x9872));
}

/* 0000:B7F8 hud_format_range (spec 2): "Range n.n km" into the text scratch */
void hud_format_range(int dist)
{
    strcpy_fn(ds_str(0x9876), ds_str(0x430A));
    ds_itoa((s16)dist >> 6, 0xDEAA);
    strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
    strcat_fn(ds_str(0x9876), ds_str(0x4311));
    ds_itoa((((u8)dist & 0x3F) * 2) / 13, 0xDEAA);
    strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
    strcat_fn(ds_str(0x9876), ds_str(0x4313));
}

/* ------------------------------------------------------------------------------------------------------------------------- the pass itself */

/* 0000:A17E obj_overlay_frame (spec simflow.md Part 9 section 1) */
void obj_overlay_frame(void)
{
    s16 v2e = 0, v30 = 0;                       /* the original's [bp-2Eh] / [bp-30h] (stale between the blocks that use them) */
    DSW(0x9504) = DSW(0x9D08);
    DSW(0x9D08) = 0;

    /* ---- S1: one box per live projectile */
    for (int i = 0; i < 12; i++) {
        u16 p = PROJ(i);
        if (DSW((u16)(p + 0xE)) == 0) continue;
        r3d_project_point((s16)DSW((u16)(p)), (s16)DSW((u16)(p + 2)), (s16)DSW((u16)(p + 4)));
        if (DSW(0x49E0) == 0xFFFF) continue;
        hud_target_box((s16)DSW(0x49E0), (s16)DSW(0x49E8), 6, 0, i < 8 ? 0x11 : 0x0A);
    }

    /* ---- S2: the cannon tracers */
    int sq = (s16)fx_isqrt((s16)((DSW(0x3660) + 2) << 2));
    s16 hit_scale = (s16)(sq != 0 ? 0x17A / sq : 0);
    for (int i = 0; i < (s16)(DSW(0xB16C) + 4); i++) {
        u16 b = BULLET(i);
        if (DSW(b) == 0) continue;
        r3d_project_point((s16)DSW(b), (s16)DSW((u16)(b + 2)), (s16)DSW((u16)(b + 4)));
        s16 p0x = (s16)DSW(0x49E0), p0y = (s16)DSW(0x49E8);
        r3d_project_point((s16)(DSW(b) + ((s16)DSW((u16)(b + 6)) >> 1)), (s16)(DSW((u16)(b + 2)) + ((s16)DSW((u16)(b + 8)) >> 1)), (s16)(DSW((u16)(b + 4)) + ((s16)DSW((u16)(b + 0xA)) >> 1)));
        if (DSW(0x49E0) == 0xFFFF) continue;
        if (p0x == -1) continue;
        gfx_set_fg(i < (s16)DSW(0xB16C) ? 0x0D : 0x29);
        hud_line_view((s16)DSW(0x49E0), (s16)DSW(0x49E8), p0x, p0y);
        int hit = 0;
        if (i < (s16)DSW(0xB16C)) {                                       /* a round of the player against the units */
            for (int j = 0; j < (s16)DSW(0xDED0); j++) {
                u16 u = UNIT(j);
                if (!(DSB((u16)(u + 0x18)) & 2)) continue;
                int dxa = abs16((s16)(DSW(b) - DSW((u16)(u + 2))));
                int dya = abs16((s16)(DSW((u16)(b + 2)) - DSW((u16)(u + 4))));
                int dza = abs16((s16)(DSW((u16)(b + 4)) - DSW((u16)(u + 6)))) >> 5;
                s16 d = (s16)abs16((s16)(dza + dya + dxa));
                s16 den = (s16)(DSW(0x3654) + 1);
                s16 lim = (s16)(den != 0 ? hit_scale / den : 0);
                if (lim <= d) continue;
                DSB((u16)(u + 0x18)) |= 0x10;
                sim_dmg_unit_kill(j);
                strcat_fn(ds_str(0x9876), ds_str(0x41F6));
                msg_post(ds_str(0x9876));
                DSW(b) = 0;
                DSW(0xC0A0) = DSW((u16)(u + 2));
                DSW(0xC0AA) = DSW((u16)(u + 4));
                DSW(0xC0B0) = DSW((u16)(u + 6));
                DSW(0x9D0C) = 0x10;
            }
        } else {                                                          /* an enemy round against the player */
            int dxa = abs16((s16)(DSW(b) - DSW(0xC0A4)));
            int dya = abs16((s16)(DSW((u16)(b + 2)) - DSW(0xC0B2)));
            int dza = abs16((s16)(DSW((u16)(b + 4)) - DSW(0x2DF4))) >> 5;
            s16 d = (s16)abs16((s16)(dza + dya + dxa));
            if (d < 0x20) {
                hit = 1;
                msg_post(ds_str(0x420C));
                s16 den = (s16)(4 - (s16)DSW(0x3654));
                if (d < (s16)(den != 0 ? 0x20 / den : 0)) sim_dmg_player_blast();
            }
        }
        if (hit) {
            DSW(0xC0A0) = DSW(b); DSW(0xC0AA) = DSW((u16)(b + 2)); DSW(0xC0B0) = DSW((u16)(b + 4));
            DSW(0x9D0C) = 0xFFFF;
            DSW(b) = 0;
        }
        if ((s16)DSW((u16)(b + 4)) < 0) {                                 /* below the ground: an impact */
            if ((s16)DSW(0x9D0C) <= 0) {
                DSW(0xC0A0) = DSW(b); DSW(0xC0AA) = DSW((u16)(b + 2)); DSW(0xC0B0) = DSW((u16)(b + 4));
                DSW(0x9D0C) = 0xFFFF;
            }
            DSW(b) = 0;
            int o = (s16)sim_obj_find_at((s16)DSW(0xC0A0), (s16)DSW(0xC0AA));
            if (o != -1 && !(DSB((u16)(SITE(o) + 8)) & 0x80)) {
                if (sim_dmg_wobj_destroyable(0)) {
                    u16 w = DSW(0x9F10);
                    s16 wx = (s16)(((s32)(DSW((u16)(w + 4)) | (u32)DSW((u16)(w + 6)) << 16)) >> 5);
                    s16 wy = (s16)(0x8000 - (s16)(((s32)(DSW((u16)(w + 8)) | (u32)DSW((u16)(w + 0xA)) << 16)) >> 5));
                    v2e = wx; v30 = wy;
                    s16 dist = (s16)fx_dist((s16)(DSW(0xC0A0) - wx), (s16)(DSW(0xC0AA) - wy));
                    s16 den = (s16)(DSW(0x3654) + 2);
                    s16 pw = (s16)((s16)sim_wpn_suit(0x12, o) * 6);
                    if ((s16)(den != 0 ? pw / den : 0) > dist) {
                        sim_dmg_object_destroy(o);
                        strcat_fn(ds_str(0x9876), ds_str(0x421B));
                        msg_post(ds_str(0x9876));
                        DSW(0x9D0C) = 0x10;
                        DSW(0xC0B0) = 0;
                    }
                }
            }
        }
    }

    /* ---- S3: the explosion */
    if (DSW(0x9D0C) != 0) {
        if ((s16)DSW(0xC0B0) > 0 && (s16)DSW(0x98FE) >= 0) {
            u16 u = UNIT(DSW(0x98FE));
            DSW(0xC0A0) = DSW((u16)(u + 2)); DSW(0xC0AA) = DSW((u16)(u + 4)); DSW(0xC0B0) = DSW((u16)(u + 6));
        }
        r3d_project_point((s16)DSW(0xC0A0), (s16)DSW(0xC0AA), (s16)DSW(0xC0B0));
        if (DSW(0x49E0) != 0xFFFF) {
            s16 dep = (s16)DSW(0xDE92);
            s16 size = (s16)abs16(dep != 0 ? 0x100 / dep : 0);
            if ((s16)DSW(0xC0B0) > 0) sim_fx_explosion_sprite((s16)DSW(0x49E0), (s16)DSW(0x49E8), size, 0x10 - abs16((s16)DSW(0x9D0C)));
        }
        DSW(0x9D0C) = (u16)(DSW(0x9D0C) - (u16)lib_sign((s16)DSW(0x9D0C)));
        if (DSW(0x9D0C) == 0) DSW(0x98FE) = 0xFFFF;
    } else DSW(0x9D0E) = 0;

    if (DSW(0x365E) == 0) return;

    /* ---- S4: the target symbology */
    int lock = 0;
    DSW(0xE55C) = 0;
    int bay = (u16)(DSW(0x364E) << 2);
    u16 wid = DSW((u16)(0x363E + bay));
    u16 qty = DSW((u16)(0x3640 + bay));

    /* S4a: the ground-target box */
    if (DSW(0xE2FE) == 2 && DSW(0xC07A) == 0 && (s16)DSW(0x3D66) >= 0) {
        u16 st = SITE(DSW(0x3D66));
        r3d_project_point((s16)DSW((u16)(st + 2)), (s16)DSW((u16)(st + 4)), 0);
        s16 kind = (s16)DSW((u16)(0x3678 + (u16)(wid * 0x1A)));
        u16 pr = (u16)(0x336A + (u16)(kind * 0x12));
        s16 cls = (s16)DSW((u16)(pr + 4));
        int colour = HUDCOL, box = 0;
        s16 cap = (s16)sim_wpn_suit(wid, DSW(0x3D66));
        int ok = cap != 0 && qty != 0;
        if (ok && cls == 4 && DSW((u16)(st + 6)) == 0) ok = 0;
        if (ok && cls == 5 && !sim_obj_cls_ship(DSW(0x3D66))) ok = 0;
        if (!ok) { if (kind != -1) colour = 6; }
        else {
            s16 dep = (s16)DSW(0xDE92);
            s16 q7 = (s16)(dep / -7);
            if ((s16)DSW(pr) > q7) {
                if (kind == 0x1C) {
                    s16 rng = (s16)sim_wpn_site_dist(DSW(0x3D66));
                    s16 lim = (s16)((DSW(0x2DF4) >> 5) * 5);
                    if (lim > rng && dep < 0) {
                        DSW(0xE55C) = 1; box = 1;
                        if ((lim >> 1) > rng) { lock = 1; colour = 0x29; }
                    }
                } else if (cls != 7) {
                    if ((s16)fx_dist((s16)(DSW(0x49E0) - 0xA0), (s16)(DSW(0x49E8) - 0x34)) < 0x30) {
                        DSW(0xE55C) = 1; box = 1;
                        if ((s16)DSW(pr) > (s16)(-dep >> 2)) { lock = 1; colour = 0x29; }
                    }
                }
            }
        }
        if (DSW(0x49E0) != 0xFFFF) hud_target_box((s16)DSW(0x49E0), (s16)DSW(0x49E8), cap ? cap + 5 : 9, box, colour);
        if (kind == 0x1C) {
            r3d_project_point((s16)(fx_sin_mul(DSW(0x2DEE), 0x80) + (s16)DSW((u16)(st + 2))), (s16)((s16)DSW((u16)(st + 4)) - (s16)fx_cos_mul(DSW(0x2DEE), 0x80)), (s16)DSW(0x2DF4));
            if (DSW(0x49E0) != 0xFFFF) hud_draw_diamond();
        }
    }

    /* S4b: the floating name of a site */
    if ((s16)DSW(0x3D6A) > 0 && (s16)DSW(0xC07E) >= 0) {
        u16 st = SITE(DSW(0xC07E));
        r3d_project_point((s16)DSW((u16)(st + 2)), (s16)DSW((u16)(st + 4)), 0);
        hud_ping_label(ds_str(NAMEP(DSW(st))), (s16)DSW(0x9872), (s16)(DSW(0x3660) - DSW(0x3D6A)));
    }
    DSB(0x9B05) &= 0xFD;
    DSW((u16)(DSW(0x3FE2) + 2)) = 4;
    DSW((u16)(DSW(0x3FFA) + 2)) = 4;

    int sel = 0;
    if (DSW(0xDFDC) == 0x13 && (DSW(0xE2FE) == 2 || DSW(0xE2FE) == 0)) {
        /* S4c: the designation request */
        if (DSB(0x9B05) & 1) {
            int s = -1;
            s16 p = (s16)DSW(0x2DF0);
            s16 dist;
            if (p < 0) {
                s16 cx = (s16)(fx_sin_mul((u16)-p, 0x20) + 1);
                dist = (s16)(cx != 0 ? (s32)(s16)fx_cos_mul((u16)p, DSW(0x2DF4)) / cx : 0);
            } else dist = 0x280;
            v30 = (s16)((s16)DSW(0xC0B2) - (s16)fx_cos_mul(DSW(0x2DEE), dist));
            v2e = (s16)(fx_sin_mul(DSW(0x2DEE), dist) + (s16)DSW(0xC0A4));
            s = (s16)sim_obj_find_at(v2e, v30);
            if (s != -1 && (DSW(0xE2FE) != 0 || (s16)(DSW((u16)(SITE(s) + 8)) & 0x501) > 1)) {
                DSW(0x3D66) = (u16)s;
                if (DSW(0x3D68) == 0) DSW(0x3D68) = 0xFFFF;
                DSB(0x9B05) &= 0xFE;
            }
        }
        if (DSW(0x41F4) != 0) DSW(0x41F4)--;
        if (DSW(0x3D66) == 0xFFFF) {
            hud_lock_indicator(1, lock);
        } else {
            sel = DSB(0x3D66) & 0x7F;
            u16 st = SITE(sel);
            s16 kind = (s16)DSW((u16)(0x3678 + (u16)(wid * 0x1A)));
            if (kind == -1 && (DSB(0x9B04) & 4)) {                        /* the camera: the photo mode */
                DSB(0x9B05) |= 2;
                DSW(0x9D08) = 1;
                DSW(0xB28C) = 0;
                for (int k = 0; k < 2; k++) {
                    u16 mt = MT(k);
                    if (DSW(mt) == 1 && DSW((u16)(mt + 2)) == (u16)sel && !(DSB((u16)(st + 8)) & 0x80)) {
                        s16 d = (s16)sim_wpn_site_dist(sel);
                        s16 h = (s16)(abs16((s16)(DSW(0x9502) - DSW(0x2DEE))) >> 4);
                        s16 e = (s16)fx_heading((s16)-(s16)(DSW(0x2DF4) >> 5), d);
                        s16 pe = (s16)(abs16((s16)(e - (s16)DSW(0x2DF0) + 0x6EF)) >> 4);
                        if ((s16)(h + pe + d) < (s16)((4 - (s16)DSW(0x3654)) << 7)) DSW(0xB28C) |= (u16)(k + 1);
                    }
                }
                if (DSW(0x41F4) == 0 && DSB(0x4088) != 0 && (input_button(1) != 0 || DSW(0x9508) != 0)) {
                    DSW(0x41F4) = 4;
                    gfx_set_fg(0);
                    fill_rows_far(DSP(DSW(0x3FFA)), 0xA9, 0x6E, 0xF5, 0xA9);
                    strcpy_fn(ds_str(0x9876), ds_str(0x4231));
                    DSW(0x41F2)++;
                    ds_itoa((s16)DSW(0x41F2), 0xDEAA);
                    strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
                    hud_text2(ds_str(0x9876), 0xD1, 0x8E, 0xF);
                    snd_cue(0x12, 2);
                }
                if (DSW(0x41F4) == 0) {
                    DSW(0x98CE) = (u16)(DSW(0x2DF0) - 0x6EF);
                    mfd_view_render(sim_obj_wreck_shape(sel), (s16)DSW((u16)(st + 2)), (s16)DSW((u16)(st + 4)), 0, 0, 0, 0, 3, 0);
                    DSW((u16)(DSW(0x3FE2) + 6)) = 0x2E;
                    DSW((u16)(DSW(0x3FE2) + 2)) = 1;
                    txt_draw(DSP(DSW(0x3FE2)), ds_str((DSW(0x3D60) & 1) ? 0x4238 : 0x423F), 0xC5, 0xC1, 0x2F);
                }
                if (DSW(0x41F4) == 3 && DSW(0xB28C) != 0) {
                    msg_post(ds_str(0x4246));
                    if ((DSW(0xB28C) & 1) && !(DSB(0x9B05) & 0x40)) { mission_target_done(0); mission_log_event(0x8A, sel); }
                    if ((DSW(0xB28C) & 2) && !(DSB(0x9B05) & 0x20)) { mission_target_done(1); mission_log_event(0x4A, sel); }
                }
            } else {
                int tv = DSW(0x43B4) == 0 && (s16)DSW(0xB04C) > 8 && DSW((u16)(PROJ(DSW(0xB04C)) + 0xE)) != 0 && DSW((u16)(PROJ(DSW(0xB04C)) + 0x10)) == 0x1B;
                if (tv) {                                                 /* the AGM-65 TV view */
                    mfd_view_render(sim_obj_wreck_shape(sel), (s16)DSW((u16)(st + 2)), (s16)DSW((u16)(st + 4)), 0, 0, 0, 0, 4, -1);
                    hud_lock_indicator(0, lock);
                    DSW((u16)(DSW(0x3FE2) + 6)) = 0x2E;
                    DSW((u16)(DSW(0x3FE2) + 2)) = 1;
                    txt_draw(DSP(DSW(0x3FE2)), ds_str((DSW(0x3D60) & 1) ? 0x425B : 0x4263), 0xC5, 0xC1, 0x2F);
                } else {                                                  /* the target panel */
                    mfd_view_render(sim_obj_wreck_shape(sel), (s16)DSW((u16)(st + 2)), (s16)DSW((u16)(st + 4)), 0, 0, 0, 0, 1, -1);
                    hud_lock_indicator(1, lock);
                    hud_update_aspect_label();
                    hud_format_range(sim_wpn_site_dist(sel));
                    hud_text2(ds_str(0x9876), 0xB5, 0x9B, MONOCOL);
                    strcpy_fn(ds_str(0x9876), ds_str(NAMEP(DSB((u16)(st + 0xE)) & 0x7F)));
                    int col = MONOCOL;
                    hud_text2(ds_str(0x9876), 0xCD - 2 * (int)strlen(ds_str(0x9876)), 0x70, col);
                    if (strlen(ds_str(NAMEP(DSW(st)))) != 0) {
                        strcpy_fn(ds_str(0x9876), strlen(ds_str(NAMEP(DSB((u16)(st + 0xE)) & 0x7F))) != 0 ? ds_str(0x426B) : ds_str(0x4270));
                        strcat_fn(ds_str(0x9876), ds_str(NAMEP(DSW(st))));
                        hud_text2(ds_str(0x9876), 0xCD - 2 * (int)strlen(ds_str(0x9876)), 0x76, MONOCOL);
                    }
                    if (DSW(0xE2FE) == 0) {
                        u16 s3 = SITE(DSW(0x3D66));
                        r3d_project_point((s16)DSW((u16)(s3 + 2)), (s16)DSW((u16)(s3 + 4)), 0);
                        hud_target_box((s16)DSW(0x49E0), (s16)DSW(0x49E8), 8, 0, HUDCOL);
                    } else if (DSW(0x3D66) == DSW(0xE2DA) && (s16)DSW(0xE2D8) < 5) {
                        hud_text2(ds_str(0x4271), 0xB3, 0x7C, MONOCOL);
                    } else if (DSW(0x3D66) == DSW(0xE2EC)) {
                        hud_text2(ds_str(0x4280), 0xAD, 0x7C, MONOCOL);
                    } else if (!(DSW(0x3D60) & 1)) {
                        int forbidden = 0;
                        u16 ty = DSB((u16)(st + 0xE)) & 0x7F;
                        if ((s16)DSW(0x3656) < 2 && (DSB((u16)(0xC604 + ty)) & 0xC0) != 0) forbidden = 1;
                        else if (DSB((u16)(st + 9)) & 5) forbidden = 1;
                        else {
                            u16 cx = (u16)(DSW((u16)(st + 2)) >> 11), cy = (u16)(DSW((u16)(st + 4)) >> 11);
                            if (DSB((u16)(0xB174 + cx + (u16)(cy << 4))) & 1) forbidden = 1;
                        }
                        if (forbidden) hud_text2(ds_str(0x4291), 0xBD, 0x7C, MONOCOL);
                    }
                }
            }
        }
    }

    /* S4d */
    DSW(0x9508) = (u16)input_button(1);

    /* S4e: the air target */
    if (DSW(0xE2FE) == 1) {
        if (DSW(0xC07A) == 0 && !(DSB(0x3D64) & 0x80)) {
            u16 u = UNIT(DSW(0x3D64));
            r3d_project_point((s16)DSW((u16)(u + 2)), (s16)DSW((u16)(u + 4)), (s16)DSW((u16)(u + 6)));
            if (DSW(0x49E0) != 0xFFFF) {
                int colour = 6, box = 0;
                s16 kind = (s16)DSW((u16)(0x3678 + (u16)(wid * 0x1A)));
                u16 pr = (u16)(0x336A + (u16)(kind * 0x12));
                if (qty != 0 && DSW((u16)(pr + 4)) == 7) {
                    colour = HUDCOL;
                    if ((s16)fx_dist((s16)(DSW(0x49E0) - 0xA0), (s16)(DSW(0x49E8) - 0x38)) < 0x30) {
                        s16 cx = (s16)-(s16)DSW(0xDE92);
                        if ((s16)DSW(pr) > (s16)(cx >> 3)) {
                            DSW(0xE55C) = 1; box = 1;
                            if ((s16)DSW(pr) > (s16)(cx >> 2)) { lock = 1; colour = 0x29; }
                        }
                    }
                }
                hud_target_box((s16)DSW(0x49E0), (s16)DSW(0x49E8), 9, box, colour);
            }
        }
        if (DSW(0xDFDC) == 0x13) {
            if (DSW(0x3D64) == 0xFFFF) hud_lock_indicator(1, lock);
            else {
                int su = DSB(0x3D64) & 0x7F;
                u16 u = UNIT(su);
                sim_r3d_unit_terrain_class(su);
                u16 type = DSW((u16)(u + 0x16));
                mfd_view_render(DSW((u16)(0x2FDC + (u16)(type << 5))), (s16)DSW((u16)(u + 2)), (s16)DSW((u16)(u + 4)), (s16)DSW((u16)(u + 6)),
                                (s16)DSW((u16)(u + 0x10)), (s16)DSW((u16)(u + 0x12)), (s16)DSW((u16)(u + 0x14)), 1, 1);
                hud_lock_indicator(1, lock);
                hud_update_aspect_label();
                hud_format_range(fx_dist((s16)(DSW(0xC0A4) - DSW((u16)(u + 2))), (s16)(DSW(0xC0B2) - DSW((u16)(u + 4)))));
                hud_text2(ds_str(0x9876), 0xB5, 0x9B, MONOCOL);
                strcpy_fn(ds_str(0x9876), ds_str((u16)(0x2FC2 + (u16)(type << 5))));
                strcat_fn(ds_str(0x9876), ds_str((u16)(0x2FC9 + (u16)(type << 5))));
                hud_text2(ds_str(0x9876), 0xB9, 0x74, MONOCOL);
                if (su == 0 && (s16)DSW(0xE2D8) >= 5) hud_text2(ds_str(0x429B), 0xB3, 0x7C, MONOCOL);
                if (DSW(0x98DE) != 0 && (DSW(0x3D60) & 1)) {              /* a dead store of the original ([9506], never read) */
                    s32 prod = (s32)(s16)DSW((u16)(u + 0x1A)) * (s32)(u32)(u16)(0x8000 - DSW((u16)(u + 0x12)));
                    s16 v = (s16)(prod >> 15);
                    DSW(0x9506) = (u16)v;
                    s16 sn = (s16)fx_sin_mul(DSW((u16)(u + 0x14)), v);
                    DSW(0x9506) = (u16)(v - (s16)(abs16(sn) >> 1));
                }
            }
        }
    }
    DSW((u16)(DSW(0x3FE2) + 2)) = 2;
    DSW((u16)(DSW(0x3FFA) + 2)) = 2;

    /* S4f: the floating name of a unit */
    if ((s16)DSW(0x3D6A) > 0 && (s16)DSW(0xC07E) < 0) {
        int su = (u16)(0xFFFF - DSW(0xC07E));
        u16 u = UNIT(su);
        r3d_project_point((s16)DSW((u16)(u + 2)), (s16)DSW((u16)(u + 4)), (s16)DSW((u16)(u + 6)));
        hud_ping_label(ds_str((u16)(0x2FC2 + (u16)(DSW((u16)(u + 0x16)) << 5))), (s16)DSW(0x9872), (s16)(DSW(0x3660) - DSW(0x3D6A)));
    }

    /* S4g: the free-fall bomb cue */
    if (DSW(0xE2FE) == 2 && DSW(0xC07A) == 0) {
        s16 kind = (s16)DSW((u16)(0x3678 + (u16)(wid * 0x1A)));
        if (kind == 0x1E && abs16((s16)DSW(0x2DF2)) < 0x2000) {
            s16 a = (s16)sim_fm_bomb_dive_angle();
            s16 cx = (s16)(fx_sin_mul((u16)-a, 0x20) + 1);
            s16 r = (s16)(cx != 0 ? (s32)(s16)fx_cos_mul((u16)a, DSW(0x2DF4)) / cx : 0);
            v2e = (s16)(fx_sin_mul(DSW(0x2DEE), r) + (s16)DSW(0xC0A4));
            v30 = (s16)((s16)DSW(0xC0B2) - (s16)fx_cos_mul(DSW(0x2DEE), r));
            r3d_project_point(v2e, v30, 0);
            if (DSW(0x49E0) == 0xFFFF) {
                s16 off = (s16)((s16)(fx_sin_mul(DSW(0x2DF2), (s16)(0x5A - (s16)DSW(0xC684))) << 2) / 3);
                DSW(0x49E0) = (u16)(off + 0xA0);
                DSW(0x49E8) = 0x5A;
            } else hud_target_box((s16)DSW(0x49E0), (s16)DSW(0x49E8), 5, 1, 0x29);
            gfx_set_fg(HUDCOL);
            hud_line_win(0xA0, (s16)DSW(0xC684), (s16)DSW(0x49E0), (s16)DSW(0x49E8));
        }
        if ((kind == 0x1E || kind == 0x1D) && (s16)DSW(0x3D66) >= 0) {
            u16 st = SITE(DSW(0x3D66));
            r3d_project_point((s16)(fx_sin_mul(DSW(0x2DEE), 0x80) + (s16)DSW((u16)(st + 2))), (s16)((s16)DSW((u16)(st + 4)) - (s16)fx_cos_mul(DSW(0x2DEE), 0x80)), (s16)DSW(0x2DF4));
            if (DSW(0x49E0) != 0xFFFF) {
                int too_low;
                int hi = 0x3A + (DSB(0x294B) == 0 ? 6 : 0);
                if (kind == 0x1E) {
                    DSW(0xDE92) = (u16)fx_clamp_wrap((s16)((s16)fx_dist((s16)(v2e - (s16)DSW((u16)(st + 2))), (s16)(v30 - (s16)DSW((u16)(st + 4)))) >> 3), 0, hi);
                    too_low = (u16)((0x5DC - DSW(0xB168)) << 1) > DSW(0x2DF4);
                } else {
                    DSW(0xDE92) = (u16)fx_clamp_wrap((s16)((s16)sim_wpn_site_dist(DSW(0x3D66)) >> 3), 0, hi);
                    too_low = DSW(0xB168) > DSW(0x2DF4);
                }
                s16 bar = (s16)DSW(0xDE92);
                if (!too_low || (DSW(0x3D60) & 1)) {
                    gfx_set_fg(0x29);
                    hud_line_view(0x9F - bar, 0x21, 0x9F - bar, 0x1E);
                    hud_line_view(bar + 0xA0, 0x21, bar + 0xA0, 0x1E);
                    hud_line_view(0x9F - bar, 0x1E, bar + 0xA0, 0x1E);
                }
                hud_draw_diamond();
            }
        }
    }

    /* S4h: the radio beacon of a supply drop */
    for (int k = 0; k < 2; k++) {
        u16 mt = MT(k);
        if (!(DSB((u16)(mt + 6)) & 0x10)) continue;
        if (!((s16)DSW(0xDE88) > (s16)DSW(0x98E2))) continue;
        int s = (s16)DSW((u16)(mt + 2));
        s16 d = (s16)sim_wpn_site_dist(s);
        if (d >= 0xC80) continue;
        if (s == (s16)DSW(0x3D66)) {
            u16 st = SITE(DSW(0x3D66));
            r3d_project_point((s16)(fx_sin_mul(DSW(0x2DEE), 0x80) + (s16)DSW((u16)(st + 2))), (s16)((s16)DSW((u16)(st + 4)) - (s16)fx_cos_mul(DSW(0x2DEE), 0x80)), (s16)DSW(0x2DF4));
            if (DSW(0x49E0) != 0xFFFF) hud_draw_diamond();
        }
        u16 st = SITE(s);
        r3d_project_point((s16)DSW((u16)(st + 2)), (s16)DSW((u16)(st + 4)), 0);
        s16 fps = (s16)DSW(0x3660);
        DSW(0x9872) = 0xA;
        hud_ping_label(ds_str(0x42AA), 0xA, fps != 0 ? (s16)DSW(0x3D60) % fps : 0);
        if (fps != 0 && (s16)DSW(0x3D60) % (s16)(fps << 3) == 0) {
            strcpy_fn(ds_str(0x9876), ds_str(0x42B7));
            ds_itoa(fx_heading((s16)(DSW((u16)(st + 2)) - DSW(0xC0A4)), (s16)(DSW(0xC0B2) - DSW((u16)(st + 4)))) / 0xB6, 0xDEAA);
            strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
            msg_post(ds_str(0x9876));
            if (d < 0x100 && DSW(0x3640) != 0) msg_post(ds_str(0x42CD));
        }
    }

    /* ---- S5 */
    if (DSW(0xDFDC) == 0x13 && DSW(0x9504) != 0 && DSW(0x9D08) == 0) mfd_fill(2, 0x9F);
}
