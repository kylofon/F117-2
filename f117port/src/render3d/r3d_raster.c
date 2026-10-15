/* VGAME segment 1359: the raster back end of the 3D renderer, mode M (MCGA, 320x200) only; the modes E / C / T of the library are dead in this game (spec render3d.md Part 5,
 * the Python reference is port/notes/s37/R7b_ref.py). A polygon is assembled in two row tables, edge_L (min x per row, idle 7FFFh) and edge_R (max x per row, idle 8001h)
 * with the first touched row in edge_miny; fill_M paints the spans and resets the rows it has visited (including the "gap row" behaviour: it stops at the first row whose L
 * is 7FFFh). The page written is the segment in DS:85EA (r3d_page()). */
#include "render3d/r3d.h"

u8 *r3d_test_page;

#define WIN R3D_W

/* 1359:0000 gfx_mode_init(mode_char): the upper-cased letter into DS:85C4; the MCGA case sets up the row table. Only 'M' is used */
void gfxlib_mode_init(int mode)
{
    int c = mode & 0xFF;
    if (c >= 'a') c -= 0x20;
    WIN->gfx_mode = (u16)c;
    if (c == 'M') gfxlib_rows_mcga();
}

/* 1359:010A gfxlib_rows_mcga (rows_M): DS:85EC[i] = i * 320 for 200 rows, dispatch base [85C2] = 70h */
void gfxlib_rows_mcga(void)
{
    for (int i = 0; i < 200; i++) WIN->gfx_rows[i] = (u16)(i * 320);
    WIN->gfx_table = 0x70;
}

/* 1359:01D4 set_colour_M(colour word): low byte colour, high byte fill type; a shaded ramp colour (hi FFh, lo >= A0h) loses the shade offset DS:4894, not below the ramp base
 * (an unsigned compare: a wrap keeps the large value, quirk) */
void r3d_setcolor_M(u16 colour)
{
    WIN->gfx_colour = colour;
    u8 al = (u8)colour, ah = (u8)(colour >> 8);
    if (ah == 0xFF && al >= 0xA0) {
        ah = al & 0xF0;
        al = (u8)(al - DSB(0x4894));
        if (al <= ah) al = ah;
        DSB(0x85D6) = al;
    }
}

/* 1359:0719 edge_dda(rec): one clipped edge into the row tables (min x into L, max x into R per row). An edge with an end outside the window rows is dropped */
void r3d_edge_dda(u16 rec)
{
    u16 ax = ER_W(rec, 0), dx = ER_W(rec, 8), bx = ER_W(rec, 4), sv = ER_W(rec, 0xC), t;
    if ((s16)ax > (s16)dx) { t = ax; ax = dx; dx = t; t = sv; sv = bx; bx = t; }
    u16 di = (u16)(bx * 2);
    dx = (u16)(dx - ax);
    s16 bp = 2;
    if ((s16)sv < (s16)bx) { bp = -2; t = bx; bx = sv; sv = t; }
    s16 ymin = WIN->win_ymin, ymax = WIN->win_ymax;
    if ((s16)bx < ymin || (s16)bx > ymax || (s16)sv < ymin || (s16)sv > ymax) return;
    if ((s16)bx < WIN->edge_miny) WIN->edge_miny = (s16)bx;
    sv = (u16)(sv - bx);
    if (sv <= dx) {                                         /* x-major */
        u16 cx = (u16)(dx + 1), err = (u16)-(cx >> 1);
        if ((s16)ax < R3D_L(di)) R3D_L(di) = (s16)ax;
        for (;;) {
            err = (u16)(err + sv);
            if (!(err & 0x8000)) {
                if ((s16)ax > R3D_R(di)) R3D_R(di) = (s16)ax;
                err = (u16)(err - dx);
                cx--;
                if (cx == 0) return;
                ax++;
                di = (u16)(di + bp);
                if ((s16)ax < R3D_L(di)) R3D_L(di) = (s16)ax;
            } else {
                ax++;
                cx--;
                if (cx == 0) {
                    ax--;
                    if ((s16)ax > R3D_R(di)) R3D_R(di) = (s16)ax;
                    return;
                }
            }
        }
    } else {                                                /* y-major */
        u16 cx = (u16)(sv + 1), err = (u16)-(cx >> 1);
        for (;;) {
            if ((s16)ax < R3D_L(di)) R3D_L(di) = (s16)ax;
            if ((s16)ax > R3D_R(di)) R3D_R(di) = (s16)ax;
            err = (u16)(err + dx);
            if (!(err & 0x8000)) {
                cx--;
                if (cx == 0) return;
                err = (u16)(err - sv);
                ax++;
                di = (u16)(di + bp);
            } else {
                di = (u16)(di + bp);
                cx--;
                if (cx == 0) return;
            }
        }
    }
}

/* 1359:07D6 wall_fill(a, b, x): the rows between a and b (clamped to the window; dropped if the range is at or beyond a window edge, or a single row) get x as both
 * their L and R candidate: the wall column beyond a window side */
void r3d_wall_fill(s16 a, s16 b, s16 x)
{
    s16 bx = a, cx = b;
    if (cx < bx) { s16 t = bx; bx = cx; cx = t; }
    s16 ymin = WIN->win_ymin, ymax = WIN->win_ymax;
    if (cx <= ymin || bx >= ymax) return;
    if (bx < ymin) bx = ymin;
    if (cx > ymax) cx = ymax;
    s16 n = (s16)(cx - bx);
    if (n == 0) return;
    if (bx < WIN->edge_miny) WIN->edge_miny = bx;
    for (int row = bx; row <= bx + n; row++) {
        u16 o = (u16)(2 * row);
        if (x < R3D_L(o)) R3D_L(o) = x;
        if (x > R3D_R(o)) R3D_R(o) = x;
    }
}

/* the row loop of fill_M: yields the next row with a span (si = 2 * row, base = its offset in the page, L / R clamped to the window) and resets every row it passes;
 * stops, and sets edge_miny to 7FFFh, at the first row whose L is 7FFFh */
typedef struct Spans { u16 si; } Spans;
static bool spans_begin(Spans *s, bool unsigned_check)
{
    u16 r = (u16)WIN->edge_miny;
    if (r == 0x7FFF || (unsigned_check && r >= 0x7FFF)) { WIN->edge_miny = 0x7FFF; return false; }
    s->si = (u16)(2 * r);
    return true;
}
static bool spans_next(Spans *s, u16 *base, s16 *l, s16 *r)
{
    s16 xmin = WIN->win_xmin, xmax = WIN->win_xmax;
    for (;;) {
        s16 L = R3D_L(s->si), R = R3D_R(s->si);
        if (L == 0x7FFF) { WIN->edge_miny = 0x7FFF; return false; }
        u16 si = s->si;
        *base = R3D_ROW(si);
        R3D_L(si) = 0x7FFF;
        R3D_R(si) = (s16)0x8001;
        s->si = (u16)(si + 2);
        if (!(L > xmax || R < xmin)) {
            *l = L > xmin ? L : xmin;
            *r = R < xmax ? R : xmax;
            s->si = (u16)(si + 2);
            return true;
        }
    }
}

/* 1359:0AF7 fill_M: the colour word in DS:85D6 selects the fill: type < FBh or FFh a two-colour dither ((x ^ y) & 1 picks the high colour), FEh AND, FDh OR,
 * FCh stipple (low colour on odd (x ^ y)), FBh only resets the rows */
void r3d_fill_M(void)
{
    u16 colour = WIN->gfx_colour;
    u8 al = (u8)colour, ah = (u8)(colour >> 8);
    u8 *page = r3d_page();
    Spans sp;
    u16 base, si_row;
    s16 l, r;
    if (ah < 0xFB || ah == 0xFF) {
        if (ah == 0xFF) ah = al;
        WIN->fill_dither = (u16)(al | ah << 8);
        if (!spans_begin(&sp, false)) return;
        while (spans_next(&sp, &base, &l, &r)) {
            si_row = (u16)(sp.si - 2);
            for (int x = l; x <= r; x++) page[(u16)(base + x)] = ((x ^ (si_row >> 1)) & 1) ? ah : al;
        }
    } else if (ah == 0xFE || ah == 0xFD) {
        if (!spans_begin(&sp, false)) return;
        while (spans_next(&sp, &base, &l, &r))
            for (int x = l; x <= r; x++) {
                u16 o = (u16)(base + x);
                page[o] = ah == 0xFE ? (u8)(page[o] & al) : (u8)(page[o] | al);
            }
    } else if (ah == 0xFC) {
        if (!spans_begin(&sp, true)) return;
        while (spans_next(&sp, &base, &l, &r)) {
            si_row = (u16)(sp.si - 2);
            for (int x = l; x <= r; x++)
                if ((x ^ (si_row >> 1)) & 1) page[(u16)(base + x)] = al;
        }
    } else {                                                /* FBh: clear only */
        if (!spans_begin(&sp, false)) return;
        while (spans_next(&sp, &base, &l, &r)) {}
    }
}

/* 1359:045D line_M(rec): a Bresenham line between the clipped ends rec+0,+4 and rec+8,+0C in the byte colour DS:85D6, both ends drawn (error starts at -(n >> 1), a tie steps) */
void r3d_line_M(u16 rec)
{
    s16 bx = ER_S(rec, 0), di = ER_S(rec, 4), bp = ER_S(rec, 8), cx = ER_S(rec, 0xC), t;
    u16 ady = (u16)(cx - di), adx = (u16)(bp - bx);
    if ((s16)ady < 0) ady = (u16)-ady;
    if ((s16)adx < 0) adx = (u16)-adx;
    u8 c = (u8)WIN->gfx_colour;
    u8 *page = r3d_page();
    u16 o, n, err;
    if (ady <= adx) {                                       /* x-major */
        if (bp < bx) { t = bx; bx = bp; bp = t; t = di; di = cx; cx = t; }
        s16 step = cx < di ? -320 : 320;
        o = (u16)(R3D_ROW((u16)(2 * di)) + bx);
        n = (u16)(bp - bx + 1);
        err = (u16)-(n >> 1);
        for (;;) {
            page[o] = c;
            o++;
            err = (u16)(err + ady);
            if (!(err & 0x8000)) { o = (u16)(o + step); err = (u16)(err - adx); }
            if (--n == 0) return;
        }
    } else {                                                /* y-major */
        if (cx < di) { t = bx; bx = bp; bp = t; t = di; di = cx; cx = t; }
        s16 step = bp < bx ? -1 : 1;
        n = (u16)(cx - di + 1);
        o = (u16)(R3D_ROW((u16)(2 * di)) + bx);
        err = (u16)-(n >> 1);
        for (;;) {
            page[o] = c;
            o = (u16)(o + 320);
            err = (u16)(err + adx);
            if (!(err & 0x8000)) { o = (u16)(o + step); err = (u16)(err - ady); }
            if (--n == 0) return;
        }
    }
}

/* 1359:0040 / 0049 / 0052 the dispatch stubs, mode M: colour, line, fill (the only mode of the game; the table is selected by DS:85C2) */
void gfxlib_dispatch_a(u16 colour) { r3d_setcolor_M(colour); }
void gfxlib_dispatch_b(u16 rec) { r3d_line_M(rec); }
void gfxlib_dispatch_c(void) { r3d_fill_M(); }
