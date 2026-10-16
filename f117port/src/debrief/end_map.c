/* The map debrief of END (spec debrief.md section 6): the briefing-style map with the sites, the route replay event by event with the clock and the running
 * score, the event pictures and texts. References: port/notes/s39/s39_ref_ui.py (replay_step, event_icon, event_text, status, site_marks, hover). */
#include <stdio.h>
#include <string.h>

#include "debrief/debrief.h"

#define SF(buf, off, ...) snprintf((buf), sizeof(buf), e_str(off), __VA_ARGS__)

/* 0000:0E82 icon_erase: the background saved under the event picture back to page 1 and the screen */
void icon_erase(void)
{
    blit_rect(2, 0, 160, 1, (s16)DSW(0x55AE), (s16)DSW(0x55B0), 48, 40);
    blit_rect(2, 0, 160, 0, (s16)DSW(0x55AE), (s16)DSW(0x55B0), 48, 40);
}

/* 0000:0880 event_icon_draw: the 48 x 40 picture of breficon.pic (page 2) for the event, on the far side of the event point */
void event_icon_draw(void *r, int idx)
{
    const u8 *rec = r;
    u8 r4 = rec[4];
    int t = r4 & 0x3F, icon = 0;
    switch (t) {
    case 1: icon = (DSB((u16)(E_SITES + 16 * (rec[5] & 0x7F) + 8)) & 8) ? 15 : 0; break;
    case 2: case 12: icon = 2; break;
    case 3: icon = 1; break;
    case 4: icon = 6; break;
    case 5: icon = 3; break;
    case 6: icon = 5; break;
    case 7: icon = 4; break;
    case 8:
        if (idx == 0) icon = 8;
        else if (CW(0x26) == 3) icon = 7;
        else if (CW(0x26) == 1) icon = 14;
        else if (D_LANDCLS == 0) icon = 11;
        else icon = 13;
        break;
    case 10: icon = 10; break;
    case 11: {
        int j = (r4 & 0x80) ? 0 : 1;
        icon = DSW((u16)(0x1456 + 12 * DSW((u16)(0x71E8 + 0x12 * j + 8)) + 4)) == 3 ? 9 : 12;
        break;
    }
    default: break;                                              /* 9 is never drawn */
    }
    unsigned X = map_x(rec[2]), Y = map_y(rec[3]);
    if (X < 0x73) X += 10; else X -= 0x3A;
    if (Y < 0x59) Y += 10; else Y -= 0x28;
    X &= 0xFFFF;
    Y &= 0xFFFF;
    DSW(0x55AE) = (u16)X;
    DSW(0x55B0) = (u16)Y;
    blit_rect(1, (s16)X, (s16)Y, 2, 0, 160, 48, 40);
    blit_rect(2, (icon % 6) * 48, (icon / 6) * 40, 1, (s16)X, (s16)Y, 48, 40);
    blit_rect(1, (s16)X, (s16)Y, 0, (s16)X, (s16)Y, 48, 40);
}

/* 0000:0A98 event_text_draw: the text of one event in the 110 x 16 box at (140, 180) */
void event_text_draw(void *r, int idx)
{
    const u8 *rec = r;
    char buf[512];
    u8 r4 = rec[4];
    int t = r4 & 0x3F;
    int o = rec[5] & 0x7F;
    u16 site = (u16)(E_SITES + 16 * o);
    u16 unit = (u16)(E_UNITS_T + 32 * o);
    const char *noun = (r4 & 0x80) ? e_str(0xE0) : (r4 & 0x40) ? e_str(0xF2) : e_name(DSB((u16)(site + 0xE)));   /* (B) the index is not masked */
    u16 place = DSW(site);
    buf[0] = 0;
    switch (t) {
    case 1: case 12:
        if ((DSB((u16)(E_OBJCLS + (DSB((u16)(site + 0xE)) & 0x7F))) & 0xF) == 0xD) SF(buf, 0x106, e_name(place));
        else if (place != 0) SF(buf, 0x111, noun, e_name(place));
        else SF(buf, 0x124, noun);
        break;
    case 2:
        if (r4 & 0x80) snprintf(buf, sizeof buf, "%s", e_str(0x141));
        else if (r4 & 0x40) snprintf(buf, sizeof buf, "%s", e_str(0x15D));
        else if ((DSB((u16)(E_OBJCLS + o)) & 0xF) == 0xD) SF(buf, 0x17B, e_name(o));
        else SF(buf, 0x186, e_name(o));
        break;
    case 3: SF(buf, 0x131, e_str(unit), e_str((u16)(unit + 7))); break;
    case 4: {
        u16 wid = CW((u16)(0x38 + 2 * o));
        SF(buf, 0x242, e_str(DSW((u16)(0x1B98 + 2 * wid))));
        break;
    }
    case 5: SF(buf, 0x205, e_str(DSW((u16)(0x1B3E + 2 * o)))); break;
    case 6: SF(buf, 0x232, e_str(unit), e_str((u16)(unit + 7))); break;
    case 7:
        if (place != 0) SF(buf, 0x217, e_name(place), noun);
        else SF(buf, 0x223, noun);
        break;
    case 8:
        if (idx == 0) {
            u16 sl = DSW(0x71EC);
            u16 nm = DSW((u16)(E_SITES + 16 * sl));
            if (nm == 0) nm = DSB((u16)(E_SITES + 16 * sl + 0xE));
            SF(buf, 0x24E, e_name(nm));
        } else {
            snprintf(buf, sizeof buf, "%s", e_str(0x260));
            u16 c26 = CW(0x26);
            const char *tail = "";
            if (c26 == 1) tail = e_str(0x26E);
            else if (c26 == 2) tail = CW(0x28) == 0 ? (D_LANDCLS != 0 ? e_str(0x276) : e_str(0x283)) : e_str(0x28C);
            else if (c26 == 3) tail = e_str(0x29A);
            strncat(buf, tail, sizeof buf - strlen(buf) - 1);
        }
        break;
    case 10:
        if (place != 0) SF(buf, 0x1DF, noun, e_name(place));
        else SF(buf, 0x1F5, noun);
        break;
    case 11:
        snprintf(buf, sizeof buf, "%s", e_str(0x193));
        if (r4 & 0x80) strncat(buf, e_str(0x1A3), sizeof buf - strlen(buf) - 1);
        else if (r4 & 0x40) strncat(buf, e_str(0x1C0), sizeof buf - strlen(buf) - 1);
        break;
    default: break;
    }
    TextCtx *c1 = tctx(END_CTX1);
    c1->fg = 0xE2;
    gfx_fill_rect(c1, 140, 180, 238, 195);
    c1->font = 3;
    c1->fg = 0xF;
    c1->bg = 8;
    ui_text_wrap(c1, buf, 140, 180, 106, 8, 1);
    blit_rect(1, 140, 180, 0, 140, 180, 110, 16);
}

/* 0000:0DA3 status_draw: TIME, the running SCORE of the events 0..idx, "In Flight" */
void status_draw(int idx)
{
    char t[16], buf[64];
    TextCtx *c1 = tctx(END_CTX1);
    c1->fg = 0xE2;
    gfx_fill_rect(c1, 75, 180, 248, 195);
    c1->font = 3;
    c1->fg = 0xF;
    c1->bg = 8;
    fmt_time(DSW((u16)(E_LOG + 6 * idx)), t);
    SF(buf, 0x2A7, t);
    ui_text_shadow(c1, buf, 78, 180);
    SF(buf, 0x2B0, (s16)score((unsigned)idx));
    ui_text_shadow(c1, buf, 78, 188);
    if (idx != 0) text_shadow_centered(c1, e_str(0x2BB), 195, 184);
    blit_rect(1, 75, 180, 0, 75, 180, 174, 17);
}

/* 0000:0714 replay_step: draws the route segments up to the next stopping event, shows its picture and text; returns the next index (0: the log is
 * finished and the clean map is back) */
int replay_step(int idx)
{
    if (idx != 0) icon_erase();
    u16 rec = (u16)(E_LOG + 6 * idx);
    int t;
    for (;;) {
        t = DSB((u16)(rec + 4)) & 0x3F;
        bool stop = !(t == 9 || (EP(0x3A) == 2 && (t == 6 || t == 7)));
        if (idx == 0) {
            DSW(0x55AA) = (u16)map_x(DSB((u16)(rec + 2)));
            DSW(0x55AC) = (u16)map_y(DSB((u16)(rec + 3)));
        } else if (t != 0) {
            int x = (int)map_x(DSB((u16)(rec + 2))), y = (int)map_y(DSB((u16)(rec + 3)));
            gfx_line((int *)DSP(END_CTX0), (s16)DSW(0x55AA), (s16)DSW(0x55AC), x, y, 0);
            gfx_line((int *)DSP(END_CTX1), (s16)DSW(0x55AA), (s16)DSW(0x55AC), x, y, 0);
            DSW(0x55AA) = (u16)x;
            DSW(0x55AC) = (u16)y;
        }
        status_draw(idx);
        ui_wait_ticks(8);
        if (stop) break;
        rec = (u16)(rec + 6);
        idx++;
    }
    if (t != 0) {
        event_icon_draw(DSP(rec), idx);
        int old = idx++;
        event_text_draw(DSP(rec), old);
        return idx;
    }
    blit_rect(4, 0, 0, 0, 0, 0, 320, 169);
    blit_rect(4, 0, 0, 1, 0, 0, 320, 169);
    status_draw(0);
    return 0;
}

/* 0000:04D2 site_marks_draw: the site dots one by one (wait_input(8) between them until Space skips the animation) */
void site_marks_draw(char pick)
{
    u16 n = DSW(0x54B8);
    for (u16 k = 0; k < n; k++) {
        if (((u8)pick) == 0) {
            pick = (char)wait_input(8);
            end_snd(0x1C);
        }
        u16 q = (u16)(E_SITES + 16 * DSB((u16)(0x5440 + 2 * k)));
        int x = (s16)DSW((u16)(0x54BA + 2 * k)), y = (s16)DSW((u16)(0x5532 + 2 * k));
        TextCtx *c0 = tctx(END_CTX0), *c1 = tctx(END_CTX1);
        if (DSB((u16)(0x5441 + 2 * k)) == 0) {
            u16 seg = DSW((u16)(q + 6)) != 0 ? ((DSB((u16)(q + 8)) & 8) ? DSW(0x7226) : DSW(0x720C)) : DSW(0x5DA0);
            ui_sprite_draw(c1, x, y, seg);
            ui_sprite_draw(c0, x, y, seg);
        } else {
            ui_sprite_draw(c1, x - 1, y, DSW(0x720C));
            ui_sprite_draw(c1, x + 2, y, DSW(0x5DA0));
            ui_sprite_draw(c0, x - 1, y, DSW(0x720C));
            ui_sprite_draw(c0, x + 2, y, DSW(0x5DA0));
        }
    }
}

/* 0000:046E map_hover: the menu hook; the two panel buttons live in palette entries FFh and FEh, the one under the cursor is shown salmon */
void map_hover(void)
{
    int nw = hit_test(DSP(0x50B9), DSB(0x50B8));
    int prev = DSB(0x5B9E);
    if (prev != nw) {
        if (prev != 0) end_pal_queue_push(0x100 - prev, DSP(0x1324), 1);
        if (nw != 0) end_pal_queue_push(0x100 - nw, DSP(0x131E), 1);
    }
    DSB(0x5B9E) = (u8)nw;
}

/* 0000:02A4 map_screen_init */
void map_screen_init(void)
{
    DSB(0x5B9E) = 0;
    fade_out(1);
    load_pic(e_str(0xA7), 2);                                    /* breficon.pic: the event pictures */
    load_pic(e_str(0xB4), 3);                                    /* brefscrn.pic: the frame */
    load_pic(e_str_at(0xCE, (s16)EP(0x38)), 4);                  /* the theatre map */
    ui_load_file_near(e_str(0xC1), DSP(0x1F25));                 /* brefscrn.pal */
    blit_rect(3, 0, 0, 0, 0, 0, 320, 200);
    blit_rect(3, 0, 0, 1, 0, 0, 320, 200);
    fade_in(1);
    screen_on();
    ui_wait_ticks(60);
    place_icons();
    blit_rect(4, 0, 0, 1, 0, 0, 320, 169);
    blit_rect(4, 0, 0, 0, 0, 0, 320, 169);
    site_marks_draw(0);
    ui_sprite_draw(tctx(END_CTX0), 229, 50, DSW(0x5DB6));
    ui_sprite_draw(tctx(END_CTX1), 229, 50, DSW(0x5DB6));
    blit_rect(1, 0, 0, 4, 0, 0, 320, 169);
    DSB(0x5B9E) = 0;
    cursor_set_pos(272, 56);
}

/* 0000:03D9 map_screen: the menu around the replay; at the end the mission score goes into the pilot record */
void map_screen(void)
{
    map_screen_init();
    int idx = replay_step(0);
    bool done = false;
    do {
        int r = menu_loop(DSP(0x50B5));
        if (r == 1) idx = replay_step(idx);
        else if (r == 2 || r == 0x63) done = true;
    } while (!done);
    u16 sc = (u16)score(0x100);
    u8 *p = e_pilot();
    if (CW(0x30) == 0) {
        PW(p, 0x30) = sc;
        if (PW(p, 0x2E) < sc) PW(p, 0x2E) = sc;
        u32 lo = (u32)PW(p, 0x32) + sc;                          /* the career score: a 32-bit sum of 16-bit halves */
        PW(p, 0x32) = (u16)lo;
        PW(p, 0x34) = (u16)(PW(p, 0x34) + (lo >> 16));
    } else {
        PW(p, 0x30) = 0;
    }
}
