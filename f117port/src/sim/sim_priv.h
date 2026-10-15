#pragma once
/* Declarations private to the sim flow module that the generated headers do not have: the routines of other owners that the HUD code needs before their module
 * is ported (render3d's 2D edge machinery, segment 1001), the stubs of the modules not ported yet, and a few port helpers. */
#include "sim/sim.h"

void video_set_mode(int mode);                       /* frontend/player.c */
void placeholder_note_vgame_pass(void);              /* programs/placeholder.c: the stand-in END counts the passes */

/* ---- 1001: the 2D edge tables and the clipped line / fill (owner render3d, render3d/edge2d.c; the HUD draws through it) ---- */
void fill_rows_far(void *vp, int xl, int ymin, int xr, int ymax);            /* 1001:024C */
void line_clip_far(void);                                                   /* 1001:032A: the segment is in DS:2299 (x1) 229B (x2) 229D (y1) 229F (y2) */
bool line_clip_draw(void);                                                  /* 1001:0336: true = rejected (CF) */
void tables_clear(void);                                                    /* 1001:0525 */
void poly_fill_far(void *vp, int n, const s16 *pts, int colour);            /* 1001:01EC */
void poly_flush_far(int mode);                                              /* 1001:0504 */
void edge_to_tables(void);                                                  /* 1001:06AC */
void gfxlib_mode_init(int mode);
void view_range_setup(void);                                                /* 0000:D76B (owner render3d, trivial, ported early) */                                            /* 1359:0000: the row offset table for the renderer */

/* ---- stubs of the modules that are not ported yet (sim_stubs.c) ---- */
void r3d_project_point(int x_u, int y_u, int z);                  /* 0000:B85D, render3d's (S5.6) */
void r3d_scene_draw(int yaw, int pitch, int roll, s32 x, s32 y, s32 z, int x0, int y0, int w, int h); /* 0000:03CE (render3d/r3d_scene.c) */
void sim_matrix_from_angles(u16 m_off, int a, int b, int c);   /* 0000:E26B, render3d's (S5.6) */
void r3d_draw_sky_ground(void *ctx);                              /* 0000:1442 (render3d/r3d_sky.c) */
void sim_input_flight(void);                         /* 0000:1E9E: TODO(S5.7) the player flight model; here only its key fetch and first-call set-up */
void timer_install_sim(void);
void timer_remove_sim(void);
void vgame_start(void);                              /* 0000:0010 */
