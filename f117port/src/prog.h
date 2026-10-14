#pragma once
/* The seven programs and F117.COM's chain loop (port/spec/HANDOFFS.md section 1, platform.md §6,
 * RE_GUIDE 2.1).
 *
 * In the original, F117.COM runs SETUP, MPS_LOGO, PLAYER, DSWAP, START, VGAME and END with INT 21h/4Bh and
 * reads each return code. In the port each program is a C function `<name>_main(tail)` that returns the exit
 * code (the program's `exit(n)` / `INT 21h/4Ch` becomes `return n`), and chain_run() is the loop. The
 * programs' real bodies arrive module by module in S5; until then programs/placeholder.c supplies a stand-in
 * for each that writes the comm fields and exit code a plausible run would. */
#include "types.h"

typedef enum { PROG_SETUP, PROG_MPS_LOGO, PROG_PLAYER, PROG_START, PROG_VGAME, PROG_END, PROG_DSWAP, PROG_COUNT } ProgId;

/* Exit codes (HANDOFFS section 1). */
#define EXIT_SETUP_OK    0x24      /* SETUP: configured; anything else quits */
#define EXIT_LOGO_NOKEY  0         /* MPS_LOGO: no key (play the PLAYER intro) */
#define EXIT_LOGO_ESC    1         /* Esc: skip the intro */
#define EXIT_LOGO_ALT_Q  2         /* Alt-Q: quit */
#define EXIT_LOGO_KEY    3         /* any other key: skip the intro */
#define EXIT_DSWAP_OK    1         /* DSWAP: program found, continue (0 only after a fatal memory error: quit) */
#define EXIT_START_FLY   0x0C      /* START: fly, PLAYER " title-2" first */
#define EXIT_START_FLY_NOHANGAR 0x0D   /* START: fly, no title-2 */
#define EXIT_VGAME_END   0x81      /* VGAME: the initial value of DS:008D, anything non-zero goes to END */
#define EXIT_END_RESTART 0x23      /* END: back to START through DSWAP " 0" */

/* A program: `tail` is the DOS command tail after the program name (with its leading space), "" if none. */
typedef int (*ProgMain)(const char *tail);

typedef struct {
    const char *exe;               /* file name in the game folder */
    ProgMain    main;
} ProgDef;

extern const ProgDef prog_table[PROG_COUNT];

/* The program entry points. SETUP, MPS_LOGO, PLAYER and DSWAP are the real ports (src/frontend, S5.3), START is src/shell (S5.4); VGAME and END
 * are still the placeholders of programs/placeholder.c (replaced by S5.5 + S5.7, S5.8). */
int setup_main(const char *tail);
int mps_logo_main(const char *tail);
int player_main(const char *tail);
int start_main(const char *tail);
int vgame_main(const char *tail);
int end_main(const char *tail);
int dswap_main(const char *tail);

/* true: --chain-test runs the stand-ins of programs/placeholder.c instead of the real ports (it needs no game files). */
extern bool prog_use_stubs;

/* F117.COM's start-up part: the PSP block shrunk to 7Ch paragraphs, the comm block allocated and zeroed, 0000:04F0..04F4 set. */
void prog_comm_init(void);

/* How often each program ran (the chain test checks the sequence). */
extern int prog_run_count[PROG_COUNT];

/* Runs one program (a logged EXEC) and returns its exit code. */
int prog_run(ProgId id, const char *tail);

/* Logging of program switches and exit codes: on for --chain-test, --trace and F117_TRACE=1. */
extern bool prog_trace;
void prog_log(const char *fmt, ...);

/* An abort requested while a program runs (Esc in a placeholder screen, window closed): the chain loop
 * stops after the current program, like the Ctrl-Break model (platform.md §5.4). */
extern bool prog_abort;

/* F117.COM: allocates the comm block, then runs the chain. `f117_tail` is F117's own command tail (passed
 * to SETUP). Returns the number of programs run. */
int chain_run(const char *f117_tail);

/* Files the chain needs in the game folder (the launcher's MissingGameFile list). Returns the first
 * missing name, or NULL if all are there. */
const char *game_files_missing(void);
