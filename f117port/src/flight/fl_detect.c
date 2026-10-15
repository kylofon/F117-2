/* Detection (owner detect, spec port/spec/flight.md section 3, simflow.md Part 4 section 4 and Part 5 section 1): the radar formula 5460, the per-site scan
 * 516D (signal bars, "detected" / "Radar ID" messages, level build-up, SAM launches), the radar warning 5588, the alert network 559A and the site scheduler with
 * the stealth bars and the threat marker 4F2A. 5460 / 516D / 5588 / 559A are translated from port/notes/s38/s38_ref.py, 4F2A from the disassembly. */
#include "flight/fl.h"

#define S(o) ((s16)DSW(o))
#define U(o) DSW(o)
#define SETW(o, v) (DSW(o) = (u16)(v))

/* 0000:5460 det_radar_signal: the signal of radar type `rtype` at (x, y, z) on the player; *bearing / *dist are not written for type 0 / -1 (Q-A1) */
int det_radar_signal(int x, int y, int z, int rtype, int *bearing, int *dist)
{
    s16 rt = (s16)rtype;
    if (rt == 0 || rt == -1) return 0;
    s16 dx = (s16)(U(0xC0A4) - (u16)x), dy = (s16)(U(0xC0B2) - (u16)y);
    s16 D = (s16)((s16)fx_dist(dx, dy) >> 6);
    u16 R = (u16)(0x2E80 + 14 * rt);
    s16 maxr = S(R + 8);
    int cell = DSB((u16)(0xB174 + ((U(0xC0B2) >> 11) << 4) + (U(0xC0A4) >> 11))) & 0x0C;
    s16 k = (s16)(2 * S(0x3654) + S(R + 10) + 1);
    s32 num = (s32)(maxr - D) * (s32)(cell * k);
    s16 sig0 = (s16)(num / maxr);
    u16 brg = fx_heading(dx, (s16)-dy);
    int hb = (u16)fl_abs16((s16)(brg - U(0x2DEE))) >> 8;
    int a = hb & 0x80 ? hb - 256 : hb;
    if (a > 0x40) a = 0x80 - a;
    int f = a;
    if (DSB(R + 12) & 1) f = (u16)((0x60 - a) * (U(0xB052) >> 5)) >> 9;
    s16 sig = (s16)((s16)((s16)((s16)(f + 0x20) >> 1) * (sig0 >> 1)) >> 4);
    if (((u16)fl_abs16((s16)((u16)z - U(0x2DF4))) >> 10) > D) sig = 0;
    if ((s16)(sig + U(0x3D5C)) > 100) frame_rate_halve();
    if (fl_pilot_w(0x42) != 0) sig = (s16)(sig >> 1);
    *bearing = brg;
    *dist = D;
    return sig;
}

/* 0000:5588 det_radar_warning */
void det_radar_warning(void)
{
    SETW(0x3D92, 2 * fl_rate());
    snd_cue(8, 1);
}

/* 0000:559A det_alert_network: the whole network learns where the player (or the decoy) is; every radar's level gets a floor */
void det_alert_network(void)
{
    SETW(0x3D62, U(0xB286));
    if (U(0x39B2) != 0) { SETW(0xB2A0, U(0x39AA)); SETW(0xB75A, U(0x39AC)); }
    else { SETW(0xB2A0, U(0xC0A4)); SETW(0xB75A, U(0xC0B2)); }
    SETW(0xB76E, U(0x2DF4));
    SETW(0x98F0, U(0x2DEE));
    SETW(0xC0B6, 0xFF);
    s16 lo = (s16)((S(0x3656) + S(0x3654) - 1) << 4);
    for (int i = 0; i < S(0xE540); i++) {
        u16 r = (u16)(SITE_BASE + 16 * i);
        if (U(r + 6) != 0) SETW(r + 10, fx_clamp_wrap(S(r + 10), lo, 0xFF));
    }
}

/* 0000:516D det_site_radar_update(site) */
void det_site_radar_update(int site)
{
    static int brg_i, dist_i;                                             /* PORT: 5460 leaves them unwritten for type 0 / -1 (stack locals of the original) */
    u16 r = (u16)(SITE_BASE + 16 * site);
    s16 t = S(r + 6);
    s16 sig = (s16)det_radar_signal(U(r + 2), U(r + 4), 0, t, &brg_i, &dist_i);
    u16 brg = (u16)brg_i;
    s16 D = (s16)dist_i;
    SETW(0x3D6C, 0);
    s16 sg = S(0x3D5C);
    if (sig > 0) {
        int c = 0x2A;
        if (sg + sig > 50) c = 0x29;
        if (sg + sig > 100) c = 0x11;
        SETW(0x3D6C, c);
        hud_threat_bar(sig < 99 ? sig : 99, c, 1, 4);
        SETW(0xC070, (s32)(s16)(sg + sig) * D / 100);
        SETW(0x3D6A, U(0x3660));
        SETW(0xC07E, site);
        SETW(0xE54C, DSB((u16)(0x2E80 + 14 * t + 12)) & 1);
        if (U(r + 10) != 0) {
            s16 hb = fl_sx8(brg >> 8);
            SETW(0xC13A, hb - 0x20);
            SETW(0xC13C, hb + 0x20);
        }
        SETW(0x9872, c);
        if (!(DSB(r + 8) & 4)) {
            if (U(0xDEDA) == 0 && U(0x365E) != 0) {
                mfd_map_restore_bg();
                mfd_map_plot(U(r + 2), U(r + 4), 0, 1);
                mfd_map_save_bg();
                if (U(0xDFDC) == 0x14) mfd_map_route();
            }
            DSB(r + 8) |= 4;
            msg_site_label(site);
            fl_strcat(0x9876, 0x3DDA);
            msg_post(ds_str(0x9876));
        }
    }
    if ((s16)(sg + sig) > 100) {
        det_radar_warning();
        int inc = ((S(0x3656) + S(0x3654) + 1) << 4) >> ((DSB(0x9B04) & 0x10) ? 1 : 0);
        s16 lv = (s16)(U(r + 10) + inc);
        if (lv > 0xFF) lv = 0xFF;
        SETW(r + 10, lv);
        if (!(DSB(r + 9) & 1) && U(0x39B2) == 0 && lv > 0x7F) det_alert_network();
        lv = S(r + 10);
        if (S(0x3654) >= S(0xB054) && lv > 0xC0 && t != 0x15 && S(0x98F2) > 0x500) {
            SETW(0xDED6, U(0xDED6) + 1);
            if (!(DSB(r + 9) & 0x10) && lv >= 0xFA) {
                DSB(r + 9) |= 0x10;
                SETW(0xDED2, U(0xDED2) + 1);
                msg_site_label(site);
                fl_strcat(0x9876, 0x3DE4);
                msg_post(ds_str(0x9876));
                snd_cue(8, 1);
                mission_log_event(7, site);
            } else {                                                      /* SAM launch into slot site & 7 */
                u16 m = (u16)(PROJ_BASE + 0x1C * (site & 7)), w = (u16)(WPN_BASE + 0x12 * t);
                if (U(m + 0xE) == 0 && S(w + 8) > D) {
                    SETW(m, U(r + 2) + 8); SETW(m + 2, U(r + 4)); SETW(m + 4, 0); SETW(m + 6, 1);
                    SETW(m + 8, brg); SETW(m + 0xA, 0x4000);
                    SETW(m + 0xE, ((s32)(S(w + 8) * S(0x3660)) << 4) / S(m + 6));
                    SETW(m + 0x10, t); SETW(m + 0x16, site);
                    msg_site_label(site);
                    fl_strcat(0x9876, 0x3DF0);
                    fl_strcat(0x9876, w);
                    msg_post(ds_str(0x9876));
                    fl_comm_add(0x2C, 1);
                }
            }
        }
        DSB(r + 8) |= 0x10;
    } else {
        DSB(r + 8) &= (u8)~0x10;
        s16 lv = (s16)(U(r + 10) - 0x10);
        SETW(r + 10, lv < 0 ? 0 : lv);
    }
}

/* 0000:4F2A hud_threat_update: (A) the stealth bars from the signature, (B) the staggered site scheduler running 516D, (C) the threat marker and arrow */
void hud_threat_update(void)
{
    if ((S(0x3D6A) == 0 || U(0xDEC8) != U(0x3D5C)) && U(0x365E) != 0) {
        SETW(0xDEC8, U(0x3D5C));
        hud_threat_bar(fx_clamp_wrap(100 - S(0x3D5C), 0, 0x63), 1, 0, 10);
        hud_threat_bar((s16)-fx_clamp_wrap(S(0x3D5C), 0, 0x63), 0x2A, 0, 10);
        if (S(0x3D6A) == 0 && U(0xDEDA) == 0) {
            if (U(0xDFDC) == 0x14) mfd_nav_page();
            SETW(0xC13A, 0);
            SETW(0xC13C, 0x100);
        }
    }
    for (int i = 0; i < S(0x98FC); i++) {
        u16 r = (u16)(SITE_BASE + 16 * i);
        if (U(r + 6) == 0 || (DSB(r + 8) & 0x80)) { DSB(r + 8) &= (u8)~2; continue; }
        int c = 2 * S(0x3656) + (S(0xC600) >= 0x80 ? 3 : 1);
        int ph = (u16)-(s16)((S(0x3D60) >> 10) * i) & 7;
        if (c >= ph || U(r + 10) != 0 || (DSB(r + 9) & 1)) {
            SETW(r + 12, U(r + 12) - 1);
            if (S(r + 12) <= 0) SETW(r + 12, (s16)((s16)((DSB(0x3660)) << 8) / (s16)((S(r + 10) >> 2) + 0x10)) + i);
            if (S(r + 12) == 4 && S(0x3D6A) < 0) {
                det_site_radar_update(i);
                DSB(r + 8) |= 2;
            }
        } else {
            DSB(r + 8) &= (u8)~2;
        }
    }
    if (U(0xDEDA) == 0 && S(0x3D6A) > 0 && U(0x365E) != 0 && S(0xC070) > 1) {
        s16 str;
        if (U(0x98DE) != 0) {
            s16 rate = fl_rate();
            s32 prod = (s32)(s16)fx_clamp_wrap(rate - S(0x3D6A), 1, rate) * S(0xC070);
            str = (s16)((s16)(prod / rate) << 6);
        } else {
            str = (s16)(S(0xC070) << 6);
            SETW(0xC070, 0);
        }
        s16 x, y;
        if (S(0xC07E) >= 0) {
            u16 r = (u16)(0xB2A4 + 16 * U(0xC07E));
            x = S(r); y = S(r + 2);
        } else {
            u16 o = (u16)(S(0xC07E) * -0x24);
            x = S((u16)(0xC11C + o)); y = S((u16)(0xC11E + o));
        }
        mfd_map_plot(x, y, (DSB(0x3D6A) & 1) ? 0xF : 0, 1);
        mfd_map_arc(x, y, str, S(0x9872), S(0xE54C), S(0xC13A), S(0xC13C));
    }
    SETW(0x3D6A, U(0x3D6A) - 1);
}
