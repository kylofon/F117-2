/* The pieces of the C runtime and of the MicroProse library that the game logic calls and that have an
 * observable behaviour worth keeping (platform.md §3.2 and the "Key findings" list): memory and string helpers
 * with their quirks, the exact random generator, the long arithmetic helpers (with the 8086 divide fault),
 * dist_approx. printf/sprintf/fprintf and the stdio internals are the host's libc (spec §3.2: "Do not port
 * function by function"). Pointers are host pointers into mem[] (platform.h, "Address model"). */
#include <stdio.h>
#include <string.h>

#include "gen/module_platform.h"
#include "host.h"
#include "platform/platform.h"

/* ------------------------------------------------------------------ integer helpers */

/* END 0000:52bc / START 96AE / VGAME F302: 16-bit neg (0x8000 stays 0x8000) */
int abs_fn(int a)
{
    s16 v = (s16)a;
    return v < 0 ? (s16)(u16)(0u - (u16)v) : v;
}

/* END 0000:452C / START 8BEA: octagonal distance. ax = |x1-x2|, dx = |y1-y2| (16-bit neg), result = max +
 * min/4 (logical shifts). Note shell.md's dist_approx_xy (5413) is a different routine (max + min/2). */
int dist_approx(int x1, int y1, int x2, int y2)
{
    s16 ax = (s16)(u16)((u16)x1 - (u16)x2);
    if (ax < 0) ax = (s16)(u16)(0u - (u16)ax);
    s16 dx = (s16)(u16)((u16)y1 - (u16)y2);
    if (dx < 0) dx = (s16)(u16)(0u - (u16)dx);
    u16 r;
    if (ax >= dx) r = (u16)((u16)ax + ((u16)dx >> 2));
    else r = (u16)((u16)dx + ((u16)ax >> 2));
    return (s16)r;
}

/* ------------------------------------------------------------------ memory and strings */
/* All forward copies (rep movs with DF clear): an overlapping move repeats data as the CPU does. */

/* END 0000:4B0C / START 8B6C: mem_copy(src, dst, n) bytes */
void mem_copy(void *src, void *dst, unsigned n)
{
    const u8 *s = src;
    u8 *d = dst;
    for (u16 i = 0; i < (u16)n; i++) d[i] = s[i];
}

/* END 0000:4B26 / START 8B86: n WORDS */
void mem_copy_w(void *src, void *dst, unsigned nwords)
{
    const u8 *s = src;
    u8 *d = dst;
    for (u32 i = 0; i < (u16)nwords * 2u; i++) d[i] = s[i];
}

/* END 0000:4ADE / START 8B3E */
void mem_zero(void *dst, unsigned n) { memset(dst, 0, (u16)n); }

/* END 0000:4AF5 / START 8B55 */
void mem_zero_w(void *dst, unsigned nwords) { memset(dst, 0, (u16)nwords * 2u); }

/* END 0000:4B40 / START 8BA0: ORIGINAL BUG kept: the value is loaded into AX and then AL is cleared (xor al,al)
 * before rep stosb, so this always fills with zero (platform.md "Key findings"). */
void mem_fill(void *dst, int val, unsigned n)
{
    (void)val;
    memset(dst, 0, (u16)n);
}

/* END 0000:4B5A / START 8BBA: the word version does fill with val */
void mem_fill_w(void *dst, int val, unsigned nwords)
{
    u8 *d = dst;
    for (u16 i = 0; i < (u16)nwords; i++) { d[2 * i] = (u8)val; d[2 * i + 1] = (u8)((u16)val >> 8); }
}

/* END 0000:5290 / START 9612 / VGAME F2D6 */
void *memcpy_fn(void *d, void *s, unsigned n)
{
    u8 *dp = d;
    const u8 *sp = s;
    for (u16 i = 0; i < (u16)n; i++) dp[i] = sp[i];
    return d;
}

/* END 0000:5272 / START 95F4 / VGAME F29A: copy between two segment:offset addresses, forward */
void movedata(unsigned sseg, unsigned soff, unsigned dseg, unsigned doff, unsigned n)
{
    for (u16 i = 0; i < (u16)n; i++) wr8((u16)dseg, (u16)(doff + i), rd8((u16)sseg, (u16)(soff + i)));
}

/* END 0000:5110 / START 946C / VGAME F006 */
char *strcat_fn(char *d, char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

/* END 0000:5150 / START 94AC / VGAME F046 / MPS_LOGO 0146:061E */
char *strcpy_fn(char *d, char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0) { }
    return r;
}

/* END 0000:5182 / START 94DE / VGAME F078: a 16-bit count */
unsigned strlen_fn(char *s) { return (u16)strlen(s); }

/* END 0000:4B72: copies up to and including the NUL; the source is a far pointer in the original */
void str_copy_far(char *dst, const char *src)
{
    do { *dst++ = *src; } while (*src++);
}

/* ------------------------------------------------------------------ random numbers (platform.md §3.2) */
/* seed = seed * 343FDh + 269EC3h (32 bits), result = (seed >> 16) & 7FFFh. The seed is a dword in the program's
 * DGROUP (plat_ds()->rand_seed; END 5158, START AE8C, SETUP 0EB4, VGAME 926E); without an image (host tests,
 * placeholder programs) it is a private variable. */
static u32 free_seed;

static u32 *seed_ptr(void)
{
    const ProgDs *d = plat_ds();
    if (cur_dgroup && d->rand_seed) return (u32 *)DSP(d->rand_seed);
    return &free_seed;
}

/* END 0000:52CA / SETUP 179E / START 96BC / VGAME F310: low word = seed, high word = 0 */
void srand_fn(unsigned seed)
{
    u32 *p = seed_ptr();
    u8 *b = (u8 *)p;
    b[0] = (u8)seed; b[1] = (u8)(seed >> 8); b[2] = 0; b[3] = 0;
}

/* END 0000:52DC / SETUP 17B0 / START 96CE / VGAME F322 */
int rand_fn(void)
{
    u8 *b = (u8 *)seed_ptr();
    u32 s = (u32)(b[0] | b[1] << 8 | b[2] << 16 | (u32)b[3] << 24);
    s = s * 0x343FDu + 0x269EC3u;
    b[0] = (u8)s; b[1] = (u8)(s >> 8); b[2] = (u8)(s >> 16); b[3] = (u8)(s >> 24);
    return (int)((s >> 16) & 0x7FFF);
}

/* END 0000:3E45 / SETUP 0A70: (rand() * n) >> 15 as a long product with an arithmetic shift, 0..n-1. The original
 * zero-extends n (sub dx,dx), so a negative n is 65536 + n. */
int rand_n(int n)
{
    s32 p = (s32)((u32)rand_fn() * (u32)(u16)n);
    return (s16)(p >> 15);
}

/* ------------------------------------------------------------------ the 8086 DIV and the long helpers */

/* dx:ax / d with a 16-bit quotient: d == 0 or a quotient above 0FFFFh is the divide fault (INT 0 -> message
 * R6003, END 4E8A). */
u16 rtl_div32_16(u32 dxax, u16 d, u16 *rem)
{
    if (d == 0 || (dxax >> 16) >= d) rtl_divide_error();
    if (rem) *rem = (u16)(dxax % d);
    return (u16)(dxax / d);
}

/* END 0000:5302 / VGAME F392: signed 32-bit divide, truncating; the operands are made positive first (so
 * -2^31 / x behaves as the unsigned 2^31), the sign is restored at the end. A zero divisor is the divide fault. */
long rtl_aFldiv(long a, long b)
{
    u32 ua = (u32)(s32)a, ub = (u32)(s32)b;
    int neg = 0;
    if ((s32)ua < 0) { ua = 0u - ua; neg ^= 1; }
    if ((s32)ub < 0) { ub = 0u - ub; neg ^= 1; }
    if (ub == 0) rtl_divide_error();
    u32 q = ua / ub;
    if (neg) q = 0u - q;
    return (s32)q;
}

/* END 0000:539C / VGAME F42C: the low 32 bits of the product */
long rtl_aFlmul(long a, long b) { return (s32)((u32)(s32)a * (u32)(s32)b); }

/* VGAME F45E: shl dx:ax, cl: the count is CL (0..255), each step a 1-bit shift */
long rtl_aFlshl(long a, int n)
{
    u8 c = (u8)n;
    return c >= 32 ? 0 : (s32)((u32)(s32)a << c);
}

/* END 0000:53CE / VGAME F46A: sar dx:ax, cl */
long rtl_aFlshr(long a, int n)
{
    u8 c = (u8)n;
    s32 v = (s32)a;
    return c >= 32 ? (v < 0 ? -1 : 0) : (v >> c);
}

/* START 0000:97F4 / VGAME F50E: shr dx:ax, cl */
unsigned long rtl_aFulshr(unsigned long a, int n)
{
    u8 c = (u8)n;
    return c >= 32 ? 0 : (unsigned long)((u32)a >> c);
}

/* START 0000:97D8 / VGAME F476: *p <<= n on a long in memory */
void rtl_lshl_mem(long *p, int n) { *p = rtl_aFlshl(*p, n); }
