#pragma once
/* The functions of the modules that are not ported yet and that the sim flow code calls (sim_stubs.c). Each is an empty function marked
 * `STUB ssss:oooo name` there; the test harness makes the original at that address return 0 at once, so both sides stay comparable.
 * Signatures are what the callers pass (cdecl, words), not final: render3d (S5.6) and flight, weapons, AI, detection, damage (S5.7) replace them. */
#include "types.h"

/* ---- render3d (ported, S5.6b): the names the sim code calls */
void sim_r3d_begin_scene(void *vp, int yaw, int pitch, int roll, int a, int b, int z, int flag, int fov);   /* 0000:DAE8 */
void sim_r3d_draw_terrain(int yaw, int pitch, s32 x, s32 y, s32 z);                                          /* 0000:04DA */
void sim_r3d_place_object(unsigned type, int a1, int a2, int a3, int x, int y, int z);                       /* 0000:E7AC */
void sim_r3d_end_scene(void);                                                                                /* 0000:DB3E */
void sim_r3d_draw_queued(void);                                                                              /* 0000:DF08 */
void sim_fx_fire_puffs(int a0, int a1, int a2, int rx, int ry, int rz);                                      /* 0000:C73C */
void sim_fx_smoke_draw(int camx, int camy, int camz);                                                        /* 0000:C834 */
/* ---- weapons / damage / flight / render3d: what the overlay pass of the 3D scene calls (TODO(S5.6) / TODO(S5.7)) */
void sim_dmg_unit_kill(int u);                          /* 0000:71B4 */
void sim_dmg_player_blast(void);                        /* 0000:74F6 */
int  sim_dmg_wobj_destroyable(int o);                   /* 0000:780A */
void sim_dmg_object_destroy(int o);                     /* 0000:72A4 */
int  sim_obj_find_at(int x, int y);                     /* 0000:BAD1 */
int  sim_wpn_suit(int wpn, int site);                   /* 0000:8BAB */
int  sim_obj_cls_ship(int o);                           /* 0000:BCAC */
int  sim_wpn_site_dist(int site);                       /* 0000:BB86 */
unsigned sim_obj_wreck_shape(int o);                    /* 0000:BC12 */
void sim_r3d_unit_terrain_class(int u);                 /* 0000:BCD7 */
int  sim_fm_bomb_dive_angle(void);                      /* 0000:BBED */
void sim_fx_explosion_sprite(int x, int y, int size, int frame);   /* 0000:1A85 */
