/* ASOUND.LOG, the logo-jingle overlay of MPS_LOGO (sound.md section 11): the same sequencer on the older layout, driven by the overlay's own INT 8
 * hook. Slow mode: PIT reload 4C90h = 60.88 Hz, the sequencer runs on every interrupt. Fast mode (an effect unit started; the jingle never does):
 * reload 0F88h = 300.1 Hz, snd_noise-style pitch noise on every interrupt, the sequencer on every 5th. The port keeps that clock itself, against
 * host_ns(), called from the tick (timer_set_aux). Ported from tools/sound/asnd.py (Logo). */
#include <string.h>

#include "platform/timer.h"
#include "sound/snd_priv.h"
#include "gen/module_sound.h"
#include "host.h"

static u8 logo_mem[SND_MEM];
static bool logo_loaded, logo_bound;
bool snd_logo_fast;                              /* ASOUND.LOG: the 300 Hz INT 8 routine is installed (04AE) */
static int logo_div = 3;                         /* interrupts until the next sequencer step in fast mode */
static u64 logo_next_ns;

#define A_playing 0x43A
#define LOGO_DS_SKIP 0x11C0                      /* the data segment starts at image offset 11C0h */

void snd_use_logo(void) { snd_m = logo_mem; snd_l = &snd_lay_logo; }

bool snd_read_image(const char *path, u8 *mem, unsigned skip);

bool snd_log_load_file(const char *path)
{
    logo_bound = false;
    logo_loaded = snd_read_image(path, logo_mem, LOGO_DS_SKIP);
    return logo_loaded;
}

bool snd_log_load(char letter)
{
    logo_bound = false;
    logo_loaded = false;
    if (letter >= 'a' && letter <= 'z') letter = (char)(letter - 0x20);
    if (letter == 'N') return false;
    /* PORT: RSOUND.LOG (MT-32) and ISOUND.LOG (IBM / Tandy) are not ported: every letter plays ASOUND.LOG on the AdLib model */
    char *path = host_game_path("ASOUND.LOG", false);
    if (!path) return false;
    bool ok = snd_log_load_file(path);
    host_free(path);
    return ok;
}

/* ---- commands (entry 101) */
void logo_reset(void)                            /* 019A + 0220 */
{
    sw_(0x3DC, 0); sw_(0x3DA, 0); sw_(0x3DE, 0); sb_(A_playing, 0);
    for (int i = 0; i < 7; i++) {
        int v = voice_addr(i);
        for (int k = 0; k < 3; k++) sb_(v + k, 0);
    }
    sb_(snd_l->c0, 0); sb_(snd_l->c1, 0);
    sw_(snd_l->m0, 0); sw_(snd_l->m1, 0); sw_(snd_l->b0, 0); sw_(snd_l->b1, 0); sw_(snd_l->d0, 0); sw_(snd_l->d1, 0);
    sb_(A_playing, 0xFF);
    for (int r = 0xFF; r >= 1; r--) opl_write(r, 0);                       /* 0220 logo_hw_reset */
    opl_write(1, 0x20);
}

void logo_spawn(int voice, int track, int lptr)  /* 0515 */
{
    int keep = mb(A_playing);
    sb_(A_playing, 0);
    int v = voice_addr(voice);
    vsb(v, F_slide, 0); vsb(v, F_rrel, 0xFF); vsb(v, F_rstep, 0); vsb(v, F_tune, 0); vsb(v, F_vofs, 0);
    vsw(v, F_start, track); vsw(v, F_ptr, track); vsw(v, F_ls1, track); vsw(v, F_ls2, track);
    vsw(v, F_cnt1, 0); vsw(v, F_cnt2, 0); vsb(v, F_gate, 0);
    vsw(v, F_lptr, lptr); vsb(v, F_t, 1);
    sb_(A_playing, keep);
}

void logo_cmd1(void) { for (int i = 0; i < 7; i++) vsw(voice_addr(i), F_ptr, 0x100); }   /* pause: every stream is the silence at DS:0100 */

void logo_cmd2(void)                             /* the jingle: six voices */
{
    static const u16 tracks[6] = { 0x10E, 0x120, 0x14C, 0x17A, 0x1A6, 0x1B6 };
    for (int i = 0; i < 6; i++) logo_spawn(i, tracks[i], 0);
}

/* ---- the sequencer step and the two INT 8 routines */
void logo_poll(void)                             /* 0344 */
{
    lcg_rand();
    if (mb(A_playing) == 0) return;
    update_all();
    if ((mb(snd_l->c1) | mb(snd_l->c0)) == 0) { snd_logo_fast = false; return; }   /* 03F8: back to the slow ISR */
    unit_tail();
}

void logo_isr_slow(void) { logo_div = 3; logo_poll(); }          /* 04E5 */

void logo_isr_fast(void)                         /* 042F */
{
    noise_tick();
    if (--logo_div == 0) { logo_div = 5; logo_poll(); }
}

static unsigned logo_isr_count;
unsigned snd_log_isr_count(void) { return logo_isr_count; }          /* TEST */

void snd_log_isr(void)
{
    if (!logo_bound) return;
    logo_isr_count++;
    host_audio_lock();
    snd_use_logo();
    if (snd_logo_fast) logo_isr_fast(); else logo_isr_slow();
    host_audio_unlock();
}

/* The overlay's interrupt clock: 1193182 Hz / 4C90h (slow) or / 0F88h (fast), against the host clock, caught up from the 70 Hz tick. */
static u64 period_ns(void) { return snd_logo_fast ? 3976ull * 1000000000ull / 1193182ull : 19600ull * 1000000000ull / 1193182ull; }

static void logo_clock(void)
{
    u64 now = host_ns();
    for (int n = 0; logo_next_ns <= now && n < 64; n++) {
        snd_log_isr();
        logo_next_ns += period_ns();
    }
    if (logo_next_ns <= now) logo_next_ns = now + period_ns();      /* far behind (a long stall): drop the backlog */
}

void snd_log_init(void)
{
    logo_bound = false;
    if (!logo_loaded) return;
    host_audio_lock();
    snd_use_logo();
    snd_logo_fast = false;
    logo_div = 3;
    logo_reset();
    logo_bound = true;
    host_audio_unlock();
    logo_next_ns = host_ns();
    timer_set_aux(logo_clock);                              /* 03F8: INT 8 hooked */
}

void snd_log_cmd(int cmd)
{
    if (!logo_bound) return;
    host_audio_lock();
    snd_use_logo();
    if (cmd == 0) logo_reset();
    else if (cmd == 1) logo_cmd1();
    else if (cmd == 2) logo_cmd2();
    host_audio_unlock();
}

void snd_log_shutdown(void)
{
    timer_set_aux(NULL);
    logo_bound = false;
    snd_logo_fast = false;
}
