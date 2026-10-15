/* The player flight model (owner flight, spec port/spec/flight.md section 2, simflow.md Part 2 sections 2-3): 1E9E sim_input_flight (one key per frame, the stick,
 * autopilot, buffet, crash sequence, engine and fuel, G load, speed / lift / turn, the incremental rotation matrices, stall, ground handling, position, touch-down,
 * the history ring and the cockpit sway), the orientation helpers 2C6E / 2C9E / 2E43 with E578, the joystick re-centre 10A8:0C87 and BBED.
 * Translated from the differential-tested Python reference port/notes/s36/r2_fm.py; the 16-bit wraps are the original's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flight/fl.h"

#define S(o) ((s16)DSW(o))
#define U(o) DSW(o)
#define SETW(o, v) (DSW(o) = (u16)(v))

void fl_strcpy(u16 dst, u16 src) { memmove(ds_str(dst), ds_str(src), strlen(ds_str(src)) + 1); }
void fl_strcat(u16 dst, u16 src) { fl_strcpy((u16)(dst + strlen(ds_str(dst))), src); }

/* ------------------------------------------------------------------------------------------------------- orientation helpers */

/* 0000:E578 mat_mul: C = A x B, 3x3 Q15 row major, mul15 products, 16-bit wrapping sums (render3d.md Part 1 E578) */
void mat_mul_0000_E578(u16 a, u16 b, u16 c)
{
    s16 r[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            s16 sum = 0;
            for (int k = 0; k < 3; k++) sum = (s16)(sum + fx_mul15(DSS(a + 2 * (3 * i + k)), DSS(b + 2 * (3 * k + j))));
            r[3 * i + j] = sum;
        }
    for (int i = 0; i < 9; i++) DSW(c + 2 * i) = (u16)r[i];
}

/* 0000:2C6E fm_rotate_apply: fm_mat = A x B through the scratch 2DDC; every 8th rotation sets the re-orthogonalisation flag [2DFB] */
void fm_rotate_apply(u16 a, u16 b)
{
    SETW(0x2DF8, U(0x2DF8) + 1);
    if ((U(0x2DF8) & 7) == 0) DSB(0x2DFB) = 1;
    mat_mul_0000_E578(a, b, 0x2DDC);
    memmove(DSP(0x2D94), DSP(0x2DDC), 18);
}

/* 0000:2C9E fm_mat_to_euler: yaw / pitch / roll from fm_mat (2D94); near the vertical and on wings-level with [2DFA] the matrix is rebuilt next */
static u16 quadrant(u16 a, s16 s, s16 c)
{
    if (s <= 0 && c < 0) return (u16)(a + 0x8000);
    if (s > 0 && c < 0) return (u16)(0x8000 - a);
    if (s < 0 && c > 0) return (u16)-a;
    return a;
}

void fm_mat_to_euler(void)
{
    s16 m[9];
    for (int k = 0; k < 9; k++) m[k] = DSS(0x2D94 + 2 * k);
    u16 pitch = fx_asin((s16)-m[5]), yaw, roll;
    s16 c = (s16)fx_cos16(pitch);
    if (c != 0) {
        yaw = abs(m[2]) < 0x5A81 ? fx_asin(fl_abs16((s16)fx_ratio_q15(m[2], c))) : fx_acos(fl_abs16((s16)fx_ratio_q15(m[8], c)));
        yaw = quadrant(yaw, m[2], m[8]);
        roll = abs(m[3]) < 0x5A81 ? fx_asin(fl_abs16((s16)fx_ratio_q15(m[3], c))) : fx_acos(fl_abs16((s16)fx_ratio_q15(m[4], c)));
        roll = quadrant(roll, m[3], m[4]);
    } else {
        roll = 0;
        yaw = quadrant(fx_asin(m[1]), m[3], m[4]);
    }
    SETW(0x2DF0, pitch);
    SETW(0x2DEE, yaw);
    SETW(0x2DF2, roll);
    s16 p = S(0x2DF0);
    if (p > 0x38E3 && p < 0x4001) DSB(0x2DFB) = 1;
    if (p > -0x4001 && p < (s16)0xC71D) DSB(0x2DFB) = 1;
    if (DSB(0x2DFA) != 0 && S(0x2DF2) == 0) DSB(0x2DFB) = 1;
}

/* 0000:2E43 fm_mat_from_euler: fm_mat from the Euler angles (E26B), clean */
void fm_mat_from_euler(void)
{
    sim_matrix_from_angles(0x2D94, U(0x2DEE), S(0x2DF0), U(0x2DF2));
    DSB(0x2DFB) = 0;
    SETW(0x2DF8, 0);
}

/* 0000:BBED fm_bomb_dive_angle: the free-fall bomb's minimum pitch (flight.md 2.3) */
int fm_bomb_dive_angle(void)
{
    s32 num = (s32)(s16)(0x4000 - fl_abs16(S(0x2DF0))) << 12;
    s32 den = (u16)(U(0x2DF4) + 0x1000);
    return (u16)(num / den - 0x4000);
}

/* 10A8:0C87 joy_recenter: raw read, then min = max = centre = raw for both axes (table DS:2CA2) */
void joy_recenter(void)
{
    joy_read_raw();
    for (int si = 0; si < 4; si += 2) {
        u16 v = U(0x2CCA + si);
        SETW(0x2CB2 + si, v);
        SETW(0x2CA2 + si, v);
        SETW(0x2CAA + si, v);
    }
}

/* -------------------------------------------------------------------------------------------------------------- 1E9E */

static void g_text(void)
{
    s16 g = S(0x98DC);
    char whole[8], frac[8];
    snprintf(whole, sizeof whole, "%d", g / 16);
    snprintf(frac, sizeof frac, "%d", (fl_abs16(g) & 0xE) >> 1);
    char out[24];
    snprintf(out, sizeof out, "%s.%sG", whole, frac);
    strcpy(ds_str(0x98D0), out);
    strcpy(ds_str(0x9876), frac);                            /* itoa writes its digits to 9876 (the reference keeps that side effect) */
}

static void rot_set(u16 m, int i0, int i1, int is, int ins, s16 ang)
{
    s16 c = (s16)fx_cos16(ang), s = (s16)fx_sin16(ang);
    SETW(m + 2 * i0, c);
    SETW(m + 2 * i1, c);
    SETW(m + 2 * is, s);
    SETW(m + 2 * ins, -s);
}

/* the keys of 1E9E (simflow.md Part 2 section 2.1) */
static void flight_keys(u16 key)
{
    switch (key) {
    case 0x011B:                                                          /* Esc: shortens the ejection sequence */
        if (S(0xC06E) != 0) SETW(0xDECA, 0);
        break;
    case 0x0B30:                                                          /* 0: wheel brakes; released at full throttle on a deck = catapult */
        DSB(0x9B04) ^= 8;
        hud_lamp_recolor(DSP(DSW(CTX_P0_PTR)), 0x23, DSB(0x9B04) & 8);
        if (!(DSB(0x9B04) & 8) && U(0xC0A2) != 0 && S(0x2DFE) == 0x64) {
            SETW(0xB052, 0x546);
            snd_cue(0x11, 2);
        }
        break;
    case 0x0C2D: SETW(0x2DFE, fx_clamp_wrap(S(0x2DFE) - 10, 0, 100)); hud_draw_throttle(); break;
    case 0x0C5F: SETW(0x2DFE, 0); snd_cue(0xC, 0); hud_draw_throttle(); break;
    case 0x0D2B: SETW(0x2DFE, 0x64); hud_draw_throttle(); break;
    case 0x0D3D: SETW(0x2DFE, fx_clamp_wrap(S(0x2DFE) + (S(0x2DFE) < 10 ? 5 : 10), 0, 100)); hud_draw_throttle(); break;
    case 0x1000: sim_quit_requester(); break;                            /* Alt-Q */
    case 0x1900: sim_pause_wait(); break;                                /* Alt-P */
    case 0x1F00:                                                          /* Alt-S: joystick re-centre, 40-frame debounce */
        if (U(0x2E02) == 0) { joy_recenter(); SETW(0x2E02, 0x28); }
        break;
    case 0x2E63:                                                          /* c: HUD style */
        DSB(0x294B) = DSB(0x294B) == 0;
        hud_layout_init_far();
        break;
    case 0x3000: sim_boss_screen(); hud_draw_throttle(); break;          /* Alt-B */
    }
}

/* 0000:1E9E sim_input_flight */
void sim_input_flight(void)
{
    if (S(0xC0AE) == 0) {                                                 /* first call */
        DSW(0xB052) = DSW(0x2DF6) = DSW(0x2DF4) = DSW(0xB170) = DSW(0x2DFE) = DSW(0x2DF2) = DSW(0x2DF0) = 0;
        u16 th = (u16)fl_pilot_w(0x38);
        SETW(0x2DEE, S(0x3D7C + 2 * th) > 0 ? 0 : 0x8000);
        if (DSB((u16)(0xB2AB + 16 * U(0xE2DC))) & 2) DSB(0x2DEF) = (u8)(DSB(0x2DEF) + 4);
        fm_mat_from_euler();
        hud_draw_throttle();
        SETW(0xC0AE, 1);
    }
    SETW(0xDE90, 0);
    if (dos_kbhit()) {
        SETW(0xDE90, bios_keyboard(0));
        while (dos_kbhit()) bios_keyboard(0);
    }
    flight_keys(U(0xDE90));
    if (U(0x2E02)) SETW(0x2E02, U(0x2E02) - 1);
    if (S(0x2DFE) != 0 && S(0xB170) == 0) snd_cue(0xB, 2);

    /* ---- stick (2.2) */
    if (comm_get()->input_devices & 1) joy_poll();
    else {
        int k = (u16)(U(0x43BA) + 1);
        DSB(0x2CD2) = (u8)((s16)((DSB(0x2CD6) - 0x80) * k) / 3 + 0x80);
        DSB(0x2CD3) = (u8)((s16)((DSB(0x2CD7) - 0x80) * k) / 3 + 0x80);
    }
    SETW(0xE446, (DSB(0x2CD3) >> 4) - 8);
    SETW(0xDE8A, (DSB(0x2CD2) >> 4) - 8);
    if (S(0xDE8A) < 0) SETW(0xDE8A, S(0xDE8A) + 1);
    if (S(0xE446) < 0) SETW(0xE446, S(0xE446) + 1);
    s16 r = S(0xDE8A);
    SETW(0xDE8A, (s16)((s16)-(fl_abs16(r) + 2) * r) * 2);
    s16 p = (s16)(6 * S(0xE446));
    if (p < 0) p = (s16)((p + 1) >> 1);
    SETW(0xE446, p);
    if (U(0x2DF4) == U(0xC0A2) && S(0xE446) < 0 && S(0x2DF0) <= 0) SETW(0xE446, 0);
    if (S(0xDE8A) != 0 || S(0xE446) != 0) SETW(0x3652, 0);
    hud_lamp_update(4, U(0x3652) != 0 ? 15 : 0);

    /* ---- autopilot (2.3) */
    if (U(0x3652) != 0) {
        s16 a = (s16)fx_clamp(S(0xC078) - S(0x2DEE), -0x1400, 0x1400);
        s16 bp28 = (s16)(a << 1);
        SETW(0xDE8A, -fx_clamp_wrap((s16)(bp28 - S(0x2DF2)) >> 6, -0x18, 0x18));
        s16 bp10 = (s16)fx_clamp((s16)((s16)((s16)(S(0x3652) - S(0x2DF4)) << 4) - S(0x98C6)), -0x1400, 0xC00);
        SETW(0xE446, fx_clamp_wrap((s16)(bp10 - S(0x2DF0)) >> 7, -8, 8));
    }

    /* ---- buffet, flare assist (2.4) */
    u16 cm = (u16)fl_pilot_w(0x40);
    s16 bp24 = cm != 0 ? (s16)(((s32)(s16)(500 - S(0x2DF4)) * S(0xB168)) >> 13) : 0;
    if (!(DSB(0x9B04) & 1)) bp24 = (s16)(bp24 + fx_clamp_wrap((s16)(S(0xB168) - 0xC8) >> 4, 0, 0x20));
    if (DSB(0x9B04) & 2) bp24 = (s16)(bp24 >> 1);
    if (DSB(0x3632) & 4) bp24 = (s16)(bp24 + fx_clamp_wrap((s16)(S(0xC5C8) << 2), 0, 0x20));
    if (bp24 > 0 && U(0xC0A2) < U(0x2DF4)) {
        SETW(0xDE8A, S(0xDE8A) + fl_rand_n(bp24) - (bp24 >> 1));
        SETW(0xE446, S(0xE446) + ((fl_rand_n(bp24) - (bp24 >> 1)) >> 1));
    }
    if ((DSB(0x9B04) & 1) && S(0xE446) <= 0 && U(0xB052) > U(0xB00A) && cm < 2 && fl_abs16(S(0x2DF2)) < 0x3000 && S(0x98EA) == 0) {
        s16 t = (s16)((s16)((s16)((s16)(S(0x98C6) - S(0x2DF0)) >> 3) - S(0x2DF4) + 0xC8) >> 3);
        if (t > 0) SETW(0xE446, fx_clamp_wrap(t, 0, 0x20));
    }

    /* ---- ejection sequence, throttle cap, engine, fuel (2.5) */
    s16 rate = fl_rate();
    if (S(0xC06E) != 0) {
        SETW(0xDE8A, 0x40);
        SETW(0xE446, fl_abs16(S(0x2DF2)) > 0x4000 ? 0x10 : 0xFFF8);
        int hi = 0x80 / rate, lo = -0x60 / rate;
        SETW(0xC06E, S(0xC06E) + 1);
        SETW(0xDECA, S(0xDECA) + fx_clamp_wrap((s16)(0x20 - S(0xC06E)), lo, hi));
        if (S(0xDECA) < 0) {
            SETW(0xDECA, 0);
            if ((U(0x98E2) & 7) == 0) mission_end(0);
        }
        if (U(0x2DF4) == 0 && S(0x3D68) == -1) {
            SETW(0xB052, 0); SETW(0x2DFE, 0); SETW(0xC0B0, 0); SETW(0x3D68, 0);
            SETW(0xC0A0, U(0xC0A4)); SETW(0xB2A4, U(0xC0A4)); SETW(0xC0AA, U(0xC0B2)); SETW(0xB2A6, U(0xC0B2));
            SETW(0x9D0C, 0xFFF0);
            snd_cue(5, 2);
        }
        if ((U(0xC06E) & 0xFF00) == 0 && (U(0xC06E) & 0xFC) == 0x10 && (DSB(0x3D60) & 3) == 1) {
            SETW(0x3D68, 0xFFFF);
            u16 ax = DSB(0x3D60) & 0xF;
            SETW(0x3A5A, ax);
            SETW(0xC0A0, U(0xC0A4)); SETW(0x39DA + 8 * ax, U(0xC0A4));
            SETW(0xC0AA, U(0xC0B2)); SETW(0x39DC + 8 * ax, U(0xC0B2));
            SETW(0xC0B0, U(0x2DF4)); SETW(0x39DE + 8 * ax, U(0x2DF4));
            SETW(0x9D0C, 0xFFF0);
            snd_cue(4, 2);
            SETW(0x2DF0, 0xC000);
            DSB(0x2DFB) = 1;
        }
    }
    if (DSB(0x3632) & 2) {
        s16 m = (s16)((s16)(0x19 - S(0xC5C8)) << 2);
        if (m < S(0x2DFE)) {
            SETW(0x2DFE, m);
            if (m < 0) SETW(0x2DFE, 0);
            hud_draw_throttle();
        }
    }
    s16 d = (s16)(S(0x2DFE) - S(0xB170));
    SETW(0xB170, S(0xB170) + d / 4 / rate);
    if (S(0xB170) < S(0x2DFE)) SETW(0xB170, S(0xB170) + 1);
    if (S(0x2DFE) < S(0xB170)) SETW(0xB170, S(0x2DFE));
    u16 per = (u16)(U(0x3660) << 1);
    if (per != 0 && U(0x3D60) % per == 0 && S(0x2DFE) != 0) {
        u16 cxx = (DSB(0x3632) & 0x20) ? U(0xC5C8) : 0;
        s16 t = S(0x2DFE);
        s16 ax = (s16)((s16)(t * t) / -1000);
        SETW(0x3634, S(0x3634) + (s16)(ax - cxx - 2));
    }
    if (S(0x3634) <= 0) { SETW(0xB170, 0); SETW(0x3634, 0); }

    /* ---- G load (2.6) */
    int idx = ((u16)fl_abs16(S(0x2DF2)) & 0x7F00) >> 8;
    SETW(0x98DC, DSB(0x2D14 + idx));
    if (U(0xC0A2) < U(0x2DF4)) SETW(0x98DC, S(0x98DC) + S(0xE446));
    if (S(0x98DC) > 0x80) {
        SETW(0x98DC, 0x80);
        SETW(0xE446, fx_clamp_wrap(0x80 - DSB(0x2D14 + idx), 0, S(0xE446)));
    }
    g_text();

    /* ---- speed, lift, turn (2.7) */
    s16 A = (s16)fx_sin_mul(U(0x2DF0), 0x78);
    s16 bp2c = (s16)((s32)(s16)(S(0xB170) - A) * 720 / 100);
    s32 ax32 = (s32)(u16)((U(0x2DF6) >> 6) + 0x400) * 0x78;
    SETW(0xE448, ax32 >> 10);
    s16 e448 = S(0xE448);
    s32 b1 = (u16)((U(0x2DF4) >> 7) + 0x400);
    s16 ax = (s16)(((s32)bp2c * b1) >> 10);
    s16 cx = (s16)(0x64 - (S(0x3634) >> 9));
    s16 q = (s16)((s32)ax * cx / 90);
    bp2c = (s16)(((s32)q * (s16)(0x80 - S(0x98DC))) >> 7);
    s16 t = (s16)fx_isqrt((s16)(S(0x98DC) << 2));
    s16 e = (s16)(((s32)e448 * t) >> 3);
    e = fl_abs16(e);
    SETW(0xE448, e);
    if (DSB(0x9B04) & 2) { SETW(0xE448, S(0xE448) - (e >> 2)); bp2c = (s16)(bp2c - (bp2c >> 4)); }
    if (!(DSB(0x9B04) & 1)) bp2c = (s16)(bp2c - (bp2c >> 3));
    SETW(0xB00A, S(0xE448) * 0x1B);
    s16 bp18 = (s16)(fx_clamp_wrap(bp2c, 0, 0x383) * 0x1B);
    s32 diff = (s32)bp18 - S(0xB052);
    SETW(0xB052, U(0xB052) + diff / 16 / rate);
    s16 bp16 = (s16)fx_clamp_wrap((s16)(bp18 - S(0xB052)) >> 2, 0, S(0xB00A));
    s32 den = (s32)fl_abs16(S(0xB052)) + 1;
    SETW(0xB758, (s32)(s16)(S(0xB00A) - bp16) * 0xC00 / den);
    if (U(0xB758) > 0x2000) SETW(0xB758, 0x2000);
    SETW(0x98C6, fx_cos_mul(U(0x2DF2), (s16)(U(0xB758) - 0x200)));
    if (DSB(0x9B04) & 8) {                                                /* wheel brakes */
        if (U(0x2DF4) == U(0xC0A2)) {
            u16 dec = (u16)((s16)((4 - cm) * 0xD8));
            SETW(0xB052, U(0xB052) - dec / U(0x3660));
            if (U(0xC0A2) != 0 && U(0xB052) < 0x1B0) SETW(0xB052, 0);
        } else {
            SETW(0xB052, U(0xB052) - (U(0xB052) >> 4) / U(0x3660));
        }
    }
    if (U(0xB052) > 0xAFC8) SETW(0xB052, 0);
    s16 bp22 = (s16)fx_cos_mul(U(0x2DF0), S(0xB052));
    SETW(0xB168, U(0xB052) / 27);
    sim_snd_cmd(0x17, U(0xB168), 0, 0);
    s16 D = (s16)(DSB(0xB053) + 0x20);
    s16 ls = (s16)fx_sin_mul(U(0x2DF2), (s16)(S(0x98DC) << 4));
    s16 bp14 = (s16)fx_cos_mul(U(0x2DF0), (s16)(((s32)ls << 7) / D));
    if (U(0x2DF4) == U(0xC0A2)) {                                         /* ground handling */
        bp14 = (s16)-(s16)(S(0xDE8A) << 6);
        SETW(0xDE8A, 0);
        if (S(0xB168) < S(0xE448)) SETW(0xE446, 0);
    }
    if (S(0x98E0) != 0) { SETW(0xE446, 0xFC00 - S(0x2DF0)); SETW(0xB052, 0); SETW(0x2DFE, 0); }

    /* ---- attitude (2.8) */
    s16 bp26 = (s16)(((s32)S(0xDE8A) << 7) / rate);
    if (bp26 != 0) { rot_set(0x2DCA, 4, 0, 1, 3, bp26); fm_rotate_apply(0x2D94, 0x2DCA); }
    s16 bp20 = (s16)((s16)(S(0xE446) << 7) / rate);
    if (bp20 != 0) { rot_set(0x2DB8, 4, 8, 7, 5, bp20); fm_rotate_apply(0x2D94, 0x2DB8); }
    s16 bp12 = (s16)(bp14 / rate);
    if (bp12 != 0) { rot_set(0x2DA6, 8, 0, 2, 6, bp12); fm_rotate_apply(0x2DA6, 0x2D94); }
    fm_mat_to_euler();
    if (U(0xB052) < U(0xB00A) && U(0xC0A2) < U(0x2DF4)) {                 /* stall mush */
        int n = (cm == 2 || S(0xC5C8) > 8) ? 1 : 2;
        DSB(0x2DFB) = 1;
        SETW(0x2DF0, U(0x2DF0) - ((u16)(U(0xB00A) - U(0xB052)) >> n));
        if (S(0x2DF0) < 0 || U(0x2DF4) < 0xC8) snd_cue(0xE, 1);
    }
    if (U(0x2DF4) == U(0xC0A2)) {
        if (S(0x2DF2) != 0) { SETW(0x2DF2, 0); DSB(0x2DFB) = 1; }
        s16 pp = S(0x2DF0);
        if (pp < 0 || (pp > 0 && S(0xB168) < S(0xE448))) {
            if (S(0x98E0) == 0) SETW(0x2DF0, 0);
            DSB(0x2DFB) = 1;
        }
    }
    SETW(0x98E0, 0);
    if (S(0xE2FE) != 0) {
        s16 si = fl_abs16(S(0x2DF2));
        si = (s16)((si + (si < 0 ? 1 : 0)) >> 1);
        DSB(0xE542) = ((int)fl_abs16(S(0x2DF0)) - si) > 0x1000 ? 1 : 0;
    } else {
        DSB(0xE542) = 1;
    }
    if (DSB(0x2DFB)) fm_mat_from_euler();

    /* ---- position, altitude, touch-down (2.9) */
    s16 bp0c = S(0x2DF6);
    s16 vs = (s16)fx_mul15(U(0xB052) >> 4, fx_sin16(S(0x2DF0) - S(0x98C6)));
    SETW(0xE55A, vs);
    SETW(0x2DF6, U(0x2DF6) + vs / rate);
    s16 v1 = (s16)fx_mul15(bp22, fx_sin16(U(0x2DEE)));
    DSL(0xBA10) = (u32)((s32)DSL(0xBA10) + v1 / 16 / rate);
    s16 v2 = (s16)fx_mul15(bp22, fx_cos16(U(0x2DEE)));
    DSL(0xC02A) = (u32)((s32)DSL(0xC02A) + v2 / 16 / rate);
    if (U(0x2DF6) > 0xF230 || U(0xC0A2) > U(0x2DF6)) SETW(0x2DF6, U(0xC0A2));
    if (U(0x2DF6) > 0xEA60) SETW(0x2DF6, 0xEA60);
    u16 alt = U(0x2DF6);
    SETW(0x2DF4, alt < 0x2000 ? alt : alt < 0x4000 ? ((alt - 0x2000) >> 1) + 0x2000 : ((alt - 0x4000) >> 2) + 0x3000);
    if (U(0x2DF4) == U(0xC0A2)) {
        if (bp0c > S(0xC0A2) && S(0x3D74) != 0) {
            if (S(0xC06E) == 0) snd_cue(0xA, 2);
            u16 lim = (DSB((u16)(0xB2AB + (u16)(U(0xDFE0) << 4))) & 2) ? 0x100 : 0x80;
            u16 sink = (u16)-(u16)(cm * U(0xE55A));
            bool crash = lim < sink;
            if (!crash && cm != 0)
                crash = (DSB(0x9B04) & 1) || ((u16)(((0x18 / cm) & 0xFF) << 8) < (u16)fl_abs16(S(0x2DF2)));
            if (crash) { snd_cue(4, 2); frame_delay(0x3C); mission_end(5); }
        }
        SETW(0xE55A, 0);
    }
    u16 h = (u16)(0xB056 + 16 * (DSB(0x3D60) & 0xF));
    DSL(h) = DSL(0xBA10);
    DSL(h + 4) = DSL(0xC02A);
    SETW(h + 8, U(0x2DF4));
    SETW(h + 10, U(0x2DEE));
    SETW(h + 12, U(0x2DF0));
    SETW(h + 14, U(0x2DF2));
    if (S(0xE2FE) == 1) {                                                 /* the cockpit sway of the air-to-air mode */
        s16 n;
        if (S(0x3D64) >= 0) {
            u16 o = (u16)(S(0x3D64) * 0x24);
            s16 dsx = (s16)fx_dist((s16)(S(0xC0A4) - DSS((u16)(0xC140 + o))), (s16)(S(0xC0B2) - DSS((u16)(0xC142 + o))));
            n = (s16)fx_clamp_wrap((s16)(dsx * S(0x3660)) >> 7, 0, 0xC);
        } else {
            n = (s16)(S(0x3660) - 1);
        }
        u16 rec = (u16)(0xB056 + 16 * ((DSB(0x3D60) - n) & 0xF));
        s16 dy = (s16)(S(0x2DEE) - DSS(rec + 10)), dp = (s16)(S(0x2DF0) - DSS(rec + 12));
        SETW(0xE546, fx_sin_mul(U(0x2DF2), dp >> 2) + fx_cos_mul(U(0x2DF2), (s16)-dy >> 2));
        SETW(0xE550, fx_sin_mul(U(0x2DF2), dy >> 2) + fx_cos_mul(U(0x2DF2), dp >> 1));
    }
}
