/* f117 --video-selftest: the MGRAPHIC primitives of the port against hashes computed from the ORIGINAL driver under Unicorn
 * (port/notes/s52/video_emu.py -> video_vectors.h). Every scenario fills the pages with the same pattern, calls the same
 * function with the same arguments and hashes the four pages plus the structures the call may change; the Unicorn run and this
 * file must agree bit for bit. Text scenarios need the fonts of MGRAPHIC.EXE in the game folder (skipped without them). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen/module_platform.h"
#include "gen/module_video.h"
#include "host.h"
#include "mem.h"
#include "platform/platform.h"
#include "platform/timer.h"
#include "platform/vga.h"
#include "video/video.h"
#include "video_vectors.h"

static int checks, fails;

static void check(bool ok, const char *what, ...)
{
    checks++;
    if (ok) return;
    fails++;
    if (fails > 2000) return;
    va_list ap;
    va_start(ap, what);
    printf("FAIL: ");
    vprintf(what, ap);
    printf("\n");
    va_end(ap);
}

static u32 crc32_update(u32 crc, const u8 *p, size_t n)
{
    static u32 tab[256];
    if (!tab[1]) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = tab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

#define SC_PAGEW 0x100          /* the structure offsets of video_emu.py inside the scratch block */
#define SC_BLIT  0x200
#define SC_TEXT  0x300
#define SC_STR   0x400
#define SC_WIN   0x600
#define SC_SCAN  0x800
#define SC_TBL   0x1000

static u16 vpage[4];            /* page 0 = A000, 1..3 = 64 KB DOS blocks (the emulated pages have a zero tail, so do these) */
static u16 scratch;

/* mg_harness.lcg_fill: about a quarter of the bytes are 0, the key colour */
static void lcg_fill_buf(u8 *dst, u32 seed, int n)
{
    u32 s = seed;
    for (int i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        u8 b = (u8)(s >> 16);
        dst[i] = (b & 3) == 0 ? 0 : b;
    }
}

static void setup_pages(int seed)
{
    for (int n = 1; n <= 3; n++) page_seg_set(n, vpage[n]);
    for (int n = 0; n < 4; n++) {
        u8 *p = seg_ptr(vpage[n]);
        lcg_fill_buf(p, (u32)(seed * 7 + n), 64000);
        memset(p + 64000, 0, 65536 - 64000);
    }
}

static u32 crc_pages(void)
{
    u32 c = 0;
    for (int n = 0; n < 4; n++) c = crc32_update(c, seg_ptr(vpage[n]), 64000);
    return c;
}

static int dbg_aux;
static u32 aux16(u32 c, int v)
{
    if (dbg_aux) printf("aux16 %04X\n", v & 0xFFFF);
    u8 b[2] = { (u8)v, (u8)(v >> 8) };
    return crc32_update(c, b, 2);
}

static int lcg_rnd(u32 *s, int n)
{
    *s = *s * 1664525u + 1013904223u;
    return (int)((*s >> 16) % (u32)n);
}

static void make_edges(u8 *t, u32 seed)                    /* video_emu.make_edges */
{
    u32 g = seed;
    memset(t, 0, 0x1B8 * 2);
    for (int y = 0; y < 220; y++) {
        int L = lcg_rnd(&g, 340), R, k = lcg_rnd(&g, 20);
        if (k == 0) R = L - (1 + lcg_rnd(&g, 4));
        else if (k == 1) R = L;
        else if (k == 2) { L = R = lcg_rnd(&g, 2) ? 319 : 0; }
        else if (k == 3) { L = lcg_rnd(&g, 4); R = 315 + lcg_rnd(&g, 10); }
        else R = L + lcg_rnd(&g, 90);
        t[2 * y] = (u8)L; t[2 * y + 1] = (u8)(L >> 8);
        t[0x1B8 + 2 * y] = (u8)R; t[0x1B8 + 2 * y + 1] = (u8)(R >> 8);
    }
}

static u16 sprite_bytes(u16 seg)                            /* video_emu.sprite_size */
{
    const u8 *p = mp(seg, 0);
    u16 h = (u16)(p[2] | p[3] << 8), n = 4;
    for (u16 i = 0; i < h; i++) n = (u16)(n + 4 + (p[n] | p[n + 1] << 8));
    return n;
}

static u32 run_vec(const VideoVec *v)
{
    const short *a = v->a;
    u8 *sc = mp(scratch, 0);
    u32 c = 0;
    switch (v->op) {
    case OP_LINE:
        setup_pages(a[7]);
        page_select(a[5]); offset_set((unsigned)(u16)a[6]); color_set(a[4]);
        line(a[0], a[1], a[2], a[3]);
        color_set(a[4] ^ 0x5A); pixel_last();
        return crc_pages();
    case OP_POLY:
        setup_pages(a[7]);
        make_edges(sc + SC_TBL, (u32)a[6]);
        page_select(a[4]); offset_set((unsigned)(u16)a[5]); color_set(a[3]);
        if (a[2] < 0) poly_fill_solid(a[0], sc + SC_TBL, a[1]);
        else poly_fill(a[0], sc + SC_TBL, a[1], a[2]);
        return crc_pages();
    case OP_BLITRECT:
        setup_pages(a[8]);
        blit_rect(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        return crc_pages();
    case OP_RECOLOR:
        setup_pages(a[7]);
        sc[SC_PAGEW] = (u8)a[0]; sc[SC_PAGEW + 1] = 0;
        rect_recolor((int *)(sc + SC_PAGEW), a[1], a[2], a[3], a[4], a[5], a[6]);
        return crc_pages();
    case OP_BLITKEY: {
        setup_pages(a[11]);
        BlitCtx *b = (BlitCtx *)(sc + SC_BLIT);
        b->seg = vpage[a[1]]; b->sx = a[2]; b->sy = a[3]; b->dpage = a[4]; b->dx = a[5]; b->dy = a[6]; b->w = a[7]; b->h = a[8];
        b->ymin = vec_blit_clip[a[9]][0]; b->ymax = vec_blit_clip[a[9]][1];
        b->xmin = vec_blit_clip[a[9]][2]; b->xmax = vec_blit_clip[a[9]][3];
        if (a[0]) blit_key_clip(b); else blit_key(b);
        return crc32_update(crc_pages(), sc + SC_BLIT + 2, 22);   /* not the source segment */
    }
    case OP_TEXT: {
        setup_pages(a[11] + 11);
        TextCtx *t = (TextCtx *)(sc + SC_TEXT);
        t->page = 1; t->flags = a[2]; t->fg = a[3]; t->bg = a[4]; t->x = a[5]; t->y = a[6]; t->font = a[1];
        t->ymin = a[7]; t->ymax = a[8]; t->xmin = a[9]; t->xmax = a[10];
        char *s = (char *)(sc + SC_STR);
        memcpy(s, vec_str[a[11]], (size_t)vec_str_len[a[11]] + 1);
        switch (a[0]) {
        case 0: text_noclip(t, s); break;
        case 1: text_clip_y(t, s); break;
        case 2: text_clip_xl(t, s); break;
        case 3: text_clip_all(t, s); break;
        default: text_clip_xr(t, s); break;
        }
        c = crc32_update(crc_pages(), sc + SC_TEXT, 22);
        c = crc32_update(c, sc + SC_STR, (size_t)vec_str_len[a[11]] + 1);
        u8 col = video_color();
        return crc32_update(c, &col, 1);
    }
    case OP_FCW:
        for (int ch = -3; ch < 260; ch++) c = aux16(c, font_char_width(ch, a[0]));
        return c;
    case OP_HUD:
        setup_pages(a[6]);
        page_select(a[5]); color_set(a[4]);
        hud_tick_strip(a[0], a[1], a[2], a[3] & 0xFF);
        return crc_pages();
    case OP_SPRITE: {
        setup_pages(a[0]);
        const VecSprite *sp = &vec_sprites[a[1]];
        u16 base = (u16)pool_init();
        if (base) memset(mp(base, 0), 0, 0x2000 * 16);       /* the emulated pool starts zeroed; sprite_bytes reads past header-only sprites */
        c = aux16(0, base ? 1 : 0);
        u16 segs[8] = { 0 };
        for (int g = 0; g < sp->ng; g++) {
            const VecGrab *gr = &sp->g[g];
            u16 seg = (u16)sprite_grab(gr->page, gr->x, gr->y, gr->w, gr->h);
            segs[g] = seg;
            c = aux16(c, seg ? (u16)(seg - base) : 0xFFFF);
            if (seg) c = crc32_update(c, mp(seg, 0), sprite_bytes(seg));
        }
        for (int d = 0; d < sp->nd; d++) {
            const VecDraw *dr = &sp->d[d];
            TextCtx *w = (TextCtx *)(sc + SC_WIN);
            memset(w, 0, sizeof *w);
            w->page = dr->wpage; w->ymin = dr->ymin; w->ymax = dr->ymax; w->xmin = dr->xmin; w->xmax = dr->xmax;
            s16 *r = sprite_scaled(w, dr->x, dr->y, dr->dw, dr->dh, 0, segs[dr->idx]);
            if (dbg_aux) printf("rect %d %d %d %d\n", r[0], r[1], r[2], r[3]);
            c = crc32_update(c, (const u8 *)r, 8);
        }
        u32 pc = crc_pages();
        u8 pb[4] = { (u8)pc, (u8)(pc >> 8), (u8)(pc >> 16), (u8)(pc >> 24) };
        c = crc32_update(c, pb, 4);
        u16 freed = (u16)pool_free();
        return aux16(c, freed == base ? 1 : 0);
    }
    case OP_FX:
        vga_set_mode(0x13);
        shake_set(a[0]);
        for (int i = 0; i < a[1]; i++) {
            explosion_fx_tick();
            u8 rgb[3];
            vga_dac_get(13, &rgb[0], &rgb[1], &rgb[2]);
            vga_out(0x3D4, 0x0D);
            u8 b[4] = { rgb[0], rgb[1], rgb[2], vga_in(0x3D5) };
            c = crc32_update(c, b, 4);
        }
        return c;
    case OP_MISC: {
        setup_pages(a[5]);
        switch (a[0]) {
        case 0: page_select(a[2]); page_load(vpage[a[1]]); break;
        case 1: {
            flip(a[1]);
            u8 fl = (u8)flip_flag();
            return crc32_update(crc_pages(), &fl, 1);
        }
        case 2: clear_screen(); break;
        case 3:
            lcg_fill_buf(sc + SC_SCAN, (u32)(a[5] + 5), 320);
            scanline_put(vpage[a[1]], (unsigned)(u16)a[2], sc + SC_SCAN);
            break;
        default: clear_es(vpage[a[1]]); break;
        }
        return crc_pages();
    }
    case OP_CONST: {
        static const int ys[4] = { 0, 1, 100, 199 };
        c = aux16(c, (int)get_page_bytes()); c = aux16(c, (int)get_view_off_a()); c = aux16(c, (int)get_view_off_b());
        c = aux16(c, (int)offset_get()); c = aux16(c, flip_flag()); c = aux16(c, (int)get_page_bytes_b());
        c = aux16(c, (int)pixel_offset(5, 7)); c = aux16(c, get_const_3()); c = aux16(c, get_const_0()); c = aux16(c, get_const_1());
        offset_set_r(0x1234); c = aux16(c, (int)offset_get());
        offset_clear(); c = aux16(c, (int)offset_get());
        offset_set(0xABCD); c = aux16(c, (int)offset_get());
        for (int i = 0; i < 4; i++) c = aux16(c, (int)row_offset(ys[i]));
        page_seg_set(2, 0x4321); c = aux16(c, (int)page_es(2));
        page_seg_set_b(0x1357, 3); c = aux16(c, (int)page_es(3));
        seg_set(0x4444); c = aux16(c, (int)seg_get());
        page_select_1(); c = aux16(c, (int)seg_get());
        page_select_r(2); c = aux16(c, (int)seg_get());
        return c;
    }
    case OP_GFXLINE: {                                  /* the program's own line: START's DGROUP tables, plat_prog = START */
        setup_pages(a[6]);
        u16 dg;
        if (!dos_mem_alloc(0x1000, &dg)) host_fatal("video-selftest: no memory");
        memset(mp(dg, 0), 0, 0x10000);
        u16 save_dg = cur_dgroup;
        ProgId save_prog = plat_prog;
        cur_dgroup = dg;
        plat_prog = PROG_START;
        for (int n = 0; n < 4; n++) DSW(0xCAB0 + 2 * n) = vpage[n];
        DSW(0xE000) = (u16)a[0];
        gfx_line((int *)DSP(0xE000), a[1], a[2], a[3], a[4], a[5]);
        cur_dgroup = save_dg;
        plat_prog = save_prog;
        return crc_pages();
    }
    case OP_PALW: {
        vga_set_mode(0x13);                             /* a zeroed DAC */
        u8 *rgb = sc + 0x1100;
        lcg_fill_buf(rgb, (u32)a[3], 768);
        for (int i = 0; i < 768; i++) rgb[i] &= 0x3F;
        mem[0x489] = a[2] ? 2 : 0;
        pal_write_vsync(rgb, a[0], a[1]);
        mem[0x489] = 0;
        u8 dac[768];
        dac_get(0, 256, dac);
        return crc32_update(0, dac, 768);
    }
    default:
        return 0;
    }
}

/* The routines whose original needs the program's own DGROUP and interrupts (checked by hand): gfx_fill_rect, pal_set_default,
 * pal_apply_queue (the timer ISR sets the flag it waits for), the three palette helpers. */
static void tick_sets_flag(void) { DSB(0xADFC) = 1; }

static void test_program_side(void)
{
    dos_arena_reset(PSP_SEG, 0x7C);
    vpage[0] = 0xA000;
    for (int n = 1; n <= 3; n++)
        if (!dos_mem_alloc(0x1000, &vpage[n])) host_fatal("video-selftest: no memory");
    u16 dg;
    if (!dos_mem_alloc(0x1000, &dg)) host_fatal("video-selftest: no memory");
    memset(mp(dg, 0), 0, 0x10000);
    video_init();
    setup_pages(1);
    u16 save_dg = cur_dgroup;
    ProgId save_prog = plat_prog;
    cur_dgroup = dg;
    plat_prog = PROG_START;

    /* gfx_fill_rect: the rectangle in the colour of the window, nothing else, draw segment kept */
    static u8 before[64000];
    memcpy(before, seg_ptr(vpage[1]), 64000);
    DSW(0x100) = 1;
    DSB(0x104) = 77;
    seg_set(0x1234);
    gfx_fill_rect(DSP(0x100), 10, 5, 40, 20);
    int bad = 0;
    const u8 *after = seg_ptr(vpage[1]);
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            bool in = x >= 10 && x <= 40 && y >= 5 && y <= 20;
            if (after[y * 320 + x] != (in ? 77 : before[y * 320 + x])) bad++;
        }
    check(bad == 0, "gfx_fill_rect: %d pixels wrong", bad);
    check(seg_get() == 0x1234, "gfx_fill_rect keeps the draw segment");
    check(video_color() == 77, "gfx_fill_rect sets the colour");
    gfx_fill_rect(DSP(0x100), 50, -3, 60, 9);                           /* y0 < 0: nothing drawn, no crash */
    check(memcmp(seg_ptr(vpage[1]) + 5 * 320 + 100, before + 5 * 320 + 100, 20) == 0, "gfx_fill_rect y0 < 0 draws nothing");

    /* pal_set_default (the program's palette buffer, START 64E3) and pal_set */
    u8 pal[768];
    for (int i = 0; i < 768; i++) pal[i] = (u8)((i * 5) & 0x3F);
    memcpy(DSP(0x64E3), pal, 768);
    vga_set_mode(0x13);
    pal_set_default();
    u8 got[768];
    dac_get(0, 256, got);
    check(memcmp(got, pal, 768) == 0, "pal_set_default");
    for (int i = 0; i < 768; i++) pal[i] = (u8)((i * 3 + 1) & 0x3F);
    memcpy(DSP(0x200), pal, 768);
    pal_set(DSP(0x200));
    dac_get(0, 256, got);
    check(memcmp(got, pal, 768) == 0, "pal_set");

    /* pal_apply_queue: waits for the tick flag, applies the records {first, rgb ptr, count} and empties the queue */
    static const u8 rgb1[6] = { 1, 2, 3, 4, 5, 6 }, rgb2[3] = { 60, 61, 62 };
    memcpy(DSP(0x300), rgb1, 6);
    memcpy(DSP(0x310), rgb2, 3);
    DSW(0x64A6) = 100; DSW(0x64A8) = 0x300; DSW(0x64AA) = 2;
    DSW(0x64AC) = 200; DSW(0x64AE) = 0x310; DSW(0x64B0) = 1;
    DSB(0x64E2) = 2;
    timer_set_hook(tick_sets_flag);
    timer_install();
    pal_apply_queue();
    timer_remove();
    timer_set_hook(NULL);
    u8 c[3];
    dac_get(100, 2, got);
    vga_dac_get(200, &c[0], &c[1], &c[2]);
    check(memcmp(got, rgb1, 6) == 0 && c[0] == 60 && c[1] == 61 && c[2] == 62 && DSB(0x64E2) == 0, "pal_apply_queue");

    cur_dgroup = save_dg;
    plat_prog = save_prog;
}

int video_selftest(void)
{
    int ran = 0, skipped = 0;
    for (size_t i = 0; i < sizeof video_vec / sizeof video_vec[0]; i++) {
        const VideoVec *v = &video_vec[i];
        video_init();
        if ((v->op == OP_TEXT || v->op == OP_FCW) && !video_fonts_loaded()) { skipped++; continue; }
        dos_arena_reset(PSP_SEG, 0x7C);
        vpage[0] = 0xA000;
        for (int n = 1; n <= 3; n++)
            if (!dos_mem_alloc(0x1000, &vpage[n])) host_fatal("video-selftest: no memory");
        if (!dos_mem_alloc(0x200, &scratch)) host_fatal("video-selftest: no memory");
        memset(mp(scratch, 0), 0, 0x2000);
        if (v->op == OP_SPRITE) {                     /* the emulated pool is 2000h paragraphs: leave that much */
            u16 filler, big = dos_mem_largest();
            if (big > 0x2000 && !dos_mem_alloc((u16)(big - 0x2000), &filler)) host_fatal("video-selftest: no memory");
        }
        const char *dump0 = getenv("F117_VEC_DUMP");
        dbg_aux = dump0 && (size_t)atoi(dump0) == i;
        u32 got = run_vec(v);
        dbg_aux = 0;
        ran++;
        const char *dump = getenv("F117_VEC_DUMP");         /* developer aid: the pages after scenario N into work/s52/c_pages.bin */
        if (dump && (size_t)atoi(dump) == i) {
            FILE *f = fopen("work/s52/c_pages.bin", "wb");
            if (f) { for (int n = 0; n < 4; n++) fwrite(seg_ptr(vpage[n]), 1, 64000, f); fclose(f); }
        }
        check(got == v->crc, "scenario %zu (op %d): hash %08X, the original gives %08X", i, v->op, got, v->crc);
    }
    test_program_side();
    printf("video-selftest: %d scenarios against the original MGRAPHIC (%d text scenarios skipped: no MGRAPHIC.EXE): %d checks, %d failed\n",
           ran, skipped, checks, fails);
    if (fails) { printf("video-selftest FAILED\n"); return 1; }
    printf("video-selftest OK\n");
    return 0;
}
