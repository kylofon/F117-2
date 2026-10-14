/* The HUD overlay of the C part, run once per frame after the 3D view (0000:7898; spec simflow.md Part 7 section 4): the hit flash, the master-mode lamps, the stall
 * lamp, the weapon and gun line, the flight-path marker, the fuel lamp, the scope, the designator cross, the speed margin and vertical speed bars, the sink-rate
 * mark, the air-to-air cursor and seeker circle, the numbers, the waypoint caret and the message line. Written against the disassembly. */
#include <stdio.h>
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"

static u16 pilot_w(int off) { return *(u16 *)(far_mp(ds_far(0x98F4)) + off); }
static void *ctx_ptr(u16 ptr_off) { return DSP(DSW(ptr_off)); }
static char *strbuf(void) { return ds_str(0x9876); }
static void ds_itoa(int v, u16 buf) { snprintf((char *)DSP(buf), 12, "%d", (s16)v); }
#define SITE(i) ((u16)(0xB2A2 + 16 * (u16)(i)))

/* 0000:7898 hud_overlay_frame */
void hud_overlay_frame(void)
{
    DSB(0x4088) = (u8)flip_flag();
    u16 alt_disp = DSW(0x2DF6) >= 0x64 ? (u16)((u16)(DSW(0x2DF6) / 5) * 5) : DSW(0x2DF6);

    if (DSW(0x365E) != 0) {
        /* 1. the hit flash */
        if (DSW(0x98EE)) {
            DSW(0x98EE) = 0;
            if (!(DSW(0xC07A) & 0x80)) {
                gfx_set_fg(0xD);
                hud_fill_rect(0, 0, 0x13F, 0x59);
                shake_set(0x3C);
            }
        }
        /* 2. the master mode lamps 0..2 */
        if (DSW(0xE2FE) != DSW(0x409C)) {
            hud_lamp_recolor(ctx_ptr(CTX_P2_PTR), (s16)DSW(0x409C), 0);
            hud_lamp_recolor(ctx_ptr(CTX_P2_PTR), (s16)DSW(0xE2FE), 1);
            blit_rect(2, 0x77, 0x5E, 0, 0x77, 0x5E, 0x3B, 9);
            if (DSW(0x2DFC)) blit_rect(2, 0x77, 0x5E, 1, 0x77, 0x5E, 0x3B, 9);
            DSW(0x409C) = DSW(0xE2FE);
        }
        DSB(0x2956) = 1;
        if (DSW(0xC07A) == 0) {
            /* 3. the stall lamp */
            if ((s16)DSW(0xB168) < (s16)DSW(0xE448) && DSW(0x2DF4) != DSW(0xC0A2)) {
                int v = DSB(0x3D60) & 1;
                hud_lamp_recolor(ctx_ptr(CTX_P0_PTR), 4, v);
                hud_lamp_recolor(ctx_ptr(CTX_P1_PTR), 4, v);
                hud_lamp_recolor(ctx_ptr(CTX_P2_PTR), 4, v);
            } else if (DSW(0x4712) != 0) {
                hud_lamp_recolor(ctx_ptr(CTX_P0_PTR), 4, 0);
                hud_lamp_recolor(ctx_ptr(CTX_P1_PTR), 4, 0);
                hud_lamp_recolor(ctx_ptr(CTX_P2_PTR), 4, 0);
            }
            /* 4. the weapon line and the gun */
            u16 sel = DSW(0x364E);
            strcpy_fn(strbuf(), ds_str(0x40A0));
            ds_itoa((s16)DSW((u16)(0x3640 + 4 * sel)), 0xDEAA);
            strcat_fn(strbuf(), ds_str(0xDEAA));
            strcat_fn(strbuf(), ds_str(0x40A1));
            strcat_fn(strbuf(), ds_str((u16)(0x366C + 0x1A * DSW((u16)(0x363E + 4 * sel)))));
            hud_text(strbuf(), DSB(0x294B) == 0 ? 0x35 : 0x6A, 0x55, (s8)DSB(0x2CA0));
            if (pilot_w(0x42) == 0) {
                strcpy_fn(strbuf(), ds_str(0x40A3));
                ds_itoa((s16)DSW(0x3650), 0xDEAA);
                strcat_fn(strbuf(), ds_str(0xDEAA));
                hud_text(strbuf(), DSB(0x294B) == 0 ? 0xEF : 0xBC, 0x55, (s8)DSB(0x2CA0));
            }
            /* 5. the flight-path marker */
            if (DSW(0xE2FE) == 0 || DSW(0xE2FE) == 2) {
                gfx_set_fg((s8)DSB(0x2CA1));
                s16 y = (s16)((DSB(0x294B) == 0 ? 0x34 : 0x3C) + ((s16)DSW(0x98C6) >> 6));
                DSW(0xC684) = (u16)y;
                if (DSB(0x294B) == 0 && y > 10 && y < 0x5D) cockpit_keyblit(0x9A, y - 4, 0x37, 0x4D, 0xB, 7, (s8)DSB(0x2CA0));
            }
            /* 6. the fuel lamp */
            {
                int v = 3;
                if (DSB(0x3D60) & 1) {
                    u16 s = SITE(DSW(0xE2EE));
                    s16 d = (s16)fx_dist((s16)(DSW(0xC0A4) - DSW(s + 2)), (s16)(DSW(0xC0B2) - DSW(s + 4)));
                    if ((d >> 4) > (s16)DSW(0x3634)) v = 0x11;
                }
                hud_lamp_update(9, v);
            }
            /* 7. the left MFD scope */
            if (DSW(0xDEDA) == 1) mfd_scope_draw((s8)DSB(0x4088));
            if (DSB(0x294B) == 0) {
                /* 8. the designator cross */
                if (DSB(0x9B05) & 2) {
                    int show = 1;
                    if (DSW(0xB28C)) show = DSB(0x3D60) & 1;
                    if (show) {
                        gfx_set_fg((s8)DSB(0x2CA0));
                        hud_line_view(0x9C, 0x55, 0xA4, 0x55);
                        hud_line_view(0xA0, 0x52, 0xA0, 0x58);
                    }
                }
                if (DSB(0x294A) == 0) {
                    gfx_set_fg(DSW(0x3658) >= 1 ? 0x2A : 0);
                    s16 n = (s16)fx_clamp_wrap(((s16)((s16)(DSW(0xE448) - DSW(0xB168)) << 1)) / 5 + 0x1D, 0, 0x39);
                    if (n != 0) hud_line_view(0x48, 0x51 - n, 0x48, 0x51);
                    hud_line_view(0xF7, 0x34, 0xF7, fx_clamp_wrap(0x34 - ((s16)DSW(0xE55A) >> 4), 0x14, 0x51));
                    if (!(DSW(0x9B04) & 1) && (DSB(0x3D60) & 1) && pilot_w(0x40) != 0 && (s16)DSW(0xE55A) < 0) {
                        u16 lim = (DSB((u16)(SITE(DSW(0xDFE0)) + 9)) & 2) ? 0x100 : 0x80;
                        int y = (lim / pilot_w(0x40) >> 4) + 0x34;
                        gfx_set_fg((s8)DSB(0x2CA0));
                        hud_line_view(0xF2, y - 2, 0xF4, y);
                        hud_line_view(0xF2, y + 2, 0xF4, y);
                    }
                }
                if (DSB(0x9B05) & 4) hud_ils();
                if (DSW(0xE2FE) == 1) {
                    int cl = (DSB(0x294B) + 4) & 31;
                    int cy = ((s16)DSW(0xE550) >> cl) + 0x34;
                    int cx = ((s16)DSW(0xE546) >> cl) + 0x9F;
                    if (cx > 10 && cx < 0x135 && cy > 8 && cy < 0x56) cockpit_keyblit(cx - 6, cy - 5, 0x26, 0x4C, 0xD, 0xB, 0xF);
                    u16 wrec = (u16)(0x366C + 0x1A * DSW((u16)(0x363E + 4 * DSW(0x364E))));
                    u16 ttype = DSW(wrec + 12);
                    if (DSW((u16)(0x336E + 0x12 * ttype)) == 7) {
                        gfx_set_fg((s8)DSB(0x2CA1));
                        int prev_x = 0, prev_y = 0;
                        for (int a = 0; a <= 0x100; a += 0x10) {
                            u16 ang = (u16)((a & 0xFF) << 8);
                            int px = fx_sin_mul(ang, 0x28) + 0x9F;
                            int py = -(fx_cos_mul(ang, 0x23) - 0x34);
                            if (a != 0) hud_line_view(px, py, prev_x, prev_y);
                            prev_x = px; prev_y = py;
                        }
                    }
                }
                /* 9. the numbers */
                hud_num_left((s16)DSW(0xB168), DSB(0x294A) == 0 ? 0x50 : 0x3A, 0x32, (s8)DSB(0x2CA0));
                hud_num_right(alt_disp, DSB(0x294A) == 0 ? 0xF0 : 0x108, 0x32, (s8)DSB(0x2CA0));
                if (DSB(0x294A) != 0) {
                    if (DSB(0x294A) == 2) {
                        hud_num_left(DSW(0x2DEE) / 0xB6, 0x9A, 8, (s8)DSB(0x2CA0));
                        hud_box(0x97, 5, 0xA7, 0xF, (s8)DSB(0x2CA0));
                    }
                    hud_box(0x37, 0x2F, 0x4F, 0x39, (s8)DSB(0x2CA0));
                    hud_box(0xF1, 0x2F, 0x109, 0x39, (s8)DSB(0x2CA0));
                }
                if ((s16)DSW(0x3D7A) > 1) hud_text(ds_str(0x40A8), 0x6E, 0x55, (s8)DSB(0x2CA0));
                if (DSB(0x9B05) & 0x10) hud_text(ds_str(0x40AE), 0xB6, 0x55, (s8)DSB(0x2CA0));
                /* 10. the waypoint caret */
                if (!(DSW(0x3632) & 8)) {
                    u16 wp = (u16)(0x2E6C + 4 * DSW(0x2E7C));
                    s16 dx = (s16)(DSW(wp) - DSW(0xC0A4)), dy = (s16)(DSW(wp + 2) - DSW(0xC0B2));
                    if ((s16)fx_dist(dx, dy) < 0x200 && (s16)DSW(0x2E7C) < 3) {
                        DSW(0x2E7C)++;
                        strcpy_fn(strbuf(), ds_str(0x40B7));
                        ds_itoa((s16)DSW(0x2E7C), 0xDEAA);
                        strcat_fn(strbuf(), ds_str(0xDEAA));
                        strcat_fn(strbuf(), ds_str(0x40C1));
                        msg_post(strbuf());
                    }
                    DSW(0xC078) = fx_heading(dx, -dy);
                    int xc = fx_clamp_wrap(((s16)(DSW(0xC078) - DSW(0x2DEE)) >> 6) / 3 + 0x9F, 0x59, 0xE5);
                    gfx_set_fg(0xB);
                    if (DSB(0x294A) == 2) {
                        hud_line_view(xc, 0x11, xc, 0x11);
                        hud_line_view(xc - 1, 0x12, xc + 1, 0x12);
                    } else {
                        hud_line_view(xc - 2, 0xB, xc, 0xD);
                        hud_line_view(xc, 0xD, xc + 2, 0xB);
                        hud_line_view(xc - 2, 0xB, xc + 2, 0xB);
                    }
                }
            } else {
                /* the small view */
                if (!(DSW(0x3632) & 8)) {
                    u16 wp = (u16)(0x2E6C + 4 * DSW(0x2E7C));
                    s16 dx = (s16)(DSW(wp) - DSW(0xC0A4)), dy = (s16)(DSW(wp + 2) - DSW(0xC0B2));
                    DSW(0xC078) = fx_heading(dx, -dy);
                    int xc = fx_clamp_wrap(((s16)(DSW(0xC078) - DSW(0x2DEE)) >> 6) / 7 + 0x9F, 0x82, 0xBC);
                    gfx_set_fg(0xB);
                    hud_line_view(xc, 0x1B, xc, 0x1B);
                    hud_line_view(xc - 1, 0x1C, xc + 1, 0x1C);
                }
                if (DSB(0x294A) != 0) {
                    hud_num_left((s16)DSW(0xB168), 0x67, 0x3A, (s8)DSB(0x2CA0));
                    hud_num_right(alt_disp, 0xDA, 0x3A, (s8)DSB(0x2CA0));
                    if (DSB(0x294A) == 2) hud_num_left(DSW(0x2DEE) / 0xB6, 0x9A, 0x14, (s8)DSB(0x2CA0));
                }
            }
        }
    }
    /* the message line */
    if (DSW(0x409E) != 0 && ((DSW(0xC07A) == 0 && DSB(0x294B) == 0) || DSW(0x365A) != 0)) {
        int len = (int)strlen(ds_str(0xC578));
        hud_text2(ds_str(0xC578), -(((len & 0xFE) << 1) - 0xA0), 0x14, (s8)DSB(0x2CA0));
        DSW(0x409E)--;
    }
    if (DSW(0xC07A) == 0x42) {                           /* the left external view: the altitude in the status strip */
        blit_rect(2, 0xC4, 0xB9, DSB(0x4088), 0xC4, 0xB9, 0x1E, 5);
        snprintf(ds_str(0xC5D8), 12, "%ld", (long)alt_disp);
        hud_text(ds_str(0xC5D8), 0xC8, 0xB9, 0x29);
        blit_rect(DSB(0x4088), 0xC4, 0xB9, 1 - DSB(0x4088), 0xC4, 0xB9, 0x1E, 5);
    }
}
