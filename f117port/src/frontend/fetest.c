/* f117 --frontend-test REFDIR: the front end against references (port/notes/s53/frontend_ref.py writes REFDIR; ctest `frontend`).
 *
 *  1. SETUP: the text screen (80 x 25 chars + attributes) at the first and the second question and at exit, and the comm block, equal what the
 *     ORIGINAL SETUP.EXE leaves under Unicorn (setup1/2/3.bin, setup_comm.bin).
 *  2. MPS_LOGO: after each of the 82 frames the CRC of the screen and of the DAC equal the original's; a key completes the picture; the
 *     frame pacing follows the BIOS clock (the delays of LABSLOGO.SS at 18.2065 Hz).
 *  3. PLAYER: every .PAN of the game folder streams through the ring buffer (a small ring: it wraps) and every cel's screen CRC equals the
 *     Phase 2 decoder's; the palette ends equal; two files play in order; the fade follows the formula of frontend.md 8.3.2; the animation
 *     tick runs at 60.08 Hz (6 of 7 retrace ticks) and the first frame fades in over 5 s.
 *  4. DSWAP, MISC and the text-mode model: small checks.
 * 77 (ctest SKIP) when the game folder has no SETUP.EXE. */
#include <stdio.h>
#include <stdlib.h>
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

static int checks, fails;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; if (fails <= 40) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static u32 crc32(const u8 *p, size_t n)
{
    static u32 table[256];
    if (!table[1]) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static u8 *slurp(const char *dir, const char *name, size_t *len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (len) *len = (size_t)n;
    return b;
}

static u16 rd16le(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 rd32le(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

/* ------------------------------------------------------------------ 1. SETUP */

static u8 snap[2][4000];

static int setup_getche(int n)
{
    if (n == 1) { memcpy(snap[0], tm_buffer(), 4000); return 'N'; }
    if (n == 2) { memcpy(snap[1], tm_buffer(), 4000); return '2'; }
    return -1;
}

static void text_row(const u8 *buf, int row, char *out)
{
    for (int c = 0; c < 80; c++) out[c] = (char)((buf[2 * (row * 80 + c)] >= 0x20 && buf[2 * (row * 80 + c)] < 0x7F) ? buf[2 * (row * 80 + c)] : '#');
    out[80] = 0;
}

static void compare_screen(const char *what, const u8 *got, const u8 *ref)
{
    int diff = 0;
    for (int i = 0; i < 4000; i++) if (got[i] != ref[i]) diff++;
    CHECK(diff == 0, "%s: %d of 4000 text bytes differ from the original", what, diff);
    if (diff) {
        for (int r = 0; r < 25; r++)
            if (memcmp(got + 160 * r, ref + 160 * r, 160)) {
                char a[81], b[81];
                text_row(got, r, a);
                text_row(ref, r, b);
                printf("  row %2d port: |%s|\n         orig: |%s|\n", r, a, b);
                break;
            }
    }
}

static void test_setup(const char *dir)
{
    size_t n1, n2, n3, nc;
    u8 *r1 = slurp(dir, "setup1.bin", &n1), *r2 = slurp(dir, "setup2.bin", &n2), *r3 = slurp(dir, "setup3.bin", &n3), *rc = slurp(dir, "setup_comm.bin", &nc);
    CHECK(r1 && r2 && r3 && rc && n1 == 4000 && n2 == 4000 && n3 == 4000 && nc == 0x80, "SETUP references missing in %s", dir);
    if (!(r1 && r2 && r3 && rc)) return;
    prog_comm_init();
    fe_test.getche_n = 0;
    fe_test.getche = setup_getche;
    int code = platform_exec(PROG_SETUP, setup_main, "");
    fe_test.getche = NULL;
    CHECK(code == 0x24, "SETUP exit code %02Xh, expected 24h", code);
    CHECK(fe_test.getche_n == 2, "SETUP asked %d keys, expected 2", fe_test.getche_n);
    compare_screen("SETUP, joystick question", snap[0], r1);
    compare_screen("SETUP, sound menu", snap[1], r2);
    compare_screen("SETUP, at exit", tm_buffer(), r3);
    const u8 *comm = (const u8 *)comm_get();
    int diff = 0;
    for (int i = 0; i < 0x80; i++) {
        if (i == 0x72) continue;                            /* PORT: the mouse bit (the original reads INT 33h, none under the harness) */
        if (comm[i] != rc[i]) { diff++; printf("  comm+%02X port %02X original %02X\n", i, comm[i], rc[i]); }
    }
    CHECK(diff == 0, "SETUP comm block: %d bytes differ", diff);
    CHECK((comm_get()->input_devices & 3) == 2, "SETUP comm+72h = %04X, expected mouse bit only", comm_get()->input_devices);
    free(r1); free(r2); free(r3); free(rc);
}

/* ------------------------------------------------------------------ 2. MPS_LOGO */

static u8 *logo_ref;
static int logo_seen;
static u64 logo_t[3];

static u32 dac_crc(void)
{
    u8 d[768];
    dac_get(0, 256, d);
    return crc32(d, 768);
}

static void logo_frame(int frame)
{
    logo_seen++;
    if (frame == 1) logo_t[0] = host_ns();
    if (frame == 82) logo_t[1] = host_ns();
    if (!logo_ref) return;
    u32 sc = crc32(vga_page(0xA000), 64000), dc = dac_crc();
    u32 rs = rd32le(logo_ref + 8 * (frame - 1)), rd = rd32le(logo_ref + 8 * (frame - 1) + 4);
    CHECK(sc == rs, "MPS_LOGO frame %d: screen CRC %08X, original %08X", frame, sc, rs);
    CHECK(dc == rd, "MPS_LOGO frame %d: DAC CRC %08X, original %08X", frame, dc, rd);
}

static void test_logo(const char *dir)
{
    size_t n;
    logo_ref = slurp(dir, "logo.bin", &n);
    CHECK(logo_ref && n == 82 * 8, "logo.bin missing or the wrong size");
    prog_comm_init();
    fe_test.logo_nowait = true;
    fe_test.logo_frame = logo_frame;
    logo_seen = 0;
    int code = platform_exec(PROG_MPS_LOGO, mps_logo_main, " N");
    CHECK(code == 0, "MPS_LOGO exit code %d, expected 0 (no key)", code);
    CHECK(logo_seen == 82, "MPS_LOGO drew %d frames, expected 82", logo_seen);
    CHECK(vga_mode() == 0x13, "MPS_LOGO leaves mode %02Xh", vga_mode());

    /* a key press completes the picture without waiting and decides the exit code */
    static const struct { u8 make; int expect; } keys[] = { { 0x01, 1 }, { 0x39, 3 } };       /* Esc, space */
    for (size_t k = 0; k < 2; k++) {
        logo_seen = 0;
        kbd_flush();
        kbd_host_byte(keys[k].make);
        kbd_host_byte((u8)(keys[k].make | 0x80));
        code = platform_exec(PROG_MPS_LOGO, mps_logo_main, " N");
        CHECK(code == keys[k].expect, "MPS_LOGO with key %02X: exit code %d, expected %d", keys[k].make, code, keys[k].expect);
        CHECK(logo_seen == 82, "MPS_LOGO with a key drew %d frames, expected 82", logo_seen);
        kbd_flush();
    }
    free(logo_ref);
    logo_ref = NULL;

    /* pacing: frame 1 to frame 82 takes the sum of the delays 2..82 (BIOS ticks, 18.2065 Hz) */
    char *path = host_game_path("LABSLOGO.SS", false);
    CHECK(path != NULL, "LABSLOGO.SS");
    if (!path) return;
    FILE *f = fopen(path, "rb");
    host_free(path);
    u8 hdr[0x1F + 14 * 82];
    CHECK(f && fread(hdr, 1, sizeof hdr, f) == sizeof hdr, "LABSLOGO.SS read");
    if (f) fclose(f);
    unsigned total = 0;
    for (int k = 1; k < 82; k++) total += rd16le(hdr + 0x1F + 14 * k + 4);
    fe_test.logo_nowait = false;
    code = platform_exec(PROG_MPS_LOGO, mps_logo_main, " N");
    double secs = (double)(logo_t[1] - logo_t[0]) / 1e9, want = total * 65536.0 / 1193182.0;
    printf("MPS_LOGO pacing: frames 1..82 took %.3f s, the delays add up to %u BIOS ticks = %.3f s\n", secs, total, want);
    CHECK(code == 0 && secs > want - 0.12 && secs < want + 0.12, "MPS_LOGO frame pacing %.3f s, expected %.3f s", secs, want);
    fe_test.logo_nowait = true;
    fe_test.logo_frame = NULL;
}

/* ------------------------------------------------------------------ 3. PLAYER */

static u8 *pan_ref;
static size_t pan_ref_len;
static int pan_cels, pan_files[4];
static int pan_last_file;

static void pan_cel_hook(int file, int cel)
{
    pan_cels++;
    if (file >= 0 && file < 4) pan_files[file]++;
    pan_last_file = file;
    if (file != 0 || !pan_ref) return;                      /* the references are for single files from a cleared screen */
    int n = rd16le(pan_ref + 2);
    if (cel < 0 || cel >= n) { CHECK(0, "PLAYER cel index %d of %d", cel, n); return; }
    u32 got = crc32(vga_page(0xA000), 64000), want = rd32le(pan_ref + 4 + 768 + 4 * (size_t)cel);
    CHECK(got == want, "PLAYER cel %d: screen CRC %08X, expected %08X", cel, got, want);
}

static void test_clear_screen(void)
{
    vga_set_mode(0x13);
    memset(vga_page(0xA000), 0, 64000);
}

static void test_pan_file(const char *dir, const char *name)
{
    char fn[64];
    snprintf(fn, sizeof fn, "%s.pn3", name);
    pan_ref = slurp(dir, fn, &pan_ref_len);
    CHECK(pan_ref != NULL, "reference %s missing", fn);
    if (!pan_ref) return;
    int n = rd16le(pan_ref + 2);
    test_clear_screen();
    pan_cels = 0;
    memset(pan_files, 0, sizeof pan_files);
    char tail[80];
    snprintf(tail, sizeof tail, " %s", name);
    fe_test.player_fast = true;
    fe_test.player_cel = pan_cel_hook;
    fe_test.player_ring_paras = 0x2800;                     /* 160 KB: the longer files wrap the ring several times */
    prog_comm_init();
    comm_get()->input_devices = 0;
    int code = platform_exec(PROG_PLAYER, player_main, tail);
    CHECK(code == 0, "PLAYER %s: exit code %d", name, code);
    CHECK(pan_cels == n, "PLAYER %s: showed %d cels, the file has %d", name, pan_cels, n);
    u8 dac[768];
    dac_get(0, 256, dac);
    CHECK(memcmp(dac, pan_ref + 4, 768) == 0, "PLAYER %s: the final palette differs", name);
    CHECK(rd16le(pan_ref) > 0, "PLAYER %s: speed", name);
    free(pan_ref);
    pan_ref = NULL;
}

/* real time: the fade of TITLE-1 and the animation clock */
static struct { u64 ns[2]; unsigned long cb[2], isr[2]; u32 ticks[2]; bool pal_ok[2]; int n; } rt;
static const u8 *rt_pal6;

static void rt_hook(int file, int cel)
{
    (void)file;
    int k = cel == 0 ? 0 : cel == 12 ? 1 : -1;
    if (k < 0) return;
    rt.ns[k] = host_ns();
    rt.cb[k] = fe_test.player_callbacks;
    rt.isr[k] = fe_test.player_isr_ticks;
    rt.ticks[k] = timer_ticks;
    u8 dac[768];
    dac_get(0, 256, dac);
    if (k == 0) {                                           /* the fade starts from black */
        int nz = 0;
        for (int i = 0; i < 768; i++) nz += dac[i] != 0;
        rt.pal_ok[0] = nz == 0;
    } else {
        rt.pal_ok[1] = memcmp(dac, rt_pal6, 768) == 0;      /* and has reached the palette by the 13th frame */
        kbd_host_byte(0x39);                                /* a key ends PLAYER */
        kbd_host_byte(0xB9);
    }
    rt.n++;
}

static void test_player_realtime(const char *dir)
{
    size_t n;
    u8 *ref = slurp(dir, "TITLE-1.pn3", &n);
    CHECK(ref != NULL, "TITLE-1.pn3 missing");
    if (!ref) return;
    rt_pal6 = ref + 4;
    memset(&rt, 0, sizeof rt);
    test_clear_screen();
    kbd_flush();
    fe_test.player_fast = false;
    fe_test.player_ring_paras = 0;
    fe_test.player_cel = rt_hook;
    fe_test.player_callbacks = fe_test.player_isr_ticks = 0;
    prog_comm_init();
    comm_get()->input_devices = 0;
    int code = platform_exec(PROG_PLAYER, player_main, " TITLE-1");
    fe_test.player_cel = NULL;
    CHECK(rt.n == 2, "PLAYER real time: the hook ran %d times, expected 2", rt.n);
    CHECK(code == 0x20, "PLAYER ended by a key: exit code %d, expected 20h (the ASCII of the space)", code);
    CHECK(rt.pal_ok[0], "PLAYER: the DAC is not black while the first frame is shown (the fade starts from black)");
    CHECK(rt.pal_ok[1], "PLAYER: the palette has not reached its target when frame 13 is shown");
    unsigned long cb = rt.cb[1] - rt.cb[0], isr = rt.isr[1] - rt.isr[0];
    double secs = (double)(rt.ns[1] - rt.ns[0]) / 1e9;
    /* cel k is shown 300 + 3 k callbacks after cel 0 (the 5 s fade at 60 callbacks a second, then 60 / speed = 3 ticks a frame) */
    CHECK(cb == 300 + 3 * 12, "PLAYER: %lu tick callbacks between frame 1 and frame 13, expected %d", cb, 300 + 3 * 12);
    double rate = (double)cb / secs;
    printf("PLAYER clock: %lu callbacks / %lu ISR ticks in %.3f s: %.2f callbacks/s (expected 60.07), %.2f ISR ticks/s (70.09), fade 5 s = 300 callbacks = %.2f s\n",
           cb, isr, secs, rate, (double)isr / secs, 300.0 / rate);
    CHECK(rate > 59.4 && rate < 60.8, "PLAYER animation clock %.2f Hz, expected 60.07", rate);
    CHECK(isr * 6 >= cb * 7 - 7 && isr * 6 <= cb * 7 + 7, "PLAYER: %lu ISR ticks for %lu callbacks (every 7th tick must be dropped)", isr, cb);
    free(ref);
}

/* the fade formula of frontend.md 8.3.2, probed in PLAYER's DGROUP: the DAC after tick t >= 2 is (v * min(FFFFh, 219 t) + 8000h) >> 16 */
static int fade_probe(const char *tail)
{
    (void)tail;
    u8 target[768];
    for (int i = 0; i < 768; i++) target[i] = (u8)((i * 7) & 63);
    target[0] = 63; target[1] = 1; target[2] = 32;
    memset(DSP(PLAYER_DS_pal), 0, 768);
    DSW(PLAYER_DS_fade_meas) = 0;
    DSW(PLAYER_DS_fade_steps) = 0;
    fade_init(5);                                           /* [13D4] = 300 */
    CHECK(DSW(PLAYER_DS_fade_steps) == 300, "fade_init(5): %u steps", DSW(PLAYER_DS_fade_steps));
    CHECK(DSW(PLAYER_DS_fade_cnt) == 256, "fade_init: %u colours a tick", DSW(PLAYER_DS_fade_cnt));
    CHECK(DSW(PLAYER_DS_fade_step) == 219, "fade_init: step %u, expected 219", DSW(PLAYER_DS_fade_step));
    pal_load(0, 256, target);                               /* while the fade is pending the DAC gets zeros */
    u8 dac[768];
    dac_get(0, 256, dac);
    int nz = 0;
    for (int i = 0; i < 768; i++) nz += dac[i] != 0;
    CHECK(nz == 0, "pal_load during a fade must write zeros to the DAC");
    CHECK(memcmp(DSP(PLAYER_DS_pal), target, 768) == 0, "pal_load keeps the palette for the fade");
    for (int t = 1; t <= 300; t++) {
        fade_step();
        dac_get(0, 256, dac);
        u32 f = 219u * (u32)t;
        if (f > 0xFFFF) f = 0xFFFF;
        int bad = 0;
        for (int i = 0; i < 768; i++) {
            u32 want = t == 1 ? 0 : ((u32)target[i] * f + 0x8000u) >> 16;     /* tick 1 writes the slice made before the palette existed */
            if (dac[i] != want) bad++;
        }
        if (t == 1 || t == 2 || t == 100 || t == 299 || t == 300) CHECK(bad == 0, "fade tick %d: %d DAC bytes differ from the formula", t, bad);
        else if (bad) CHECK(0, "fade tick %d: %d DAC bytes differ from the formula", t, bad);
    }
    CHECK(DSW(PLAYER_DS_fade_steps) == 0, "fade finished: %u steps left", DSW(PLAYER_DS_fade_steps));
    CHECK(memcmp(dac, target, 768) == 0, "fade end: the DAC equals the palette");
    return 0;
}

static void test_player(const char *dir)
{
    static const char *const names[] = { "TITLE-1", "TITLE-2", "TITLE-3A", "CREDIT", "C-PIT", "BUNKER" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) test_pan_file(dir, names[i]);

    /* two files in order (the ring carries the first file's cels and then the second's) */
    test_clear_screen();
    pan_cels = 0;
    memset(pan_files, 0, sizeof pan_files);
    fe_test.player_fast = true;
    fe_test.player_cel = pan_cel_hook;
    fe_test.player_ring_paras = 0x2800;
    prog_comm_init();
    comm_get()->input_devices = 0;
    size_t n1, n2;
    u8 *a = slurp(dir, "TITLE-1.pn3", &n1), *b = slurp(dir, "TITLE-2.pn3", &n2);
    int code = platform_exec(PROG_PLAYER, player_main, " TITLE-1 TITLE-2");
    CHECK(a && b && code == 0 && pan_files[0] == rd16le(a + 2) && pan_files[1] == rd16le(b + 2),
          "PLAYER, two files: exit %d, cels %d + %d, expected %d + %d", code, pan_files[0], pan_files[1], a ? rd16le(a + 2) : -1, b ? rd16le(b + 2) : -1);
    free(a);
    free(b);
    fe_test.player_cel = NULL;

    /* the fade formula, then the real-time run (5 s fade + 12 frames) */
    fe_test.player_fast = false;
    fe_test.player_ring_paras = 0;
    platform_exec(PROG_PLAYER, fade_probe, "");
    test_player_realtime(dir);
    fe_test.player_cel = NULL;
}

/* ------------------------------------------------------------------ 4. DSWAP, MISC, text mode */

static void test_small(void)
{
    prog_comm_init();
    CHECK(platform_exec(PROG_DSWAP, dswap_main, " 0") == 1, "DSWAP 0");
    CHECK(platform_exec(PROG_DSWAP, dswap_main, " 1") == 1, "DSWAP 1");
    CHECK(platform_exec(PROG_DSWAP, dswap_main, " 2") == 1, "DSWAP 2");
    CHECK(atoi_fn("  -42x") == -42 && atoi_fn("17") == 17 && atol_fn("+100000") == 100000L, "atoi / atol");

    kbd_flush();
    CHECK(kbd_empty_misc() == -1, "MISC kbd_empty with an empty buffer");
    kbd_host_byte(0x01);
    kbd_host_byte(0x81);
    CHECK(kbd_empty_misc() == 0, "MISC kbd_empty with a key waiting");
    CHECK(kbd_get_misc() == 0x011B, "MISC kbd_get (Esc)");
    CHECK(joy_button_misc(0) == 0 && joy_button_misc(1) == 0, "MISC joy_button without a gamepad");
    wr8(0x40, 0x17, 0xFF);
    kbd_locks_clear();
    CHECK(rd8(0x40, 0x17) == 0xF0, "MISC kbd_locks_clear");

    tm_set_mode3();
    for (int i = 0; i < 81; i++) tm_tty('x');
    int r, c;
    tm_cursor(&r, &c);
    CHECK(r == 1 && c == 1, "teletype wraps at column 80");
    tm_gotoxy(24, 0);
    tm_tty('A');
    tm_tty(0x0D);
    tm_tty(0x0A);
    tm_cursor(&r, &c);
    CHECK(r == 24 && c == 0 && tm_buffer()[2 * (23 * 80)] == 'A' && tm_buffer()[2 * (24 * 80)] == ' ', "teletype scrolls at the bottom row");
    kbd_flush();
}

int frontend_selftest(const char *refdir)
{
    char *p = host_game_path("SETUP.EXE", false);
    if (!p) {
        printf("frontend-test: no game files in %s\n", host_game_dir());
        return 77;
    }
    host_free(p);
    test_setup(refdir);
    test_logo(refdir);
    test_player(refdir);
    test_small();
    vga_set_mode(0x03);
    printf("frontend-test %s: %d checks, %d failed\n", fails ? "FAILED" : "OK", checks, fails);
    return fails ? 1 : 0;
}
