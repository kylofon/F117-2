#pragma once
/* The timer tick (platform.md §4, PLAN.md Decisions 2026-10-13).
 *
 * The original reprograms the PIT to run its ISR several times per video frame and divides that down so that
 * one "tick" is one vertical retrace. The port drops the PIT: the host delivers exactly one tick per retrace
 * (70.0866 Hz in mode 13h, host.h) and timer_isr() runs on the main thread from host_pump().
 *
 * What a tick does is program-specific and belongs to S5.1 / S5.9: the frame counter `[50CC]`, the sound
 * poll with its dropped 7th tick, and the `[3660]` frame-rate estimator. This skeleton only provides the
 * tick source: a free-running counter and a hook the ported ISR body is installed into. */
#include "types.h"

void timer_init(void);                           /* installs timer_isr as the host tick handler */
extern volatile u32 timer_ticks;                 /* ticks since timer_init */

/* The per-program ISR body (S5): called on every tick after the counter was advanced. NULL = none. */
void timer_set_hook(void (*isr_body)(void));
