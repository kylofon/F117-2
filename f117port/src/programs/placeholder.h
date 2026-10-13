#pragma once
/* PHASE 4 ONLY: stand-ins for the seven programs. They are replaced by the real ports in S5 (each real
 * `<name>_main` simply stops being defined here). */
#include "types.h"

/* false: --chain-test, nothing is shown and nothing waits. true: each stand-in shows a test pattern
 * (mode 13h, DAC ramp; the number of white blocks on top = the program's number in the chain) for about
 * half a second of 70.09 Hz ticks; Esc or closing the window aborts the chain. */
extern bool placeholder_interactive;
void placeholder_init(void);        /* installs the Esc handler on the host keyboard */
