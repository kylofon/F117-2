/* F117.COM replaced by a C function: the chain loop and the exit-code dispatch (HANDOFFS.md section 1,
 * RE_GUIDE 2.1). Program sequence, exactly as the original:
 *
 *   start-up   shrink own block to 7Ch paragraphs, allocate the comm block (140h paragraphs), store its
 *              segment at 0000:04F0, 04F2 = 1, 04F4 = 0
 *   1  SETUP (F117's tail)           must return 24h, else quit
 *   2  MPS_LOGO " X"                 X = comm[0Dh] & 5Fh; 2 = quit; the code is kept
 *   3  drivers (overlays)            MGRAPHIC, sound, MISC bound; off-screen page = graphics entry 0
 *   4  PLAYER " title-1 ..."         only if MPS_LOGO returned 0; exit code ignored
 *   L  DSWAP " 0"                    0 = quit
 *      START ""                      0 = quit; any other value flies
 *      DSWAP " 1"                    0 = quit
 *      PLAYER " title-2"             only if START returned 0Ch
 *      VGAME ""                      0 = quit
 *      DSWAP " 2"                    0 = quit
 *      END ""                        23h = back to L; anything else quits
 *
 * Quit = far call to the sound driver's shutdown entry (nothing to do in the port), then INT 21h/4C00h. */
#include "prog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "comm.h"
#include "host.h"
#include "mem.h"
#include "platform/platform.h"

const ProgDef prog_table[PROG_COUNT] = {
    [PROG_SETUP]    = { "SETUP.EXE",    setup_main },
    [PROG_MPS_LOGO] = { "MPS_LOGO.EXE", mps_logo_main },
    [PROG_PLAYER]   = { "PLAYER.EXE",   player_main },
    [PROG_START]    = { "START.EXE",    start_main },
    [PROG_VGAME]    = { "VGAME.EXE",    vgame_main },
    [PROG_END]      = { "END.EXE",      end_main },
    [PROG_DSWAP]    = { "DSWAP.EXE",    dswap_main },
};

bool prog_trace;
int prog_run_count[PROG_COUNT];
bool prog_abort;

void prog_log(const char *fmt, ...)
{
    if (!prog_trace) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

int prog_run(ProgId id, const char *tail)
{
    prog_log("[chain] EXEC %-12s tail \"%s\"", prog_table[id].exe, tail);
    prog_run_count[id]++;
    int code = platform_exec(id, prog_table[id].main, tail);
    prog_log("[chain]   %s exit code %02Xh", prog_table[id].exe, code);
    return code;
}

/* The files F117.COM and the programs open from the game folder (launcher/game.cpp MissingGameFile). */
const char *game_files_missing(void)
{
    static const char *const names[] = { "F117.COM", "SETUP.EXE", "MPS_LOGO.EXE", "PLAYER.EXE", "START.EXE",
                                         "VGAME.EXE", "END.EXE", "DSWAP.EXE", "MGRAPHIC.EXE", "MISC.EXE" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char *p = host_game_path(names[i], false);
        if (!p) return names[i];
        host_free(p);
    }
    return NULL;
}

static void f117_quit(const char *why)
{
    prog_log("[chain] quit: %s", why);
}

int chain_run(const char *f117_tail)
{
    int runs = 0;
#define RUN(id, tail) (runs++, prog_run((id), (tail)))

    /* F117.COM start-up: SP = 07B8, own block shrunk to 7Ch paragraphs, then the comm block. */
    dos_arena_reset(PSP_SEG, 0x7C);
    u16 comm_seg;
    if (!dos_mem_alloc(COMM_PARAS, &comm_seg)) host_fatal("F117.COM: not enough memory for the comm block");
    wr16(0, COMM_SEG_PTR, comm_seg);
    wr16(0, COMM_FIRST_RUN, 1);
    wr16(0, COMM_INIT_DONE, 0);
    memset(mp(comm_seg, 0), 0, COMM_SIZE);     /* PORT: DOS gives the original uncleared memory; the port starts from zero */
    prog_log("[chain] F117.COM: comm block %04X:0000 (%u bytes), 0000:04F0 = %04X", comm_seg, COMM_SIZE, comm_seg);

    if (RUN(PROG_SETUP, f117_tail) != EXIT_SETUP_OK) { f117_quit("SETUP did not return 24h"); return runs; }
    if (prog_abort) { f117_quit("aborted"); return runs; }

    char tail[96];
    snprintf(tail, sizeof tail, " %c", comm_get()->snd_driver[0] & 0x5F);
    int logo = RUN(PROG_MPS_LOGO, tail);
    if (logo == EXIT_LOGO_ALT_Q) { f117_quit("MPS_LOGO returned 2 (Alt-Q)"); return runs; }
    if (prog_abort) { f117_quit("aborted"); return runs; }

    /* The drivers: MGRAPHIC, the sound driver and MISC are bound (platform.md §6: static C function tables in the
     * port, so nothing is loaded), and comm+20h = the off-screen page, the result of graphics entry 0 (2):
     * 0FA00h bytes = 0FA0h paragraphs. */
    u16 page_seg;
    if (!dos_mem_alloc(0x0FA0, &page_seg)) host_fatal("F117.COM: not enough memory for the off-screen page");
    comm_get()->seg_offscreen = page_seg;
    prog_log("[chain] drivers bound (function tables); off-screen page at %04X:0000", page_seg);

    if (logo == EXIT_LOGO_NOKEY) {
        RUN(PROG_PLAYER, " title-1 title-2 title-3a credit c-pit bunker");   /* exit code ignored */
        if (prog_abort) { f117_quit("aborted"); return runs; }
    }

    for (;;) {                                                      /* L */
        if (RUN(PROG_DSWAP, " 0") == 0) { f117_quit("DSWAP 0 returned 0"); break; }
        int start = RUN(PROG_START, "");
        if (start == 0) { f117_quit("START returned 0"); break; }
        if (prog_abort) { f117_quit("aborted"); break; }
        if (RUN(PROG_DSWAP, " 1") == 0) { f117_quit("DSWAP 1 returned 0"); break; }
        if (start == EXIT_START_FLY) RUN(PROG_PLAYER, " title-2");
        if (prog_abort) { f117_quit("aborted"); break; }
        if (RUN(PROG_VGAME, "") == 0) { f117_quit("VGAME returned 0"); break; }
        if (prog_abort) { f117_quit("aborted"); break; }
        if (RUN(PROG_DSWAP, " 2") == 0) { f117_quit("DSWAP 2 returned 0"); break; }
        int end = RUN(PROG_END, "");
        if (end != EXIT_END_RESTART) { f117_quit("END did not return 23h"); break; }
        if (prog_abort) { f117_quit("aborted"); break; }
    }
#undef RUN
    return runs;
}
