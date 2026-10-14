/* Stand-ins for what VGAME calls and the port has not got yet (PORTING.md "TODO(S5.x)"): the sound driver (S5.9), the 3D scene (render3d, S5.6), and the player
 * flight model with its key fetch (flight, S5.7). The test harness (port/notes/s55/vgame_emu.py) makes the ORIGINAL functions at the same addresses return at once,
 * so the two sides stay comparable. */
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"
#include "sim/sim_stubs.h"

/* ---- the sound driver, slots 101 / 103 / 104 of ASOUND.117 (sound.md "2. Driver interface") ---- */
int sim_snd_cmd(int cmd, int a, int b, int c) { (void)cmd; (void)a; (void)b; (void)c; return 0; }   /* TODO(S5.9): snd_cmd(cmd, ...) */
unsigned sim_poll_calls;                                                                            /* TEST: how often the sound poll ran */
int sim_poll_result;                                                                                /* TEST: what it returns */
int sim_snd_poll(void) { sim_poll_calls++; return sim_poll_result; }                                                                /* TODO(S5.9): snd_poll(); > 0 fast tick divisor, < 0 idle */
unsigned sim_noise_calls;                                                                           /* TEST */
void sim_snd_noise(void) { sim_noise_calls++; }                                                                        /* TODO(S5.9): snd_noise() */

/* 1359:0000 gfxlib_mode_init('M'): the renderer's row offset table DS:85EC (200 words, y * 320), the dispatch base [85C2] = 70h, the mode letter [85C4].
 * Only the MCGA case is used. Owner render3d (S5.6 takes it over). */
void gfxlib_mode_init(int mode)
{
    int c = mode & 0xFF;
    if (c >= 'a') c -= 0x20;
    DSW(0x85C4) = (u16)c;
    if (c != 'M') return;
    for (int i = 0; i < 200; i++) DSW((u16)(0x85EC + 2 * i)) = (u16)(i * 320);
    DSW(0x85C2) = 0x70;
}

/* ------------------------------------------------------------------------------------------------------------ stubs (see sim_stubs.h) */

/* STUB 0000:119c map_render_terrain */
void map_render_terrain(void *ctx, int px, int py, int shift) { (void)ctx; (void)px; (void)py; (void)shift; }   /* TODO(S5.6) */

/* STUB 0000:0d08 terr_find_scenery_at */
unsigned terr_find_scenery_at(long x, long y_inv) { (void)x; (void)y_inv; return 0; }              /* TODO(S5.7): flight.md 6.3 */
/* STUB 0000:0f31 terr_set_scenery_state */
void terr_set_scenery_state(unsigned h, unsigned newid) { (void)h; (void)newid; }                  /* TODO(S5.7): flight.md 6.4 */

/* STUB 0000:4f2a hud_threat_update */
void hud_threat_update(void) { }   /* TODO(S5.7) */
/* STUB 0000:5730 ai_update_units */
void ai_update_units(void) { }   /* TODO(S5.7) */
/* STUB 0000:671a wpn_proj_update_all */
void wpn_proj_update_all(void) { }   /* TODO(S5.7) */
/* STUB 0000:461c wpn_cm_tick */
void wpn_cm_tick(void) { }   /* TODO(S5.7) */
/* STUB 0000:465b wpn_gun_update */
void wpn_gun_update(void) { }   /* TODO(S5.7) */
/* STUB 0000:65d4 ai_unit_takeoff */
void ai_unit_takeoff(int u, int site) { (void)u; (void)site; }   /* TODO(S5.7) */
/* STUB 0000:7470 mission_target_done */
int mission_target_done(int j) { (void)j; return 0; }   /* TODO(S5.7) */
/* STUB 0000:753f wpn_release */
void wpn_release(void) { }   /* TODO(S5.7) */
/* STUB 0000:4512 wpn_release_countermeasure */
void wpn_release_countermeasure(int type) { (void)type; }   /* TODO(S5.7) */

/* ---- render3d (TODO(S5.6)): the scene functions that the view code calls */
/* STUB 0000:dae8 r3d_begin_scene */
void sim_r3d_begin_scene(void *vp, int yaw, int pitch, int roll, int a, int b, int z, int flag, int fov) { (void)vp; (void)yaw; (void)pitch; (void)roll; (void)a; (void)b; (void)z; (void)flag; (void)fov; }
/* STUB 0000:04da r3d_draw_terrain */
void sim_r3d_draw_terrain(int yaw, int pitch, s32 x, s32 y, s32 z) { (void)yaw; (void)pitch; (void)x; (void)y; (void)z; }
/* STUB 0000:e7ac r3d_place_object */
void sim_r3d_place_object(unsigned type, int a1, int a2, int a3, int x, int y, int z) { (void)type; (void)a1; (void)a2; (void)a3; (void)x; (void)y; (void)z; }
/* STUB 0000:db3e r3d_end_scene */
void sim_r3d_end_scene(void) { }
/* STUB 0000:df08 r3d_draw_queued */
void sim_r3d_draw_queued(void) { }
/* STUB 0000:c73c fx_fire_puffs */
void sim_fx_fire_puffs(int a0, int a1, int a2, int rx, int ry, int rz) { (void)a0; (void)a1; (void)a2; (void)rx; (void)ry; (void)rz; }
/* STUB 0000:c834 fx_smoke_draw */
void sim_fx_smoke_draw(int camx, int camy, int camz) { (void)camx; (void)camy; (void)camz; }

/* ---- damage, weapons, flight (TODO(S5.7)) and render3d (TODO(S5.6)) functions of the overlay pass */
/* STUB 0000:71b4 dmg_unit_kill */
void sim_dmg_unit_kill(int u) { (void)u; }
/* STUB 0000:74f6 dmg_player_blast */
void sim_dmg_player_blast(void) { }
/* STUB 0000:780a dmg_wobj_destroyable */
int sim_dmg_wobj_destroyable(int o) { (void)o; return 0; }
/* STUB 0000:72a4 dmg_object_destroy */
void sim_dmg_object_destroy(int o) { (void)o; }
/* STUB 0000:bad1 obj_find_at */
int sim_obj_find_at(int x, int y) { (void)x; (void)y; return 0; }
/* STUB 0000:8bab wpn_suit */
int sim_wpn_suit(int wpn, int site) { (void)wpn; (void)site; return 0; }
/* STUB 0000:bcac obj_cls_ship */
int sim_obj_cls_ship(int o) { (void)o; return 0; }
/* STUB 0000:bb86 wpn_site_dist */
int sim_wpn_site_dist(int site) { (void)site; return 0; }
/* STUB 0000:bc12 obj_wreck_shape */
unsigned sim_obj_wreck_shape(int o) { (void)o; return 0; }
/* STUB 0000:bcd7 r3d_unit_terrain_class */
void sim_r3d_unit_terrain_class(int u) { (void)u; }
/* STUB 0000:bbed fm_bomb_dive_angle */
int sim_fm_bomb_dive_angle(void) { return 0; }
/* STUB 0000:1a85 fx_explosion_sprite */
void sim_fx_explosion_sprite(int x, int y, int size, int frame) { (void)x; (void)y; (void)size; (void)frame; }
