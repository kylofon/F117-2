/* VGAME segment 12EF: the 2D edge clipper and the polygon assembler of the 3D renderer (spec render3d.md Part 5; the Python reference is port/notes/s37/R7b_ref.py).
 * An edge record (24 bytes, R3D_EDGES + 24 i) holds s32 coordinates before clip_edge and clipped s16 ones after it (see r3d.h and the spec's edge record table). The
 * out-codes are 1 y < ymin, 2 y > ymax, 4 x < xmin, 8 x > xmax; the window is DS:85CA.. (R3dWork). The INT 0 divide-overflow of the 16-bit intercept cannot happen
 * (spec: |xb - bx| <= |dx| and |yb - cy| <= |dy|); r3d_idiv16 maps it to 0 if it ever did. Not ported (no callers in the game): clip_add_edge, draw_line. */
#include "render3d/r3d.h"

u8 r3d_bisect_last_bp;

#define WIN R3D_W

/* 12EF:02D1 outcode16(x, y): the signed comparisons are exact; an overflow of x - xmin or y - ymin only sets the byte flag DS:85C0 */
u8 r3d_outcode16(s16 x, s16 y)
{
    u8 c = 0;
    s32 d = (s32)x - WIN->win_xmin;
    if (d < -0x8000 || d > 0x7FFF) WIN->clip_ovf = 1;
    if (x < WIN->win_xmin) c |= 4;
    if (x > WIN->win_xmax) c |= 8;
    d = (s32)y - WIN->win_ymin;
    if (d < -0x8000 || d > 0x7FFF) WIN->clip_ovf = 1;
    if (y < WIN->win_ymin) c |= 1;
    if (y > WIN->win_ymax) c |= 2;
    return c;
}

/* 12EF:0665 outcode32(x, y): hi word sign / nonzero decides, else the lo word is compared UNSIGNED against the window words; x < xmin excludes the xmax test */
u8 r3d_outcode32(s32 x, s32 y)
{
    u16 xmin = (u16)WIN->win_xmin, ymin = (u16)WIN->win_ymin, xmax = (u16)WIN->win_xmax, ymax = (u16)WIN->win_ymax;
    s16 xh = (s16)((u32)x >> 16), yh = (s16)((u32)y >> 16);
    u16 xl = (u16)x, yl = (u16)y;
    u8 c = 0;
    if (xh < 0 || (xh == 0 && xl < xmin)) c |= 4;
    else if (xh > 0 || xl > xmax) c |= 8;
    if (yh < 0 || (yh == 0 && yl < ymin)) c |= 1;
    else if (yh > 0 || yl > ymax) c |= 2;
    return c;
}

/* 12EF:063E on_border: both hi words 0 and a lo word equal to a window bound (any of the four) */
int r3d_on_border(s32 x, s32 y)
{
    if (((u32)x >> 16) || ((u32)y >> 16)) return 0;
    u16 xl = (u16)x, yl = (u16)y;
    return xl == (u16)WIN->win_xmin || xl == (u16)WIN->win_xmax || yl == (u16)WIN->win_ymin || yl == (u16)WIN->win_ymax;
}

static u8 side_bits(u8 c0, u8 c1)
{
    return (u8)(((c0 & 8) ? 1 : 0) | ((c0 & 4) ? 2 : 0) | ((c1 & 8) ? 4 : 0) | ((c1 & 4) ? 8 : 0));
}

/* 12EF:020B flags16: rec+6 / +0E = y clamped to the window (ymax wins), rec+2 = side bits * 101h */
void r3d_clip_flags16(u16 rec, u8 c0, u8 c1, s16 y0, s16 y1)
{
    s16 v = y0;
    if (c0 & 1) v = WIN->win_ymin;
    if (c0 & 2) v = WIN->win_ymax;
    ER_S(rec, 6) = v;
    v = y1;
    if (c1 & 1) v = WIN->win_ymin;
    if (c1 & 2) v = WIN->win_ymax;
    ER_S(rec, 0xE) = v;
    ER_W(rec, 2) = (u16)(side_bits(c0, c1) * 0x101);
}

/* 12EF:05D9 flags32: as flags16 on the lo words rec+4 / rec+0C, ymin wins; returns the side bits */
u8 r3d_clip_flags32(u16 rec, u8 c0, u8 c1)
{
    ER_S(rec, 6) = (c0 & 1) ? WIN->win_ymin : (c0 & 2) ? WIN->win_ymax : ER_S(rec, 4);
    ER_S(rec, 0xE) = (c1 & 1) ? WIN->win_ymin : (c1 & 2) ? WIN->win_ymax : ER_S(rec, 0xC);
    return side_bits(c0, c1);
}

/* idiv with the INT 0 case (impossible here) mapped to 0 */
static s16 r3d_idiv16(s32 num, s16 den)
{
    if (den == 0) return 0;
    s32 q = num / den;
    return (q < -0x8000 || q > 0x7FFF) ? 0 : (s16)q;
}

/* 12EF:0265 intercept(code, x, y): the point where the edge leaves the window, x border first. Returns CF (1 = the edge misses the window: *fail = 0Ah left / 05h right) */
int r3d_clip_intercept(u8 code, s16 x, s16 y, s16 *ox, s16 *oy, u8 *fail)
{
    *fail = 0;
    if (code == 0) { *ox = x; *oy = y; return 0; }
    s16 dx = WIN->clip_dx, dy = WIN->clip_dy, yb;
    if (code & 0xC) {
        s16 xb = (code & 4) ? WIN->win_xmin : WIN->win_xmax;
        s16 yy = (s16)(r3d_idiv16((s32)(s16)(xb - x) * dy, dx) + y);
        if (yy < WIN->win_ymin) yb = WIN->win_ymin;
        else if (yy > WIN->win_ymax) yb = WIN->win_ymax;
        else { *ox = xb; *oy = yy; return 0; }
    } else yb = (code & 1) ? WIN->win_ymin : WIN->win_ymax;
    s16 xx = (s16)(r3d_idiv16((s32)(s16)(yb - y) * dx, dy) + x);
    *ox = xx; *oy = yb;
    if (xx < WIN->win_xmin) { *fail = 0x0A; return 1; }
    if (xx > WIN->win_xmax) { *fail = 0x05; return 1; }
    return 0;
}

/* the point slots P (x s32 at +0, y s32 at +4) at DS offset p */
static void pt_get(u16 p, s32 *x, s32 *y) { *x = DSSL(p); *y = DSSL(p + 4); }
static void pt_set(u16 p, s32 x, s32 y) { DSSL(p) = x; DSSL(p + 4) = y; }

/* 12EF:04EF bisect_in(P0 = si, P1 = di): up to 32 floor midpoints to find a point inside the window. Returns CF (1 = none); the point in *mx, *my; the slots are updated
 * in place and r3d_bisect_last_bp keeps the out-code of the last midpoint (the caller uses it, not c0: quirk) */
int r3d_bisect_in(u16 si, u16 di, s32 *mx, s32 *my)
{
    s32 ax, ay;
    WIN->clip_iter = 0x20;
    pt_get(di, &ax, &ay);
    for (;;) {
        WIN->clip_iter--;
        if (WIN->clip_iter < 0) return 1;
        s32 bx, by;
        pt_get(si, &bx, &by);
        s32 x = (s32)((u32)ax + (u32)bx) >> 1, y = (s32)((u32)ay + (u32)by) >> 1;
        u8 c = r3d_outcode32(x, y);
        r3d_bisect_last_bp = c;
        if (c == 0) { *mx = x; *my = y; return 0; }
        if (WIN->clip_c1 & c) {
            if (WIN->clip_c0 & c) return 1;
            pt_set(di, x, y); ax = x; ay = y;
        } else {
            pt_set(si, x, y);
            pt_get(di, &ax, &ay);
        }
    }
}

/* the half() of 056B: a sum whose lo and hi words are equal is returned unhalved, except FFFFFFFFh which gives 0 (quirk of the original) */
static s32 half_q(u32 v)
{
    u16 lo = (u16)v, hi = (u16)(v >> 16);
    if (lo == hi) return lo == 0xFFFF ? 0 : (s32)v;
    return (s32)v >> 1;
}

/* 12EF:056B bisect_out(inside = si, outside = di): up to 32 midpoints towards the window border; the first midpoint on the border, else the last inside point */
void r3d_bisect_out(u16 si, u16 di, s32 *mx, s32 *my)
{
    s32 ax, ay;
    WIN->clip_iter = 0x20;
    pt_get(di, &ax, &ay);
    for (;;) {
        WIN->clip_iter--;
        if (WIN->clip_iter < 0) { pt_get(si, mx, my); return; }
        s32 bx, by;
        pt_get(si, &bx, &by);
        s32 x = half_q((u32)ax + (u32)bx), y = half_q((u32)ay + (u32)by);
        if (r3d_outcode32(x, y)) { pt_set(di, x, y); ax = x; ay = y; continue; }
        if (r3d_on_border(x, y)) { *mx = x; *my = y; return; }
        pt_set(si, x, y);
        pt_get(di, &ax, &ay);
    }
}

/* 12EF:0465 the 16-bit path; false = fall into 0315 (re-sign-extend and take the 32-bit path) */
static bool fast16(u16 rec)
{
    WIN->clip_ovf = 0;
    s16 x0 = ER_S(rec, 0), y0 = ER_S(rec, 4), x1 = ER_S(rec, 8), y1 = ER_S(rec, 0xC);
    u8 c1 = r3d_outcode16(x1, y1);
    u8 c0 = r3d_outcode16(x0, y0);
    if (c0 == 0 && c1 == 0) { ER_W(rec, 2) = 0; return true; }
    if (c0 & c1) {
        if (c0 & c1 & 3) { ER_W(rec, 2) = 0x8080; return true; }
        r3d_clip_flags16(rec, c0, c1, y0, y1);
        ER_W(rec, 2) = (c0 & 8) ? 0x8585 : 0x8A8A;
        return true;
    }
    r3d_clip_flags16(rec, c0, c1, y0, y1);
    s32 d = (s32)x1 - x0;
    if (d < -0x8000 || d > 0x7FFF) return false;
    WIN->clip_dx = (s16)d;
    d = (s32)y1 - y0;
    if (d < -0x8000 || d > 0x7FFF) return false;
    WIN->clip_dy = (s16)d;
    WIN->clip_c1b = c1;
    if (WIN->clip_ovf & 1) return false;
    s16 x, y;
    u8 f;
    if (r3d_clip_intercept(c0, x0, y0, &x, &y, &f)) { ER_W(rec, 2) = (u16)((f | 0x80) * 0x101); return true; }
    ER_S(rec, 0) = x; ER_S(rec, 4) = y;
    if (r3d_clip_intercept(WIN->clip_c1b, ER_S(rec, 8), ER_S(rec, 0xC), &x, &y, &f)) { ER_W(rec, 2) = (u16)((f | 0x80) * 0x101); return true; }
    ER_S(rec, 8) = x; ER_S(rec, 0xC) = y;
    return true;
}

/* 12EF:0359 the 32-bit path (coordinates beyond s16 or an overflowing dx / dy) */
static void path32(u16 rec)
{
    s32 x0 = ER_D(rec, 0), y0 = ER_D(rec, 4), x1 = ER_D(rec, 8), y1 = ER_D(rec, 0xC);
    u8 c1 = r3d_outcode32(x1, y1);
    u8 c0 = r3d_outcode32(x0, y0);
    if (c0 == 0 && c1 == 0) { ER_W(rec, 2) = 0; return; }       /* 030F: a latent near ret in the original, unreachable with the game's windows (spec): treated as accepted */
    if (c0 & c1) {
        if (c0 & c1 & 3) ER_W(rec, 2) = 0x8080;
        else ER_W(rec, 2) = (u16)((r3d_clip_flags32(rec, c0, c1) | 0x80) * 0x101);
        return;
    }
    for (int i = 0; i < 16; i += 2) {                            /* copy the record's 16 bytes to 8568 and 8578 */
        u16 w = ER_W(rec, i);
        DSW((u16)(0x8568 + i)) = w;
        DSW((u16)(0x8578 + i)) = w;
    }
    u8 al = r3d_clip_flags32(rec, c0, c1);
    ER_W(rec, 2) = (u16)(al * 0x101);
    s32 px, py;
    if (c1 == 0) {
        r3d_bisect_out(0x8570, 0x8568, &px, &py);
        ER_W(rec, 0) = (u16)px; ER_W(rec, 4) = (u16)py;
        return;
    }
    if (c0 == 0) {
        r3d_bisect_out(0x8568, 0x8570, &px, &py);
        ER_W(rec, 8) = (u16)px; ER_W(rec, 0xC) = (u16)py;
        return;
    }
    WIN->clip_c0 = c0; WIN->clip_c1 = c1;
    s32 mx, my;
    if (r3d_bisect_in(0x8568, 0x8570, &mx, &my)) {
        ER_W(rec, 2) = (u16)((r3d_bisect_last_bp & 4 ? 0x8A : 0x85) * 0x101);      /* BP = out-code of the LAST midpoint (quirk) */
        return;
    }
    pt_set(0x8568, mx, my);
    r3d_bisect_out(0x8568, 0x8578, &px, &py);
    ER_W(rec, 0) = (u16)px; pt_set(0x8568, px, py); ER_W(rec, 4) = (u16)py;
    r3d_bisect_out(0x8568, 0x8580, &px, &py);
    ER_W(rec, 8) = (u16)px; ER_W(rec, 0xC) = (u16)py;
}

/* 12EF:0333 clip_edge(rec): clip the edge to the window; the flag word at rec+2 reports the result (00 inside, side bits, 80h rejected, 85h / 8Ah rejected right / left) */
void r3d_clip_edge(u16 rec)
{
    static const u8 offs[4] = { 0, 8, 4, 0xC };
    bool fits = true;
    for (int i = 0; i < 4; i++) {
        u16 lo = ER_W(rec, offs[i]), hi = ER_W(rec, offs[i] + 2);
        if (hi != ((lo & 0x8000) ? 0xFFFF : 0)) fits = false;
    }
    if (fits) {
        if (fast16(rec)) return;
        for (int i = 0; i < 16; i += 4) ER_W(rec, i + 2) = (ER_W(rec, i) & 0x8000) ? 0xFFFF : 0;     /* 0315 */
    }
    path32(rec);
}

/* 12EF:00AA note_left / 00C5 note_right: widen the wall's y range with (min, max) of the two values */
static void note(u16 lo_off, u16 hi_off, s16 a, s16 b)
{
    if (b < a) { s16 t = a; a = b; b = t; }
    if (b >= DSS(hi_off)) DSS(hi_off) = b;
    if (a < DSS(lo_off)) DSS(lo_off) = a;
}
void r3d_note_left(s16 a, s16 b) { note(0x85B4, 0x85B6, a, b); }
void r3d_note_right(s16 a, s16 b) { note(0x85B8, 0x85BA, a, b); }

/* 12EF:00E0 note_zclip_point: the near-plane point rec+10h..17h into the first free of 8588 / 8590 (a third overwrites 8590 and disables the closing edge: quirk) */
void r3d_note_zclip_point(u16 rec)
{
    u16 dst = WIN->zclip_n == 0 ? 0x8588 : 0x8590;
    WIN->zclip_n++;
    for (int i = 0; i < 8; i += 2) DSW((u16)(dst + i)) = ER_W(rec, 0x10 + i);
}

/* 12EF:003E poly_add_edge(rec): rasterise a clipped edge into the row tables and note the side walls */
void r3d_poly_add_edge(u16 rec)
{
    u16 f = ER_W(rec, 2);
    WIN->poly_flags |= f;
    u8 al = (u8)f;
    if (al & 0x40) r3d_note_zclip_point(rec);
    if (al & 0x80) {
        if (al & 3) ((al & 2) ? r3d_note_left : r3d_note_right)(ER_S(rec, 6), ER_S(rec, 0xE));
        return;
    }
    WIN->poly_drawn |= 4;
    r3d_edge_dda(rec);
    al = (u8)ER_W(rec, 2);
    if (al & 3) ((al & 2) ? r3d_note_left : r3d_note_right)(ER_S(rec, 4), ER_S(rec, 6));
    if (al & 0xC) ((al & 8) ? r3d_note_left : r3d_note_right)(ER_S(rec, 0xC), ER_S(rec, 0xE));
}

/* 12EF:0185 wall_right / 019B wall_left: the wall column beyond the window side */
void r3d_wall_right(void) { r3d_wall_fill(WIN->wall_r_hi, WIN->wall_r_lo, (s16)(WIN->win_xmax + 1)); }
void r3d_wall_left(void) { r3d_wall_fill(WIN->wall_l_hi, WIN->wall_l_lo, (s16)(WIN->win_xmin - 1)); }

/* 12EF:01F9 line_emit(rec): draw a clipped, not rejected edge as a line */
void r3d_line_emit(u16 rec)
{
    if (!(ER_W(rec, 2) & 0x80)) r3d_line_M(rec);
}

/* 12EF:010A poly_finish(colour): set the colour, add the closing edge when two near-plane points were saved, add the walls, fill the rows and reset the assembler */
void r3d_poly_finish(s16 colour)
{
    r3d_setcolor_M((u16)colour);
    if (WIN->zclip_n == 2) {
        r3d_clip_edge(R3D_CLOSE);
        r3d_poly_add_edge(R3D_CLOSE);
    }
    u16 f = WIN->poly_flags;
    if (WIN->poly_drawn) {
        if (f & 5) r3d_wall_right();
        if (f & 0xA) r3d_wall_left();
        r3d_fill_M();
    } else if ((f & 5) && (f & 0xA)) {
        r3d_wall_right();
        r3d_wall_left();
        r3d_fill_M();
    }
    WIN->wall_r_lo = 0x7FFF; WIN->wall_l_lo = 0x7FFF;
    WIN->wall_r_hi = (s16)0x8000; WIN->wall_l_hi = (s16)0x8000;
    WIN->poly_drawn = 0; WIN->poly_flags = 0; WIN->zclip_n = 0;
}
