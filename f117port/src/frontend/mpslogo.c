/* MPS_LOGO.EXE ("LABS.EXE Version 1.32", frontend.md section 2, 8.4): the MicroProse logo. LABSLOGO.SS is a "MADSS1" sprite series
 * of 82 frames that is drawn one after the other on the 320 x 200 screen, paced by the BIOS clock (18.2 Hz, `delay` ticks per
 * frame); a key press completes the picture without waiting. The jingle is played by the sound driver's .LOG overlay (S5.9; the call
 * points are in sndstub.c). Returns 0 no key, 1 Esc, 2 Alt-Q, 3 any other key. Command line: " X" with X the driver letter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* The series in memory: the original keeps a DOS block (header 22h bytes, 14 bytes per frame, then the sprite data) and far pointers
 * to the frames; the port keeps the sprite data in a malloc'd block and the frame table in a struct. */
typedef struct SsFrame {
    u8  *data;                            /* record +0: the RLE stream of the frame */
    u16  delay, x, y, w1, h1;             /* +4 delay in BIOS ticks (18.2 Hz), +6 x, +8 y, +A width - 1, +C height - 1 */
} SsFrame;

typedef struct SsSeries {
    char name[9];
    u8   scaled;                          /* header +9: must be 0 ("SPRITES.C ser->scaled==no") */
    u8   mode;                            /* header +0Ah: 1..5, selects the first DAC slot (table DS:004A) */
    int  nframes;                         /* +10h */
    int  ncols;                           /* +12h */
    u8   base;                            /* +13h: first DAC slot of the series */
    u8  *block;                           /* the sprite data (all frames) */
    SsFrame *frame;                       /* nframes entries, frame k at frame[k - 1] */
} SsSeries;

typedef struct SsScreen { u16 h, w, off, seg; } SsScreen;       /* {200, 320, 0, A000h}: height, pitch, offset, segment */

/* MPS_LOGO 0006:000C: prints the banner and the message, exit(-1). The screen is still in text mode when this runs. */
void logo_fatal(const char *msg)
{
    char line[160];
    snprintf(line, sizeof line, "LABS.EXE Version %s \r\nERROR: %s\r\n", "1.32", msg);
    fputs(line, stderr);
    for (const char *p = line; *p; p++) tm_tty((u8)*p);
    exit_fn(0xFF);
}

/* MPS_LOGO 0006:0044: fills B800:0000, 7D00h words, with 0 (harmless in the port) */
void text_clear_b800(void) { memset(tm_buffer(), 0, 64000); }

/* MPS_LOGO 0006:033A pal_set / 038E pal_get: the DAC through ports 3C8/3C9 and 3C7/3C9, 256 entries of 6-bit RGB, no retrace wait.
 * (static: the name pal_set belongs to the shell's library in platform.md, which needs the START/END tables.) */
static void logo_pal_set(const u8 *rgb768) { dac_set(0, 256, rgb768); }
static void logo_pal_get(u8 *rgb768) { dac_get(0, 256, rgb768); }

/* MPS_LOGO 0006:03E4: INT 10h AH=0 */
void set_video_mode(int mode) { vga_set_mode((u8)mode); }

/* MPS_LOGO 0045:0062: the BIOS tick count. Quirk kept: the routine assembles the dword from the four bytes of 0040:006C with 8-bit shifts
 * by 8 (which give 0), so the result is the low WORD of the tick count sign-extended by `cdq` (it jumps at 8000h ticks = 30 minutes). */
u32 bios_ticks(void)
{
    timer_poll_bios_clock();
    return (u32)(s32)(s16)rd16(0x40, 0x6C);
}

/* MPS_LOGO 006D:0002 ss_remap_rows_a (the shape the loader uses for mode 4): adds base - 1 to every pixel of a frame's RLE stream.
 * Row markers: FF ends the data, FE an empty row; F9 starts a row of (count, value) pairs, anything else a row of literals
 * (FD n v = run, FC = transparent). FCh is never changed. */
void ss_remap_rows_a(u8 *p, int base)
{
    const u8 add = (u8)(base - 1);
    for (;;) {
        u8 al = *p++;
        if (al == 0xFF) return;
        if (al == 0xFE) continue;
        if (al == 0xF9) {                                  /* pair mode */
            for (;;) {
                al = *p++;
                if (al == 0xFF) return;
                if (al == 0xFE) break;
                if (*p != 0xFC) *p = (u8)(*p + add);       /* the value byte */
                p++;
            }
        } else {                                           /* literal mode: the marker byte itself is consumed */
            for (;;) {
                al = *p;
                if (al == 0xFF) return;
                if (al == 0xFE) { p++; break; }
                if (al == 0xFD) { p += 2; al = *p; }       /* FD n v: the run value is the third byte */
                if (al != 0xFC) *p = (u8)(al + add);
                p++;
            }
        }
    }
}

/* MPS_LOGO 006D:0084 ss_remap_rows_b (mode byte 1): the same walk, the new value comes from a table at DS:0670 (the palette
 * translation of the 4-colour series). LABSLOGO is mode 4, so the shipped data never takes this path. */
void ss_remap_rows_b(u8 *p, int base)
{
    (void)p; (void)base;                                   /* TODO(verify): no shipped series has mode byte 1 */
}

/* MPS_LOGO 006D:0112 ss_load: "name.SS", rb. `mode` 0 = the first DAC slot comes from the table DS:004A[M], 2 = 0F8h - ncols. The
 * series' palette is merged into pal768 (the other colours keep their value). NULL on an error; the error code is kept in DS:0C74
 * (1 open, 2 signature, 3 memory, 5 name differs (not fatal), 7 bad header). */
void *ss_load(const char *name, int mode, u8 *pal768)
{
    DSW(LOGO_DS_ss_error) = 0;
    char path[96];
    snprintf(path, sizeof path, "%s%s", name, strchr(name, '.') ? "" : ".SS");      /* strpos(name, ".") == 0 -> strcat ".SS" */
    char *full = host_game_path(path, false);
    FILE *f = full ? fopen(full, "rb") : NULL;
    if (full) host_free(full);
    if (!f) { DSW(LOGO_DS_ss_error) = 1; return NULL; }

    u8 hdr[0x1F];
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr || memcmp(hdr, "MADSS1", 6) != 0) {
        fclose(f);
        DSW(LOGO_DS_ss_error) = 2;
        return NULL;
    }
    if (strncmp((const char *)hdr + 7, name, 9) != 0) DSW(LOGO_DS_ss_error) = 5;    /* the name in the file differs: not fatal */
    u32 data_len = (u32)(hdr[0x16] | hdr[0x17] << 8 | hdr[0x18] << 16 | (u32)hdr[0x19] << 24);
    SsSeries *s = calloc(1, sizeof *s);
    if (!s) { fclose(f); DSW(LOGO_DS_ss_error) = 3; return NULL; }
    memcpy(s->name, hdr + 7, 8);
    s->scaled = 0;                                         /* header +9 := 0 */
    s->mode = hdr[0x1E];
    s->nframes = hdr[0x14] | hdr[0x15] << 8;
    if (s->mode < 1 || s->mode > 5) {                      /* error 7: bad header */
        fclose(f);
        free(s);
        DSW(LOGO_DS_ss_error) = 7;
        return NULL;
    }
    s->frame = calloc((size_t)(s->nframes > 0 ? s->nframes : 1), sizeof(SsFrame));
    s->block = malloc(data_len ? data_len : 1);
    if (!s->frame || !s->block) { fclose(f); free(s->frame); free(s->block); free(s); DSW(LOGO_DS_ss_error) = 3; return NULL; }
    u32 *off = calloc((size_t)(s->nframes > 0 ? s->nframes : 1), sizeof(u32));
    for (int k = 0; k < s->nframes; k++) {                 /* 14 bytes per frame: data offset, delay, x, y, w - 1, h - 1 */
        u8 r[14];
        if (fread(r, 1, 14, f) != 14) memset(r, 0, sizeof r);
        off[k] = (u32)(r[0] | r[1] << 8 | r[2] << 16 | (u32)r[3] << 24);
        s->frame[k].delay = (u16)(r[4] | r[5] << 8);
        s->frame[k].x = (u16)(r[6] | r[7] << 8);
        s->frame[k].y = (u16)(r[8] | r[9] << 8);
        s->frame[k].w1 = (u16)(r[10] | r[11] << 8);
        s->frame[k].h1 = (u16)(r[12] | r[13] << 8);
    }
    u8 ncols = 0;
    if (fread(&ncols, 1, 1, f) != 1) ncols = 0;
    s->ncols = ncols;
    u8 pal[4 * 256];
    if (ncols && fread(pal, 1, 4u * ncols, f) != 4u * ncols) memset(pal, 0, sizeof pal);
    size_t got = fread(s->block, 1, data_len, f);
    if (got < data_len) memset(s->block + got, 0xFF, data_len - got);
    fclose(f);
    for (int k = 0; k < s->nframes; k++) s->frame[k].data = s->block + (off[k] < data_len ? off[k] : 0);
    free(off);

    if (s->mode == 1) {                                    /* mode byte 1: the remap table walk, no palette merge */
        for (int k = 0; k < s->nframes; k++) ss_remap_rows_b(s->frame[k].data, 0);
        return s;
    }
    s->base = mode == 2 ? (u8)(0xF8 - s->ncols) : DSB(LOGO_DS_base_table + s->mode);
    for (int k = 1; k <= s->ncols; k++)                    /* entry k (r, g, b; the 4th byte is ignored) -> DAC slot base + k - 1 */
        memcpy(pal768 + 3 * (s->base + k - 1), pal + 4 * (k - 1), 3);
    for (int k = 0; k < s->nframes; k++) ss_remap_rows_a(s->frame[k].data, s->base);
    return s;
}

/* MPS_LOGO 006D:05A2: frees the series and clears the pointer */
void ss_free(void **p)
{
    SsSeries *s = *p;
    if (s) { free(s->frame); free(s->block); free(s); }
    *p = NULL;
}

/* The run writer of ss_draw_rle (MPS_LOGO 006D:0678): count in AL, value in AH; FCh skips. A count of 0 writes 256 pixels (dec al; jne). */
static void run_write(u8 *scr, u16 *di, u8 count, u8 value)
{
    unsigned n = count ? count : 256;
    if (value == 0xFC) { *di = (u16)(*di + (count)); return; }          /* "mov ah,0; add di,ax": a skip of 0 skips 0 */
    while (n--) scr[(*di)++ & 0xFFFF] = value;
}

/* MPS_LOGO 006D:05BE ss_draw(ss, frame, x, y, scr); x or y = 3E7h: the frame's own position. The RLE walk is 006D:0678 ss_draw_rle. */
void ss_draw(void *series, int frame, int x, int y, const void *screen)
{
    const SsSeries *s = series;
    const SsScreen *scr = screen;
    const SsFrame *fr = &s->frame[frame - 1];
    if (x == 0x3E7 || y == 0x3E7) { x = fr->x; y = fr->y; }
    /* the assertion "SPRITES.C ser->scaled==no" (line 190h) */
    if (s->scaled) { fprintf(stderr, "Assertion failed: ser->scaled==no, file SPRITES.C, line 400\n"); exit_fn(0xFF); }
    u8 *dst = seg_ptr(scr->seg);
    u16 di = (u16)(scr->off + (u16)(scr->w * y) + x);
    u16 row_start = di;
    const u8 *p = fr->data;
    for (;;) {
        u8 al = *p++;
        if (al == 0xFE) { row_start = (u16)(row_start + scr->w); di = row_start; continue; }
        if (al == 0xFF) return;
        if (al == 0xFA) {                                  /* literal mode */
            for (;;) {
                al = *p++;
                if (al == 0xFE) break;
                if (al == 0xFD) { u8 n = p[0], v = p[1]; p += 2; run_write(dst, &di, n, v); continue; }
                if (al != 0xFC) dst[di & 0xFFFF] = al;
                di++;
            }
        } else {                                           /* pair mode: the marker byte is discarded */
            for (;;) {
                al = *p++;
                if (al == 0xFE) break;
                u8 v = *p++;
                run_write(dst, &di, al, v);
            }
        }
        row_start = (u16)(row_start + scr->w);
        di = row_start;
    }
}

/* MPS_LOGO 0006:0072 logo_main(argc, argv). Returns 0 no key, 1 Esc, 2 Alt-Q, 3 any other key. */
int logo_main(int argc, char **argv)
{
    DSW(LOGO_DS_fast) = 1;
    int result = 0;
    SsScreen scr = { 200, 320, 0, 0xA000 };
    if (argc < 2) logo_fatal("01 Specify sound driver (R/A/T/I/N)");
    char drv = argv[1][0];
    if (drv >= 'a' && drv <= 'z') drv = (char)(drv - 0x20);
    int ovl = 0;                                           /* the segment of the loaded .LOG overlay: always "loaded" in the port */
    switch (drv) {
    case 'R': case 'A': case 'I': case 'T': ovl = 1; break;   /* rsound.log / asound.log / isound.log / tsound.log: S5.9 plays them */
    case 'N': break;
    default: logo_fatal("02 Specify sound driver (R/A/T/I/N)");
    }
    /* PORT: the .LOG overlay is not loaded (no overlay loader); "03 Bad ovrload" cannot happen. TSOUND.LOG is not shipped, T would die with 03. */
    if (drv != 'N') {
        (void)ovl;
        while (snd_init_log(0, 0) != 0) host_pump();         /* ovl_bind_log, then snd_init_log until it returns 0 */
    }
    text_clear_b800();
    set_video_mode(0x13);
    u8 pal[768];
    logo_pal_get(pal);
    SsSeries *ss = ss_load("labslogo", 0, pal);
    if (!ss) logo_fatal("04 bad series load");
    logo_pal_set(pal);
    if (drv != 'N') snd_cmd_log(2);                         /* the jingle */
    u32 t = bios_ticks();
    for (int i = 1; i <= ss->nframes; i++) {
        t += ss->frame[i - 1].delay;
        while (DSW(LOGO_DS_fast) && !fe_test.logo_nowait && (s32)bios_ticks() < (s32)t) {
            host_pump();
            if (host_quit_requested()) {                    /* PORT: a closed window is Alt-Q (the original would die to Ctrl-Break) */
                result = 0x1000;
                DSW(LOGO_DS_fast) = 0;
            }
        }
        ss_draw(ss, i, 0x3E7, 0, &scr);
        if (fe_test.logo_frame) fe_test.logo_frame(i);
        if (bios_keyboard(1)) {
            result = bios_keyboard(0);
            DSW(LOGO_DS_fast) = 0;                          /* the rest is drawn without waiting */
        }
        if (host_quit_requested()) { result = 0x1000; DSW(LOGO_DS_fast) = 0; }
    }
    void *sp = ss;
    ss_free(&sp);
    if (drv != 'N') {
        snd_cmd_log(0);
        snd_shutdown_log();
    }
    if (result == 0) return 0;
    if (result == 0x011B) return 1;
    if (result == 0x1000) return 2;
    return 3;
}

/* The chain's entry: the DOS command tail " X" becomes argv[1] = "X" (argv[0] is the program name). */
int mps_logo_main(const char *tail)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%s", tail);
    char *argv[8] = { "MPS_LOGO", NULL };
    int argc = 1;
    for (char *tok = strtok(buf, " \t"); tok && argc < 7; tok = strtok(NULL, " \t")) argv[argc++] = tok;
    return logo_main(argc, argv);
}
