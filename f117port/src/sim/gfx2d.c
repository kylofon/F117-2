/* Segment 1001 of VGAME: the 2D clip and edge-table routines the HUD, the cockpit and the sky/ground bands draw through (spec render3d.md Part A; owner render3d,
 * ported here in S5.5 because the sim flow code calls them). TODO(S5.6): the rest of segment 1001 (hz_draw, ...) comes with render3d; keep this file's semantics.
 *
 *   line_clip_draw  1001:0336  Cohen-Sutherland clip of DS:2299..229F to 0..[2293] x 0..[2295], then MGRAPHIC slot 31 `line`
 *   edge_to_tables  1001:06AC  the same clip core, then the segment into the left / right edge tables L = DS:22A1, R = DS:2459 (220 words each)
 *   fill_rows_far   1001:024C  a solid rectangle through slot 40;  poly_fill_far 1001:01EC  a convex polygon through slot 37
 * The INT 0 handlers of the original (divide overflow in the intercept computation) are the saturating rdiv(). */
#include "sim/sim.h"
#include "sim/sim_priv.h"

#define XMAX ((s16)DSW(0x2293))
#define YMAX ((s16)DSW(0x2295))
#define LTAB 0x22A1
#define RTAB 0x2459

/* 1001:04C2 clip_outcode: 8 = x < 0, 1 = x > XMAX, 4 = y < 0, 2 = y > YMAX (signed compares) */
static u8 outcode(s16 x, s16 y)
{
    u8 al = 0x0F;
    if (x >= 0) al &= (u8)~8;
    if (x <= XMAX) al &= (u8)~1;
    if (y >= 0) al &= (u8)~4;
    if (y <= YMAX) al &= (u8)~2;
    return al;
}

/* the idiv + rounding block of 0336 / 06AC (spec render3d.md A.8); D = the divisor, h = its stored half; a divide overflow gives +-7F00 and remainder 0 */
static s16 rdiv(s32 num, s16 D, s16 h)
{
    u16 hw = (u16)((u32)num >> 16);
    s16 q, r;
    if (D == 0 || num / D < -32768 || num / D > 32767) {
        q = ((hw ^ (u16)D) & 0x8000) ? (s16)-0x7F00 : 0x7F00;
        r = 0;
    } else {
        q = (s16)(num / D);
        r = (s16)(num % D);
    }
    if ((hw ^ (u16)D) & 0x8000) { r = (s16)-r; q = (s16)(q - 1); }
    r = (s16)(r - h);
    if (((u16)(r ^ hw) & 0x8000) == 0) q = (s16)(q + 1);
    return q;
}

/* the clip core (spec A.8). `scratch` = 228A (0336) or 261A (06AC). Returns 0 = ACCEPT (DS:2299..229F hold the clipped segment), 1 = TRIVIAL_REJECT, 2 = LATE_REJECT;
 * for the edge variant `edge` is true: the parts beyond a side border call edge_column, and the reject cases report cx / bp / di / x through the out parameters. */
typedef struct ClipOut { s16 cx, bp, di, x; u8 al, oc; } ClipOut;
static void edge_column(u16 x, s16 ya, s16 yb);
static void edge_column_x(s16 x, s16 ya, s16 yb);

static int clip_core(u16 scratch, bool edge, ClipOut *o)
{
    u16 s_oc = scratch, s_dx = (u16)(scratch + 1), s_dy = (u16)(scratch + 3), s_hx = (u16)(scratch + 5), s_hy = (u16)(scratch + 7);
    s16 cx = (s16)DSW(0x2299), dx = (s16)DSW(0x229D), si = (s16)DSW(0x229B), di = (s16)DSW(0x229F);
    s16 bp, bx;
    DSB(s_oc) = outcode(cx, dx);
    u8 al = outcode(si, di);
    if (al == 0) {
        if (DSB(s_oc) == 0) return 0;
        s16 t;
        t = si; si = cx; cx = t;
        t = di; di = dx; dx = t;
        u8 tmp = DSB(s_oc); DSB(s_oc) = al; al = tmp;
        DSW(0x2299) = (u16)cx;
        DSW(0x229D) = (u16)dx;
    }
    bp = dx;
    if (DSB(s_oc) & al) { o->cx = cx; o->di = di; o->bp = bp; o->al = al; o->oc = DSB(s_oc); return 1; }
    {
        s32 tdx = (s32)si - cx, tdy = (s32)di - bp;
        s16 Dx, Dy, hx;
        bool oxf = tdx < -32768 || tdx > 32767, oyf = tdy < -32768 || tdy > 32767;
        if (oxf) {
            Dx = (s16)(tdx >> 1); hx = (s16)(Dx >> 1);
            Dy = oyf ? (s16)(tdy >> 1) : (s16)(((s16)tdy) >> 1);
        } else {
            Dx = (s16)tdx; hx = (s16)(Dx >> 1);
            if (oyf) { Dy = (s16)(tdy >> 1); Dx = (s16)(Dx >> 1); hx = (s16)(hx >> 1); }
            else Dy = (s16)tdy;
        }
        DSW(s_dx) = (u16)Dx;
        DSW(s_hx) = (u16)hx;
        DSW(s_dy) = (u16)Dy;
        DSW(s_hy) = (u16)(Dy >> 1);
    }
    for (;;) {
        s16 Dx = (s16)DSW(s_dx), Dy = (s16)DSW(s_dy), hx = (s16)DSW(s_hx), hy = (s16)DSW(s_hy);
        s16 rx, ry, x;
        bx = 0;
        if (al & 9) {
            bx = si < 0 ? 0 : XMAX;
            s16 y = (s16)(rdiv((s32)(s16)(bx - cx) * Dy, Dx, hx) + bp);
            if (y >= 0 && y <= YMAX) { rx = bx; ry = y; goto got; }
            if (y < 0) bx = 0;
            else bx = di > YMAX ? YMAX : 0;
        } else {
            bx = di > YMAX ? YMAX : 0;
        }
        x = (s16)(rdiv((s32)(s16)(bx - bp) * Dx, Dy, hy) + cx);
        if (x < 0 || x > XMAX) { o->cx = cx; o->di = di; o->bp = bp; o->x = x; o->al = al; return 2; }
        rx = x; ry = bx;
    got:
        if (edge) edge_column((u16)rx, ry, di);
        if (DSB(s_oc) == 0) { DSW(0x229F) = (u16)ry; DSW(0x229B) = (u16)rx; return 0; }
        DSW(0x229D) = (u16)ry;
        DSW(0x2299) = (u16)rx;
        { s16 t = si; si = cx; cx = t; t = di; di = bp; bp = t; }
        al = DSB(s_oc);
        DSB(s_oc) = 0;
    }
}

/* 1001:0336 line_clip_draw: true = rejected (CF); otherwise the clipped segment is drawn with slot 31 `line` */
bool line_clip_draw(void)
{
    ClipOut o;
    if (clip_core(0x228A, false, &o) != 0) return true;
    line((s16)DSW(0x2299), (s16)DSW(0x229D), (s16)DSW(0x229B), (s16)DSW(0x229F));
    return false;
}

/* 1001:032A line_clip_far */
void line_clip_far(void) { (void)line_clip_draw(); }

/* 1001:0525 tables_clear: only the dirty rows [2615]..[2617] are reset (L = FFFFh, R = 0) */
void tables_clear(void)
{
    u16 y0 = DSW(0x2615);
    if (y0 & 0x8000) return;
    u16 n = (u16)(DSW(0x2617) + 1 - y0);
    for (u16 i = 0; i < n; i++) {
        DSW((u16)(LTAB + 2 * (y0 + i))) = 0xFFFF;
        DSW((u16)(RTAB + 2 * (y0 + i))) = 0;
    }
    DSW(0x2615) = 0xFFFF;
    DSW(0x2617) = 0;
}

/* 1001:055B edge_column: x = 0 -> L, x = XMAX -> R (other x: nothing); rows between ya and yb are overwritten with x */
static void edge_column(u16 x, s16 ya, s16 yb)
{
    u16 tbl;
    if (x == 0) tbl = LTAB;
    else if (x == DSW(0x2293)) tbl = RTAB;
    else return;
    s16 hi = ya, lo = yb;
    if (hi < lo) { s16 t = hi; hi = lo; lo = t; }
    if (lo < 0) lo = 0;
    if (hi > YMAX) hi = YMAX;
    if ((u16)hi > DSW(0x2617)) DSW(0x2617) = (u16)hi;
    if ((u16)lo < DSW(0x2615)) DSW(0x2615) = (u16)lo;
    u16 n = (u16)(hi - lo + 1);
    for (u16 i = 0; i < n; i++) DSW((u16)(tbl + 2 * (u16)(lo + i))) = x;
}

/* 1001:05AA edge_column_x: a point beyond a side border becomes the border column */
static void edge_column_x(s16 x, s16 ya, s16 yb)
{
    if (x < 0) edge_column(0, ya, yb);
    else if (x > XMAX) edge_column((u16)XMAX, ya, yb);
}

/* 1001:05B9 edge_walk: the (clipped) segment DS:2299..229F into the tables, left end first, x-major or y-major Bresenham; unsigned compares */
static void edge_walk(void)
{
    if (DSW(0x229B) < DSW(0x2299)) {
        u16 t = DSW(0x229B); DSW(0x229B) = DSW(0x2299); DSW(0x2299) = t;
        t = DSW(0x229F); DSW(0x229F) = DSW(0x229D); DSW(0x229D) = t;
    }
    u16 DX = (u16)(DSW(0x229B) - DSW(0x2299));
    DSW(0x2611) = DX;
    s16 dyv = (s16)(DSW(0x229F) - DSW(0x229D));
    u16 yhi = DSW(0x229F), ylo = DSW(0x229D);
    int step = 2;
    if (dyv < 0) { u16 t = yhi; yhi = ylo; ylo = t; step = -2; dyv = (s16)-dyv; }
    u16 dy = (u16)dyv;
    DSW(0x2613) = dy;
    if (yhi > DSW(0x2617)) DSW(0x2617) = yhi;
    if (ylo < DSW(0x2615)) DSW(0x2615) = ylo;
    u16 o = (u16)(2 * DSW(0x229D));
    u16 x = DSW(0x2299);
#define LW(o_) DSW((u16)(LTAB + (o_)))
#define RW(o_) DSW((u16)(RTAB + (o_)))
    if (dy < DX) {
        s16 err = (s16)-(s16)((u16)(DX + 1) >> 1);
        s16 n = (s16)DX;
        if (x < LW(o)) LW(o) = x;
        for (;;) {
            n--;
            if (n < 0) { if (x > RW(o)) RW(o) = x; return; }
            err = (s16)(err + dy);
            if (err >= 0) {
                if (x > RW(o)) RW(o) = x;
                x++;
                err = (s16)(err - DX);
                o = (u16)(o + step);
                if (x < LW(o)) LW(o) = x;
            } else x++;
        }
    } else {
        s16 err = (s16)-(s16)((u16)(dy + 1) >> 1);
        s16 n = (s16)dy;
        for (;;) {
            if (x < LW(o)) LW(o) = x;
            if (x > RW(o)) RW(o) = x;
            n--;
            if (n < 0) return;
            o = (u16)(o + step);
            err = (s16)(err + DX);
            if (err < 0) continue;
            err = (s16)(err - dy);
            x++;
        }
    }
#undef LW
#undef RW
}

/* 1001:06AC edge_to_tables: clip the segment DS:2299..229F and put it into the edge tables (parts beyond the side borders become border columns) */
void edge_to_tables(void)
{
    ClipOut o;
    int r = clip_core(0x261A, true, &o);
    if (r == 0) { edge_walk(); return; }
    if (r == 1) {
        if ((o.al & o.oc) & 6) return;
        edge_column_x(o.cx, o.di, o.bp);
        return;
    }
    edge_column_x(o.x, o.di, o.bp);
}

/* 1001:024C fill_rows_far: the window `vp` (page word at +0, colour byte at +4): rows ymin..ymax from xl to xr, solid, through slot 40 */
void fill_rows_far(void *vp, int xl, int ymin, int xr, int ymax)
{
    const u8 *w = vp;
    unsigned old = seg_get();
    page_select_r((s16)(w[0] | w[1] << 8));
    color_set_r(w[4]);
    tables_clear();
    DSW(0x2617) = (u16)ymax;
    DSW(0x2615) = (u16)ymin;
    u16 n = (u16)(ymax - ymin + 1);
    for (u16 i = 0; i < n; i++) {
        DSW((u16)(LTAB + 2 * (u16)(ymin + i))) = (u16)xl;
        DSW((u16)(RTAB + 2 * (u16)(ymin + i))) = (u16)xr;
    }
    poly_fill_solid((s16)DSW(0x2615), DSP(LTAB), (s16)DSW(0x2617));
    seg_set(old);
}

/* 1001:01EC poly_fill_far: n edges of the closed polygon pts[0..n-1], filled in `colour` (slot 37, mode 0) */
void poly_fill_far(void *vp, int n, const s16 *pts, int colour)
{
    (void)vp;
    color_set_r(colour & 0xFF);
    tables_clear();
    u16 cnt = (u16)(n - 1);
    DSW(0x2299) = (u16)pts[0];
    DSW(0x229D) = (u16)pts[1];
    for (u16 k = 1; k <= cnt; k++) {
        DSW(0x229B) = (u16)pts[2 * k];
        DSW(0x229F) = (u16)pts[2 * k + 1];
        edge_to_tables();
        DSW(0x2299) = (u16)pts[2 * k];
        DSW(0x229D) = (u16)pts[2 * k + 1];
    }
    DSW(0x229B) = (u16)pts[0];
    DSW(0x229F) = (u16)pts[1];
    edge_to_tables();
    poly_fill((s16)DSW(0x2615), DSP(LTAB), (s16)DSW(0x2617), 0);
}

/* 1001:0504 poly_flush_far */
void poly_flush_far(int mode) { poly_fill((s16)DSW(0x2615), DSP(LTAB), (s16)DSW(0x2617), mode); }
