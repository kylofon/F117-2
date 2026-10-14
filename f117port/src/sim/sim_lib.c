/* The fixed-point and library helpers of VGAME (owner lib; spec simflow.md Part 2 section 1, Part 5, Part 8): the 109D segment (Q15 multiply and the sine table
 * interpolation), the 0000:C9xx / CAxx / 2Exx helpers, the palette stepper and the little RTL pieces the sim flow code calls. The sine table is read from the loaded
 * image (DS:2084), as the original does. */
#include <string.h>

#include "sim/sim.h"

/* 109D:0006 fx_mul15: spec simflow.md "109d:0006 fx_mul15(int a, int b) -> int": (a*b + 0x4000) >> 15, rounding half up, truncated to 16 bits */
int fx_mul15(int a, int b)
{
    s32 p = (s32)(s16)a * (s16)b;
    return (s16)((p >> 15) + ((p >> 14) & 1));
}

/* 109D:0090 fx_sin_interp (BX = angle -> BX): the table T at DS:2084 (257 words), interpolated over the low byte with rounding */
int fx_sin_interp(int a)
{
    u16 ang = (u16)a;
    int idx = ang >> 8, fr = ang & 255;
    s16 t0 = DSS(VGAME_DS_sin_tab + 2 * idx), t1 = DSS(VGAME_DS_sin_tab + 2 * idx + 2);
    s16 d = (s16)(t1 - t0);
    s32 p = (s32)d * fr;
    return (s16)(t0 + (s16)(p >> 8) + ((p >> 7) & 1));
}

/* 109D:0088 fx_sin16_reg: the register entry of 0090 (BX in, BX out) */
int fx_sin16_reg(int a) { return fx_sin_interp(a); }

/* 109D:007C fx_sin16 */
int fx_sin16(int a) { return fx_sin_interp(a); }

/* 109D:006C fx_cos16 = sin16(a + 0x4000) */
int fx_cos16(int a) { return fx_sin_interp((u16)(a + 0x4000)); }

/* 0000:C94E fx_clamp_wrap: spec simflow.md "0000:c94e fx_clamp_wrap(int v, int lo, int hi)". The last line is the original's quirk */
int fx_clamp_wrap(int v, int lo, int hi)
{
    v = (s16)v; lo = (s16)lo; hi = (s16)hi;
    if (hi < v) return hi;
    if (lo <= v) return v;
    return v <= -0x4000 ? hi : lo;
}

/* 0000:C96D fx_clamp: plain signed clamp */
int fx_clamp(int v, int lo, int hi)
{
    v = (s16)v; lo = (s16)lo; hi = (s16)hi;
    return hi < v ? hi : lo > v ? lo : v;
}

/* the C library abs() at 16 bits: -32768 stays -32768 */
static inline s16 abs16(s16 v) { return (s16)(v < 0 ? -v : v); }

/* 0000:C987 fx_dist: spec simflow.md "0000:c987 fx_dist(int a,int b)": max + (min >> 1) with the 16-bit abs() quirk, capped at 0x7FFF (a negative sum is returned as it is) */
int fx_dist(int a, int b)
{
    s16 x = abs16((s16)a), y = abs16((s16)b);
    s16 mx = y >= x ? y : x, mn = y >= x ? x : y;
    s32 sum = (s32)mx + (mn >> 1);
    if (sum < 0) return (s16)sum;
    if (sum > 0x7FFF) return 0x7FFF;
    return (int)sum;
}

/* 0000:C9D6 fx_heading: spec simflow.md "0000:c9d6 fx_heading(int x,int y)": 0 = +y, 4000h = +x, 8000h = -y, C000h = -x; the long arithmetic and abs() of the original */
u16 fx_heading(int x, int y)
{
    s16 xs = (s16)x, ys = (s16)y;
    if (xs == 0) return ys > 0 ? 0 : 0x8000;
    if (ys == 0) return xs > 0 ? 0x4000 : 0xC000;
    s16 axs = abs16(xs), ays = abs16(ys);
    s32 num, den;
    int f;
    if (ays >= axs) { num = (s32)((u32)(s32)axs << 14); den = ays; f = 0; }
    else            { num = (s32)((u32)(s32)ays << 14); den = axs; f = 1; }
    s16 t = (s16)(num / den);
    s32 e = ((s32)abs16((s16)(0x1333 - t)) * 0xB00) >> 14;
    s32 m = 0x2800 - e;
    s16 r = (s16)(((s32)((u32)(s32)t * (u32)m)) >> 14);
    u16 res;
    if (xs > 0) {
        if (ys > 0) res = f ? (u16)(0x4000 - r) : (u16)r;
        else        res = f ? (u16)(r + 0x4000) : (u16)(0x8000 - r);
    } else {
        if (ys > 0) res = f ? (u16)(r - 0x4000) : (u16)-r;
        else        res = f ? (u16)(0xC000 - r) : (u16)(r + 0x8000);
    }
    return res;
}

/* 0000:CAEE fx_sin_mul = mul15(m, sin16(a)); 0000:CB07 fx_cos_mul = fx_sin_mul(a + 4000h, m) */
int fx_sin_mul(u16 a, int m) { return fx_mul15(m, fx_sin16(a)); }
int fx_cos_mul(u16 a, int m) { return fx_sin_mul((u16)(a + 0x4000), m); }

/* 0000:CB1B fx_sext8 */
int fx_sext8(int x) { return (s8)(x & 0xFF); }

/* 0000:2F40 fx_isqrt: Babylonian, signed idiv, may be off by one */
int fx_isqrt(int n)
{
    s16 v = (s16)n;
    if (v < 0) v = (s16)-v;
    if (v < 4) return 1;
    s16 g = (s16)(v >> 2), q;
    for (;;) {
        q = (s16)(v / g);
        g = (s16)((q + g) >> 1);
        int dd = g - q;
        if (dd < 0) dd = -dd;
        if (dd <= 1) return g;
    }
}

/* 0000:2EBE fx_asin: the inverse of the sine table; the result is in -4000h..4000h */
u16 fx_asin(int v)
{
    s16 s = (s16)v;
    if (s == -0x8000) return 0xC000;
    s32 a = s < 0 ? -s : s;
    int i = (int)(a >> 9) + 1;
    while (i >= 0 && DSS(VGAME_DS_sin_tab + 2 * i) > a) i--;
    s16 ti = DSS(VGAME_DS_sin_tab + 2 * i), t1 = DSS(VGAME_DS_sin_tab + 2 * i + 2);
    s16 dd = (s16)(t1 - ti);
    s32 ang = (s32)(i << 8) + ((s32)((a - ti)) * 256) / dd;
    return (u16)(s < 0 ? -ang : ang);
}

/* 0000:2F2F fx_acos = 4000h - asin */
u16 fx_acos(int q15) { return (u16)(0x4000 - fx_asin(q15)); }

/* 0000:2E64 fx_ratio_q15: x/y in Q15, the signs in bytes, the divisor |y| computed at 16 bits and sign-extended (so y = -32768 divides by FFFF8000h) */
int fx_ratio_q15(int x, int y)
{
    s16 xs = (s16)x, ys = (s16)y;
    int sx = xs < 0 ? -1 : 1, sy = ys < 0 ? -1 : 1;
    u32 den = (u32)(s32)abs16(ys);
    u32 num = (u32)(u16)abs16(xs) << 16;
    u16 q = (u16)((num / den) >> 1);
    return (s16)((s16)((s16)q * sy) * sx);
}

/* 0000:C8FA lib_scale_eighths: 0.5, 0.625, 0.75, 0.875 of v for mode 0..3, else v */
int lib_scale_eighths(int mode, int v)
{
    s16 s = (s16)v;
    switch ((s16)mode) {
    case 0:  return s >> 1;
    case 1:  return (s16)((s >> 1) + (s >> 3));
    case 2:  return (s16)((s >> 1) + (s >> 2));
    case 3:  return (s16)((s >> 1) + (s >> 2) + (s >> 3));
    default: return s;
    }
}

/* 0000:CB39 lib_sign */
int lib_sign(int v) { s16 s = (s16)v; return s > 0 ? 1 : s < 0 ? -1 : 0; }

/* 0000:CB56 rand_seed_timer: seed = bios_timer(); [9514] = seed; srand(seed) */
void rand_seed_timer(void)
{
    unsigned seed = bios_timer();
    DSW(0x9514) = (u16)seed;
    srand_fn(seed);
}

/* 0000:EA26 pal_fade_step: spec simflow.md Part 8 table: `count` RGB triples; every component of cur moves one step toward target, returns the number changed */
int pal_fade_step(void *cur, void *target, int count)
{
    u8 *c = cur, *t = target;
    int changed = 0;
    for (int i = 0; i < 3 * count; i++) {
        if (c[i] < t[i]) { c[i]++; changed++; }
        else if (c[i] > t[i]) { c[i]--; changed++; }
    }
    return changed;
}

/* 0000:0CE7 set_file_ext: replaces the text from the first '.' (or the NUL) by ext */
void set_file_ext(char *name, const char *ext)
{
    char *p = name;
    while (*p && *p != '.') p++;
    strcpy(p, ext);
}

/* 0000:F2B8 strupr */
char *strupr(char *s)
{
    for (char *p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    return s;
}

/* 0000:F492 rtl_aFlshr_mem: *p = *p >> n (arithmetic) */
void rtl_aFlshr_mem(long *p, int n) { *p = rtl_aFlshr(*p, n); }

/* 0000:F4AE rtl_aFuldiv */
u32 rtl_aFuldiv(u32 a, u32 b) { return a / b; }

/* 0000:F186 filelength */
long filelength_fn(int fd)
{
    long cur = lseek_fn(fd, 0, 1);
    long end = lseek_fn(fd, 0, 2);
    lseek_fn(fd, cur, 0);
    return end;
}

/* 0000:1E0B pit_read_retrace: the PIT counter latched at the start of a retrace (the host tick IS the retrace) */
u16 pit_read_retrace(void) { return (u16)pit_read(); }

/* 1199:0178 dos_buf_open (unused in VGAME): [4632] = handle, then the first buffer fill */
void dos_buf_open(int h) { DSW(0x4632) = (u16)h; dos_buf_fill(); }

/* 11CE:0009 pic_hook_nop; 0000:02C0 / 02C1: a lone ret */
void pic_hook_nop(void) { }
void stub_ret_02c0(void) { }
void stub_ret_02c1(void) { }

/* 0000:CB72 input_button: button n of the joystick (when it is in use) plus the key pulse [43BE + 2n] (Backspace = gun, Enter = release) */
int input_button(int n)
{
    int j = 0;
    if (comm_get()->input_devices & 1) j = ((joy_port_201() >> (n + 4)) & 1) ^ 1;
    return (s16)(j + (s16)DSW((u16)(0x43BE + 2 * n)));
}
