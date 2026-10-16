/* PLAYER.EXE (frontend.md section 3, 8.3): plays the title / credit animations (.PAN files) on the 320 x 200 screen. The files are
 * LZ-decoded chunk by chunk into a ring buffer (all free DOS memory) while the timer tick consumes the cels: the tick callback shows
 * the next cel when the frame countdown (60 / speed ticks) reaches 0. The first file's first palette fades in from black over 5 s.
 * Any key, joystick or mouse button ends it. The timer ISR body runs at 60.08 Hz on the 70.09 Hz retrace (every 7th tick is dropped).
 *
 * The program's variables live in the loaded image's DGROUP (02A3) at the original offsets (fe.h PLAYER_DS_*); linear 20-bit addresses
 * (seg << 4 | off) are kept as u32 where the original keeps them as dword pairs. The ring buffer is a DOS block in mem[]. */
#include <stdio.h>
#include <string.h>

#include "comm.h"
#include "frontend/fe.h"
#include "gen/module_frontend.h"
#include "gen/module_platform.h"
#include "gen/module_video.h"
#include "host.h"
#include "platform/platform.h"
#include "platform/timer.h"
#include "platform/vga.h"
#include "prog.h"
#include "video/video.h"

FeTest fe_test;
static int cur_file;                                         /* the 0-based index of the file being shown (tests) */

#define DSL32(o) DSL(o)                                     /* a dword stored as (low word, high word): the linear addresses */

/* ---- video mode, break handler ---------------------------------------------------------------------------- */

/* PLAYER 0000:0520 / 0540: INT 10h AH=0Fh and AH=00 */
int video_get_mode(void) { return vga_mode(); }
void video_set_mode(int mode) { vga_set_mode((u8)mode); }

/* PLAYER 0000:0583: [2092] = 0, INT 1Bh and INT 23h -> break_isr 0560, which sets [2092] = 3 (platform break_isr, ProgDs) */
void break_hooks_install(void)
{
    DSW(PLAYER_DS_break) = 0;
    break_hook_install();
}

/* PLAYER 0000:05D0: the saved vectors come back */
void break_hooks_restore(void) { break_hook_remove(); }

/* ---- the screen and the fade ------------------------------------------------------------------------------------ */

/* PLAYER 0000:0DB0 screen_off / 0DCA screen_on: wait for the retrace, sequencer register 1 bit 5 (screen off / on): the same code as MGRAPHIC
 * slots 69 / 70 (video/mgraphic.c, gen/module_video.h), which PLAYER, DSWAP and the later programs share in the port. */

/* screen off / on around a cel; tests (player_fast) do not wait for the retrace, which makes every cel take a frame of real time */
static void blank(bool off)
{
    if (!fe_test.player_fast) { if (off) screen_off(); else screen_on(); return; }
    vga_out(0x3C4, 1);
    vga_out(0x3C5, off ? (u8)(vga_in(0x3C5) | 0x20) : (u8)(vga_in(0x3C5) & 0xDF));
}

/* PLAYER 0000:0D0C: DAC `count` colours from `first`; the palette is also kept in the table DS:13DA (the target of the fade). While a
 * fade is pending ([13D4] != 0) the DAC gets zeros. With the BIOS grey-scale flag (0040:0089 bit 1) every colour becomes
 * (R * 4CCDh + G * 970Ah + B * 1C29h + 8000h) >> 16 in all three components. */
void pal_load(int first, int count, const u8 *rgb)
{
    u8 *table = DSP(PLAYER_DS_pal);                         /* PORT: the original stores at DS:13DA + 3 * (the running pointer): first is 0 */
    int pending = DSW(PLAYER_DS_fade_steps) != 0;
    if (rd8(0x40, 0x89) & 2) {
        for (int i = 0; i < count; i++) {
            u32 sum = (u32)rgb[3 * i] * 0x4CCDu + (u32)rgb[3 * i + 1] * 0x970Au + (u32)rgb[3 * i + 2] * 0x1C29u;
            u8 g = (u8)((sum + 0x8000u) >> 16);
            u8 out[3] = { pending ? 0 : g, pending ? 0 : g, pending ? 0 : g };
            table[3 * i] = table[3 * i + 1] = table[3 * i + 2] = g;
            dac_set((first + i) & 0xFF, 1, out);
        }
        return;
    }
    memcpy(table, rgb, 3u * (unsigned)count);
    if (pending) {
        u8 zero[768];
        memset(zero, 0, sizeof zero);
        dac_set(first, count, zero);
    } else {
        dac_set(first, count, rgb);
    }
}

/* PLAYER 0000:0E65: the next [13D0] colours of the faded palette into DS:16DA: (v * factor + 8000h) >> 16 per component. idx = ([13CE] +
 * [13D0]) & FFh; after colour 255 the factor moves by [13D8] (clamped at FFFFh rising / 0 falling). */
void fade_slice(void)
{
    u16 count = DSW(PLAYER_DS_fade_cnt);
    u16 bx = (u16)(DSW(PLAYER_DS_fade_idx) + count) & 0x00FF;
    DSW(PLAYER_DS_fade_idx) = bx;
    const u8 *src = DSP(PLAYER_DS_pal) + 3 * bx;
    u8 *dst = DSP(PLAYER_DS_slice);
    for (u16 i = 0; i < count; i++) {
        u16 f = DSW(PLAYER_DS_fade_factor);
        for (int c = 0; c < 3; c++) {
            u32 prod = (u32)src[c] * f;                      /* mul word [13D6]: dx:ax; shl ax,1; adc dx,0 rounds */
            dst[c] = (u8)((prod + 0x8000u) >> 16);
        }
        dst += 3;
        src += 3;
        bx = (bx + 1) & 0xFF;                                /* inc bl */
        if (bx == 0) {
            src = DSP(PLAYER_DS_pal);
            s16 step = (s16)DSW(PLAYER_DS_fade_step);
            u32 nf = (u32)DSW(PLAYER_DS_fade_factor) + (u16)step;
            if (step >= 0) DSW(PLAYER_DS_fade_factor) = nf > 0xFFFF ? 0xFFFF : (u16)nf;      /* add; jae ok else := FFFFh */
            else DSW(PLAYER_DS_fade_factor) = nf > 0xFFFF ? (u16)nf : 0;                      /* add; jb ok (carry) else := 0 */
        }
    }
}

/* PLAYER 0000:0E0C: steps = |secs| * 60 ticks; [13D0] = colours per tick (the original measures the DAC write speed over one retrace,
 * 0DE4 fade_measure: [13D2] = bytes per retrace, colours = min(256, [13D2] / 20); PORT: no measurement, 256 colours per tick). */
void fade_init(int secs)
{
    if (DSW(PLAYER_DS_fade_meas) == 0) DSW(PLAYER_DS_fade_meas) = 5120;      /* fade_measure: PORT, any value >= 5120 gives 256 */
    int sign = secs < 0 ? -1 : 1;
    unsigned asecs = (unsigned)(secs < 0 ? -secs : secs);
    DSW(PLAYER_DS_fade_steps) = (u16)((asecs & 0xFF) * 60);                  /* mov ah,3Ch; mul ah */
    u16 q = (u16)(DSW(PLAYER_DS_fade_meas) / 20);
    u16 count = (q >> 8) ? 0x100 : q;                                        /* or ah,ah; je: a quotient above FFh gives 100h */
    DSW(PLAYER_DS_fade_cnt) = count;
    DSW(PLAYER_DS_fade_idx) = (u16)(0u - count);
    u32 prod = (u32)count * DSW(PLAYER_DS_fade_steps);                       /* mul word [13D4] -> dx:ax */
    u16 cx = (u16)((((prod >> 16) & 0xFF) << 8) | ((prod >> 8) & 0xFF));     /* mov ch,dl; mov cl,ah */
    cx = (u16)(cx - 1);
    u16 step = (u16)(0x10000u / (cx ? cx : 1));                              /* div cx: dx:ax = 1:0 */
    if (sign < 0) step = (u16)(0u - step);
    DSW(PLAYER_DS_fade_step) = step;
    DSW(PLAYER_DS_fade_factor) = step;
    fade_slice();
    if (fe_test.player_fast) DSW(PLAYER_DS_fade_steps) = 0;                  /* tests: no fade */
}

/* PLAYER 0000:0EE3: one tick of the fade: writes the slice computed last time at DAC index [13CE] and computes the next */
void fade_step(void)
{
    int idx = DSW(PLAYER_DS_fade_idx) & 0xFF;
    int count = DSW(PLAYER_DS_fade_cnt);
    const u8 *s = DSP(PLAYER_DS_slice);
    for (int i = 0; i < count; i++) dac_set((idx + i) & 0xFF, 1, s + 3 * i);
    fade_slice();
    DSW(PLAYER_DS_fade_steps)--;
}

/* ---- the ring buffer ------------------------------------------------------------------------------------------- */

/* PLAYER 0000:05F6: forward byte copy between two linear addresses */
void huge_copy(u32 src, u32 dst, unsigned n)
{
    for (unsigned i = 0; i < (n & 0xFFFF); i++) mem[dst + i] = mem[src + i];       /* n is a 16-bit count; 0 copies nothing */
}

/* PLAYER 0000:0664: the word at a linear address */
unsigned huge_peek_word(u32 linear) { return (unsigned)(mem[linear] | mem[linear + 1] << 8); }

/* PLAYER 0000:03FE: 1 when 0C000h bytes can be decoded at the write position A (frontend.md 8.3.1) */
int ring_make_room(void)
{
    u32 A = DSL32(PLAYER_DS_write), H = DSL32(PLAYER_DS_head), T = DSL32(PLAYER_DS_tail);
    u32 lim = A + 0xC000u;
    if (T > H) return lim < T;                               /* the buffer has wrapped: the tail is ahead of the head */
    if (lim <= DSL32(PLAYER_DS_buf_end)) return 1;
    u32 S = DSL32(PLAYER_DS_buf_start);
    u16 used = (u16)(A - H);                                 /* di = [2082] - [2094], 16 bits */
    u32 keep = 0;
    if (DSW(PLAYER_DS_ncels)) keep = used < 0x2000 ? 0x2000 : used;
    if (S + 0xC000u + keep >= T) return 0;
    huge_copy(A - keep, S, (unsigned)keep);
    DSL32(PLAYER_DS_prev_head) = H;
    A = S + keep;
    DSL32(PLAYER_DS_write) = A;
    DSL32(PLAYER_DS_wrap_head) = A - used;
    DSL32(PLAYER_DS_head) = A - used;
    return 1;
}

/* ---- the PAN bit reader ------------------------------------------------------------------------------------------ */

/* PLAYER 0000:0B84: the read pointer passed the end of the 4 KB buffer: the byte at [si] moves to [si - 1000h], then 1000h bytes are read */
u16 bits_refill(u16 si)
{
    u8 al = DSB(si);
    si = (u16)(si - 0x1000);
    DSB(si) = al;
    dos_read(DSW(PLAYER_DS_handle), 0x1000, DSP(PLAYER_DS_filebuf));            /* errors and short reads are not checked */
    return si;
}

/* PLAYER 0000:0BA2: 1 = opened. PORT: a missing file is reported by returning 0 (the original's carry) instead of the wrapper's fatal error. */
int bits_open(const char *name)
{
    DSW(PLAYER_DS_bitsleft) = 0;
    DSB(PLAYER_DS_eof) = 0;
    DSW(PLAYER_DS_fileptr) = (u16)(PLAYER_DS_filebuf + 0x1000);                  /* the first read refills */
    char *path = host_game_path(name, false);
    if (!path) return 0;
    host_free(path);
    DSW(PLAYER_DS_handle) = (u16)dos_open((char *)name, 0);
    return 1;
}

/* PLAYER 0000:0BCD */
int bits_close(void)
{
    dos_close(DSW(PLAYER_DS_handle));
    return 1;
}

/* PLAYER 0000:0BDC: set by pan_decode_chunk: 1 when the chunk ended with the control byte 0 (the end of the stream) */
int bits_eof(void) { return DSB(PLAYER_DS_eof); }

/* PLAYER 0000:0BE2 (tools/lzs.py): decodes one chunk of the LZ stream to the linear address `dst` and returns the number of bytes. The
 * stream is a sequence of 16-bit control words (bits LSB first; the next word is fetched while the 16th bit is taken) and bytes from the
 * file buffer: 1 = literal byte; 00 + 2 bits + byte = short match (2..5 bytes, offset 1..256); 01 + word = long match (offset up to 2000h,
 * count 3..9, or a count byte >= 2: count + 1); a count byte 0 or 1 ends the chunk (0: the end of the stream). Matches read the bytes
 * before `dst` (the ring's history). */
/* the file buffer reads of pan_decode_chunk: `cmp si,11B7h; jb` before a word, `jbe` before a byte */
static u16 fetch_word(u16 *si)
{
    if (*si >= PLAYER_DS_fileend) *si = bits_refill(*si);
    u16 w = (u16)(DSB(*si) | DSB(*si + 1) << 8);
    *si = (u16)(*si + 2);
    return w;
}

static u8 fetch_byte(u16 *si)
{
    if (*si > PLAYER_DS_fileend) *si = bits_refill(*si);
    return DSB((*si)++);
}

int pan_decode_chunk(u32 dst)
{
    u16 si = DSW(PLAYER_DS_fileptr);
    u16 bp = DSW(PLAYER_DS_bits);
    u16 dx = DSW(PLAYER_DS_bitsleft);
    u32 di = dst;
    if (dx == 0) {                                           /* no control word yet */
        bp = fetch_word(&si);
        dx = 0x10;
    }
    for (;;) {
        /* one control bit: the 16th bit of a word is taken from the old word while the next one is loaded */
        int bit;
#define GETBIT() do { dx--; if (dx != 0) { bit = bp & 1; bp >>= 1; } else { u16 nw = fetch_word(&si); dx = 0x10; bit = bp & 1; bp = nw; } } while (0)
        GETBIT();
        if (bit) {                                           /* literal */
            mem[di++] = fetch_byte(&si);
            continue;
        }
        unsigned cx;
        u16 bx;
        GETBIT();
        if (bit) {                                           /* long match: offset 2000h - (hi >> 3 : lo), count (hi & 7) + 2 or a count byte */
            u16 w = fetch_word(&si);
            u8 bh = (u8)(((w >> 8) >> 3) | 0xE0);
            bx = (u16)((bh << 8) | (w & 0xFF));
            u8 ah = (u8)((w >> 8) & 7);
            if (ah == 0) {
                u8 al = fetch_byte(&si);
                if (al == 0 || al == 1) {                    /* end of the chunk (0: the end of the stream) */
                    DSW(PLAYER_DS_fileptr) = si;
                    DSW(PLAYER_DS_bitsleft) = dx;
                    DSW(PLAYER_DS_bits) = bp;
                    DSB(PLAYER_DS_eof) = (u8)(al ^ 1);
                    return (int)(di - dst);
                }
                cx = (unsigned)al + 1;
            } else {
                cx = (unsigned)ah + 2;
            }
        } else {                                             /* short match: two count bits, a byte for the offset */
            GETBIT(); cx = (unsigned)bit << 1;
            GETBIT(); cx |= (unsigned)bit;
            cx += 2;
            bx = (u16)(0xFF00 | fetch_byte(&si));
        }
        for (; cx; cx--, di++) mem[di] = mem[di + (s16)bx];  /* es:[bx + di] with a negative bx */
    }
#undef GETBIT
}

/* ---- the cels ------------------------------------------------------------------------------------------------------ */

/* PLAYER 0000:1006: one cel's delta RLE onto the screen (grammar RE_GUIDE 4); returns the linear address after the terminator */
u32 cel_rle_draw(u32 src, u16 dseg, u16 doff) { return pan_rle_draw(mem, src, MEM_SIZE, seg_ptr(dseg) + doff); }

/* PLAYER 0000:07EA: shows the cel at the linear address p and returns the address after it. A 'Z' header (the first cel of a file:
 * speed, cel count, size table, 768 palette bytes) sets the frame time and loads the palette with the screen blanked. */
u32 cel_show(u32 p)
{
    if (mem[p] == 0x5A) {
        DSW(PLAYER_DS_cel_in_file) = 0;
        cur_file++;
        u8 speed = mem[p + 1];
        DSW(PLAYER_DS_frame_ticks) = (u16)(60 / (speed ? speed : 1));        /* idiv by the speed byte (0 is a divide error in the original) */
        if (fe_test.player_fast) DSW(PLAYER_DS_frame_ticks) = 1;
        u16 n = (u16)(mem[p + 2] | mem[p + 3] << 8);
        p += (u32)((u16)(n + 0x182) << 1);                  /* past the header: 4 + 2 * n + 768 bytes */
        blank(true);
        pal_load(0, 256, mem + p - 0x300);
    }
    if (mem[p] != 0x42 || mem[p + 1] != 0 || mem[p + 2] != 1 || mem[p + 3] != 0) {
        fprintf(stderr, "Assertion failed: cel[0] == 'B', file player.c, line 477\n");     /* the four asserts of 0860..08C0 */
        exit_fn(0xFF);
    }
    u32 end = cel_rle_draw(p + 4, 0xA000, 0);
    blank(false);
    if (fe_test.player_cel) fe_test.player_cel(cur_file, DSW(PLAYER_DS_cel_in_file));
    return end;
}

/* PLAYER 0000:0738: called from the timer ISR once per tick (not re-entrant: [1E7A]) */
void tick_callback(void)
{
    if (DSW(PLAYER_DS_hold)) return;
    DSW(PLAYER_DS_hold) = 1;
    DSW(PLAYER_DS_tick_counter)++;
    fe_test.player_callbacks++;
    if (DSW(PLAYER_DS_countdown) && DSW(PLAYER_DS_fade_steps)) {
        fade_step();
    } else if (DSW(PLAYER_DS_countdown) == 0 || --DSW(PLAYER_DS_countdown) == 0) {
        u32 T = DSL32(PLAYER_DS_tail), H = DSL32(PLAYER_DS_head);
        if (T == H) {
            DSW(PLAYER_DS_idle)++;
        } else {
            T = cel_show(T);
            DSL32(PLAYER_DS_tail) = T;
            if (T > H && !(DSL32(PLAYER_DS_prev_head) > T)) DSL32(PLAYER_DS_tail) = DSL32(PLAYER_DS_wrap_head);       /* the data wrapped */
            DSW(PLAYER_DS_cel_in_file)++;
            DSW(PLAYER_DS_cels_shown)++;
            DSW(PLAYER_DS_countdown) = DSW(PLAYER_DS_frame_ticks);
        }
    }
    DSW(PLAYER_DS_hold) = 0;
}

/* PLAYER 026B:0336 timer_step, the body of the ISR, once per retrace tick (70.09 Hz): with the rate class 4 the 7th tick is dropped
 * (no callback, no sound poll), so the animation clock runs at 6/7 * 70.09 = 60.08 Hz. */
void timer_step(void)
{
    fe_test.player_isr_ticks++;
    if (DSW(PLAYER_DS_class) == 4) {
        s8 v = (s8)(DSB(PLAYER_DS_skip) - 1);
        DSB(PLAYER_DS_skip) = (u8)v;
        if (v <= 0) { DSB(PLAYER_DS_skip) = 7; fe_snd_poll_skipped(); return; }
    }
    tick_callback();
    fe_snd_poll();                                          /* slot 103: its result steers the fast / idle state of the sound step */
}

/* PLAYER 0000:0F1A: ((inp(201h) >> (n + 4)) & 1) ^ 1 */
int joy_button(int n) { return ((joy_port_201() >> (n + 4)) & 1) ^ 1; }

/* PLAYER 0000:0F47: INT 33h AX=3: the left / right buttons into [19DA] / [19DB] */
void mouse_poll_buttons(void)
{
    int x, y;
    u8 b;
    host_mouse_read(&x, &y, &b);
    DSB(0x19DA) = (b & 1) ? 1 : 0;
    DSB(0x19DB) = (b & 2) ? 1 : 0;
}

/* PLAYER 0000:01F6: sound off, timer and break handlers restored, the video mode back unless it was 13h, the buffer freed, exit */
void player_exit(int code)
{
    fe_snd_cmd(0, 0);
    timer_remove();
    DSB(PLAYER_DS_timer_on) = 0;
    break_hooks_restore();
    if (DSW(PLAYER_DS_old_mode) != 0x13) video_set_mode(DSW(PLAYER_DS_old_mode));
    dos_freemem_chk(DSW(PLAYER_DS_mseg));
    exit_fn(code);
}

/* PLAYER 0000:0696: the input poll of every wait loop: the first call starts the sound and arms the tick callback; any key, joystick button or
 * mouse button (or Ctrl-Break) ends the program. */
void poll_input(void)
{
    host_pump();
    if (DSW(PLAYER_DS_break)) player_exit(DSW(PLAYER_DS_break));
    if (DSW(PLAYER_DS_hold)) fe_snd_cmd(3, 0);              /* the intro soundtrack (frontend.md 3.1); S5.9 */
    Comm *comm = comm_get();
    int buttons = 0;
    if (comm->input_devices & 1) buttons = joy_button(1) | joy_button(0);
    if (comm->input_devices & 2) {
        mouse_poll_buttons();
        buttons |= (DSB(0x19DB) | DSB(0x19DA));
    }
    int key = dos_kbhit();
    if (key || buttons) {
        DSW(PLAYER_DS_hold) = 1;
        player_exit(key ? bios_keyboard(0) : 0x1B);
    }
    DSW(PLAYER_DS_hold) = 0;
    if (fe_test.player_fast)                                 /* tests: run the animation faster than real time */
        for (int i = 0; i < 6; i++) tick_callback();
}

/* PLAYER 0000:005C: all free DOS memory (>= 2000h paragraphs) is the ring buffer; the screen goes off, the fade is set up, the timer and the
 * break handlers installed, and the video mode set to 13h unless it already is. */
void player_init(void)
{
    ds_far_wr(PLAYER_DS_comm, far_make(rd16(0, COMM_SEG_PTR), 0));
    u16 size = 0, mseg = 0;
    if (dos_allocmem_paras(0xFFFF, &size) != 8) { fprintf(stderr, "Assertion failed: _dos_allocmem ( -1, &size) == 8, file player.c\n"); exit_fn(0xFF); }
    if (fe_test.player_ring_paras && fe_test.player_ring_paras < size) size = (u16)fe_test.player_ring_paras;       /* tests: a small ring */
    if (dos_allocmem_paras(size, &mseg) != 0) { fprintf(stderr, "Assertion failed: _dos_allocmem (size, &mseg) == 0, file player.c\n"); exit_fn(0xFF); }
    if (size < 0x2000) { fprintf(stderr, "Assertion failed: size >= 0x2000, file player.c\n"); exit_fn(0xFF); }
    DSW(PLAYER_DS_mseg) = mseg;
    cur_file = -1;
    DSL32(PLAYER_DS_sizes) = (u32)mseg << 4;                 /* the size table: 80h paragraphs at the front */
    memset(mem + ((u32)mseg << 4), 0, 0x800);                /* PORT: DOS memory is not cleared; the table beyond the cel count is read as 0 */
    u32 S = ((u32)mseg + 0x80) << 4;
    DSL32(PLAYER_DS_buf_start) = S;
    DSL32(PLAYER_DS_buf_end) = ((u32)size + mseg) << 4;
    DSL32(PLAYER_DS_write) = DSL32(PLAYER_DS_prev_head) = DSL32(PLAYER_DS_wrap_head) = DSL32(PLAYER_DS_tail) = DSL32(PLAYER_DS_head) = S;
    screen_off();
    fade_init(5);
    DSW(PLAYER_DS_hold) = 1;
    DSW(PLAYER_DS_class) = 4;                                /* PORT: timer_calibrate would measure the PIT per retrace; the 70 Hz VGA mode is class 4 */
    DSW(PLAYER_DS_div0) = 1;
    DSB(PLAYER_DS_timer_on) = 1;
    timer_set_hook(timer_step);                              /* 026B:000A timer_install */
    timer_install();
    break_hooks_install();
    DSW(PLAYER_DS_old_mode) = (u16)video_get_mode();
    if (DSW(PLAYER_DS_old_mode) != 0x13) video_set_mode(0x13);
}

/* PLAYER 0000:0244: plays one file: "name" with its suffix replaced by .PAN, decoded chunk by chunk into the ring buffer. The ring
 * is advanced while the tick callback consumes it; poll_input runs whenever there is no room. */
void pan_play_file(const char *name)
{
    char path[64];
    snprintf(path, sizeof path, "%s.", name);               /* strcat(name, ".") then cut at the first '.' and append .PAN */
    *strchr(path, '.') = 0;
    strcat(path, ".PAN");
    if (!bits_open(path)) return;
    DSW(PLAYER_DS_ncels) = 0;
    u32 sizes = 0;                                           /* the cel size table pointer (bp-4:bp-2) */
    do {
        while (!ring_make_room()) poll_input();
        int n = pan_decode_chunk(DSL32(PLAYER_DS_write));
        if (DSW(PLAYER_DS_ncels) == 0) {
            u32 A = DSL32(PLAYER_DS_write);
            DSW(PLAYER_DS_ncels) = (u16)huge_peek_word(A + 2);
            if ((s16)DSW(PLAYER_DS_ncels) > 0x400) {
                fprintf(stderr, "Assertion failed: cels <= 1024, file player.c, line 240\n");
                exit_fn(0xFF);
            }
            huge_copy(A + 4, DSL32(PLAYER_DS_sizes), (unsigned)(DSW(PLAYER_DS_ncels) * 2));
            sizes = DSL32(PLAYER_DS_sizes);
        }
        DSL32(PLAYER_DS_write) += (u32)n;
        u32 A = DSL32(PLAYER_DS_write);
        while (DSL32(PLAYER_DS_head) + huge_peek_word(sizes) <= A) {      /* every cel that is complete now */
            DSL32(PLAYER_DS_head) += huge_peek_word(sizes);
            sizes += 2;
        }
    } while (!bits_eof());
    bits_close();
}

/* PLAYER 0000:019A: waits until every cel has been shown, then (several files: the intro) until the sound is done, and exits with 0 */
void player_run(int argc)
{
    while (DSL32(PLAYER_DS_tail) != DSL32(PLAYER_DS_head)) poll_input();
    if (argc > 2)
        while (fe_snd_cmd(2, 0) != 0) poll_input();          /* slot 101 cmd 2: the voice-active query (frontend.md 3 item 5) */
    player_exit(0);
}

/* PLAYER 0000:0010 player_main(argc, argv): argv[1..] = file names without extension. The chain's entry takes the DOS command tail. */
int player_main(const char *tail)
{
    char buf[128];
    snprintf(buf, sizeof buf, "%s", tail);
    char *argv[16] = { "PLAYER", NULL };
    int argc = 1;
    for (char *tok = strtok(buf, " \t"); tok && argc < 15; tok = strtok(NULL, " \t")) argv[argc++] = tok;
    if (argc < 2) {
        fputs("usage: player pan_file(s)\n", stderr);
        exit_fn(99);
    }
    player_init();
    for (int i = 1; i < argc; i++) pan_play_file(argv[i]);
    player_run(argc);
    return 0;
}
