#pragma once
/* The leaf routines of render3d (S5.6a, port/spec/render3d.md Parts 5 and 6): the 12EF edge clipper and polygon assembler, the 1359 raster back end
 * (mode M), segment 1001 (edge2d.c), the sky/ground backdrop 1442 and the 1433 fixed-point library. Everything works on VGAME's DGROUP in mem[] at the
 * original offsets (cur_dgroup), exactly as the original; the prototypes of the typed functions are in gen/module_render3d.h. */
#include "gen/module_render3d.h"
#include "gen/symbols_vgame.h"
#include "mem.h"
#include "platform/vga.h"
#include "types.h"

/* ---- the working buffers of 12EF / 1359 (spec render3d.md "DGROUP globals seen", Part 5): ONE struct over DS:8568 .. DS:9145. Several routines rely on the adjacency
 * of the arrays (the row tables are 480 words; edge_L / edge_R are 2 bytes apart from the end of the previous table), so the arrays are addressed by DS offset
 * through R3D_L() / R3D_R() / R3D_ROW() and may run into their neighbours exactly as in the original. ---- */
#pragma pack(push, 1)
typedef struct R3dWork {
    s32 clip_seg_a[4];      /* 8568  P0 x,y, P1 x,y copy for bisection (12EF:0333) */
    s32 clip_seg_b[4];      /* 8578  second copy (the original ends) */
    s32 zclip_pt[4];        /* 8588  near-plane points 0 and 1 (x s32, y s32 each); also the closing edge record */
    u8  pad8598[16];        /* 8598 */
    u16 clip_c0;            /* 85A8  out-code of end 0 (bisect_in) */
    u16 clip_c1;            /* 85AA  out-code of end 1 */
    s16 clip_dx;            /* 85AC  x1 - x0 (fast path) */
    s16 clip_dy;            /* 85AE  y1 - y0 */
    s16 clip_iter;          /* 85B0  bisection counter 32..-1 */
    u16 poly_flags;         /* 85B2  OR of the edge flag words */
    s16 wall_l_lo, wall_l_hi;   /* 85B4, 85B6  left wall y range (reset 7FFFh / 8000h) */
    s16 wall_r_lo, wall_r_hi;   /* 85B8, 85BA  right wall y range */
    u16 poly_drawn;         /* 85BC  4 once an edge was rasterised */
    u16 zclip_n;            /* 85BE  near-plane points saved */
    u8  clip_ovf;           /* 85C0  16-bit compare overflow seen (outcode16) */
    u8  clip_c1b;           /* 85C1  out-code of end 1 for the 2nd intercept */
    u16 gfx_table;          /* 85C2  CS offset of the dispatch table (M = 0070h) */
    u16 gfx_mode;           /* 85C4  upper-cased mode letter */
    u16 gfx_page_no;        /* 85C6 */
    u8  pad85c8[2];         /* 85C8 */
    s16 win_xmin, win_ymin, win_xmax, win_ymax;     /* 85CA..85D0  clip window */
    u8  pad85d2[4];         /* 85D2 */
    u16 gfx_colour;         /* 85D6  colour word: low byte colour, high byte fill type */
    u8  pad85d8[10];        /* 85D8 */
    u16 gfx_pages[4];       /* 85E2  page segments */
    u16 gfx_seg;            /* 85EA  current draw segment */
    u16 gfx_rows[480];      /* 85EC  row offsets (M: y * 320, 200 used) */
    s16 edge_L[480];        /* 89AC  min x per row, idle 7FFFh (at DS offset; see above) */
    u8  pad8d6c[2];         /* 8D6C */
    s16 edge_R[480];        /* 8D6E  max x per row, idle 8001h */
    u8  pad912e[2];         /* 912E */
    s16 edge_miny;          /* 9130  first touched row, idle 7FFFh */
    u8  pad9132[14];        /* 9132 */
    u16 fill_dither;        /* 9140  lo / hi dither colours */
} R3dWork;
#pragma pack(pop)
#define R3D_W_OFS 0x8568
#define R3D_W ((R3dWork *)DSP(R3D_W_OFS))
_Static_assert(offsetof(R3dWork, clip_c0) == 0x85A8 - R3D_W_OFS, "clip_c0");
_Static_assert(offsetof(R3dWork, poly_flags) == 0x85B2 - R3D_W_OFS, "poly_flags");
_Static_assert(offsetof(R3dWork, clip_ovf) == 0x85C0 - R3D_W_OFS, "clip_ovf");
_Static_assert(offsetof(R3dWork, win_xmin) == 0x85CA - R3D_W_OFS, "win");
_Static_assert(offsetof(R3dWork, gfx_colour) == 0x85D6 - R3D_W_OFS, "gfx_colour");
_Static_assert(offsetof(R3dWork, gfx_pages) == 0x85E2 - R3D_W_OFS, "gfx_pages");
_Static_assert(offsetof(R3dWork, gfx_rows) == 0x85EC - R3D_W_OFS, "gfx_rows");
_Static_assert(offsetof(R3dWork, edge_L) == 0x89AC - R3D_W_OFS, "edge_L");
_Static_assert(offsetof(R3dWork, edge_R) == 0x8D6E - R3D_W_OFS, "edge_R");
_Static_assert(offsetof(R3dWork, edge_miny) == 0x9130 - R3D_W_OFS, "edge_miny");
_Static_assert(offsetof(R3dWork, fill_dither) == 0x9140 - R3D_W_OFS, "fill_dither");

/* byte-offset access to the row tables (o = 2 * row, wraps like the 16-bit register) */
#define R3D_ROW(o) DSW((u16)(0x85EC + (u16)(o)))
#define R3D_L(o)   DSS((u16)(0x89AC + (u16)(o)))
#define R3D_R(o)   DSS((u16)(0x8D6E + (u16)(o)))
#define R3D_MINY   0x9130
#define R3D_EDGES  0x5EBA                       /* edge records, 24 bytes each */
#define R3D_CLOSE  0x8588                       /* the closing edge reuses zclip_pt as a record */

/* edge record fields (DS offset rec): +0 x0, +4 y0, +8 x1, +0C y1 (s32 before clipping, s16 after), +2 flag word, +6 / +0E clamped y, +10h near-plane point */
#define ER_W(rec, o) DSW((u16)((rec) + (o)))
#define ER_S(rec, o) DSS((u16)((rec) + (o)))
#define ER_D(rec, o) DSSL((u16)((rec) + (o)))

/* the page the raster writes to: ES = DS:85EA, a VGA page segment. The unit test redirects it to a plain buffer. */
extern u8 *r3d_test_page;
u8 *seg_ptr(u16 seg);                   /* video/mgraphic.c: A000..AFFF = the VGA model, else mem[] (page 2 lives in conventional memory) */
static inline u8 *r3d_page(void) { return r3d_test_page ? r3d_test_page : seg_ptr(DSW(0x85EA)); }

/* driver call trace: the unit test sets this to see the calls the 1001 code makes to MGRAPHIC (colour, line, polygon fill) instead of making them. kinds: 32 colour (v[0]),
 * 31 line (v[0..3] = x1 y1 x2 y2 as stored), 37 poly_fill (v[0] ymin, v[1] ymax, v[2] mode), 100 poly_fill_far (v[0] colour, n points in v[1..]). */
extern void (*r3d_trace)(int kind, const s16 *v, int n);

/* routines of this module that are labels or have register interfaces in the original (no entry in the generated header) */
void r3d_setcolor_M(u16 colour);        /* 1359:01D4 */
void r3d_line_M(u16 rec);               /* 1359:045D */
void r3d_fill_M(void);                  /* 1359:0AF7 */
u16  r3d_tab_seg(void);                 /* segment 14B2 (the 1433 tables) in the loaded image */
extern u8 r3d_bisect_last_bp;           /* BP after r3d_bisect_in: out-code of the last midpoint (quirk) */
