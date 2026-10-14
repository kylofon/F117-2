/* The shell's screens, part 1 (spec shell.md Part A): the ready room and its home card, the hangar and launch, the arming screen, and the
 * briefing map with its route, range rings and site icons; with the map helpers of Part B (range ring, route line, labels, the sine
 * routines they use). START 0000:008D .. 1F3F, 2E50 .. 31FA, 8C15 .. 8CA6. The ID quiz (057E, 0703, 0786) is dead code in this build and not ported.
 *
 * The button tables, colour triples and strings are data in the loaded image's DGROUP; where the original keeps a pointer in a table the
 * port reads the word and addresses DGROUP with it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell/shell.h"

#define MISSION_LOADED DSB(START_DS_mission_loaded)           /* D2A2 */
#define HOVER_PREV     DSB(START_DS_ui_hover_prev)            /* D096 */
#define BRIEF_MODE     DSB(START_DS_brief_mode)               /* B2E8 */

/* the site record i as a DS offset (SW / SB: shell.h) */
static inline u16 locname_off(u16 site)                        /* DS:DC70[ w0 != 0 ? w0 : byte 0E ] */
{
    u16 idx = SW(site, 0) ? SW(site, 0) : SB(site, 0x0E);
    return DSW((u16)(START_DS_wld_loc_names + 2 * idx));
}

/* ---------------------------------------------------------------------------------------------------- sine routines */

/* START 0000:8C88 ui_sin_interp: sine of a 16-bit angle from the 257-word table at DS:ABF8, linear interpolation, rounded */
int ui_sin_interp(unsigned ang)
{
    u16 frac = ang & 0xFF, idx = (u16)(ang >> 8);
    s16 cur = (s16)DSW((u16)(0xABF8 + 2 * idx)), next = (s16)DSW((u16)(0xABFA + 2 * idx));
    s32 prod = (s32)(s16)(next - cur) * (s32)frac;
    u16 add = (u16)((prod >> 8) & 0xFFFF);
    u16 rnd = (prod >> 7) & 1;                                  /* `shl al, 1` leaves bit 7 of the low byte in the carry */
    return (s16)(u16)(cur + add + rnd);
}

/* START 0000:8C7C ui_sin16 / 8C6C ui_cos16 */
int ui_sin16(unsigned ang) { return ui_sin_interp(ang & 0xFFFF); }
int ui_cos16(unsigned ang) { return ui_sin_interp((u16)(ang + 0x4000)); }

/* START 0000:8C15 ui_fixmul_q14: round(a * b / 32768) as the instruction sequence does it (imul; shl/rcl; shl/adc) */
int ui_fixmul_q14(int a, int b)
{
    s32 p = (s32)(s16)a * (s32)(s16)b;
    u16 lo = (u16)p, hi = (u16)((u32)p >> 16);
    u16 dx = (u16)((hi << 1) | (lo >> 15));
    dx = (u16)(dx + ((lo >> 14) & 1));
    return (s16)dx;
}

/* START 0000:3301 / 3318 */
int ui_sin_mul(unsigned ang, int r) { return ui_fixmul_q14(ui_sin16(ang), r); }
int ui_cos_mul(unsigned ang, int r) { return ui_sin_mul((u16)(ang + 0x4000), r); }

/* ------------------------------------------------------------------------------------------------------ map helpers */

/* START 0000:2E50 map_draw_range_ring: spec shell.md "2E50 map_draw_range_ring" */
void map_draw_range_ring(int wx, int wy, int radius, int colour, int solid)
{
    TextCtx *w1 = tctx(SH_CTX1);
    w1->fg = (s16)colour;
    int step = solid ? 8 : 0x10;
    if (!solid && (s16)radius >= 3000) step = 8;
    if (!solid && (s16)radius >= 7000) step = 4;
    int r = (s16)radius >> 7;
    int cx = (u16)wx / 146, cy = (u16)wy / 195;
    int a = 0, px = 0, py = 0, prevx = 0, prevy = 0;
    while (a <= 0x100) {
        u16 ang = (u16)((a & 0xFF) << 8);
        px = (s16)((s16)(ui_sin_mul(ang, r) << 7) / 146 + cx);
        py = (s16)((s16)(ui_cos_mul(ang, r) << 7) / -195 + cy);
        if (a == 0 || !solid) {
            if ((u16)px < 320 && (u16)py < 200) gfx_line((int *)w1, px, py, px, py, colour);
        } else {
            gfx_line((int *)w1, px, py, prevx, prevy, colour);
        }
        prevx = px;
        prevy = py;
        a += step;
    }
}

/* START 0000:2F4C map_draw_route_line: spec shell.md "2F4C int map_draw_route_line" -- the animated route, one dot per tick */
int map_draw_route_line(int x1, int y1, int x2, int y2, int skip)
{
    int dx = abs((s16)(x2 - x1)), dy = abs((s16)(y2 - y1));
    if (dx == 0 && dy == 0) return 0;
    int n, sx, sy;
    if (dy <= dx) {
        n = dx - 1;
        sy = ((s16)((y2 - y1) << 6)) / dx;
        sx = (x2 > x1) ? 0x40 : -0x40;
    } else {
        n = dy - 1;
        sx = ((s16)((x2 - x1) << 6)) / dy;
        sy = (y2 > y1) ? 0x40 : -0x40;
    }
    s16 x = (s16)((x1 << 6) + 0x20), y = (s16)((y1 << 6) + 0x20);
    for (int i = 0; i < n; i++) {
        if (skip == 0) skip = ui_wait_skip(1);
        int px = x >> 6, py = y >> 6;
        if (i > 1) {
            gfx_line((int *)tctx(SH_CTX1), px, py, px, py, 0);
            if (skip == 0) gfx_line((int *)tctx(SH_CTX0), px, py, px, py, 0);
        }
        x = (s16)(x + sx);
        y = (s16)(y + sy);
    }
    return skip;
}

/* START 0000:31FA map_place_label: the label near site p1, away from the points p2 and p3 (sites, DS offsets) */
void map_place_label(char *s, u16 p1, u16 p2, u16 p3)
{
    int X = SW(p1, 2) / 146, Y = SW(p1, 4) / 195;
    int x2 = SW(p2, 2) / 146, y2 = SW(p2, 4) / 195;
    int x3 = SW(p3, 2) / 146, y3 = SW(p3, 4) / 195;
    int x, y;
    if (X <= x2 && X <= x3) { x = X - 5; y = Y - 3; }
    else if (x2 <= X && x3 <= X) { x = X + 3; y = Y - 3; }
    else if (y2 >= Y && y3 >= Y) { x = X - 2; y = Y - 8; }
    else if (y2 <= Y && y3 <= Y) { x = X - 2; y = Y + 3; }
    else if ((s16)(((y3 - y2) * (x3 - x2)) & 0xFFFF) > 0) { x = X - 4; y = Y + 2; }
    else { x = X + 3; y = Y - 7; }
    ui_text(tctx(SH_CTX1), s, x, y);
}

/* ------------------------------------------------------------------------------------------------------ ready room */

/* START 0000:6A7C ui_str_last_word: the surname (the whole string when it has no space) */
char *ui_str_last_word(char *s)
{
    int i = (int)strlen(s) - 1;
    while (s[i] != ' ' && i > 0) i--;
    if (i > 0) i++;
    return s + i;
}

/* START 0000:02BA roster_difficulty_rating: spec shell.md "02BA" */
int roster_difficulty_rating(void)
{
    u8 *p = sh_pilot();
    u16 r = (u16)(DSB((u16)(START_DS_tbl_diff_theatre + PW(p, PIL_THEATRE))) + DSB((u16)(START_DS_tbl_diff_mission_type + PW(p, PIL_MTYPE)))
                  + DSB((u16)(START_DS_tbl_diff_skill + PW(p, PIL_SKILL))) + DSB((u16)(START_DS_tbl_diff_crash + PW(p, PIL_CRASH))));
    r = (u16)(r / 3);
    r = (u16)(r - PW(p, PIL_MODEL) + 2);
    if (r > 10) r = 10;
    return r;
}

/* START 0000:008D roster_home_card_draw: spec shell.md "008D" -- the pilot's card on homescrn */
void roster_home_card_draw(void)
{
    u8 *p = sh_pilot();
    char buf[0x50];
    TextCtx *c1 = tctx(SH_CTX1);
    if (MISSION_LOADED) ui_cursor_set_pos(0xA8, 0x4E); else ui_cursor_set_pos(0x115, 0x35);
    c1->font = 3;
    c1->fg = 8;
    snprintf(buf, sizeof buf, "%s%s", ds_str_ptr(START_DS_tbl_rank_names, PW(p, PIL_RANK)),
             ui_str_last_word(ds_str((u16)(START_DS_roster_names + PILOT_SIZE * PW(p, 0)))));
    buf[0x18] = 0;
    ui_text(c1, buf, 0x69, 0x7A);
    ui_text(c1, ds_str_ptr(START_DS_tbl_theatre_names, PW(p, PIL_THEATRE)), 0x67, 0x84);
    ui_text(c1, ds_str_ptr(START_DS_tbl_war_names, PW(p, PIL_WAR)), 0x73, 0x8B);
    ui_text(c1, ds_str_ptr(START_DS_tbl_mission_type_names, PW(p, PIL_MTYPE)), 0x72, 0x97);
    ui_text(c1, ds_str_ptr(START_DS_tbl_skill_names, PW(p, PIL_SKILL)), 0x82, 0xA7);
    ui_text(c1, ds_str_ptr(START_DS_tbl_crash_names, PW(p, PIL_CRASH)), 0x82, 0xAF);
    ui_text(c1, ds_str_ptr(START_DS_tbl_flight_model_names, PW(p, PIL_MODEL)), 0x82, 0xB7);
    snprintf(buf, sizeof buf, "%d", roster_difficulty_rating());                    /* DS:0050 "%d" */
    ui_text(c1, buf, 0xBD, 0x73);
    blit_rect(1, 0x67, 0x73, 0, 0x67, 0x73, 0x64, 0x49);
}

/* START 0000:020F ui_ready_room: spec shell.md "020F" -- the state machine of the screens */
void ui_ready_room(void)
{
    int state = 0, done = 0;
    if (rd16(0, 0x4F2) != 0 || PW(sh_pilot(), PIL_STATUS) != 0) state = 4;           /* first run / retired or killed: the roster first */
    while (!done) {
        if (state == 0) {
            ui_show_screen(5, roster_home_card_draw);
            do state = ui_menu_loop(DSP(START_DS_btn_home)); while (state == 0x63);     /* Esc: wait again without redrawing */
        }
        switch (state) {
        case 1: ui_cursor_erase(); state = ui_orders_screen(); break;
        case 2: ui_cursor_erase(); state = brief_screen_run(); break;
        case 3: ui_cursor_erase(); state = arm_screen_run(); break;
        case 4: ui_cursor_erase(); state = roster_screen_run(); break;
        case 5: ui_cursor_erase(); done = ui_hangar_launch() & 0xFF; state = 0; break;
        default: break;
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- hangar, launch */

/* the hangar strip records: 12 bytes (sx, sy, dx, dy, w, h) at DS:006A and DS:0106 */
static void hangar_blit(u16 rec, int dpage)
{
    blit_rect(3, (s16)DSW(rec), (s16)DSW(rec + 2), dpage, (s16)DSW(rec + 4), (s16)DSW(rec + 6), (s16)DSW(rec + 8), (s16)DSW(rec + 10));
}

/* START 0000:0311 ui_hangar_hook: hook of ui_show_screen(4 hangscrn) */
void ui_hangar_hook(void)
{
    ui_load_screen_pic(30, 3);
    blit_rect(1, 0, 0, 4, 0, 0, 320, 200);
    hangar_blit(START_DS_tbl_hangar_rects, 0);
    hangar_blit(START_DS_tbl_hangar_rects, 1);
}

/* START 0000:0448 ui_hangar_anim: the hangar door opens and the aircraft rolls out; any key or click ends it */
int ui_hangar_anim(void)
{
    sh_snd(0x1F);
    for (int i = 0; i < 0x12; i++) {
        DSB(0xADFC) = 0;
        int idx = (s8)DSB((u16)(START_DS_tbl_hangar_frames + i));
        if (idx != -1) {
            blit_rect(4, 30, 108, 1, 30, 108, 105, 92);
            hangar_blit((u16)(START_DS_tbl_hangar_rects + 12 * idx), 1);
            blit_rect(1, 30, 108, 0, 30, 108, 105, 92);
        }
        int idx2 = (s8)DSB((u16)(START_DS_tbl_hangar_frames2 + i));
        if (idx2 != -1) hangar_blit((u16)(START_DS_tbl_hangar_rects2 + 12 * idx2), 0);
        if (ui_wait_input((s16)(8 - DSB(0xADFC)))) return 0;
    }
    blit_rect(4, 30, 108, 0, 30, 108, 105, 92);
    ui_wait_input(0x78);
    pal_fade_out(1);
    return 0;
}

/* START 0000:038C ui_hangar_launch: spec shell.md "038C"; 1 = launch */
int ui_hangar_launch(void)
{
    u16 start = SITE(DSW(START_DS_mission_start_site));
    bool no_hangar = (SB(start, 9) & 2) != 0;
    if (!no_hangar) ui_show_screen(4, ui_hangar_hook); else ui_show_screen(0x20, NULL);
    if (MISSION_LOADED == 0) {
        ui_dialog_run(DSP(START_DS_dlg_need_briefing), 0xA5, 0x9E);
        return 0;
    }
    int r = ui_dialog_run(DSP(START_DS_dlg_ready), 0xA1, 0xA4);
    if (r == 0x63) { ui_cursor_erase(); return 0; }
    if (r > 0x63) return r;
    if (r == 1) {
        memcpy(DSP(0x67E0), DSP(START_DS_rgb_hover), 3);
        ui_cursor_erase();
        ui_wait_ticks(0x14);
        if (!no_hangar) ui_hangar_anim();
        screen_off();
        tctx(SH_CTX0)->fg = 0;
        gfx_fill_rect(tctx(SH_CTX0), 0, 0, 0x13F, 0xC8);
        return 1;
    }
    if (r == 2) { ui_cursor_erase(); return 0; }
    return r;
}

/* ------------------------------------------------------------------------------------------------------------ arming */

#define ARM_BTN(i) ((u16)(0x6C5E + 8 * (i)))                    /* entry i of the arming button table */

/* START 0000:0D65 arm_draw_bay: spec shell.md "0D65" */
void arm_draw_bay(int slot, int wid)
{
    u8 *p = sh_pilot();
    u16 bx = (u16)((PW(p, PIL_MODEL) * 4 + slot) * 2);
    int x = (s16)DSW((u16)(START_DS_tbl_bay_x + bx)), y = (s16)DSW((u16)(START_DS_tbl_bay_y + bx));
    blit_rect(2, 0xD2 + 0x37 * (slot & 1), 0x12 * (slot & 0xFE), 1, x, y, 0x37, 0x24);
    blit_rect(2, ((u16)wid / 6) * 0x33, ((u16)wid % 6) << 5, 1, x + 2, y + 2, 0x33, 0x20);
    blit_rect(1, x, y, 0, x, y, 0x37, 0x24);
}

/* START 0000:0E23 arm_roll_unavailable: spec shell.md "0E23". The original tests D878[table[j]] but sets D878[j] (reproduced). */
void arm_roll_unavailable(void)
{
    for (int i = 0; i < 0x14; i++) DSB((u16)(START_DS_arm_unavail + i)) = 0;
    unsigned n = ui_rand_mod(5);
    unsigned i = 0;
    while (n > i) {
        unsigned j = ui_rand_mod(0x10);
        u8 t = DSB((u16)(START_DS_tbl_weapon_knockout + j));
        if (t == 0xFF) continue;
        if (DSB((u16)(START_DS_arm_unavail + t)) != 0) continue;
        DSB((u16)(START_DS_arm_unavail + j)) = 1;
        i++;
    }
    DSB(START_DS_pilot_model_copy_b) = (u8)PW(sh_pilot(), PIL_MODEL);
    DSB(START_DS_arm_unavail) = (u8)PW(sh_pilot(), PIL_MODEL);
}

/* START 0000:0E96 arm_draw_fuel: spec shell.md "0E96" -- the fuel line of the arming screen (10000 lbs + 1900 per tank) */
void arm_draw_fuel(void)
{
    Comm *comm = comm_get();
    TextCtx *c1 = tctx(SH_CTX1);
    char buf[0x14];
    c1->fg = 0x42;
    gfx_fill_rect(c1, 0x76, 0x4D, 0xA1, 0x51);
    int fuel = MISSION_LOADED ? 10000 : 0;
    for (int i = 0; i < 4; i++) if (comm->weapon_id[i] == 0x11) fuel += 0x76C;
    c1->fg = 0xF;
    c1->font = 3;
    snprintf(buf, sizeof buf, "%d lbs.", (s16)fuel);
    ui_text(c1, buf, 0x76, 0x4D);
    blit_rect(1, 0x76, 0x4D, 0, 0x76, 0x4D, 0x2C, 5);
}

/* START 0000:0BD4 arm_show_weapon_info: spec shell.md "0BD4" -- picture, name plate and description of weapon row `row` (1..18; 0x14 clears) */
int arm_show_weapon_info(int row)
{
    TextCtx *c1 = tctx(SH_CTX1);
    if (row == 0x14) {
        c1->fg = 3;
        gfx_fill_rect(c1, 9, 0x88, 0xDD, 0xC1);
        tctx(SH_CTX0)->fg = 3;
        gfx_fill_rect(tctx(SH_CTX0), 9, 0x88, 0xDD, 0xC1);
        return 0;
    }
    if (row == 0) row = 0x14;
    row--;
    int sel = row;
    c1->fg = 3;
    gfx_fill_rect(c1, 9, 0xAA, 0xDD, 0xC1);
    int page = 3;
    if ((u16)sel > 9) { page = 4; sel -= 10; }
    blit_rect(page, (s16)DSW((u16)(START_DS_tbl_wpn_pic_sx + 2 * sel)), (s16)DSW((u16)(START_DS_tbl_wpn_pic_sy + 2 * sel)), 1, 0x3F, 0x88, 0xA0, 0x28);
    int sx = page == 3 ? 0xCC : 0x106;
    int sy = 12 * sel + 0x50;
    blit_rect(2, sx, sy, 1, 9, 0x88, 0x3A, 6);
    blit_rect(2, sx, sy + 6, 1, 9, 0x90, 0x3A, 6);
    c1->page = 1;
    c1->font = 4;
    c1->fg = 0xF;
    c1->bg = 8;
    ui_text_wrap(c1, ds_str_ptr(START_DS_tbl_weapon_desc, row), 9, 0xAA, 0xD5, 8, 0);
    blit_rect(1, 9, 0x88, 0, 9, 0x88, 0xD5, 0x3A);
    return 0;
}

/* START 0000:07EE arm_screen_draw: spec shell.md "07EE" -- hook of the arming screen */
void arm_screen_draw(void)
{
    u8 *p = sh_pilot();
    Comm *comm = comm_get();
    DSB(START_DS_pilot_model_copy_b) = (u8)PW(p, PIL_MODEL);
    DSB(START_DS_arm_unavail) = (u8)PW(p, PIL_MODEL);
    for (u16 i = 0; i < 4; i++)
        if (!(MISSION_LOADED != 0 && (u16)((2 - PW(p, PIL_MODEL)) << 1) > i)) comm->weapon_id[i] = 0x13;
    for (int i = 0; i < 0x10; i++) {
        if (DSB((u16)(START_DS_arm_unavail + i)) != 0) {
            memcpy(DSP((u16)(0x67E0 - 3 * i)), DSP(START_DS_rgb_disabled), 3);
            ui_order_desc((unsigned *)DSP(ARM_BTN(i)));
        } else {
            ui_order_asc((unsigned *)DSP(ARM_BTN(i)));
        }
    }
    HOVER_PREV = 0;
    ui_load_screen_pic(0xF, 2);
    ui_load_screen_pic(0xD, 3);
    ui_load_screen_pic(0xE, 4);
    for (u16 i = 0; (u16)((2 - PW(p, PIL_MODEL)) << 1) > i; i++) arm_draw_bay(i, comm->weapon_id[i]);
    if (MISSION_LOADED == 0) comm->fuel_lbs = 0;
    TextCtx *c1 = tctx(SH_CTX1);
    char buf[0x14];
    c1->fg = 0xF;
    c1->font = 3;
    snprintf(buf, sizeof buf, "%d lbs.", (s16)comm->fuel_lbs);
    ui_text(c1, buf, 0x76, 0x39);
    blit_rect(1, 0x76, 0x39, 0, 0x76, 0x39, 0x2C, 5);
    arm_draw_fuel();
    ui_cursor_set_pos(0x10F, 0x94);
}

/* START 0000:0B01 arm_hover_hook: spec shell.md "0B01" */
void arm_hover_hook(void)
{
    int h = ui_hit_test(DSP(ARM_BTN(0)), DSB(0x6C5D)) & 0xFF;
    if (h != 0) {
        if (h <= 0x12 && h != HOVER_PREV) {
            pal_queue_push(0x100 - h, DSP(START_DS_rgb_hover), 1);
            arm_show_weapon_info(h);
        } else if (h >= 0x17 && h != HOVER_PREV) {
            pal_queue_push(0x100 - h, DSP(START_DS_rgb_hover), 1);
        }
    }
    if (HOVER_PREV != 0 && HOVER_PREV != h) {
        if (HOVER_PREV <= 0x12) {
            pal_queue_push(0x100 - HOVER_PREV, DSP(START_DS_rgb_normal), 1);
            if (h == 0) arm_show_weapon_info(0x14);
        } else if (HOVER_PREV >= 0x17) {
            pal_queue_push(0x100 - HOVER_PREV, DSP(START_DS_rgb_normal), 1);
        }
    }
    HOVER_PREV = (u8)h;
}

/* START 0000:0963 arm_screen_run: spec shell.md "0963"; returns the next state (0 = ready room, 5 = hangar) */
int arm_screen_run(void)
{
    Comm *comm = comm_get();
    u8 *p = sh_pilot();
    int slot = 0;
    ui_show_screen(1, arm_screen_draw);
    for (;;) {
        int r = ui_menu_loop(DSP(START_DS_btn_arming));
        if (r == 0x63 || r == 24) { ui_cursor_erase(); ui_load_screen_pic(0xB, 2); return 0; }
        if (r > 0x63 || r < 1) continue;
        if (r <= 0x12) {
            if (comm->weapon_id[(s8)slot] == 0x12) {
                ui_cursor_erase();
                sh_snd(0x22);
                ui_load_screen_pic(0xB, 2);
                ui_popup_text(ds_str(0x1F6), 0xA0, 0x64, 4, 1, 0x78);
                ui_wait_skip(0xD2);
                ui_popup_restore();
                ui_load_screen_pic(0xF, 2);
                continue;
            }
            sh_snd(0x1E);
            comm->weapon_id[slot] = (u16)(r - 1);
            comm->weapon_qty[slot] = DSB((u16)(START_DS_tbl_bay_quantity + r));
            arm_draw_bay(slot, r - 1);
            arm_draw_fuel();
            continue;
        }
        if (r >= 0x13 && r <= 0x16) {
            sh_snd(0x1D);
            if (PW(p, PIL_MODEL) != 0 && r > 0x14) continue;
            if (r - slot == 0x13) continue;
            pal_queue_push(0x100 - r, DSP(START_DS_rgb_red2), 1);
            pal_queue_push(0xED - slot, DSP(START_DS_rgb_white), 1);
            slot = r - 0x13;
            continue;
        }
        if (r == 23) { ui_cursor_erase(); ui_load_screen_pic(0xB, 2); return 5; }
    }
}

/* ----------------------------------------------------------------------------------------------------------- briefing */

#define BRIEF_BTN_COUNT DSB(0x6D21)
#define SITE_A(i) SITE(DSB((u16)(START_DS_brief_site_pairs + 2 * (i))))
#define SITE_B(i) SITE(DSB((u16)(START_DS_brief_site_pairs + 2 * (i) + 1)))

/* the average radar range: |max * factor| >> 4 with the 16-bit multiply of the original (sign re-applied after the shift) */
static int radar_avg(u16 type)
{
    u16 rec = (u16)(START_DS_tbl_radar_types + 0xE * type);
    s16 prod = (s16)((s16)DSW(rec + 8) * (s16)DSW(rec + 10));
    int sign = prod < 0 ? -1 : 0;
    s16 a = (s16)((prod ^ sign) - sign);
    a = (s16)(a >> 4);
    return (s16)((a ^ sign) - sign);
}

/* START 0000:1B37 brief_hint_text: spec shell.md "1B37" -- the one-line help under the buttons */
void brief_hint_text(int h)
{
    TextCtx *c1 = tctx(SH_CTX1);
    c1->fg = 0xE2;
    gfx_fill_rect(c1, 0x57, 0xB4, 0xEA, 0xC4);
    c1->font = 4;
    c1->fg = 0xF;
    c1->bg = 8;
    u16 text;
    switch (h) {
    case 1: text = 0x327; break;
    case 2: text = 0x35E; break;
    case 3: text = 0x3C9; break;
    case 4: text = 0x3FE; break;
    case 5: text = 0x43B; break;
    case 6: text = 0x393; break;
    case 7: text = 0x47A; break;
    case 8: text = 0x4B6; break;
    case 0x65: text = 0x4E5; break;
    case 0x64: text = 0x51E; break;
    default: text = 0x55E; break;
    }
    ui_text_wrap(c1, ds_str(text), 0x57, 0xB4, 0x92, 8, 1);
    blit_rect(1, 0x57, 0xB4, 0, 0x57, 0xB4, 0x92, 0x10);
}

/* START 0000:1C2F brief_site_info: spec shell.md "1C2F" -- the data box of site button n */
void brief_site_info(int n)
{
    char buf[0x100], tmp[0x52];
    u16 R = 0, Ab = 0;
    for (int k = 0; k < 2; k++) {
        u16 s = SITE(DSB((u16)(START_DS_brief_site_pairs + 2 * n + k)));
        if (SW(s, 6) != 0) R = s;
        if (SB(s, 8) & 1) Ab = s;
    }
    buf[0] = 0;
    if (Ab) {
        strcat(buf, ds_str(0x58B));
        snprintf(tmp, sizeof tmp, ds_str(0x59A), ds_str(locname_off(Ab)));
        strcat(buf, tmp);
        strcat(buf, ds_str((SB(Ab, 9) & 1) ? 0x5A3 : (SB(Ab, 9) & 4) ? 0x5B4 : 0x5C6));
        if (SW(Ab, 0xC) != 0) {
            snprintf(tmp, sizeof tmp, ds_str(0x5D7), ds_str((u16)(START_DS_tbl_fighter_types + (SW(Ab, 0xA) << 5))));
            strcat(buf, tmp);
            snprintf(tmp, sizeof tmp, ds_str(0x5E6), (s16)SW(Ab, 0xC));
            strcat(buf, tmp);
        }
    }
    if (R) {
        u16 type = SW(R, 6);
        u16 rr = (u16)(START_DS_tbl_radar_types + 0xE * type);
        if (Ab) strcat(buf, ds_str(0x5F2));
        strcat(buf, ds_str(0x5F4));
        if (!Ab) {
            snprintf(tmp, sizeof tmp, ds_str(0x601), ds_str(locname_off(R)));
            strcat(buf, tmp);
            if (SB(R, 9) & 1) strcat(buf, ds_str(0x60A));
        }
        snprintf(tmp, sizeof tmp, ds_str(0x629), ds_str(rr), ds_str((DSB((u16)(rr + 12)) & 1) ? 0x61B : 0x623));
        strcat(buf, tmp);
        int v;
        u16 label;
        if (DSB(START_DS_brief_range_mode) == 1) { v = (s16)DSW(rr + 8); label = 0x637; }
        else { v = radar_avg(type); label = 0x63D; }
        snprintf(tmp, sizeof tmp, ds_str(0x643), v, ds_str(label));
        strcat(buf, tmp);
        snprintf(tmp, sizeof tmp, ds_str(0x655), (s16)DSW((u16)(START_DS_tbl_sam_types + 0x12 * type + 8)));
        strcat(buf, tmp);
        if (SB(R, 8) & 0x40) strcat(buf, ds_str(0x66A));
        if (SB(R, 8) & 0x20) strcat(buf, ds_str(0x68C));
    }
    ui_popup_text(buf, (s16)DSW(START_DS_ui_cursor_x) + 16, (s16)DSW(START_DS_cursor_y) - 16, 3, 0, 0x78);
}

/* START 0000:183A brief_draw_ranges: spec shell.md "183A" -- the range rings of mode 1 (radar) and 2 (missiles) */
void brief_draw_ranges(void)
{
    int col = 0xC0;
    for (u16 i = 0; i < DSW(START_DS_brief_site_btn_count); i++, col++) {
        u16 site = SITE_A(i);
        if (SW(site, 6) == 0) site = SITE_B(i);
        if (SW(site, 6) == 0) continue;
        u16 type = SW(site, 6);
        int radius, solid;
        if (BRIEF_MODE == 2) {
            radius = (s16)DSW((u16)(START_DS_tbl_sam_types + 0x12 * type + 8));
            solid = 1;
        } else {
            radius = DSB(START_DS_brief_range_mode) == 1 ? (s16)DSW((u16)(START_DS_tbl_radar_types + 0xE * type + 8)) : radar_avg(type);
            solid = DSB((u16)(START_DS_tbl_radar_types + 0xE * type + 12)) & 1;
        }
        radius = (s16)(radius << 6);
        memcpy(DSP((u16)(START_DS_pal_buffer + 3 * col)), DSP((SB(site, 8) & 0x40) ? START_DS_rgb_white : START_DS_rgb_cyan), 3);
        if (SB(site, 9) & 0x40) map_draw_range_ring((s16)SW(site, 2), (s16)SW(site, 4), radius, col, solid);
    }
    pal_queue_push(0xC0, DSP(0x6723), DSW(START_DS_brief_site_btn_count));
}

/* START 0000:196A brief_draw_site_icons: spec shell.md "196A"; done != 0: instant, 0: one pause and sound per icon */
int brief_draw_site_icons(char done)
{
    TextCtx *c0 = tctx(SH_CTX0), *c1 = tctx(SH_CTX1);
    for (u16 i = 0; i < DSW(START_DS_brief_site_btn_count); i++) {
        if (done == 0) { done = (char)ui_wait_skip(8); sh_snd(0x1C); }
        u16 A = SITE_A(i);
        int x = (s16)DSW((u16)(0x6D62 + 8 * i)) + 6, y = (s16)DSW((u16)(0x6D64 + 8 * i)) + 5;
        if (DSB((u16)(START_DS_brief_site_pairs + 2 * i + 1)) != 0) {
            unsigned s1;
            int x1, x2;
            if (BRIEF_MODE == 1) { s1 = DSW(0xD08E); x1 = x - 2; x2 = x1 + 5; }
            else { s1 = DSW(0xE082); x1 = x - 1; x2 = x1 + 3; }
            ui_sprite_draw(c1, x1, y, s1);
            ui_sprite_draw(c1, x2, y, DSW(0xD29E));
            if (done == 0) {
                ui_sprite_draw(c0, x1, y, s1);
                ui_sprite_draw(c0, x2, y, DSW(0xD29E));
            }
        } else {
            unsigned seg;
            if (SW(A, 6) != 0) {
                if (SB(A, 8) & 8) seg = DSW(0xE09A);
                else if (BRIEF_MODE == 1) { seg = DSW(0xD08E); x--; }
                else seg = DSW(0xE082);
            } else {
                seg = DSW(0xD29E);
            }
            ui_sprite_draw(c1, x, y, seg);
            if (done == 0) ui_sprite_draw(c0, x, y, seg);
        }
    }
    return done;
}

/* START 0000:1AB6 brief_draw_hostile: spec shell.md "1AB6". The scaled coordinates overwrite the loop counters (an original bug kept). */
void brief_draw_hostile(void)
{
    TextCtx *c1 = tctx(SH_CTX1);
    u16 x, y;
    int res = 0x64;
    for (x = 0; x < 0x10; x++) {
        for (y = 0; y < 0x10; ) {
            if (DSB((u16)(START_DS_map_hostile_grid + x + (y << 4))) & 0x10) {
                res = 0x65;
                y = (u16)(0x7FF * y) / 0xC3;
                x = (u16)(0x7FF * x) / 0x92;
                ui_sprite_draw(c1, x, y, DSW(0xCAC4));
            }
            y++;
            if (y >= 0x10) break;
        }
    }
    brief_hint_text(res);
}

/* START 0000:149C brief_draw_route: spec shell.md "149C" -- the route A -> B -> C -> D with its pop-up texts; returns done */
char brief_draw_route(char done)
{
    TextCtx *c0 = tctx(SH_CTX0), *c1 = tctx(SH_CTX1);
    u16 A = SITE(DSW(START_DS_mission_start_site)), B = SITE(DSW(START_DS_mission_target1_site));
    u16 C = SITE(DSW(START_DS_mission_target2_site)), D = SITE(DSW(START_DS_mission_land_site));
    int Ax = SW(A, 2) / 146, Ay = SW(A, 4) / 195, Bx = SW(B, 2) / 146, By = SW(B, 4) / 195;
    int Cx = SW(C, 2) / 146, Cy = SW(C, 4) / 195, Dx = SW(D, 2) / 146, Dy = SW(D, 4) / 195;
    char buf[0x100];
    ui_sprite_draw(c0, Ax - 1, Ay - 2, DSW(0xD88E));
    ui_sprite_draw(c1, Ax - 1, Ay - 2, DSW(0xD88E));
    if (done == 0) {
        snprintf(buf, sizeof buf, "Depart from %s at %s hours.", ds_str(locname_off(A)), ds_str(START_DS_brief_time_launch));
        ui_popup_text(buf, Ax, Ay + 0x18, 3, 1, 0x78);
        done = (char)ui_wait_skip(0xF0);
        ui_popup_restore();
    }
    done = (char)map_draw_route_line(Ax, Ay, Bx, By, done);
    ui_sprite_draw(c0, Bx - 1, By - 1, DSW(0xE09C));
    ui_sprite_draw(c1, Bx - 1, By - 1, DSW(0xE09C));
    if (done == 0) {
        brief_format_objective(DSP(START_DS_objective_rec), buf);
        ui_popup_text(buf, Bx, By + 0x18, 3, 1, 0x78);
        done = (char)ui_wait_skip(0xF0);
        ui_popup_restore();
    }
    done = (char)map_draw_route_line(Bx, By, Cx, Cy, done);
    ui_sprite_draw(c0, Cx - 1, Cy - 1, DSW(0xE09C));
    ui_sprite_draw(c1, Cx - 1, Cy - 1, DSW(0xE09C));
    if (done == 0) {
        brief_format_objective(DSP(0xE070), buf);
        ui_popup_text(buf, Cx, Cy + 0x18, 3, 1, 0x78);
        done = (char)ui_wait_skip(0xF0);
        ui_popup_restore();
    }
    done = (char)map_draw_route_line(Cx, Cy, Dx, Dy, done);
    ui_sprite_draw(c0, Dx - 1, Dy - 2, DSW(0xD88E));
    ui_sprite_draw(c1, Dx - 1, Dy - 2, DSW(0xD88E));
    if (done == 0) {
        snprintf(buf, sizeof buf, ds_str(0x30F), ds_str(locname_off(D)));         /* "Land at %s." */
        ui_popup_text(buf, Dx, Dy + 0x18, 3, 1, 0x78);
        done = (char)ui_wait_skip(0xF0);
        ui_popup_restore();
    }
    c1->fg = 1;
    c1->font = 4;
    if (DSW(START_DS_mission_land_site) == DSW(START_DS_mission_start_site)) {
        map_place_label(ds_str(0x31B), A, B, C);
    } else {
        map_place_label(ds_str(0x31F), A, B, C);
        map_place_label(ds_str(0x321), D, B, C);
    }
    map_place_label(ds_str(0x323), B, A, C);
    map_place_label(ds_str(0x325), C, B, D);
    return done;
}

/* START 0000:141D brief_redraw: spec shell.md "141D" */
void brief_redraw(char done)
{
    blit_rect(4, 0, 0, 1, 0, 0, 320, 0xA9);
    done = (char)brief_draw_site_icons(done);
    brief_draw_route(done);
    if (BRIEF_MODE == 1 || BRIEF_MODE == 2) brief_draw_ranges();
    else if (BRIEF_MODE == 3) brief_draw_hostile();
    ui_sprite_draw(tctx(SH_CTX1), 0xE5, 0x32, DSW(0xD330));
    pal_apply_queue();
    blit_rect(1, 0, 0, 0, 0, 0, 320, 0xA9);
}

/* START 0000:1F3F brief_build_site_buttons: spec shell.md "1F3F" -- one 14x14 box per site (or per pair of sites at one place) */
void brief_build_site_buttons(void)
{
    u16 dst = 0x6D62;
    DSW(START_DS_brief_site_btn_count) = 0;
    for (u16 i = 0; DSW(START_DS_wld_site_count) > i; i++) {
        u16 site = SITE(i);
        if (!(SW(site, 6) != 0 || (SB(site, 8) & 1) || (SB(site, 9) & 2))) continue;
        if (SB(site, 9) & 8) continue;
        if (SW(site, 6) != 0) SB(site, 9) &= 0xBF;
        u16 j;
        if ((SB(site, 8) & 8) && !(SB(site, 9) & 4)) {
            j = DSW(START_DS_wld_site_count);
        } else {
            for (j = 0; j < DSW(START_DS_wld_site_count); j++) {
                u16 p = SITE(j);
                if (!(SW(p, 6) != 0 || (SB(p, 8) & 1) || (SB(p, 9) & 2))) continue;
                if (SB(p, 9) & 8) continue;
                if (SW(p, 0) != SW(site, 0)) continue;
                if (j == i) continue;
                break;
            }
        }
        u16 cx = 0, cy = 0;
        if (j == DSW(START_DS_wld_site_count)) {
            cx = SW(site, 2) / 146;
            cy = SW(site, 4) / 195;
        } else if (j > i) {
            u16 p = SITE(j);
            cx = (u16)(SW(p, 2) + SW(site, 2)) / 0x124;
            cy = (u16)(SW(p, 4) + SW(site, 4)) / 0x186;
        }
        if (j <= i) continue;
        DSW(dst) = cx >= 7 ? (u16)(cx - 7) : 0;
        DSW((u16)(dst + 2)) = cy >= 7 ? (u16)(cy - 7) : 0;
        DSW((u16)(dst + 4)) = (u16)(DSW(dst) + 0xE);
        DSW((u16)(dst + 6)) = (u16)(DSW((u16)(dst + 2)) + 0xE);
        u16 m = DSW(START_DS_brief_site_btn_count);
        DSB((u16)(START_DS_brief_site_pairs + 2 * m)) = (u8)i;
        DSB((u16)(START_DS_brief_site_pairs + 2 * m + 1)) = j < DSW(START_DS_wld_site_count) ? (u8)j : 0;
        dst += 8;
        DSW(START_DS_brief_site_btn_count)++;
    }
    BRIEF_BTN_COUNT = (u8)(DSB(START_DS_brief_site_btn_count) + 8);
}

/* START 0000:0F4E brief_setup: spec shell.md "0F4E" */
void brief_setup(void)
{
    HOVER_PREV = 0;
    DSB(START_DS_brief_range_mode) = 0;
    DSB(START_DS_brief_mode_active) = 1;
    BRIEF_MODE = 1;
    pal_fade_out(1);
    ui_load_screen_pic(2, 3);
    ui_load_screen_pic(PW(sh_pilot(), PIL_THEATRE) + 0x10, 4);
    pal_select(2);
    brief_new_mission();
    ui_cursor_set_pos(0x110, 0x77);
}

/* START 0000:1308 brief_new_mission: spec shell.md "1308" -- generates the mission on the first display */
void brief_new_mission(void)
{
    blit_rect(3, 0, 0, 0, 0, 0, 320, 200);
    blit_rect(3, 0, 0, 1, 0, 0, 320, 200);
    memcpy(DSP(0x67DA), DSP(START_DS_rgb_normal), 3);
    memcpy(DSP(0x67DD), DSP(0x67DA), 3);
    memcpy(DSP(0x67E0), DSP(0x67DD), 3);
    memcpy(DSP((u16)(0x67E3 - 3 * (s8)BRIEF_MODE)), DSP(START_DS_rgb_highlight), 3);
    pal_fade_in(1);
    ui_wait_skip(10);
    if (MISSION_LOADED == 0) {
        ui_popup_text(ds_str(0x2DA), 0xA0, 0x64, 4, 1, 0xA0);                    /* "Decoding mission orders." */
        mission_site_setup();
        ui_wait_skip(0x46);
        ui_popup_restore();
    }
    brief_build_site_buttons();
    blit_rect(4, 0, 0, 1, 0, 0, 320, 0xA9);
    blit_rect(4, 0, 0, 0, 0, 0, 320, 0xA9);
    brief_redraw(0);
    MISSION_LOADED = 1;
    brief_hint_text(0);
    HOVER_PREV = 0;
}

/* START 0000:11DB brief_hover_hook: spec shell.md "11DB" */
void brief_hover_hook(void)
{
    u8 h = (u8)ui_hit_test(DSP(0x6D22), BRIEF_BTN_COUNT);
    if (h == HOVER_PREV) return;
    brief_hint_text(h);
    if (HOVER_PREV != 0) {
        if (HOVER_PREV > 8) {
            ui_popup_restore();
            int idx = HOVER_PREV - 9;
            if (SW(SITE_A(idx), 6) != 0 || SW(SITE_B(idx), 6) != 0)
                pal_queue_push(HOVER_PREV + 0xB7, DSP((u16)(START_DS_pal_buffer + 3 * (HOVER_PREV + 0xB7))), 1);
        } else if (BRIEF_MODE != HOVER_PREV) {
            pal_queue_push(0x100 - HOVER_PREV, DSP(START_DS_rgb_normal), 1);
        }
    }
    if (h != 0) {
        if (h > 8) {
            int idx = h - 9;
            brief_site_info(idx);
            if (SW(SITE_A(idx), 6) != 0 || SW(SITE_B(idx), 6) != 0) pal_queue_push(h + 0xB7, DSP(START_DS_rgb_red), 1);
        } else if (BRIEF_MODE != h) {
            pal_queue_push(0x100 - h, DSP(START_DS_rgb_hover), 1);
        }
    }
    HOVER_PREV = h;
}

/* toggle or set the range-ring bit (0x40 of the high flag byte) on the sites of button i that have a type; op 0 = xor, 1 = or, 2 = clear */
static void ring_bit(u16 i, int op)
{
    u16 s[2] = { SITE_A(i), SITE_B(i) };
    for (int k = 0; k < 2; k++) {
        if (SW(s[k], 6) == 0) continue;
        if (op == 0) SB(s[k], 9) ^= 0x40; else if (op == 1) SB(s[k], 9) |= 0x40; else SB(s[k], 9) &= 0xBF;
    }
}

/* START 0000:0FA3 brief_screen_run: spec shell.md "0FA3"; returns the next state (0 = ready room, 3 = arming) */
int brief_screen_run(void)
{
    brief_setup();
    for (;;) {
        int r = ui_menu_loop(DSP(START_DS_btn_brief));
        switch (r) {
        case 1: BRIEF_MODE = 1; break;
        case 2: BRIEF_MODE = 2; break;
        case 3: BRIEF_MODE = 3; break;
        case 4:
            for (u16 i = 0; i < DSW(START_DS_brief_site_btn_count); i++) ring_bit(i, 1);
            brief_redraw(1);
            break;
        case 5:
            for (u16 i = 0; i < DSW(START_DS_brief_site_btn_count); i++) ring_bit(i, 2);
            brief_redraw(1);
            break;
        case 6: brief_orders_screen(); break;
        case 7: ui_cursor_erase(); return 3;
        case 8: MISSION_LOADED = 0; brief_new_mission(); break;
        case 0x63: ui_cursor_erase(); return 0;
        case 0x120:
            if (BRIEF_MODE == 1) {
                u8 h = (u8)ui_hit_test(DSP(0x6D22), BRIEF_BTN_COUNT);
                if (h > 8) ui_popup_restore();
                DSB(START_DS_brief_range_mode) ^= 1;
                brief_redraw(1);
                if (h > 8) brief_site_info(h - 9);
            }
            break;
        default:
            if ((s16)BRIEF_BTN_COUNT > r && r > 8) {
                ring_bit((u16)(r - 9), 0);
                ui_popup_restore();
                brief_redraw(1);
                pal_queue_push(r + 0xB7, DSP(START_DS_rgb_red), 1);
                brief_site_info(r - 9);
            }
            break;
        }
        if (DSB(START_DS_brief_mode_active) == BRIEF_MODE) continue;
        if (DSB(START_DS_brief_mode_active) != 0) pal_queue_push(0x100 - (s8)DSB(START_DS_brief_mode_active), DSP(START_DS_rgb_normal), 1);
        pal_queue_push(0x100 - (s8)BRIEF_MODE, DSP(START_DS_rgb_highlight), 1);
        DSB(START_DS_brief_mode_active) = BRIEF_MODE;
        brief_redraw(1);
    }
}
