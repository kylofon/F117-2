/* The 70.09 Hz retrace tick source, the BIOS clock and the timer_install/timer_remove pair (platform.md §4). */
#include "platform/timer.h"

#include "gen/module_platform.h"
#include "host.h"
#include "mem.h"
#include "platform/platform.h"

volatile u32 timer_ticks;
static void (*isr_hook)(void);
static void (*aux_hook)(void);
static bool installed;

/* The BIOS tick counter 0040:006C is 18.2065 Hz = 1193182 / 65536 per second: ticks = ms * 1193182 / 65 536 000.
 * (Also a midnight flag at 0040:0070 in the real BIOS; nothing reads it.) */
bool timer_bios_frozen;                          /* test aid (ctest sim): 0040:006C stays as it is, so seeds taken from the BIOS clock are reproducible */

void timer_poll_bios_clock(void)
{
    if (timer_bios_frozen) return;
    u64 ms = host_ns() / 1000000ull;
    wr32(0x40, 0x6C, (u32)(ms * 1193182ull / 65536000ull));
}

static void timer_isr(void)
{
    timer_ticks++;
    timer_poll_bios_clock();
    break_check_window();
    if (aux_hook) aux_hook();
    if (installed && isr_hook) isr_hook();
}

void timer_init(void)
{
    timer_ticks = 0;
    isr_hook = NULL;
    aux_hook = NULL;
    installed = false;
    host_set_tick_handler(timer_isr);
}

void timer_set_hook(void (*isr_body)(void)) { isr_hook = isr_body; }
void timer_set_aux(void (*clock)(void)) { aux_hook = clock; }
bool timer_installed(void) { return installed; }

/* END 0000:4B8C / SETUP 0FC0 / START 8CA8: the original calibrates against the retrace and saves INT 8; the port starts the hook */
void timer_install(void) { installed = true; }

/* END 0000:4BCA / SETUP 0FFE / START 8CE6: PIT back to 18.2 Hz and INT 8 restored; here the hook stops */
void timer_remove(void)
{
    installed = false;
    isr_hook = NULL;
}

/* END 0000:4CBC / START 8DD8 / SETUP 111A / VGAME 1D76 / PLAYER 026B:0272: measures PIT counts per retrace to pick the ISR rate class
 * (platform.md §4). PORT: no PIT; the host tick IS the retrace (class 4, 70 Hz), nothing to measure. */
void timer_calibrate(void) { }

/* END 0000:4DA3 / START 8EAD / SETUP 11AF / PLAYER 026B:0307: the PIT channel 0 count, only used by the calibration.
 * PORT: a free-running 16-bit counter derived from the host clock (1193182 Hz, counting down). */
unsigned pit_read(void) { return (unsigned)(0x10000u - (u32)((host_ns() / 1000ull) * 1193182ull / 1000000ull) % 0x10000u) & 0xFFFF; }

/* END 0000:45A6 / START 8607 / VGAME 1E60: INT 1Ah/00: the low word of the BIOS tick count */
unsigned bios_timer(void)
{
    timer_poll_bios_clock();
    return rd16(0x40, 0x6C);
}
