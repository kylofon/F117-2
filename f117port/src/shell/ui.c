/* The shell's UI toolkit (spec shell.md Part B): text helpers, framed popups and dialogs, the menu/input engine with the software cursor,
 * the waits, the button-highlight animation, the palette queue and the fades, the screen loader. START 0000:24F7 .. 4100 and the library
 * pieces it uses (826C, 3517). The button tables, dialog descriptors, colour triples and picture names stay in the loaded image's DGROUP
 * (DS offsets in the generated symbols header); the callbacks that those tables hold as code offsets are mapped by menu_cb().
 *
 * PORT: the fades do not juggle the timer (30A0 removes and reinstalls the ISR for the BIOS clock) and do not calibrate on the BIOS clock:
 * every pal_fade step is one pal_set (one retrace), 15 steps per speed unit (the original's 4 BIOS ticks = 0.22 s). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell/shell.h"

ShellTest shell_test;

/* ---------------------------------------------------------------------------------------------------------------- text */

/* START 0000:3567 ui_text: spec shell.md "3567 ui_text" */
void ui_text(void *c, char *s, int x, int y)
{
    TextCtx *t = c;
    t->x = (s16)x;
    t->y = (s16)y;
    text_noclip(c, s);                                           /* slot 5 */
}

/* START 0000:373C ui_text_width: spec shell.md "373C int ui_text_width" */
int ui_text_width(void *c, char *s)
{
    int font = ((TextCtx *)c)->font;
    u16 w = 0;
    for (; *s; s++) w = (u16)(w + font_char_width((u8)*s, font));    /* slot 47 */
    return (s16)w;
}

/* START 0000:36C4 ui_text_shadow: spec shell.md "36C4 ui_text_shadow" */
void ui_text_shadow(void *c, char *s, int x, int y)
{
    TextCtx *t = c;
    if (t->bg != -1) {
        TextCtx copy = *t;
        copy.fg = copy.bg;
        ui_text(&copy, s, x + 1, y);
        ui_text(&copy, s, x, y + 1);
        ui_text(&copy, s, x + 1, y + 1);
    }
    ui_text(c, s, x, y);
}

/* START 0000:3776 ui_text_wrap: spec shell.md "3776 int ui_text_wrap". Reproduces the original's scan, including the back-up test that
 * compares with the START of the whole string (not of the line). */
int ui_text_wrap(void *c, char *s, int x, int y, int width, int line_h, int centre)
{
    TextCtx *t = c;
    u16 maxw = 0, lw, len;
    int font = t->font;
    char *p = s, *ls = s;
    bool done = false;
    t->y = (s16)y;
    for (;;) {
        len = 0;
        lw = 0;
        while (lw <= (u16)width) {
            if (*p == 0 || *p == '\n') break;
            lw = (u16)(lw + font_char_width((u8)*p, font));
            p++;
            len++;
        }
        if (lw > (u16)width) { p--; len--; }
        while (*p != ' ') {
            if (*p == 0 || *p == '\n' || *p == '-' || s >= p) break;
            p--;
            len--;
        }
        if (*p == '-') len++;
        if (*p == 0) done = true;
        if (len != 0) {
            char buf[0x200];
            memcpy(buf, ls, len);
            buf[len] = 0;
            lw = (u16)ui_text_width(t, buf);
            if (lw > maxw) maxw = lw;
            if (t->page != -1) {
                t->x = (s16)(centre ? (u16)(((u16)width - lw) >> 1) + (u16)x : x);
                ui_text_shadow(t, buf, t->x, t->y);
            }
            t->y = (s16)(t->y + line_h);
        } else if (*p == '\n') {
            t->y = (s16)(t->y + line_h);
        }
        p++;
        ls = p;
        if (done) return (s16)maxw;
    }
}

/* START 0000:2A84 ui_text_centered: spec shell.md "2A84 ui_text_centered" */
void ui_text_centered(void *c, char *s, int cx, int y)
{
    TextCtx *t = c;
    t->x = (s16)(cx - (ui_text_width(c, s) >> 1));
    t->y = (s16)y;
    text_noclip(c, s);
}

/* START 0000:3693 ui_sprite_draw: spec shell.md "3693 ui_sprite_draw": the sprite's header words are its size */
void ui_sprite_draw(void *c, int x, int y, unsigned seg)
{
    sprite_scaled(c, x, y, rd16((u16)seg, 0), rd16((u16)seg, 2), 0, seg);          /* slot 22 */
}

/* START 0000:2B06 ui_wait_ticks: spec shell.md "2B06 ui_wait_ticks" */
void ui_wait_ticks(int n)
{
    while ((s16)n != 0) {
        pal_apply_queue();
        n--;
    }
}

/* ------------------------------------------------------------------------------------------- frames, buttons, popups */

/* START 0000:2CB4 ui_draw_frame: spec shell.md "2CB4 ui_draw_frame". The bevel tiles come from page 2 (requestr.pic). */
void ui_draw_frame(void *c, int x, int y, int w, int h)
{
    TextCtx *t = c;
    int pg = t->page;
    blit_rect(2, 0, 0, pg, x, y, 2, 2);
    blit_rect(2, 2, 0, pg, x + w - 2, y, 2, 2);
    blit_rect(2, 0, 2, pg, x, y + h - 2, 2, 2);
    blit_rect(2, 2, 2, pg, x + w - 2, y + h - 2, 2, 2);
    t->fg = 0xE0; gfx_fill_rect(c, x + 2, y, x + w - 3, y + 1);
    t->fg = 0xE6; gfx_fill_rect(c, x + 2, y + h - 2, x + w - 3, y + h - 1);
    t->fg = 0xE1; gfx_fill_rect(c, x, y + 2, x + 1, y + h - 3);
    t->fg = 0xE5; gfx_fill_rect(c, x + w - 2, y + 2, x + w - 1, y + h - 3);
    t->fg = 0xE2; gfx_fill_rect(c, x + 2, y + 2, x + w - 3, y + h - 3);
}

/* START 0000:2DF2 ui_draw_button: spec shell.md "2DF2 int ui_draw_button" */
int ui_draw_button(char *s, int cx, int y, int colour)
{
    TextCtx *wt = tctx(SH_CTX2);
    wt->font = 1;
    int w = ui_text_width(wt, s) + 8;
    int x = cx - (w >> 1);
    ui_draw_frame(wt, x, y, w, 16);
    wt->fg = (s16)colour;
    ui_text_shadow(wt, s, x + 4, y + 4);
    return w;
}

/* popup geometry: DS:B2F6 x, B2F8 y, B2FC w, B302 h */
static void popup_src(int *sx, int *sy)
{
    if ((s16)DSW(START_DS_ui_popup_h) > 0x62) { *sx = (s16)DSW(START_DS_ui_popup_w); *sy = 4; }
    else { *sx = 0; *sy = (s16)DSW(START_DS_ui_popup_h) + 4; }
}

/* START 0000:2B1B ui_popup_text: spec shell.md "2B1B ui_popup_text" */
void ui_popup_text(char *s, int x, int y, int font, int centre, int wrap_w)
{
    TextCtx *wt = tctx(SH_CTX2);
    wt->font = (s16)font;
    TextCtx loc = *wt;
    loc.page = -1;
    int w = ui_text_wrap(&loc, s, 0, 0, wrap_w, 8, 0);
    DSW(START_DS_ui_popup_w) = (u16)(w + 12);
    DSW(START_DS_ui_popup_h) = (u16)(loc.y + 12);
    int pw = (s16)DSW(START_DS_ui_popup_w), ph = (s16)DSW(START_DS_ui_popup_h);
    int px = centre ? x - (pw >> 1) : x;
    if (px < 0) px = 0; else if (px + pw > 0x140) px = 0x140 - pw;
    int py = centre ? y - (ph >> 1) : y;
    if (py < 0) py = 0; else if (py + ph > 0xC8) py = 0xC8 - ph;
    DSW(0xB2F6) = (u16)px;
    DSW(START_DS_ui_popup_y) = (u16)py;
    int sx, sy;
    popup_src(&sx, &sy);
    blit_rect(1, px, py, 2, sx, sy, pw, ph);                     /* the background under the box */
    ui_draw_frame(wt, 0, 4, pw, ph);
    wt->fg = 0xF;
    wt->bg = 8;
    ui_text_wrap(wt, s, 6, 0xA, pw - 12, 8, centre);
    blit_rect(2, 0, 4, 1, px, py, pw, ph);
    blit_rect(2, 0, 4, 0, px, py, pw, ph);
}

/* START 0000:3588 ui_popup_restore: spec shell.md "2B1B ui_popup_text" (restore) */
void ui_popup_restore(void)
{
    int sx, sy;
    popup_src(&sx, &sy);
    int px = (s16)DSW(0xB2F6), py = (s16)DSW(START_DS_ui_popup_y), pw = (s16)DSW(START_DS_ui_popup_w), ph = (s16)DSW(START_DS_ui_popup_h);
    blit_rect(2, sx, sy, 1, px, py, pw, ph);
    blit_rect(2, sx, sy, 0, px, py, pw, ph);
}

/* START 0000:2AB4 ui_popup_restore_box: the same for a dialog descriptor (w at +6, h at +8; the background is at (0, h + 4)) */
void ui_popup_restore_box(void *box, int x, int y)
{
    u8 *b = box;
    int w = (s16)PW(b, 6), h = (s16)PW(b, 8);
    blit_rect(2, 0, h + 4, 1, x, y, w, h);
    blit_rect(2, 0, h + 4, 0, x, y, w, h);
}

/* ------------------------------------------------------------------------------------------------------------ dialogs */

/* START 0000:332E ui_dialog_draw: spec shell.md "332E ui_dialog_draw". The descriptor is data in DGROUP: msg, button1, button2 (word
 * pointers), w, h (in/out), cb, hotkey, count and the two button rects the draw fills in. */
void ui_dialog_draw(void *dv, int x, int y)
{
    u8 *d = dv;
    TextCtx *wt = tctx(SH_CTX2);
    wt->font = 4;
    TextCtx loc = *wt;
    loc.page = -1;
    char *msg = ds_str(PW(d, 0));
    int tw = ui_text_wrap(&loc, msg, 0, 0, (u16)(PW(d, 6) - 12), 8, 0);
    PW(d, 6) = (u16)(tw + 12);
    PW(d, 8) = (u16)(loc.y + 0x1A);
    int w = (s16)PW(d, 6), h = (s16)PW(d, 8), ly = loc.y;
    ui_draw_frame(wt, 0, 4, w, h);
    wt->fg = 0xF;
    wt->bg = 8;
    ui_text_wrap(wt, msg, 6, 8, w - 12, 8, 1);
    int c1, c2 = 0;
    if (PW(d, 4)) {
        c1 = (u16)(w - 4) / 3 + 2;
        c2 = (u16)((u16)(w - 4) << 1) / 3 + 2;
    } else {
        c1 = ((u16)(w - 4) >> 1) + 2;
    }
    int bw = ui_draw_button(ds_str(PW(d, 2)), c1, ly + 0xA, 0xE8);
    PW(d, 0x0E) = (u16)(x - (bw >> 1) + c1);
    PW(d, 0x12) = (u16)((bw >> 1) + x + c1);
    PW(d, 0x10) = (u16)(y + ly + 6);
    PW(d, 0x14) = (u16)(y + ly + 0x16);
    if (PW(d, 4)) {
        bw = ui_draw_button(ds_str(PW(d, 4)), c2, ly + 0xA, 0xE7);
        PW(d, 0x16) = (u16)(x - (bw >> 1) + c2);
        PW(d, 0x1A) = (u16)((bw >> 1) + x + c2);
        PW(d, 0x18) = (u16)(y + ly + 6);
        PW(d, 0x1C) = (u16)(y + ly + 0x16);
    }
    blit_rect(1, x, y, 2, 0, h + 4, w, h);
    blit_rect(2, 0, 4, 1, x, y, w, h);
    blit_rect(2, 0, 4, 0, x, y, w, h);
}

/* the idle callbacks of the menu tables are code offsets in the image; the port maps them to its functions */
static void menu_cb(u16 off)
{
    switch (off) {
    case 0x3DA2: ui_dialog_hover(); break;
    case 0x0B01: arm_hover_hook(); break;
    case 0x11DB: brief_hover_hook(); break;
    case 0x63FB: roster_hover_cb(); break;
    case 0x71AD: roster_options_hover_cb(); break;
    case 0x5E6F: ui_orders_hover(); break;
    default: host_fatal("menu callback %04X is not ported", off);
    }
}

/* START 0000:3CEA ui_dialog_run: spec shell.md "3CEA int ui_dialog_run" */
int ui_dialog_run(void *dv, int x, int y)
{
    u8 *d = dv;
    u8 saved_hover = DSB(START_DS_ui_hover_prev);
    int sx = (s16)DSW(START_DS_ui_cursor_x), sy = (s16)DSW(START_DS_cursor_y);
    memcpy(DSP(0x679B), DSP(START_DS_rgb_normal), 3);
    memcpy(DSP(0x6798), DSP(0x679B), 3);
    pal_queue_push(0xE7, DSP(0x6798), 2);
    pal_apply_queue();
    ui_dialog_draw(d, x, y);
    memcpy(DSP(START_DS_ui_menu_cur), d + 0x0A, 0x84);
    ui_cursor_set_pos((s16)DSW(0xB310) - 3, (s16)DSW(0xB312) - 3);
    int r;
    do r = ui_menu_loop(DSP(START_DS_ui_menu_cur)); while (r == 0x63);
    ui_wait_ticks(6);
    ui_popup_restore_box(d, x, y);
    DSB(START_DS_ui_hover_prev) = saved_hover;
    ui_cursor_set_pos(sx, sy);
    return r;
}

/* START 0000:3DA2 ui_dialog_hover: spec shell.md "3DA2 ui_dialog_hover" -- the cb of every dialog */
void ui_dialog_hover(void)
{
    int i = ui_hit_test(DSP(0xB30C), DSB(0xB30B));
    if (i) {
        memcpy(DSP(0x679E - 3 * i), DSP(START_DS_rgb_hover), 3);
        memcpy(DSP(0x6795 + 3 * i), DSP(START_DS_rgb_normal), 3);
    } else {
        memcpy(DSP(0x679B), DSP(START_DS_rgb_normal), 3);
        memcpy(DSP(0x6798), DSP(0x679B), 3);
    }
    if (DSB(START_DS_ui_hover_prev) != i) pal_queue_push(0xE7, DSP(0x6798), 2);
    DSB(START_DS_ui_hover_prev) = (u8)i;
}

/* ---------------------------------------------------------------------------------------------------------- the cursor */

/* START 0000:393D ui_hit_test: spec shell.md "393D int ui_hit_test"; rects of 4 words (x0, y0, x1, y1), unsigned compares */
int ui_hit_test(void *rects, int n)
{
    u8 *r = rects;
    u16 cx = DSW(START_DS_ui_cursor_x), cy = DSW(START_DS_cursor_y);
    int i;
    for (i = 0; (s16)i < (s16)n; i++, r += 8) {
        if (PW(r, 0) > cx) continue;
        if (PW(r, 4) < cx) continue;
        if (PW(r, 2) > cy) continue;
        if (PW(r, 6) < cy) continue;
        break;
    }
    return (s16)i < (s16)n ? i + 1 : 0;
}

/* START 0000:3989 ui_cursor_erase: spec shell.md "3989": the clean page 1 over the screen where the cursor was drawn */
void ui_cursor_erase(void)
{
    blit_rect(1, (s16)DSW(START_DS_cursor_old_x), (s16)DSW(START_DS_cursor_old_y), 0, (s16)DSW(START_DS_cursor_old_x),
              (s16)DSW(START_DS_cursor_old_y), DSB(START_DS_cursor_w), DSB(START_DS_cursor_h));
}

/* START 0000:3E20 ui_cursor_init: spec shell.md "3E20" */
void ui_cursor_init(unsigned seg)
{
    DSW(START_DS_cursor_sprite_seg) = (u16)seg;
    DSB(START_DS_cursor_w) = rd8((u16)seg, 0);
    DSB(START_DS_cursor_h) = rd8((u16)seg, 2);
    DSB(0xE094) = 2;
    DSB(0xE095) = 2;
    DSW(START_DS_cursor_old_y) = 0;
    DSW(START_DS_cursor_y) = 0;
    DSW(START_DS_cursor_old_x) = 0;
    DSW(START_DS_ui_cursor_x) = 0;
}

/* START 0000:3E84 ui_cursor_set_pos: spec shell.md "3E84" */
void ui_cursor_set_pos(int x, int y)
{
    DSW(START_DS_ui_cursor_x) = (u16)x;
    DSW(START_DS_cursor_y) = (u16)y;
    if (comm_get()->input_devices & 2) mouse_set_pos();
}

/* START 0000:3E5E ui_check_break: spec shell.md "3E5E". PORT: START never installs the Ctrl-Break handler, so [73F2] stays 0 in the
 * original; a closed window must still end the program. */
void ui_check_break(void)
{
    if (DSB(0x73F2) || host_quit_requested()) shell_exit(0);
}

/* START 0000:3E6D ui_kbd_flush (no callers) */
void ui_kbd_flush(void)
{
    while (kbd_empty_misc() == 0) kbd_get_misc();
}

/* START 0000:39B3 / 39D5: x0 > x1 disables a button rectangle, x0 <= x1 enables it */
void ui_order_desc(unsigned *p)                                  /* 39B3 */
{
    u16 *w = (u16 *)p;
    if (w[2] > w[0]) { u16 t = w[0]; w[0] = w[2]; w[2] = t; }
}

void ui_order_asc(unsigned *p)                                   /* 39D5 */
{
    u16 *w = (u16 *)p;
    if (w[2] < w[0]) { u16 t = w[0]; w[0] = w[2]; w[2] = t; }
}

/* START 0000:39F7 ui_poll_input: spec shell.md "39F7 int ui_poll_input". The input and cursor engine of every menu: joystick and mouse
 * buttons, the key word, arrow keys that jump to the next button rectangle, the clamp, the cursor sprite. */
int ui_poll_input(void *mv)
{
    u8 *m = mv;
    Comm *comm = comm_get();
    if (shell_test.on_poll) { shell_test.polls++; shell_test.on_poll(shell_test.polls); }
    int a = 0, b = 0, key = 0;
    int old_x = 0, old_y = 0;
    if (comm->input_devices & 1) { a = joy_button_misc(0); b = joy_button_misc(1); }
    if (comm->input_devices & 2) {
        mouse_poll();
        old_x = (s16)DSW(START_DS_ui_cursor_x);
        old_y = (s16)DSW(START_DS_cursor_y);
        a |= mouse_button(0);
        b |= mouse_button(1);
    }
    if (kbd_empty_misc() == 0) key = (u16)kbd_get_misc();
    else if (a) key = 0x0D;
    else if (b) key = 0x1B;
    switch (key) {
    case 0:
        if (comm->input_devices & 1) {
            DSW(START_DS_ui_cursor_x) = (u16)(DSW(START_DS_ui_cursor_x) + ((int)DSB(0x69B2) - 0x80) / 0x1E);
            DSW(START_DS_cursor_y) = (u16)(DSW(START_DS_cursor_y) + ((int)DSB(0x69B3) - 0x80) / 0x1E);
        }
        break;
    case 0x1000: ui_quit_prompt(); break;
    case 0x4800: case 0x4B00: case 0x4D00: case 0x5000: {
        int best = 1000, bx = -1, by = 0;
        int n = m[3];                                             /* count byte of the menu */
        u8 *r = m + 4;
        for (int i = 0; i < n; i++, r += 8) {
            int cx = (u16)(PW(r, 4) + PW(r, 0)) >> 1, cy = (u16)(PW(r, 2) + PW(r, 6)) >> 1;
            int mx = (s16)DSW(START_DS_ui_cursor_x), my = (s16)DSW(START_DS_cursor_y);
            bool cand = false;
            int adx = abs(mx - cx), ady = abs(my - cy);
            switch (key) {
            case 0x4800: cand = my > cy && ady >= adx; break;
            case 0x5000: cand = my < cy && ady >= adx; break;
            case 0x4B00: cand = mx > cx && ady <= adx; break;
            case 0x4D00: cand = mx < cx && ady <= adx; break;
            }
            if (cand) {
                int dd = dist_approx(mx, my, cx, cy);
                if (dd < best) { best = dd; bx = cx; by = cy; }
            }
        }
        if (bx != -1) { DSW(START_DS_ui_cursor_x) = (u16)bx; DSW(START_DS_cursor_y) = (u16)by; }
        break;
    }
    default: break;
    }
    if ((s16)DSW(START_DS_ui_cursor_x) < (s16)DSW(START_DS_cursor_xmin)) DSW(START_DS_ui_cursor_x) = DSW(START_DS_cursor_xmin);
    else if ((s16)DSW(START_DS_ui_cursor_x) > (s16)DSW(START_DS_cursor_xmax)) DSW(START_DS_ui_cursor_x) = DSW(START_DS_cursor_xmax);
    if ((s16)DSW(START_DS_cursor_y) < (s16)DSW(START_DS_cursor_ymin)) DSW(START_DS_cursor_y) = DSW(START_DS_cursor_ymin);
    else if ((s16)DSW(START_DS_cursor_y) > (s16)DSW(START_DS_cursor_ymax)) DSW(START_DS_cursor_y) = DSW(START_DS_cursor_ymax);
    if ((comm->input_devices & 2) && ((s16)DSW(START_DS_ui_cursor_x) != old_x || (s16)DSW(START_DS_cursor_y) != old_y)) mouse_set_pos();
    ui_cursor_erase();
    ui_sprite_draw(tctx(SH_CTX0), (s16)DSW(START_DS_ui_cursor_x), (s16)DSW(START_DS_cursor_y), DSW(START_DS_cursor_sprite_seg));
    DSW(START_DS_cursor_old_x) = DSW(START_DS_ui_cursor_x);
    DSW(START_DS_cursor_old_y) = DSW(START_DS_cursor_y);
    ui_check_break();
    int result = 0;
    int k = key;
    if (k == 0x5300) {
        result = 0x62;
    } else {
        k &= 0xFF;
        if (k == 8) result = 0x62;
        else if (k == 0x0D && DSW(START_DS_ui_last_key) != 0x0D) result = 1;
        else if (k == 0x1B && DSW(START_DS_ui_last_key) != 0x1B) result = 2;
    }
    DSW(START_DS_ui_last_key) = (u16)k;
    if (result == 0) result = k;
    return result;
}

/* START 0000:38AC ui_menu_loop: spec shell.md "38AC int ui_menu_loop". The descriptor: cb (word, a code offset), hotkey, count, rects. */
int ui_menu_loop(void *mv)
{
    u8 *m = mv;
    int result = 0;
    do {
        if (comm_get()->input_devices & 1) joy_poll();
        pal_apply_queue();
        if (PW(m, 0)) menu_cb(PW(m, 0));
        u8 k = (u8)ui_poll_input(m);
        if (k == 0) continue;
        if (k == 1) {
            int i = ui_hit_test(m + 4, m[3]);
            if (i) { result = i; sh_snd(0x1A); }
            else sh_snd(0x22);
        } else if (k == 2) {
            result = 0x63;
            sh_snd(0x23);
        } else if (m[2] == k) {
            result = (int)(s8)k + 0x100;
        }
    } while (result == 0);
    return result;
}

/* ----------------------------------------------------------------------------------------------------------- the waits */

/* the buttons of the joystick and the mouse, shared by the wait loops: 1 = button 0, 2 = button 1 */
static int pointer_buttons(void)
{
    int r = 0;
    Comm *comm = comm_get();
    if (comm->input_devices & 1) {
        if (joy_button_misc(0)) r = 1;
        else if (joy_button_misc(1)) r = 2;
    }
    if (r == 0 && (comm->input_devices & 2)) {
        mouse_poll();
        if (mouse_button(0)) r = 1;
        else if (mouse_button(1)) r = 2;
    }
    return r;
}

/* START 0000:3EA5 ui_wait_click: spec shell.md "3EA5 int ui_wait_click" */
int ui_wait_click(void)
{
    for (;;) {
        pal_apply_queue();
        int r = pointer_buttons();
        if (r) return r;
        if (kbd_empty_misc() == 0) {
            int key = kbd_get_misc();
            if (key == 0x1000) ui_quit_prompt();
            key &= 0xFF;
            if (key == 0x0D) return 1;
            if (key == 0x1B) return 2;
        }
    }
}

/* START 0000:3F33 ui_wait_input: spec shell.md "3F33 int ui_wait_input". Phase 1 (only if ticks != 0): wait for the buttons to be released, a tick
 * each; phase 2: the keyboard is looked at first, then ticks run out or a button / key sets the result. ticks == 0 returns after one look at the
 * keyboard (the spec's "65536 ticks" is wrong: the zero test comes before the decrement). */
int ui_wait_input(int ticks)
{
    u16 t = (u16)ticks;
    int result = 0;
    Comm *comm = comm_get();
    if (t != 0) {
        do {
            pal_apply_queue();
            t--;
            result = 0;
            if (comm->input_devices & 1) { if (joy_button_misc(0) || joy_button_misc(1)) result = 1; }
            if (comm->input_devices & 2) {
                mouse_poll();
                if (mouse_button(0) || mouse_button(1)) result = 1;
            }
        } while (result != 0 && t != 0);
    }
    result = 0;
    for (;;) {
        if (kbd_empty_misc() == 0) {
            int key = kbd_get_misc();
            if (key == 0x1000) ui_quit_prompt();
            key &= 0xFF;
            if (key == 0x0D) result = 1;
            if (key == 0x1B) result = 2;
            if (key == 0x20) result = 3;
        }
        if (t == 0) break;
        if (result != 0) break;
        pal_apply_queue();
        t--;
        if (comm->input_devices & 1) {
            if (joy_button_misc(0)) result = 1;
            if (joy_button_misc(1)) result = 2;
        }
        if (comm->input_devices & 2) {
            mouse_poll();
            if (mouse_button(0)) result = 1;
            if (mouse_button(1)) result = 2;
        }
    }
    return result;
}

/* START 0000:405F ui_wait_skip: spec shell.md "405F int ui_wait_skip": only Space reports "skip" */
int ui_wait_skip(int ticks)
{
    switch (ui_wait_input(ticks)) {
    case 1: sh_snd(0x1A); return 0;
    case 2: sh_snd(0x23); return 0;
    case 3: sh_snd(0x1A); return 1;
    default: return 0;
    }
}

/* ------------------------------------------------------------------------------------------ button highlight animation */

/* START 0000:28CE ui_anim_start: spec shell.md "4 Button-highlight animation" */
void ui_anim_start(int src_page, int tbl, int seq, int steps)
{
    if (DSB(START_DS_ui_anim_left) != 0) return;                 /* busy: the request is dropped */
    DSW(START_DS_ui_anim_rects) = (u16)tbl;
    DSW(START_DS_ui_anim_seq) = (u16)seq;
    DSB(START_DS_ui_anim_src_page) = (u8)src_page;
    DSB(START_DS_ui_anim_left) = (u8)steps;
    DSB(START_DS_ui_anim_tick) = 0;
    DSW(START_DS_ui_anim_h) = 0;
}

/* START 0000:2A7E */
int ui_anim_active(void) { return DSB(START_DS_ui_anim_left); }

/* START 0000:2901 ui_anim_step: runs on every 8th call; blits the highlight cels of the current sequence entry */
int ui_anim_step(void)
{
    if (DSB(START_DS_ui_anim_left) != 0) {
        u8 al = DSB(START_DS_ui_anim_tick)++;
        if ((al & 7) == 0) goto run;
    }
    return DSB(START_DS_ui_anim_left);
run:
    if (DSW(START_DS_ui_anim_h) != 0)
        blit_rect(3, (s16)DSW(START_DS_ui_anim_x), (s16)DSW(START_DS_ui_anim_y), 1, (s16)DSW(START_DS_ui_anim_x),
                  (s16)DSW(START_DS_ui_anim_y), (s16)DSW(START_DS_ui_anim_w), (s16)DSW(START_DS_ui_anim_h));
    u16 minx = 0x140, miny = 0x140, maxx = 0, maxy = 0;
    u16 seq = DSW(START_DS_ui_anim_seq);
    u16 rec = (u16)(DSB(seq) * 12 + DSW(START_DS_ui_anim_rects));
    for (u16 i = 0; i < DSB(seq + 1); i++, rec = (u16)(rec + 12)) {
        blit_rect(DSB(START_DS_ui_anim_src_page), (s16)DSW(rec), (s16)DSW(rec + 2), 1, (s16)DSW(rec + 4), (s16)DSW(rec + 6),
                  (s16)DSW(rec + 8), (s16)DSW(rec + 10));
        if (DSW(rec + 4) < minx) minx = DSW(rec + 4);
        if (DSW(rec + 6) < miny) miny = DSW(rec + 6);
        if ((u16)(DSW(rec + 4) + DSW(rec + 8)) > maxx) maxx = (u16)(DSW(rec + 4) + DSW(rec + 8));
        if ((u16)(DSW(rec + 6) + DSW(rec + 10)) > maxy) maxy = (u16)(DSW(rec + 6) + DSW(rec + 10));
    }
    u16 ux0 = DSW(START_DS_ui_anim_x) < minx ? DSW(START_DS_ui_anim_x) : minx;
    u16 uy0 = DSW(START_DS_ui_anim_y) < miny ? DSW(START_DS_ui_anim_y) : miny;
    u16 ux1 = (u16)(DSW(START_DS_ui_anim_x) + DSW(START_DS_ui_anim_w));
    if (!(ux1 > maxx)) ux1 = maxx;
    u16 uy1 = (u16)(DSW(START_DS_ui_anim_y) + DSW(START_DS_ui_anim_h));
    if (!(uy1 > maxy)) uy1 = maxy;
    blit_rect(1, ux0, uy0, 0, ux0, uy0, (s16)(ux1 - ux0), (s16)(uy1 - uy0));
    DSW(START_DS_ui_anim_y) = miny;
    DSW(START_DS_ui_anim_x) = minx;
    DSW(START_DS_ui_anim_w) = (u16)(maxx - minx);
    DSW(START_DS_ui_anim_h) = (u16)(maxy - miny);
    DSW(START_DS_ui_anim_seq) = (u16)(seq + 2);
    DSB(START_DS_ui_anim_left)--;
    return DSB(START_DS_ui_anim_left);
}

/* ------------------------------------------------------------------------------------------- palette queue and fades */

/* START 0000:3517 pal_queue_push: spec shell.md "3517 pal_queue_push". `rgb` is a pointer into DGROUP; the record keeps its offset. */
void pal_queue_push(int first, void *rgb, int count)
{
    u16 e = (u16)(START_DS_pal_queue + 6 * DSB(START_DS_pal_queue_count));
    DSW(e) = (u16)first;
    DSW(e + 2) = ds_off(rgb);
    DSW(e + 4) = (u16)count;
    DSB(START_DS_pal_queue_count)++;
}

/* START 0000:3548: pal_queue_push and the click sound (no callers) */
void pal_queue_push_click(int first, void *rgb, int count)
{
    pal_queue_push(first, rgb, count);
    sh_snd(0x1A);
}

/* START 0000:826C pal_select: palette n (clamped to 8) of PALETTES.PAL into the working palette DS:64E3 */
void pal_select(int n)
{
    if ((s16)n >= 8) n = 8;
    FarPtr p = ds_far(START_DS_palettes_ptr);
    memcpy(DSP(START_DS_pal_buffer), far_mp(far_add(p, (u16)(n * 0x300))), 0x300);
}

/* START 0000:31AD pal_fade_calibrate: counts the pal_set calls that fit in 4 BIOS ticks. PORT: no measuring; the fade is paced by the
 * retrace (one pal_set per tick), 15 per speed unit. */
void pal_fade_calibrate(void *buf)
{
    (void)buf;
    DSB(START_DS_pal_fade_calls) = 15;
}

/* START 0000:30A0 pal_fade: spec shell.md "3070 / 3088 / 30A0": out[i] = src[i] * cur^2 >> 16 for cur = from .. to over steps */
void pal_fade(void *pal, int from, int to, int speed)
{
    u8 out[0x300];
    const s8 *src = pal;
    while (DSB(START_DS_pal_queue_count) != 0) pal_apply_queue();
    s32 cur = (s16)from, delta = 0, steps = 0;
    bool final_frame = (s16)speed == 0;
    if (final_frame) cur = (s16)to;
    for (;;) {
        for (int i = 0; i < 0x300; i++) out[i] = (u8)(((s32)src[i] * cur * cur) >> 16);
        if (DSB(START_DS_pal_fade_calls) == 0) pal_fade_calibrate(out);
        pal_set(out);
        if ((s16)to == cur) break;
        if (steps == 0) {
            steps = (s32)(s8)DSB(START_DS_pal_fade_calls) * (s16)speed;
            s32 div = steps ? steps : 0xFF;
            delta = ((s16)to - (s16)from) / div;
        }
        steps--;
        if (steps > 0) cur += delta; else cur = (s16)to;
    }
}

/* START 0000:3070 / 3088 */
void pal_fade_in(int speed) { pal_fade(DSP(START_DS_pal_buffer), 0, 0x100, speed); }
void pal_fade_out(int speed) { pal_fade(DSP(START_DS_pal_buffer), 0x100, 0, speed); }

/* ------------------------------------------------------------------------------------------------ files and screens */

/* START 0000:2864 ui_load_file_far: open, read up to FFFFh bytes to seg:off, close; returns the byte count */
unsigned ui_load_file_far(char *name, unsigned off, unsigned seg)
{
    int fd = dos_open(name, 0);
    int n = dos_read_far(fd, 0xFFFF, off, seg);
    dos_close(fd);
    return (unsigned)n;
}

/* START 0000:289A ui_load_file_near: the same into a buffer in DGROUP */
unsigned ui_load_file_near(char *name, void *buf)
{
    int fd = dos_open(name, 0);
    int n = dos_read(fd, 0xFFFF, buf);
    dos_close(fd);
    return (unsigned)n;
}

/* START 0000:35FC ui_load_screen_pic: "<name>.pic" into the file buffer, decoded into the page (the palette is not touched) */
void ui_load_screen_pic(int name_idx, int page_slot)
{
    char tmp[0x20];
    snprintf(tmp, sizeof tmp, "%s.pic", ds_str_ptr(START_DS_tbl_pic_names, name_idx));
    ui_load_file_far(tmp, DSW(START_DS_file_buf_off), DSW(START_DS_file_buf_seg));
    pic_to_seg_b(DSW(START_DS_file_buf_seg2), DSW(START_DS_ui_page_seg + 2 * page_slot));
}

/* START 0000:3641 ui_show_screen: fade out, picture to page 1, its palette, page 1 to the screen, the screen's own overlay, fade in */
void ui_show_screen(int name_idx, void *overlay)
{
    pal_fade_out(1);
    ui_load_screen_pic(name_idx, 1);
    pal_select(name_idx);
    blit_rect(1, 0, 0, 0, 0, 0, 320, 200);
    if (overlay) ((void (*)(void))overlay)();
    pal_fade_in(1);
    screen_on();
}

/* START 0000:7611 ui_quit_prompt: Alt-Q. adv.pic asks the question; Y quits, anything else restores the screen. */
void ui_quit_prompt(void)
{
    u8 save[0x300];
    sh_snd(0);
    memcpy(save, DSP(START_DS_pal_buffer), 0x300);
    pal_fade_out(1);
    ui_load_screen_pic(0x1F, 0);
    ui_load_file_near("adv.pal", DSP(START_DS_pal_buffer));
    pal_fade_in(1);
    while (kbd_empty_misc() != 0) host_pump();
    int c = kbd_get_misc() & 0x5F;
    if (c == 0x59) { shell_exit(0); return; }
    pal_fade_out(1);
    blit_rect(1, 0, 0, 0, 0, 0, 320, 200);
    memcpy(DSP(START_DS_pal_buffer), save, 0x300);
    pal_fade_in(1);
}

/* START 0000:76C0 ui_rand_mod: rand() % n (unsigned) */
unsigned ui_rand_mod(unsigned n)
{
    int r = rand_fn();
    if ((u16)n == 0) rtl_divide_error();
    return (unsigned)(r % (u16)n);
}
