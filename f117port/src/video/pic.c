/* .PIC pictures: the LZW + RLE-90 decoder of START 0000:86A8-8A31 (the same code is in END, DSWAP and VGAME), with its
 * layers as functions of their own: lzw_reset, lzw_next, rle90_fill, pic_begin, pic_row, and the four front ends
 * pic_to_page / pic_to_seg_a / pic_to_seg_b / pic_to_seg_c (spec video.md section 9, format FORMATS.md).
 *
 * PORT: the decoder's working variables (DS:8D60..8D80, the 512-byte file buffer, the string stack, the table at 7420)
 * are C statics here instead of the calling program's DGROUP; nothing outside the decoder reads them. The four front
 * ends produce the same pixels (they differ in the page/segment argument and the reader), so they share one routine.
 * The RLE-90 repeat counter is a BYTE as in the original: `90 01` repeats 255+1 times (it never occurs in the shipped files). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen/module_platform.h"
#include "gen/module_video.h"
#include "host.h"
#include "mem.h"
#include "video/video.h"

#define PIC_PIXELS 64000

enum { SRC_HOST, SRC_DOS, SRC_GUEST };

typedef struct PicState {
    PicSrc *src;                /* host memory file or DOS handle (via PicSrc) */
    int kind;
    u16 mem_seg;                /* SRC_GUEST: segment of the picture held in memory, read in 512-byte blocks */
    u32 mem_pos;
    u8  buf[512];               /* DS:6235 */
    u16 bi;                     /* read position in buf (the original's SI) */
    u32 acc;                    /* bit accumulator (the original keeps a word and a bit count) */
    int nacc;
    u8  width;                  /* 8D74 current code width 9..maxbits */
    u16 mask;                   /* 8D76 */
    u16 free_;                  /* 8D78 next free table entry */
    u8  maxbits;                /* 8D75 */
    bool nibble;                /* 8D7D: 4-bit pixels */
    u16 prev;                   /* 8D7E previous code */
    u8  first;                  /* 8D80 first byte of the previous string */
    u8  rle_n, rle_v;           /* 8D72, 8D73 */
    u16 pre[4096];              /* table at 7420: prefix word, 0xFFFF = a root */
    u8  suf[4096];              /* suffix byte */
    u8  stack[4100];
    int sp;
    u8  row[320];               /* DS:8C20 */
} PicState;

static PicState D;

/* 81C6 (file) / 819D (memory): the next 512 bytes into the buffer. */
static void refill(void)
{
    PicSrc *s = D.src;
    memset(D.buf, 0, sizeof D.buf);
    switch (D.kind) {
    case SRC_DOS:
        dos_read(s->fd, 512, D.buf);
        break;
    case SRC_GUEST:
        memcpy(D.buf, mp(D.mem_seg, 0) + D.mem_pos, 512);
        D.mem_pos += 512;
        break;
    default:
        if (s->pos < s->len) {
            size_t n = s->len - s->pos < 512 ? s->len - s->pos : 512;
            memcpy(D.buf, s->data + s->pos, n);
        }
        s->pos += 512;
        break;
    }
    D.bi = 0;
}

static u16 next_word(void)
{
    if (D.bi >= 512) refill();
    u16 w = (u16)(D.buf[D.bi] | D.buf[D.bi + 1] << 8);
    D.bi = (u16)(D.bi + 2);
    return w;
}

/* START 0000:88D4: code width 9, free entry 256, prefix table cleared, roots 0..255 */
void lzw_reset(void)
{
    D.width = 9;
    D.mask = 0x1FF;
    D.free_ = 0x100;
    for (int i = 0; i < 4096; i++) D.pre[i] = 0xFFFF;
    for (int i = 0; i < 256; i++) D.suf[i] = (u8)i;
}

/* START 0000:8983: the next decoded byte */
int lzw_next(void)
{
    if (D.sp == 0) {
        while (D.nacc < D.width) {                           /* LSB-first bit stream */
            D.acc |= (u32)next_word() << D.nacc;
            D.nacc += 16;
        }
        u16 code = (u16)(D.acc & D.mask);
        D.acc >>= D.width;
        D.nacc -= D.width;
        u16 cx = code, walk = code;
        if ((s16)code >= (s16)D.free_) {                     /* the code is the entry being defined */
            cx = D.free_;
            walk = D.prev;
            D.stack[D.sp++] = D.first;
        }
        while (D.pre[walk & 0xFFF] != 0xFFFF) {
            D.stack[D.sp++] = D.suf[walk & 0xFFF];
            walk = D.pre[walk & 0xFFF];
            if (D.sp > 4090) break;
        }
        u8 root = D.suf[walk & 0xFFF];
        D.first = root;
        D.stack[D.sp++] = root;
        D.suf[D.free_ & 0xFFF] = root;                       /* the new entry: previous code + first byte */
        D.pre[D.free_ & 0xFFF] = D.prev;
        D.free_++;
        if (D.free_ > D.mask) {
            D.width++;
            D.mask = (u16)((D.mask << 1) | 1);
        }
        if (D.width > D.maxbits) lzw_reset();
        D.prev = cx;
    }
    return D.stack[--D.sp];
}

/* START 0000:8889: header byte (bit 7 set = 8-bit pixels, maxbits = 256 - byte; else 4-bit, maxbits = byte), table reset */
void pic_begin(void)
{
    D.sp = 0;
    D.rle_n = 0;
    D.rle_v = 0;
    D.bi = 512;
    D.acc = 0;
    D.nacc = 0;
    u16 w = next_word();
    D.acc = w >> 8;                                          /* the high byte of that word is the start of the code stream */
    D.nacc = 8;
    u8 b0 = (u8)w;
    if (b0 & 0x80) { D.nibble = false; D.maxbits = (u8)-(s8)b0; }
    else { D.nibble = true; D.maxbits = b0; }
    lzw_reset();
}

/* START 0000:890A: n pixels of RLE-90 expansion into the row buffer (4-bit: n/2 bytes, two pixels each).
 * `90 n` repeats the previous byte n-1 times more: the counter is set to n-1, then decremented once more for the byte
 * emitted at once (8945..894D), exactly as the original. */
static int rle_out;
void rle90_fill(unsigned n)
{
    u16 cnt = (u16)n;
    if (D.nibble) cnt >>= 1;
    u8 *o = D.row + rle_out;
    while (cnt) {
        if (D.rle_n == 0) {
            u8 v = (u8)lzw_next();
            if (v != 0x90) {
                D.rle_v = v;
            } else {
                v = (u8)lzw_next();
                if (v == 0) D.rle_v = 0x90;
                else { D.rle_n = (u8)(v - 1); D.rle_n--; }
            }
        } else {
            D.rle_n--;
        }
        u8 px = D.rle_v;
        if (D.nibble) { *o++ = px & 0x0F; *o++ = px >> 4; }
        else *o++ = px;
        cnt--;
    }
}

/* START 0000:885F: decode one row (320 pixels) into the row buffer; the first row starts the stream */
void pic_row(int row)
{
    if (row == 0) pic_begin();
    rle_out = 0;
    rle90_fill(320);
}

static void pic_start(PicSrc *s, int kind)
{
    D.src = s;
    D.kind = kind;
    D.bi = 512;
    D.sp = 0;
}

/* The whole picture into out[64000]; the unit of all four front ends. Returns the pixel count. */
static size_t pic_run(u8 *out, void (*row_sink)(int row, const u8 *px, void *ctx), void *ctx)
{
    size_t n = 0;
    for (int row = 0; row < 200; row++) {
        pic_row(row);
        if (out) memcpy(out + row * 320, D.row, 320);
        if (row_sink) row_sink(row, D.row, ctx);
        n += 320;
    }
    return n;
}

size_t pic_decode(PicSrc *src, u8 *out)
{
    pic_start(src, src->data ? SRC_HOST : SRC_DOS);
    return pic_run(out, NULL, NULL);
}

static void sink_seg(int row, const u8 *px, void *ctx)
{
    scanline_put(*(unsigned *)ctx, (unsigned)(row * 320), (void *)px);
}

static void to_seg(PicSrc *s, int kind, u16 seg)
{
    unsigned es = seg;
    pic_start(s, kind);
    clear_es(es);                                            /* slot 59 before the rows */
    pic_run(NULL, sink_seg, &es);
}

/* START 0000:86A8 (slots 56, 59, 51, 53): a .PIC from the DOS handle into a page */
void pic_to_page(int fh, int page)
{
    PicSrc s = { NULL, 0, 0, fh };
    to_seg(&s, SRC_DOS, (u16)page_es(page));
}

/* START 0000:871A (51, 55) and 87F6 (52, 54): the same into a segment */
void pic_to_seg_a(int fh, unsigned seg)
{
    PicSrc s = { NULL, 0, 0, fh };
    to_seg(&s, SRC_DOS, (u16)seg);
}

void pic_to_seg_c(int fh, unsigned seg)
{
    PicSrc s = { NULL, 0, 0, fh };
    to_seg(&s, SRC_DOS, (u16)seg);
}

/* START 0000:8782: a picture held in memory at buf_seg:0000, read in 512-byte blocks (819D), into a segment */
void pic_to_seg_b(unsigned buf_seg, unsigned seg)
{
    PicSrc s = { NULL, 0, 0, -1 };
    D.mem_seg = (u16)buf_seg;
    D.mem_pos = 0;
    to_seg(&s, SRC_GUEST, (u16)seg);
}

/* ------------------------------------------------------------------ files of the game folder */
static u8 *read_game_file(const char *name, size_t *len)
{
    char *path = host_game_path(name, false);
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    host_free(path);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = malloc(n > 0 ? (size_t)n : 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) *len = (size_t)n;
    return buf;
}

bool pic_decode_file(const char *name, u8 *out)
{
    size_t len = 0;
    u8 *data = read_game_file(name, &len);
    if (!data) return false;
    PicSrc s = { data, len, 0, -1 };
    size_t n = pic_decode(&s, out);
    free(data);
    return n == PIC_PIXELS;
}

bool pal_read_file(const char *name, u8 *out768)
{
    size_t len = 0;
    u8 *data = read_game_file(name, &len);
    if (!data) return false;
    bool ok = len >= 768;
    if (ok) memcpy(out768, data, 768);
    free(data);
    return ok;
}
