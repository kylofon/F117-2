/* The assembly HUD of the front cockpit (segment 10A8; spec simflow.md Part 8 "Part 1: HUD"): the colour and layout set-up, then the per-frame drawing of
 * the airspeed, altitude and heading tapes, the glass frame, the aiming reticle, the G text and the pitch ladder. The HUD draws on the draw page 1 with MGRAPHIC
 * slots (text, line, blit_key, hud_tick_strip); the bitmaps come from the cockpit picture (256PIT.PIC) held in page 2. */
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"

/* 10A8:0A22 hud_layout_init: the layout block for the HUD style [294B] (1 = compact, else wide); generated from the immediates of the original */
void hud_layout_init(void)
{
    if (DSB(0x294B) == 1) {
        DSW(0x291D) = 0x4E;
        DSB(0x291F) = 0x8;
        DSW(0x2920) = 0xA;
        DSB(0x2922) = 0x0;
        DSW(0x2923) = 0x66;
        DSW(0x2925) = 0xCC;
        DSB(0x291C) = 0x88;
        DSW(0x2927) = 0x12;
        DSW(0x2929) = 0xAF;
        DSW(0x292B) = 0x4;
        DSW(0x292D) = 0x2;
        DSW(0x292F) = 0xFFE9;
        DSW(0x2931) = 0xFFF8;
        DSW(0x2933) = 0x9;
        DSW(0x2935) = 0x17;
        DSW(0x2937) = 0x1A;
        DSB(0x2939) = 0x34;
        DSW(0x293A) = 0x1F;
        DSW(0x293C) = 0x20;
        DSW(0x293E) = 0x3C;
        DSW(0x2940) = 0x9F;
        DSW(0x2942) = 0x2380;
        DSW(0x2944) = 0x42;
        DSW(0x2946) = 0x3C;
        DSW(0x2948) = 0x6C;
        DSW(0x285A) = 0x22;
        DSW(0x285C) = 0x4A;
        DSW(0x2858) = 0x2;
        DSW(0x286E) = 0x2;
        DSW(0x2884) = 0x2;
        DSW(0x289A) = 0x2;
        DSW(0x286C) = 0x14;
        DSW(0x2874) = 0x82;
        DSW(0x2876) = 0xBC;
        DSW(0x28C4) = 0x36;
        DSW(0x28C6) = 0x4C;
        DSW(0x28CA) = 0x99;
        DSW(0x28CC) = 0x37;
        DSW(0x28CE) = 0xD;
        DSW(0x28D0) = 0x9;
        DSW(0x28AC) = 0x82;
        DSW(0x28AE) = 0x19;
        DSW(0x28B0) = 0x3B;
        DSW(0x28B2) = 0x2;
        DSW(0x2886) = 0x1C;
        DSW(0x288A) = 0x7E;
        DSW(0x288C) = 0xC3;
        DSW(0x2898) = 0x16;
    } else {
        DSW(0x291D) = 0x5A;
        DSB(0x291F) = 0x11;
        DSW(0x2920) = 0x14;
        DSB(0x2922) = 0x1;
        DSW(0x2923) = 0x31;
        DSW(0x2925) = 0xFF;
        DSB(0x291C) = 0x6D;
        DSW(0x2927) = 0x2D;
        DSW(0x2929) = 0xF8;
        DSW(0x292B) = 0xA;
        DSW(0x292D) = 0x5;
        DSW(0x292F) = 0xFFCE;
        DSW(0x2931) = 0xFFF4;
        DSW(0x2933) = 0xD;
        DSW(0x2935) = 0x32;
        DSW(0x2937) = 0x34;
        DSB(0x2939) = 0x68;
        DSW(0x293A) = 0x4F;
        DSW(0x293C) = 0x24;
        DSW(0x293E) = 0x34;
        DSW(0x2940) = 0x9F;
        DSW(0x2942) = (u16)get_view_off_b();
        DSW(0x2944) = 0xA0;
        DSW(0x2946) = 0x44;
        DSW(0x2948) = 0x3C;
        DSW(0x285A) = 0x16;
        DSW(0x285C) = 0x52;
        DSW(0x2858) = 0x0;
        DSW(0x286E) = 0x0;
        DSW(0x2884) = 0x0;
        DSW(0x289A) = 0x0;
        DSW(0x286C) = 0x6;
        DSW(0x2874) = 0x5A;
        DSW(0x2876) = 0xE6;
        DSW(0x28C4) = 0x45;
        DSW(0x28C6) = 0x49;
        DSW(0x28CA) = 0x93;
        DSW(0x28CC) = 0x2C;
        DSW(0x28CE) = 0x19;
        DSW(0x28D0) = 0xF;
        DSW(0x28AC) = 0x5A;
        DSW(0x28AE) = 0xC;
        DSW(0x28B0) = 0x8D;
        DSW(0x28B2) = 0x3;
        DSW(0x2886) = 0x14;
        DSW(0x288A) = 0x4E;
        DSW(0x288C) = 0xF1;
        DSW(0x2898) = 0x10;
    }
}

/* 10A8:09CE hud_color_init: the panel segment into the four blit contexts, the HUD colours by day (green) or night (white); falls through into hud_layout_init */
void hud_color_init(void)
{
    u16 seg = DSW(0xE558);
    DSW(0x28A4) = DSW(0x28C2) = DSW(0x28E0) = DSW(0x28FE) = seg;
    if (DSW(0x43B0)) {
        DSW(0x2850) = DSW(0x2866) = DSW(0x2892) = 0xA;
        DSB(0x28BD) = DSB(0x28DB) = DSB(0x2CA0) = 0xA;
        DSW(0x287C) = 0x2F;
        DSB(0x2CA1) = 0x2F;
    } else {
        DSW(0x2850) = DSW(0x2866) = DSW(0x2892) = 0xF;
        DSB(0x28BD) = DSB(0x28DB) = DSB(0x2CA0) = 0xF;
        DSW(0x287C) = 7;
        DSB(0x2CA1) = 7;
    }
    hud_layout_init();
}

/* ---------------------------------------------------------------------------------------------------------------------- hud_draw_front */

#define T0 0x284C                                        /* the text / blit contexts of the HUD (spec: T0..T3, B0..B3) */
#define T1 0x2862
#define T2 0x2878
#define T3 0x288E
#define B0 0x28A4
#define B1 0x28C2

/* the byte division `div r8`: the quotient must fit a byte (the original would raise #DE, which never happens with valid data) */
static u16 div8(u16 ax, u8 d, u8 *rem)
{
    if (d == 0) { *rem = 0; return 0; }
    *rem = (u8)(ax % d);
    return (u16)((ax / d) & 0xFF);
}

#define XP(o) ((u16)(0x2B12 + (o)))
#define YP(o) ((u16)(0x2B82 + (o)))

/* 10A8:000C hud_draw_front: the head-up display of the front view, drawn on the draw page with MGRAPHIC slots (spec simflow.md Part 8 "Part 1: HUD") */
void hud_draw_front(void)
{
    u8 ff = (u8)flip_flag();
    DSW(T0) = DSW(T1) = DSW(T2) = DSW(T3) = DSW(0x28C8) = DSW(0x28AA) = DSW(0x2904) = DSW(0x28E6) = ff;
    u8 level = DSB(0x294A), style = DSB(0x294B);
    u16 di;

    if (level == 2) goto frame;
    if (level == 0) {                                    /* ---- the airspeed tape */
        u8 r8;
        u16 q = div8(DSW(0xB168), 0x32, &r8);
        u8 dl = (u8)(q - 1);
        u16 px = div8((u16)(r8 << (DSB(0x2922) & 31)), 5, &r8);
        u8 y = (u8)(px + DSB(0x291D));
        DSB(0x2856) = y;
        DSB(0x2971) = dl;
        DSB(0x2952) = (u8)(y - DSB(0x291F));
        di = (u8)(dl << 1);
        DSB(0x2955) = 5;
        for (;;) {
            DSB(0x2856) = (u8)(DSB(0x2856) - DSB(0x2920));
            DSW(0x2854) = DSW(0x2923);
            if (di >= 0x28) {
                di = 0;
                if (--DSB(0x2955) == 0) break;
                DSW(0x2952) = (u16)(DSW(0x2952) - DSW(0x2920));
                continue;
            }
            DSW(0x296C) = DSW((u16)(0x297E + di));
            DSW(0x296E) = DSW((u16)(0x29CE + di));
            text_clip_y_r(DSP(T0), ds_str(0x296C));
            di += 2;
            if (--DSB(0x2955) == 0) break;
        }
        hud_tick_strip(0, DSW(0x2952), DSB(0x294B), DSB(0x2971));
    }
    /* ---- the reference marks of the tapes */
    color_set_r(DSB(0x2CA0));
    if (style != 0) {
        if (level == 0) { line(0x7A, 0x3C, 0x7C, 0x3C); line(0xC4, 0x3C, 0xC6, 0x3C); }
        line(0x9F, 0x1D, 0x9F, 0x1E);
    } else {
        if (level == 0) { line(0x49, 0x34, 0x4C, 0x34); line(0xF3, 0x34, 0xF6, 0x34); }
        line(0x9F, 0x10, 0x9F, 0x12);
    }
    if (level == 0) {                                    /* ---- the altitude tape */
        u16 q = DSW(0x2DF6) / 1000, r = DSW(0x2DF6) % 1000;
        DSW(0x294C) = r;
        u16 px = (u16)((u16)(r << (DSB(0x2922) & 31)) / 100);
        di = (u16)((q - 1) << 1);
        DSB(0x2955) = 5;
        if ((s16)di < 0) {                               /* below 1000 feet: hundreds labels */
            u8 f8;
            u16 h = div8(DSW(0x294C), 100, &f8);
            u8 dl = (u8)(h - 1);
            u16 pxl = div8((u16)(f8 << (DSB(0x2922) & 31)), 10, &f8);
            u8 y = (u8)(pxl + DSB(0x291D));
            DSB(0x2856) = y;
            DSB(0x2971) = dl;
            DSB(0x2952) = (u8)(y - DSB(0x291F));
            di = (u8)(dl << 1);
            for (;;) {
                DSB(0x2856) = (u8)(DSB(0x2856) - DSB(0x2920));
                DSW(0x2854) = DSW(0x2925);
                if (di >= 0x28) {
                    di = 0;
                    DSB(0x2955)--;                       /* no test here (the original falls through into 01CC again) */
                    DSW(0x2952) = (u16)(DSW(0x2952) - DSW(0x2920));
                    continue;
                }
                DSB(0x2976) = DSB((u16)(0x2A1F + di));
                text_clip_y_r(DSP(T0), ds_str(0x2976));
                di += 2;
                if (--DSB(0x2955) == 0) goto strip2;
                if (di < 0x14) continue;
                di = 2;
                break;
            }
        } else {                                         /* 1000 feet and above */
            DSW(0x2856) = (u16)(px + DSW(0x291D));
            DSB(0x2971) = 2;
            DSB(0x2952) = (u8)(DSB(0x2856) - DSB(0x291F));
        }
        for (;;) {                                       /* 0224: the thousands labels */
            DSB(0x2856) = (u8)(DSB(0x2856) - DSB(0x2920));
            DSW(0x2854) = DSW(0x2925);
            char *text;
            if (di == 0) text = ds_str(0x297A);
            else { DSW(0x2972) = DSW((u16)(0x2A1E + di)); text = ds_str(0x2972); }
            text_clip_y_r(DSP(T0), text);
            di += 2;
            if (--DSB(0x2955) == 0) break;
        }
    strip2:
        hud_tick_strip(2, DSW(0x2952), DSB(0x294B), DSB(0x2971));
    }
    /* ---- the heading tape (levels 0 and 1) */
    {
        u16 t = (u16)(DSW(0x2DEE) - 0x2000);
        u8 dl = (u8)(t >> 8);
        u16 u = (t >> 7) & 0x3F;
        u16 px = (u16)((u16)(u * DSB(0x2927)) >> 6);
        DSB(0x2954) = (u8)px;
        u16 x = (u8)(DSB(0x291C) - (u8)px);
        DSW(0x2950) = x;
        DSW((u16)(T1 + 8)) = x;
        u16 k8 = (u16)((dl >> 2) & 0x38);
        DSW(0x294E) = k8;
        text_clip_xl_r(DSP(T1), ds_str((u16)(0x2AAA + k8)));
        DSW(0x294E) = (u16)((DSW(0x294E) + 8) & 0x3F);
        x = (u16)(DSW(0x2950) + DSW(0x2927));
        DSW(0x2950) = DSW((u16)(T1 + 8)) = x;
        text_noclip_r(DSP(T1), ds_str((u16)(0x2AAA + DSW(0x294E))));
        DSW(0x294E) = (u16)((DSW(0x294E) + 8) & 0x3F);
        x = (u16)(DSW(0x2950) + DSW(0x2927));
        DSW(0x2950) = DSW((u16)(T1 + 8)) = x;
        text_noclip_r(DSP(T1), ds_str((u16)(0x2AAA + DSW(0x294E))));
        if (DSW(0x2950) < DSW(0x2929)) {
            DSW(0x294E) = (u16)((DSW(0x294E) + 8) & 0x3F);
            u16 bx = (u16)(0x2AAA + DSW(0x294E));
            DSW((u16)(bx + 4)) = DSW(bx);
            DSW((u16)(bx + 6)) = DSW((u16)(bx + 2));
            bx = (u16)(bx + 4);
            x = (u16)(DSW(0x2950) + DSW(0x2927));
            DSW(0x2950) = DSW((u16)(T1 + 8)) = x;
            text_clip_xr_r(DSP(T1), ds_str(bx));
            u8 rem;
            div8(DSB(0x2954), DSB(0x292B), &rem);
            s16 ph = rem;
            if (DSW(0x2DEE) & 0x2000) ph = (s16)(ph + (s16)DSW(0x292D));
            if (ph >= (s16)DSW(0x292B)) ph = (s16)(ph - (s16)DSW(0x292B));
            u16 d2 = (u16)(ph << 1);
            DSW(0x28A8) = DSW((u16)((style == 1 ? 0x2AFE : 0x2AEA) + d2));
            blit_key_r(DSP(B0));
        }
    }

frame:                                                   /* ---- the glass frame */
    color_set_r(0xF);
    if (style == 0) {
        line(0x2E, 0x5A, 0x2E, 0xE);
        line(0x112, 0x5A, 0x112, 0xE);
        color_set_r(8);
        line(0x2D, 0x5A, 0x2D, 0xE);
        line(0x113, 0x5A, 0x113, 0xE);
        line(0x113, 0xE, 0xE8, 2);
        line(0xE8, 2, 0x5A, 2);
        line(0x2D, 0xE, 0x59, 2);
    } else {
        line(0x62, 0x5A, 0x62, 0x1E);
        line(0xDD, 0x5A, 0xDD, 0x1E);
        color_set_r(8);
        line(0x61, 0x5A, 0x61, 0x1E);
        line(0xDE, 0x5A, 0xDE, 0x1E);
        line(0xDE, 0x1E, 0xD1, 0x11);
        line(0xD1, 0x11, 0x6E, 0x11);
        line(0x6E, 0x11, 0x61, 0x1E);
    }
    if (DSB(0x2956)) blit_key_r(DSP(B1));                /* the reticle */
    DSW((u16)(T3 + 8)) = DSW(0x2948);
    text_noclip_r(DSP(T3), ds_str(0x98D0));              /* the G text */

    /* ---- the pitch ladder: the rungs */
    {
        s16 P = (s16)DSW(0x2DF0);
        u16 A = (u16)((P < 0 ? -P : P)) >> 6;
        u32 prod = (u32)A * 0x168;
        u16 tq = (u16)(prod >> 8);
        u8 rem8;
        u8 chq = (u8)div8(tq, 0x28, &rem8);
        u16 f = (u16)((u16)(rem8 * DSB(0x2937)) / 0x28);
        u8 fl = (u8)f;
        if (P < 0) fl = (u8)(DSB(0x2937) - fl);
        u16 bx = (u8)(fl + DSB(0x2939));
        u8 ch = chq;
        if (P < 0) ch = (u8)(-ch - 1);
        s16 k0 = (s16)((s8)ch - 2);
        DSW(0x2C2C) = (u16)k0;
        u8 dl = (u8)k0;
        int cl = 5;
        di = 0;
        int si = 0;
        ch = 0;
        for (;;) {
            u16 L = DSW(0x292F), Mi = DSW(0x2931), Ni = DSW(0x2933), R = DSW(0x2935);
            int kind;
            if (dl == 9) kind = 4;
            else if ((s8)dl > 9) kind = 5;
            else if (dl == 0xF7) kind = 1;
            else if ((s8)dl < (s8)0xF7) kind = 6;
            else if (dl == 0) kind = 1;
            else if ((s8)dl > 0) kind = 2;
            else kind = 3;
            switch (kind) {
            case 1:                                      /* horizon shape (r == 0 and r == -9) */
                DSW(XP(di)) = L; DSW(XP(di + 2)) = Mi; DSW(XP(di + 4)) = Ni; DSW(XP(di + 6)) = R;
                DSW(YP(di)) = DSW(YP(di + 2)) = DSW(YP(di + 4)) = DSW(YP(di + 6)) = bx;
                DSB((u16)(0x2BF2 + si)) = 1; DSB((u16)(0x2BF3 + si)) = 1;
                DSW((u16)(0x2C0E + si)) = di;
                di += 6;
                DSW((u16)(0x2C0F + si)) = di;
                di += 2;
                ch = (u8)(ch + 2);
                break;
            case 2: case 5: {                            /* solid rungs 1..8 (legs down) and beyond the zenith (legs up) */
                u16 legs = kind == 2 ? (u16)(bx + 5) : (u16)(bx - 5);
                DSW(XP(di)) = L; DSW(XP(di + 2)) = L; DSW(XP(di + 4)) = R; DSW(XP(di + 6)) = R;
                DSW(YP(di + 2)) = bx; DSW(YP(di + 4)) = bx;
                DSW(YP(di)) = legs; DSW(YP(di + 6)) = legs;
                DSB((u16)(0x2BF2 + si)) = 3; DSB((u16)(0x2BF3 + si)) = 0;
                di += 2;
                DSW((u16)(0x2C0E + si)) = di;
                di += 2;
                DSW((u16)(0x2C0F + si)) = di;
                di += 4;
                ch = (u8)(ch + 3);
                break;
            }
            case 4:                                      /* the zenith: a plain line */
                DSW(XP(di)) = L; DSW(XP(di + 2)) = R;
                DSW(YP(di)) = DSW(YP(di + 2)) = bx;
                DSB((u16)(0x2BF2 + si)) = 1; DSB((u16)(0x2BF3 + si)) = 0;
                DSW((u16)(0x2C0E + si)) = di;
                di += 2;
                DSW((u16)(0x2C0F + si)) = di;
                di += 2;
                ch = (u8)(ch + 1);
                break;
            default: {                                   /* dashed rungs -8..-1 (legs up) and below the nadir (legs down) */
                u16 legs = kind == 6 ? (u16)(bx + 5) : (u16)(bx - 5);
                DSW(XP(di)) = L; DSW(XP(di + 2)) = L; DSW(XP(di + 4)) = Mi; DSW(XP(di + 6)) = Ni; DSW(XP(di + 8)) = R; DSW(XP(di + 10)) = R;
                DSW(YP(di + 2)) = bx; DSW(YP(di + 4)) = bx; DSW(YP(di + 6)) = bx; DSW(YP(di + 8)) = bx;
                DSW(YP(di)) = legs; DSW(YP(di + 10)) = legs;
                DSB((u16)(0x2BF2 + si)) = 2; DSB((u16)(0x2BF3 + si)) = 2;
                di += 2;
                DSW((u16)(0x2C0E + si)) = di;
                di += 6;
                DSW((u16)(0x2C0F + si)) = di;
                di += 4;
                ch = (u8)(ch + 4);
                break;
            }
            }
            si += 2;
            bx = (u16)(bx - DSW(0x2937));
            dl++;
            if (--cl == 0) break;
        }
        DSB(0x2C2B) = ch;
        di -= 2;
        /* the roll rotation of every point (last to first) */
        s16 c = (s16)fx_sin16_reg((u16)(0x4000 - DSW(0x2DF2)));
        s16 sn = (s16)fx_sin16_reg((u16)(-DSW(0x2DF2)));
        for (;; di -= 2) {
            s16 x = (s16)DSW(XP(di)), y = (s16)DSW(YP(di));
            s32 cx_ = (s32)c * x, cy_ = (s32)c * y, sy_ = (s32)sn * y, sx_ = (s32)sn * x;
            s16 nx = (s16)((s16)(((u32)cx_ << 1) >> 16) - (s16)(((u32)sy_ << 1) >> 16));
            s16 t1 = (s16)((s16)(((u32)(cy_ * 3)) >> 16) >> 1);
            s16 t2 = (s16)((s16)(((u32)(sx_ * 3)) >> 16) >> 1);
            DSW(XP(di)) = (u16)nx;
            DSW(YP(di)) = (u16)(t1 + t2);
            if ((s16)(di - 2) < 0) break;
        }
    }
    {
        u16 sv2293 = DSW(0x2293), sv2295 = DSW(0x2295);
        if (DSB(0xE542) != 0) {
            color_set_r(DSB(0x2CA1));
            offset_set_r(DSW(0x2942));
            DSW(0x2293) = DSW(0x2944);
            DSW(0x2295) = DSW(0x2946);
            di = 0;
            int si = 0;
            DSB(0x2C2A) = 0;
            do {
                do {
                    DSW(0x2299) = (u16)(DSW(XP(di)) + DSW(0x293A));
                    DSW(0x229D) = (u16)(DSW(YP(di)) + DSW(0x293C));
                    DSW(0x229B) = (u16)(DSW(XP(di + 2)) + DSW(0x293A));
                    DSW(0x229F) = (u16)(DSW(YP(di + 2)) + DSW(0x293C));
                    (void)line_clip_draw();
                    di += 2;
                    DSB(0x2C2A)++;
                } while (--DSB((u16)(0x2BF2 + si)) != 0);
                di += 2;
                si++;
                if (DSB((u16)(0x2BF2 + si)) == 0) si++;
            } while ((s8)DSB(0x2C2A) < (s8)DSB(0x2C2B));
            /* the rung labels (absolute screen coordinates; the text ignores the draw offset) */
            DSW(0x2C2C) = (u16)(DSW(0x2C2C) + 0xB);
            si = 0;
            DSW(0x2C2E) = 5;
            u16 a = (u16)(DSB(0x2DF3) >> 2);             /* roll >> 10 */
            u16 d = (u16)(a << 1);
            DSW(0x2C98) = (u16)(DSW((u16)(0x264C + d)) + 2);
            DSW(0x2C9A) = DSW((u16)(0x26CC + d));
            DSW(0x2C9C) = (u16)(DSW((u16)(0x274C + d)) + 2);
            DSW(0x2C9E) = DSW((u16)(0x27CC + d));
            u16 b = (u16)((u8)(DSB(0x2DF3) + 0x80) >> 2);
            d = (u16)(b << 1);
            DSW(0x2C90) = (u16)(DSW((u16)(0x264C + d)) - 1);
            DSW(0x2C92) = DSW((u16)(0x26CC + d));
            DSW(0x2C94) = (u16)(DSW((u16)(0x274C + d)) - 1);
            DSW(0x2C96) = DSW((u16)(0x27CC + d));
            for (;;) {
                u16 lab = (u16)(DSW(0x2C2C) << 2);
                DSW(0x2C8C) = DSW((u16)(0x2C30 + lab));
                DSW(0x2C8E) = DSW((u16)(0x2C32 + lab));
                u16 pt = DSW((u16)(0x2C0E + si)) & 0xFF;
                s16 xx = (s16)DSW(XP(pt)) + ((s16)lab < 0x2C ? (s16)DSW(0x2C94) : (s16)DSW(0x2C90));
                DSW((u16)(T2 + 8)) = (u16)(xx + (s16)DSW(0x2940));
                s16 yy = (s16)DSW(YP(pt)) + ((s16)pt < 0x2C ? (s16)DSW(0x2C96) : (s16)DSW(0x2C92));
                DSW((u16)(T2 + 0xA)) = (u16)(yy + (s16)DSW(0x293E));
                text_clip_all_r(DSP(T2), ds_str(0x2C8C));
                si++;
                lab = (u16)(DSW(0x2C2C) << 2);
                DSW(0x2C8C) = DSW((u16)(0x2C30 + lab));
                DSW(0x2C8E) = DSW((u16)(0x2C32 + lab));
                pt = DSW((u16)(0x2C0E + si)) & 0xFF;
                xx = (s16)DSW(XP(pt)) + ((s16)lab < 0x2C ? (s16)DSW(0x2C9C) : (s16)DSW(0x2C98));
                DSW((u16)(T2 + 8)) = (u16)(xx + (s16)DSW(0x2940));
                yy = (s16)DSW(YP(pt)) + ((s16)pt < 0x2C ? (s16)DSW(0x2C9E) : (s16)DSW(0x2C9A));
                DSW((u16)(T2 + 0xA)) = (u16)(yy + (s16)DSW(0x293E));
                text_clip_all_r(DSP(T2), ds_str(0x2C8C));
                si++;
                DSW(0x2C2C)++;
                if (--DSW(0x2C2E) == 0) break;
            }
        }
        DSW(0x2295) = sv2295;
        DSW(0x2293) = sv2293;
        offset_clear();
    }
}
#undef XP
#undef YP

/* 10A8:0004 / 0008 / 0000: the far thunks */
void hud_color_init_far(void) { hud_color_init(); }
void hud_layout_init_far(void) { hud_layout_init(); }
void hud_draw_far(void) { hud_draw_front(); }
