/* The sim flow's test facilities (S5.5).
 *
 *  f117 --sim-test DIR --game-dir Game     (ctest `sim`; port/notes/s55/sim_ref.py writes DIR and reads the answers)
 *     DIR/comm.bin           the comm block START leaves (5120 bytes)
 *     DIR/startup_port.bin   (written) VGAME's DGROUP up to the end of its BSS, the pages and the DAC at the entry of sim_run, to compare with the same dump of
 *                            the ORIGINAL VGAME run under Unicorn (vgame_emu.py)
 *     DIR/<snap>.mem, .a000, .dac, .meta   snapshots of the whole machine (conventional memory, the screen page, the DAC, "dgroup SEG / mgdata SEG / mgcode SEG")
 *     DIR/scn.txt            scenarios (below); the answers go to DIR/scn_port.bin
 *     DIR/ticks.txt          (written) the ISR body's counters
 *  The game folder is only read.
 *
 * Scenario lines:  scn <id> <snap>  /  poke b|w|l OFF VALUE  /  pokes OFF HEXBYTES  /  call NAME [args]  /  end
 * Numbers are decimal or 0x hex; a pointer argument is a DS offset. The record written at `end`:
 *   u32 'SCN1', u16 hash of the return values (h = h * 31 + ax, 16 bits), u16 number of calls, u8 DGROUP[BSS_END], u32 crc of every row of pages 0 / 1 / 2 (200 each), u8 DAC[768], u8 comm[5120]. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prog.h"
#include "gen/module_render3d.h"
#include "sim/sim.h"
#include "sim/sim_priv.h"
#include "sim/simtest.h"

#define BSS_END 0xE55E

static u8 *slurp(const char *dir, const char *name, size_t *len)
{
    char path[2300];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) b[n] = 0;
    if (len) *len = (size_t)n;
    return b;
}

static u32 crc32(const u8 *p, size_t n)
{
    static u32 table[256];
    if (!table[1]) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void run_ticks(const char *dir);
static void run_ticks_hook(const char *dir) { run_ticks(dir); }

/* the dump of the state at the entry of sim_run (called from sim_run when sim_test.stop_at_run is set): DGROUP, page 0, page 1, page 2, the DAC */
void sim_dump_state(const char *dir, const char *name)
{
    if (sim_test.ticks) { run_ticks_hook(dir); return; }
    char path[2300];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(DSP(0), 1, BSS_END, f);
    fwrite(vga_page(0xA000), 1, 64000, f);
    fwrite(seg_ptr(DSW(0x85E4)), 1, 64000, f);
    fwrite(seg_ptr(DSW(0x85E6)), 1, 64000, f);
    u8 dac[768];
    dac_get(0, 256, dac);
    fwrite(dac, 1, 768, f);
    fclose(f);
}

/* ticks mode (DIR/ticks.req exists): the timer ISR body on the start-up state; the counters go to DIR/ticks.txt (checked by sim_ref.py) */
static void run_ticks(const char *dir)
{
    char path[2300];
    snprintf(path, sizeof path, "%s/ticks.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    DSW(0x2639) = 4;
    DSW(0x263B) = 17024;
    DSW(0x2636) = 1;
    DSW(0x2630) = DSW(0x2632) = DSW(0x263B);
    DSB(0x264B) = 0;
    sim_poll_calls = sim_noise_calls = 0;
    sim_poll_result = 0;
    for (int i = 0; i < 7000; i++) sim_tick_for_test();
    fprintf(f, "plain ticks %u polls %u noise %u tick_count %u tick_byte %u tick_count2 %u\n", 7000u, sim_poll_calls, sim_noise_calls, DSW(0x2646), DSB(0x264A), DSW(0x2648));
    for (int mode = 0; mode < 2; mode++) {
        sim_poll_result = mode == 0 ? 3 : -1;
        sim_poll_calls = sim_noise_calls = 0;
        for (int i = 0; i < 14; i++) sim_tick_for_test();
        fprintf(f, "poll%+d polls %u noise %u class %u clocks %u flag %u\n", sim_poll_result, sim_poll_calls, sim_noise_calls, DSW(0x2636), DSW(0x2630), DSB(0x2638));
    }
    fclose(f);
}

static int run_startup(const char *dir)
{
    size_t n;
    u8 *blob = slurp(dir, "comm.bin", &n);
    if (!blob || n != COMM_SIZE) { printf("sim-test: comm.bin missing in %s\n", dir); return 1; }
    prog_comm_init();
    memcpy(comm_get(), blob, COMM_SIZE);
    free(blob);
    comm_get()->seg_offscreen = (u16)gfx_alloc(2);
    comm_get()->seg_mgraphic = comm_get()->seg_sound = comm_get()->seg_misc = 0;
    sim_test.ticks = false;
    { char tp[2300]; snprintf(tp, sizeof tp, "%s/ticks.req", dir); FILE *tf = fopen(tp, "rb"); if (tf) { fclose(tf); sim_test.ticks = true; } }
    if (rd16(0, COMM_INIT_DONE) == 0) { screen_off(); mode13_set(); wr16(0, COMM_INIT_DONE, 1); }
    sim_test.active = true;
    sim_test.dump_dir = dir;
    sim_test.stop_at_run = true;
    int code = platform_exec(PROG_VGAME, vgame_main, "");
    sim_test.stop_at_run = false;
    printf("sim-test: startup run ended with code %02Xh\n", code);
    return code == 0x7F ? 0 : 1;
}

/* ------------------------------------------------------------------------------------------------------- function calls */

typedef long (*CallFn)(const long *a);
typedef struct { const char *name; CallFn fn; bool ret; } CallDef;

#define A(i) (a[i])
#define DSPTR(i) DSP((u16)a[i])
#define DSTR(i) ((char *)DSP((u16)a[i]))
/* a wrapper of a function returning void / a value */
#define WV(n, expr) static long w_##n(const long *a) { (void)a; expr; return 0; }
#define WR(n, expr) static long w_##n(const long *a) { (void)a; return (long)(expr); }

/* ---- library */
WR(fx_mul15, fx_mul15((int)A(0), (int)A(1)))
WR(fx_sin16, fx_sin16((int)A(0)))
WR(fx_cos16, fx_cos16((int)A(0)))
WR(fx_clamp, fx_clamp((int)A(0), (int)A(1), (int)A(2)))
WR(fx_clamp_wrap, fx_clamp_wrap((int)A(0), (int)A(1), (int)A(2)))
WR(fx_dist, fx_dist((int)A(0), (int)A(1)))
WR(fx_heading, fx_heading((int)A(0), (int)A(1)))
WR(fx_sin_mul, fx_sin_mul((u16)A(0), (int)A(1)))
WR(fx_cos_mul, fx_cos_mul((u16)A(0), (int)A(1)))
WR(fx_sext8, fx_sext8((int)A(0)))
WR(fx_isqrt, fx_isqrt((int)A(0)))
WR(fx_asin, fx_asin((int)A(0)))
WR(fx_acos, fx_acos((int)A(0)))
WR(fx_ratio_q15, fx_ratio_q15((int)A(0), (int)A(1)))
WR(lib_scale_eighths, lib_scale_eighths((int)A(0), (int)A(1)))
WR(lib_sign, lib_sign((int)A(0)))
WR(pal_fade_step, pal_fade_step(DSPTR(0), DSPTR(1), (int)A(2)))
/* ---- gfx2d (segment 1001) */
WV(line_clip_far, line_clip_far())
WV(fill_rows_far, fill_rows_far(DSPTR(0), (int)A(1), (int)A(2), (int)A(3), (int)A(4)))
WV(tables_clear, tables_clear())
WV(edge_to_tables, edge_to_tables())
WV(poly_flush_far, poly_flush_far((int)A(0)))
WV(poly_fill_far, poly_fill_far(DSPTR(0), (int)A(1), (const s16 *)DSPTR(2), (int)A(3)))
/* ---- HUD primitives */
WV(hud_lamp_recolor, hud_lamp_recolor(DSPTR(0), (int)A(1), (int)A(2)))
WV(hud_lamp_set, hud_lamp_set((int)A(0), (int)A(1)))
WV(hud_lamp_update, hud_lamp_update((int)A(0), (int)A(1)))
WV(hud_lamp_timers, hud_lamp_timers())
WV(hud_digits_draw, hud_digits_draw((int)A(0), (int)A(1), DSTR(2)))
WV(hud_number_draw, hud_number_draw((int)A(0), (int)A(1)))
WV(hud_draw_fuel, hud_draw_fuel())
WV(hud_draw_throttle, hud_draw_throttle())
WV(hud_draw_store_count, hud_draw_store_count((int)A(0)))
WV(hud_draw_store_counts, hud_draw_store_counts())
WV(gfx_set_fg, gfx_set_fg((int)A(0)))
WV(hud_fill_rect, hud_fill_rect((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(gfx_restore_rect, gfx_restore_rect((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(mfd_fill, mfd_fill((int)A(0), (int)A(1)))
WV(hud_line_clip, hud_line_clip((int)A(0), (int)A(1), (int)A(2), (int)A(3), (int)A(4), (int)A(5), (int)A(6), (int)A(7), (int)A(8)))
WV(hud_line_full, hud_line_full((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(hud_line_view, hud_line_view((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(hud_line_win, hud_line_win((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(hud_box, hud_box((int)A(0), (int)A(1), (int)A(2), (int)A(3), (int)A(4)))
WV(hud_pixel, hud_pixel((int)A(0), (int)A(1), (int)A(2)))
WV(txt_draw, txt_draw(DSPTR(0), DSTR(1), (int)A(2), (int)A(3), (int)A(4)))
WV(hud_text, hud_text(DSTR(0), (int)A(1), (int)A(2), (int)A(3)))
WV(hud_num_left, hud_num_left((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(hud_num_right, hud_num_right((unsigned)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(msg_post, msg_post(DSTR(0)))
WV(mfd_text, mfd_text((int)A(0), (int)A(1), (int)A(2), DSTR(3), (int)A(4)))
WV(hud_side_strips, hud_side_strips((int)A(0)))
WV(pal_flight_load, pal_flight_load())
WV(pal_save, pal_save())
WV(pal_restore, pal_restore())
WV(hud_color_init, hud_color_init())
WV(hud_layout_init, hud_layout_init())

/* ---- cockpit and MFDs */
WV(cockpit_keyblit, cockpit_keyblit((int)A(0), (int)A(1), (int)A(2), (int)A(3), (int)A(4), (int)A(5), (int)A(6)))
WV(mfd_icon, mfd_icon((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(cockpit_refresh_panels, cockpit_refresh_panels())
WR(mfd_map_sx, mfd_map_sx((int)A(0)))
WR(mfd_map_sy, mfd_map_sy((int)A(0)))
WR(mfd_map_plot, mfd_map_plot((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
static long w_mfd_map_to_screen(const long *a)                          /* the results go to the words at the DS offsets A(2), A(3) */
{
    int sx = 0, sy = 0;
    int r = mfd_map_to_screen((int)a[0], (int)a[1], &sx, &sy);
    DSW((u16)a[2]) = (u16)sx;
    DSW((u16)a[3]) = (u16)sy;
    return r;
}
WV(mfd_map_line, mfd_map_line((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(mfd_map_arc, mfd_map_arc((int)A(0), (int)A(1), (int)A(2), (int)A(3), (int)A(4), (int)A(5), (int)A(6)))
WV(mfd_map_route, mfd_map_route())
WV(mfd_map_save_bg, mfd_map_save_bg())
WV(mfd_map_restore_bg, mfd_map_restore_bg())
WV(mfd_map_redraw, mfd_map_redraw((int)A(0), (int)A(1)))
WV(mfd_zoom_in, mfd_zoom_in())
WV(mfd_zoom_out, mfd_zoom_out())
WV(mfd_init_geometry, mfd_init_geometry())
WV(hud_threat_bar, hud_threat_bar((int)A(0), (int)A(1), (int)A(2), (int)A(3)))
WV(mfd_left_select, mfd_left_select((int)A(0)))
WV(mfd_right_select, mfd_right_select((int)A(0)))
WV(mfd_tv_label, mfd_tv_label())
WV(mfd_refresh, mfd_refresh((int)A(0)))
WV(mfd_fuel_bar, mfd_fuel_bar((int)A(0), (int)A(1)))
WV(fmt_2digit, fmt_2digit((int)A(0)))
WV(fmt_clock_hhmm, fmt_clock_hhmm((unsigned)A(0)))
WV(fmt_clock_hhmmss, fmt_clock_hhmmss((unsigned)A(0)))
WV(site_name, site_name((int)A(0)))
WV(msg_site_label, msg_site_label((int)A(0)))
WV(nav_copy_xy, nav_copy_xy((int)A(0), (int)A(1)))
WV(mfd_nav_page, mfd_nav_page())
WV(mfd_weapons_page, mfd_weapons_page())
WV(mfd_objectives_page, mfd_objectives_page())
WV(hud_ils, hud_ils())

/* ---- the step, keys and small pieces */
WV(sim_frame_step, sim_frame_step())
WV(sim_key_and_signature, sim_key_and_signature())
WV(input_command_key, input_command_key((unsigned)A(0)))
WV(snd_cue, snd_cue((int)A(0), (int)A(1)))
WV(snd_engine_refresh, snd_engine_refresh())
WV(frame_params_update, frame_params_update())
WV(frame_rate_halve, frame_rate_halve())
WV(view_range_setup, view_range_setup())
WV(hud_glideslope_update, hud_glideslope_update())
WV(fx_smoke_update, fx_smoke_update())
WV(fx_fall_update, fx_fall_update())
WV(mission_cheat_rearm, mission_cheat_rearm())
WV(cockpit_widgets_reset, cockpit_widgets_reset())
WV(mission_name_targets, mission_name_targets())
WV(sim_reset_clock_palette, sim_reset_clock_palette())
WV(pal_time_of_day_step, pal_time_of_day_step())
WV(mission_end, mission_end((int)A(0)))
WV(mission_log_event, mission_log_event((int)A(0), (int)A(1)))
WV(mission_world_export, mission_world_export())
WV(cockpit_daynight_recolor, cockpit_daynight_recolor())
WR(input_button, input_button((int)A(0)))

/* ---- the HUD overlay and the scope */
WV(hud_overlay_frame, hud_overlay_frame())
WV(mfd_scope_draw, mfd_scope_draw((int)A(0)))
WV(mfd_w2s, mfd_w2s((int)A(0), (int)A(1)))
WV(mfd_target_box, mfd_target_box((int)A(0), (int)A(1), (int)A(2)))
WV(hud_draw_front, hud_draw_front())
WV(cockpit_draw_adi, cockpit_draw_adi())
WV(view_frame, view_frame())
WV(r3d_project_point, r3d_project_point((int)A(0), (int)A(1), (int)A(2)))
WV(hud_target_box, hud_target_box((int)A(0), (int)A(1), (int)A(2), (int)A(3), (int)A(4)))
WV(hud_draw_diamond, hud_draw_diamond())
WV(hud_update_aspect_label, hud_update_aspect_label())
WV(hud_seeker_cross, hud_seeker_cross((int)A(0)))
WV(hud_lock_bracket, hud_lock_bracket((int)A(0)))
WV(hud_lock_indicator, hud_lock_indicator((int)A(0), (int)A(1)))
WV(hud_ping_label, hud_ping_label(ds_str((u16)A(0)), (int)A(1), (int)A(2)))
WV(hud_format_range, hud_format_range((int)A(0)))
WV(obj_overlay_frame, obj_overlay_frame())
WV(mfd_view_sky_bands, mfd_view_sky_bands((int)A(0)))
WV(mfd_view_ground_bands, mfd_view_ground_bands((int)A(0), (int)A(1)))
WV(mfd_view_flir_tint, mfd_view_flir_tint((int)A(0), (int)A(1)))
WV(mfd_view_render, mfd_view_render((unsigned)A(0), (int)A(1), (int)A(2), (int)A(3), (int)A(4), (int)A(5), (int)A(6), (int)A(7), (int)A(8)))

/* ---- render3d (S5.6b, --render-test): 32-bit arguments come as two words, lo first */
#define L32(i) ((s32)(((u32)(u16)A(i + 1) << 16) | (u16)A(i)))
WV(r3d_scene_draw, r3d_scene_draw((s16)A(0), (s16)A(1), (s16)A(2), L32(3), L32(5), L32(7), (s16)A(9), (s16)A(10), (s16)A(11), (s16)A(12)))
WV(r3d_draw_world_objects, r3d_draw_world_objects())
WV(map_render_terrain, map_render_terrain(DSP((u16)A(0)), (s16)A(1), (s16)A(2), (s16)A(3)))
WV(terr_probe_cell, terr_probe_cell((s16)A(0), (s16)A(1), (s16)A(2)))

#define E(n) { #n, w_##n, false }          /* a function without a result worth comparing */
#define ER(n) { #n, w_##n, true }          /* the result goes into the hash */
static const CallDef calls[] = {
    ER(fx_mul15), ER(fx_sin16), ER(fx_cos16), ER(fx_clamp), ER(fx_clamp_wrap), ER(fx_dist), ER(fx_heading), ER(fx_sin_mul), ER(fx_cos_mul), ER(fx_sext8), ER(fx_isqrt),
    ER(fx_asin), ER(fx_acos), ER(fx_ratio_q15), ER(lib_scale_eighths), ER(lib_sign), ER(pal_fade_step),
    E(line_clip_far), E(fill_rows_far), E(tables_clear), E(edge_to_tables), E(poly_flush_far), E(poly_fill_far),
    E(hud_lamp_recolor), E(hud_lamp_set), E(hud_lamp_update), E(hud_lamp_timers), E(hud_digits_draw), E(hud_number_draw), E(hud_draw_fuel), E(hud_draw_throttle),
    E(hud_draw_store_count), E(hud_draw_store_counts), E(gfx_set_fg), E(hud_fill_rect), E(gfx_restore_rect), E(mfd_fill), E(hud_line_clip), E(hud_line_full),
    E(hud_line_view), E(hud_line_win), E(hud_box), E(hud_pixel), E(txt_draw), E(hud_text), E(hud_num_left), E(hud_num_right), E(msg_post), E(mfd_text),
    E(cockpit_keyblit), E(mfd_icon), E(cockpit_refresh_panels), E(mfd_map_line), E(mfd_map_arc), E(mfd_map_route), E(mfd_map_save_bg), E(mfd_map_restore_bg), E(mfd_map_redraw), E(mfd_zoom_in), E(mfd_zoom_out), E(mfd_init_geometry), E(hud_threat_bar), E(mfd_left_select), E(mfd_right_select), E(mfd_tv_label), E(mfd_refresh), E(mfd_fuel_bar), E(fmt_2digit), E(fmt_clock_hhmm), E(fmt_clock_hhmmss), E(site_name), E(msg_site_label), E(nav_copy_xy), E(mfd_nav_page), E(mfd_weapons_page), E(mfd_objectives_page), E(hud_ils),
    ER(mfd_map_sx), ER(mfd_map_sy), ER(mfd_map_plot), ER(mfd_map_to_screen),
    E(sim_frame_step), E(sim_key_and_signature), E(input_command_key), E(snd_cue), E(snd_engine_refresh), E(frame_params_update), E(frame_rate_halve), E(view_range_setup), E(hud_glideslope_update), E(fx_smoke_update), E(fx_fall_update), E(mission_cheat_rearm), E(cockpit_widgets_reset), E(mission_name_targets), E(sim_reset_clock_palette), E(pal_time_of_day_step), E(mission_end), E(mission_log_event), E(mission_world_export), E(cockpit_daynight_recolor), ER(input_button),
    E(hud_overlay_frame), E(mfd_scope_draw), E(mfd_w2s), E(mfd_target_box), E(hud_draw_front), E(cockpit_draw_adi), E(view_frame), E(r3d_project_point), E(hud_target_box), E(hud_draw_diamond), E(hud_update_aspect_label), E(hud_seeker_cross), E(hud_lock_bracket), E(hud_lock_indicator), E(hud_ping_label), E(hud_format_range), E(obj_overlay_frame), E(mfd_view_sky_bands), E(mfd_view_ground_bands), E(mfd_view_flir_tint), E(mfd_view_render),
    E(r3d_scene_draw), E(r3d_draw_world_objects), E(map_render_terrain), E(terr_probe_cell),
    E(hud_side_strips), E(pal_flight_load), E(pal_save), E(pal_restore), E(hud_color_init), E(hud_layout_init),
};

static const CallDef *find_call(const char *name)
{
    for (size_t i = 0; i < sizeof calls / sizeof calls[0]; i++)
        if (!strcmp(calls[i].name, name)) return &calls[i];
    return NULL;
}

/* ---- snapshots */

typedef struct Snap { char name[32]; u8 *mem; u8 *a000; u8 *dac; u16 dgroup, mgdata, mgcode; } Snap;
static Snap snaps[8];
static int nsnaps;

static Snap *get_snap(const char *dir, const char *name)
{
    for (int i = 0; i < nsnaps; i++) if (!strcmp(snaps[i].name, name)) return &snaps[i];
    if (nsnaps >= 8) return NULL;
    Snap *s = &snaps[nsnaps];
    char fn[80];
    size_t n;
    snprintf(s->name, sizeof s->name, "%s", name);
    snprintf(fn, sizeof fn, "%s.mem", name); s->mem = slurp(dir, fn, &n);
    snprintf(fn, sizeof fn, "%s.a000", name); s->a000 = slurp(dir, fn, &n);
    snprintf(fn, sizeof fn, "%s.dac", name); s->dac = slurp(dir, fn, &n);
    snprintf(fn, sizeof fn, "%s.meta", name);
    u8 *meta = slurp(dir, fn, &n);
    if (!s->mem || !s->a000 || !s->dac || !meta) return NULL;
    unsigned d, m1, m2;
    if (sscanf((char *)meta, "dgroup %x mgdata %x mgcode %x", &d, &m1, &m2) != 3) return NULL;
    s->dgroup = (u16)d; s->mgdata = (u16)m1; s->mgcode = (u16)m2;
    free(meta);
    nsnaps++;
    return s;
}

static void restore_snap(const Snap *s)
{
    memcpy(mem, s->mem, 0xA0000);
    memcpy(vga_page(0xA000), s->a000, 64000);
    dac_set(0, 256, s->dac);
    cur_dgroup = s->dgroup;
    plat_prog = PROG_VGAME;
    video_load_driver_state(s->mgdata, s->mgcode);
    timer_bios_frozen = true;
    bool render = sim_test.render;
    memset(&sim_test, 0, sizeof sim_test);
    sim_test.render = render;
    sim_test.active = true;
    sim_test.frames_left = -1;
}

static long parse_num(const char *t) { return strtol(t, NULL, 0); }

static char cur_id[64];

static int run_scenarios(const char *dir)
{
    size_t n;
    char *txt = (char *)slurp(dir, "scn.txt", &n);
    if (!txt) { printf("sim-test: scn.txt missing\n"); return 1; }
    char path[2300];
    snprintf(path, sizeof path, "%s/scn_port.bin", dir);
    FILE *out = fopen(path, "wb");
    if (!out) return 1;
    int count = 0, bad = 0;
    u32 hash = 0, ncalls = 0;
    u32 magic = 0x314E4353;
    static u8 rec[BSS_END];
    for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *tok[16];
        int nt = 0;
        char *save = NULL;
        for (char *t = strtok_r(line, " \t", &save); t && nt < 16; t = strtok_r(NULL, " \t", &save)) tok[nt++] = t;
        if (nt == 0 || tok[0][0] == '#') continue;
        if (!strcmp(tok[0], "scn") && nt >= 3) {
            Snap *s = get_snap(dir, tok[2]);
            if (!s) { printf("sim-test: snapshot %s missing\n", tok[2]); bad++; continue; }
            restore_snap(s);
            hash = 0;
            ncalls = 0;
            snprintf(cur_id, sizeof cur_id, "%s", tok[1]);
        } else if (!strcmp(tok[0], "poke") && nt >= 4) {
            long off = parse_num(tok[2]), v = parse_num(tok[3]);
            if (tok[1][0] == 'b') DSB(off) = (u8)v;
            else if (tok[1][0] == 'w') DSW(off) = (u16)v;
            else DSL(off) = (u32)v;
        } else if (!strcmp(tok[0], "pokes") && nt >= 3) {
            long off = parse_num(tok[1]);
            for (const char *h = tok[2]; h[0] && h[1]; h += 2, off++) {
                char b2[3] = { h[0], h[1], 0 };
                DSB(off) = (u8)strtoul(b2, NULL, 16);
            }
        } else if (!strcmp(tok[0], "call") && nt >= 2) {
            const CallDef *c = find_call(tok[1]);
            if (!c) { printf("sim-test: no wrapper for %s\n", tok[1]); bad++; continue; }
            long a[12] = { 0 };
            for (int i = 2; i < nt && i < 14; i++) a[i - 2] = parse_num(tok[i]);
            long r = c->fn(a);
            if (getenv("F117_SIM_TRACE") && !strcmp(getenv("F117_SIM_TRACE"), cur_id)) printf("T %u %u %s %ld %ld %ld %ld\n", ncalls, (u32)r & 0xFFFF, c->name, a[0], a[1], a[2], a[3]);
            if (c->ret) hash = (hash * 31 + ((u32)r & 0xFFFF)) & 0xFFFF;
            ncalls++;
        } else if (!strcmp(tok[0], "end")) {
            fwrite(&magic, 4, 1, out);
            u16 r16[2] = { (u16)hash, (u16)ncalls };
            fwrite(r16, 2, 2, out);
            memcpy(rec, DSP(0), BSS_END);
            fwrite(rec, 1, BSS_END, out);
            const u8 *pg[3] = { vga_page(0xA000), seg_ptr(DSW(0x85E4)), seg_ptr(DSW(0x85E6)) };
            for (int p = 0; p < 3; p++)
                for (int y = 0; y < 200; y++) { u32 c = crc32(pg[p] + 320 * y, 320); fwrite(&c, 4, 1, out); }
            u8 dac[768];
            dac_get(0, 256, dac);
            fwrite(dac, 1, 768, out);
            fwrite(comm_get(), 1, COMM_SIZE, out);
            const char *dump = getenv("F117_SIM_DUMP");               /* debugging: the pages of scenario <id> in full */
            if (dump && !strcmp(dump, cur_id)) {
                for (int p = 0; p < 3; p++) {
                    char fn[2400];
                    snprintf(fn, sizeof fn, "%s/dump_page%d.bin", dir, p);
                    FILE *df = fopen(fn, "wb");
                    if (df) { fwrite(pg[p], 1, 64000, df); fclose(df); }
                }
            }
            count++;
        }
    }
    fclose(out);
    free(txt);
    printf("sim-test: %d scenarios run%s\n", count, bad ? " (with errors)" : "");
    return bad ? 1 : 0;
}

void sim_test_render_mode(void) { sim_test.render = true; }

int sim_selftest(const char *dir)
{
    size_t n;
    char *t = (char *)slurp(dir, "scn.txt", &n);
    if (t) { free(t); return run_scenarios(dir); }
    return run_startup(dir);
}
