/* The cockpit panel pieces (spec simflow.md Part 3 sections 5-8, Part 7): the colour-keyed blits from the picture page, the panel refresh after the picture was
 * reloaded, the MFD icons. */
#include "sim/sim.h"
#include "sim/sim_priv.h"

/* 0000:91A4 cockpit_keyblit: colour 0 is transparent; from the picture page (segment [E558]) to the back page; BlitCtx at DS:4062 (+ two bytes at 407A / 407B) */
void cockpit_keyblit(int dx, int dy, int sx, int sy, int w, int h, int clip)
{
    DSW(0x4062) = DSW(0xE558);
    DSW(0x4064) = (u16)sx;
    DSW(0x4066) = (u16)sy;
    DSW(0x4068) = DSB(0x4088) != 0;
    DSW(0x406A) = (u16)dx;
    DSW(0x406C) = (u16)dy;
    DSW(0x406E) = (u16)w;
    DSW(0x4070) = (u16)h;
    DSB(0x407B) = (u8)clip;
    if ((s16)clip != 0) {
        DSB(0x407A) = 1;
        blit_key_clip(DSP(0x4062));
    } else {
        DSB(0x407A) = 0x10;
        blit_key(DSP(0x4062));
    }
}

/* 0000:9154 mfd_icon: a 7x7 icon of the panel picture's icon sheet (cx * 8 + 1, cy * 8 + 29h) centred on (x, y), clipped to the left MFD box */
void mfd_icon(int cx, int cy, int x, int y)
{
    DSW(0x4044) = DSW(0xE558);
    DSW(0x4046) = (u16)(cx * 8 + 1);
    DSW(0x4048) = (u16)(cy * 8 + 0x29);
    DSW(0x404A) = DSB(0x4088) != 0;
    DSW(0x404C) = (u16)(x - 3);
    DSW(0x404E) = (u16)(y - 3);
    DSW(0x4050) = 7;
    DSW(0x4052) = 7;
    blit_key_clip(DSP(0x4044));
}

/* 0000:3968 cockpit_refresh_panels: after the picture was reloaded: the lit lamps again, both MFDs, the numbers */
void cockpit_refresh_panels(void)
{
    for (int i = 0; i < 0x25; i++) {
        if (DSW((u16)(0x46E2 + 12 * i))) {
            hud_lamp_recolor(DSP(DSW(CTX_P0_PTR)), i, 1);
            if (i < 4) hud_lamp_recolor(DSP(DSW(CTX_P2_PTR)), i, 1);
        }
    }
    mfd_left_select((s16)DSW(0xDEDA));
    mfd_right_select((s16)DSW(0xDFDC));
    hud_draw_fuel();
    hud_draw_throttle();
    hud_draw_store_counts();
}

/* 0000:17a3 cockpit_draw_adi: the attitude indicator (32x28 at (265,140)): the horizon polygon from the cached sin/cos of the camera, the bezel, then the copy to the screen page
 * (spec simflow.md section 7; the vertex arithmetic is the asm at 17a3:1817..18c8) */
void cockpit_draw_adi(void)
{
    s16 a[2], b[2], c[2], d[2], pts[8];
    offset_set(pixel_offset(0x109, 0x8C));
    DSW(0x2293) = 0x20;
    DSW(0x2295) = 0x1C;
    s16 *ctx = (s16 *)DSP(DSW(0x0D1C));
    ctx[0] = (s16)(s8)DSB(0x4088);
    ctx[2] = 9;
    fill_rows_far(ctx, 0, 0, 0x20, 0x1C);
    s16 den = (s16)DSW(0x9F12);
    if (den < 0x20) den = 0x20;
    s16 t = (s16)rtl_aFldiv((long)(s16)DSW(0x98F8) << 4, den);
    for (int k = 0; k < 2; k++) {
        s16 v = k == 0 ? t : (s16)0xA7B8;
        s16 si = (s16)fx_mul15((s16)0xA7B8, (s16)DSW(0x98E4));
        s16 di = (s16)fx_mul15(v, (s16)DSW(0x98C8));
        a[k] = (s16)(si - di + 0x10);
        b[k] = (s16)(0x10 - di - si);
        si = (s16)fx_mul15(v, (s16)DSW(0x98E4));
        di = (s16)fx_mul15((s16)0xA7B8, (s16)DSW(0x98C8));
        c[k] = (s16)(((s16)(di + si) >> 2) - di - si + 0xE);
        d[k] = (s16)(((s16)(si - di) >> 2) - si + di + 0xE);
    }
    pts[0] = a[0]; pts[1] = c[0]; pts[2] = b[0]; pts[3] = d[0];
    pts[4] = b[1]; pts[5] = d[1]; pts[6] = a[1]; pts[7] = c[1];
    poly_fill_far(ctx, 4, pts, 1);
    DSW(0x2293) = 0x13F;
    DSW(0x2295) = 0xC7;
    offset_set(0);
    cockpit_keyblit(0x109, 0x8A, 0x11C, 0x21, 0x21, 0x21, 0);
    int p = (s8)DSB(0x4088);
    blit_rect(p, 0x109, 0x8C, 1 - p, 0x109, 0x8C, 0x20, 0x1C);
}
