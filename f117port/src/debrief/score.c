/* The numbers of END (spec debrief.md sections 1.6, 4, 6.2, 6.10, 7.2, 7.3): the event-log score, the landing class, promotion, ribbons and medals,
 * the icon placement of the map and the clock. Translated from port/notes/s39/s39_ref.py (0 mismatches against the original under Unicorn);
 * all state is DGROUP at the original offsets, the mission blob (sites, grid, event log) is read there as the original does. */
#include <string.h>

#include "debrief/debrief.h"

static s32 w32(s64 v) { return (s32)(u32)(u64)v; }

/* the name pointer table entry (00C9 built it from the pool) */
char *e_name(int idx) { return (char *)DSP(DSW((u16)(E_NAMES + 2 * idx))); }

/* 0000:0ECD, 0EE0 */
unsigned map_x(unsigned v) { return (unsigned)(((v << 7) & 0xFFFF) / 146); }
unsigned map_y(unsigned v) { return (unsigned)(((v << 7) & 0xFFFF) / 195); }

/* 0000:00A1 */
int landing_class(void)
{
    u16 y = (u16)(CW(0x76) >> 11), x = (u16)(CW(0x74) >> 11);
    D_LANDCLS = DSB((u16)(E_GRID + ((y << 4) & 0xFFFF) + x)) & 3;
    return D_LANDCLS;
}

/* 0000:0EF3: "hh:mm:ss"; hours do not wrap, the tens digit of the hour is never advanced (quirk 4) */
char *fmt_time(unsigned ticks, char *buf)
{
    ticks = (ticks + (((DSW(0x71F0) + DSW(0x7202)) & 0xF) << 8)) & 0xFFFF;
    int night = CB(0x33);
    memcpy(buf, "00:00:00", 9);
    buf[0] = (char)(buf[0] + night);
    buf[1] = (char)(buf[1] + (ticks / 0x708) % 10);
    buf[1] = (char)(buf[1] + (night == 0 ? 6 : 8));
    if ((u8)buf[1] > '9') {
        buf[1] = (char)(buf[1] - 10);
        buf[0]++;
    }
    unsigned mi = (ticks / 30) % 60;
    buf[3] = (char)(buf[3] + mi / 10);
    buf[4] = (char)(buf[4] + mi % 10);
    unsigned se = ((ticks << 1) & 0xFFFF) % 60;
    buf[6] = (char)(buf[6] + se / 10);
    buf[7] = (char)(buf[7] + se % 10);
    return buf;
}

/* ------------------------------------------------------------------------------------------------------------ 3021 score */

static s16 tw(u16 base, u16 ws) { return (s16)DSW((u16)(base + ws)); }     /* signed word of a war-indexed table */

static void obj_primary(s64 *S, u16 ws)
{
    *S += tw(0xDEC, ws);
    D_PRIMARY = 1;
    if (D_SECONDARY) *S += (s16)DSW(0xE00) - (s16)DSW(0xDFE);
}

static void obj_secondary(s64 *S, int idx_words)
{
    *S += (s16)DSW((u16)(0xDFE + idx_words * 2));
    D_SECONDARY = 1;
}

static s64 mul8(s64 S, s16 m) { return w32(w32(S * m) / 8); }

int score(unsigned limit)
{
    static const u16 counters[] = { 0x720E, 0x6D89, 0x5B9A, 0x55E8, 0x55DB, 0x6984, 0x64C8, 0x6D88, 0x55DC };
    for (size_t k = 0; k < sizeof counters / sizeof counters[0]; k++) DSB(counters[k]) = 0;
    s64 S = 0;
    u16 ng = 0;
    int end_seen = 0;
    u16 war = EP(0x3A);
    u16 ws = (u16)(war * 2);
    for (unsigned i = 0;; i++) {
        if (i > (limit & 0xFFFF)) break;
        u16 rec = (u16)(E_LOG + 6 * i);
        u8 r4 = DSB((u16)(rec + 4));
        if (r4 == 0) break;
        u8 r5 = DSB((u16)(rec + 5));
        int arg = r5 >= 128 ? r5 - 256 : r5;
        u16 site = (u16)(E_SITES + (((arg & 0x7F) << 4) & 0xFFFF));
        u8 cls = DSB((u16)(E_OBJCLS + DSB((u16)(site + 0xE))));
        int t = r4 & 0x3F;
        u16 sa = (u16)(E_SITES + ((arg << 4) & 0xFFFF));            /* the unmasked site of events 1 and 12 */
        switch (t) {
        case 1: case 12: {
            if (DSB((u16)(sa + 9)) & 0x10) S -= tw(0xDF2, ws);
            if (r4 & 0x80) { obj_primary(&S, ws); DSB(0x6984)++; }
            else if (r4 & 0x40) { obj_secondary(&S, D_PRIMARY); DSB(0x6984)++; }
            else if (cls & 0x40) { S += tw(0xDCE, ws); DSB(0x55E8)++; }
            else {
                u16 fl = DSW((u16)(sa + 8));
                if (fl & 0x0500) { S += tw(0xDCE, ws); DSB(0x55E8)++; }
                else if ((fl & 0x1000) || war != 0) { S += tw(0xDF8, ws); DSB(0x6984)++; }
                else {
                    s16 v = tw(0xDE0, ws);
                    S += v;
                    if (v == 0) DSB(0x6D88)++;
                }
            }
            break;
        }
        case 2:
            if (r4 & 0x80) { S += tw(0xDEC, ws); D_PRIMARY = 1; DSB(0x6984)++; }
            else if (r4 & 0x40) { S += tw(0xDFE, ws); D_SECONDARY = 1; DSB(0x6984)++; }       /* war-indexed here, not [6D89] */
            else if (cls & 0x40) { S += tw(0xDCE, ws); DSB(0x55E8)++; }
            else {
                u8 tile = DSB((u16)(E_GRID + (DSB((u16)(rec + 3)) & 0xF0) + (DSB((u16)(rec + 2)) >> 4)));
                if (tile & 3) { S += tw(0xDCE, ws); DSB(0x55E8)++; }
                else if (cls & 0x80) { S += tw(0xDBC, ws); DSB(0x5B9A)++; }
                else {
                    S += (s16)DSW((u16)(0xE08 + (((war << 4) + (cls & 0xF)) * 2)));
                    if ((s16)DSW((u16)(0xDD4 + ws)) < 0) DSB(0x6D88)++;
                    else DSB(0x6984)++;
                }
            }
            break;
        case 3:
            if (arg & 0x80) S -= tw(0xDE6, ws);
            if (r4 & 0x80) { obj_primary(&S, ws); DSB(0x64C8)++; }
            else if (r4 & 0x40) { obj_secondary(&S, D_PRIMARY); DSB(0x64C8)++; }
            else {
                u16 u = (u16)(E_UNITS_T + ((arg & 0x7F) << 5));
                u16 radar = DSW((u16)(u + 0x18));
                if (radar == 0xFFFF) { S += tw(0xDC8, ws); DSB(0x55DB)++; }
                else if ((arg & 0x80) || war != 0) {
                    if (radar == 0) S += (s16)DSW((u16)(0xE02 + ws));
                    else if (DSB((u16)(E_SITES + ((radar << 4) & 0xFFFF) + 8)) & 4) S += tw(0xDB6, ws);   /* radar is used as a site index (quirk 2) */
                    else S += tw(0xDC2, ws);
                    DSB(0x64C8)++;
                } else { S += tw(0xDDA, ws); DSB(0x55DC)++; }
            }
            break;
        case 6: S += tw(0xDE6, ws); break;
        case 7: S += tw(0xDF2, ws); break;
        case 8:
            if (S < 0) S = 0;
            if (i != 0) end_seen = 1;
            break;
        case 9: {
            u8 cell = DSB((u16)(E_GRID + (DSB((u16)(rec + 3)) & 0xF0) + (DSB((u16)(rec + 2)) >> 4)));
            if ((cell & 3) == 0) {
                if (war == 0) { ng++; S += 1; }
                else if (war == 1) {
                    ng++;
                    if (EP(0x42) != 0 && (ng & 7) == 0) S += 1;
                    if ((ng & 3) == 0) S += 1;
                }
            }
            break;
        }
        case 10: case 11:
            if (r4 & 0x80) obj_primary(&S, ws);
            else if (r4 & 0x40) obj_secondary(&S, D_PRIMARY);
            break;
        default: break;                                              /* 0, 4, 5 and > 12 score nothing */
        }
        S = w32(S);
    }
    S = mul8(S, (s16)DSW((u16)(0xE68 + 2 * EP(0x38))));
    S = mul8(S, (s16)DSW((u16)(0xE72 + 2 * EP(0x3C))));
    S = mul8(S, (s16)DSW((u16)(0xE7A + 2 * EP(0x3E))));
    S = mul8(S, (s16)DSW((u16)(0xE82 + 2 * EP(0x40))));
    if (end_seen && CW(0x26) == 2) S = mul8(S, (s16)DSW((u16)(0xE88 + 2 * D_LANDCLS)));
    u16 r = CW(0x2E);
    if (r >= 0x1B58 && r < 0x2328) S = mul8(S, 9);
    else if (r >= 0x2328 && r < 0x2CEC) S = mul8(S, 10);
    else if (r >= 0x2CEC) S = mul8(S, 11);
    return (int)((u64)S & 0xFFFF);
}

/* --------------------------------------------------------------------------------------------- 2AFA promotion, 2476 awards */

void promotion_check(void)
{
    u8 *p = e_pilot();
    if (CW(0x30) != 0) return;
    for (int i = 5;; i--) {
        s16 rank = (s16)PW(p, 0x20);
        if (rank > i) return;
        u16 need = DSW((u16)(0x5B0 + 2 * i));
        s16 hi = (s16)PW(p, 0x34);
        if (hi < 0 || (hi == 0 && need > PW(p, 0x32))) continue;
        s32 sc = (s32)(PW(p, 0x32) | (u32)PW(p, 0x34) << 16);
        u16 miss = PW(p, 0x36);
        if (DSW((u16)(0x5C8 + 2 * i)) > miss) continue;
        s32 q = sc / (s32)miss;                    /* miss >= the mission table entry (>= 2) here */
        if (q < 0 || (q < 0x10000 && q < DSW((u16)(0x5BC + 2 * i)))) continue;
        if (PW(p, 0x20) == 5 && miss == 0x63) { D_PROMOTED = 1; PW(p, 0x20)++; return; }
        if (PW(p, 0x20) >= 5) return;
        if (D_CAPTURED == 1) { D_PROMO_WITHHELD = 1; return; }
        D_PROMOTED = 1;
        PW(p, 0x20)++;
        return;
    }
}

void award_check(void)
{
    u8 *p = e_pilot();
    bool ghost = D_CAPTURED != 0 || CW(0x30) != 0;
    if (PW(p, 0x44) == 0 && CW(0x30) != 0 && (D_PRIMARY != 0 || D_SECONDARY != 0)) { PW(p, 0x44) = 1; D_CRR = 1; }
    if (CW(0x30) == 0) {
        u16 n = PW(p, 0x36);
        if (PW(p, 0x46) == 0 && n >= 5 && n < 10) { PW(p, 0x46) = 1; D_RIBBON = 1; }
        if (PW(p, 0x48) == 0 && n >= 10) { PW(p, 0x46) = 0; PW(p, 0x48) = 1; D_RIBBON = 2; }
        if (PW(p, 0x4A) == 0 && n >= 30 && n < 60) { PW(p, 0x4A) = 1; D_RIBBON = 3; }
        if (PW(p, 0x4C) == 0 && n >= 60) { PW(p, 0x4A) = 0; PW(p, 0x4C) = 1; D_RIBBON = 4; }
    }
    if (PW(p, 0x22) == 0 && CW(0x36) >= 7 && (CW(0x34) & 8) && (CW(0x34) & 4)) { PW(p, 0x22) = 1; D_MEDAL = 1; }
    u16 last = PW(p, 0x30);
    if (PW(p, 0x2C) == 0 && last > 0x4B0) {
        if (ghost) D_MEDAL_GHOST = 6;
        else { D_MEDAL = 6; PW(p, 0x2C) = 1; }
        return;
    }
    static const struct { u16 off, mult; u8 code; } tab[] = { { 0x2A, 900, 5 }, { 0x28, 600, 4 }, { 0x26, 300, 3 } };
    for (int k = 0; k < 3; k++) {
        if ((u16)((PW(p, tab[k].off) + 1) * tab[k].mult) <= last) {
            if (ghost) D_MEDAL_GHOST = tab[k].code;
            else { D_MEDAL = tab[k].code; PW(p, tab[k].off)++; }
            return;
        }
    }
    if (PW(p, 0x24) < 9 && (u16)((PW(p, 0x24) + 1) * 100) <= last) {
        if (ghost) D_MEDAL_GHOST = 2;
        else { D_MEDAL = 2; PW(p, 0x24)++; }
    }
}

/* ------------------------------------------------------------------------------------------------------- 05CA place_icons */

static bool site_included(u16 q)
{
    if (DSW((u16)(q + 6)) == 0) {
        u16 fl = DSW((u16)(q + 8));
        if (!(fl & 1) && !((fl >> 8) & 2)) return false;
    }
    return !(DSB((u16)(q + 9)) & 8);
}

void place_icons(void)
{
    u16 n_sites = DSW(0x6446);
    DSW(0x54B8) = 0;
    u16 p = E_SITES;
    for (u16 i = 0; i < n_sites; i++, p = (u16)(p + 16)) {
        if (!site_included(p)) continue;
        if (DSW((u16)(p + 6)) != 0) DSB((u16)(p + 9)) &= 0xBF;
        u16 fl = DSW((u16)(p + 8));
        u16 j = n_sites;
        if (!(fl & 8) || ((fl >> 8) & 4)) {
            u16 q = E_SITES;
            for (j = 0; j < n_sites; j++, q = (u16)(q + 16))
                if (site_included(q) && DSW(q) == DSW(p) && j != i) break;
        }
        u16 x = 0, y = 0;
        if (j == n_sites) {
            x = (u16)(DSW((u16)(p + 2)) / 146);
            y = (u16)(DSW((u16)(p + 4)) / 195);
        } else if (j > i) {
            u16 q = (u16)(E_SITES + 16 * j);
            x = (u16)((u16)(DSW((u16)(q + 2)) + DSW((u16)(p + 2))) / 292);
            y = (u16)((u16)(DSW((u16)(q + 4)) + DSW((u16)(p + 4))) / 390);
        }
        if (j == n_sites || j > i) {
            u16 k = DSW(0x54B8);
            DSW((u16)(0x54BA + 2 * k)) = (u16)(x - 1);
            DSW((u16)(0x5532 + 2 * k)) = (u16)(y - 2);
            DSB((u16)(0x5440 + 2 * k)) = (u8)i;
            DSB((u16)(0x5441 + 2 * k)) = (u8)(n_sites > j ? j : 0);
            DSW(0x54B8) = (u16)(k + 1);
        }
    }
}
