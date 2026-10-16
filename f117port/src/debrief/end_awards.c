/* The awards screen of END (spec debrief.md section 7): promotion, medals and ribbons with their pictures and texts. The numbers (2AFA promotion_check,
 * 2476 award_check) are in score.c. Reference: port/notes/s39/s39_ref_ui.py (awards_screen, promo_text, medal_text, rank_picture, ribbons). */
#include <stdio.h>
#include <string.h>

#include "debrief/debrief.h"

#define SF(buf, off, ...) snprintf((buf), sizeof(buf), e_str(off), __VA_ARGS__)

/* 0000:23D6 ribbon_draw: a ribbon bar 11 x 2 from the strip of medals.pic (page 3); rows of three, the last row centred, stacked upwards from y = 149 */
void ribbon_draw(int id, int j, int n)
{
    int q = n / 3;
    if (n % 3 == 0) q--;
    int base;
    if (3 * q > j) base = 130;
    else base = n % 3 == 0 ? 130 : n % 3 == 1 ? 141 : 136;
    int dy = 149 + 2 * (j / 3 - q);
    int dx = base + (j % 3) * 11;
    blit_rect(3, 260, 2 * id, 1, dx, dy, 11, 2);
}

/* 0000:22EB ribbons_draw: up to 12 ribbon ids: the pilot's medals (+22..+2C), the ribbons (+44..+4C), then the three always shown */
void ribbons_draw(void)
{
    int ids[16], n = 0, k = 0;
    for (int j = 0; j < 6; j++, k++)
        if (EP((u16)(0x22 + 2 * j)) != 0) ids[n++] = k;
    for (int j = 0; j < 5; j++, k++)
        if (EP((u16)(0x44 + 2 * j)) != 0) ids[n++] = k;
    for (int j = 0; j < 3; j++) {
        if (n >= 12) break;
        ids[n++] = k++;
    }
    for (int j = 0; j < n; j++) ribbon_draw(ids[j], j, n);
}

/* 0000:2200 the callback of the awards picture: the screen into pages 1 and 4, the sheet of medals.pic on page 3, the ribbon strip */
static void awards_cb(void)
{
    blit_rect(0, 0, 0, 1, 0, 0, 320, 200);
    blit_rect(0, 0, 0, 4, 0, 0, 320, 200);
    load_pic(e_str(0x5A4), 3);                                   /* medals.pic */
    ribbons_draw();
    blit_rect(1, 0, 0, 0, 0, 0, 320, 200);
}

/* 0000:26A9 show_award_text */
void show_award_text(char *s)
{
    blit_rect(1, 152, 19, 0, 152, 19, 158, 52);
    tctx(END_CTX0)->font = 4;
    ui_text_wrap(tctx(END_CTX0), s, 152, 19, 158, 8, 1);
    prompt_continue(231, 63);
}

/* 0000:28ED rank_picture: the shoulder boards of the new rank on the right panel */
void rank_picture(void)
{
    blit_rect(3, 180, 0, 1, 219, 79, 80, 112);
    int r = (s16)EP(0x20);
    if (r == 1 || r == 2 || r == 3 || r == 4 || r == 6) {
        int sy = (r - 1) * 29;
        blit_rect(3, 286, sy, 1, 243, 98, 34, 29);
        blit_rect(3, 286, sy, 1, 243, 141, 34, 29);
    } else if (r == 5) {
        blit_rect(3, 281, 116, 1, 241, 98, 39, 29);
        blit_rect(3, 242, 116, 1, 241, 141, 39, 29);
    }
    blit_rect(1, 219, 79, 0, 219, 79, 80, 112);
}

/* 0000:2A01 promo_text. (B) a Purple Heart earned together with a promotion is stored in the record but its line is suppressed. */
void promo_text(void)
{
    char buf[512];
    bool shown = false;
    char *name = e_str(0x7228);
    if (D_PROMOTED) {
        const char *pre = CW(0x28) == 0 ? e_str(0x703) : e_str(0x704);
        const char *rk = e_str_at(0x5F2, (s16)EP(0x20));
        snprintf(buf, sizeof buf, e_str(DSW(0x4F0F)), name, pre, rk);
        rank_picture();
        shown = true;
    } else if (D_PROMO_WITHHELD) {
        snprintf(buf, sizeof buf, e_str(DSW(0x4F0D)), name, e_str_at(0x5F4, (s16)EP(0x20)));
        shown = true;
    } else if (!(D_MEDAL || D_CRR || D_RIBBON || D_MEDAL_GHOST)) {
        SF(buf, 0x712, name);                                    /* "%s remained at his present rank." */
        shown = true;
    }
    if (shown) show_award_text(buf);
    if (EP(0x36) == 0x63 && CW(0x28) == 0) show_award_text(e_str(DSW(0x4F11)));
    if (D_PROMOTED && D_MEDAL == 1) D_MEDAL = 0;
}

/* 0000:2703 medal_text */
void medal_text(void)
{
    char buf[512];
    char *name = e_str(0x7228);
    blit_rect(4, 219, 79, 0, 219, 79, 80, 112);
    blit_rect(4, 219, 79, 1, 219, 79, 80, 112);
    int md = D_MEDAL;
    if (md) {
        const char *pre = CW(0x28) != 0 ? e_str(0x61F) : e_str(0x62D);     /* "posthumously " / "" */
        SF(buf, 0x62E, name, pre, e_str_at(0x5D2, md));
        blit_rect(3, 180, 0, 1, 219, 79, 80, 112);
        blit_rect(3, ((md - 1) % 3) * 60, ((md - 1) / 3) * 100, 1, 231, 87, 60, 96);
        blit_rect(1, 219, 79, 0, 219, 79, 80, 112);
        show_award_text(buf);
    }
    if (D_CRR) {
        SF(buf, 0x647, name);
        show_award_text(buf);
    }
    if (D_RIBBON) {
        snprintf(buf, sizeof buf, e_str((md || D_CRR) ? 0x6A4 : 0x6D6), name, e_str_at(0x5EA, D_RIBBON), (s16)EP(0x36));
        show_award_text(buf);
    }
    if (D_MEDAL_GHOST) {
        snprintf(buf, sizeof buf, e_str(CW(0x30) != 0 ? DSW(0x4F09) : DSW(0x4F0B)), e_str_at(0x5DE, D_MEDAL_GHOST));
        show_award_text(buf);
    }
}

/* 0000:2265 awards_screen */
int awards_screen(void)
{
    if (CW(0x30) != 0 && !D_PRIMARY && !D_SECONDARY) return 0;   /* training without an objective: no awards screen */
    D_RIBBON = D_CRR = D_MEDAL = D_PROMO_WITHHELD = D_MEDAL_GHOST = D_PROMOTED = 0;
    show_screen_pic("awards", (void *)awards_cb);
    TextCtx *c0 = tctx(END_CTX0);
    c0->fg = 0xFD;
    c0->bg = 0xFE;
    promotion_check();
    award_check();
    if (D_MEDAL || D_CRR || D_RIBBON || D_PROMOTED) end_snd(0x24);
    promo_text();
    medal_text();
    end_snd(0);
    return 0;
}
