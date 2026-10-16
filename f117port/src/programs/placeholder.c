/* PHASE 4 ONLY: placeholder programs. S5.3 replaced SETUP, MPS_LOGO, PLAYER and DSWAP by the real ports (src/frontend); their
 * stand-ins below (*_stub) are used only by `--chain-test` (prog_use_stubs), which runs the chain without game files.
 * Each one logs its name, writes into the comm block (and the BIOS
 * words) what a plausible run of the real program would, and returns the real program's exit code
 * (HANDOFFS.md sections 1-6). The chain is driven by what they write:
 *
 *   START call 1 -> 0Ch (fly, title-2 first), landed;  START call 2 -> 0Dh (fly, no title-2), ejected;
 *   START call 3 -> 0 (quit at the quit prompt).
 *
 * so START -> VGAME -> END -> START runs twice before the chain ends. */
#include "programs/placeholder.h"

#include <stdio.h>
#include <string.h>

#include "comm.h"
#include "host.h"
#include "mem.h"
#include "platform/platform.h"
#include "platform/timer.h"
#include "platform/vga.h"
#include "prog.h"

bool placeholder_interactive;

/* ---- the test screen --------------------------------------------------------------------------- */

static void key_isr(u8 xt)
{
    if (xt == 0x01) prog_abort = true;                  /* Esc make code */
}

/* Only the stub chain (--chain-test) may install it: with the real programs an Esc anywhere (a cancelled dialog, a crash in flight)
 * set prog_abort, and the chain quit to the desktop when that program ended (bug found 2026-10-16). */
void placeholder_init(void)
{
    if (prog_use_stubs) kbd_set_observer(key_isr);
}

static void screen(int number)
{
    vga_set_mode(0x13);
    vga_out(0x3C8, 0);                                  /* DAC: 3 bits red, 3 bits green, 2 bits blue */
    for (int i = 0; i < 256; i++) {
        vga_out(0x3C9, (u8)((i & 7) * 9));
        vga_out(0x3C9, (u8)(((i >> 3) & 7) * 9));
        vga_out(0x3C9, (u8)((i >> 6) * 21));
    }
    u8 *px = vga_page(0xA000);
    for (int y = 40; y < 200; y++)                      /* 16 x 16 cells of all 256 colours */
        for (int x = 0; x < 320; x++) px[y * 320 + x] = (u8)((y - 40) / 10 * 16 + x / 20);
    for (int k = 0; k < number; k++)                    /* program number: white blocks */
        for (int y = 5; y < 35; y++)
            for (int x = 10 + k * 44; x < 10 + k * 44 + 40; x++) px[y * 320 + x] = 0xFF;
}

/* Half a second of ticks; the host keeps pumping, so the window stays alive. */
static void hold(int number)
{
    if (!placeholder_interactive) return;
    screen(number);
    u32 until = timer_ticks + 35;
    while (timer_ticks < until && !prog_abort && !host_quit_requested()) host_pump();
    if (host_quit_requested()) prog_abort = true;
}

/* ---- helpers ------------------------------------------------------------------------------------ */

static int tail_number(const char *tail)
{
    while (*tail == ' ') tail++;
    return *tail >= '0' && *tail <= '9' ? *tail - '0' : -1;
}

static u16 pilot_rd(int off) { return rd16(rd16(0, COMM_SEG_PTR), (u16)(0x1272 + off)); }
static void pilot_wr(int off, u16 v) { wr16(rd16(0, COMM_SEG_PTR), (u16)(0x1272 + off), v); }

static int start_calls;                                 /* host-side: which pass of START/VGAME/END this is */

/* ---- SETUP: asks the configuration questions, keeps the answers only in the comm block --------- */

int setup_stub(const char *tail)
{
    Comm *c = comm_get();
    prog_log("[SETUP] tail \"%s\": hardware questions, answers into the comm block", tail);
    strcpy(c->gfx_driver, "Mgraphic.exe");              /* 00 */
    strcpy(c->snd_driver, "Asound.117");                /* 0D menu 2: AdLib */
    c->clearance = 1;                                   /* 22 */
    c->joy_calib_byte = 0;                              /* 24 */
    c->mission_state = 1;                               /* 26 (unused value) */
    c->unused_2a = 0;                                   /* 2A */
    c->nonscoring = 0;                                  /* 30 */
    c->adapter = 3;                                     /* 32 MCGA: the driver name starts with 'M' */
    c->unused_70 = 0;                                   /* 70 */
    c->input_devices = 2;                               /* 72 bit 1: mouse driver present */
    hold(1);
    return EXIT_SETUP_OK;
}

/* ---- MPS_LOGO: the logo animation; no key pressed -> 0 (the PLAYER intro follows) -------------- */

int mps_logo_stub(const char *tail)
{
    char letter = tail[0] == ' ' ? tail[1] : tail[0];
    prog_log("[MPS_LOGO] sound driver letter '%c' (LABSLOGO.SS and the .LOG of that driver): no key pressed", letter);
    hold(2);
    return EXIT_LOGO_NOKEY;
}

/* ---- PLAYER: the .PAN animation player; its exit code is ignored -------------------------------- */

int player_stub(const char *tail)
{
    prog_log("[PLAYER] animations:%s", tail);
    hold(3);
    return 0;
}

/* ---- DSWAP: swaps the program into memory (hard-disk install: always found) --------------------- */

int dswap_stub(const char *tail)
{
    static const char *const names[3] = { "start.exe", "vgame.exe", "end.exe" };
    int n = tail_number(tail);
    prog_log("[DSWAP] %d = %s found", n, n >= 0 && n < 3 ? names[n] : "?");
    return EXIT_DSWAP_OK;
}

/* ---- START: stand-in of the --chain-test (the real START is src/shell, S5.4) ------------------------ */

int start_stub(const char *tail)
{
    (void)tail;
    Comm *c = comm_get();
    start_calls++;
    if (rd16(0, COMM_FIRST_RUN)) {                      /* 04F2: first run -> the roster screen first */
        prog_log("[START] first run: roster screen, ROSTER.FIL");
        wr16(0, COMM_FIRST_RUN, 0);
    }
    if (!rd16(0, COMM_INIT_DONE)) wr16(0, COMM_INIT_DONE, 1);   /* 04F4: after MGRAPHIC slots 69/60 */
    hold(4);
    if (start_calls >= 3) {
        prog_log("[START] pass %d: quit prompt answered Y", start_calls);
        return 0;
    }
    c->mission_state = 3;                               /* 26 */
    c->end_code = 0;                                    /* 28 */
    c->missiles_launched = 0;                           /* 2C */
    c->fuel_lbs = 12 * 400;                             /* 2E 12 x route score */
    c->nonscoring = 0;                                  /* 30 */
    c->night = (u8)(start_calls == 2);                  /* 33 */
    for (int i = 0; i < 4; i++) {
        c->weapon_id[i] = (u16)(i == 0 ? 7 : 0);        /* 38 */
        c->weapon_qty[i] = (u16)(i == 0 ? 2 : 0);       /* 40 */
    }
    c->world_blob[0] = 0; c->world_blob[1] = 0;         /* 7A theatre object ids */
    c->world_blob[2] = 1; c->world_blob[3] = 0;         /* 7C site count */
    pilot_wr(0x38, 0);                                  /* 1272+38 theatre; +3A war level, +3C type read by END */
    int code = start_calls == 1 ? EXIT_START_FLY : EXIT_START_FLY_NOHANGAR;
    prog_log("[START] pass %d: mission set up (%s mission), fly", start_calls, c->night ? "night" : "day");
    return code;
}

/* ---- VGAME: the flight; writes the result for END ---------------------------------------------- */

/* the real VGAME (src/sim) tells the stand-in END that a pass happened (the real START does not count its passes) */
void placeholder_note_vgame_pass(void) { start_calls++; }

int vgame_stub(const char *tail)
{
    (void)tail;
    Comm *c = comm_get();
    hold(5);
    c->damage_mask = start_calls == 1 ? 0x0000 : 0x0013;                    /* 34 */
    c->hits_taken = start_calls == 1 ? 0 : 3;                               /* 36 */
    c->missiles_launched = start_calls == 1 ? 2 : 5;                        /* 2C */
    c->final_x = 0x1800; c->final_y = 0x2400;                               /* 74, 76 */
    if (start_calls == 1) { c->mission_state = 3; c->end_code = 0; }        /* landed safely */
    else                  { c->mission_state = 2; c->end_code = 0; }        /* ejected, survived */
    prog_log("[VGAME] pass %d: flight over, comm+26h = %u, comm+28h = %u, %u missiles launched", start_calls,
             c->mission_state, c->end_code, c->missiles_launched);
    return EXIT_VGAME_END;
}

/* ---- END: debrief, scoring, promotion, medals --------------------------------------------------- */

/* The screen END picks from comm+26h/28h (HANDOFFS section 5). */
static const char *end_screen(u16 state, u16 code)
{
    if (state == 3 && code == 0) return "landed safely";
    if (state == 2 && code == 0) return "ejected: rescue or captured";
    if (state == 1 && code == 1) return "cemetery: crashed spectacularly";
    if (state == 1 && code == 2) return "exploded on a hill";
    if (state == 1 && code == 5) return "crashed on the runway";
    if (state == 1 && code == 6) return "eject attempt failed";
    return "(garbage)";
}

int end_stub(const char *tail)
{
    (void)tail;
    Comm *c = comm_get();
    hold(6);
    u16 score = (u16)(250 * start_calls);
    prog_log("[END] pass %d: debrief \"%s\"; mission score %u", start_calls, end_screen(c->mission_state, c->end_code),
             c->nonscoring ? 0u : score);
    if (c->nonscoring == 0) {                           /* the mission counts */
        pilot_wr(PILOT_MISSIONS, (u16)(pilot_rd(PILOT_MISSIONS) + 1));
        pilot_wr(PILOT_LAST_SCORE, score);
        if (score > pilot_rd(PILOT_BEST_SCORE)) pilot_wr(PILOT_BEST_SCORE, score);
        u32 career = (u32)(pilot_rd(PILOT_CAREER) | pilot_rd(PILOT_CAREER + 2) << 16) + score;
        pilot_wr(PILOT_CAREER, (u16)career);
        pilot_wr(PILOT_CAREER + 2, (u16)(career >> 16));
    }
    return EXIT_END_RESTART;
}

/* The stand-in of a program (the chain test): the stubs of the programs that need game files, and VGAME / END which are still only placeholders. */
ProgMain placeholder_main(ProgId id)
{
    switch (id) {
    case PROG_SETUP:    return setup_stub;
    case PROG_MPS_LOGO: return mps_logo_stub;
    case PROG_PLAYER:   return player_stub;
    case PROG_DSWAP:    return dswap_stub;
    case PROG_START:    return start_stub;
    case PROG_VGAME:    return vgame_stub;
    case PROG_END:      return end_stub;
    default:            return NULL;
    }
}
