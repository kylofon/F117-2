/* The HUD / cockpit drawing primitives of VGAME (spec simflow.md Part 2 section 4, Part 7 section 1): lamps, the little digit strips, lines and boxes through the
 * clipping line of segment 1001, text and numbers on the back page, messages, the side strips. All of it draws on page [4088] ? ctx1 : ctx0 and only while the
 * instrument panel is shown ([365E]). */
#include <stdio.h>
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"

static inline void *ctx_ptr(u16 ptr_off) { return DSP(DSW(ptr_off)); }              /* the TextCtx a pointer word at DS:ptr_off refers to */
static inline TextCtx *cur_ctx(void) { return (TextCtx *)ctx_ptr(DSB(0x4088) ? CTX_P1_PTR : CTX_P0_PTR); }

static void ds_itoa(int v, u16 buf) { snprintf((char *)DSP(buf), 12, "%d", (s16)v); }
static void ds_ltoa(long v, u16 buf) { snprintf((char *)DSP(buf), 12, "%ld", v); }

/* ------------------------------------------------------------------------------------------------------------------ lamps */

/* 0000:D83E hud_lamp_recolor: the lamp rectangle (table DS:46D8, 12 bytes per lamp: x y w h set state) of the page of `ctx` changes between its two colour sets
 * (DS:4672, 8 words per set); the state word follows */
void hud_lamp_recolor(void *ctx, int lamp, int on)
{
    u16 r = (u16)(0x46D8 + 12 * lamp);
    int x = DSS(r), y = DSS(r + 2), x1 = x + DSS(r + 4), y1 = y + DSS(r + 6);
    int set = DSS(r + 8);
    int from = on ? 0 : 1, to = on ? 1 : 0;
    u16 row = (u16)(0x4672 + 16 * set);
    rect_recolor(ctx, x, y, x1, y1, DSS(row + 2 * from), DSS(row + 2 * to));
    rect_recolor(ctx, x, y, x1, y1, DSS(row + 4 + 2 * from), DSS(row + 4 + 2 * to));
    if (set == 4) {
        rect_recolor(ctx, x, y, x1, y1, DSS(row + 8 + 2 * from), DSS(row + 8 + 2 * to));
        rect_recolor(ctx, x, y, x1, y1, DSS(row + 12 + 2 * from), DSS(row + 12 + 2 * to));
    }
    DSW(r + 10) = (u16)(on ? 1 : 0);
}

/* 0000:4F05 hud_lamp_set: lamp `lamp` on page 0 to `on` if the panel is shown and the state differs */
void hud_lamp_set(int lamp, int on)
{
    if (DSW(0x365E) == 0) return;
    if ((s16)DSW((u16)(0x46E2 + 12 * lamp)) == (s16)on) return;
    hud_lamp_recolor(ctx_ptr(CTX_P0_PTR), lamp, on);
}

/* 0000:898B hud_lamp_update: table DS:3F02 {lamp, off value, current}; "on" is value != off value; entry 2 is also set on the clean page 2 */
void hud_lamp_update(int idx, int state)
{
    if (DSW(0x365E) == 0) return;
    u16 e = (u16)(0x3F02 + 6 * idx);
    if ((s16)DSW(e + 4) == (s16)state) return;
    hud_lamp_recolor(ctx_ptr(CTX_P0_PTR), (s16)DSW(e), (s16)DSW(e + 2) != (s16)state);
    if (idx == 2) hud_lamp_recolor(ctx_ptr(CTX_P2_PTR), (s16)DSW(e), (s16)DSW(e + 2) != (s16)state);
    DSW(e + 4) = (u16)state;
}

/* 0000:4EBC hud_lamp_timers: the chaff / flare / auxiliary indicator countdowns drive lamps 0Eh, 0Fh and 18h */
void hud_lamp_timers(void)
{
    int t;
    t = 0; if (DSW(0x3D8E)) { DSW(0x3D8E)--; t = 1; } hud_lamp_set(0xE, t);
    t = 0; if (DSW(0x3D90)) { DSW(0x3D90)--; t = 1; } hud_lamp_set(0xF, t);
    t = 0; if (DSW(0x3D92)) { DSW(0x3D92)--; t = 1; } hud_lamp_set(0x18, t);
}

/* ------------------------------------------------------------------------------------------------------------------ digits */

/* 0000:D92D hud_digits_draw: the first n digits of `s` (slot table DS:4654 {x, y, n}) blitted from the 4x7 digit strip of page 2 (sx = DS:4640[digit], sy = 31) */
void hud_digits_draw(int page, int slot, char *s)
{
    u16 e = (u16)(0x4654 + 6 * slot);
    int x = DSS(e), y = DSS(e + 2), n = DSS(e + 4);
    for (;;) {
        int c = (s8)*s++;
        if (c == 0) return;
        if (n-- == 0) return;
        int digit = (c >= '0' && c <= '9') ? c - '0' : 0;
        blit_rect(2, DSS((u16)(0x4640 + 2 * digit)), 0x1F, page, x, y, 4, 7);
        x += 5;
    }
}

/* 0000:38AB hud_number_draw: ltoa into DS:B274; 0..99 padded to three characters in DS:2E4A */
void hud_number_draw(int slot, int v)
{
    ds_itoa(v, 0xB274);
    u16 p = 0x2E4A;
    if ((s16)v > 99) p = 0xB274;
    else {
        char al;
        if ((s16)v > 9) { DSB(0x2E4B) = DSB(0xB274); al = (char)DSB(0xB275); }
        else { DSB(0x2E4B) = '0'; al = (char)DSB(0xB274); }
        DSB(0x2E4C) = (u8)al;
    }
    hud_digits_draw(0, slot, ds_str(p));
}

/* 0000:38F8 hud_draw_fuel: slot 3 = min(fuel, 9999) / 10 */
void hud_draw_fuel(void)
{
    if (DSW(0x365E) == 0) return;
    int f = (s16)DSW(0x3634);
    if (f > 9999) f = 9999;
    hud_number_draw(3, f / 10);
}

/* 0000:3926 hud_draw_throttle: slot 4 = throttle */
void hud_draw_throttle(void)
{
    if (DSW(0x365E)) hud_number_draw(4, (s16)DSW(0x2DFE));
}

/* 0000:44C4 hud_draw_store_count: the 2-digit count of flares (1), chaff (2) or decoys (3) in its panel field */
void hud_draw_store_count(int type)
{
    if (DSW(0x365E) == 0) return;
    int v = (s16)DSW((u16)(0x3636 + 2 * type));
    ds_itoa(v, 0xDEB4);
    u16 p = 0x3DA4;
    if (v > 9) p = 0xDEB4;
    else DSB(0x3DA5) = DSB(0xDEB4);
    hud_digits_draw(0, (s16)DSW((u16)(0x3D9C + 2 * type)), ds_str(p));
}

/* 0000:44A9 hud_draw_store_counts */
void hud_draw_store_counts(void)
{
    for (int i = 1; i < 4; i++) hud_draw_store_count(i);
}

/* ------------------------------------------------------------------------------------------------------------------ colours, rectangles */

/* 0000:8929 gfx_set_fg: the foreground colour of ctx0 and ctx1 */
void gfx_set_fg(int c)
{
    ((TextCtx *)ctx_ptr(CTX_P0_PTR))->fg = (s16)c;
    ((TextCtx *)ctx_ptr(CTX_P1_PTR))->fg = (s16)c;
}

/* 0000:893F hud_fill_rect (the spec calls it gfx_fill_rect): a solid rectangle in the ctx colour on both display pages */
void hud_fill_rect(int x0, int y0, int x1, int y1)
{
    fill_rows_far(ctx_ptr(CTX_P0_PTR), x0, y0, x1, y1);
    fill_rows_far(ctx_ptr(CTX_P1_PTR), x0, y0, x1, y1);
}

/* 0000:8BD3 gfx_restore_rect: the clean cockpit picture (page 2) back onto pages 0 and 1 */
void gfx_restore_rect(int x, int y, int w, int h)
{
    blit_rect(2, x, y, 0, x, y, w, h);
    blit_rect(2, x, y, 1, x, y, w, h);
}

/* 0000:8A5E mfd_fill (8A4E mfd_clear forwards): the left (which == 1) or right MFD box in `colour` on the current page */
void mfd_fill(int which, int colour)
{
    gfx_set_fg(colour);
    void *c = ctx_ptr(DSB(0x4088) ? CTX_P1_PTR : CTX_P0_PTR);
    if ((s16)which == 1) fill_rows_far(c, 0x4A, 0x6E, 0x96, 0xA9);
    else                 fill_rows_far(c, 0xA9, 0x6E, 0xF5, 0xA9);
}
void mfd_clear(int which, int colour) { mfd_fill(which, colour); }

/* 0000:9770 and 973C are in sim_mfd.c */

/* ------------------------------------------------------------------------------------------------------------------ lines */

/* 0000:87A9 hud_line_clip: the line clipped to the window (xmin..xmax, ymin..ymax) in the ctx0 colour; `both` also draws it on the other page */
void hud_line_clip(int x0, int y0, int x1, int y1, int xmin, int xmax, int ymin, int ymax, int both)
{
    int h = ymax - ymin + 1;
    offset_set(pixel_offset(xmin, ymin));
    DSW(0x2293) = (u16)(xmax - xmin);
    DSW(0x2295) = (u16)(h - 1);
    color_set(((TextCtx *)ctx_ptr(CTX_P0_PTR))->fg);
    u16 ax = (u16)(x0 - xmin), cx = (u16)(y0 - ymin), dx = (u16)(x1 - xmin), bx = (u16)(y1 - ymin);
    DSW(0x2299) = ax; DSW(0x229D) = cx; DSW(0x229B) = dx; DSW(0x229F) = bx;
    line_clip_far();
    if (both) {
        DSB(0x4088) = (u8)flip_flag();
        page_select(DSB(0x4088) < 1);
        color_set(((TextCtx *)ctx_ptr(CTX_P0_PTR))->fg);
        DSW(0x2299) = ax; DSW(0x229D) = cx; DSW(0x229B) = dx; DSW(0x229F) = bx;
        line_clip_far();
        page_select(DSB(0x4088) >= 1);
    }
    DSW(0x2293) = 0x13F;
    DSW(0x2295) = 0xC7;
    offset_set(0);
}

/* 0000:871F hud_line_full / 8888 hud_line_full2: the whole screen is the clip window */
void hud_line_full(int x0, int y0, int x1, int y1) { hud_line_clip(x0, y0, x1, y1, 0, 0x13F, 0, 0xC7, 0); }
void hud_line_full2(int x0, int y0, int x1, int y1) { hud_line_clip(x0, y0, x1, y1, 0, 0x13F, 0, 0xC7, 0); }

/* 0000:873F hud_line_view: a line in the ctx0 colour clipped to the ctx0 window; leaves the offset and clip set */
void hud_line_view(int x0, int y0, int x1, int y1)
{
    TextCtx *c = (TextCtx *)ctx_ptr(CTX_P0_PTR);
    int w = c->xmax - c->xmin + 1, h = c->ymax - c->ymin + 1;
    offset_set(pixel_offset(c->xmin, c->ymin));
    DSW(0x2293) = (u16)(w - 1);
    DSW(0x2295) = (u16)(h - 1);
    color_set(c->fg);
    DSW(0x2299) = (u16)x0;
    DSW(0x229D) = (u16)y0;
    DSW(0x229B) = (u16)x1;
    DSW(0x229F) = (u16)y1;
    line_clip_far();
}

/* 0000:88F6 hud_line_win: clipped to the HUD window (48..271 x 15..90), or (99..220 x 30..90) in the small view */
void hud_line_win(int x0, int y0, int x1, int y1)
{
    if (DSB(0x294B)) hud_line_clip(x0, y0, x1, y1, 0x63, 0xDC, 0x1E, 0x5A, 0);
    else             hud_line_clip(x0, y0, x1, y1, 0x30, 0x10F, 0x0F, 0x5A, 0);
}

/* 0000:88A8 hud_box: an outline rectangle */
void hud_box(int x0, int y0, int x1, int y1, int col)
{
    gfx_set_fg(col);
    hud_line_full2(x0, y0, x1, y0);
    hud_line_full2(x0, y1, x1, y1);
    hud_line_full2(x0, y0, x0, y1);
    hud_line_full2(x1, y0, x1, y1);
}

/* 0000:8970 hud_pixel */
void hud_pixel(int x, int y, int color)
{
    gfx_set_fg(color);
    hud_line_full(x, y, x, y);
}

/* ------------------------------------------------------------------------------------------------------------------ text */

/* 0000:8B04 txt_draw: font 0 (4 px per character), upper case in place, opaque or transparent as the context says */
void txt_draw(void *c, char *s, int x, int y, int col)
{
    TextCtx *t = c;
    t->font = 0;
    t->x = (s16)x;
    t->y = (s16)y;
    t->fg = (s16)col;
    strupr(s);
    text_noclip(t, s);
}

/* 0000:8AA2 hud_text / 8AD3 hud_text2 (a second copy): text on the back page */
void hud_text(char *s, int x, int y, int col) { txt_draw(cur_ctx(), s, x, y, col); }
void hud_text2(char *s, int x, int y, int col) { txt_draw(cur_ctx(), s, x, y, col); }

/* 0000:8B3B hud_num_left: itoa into DS:C5D8, left aligned at x */
void hud_num_left(int v, int x, int y, int col)
{
    ds_itoa(v, 0xC5D8);
    hud_text(ds_str(0xC5D8), x, y, col);
}

/* 0000:8B5C hud_num_right: the unsigned value right aligned to end at xr */
void hud_num_right(unsigned v, int xr, int y, int col)
{
    ds_ltoa((long)(u16)v, 0xC5D8);
    int len = (int)strlen(ds_str(0xC5D8));
    hud_text(ds_str(0xC5D8), xr - 4 * len, y, col);
}

/* 0000:8B93 msg_post: one message slot; it stays for 3 * sim_rate frames */
void msg_post(char *text)
{
    strcpy_fn(ds_str(0xC578), text);
    DSW(0x409E) = (u16)(DSW(0x3660) * 3);
}

/* 0000:89FE mfd_text: a character cell of the MFD text area (4 x 6 pixels) */
void mfd_text(int mode, int col, int row, char *s, int colour)
{
    if (DSW(0x365E) == 0) return;
    if ((s16)mode == 1) hud_text(s, col * 4 + 0x4A, row * 6 + 0x6E, colour);
    else if ((s16)mode == 2) hud_text(s, col * 4 + 0xA9, row * 6 + 0x6E, colour);
}

/* ------------------------------------------------------------------------------------------------------------------ side strips */

/* 0000:D9A3 hud_side_strips: the cockpit's side decorations on the back page and the weapon-select highlight of the panel */
void hud_side_strips(int page)
{
    if (DSW(0x463E) == 0) return;
    int ax, ay, bx, by, h;
    if (DSB(0x294B)) {
        cockpit_keyblit(0x21, 0, 0x8D, 0, 0x36, 0x5B, 0);
        cockpit_keyblit(0xE9, 0, 0xC4, 0, 0x36, 0x5B, 0);
        ax = 0x5F; ay = 0x2B; bx = 0xDB; h = 0x30; by = 0x55;
    } else {
        ax = 0x2B; bx = 0x110; h = 0x3A; ay = 0x21; by = 0x21;
    }
    blit_rect(2, 0x108, 0x21, page, ax, ay, 6, h);
    blit_rect(2, 0x10F, 0x21, page, bx, ay, 6, h);
    if (DSW(0x46C4) != DSW(0x46C2)) {
        int old = (s16)DSW(0x46C4);
        if (old) {
            u16 rec = (u16)(0x46C6 + 6 * (old - 1));
            rect_recolor(ctx_ptr(CTX_P2_PTR), 0xFC, DSS(rec) - 3, 0x103, DSS(rec + 2) + DSS(rec) - 3, DSS(rec + 4), 0x1E);
        }
        int sel = (s16)DSW(0x46C2);
        if (sel) {
            u16 rec = (u16)(0x46C6 + 6 * (sel - 1));
            rect_recolor(ctx_ptr(CTX_P2_PTR), 0xFC, DSS(rec) - 3, 0x103, DSS(rec + 2) + DSS(rec) - 3, 0x1E, DSS(rec + 4));
        }
        DSW(0x46C4) = DSW(0x46C2);
    }
    blit_rect(2, 0xFB, 0x43, page, by, 0x46, 0xA, 0x15);
}
