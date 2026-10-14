/* DSWAP.EXE (frontend.md section 4, 8.1): makes sure the next program is on the disk in the drive ("0" = START.EXE, "1" = VGAME.EXE,
 * "2" = END.EXE). On a hard-disk install the fopen succeeds and it exits with 1 at once. PORT: the floppy swap prompt (SWAP.PIC, the
 * INT 13h presence tests) is never shown; the game folder always has the program. Exit code 1 = continue (0 only after a fatal memory error). */
#include <stdio.h>
#include <string.h>

#include "comm.h"
#include "frontend/fe.h"
#include "gen/module_frontend.h"
#include "gen/module_platform.h"
#include "gen/module_video.h"
#include "host.h"
#include "platform/platform.h"
#include "prog.h"

/* DSWAP 0000:1BD0: blanks and tabs, an optional sign, digits; 32-bit accumulate without an overflow check. 0FF8 is the atoi entry (jmp 1BD0). */
long atol_fn(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    u32 acc = 0;
    for (; *s >= '0' && *s <= '9'; s++) acc = acc * 10u + (u32)(*s - '0');
    return (long)(neg ? 0u - acc : acc);
}

/* DSWAP 0000:0FF8 */
int atoi_fn(const char *s) { return (int)atol_fn(s); }

/* DSWAP 0000:0219: screen off (MGRAPHIC slot 69), the 2000h-paragraph disk buffer freed, the Ctrl-Break handler removed, exit(code) */
void dswap_exit(int code)
{
    screen_off();
    break_hook_remove();
    exit_fn(code);
}

/* DSWAP 0000:0010 dswap_main(argc, argv); the chain passes the tail " 0" / " 1" / " 2" */
int dswap_main(const char *tail)
{
    static const char *const names[3] = { "start.exe", "vgame.exe", "end.exe" };       /* DS:006A */
    break_hook_install();
    int n = atoi_fn(tail);
    const char *name = n >= 0 && n < 3 ? names[n] : names[0];
    char *path = host_game_path(name, false);                 /* fopen(name, "r") */
    if (path) host_free(path);
    /* PORT: not found -> the original asks for the disk (SWAP.PIC, drives A and B, INT 13h tests); the port never shows it. */
    dswap_exit(1);
    return 1;
}
