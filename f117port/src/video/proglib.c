/* The video routines the programs carry in their own library segment (not in MGRAPHIC): the clipped Bresenham line
 * gfx_line, the solid rectangle gfx_fill_rect, the DAC writers (pal_write_vsync, pal_set, pal_set_default) and the palette queue (pal_apply_queue). They
 * exist in START, END and DSWAP (spec platform.md section 1, owner video; VGAME has its own 14A2:000E / 14A2:00C5).
 * Their tables are in each program's DGROUP, at different offsets: VProg below.
 *
 * Not here: the fades (pal_fade*, shell/debrief), pal_queue_push (shell), the flight palettes A/B/C and
 * pal_flight_load (simflow, VGAME 1088:0006: it picks one of three 768-byte palettes in VGAME's DGROUP and writes it with
 * pal_write_vsync). */
#include <string.h>

#include "gen/module_platform.h"
#include "gen/module_video.h"
#include "host.h"
#include "mem.h"
#include "platform/platform.h"
#include "platform/vga.h"
#include "video/video.h"

typedef struct {
    u16 pagetab;        /* DS offset of the word table: page number -> segment (START CAB0, END 55D0, DSWAP 29E0) */
    u16 ylimit;         /* gfx_line clips y to 0..ylimit-1 (A8h; DSWAP 140h) */
    u16 palbuf;         /* pal_set_default's 768-byte palette buffer */
    u16 queue, qcount, qflag;   /* pal_apply_queue: 6-byte records, their count byte, the flag the timer ISR sets */
} VProg;

static const VProg vprog[PROG_COUNT] = {
    [PROG_START] = { 0xCAB0, 0xA8, 0x64E3, 0x64A6, 0x64E2, 0xADFC },
    [PROG_END]   = { 0x55D0, 0xA8, 0x1F25, 0x1EE8, 0x1F24, 0x50CC },
    [PROG_DSWAP] = { 0x29E0, 0x140, 0x0435, 0, 0, 0 },
};

static const VProg *vp(const char *who)
{
    const VProg *p = &vprog[plat_prog];
    if (!p->pagetab) host_fatal("%s: not part of %s", who, prog_table[plat_prog].exe);
    return p;
}

u8 pal6_to_8(u8 v) { return (u8)(v << 2 | v >> 4); }

void dac_set(int first, int count, const u8 *rgb6)
{
    for (int i = 0; i < count; i++) vga_dac_set((u8)(first + i), rgb6[3 * i], rgb6[3 * i + 1], rgb6[3 * i + 2]);
}

void dac_get(int first, int count, u8 *rgb6)
{
    for (int i = 0; i < count; i++) vga_dac_get((u8)(first + i), &rgb6[3 * i], &rgb6[3 * i + 1], &rgb6[3 * i + 2]);
}

/* START 0000:829A, END 0000:4206, DSWAP 0000:0790: Bresenham into the page whose segment is in the program's table,
 * clipped per pixel to x 0..319 (signed) and y 0..ylimit-1. `page` points at the page number. */
void gfx_line(int *page, int x1, int y1, int x2, int y2, int colour)
{
    const VProg *p = vp("gfx_line");
    const u8 *pp = (const u8 *)page;
    u16 pn = (u16)(pp[0] | pp[1] << 8);
    u8 *pg = seg_ptr(DSW(p->pagetab + 2 * pn));
    s16 si = (s16)x1, di = (s16)y1, cx = (s16)x2, dx = (s16)y2, bx = di, ax = si;
    s16 ystep = 1;
    if (ax > cx) { s16 t = cx; cx = ax; ax = t; t = dx; dx = bx; bx = t; si = ax; di = bx; }
    u8 al = (u8)colour;
    int ah = 0;
    s16 major = 0, minor = 0, e = 0, n;
    if (ax == cx && bx == dx) {
        n = 0;                                               /* one pixel */
    } else {
        cx = (s16)(cx - ax);
        dx = (s16)(dx - bx);
        if (dx < 0) { dx = (s16)-dx; ystep = -1; }
        if (cx < dx) { ah = 1; s16 t = dx; dx = cx; cx = t; }
        major = cx;
        minor = dx;
        n = major;
        e = (s16)-((s16)(major + 1) >> 1);
    }
    for (;;) {
        if (si >= 0 && si < 320 && di >= 0 && di < (s16)p->ylimit) pg[(u16)(di * 320 + si)] = al;
        if (--n < 0) break;
        if (ah == 0) si++; else di = (s16)(di + ystep);
        e = (s16)(e + minor);
        if (e < 0) continue;
        e = (s16)(e - major);
        if (ah == 0) di = (s16)(di + ystep); else si++;
    }
}

/* START 0000:8378, END 0000:42E4, VGAME 14A2:000E: `count` RGB triples (6-bit) from DAC entry `first`, written while the
 * display is in retrace. PORT: one wait for the retrace, then all entries (the original spreads long writes over several
 * frames; the fades calibrate against pal_set calls per 4 BIOS ticks, so they adapt). With BIOS data 0040:0089 bit 1 set
 * (grey-scale summing) every entry becomes the rounded 0.30 R + 0.59 G + 0.11 B (+1 at index 255, a carry bug of the original;
 * PORT: its grey path also returns with an unbalanced stack, the port just returns). */
void pal_write_vsync(void *rgb, int first, int count)
{
    const u8 *s = rgb;
    vga_wait_vretrace_active();
    bool grey = (mem[0x489] & 2) != 0;
    for (int i = 0; i < (u16)count; i++, s += 3) {
        u8 r = s[0], g = s[1], b = s[2];
        if (grey) {
            u32 sum = r * 0x4CCDu + g * 0x970Au + b * 0x1C29u;
            u8 v = (u8)((sum >> 16) + ((sum >> 15) & 1));
            if ((u8)(first + i) == 0xFF) v++;               /* `inc bx` carries from BL into BH, the grey value, at index 255 */
            r = g = b = v;
        }
        vga_dac_set((u8)(first + i), r, g, b);
    }
}

/* START 0000:8364, END 0000:42D0: the program's whole 768-byte palette to the DAC */
void pal_set(void *pal) { pal_write_vsync(pal, 0, 256); }

/* END 0000:42BE, START 0000:8352, DSWAP 0000:0848: INT 10h/1012h with the program's palette buffer, no retrace wait */
void pal_set_default(void)
{
    const VProg *p = vp("pal_set_default");
    dac_set(0, 256, DSP(p->palbuf));
}

/* START 0000:8427, END 0000:4393: wait one timer tick (the ISR sets the flag byte), then apply the queued DAC changes
 * ({first, DS offset of the RGB data, count} words; the count byte is decremented as they are applied). */
void pal_apply_queue(void)
{
    const VProg *p = vp("pal_apply_queue");
    if (!p->queue) host_fatal("pal_apply_queue: not part of %s", prog_table[plat_prog].exe);
    DSB(p->qflag) = 0;
    while (!DSB(p->qflag)) host_pump();
    u16 si = p->queue;
    while (DSB(p->qcount)) {
        u16 first = DSW(si), rgb = DSW(si + 2), count = DSW(si + 4);
        dac_set(first, count, DSP(rgb));
        si = (u16)(si + 6);
        DSB(p->qcount)--;
    }
}

/* START 0000:8A32 (END 0000:49D2 fills with w->fg, same shape): a solid rectangle through slot 40. `w` points at the window
 * {page word, ..., colour byte at +4}; the draw segment is saved and restored, the driver's current colour stays set to the
 * window's. The original keeps the two 220-word edge tables in DGROUP (8F86 / 913E); here they are local, and the rows are
 * limited to what the tables hold. PORT: y0 < 0, y1 < y0 or y1 > 219 would corrupt memory in the original; they draw nothing here. */
void gfx_fill_rect(void *w, int x0, int y0, int x1, int y1)
{
    const u8 *wp = w;
    unsigned saved = seg_get();
    page_select_r((s16)(wp[0] | wp[1] << 8));
    color_set_r(wp[4]);
    if (y0 >= 0 && y1 >= y0 && y1 <= 219) {
        u8 tbl[0x1B8 * 2];
        memset(tbl, 0xFF, sizeof tbl);
        for (int y = y0; y <= y1; y++) {
            tbl[2 * y] = (u8)x0; tbl[2 * y + 1] = (u8)(x0 >> 8);
            tbl[0x1B8 + 2 * y] = (u8)x1; tbl[0x1B8 + 2 * y + 1] = (u8)(x1 >> 8);
        }
        poly_fill_solid(y0, tbl, y1);
    }
    seg_set(saved);
}
