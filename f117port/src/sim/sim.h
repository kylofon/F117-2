#pragma once
/* The sim flow (S5.5, port/spec/simflow.md): VGAME.EXE. The prototypes of the functions the spec names are in gen/module_simflow.h; this header holds
 * what the port adds around them: DGROUP accessors, the contexts the HUD draws through, the stubs of the modules that are not ported yet
 * (render3d S5.6, flight / weapons / AI / detection / damage S5.7), the sound call points (S5.9) and the test hooks of simtest.c.
 *
 * Address model (PORTING.md): a pointer argument of the original is a host pointer into mem[] (DSP(off)); a near pointer stored in a table or
 * struct is a DS offset (word). DGROUP is VGAME's: cur_dgroup. TextCtx / BlitCtx are video.h's. */
#include "comm.h"
#include "gen/module_flight.h"
#include "gen/module_platform.h"
#include "gen/module_simflow.h"
#include "gen/module_video.h"
#include "gen/symbols_vgame.h"
#include "host.h"
#include "mem.h"
#include "platform/platform.h"
#include "platform/timer.h"
#include "platform/vga.h"
#include "video/video.h"

/* ---- DGROUP words and bytes by offset (the generated VGAME_DS_ names where the spec has one) ---- */
#define W(o)   DSW(o)
#define SW_(o) DSS(o)
#define B(o)   DSB(o)

static inline TextCtx *tctx(u16 off) { return (TextCtx *)DSP(off); }
static inline char *ds_str(u16 off) { return (char *)DSP(off); }

/* The three page contexts: each is a TextCtx (22 bytes) followed by a word that points back at it (3FE2, 3FFA, 4012). */
#define CTX_P0     0x3FCC
#define CTX_P1     0x3FE4
#define CTX_P2     0x3FFC
#define CTX_P0_PTR 0x3FE2
#define CTX_P1_PTR 0x3FFA
#define CTX_P2_PTR 0x4012

/* ---- library: sim_lib.c ---- */
int  fx_mul15(int a, int b);
int  fx_sin16(int a);
int  fx_cos16(int a);
static inline int fx_neg16(int v) { return (s16)(-v); }

/* ---- the sound driver (S5.9 fills in; sim_stubs.c): slot 101 snd_cmd, 103 snd_poll, 104 snd_noise ---- */
int  sim_snd_cmd(int cmd, int a, int b, int c);         /* TODO(S5.9) */
extern unsigned sim_poll_calls, sim_noise_calls;         /* TEST counters of the sound stand-ins */
extern int sim_poll_result;
void sim_tick_for_test(void);
int  sim_snd_poll(void);                                 /* TODO(S5.9): its result steers the tick divisor of the original ISR */
void sim_snd_noise(void);                                /* TODO(S5.9) */

/* ---- test hooks (simtest.c); all idle in a normal run ---- */
typedef struct SimTest {
    bool active;
    bool render;                    /* --render-test: r3d_scene_draw runs in full under sim_test.active (kept over restore_snap) */
    const char *dump_dir;           /* tests: where simtest.c writes */
    bool stop_at_run;               /* tests: leave the program at the entry of sim_run after dumping the state */
    bool ticks;                     /* tests: ticks mode, the ISR body is run at the entry of sim_run instead */
    int  frames_left;               /* >= 0: end the mission with the abort code after this many frame-loop iterations */
    u16  keys[64];                  /* scripted key words for 1E9E's key fetch (one per frame), 0 = none */
    int  nkeys, next_key;
} SimTest;
extern SimTest sim_test;
