/* Program images: LZEXE 0.91 unpacker (the same algorithm as tools/unlzexe.py), plain MZ reader, and the DOS
 * loader that puts a program into mem[] (platform.md §2 "Start-up", §6; the work of F117.COM's INT 21h/4Bh and
 * of the c0 start-up code that the port does not execute). The seven programs are small-model C programs
 * whose initialised data (strings, tables, initial variable values) is in the image, so the port needs the
 * real DGROUP contents at the original offsets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform/platform.h"

static u16 le16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }

/* Appends to a growing buffer; false when the 4 MB sanity limit is hit. */
typedef struct { u8 *p; size_t n, cap; } Buf;

static bool buf_put(Buf *b, u8 v)
{
    if (b->n == b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 65536;
        if (cap > (4u << 20)) return false;
        u8 *q = realloc(b->p, cap);
        if (!q) return false;
        b->p = q;
        b->cap = cap;
    }
    b->p[b->n++] = v;
    return true;
}

/* The LZEXE 0.91 bit stream: 16-bit words, least significant bit first, the next word is read right after the
 * 16th bit was used. Reads past the end of the file return 0 (a truncated file ends in a false return). */
typedef struct { const u8 *d; size_t len, src; u16 buf; int cnt; } Bits;

static u8 byte_at(Bits *s, size_t i) { return i < s->len ? s->d[i] : 0; }
static u16 word_at(Bits *s, size_t i) { return (u16)(byte_at(s, i) | byte_at(s, i + 1) << 8); }

static int bit(Bits *s)
{
    int b = s->buf & 1;
    if (--s->cnt == 0) { s->buf = word_at(s, s->src); s->src += 2; s->cnt = 16; }
    else s->buf >>= 1;
    return b;
}

static bool lz91(const u8 *d, size_t len, ExeFile *e)
{
    if (len < 0x40) return false;
    const u8 *h = d;                                      /* the 14-word header */
    size_t hdr = (size_t)le16(h + 8) * 16;
    size_t inf = hdr + (size_t)le16(h + 22) * 16;         /* decompressor segment (cs of the packed header) */
    if (inf + 14 > len) return false;
    e->ip = le16(d + inf);
    e->cs = le16(d + inf + 2);
    e->sp = le16(d + inf + 4);
    e->ss = le16(d + inf + 6);

    /* relocation table (0.91: byte spans, 0 / word escapes) */
    size_t p = inf + 0x158;
    u32 *rel = NULL;
    int nrel = 0, caprel = 0;
    unsigned off = 0, seg = 0;
    for (;;) {
        if (p >= len) { free(rel); return false; }
        unsigned span = d[p++];
        if (span == 0) {
            if (p + 2 > len) { free(rel); return false; }
            span = le16(d + p);
            p += 2;
            if (span == 0) { seg += 0x0FFF; continue; }
            if (span == 1) break;
        }
        off += span;
        seg += (off & ~0x0Fu) >> 4;
        off &= 0x0F;
        if (nrel == caprel) {
            caprel = caprel ? caprel * 2 : 256;
            u32 *q = realloc(rel, (size_t)caprel * sizeof *rel);
            if (!q) { free(rel); return false; }
            rel = q;
        }
        rel[nrel++] = (u32)seg << 16 | off;               /* seg:off -> the word at seg*16 + off; kept as a pair */
    }

    Buf out = { 0 };
    Bits s = { d, len, hdr, 0, 16 };
    s.buf = word_at(&s, s.src);
    s.src += 2;
    for (;;) {
        if (s.src > len + 4) goto fail;
        if (bit(&s)) {
            if (!buf_put(&out, byte_at(&s, s.src++))) goto fail;
            continue;
        }
        long span;
        unsigned n;
        if (!bit(&s)) {
            n = (unsigned)(bit(&s) << 1 | bit(&s)) + 2;
            span = (long)byte_at(&s, s.src++) - 0x100;
        } else {
            unsigned lo = byte_at(&s, s.src), hi = byte_at(&s, s.src + 1);
            s.src += 2;
            span = (long)((lo | ((hi & 0xF8u) << 5) | 0xE000u)) - 0x10000;
            n = (hi & 7) + 2;
            if (n == 2) {
                n = byte_at(&s, s.src++);
                if (n == 0) break;
                if (n == 1) continue;
                n++;
            }
        }
        for (unsigned i = 0; i < n; i++) {
            long idx = (long)out.n + span;
            if (idx < 0) goto fail;
            if (!buf_put(&out, out.p[idx])) goto fail;
        }
    }
    e->image = out.p;
    e->image_len = (u32)out.n;
    e->nreloc = nrel;
    e->reloc = rel;
    {
        size_t packed_load = len - hdr;
        long extra = (long)le16(h + 10) - (long)((out.n - packed_load + 15) / 16);
        e->min_alloc = extra > 0 ? (u16)extra : 0;
    }
    return true;
fail:
    free(out.p);
    free(rel);
    return false;
}

static bool plain_mz(const u8 *d, size_t len, ExeFile *e)
{
    if (len < 0x1C) return false;
    unsigned last = le16(d + 2), pages = le16(d + 4), nrel = le16(d + 6), hdr = (unsigned)le16(d + 8) * 16;
    size_t total = (size_t)pages * 512 - (last ? 512 - last : 0);
    if (total > len) total = len;
    size_t tab = le16(d + 24);
    if (hdr > total || tab + (size_t)nrel * 4 > len) return false;
    e->image_len = (u32)(total - hdr);
    e->image = malloc(e->image_len ? e->image_len : 1);
    e->reloc = malloc(nrel ? nrel * sizeof *e->reloc : 1);
    if (!e->image || !e->reloc) { free(e->image); free(e->reloc); return false; }
    memcpy(e->image, d + hdr, e->image_len);
    e->nreloc = (int)nrel;
    for (unsigned i = 0; i < nrel; i++)
        e->reloc[i] = (u32)le16(d + tab + i * 4 + 2) << 16 | le16(d + tab + i * 4);
    e->ss = le16(d + 14);
    e->sp = le16(d + 16);
    e->ip = le16(d + 20);
    e->cs = le16(d + 22);
    e->min_alloc = le16(d + 10);
    return true;
}

bool exe_parse(const u8 *file, size_t len, ExeFile *out)
{
    memset(out, 0, sizeof *out);
    if (len < 0x20 || file[0] != 'M' || file[1] != 'Z') return false;
    if (!memcmp(file + 0x1C, "LZ91", 4)) return lz91(file, len, out);
    return plain_mz(file, len, out);
}

void exe_free(ExeFile *e)
{
    free(e->image);
    free(e->reloc);
    memset(e, 0, sizeof *e);
}

/* ------------------------------------------------------------------ the loader */

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
    u8 *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *len = buf ? (size_t)n : 0;
    return buf;
}

/* MSC c0 start-up: `add sp, N` right at the entry point (END 4DF4: SP = 0800h + 724Eh) leaves the stack
 * after the BSS; find the constant in the first bytes of the entry code. 0 if there is none. */
static u16 c0_add_sp(const ExeFile *e)
{
    size_t at = (size_t)e->cs * 16 + e->ip;
    for (size_t i = at; i + 4 <= e->image_len && i < at + 64; i++)
        if (e->image[i] == 0x81 && e->image[i + 1] == 0xC4) return le16(e->image + i + 2);
    return 0;
}

/* The MSC start-up loads DGROUP with `mov di, imm16` (relocatable word) right before `mov si,[2]` (END 4DDF:
 * BF FA 05 8B 36 02 00). The stack segment in the EXE header is a separate segment after the image and is not
 * DGROUP. Returns false if the pattern is not at the entry point. */
static bool c0_dgroup(const ExeFile *e, u16 *rel)
{
    size_t at = (size_t)e->cs * 16 + e->ip;
    for (size_t i = at; i + 7 <= e->image_len && i < at + 48; i++)
        if (e->image[i] == 0xBF && e->image[i + 3] == 0x8B && e->image[i + 4] == 0x36 && e->image[i + 5] == 2 && e->image[i + 6] == 0) {
            *rel = le16(e->image + i + 1);
            return true;
        }
    return false;
}

bool prog_image_load(ProgId id, const char *tail, ProgImage *out)
{
    size_t len;
    u8 *file = read_game_file(prog_table[id].exe, &len);
    if (!file) return false;
    ExeFile e;
    bool ok = exe_parse(file, len, &e);
    free(file);
    if (!ok) return false;

    /* The block DOS gives a small-model program: PSP, image, and DGROUP up to 64 KB (c0 shrinks the block to
     * DGROUP + 1000h paragraphs at most, platform.md §2). */
    u16 dg_rel;
    if (!c0_dgroup(&e, &dg_rel)) { exe_free(&e); return false; }
    u32 img_paras = (e.image_len + 15) >> 4;
    u32 need = 0x10 + (u32)dg_rel + 0x1000;
    if (need < 0x10 + img_paras + e.min_alloc) need = 0x10 + img_paras + e.min_alloc;
    if (need > 0xFFFF) { exe_free(&e); return false; }
    u16 psp;
    if (!dos_mem_alloc((u16)need, &psp)) { exe_free(&e); return false; }

    wr16((u16)(psp - 1), 1, psp);                          /* the block belongs to the program; so do its allocations */
    dos_owner = psp;
    u16 load = (u16)(psp + 0x10);
    memset(mp(psp, 0), 0, (size_t)need * 16);
    memcpy(mp(load, 0), e.image, e.image_len);
    for (int i = 0; i < e.nreloc; i++) {                   /* word at seg*16+off (image-relative) += load segment */
        u32 at = (e.reloc[i] >> 16) * 16u + (e.reloc[i] & 0xFFFF);
        if (at + 2 <= e.image_len) {
            u8 *w = mp(load, 0) + at;
            u16 v = (u16)(w[0] | w[1] << 8);
            v = (u16)(v + load);
            w[0] = (u8)v;
            w[1] = (u8)(v >> 8);
        }
    }
    /* PSP: INT 20h, top of memory (offset 2), the command tail (offset 80h: length, text, CR). */
    wr8(psp, 0, 0xCD);
    wr8(psp, 1, 0x20);
    wr16(psp, 2, MEM_TOP_SEG);
    size_t tl = strlen(tail);
    if (tl > 126) tl = 126;
    wr8(psp, 0x80, (u8)tl);
    memcpy(mp(psp, 0x81), tail, tl);
    wr8(psp, (u16)(0x81 + tl), 0x0D);

    out->psp = psp;
    out->load = load;
    out->dgroup = (u16)(load + dg_rel);
    out->paras = (u16)need;
    out->stack_limit = c0_add_sp(&e);
    out->stack_top = (u16)(e.sp + out->stack_limit);
    exe_free(&e);
    return true;
}

void prog_image_free(const ProgImage *img)
{
    dos_mem_free_owned(img->psp);
    dos_owner = PSP_SEG;
}
