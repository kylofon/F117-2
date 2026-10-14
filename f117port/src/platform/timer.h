#pragma once
/* The timer tick (platform.md §4, PLAN.md Decisions 2026-10-13).
 *
 * The original reprograms the PIT to run its ISR several times per video frame and divides that down so that
 * one "tick" is one vertical retrace. The port drops the PIT: the host delivers exactly one tick per retrace
 * (70.0866 Hz in mode 13h, host.h) and timer_isr() runs on the main thread from host_pump().
 *
 * What a tick does is program-specific and belongs to the program modules (S5.4..S5.9): the frame counter
 * `[50CC]`, the sound poll with its dropped 7th tick, and the `[3660]` frame-rate estimator. The mechanism here:
 *
 *   timer_set_hook(body);  timer_install();      the program's ISR body runs once per tick from now on
 *   timer_remove();                              the hook stops (also done when the program ends)
 *
 * A tick first advances timer_ticks and the BIOS clock at 0040:006C (18.2065 Hz, from the host clock; bios_timer()
 * reads it), turns a closed window into Ctrl-Break for programs that installed the hook, then calls the body.
 * The original's ISR runs several times per tick (class 4 = 4 interrupts per retrace at 70 Hz, platform.md §4,
 * sound polls at 70 or 280 Hz): a program module that needs the sub-ticks (sound, S5.9) calls its sub-tick
 * routine k times from its body. */
#include "types.h"

void timer_init(void);                           /* once: installs timer_isr as the host tick handler */
extern volatile u32 timer_ticks;                 /* ticks since timer_init */

/* The per-program ISR body (S5): called on every tick after the counter was advanced, while installed. NULL = none. */
void timer_set_hook(void (*isr_body)(void));
bool timer_installed(void);
