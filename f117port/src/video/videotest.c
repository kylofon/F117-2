/* Headless checks and the composed screen of the video module (S5.2).
 *   f117 --video-selftest              drivers primitives against vectors computed from the ORIGINAL MGRAPHIC under Unicorn
 *                                      (port/notes/s52/video_emu.py -> video_vectors.h); text needs the fonts of MGRAPHIC.EXE
 *   f117 --decoder-test REFDIR         every PIC / PAN / PAL / font of the game folder against the output of the Phase 2
 *                                      Python decoders (port/notes/s52/decoder_ref.py writes REFDIR); 77 = no game files
 *   f117 --video-test                  a composed screen (picture, fonts, sprites, lines, a palette fade) for a visual check;
 *                                      F117_SNAPSHOT_DIR=dir writes it as PNG
 * Run by ctest (CMakeLists.txt). */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen/module_platform.h"
#include "gen/module_video.h"
#include "host.h"
#include "mem.h"
#include "platform/platform.h"
#include "platform/vga.h"
#include "video/video.h"

static int checks, fails;

static void check(bool ok, const char *what, ...)
{
    checks++;
    if (ok) return;
    fails++;
    if (fails > 40) return;
    va_list ap;
    va_start(ap, what);
    printf("FAIL: ");
    vprintf(what, ap);
    printf("\n");
    va_end(ap);
}

/* ------------------------------------------------------------------------------------------ decoder test */
static u8 *slurp(const char *dir, const char *name, size_t *len)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = malloc(n > 0 ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b && len) *len = (size_t)n;
    return b;
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

/* index.txt lines: "pic NAME", "pal NAME", "pan NAME", "fonts" */
int video_decoder_test(const char *refdir)
{
    size_t ilen;
    char *idx = (char *)slurp(refdir, "index.txt", &ilen);
    if (!idx) { printf("decoder-test: no index.txt in %s\n", refdir); return 1; }
    idx = realloc(idx, ilen + 1);
    idx[ilen] = 0;
    int npic = 0, npal = 0, npan = 0, ncel = 0, nfont = 0, nglyph = 0, nfront = 0;
    char first_pics[4][64];
    static u8 px[64000], scr[64000];
    char *sp1 = NULL;
    for (char *line = strtok_r(idx, "\r\n", &sp1); line; line = strtok_r(NULL, "\r\n", &sp1)) {
        char kind[16], name[128] = "";
        if (sscanf(line, "%15s %127s", kind, name) < 1) continue;
        char fn[160];
        size_t n;
        if (!strcmp(kind, "pic")) {
            snprintf(fn, sizeof fn, "%s.PIC", name);
            bool ok = pic_decode_file(fn, px);
            snprintf(fn, sizeof fn, "%s.pix", name);
            u8 *ref = slurp(refdir, fn, &n);
            check(ok && ref && n == 64000 && !memcmp(px, ref, 64000), "PIC %s differs from tools/picdec.py", name);
            free(ref);
            if (npic < 4) snprintf(first_pics[npic], sizeof first_pics[0], "%s", name);
            npic++;
        } else if (!strcmp(kind, "pal")) {
            u8 pal[768];
            snprintf(fn, sizeof fn, "%s.PAL", name);
            bool ok = pal_read_file(fn, pal);
            snprintf(fn, sizeof fn, "%s.pal", name);
            u8 *ref = slurp(refdir, fn, &n);
            check(ok && ref && n == 768 && !memcmp(pal, ref, 768), "PAL %s", name);
            free(ref);
            npal++;
        } else if (!strcmp(kind, "pan")) {
            /* reference: NAME.pan = speed, ncels (u16 each), 768 palette bytes (8-bit, panlib's), then one CRC32 per frame */
            snprintf(fn, sizeof fn, "%s.PAN", name);
            char *path = host_game_path(fn, false);
            size_t flen = 0;
            u8 *file = NULL;
            if (path) { FILE *f = fopen(path, "rb"); host_free(path); if (f) { fseek(f, 0, SEEK_END); flen = (size_t)ftell(f); fseek(f, 0, SEEK_SET); file = malloc(flen); if (file && fread(file, 1, flen, f) != flen) { free(file); file = NULL; } fclose(f); } }
            snprintf(fn, sizeof fn, "%s.pan", name);
            u8 *ref = slurp(refdir, fn, &n);
            PanFile pf;
            bool ok = file && ref && pan_parse(file, flen, &pf);
            check(ok, "PAN %s: cannot parse", name);
            if (ok) {
                int rsp = ref[0] | ref[1] << 8, rn = ref[2] | ref[3] << 8;
                check(pf.speed == rsp && pf.ncels == rn && n == 4 + 768 + 4 * (size_t)rn, "PAN %s: header speed %d/%d cels %d/%d", name, pf.speed, rsp, pf.ncels, rn);
                bool palok = true;
                for (int i = 0; i < 768; i++) if (pal6_to_8(pf.pal6[i]) != ref[4 + i]) palok = false;
                check(palok, "PAN %s: palette", name);
                memset(scr, 0, sizeof scr);
                int bad = 0;
                for (int c = 0; c < pf.ncels && c < rn; c++) {
                    pan_apply_cel(&pf, c, scr);
                    u32 crc = crc32_update(0, scr, 64000);
                    const u8 *r = ref + 4 + 768 + 4 * c;
                    u32 want = (u32)(r[0] | r[1] << 8 | r[2] << 16 | (u32)r[3] << 24);
                    if (crc != want) bad++;
                    ncel++;
                }
                check(bad == 0, "PAN %s: %d of %d frames differ from tools/panlib.py", name, bad, pf.ncels);
                pan_free(&pf);
            }
            free(file);
            free(ref);
            npan++;
        } else if (!strcmp(kind, "fonts")) {
            /* fonts.txt: "F i first last fixed height spacing extra", then "W v v v..." (proportional) and "G c hex.." */
            size_t tl;
            char *t = (char *)slurp(refdir, "fonts.txt", &tl);
            check(t != NULL, "fonts.txt missing");
            if (!t) continue;
            t = realloc(t, tl + 1);
            t[tl] = 0;
            FontInfo fi;
            bool have = false;
            int cur = -1;
            char *sp2 = NULL;
            for (char *l = strtok_r(t, "\r\n", &sp2); l; l = strtok_r(NULL, "\r\n", &sp2)) {
                if (l[0] == 'F') {
                    int i, first, last, fixed, h, sp, extra;
                    sscanf(l, "F %d %d %d %d %d %d %d", &i, &first, &last, &fixed, &h, &sp, &extra);
                    have = video_font_info(i, &fi);
                    check(have && fi.first == first && fi.last == last && fi.fixed == fixed && fi.height == h && fi.spacing == sp && fi.extra == extra,
                          "font %d header", i);
                    cur = i;
                    nfont++;
                } else if (l[0] == 'W' && have) {
                    char *p = l + 1;
                    for (int c = fi.first; c <= fi.last; c++) {
                        int v = (int)strtol(p, &p, 10);
                        check(video_font_width(&fi, c) == v, "font %d width of %d", cur, c);
                    }
                } else if (l[0] == 'G' && have) {
                    int c, off;
                    char *p = l + 1;
                    c = (int)strtol(p, &p, 10);
                    int rows = fi.height + fi.extra;
                    bool ok = true;
                    for (int r = 0; r < rows; r++) {
                        off = (int)strtol(p, &p, 16);
                        if (video_font_row(&fi, c, r) != off) ok = false;
                    }
                    check(ok, "font %d glyph %d rows", cur, c);
                    nglyph++;
                }
            }
            free(t);
        }
    }
    /* the four front ends (pic_to_page, pic_to_seg_a/b/c: DOS handle or picture held in memory) on the first pictures */
    dos_arena_reset(PSP_SEG, 0x7C);
    video_init();
    u16 dst, bufseg;
    if (!dos_mem_alloc(0x1000, &dst) || !dos_mem_alloc(0x1000, &bufseg)) host_fatal("decoder-test: no memory");
    page_seg_set(1, dst);
    for (int k = 0; k < npic && k < 4; k++) {
        char fn[96];
        size_t rn;
        snprintf(fn, sizeof fn, "%s.pix", first_pics[k]);
        u8 *ref = slurp(refdir, fn, &rn);
        snprintf(fn, sizeof fn, "%s.PIC", first_pics[k]);
        for (int mode = 0; mode < 4; mode++) {
            memset(mp(dst, 0), 0x55, 65536);
            if (mode == 3) {
                char *path = host_game_path(fn, false);
                FILE *f = path ? fopen(path, "rb") : NULL;
                host_free(path);
                size_t fl = 0;
                if (f) { fl = fread(mp(bufseg, 0), 1, 0xF000, f); fclose(f); }
                pic_to_seg_b(bufseg, dst);
                (void)fl;
            } else {
                int fd = dos_open(fn, 0);
                if (mode == 0) pic_to_page(fd, 1);
                else if (mode == 1) pic_to_seg_a(fd, dst);
                else pic_to_seg_c(fd, dst);
                dos_close(fd);
            }
            check(ref && rn == 64000 && !memcmp(mp(dst, 0), ref, 64000), "%s: front end %d differs", first_pics[k], mode);
            nfront++;
        }
        free(ref);
    }
    free(idx);
    printf("decoder-test: %d PIC (%d front-end runs), %d PAL, %d PAN (%d frames), %d fonts (%d glyphs): %d checks, %d failed\n",
           npic, nfront, npal, npan, ncel, nfont, nglyph, checks, fails);
    if (fails) { printf("decoder-test FAILED\n"); return 1; }
    printf("decoder-test OK\n");
    return 0;
}

/* ------------------------------------------------------------------------------------------ the composed screen */
static int px_diff(const u8 *a, const u8 *b, size_t n)
{
    int d = 0;
    for (size_t i = 0; i < n; i++) d += a[i] != b[i];
    return d;
}

static void put_text(TextCtx *c, int font, int x, int y, int fg, int bg, int flags, char *s)
{
    c->page = 1;
    c->font = (s16)font;
    c->x = (s16)x;
    c->y = (s16)y;
    c->fg = (s16)fg;
    c->bg = (s16)bg;
    c->flags = (s16)flags;
    text_noclip(c, s);
}

/* Draws picture + palette, text in each font (transparent, opaque, colour bytes, clipped), sprites (plain, scaled, mirrored,
 * flipped, clipped by the window), lines, polygon modes, a key-colour blit; flips to the screen and runs a palette fade.
 * The script checks what it can on its own (the flip copied the page, the fade ends on the original palette). */
int video_screen_test(bool wait)
{
    char *probe = host_game_path("FLAG.PIC", false);
    if (!probe || !video_fonts_loaded()) {
        printf("video-test: no game files in %s (FLAG.PIC, MGRAPHIC.EXE)\n", host_game_dir());
        return 77;
    }
    host_free(probe);
    dos_arena_reset(PSP_SEG, 0x7C);
    mode13_set();
    u16 p1 = (u16)gfx_alloc(2);
    page_seg_set(1, p1);
    u16 scratch;
    if (!dos_mem_alloc(0x40, &scratch)) host_fatal("video-test: no memory");
    memset(mp(scratch, 0), 0, 0x400);
    TextCtx *tc = (TextCtx *)mp(scratch, 0);

    u8 pal[768];
    memset(pal, 0, sizeof pal);
    char name[] = "FLAG.PIC";
    bool have_pic = false, have_pal = pal_read_file("FLAG.PAL", pal);
    int fd = dos_open(name, 0);
    pic_to_page(fd, 1);
    have_pic = true;
    dos_close(fd);
    if (!have_pal) {                                       /* no palette file: a grey ramp so the text is visible */
        for (int i = 0; i < 256; i++) pal[3 * i] = pal[3 * i + 1] = pal[3 * i + 2] = (u8)(i >> 2);
    }
    dac_set(0, 256, pal);
    check(have_pic, "picture");

    /* a panel under the text so every font is readable whatever the picture is */
    page_select(1);
    for (int y = 4; y < 112; y++) { color_set(0); line(4, y, 315, y); }
    static const char *const sample = "F-117A Nighthawk 0123456789 :/-.";
    static char buf[64];
    for (int f = 0; f < 7; f++) {
        strcpy(buf, sample);
        put_text(tc, f, 8, 8 + f * 13, 15, 0, 0, buf);                       /* transparent, white */
        strcpy(buf, "\x8C" "red " "\x8E" "yellow " "\x8A" "green");
        put_text(tc, f, 190, 8 + f * 13, 15, 0, 0, buf);                      /* colour bytes */
    }
    strcpy(buf, "opaque text, font 4");
    put_text(tc, 4, 8, 98, 14, 4, 1, buf);                                    /* opaque */
    strcpy(buf, "clipped at the window edges");
    tc->xmin = 40; tc->xmax = 120; tc->ymin = 0; tc->ymax = 200;
    tc->page = 1; tc->font = 3; tc->x = 20; tc->y = 90; tc->fg = 11; tc->flags = 0;
    text_clip_all(tc, buf);
    check(tc->x >= 40, "left clip moved x to %d", tc->x);

    /* sprites: grab 48x40 from the picture area and draw it scaled, mirrored, flipped and clipped */
    unsigned pool = pool_init();
    check(pool != 0, "pool_init");
    unsigned spr = sprite_grab(1, 140, 130, 48, 40);
    check(spr != 0, "sprite_grab");
    tc->page = 1; tc->xmin = 0; tc->xmax = 319; tc->ymin = 0; tc->ymax = 199;
    s16 *r = sprite_scaled(tc, 4, 150, 48, 40, 0, spr);
    check(r[2] == 48 && r[3] == 40, "plain sprite visible %dx%d", r[2], r[3]);
    r = sprite_scaled(tc, 60, 130, 96, 70, 0, spr);
    check(r[2] == 96 && r[3] == 70, "2x sprite");
    r = sprite_scaled(tc, 250, 130, -48, 40, 0, spr);
    check(r[2] == 48, "mirrored sprite");
    r = sprite_scaled(tc, 200, 199, 48, -40, 0, spr);
    check(r[3] == 1, "flipped sprite clipped at the bottom: %d rows", r[3]);
    tc->xmin = 160; tc->xmax = 230; tc->ymin = 120; tc->ymax = 160;
    r = sprite_scaled(tc, -10, -10, 90, 60, 0, spr);
    check(r[2] > 0 && r[3] > 0, "window-clipped sprite");
    pool_free();

    /* lines, polygon modes */
    page_select(1);
    color_set(14);
    line(0, 199, 319, 120);
    line(0, 120, 319, 199);
    color_set(12);
    line(10, 115, 10, 195);
    line(10, 115, 300, 115);
    u16 tbl;
    if (!dos_mem_alloc(0x40, &tbl)) host_fatal("video-test: no memory");
    u8 *t = mp(tbl, 0);
    memset(t, 0, 0x400);
    for (int y = 100; y <= 110; y++) {                     /* a small fan-shaped polygon, rows 100..110 */
        *(u16 *)(t + 2 * y) = (u16)(260 - (y - 100) * 2);
        *(u16 *)(t + 0x1B8 + 2 * y) = (u16)(290 + (y - 100) * 2);
    }
    color_set(9);
    poly_fill(100, t, 110, 0);
    offset_set(0);

    /* flip to the screen and compare */
    flip(0x7D00);
    check(flip_flag() == 1, "flip flag");
    check(px_diff(seg_ptr(0xA000), seg_ptr(p1), 64000) == 0, "A000 equals the off-screen page after flip");
    for (int i = 0; i < 4; i++) host_pump();
    host_snapshot_now("video_screen");

    /* palette fade down and up; the DAC ends on the original palette */
    u8 cur[768], back[768];
    for (int step = 16; step >= 0; step--) {
        for (int i = 0; i < 768; i++) cur[i] = (u8)(pal[i] * step / 16);
        pal_write_vsync(cur, 0, 256);
        if (step == 8) host_snapshot_now("video_fade_half");
    }
    for (int step = 0; step <= 16; step++) {
        for (int i = 0; i < 768; i++) cur[i] = (u8)(pal[i] * step / 16);
        pal_write_vsync(cur, 0, 256);
    }
    dac_get(0, 256, back);
    check(memcmp(back, pal, 768) == 0, "fade ends on the original palette");

    if (!host_quit_requested()) {
        printf("video-test: %d checks, %d failed; picture, fonts, sprites and the fade are on screen", checks, fails);
        if (getenv("F117_SNAPSHOT_DIR")) printf(" (PNGs in %s)", getenv("F117_SNAPSHOT_DIR"));
        printf("\n");
        if (wait && !getenv("F117_SNAPSHOT_DIR")) {
            printf("video-test: close the window to leave\n");
            while (!host_quit_requested()) host_pump();
        }
    }
    printf(fails ? "video-test FAILED\n" : "video-test OK\n");
    return fails ? 1 : 0;
}
