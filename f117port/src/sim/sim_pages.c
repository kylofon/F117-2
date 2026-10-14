/* The text pages of the right MFD and the clock / site name helpers (spec simflow.md Part 7 sections 1 and 3, section 5 ILS): navigation, weapons, objectives,
 * the ILS needles, "HH:MM" strings and the site names. All strings are built in the shared scratch buffer DS:9876 (and DEAA for numbers) exactly as the original does. */
#include <stdio.h>
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"

#define SITES 0xB2A2
#define NAME_PTR(idx) ds_str(DSW((u16)(0xDEDC + 2 * (idx))))   /* DEDC: name pointer table */

static char *strbuf(void) { return ds_str(0x9876); }
static void ds_itoa(int v, u16 buf) { snprintf((char *)DSP(buf), 12, "%d", (s16)v); }
static u16 pilot_w(int off) { return *(u16 *)(far_mp(ds_far(0x98F4)) + off); }
static void strcat_ds(u16 off) { strcat_fn(strbuf(), ds_str(off)); }

/* ------------------------------------------------------------------------------------------------------------- strings */

/* 0000:9641 fmt_2digit: n % 60 as two digits appended to DS:9876 */
void fmt_2digit(int n)
{
    s16 v = (s16)((s16)n % 60);
    if (v < 10) strcat_ds(0x4113);
    ds_itoa(v, 0xDEAA);
    strcat_fn(strbuf(), ds_str(0xDEAA));
}

/* the "HH:" part of the two clocks: hours = t / 1800 as two digits, then +6 (day) or +18 (night): the first character += night, the second += 6 or 8 */
static void clock_hours(u16 t)
{
    fmt_2digit(t / 0x708);
    DSB(0x9876) = (u8)(DSB(0x9876) + DSB(0x3658));
    DSB(0x9877) = (u8)(DSB(0x9877) + (DSW(0x3658) < 1 ? 6 : 8));
    if ((s8)DSB(0x9877) > 0x39) {
        DSB(0x9877) = (u8)(DSB(0x9877) - 10);
        DSB(0x9876)++;
    }
}

/* 0000:955C fmt_clock_hhmm: t in 2-second units + the start offset [9870]: "HH:MM" */
void fmt_clock_hhmm(unsigned t)
{
    u16 tt = (u16)(t + DSW(0x9870));
    strcpy_fn(strbuf(), ds_str(0x410B));
    clock_hours(tt);
    strcat_ds(0x410C);
    fmt_2digit(tt / 0x1E);
}

/* 0000:95C4 fmt_clock_hhmmss: "HH:MM:SS" */
void fmt_clock_hhmmss(unsigned t)
{
    u16 tt = (u16)(t + DSW(0x9870));
    strcpy_fn(strbuf(), ds_str(0x410E));
    clock_hours(tt);
    strcat_ds(0x410F);
    fmt_2digit(tt / 0x1E);
    strcat_ds(0x4111);
    fmt_2digit((u16)(tt << 1));
}

/* 0000:4A73 site_name: the place name of a site (or its type's name), at most 18 characters and a '.' */
void site_name(int site)
{
    u16 s = (u16)(SITES + 16 * site);
    DSB(0x9876) = 0;
    u16 idx = DSW(s);
    if (strlen(NAME_PTR(idx)) == 0) idx = DSB(s + 0xE) & 0x7F;
    strcpy_fn(strbuf(), NAME_PTR(idx));
    if (strlen(strbuf()) > 0x12) { DSB(0x9888) = '.'; DSB(0x9889) = 0; }
}

/* 0000:49E7 msg_site_label: "Noun at Place" (up to 24 characters and a '.') */
void msg_site_label(int site)
{
    u16 s = (u16)(SITES + 16 * site);
    strcpy_fn(strbuf(), NAME_PTR(DSB(s + 0xE) & 0x7F));
    if (strlen(NAME_PTR(DSW(s))) != 0) {
        if (strlen(NAME_PTR(DSB(s + 0xE) & 0x7F)) != 0) strcat_ds(0x3DD4);
        strcat_fn(strbuf(), NAME_PTR(DSW(s)));
    }
    if (strlen(strbuf()) > 0x19) { DSB(0x988E) = '.'; DSB(0x988F) = 0; }
}

/* 0000:D7D0 nav_copy_xy: the x, y of site `src` into waypoint slot `dst` */
void nav_copy_xy(int dst, int src)
{
    DSW((u16)(0x2E6C + 4 * dst)) = DSW((u16)(0xB2A4 + 16 * src));
    DSW((u16)(0x2E6E + 4 * dst)) = DSW((u16)(0xB2A6 + 16 * src));
}

/* ----------------------------------------------------------------------------------------------------------------- pages */

/* 0000:9208 mfd_nav_page: right MFD page 14h: the clock, four waypoint legs with distance, minutes and arrival time, and the fuel bars */
void mfd_nav_page(void)
{
    s16 fuel[4] = { 0, 0, 0, 0 };
    int px = 0, py = 0, col;
    u16 t;
    int row;
    mfd_clear(2, DSW(0xB80C) ? 1 : 0x2A);
    t = DSW(0x98E2);
    mfd_text(2, 2, 0, ds_str(0x40EA), 15);
    fmt_clock_hhmmss(t);
    mfd_text(2, 8, 0, strbuf(), 15);
    row = 1;
    for (int i = 0; i < 4; i++) {
        s16 cur = (s16)DSW(0x2E7C);
        if (i > cur) {
            col = (i & 1) ? 3 : 11;
            px = (s16)DSW((u16)(0x2E68 + 4 * i));
            py = (s16)DSW((u16)(0x2E6A + 4 * i));
        } else {
            col = 0;
            px = (s16)DSW(0xC0A4);
            py = (s16)DSW(0xC0B2);
            if (i == cur) col = DSW(0xB80C) ? 15 : ((i & 1) ? 3 : 11);
        }
        if (DSW(0xB80C) == 0 && i == (s16)DSW(0x2E7E)) col = 15;
        int wx = (s16)DSW((u16)(0x2E6C + 4 * i)), wy = (s16)DSW((u16)(0x2E6E + 4 * i));
        strcpy_fn(strbuf(), ds_str(0x40EF));
        ds_itoa(i + 1, 0xDEAA); strcat_fn(strbuf(), ds_str(0xDEAA));
        strcat_ds(0x40FA);
        int dist = fx_dist(px - wx, py - wy);
        ds_itoa((s16)dist >> 6, 0xDEAA); strcat_fn(strbuf(), ds_str(0xDEAA));
        strcat_ds(0x40FC);
        mfd_text(2, 0, row++, strbuf(), col);
        u16 spd = DSW(0xB052) > 0x7D0 ? DSW(0xB052) : 0x2A30;
        u16 eta = (u16)((u16)dist / (spd >> 6));
        s16 burn = (DSW(0x3632) & 0x20) ? (s16)DSW(0xC5C8) : 0;
        s16 thr = (s16)DSW(0x2DFE);
        s16 q = (s16)((s16)(thr * thr) / 1000);
        fuel[i] = (s16)((s16)((s16)((s16)(q + burn) * (s16)eta)) * 5);
        strcpy_fn(strbuf(), ds_str(0x4100));
        ds_itoa((s16)eta >> 2, 0xDEAA); strcat_fn(strbuf(), ds_str(0xDEAA));
        strcat_ds(0x4101);
        mfd_text(2, 1, row, strbuf(), col);
        if (i >= (s16)DSW(0x2E7C)) t = (u16)(t + (u16)(eta << 3));
        fmt_clock_hhmm(t);
        mfd_text(2, 13, row++, strbuf(), col);
    }
    mfd_fuel_bar(10000, 0);
    s16 f = (s16)DSW(0x3634);
    for (int i = 0; i < 4; i++) {
        if (i < (s16)DSW(0x2E7C)) continue;
        col = i == (s16)DSW(0x2E7C) ? 15 : ((i & 1) ? 3 : 11);
        mfd_fuel_bar(f, col);
        f = (s16)(f - fuel[i]);
    }
    mfd_fuel_bar(f, 0x2F);
}

/* 0000:9678 mfd_weapons_page: right MFD page 15h: per store the quantity and the weapon name */
void mfd_weapons_page(void)
{
    int y = 0x70, col = 0x2F;
    int n = pilot_w(0x42) < 1 ? 4 : 2;
    blit_rect(2, 0xA9, 0x6E, 1, 0xA9, 0x6E, 0x4D, 0x3C);
    for (int i = 0; i < n; i++) {
        strcpy_fn(strbuf(), ds_str(0x4115));
        ds_itoa((s16)DSW((u16)(0x3640 + 4 * i)), 0xDEAA);
        strcat_fn(strbuf(), ds_str(0xDEAA));
        hud_text2(strbuf(), 0xAC, y + 6, col);
        strcpy_fn(strbuf(), ds_str(0x4117));
        strcat_fn(strbuf(), ds_str((u16)(0x366C + 0x1A * DSW((u16)(0x363E + 4 * i)))));
        hud_text2(strbuf(), 0xAC, y, col);
        y += 0xF;
    }
}

/* 0000:97A4 mfd_objectives_page: right MFD page 18h: the mission orders */
void mfd_objectives_page(void)
{
    static const u16 verb[8] = { 0x413D, 0x4172, 0x415F, 0x414E, 0x4180, 0x41A4, 0x4193, 0x41B7 };
    int row = 0;
    char buf[32];
    mfd_clear(2, 0);
    for (int k = 0; k < 2; k++) {
        u16 rec = (u16)(0xE2D8 + 0x12 * k);
        int color = k == 0 ? 0x11 : 0x29;
        if (DSW(0x9B04) & (0x4000 >> k)) {
            strcpy_fn(strbuf(), ds_str(k == 0 ? 0x4122 : 0x4118));
            strcat_ds(0x412A);
            mfd_text(2, 1, row++, strbuf(), 15);
            mfd_text(2, 1, row++, ds_str(0x4133), 15);
            continue;
        }
        u16 kind = DSW(rec);
        if ((u16)(kind - 1) <= 7) strcpy_fn(strbuf(), ds_str(verb[kind - 1]));
        mfd_text(2, 1, row++, strbuf(), color);
        u16 site = (u16)(SITES + 16 * DSW(rec + 2));
        strcpy_fn(strbuf(), NAME_PTR(DSB(site + 0xE) & 0x7F));
        if (strlen(NAME_PTR(DSW(site))) != 0) {
            strcat_ds(0x41CA);
            mfd_text(2, 1, row++, strbuf(), color);
            site_name((s16)DSW(rec + 2));
            mfd_text(2, 1, row++, strbuf(), color);
        } else {
            mfd_text(2, 1, row++, strbuf(), k == 0 ? 0x11 : 0x29);
        }
        if (DSW(rec + 6) & 2) {
            fmt_clock_hhmmss(DSW(0xDE88));
            strcpy_fn(buf, ds_str(0x41CF));
            strcat_fn(buf, strbuf());
            if ((s16)DSW(0xDE88) < (s16)DSW(0x98E2)) strcpy_fn(strbuf(), ds_str(0x41D7));   /* the original overwrites 9876, not buf: "Time expired" never shows */
            mfd_text(2, 1, row++, buf, 7);
        }
    }
    mfd_text(2, 1, row++, ds_str(0x41E4), 10);
    site_name((s16)DSW(0xE2EE));
    mfd_text(2, 1, row++, strbuf(), 10);
}

/* 0000:99A7 hud_ils: the F9 overlay: localiser (x 139..181) and glide slope (y 40..72) needles toward the landing site */
void hud_ils(void)
{
    if (DSW(0x365E) == 0 || DSB(0x294B)) return;
    u16 site = (u16)(SITES + 16 * DSW(0xDFE0));
    if (DSB(site + 9) & 8) return;
    s16 dx = (s16)(DSW(site + 2) - DSW(0xC0A4));
    s16 dy = (s16)(DSW(site + 4) - DSW(0xC0B2));
    s16 adx = (s16)(dx < 0 ? -dx : dx);
    u16 ang = fx_heading(dx, (s16)(lib_sign(dy) * adx - dy));
    if (dy < 0) dy = (s16)-dy;
    if (dy < 0x40 || dy > 0xA00) return;
    gfx_set_fg(DSB(0x2CA0));
    int x = fx_clamp_wrap((s8)(((u16)(ang - DSW(0x2DEE)) >> 8) & 0xFF) + 0x9F, 0x8B, 0xB5);
    DSW(0x49E0) = (u16)x;
    hud_line_full2(x, 0x27, x, 0x49);
    hud_text2(ds_str(0x41EE), x - 6, 0x21, DSB(0x2CA0));
    s16 d, cx;
    if (DSB(site + 9) & 2) { d = (s16)((dy - 0x18) >> 5); cx = (s16)(DSW(0x2DF4) - 0x80); }
    else                   { d = (s16)((dy - 0x38) >> 6); cx = (s16)DSW(0x2DF4); }
    cx = (s16)((u16)cx >> 7);
    cx = (s16)(cx - d);
    DSW(0x49E8) = (u16)cx;
    int y = fx_clamp_wrap(cx, -0x10, 0x10) + 0x38;
    DSW(0x49E8) = (u16)y;
    hud_line_full2(0x8C, y, 0xB4, y);
}
