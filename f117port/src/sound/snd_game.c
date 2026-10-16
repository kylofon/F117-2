/* ASOUND.117, the in-game AdLib driver (sound.md sections 2, 3, 6, 7.1, 8): the loader, snd_init / snd_cmd / snd_shutdown / snd_poll / snd_noise,
 * the 46 commands as track starts, the engine drone and the poll body, plus the sound step of the program ISRs (snd_isr_step).
 * State 0 only (plain OPL2): the Sound Blaster / Pro Audio Spectrum paths and the speech playback (commands 41-45, SPEECH.117 through the
 * 8000 Hz PCM of the SB DMA or the AdLib 'speech' trick) are not ported (PORT: those commands are silent). Ported from tools/sound/asnd.py (Game). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound/snd_priv.h"
#include "gen/module_sound.h"
#include "host.h"

static u8 game_mem[SND_MEM];
static bool game_loaded;                        /* ASOUND.117 was read */
static bool game_bound;                         /* snd_init has run (and snd_shutdown has not) */
static bool isr_fast;                           /* snd_poll said "effect units running": snd_noise runs SND_FAST_CLASS times per tick */

void snd_use_game(void) { snd_m = game_mem; snd_l = &snd_lay_game; }
bool snd_active(void) { return game_bound; }

/* An ASOUND.117 / ASOUND.LOG file: an MZ-style header of `[08h]` paragraphs, then the image whose first `skip` bytes are not part of the data
 * segment (0 for ASOUND.117; the .LOG overlay's code comes first and its data segment starts at image offset 11C0h). */
bool snd_read_image(const char *path, u8 *mem, unsigned skip)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    u8 *d = NULL;
    long n = 0;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    bool ok = false;
    if (n > 0x40 && fseek(f, 0, SEEK_SET) == 0 && (d = malloc((size_t)n)) != NULL && fread(d, 1, (size_t)n, f) == (size_t)n) {
        unsigned hdr = (unsigned)(d[8] | d[9] << 8) * 16u;
        if (hdr + skip < (unsigned)n) {
            size_t len = (size_t)n - hdr - skip;
            if (len > SND_MEM) len = SND_MEM;
            memset(mem, 0, SND_MEM);
            memcpy(mem, d + hdr + skip, len);
            ok = true;
        }
    }
    free(d);
    fclose(f);
    return ok;
}

bool snd_load_driver_file(const char *path)
{
    game_bound = false;
    game_loaded = snd_read_image(path, game_mem, 0);
    return game_loaded;
}

bool snd_load_driver(char letter)
{
    game_bound = false;
    game_loaded = false;
    if (letter >= 'a' && letter <= 'z') letter = (char)(letter - 0x20);
    if (letter == 'N') return false;                       /* NSOUND: no sound */
    /* PORT: ISOUND (IBM / Tandy / speaker) and RSOUND (Roland MT-32) are not ported; they play through the AdLib model instead (decision U7 item 1) */
    char *path = host_game_path("ASOUND.117", false);
    if (!path) return false;
    bool ok = snd_load_driver_file(path);
    host_free(path);
    return ok;
}

unsigned snd_poll_count(void) { snd_use_game(); return mw(0x1B6E); }

/* ---- reset, voices, engine */
#define A_busy 0x17EC
#define A_poll_result 0x17DC
#define A_rate_state 0x17DE
#define A_eng_value 0x17F4
#define A_eng_on 0x17F6
#define A_eng_last 0x17E6
#define A_eng_fnum 0x17E8
#define A_eng_vol 0x17EA
#define A_speech_seg 0x17E2
#define A_state 0x17F2

void opl_hw_reset(void)                          /* 0210 */
{
    for (int r = 0x5F; r >= 0x40; r--) opl_write(r, 0x3F);
    for (int r = 0xFF; r >= 0x60; r--) opl_write(r, 0);
    for (int r = 0x3F; r >= 1; r--) opl_write(r, 0);
    opl_write(1, 0x20);
}

void snd_reset_all(void)                         /* 018F (cmd 0) */
{
    int old = mw(A_busy);
    sw_(A_busy, 0xFFFF);
    for (int i = 0; i < 7; i++) {
        int v = voice_addr(i);
        for (int k = 0; k < 4; k++) sb_(v + k, 0);
    }
    sb_(snd_l->c0, 0); sb_(snd_l->c1, 0);
    sw_(snd_l->m0, 0); sw_(snd_l->m1, 0); sw_(snd_l->b0, 0); sw_(snd_l->b1, 0); sw_(snd_l->d0, 0); sw_(snd_l->d1, 0);
    opl_hw_reset();
    sw_(A_busy, old);
}

void spawn_voice(int voice, int track, int lptr)         /* 05E7 (entries 05B9..05E3 set BX = the voice) */
{
    int old = mw(A_busy);
    sw_(A_busy, 0);
    int v = voice_addr(voice);
    vsb(v, F_slide, 0); vsb(v, F_rstep, 0); vsb(v, F_pstep, 0); vsb(v, F_tune, 0); vsb(v, F_vofs, 0);
    vsb(v, F_rrel, 0xFF); vsb(v, F_pan, 0x40);
    vsw(v, F_start, track); vsw(v, F_ptr, track); vsw(v, F_ls1, track); vsw(v, F_ls2, track);
    vsw(v, F_cnt1, 0); vsw(v, F_cnt2, 0); vsb(v, F_gate, 0);
    vsw(v, F_lptr, lptr); vsb(v, F_t, 1);
    sw_(A_busy, old);
}
void spawn_voice0(int track) { spawn_voice(0, track, 0); }
void spawn_voice1(int track) { spawn_voice(1, track, 0); }
void spawn_voice2(int track) { spawn_voice(2, track, 0); }
void spawn_voice3(int track) { spawn_voice(3, track, 0); }
void spawn_voice4(int track) { spawn_voice(4, track, 0); }
void spawn_voice5(int track) { spawn_voice(5, track, 0); }
void spawn_voice6(int track) { spawn_voice(6, track, 0); }

void units_started(void)                         /* 05A5 */
{
    if (snd_l->logo) { snd_logo_fast = true; return; }          /* ASOUND.LOG 04AE: INT 8 -> the 300 Hz routine */
    if (mw(A_rate_state) != 1) { sw_(A_rate_state, 1); sw_(A_poll_result, 1); }
}

/* ---- the command table (cs:0911) */
static int cmd_n, cmd_arg, cmd_ret;             /* the command being run: BX = 2 * cmd, the stack argument, AX */

typedef struct { signed char voice; u16 track; } SeqEnt;
static void seq(const SeqEnt *s) { for (; s->voice >= 0; s++) spawn_voice(s->voice, s->track, 0); }
#define CMD_SEQ(n, ...) void cmd##n(void) { static const SeqEnt s_[] = { __VA_ARGS__, { -1, 0 } }; seq(s_); }

void cmd1(void) { for (int i = 0; i < 7; i++) vsw(voice_addr(i), F_ls1, 0xE9A); }     /* soft stop: every voice loops the silence at E9A */
int  cmd2(void) { int r = 0; for (int i = 0; i < 7; i++) r |= vfb(voice_addr(i), F_t); cmd_ret = r; return r; }   /* OR of the voice timers */
CMD_SEQ(3, { 0, 0xFE0 }, { 1, 0x11AC }, { 2, 0x121A }, { 3, 0x12FC }, { 4, 0x1448 }, { 5, 0x154C })    /* PLAYER intro soundtrack */
void cmd4(void) { spawn_voice(1, mw(0x1B60 + 0x86E + (((cmd_n * 2) >> 3) & 6)), 0); }
void cmd5(void) { spawn_voice(0, mw(0x1B60 + 0x857 + (((cmd_n * 2) >> 2) & 6)), 0); }
CMD_SEQ(6, { 4, 0xF28 })
CMD_SEQ(7, { 3, 0xF52 })
CMD_SEQ(8, { 3, 0xF4A })
void cmd9(void)                                  /* the missile launch: not again while it still plays */
{
    if (mw(0x1972) == 0xF3C && mb(0x1964) != 0) return;
    spawn_voice(3, 0xF3C, 0);
}
CMD_SEQ(10, { 5, 0xF94 })

static void set_engine(int bx)                   /* 03D8: the engine drone value, clamped to 301h; 0 also clears the last value */
{
    if (bx < 0x8000 && bx > 0x301) bx = 0x301;
    sw_(A_eng_value, bx);
    if (bx == 0) { sw_(A_eng_last, 0); return; }
    sw_(A_eng_fnum, (bx & 0xFFFE) + 0x883);
}
void cmd11(void)                                 /* engine on if idle; the afterburner / gear effect on voice 3 */
{
    if (mw(A_eng_value) == 0) set_engine(1);
    spawn_voice(3, 0xFA2, 0);
}
CMD_SEQ(12, { 3, 0xFB4 })
CMD_SEQ(13, { 0, 0xF34 })
CMD_SEQ(14, { 5, 0xF8A })
CMD_SEQ(15, { 0, 0xF78 })
CMD_SEQ(16, { 0, 0xF70 })
CMD_SEQ(17, { 1, 0xF68 })
CMD_SEQ(18, { 5, 0xF80 })
CMD_SEQ(19, { 3, 0xFC8 }, { 2, 0xFD4 })
CMD_SEQ(20, { 5, 0xF5A })

void opl_mute_reg(int reg) { opl_write(reg, snd_sh(reg) | 0x3F); }
void opl_unmute_reg(int reg, int vol) { opl_write(reg, (snd_sh(reg) & 0xC0) | (vol ^ 0x3F)); }

void cmd21(void)                                 /* mute the carriers and park the effect units */
{
    sb_(0x17F0, mb(snd_l->c0)); sb_(snd_l->c0, 0);
    sb_(0x17F1, mb(snd_l->c1)); sb_(snd_l->c1, 0);
    sw_(A_busy, 0xFFFF);
    static const u8 regs[] = { 0x43, 0x44, 0x45, 0x4B, 0x4C, 0x4D, 0x53 };
    for (unsigned i = 0; i < sizeof regs; i++) opl_mute_reg(regs[i]);
}
void cmd22(void)                                 /* unmute the carriers and restore the units */
{
    static const u8 regs[] = { 0x43, 0x44, 0x45, 0x4B, 0x4C, 0x4D, 0x53 };
    for (int i = 0; i < 7; i++) opl_unmute_reg(regs[i], vfb(voice_addr(i), F_vol));
    sb_(snd_l->c0, mb(0x17F0)); sb_(snd_l->c1, mb(0x17F1));
    if (mb(0x17F0) | mb(0x17F1)) units_started();
    sw_(A_busy, 0);
}
void cmd_engine_value(void) { set_engine(cmd_arg); }          /* 23: the engine drone value from the stack argument */
void cmd_engine_on(void) { sb_(A_eng_on, 0xFF); }             /* 24 */
void cmd_engine_off(void) { sb_(A_eng_on, 0); sw_(A_eng_last, 0xFFFF); }   /* 25 */
CMD_SEQ(26, { 5, 0xE9C })
CMD_SEQ(27, { 0, 0xEB4 })
static void cmd_rnd(int voice, int track, int off, int base)   /* 28 / 33: a random byte patched into the track, and the track's loop pointer */
{
    int r = (int)(lcg_rand() & 3);
    sb_(track + off, r + base);
    spawn_voice(voice, track, r + base);
}
void cmd28(void) { cmd_rnd(0, 0xEC0, 6, 0x21); }
CMD_SEQ(29, { 0, 0xED2 })
CMD_SEQ(30, { 0, 0xECA })
CMD_SEQ(31, { 2, 0xEE4 })
CMD_SEQ(32, { 2, 0xEEC })
void cmd33(void) { cmd_rnd(0, 0xEDA, 6, 1); }
CMD_SEQ(34, { 5, 0xEAC })
CMD_SEQ(35, { 5, 0xEA4 })
CMD_SEQ(36, { 0, 0x876 }, { 1, 0x992 }, { 2, 0xA9A }, { 3, 0xB2E })                    /* END: the jingles */
void cmd37(void)
{
    static const SeqEnt s[] = { { 0, 0xB66 }, { 1, 0xBA0 }, { 2, 0xBDE }, { 3, 0xC1C }, { 4, 0xB5E }, { 5, 0xB5E }, { -1, 0 } };
    seq(s);
    sw_(voice_addr(4) + snd_lay_game.vf[F_lptr], 0xB6A);
    sw_(voice_addr(5) + snd_lay_game.vf[F_lptr], 0xBA6);
}
CMD_SEQ(38, { 0, 0xC76 }, { 1, 0xD10 }, { 2, 0xDB2 }, { 3, 0xE24 })
CMD_SEQ(39, { 0, 0x766 }, { 1, 0x7B4 }, { 2, 0x7CE }, { 3, 0x7EE }, { 4, 0x830 }, { 5, 0x85C })
void cmd40(void) { sw_(A_speech_seg, cmd_arg); }
static void cmd_none(void) { }                    /* 41-45: the speech clips (SPEECH.117 through the SB DMA / AdLib speech trick): PORT: silent */

static void (*const cmd_tab[46])(void) = {
    snd_reset_all, cmd1, cmd_none, cmd3, cmd4, cmd5, cmd6, cmd7, cmd8, cmd9, cmd10, cmd11, cmd12, cmd13, cmd14, cmd15, cmd16, cmd17,
    cmd18, cmd19, cmd20, cmd21, cmd22, cmd_engine_value, cmd_engine_on, cmd_engine_off, cmd26, cmd27, cmd28, cmd29, cmd30, cmd31, cmd32, cmd33,
    cmd34, cmd35, cmd36, cmd37, cmd38, cmd39, cmd40, cmd_none, cmd_none, cmd_none, cmd_none, cmd_none,
};

/* ---- the driver entries (slots 100-104) */
void snd_init(unsigned speech_seg)
{
    isr_fast = false;
    game_bound = false;
    if (!game_loaded) return;
    host_audio_lock();
    snd_use_game();
    sw_(A_speech_seg, (int)speech_seg);
    sw_(A_state, 0);
    snd_reset_all();
    game_bound = true;
    host_audio_unlock();
}

int snd_cmd(int cmd, int arg)
{
    if (!game_bound || (unsigned)(cmd & 0xFFFF) > 0x2D) return 0;
    host_audio_lock();
    snd_use_game();
    cmd_n = cmd & 0xFFFF;
    cmd_arg = arg & 0xFFFF;
    cmd_ret = 0;
    if (cmd_n == 2) cmd2(); else cmd_tab[cmd_n]();
    host_audio_unlock();
    return cmd_ret;
}

void snd_shutdown(void)
{
    if (game_bound) {
        host_audio_lock();
        snd_use_game();
        snd_reset_all();                                   /* the RTC -> DOS clock part is not needed: the PIT was never retuned */
        host_audio_unlock();
    }
    game_bound = false;
    isr_fast = false;
}

void engine_vol(void)                            /* 03FB */
{
    if (mb(A_eng_on) == 0 || mw(A_eng_value) == 0) return;
    unsigned ax = mw(A_eng_vol);
    opl_write(0x50, (int)(0x3F - ax));
    opl_write(0x53, (int)ax);
}

void poll_body(void)                             /* 0430 */
{
    sw_(0x1B6E, mw(0x1B6E) + 1);
    lcg_rand();
    if (mw(A_busy) != 0) return;
    bool eng = mb(A_eng_on) != 0 && mw(A_eng_value) != 0;
    if (eng) {
        if (mb(0x19BE) == 0) { sw_(0x19CE, 0xE94); sb_(0x19BE, 1); }       /* voice 6: the drone track FC 09 F9 2B 00 03 00 00 */
        else { sb_(0x19BE, 2); sb_(0x19C6, 2); }
    }
    update_all();
    if (eng) {
        unsigned ax = mw(A_eng_value);
        if (ax != mw(A_eng_last)) {
            sw_(A_eng_last, (int)ax);
            opl_freq(6, mw(A_eng_fnum));
            sw_(A_eng_vol, (int)((((ax % 0x55) << 8) | (ax / 0x55)) + 0x36));
            engine_vol();
        }
    }
    if ((mb(snd_l->c1) | mb(snd_l->c0)) == 0) {
        if (mw(A_rate_state) != 0xFFFF) { sw_(A_rate_state, 0xFFFF); sw_(A_poll_result, 0xFFFF); }
        return;
    }
    unit_tail();
}

int snd_poll(void)
{
    if (!game_bound) return 0;
    host_audio_lock();
    snd_use_game();
    sw_(A_poll_result, 0);
    poll_body();
    int r = (s16)mw(A_poll_result);
    host_audio_unlock();
    return r;
}

void snd_noise(void)
{
    if (!game_bound) return;
    host_audio_lock();
    snd_use_game();
    noise_tick();
    host_audio_unlock();
}

unsigned snd_isr_ticks, snd_isr_polls, snd_isr_noises;

int snd_isr_step(bool poll)
{
    int r = 0;
    snd_isr_ticks++;
    if (poll) {
        snd_isr_polls++;
        r = snd_poll();
        if (r > 0) isr_fast = true;
        else if (r < 0) isr_fast = false;
    }
    if (isr_fast)
        for (int i = 0; i < SND_FAST_CLASS; i++) { snd_isr_noises++; snd_noise(); }
    return r;
}
