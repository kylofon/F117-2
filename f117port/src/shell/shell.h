#pragma once
/* The shell (S5.4, port/spec/shell.md): START.EXE. The prototypes of the functions the spec names are in gen/module_shell.h; this header
 * holds what the port adds around them: accessors for the DGROUP structures the ported code shares with the loaded image (the image
 * keeps the strings, tables, dialog descriptors and button tables at their original offsets, PORTING.md "DGROUP offsets"), the sound call
 * points, and the test hooks of shelltest.c.
 *
 * Address model (PORTING.md): a pointer argument of the original is a host pointer into mem[] (DSP(off)); a "near pointer" stored in
 * a table or struct is a DS offset (word). TextCtx is video.h's. */
#include "comm.h"
#include "frontend/fe.h"
#include "gen/module_frontend.h"
#include "gen/module_platform.h"
#include "gen/module_shell.h"
#include "gen/module_video.h"
#include "host.h"
#include "mem.h"
#include "platform/platform.h"
#include "platform/timer.h"
#include "video/video.h"

/* ---- DGROUP structures ---------------------------------------------------------------------------------------- */
#define SH_CTX0 0xDC02u         /* TextCtx of page 0 (the screen) */
#define SH_CTX1 0xDC18u         /* TextCtx of page 1 (the working page) */
#define SH_CTX2 0xDC2Eu         /* TextCtx of page 2 (also `win_text`, the popup/dialog text context) */
static inline TextCtx *tctx(u16 off) { return (TextCtx *)DSP(off); }

static inline u8 *sh_pilot(void) { return far_mp(ds_far(START_DS_ptr_pilot)); }              /* DS:CACA = comm+1272h */
#define PW(p, o) (*(u16 *)((u8 *)(p) + (o)))                                                  /* word of a record */
#define PS(p, o) (*(s16 *)((u8 *)(p) + (o)))
#define PD(p, o) (*(u32 *)((u8 *)(p) + (o)))
static inline char *ds_str(u16 off) { return (char *)DSP(off); }                              /* the string at DS:off */
static inline char *ds_str_ptr(u16 table, int idx) { return (char *)DSP(DSW(table + 2 * idx)); }   /* word table of near string pointers */

/* The pilot record (50h bytes, shell.md Part D): offsets */
#define PIL_SLOT      0x00
#define PIL_NAME      0x02
#define PIL_RANK      0x20
#define PIL_THEATRE   0x38
#define PIL_WAR       0x3A
#define PIL_MTYPE     0x3C
#define PIL_SKILL     0x3E
#define PIL_CRASH     0x40
#define PIL_MODEL     0x42
#define PIL_STATUS    0x4E
#define PILOT_SIZE    0x50
#define ROSTER_PILOT(i) ((u16)(START_DS_roster_pilots + PILOT_SIZE * (i)))         /* DS:D892 + 50h * i */

#define SW(site, f) DSW((u16)((site) + (f)))                                                    /* word / byte field of a record at a DS offset */
#define SB(site, f) DSB((u16)((site) + (f)))
/* The 16-byte site record at DS:CBDE and the 36-byte unit record at DS:D3BE */
#define SITE_SIZE     16
#define SITE(i)       ((u16)(START_DS_wld_sites + SITE_SIZE * (u16)(i)))
#define UNIT_SIZE     36
#define UNIT(i)       ((u16)(START_DS_wld_units + UNIT_SIZE * (u16)(i)))

/* ---- sound (S5.9 fills in the stub) --------------------------------------------------------------------------------- */
void start_tick(void);                                      /* the ISR body of START (start.c) */
static inline void sh_snd(int cmd) { fe_snd_cmd(cmd, 0); }              /* slot 101 snd_cmd(cmd) of ASOUND.117 */

/* ---- test hooks (shelltest.c; all idle in a normal run) -------------------------------------------------------------- */
typedef struct ShellTest {
    bool active;
    bool use_seed;                        /* shell_init seeds rand() with `seed` instead of the BIOS clock */
    u32 seed;
    bool leave_space;                     /* shell_exit leaves a space key in the type-ahead buffer (the chain test: ends PLAYER title-2) */
    unsigned polls;                       /* ui_poll_input calls so far */
    void (*on_poll)(unsigned n);          /* called at the entry of ui_poll_input: the test may set the cursor and queue a key */
} ShellTest;
extern ShellTest shell_test;


void shell_test_from_env(void);           /* F117_SHELL_SCRIPT: the script of a normal run (shelltest.c) */
int shell_selftest(const char *refroot);   /* f117 --shell-test */
