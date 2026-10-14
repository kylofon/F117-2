/* .PAN animations (PLAYER.EXE): an LZ stream (PLAYER 0000:0BE2 pan_decode_chunk, tools/lzs.py) that holds a 4-byte
 * header, a cel size table, a 768-byte 6-bit palette and the delta-RLE cels PLAYER applies to the screen (0000:1006,
 * tools/panlib.py rle_apply). These are the whole-file forms of those two routines for the tests and for the front end
 * (S5.3), which may also stream: PLAYER's own ring-buffer logic belongs to the front end.
 *
 * Format notes verified against the Python decoders (tools/lzs.py, panlib.py) on every shipped .PAN: a control word is
 * fetched while the 16th bit of the old one is taken; the end markers (a length byte 0 or 1 after a long match header)
 * are skipped; a source overrun ends the stream. */
#include <stdlib.h>
#include <string.h>

#include "video/video.h"

typedef struct LzIn {
    const u8 *d;
    size_t n, pos;
    u32 bits;
    int nbits;
    bool eof;
} LzIn;

static int lz_getbit(LzIn *z)
{
    if (z->nbits == 0) {
        if (z->pos + 2 > z->n) { z->eof = true; return 0; }
        z->bits = (u32)(z->d[z->pos] | z->d[z->pos + 1] << 8);
        z->pos += 2;
        z->nbits = 16;
    }
    int b = (int)(z->bits & 1);
    z->bits >>= 1;
    z->nbits--;
    if (z->nbits == 0 && z->pos + 2 <= z->n) {                /* PLAYER fetches the next word while taking the 16th bit */
        z->bits = (u32)(z->d[z->pos] | z->d[z->pos + 1] << 8);
        z->pos += 2;
        z->nbits = 16;
    }
    return b;
}

static int lz_byte(LzIn *z)
{
    if (z->pos >= z->n) { z->eof = true; return 0; }
    return z->d[z->pos++];
}

size_t lz_decode(const u8 *in, size_t n, u8 **out)
{
    size_t cap = n * 4 + 1024, len = 0;
    u8 *o = malloc(cap);
    LzIn z = { in, n, 0, 0, 0, false };
    if (!o || n < 2) { *out = o; return 0; }
    for (;;) {
        size_t off, cnt;
        if (lz_getbit(&z)) {
            int b = lz_byte(&z);
            if (z.eof) break;
            if (len + 1 > cap) { cap *= 2; o = realloc(o, cap); }
            o[len++] = (u8)b;
            continue;
        }
        if (z.eof) break;
        if (!lz_getbit(&z)) {
            int hi = lz_getbit(&z), lo = lz_getbit(&z);
            cnt = (size_t)((hi << 1 | lo) + 2);
            int b = lz_byte(&z);
            off = (size_t)(0x100 - b);
        } else {
            int lo = lz_byte(&z), hi = lz_byte(&z);
            if (z.eof) break;
            off = (size_t)(0x2000 - (((hi >> 3) << 8) | lo));
            cnt = (size_t)(hi & 7);
            if (cnt) cnt += 2;
            else {
                int b = lz_byte(&z);
                if (z.eof) break;
                if (b <= 1) continue;                          /* end-of-chunk marker */
                cnt = (size_t)b + 1;
            }
        }
        if (z.eof || off > len || off == 0) break;
        if (len + cnt > cap) { while (len + cnt > cap) cap *= 2; o = realloc(o, cap); }
        for (size_t i = 0; i < cnt; i++, len++) o[len] = o[len - off];
    }
    *out = o;
    return len;
}

bool pan_parse(const u8 *file, size_t flen, PanFile *p)
{
    memset(p, 0, sizeof *p);
    u8 *o;
    size_t len = lz_decode(file, flen, &o);
    if (len < 4 || o[0] != 0x5A) { free(o); return false; }
    int n = o[2] | o[3] << 8;
    size_t hdr = 4 + 2 * (size_t)n + 768;
    if (n == 0 || hdr > len) { free(o); return false; }
    p->speed = o[1];
    p->ncels = n;
    p->data = o;
    p->len = (u32)len;
    memcpy(p->pal6, o + 4 + 2 * (size_t)n, 768);
    p->cel_start = malloc(sizeof(u32) * (size_t)n);
    p->cel_end = malloc(sizeof(u32) * (size_t)n);
    u32 pos = (u32)hdr, end = 0;
    for (int i = 0; i < n; i++) {
        end += (u32)(o[4 + 2 * i] | o[5 + 2 * i] << 8);
        p->cel_start[i] = pos;
        p->cel_end[i] = end;
        pos = end;
    }
    return true;
}

void pan_free(PanFile *p)
{
    free(p->data);
    free(p->cel_start);
    free(p->cel_end);
    memset(p, 0, sizeof *p);
}

/* PLAYER 0000:1006 on one cel: fill pairs, literal runs, skips and the long forms; the screen is updated in place. */
u32 pan_apply_cel(const PanFile *p, int cel, u8 *scr)
{
    if (cel < 0 || cel >= p->ncels) return 0;
    const u8 *src = p->data;
    u32 pos = p->cel_start[cel] + 4;                           /* the cel header B 00 01 00 */
    u32 end = p->len;
    long di = 0;
    const long N = 64000;
#define NEED(k) do { if (pos + (u32)(k) > end) return pos; } while (0)
#define PUT_RUN(ptr, cnt) do { long c_ = (long)(cnt); long room = N - di; \
        if (room > 0) { memcpy(scr + di, (ptr), (size_t)(c_ < room ? c_ : room)); }         di += c_; } while (0)
#define PUT_FILL(val, cnt) do { long c_ = (long)(cnt); long room = N - di; \
        if (room > 0) { memset(scr + di, (val), (size_t)(c_ < room ? c_ : room)); }         di += c_; } while (0)
    NEED(1);
    int cl = src[pos++];
    for (;;) {
        if (cl == 0) {
            while (cl == 0) {
                NEED(2);
                int cnt = src[pos], val = src[pos + 1];
                pos += 2;
                PUT_FILL(val, cnt);
                NEED(1);
                cl = src[pos++];
            }
            continue;
        }
        if (cl < 0x80) {
            NEED(cl);
            PUT_RUN(src + pos, cl);
            pos += (u32)cl;
            NEED(1);
            cl = src[pos++];
            continue;
        }
        cl -= 0x80;
        if (cl) {
            di += cl;
            NEED(1);
            cl = src[pos++];
            continue;
        }
        NEED(2);
        int ax = src[pos] | src[pos + 1] << 8;
        pos += 2;
        if (ax == 0) return pos;
        if (ax < 0x8000) {
            di += ax;
        } else {
            int ch = (ax >> 8) - 0x80;
            int cnt = (ch & 0x3F) << 8 | (ax & 0xFF);
            if (ch < 0x40) { NEED(cnt); PUT_RUN(src + pos, cnt); pos += (u32)cnt; }
            else { NEED(1); PUT_FILL(src[pos], cnt); pos += 1; }
        }
        NEED(1);
        cl = src[pos++];
    }
#undef NEED
#undef PUT_RUN
#undef PUT_FILL
}
