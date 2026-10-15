#pragma once
/* The sim flow's test entry (simtest.c): f117 --sim-test DIR. */
int sim_selftest(const char *dir);
void sim_dump_state(const char *dir, const char *name);
void sim_test_render_mode(void);     /* --render-test: r3d_scene_draw runs in full inside the scenarios */
