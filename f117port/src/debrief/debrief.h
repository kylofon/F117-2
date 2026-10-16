#pragma once
/* The debrief (S5.8, port/spec/debrief.md): END.EXE. The prototypes of the functions the spec names are in gen/module_debrief.h; this
 * header holds what the port adds around them: accessors for the DGROUP variables (the loaded image keeps the strings, tables and the
 * mission data at their original offsets, PORTING.md "DGROUP offsets"), the comm block and the pilot record, and the test hooks of
 * endtest.c. The UI toolkit of START (ui_text, ui_text_wrap, ...) is reused from src/shell where it does not touch program state; the
 * copies whose state lives at END's own DGROUP offsets (cursor, palette queue, fades, waits) are in end_ui.c. */
#include "shell/shell.h"
#include "gen/module_debrief.h"
#include "gen/symbols_end.h"

#define END_CTX0 0x6D8Cu              /* TextCtx of page 0 (the screen) */
#define END_CTX1 0x6DA2u              /* TextCtx of page 1 (the working page) */

static inline u8 *e_comm(void) { return (u8 *)comm_get(); }
static inline u8 *e_pilot(void) { return e_comm() + 0x1272; }     /* the active pilot record, comm+1272h */
#define CW(o)  (*(u16 *)(e_comm() + (o)))                           /* comm word / byte */
#define CB(o)  (*(u8 *)(e_comm() + (o)))
#define EP(o)  PW(e_pilot(), (o))                                    /* pilot record word */

#define E_SITES   0x56EAu             /* 16-byte site records */
#define E_GRID    0x55EAu             /* 16 x 16 territory grid */
#define E_LOG     0x5E38u             /* 6-byte event records */
#define E_OBJCLS  0x6902u             /* object class table */
#define E_NAMES   0x6DFAu             /* near pointer table of the names */
#define E_NAMEPOOL 0x6EFAu
#define E_UNITS_T 0x179Eu             /* 32-byte unit type records */

#define D_PAGE_SEG(i) DSW((u16)(0x55D0 + 2 * (i)))
#define D_LANDCLS   DSB(0x6440)
#define D_CAPTURED  DSB(0x6982)
#define D_PRIMARY   DSB(0x6D89)
#define D_SECONDARY DSB(0x720E)
#define D_MEDAL     DSB(0x643A)
#define D_MEDAL_GHOST DSB(0x55DA)
#define D_CRR       DSB(0x55DD)
#define D_RIBBON    DSB(0x55E2)
#define D_PROMOTED  DSB(0x55E6)
#define D_PROMO_WITHHELD DSB(0x55E7)

static inline char *e_str(u16 off) { return (char *)DSP(off); }                    /* the string at DS:off */
static inline char *e_str_at(u16 table, int idx) { return (char *)DSP(DSW((u16)(table + 2 * idx))); }   /* word table of near string pointers */

/* score.c (the numbers) */
char *e_name(int idx);                              /* name pointer table entry */

/* end_ui.c */
void end_snd(int cmd);
void end_tick(void);                                /* the timer ISR body */

/* ---- test hooks (endtest.c; idle in a normal run) ---- */
typedef struct EndTest {
    bool active;
    bool use_seed;
    u32 seed;
    void (*on_wait)(unsigned ticks);      /* at the entry of wait_input */
    void (*on_poll)(unsigned n);          /* at the entry of poll_input */
    unsigned polls, waits;
} EndTest;
extern EndTest end_test;
void end_test_from_env(void);             /* F117_END_SCRIPT: the script of a normal run */
int debrief_selftest(const char *refroot);
