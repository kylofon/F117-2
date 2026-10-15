/* Stand-ins for what VGAME calls and the port has not got yet (PORTING.md "TODO(S5.x)"): the sound driver (S5.9), the 3D scene (render3d, S5.6), and the player
 * flight model with its key fetch (flight, S5.7). The test harness (port/notes/s55/vgame_emu.py) makes the ORIGINAL functions at the same addresses return at once,
 * so the two sides stay comparable. */
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"
#include "sim/sim_stubs.h"
#include "gen/module_render3d.h"

/* ---- the sound driver, slots 101 / 103 / 104 of ASOUND.117 (sound.md "2. Driver interface") ---- */
int sim_snd_cmd(int cmd, int a, int b, int c) { (void)cmd; (void)a; (void)b; (void)c; return 0; }   /* TODO(S5.9): snd_cmd(cmd, ...) */
unsigned sim_poll_calls;                                                                            /* TEST: how often the sound poll ran */
int sim_poll_result;                                                                                /* TEST: what it returns */
int sim_snd_poll(void) { sim_poll_calls++; return sim_poll_result; }                                                                /* TODO(S5.9): snd_poll(); > 0 fast tick divisor, < 0 idle */
unsigned sim_noise_calls;                                                                           /* TEST */
void sim_snd_noise(void) { sim_noise_calls++; }                                                                        /* TODO(S5.9): snd_noise() */


/* ------------------------------------------------------------------------------------------------------------ stubs (see sim_stubs.h) */

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

/* ---- render3d (ported by S5.6b): the scene functions that the view code calls, under the names the sim code uses */
static u16 vp_off(void *vp) { return (u16)((u8 *)vp - DSP(0)); }
void sim_r3d_begin_scene(void *vp, int yaw, int pitch, int roll, int a, int b, int z, int flag, int fov) { r3d_begin_scene(vp_off(vp), (s16)yaw, (s16)pitch, (s16)roll, (s16)a, (s16)b, (s16)z, (s16)flag, (s16)fov); }
void sim_r3d_draw_terrain(int yaw, int pitch, s32 x, s32 y, s32 z) { r3d_draw_terrain((s16)yaw, (s16)pitch, x, y, z); }
void sim_r3d_place_object(unsigned type, int a1, int a2, int a3, int x, int y, int z) { r3d_place_object((u16)type, (s16)a1, (s16)a2, (s16)a3, (s16)x, (s16)y, (s16)z); }
void sim_r3d_end_scene(void) { r3d_end_scene(); }
void sim_r3d_draw_queued(void) { r3d_draw_queued(); }
void sim_fx_fire_puffs(int a0, int a1, int a2, int rx, int ry, int rz) { fx_fire_puffs((s16)a0, (s16)a1, (s16)a2, (s16)rx, (s16)ry, (s16)rz); }
void sim_fx_smoke_draw(int camx, int camy, int camz) { fx_smoke_draw((s16)camx, (s16)camy, (s16)camz); }

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
int sim_wpn_site_dist(int site) { return wpn_site_dist(site); }                                  /* 0000:BB86 (render3d/r3d_world.c until S5.7) */
unsigned sim_obj_wreck_shape(int o) { return obj_wreck_shape((s16)o); }                           /* 0000:BC12 (render3d) */
void sim_r3d_unit_terrain_class(int u) { r3d_unit_terrain_class((u16)u); }                       /* 0000:BCD7 (render3d) */
/* STUB 0000:bbed fm_bomb_dive_angle */
int sim_fm_bomb_dive_angle(void) { return 0; }
void sim_fx_explosion_sprite(int x, int y, int size, int frame) { fx_explosion_sprite((s16)x, (s16)y, (s16)size, (s16)frame); }   /* 0000:1A85 (render3d) */
