/* The pilot-orders screen (ordrscrn, spec shell.md Part C section 4: START 0000:5A8A .. 60DC) and the mission-orders pages (the folder,
 * 20D6 with its text helpers 24F7, 25D9, 2651). Hotspot 13 and 15 (not 13 and 14, as the spec has it) are the Air mission types that the
 * Lockheed model disables: the asm steps the table pointer by 16 bytes (two entries). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell/shell.h"

#define HOVER_PREV DSB(START_DS_ui_hover_prev)

/* ------------------------------------------------------------------------------------------------ the orders screen */

/* START 0000:60DC ui_theatre_name_draw: the chosen theatre's name on the chart */
void ui_theatre_name_draw(void)
{
    TextCtx *c1 = tctx(SH_CTX1);
    char *name = ds_str_ptr(START_DS_tbl_theatre_names, PW(sh_pilot(), PIL_THEATRE));
    blit_rect(3, 0xD4, 0x79, 1, 0xD4, 0x79, 0x46, 0xB);
    c1->font = 4;
    c1->fg = 8;
    ui_text(c1, name, 0xD4, 0x7C);
    blit_rect(1, 0xD4, 0x79, 0, 0xD4, 0x79, 0x46, 0xB);
}

/* START 0000:5A8A ui_orders_draw: spec shell.md "5A8A" -- hook of the orders screen */
void ui_orders_draw(void)
{
    u8 *p = sh_pilot();
    Comm *comm = comm_get();
    ui_load_screen_pic(10, 4);
    blit_rect(1, 0, 0, 3, 0, 0, 320, 200);
    HOVER_PREV = 0;
    DSB(START_DS_ui_tooltip_frozen) = 0;
    memcpy(DSP((u16)(START_DS_pal_buffer + 3 * DSB((u16)(START_DS_theatre_colour_tbl + PW(p, PIL_THEATRE))))), DSP(START_DS_rgb_highlight), 3);
    if (PW(p, PIL_MODEL) != 0) {
        memcpy(DSP(0x67BC), DSP(START_DS_rgb_disabled), 3);
        memcpy(DSP(0x67B6), DSP(0x67BC), 3);
    }
    u16 e = 0x6FD2;
    for (int i = 0; i < 2; i++, e += 0x10) {                 /* entries 13 and 15 of the table (the step is 16 bytes) */
        if (PW(p, PIL_MODEL) == 0) ui_order_asc((unsigned *)DSP(e)); else ui_order_desc((unsigned *)DSP(e));
    }
    if (comm->clearance == 0) {
        memcpy(DSP(0x67B9), DSP(START_DS_rgb_disabled), 3);
        memcpy(DSP(0x67BC), DSP(0x67B9), 3);
        ui_order_desc((unsigned *)DSP(0x6FD2));
        ui_order_desc((unsigned *)DSP(0x6FDA));
    }
    int war = PW(p, PIL_WAR), mt = PW(p, PIL_MTYPE);
    ui_sprite_draw(tctx(SH_CTX0), DSW((u16)(START_DS_ui_marker_x + 2 * war)), DSW((u16)(START_DS_ui_marker_y + 2 * war)), DSW(START_DS_ui_sprite_seg));
    ui_sprite_draw(tctx(SH_CTX1), DSW((u16)(START_DS_ui_marker_x + 2 * war)), DSW((u16)(START_DS_ui_marker_y + 2 * war)), DSW(START_DS_ui_sprite_seg));
    ui_sprite_draw(tctx(SH_CTX0), DSW((u16)(START_DS_ui_marker_x + 6 + 2 * mt)), DSW((u16)(START_DS_ui_marker_y + 6 + 2 * mt)), DSW(START_DS_ui_sprite_seg));
    ui_sprite_draw(tctx(SH_CTX1), DSW((u16)(START_DS_ui_marker_x + 6 + 2 * mt)), DSW((u16)(START_DS_ui_marker_y + 6 + 2 * mt)), DSW(START_DS_ui_sprite_seg));
    ui_theatre_name_draw();
}

/* START 0000:5FFA ui_tooltip_clear: the tooltip panel from the sprite sheet, unless the highlight animation is running */
void ui_tooltip_clear(void)
{
    if (ui_anim_active() == 0) {
        blit_rect(4, 0xBA, 0x68, 1, 0, 0x68, 0x86, 0x60);
        blit_rect(4, 0xBA, 0x68, 0, 0, 0x68, 0x86, 0x60);
    }
}

/* START 0000:6045 ui_tooltip_show: the first call starts the panel animation, later calls print the text into the panel */
void ui_tooltip_show(const char *text)
{
    if (DSB(START_DS_ui_tooltip_frozen) == 0) {
        ui_anim_start(4, 0x10E0, 0x10C0, 9);
        return;
    }
    TextCtx *c1 = tctx(SH_CTX1);
    blit_rect(4, 0xBA, 0x68, 1, 0, 0x68, 0x86, 0x60);
    c1->font = 4;
    c1->bg = -1;
    c1->fg = 0;
    ui_text_wrap(c1, (char *)text, 0xF, 0x78, 0x6E, 8, 0);
    blit_rect(1, 0, 0x68, 0, 0, 0x68, 0x86, 0x61);
}

/* the tooltip text of hotspot h: theatre description, war level or mission type text; NULL for none */
static char *tooltip_text(int h)
{
    if (h <= 9) return ds_str(DSW((u16)(0x9B29 + 2 * DSB((u16)(0x0DE2 + h)))));
    if (h <= 0xC) return ds_str(DSW((u16)(0xA16D + 2 * h)));
    if (h <= 0x10) return ds_str(DSW((u16)(0x954A + 2 * h)));
    return NULL;
}

/* START 0000:5E6F ui_orders_hover: spec shell.md "5E6F" -- idle callback of the orders menu */
void ui_orders_hover(void)
{
    u8 *p = sh_pilot();
    u8 hover = (u8)ui_hit_test(DSP(0x6F72), DSB(0x6F71));
    if (HOVER_PREV != 0 && HOVER_PREV != hover) {
        bool idle = false;
        if (HOVER_PREV <= 9) {
            if (DSB((u16)(0x0DE2 + HOVER_PREV)) != PW(p, PIL_THEATRE)) pal_queue_push(0x100 - HOVER_PREV, DSP(START_DS_rgb_white), 1);
        } else {
            idle = true;
        }
        if (idle) pal_queue_push(0x100 - HOVER_PREV, DSP(START_DS_pal_rgb_idle), 1);
        if (hover == 0 || hover == 0x11) ui_tooltip_clear();
    }
    if (hover != 0 && HOVER_PREV != hover) {
        bool red = false;
        if (hover <= 9) {
            if (DSB((u16)(0x0DE2 + hover)) != PW(p, PIL_THEATRE)) red = true;
            ui_tooltip_show(tooltip_text(hover));
        } else if (hover <= 0xC) {
            red = true;
            ui_tooltip_show(tooltip_text(hover));
        } else if (hover <= 0x10) {
            red = true;
            ui_tooltip_show(tooltip_text(hover));
        } else {
            red = true;
        }
        if (red) pal_queue_push(0x100 - hover, DSP(START_DS_rgb_red2), 1);
    }
    HOVER_PREV = hover;
    if (ui_anim_active() && ui_anim_step() == 0) {
        DSB(START_DS_ui_tooltip_frozen) = 1;
        if (hover != 0 && hover <= 0x10) ui_tooltip_show(tooltip_text(hover));
    }
}

/* START 0000:5BF0 ui_orders_screen: spec shell.md "5BF0"; returns 0 */
int ui_orders_screen(void)
{
    u8 *p = sh_pilot();
    Comm *comm = comm_get();
    ui_show_screen(6, ui_orders_draw);
    for (;;) {
        int sel = ui_menu_loop(DSP(START_DS_ui_orders_menu));
        if (DSB(START_DS_mission_loaded) != 0 && sel < 0x11) {
            if (ui_dialog_run(DSP(START_DS_dlg_change_orders), 0x56, 0x46) == 2) sel = 0;
            else DSB(START_DS_mission_loaded) = 0;
        }
        if (sel == 0x63) { ui_cursor_erase(); return 0; }
        if (sel == 17) {                                         /* Fly: save the roster, flash the panel, back to the ready room */
            ui_cursor_erase();
            roster_save(0);
            if (DSB(START_DS_ui_tooltip_frozen)) ui_anim_start(4, 0x10E0, 0x10D2, 7);
            for (int i = 0; i < 0x3E; i++) {
                pal_apply_queue();
                blit_rect(2, 0xF1 + i, 0xA7, 0, 0x102 + i, 0xBC, 1, 12);
                ui_anim_step();
            }
            ui_wait_ticks(0x1E);
            return 0;
        }
        if (sel >= 1 && sel <= 9) {                              /* a theatre; the others need the security clearance */
            if (sel != 4 && comm->clearance == 0) { ui_dialog_run(DSP(START_DS_dlg_clearance), 0x46, 0x3C); continue; }
            int th = DSB((u16)(0x0DE2 + sel));
            if (th != (s16)PW(p, PIL_THEATRE)) {
                pal_queue_push(DSB((u16)(START_DS_theatre_colour_tbl + PW(p, PIL_THEATRE))), DSP(START_DS_rgb_white), 1);
                PW(p, PIL_THEATRE) = (u16)th;
                pal_queue_push(DSB((u16)(START_DS_theatre_colour_tbl + PW(p, PIL_THEATRE))), DSP(START_DS_rgb_highlight), 1);
                ui_theatre_name_draw();
            }
            continue;
        }
        int old_marker = sel - 10;           /* PORT: when the choice is unchanged the original erases a marker index left on the stack; here the same marker */
        if (sel >= 10 && sel <= 12) {                            /* war level */
            if ((s16)(PW(p, PIL_WAR) - sel) != -10) { old_marker = PW(p, PIL_WAR); PW(p, PIL_WAR) = (u16)(sel - 10); }
        } else if (sel >= 13 && sel <= 16) {                     /* mission type */
            if ((s16)(PW(p, PIL_MTYPE) - sel) != -13) { old_marker = PW(p, PIL_MTYPE) + 3; PW(p, PIL_MTYPE) = (u16)(sel - 13); }
        } else {
            continue;
        }
        int ox = DSW((u16)(START_DS_ui_marker_x + 2 * old_marker)), oy = DSW((u16)(START_DS_ui_marker_y + 2 * old_marker));
        blit_rect(3, ox, oy, 1, ox, oy, 12, 8);
        blit_rect(3, ox, oy, 0, ox, oy, 12, 8);
        int nx = DSW((u16)(START_DS_ui_marker_x + 2 * (sel - 10))), ny = DSW((u16)(START_DS_ui_marker_y + 2 * (sel - 10)));
        sh_snd(0x1B);
        ui_sprite_draw(tctx(SH_CTX1), nx, ny, DSW(START_DS_ui_sprite_seg));
        ui_sprite_draw(tctx(SH_CTX0), nx, ny, DSW(START_DS_ui_sprite_seg));
    }
}

/* ----------------------------------------------------------------------------------------- the mission-orders pages */

/* START 0000:25D9 brief_format_date: "Apr 15, 1986" for the pilot's theatre and the days flown */
char *brief_format_date(char *buf)
{
    u8 *p = sh_pilot();
    int t = PW(p, PIL_THEATRE);
    int month = DSB((u16)(START_DS_date_start_month + t));
    int year = (s16)DSW((u16)(START_DS_date_year + 2 * t));
    int day = (s16)(DSB((u16)(START_DS_date_start_day + t)) + PW(p, 0x36));
    while ((u16)DSB((u16)(START_DS_days_in_month + month)) < (u16)day) {
        day -= DSB((u16)(START_DS_days_in_month + month));
        month++;
    }
    if ((u16)month > 12) { month -= 12; year++; }
    sprintf(buf, "%s %d, %d", ds_str(DSW((u16)(START_DS_month_names + 2 * month))), day, year);
    return buf;
}

/* START 0000:2651 brief_format_objective: spec shell.md "2651" -- the objective sentence of mission slot `rec` */
void brief_format_objective(void *recv, char *out)
{
    u8 *rec = recv;
    u16 tgt = SITE(PW(rec, 2));
    u16 place = DSW(tgt) ? DSW((u16)(START_DS_wld_loc_names + 2 * DSW(tgt))) : 0;
    u16 obj = (u16)(START_DS_objective_tbl + 12 * PW(rec, 8));
    u8 kind = DSB(obj);
    char tmp[0x52];
    u16 loc = START_DS_wld_loc_names;
    switch (kind) {
    case 1: case 2: {
        const char *verb = ds_str(kind == 1 ? 0x8CF : 0x8DA);
        if (DSW(tgt)) sprintf(out, ds_str(0x8E2), verb, ds_str(DSW((u16)(loc + 2 * DSB((u16)(tgt + 0xE))))), ds_str(place));
        else sprintf(out, ds_str(0x8F2), verb, ds_str(DSW((u16)(loc + 2 * DSB((u16)(tgt + 0xE))))));
        if (DSB((u16)(obj + 4)) & 0x20) sprintf(tmp, ds_str(0x8FC), ds_str(START_DS_brief_time_tot));
        else if (DSB((u16)(obj + 4)) & 2) sprintf(tmp, ds_str(0x937), ds_str(START_DS_brief_time_tot));
        else {
            size_t n = strlen(out);
            if (out[n - 1] == ',') out[n - 1] = 0;
            strcpy(tmp, ds_str(0x949));
        }
        strcat(out, tmp);
        break;
    }
    case 3:
        sprintf(out, ds_str(0x94B), (char *)rec + 0xA, ds_str(START_DS_brief_time_tot));
        strcat(out, ds_str(0x98D));
        break;
    case 4:
        sprintf(out, ds_str(0x9BD), (char *)rec + 0xA, ds_str(START_DS_brief_time_tot));
        strcat(out, ds_str(0x9F2));
        break;
    case 5:
        sprintf(out, ds_str(0xA29), ds_str(DSW((u16)(loc + 2 * DSW(SITE(DSW(START_DS_mission_special_site)))))), ds_str(START_DS_brief_time_launch),
                ds_str(place), ds_str(START_DS_brief_time_tot));
        break;
    case 7:
        sprintf(out, ds_str(0xA89), ds_str(place), ds_str(START_DS_brief_time_launch));
        break;
    case 6:
        sprintf(out, ds_str(0xAE7), ds_str(START_DS_brief_grid_special), ds_str(START_DS_brief_time_launch), ds_str(place), ds_str(START_DS_brief_time_tot));
        break;
    case 8:
        strcpy(tmp, ds_str((s16)DSW((u16)(obj + 6)) == -2 ? 0xB6D : 0xB7D));
        sprintf(out, ds_str(0xB7E), ds_str((u16)(START_DS_tbl_fighter_types + (DSW(0xD3D4) << 5))), tmp, (char *)rec + 0xA);
        break;
    default: break;
    }
}

/* START 0000:24F7 brief_draw_target_summary: the intelligence summary of mission slot `which` on the orders page */
void brief_draw_target_summary(void *c, int which)
{
    TextCtx *t = c;
    u16 rec = (u16)(START_DS_objective_rec + 0x12 * which);
    char buf[0x100];
    int y;
    t->font = 3;
    t->fg = 0;
    if (which == 0) { y = 0x4E; ui_text(t, ds_str(0x7F0), 0x26, y); }
    else { y = 0x8C; ui_text(t, ds_str(0x815), 0x26, y); }
    ui_text_wrap(t, ds_str(DSW((u16)(START_DS_objective_text_tbl + 2 * DSW(rec + 8)))), 0x26, y + 8, 0x104, 7, 0);
    y = t->y + 2;
    t->fg = 4;
    ui_text(t, ds_str(0x83C), 0x26, y);                          /* "OBJECTIVE:" */
    t->fg = 0;
    brief_format_objective(DSP(rec), buf);
    ui_text_wrap(t, buf, 0x26, y + 8, 0x104, 6, 0);
}

/* START 0000:20D6 brief_orders_screen: spec shell.md "20D6" -- the folder: orders, intelligence, flight plan, rules, armament */
void brief_orders_screen(void)
{
    u8 *p = sh_pilot();
    Comm *comm = comm_get();
    char buf[0x62];
    TextCtx *c = tctx(0xDC5A);                                     /* the context of page 4 */
    ui_load_screen_pic(3, 4);
    c->bg = -1;
    u16 start = SITE(DSW(START_DS_mission_start_site));
    u16 idx = SW(start, 0) ? SW(start, 0) : DSB((u16)(start + 0xE));
    char *base = ds_str(DSW((u16)(START_DS_wld_loc_names + 2 * idx)));
    c->font = 4;
    c->fg = 0;
    ui_text_centered(c, ds_str(0x6D2), 0xA8, 0x25);
    static const u16 titles[4] = { 0x6F2, 0x70C, 0x729, 0x73D };
    ui_text_centered(c, ds_str(titles[PW(p, PIL_MTYPE) & 3]), 0xA8, 0x2D);
    c->font = 3;
    snprintf(buf, sizeof buf, "Pilot: %s%s.", ds_str_ptr(START_DS_tbl_rank_names, PW(p, PIL_RANK)),
             ds_str((u16)(START_DS_roster_names + PILOT_SIZE * PW(p, 0))));
    ui_text_centered(c, buf, 0xA8, 0x36);
    snprintf(buf, sizeof buf, "%s - ", base);
    brief_format_date(buf + strlen(buf));
    ui_text(c, buf, 0x26, 0x43);
    brief_draw_target_summary(c, 0);
    brief_draw_target_summary(c, 1);
    pal_fade_out(1);
    pal_select(3);
    blit_rect(4, 0, 0, 0, 0, 0, 320, 200);
    pal_fade_in(1);
    if (ui_wait_click() != 2) {
        ui_load_screen_pic(9, 4);
        blit_rect(0, 0x26, 0xBA, 4, 0x26, 0, 0x104, 0xE);
        ui_text(c, ds_str(0x76E), 0x26, 0x12);                   /* "FLIGHT PLAN:" */
        snprintf(buf, sizeof buf, "Depart from %s at %s hours.", base, ds_str(START_DS_brief_time_launch));
        ui_text(c, buf, 0x26, 0x1E);
        ui_text_wrap(c, ds_str(DSW(0xA361)), 0x26, 0x2A, 0x104, 8, 0);
        c->fg = 0;
        c->bg = -1;
        u16 land = SITE(DSW(START_DS_mission_land_site));
        u16 lidx = SW(start, 0) ? SW(land, 0) : DSB((u16)(land + 0xE));
        char *base2 = ds_str(DSW((u16)(START_DS_wld_loc_names + 2 * lidx)));
        snprintf(buf, sizeof buf, "Land at %s.", base2);
        ui_text(c, buf, 0x26, 0x46);
        ui_text(c, ds_str(0x7A3), 0x26, 0x56);                   /* "RULES OF ENGAGEMENT:" */
        ui_text_wrap(c, ds_str(DSW((u16)(0x9781 + 2 * PW(p, PIL_WAR)))), 0x26, 0x5E, 0x104, 8, 0);
        ui_text(c, ds_str(0x7B8), 0x26, 0x8C);                   /* "SUGGESTED ARMAMENT:" */
        ui_text(c, ds_str(DSW((u16)(0xA409 + 2 * (s8)DSB(0xCAD2)))), 0x40, 0x94);
        ui_text(c, ds_str(DSW((u16)(0xA409 + 2 * (s8)DSB(0xCAD3)))), 0xB4, 0x94);
        if (PW(p, PIL_MODEL) == 0) {
            ui_text(c, ds_str(DSW((u16)(0xA409 + 2 * (s8)DSB(0xCAD4)))), 0x40, 0x9C);
            ui_text(c, ds_str(DSW((u16)(0xA409 + 2 * (s8)DSB(0xCAD5)))), 0xB4, 0x9C);
        }
        snprintf(buf, sizeof buf, "Est. Fuel required: %d lbs.", (s16)comm->fuel_lbs);
        ui_text(c, buf, 0x26, 0xA8);
        pal_fade_out(1);
        blit_rect(4, 0, 0, 0, 0, 0, 320, 200);
        pal_fade_in(1);
        ui_wait_click();
    }
    pal_fade_out(1);
    ui_load_screen_pic(PW(p, PIL_THEATRE) + 0x10, 4);
    blit_rect(1, 0, 0, 0, 0, 0, 320, 200);
    pal_select(2);
    memcpy(DSP((u16)(0x67E3 - 3 * (s8)DSB(START_DS_brief_mode))), DSP(START_DS_rgb_highlight), 3);
    HOVER_PREV = 0;
    pal_fade_in(1);
    ui_cursor_set_pos(0x110, 0x86);
}
