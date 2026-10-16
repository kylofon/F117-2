/* The sound module (S5.9): the AdLib driver ASOUND.117 and the logo overlay ASOUND.LOG, ported from port/spec/sound.md (sequencer verified write for
 * write against the real drivers, tools/sound/asnd.py). The driver's data (tracks, instruments, tables) is read from the game folder at run time.
 *
 * Threads: the sequencer runs on the main thread only (commands from the programs, the poll from the tick); its OPL writes go through the sink,
 * by default host_opl_write, which locks the audio thread's mutex around the chip. */
#pragma once
#include "types.h"

/* ---- the in-game driver ASOUND.117 (slots 100-104; F117.COM binds it after MPS_LOGO, calls shutdown at quit) */
bool snd_load_driver(char letter);             /* chain: 'A' AdLib; 'I' / 'R' / 'T' fall back to AdLib (PORT); 'N' no sound. false = silent */
void snd_init(unsigned speech_seg);            /* slot 100 */
int  snd_cmd(int cmd, int arg);                /* slot 101: cmd > 2Dh ignored; cmd 2 returns the voice-active byte */
void snd_shutdown(void);                       /* slot 102 */
int  snd_poll(void);                           /* slot 103: 0 unchanged, 1 fast (effect unit started), -1 idle (FFFFh) */
void snd_noise(void);                          /* slot 104 */
bool snd_active(void);                         /* a driver is bound */

/* The sound step of a program's ISR body (START, END, PLAYER: the platform ISR of platform.md section 4): the poll (unless `poll` is false, the
 * skipped 7th tick of END / PLAYER) steers the fast / idle state, and in the fast state snd_noise runs SND_FAST_CLASS times per tick (the
 * original runs its ISR `class` times per retrace, class 4 at 70 Hz, and calls slot 104 on every one of them). Returns the poll result. */
#define SND_FAST_CLASS 4
int  snd_isr_step(bool poll);
extern unsigned snd_isr_ticks, snd_isr_polls, snd_isr_noises;     /* TEST: how often snd_isr_step ran, polled, and called snd_noise */

/* ---- the logo overlay ASOUND.LOG (MPS_LOGO's own slots 100-102; it hooks INT 8 itself: 60.88 Hz, 300 Hz while an effect unit runs) */
bool snd_log_load(char letter);                /* 'A' / 'I' / 'R' / 'T' -> ASOUND.LOG (PORT: AdLib for all); 'N' false */
void snd_log_init(void);                       /* logo_reset (cmd 0); starts the overlay's clock */
void snd_log_cmd(int cmd);                     /* 0 reset / silence, 1 pause, 2 the jingle */
void snd_log_shutdown(void);
unsigned snd_log_isr_count(void);              /* TEST: INT 8s run so far */
void snd_log_isr(void);                        /* one INT 8 of the overlay (the clock calls it; the tests call it directly) */

/* ---- OPL output and tests */
typedef void (*SndOplSink)(u8 reg, u8 val);
void snd_set_opl_sink(SndOplSink sink);        /* NULL: back to host_opl_write */
bool snd_load_driver_file(const char *path);   /* TEST: ASOUND.117 from an explicit path (no host game folder) */
bool snd_log_load_file(const char *path);      /* TEST: ASOUND.LOG */
unsigned snd_poll_count(void);                 /* cs:000E, the number of polls since init */

/* TEST: f117 --sound-test DIR (sndtest.c) */
int sound_selftest(const char *dir);
