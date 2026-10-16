/* f117 --sound-test DIR (ctest `sound`, driven by port/notes/s59/sound_ref.py):
 *  (1) the register streams: DIR/list.txt names scenarios; for each, DIR/<name>.ops (a script of driver calls recorded from the ORIGINAL driver running
 *      under Unicorn) is replayed on the port sequencer and the OPL writes (op index, register, value) and the snd_poll results go to
 *      DIR/<name>.out, which the script compares with the original log, line by line.
 *        game scripts:  i = snd_init, c N A = snd_cmd(N, A), p = snd_poll, n = snd_noise
 *        logo scripts:  L = load + init, C N = snd_cmd_log(N), I = one INT 8 of the overlay
 *  (2) the poll rates (printed as `rate NAME polls ticks`, checked by the script): the ISR bodies of START, PLAYER, END and VGAME run for 7000 ticks on a
 *      scratch DGROUP, and START with the logo overlay clock run against the host clock for two seconds.
 * Exit 77 without ASOUND.117 / ASOUND.LOG in --game-dir. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/fe.h"
#include "gen/module_frontend.h"
#include "gen/module_platform.h"
#include "host.h"
#include "mem.h"
#include "platform/timer.h"
#include "sound/snd.h"

void end_tick(void);                       /* debrief/end_ui.c */
void start_tick(void);                     /* shell/start.c */
void sim_tick_for_test(void);              /* sim/sim_main.c */
extern unsigned sim_poll_calls;            /* sim/sim_stubs.c */

static FILE *out;
static unsigned op_index;

static void sink_log(u8 reg, u8 val) { fprintf(out, "%u %02X %02X\n", op_index, reg, val); }

static int run_script(const char *dir, const char *name)
{
    char path[2400];
    snprintf(path, sizeof path, "%s/%s.ops", dir, name);
    FILE *in = fopen(path, "r");
    if (!in) { printf("sound-test: %s missing\n", path); return 1; }
    snprintf(path, sizeof path, "%s/%s.out", dir, name);
    out = fopen(path, "w");
    if (!out) { fclose(in); return 1; }
    char line[128];
    op_index = 0;
    bool game = strncmp(name, "logo", 4) != 0;
    snd_set_opl_sink(NULL);
    while (fgets(line, sizeof line, in)) {
        int a = 0, b = 0;
        char op = line[0];
        if (op == 'i' || op == 'L') {                                     /* init: its own writes are not part of the comparison */
            if (game) { if (!snd_load_driver('A')) return 77; snd_init(0); }
            else { if (!snd_log_load('A')) return 77; snd_log_init(); timer_set_aux(NULL); }   /* the clock is driven by the script */
            snd_set_opl_sink(sink_log);
            continue;
        }
        sscanf(line + 1, "%d %d", &a, &b);
        if (op == 'c') snd_cmd(a, b);
        else if (op == 'p') fprintf(out, "%u P %d\n", op_index, snd_poll());
        else if (op == 'n') snd_noise();
        else if (op == 'C') snd_log_cmd(a);
        else if (op == 'I') snd_log_isr();
        op_index++;
    }
    snd_set_opl_sink(NULL);
    if (game) snd_shutdown(); else snd_log_shutdown();
    fclose(in);
    fclose(out);
    return 0;
}

/* ---- the poll rates */
static void scratch_ds(void)
{
    cur_dgroup = 0x6000;
    memset(mp(cur_dgroup, 0), 0, 0x10000);
}

static void rate(const char *name, unsigned polls, unsigned ticks)
{
    printf("rate %s polls %u ticks %u\n", name, polls, ticks);
}

static void rate_tests(void)
{
    const unsigned N = 7000;
    /* START (8D0C): the tick flag and the poll, no skip */
    scratch_ds();
    snd_isr_ticks = snd_isr_polls = 0;
    for (unsigned i = 0; i < N; i++) start_tick();
    rate("START", snd_isr_polls, N);
    /* PLAYER (026B:0336): class 4 drops every 7th tick; class 5 (a 60 Hz display) does not */
    scratch_ds();
    DSW(PLAYER_DS_class) = 4;
    DSB(PLAYER_DS_skip) = 7;
    snd_isr_polls = 0;
    for (unsigned i = 0; i < N; i++) timer_step();
    rate("PLAYER", snd_isr_polls, N);
    scratch_ds();
    DSW(PLAYER_DS_class) = 5;
    DSB(PLAYER_DS_skip) = 7;
    snd_isr_polls = 0;
    for (unsigned i = 0; i < N; i++) timer_step();
    rate("PLAYER60", snd_isr_polls, N);
    /* END (4D51) */
    scratch_ds();
    snd_isr_polls = 0;
    for (unsigned i = 0; i < N; i++) end_tick();
    rate("END", snd_isr_polls, N);
    /* VGAME (1E67): sim_snd_poll counts every poll that is not skipped */
    scratch_ds();
    DSW(0x2639) = 4;
    DSW(0x263B) = 17024;
    DSW(0x2636) = 1;
    DSW(0x2630) = DSW(0x2632) = DSW(0x263B);
    DSB(0x264B) = 0;
    sim_poll_calls = 0;
    for (unsigned i = 0; i < N; i++) sim_tick_for_test();
    rate("VGAME", sim_poll_calls, N);
    /* the real tick: START ISR body on the host clock for two seconds */
    scratch_ds();
    host_wait_until_ns(host_ns() + 300000000ull);        /* the ticks that fell due while the scripts ran are delivered first */
    snd_isr_polls = 0;
    u32 t0 = timer_ticks;
    timer_set_hook(start_tick);
    timer_install();
    host_wait_until_ns(host_ns() + 2000000000ull);
    timer_remove();
    printf("rate START_live polls %u ticks %u\n", snd_isr_polls, (unsigned)(timer_ticks - t0));
}

static int logo_clock_test(void)
{
    if (!snd_log_load('A')) return 77;
    snd_set_opl_sink(NULL);
    snd_log_init();                                  /* installs the clock */
    snd_log_cmd(2);                                  /* the jingle: slow mode */
    u32 t0 = timer_ticks;
    unsigned c0 = snd_log_isr_count();
    host_wait_until_ns(host_ns() + 2000000000ull);
    unsigned c1 = snd_log_isr_count();
    printf("rate LOGO_live polls %u ticks %u\n", c1 - c0, (unsigned)(timer_ticks - t0));
    snd_log_shutdown();
    return 0;
}

int sound_selftest(const char *dir)
{
    char *a = host_game_path("ASOUND.117", false), *l = host_game_path("ASOUND.LOG", false);
    if (!a || !l) { host_free(a); host_free(l); printf("sound-test: ASOUND.117 / ASOUND.LOG not found in the game folder\n"); return 77; }
    host_free(a);
    host_free(l);
    char path[2400];
    snprintf(path, sizeof path, "%s/list.txt", dir);
    FILE *lf = fopen(path, "r");
    if (!lf) { printf("sound-test: %s missing\n", path); return 1; }
    char name[128];
    int n = 0;
    while (fgets(name, sizeof name, lf)) {
        name[strcspn(name, "\r\n")] = 0;
        if (!name[0]) continue;
        int rc = run_script(dir, name);
        if (rc) { fclose(lf); return rc; }
        n++;
    }
    fclose(lf);
    printf("sound-test: %d scripts replayed\n", n);
    rate_tests();
    int rc = logo_clock_test();
    if (rc) return rc;
    printf("sound-test: done\n");
    return 0;
}
