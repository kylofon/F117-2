/* The MGRAPHIC driver (MGRAPHIC.EXE, code segment 019C) as C functions: slots 0-83 of port/spec/video.md.
 *
 * The driver is bound as a static function table in the port (platform.md section 6), so the slots are plain functions.
 * Every function cites its slot, original address (019C:xxxx) and spec section. The driver's working variables are C
 * statics (video.md section 1 says an ordinary static is fine); the fonts live in the driver image, which video_init()
 * reads from the game folder (MGRAPHIC.EXE is read, never shipped).
 *
 * Faithful quirks kept (all verified against the disassembly, work/s32/mg.asm, and against the original code under
 * Unicorn: port/notes/s52/video_emu.py):
 *   - text: the vertical clip works on BYTES (y low byte, height), the draw routine writes the last colour byte back to
 *     ctx->fg and sets the driver's current colour (slot 33) to the context's foreground; glyph rows are read as 16-bit
 *     words, so an 8-pixel proportional glyph in opaque mode takes its spacing column from the NEXT glyph's bits;
 *   - line: endpoints are compared UNSIGNED, a zero length makes one pixel; poly_fill compares the edge tables UNSIGNED;
 *   - rect_recolor / blit_key rows and widths of 0 loop 65536 times (the original uses `loop`);
 *   - nothing is clipped, offsets are 16-bit and wrap inside the 64 KB segment.
 * PORT: row offsets are computed as y*320 (the original reads cs:0004 + 2*y, which is only valid for y < 200). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen/module_video.h"
#include "host.h"
#include "mem.h"
#include "platform/vga.h"
#include "video/video.h"

/* ------------------------------------------------------------------ driver state (cs:0194.. and data 1800..1982) */
static u16 page_tab[8] = { 0xA000 };       /* cs:0787, five words; PORT: room for stray page numbers (they read 0) */
static u16 draw_seg;                       /* cs:0194 */
static u16 draw_off;                       /* cs:0196 */
static u8  flip_last = 1;                  /* cs:0198 (the image holds 1) */
static u16 flip_disable;                   /* cs:11DC, never set */
static u8  shake;                          /* cs:1250 */
static u8  colour = 0x0F;                  /* data 1866 */
static u16 last_x, last_y;                 /* data 185A, 185C */
static u16 rng = 0x04D2;                   /* data 1982 */
static u16 data_cc, data_ce;               /* data 00CC, 00CE: written, never read */

/* sprite pool: cs:082A..083C */
static u16 pool_off, pool_seg, pool_base, pool_end, pool_limit;     /* 82A 82C 82E 830 832 */
static u16 enc_lead, enc_trail, enc_len, enc_h, enc_w;              /* 834 836 838 83A 83C */

/* The driver image (data segment = image offset 0); fonts at 00D0. */
static u8 *img;
static size_t img_len;

static inline u16 rowoff(u16 y) { return (u16)(y * 320u); }

u8 *seg_ptr(u16 seg)
{
    if (seg >= 0xA000 && seg < 0xB000) return vga_page(seg);
    return mp(seg, 0);
}

u16 video_draw_seg(void) { return draw_seg; }
u16 video_draw_off(void) { return draw_off; }
u8  video_color(void) { return colour; }
u16 video_page_seg(int page) { return (unsigned)page < 8 ? page_tab[page] : 0; }
static inline u16 page_seg(int page) { return video_page_seg((u16)page); }

/* ------------------------------------------------------------------ the image and the fonts */
static inline u8  ib(unsigned o) { return o < img_len ? img[o] : 0; }
static inline u16 iw(unsigned o) { return (u16)(ib(o) | ib(o + 1) << 8); }

bool video_fonts_loaded(void) { return img != NULL; }

/* MGRAPHIC.EXE: MZ header, image at header paragraphs * 16 (RE_GUIDE 2.2); the data segment is the load segment. */
static void load_image(void)
{
    char *path = host_game_path("MGRAPHIC.EXE", false);
    if (!path) return;
    FILE *f = fopen(path, "rb");
    host_free(path);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = n > 64 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n && buf[0] == 'M' && buf[1] == 'Z') {
        size_t hdr = (size_t)(buf[8] | buf[9] << 8) * 16;
        if (hdr < (size_t)n) {
            img_len = (size_t)n - hdr;
            img = malloc(img_len);
            if (img) memcpy(img, buf + hdr, img_len);
        }
    }
    free(buf);
    fclose(f);
}

bool video_font_info(int font, FontInfo *out)
{
    if (!img || font < 0 || font >= (int)iw(0xD0)) return false;
    int p = iw((unsigned)(0xD2 + 2 * font));
    out->ptr = p;
    out->first = ib((unsigned)p - 8);
    out->last = ib((unsigned)p - 7);
    out->fixed = ib((unsigned)p - 5);
    out->height = ib((unsigned)p - 4);
    out->spacing = ib((unsigned)p - 3);
    out->extra = ib((unsigned)p - 2);
    return true;
}

int video_font_width(const FontInfo *f, int ch)
{
    if (ch < f->first || ch > f->last) return 0;
    return ib((unsigned)(f->ptr - 9 - (f->last - ch)));
}

int video_font_row(const FontInfo *f, int ch, int row)
{
    return ib((unsigned)(f->ptr + row * (f->last - f->first + 1) + (ch - f->first)));
}

/* Called once, when the chain loads the drivers: the driver's variables as they are in the image. */
void video_init(void)
{
    memset(page_tab, 0, sizeof page_tab);
    page_tab[0] = 0xA000;
    draw_seg = 0;
    draw_off = 0;
    flip_last = 1;
    flip_disable = 0;
    shake = 0;
    colour = 0x0F;
    last_x = last_y = 0;
    rng = 0x04D2;
    pool_off = pool_seg = pool_base = pool_end = pool_limit = 0;
    enc_lead = enc_trail = enc_len = enc_h = enc_w = 0;
    free(img);
    img = NULL;
    img_len = 0;
    load_image();
}

/* ------------------------------------------------------------------ slot 0 gfx_alloc (04FE) spec "Key findings" */
unsigned gfx_alloc(int n)
{
    if (n == 0) return page_tab[0];
    u16 seg;
    if (!dos_mem_alloc(0x0FA0, &seg)) {                     /* 0FA00h bytes = one page */
        vga_set_mode(0x03);
        host_fatal("Insufficient memory for MCGA graphics");
    }
    return seg;
}

/* ------------------------------------------------------------------ text (slots 1-10, 47): spec section 3 */
/* The engine of 03E8 (init), 031D (right clip), 02B9 (left clip), 037F (vertical clip) and 061A (draw). */
enum { CLIP_R = 1, CLIP_L = 2, CLIP_V = 4 };

typedef struct TextState {
    u16 gptr;           /* 17F8 glyph data pointer (the font pointer)                        */
    u16 wtab;           /* 17FA width table base = p - 9 - (last - first)                    */
    u16 stride;         /* 17FC row stride in the glyph data                                 */
    u16 x, y;           /* 17FE, 1800                                                        */
    u8  fixed;          /* 1802 fixed advance incl. spacing, 0 = proportional                */
    u8  spacing;        /* 1803                                                              */
    u8  shift;          /* 1804                                                              */
    u8  first, last;    /* 1805, 1806                                                        */
    u8  skip_l;         /* 1807 pixels cut from the left of the first glyph                  */
    u8  trim_r;         /* 1808 pixels cut from the right of the last glyph                  */
    u16 height;         /* 1809 (only the low byte is used)                                  */
} TextState;

/* The advance of byte `c` in this font or -1 when the glyph does not exist (the clip walks skip those). */
static int glyph_adv(const TextState *t, u8 c)
{
    if (c >= 0x80 || c > t->last) return -1;
    u8 k = (u8)(c - t->first);
    if (c < t->first) return -1;
    u8 a = t->fixed;
    if (!a) a = (u8)(ib((unsigned)(u16)(t->wtab + k)) + t->spacing);
    return a;
}

/* 0x31D: walk the string adding advances; cut after the glyph that reaches xmax+1. Returns false: nothing to draw. */
static bool clip_right(TextState *t, TextCtx *c, u8 *s, u8 **rest_ptr, u8 *rest_val)
{
    u16 xmax = (u16)c->xmax;
    if (t->x >= xmax) return false;
    u16 lim = (u16)(xmax + 1);
    u16 xe = t->x;
    for (u8 *p = s;; p++) {
        u8 ch = *p;
        if (ch == 0) return true;                           /* the string ends first: drawn unchanged */
        int a = glyph_adv(t, ch);
        if (a < 0) continue;
        xe = (u16)(xe + a);
        if (xe < lim) continue;
        t->trim_r = (u8)(xe - lim);
        *rest_ptr = p + 1;
        *rest_val = p[1];
        p[1] = 0;
        return true;
    }
}

/* 0x2B9: returns false when nothing remains to draw; *sp moves to the first partly visible glyph. */
static bool clip_left(TextState *t, TextCtx *c, u8 **sp)
{
    s16 d = (s16)(c->xmax - c->xmin);
    if (d < 0) return false;
    u16 xmin = (u16)c->xmin;
    if (t->x >= xmin) return true;
    u16 cx = t->x;
    t->x = xmin;
    for (u8 *p = *sp;; p++) {
        u8 ch = *p;
        if (ch == 0) return false;
        int a = glyph_adv(t, ch);
        if (a < 0) continue;
        cx = (u16)(cx + a);
        if ((s16)cx <= (s16)xmin) continue;                 /* wholly left of xmin */
        t->skip_l = (u8)(a - (u16)(cx - xmin));
        *sp = p;
        return true;
    }
}

/* 0x37F: byte arithmetic on the rows. Returns false when the string is wholly above or below. The bottom clip of a string
 * that was also clipped at the top uses the ORIGINAL y (dl is not reloaded), so its height is too large by ymin - y. */
static bool clip_vert(TextState *t, TextCtx *c)
{
    u8 cl = (u8)c->ymin, ch = (u8)c->ymax;
    u8 dl = (u8)t->y;
    u8 dh = (u8)((u8)t->height - 1 + dl);
    if (dl > ch || dh < cl) return false;
    if (dl < cl) {
        t->height = (u16)((t->height & 0xFF00) | (u8)(dh - cl + 1));
        t->y = (u16)((t->y & 0xFF00) | cl);
        t->gptr = (u16)(t->gptr + (u16)((u8)(cl - dl) * t->stride));
    }
    if (dh > ch) t->height = (u16)((t->height & 0xFF00) | (u8)(ch - dl + 1));
    return true;
}

/* 0x61A: draw from s. */
static void text_draw(TextState *t, TextCtx *c, u8 *s)
{
    colour = (u8)c->fg;                                     /* lcall color_set_r(ah = fg) */
    u8 cur_fg = (u8)c->fg, bg = (u8)c->bg, mode = (u8)c->flags;
    u8 *pg = seg_ptr(page_seg(c->page));
    u16 di = (u16)(rowoff(t->y) + t->x);
    for (;;) {
        u8 ch = s[0], next = s[1];
        s++;
        if (ch == 0) break;
        if (ch & 0x80) { cur_fg = ch & 0x7F; continue; }
        if (ch > t->last) continue;
        if (ch < t->first) continue;
        u8 idx = (u8)(ch - t->first);
        u8 vis = t->fixed;
        if (!vis) vis = (u8)(ib((unsigned)(u16)(t->wtab + idx)) + t->spacing);
        if (next == 0) vis = (u8)(vis - t->trim_r);
        vis = (u8)(vis - t->skip_l);
        u16 g = (u16)(((unsigned)idx << t->shift) + t->gptr);
        u8 rows = (u8)t->height;
        u16 gi = g;
        u16 rdi = di;
        if (mode == 1) {                                    /* 06F5 opaque */
            do {
                u16 bx = (u16)(ib(gi) << 8 | ib((unsigned)(u16)(gi + 1)));
                bx = (u16)(bx << t->skip_l);
                u8 n = vis;
                u16 o = rdi;
                do {
                    bool bit = (bx & 0x8000) != 0;
                    bx = (u16)(bx << 1);
                    pg[o++] = bit ? cur_fg : bg;
                } while (--n);
                rdi = (u16)(rdi + 320);
                gi = (u16)(gi + t->stride);
            } while (--rows);
        } else {                                            /* 06B0 transparent */
            u8 w = vis;
            bool draw = w != 0;
            if (draw && next != 0) { w = (u8)(w - t->spacing); draw = w != 0; }
            if (draw) {
                do {
                    u16 bx = (u16)(ib(gi) << 8 | ib((unsigned)(u16)(gi + 1)));
                    bx = (u16)(bx << t->skip_l);
                    u8 n = w;
                    u16 o = rdi;
                    do {
                        bool bit = (bx & 0x8000) != 0;
                        bx = (u16)(bx << 1);
                        if (bit) pg[o] = cur_fg;
                        o++;
                    } while (--n);
                    rdi = (u16)(rdi + 320);
                    gi = (u16)(gi + t->stride);
                } while (--rows);
            }
        }
        t->skip_l = 0;                                      /* 06E1 */
        di = (u16)(di + vis);
        t->x = (u16)(t->x + vis);
    }
    c->fg = (s16)((c->fg & ~0xFF) | cur_fg);                /* 0613: mov [bp+4], al */
}

static void text_run(TextCtx *c, char *str, int clip)
{
    u8 *s = (u8 *)str;
    TextState t;
    memset(&t, 0, sizeof t);
    t.x = (u16)c->x;
    t.y = (u16)c->y;
    u8 *rest_ptr = NULL, rest_val = 0;
    bool go = false;
    if (s[0] != 0 && (u16)c->font <= iw(0xD0) && img) {     /* 03E8: `ja` after cmp font, [00D0] */
        u16 p = iw((unsigned)(0xD2 + 2 * (u16)c->font));
        t.gptr = p;
        s8 hgt = (s8)ib((unsigned)(u16)(p - 4));
        if (c->flags & 1) hgt = (s8)(hgt + (s8)ib((unsigned)(u16)(p - 2)));
        t.height = (u16)(s16)hgt;
        t.spacing = ib((unsigned)(u16)(p - 3));
        u8 fx = ib((unsigned)(u16)(p - 5));
        t.fixed = fx ? (u8)(fx + t.spacing) : 0;
        t.first = ib((unsigned)(u16)(p - 8));
        t.last = ib((unsigned)(u16)(p - 7));
        u8 span = (u8)(t.last - t.first);
        t.shift = (u8)(ib((unsigned)(u16)(p - 6)) - 1);
        t.wtab = (u16)(p - 9 - span);
        t.stride = (u16)((span + 1) << (t.shift & 15));
        go = true;
        if (clip & CLIP_R) go = clip_right(&t, c, s, &rest_ptr, &rest_val);
        if (go && (clip & CLIP_L)) go = clip_left(&t, c, &s);
        if (go && (clip & CLIP_V)) go = clip_vert(&t, c);
        if (go) text_draw(&t, c, s);
    }
    if (rest_ptr) *rest_ptr = rest_val;                     /* 03D1: restore the byte the right clip replaced */
    c->x = (s16)t.x;
}

/* slot 5 / 4 */  /* 019C:019A text_noclip, 022F text_noclip_r */
void text_noclip(void *c, char *s)      { text_run(c, s, 0); }
void text_noclip_r(void *c, char *s)    { text_run(c, s, 0); }
/* slot 7 / 1 */  /* 019C:01C7, 0224: vertical clip only */
void text_clip_y(void *c, char *s)      { text_run(c, s, CLIP_V); }
void text_clip_y_r(void *c, char *s)    { text_run(c, s, CLIP_V); }
/* slot 8 / 2 */  /* 019C:01DB, 020E: left clip only */
void text_clip_xl(void *c, char *s)     { text_run(c, s, CLIP_L); }
void text_clip_xl_r(void *c, char *s)   { text_run(c, s, CLIP_L); }
/* slot 10 / 3 */ /* 019C:01EF, 0219: right clip only */
void text_clip_xr(void *c, char *s)     { text_run(c, s, CLIP_R); }
void text_clip_xr_r(void *c, char *s)   { text_run(c, s, CLIP_R); }
/* slot 9 / 6 */  /* 019C:01B3, 0203: right + left + vertical */
void text_clip_all(void *c, char *s)    { text_run(c, s, CLIP_R | CLIP_L | CLIP_V); }
void text_clip_all_r(void *c, char *s)  { text_run(c, s, CLIP_R | CLIP_L | CLIP_V); }

/* slot 47, 019C:023A: advance of one glyph incl. spacing; 0 if the glyph does not exist. The result is a sign-extended byte. */
int font_char_width(int ch, int font)
{
    u8 cl = (u8)ch;
    if ((s8)cl < 0 || !img || (u16)font > iw(0xD0)) return 0;
    u16 p = iw((unsigned)(0xD2 + 2 * (u16)font));
    if (cl < ib((unsigned)(u16)(p - 8)) || cl > ib((unsigned)(u16)(p - 7))) return 0;
    u8 spacing = ib((unsigned)(u16)(p - 3)), al = ib((unsigned)(u16)(p - 5));
    if (!al) al = ib((unsigned)(u16)(p - 9 - (ib((unsigned)(u16)(p - 7)) - cl)));
    return (s8)(u8)(al + spacing);
}

/* ------------------------------------------------------------------ HUD tick strip (slot 11, 071A): spec section 6 */
void hud_tick_strip(int si_, int bx_, int cl_, int dl_)
{
    static const u16 col[4] = { 71, 248, 120, 200 }, rmin[4] = { 22, 22, 34, 34 }, rmax[4] = { 82, 82, 74, 74 };
    u8 *pg = seg_ptr(draw_seg);
    u16 si = (u16)si_, bx = (u16)bx_;
    u8 cl = (u8)cl_;
    int dir = si == 0 ? -1 : 1;                              /* std when si == 0 */
    bx--;
    if ((s8)(u8)dl_ >= 1) bx = (u16)(bx + 0x14);
    if (cl) { si += 4; bx++; }
    u16 dx = col[(si >> 1) & 3];
    u16 lo = rmin[(si >> 1) & 3], hi = rmax[(si >> 1) & 3];
    u8 ch = 10;
    for (;;) {
        if (bx < lo) break;
        if (bx <= hi) {
            u16 di = (u16)(rowoff(bx) + dx);
            if (ch == 5) {                                  /* a 2-pixel tick */
                pg[di] = colour; di = (u16)(di + dir);
                pg[di] = colour;
            } else {
                if (ch == 10) {                             /* the major tick: 2 pixels, a third unless cl */
                    pg[di] = colour; di = (u16)(di + dir);
                    pg[di] = colour; di = (u16)(di + dir);
                    if (cl) goto next;
                }
                pg[di] = colour;                            /* every other row: the scale line itself, 1 pixel */
            }
        }
    next:
        bx = (u16)(bx - 2);
        ch = ch == 10 ? 1 : (u8)(ch + 1);
    }
}

/* ------------------------------------------------------------------ pages, offsets, colour (slots 12-16, 23-30, 32-36, 38-39, 49, 56-59, 62-67, 75, 79-83) */
void page_select_1(void)              { draw_seg = page_tab[1]; }                  /* 019C:07A3 slot 12 */
void page_select_r(int page)          { draw_seg = page_seg(page); }                /* 019C:07B2 slot 13 */
void page_select(int page)            { draw_seg = page_seg(page); }                /* 019C:07AC slot 14 */
void seg_set(unsigned seg)            { draw_seg = (u16)seg; }                      /* 019C:07C3 slot 15 */
unsigned seg_get(void)                { return draw_seg; }                          /* 019C:07C8 slot 16 */
unsigned get_page_bytes(void)         { return 0xFA00; }                            /* 019C:0480 slot 23 */
unsigned get_page_bytes_b(void)       { return 0xFA00; }                            /* 019C:0486 slot 49 */
void offset_clear(void)               { draw_off = 0; }                             /* 019C:07CD slots 24, 25 */
void offset_set(unsigned off)         { draw_off = (u16)off; }                      /* 019C:07D5 slot 26 */
void offset_set_r(unsigned off)       { draw_off = (u16)off; }                      /* 019C:07DB slot 27 */
unsigned get_view_off_a(void)         { return 0x5580; }                            /* 019C:07E0 slot 28 */
unsigned get_view_off_b(void)         { return 0x1450; }                            /* 019C:07E4 slot 29 */
unsigned offset_get(void)             { return draw_off; }                          /* 019C:07E8 slot 30 */
void color_set_r(int c)               { colour = (u8)c; }                           /* 019C:0561 slot 32 */
void color_set(int c)                 { colour = (u8)c; }                           /* 019C:055B slot 33 */
void data_cc_set(int v)               { data_cc = (u16)v; }                         /* 019C:07ED slot 64 */
void data_cc_set_r(int v)             { data_cc = (u16)v; }                         /* 019C:07F3 slot 38 */
void data_ce_set(int v)               { data_ce = (u16)v; }                         /* 019C:07FE slot 65 */
void data_ce_set_r(int v)             { data_ce = (u16)v; }                         /* 019C:0804 slot 39 */
int  flip_flag(void)                  { return flip_last; }                         /* 019C:080F slot 45 */
unsigned pixel_offset(int x, int y)   { return (u16)(x + rowoff((u16)y)); }         /* 019C:0814 slot 62 */
int  get_const_3(void)                { return 3; }                                 /* 019C:0826 slot 63 */
int  get_const_0(void)                { return 0; }                                 /* 019C:048B slot 66 */
int  get_const_1(void)                { return 1; }                                 /* 019C:0490 slot 67 */
void page_seg_set(int page, unsigned seg)   { if ((unsigned)(u16)page < 8) page_tab[(u16)page] = (u16)seg; }   /* 019C:12C2 slot 57 */
void page_seg_set_b(unsigned seg, int page) { if ((unsigned)(u16)page < 8) page_tab[(u16)page] = (u16)seg; }   /* 019C:0791 slot 75 */
unsigned page_es(int page)            { return page_seg(page); }                    /* 019C:12D5 slot 56 */
unsigned row_offset(int y)            { return rowoff((u16)y); }                    /* 019C:12BA slot 58 */
void shake_set(int frames)            { shake = (u8)frames; }                       /* 019C:1251 slot 79 */
void nop_019C_046F(void) {}                                                         /* slot 80 */
void nop_019C_0470(void) {}                                                         /* slot 81 */
void nop_019C_0471(void) {}                                                         /* slot 82 */
void nop_019C_056D(void) {}                                                         /* slots 34, 35 */
void nop_019C_07C2(void) {}                                                         /* slot 83 */
void nop_019C_12D4(void) {}                                                         /* slot 61 */
void nop_019C_12DD(void) {}                                                         /* slot 53 */
void nop_019C_12DE(void) {}                                                         /* slot 54 */
void nop_019C_12DF(void) {}                                                         /* slot 55 */

/* ------------------------------------------------------------------ line, pixel (slots 31, 36): spec section 5 */
static void put_pixel(u16 x, u16 y)
{
    seg_ptr(draw_seg)[(u16)(rowoff(y) + draw_off + x)] = colour;
}

/* slot 31, 019C:0599 */
void line(int x1_, int y1_, int x2_, int y2_)
{
    u16 x1 = (u16)x1_, y1 = (u16)y1_, x2 = (u16)x2_, y2 = (u16)y2_;
    if (x1 > x2) { u16 t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    last_x = x1;
    last_y = y1;
    if (x1 == x2 && y1 == y2) { put_pixel(x1, y1); return; }
    u16 si = 1, bp = 320, cx = (u16)(x2 - x1), dx = (u16)(y2 - y1);
    if ((s16)dx < 0) { bp = (u16)-(s16)bp; dx = (u16)-(s16)dx; }
    if (cx < dx) { u16 t = bp; bp = si; si = t; t = dx; dx = cx; cx = t; }
    u16 major = cx, minor = dx;
    u8 *pg = seg_ptr(draw_seg);
    u16 di = (u16)(rowoff(y1) + draw_off + x1);
    u16 e = (u16)-(s16)((u16)(major + 1) >> 1);
    si = (u16)(si - 1);
    for (;;) {
        pg[di++] = colour;
        if ((s16)(--cx) < 0) break;
        di = (u16)(di + si);
        e = (u16)(e + minor);
        if ((s16)e < 0) continue;
        e = (u16)(e - major);
        di = (u16)(di + bp);
    }
}

/* slot 36, 019C:056E: unused by the programs */
void pixel_last(void) { put_pixel(last_x, last_y); }

/* ------------------------------------------------------------------ span fills (slots 37, 40): spec section 5 */
/* slot 37, 019C:0F3D; slot 40 = mode 0 (0F3B) */
void poly_fill(int ymin, void *tbl, int ymax, int mode)
{
    if (ymin < 0) return;                                   /* `js` on ymin */
    const u8 *t = tbl;
    u8 *pg = seg_ptr(draw_seg);
    int y = ymax;
    do {                                                    /* do-while: ymax < ymin still draws row ymax once */
        u16 L = (u16)(t[2 * y] | t[2 * y + 1] << 8);
        u16 R = (u16)(t[2 * y + 0x1B8] | t[2 * y + 0x1B9] << 8);
        if (R < L || (R == L && (R == 0 || R == 0x13F))) { y--; continue; }
        u16 n = (u16)(R - L + 1);
        u16 di = (u16)(rowoff((u16)y) + draw_off + L);
        for (u16 i = 0; i < n; i++, di++) {
            switch (mode) {
            case 0: pg[di] = colour; break;
            case 1: pg[di] |= colour; break;
            case 2: pg[di] &= colour; break;
            default: pg[di] = (u8)(((pg[di] & 0x0E) >> 1) | 0x98); break;   /* the green MFD ramp 98h-9Fh */
            }
        }
        y--;
    } while ((s16)(y * 2) >= (s16)(ymin * 2));
}

void poly_fill_solid(int ymin, void *tbl, int ymax) { poly_fill(ymin, tbl, ymax, 0); }   /* slot 40, 019C:0F3B */

/* ------------------------------------------------------------------ rectangle copy and recolour (slots 41, 42) */
/* slot 41, 019C:1188: `page` points at the page number (a DS word) */
void rect_recolor(int *page, int x0, int y0, int x1, int y1, int oldc, int newc)
{
    const u8 *pp = (const u8 *)page;
    u16 pn = (u16)(pp[0] | pp[1] << 8);
    u8 *pg = seg_ptr(page_seg(pn));
    u16 rows = (u16)(y1 - y0 + 1), w = (u16)(x1 - x0 + 1);
    u16 y = (u16)y0;
    u8 ah = (u8)oldc, al = (u8)newc;
    do {
        u16 di = (u16)(x0 + rowoff(y));
        u16 n = w;
        do {
            if (pg[di] == ah) pg[di] = al;
            di++;
        } while (--n);
        y++;
    } while (--rows);
}

/* slot 42, 019C:1131: forward copy, no overlap handling */
void blit_rect(int spage, int sx, int sy, int dpage, int dx, int dy, int w, int h)
{
    const u8 *sp = seg_ptr(page_seg(spage));
    u8 *dp = seg_ptr(page_seg(dpage));
    u16 rows = (u16)h, ys = (u16)sy, yd = (u16)dy;
    do {
        u16 si = (u16)(rowoff(ys) + sx), di = (u16)(rowoff(yd) + dx);
        for (u16 n = (u16)w; n; n--) dp[di++] = sp[si++];
        ys++;
        yd++;
    } while (--rows);
}

/* ------------------------------------------------------------------ key-colour blits (slots 17-20, 71-74): spec section 8 */
static void blit_key_body(BlitCtx *b)                       /* 019C:0EE6 */
{
    const u8 *sp = seg_ptr(b->seg);
    u8 *dp = seg_ptr(page_seg(b->dpage));
    u16 rows = (u16)b->h, ys = (u16)b->sy, yd = (u16)b->dy;
    do {
        u16 si = (u16)(rowoff(ys) + b->sx), di = (u16)(rowoff(yd) + b->dx);
        u16 n = (u16)b->w;
        do {
            u8 v = sp[si++];
            if (v) dp[di] = v;
            di++;
        } while (--n);
        ys++;
        yd++;
    } while (--rows);
}

void blit_key(void *b)   { blit_key_body(b); }               /* slots 17, 73, 019C:0ED5 */
void blit_key_r(void *b) { blit_key_body(b); }               /* slots 18, 74, 019C:0EE6 */

static void blit_key_clip_body(BlitCtx *b)                    /* 019C:0E41: w, h restored, dx dy sx sy consumed */
{
    s16 sw = b->w, sh = b->h;
    s16 ax = b->dx, cx = b->xmin, bx;
    bool ok = true;
    if (ax >= cx) {
        bx = (s16)(ax + b->w - 1);
    } else {
        bx = (s16)(ax + b->w - 1);
        if (bx >= cx) {
            cx = (s16)(cx - ax);
            b->dx = (s16)(b->dx + cx);
            b->sx = (s16)(b->sx + cx);
            b->w = (s16)(b->w - cx);
        } else ok = false;
    }
    if (ok) {
        cx = b->xmax;
        if (bx > cx) {
            if (ax <= cx) { bx = (s16)(bx - cx); b->w = (s16)(b->w - bx); }
            else ok = false;
        }
    }
    if (ok) {
        cx = b->ymin;
        ax = b->dy;
        if (ax >= cx) {
            bx = (s16)(ax + b->h - 1);
        } else {
            bx = (s16)(ax + b->h - 1);
            if (bx >= cx) {
                cx = (s16)(cx - ax);
                b->dy = (s16)(b->dy + cx);
                b->sy = (s16)(b->sy + cx);
                b->h = (s16)(b->h - cx);
            } else ok = false;
        }
    }
    if (ok) {
        cx = b->ymax;
        if (bx > cx) {
            if (ax <= cx) { bx = (s16)(bx - cx); b->h = (s16)(b->h - bx); }
            else ok = false;
        }
    }
    if (ok) blit_key_body(b);
    b->w = sw;
    b->h = sh;
}

void blit_key_clip(void *b)   { blit_key_clip_body(b); }     /* slots 19, 71, 019C:0E30 */
void blit_key_clip_r(void *b) { blit_key_clip_body(b); }     /* slots 20, 72, 019C:0E41 */

/* ------------------------------------------------------------------ screen: clear, copy, flip (slots 43, 44, 48, 51, 52, 59) */
void clear_screen(void) { memset(seg_ptr(0xA000), 0, 0xFA00); }                 /* slot 43, 019C:1122 */
void clear_es(unsigned es) { memset(seg_ptr((u16)es), 0, 0xFA00); }             /* slot 59, 019C:1118 */

/* slot 44, 019C:11F1: AX words from page 1 to A000 unless the disable word is set */
void flip(int words)
{
    if (flip_disable) { flip_last = 0; return; }
    flip_last = 1;
    memcpy(seg_ptr(page_tab[0]), seg_ptr(page_tab[1]), (size_t)(u16)words * 2);
}

/* slot 48, 019C:11DE: 32000 words from srcseg:0 to the draw segment, offset 0 */
void page_load(unsigned srcseg) { memmove(seg_ptr(draw_seg), seg_ptr((u16)srcseg), 0xFA00); }

/* slots 51, 52, 019C:12E0: 320 bytes (160 words) from the caller's stack buffer to ES:DI */
void scanline_put(unsigned es, unsigned di, void *src)
{
    u8 *d = seg_ptr((u16)es);
    for (int i = 0; i < 320; i++) d[(u16)(di + i)] = ((const u8 *)src)[i];
}

/* ------------------------------------------------------------------ mode, blanking, DAC tables, effects: spec section 8 */
void mode13_set(void) { vga_set_mode(0x13); }                                   /* slot 60, 019C:04B6 (always succeeds) */

/* slot 69, 019C:04CA: wait for retrace, 3D8h := 2, sequencer clocking-mode bit 5 set (screen off) */
void screen_off(void)
{
    vga_wait_vretrace_active();
    vga_out(0x3D8, 0x02);
    vga_out(0x3C4, 1);
    vga_out(0x3C5, (u8)(vga_in(0x3C5) | 0x20));
}

/* slot 70, 019C:04E4: wait for retrace, 3D8h := 0Ah, bit 5 cleared (screen on) */
void screen_on(void)
{
    vga_wait_vretrace_active();
    vga_out(0x3D8, 0x0A);
    vga_out(0x3C4, 1);
    vga_out(0x3C5, (u8)(vga_in(0x3C5) & 0xDF));
}

/* data 1871 + 30h*n: the five 16-colour tables (6-bit RGB) */
static const u8 pal16[5][48] = {
    { 0x00,0x00,0x00, 0x00,0x00,0x2a, 0x00,0x2a,0x00, 0x00,0x2a,0x2a, 0x2a,0x00,0x00, 0x2a,0x00,0x2a, 0x2a,0x15,0x00, 0x2a,0x2a,0x2a,
      0x15,0x15,0x15, 0x15,0x15,0x3f, 0x15,0x3f,0x15, 0x15,0x3f,0x3f, 0x3f,0x15,0x15, 0x3f,0x15,0x3f, 0x3f,0x3f,0x15, 0x3f,0x3f,0x3f },
    { 0x00,0x00,0x00, 0x00,0x00,0x2a, 0x00,0x2a,0x00, 0x00,0x2a,0x2a, 0x2a,0x00,0x00, 0x00,0x00,0x00, 0x2a,0x15,0x00, 0x2a,0x2a,0x2a,
      0x15,0x15,0x15, 0x15,0x15,0x3f, 0x15,0x3f,0x15, 0x15,0x3f,0x3f, 0x3f,0x15,0x15, 0x3f,0x15,0x3f, 0x3f,0x3f,0x15, 0x3f,0x3f,0x3f },
    { 0x00,0x00,0x00, 0x00,0x00,0x2a, 0x00,0x2a,0x00, 0x00,0x2a,0x2a, 0x2a,0x00,0x00, 0x2a,0x00,0x2a, 0x2a,0x15,0x00, 0x2a,0x2a,0x2a,
      0x15,0x15,0x15, 0x15,0x15,0x3f, 0x15,0x3f,0x15, 0x15,0x3f,0x3f, 0x3f,0x15,0x15, 0x00,0x00,0x00, 0x3f,0x3f,0x15, 0x3f,0x3f,0x3f },
    { 0 },
    { 0x00,0x00,0x00, 0x00,0x2a,0x00, 0x00,0x00,0x00, 0x00,0x2a,0x2a, 0x2a,0x00,0x00, 0x2a,0x00,0x2a, 0x2a,0x15,0x00, 0x2a,0x2a,0x2a,
      0x15,0x15,0x15, 0x15,0x15,0x3f, 0x00,0x00,0x00, 0x15,0x3f,0x3f, 0x3f,0x15,0x15, 0x3f,0x15,0x3f, 0x3f,0x3f,0x15, 0x3f,0x3f,0x3f },
};

/* slot 68, 019C:0536: DAC 0..15 := table n, then screen_on. Not called by any program. PORT: n beyond 4 is ignored. */
void palette16_set(int n)
{
    if ((unsigned)n > 4) return;
    for (int i = 0; i < 16; i++) vga_dac_set((u8)i, pal16[n][3 * i], pal16[n][3 * i + 1], pal16[n][3 * i + 2]);
    screen_on();
}

/* slot 46, 019C:125C: DAC[13] := a random red/yellow of table 0, and the CRTC start-address-low shake while the counter runs */
void explosion_fx_tick(void)
{
    static const u8 pick[4] = { 0x0C, 0x04, 0x0C, 0x0E };
    rng = (u16)(rng * 5 + 1);
    const u8 *c = pal16[0] + 3 * pick[rng & 3];
    vga_out(0x3C8, 0x0D);
    vga_out(0x3C9, c[0]);
    vga_out(0x3C9, c[1]);
    vga_out(0x3C9, c[2]);
    if (shake) {
        u8 ah = (u8)((rng >> 8) & 3);
        if (--shake == 0) ah = 0;
        vga_outw(0x3D4, (u16)(ah << 8 | 0x0D));
    }
}

/* ------------------------------------------------------------------ DOS memory and the sprite pool (slots 50, 76-78, 22, 77): spec section 7 */
unsigned mem_paras_free(void) { return dos_mem_largest(); }                      /* slot 50, 019C:04A4 */

/* slot 76, 019C:09A8: ALL free DOS memory becomes the pool */
unsigned pool_init(void)
{
    u16 n = (u16)mem_paras_free(), seg = 0;
    if (n == 0 || !dos_mem_alloc(n, &seg)) { seg = 0; n = 0; }
    pool_off = 0;
    pool_seg = pool_base = seg;
    pool_end = (u16)(seg + n);
    return seg;
}

/* slot 78, 019C:09D4: shrink the pool to what was used (free it when nothing was) */
unsigned pool_free(void)
{
    u16 es = pool_base, used = (u16)(pool_seg - pool_base);
    bool ok;
    if (pool_seg < pool_base) ok = false;
    else if (used == 0) ok = dos_mem_free(es);
    else { u16 largest; ok = dos_mem_resize(es, used, &largest); }
    if (!ok) es = 0;
    pool_seg = pool_end = pool_base = 0;
    return es;
}

/* 019C:0A02: a new sprite header at the pool top; returns false when the pool is empty */
static bool sprite_header(u16 w, u16 h)
{
    s32 avail = (s32)pool_end - (s32)pool_seg;
    if (avail <= 0) { pool_off = 0; pool_seg = 0; return false; }
    u16 a = avail >= 0x1000 ? 0x0FFF : (u16)avail;
    pool_limit = (u16)(a << 4);
    u8 *d = mp(pool_seg, pool_off);
    d[0] = (u8)w; d[1] = (u8)(w >> 8); d[2] = (u8)h; d[3] = (u8)(h >> 8);
    enc_w = w;
    enc_h = h;
    pool_off = (u16)(pool_off + 4);
    return true;
}

/* 019C:0A4D: close the sprite: the next one starts on the next paragraph; returns this sprite's segment */
static u16 sprite_close(void)
{
    u16 o = pool_off;
    pool_off = 0;
    u16 ax = (u16)(((u16)(o + 15) >> 4) + pool_seg);
    u16 old = pool_seg;
    pool_seg = ax;
    return old;
}

/* 019C:0A6A: RLE-encode one source row (colour 0 = transparent) at pool_seg:pool_off */
static void sprite_row(const u8 *src)
{
    if (pool_seg == 0) return;
    if ((u16)(enc_w + 4 + pool_off) > pool_limit) {          /* `jbe` passes: the row needs w + 4 bytes */
        u8 *hh = mp(pool_seg, 2);
        u16 h = (u16)(hh[0] | hh[1] << 8);
        h--;
        hh[0] = (u8)h; hh[1] = (u8)(h >> 8);
        pool_end = 0;                                        /* the pool is disabled */
        return;
    }
    u16 di = pool_off, bp = di;
    enc_lead = enc_trail = enc_len = 0;
    di = (u16)(di + 4);
    for (u16 i = 0; i < enc_w; i++) {
        u8 al = src[i];
        if (al) {
            enc_trail = 0;
            enc_len++;
            mp(pool_seg, di++)[0] = al;
        } else if (enc_len == 0) {
            enc_lead++;
        } else {
            enc_trail++;
            enc_len++;
            mp(pool_seg, di++)[0] = al;
        }
    }
    u16 len = enc_len;
    if (enc_trail) { len = (u16)(len - enc_trail); di = (u16)(di - enc_trail); }
    u8 *rec = mp(pool_seg, bp);
    rec[0] = (u8)len; rec[1] = (u8)(len >> 8);
    rec[2] = (u8)enc_lead; rec[3] = (u8)(enc_lead >> 8);
    pool_off = di;
}

/* slot 77, 019C:0B0C: returns the sprite's segment, 0 if the pool is exhausted */
unsigned sprite_grab(int page, int x, int y, int w, int h)
{
    if (sprite_header((u16)w, (u16)h) && enc_h != 0 && enc_w != 0) {
        u16 pn = (u16)(page < 0 ? -page : page);
        const u8 *pg = seg_ptr(page_seg(pn));
        u16 si = (u16)(rowoff((u16)y) + x);
        do {
            sprite_row(pg + si);
            si = (u16)(si + 320);
        } while (--enc_h);
    }
    return sprite_close();
}

/* slot 22, 019C:0B70 (draw half = slot 21 at 0C95): a scaled, optionally mirrored sprite of the pool. */
s16 *sprite_scaled(void *w_, int x, int y, int dw_, int dh_, unsigned off, unsigned seg)
{
    static RectP rect;
    static u8 rowbuf[0x400];
    const TextCtx *w = w_;
    s16 sgn_w = dw_ < 0 ? -1 : 0, sgn_h = dh_ < 0 ? -1 : 0;
    u16 dw = (u16)(dw_ < 0 ? -dw_ : dw_), dh = (u16)(dh_ < 0 ? -dh_ : dh_);
    u16 cut_t = 0, cut_l = 0, cut_b = 0, cut_r = 0;
    memset(&rect, 0, sizeof rect);
    const u8 *hdr = mp((u16)seg, 0);
    bool valid = seg != 0 && (hdr[0] | hdr[1]) && (hdr[2] | hdr[3]);              /* c5d: seg, [0], [2] */
    if (!valid) return &rect.x;
    u16 di = (u16)y, dx = (u16)x;
    /* clip, 0BC0 */
    s16 bx;
    if ((s16)di < 0) {
        u16 a = (u16)-(s16)di;
        if (a >= dh) return &rect.x;
        cut_t = a;
    }
    u16 bot = (u16)(di + dh - 1);
    bx = (s16)(w->ymax - w->ymin);
    if (bx < 0 || (s16)di > bx) return &rect.x;
    if ((s16)(bot - bx) > 0) cut_b = (u16)(bot - bx);
    if ((s16)dx < 0) {
        u16 a = (u16)-(s16)dx;
        if (a >= dw) return &rect.x;
        cut_l = a;
    }
    u16 right = (u16)(dx + dw - 1);
    bx = (s16)(w->xmax - w->xmin);
    if (bx < 0 || (s16)dx > bx) return &rect.x;
    if ((s16)(right - bx) > 0) cut_r = (u16)(right - bx);
    rect.x = (s16)(cut_l + dx);
    rect.y = (s16)(cut_t + di);
    rect.w = (s16)(dw - cut_l - cut_r);
    rect.h = (s16)(dh - cut_t - cut_b);

    /* draw, 0C95 */
    u8 *pg = seg_ptr(page_seg(w->page));
    dx = (u16)(dx + (u16)w->xmin);
    di = (u16)(di + (u16)w->ymin);
    u16 si = (u16)off;
    u16 sw = (u16)(mp((u16)seg, si)[0] | mp((u16)seg, si)[1] << 8);
    u16 sh = (u16)(mp((u16)seg, si + 2)[0] | mp((u16)seg, si + 2)[1] << 8);
    si = (u16)(si + 4);
    s16 vis_w = (s16)(dw - cut_l - cut_r), vis_h = (s16)(dh - cut_t - cut_b);
    if (vis_w <= 0 || vis_h <= 0) return &rect.x;
    int rowstep = 320;
    u16 skip = cut_t;
    di = (u16)(di + cut_t);
    if (sgn_h) { rowstep = -320; skip = cut_b; di = (u16)(di + vis_h - 1); }
    s16 accy = (s16)-(s16)dh;
    for (; skip; skip--) {                                   /* pre-run the row DDA over the cut rows */
        accy = (s16)(accy + sh);
        if (accy >= 0) {
            do {
                u16 len = (u16)(mp((u16)seg, si)[0] | mp((u16)seg, si)[1] << 8);
                si = (u16)(si + len + 4);
                accy = (s16)(accy - dh);
            } while (accy >= 0);
        }
    }
    u16 colskip = cut_l;
    s16 accx0 = (s16)-(s16)dw;
    dx = (u16)(dx + cut_l);
    int dir = 1;
    if (sgn_w) { dir = -1; colskip = cut_r; dx = (u16)(dx + vis_w - 1); }
    u16 srcskip = 0;
    for (; colskip; colskip--) {                             /* pre-run the column DDA */
        accx0 = (s16)(accx0 + sw);
        while (accx0 >= 0) { srcskip++; accx0 = (s16)(accx0 - dw); }
    }
    di = (u16)(rowoff(di) + dx);
    for (s16 rows = vis_h; rows > 0; rows--) {
        const u8 *rec = mp((u16)seg, si);
        u16 len = (u16)(rec[0] | rec[1] << 8);
        if (len != 0) {
            u16 lead = (u16)(rec[2] | rec[3] << 8);
            if ((u16)(len + lead) > srcskip) {
                u16 tail = (u16)(sw - len - lead);
                memset(rowbuf, 0, sizeof rowbuf);
                if ((unsigned)lead + len + tail <= sizeof rowbuf) {
                    memcpy(rowbuf + lead, rec + 4, len);
                }
                const u8 *sp = rowbuf + srcskip;
                u16 o = di;
                s16 acc = accx0;
                s16 cx = vis_w;
                for (;;) {
                    u8 a = *sp++;
                    if (a) {
                        if (--cx < 0) break;
                        pg[o] = a;
                        o = (u16)(o + dir);
                        acc = (s16)(acc + sw);
                        while (acc < 0) {                    /* the same pixel again */
                            if (--cx < 0) goto rowdone;
                            pg[o] = a;
                            o = (u16)(o + dir);
                            acc = (s16)(acc + sw);
                        }
                    } else {
                        for (;;) {
                            if (--cx < 0) goto rowdone;
                            o = (u16)(o + dir);
                            acc = (s16)(acc + sw);
                            if (acc >= 0) break;
                        }
                    }
                    acc = (s16)(acc - dw);
                    while (acc >= 0) { sp++; acc = (s16)(acc - dw); }
                }
            }
        }
    rowdone:
        di = (u16)(di + rowstep);
        accy = (s16)(accy + sh);
        while (accy >= 0) {
            u16 l2 = (u16)(mp((u16)seg, si)[0] | mp((u16)seg, si)[1] << 8);
            si = (u16)(si + l2 + 4);
            accy = (s16)(accy - dh);
        }
    }
    (void)sgn_w;
    return &rect.x;
}
