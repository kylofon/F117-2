/* VGAME's palettes (segment 1088, 14A2:00C5, 0000:4857, 0000:4D43; spec simflow.md Part 1 section 6): the flight palette A/B/C by day, night and climate,
 * the MFD green ramp, the save / restore pair of the quit requester, the one-colour-per-16-seconds drift towards the day/night palette. */
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"

/* 1088:00A3 pal_mfd_ramp_green: DAC 98h..9Fh from DS:206A; 1088:00BD pal_mfd_ramp_rt: from DS:1212 */
void pal_mfd_ramp_green(void) { pal_write_vsync(DSP(0x206A), 0x98, 8); }
void pal_mfd_ramp_rt(void) { pal_write_vsync(DSP(0x1212), 0x98, 8); }

/* 1088:0006 pal_flight_load: the climate ramps (not for climate 2) into the three palettes, the one for night / day into DS:104A and the DAC */
void pal_flight_load(void)
{
    if (DSB(0x0D47) != 2) {
        memcpy(DSP(0x179A), DSP(0x1F4A), 0x30);
        memcpy(DSP(0x182A), DSP(0x1FDA), 0x30);
        memcpy(DSP(0x1A9A), DSP(0x1F7A), 0x30);
        memcpy(DSP(0x1B2A), DSP(0x200A), 0x30);
        memcpy(DSP(0x1D9A), DSP(0x1FAA), 0x30);
        memcpy(DSP(0x1E2A), DSP(0x203A), 0x30);
    }
    u16 src;
    if (DSW(0x43AE) == 0) src = DSW(0x3658) ? 0x194A : 0x1C4A;
    else                  src = DSW(0x3658) ? 0x1C4A : 0x164A;
    memcpy(DSP(0x104A), DSP(src), 0x300);
    pal_write_vsync(DSP(0x104A), 0, 256);
    if (DSW(0x365E)) pal_mfd_ramp_green();
}

/* 14A2:00C5 pal_read_dac: wait for the retrace, then the 256 entries (6-bit) into dst */
void pal_read_dac(void *dst)
{
    vga_wait_vretrace_active();
    dac_get(0, 256, dst);
}

/* 1088:00D7 pal_save; 00EE pal_restore; 0107 pal_set_adv (ADV.PAL for the quit requester) */
void pal_save(void) { pal_read_dac(DSP(0x0D4A)); }
void pal_restore(void) { pal_write_vsync(DSP(0x0D4A), 0, 256); }
void pal_set_adv(void) { pal_write_vsync(DSP(0x134A), 0, 256); }

/* 0000:4857 sim_reset_clock_palette: seeds the generator, resets the cockpit widgets, the random step counter (a multiple of 8), the start clock, and
 * walks the live palette (104A) towards the day or night palette 2 * ((slot0 template + slot1 template) & 15) steps */
void sim_reset_clock_palette(void)
{
    rand_seed_timer();
    cockpit_widgets_reset();
    DSW(0x3D60) = (u16)(rand_n(0x1000) & 0x7FF8);
    int n = (DSB(0xE2E0) + DSB(0xE2F2)) & 0xF;
    DSW(0x9870) = (u16)(n << 8);
    DSW(0x98E2) = 0;
    u16 target = DSW(0x3658) ? 0x1C4A : 0x164A;
    for (int k = 0; k < n * 2; k++) pal_fade_step(DSP(0x104A), DSP(target), 0x100);
    pal_write_vsync(DSP(0x104A), 0, 0x100);
    pal_mfd_ramp_green();
}

/* 0000:4D43 pal_time_of_day_step: once per mission second; every 16 s one colour range of the live palette (DS:104A) moves one step towards the day or night
 * palette: t = clock & 3F: 0 (only when clock bit 6 is set) 10h..4Fh, 10h 60h..9Fh, 20h A0h..CFh, 30h D0h..FFh. The MFD ramp (98h..9Fh) is left out of the write at 10h
 * when the instrument panel is shown */
void pal_time_of_day_step(void)
{
    int t = DSB(0x98E2) & 0x3F;
    if ((t & 0xF) || DSW(0x43AE)) return;
    int count = 0, first = 0;
    switch (t) {
    case 0:    if (DSB(0x98E2) & 0x40) { first = 0x10; count = 0x40; } break;
    case 0x10: first = 0x60; count = 0x40; break;
    case 0x20: first = 0xA0; count = 0x30; break;
    case 0x30: first = 0xD0; count = 0x30; break;
    }
    if (count == 0) return;
    u16 tgt = (u16)((DSW(0x3658) ? 0x1C4A : 0x164A) + first * 3);
    u16 cur = (u16)(0x104A + first * 3);
    pal_fade_step(DSP(cur), DSP(tgt), count);
    if (t == 0x10 && DSW(0x365E)) count = 0x38;
    pal_write_vsync(DSP(cur), first, count);
}
