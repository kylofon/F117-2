#pragma once
/* The sim flow's test entry (simtest.c): f117 --sim-test DIR. */
int sim_selftest(const char *dir);
void sim_dump_state(const char *dir, const char *name);
