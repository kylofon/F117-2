/* The sequencer shared by ASOUND.117 and ASOUND.LOG (sound.md sections 5, 7): OPL access with the shadow registers, the voice primitives, the
 * effect units and noise, the per-voice tick and the events. The two builds differ in the memory layout (SndLayout) and in a few hooks
 * (setop order, the volume-dirty flag, the event table), selected by SndLayout.logo. Ported from tools/sound/asnd.py (Engine). */
#include <stddef.h>

#include "sound/snd_priv.h"
#include "gen/module_sound.h"
#include "host.h"

u8 *snd_m;
const SndLayout *snd_l;

const SndLayout snd_lay_game = {
    .logo = false, .voices = 0x190A, .vsize = 0x1E,
    .vf = { [F_t] = 0, [F_slide] = 1, [F_rstep] = 2, [F_pstep] = 3, [F_pitch] = 4, [F_inst] = 5, [F_vol] = 6, [F_gate] = 7, [F_rel] = 8, [F_rctr] = 9,
            [F_rrel] = 0xA, [F_pctr] = 0xB, [F_prel] = 0xC, [F_pan] = 0xD, [F_start] = 0xE, [F_ptr] = 0x10, [F_ls1] = 0x12, [F_ls2] = 0x14,
            [F_cnt1] = 0x16, [F_cnt2] = 0x18, [F_lptr] = 0x1A, [F_tune] = 0x1C, [F_vofs] = 0x1D },
    .cur = 0x1A02, .f03 = 0x1A03, .u0 = 0x1A04, .u1 = 0x1A05, .c0 = 0x19DC, .c1 = 0x19FC, .m0 = 0x19FE, .m1 = 0x1A00,
    .b0 = 0x1902, .b1 = 0x1904, .d0 = 0x1906, .d1 = 0x1908, .lcg = 0x17E4, .shadow = 0x1A06, .eff = 0x1B06,
    .opch = 0x1896, .opoff = 0x18A8, .fnum = 0x187A, .inst = 0x2E, .trem = 0x1892, .vib = 0x1893, .csm = 0x1894,
    .ev_min = 0xF3, .flag = 0x17D0,
};

const SndLayout snd_lay_logo = {
    .logo = true, .voices = 0x43C, .vsize = 0x1A,
    .vf = { [F_t] = 0, [F_slide] = 1, [F_rstep] = 2, [F_pstep] = -1, [F_pitch] = 3, [F_inst] = 4, [F_vol] = 5, [F_gate] = 6, [F_rel] = 7, [F_rctr] = 8,
            [F_rrel] = 9, [F_pctr] = -1, [F_prel] = -1, [F_pan] = -1, [F_start] = 0xA, [F_ptr] = 0xC, [F_ls1] = 0xE, [F_ls2] = 0x10,
            [F_cnt1] = 0x12, [F_cnt2] = 0x14, [F_lptr] = 0x16, [F_tune] = 0x18, [F_vofs] = 0x19 },
    .cur = 0x512, .f03 = 0x513, .u0 = 0x514, .u1 = 0x515, .c0 = 0x50C, .c1 = 0x50D, .m0 = 0x50E, .m1 = 0x510,
    .b0 = 0x432, .b1 = 0x434, .d0 = 0x436, .d1 = 0x438, .lcg = 0x3CE, .shadow = 0x516, .eff = 0x616,
    .opch = 0x404, .opoff = 0x416, .fnum = 0x3E8, .inst = 0x50, .trem = 0x400, .vib = 0x401, .csm = 0x402,
    .ev_min = 0xF5, .flag = 0,
};

#define L (*snd_l)
#define NVOICES 7

static SndOplSink sink;
void snd_set_opl_sink(SndOplSink s) { sink = s; }

int voice_addr(int i) { return L.voices + L.vsize * i; }
int vfb(int v, VoiceField f) { return mb(v + L.vf[f]); }
void vsb(int v, VoiceField f, int x) { sb_(v + L.vf[f], x); }
int vfw(int v, VoiceField f) { return mw(v + L.vf[f]); }
void vsw(int v, VoiceField f, int x) { sw_(v + L.vf[f], x); }
int snd_sh(int reg) { return mb(L.shadow + (reg & 0xFF)); }

static int cur_voice;                           /* the voice record being ticked (the Python model's self.cur) */

/* 036B opl_write: the shadow byte, then ports 388h / 389h */
void opl_write(int reg, int val)
{
    reg &= 0xFF;
    val &= 0xFF;
    sb_(L.shadow + reg, val);
    if (sink) sink((u8)reg, (u8)val);
    else host_opl_write((u8)reg, (u8)val);
}

/* 030B opl_freq */
void opl_freq(int ch, int bx)
{
    opl_write(0xA0 + ch, bx & 0xFF);
    opl_write(0xB0 + ch, ((bx >> 8) & 0xFF) | 0x20);
}

/* 03C4 lcg_rand: lcg = ror16(lcg + 9248h, 3) */
unsigned lcg_rand(void)
{
    unsigned v = (0x9248u + mw(L.lcg)) & 0xFFFF;
    v = ((v >> 3) | (v << 13)) & 0xFFFF;
    sw_(L.lcg, (int)v);
    return v;
}

/* ---- one voice */
static int cur_ch(void) { return mb(L.cur); }

void voice_keyoff(void)                         /* 0E5E */
{
    int ch = cur_ch();
    opl_write(0xB0 + ch, snd_sh(0xB0 + ch) & 0xDF);
}

void voice_vol(void)                            /* 0C2E */
{
    int ch = cur_ch();
    int reg = 0x40 + mb(L.opoff + mb(L.opch + 2 * ch + 1));
    int t = (vfb(cur_voice, F_vol) + vfb(cur_voice, F_vofs)) & 0x3F;
    opl_write(reg, (0x3F - t) | (snd_sh(reg) & 0xC0));
}

void voice_pitch(void)                          /* 0D1C */
{
    int ch = cur_ch();
    int p = vfb(cur_voice, F_pitch);
    int q = p / 12, r = p % 12;
    unsigned f = (mw(L.fnum + 2 * r) + (unsigned)(int)(s8)vfb(cur_voice, F_tune)) & 0xFFFF;
    opl_write(0xA0 + ch, (int)(f & 0xFF));
    opl_write(0xB0 + ch, (snd_sh(0xB0 + ch) & 0x20) | (int)(((f >> 8) | (unsigned)(q << 2)) & 0xFF));
}

void voice_slide(void)                          /* 0DCE */
{
    int ch = cur_ch();
    unsigned val = (unsigned)(((snd_sh(0xB0 + ch) & 0x1F) << 8) + snd_sh(0xA0 + ch) + (int)(s8)vfb(cur_voice, F_slide)) & 0xFFFF;
    opl_write(0xA0 + ch, (int)(val & 0xFF));
    opl_write(0xB0 + ch, (snd_sh(0xB0 + ch) & 0x20) | (int)(val >> 8));
}

/* the register groups of an instrument half (p = patch address, op = operator offset, ch = channel) */
static void r_bd(void)
{
    int bd = (snd_sh(0xBD) & 0x3F) | (mb(L.trem) ? 0x80 : 0) | (mb(L.vib) ? 0x40 : 0);
    opl_write(0xBD, bd);
    opl_write(0x08, mb(L.csm) ? 0x40 : 0);
}
static void r_level(int op, int p) { opl_write(0x40 + op, (0x3F - (mb(p + 6) & 0x3F)) | (mb(p + 7) << 6)); }
static void r_fb(int ch, int p) { opl_write(0xC0 + ch, (mb(p + 0xA) << 1) | (mb(p + 0xD) ? 0 : 1)); }
static void r_ad(int op, int p) { opl_write(0x60 + op, (mb(p) << 4) | (mb(p + 1) & 0xF)); }
static void r_sr(int op, int p) { opl_write(0x80 + op, (mb(p + 2) << 4) | (mb(p + 3) & 0xF)); }
static void r_mult(int op, int p)
{
    opl_write(0x20 + op, (mb(p + 0xB) ? 0x80 : 0) + (mb(p + 0xC) ? 0x40 : 0) + (mb(p + 4) ? 0x20 : 0) + (mb(p + 5) ? 0x10 : 0) + (mb(p + 9) & 0xF));
}
static void r_wave(int op, int p) { opl_write(0xE0 + op, mb(p + 8) & 3); }

/* 1048 inst_setop (ASOUND.117) / 0BE2 (ASOUND.LOG: another order, no initial level) */
void inst_setop(int op, int p, int ch)
{
    if (L.logo) {
        r_bd(); r_level(op, p); r_fb(ch, p); r_ad(op, p); r_sr(op, p); r_mult(op, p); r_wave(op, p);
    } else {
        opl_write(0x40 + op, 0x3F);
        r_bd(); r_fb(ch, p); r_ad(op, p); r_sr(op, p); r_mult(op, p); r_wave(op, p); r_level(op, p);
    }
}

/* 123E inst_load */
void inst_load(int n)
{
    int ch = cur_ch();
    opl_write(0xB0 + ch, snd_sh(0xB0 + ch) & 0xDF);
    int p = L.inst + 0x2C * (n & 0xFF);
    inst_setop(mb(L.opoff + mb(L.opch + 2 * ch)), p, ch);
    int e = L.eff + 8 * ch;
    sb_(e, mb(p + 0xE));
    sw_(e + 2, mw(p + 0x10));
    sw_(e + 4, mw(p + 0x12));
    sw_(e + 6, mw(p + 0x14));
    inst_setop(mb(L.opoff + mb(L.opch + 2 * ch + 1)), p + 0x16, ch);
}

/* 0E8A voice_note_on */
void voice_note_on(void)
{
    voice_vol();
    int ch = cur_ch();
    int e = L.eff + 8 * ch;
    if (mb(e) == 0) {
        voice_pitch();
        opl_write(0xB0 + ch, snd_sh(0xB0 + ch) | 0x20);
        return;
    }
    if (ch == mb(L.u0)) sb_(L.f03, 0);
    if (ch == mb(L.u1)) sb_(L.f03, 1);
    if (mb(L.f03) == 0) {                                  /* unit 0 */
        sb_(L.f03, 1);
        if (mb(L.c0)) { int o = mb(L.u0) + 0xB0; opl_write(o, snd_sh(o) & 0xDF); }
        sb_(L.u0, ch); sb_(L.c0, mb(e));
        sw_(L.m0, mw(e + 2)); sw_(L.b0, mw(e + 4)); sw_(L.d0, mw(e + 6));
    } else {                                               /* unit 1 */
        sb_(L.f03, 0);
        if (mb(L.c0)) { int o = mb(L.u1) + 0xB0; opl_write(o, snd_sh(o) & 0xDF); }   /* sic: tests unit 0's counter (original quirk) */
        sb_(L.u1, ch); sb_(L.c1, mb(e));
        sw_(L.m1, mw(e + 2)); sw_(L.b1, mw(e + 4)); sw_(L.d1, mw(e + 6));
    }
    units_started();
}

/* ---- the per-voice tick (1360 / 0D1A) */
static void advance(int v, int n) { vsw(v, F_ptr, vfw(v, F_ptr) + n); }

static void event_extra(int v, int p, int c, int a1)
{
    if (L.logo) return;
    if (c == 0xF4) { vsb(v, F_pan, a1); advance(v, 2); sb_(L.flag, 1); }
    else if (c == 0xF3) { vsb(v, F_prel, a1); vsb(v, F_pstep, mb(p + 2)); vsb(v, F_pctr, 1); advance(v, 3); }
}

static void event(int v, int p, int c)           /* 1735 jump table at cs:173A */
{
    if (!L.logo) sb_(L.flag, 0);                           /* 13C8: every event clears the stale flag first */
    int a1 = mb(p + 1);
    switch (c) {
    case 0xFF:                                             /* loop 1 */
        if (vfw(v, F_cnt1) == 0) {
            if (a1 == 0) { advance(v, 2); vsw(v, F_ls1, vfw(v, F_ptr)); vsw(v, F_cnt1, 0); }
            else { vsw(v, F_cnt1, a1); vsw(v, F_ptr, vfw(v, F_ls1)); }
        } else {
            vsw(v, F_cnt1, vfw(v, F_cnt1) - 1);
            if (vfw(v, F_cnt1) == 0) { advance(v, 2); vsw(v, F_ls1, vfw(v, F_ptr)); }
            else vsw(v, F_ptr, vfw(v, F_ls1));
        }
        break;
    case 0xFE:                                             /* loop 2 */
        if (vfw(v, F_cnt2) == 0) {
            if (a1 == 0) {
                advance(v, 2);
                vsw(v, F_ls2, vfw(v, F_ptr)); vsw(v, F_ls1, vfw(v, F_ptr));
                vsw(v, F_cnt1, 0); vsw(v, F_cnt2, 0);
            } else {
                vsw(v, F_cnt2, a1); vsw(v, F_ls1, vfw(v, F_ls2)); vsw(v, F_ptr, vfw(v, F_ls2));
            }
        } else {
            vsw(v, F_cnt2, vfw(v, F_cnt2) - 1);
            vsw(v, F_ls1, vfw(v, F_ls2));
            if (vfw(v, F_cnt2) == 0) { advance(v, 2); vsw(v, F_ls2, vfw(v, F_ptr)); }
            vsw(v, F_ptr, vfw(v, F_ls2));
        }
        break;
    case 0xFD:                                             /* end of track / jump to the loop pointer */
        if (vfw(v, F_lptr) == 0) vsw(v, F_ptr, vfw(v, F_start));
        else {
            int x = vfw(v, F_lptr);
            vsw(v, F_start, x); vsw(v, F_ptr, x); vsw(v, F_ls1, x); vsw(v, F_ls2, x);
        }
        break;
    case 0xFC: vsb(v, F_inst, a1); advance(v, 2); inst_load(a1); break;
    case 0xFB: vsb(v, F_gate, a1); advance(v, 2); break;
    case 0xFA: vsb(v, F_slide, a1); advance(v, 2); break;
    case 0xF9:
        vsb(v, F_vol, a1); advance(v, 2);
        if (L.logo) voice_vol(); else sb_(L.flag, 1);
        break;
    case 0xF8: vsb(v, F_rrel, a1); vsb(v, F_rstep, mb(p + 2)); vsb(v, F_rctr, 1); advance(v, 3); break;
    case 0xF7: vsb(v, F_tune, a1); advance(v, 2); break;
    case 0xF6: {                                           /* random patch: the track is modified in RAM */
        int n = a1;
        int r = (int)(lcg_rand() & (unsigned)(n - 1) & 0xFFFF);
        sb_(p + 2 + n + 1 + mb(p + 2 + n), mb(p + 2 + r));
        advance(v, n + 3);
        break;
    }
    case 0xF5: vsb(v, F_vofs, a1); advance(v, 2); break;
    default: event_extra(v, p, c, a1); break;
    }
}

static void read_events(int v)                   /* 13A5 */
{
    for (;;) {
        int p = vfw(v, F_ptr);
        int c = mb(p);
        if (c >= L.ev_min) { event(v, p, c); continue; }
        if (!L.logo && mb(L.flag)) voice_vol();            /* a note after a volume / pan event writes the level first */
        p = vfw(v, F_ptr);
        vsb(v, F_pitch, mb(p));
        vsb(v, F_t, mb(p + 1));
        advance(v, 2);
        if (vfb(v, F_pitch) == 0 || vfb(v, F_t) == 0) voice_keyoff();
        else {
            vsb(v, F_rel, vfb(v, F_t) - vfb(v, F_gate));
            voice_note_on();
        }
        return;
    }
}

static void tail(int v)                          /* 17D1 (ASOUND.117) / 1113 (ASOUND.LOG) */
{
    if (L.logo) {
        if (vfb(v, F_slide)) voice_slide();
        if (vfb(v, F_rctr)) {
            vsb(v, F_rctr, vfb(v, F_rctr) - 1);
            if (vfb(v, F_rctr) == 0) {
                vsb(v, F_rctr, vfb(v, F_rrel));
                if (vfb(v, F_rstep)) { vsb(v, F_vofs, vfb(v, F_vofs) + vfb(v, F_rstep)); voice_vol(); }
            }
        }
        return;
    }
    if (vfb(v, F_slide)) voice_slide();
    sb_(L.flag, 0);
    if (vfb(v, F_rctr) == 0 && vfb(v, F_pctr) == 0) return;
    vsb(v, F_rctr, vfb(v, F_rctr) - 1);
    if (vfb(v, F_rctr) == 0) {
        vsb(v, F_rctr, vfb(v, F_rrel));
        if (vfb(v, F_rstep)) { vsb(v, F_vofs, vfb(v, F_vofs) + vfb(v, F_rstep)); sb_(L.flag, 1); }
    }
    vsb(v, F_pctr, vfb(v, F_pctr) - 1);
    if (vfb(v, F_pctr) == 0) {
        vsb(v, F_pctr, vfb(v, F_prel));
        if (vfb(v, F_pstep)) { vsb(v, F_pan, vfb(v, F_pan) + vfb(v, F_pstep)); sb_(L.flag, 1); }
    }
    if (mb(L.flag)) voice_vol();
}

void voice_tick(void)
{
    int v = cur_voice;
    if (vfb(v, F_t) == 0) { sb_(L.cur, mb(L.cur) + 1); return; }
    if (vfb(v, F_rel)) {
        vsb(v, F_rel, vfb(v, F_rel) - 1);
        if (vfb(v, F_rel) == 0) voice_keyoff();
    }
    vsb(v, F_t, vfb(v, F_t) - 1);
    if (vfb(v, F_t) == 0) read_events(v);
    tail(v);
    sb_(L.cur, mb(L.cur) + 1);
}

void update_all(void)                            /* 1882 / 116E */
{
    sb_(L.cur, 0);
    for (int i = 0; i < NVOICES; i++) {
        cur_voice = voice_addr(i);
        voice_tick();
    }
}

/* ---- effect units */
void unit_tail(void)                             /* the end of 0430 */
{
    if (mb(L.c0)) {
        sw_(L.b0, mw(L.b0) + mw(L.d0));
        sb_(L.c0, mb(L.c0) - 1);
        if (mb(L.c0) == 0) {
            int al = mb(L.u0);
            if (!(mb(L.c1) && al == mb(L.u1))) { opl_write(0xA0 + al, 0); opl_write(0xB0 + al, 0); }
        }
    }
    if (mb(L.c1)) {
        sw_(L.b1, mw(L.b1) + mw(L.d1));
        sb_(L.c1, mb(L.c1) - 1);
        if (mb(L.c1) == 0) {
            int al = mb(L.u1);
            if (mb(L.c0) || al != mb(L.u0)) { opl_write(0xA0 + al, 0); opl_write(0xB0 + al, 0); }
        }
    }
}

void noise_tick(void)                            /* 0562 */
{
    unsigned v = lcg_rand();
    if (mb(L.c0)) opl_freq(mb(L.u0), (int)((((v ^ 0xFFFF) & mw(L.m0)) + mw(L.b0)) & 0xFFFF));
    if (mb(L.c1)) opl_freq(mb(L.u1), (int)(((v & mw(L.m1)) + mw(L.b1)) & 0xFFFF));
}
