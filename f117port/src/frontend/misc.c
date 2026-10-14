/* MISC.EXE (frontend.md section 5): the six keyboard / joystick thunks that SETUP, PLAYER and the later programs call through the
 * driver slots 90-95. In the port the driver is built in (platform.md section 6): these are the slot functions. */
#include "frontend/fe.h"
#include "gen/module_frontend.h"
#include "gen/module_platform.h"
#include "host.h"
#include "platform/platform.h"

/* MISC 0007:000A, slot 90: INT 16h AH=1; 0 when a key is waiting, FFFFh when not */
int kbd_empty_misc(void) { return kbd_key_waiting() ? 0 : -1; }

/* MISC 0007:0005, slot 91: INT 16h AH=0, AX = scan:ASCII */
int kbd_get_misc(void) { return (int)kbd_key_get(); }

/* MISC 0007:0000, slot 92: INT 21h AH=1, waits for a key and echoes it. PORT: returns the ASCII code; an extended key gives 0 (the
 * second DOS call that would return the scan code is not made); a closed window is Ctrl-Break and gives 100h, which SETUP treats
 * as "quit" (frontend.md section 5). */
int getche_misc(void)
{
    fe_test.getche_n++;
    if (fe_test.getche) {
        int k = fe_test.getche(fe_test.getche_n);
        if (k >= 0) { tm_tty((u8)k); return k; }
    }
    for (;;) {
        if (host_quit_requested()) return 0x100;
        if (kbd_key_waiting()) break;
    }
    int ascii = kbd_key_get() & 0xFF;
    if (ascii) tm_tty((u8)ascii);
    return ascii;
}

/* MISC 0007:0029, slot 93: port 201h bit n+4 inverted, 1 = pressed */
int joy_button_misc(int n) { return ((joy_port_201() >> (n + 4)) & 1) ^ 1; }

/* MISC 0007:0018, slots 94 and 95: 0040:0017 &= F0h (the Shift, Ctrl and Alt state bits) */
void kbd_locks_clear(void) { wr8(0x40, 0x17, (u8)(rd8(0x40, 0x17) & 0xF0)); }
