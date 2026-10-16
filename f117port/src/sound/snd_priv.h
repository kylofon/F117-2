/* The sequencer shared by both drivers (snd_engine.c) and what the two drivers add. Internal to src/sound. */
#pragma once
#include "snd.h"

/* The memory of a driver: its data segment as one array (the code-segment variables of ASOUND.117 follow the data at 1B60h). The tracks are
 * modified in place by the F6 event, so this is a private copy. */
#define SND_MEM 0x10000

typedef enum { F_t, F_slide, F_rstep, F_pstep, F_pitch, F_inst, F_vol, F_gate, F_rel, F_rctr, F_rrel, F_pctr, F_prel, F_pan,
               F_start, F_ptr, F_ls1, F_ls2, F_cnt1, F_cnt2, F_lptr, F_tune, F_vofs, F_COUNT } VoiceField;

typedef struct SndLayout {
    bool logo;                                  /* ASOUND.LOG: older build (hooks differ) */
    int voices;                                 /* DS offset of voice 0 */
    int vsize;                                  /* bytes per voice record */
    int vf[F_COUNT];                            /* field offsets (-1: not in this build) */
    int cur, f03, u0, u1, c0, c1, m0, m1, b0, b1, d0, d1;
    int lcg, shadow, eff, opch, opoff, fnum, inst, trem, vib, csm;
    int ev_min;
    int flag;                                   /* ASOUND.117: the stale 'volume dirty' local of 1360 */
} SndLayout;

extern const SndLayout snd_lay_game, snd_lay_logo;

/* The active engine: memory and layout (set by the driver that is about to run). */
extern u8 *snd_m;
extern const SndLayout *snd_l;

static inline u8  mb(int o) { return snd_m[o & 0xFFFF]; }
static inline u16 mw(int o) { return (u16)(mb(o) | mb(o + 1) << 8); }
static inline void sb_(int o, int v) { snd_m[o & 0xFFFF] = (u8)v; }
static inline void sw_(int o, int v) { sb_(o, v); sb_(o + 1, v >> 8); }

void opl_write(int reg, int val);
void opl_freq(int ch, int bx);
unsigned lcg_rand(void);
void noise_tick(void);                          /* 0562 */
void unit_tail(void);
void update_all(void);                          /* 1882 */
int  voice_addr(int i);
int  vfb(int v, VoiceField f);
void vsb(int v, VoiceField f, int x);
int  vfw(int v, VoiceField f);
void vsw(int v, VoiceField f, int x);
void units_started(void);                       /* per driver */
int  snd_sh(int reg);
extern bool snd_logo_fast;                      /* ASOUND.LOG: the effect-unit (300 Hz) INT 8 mode is on */
void snd_use_game(void);
void snd_use_logo(void);
