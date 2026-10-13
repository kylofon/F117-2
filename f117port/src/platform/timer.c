/* The 70.09 Hz retrace tick source, see timer.h. */
#include "platform/timer.h"

#include "host.h"

volatile u32 timer_ticks;
static void (*isr_hook)(void);

static void timer_isr(void)
{
    timer_ticks++;
    if (isr_hook) isr_hook();
}

void timer_init(void)
{
    timer_ticks = 0;
    isr_hook = NULL;
    host_set_tick_handler(timer_isr);
}

void timer_set_hook(void (*isr_body)(void)) { isr_hook = isr_body; }
