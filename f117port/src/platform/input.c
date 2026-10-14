/* Joystick, mouse and the Ctrl-Break hook (platform.md §5.2, §5.3, §5.4). The routines of the original talk to
 * port 201h and INT 33h/1Bh; here they read the host's gamepad and mouse and keep their state at the same DGROUP
 * offsets as the original (ProgDs in startup.c), so the programs' own code sees the same bytes. */
#include <stdio.h>

#include "gen/module_platform.h"
#include "host.h"
#include "platform/platform.h"

/* ------------------------------------------------------------------ joystick */
/* Calibration table, 5 groups of 4 words (axis 0/1 used by the code, 2 and 3 are the second stick's):
 *   +00 min raw   +08 max raw   +10 centre raw   +18 slope below centre   +20 slope above centre
 * then +28 the raw counts of the last read (x, y) and +30 the scaled bytes (x, y). END 23C4..23F5. */
#define J_MIN  0x00
#define J_MAX  0x08
#define J_CTR  0x10
#define J_SLO  0x18
#define J_SHI  0x20
#define J_RAW  0x28
#define J_OUT  0x30

static u16 jw(int off) { return DSW((u16)(plat_ds()->joy_cal + off)); }
static void jw_set(int off, u16 v) { DSW((u16)(plat_ds()->joy_cal + off)) = v; }

bool joy_present(void) { return host_joy_present(); }

/* The button bits of port 201h (bits 4..7 = A1 A2 B1 B2, 0 = pressed; no joystick reads FFh). */
u8 joy_port_201(void)
{
    if (!host_joy_present()) return 0xFF;
    return (u8)(((~host_joy_buttons() & 0x0F) << 4) | 0x0F);
}

/* END 0000:43D5 / START 8469 / SETUP 0237:005C / VGAME 10A8:0CB7: the original writes port 201h and counts loop
 * iterations (up to 65535) until the axis bits fall; 0FFFFh for both axes is "no joystick" (timeout). The port has
 * no timing loop: a gamepad is turned into plausible counts, 500..1500 with the centre at 1000, which SETUP's
 * calibration screen then learns like any other stick. */
void joy_read_raw(void)
{
    u16 x = 0xFFFF, y = 0xFFFF;
    if (host_joy_present()) {
        s16 ax[4];
        host_joy_axes(ax);
        x = (u16)(1000 + (ax[0] * 500) / 32768);
        y = (u16)(1000 + (ax[1] * 500) / 32768);
    }
    jw_set(J_RAW, x);
    jw_set(J_RAW + 2, y);
}

/* END 0000:441D / START 84B1 / SETUP 0237:00A4 / VGAME 10A8:0CFF: raw count -> byte at J_OUT + axis. 7Fh at the
 * centre, 0..7Eh below, 80h..FFh above, and the routine LEARNS: a raw value beyond the stored min / max replaces it
 * and sets the slope. */
void joy_scale(int axis)
{
    int si = (axis & 1) * 2;
    u16 dx = jw(J_RAW + si);
    u16 ax = (u16)(dx - jw(J_CTR + si));
    u8 ah;
    if (ax == 0) {
        ah = 0x7F;
    } else if (ax & 0x8000) {                                   /* below the centre */
        if (dx > jw(J_MIN + si)) {
            u16 slope = jw(J_SLO + si);
            ax = (u16)(0u - ax);
            if (slope == 0) { ah = 0x7F; }                      /* PORT: uncalibrated table; the original divides by 0 (R6003) */
            else {
                u16 q = rtl_div32_16((u32)ax << 16, slope, NULL);
                ah = (u8)(((u16)~q >> 1) >> 8);
            }
        } else {
            jw_set(J_MIN + si, dx);
            jw_set(J_SLO + si, (u16)(0u - ax));
            ah = 0;
        }
    } else {                                                    /* above the centre */
        if (dx < jw(J_MAX + si)) {
            u16 slope = jw(J_SHI + si);
            if (slope == 0) { ah = 0x80; }                      /* PORT: as above */
            else {
                u16 q = rtl_div32_16((u32)ax << 16, slope, NULL);
                ah = (u8)((q >> 1 >> 8) + 0x80);
            }
        } else {
            jw_set(J_MAX + si, dx);
            jw_set(J_SHI + si, ax);
            ah = 0xFF;
        }
    }
    DSB((u16)(plat_ds()->joy_cal + J_OUT + (axis & 1))) = ah;
}

/* END 0000:43C6 / START 845A: read, then scale both axes */
void joy_poll(void)
{
    joy_read_raw();
    joy_scale(0);
    joy_scale(1);
}

/* END 0000:448E / START 851A / SETUP 0237:0127: copies the 20 words of the calibration table (comm block +48h) */
void joy_set_calib(const void *tbl)
{
    const u8 *s = tbl;
    for (int i = 0; i < 40; i++) DSB((u16)(plat_ds()->joy_cal + i)) = s[i];
}

/* ------------------------------------------------------------------ mouse (INT 33h model, START and END) */

/* END 0000:44A4 / START 8530: the left (0) or right (1) button byte */
int mouse_button(int n)
{
    const ProgDs *d = plat_ds();
    return n == 0 ? DSB(d->mouse_btn) : DSB((u16)(d->mouse_btn + 1));
}

/* END 0000:44B9 / START 8545: INT 33h/03: position (the virtual x range is 0..27Fh, the program keeps x/2) and buttons */
void mouse_poll(void)
{
    const ProgDs *d = plat_ds();
    int x, y;
    u8 b;
    host_mouse_read(&x, &y, &b);
    DSW(d->mouse_x) = (u16)x;
    DSW(d->mouse_y) = (u16)y;
    DSB(d->mouse_btn) = (b & 1) ? 1 : 0;
    DSB((u16)(d->mouse_btn + 1)) = (b & 2) ? 1 : 0;
}

/* END 0000:44E7 / START 8573: reset, X range 0..280h, Y range 0..C7h, ratio 8:16, then position = ([x], [y]).
 * ORIGINAL QUIRK kept: the position goes in unscaled (INT 33h/04 takes virtual x, pixel x is half of it), whereas
 * mouse_set_pos doubles x. */
void mouse_init(void)
{
    const ProgDs *d = plat_ds();
    host_mouse_warp(DSW(d->mouse_x) / 2, DSW(d->mouse_y));
}

/* END 0000:451B / START 85A7: INT 33h/04 with cx = [x] * 2 */
void mouse_set_pos(void)
{
    const ProgDs *d = plat_ds();
    host_mouse_warp(DSW(d->mouse_x), DSW(d->mouse_y));
}

/* ------------------------------------------------------------------ Ctrl-Break (INT 1Bh) */
static bool break_installed;
static bool break_closed;

void break_hook_reset(void) { break_installed = false; break_closed = false; }

/* DSWAP 0000:0546 / END 457A / SETUP 0C58 / START 85DB / VGAME 1B83 (the vector is kept by the host) */
void break_hook_remove(void) { break_installed = false; }

/* DSWAP 0000:0523 / END 4557 / SETUP 0C35 / VGAME 1B60: saves INT 1Bh (read from 0:006C) and installs break_isr */
void break_hook_install(void) { break_installed = true; }

/* END 0000:4597 etc.: INT 1Bh handler, sets the program's flag byte (END and START: 1, the others FFh) */
void break_isr(void)
{
    const ProgDs *d = plat_ds();
    if (cur_dgroup && d->break_flag) DSB(d->break_flag) = d->break_value;
}

void break_isr_dispatch(void)
{
    if (break_installed) break_isr();
}

/* A closed window is Ctrl-Break to a program that installed the hook (platform.md §5.4), once; the chain loop then stops
 * after the program (prog_abort). Called from the tick. */
void break_check_window(void)
{
    if (!break_closed && host_quit_requested()) {
        break_closed = true;
        prog_abort = true;
        break_isr_dispatch();
    }
}

/* VGAME 0000:1B94 / END 458B / SETUP 0C69: the vector at 0:off as segment:offset (BX = off, AX = seg) */
unsigned long ivt_read(int off)
{
    return (unsigned long)rd16(0, (u16)(off + 2)) << 16 | rd16(0, (u16)off);
}
