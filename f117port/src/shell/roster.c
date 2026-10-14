/* The roster (spec shell.md Part D section 1 and 2): ROSTER.FIL load and save, the roster screen with its hover info panel, name entry,
 * and the pilot options screen. START 0000:615C .. 7272. START is the only program that writes ROSTER.FIL (a quit in END discards the
 * mission's changes). The record array is DS:D892 (10 x 50h bytes), the active pilot is the far record at DS:CACA (= comm+1272h). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell/shell.h"

#define HOVER_PREV DSB(START_DS_ui_hover_prev)
#define RGB_ROW 0x11CD                                   /* DS:11CD (0A,12,0F): the colour of a roster row and the idle options button */

static inline u8 *roster_rec(int i) { return DSP(ROSTER_PILOT(i)); }
static inline char *rank_str(u16 r) { return ds_str_ptr(START_DS_tbl_rank_names, r); }

/* ------------------------------------------------------------------------------------------------------ the file */

/* START 0000:804E fcb_find: the DOS volume label search for the original disk ("F117A-SF"); 0 means the original floppy is in the drive.
 * PORT: the game never runs from the floppy, the roster is always writable. */
int fcb_find(void) { return 0xFF; }

/* START 0000:7218 roster_load: "Roster.Fil" = the selected index (word) and 10 records of 50h bytes. PORT: a missing or short file leaves zeros
 * (the original does not check the open). */
void roster_load(void)
{
    FILE *f = fopen_fn(ds_str(0xEBF), "rb");
    if (!f) return;
    fread_fn(DSP(START_DS_roster_selected), 2, 1, f);
    for (int i = 0; i < 10; i++) fread_fn(roster_rec(i), PILOT_SIZE, 1, f);
    fclose(f);
}

/* START 0000:7272 roster_save: spec shell.md "1.8" */
void roster_save(int show_err)
{
    DSB(START_DS_roster_selected) = *sh_pilot();
    if (fcb_find() != 0) {
        FILE *f = fopen_fn(ds_str(0xECD), "wb");
        if (f) {
            fwrite(DSP(START_DS_roster_selected), 2, 1, f);
            for (int i = 0; i < 10; i++) fwrite(roster_rec(i), PILOT_SIZE, 1, f);
            fclose(f);
        } else if (show_err) {
            ui_dialog_run(DSP(START_DS_dlg_write_protect), 0x56, 0x46);
        }
    } else if (show_err) {
        ui_dialog_run(DSP(START_DS_dlg_orig_disk), 0x56, 0x46);
    }
}

/* START 0000:6D07 roster_startup_sync: spec shell.md "1.7" */
void roster_startup_sync(void)
{
    roster_load();
    for (int i = 0; i < 10; i++) {
        u8 *rec = roster_rec(i);
        DSW(0xD8D4) &= 1;                                        /* the original masks pilot 0's model word every time (a bug kept) */
        if (PW(rec, PIL_MODEL) != 0 && (PW(rec, PIL_MTYPE) == 0 || PW(rec, PIL_MTYPE) == 2)) PW(rec, PIL_MTYPE)++;
    }
    u8 *p = sh_pilot();
    if (rd16(0, 0x4F2) != 0) memcpy(p, roster_rec(DSB(START_DS_roster_selected)), PILOT_SIZE);
    else memcpy(roster_rec(PW(p, 0)), p, PILOT_SIZE);
    if (comm_get()->clearance == 0) { PW(p, PIL_THEATRE) = 0; PW(p, PIL_MTYPE) |= 2; }
    roster_save(0);
}

/* ------------------------------------------------------------------------------------------------------- the rows */

/* START 0000:6861 roster_draw_row: spec shell.md "1.6" -- one pilot's row into page 1 (the original draws it twice) */
void roster_draw_row(int slot, int y)
{
    TextCtx *c1 = tctx(SH_CTX1);
    u8 *rec = roster_rec(slot);
    char buf[0x52];
    blit_rect(3, 0x1E, y, 1, 0x1E, y, 0x102, 8);
    snprintf(buf, sizeof buf, "%s%s", rank_str(PW(rec, PIL_RANK)), (char *)rec + PIL_NAME);
    ui_text(c1, buf, 0x1E, y);
    snprintf(buf, sizeof buf, "%ld", (long)(s32)PD(rec, 0x32));
    ui_text(c1, buf, 0xE0 - ui_text_width(c1, buf), y);
    snprintf(buf, sizeof buf, "%d", (s16)PW(rec, 0x36));
    ui_text(c1, buf, 0xF6 - ui_text_width(c1, buf), y);
    strcpy(buf, ds_str(DSW((u16)(START_DS_status_names + 2 * PW(rec, PIL_STATUS)))));
    int w = ui_text_width(c1, buf);
    int v = 0x23 - w;
    ui_text(c1, buf, ((v - (v < 0 ? -1 : 0)) >> 1) + 0xFD, y);
}

/* START 0000:615C roster_screen_draw: spec shell.md "5. Roster screen callback" -- hook of the roster screen */
void roster_screen_draw(void)
{
    TextCtx *c1 = tctx(SH_CTX1);
    u8 *p = sh_pilot();
    ui_load_screen_pic(12, 4);
    blit_rect(1, 0, 0, 3, 0, 0, 320, 200);
    c1->font = 1;
    c1->fg = 0xFF;
    for (int i = 0, y = 0x2C; i < 10; i++, y += 10) {
        roster_draw_row(i, y);
        c1->fg--;
    }
    c1->font = 3;
    c1->fg = 0x1A;
    ui_text_centered(c1, ds_str(0xE1B), 0xA0, 0x90);            /* "Press 'Delete' key to erase a pilot" */
    memcpy(DSP((u16)(0x67E0 - 3 * PW(p, 0))), DSP(START_DS_rgb_highlight), 3);
    blit_rect(1, 0x1E, 0x2C, 0, 0x1E, 0x2C, 0x10E, 0x6E);
    ui_cursor_set_pos(0xA0, 10 * PW(p, 0) + 0x2F);
}

/* START 0000:64E5 roster_draw_info: spec shell.md "1.4" -- the info panel of the pilot under the cursor */
void roster_draw_info(int slot)
{
    TextCtx *c1 = tctx(SH_CTX1);
    u8 *rec = roster_rec(slot);
    char buf[0x28];
    c1->font = 4;
    c1->fg = 1;
    snprintf(buf, sizeof buf, "%s%s", rank_str(PW(rec, PIL_RANK)), ui_str_last_word((char *)rec + PIL_NAME));
    buf[0x18] = 0;
    ui_text(c1, buf, 0x32, 0x9F);
    c1->fg = 0x1C;
    snprintf(buf, sizeof buf, "Best Mission: %d", (s16)PW(rec, 0x2E));
    ui_text(c1, buf, 0x32, 0xA8);
    snprintf(buf, sizeof buf, "Last Mission: %d", (s16)PW(rec, 0x30));
    ui_text(c1, buf, 0x32, 0xB0);
    ui_text(c1, ds_str_ptr(START_DS_opponent_names, PW(rec, PIL_SKILL)), 0xA2, 0xA0);
    ui_text(c1, ds_str_ptr(START_DS_landing_names, PW(rec, PIL_CRASH)), 0xA2, 0xA8);
    ui_text(c1, ds_str_ptr(START_DS_tbl_flight_model_names, PW(rec, PIL_MODEL)), 0xA2, 0xB0);
    static const struct { u16 off, fmt, sy; } medals[6] = {
        { 0x2C, 0xE6D, 0x98 }, { 0x26, 0xE74, 0xA0 }, { 0x2A, 0xE7B, 0xA8 }, { 0x28, 0xE82, 0xB0 }, { 0x24, 0xE88, 0xB8 }, { 0x22, 0xE8E, 0xC0 },
    };
    int x = 0x24;
    for (int i = 0; i < 6; i++) {
        if (PW(rec, medals[i].off) == 0) continue;
        blit_rect(4, 0, medals[i].sy, 1, x, 0xBF, 0x22, 8);
        snprintf(buf, sizeof buf, ds_str(medals[i].fmt), (s16)PW(rec, medals[i].off));
        ui_text_centered(c1, buf, x + 0x11, 0xB8);
        x += 0x26;
    }
    blit_rect(1, 0x24, 0x9F, 0, 0x24, 0x9F, 0xE0, 0x29);
}

/* START 0000:63FB roster_hover_cb: spec shell.md "1.3" */
void roster_hover_cb(void)
{
    u8 *p = sh_pilot();
    u8 cur = (u8)ui_hit_test(DSP(0x7052), DSB(0x7051));
    u8 prev = HOVER_PREV;
    if (prev != 0 && cur != prev) {
        if ((s16)(prev - PW(p, 0)) != 1) pal_queue_push(0x100 - prev, DSP(RGB_ROW), 1);
        if (prev <= 10) {
            blit_rect(3, 0x24, 0x9F, 1, 0x24, 0x9F, 0xE0, 0x29);
            blit_rect(3, 0x24, 0x9F, 0, 0x24, 0x9F, 0xE0, 0x29);
        }
    }
    if (cur != 0 && cur != prev) {
        if ((s16)(cur - PW(p, 0)) != 1) pal_queue_push(0x100 - cur, DSP(START_DS_rgb_red2), 1);
        if (cur <= 10) roster_draw_info(cur - 1);
    }
    HOVER_PREV = cur;
}

/* START 0000:6AB3 roster_edit_name: spec shell.md "1.5" -- erase the entry and type a new pilot ("2nd Lt " + name); returns 0 */
int roster_edit_name(int slot)
{
    TextCtx *c1 = tctx(SH_CTX1);
    u8 *rec = roster_rec(slot);
    u8 *active = sh_pilot();
    char buf[0x30];
    int y = 10 * slot + 0x2C;
    ui_cursor_erase();
    c1->fg = 0xFF - slot;
    c1->font = 1;
    blit_rect(3, 0x1E, y, 1, 0x1E, y, 0x9E, 8);
    strcpy(buf, rank_str(0));
    ui_text(c1, buf, 0x1E, y);
    text_noclip(c1, ds_str(0xE9E));                              /* the "_" cursor after the text */
    int len0 = (int)strlen(buf), len = len0;
    blit_rect(1, 0x1E, y, 0, 0x1E, y, 0x9E, 8);
    for (;;) {
        u8 key;
        if (len0 - len == -29 || ui_text_width(c1, buf) > 0x96) {
            key = 0x0D;
        } else {
            while (kbd_empty_misc() != 0) host_pump();
            key = (u8)kbd_get_misc();
        }
        bool done = false;
        if (key == 0x1B) {
            done = true;
        } else if (key == 8) {
            if (len > len0) { len--; buf[len] = 0; }
        } else if (key == 0x0D) {
            pal_queue_push(0xFF - PW(active, 0), DSP(RGB_ROW), 1);
            pal_queue_push(0xFF - slot, DSP(START_DS_rgb_highlight), 1);
            memset(rec, 0, PILOT_SIZE);
            strcpy((char *)rec + PIL_NAME, buf + len0);
            PW(rec, 0) = (u16)slot;
            PW(rec, PIL_WAR) = 2;
            PW(rec, PIL_MTYPE) = 3;
            PW(rec, PIL_STATUS) = 0;
            memcpy(active, rec, PILOT_SIZE);
            done = true;
        } else if (isprint((s8)key) && (s8)key >= 0) {
            buf[len++] = (char)key;
            buf[len] = 0;
        }
        if (done) break;
        blit_rect(3, 0x1E, y, 1, 0x1E, y, 0x9E, 8);
        ui_text(c1, buf, 0x1E, y);
        text_noclip(c1, ds_str(0xEA0));
        blit_rect(1, 0x1E, y, 0, 0x1E, y, 0x9E, 8);
    }
    roster_draw_row(slot, y);
    blit_rect(1, 0x1E, y, 0, 0x1E, y, 0x10E, 8);
    return 0;
}

/* --------------------------------------------------------------------------------------------- the options screen */

/* START 0000:71AD roster_options_hover_cb: spec shell.md "2.2" */
void roster_options_hover_cb(void)
{
    u8 cur = (u8)ui_hit_test(DSP(0x6FFE), DSB(0x6FFD));
    u8 prev = HOVER_PREV;
    if (prev != 0 && cur != prev) pal_queue_push(0xF5 - prev, DSP(START_DS_pal_rgb_idle), 1);
    if (cur != 0 && cur != prev) pal_queue_push(0xF5 - cur, DSP(START_DS_rgb_red2), 1);
    HOVER_PREV = cur;
}

/* START 0000:6DB8 roster_options_screen: spec shell.md "2.1" -- opponent level, landing realism and aircraft model of the chosen pilot */
void roster_options_screen(void)
{
    u8 *p = sh_pilot();
    TextCtx *c0 = tctx(SH_CTX0), *c1 = tctx(SH_CTX1);
    ui_cursor_erase();
    u8 saved_hover = HOVER_PREV;
    HOVER_PREV = 0;
    int save_x = (s16)DSW(START_DS_ui_cursor_x), save_y = (s16)DSW(START_DS_cursor_y);
    DSW(START_DS_ui_cursor_x) = 0x103;
    DSW(START_DS_cursor_y) = 0xB4;
    pal_queue_push(0xEB, DSP(0x67A4), 10);
    blit_rect(1, 0xC0, 0x28, 4, 0x6F, 0, 0x6F, 0x98);
    blit_rect(4, 0, 0, 1, 0xC0, 0x28, 0x6F, 0x98);
    unsigned marker = DSW(START_DS_ui_sprite_seg);
    ui_sprite_draw(c1, 0xC0 + DSB((u16)(START_DS_opt_marker_x + PW(p, PIL_SKILL) + 1)), 0x28 + DSB((u16)(START_DS_opt_marker_y + PW(p, PIL_SKILL) + 1)), marker);
    ui_sprite_draw(c1, 0xC0 + DSB((u16)(START_DS_opt_marker_x + PW(p, PIL_CRASH) + 5)), 0x28 + DSB((u16)(START_DS_opt_marker_y + PW(p, PIL_CRASH) + 5)), marker);
    ui_sprite_draw(c1, 0xC0 + DSB((u16)(START_DS_opt_marker_x + PW(p, PIL_MODEL) + 8)), 0x28 + DSB((u16)(START_DS_opt_marker_y + PW(p, PIL_MODEL) + 8)), marker);
    blit_rect(1, 0xC0, 0x28, 0, 0xC0, 0x28, 0x6F, 0x98);
    char buf[0x3C], tmp[0x40];
    snprintf(buf, sizeof buf, "%s%s", rank_str(PW(p, PIL_RANK)), ui_str_last_word(ds_str((u16)(START_DS_roster_names + PILOT_SIZE * PW(p, 0)))));
    c1->font = 3;
    c0->font = 3;
    c1->fg = 0x1C;
    c0->fg = 0x1C;
    memset(tmp, 0, sizeof tmp);
    for (int i = 0; i < 0x32; ) {
        ui_wait_ticks(ui_rand_mod(6) + 6);
        if (buf[i] == 0) break;
        tmp[i] = buf[i];
        tmp[i + 1] = 0;
        if (ui_text_width(c0, tmp) > 0x52) break;
        sh_snd(0x21);
        ui_text(c0, tmp, 0xD9, 0x37);
        ui_text(c1, tmp, 0xD9, 0x37);
        i++;
    }
    int r;
    bool done = false;
    do {
        r = ui_menu_loop(DSP(START_DS_opt_menu));
        int old = 0;
        if (r == 0x63) { done = true; }
        else if (r >= 1 && r <= 4) { old = PW(p, PIL_SKILL) + 1; PW(p, PIL_SKILL) = (u16)(r - 1); }
        else if (r >= 5 && r <= 7) { old = PW(p, PIL_CRASH) + 5; PW(p, PIL_CRASH) = (u16)(r - 5); }
        else if (r == 8 || r == 9) {
            old = PW(p, PIL_MODEL) + 8;
            PW(p, PIL_MODEL) = (u16)(r - 8);
            if (PW(p, PIL_MODEL) != 0 && (PW(p, PIL_MTYPE) == 0 || PW(p, PIL_MTYPE) == 2)) PW(p, PIL_MTYPE)++;
        } else if (r == 10) { done = true; }
        if (r < 10 && r >= 1) {
            int ox = DSB((u16)(START_DS_opt_marker_x + old)), oy = DSB((u16)(START_DS_opt_marker_y + old));
            blit_rect(4, ox, oy, 1, ox + 0xC0, oy + 0x28, 12, 8);
            blit_rect(4, ox, oy, 0, ox + 0xC0, oy + 0x28, 12, 8);
            int nx = DSB((u16)(START_DS_opt_marker_x + r)) + 0xC0, ny = DSB((u16)(START_DS_opt_marker_y + r)) + 0x28;
            sh_snd(0x1B);
            ui_sprite_draw(c1, nx, ny, marker);
            ui_sprite_draw(c0, nx, ny, marker);
        }
    } while (!done);
    blit_rect(4, 0x6F, 0, 1, 0xC0, 0x28, 0x6F, 0x98);
    blit_rect(4, 0x6F, 0, 0, 0xC0, 0x28, 0x6F, 0x98);
    HOVER_PREV = saved_hover;
    if (r == 0x63) {
        memcpy(p, roster_rec(PW(p, 0)), PILOT_SIZE);              /* cancel: the edits are dropped */
        DSW(START_DS_ui_cursor_x) = (u16)save_x;
        DSW(START_DS_cursor_y) = (u16)save_y;
    } else {
        memcpy(roster_rec(PW(p, 0)), p, PILOT_SIZE);
        DSW(START_DS_ui_cursor_x) = 0x129;
        DSW(START_DS_cursor_y) = 0xB6;
    }
}

/* ------------------------------------------------------------------------------------------------- the screen */

/* START 0000:623E roster_screen_run: spec shell.md "1.1"; returns 0 (the ready room) */
int roster_screen_run(void)
{
    u8 *p = sh_pilot();
    wr16(0, 0x4F2, 0);
    ui_show_screen(7, roster_screen_draw);
    for (;;) {
        int r = ui_menu_loop(DSP(START_DS_roster_menu));
        if (r < 1) continue;
        if (r <= 10) {                                           /* a pilot row */
            u16 st = PW(roster_rec(r - 1), PIL_STATUS);
            if (st == 0) {
                if (DSB(START_DS_mission_loaded) && ui_dialog_run(DSP(START_DS_dlg_change_pilot), 0x56, 0x46) == 2) continue;
                DSB(START_DS_mission_loaded) = 0;
                pal_queue_push(0xFF - PW(p, 0), DSP(RGB_ROW), 1);
                memcpy(roster_rec(PW(p, 0)), p, PILOT_SIZE);
                memcpy(p, roster_rec(r - 1), PILOT_SIZE);
                PW(p, 0) = (u16)(r - 1);
                if (comm_get()->clearance == 0) { PW(p, PIL_THEATRE) = 0; PW(p, PIL_MTYPE) |= 2; }
                pal_queue_push(0x100 - r, DSP(START_DS_rgb_highlight), 1);
                roster_options_screen();
            } else if (st == 1) {
                ui_dialog_run(DSP(START_DS_dlg_retired), 0x56, 0x46);
            } else if (st == 2) {
                ui_dialog_run(DSP(START_DS_dlg_deceased), 0x56, 0x46);
            }
            continue;
        }
        if (r == 0x162) {                                        /* Delete: replace the entry under the cursor */
            int i = ui_hit_test(DSP(0x7052), DSB(0x7051));
            if (i == 0 || i > 10) continue;
            if (DSB(START_DS_mission_loaded) && ui_dialog_run(DSP(START_DS_dlg_change_pilot), 0x56, 0x46) == 2) continue;
            roster_edit_name(i - 1);
            DSB(START_DS_mission_loaded) = 0;
            continue;
        }
        if (r == 11 || r == 0x63) {                              /* Done / Esc */
            ui_cursor_erase();
            u16 st = PW(p, PIL_STATUS);
            if (st != 0) {
                if (st == 1) ui_dialog_run(DSP(START_DS_dlg_retired), 0x56, 0x46);
                else if (st == 2) ui_dialog_run(DSP(START_DS_dlg_deceased), 0x56, 0x46);
                continue;
            }
            DSB(START_DS_pilot_model_copy_b) = (u8)PW(p, PIL_MODEL);
            DSB(START_DS_arm_unavail) = (u8)PW(p, PIL_MODEL);
            roster_save(1);
            return 0;
        }
    }
}

/* START 0000:9694 isprint_fn / 96A1 ctype_test: the MSC ctype for ASCII (the table holds only ASCII classes) */
int isprint_fn(int c) { return c >= 0x20 && c < 0x7F; }
