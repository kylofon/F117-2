/* The outcome scenes (crash, rescue, capture, landing), the summary screen and the closing scene of END (spec debrief.md sections 5 and 8). Every text
 * is built from the format strings of END's DGROUP (offsets as in the spec's Appendix A.1) with the same arguments as the original; the reference is
 * port/notes/s39/s39_ref_ui.py. */
#include <stdio.h>
#include <string.h>

#include "debrief/debrief.h"

#define SF(buf, off, ...) snprintf((buf), sizeof(buf), e_str(off), __VA_ARGS__)

static void ctx0_text_style(int font, int fg, int bg)
{
    TextCtx *c = tctx(END_CTX0);
    if (font >= 0) c->font = (s16)font;
    c->fg = (s16)fg;
    c->bg = (s16)bg;
}

/* ---------------------------------------------------------------------------------------------------------- 2BEC summary */

static void cat(char *dst, size_t size, const char *s)
{
    size_t n = strlen(dst);
    snprintf(dst + n, size - n, "%s", s);
}

void summary_screen(void)
{
    char buf[1024], tmp[1024], msg[1280];
    int rating = (s16)score(0x100);
    screen_off();
    ui_load_file_far("flag.pic", DSW(0x6442), DSW(0x6444));
    pic_to_seg_b(DSW(0x7220), D_PAGE_SEG(0));
    ui_load_file_near("flag.pal", DSP(0x1F25));
    pal_set_default();
    screen_on();

    int obj = (D_PRIMARY << 1) | D_SECONDARY;
    char *on = e_str_at(0xDAE, obj);
    char *name = e_str(0x7228);
    u16 c26 = CW(0x26), c28 = CW(0x28), c30 = CW(0x30);
    buf[0] = 0;
    if (c28 != 0 && c30 != 0) {
        if (obj) SF(tmp, 0x7D1, on); else snprintf(tmp, sizeof tmp, "%s", e_str(0x7F5));
        SF(buf, 0x817, name, tmp);
    } else if (c28 != 0) {
        SF(buf, obj ? 0x860 : 0x89B, name);
    } else if (c26 != 1 && D_LANDCLS == 0) {
        if (obj) SF(tmp, 0x8CE, on); else snprintf(tmp, sizeof tmp, "%s", e_str(0x907));
        SF(buf, 0x964, name, tmp);
    } else if (c26 == 2 && D_LANDCLS != 0) {
        if (obj) SF(buf, 0x9B3, name, on); else SF(buf, 0x9F1, name);
    } else if (c26 == 3) {
        if (obj) SF(buf, 0xA44, name, on); else SF(buf, 0xA71, name);
    }                                                            /* (B) otherwise the original prints uninitialised stack; the port prints nothing */
    snprintf(msg, sizeof msg, "%s%s", buf, e_str(0xAB8));
    snprintf(tmp, sizeof tmp, "%s", c30 == 0 ? "" : e_str(0xABB));
    snprintf(buf, sizeof buf, e_str(0xAF5), rating, tmp);
    cat(msg, sizeof msg, buf);
    int air = DSB(0x64C8) + DSB(0x55DC);
    if (air > 0) {
        if (air > 1 && air < 12) { SF(buf, 0xB29, air); cat(msg, sizeof msg, buf); }
        else if (air >= 12) { SF(buf, 0xB4D, air); cat(msg, sizeof msg, buf); }
        else cat(msg, sizeof msg, e_str(0xBA1));
    }
    int gnd = DSB(0x6984) + DSB(0x6D88);
    if (gnd > 0) {
        if (gnd == 1) cat(msg, sizeof msg, e_str(0xBC2));
        else { SF(buf, 0xBEB, gnd); cat(msg, sizeof msg, buf); }
    }
    u16 war = EP(0x3A);
    int civ = DSB(0x5B9A);
    if (war == 0) {
        int mil = DSB(0x55DC) + DSB(0x6D88);
        if (civ || mil) {
            if (civ && mil) SF(tmp, 0xC1B, civ, mil);
            else if (mil) SF(tmp, 0xC44, mil);
            else SF(tmp, 0xC5E, civ);
            SF(buf, 0xC6B, tmp);
            cat(buf, sizeof buf, (civ + mil) > 1 ? e_str(0xC98) : e_str(0xCB2));
            cat(msg, sizeof msg, buf);
        }
    } else if (war == 1 && civ) {
        SF(buf, 0xCCA, civ);
        cat(buf, sizeof buf, civ > 1 ? e_str(0xD01) : e_str(0xD1B));
        cat(msg, sizeof msg, buf);
    }
    if (DSB(0x55DB) || DSB(0x55E8)) {
        cat(msg, sizeof msg, e_str(0xD32));
        if (DSB(0x55DB) && DSB(0x55E8)) cat(msg, sizeof msg, e_str(0xD77));
        else if (DSB(0x55DB)) cat(msg, sizeof msg, e_str(0xD93));
        else cat(msg, sizeof msg, e_str(0xD9D));
    }
    char *m = (char *)DSP(0x6986);                               /* the 1 KB message buffer of the original */
    snprintf(m, 0x400, "%s", msg);
    ctx0_text_style(4, 0xFD, 0xFE);
    TextCtx local = *tctx(END_CTX0);
    local.page = -1;                                             /* measure only */
    ui_text_wrap(&local, m, 0, 0, 260, 8, 0);
    int h = local.y;
    ui_text_wrap(tctx(END_CTX0), m, 30, 184 - h, 260, 8, 1);
    prompt_continue(160, 192);
    end_snd(0);
}

/* --------------------------------------------------------------------------------------------------------------- scenes */

/* 0000:3682 crash_cb: the crash text by comm+28h; (B) codes 0 and > 6 print an uninitialised pointer in the original, nothing here */
void crash_cb(void)
{
    static const u16 text[7] = { 0, 0xE90, 0xEC2, 0xEF1, 0xF24, 0xF68, 0xFA6 };
    u16 c = CW(0x28);
    if (c < 1 || c > 6) return;
    ctx0_text_style(4, 0xFD, 0xFE);
    ui_text_wrap(tctx(END_CTX0), e_str(text[c]), 175, 40, 120, 10, 1);
}

void cemetery_screen(void)
{
    show_screen_pic("cemetary", (void *)crash_cb);
    end_snd(0x25);
    prompt_continue(235, 172);
}

void rescue_cb(void)
{
    char msg[256];
    snprintf(msg, sizeof msg, "%s%s", e_str(0x105A), e_str(0x1096));
    ctx0_text_style(4, 0xFD, 0xFE);
    ui_text_wrap(tctx(END_CTX0), msg, 200, 109, 100, 8, 1);
}

void rescue_screen(void)
{
    show_screen_pic("rescue", (void *)rescue_cb);
    end_snd(0x26);
    prompt_continue(250, 192);
}

/* 0000:3BBF: sets the captured flag first (the awards and the summary read it) */
void captured_screen(void)
{
    D_CAPTURED = 1;
    show_screen_pic("tass", NULL);
    end_snd(0x26);
    ctx0_text_style(-1, 0xF, 0);
    prompt_continue(160, 192);
}

/* 0000:372F landed_cb: landed2..4 on pages 2..4, then the sky ramp of the palette buffer (entries 192..223 at DS:2165). (B) the search finds the start
 * record, so d is always 0 and the dusk ramp is always used. */
void landed_cb(void)
{
    load_pic("landed2.pic", 2);
    load_pic("landed3.pic", 3);
    load_pic("landed4.pic", 4);
    int i = 0;
    while (i < 0x100 && (DSB((u16)(E_LOG + 6 * i + 4)) & 0x3F) != 8) i++;
    u16 d = (u16)(DSW((u16)(E_LOG + 6 * i)) - DSW(E_LOG));
    if (d > 0x708) d = 0x708;
    if (d == 0) {
        memcpy(DSP(0x2165), DSP(0x1396), 0x60);
        return;
    }
    u16 tg = CB(0x33) ? 0x13F6 : 0x1336;
    for (int k = 0; k < 32; k++)
        for (int c = 0; c < 3; c++) {
            int diff = DSB((u16)(tg + c)) - DSB((u16)(0x1396 + 3 * k + c));
            DSB((u16)(0x2165 + 3 * k + c)) = (u8)(diff * 0x708 / d);
        }
}

void landed_screen(void)
{
    show_screen_pic("landed1", (void *)landed_cb);
    end_snd(0x27);
    ui_wait_ticks(0x78);
    blit_rect(2, 0, 0, 0, 51, 44, 210, 112);
    ui_wait_ticks(12);
    blit_rect(3, 0, 0, 0, 51, 30, 212, 96);
    ui_wait_ticks(12);
    blit_rect(4, 0, 0, 0, 52, 4, 212, 108);
    ui_wait_ticks(12);
    for (int i = 0; i < 4; i++) {
        blit_rect(4, 214, 16 * i, 0, 128, 126, 38, 16);
        ui_wait_ticks(12);
    }
    ctx0_text_style(-1, 0xF, 0);
    prompt_continue(160, 172);
    end_snd(0);
}

/* 0000:398F desk_cb: the rank insignia on the desk */
static void desk_cb(void)
{
    load_pic("requestr.pic", 2);
    blit_rect(2, 241 + 8 * (s16)EP(0x20), 156, 0, 275, 111, 8, 7);
}

/* 0000:39D0 ending_scene: the closing scene with a random quip, only for a pilot who is alive and flew a scoring mission */
void ending_scene(void)
{
    if (CW(0x28) != 0 || CW(0x30) != 0) return;
    ctx0_text_style(4, 0xFD, 0xFE);
    u16 n = EP(0x36), rk = EP(0x20);
    u16 tab, cnt;
    int scr;
    void (*cb)(void) = NULL;
    if (!D_PROMOTED && !D_MEDAL && !D_PRIMARY && !D_SECONDARY && n != 0x63) { tab = 0x48E7; cnt = 8; scr = 0; cb = desk_cb; }
    else if ((D_PRIMARY || D_SECONDARY) && D_LANDCLS == 0 && !D_PROMOTED && !D_MEDAL) { tab = 0x4510; cnt = 7; scr = 0; cb = desk_cb; }
    else if (n == 0x63 && rk == 6) { tab = 0x4A4A; cnt = 4; scr = 2; }
    else if (n == 0x63) { tab = 0x4E53; cnt = 4; scr = 2; }
    else if (D_PROMOTED || D_MEDAL) { tab = 0x4C32; cnt = 5; scr = 1; }
    else if (CW(0x2C) < 3) { tab = 0x4AEB; cnt = 5; scr = 1; }
    else { tab = 0x464E; cnt = 6; scr = 1; }
    DSB(0x55BE) = (u8)cnt;
    DSW(0x55C0) = tab;
    show_screen_pic(e_str_at(0x1036, scr), (void *)cb);
    char *s = e_str_at(tab, rand_n(cnt));
    ui_text_wrap(tctx(END_CTX0), s, DSW((u16)(0x103C + 2 * scr)), DSW((u16)(0x1042 + 2 * scr)), DSW((u16)(0x1048 + 2 * scr)), 8, 1);
    prompt_continue(DSW((u16)(0x104E + 2 * scr)), DSW((u16)(0x1054 + 2 * scr)));
}
