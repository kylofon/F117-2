#pragma once
/* The video module (S5.2, port/spec/video.md): the MGRAPHIC driver (slots 0-83) as C functions, the palette and line
 * routines the programs carry themselves, the .PIC / .PAN / .PAL readers. The prototypes of the functions the specs name
 * are in gen/module_video.h (driver slots, picture decoders) and gen/module_platform.h (gfx_line, pal_*); this header
 * holds the structures the programs pass to them and what the port adds around them.
 *
 * Address model (PORTING.md): a struct or string argument is a HOST pointer into mem[] (DSP(off), mp(seg, off)); a
 * "page" is a real-mode segment (page 0 = A000h = the VGA model's page, the others are DOS blocks from gfx_alloc);
 * pointers to a page are obtained through seg_ptr(). */
#include "mem.h"
#include "types.h"

#pragma pack(push, 1)
/* video.md section 3: the context of the text slots (also the window of sprite_scaled: page, ymin..xmax). */
typedef struct TextCtx {
    s16 page;               /* +00 destination page number (an index into the driver's page table, NOT a segment) */
    s16 flags;              /* +02 low byte: bit 0 = add the font's extra rows to the height; ==1 exactly = opaque background */
    s16 fg;                 /* +04 foreground colour (low byte); the draw routine writes the last colour byte back */
    s16 bg;                 /* +06 background colour (opaque mode) */
    s16 x, y;               /* +08, +0A top-left of the first glyph; x is updated to the end x */
    s16 font;               /* +0C font index 0..6 */
    s16 ymin, ymax;         /* +0E, +10 clip rows, inclusive */
    s16 xmin, xmax;         /* +12, +14 clip columns, inclusive */
} TextCtx;

/* video.md section 8: blit_key / blit_key_clip. */
typedef struct BlitCtx {
    u16 seg;                /* +00 SOURCE SEGMENT (not a page number) */
    s16 sx, sy;             /* +02, +04 */
    s16 dpage, dx, dy;      /* +06, +08, +0A destination page number and position */
    s16 w, h;               /* +0C, +0E */
    s16 ymin, ymax;         /* +10, +12 clip, inclusive (blit_key_clip only) */
    s16 xmin, xmax;         /* +14, +16 */
} BlitCtx;

/* The visible rectangle sprite_scaled returns (the original returns a far pointer to cs:084E). */
typedef struct RectP { s16 x, y, w, h; } RectP;
#pragma pack(pop)

/* ---- The driver (mgraphic.c) --------------------------------------------------------------------------- */
void video_init(void);                      /* once: driver state as MGRAPHIC.EXE is loaded; fonts from the game folder (silently none if absent) */
bool video_fonts_loaded(void);
void video_load_driver_state(u16 dseg, u16 cseg);   /* test aid: the driver's variables from a snapshot of the original overlay (sim tests) */
u8  *seg_ptr(u16 seg);                      /* host pointer to a page / segment: A000..AFFF = the VGA model, else mem[] */
u16  video_draw_seg(void);                  /* test access to the driver's variables */
u16  video_draw_off(void);
u8   video_color(void);
u16  video_page_seg(int page);              /* page_table[page] (0 for numbers beyond the table) */

/* A font as the tools see it (tools/mgfont.py): header bytes and the glyph rows. */
typedef struct FontInfo {
    int first, last, fixed, height, spacing, extra;
    int ptr;                                /* image offset of the glyph data (the font pointer) */
} FontInfo;
bool video_font_info(int font, FontInfo *out);                 /* false: no such font / fonts not loaded */
int  video_font_width(const FontInfo *f, int ch);              /* the width byte of a proportional font (0 = fixed) */
int  video_font_row(const FontInfo *f, int ch, int row);       /* glyph row byte */

/* ---- Palette and lines of the programs (palette.c); DS offsets of the program's own tables ------------------- */
void dac_set(int first, int count, const u8 *rgb6);            /* straight into the VGA DAC, no wait */
void dac_get(int first, int count, u8 *rgb6);
u8   pal6_to_8(u8 v);                                          /* the host's 6 -> 8 bit expansion */

/* ---- Pictures (pic.c, pan.c) --------------------------------------------------------------------------- */
/* A .PIC (FORMATS.md): LZW + RLE-90, 320x200 indexed. `next` supplies the file in pieces. Returns the number of pixels
 * written (64000 = complete). */
typedef struct PicSrc {
    const u8 *data;                          /* the whole file, or NULL when read through `fd` */
    size_t len, pos;
    int fd;                                  /* DOS handle (dos_read) when data == NULL */
} PicSrc;
size_t pic_decode(PicSrc *src, u8 *out64000);
bool   pic_decode_file(const char *name, u8 *out64000);        /* from the game folder (host_game_path) */
bool   pal_read_file(const char *name, u8 *out768);            /* a .PAL: 768 bytes of 6-bit RGB */

typedef struct PanFile {
    int speed, ncels;
    u8  pal6[768];                           /* 6-bit palette */
    u8  *data;                               /* the LZ-decoded stream (malloc'd) */
    u32 len;
    u32 *cel_start, *cel_end;                /* ncels entries each, into data */
} PanFile;
bool pan_parse(const u8 *file, size_t len, PanFile *out);      /* LZ stream -> header, palette, cel table */
u32  pan_apply_cel(const PanFile *p, int cel, u8 *screen64000);/* PLAYER 0000:1006 on one cel; returns the end position */
u32  pan_rle_draw(const u8 *src, u32 pos, u32 end, u8 *screen64000);   /* the same on a stream src[pos..end) (PLAYER's ring buffer in mem[]) */
void pan_free(PanFile *p);
size_t lz_decode(const u8 *in, size_t n, u8 **out);            /* PLAYER 0BE2 (tools/lzs.py): returns the length; *out malloc'd */

/* ---- Self tests (videotest.c) --------------------------------------------------------------------------- */
int video_selftest(void);                   /* --video-selftest: blitters, lines, polygons, text, sprites against vectors from the original */
int video_decoder_test(const char *refdir); /* --decoder-test DIR: every PIC/PAN/font/PAL in the game folder against the Python decoders' output */
int video_screen_test(bool wait);           /* --video-test: a composed screen for a visual check (wait = stay until the window is closed) */
