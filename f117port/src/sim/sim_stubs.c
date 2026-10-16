/* Stand-ins for what VGAME calls and the port has not got yet (PORTING.md "TODO(S5.x)"): the sound driver (S5.9); and the thin wrappers under which the sim
 * flow code calls render3d (S5.6) and the flight module (S5.7). */
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"
#include "sim/sim_stubs.h"
#include "sound/snd.h"
#include "gen/module_flight.h"
#include "gen/module_render3d.h"

/* ---- the sound driver, slots 101 / 103 / 104 of ASOUND.117 (sound.md "2. Driver interface"). In the sim tests (sim_test.active) the driver is a stand-in on
 * both sides (the ORIGINAL VGAME under Unicorn has none either): snd_cmd does nothing and snd_poll returns sim_poll_result. */
int sim_snd_cmd(int cmd, int a, int b, int c) { (void)b; (void)c; return sim_test.active ? 0 : snd_cmd(cmd, a); }
unsigned sim_poll_calls;                                                                            /* TEST: how often the sound poll ran */
int sim_poll_result;                                                                                /* TEST: what it returns */
int sim_snd_poll(void) { sim_poll_calls++; return sim_test.active ? sim_poll_result : snd_poll(); } /* > 0 fast tick divisor, < 0 idle */
unsigned sim_noise_calls;                                                                           /* TEST */
void sim_snd_noise(void) { sim_noise_calls++; if (!sim_test.active) snd_noise(); }


/* ------------------------------------------------------------------------------------------------------------ stubs (see sim_stubs.h) */

/* ---- render3d (ported by S5.6b): the scene functions that the view code calls, under the names the sim code uses */
static u16 vp_off(void *vp) { return (u16)((u8 *)vp - DSP(0)); }
void sim_r3d_begin_scene(void *vp, int yaw, int pitch, int roll, int a, int b, int z, int flag, int fov) { r3d_begin_scene(vp_off(vp), (s16)yaw, (s16)pitch, (s16)roll, (s16)a, (s16)b, (s16)z, (s16)flag, (s16)fov); }
void sim_r3d_draw_terrain(int yaw, int pitch, s32 x, s32 y, s32 z) { r3d_draw_terrain((s16)yaw, (s16)pitch, x, y, z); }
void sim_r3d_place_object(unsigned type, int a1, int a2, int a3, int x, int y, int z) { r3d_place_object((u16)type, (s16)a1, (s16)a2, (s16)a3, (s16)x, (s16)y, (s16)z); }
void sim_r3d_end_scene(void) { r3d_end_scene(); }
void sim_r3d_draw_queued(void) { r3d_draw_queued(); }
void sim_fx_fire_puffs(int a0, int a1, int a2, int rx, int ry, int rz) { fx_fire_puffs((s16)a0, (s16)a1, (s16)a2, (s16)rx, (s16)ry, (s16)rz); }
void sim_fx_smoke_draw(int camx, int camy, int camz) { fx_smoke_draw((s16)camx, (s16)camy, (s16)camz); }

/* ---- damage, weapons, flight (flight module, S5.7) and the render3d helpers of the overlay pass, under the names the sim code uses */
void sim_dmg_unit_kill(int u) { dmg_unit_kill(u); }                                              /* 0000:71B4 */
void sim_dmg_player_blast(void) { dmg_player_blast(); }                                          /* 0000:74F6 */
int sim_dmg_wobj_destroyable(int o) { return dmg_wobj_destroyable(o); }                          /* 0000:780A */
void sim_dmg_object_destroy(int o) { dmg_object_destroy(o); }                                    /* 0000:72A4 */
int sim_obj_find_at(int x, int y) { return obj_find_at(x, y); }                                  /* 0000:BAD1 */
int sim_wpn_suit(int wpn, int site) { return wpn_suit(wpn, site); }                              /* 0000:8BAB */
int sim_obj_cls_ship(int o) { return obj_cls_ship(o); }                                          /* 0000:BCAC */
int sim_wpn_site_dist(int site) { return wpn_site_dist(site); }                                  /* 0000:BB86 */
unsigned sim_obj_wreck_shape(int o) { return obj_wreck_shape((s16)o); }                           /* 0000:BC12 (render3d) */
void sim_r3d_unit_terrain_class(int u) { r3d_unit_terrain_class((u16)u); }                       /* 0000:BCD7 (render3d) */
int sim_fm_bomb_dive_angle(void) { return fm_bomb_dive_angle(); }                                /* 0000:BBED */
void sim_fx_explosion_sprite(int x, int y, int size, int frame) { fx_explosion_sprite((s16)x, (s16)y, (s16)size, (s16)frame); }   /* 0000:1A85 (render3d) */
