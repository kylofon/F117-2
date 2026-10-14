#pragma once
/* Real-mode memory model (platform.md §6, RE_GUIDE 2.1; PORTING.md "Memory model").
 *
 * mem[] is the 1 MB real-mode address space (plus the HMA margin). F-117A is seven small-model C
 * programs (SETUP, MPS_LOGO, PLAYER, START, VGAME, END, DSWAP) that F117.COM runs one after the other
 * with INT 21h/4Bh; in the port they are C functions of one process (prog.c) and what lives here is what
 * they share or what the ported code addresses as DS:offset:
 *
 *   0000:0000  interrupt vectors (all point at an IRET; the port keeps hook flags instead)
 *   0000:04F0  segment of the comm block (F117.COM allocates it), 04F2 first-run word, 04F4 init word
 *   0040:0000  BIOS data area (equipment word, keyboard flags)
 *   PSP_SEG    F117.COM's block: shrunk to 7Ch paragraphs, then the comm block (140h paragraphs, comm.h)
 *   above      DOS memory arena up to 9FFF:000F (dos_mem_alloc: INT 21h 48h/49h/4Ah with MCBs)
 *   A000-AFFF  NOT used: video memory is the VGA model's own 256 KB (platform/vga.h, vga_page())
 *   F000:FFFE  BIOS model byte
 *
 * Each program has its own DGROUP (static data, BSS, near heap, stack in one 64 KB segment): S5 loads the
 * initialised data of a program's unpacked EXE (work/unpacked, tools/unlzexe.py) into a segment of
 * mem[] and sets cur_dgroup, so ported code reads and writes its globals as DS:offset with the
 * original offsets (symbols_*.h, S4.2). The unpacking and relocation code is S5.1 work and is not here. */
#include "types.h"

#define MEM_SIZE    0x110000u
#define PSP_SEG     0x0200                       /* F117.COM's PSP; the programs it runs load after it */
#define MEM_TOP_SEG 0xA000                       /* end of conventional memory */

extern u8 mem[MEM_SIZE];

/* Current program's DGROUP segment (set by prog.c when it starts a program). */
extern u16 cur_dgroup;

static inline u32 lin(u16 seg, u16 off) { return ((u32)seg << 4) + off; }
static inline u8 *mp(u16 seg, u16 off) { return mem + lin(seg, off); }

static inline u8  rd8 (u16 seg, u16 off) { return mem[lin(seg, off)]; }
static inline u16 rd16(u16 seg, u16 off) { u8 *p = mp(seg, off); return (u16)(p[0] | p[1] << 8); }
static inline u32 rd32(u16 seg, u16 off) { u8 *p = mp(seg, off); return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static inline void wr8 (u16 seg, u16 off, u8 v)  { mem[lin(seg, off)] = v; }
static inline void wr16(u16 seg, u16 off, u16 v) { u8 *p = mp(seg, off); p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static inline void wr32(u16 seg, u16 off, u32 v) { u8 *p = mp(seg, off); p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

/* Lvalue accessors for globals of the CURRENT program's DGROUP (little-endian host). Ported code uses
 * its own symbols: DSW(START_quit_flag) = 1. */
#define DSB(o)  (*(u8  *)mp(cur_dgroup, (u16)(o)))
#define DSC(o)  (*(s8  *)mp(cur_dgroup, (u16)(o)))
#define DSW(o)  (*(u16 *)mp(cur_dgroup, (u16)(o)))
#define DSS(o)  (*(s16 *)mp(cur_dgroup, (u16)(o)))
#define DSL(o)  (*(u32 *)mp(cur_dgroup, (u16)(o)))
#define DSSL(o) (*(s32 *)mp(cur_dgroup, (u16)(o)))
#define DSP(o)  (mp(cur_dgroup, (u16)(o)))                  /* u8 * to DS:o */

/* 16:16 far pointer as stored in memory (offset first). */
typedef struct { u16 off, seg; } FarPtr;

static inline FarPtr far_rd(u16 seg, u16 off) { FarPtr p = { rd16(seg, off), rd16(seg, (u16)(off + 2)) }; return p; }
static inline void   far_wr(u16 seg, u16 off, FarPtr p) { wr16(seg, off, p.off); wr16(seg, (u16)(off + 2), p.seg); }
static inline FarPtr ds_far(u16 off) { return far_rd(cur_dgroup, off); }             /* far ptr stored at DS:off */
static inline void   ds_far_wr(u16 off, FarPtr p) { far_wr(cur_dgroup, off, p); }
static inline u8    *far_mp(FarPtr p) { return mp(p.seg, p.off); }
static inline FarPtr far_make(u16 seg, u16 off) { FarPtr p = { off, seg }; return p; }
static inline FarPtr far_add(FarPtr p, u16 n) { p.off = (u16)(p.off + n); return p; }   /* far (not huge) arithmetic */
static inline u32    far_lin(FarPtr p) { return lin(p.seg, p.off); }
static inline FarPtr far_norm(u32 linear) { FarPtr p = { (u16)(linear & 0x0F), (u16)(linear >> 4) }; return p; }
static inline bool   far_is_null(FarPtr p) { return p.off == 0 && p.seg == 0; }
static inline FarPtr ds_ptr(u16 off) { return far_make(cur_dgroup, off); }          /* DGROUP address as far ptr */

/* ---- The emulated DGROUP stack (SS = DS). Locals whose near address the original passes on (sprintf
 * buffers, structs handed to library calls) are allocated here so they have a DS offset:
 *     u16 mark = stk_mark(); u16 buf = stk_alloc(80); ... stk_release(mark);
 * dg_sp is set by the program start-up code (MSC c0: SP = top of DGROUP). */
extern u16 dg_sp;
u16  stk_alloc(u16 bytes);                        /* returns the DS offset (word aligned) */
static inline u16 stk_mark(void) { return dg_sp; }
static inline void stk_release(u16 mark) { dg_sp = mark; }

/* ---- Low memory and the DOS memory arena (INT 21h 48h/49h/4Ah, platform.md §6). */
void mem_init_machine(void);                          /* once: IVT, BIOS data area, BIOS model byte */
void dos_arena_reset(u16 psp_seg, u16 psp_paras);     /* the PSP block (psp_paras long), the rest free */
bool dos_mem_alloc(u16 paras, u16 *seg_or_largest);   /* AH=48h: false = CF (value = largest block) */
bool dos_mem_free(u16 seg);                           /* AH=49h */
bool dos_mem_resize(u16 seg, u16 paras, u16 *largest);/* AH=4Ah */
u16  dos_mem_largest(void);
extern u16 dos_owner;                                 /* owner (PSP segment) recorded in the MCB of new blocks */
void dos_mem_free_owned(u16 owner);                   /* INT 21h/4Ch: free every block of a terminated program */                           /* AH=48h BX=FFFFh result */

/* Interrupt vector table entries (INT 21h 25h/35h). */
static inline FarPtr ivt_get(u8 n) { return far_rd(0, (u16)(n * 4)); }
static inline void   ivt_set(u8 n, FarPtr p) { far_wr(0, (u16)(n * 4), p); }
