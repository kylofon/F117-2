/* The key command interpreter cba0 (spec simflow.md Part 10 section 1): every key of the cockpit that is not flight control (1E9E's own keys are the flight
 * model's), run once per frame from sim_key_and_signature with the pending key word [DE90] (scan << 8 | ascii). Part A the main key switch, part B the
 * training-mode keys, part C the navigation page keys, then the tail that runs on every call (weapon button, countermeasure modes, bay doors, landing gear). */
#include <stdio.h>
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"
#include "sim/sim_stubs.h"

static u16 pilot_w(int off) { return *(u16 *)(far_mp(ds_far(0x98F4)) + off); }
static void *ctx0(void) { return DSP(DSW(CTX_P0_PTR)); }
static void ds_itoa(int v, u16 buf) { snprintf((char *)DSP(buf), 12, "%d", (s16)v); }
static s16 abs16(s16 v) { return (s16)(v < 0 ? -v : v); }

/* sensor look direction (cef3 LOOK): azimuth of the TV camera, the right MFD shows the sensor view */
static void look(u16 az)
{
    if (DSB(0x9B05) & 2) return;
    DSW(0x9D0E) = 0;
    DSW(0x43B4) = 1;
    DSW(0xE556) = az;
    DSW(0x3D64) = DSW(0x3D66) = 0xFFFF;
    mfd_right_select(0x13);
}

/* the zoom of the last sensor object (keys { and }): a 3-bit field of the object's flag byte */
static void sensor_zoom(int inwards)
{
    if (DSW(0xDFDC) != 0x13) return;
    u16 tbl = DSW(0xE2FE) == 1 ? 0x9E0 : 0x960;
    u16 a = (u16)(tbl + DSW(0x4362));
    u8 v = DSB(a);
    if (inwards) { if ((v & 7) != 0) DSB(a) = (u8)((v & 0xF8) | ((v & 7) - 1)); }
    else         { if ((v & 7) < 7) DSB(a) = (u8)((v & 0xF8) | ((v & 7) + 1)); }
}

/* 0000:CBA0 input_command_key */
void input_command_key(unsigned key_)
{
    u16 key = (u16)key_;
    DSW(0x43C0) = 0;
    DSW(0x43BE) = 0;
    if (key == 0) goto tail;

    /* ---- part A: the main key switch */
    switch (key) {
    case 0x0231: wpn_release_countermeasure(1); break;                                      /* 1 flare */
    case 0x0332: wpn_release_countermeasure(2); break;                                      /* 2 chaff */
    case 0x0433: DSW(0x9B04) ^= 0x20; break;                                                /* 3 flare mode */
    case 0x0534: DSW(0x9B04) ^= 0x10; hud_lamp_update(5, (DSW(0x9B04) & 0x10) ? 0x0A : 0); break;   /* 4 chaff mode */
    case 0x0635: wpn_release_countermeasure(3); break;                                      /* 5 decoy */
    case 0x0736:                                                                            /* 6 landing gear lever */
        if (DSW(0xC0A2) != DSW(0x2DF4)) { DSW(0xA068) ^= 1; sim_snd_cmd(0x13, 0, 0, 0); }
        break;
    case 0x0837:                                                                            /* 7 altitude hold */
        if (DSW(0x3652) != 0) DSW(0x3652) = 0;
        else DSW(0x3652) = (s16)DSW(0x2DF4) > 500 ? DSW(0x2DF4) : 500;
        break;
    case 0x0938:                                                                            /* 8 bay doors */
        if (!(DSW(0x3632) & 0x10)) { DSW(0xE55E) ^= 1; sim_snd_cmd(0x1F, 0, 0, 0); }
        break;
    case 0x0A39: DSW(0x9B04) ^= 2; hud_lamp_recolor(ctx0(), 0x21, (DSW(0x9B04) & 2) != 0); break;   /* 9 */
    case 0x0E08: if (pilot_w(0x42) == 0) DSW(0x43BE) = 1; break;                             /* Backspace: gun */
    case 0x1C0D: DSW(0x43C0) = 1; break;                                                    /* Enter: release */
    case 0x1100: DSW(0x034A) ^= 1; break;                                                   /* Alt-W */
    case 0x1400:                                                                            /* Alt-T: training */
        DSB(0x9B05) ^= 0x10;
        if (DSB(0x9B05) & 0x10) comm_get()->nonscoring |= 1;
        break;
    case 0x1A7B: sensor_zoom(0); break;                                                     /* { */
    case 0x1B7D: sensor_zoom(1); break;                                                     /* } */
    case 0x1E00: DSW(0x43B6) ^= 1; break;                                                   /* Alt-A */
    case 0x2000:                                                                            /* Alt-D: detail level */
        if ((s16)--DSW(0x98DE) < 0) DSW(0x98DE) = get_const_3() == 3 ? 3 : 2;
        strcpy_fn(ds_str(0x9876), ds_str(0x437A));
        ds_itoa((s16)DSW(0x98DE), 0xDEAA);
        strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
        msg_post(ds_str(0x9876));
        view_range_setup();
        break;
    case 0x2100:                                                                            /* Alt-F: frame rate */
        strcpy_fn(ds_str(0x9876), ds_str(0x4399));
        ds_itoa((s16)DSW(0x3660), 0xDEAA);
        strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
        strcat_fn(ds_str(0x9876), ds_str(0x43A4));
        ds_itoa((s16)DSW(0x43B8), 0xDEAA);
        strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
        msg_post(ds_str(0x9876));
        break;
    case 0x2300: DSW(0x463E) ^= 1; break;                                                   /* Alt-H */
    case 0x2C5A:                                                                            /* Z: time acceleration */
        if (DSW(0x3D7A) == 1) { DSW(0x3D7A) = 2; DSW(0x3660) = (u16)((s16)DSW(0x3660) / 2); frame_params_update(); }
        break;
    case 0x2C7A: mfd_zoom_in(); break;                                                      /* z */
    case 0x2D58: frame_rate_halve(); break;                                                 /* X */
    case 0x2D78: mfd_zoom_out(); break;                                                     /* x */
    case 0x2E00:                                                                            /* Alt-C: tall 3D window */
        DSW(0x2DFC) ^= 1;
        if (DSW(0x2DFC) == 0) blit_rect(2, 0, 0x5B, DSB(0x4088) == 0, 0, 0x5B, 320, 16);
        break;
    case 0x2F00:                                                                            /* Alt-V: sounds */
        DSW(0x43C2) = (u16)((DSW(0x43C2) + 1) & 3);
        strcpy_fn(ds_str(0x9876), ds_str(0x43A6));
        ds_itoa(3 - (s16)DSW(0x43C2), 0xDEAA);
        strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
        msg_post(ds_str(0x9876));
        snd_engine_refresh();
        break;
    case 0x2F76: if (++DSB(0x294A) > 2) DSB(0x294A) = 0; break;                              /* v: HUD mode */
    case 0x3062: DSW(0x9D0E) = 0; DSW(0x3D66) |= 0x80; DSW(0x3D64) |= 0x80; break;           /* b */
    case 0x316E:                                                                            /* n */
        DSB(0x9B05) ^= 1;
        if (DSW(0xDFDC) == 0x13) look(0);
        break;
    case 0x326D: look(0xC000); break;                                                       /* m  left  */
    case 0x332C: look(0x4000); break;                                                       /* ,  right */
    case 0x342E: look(0x8000); break;                                                       /* .  rear  */
    case 0x352F: look(0); break;                                                            /* /  ahead */
    case 0x324D: DSW(0xC07A) = 0x42; break;                                                 /* M */
    case 0x333C: DSW(0xC07A) = 0x43; break;                                                 /* < */
    case 0x343E: DSW(0xC07A) = 0x41; break;                                                 /* > */
    case 0x353F: DSW(0xC07A) = 0x44; break;                                                 /* ? */
    case 0x3920:                                                                            /* Space: weapon select */
        if (pilot_w(0x42) != 0) DSW(0x364E) ^= 1;
        else DSW(0x364E) = (u16)((DSW(0x364E) + 1) & 3);
        if (DSB(0x9B05) & 2) mfd_tv_label();
        if (DSW(0xDFDC) == 0x15) mfd_weapons_page();
        break;
    case 0x3B00: DSW(0xC07A) = 0; break;                                                    /* F1 */
    case 0x3C00:                                                                            /* F2: master mode */
        if (DSW(0xE2FE) == 0) { DSW(0xE2FE) = 1; DSW(0x3D64) = DSW(0x3D66) = 0xFFFF; }
        else if (DSW(0xE2FE) == 1) { DSW(0xE2FE) = 2; DSW(0x3D66) = 0xFFFF; }
        else DSW(0xE2FE) = 0;
        break;
    case 0x3D00: mfd_left_select(DSW(0xDEDA) == 1 ? 0 : 1); break;                          /* F3 */
    case 0x3E00: DSW(0x43B0) ^= 1; cockpit_daynight_recolor(); break;                        /* F4 */
    case 0x3F00: mfd_right_select(0x15); break;                                             /* F5 */
    case 0x4000: DSW(0x43B2) ^= 1; hud_lamp_recolor(ctx0(), 0x19, DSW(0x43B2)); break;      /* F6 */
    case 0x4100: DSW(0xB80C) = 1; mfd_right_select(0x14); break;                            /* F7 */
    case 0x4200: DSW(0xB80C) = 0; DSW(0x2E7E) = DSW(0x2E7C); mfd_right_select(0x14); break; /* F8 */
    case 0x4300: DSB(0x9B05) ^= 4; break;                                                   /* F9 */
    case 0x4400: mfd_right_select(0x18); break;                                             /* F10 */
    case 0x5200:                                                                            /* Insert: keyboard sensitivity */
        DSW(0x43BA) = (u16)((s16)(DSW(0x43BA) + 1) > 2 ? 0 : DSW(0x43BA) + 1);
        strcpy_fn(ds_str(0x9876), ds_str(0x4388));
        ds_itoa((s16)DSW(0x43BA) + 1, 0xDEAA);
        strcat_fn(ds_str(0x9876), ds_str(0xDEAA));
        msg_post(ds_str(0x9876));
        break;
    case 0x5400: DSW(0xC07A) = 0x87; break;                                                 /* Shift-F1 .. F6 */
    case 0x5500: DSW(0xC07A) = 0x84; break;
    case 0x5600: DSW(0xC07A) = 0x85; break;
    case 0x5700: DSW(0xC07A) = 0x89; break;
    case 0x5800: DSW(0xC07A) = 0x88; break;
    case 0x5900: DSW(0xC07A) = 0x8B; break;
    case 0x5B00:                                                                            /* Shift-F8: the waypoints from the units */
        nav_copy_xy(0, (s16)DSW(0xE2DC));
        nav_copy_xy(1, (s16)DSW(0xE2DA));
        nav_copy_xy(2, (s16)DSW(0xE2EC));
        nav_copy_xy(3, (s16)DSW(0xE2EE));
        DSW(0x2E6C) = (u16)(((s16)DSW(0x2E70) + (s16)DSW(0x2E6C)) >> 1);
        DSW(0x2E6E) = (u16)(((s16)DSW(0x2E6E) + (s16)DSW(0x2E72)) >> 1);
        if (DSW(0xDFDC) == 0x14) mfd_nav_page();
        break;
    case 0x5D00:                                                                            /* Shift-F10: eject */
        if (DSW(0xC06E) == 0) {
            snd_cue(5, 2);
            snd_cue(0x14, 2);
            int lim = rand_n(300) + 500;
            int v = ((abs16((s16)DSW(0x2DF2)) >> 5) + (abs16((s16)DSW(0x2DF0)) >> 5)) + (s16)DSW(0xB168);
            if (v > lim) mission_end(6);
            else comm_get()->mission_state = 2;
            DSW(0xC06E) = 1;
            DSW(0xDEA2) = DSW(0xC0A4);
            DSW(0xDEC6) = DSW(0xC0B2);
            DSW(0xDECA) = (u16)(DSW(0x2DF4) + 8);
        }
        break;
    default: break;
    }

    /* ---- part B: training keys (while [9B05] & 10h) */
    if (DSB(0x9B05) & 0x10) {
        switch (key) {
        case 0x1300: mission_cheat_rearm(); mfd_refresh(0x16); break;                       /* Alt-R */
        case 0x1700: DSL(0xC02A) += (u32)0x20000 >> (DSW(0x4080) & 31); break;               /* Alt-I */
        case 0x2500: DSL(0xC02A) -= (u32)0x20000 >> (DSW(0x4080) & 31); break;               /* Alt-K */
        case 0x2400: DSL(0xBA10) -= (u32)0x20000 >> (DSW(0x4080) & 31); break;               /* Alt-J */
        case 0x2600: DSL(0xBA10) += (u32)0x20000 >> (DSW(0x4080) & 31); break;               /* Alt-L */
        case 0x3100:                                                                        /* Alt-N: night */
            DSW(0x43AE) = 1;
            DSW(0x3658) ^= 1;
            cockpit_palette_refresh();
            break;
        }
    }

    /* ---- part C: navigation page keys */
    if (DSW(0xDFDC) == 0x14) {
        u16 step = DSW(0xB80C) == 0 ? 0x1000 : 0;
        u16 i = DSW(0x2E7E);
        u16 xa = (u16)(0x2E6C + 4 * i), ya = (u16)(0x2E6E + 4 * i);
        int joy = comm_get()->input_devices & 1;
        int moved = 0;
        switch (key) {
        case 0x4700: DSW(0x98E2) -= 0x3C; break;
        case 0x4F00: DSW(0x98E2) += 0x3C; break;
        case 0x4800: case 0x4838: if (key == 0x4838 || joy) { DSW(ya) = (u16)(DSW(ya) - (step >> (DSW(0x4080) & 31))); moved = 1; } break;
        case 0x5000: case 0x5032: if (key == 0x5032 || joy) { DSW(ya) = (u16)(DSW(ya) + (step >> (DSW(0x4080) & 31))); moved = 1; } break;
        case 0x4B00: case 0x4B34: if (key == 0x4B34 || joy) { DSW(xa) = (u16)(DSW(xa) - (step >> (DSW(0x4080) & 31))); moved = 1; } break;
        case 0x4D00: case 0x4D36: if (key == 0x4D36 || joy) { DSW(xa) = (u16)(DSW(xa) + (step >> (DSW(0x4080) & 31))); moved = 1; } break;
        case 0x4900: case 0x4939:
            if (key == 0x4939 || joy) {
                if (DSW(0xB80C)) DSW(0x2E7C) = (u16)((DSW(0x2E7C) - 1) & 3); else DSW(0x2E7E) = (u16)((DSW(0x2E7E) - 1) & 3);
                moved = 1;
            }
            break;
        case 0x5100: case 0x5133:
            if (key == 0x5133 || joy) {
                if (DSW(0xB80C)) DSW(0x2E7C) = (u16)((DSW(0x2E7C) + 1) & 3); else DSW(0x2E7E) = (u16)((DSW(0x2E7E) + 1) & 3);
                moved = 1;
            }
            break;
        }
        if (moved) mfd_right_select(0x14);
        i = DSW(0x2E7E);
        DSW((u16)(0x2E6C + 4 * i)) = (u16)fx_clamp_wrap((s16)DSW((u16)(0x2E6C + 4 * i)), 0, 0x7FFF);
        DSW((u16)(0x2E6E + 4 * i)) = (u16)fx_clamp_wrap((s16)DSW((u16)(0x2E6E + 4 * i)), 0, 0x7FFF);
    }
    if (DSW(0xC06E) != 0) DSW(0xC07A) = 0x8C;

tail:
    /* ---- the tail: runs on every call */
    if ((s16)DSW(0x43BC) > 0) DSW(0x43BC)--;
    if (input_button(1) != 0 && DSW(0x43BC) == 0 && DSW(0xC06E) == 0) {
        wpn_release();
        if (DSW(0xDFDC) == 0x15 && DSW(0x365E)) mfd_weapons_page();
        DSW(0x43BC) = 4;
    }
    if (DSW(0x3632) & 0x40) {
        DSW(0x9B04) &= 0xFFCF;
        hud_lamp_update(5, 0);
        hud_lamp_update(6, 0);
    }
    if (DSW(0x9B04) & 0x20) {
        DSW(0x43C4)++;
        if ((s16)(20 * DSW(0x3660)) < (s16)DSW(0x43C4)) DSW(0x9B04) &= (u16)~0x20;
    } else if ((s16)DSW(0x43C4) > 0) DSW(0x43C4)--;
    hud_lamp_update(6, (DSW(0x9B04) & 0x20) ? 0x0A : (DSW(0x43C4) != 0 ? 0x2A : 0));
    if (DSW(0x3632) & 0x10) { DSW(0xE55E) = 1; DSW(0x98D8) = 2; }
    if (DSW(0x3632) & 0x08) DSW(0x3652) = 0;
    if (DSW(0xDFDC) == 0x14 && (DSW(0x3D60) & 0x1F) == 0 && DSW(0x365E)) mfd_refresh(0x14);
    if (DSW(0xE55E)) { if (DSW(0x98D8) == 2) DSW(0x9B04) |= 4; else DSW(0x98D8)++; }
    else if (DSW(0x98D8) == 0) { if (DSB(0x9B05) & 2) mfd_tv_label(); DSW(0x9B04) &= (u16)~4; }
    else DSW(0x98D8)--;
    hud_lamp_update(2, ((DSW(0x9B04) & 4) && (DSW(0x3D60) & 1)) ? 0x11 : 0);
    if (DSW(0xA068)) { if (DSW(0xC072) == 3) DSW(0x9B04) &= (u16)~1; else DSW(0xC072)++; }
    else if (DSW(0xC072) == 0) DSW(0x9B04) |= 1;
    else DSW(0xC072)--;
    int v = (DSW(0x9B04) & 1) ? 0 : (((s16)DSW(0xB168) < 250 || (DSW(0x3D60) & 1)) ? 0x0A : 0);
    hud_lamp_update(3, v);
}
