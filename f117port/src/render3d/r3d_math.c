/* VGAME segment 1433, the fixed-point library of the 3D renderer (spec render3d.md Part 6 section C; the Python reference is port/notes/s37/R7c_ref.py).
 * All routines are far cdecl in the original; the pointer arguments are near pointers into DGROUP, here DS offsets (u16). The sine, arctangent, hypot and arcsine
 * tables live in the code/data segment 14B2 of the loaded image. The 109D helpers (mul15, sin16, cos16) are sim_lib.c's. Dead routines (mul_sin, mul_cos, hypot3,
 * mat_to_euler, mulq, muldiv, div32sat, imul, mat_from_euler_b) are not ported. */
#include "render3d/r3d.h"

/* segment 14B2 = image 0x14B20: DGROUP 1E23 is at image 0x1E230, so the table segment is DS - 0x1E23 + 0x14B2 (the image is loaded contiguously) */
u16 r3d_tab_seg(void) { return (u16)(cur_dgroup - 0x1E23 + 0x14B2); }
static u16 tw(u16 off) { return rd16(r3d_tab_seg(), off); }

#define S16(o) DSS((u16)(o))
#define W16(o) DSW((u16)(o))

static s16 q15(s16 a, s16 b) { return (s16)(((s32)a * b) >> 15); }                /* shl ax,1 / rcl dx,1 : floor of the 32-bit product >> 15 */
static u16 iabs16(u16 v) { return (v & 0x8000) ? (u16)-v : v; }                  /* cwd / xor / sub: 8000h stays 8000h */

/* 1433:0004 math_sin: TS[(a >> 3) & 7FF] reflected in the second quadrant, negated in the lower half */
s16 math_sin(u16 a)
{
    u16 bx = a >> 2;
    if (a & 0x4000) bx = (u16)~bx;
    s16 v = (s16)tw(bx & 0xFFE);
    if (a & 0x8000) v = (s16)-v;
    return v;
}

/* 1433:0031 math_cos = sin(a + 4000h) */
s16 math_cos(u16 a)
{
    u16 bx = a >> 2;
    if (!(a & 0x4000)) bx = (u16)~bx;
    s16 v = (s16)tw(bx & 0xFFE);
    if ((u16)(a + 0x4000) & 0x8000) v = (s16)-v;
    return v;
}

/* 1433:0061 math_atan2(y, x): octant base DS:9178 +- TA[ratio >> 4] */
u16 math_atan2(s16 y, s16 x)
{
    u16 cx = (u16)x, dx = (u16)y, bl = 0;
    if (cx & 0x8000) { bl += 8; cx = (u16)-cx; }
    if (dx & 0x8000) { bl += 4; dx = (u16)-dx; }
    if (cx < dx) { bl += 2; u16 t = cx; cx = dx; dx = t; }
    u16 base = W16(0x9178 + (bl >> 1) * 2);
    u16 idx2;
    if (dx >= cx || cx == 0) idx2 = 0x1FFE;
    else idx2 = (u16)((((u32)dx << 16) / cx) >> 4) << 1;
    u16 t = tw((u16)(0x1000 + idx2));
    if (base & 0x80) return (u16)((base & 0xFF00) - t);
    return (u16)((base & 0xFF00) + t);
}

/* 1433:015E math_asin */
s16 math_asin(s16 x)
{
    u16 ux = (u16)x, bx = (ux & 0x8000) ? (u16)-ux : ux;
    u16 v = tw((u16)(0x3202 + ((bx >> 4) & 0x7FE)));
    return (s16)((ux & 0x8000) ? (u16)-v : v);
}

/* 1433:018E math_hypot2 = L + TH[S/L] * L (Q16), never below L */
u16 math_hypot2(s16 a, s16 b)
{
    u16 ax = iabs16((u16)a), bx = iabs16((u16)b), cx, t;
    if (ax == bx) { cx = bx; t = tw(0x3200); }
    else {
        u16 L = ax > bx ? ax : bx, S = ax > bx ? bx : ax;
        cx = L;
        t = tw((u16)(0x3000 + ((((u32)S << 16) / L) >> 8) * 2));
        bx = L;
    }
    u16 r = (u16)((((u32)t * bx) >> 16) + bx);
    return r < cx ? cx : r;
}

/* 1433:0219 mat_vec16: v' = (v * M) in place, Q15 (sum of the high words of the products, << 1) */
void mat_vec16(u16 v, u16 m)
{
    s32 x = S16(v), y = S16(v + 2), z = S16(v + 4);
    u16 out[3];
    for (int k = 0; k < 3; k++)
        out[k] = (u16)(((u16)((x * S16(m + 2 * k)) >> 16) + (u16)((y * S16(m + 6 + 2 * k)) >> 16) + (u16)((z * S16(m + 12 + 2 * k)) >> 16)) << 1);
    for (int k = 0; k < 3; k++) W16(v + 2 * k) = out[k];
}

/* 1433:02AA mat_row_mul: one row of C = A * B (SI = A row, BX = B, DI = C row); the low word of the 32-bit sum << 1 is left in DS:916A */
void mat_row_mul(u16 a, u16 b, u16 c)
{
    for (int k = 0; k < 3; k++) {
        s32 s = (s32)((u32)((s32)S16(a) * S16(b + 2 * k)) + (u32)((s32)S16(a + 2) * S16(b + 6 + 2 * k)) + (u32)((s32)S16(a + 4) * S16(b + 12 + 2 * k)));
        W16(0x916A) = (u16)((u32)s << 1);
        W16(c + 2 * k) = (u16)(s >> 15);
    }
}

/* 1433:0286 mat_mul: C = A * B, three rows in order (a destination that overlaps an operand behaves as the original) */
void mat_mul_1433_0286(u16 a, u16 b, u16 c)
{
    for (int i = 0; i < 3; i++) mat_row_mul((u16)(a + 6 * i), b, (u16)(c + 6 * i));
}

/* 1433:02E2 mat_transpose: sequential word moves (dst[0,3,6,1,4,7,2,5,8] = src[0..8]) */
void mat_transpose(u16 src, u16 dst)
{
    static const u8 order[9] = { 0, 6, 12, 2, 8, 14, 4, 10, 16 };
    for (int j = 0; j < 9; j++) W16(dst + order[j]) = W16(src + 2 * j);
}

/* 1433:0314 mat_copy: rep movsw, 9 words forward */
void mat_copy(u16 src, u16 dst)
{
    for (int j = 0; j < 9; j++) W16(dst + 2 * j) = W16(src + 2 * j);
}

/* 1433:032E vec_mat32: out_k = sum v_j * M[3j + k], 32-bit, unshifted */
void vec_mat32(u16 v, u16 m, u16 out)
{
    s32 x = S16(v), y = S16(v + 2), z = S16(v + 4), r[3];
    for (int k = 0; k < 3; k++)
        r[k] = (s32)((u32)(x * S16(m + 2 * k)) + (u32)(y * S16(m + 6 + 2 * k)) + (u32)(z * S16(m + 12 + 2 * k)));
    for (int k = 0; k < 3; k++) DSSL(out + 4 * k) = r[k];
}

/* 1433:03A9 mat_from_euler(a, b, c): every product is its own Q15 multiply (nested for triples), 16-bit sums; sin / cos of a, b, c are cached in DS:916C.. */
void mat_from_euler(u16 a, u16 b, u16 c, u16 out)
{
    s16 SA = math_sin(a), CA = math_cos(a), SC = math_sin(c), CC = math_cos(c), SB = math_sin(b), CB = math_cos(b);
    s16 m[9];
    m[0] = (s16)(q15(CC, CA) - q15(q15(SC, SB), SA));
    m[1] = (s16)(q15(q15(CC, SB), SA) + q15(SC, CA));
    m[2] = (s16)-q15(CB, SA);
    m[3] = (s16)-q15(SC, CB);
    m[4] = q15(CC, CB);
    m[5] = SB;
    m[6] = (s16)(q15(q15(SC, SB), CA) + q15(CC, SA));
    m[7] = (s16)(q15(SC, SA) - q15(q15(CC, SB), CA));
    m[8] = q15(CB, CA);
    for (int i = 0; i < 9; i++) W16(out + 2 * i) = (u16)m[i];
    W16(0x916C) = (u16)SA; W16(0x916E) = (u16)CA; W16(0x9170) = (u16)SB; W16(0x9172) = (u16)CB; W16(0x9174) = (u16)SC; W16(0x9176) = (u16)CC;
}
