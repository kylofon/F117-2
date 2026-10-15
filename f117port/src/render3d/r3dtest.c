/* The unit test of the render3d leaf routines (S5.6a): f117 --r3d-test FILE runs the C routines on the cases of a vector file that port/notes/s56/r3d_leaf_ref.py
 * computed with the Python references (port/notes/s37/R7b_ref.py, R7c_ref.py), which match the original under Unicorn with 0 mismatches. A case is
 *
 *   CASE <name> <hex args...>      the routine and its arguments
 *   IN <off>:<hex> ...             bytes written into a zeroed DGROUP (after the global G lines)
 *   PIN <off>:<hex> ...            bytes written into the draw page (which starts as the G P pattern)
 *   RET <text>                     the expected return value (a string that this file formats the same way), "-" for none
 *   OUT <off>:<hex> ...            the expected changes of DGROUP; every other byte must be unchanged
 *   POUT <off>:<hex> ...           the same for the page
 *   LOG <event>/<event>...         the expected driver calls (colour 32, line 31, poly_fill 37 with a hash of the edge tables, poly_fill_far 100)
 *
 * and `G M|P|T <off> <hex>` lines (before the first case) give the DGROUP bytes, the draw page and the 14B2 table segment that every case starts with. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render3d/r3d.h"
#include "sim/sim.h"

#define DG_SEG 0x5000
#define PAGE_SIZE 65536

static u8 page_buf[PAGE_SIZE], page_before[PAGE_SIZE], dg_before[65536], g_base[65536], g_page[PAGE_SIZE];
static char logbuf[4096];
static size_t loglen;

static u32 fnv(const u8 *p, size_t n)
{
    u32 h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void trace(int kind, const s16 *v, int n)
{
    char t[160];
    int l = 0;
    if (kind == 37) l = snprintf(t, sizeof t, "37 %04x %04x %x %08x", (u16)v[0], (u16)v[1], (u16)v[2], fnv(DSP(0x22A1), 0x370));
    else if (kind == 32) l = snprintf(t, sizeof t, "32 %02x", (u8)v[0]);
    else {
        l = snprintf(t, sizeof t, "%d", kind);
        for (int i = 0; i < n && l < 150; i++) l += snprintf(t + l, sizeof t - (size_t)l, " %0*x", (kind == 100 && i == 0) ? 2 : 4, (kind == 100 && i == 0) ? (u8)v[i] : (u16)v[i]);
    }
    if (loglen + (size_t)l + 2 < sizeof logbuf) {
        if (loglen) logbuf[loglen++] = '/';
        memcpy(logbuf + loglen, t, (size_t)l);
        loglen += (size_t)l;
        logbuf[loglen] = 0;
    }
}

/* ---- parsing */
static char *ln;
static size_t linecap;
static bool read_line(FILE *f)
{
    size_t n = 0;
    for (;;) {
        if (n + 2 >= linecap) { linecap = linecap ? linecap * 2 : 1 << 16; ln = realloc(ln, linecap); }
        if (!fgets(ln + n, (int)(linecap - n), f)) { ln[n] = 0; return n > 0; }
        n += strlen(ln + n);
        if (n && ln[n - 1] == '\n') { ln[n - 1] = 0; if (n > 1 && ln[n - 2] == '\r') ln[n - 2] = 0; return true; }
    }
}
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* apply "off:hex off:hex ..." (flag 0 = apply to dst) */
static void apply_regions(const char *s, u8 *dst, size_t size)
{
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        char *e;
        unsigned long off = strtoul(s, &e, 16);
        if (*e != ':') { fprintf(stderr, "r3d-test: bad region '%.20s'\n", s); exit(2); }
        s = e + 1;
        while (hexv(s[0]) >= 0 && hexv(s[1]) >= 0) {
            if (off < size) dst[off] = (u8)(hexv(s[0]) << 4 | hexv(s[1]));
            off++;
            s += 2;
        }
    }
}

typedef struct Case {
    char name[32];
    u32 a[16];
    int na;
    char *in, *pin, *ret, *out, *pout, *lg;
} Case;

static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); memcpy(p, s, n); return p; }

/* ---- the calls */
static char ret[160];
static void call(const Case *c)
{
    const u32 *a = c->a;
    const char *n = c->name;
    strcpy(ret, "-");
    if (!strcmp(n, "outcode16")) { snprintf(ret, sizeof ret, "%02x", r3d_outcode16((s16)a[0], (s16)a[1])); }
    else if (!strcmp(n, "outcode32")) { snprintf(ret, sizeof ret, "%02x", r3d_outcode32((s32)a[0], (s32)a[1])); }
    else if (!strcmp(n, "on_border")) { snprintf(ret, sizeof ret, "%d", r3d_on_border((s32)a[0], (s32)a[1])); }
    else if (!strcmp(n, "flags16")) { r3d_clip_flags16((u16)a[0], (u8)a[1], (u8)a[2], (s16)a[3], (s16)a[4]); }
    else if (!strcmp(n, "flags32")) { snprintf(ret, sizeof ret, "%02x", r3d_clip_flags32((u16)a[0], (u8)a[1], (u8)a[2])); }
    else if (!strcmp(n, "intercept")) {
        s16 x, y;
        u8 f;
        int cf = r3d_clip_intercept((u8)a[0], (s16)a[1], (s16)a[2], &x, &y, &f);
        snprintf(ret, sizeof ret, "%d %04x %04x %02x", cf, (u16)x, (u16)y, f);
    }
    else if (!strcmp(n, "bisect_in")) {
        s32 x = 0, y = 0;
        int cf = r3d_bisect_in((u16)a[0], (u16)a[1], &x, &y);
        if (cf) snprintf(ret, sizeof ret, "1 %02x", r3d_bisect_last_bp);
        else snprintf(ret, sizeof ret, "0 %08x %08x %02x", (u32)x, (u32)y, r3d_bisect_last_bp);
    }
    else if (!strcmp(n, "bisect_out")) {
        s32 x, y;
        r3d_bisect_out((u16)a[0], (u16)a[1], &x, &y);
        snprintf(ret, sizeof ret, "%08x %08x", (u32)x, (u32)y);
    }
    else if (!strcmp(n, "clip_edge")) { r3d_clip_edge((u16)a[0]); }
    else if (!strcmp(n, "note_left")) { r3d_note_left((s16)a[0], (s16)a[1]); }
    else if (!strcmp(n, "note_right")) { r3d_note_right((s16)a[0], (s16)a[1]); }
    else if (!strcmp(n, "note_zclip")) { r3d_note_zclip_point((u16)a[0]); }
    else if (!strcmp(n, "add_edge")) { r3d_poly_add_edge((u16)a[0]); }
    else if (!strcmp(n, "polygon")) {                         /* a0 = edges, a1 = colour, then per edge a flag (1 = near-plane point present) */
        for (u32 i = 0; i < a[0]; i++) {
            u16 rec = (u16)(R3D_EDGES + 24 * i);
            r3d_clip_edge(rec);
            if (a[2 + i]) ER_W(rec, 2) |= 0x4040;
            r3d_poly_add_edge(rec);
        }
        r3d_poly_finish((s16)a[1]);
    }
    else if (!strcmp(n, "setcolor")) { r3d_setcolor_M((u16)a[0]); }
    else if (!strcmp(n, "edge_dda")) { r3d_edge_dda((u16)a[0]); }
    else if (!strcmp(n, "wall_fill")) { r3d_wall_fill((s16)a[0], (s16)a[1], (s16)a[2]); }
    else if (!strcmp(n, "fill_M")) { r3d_fill_M(); }
    else if (!strcmp(n, "line_M")) { r3d_line_M((u16)a[0]); }
    else if (!strcmp(n, "rows_M")) { gfxlib_mode_init('m'); }
    else if (!strcmp(n, "sin")) { snprintf(ret, sizeof ret, "%04x", (u16)math_sin((u16)a[0])); }
    else if (!strcmp(n, "cos")) { snprintf(ret, sizeof ret, "%04x", (u16)math_cos((u16)a[0])); }
    else if (!strcmp(n, "atan2")) { snprintf(ret, sizeof ret, "%04x", math_atan2((s16)a[0], (s16)a[1])); }
    else if (!strcmp(n, "asin")) { snprintf(ret, sizeof ret, "%04x", (u16)math_asin((s16)a[0])); }
    else if (!strcmp(n, "hypot2")) { snprintf(ret, sizeof ret, "%04x", math_hypot2((s16)a[0], (s16)a[1])); }
    else if (!strcmp(n, "mat_vec16")) { mat_vec16((u16)a[0], (u16)a[1]); }
    else if (!strcmp(n, "mat_mul")) { mat_mul_1433_0286((u16)a[0], (u16)a[1], (u16)a[2]); }
    else if (!strcmp(n, "mat_transpose")) { mat_transpose((u16)a[0], (u16)a[1]); }
    else if (!strcmp(n, "mat_copy")) { mat_copy((u16)a[0], (u16)a[1]); }
    else if (!strcmp(n, "vec_mat32")) { vec_mat32((u16)a[0], (u16)a[1], (u16)a[2]); }
    else if (!strcmp(n, "mat_from_euler")) { mat_from_euler((u16)a[0], (u16)a[1], (u16)a[2], (u16)a[3]); }
    else if (!strcmp(n, "hz_draw")) { hz_draw(); }
    else if (!strcmp(n, "sky")) { r3d_draw_sky_ground(DSP((u16)a[0])); }
    else { fprintf(stderr, "r3d-test: unknown routine '%s'\n", n); exit(2); }
}

static int run_case(const Case *c, unsigned idx, int *per_fail)
{
    memcpy(DSP(0), g_base, 65536);
    memcpy(page_buf, g_page, PAGE_SIZE);
    apply_regions(c->in, DSP(0), 65536);
    apply_regions(c->pin, page_buf, PAGE_SIZE);
    memcpy(dg_before, DSP(0), 65536);
    memcpy(page_before, page_buf, PAGE_SIZE);
    loglen = 0; logbuf[0] = 0;
    call(c);
    apply_regions(c->out, dg_before, 65536);
    apply_regions(c->pout, page_before, PAGE_SIZE);
    int bad = 0;
    if (strcmp(ret, c->ret)) { bad = 1; if (*per_fail < 8) fprintf(stderr, "  case %u %s: RET got '%s' want '%s'\n", idx, c->name, ret, c->ret); }
    if (strcmp(logbuf, c->lg)) { bad = 1; if (*per_fail < 8) fprintf(stderr, "  case %u %s: LOG got '%.200s' want '%.200s'\n", idx, c->name, logbuf, c->lg); }
    if (memcmp(DSP(0), dg_before, 65536)) {
        bad = 1;
        if (*per_fail < 8) { size_t i = 0; while (DSP(0)[i] == dg_before[i]) i++; fprintf(stderr, "  case %u %s: DGROUP differs at %04zx (got %02x want %02x)\n", idx, c->name, i, DSP(0)[i], dg_before[i]); }
    }
    if (memcmp(page_buf, page_before, PAGE_SIZE)) {
        bad = 1;
        if (*per_fail < 8) { size_t i = 0; while (page_buf[i] == page_before[i]) i++; fprintf(stderr, "  case %u %s: page differs at %04zx (got %02x want %02x)\n", idx, c->name, i, page_buf[i], page_before[i]); }
    }
    if (bad) (*per_fail)++;
    return bad;
}

int r3d_leaf_selftest(const char *file)
{
    FILE *f = fopen(file, "rb");
    if (!f) { fprintf(stderr, "r3d-test: cannot open %s\n", file); return 77; }
    cur_dgroup = DG_SEG;
    r3d_test_page = page_buf;
    r3d_trace = trace;
    memset(g_base, 0, sizeof g_base);
    memset(g_page, 0, sizeof g_page);
    Case c;
    memset(&c, 0, sizeof c);
    unsigned total = 0, bad = 0;
    int fails = 0;
    char lastname[32] = "";
    unsigned count = 0, cbad = 0;
    bool open = false;
    while (read_line(f)) {
        if (!strncmp(ln, "G M ", 4) || !strncmp(ln, "G T ", 4) || !strncmp(ln, "G P ", 4)) {
            char buf[32];
            snprintf(buf, sizeof buf, "%s", ln + 4);
            char *e;
            unsigned long off = strtoul(ln + 4, &e, 16);
            const char *h = e;
            while (*h == ' ') h++;
            u8 *dst = ln[2] == 'M' ? g_base : ln[2] == 'P' ? g_page : mp(r3d_tab_seg(), 0);
            for (; hexv(h[0]) >= 0 && hexv(h[1]) >= 0; h += 2, off++) dst[off] = (u8)(hexv(h[0]) << 4 | hexv(h[1]));
            (void)buf;
        } else if (!strncmp(ln, "CASE ", 5)) {
            free(c.in); free(c.pin); free(c.ret); free(c.out); free(c.pout); free(c.lg);
            memset(&c, 0, sizeof c);
            char *p = ln + 5, *e;
            int k = 0;
            while (*p && *p != ' ' && k < 31) c.name[k++] = *p++;
            while (*p == ' ') p++;
            while (*p && c.na < 16) { c.a[c.na++] = (u32)strtoul(p, &e, 16); p = e; while (*p == ' ') p++; }
            c.in = dup(""); c.pin = dup(""); c.ret = dup("-"); c.out = dup(""); c.pout = dup(""); c.lg = dup("");
            open = true;
        } else if (open && !strncmp(ln, "IN ", 3)) { free(c.in); c.in = dup(ln + 3); }
        else if (open && !strncmp(ln, "PIN ", 4)) { free(c.pin); c.pin = dup(ln + 4); }
        else if (open && !strncmp(ln, "RET ", 4)) { free(c.ret); c.ret = dup(ln + 4); }
        else if (open && !strncmp(ln, "OUT ", 4)) { free(c.out); c.out = dup(ln + 4); }
        else if (open && !strncmp(ln, "POUT ", 5)) { free(c.pout); c.pout = dup(ln + 5); }
        else if (open && !strncmp(ln, "LOG ", 4)) { free(c.lg); c.lg = dup(ln + 4); }
        else if (open && !strcmp(ln, "END")) {
            if (strcmp(lastname, c.name)) {
                if (lastname[0]) printf("  %-16s %5u cases, %u mismatches\n", lastname, count, cbad);
                snprintf(lastname, sizeof lastname, "%s", c.name);
                count = cbad = 0;
            }
            total++; count++;
            if (run_case(&c, total, &fails)) { bad++; cbad++; }
            open = false;
        }
    }
    if (lastname[0]) printf("  %-16s %5u cases, %u mismatches\n", lastname, count, cbad);
    fclose(f);
    printf("r3d-test: %u cases, %u mismatches\n", total, bad);
    if (bad == 0 && total > 0) printf("r3d-test OK\n");
    return bad || !total ? 1 : 0;
}
