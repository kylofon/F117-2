/* 0000:1442 r3d_draw_sky_ground (spec render3d.md Part 6 section B; the Python reference is sky_ground() of port/notes/s37/R7c_ref.py). Below detail 3 the flat two-colour
 * split of hz_fill_far; otherwise 16 sky bands (colours 80h + i, horizon first) and 16 ground bands (70h + i), each a quadrilateral far outside the screen horizontally
 * that poly_fill_far clips. In the TV camera picture in mono mode (flag) the bands are 88h / 7Ch. */
#include "render3d/r3d.h"
#include "sim/sim.h"

/* one band boundary at perpendicular offset t: the line through (w2 + t sinC, h2 - 0.75 t cosC) with half-length vector 22600 (cosC, -0.75 sinC) (A = A7B8h = -22600) */
static void band_points(s16 t, s16 *X1, s16 *Y1, s16 *X2, s16 *Y2)
{
    s16 nsC = (s16)DSW(0x98C8), cC = (s16)DSW(0x498C), w2 = (s16)DSW(0x0D1E), h2 = (s16)DSW(0x0D20);
    s16 si = (s16)fx_mul15(cC, (s16)0xA7B8), di = (s16)fx_mul15(nsC, t);
    *X1 = (s16)(w2 - di + si);
    *X2 = (s16)(w2 - di - si);
    si = (s16)fx_mul15(cC, t);
    di = (s16)fx_mul15(nsC, (s16)0xA7B8);
    s16 s = (s16)(di + si);
    *Y1 = (s16)((s >> 2) - di - si + h2);
    s16 u = (s16)(si - di);
    *Y2 = (s16)((u >> 2) - si + h2 + di);
}

static void bands(void *ctx, s16 H, bool ground, bool flag)
{
    s16 X1[17], Y1[17], X2[17], Y2[17];
    for (int i = 0; i <= 16; i++) {
        s16 t;
        if (ground) t = i < 16 ? (s16)(H - (s16)DSW(0xC080 + 2 * i)) : (s16)0xA7B8;
        else t = i < 16 ? (s16)((s16)DSW(0xC080 + 2 * i) + H) : 0x5848;
        band_points(t, &X1[i], &Y1[i], &X2[i], &Y2[i]);
    }
    for (int i = 0; i < 16; i++) {
        s16 pts[8] = { X1[i], Y1[i], X2[i], Y2[i], X2[i + 1], Y2[i + 1], X1[i + 1], Y1[i + 1] };
        int col = flag ? (ground ? 0x7C : 0x88) : (ground ? 0x70 : 0x80) + i;
        poly_fill_far(ctx, 4, pts, col);
    }
}

void r3d_draw_sky_ground(void *ctx)
{
    DSW(0x98C8) = (u16)-DSW(0x498A);                    /* nsC, cC, sB, cB cache (read by 17a3, bf71) */
    DSW(0x98E4) = DSW(0x498C);
    DSW(0x98F8) = DSW(0x4986);
    DSW(0x9F12) = DSW(0x4988);
    if ((s8)DSB(0x98DE) < 3) { hz_fill_far(((u8 *)ctx)[4] | ((u8 *)ctx)[5] << 8); return; }
    bool flag = DSW(0x4360) && DSW(0x43B2);
    for (int i = 0; i < 16; i++) DSW(0xC080 + 2 * i) = DSW(0x0D22 + 2 * i);          /* the sky offsets (initialised data) */
    s16 cB = (s16)DSW(0x4988);
    if (cB < 0x200) cB = 0x200;
    s16 H = (s16)(((s32)(s16)DSW(0x4986) * 256) / cB);
    u8 z = DSB(0x433A);
    if (z) H = (z & 0x1F) < 16 ? (s16)(u16)((u16)H << (z & 0x1F)) : 0;
    if (DSB(0x294B)) H >>= 1;
    bands(ctx, H, false, flag);
    s16 alt = (s16)DSW(0x4980);
    u16 c0 = (u16)(alt / 0x200);
    DSW(0xC080) = c0;
    for (int i = 1; i < 16; i++) DSW(0xC080 + 2 * i) = (u16)((u16)(alt / ((16 - i) << 5)) - c0);
    DSW(0xC080) = 0;
    bands(ctx, H, true, flag);
}
