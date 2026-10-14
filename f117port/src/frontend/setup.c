/* SETUP.EXE (frontend.md section 1, 8.2): the hardware questions. Text mode 80 x 25: the banner, the joystick question (with the
 * calibration steps), the sound driver menu; the answers go into the comm block. It asks on every run (no preferences file).
 * Exit code 24h = configured. The strings and the screen layout are read from the loaded image's DGROUP (SETUP 024C), as in
 * the original; the DOS/BIOS services are the text mode model of textmode.c, the MISC thunks of misc.c and the platform layer. */
#include <string.h>

#include "comm.h"
#include "frontend/fe.h"
#include "gen/module_frontend.h"
#include "gen/module_platform.h"
#include "host.h"
#include "platform/platform.h"
#include "platform/timer.h"
#include "prog.h"

static u8 puts_attr = 0x0F;                       /* cs:14BD, the attribute byte of text_puts_at (the image's initial value) */

/* SETUP 0000:0503: if the Ctrl-Break flag [0AC8] is set -> exit(0) */
void break_check(void)
{
    if (DSB(SETUP_DS_break)) setup_exit(0);
}

/* SETUP 0000:0512 (INT 10h AH=2 through bios_int4): spec frontend.md "8.2 SETUP" */
void text_gotoxy(int row, int col) { tm_gotoxy(row, col); }

/* SETUP 0000:053B: break_hook_remove, the MISC overlay freed (none in the port), timer_remove if installed, exit(code) */
void setup_exit(int code)
{
    break_hook_remove();
    if (DSB(SETUP_DS_timer_on) == 1 || timer_installed()) timer_remove();
    DSB(SETUP_DS_timer_on) = 0;
    exit_fn(code & 0xFF);
}

/* SETUP 0000:14BE: BIOS teletype writer at (row, col). AH=9 writes the char with the attribute (kept in cs:14BD), AH=0Eh advances the
 * cursor; FFh in the string means "the next byte is the attribute" (frontend.md section 1). */
void text_puts_at(int row, int col, const char *s)
{
    tm_gotoxy(row, col);
    for (const u8 *p = (const u8 *)s; *p; p++) {
        if (*p == 0xFF) {
            if (!p[1]) break;
            puts_attr = *++p;
            continue;
        }
        tm_write_cell(*p, puts_attr);
        tm_tty(*p);
    }
}

/* SETUP 0000:1509: INT 21h AH=9 on a '$'-terminated string */
void dos_puts(const char *s) { tm_dos_puts(s); }

/* SETUP 0000:0A4B: the timer tick bumps [0782]; waits until it is above n. PORT: the tick is the retrace (70.09 Hz); the original's
 * ISR counts its own divided ticks. */
static void setup_tick(void) { DSB(SETUP_DS_tick)++; }

void wait_ticks(int n)
{
    DSB(SETUP_DS_tick) = 0;
    timer_set_hook(setup_tick);
    timer_install();
    DSB(SETUP_DS_timer_on) = 1;
    while ((int)DSB(SETUP_DS_tick) <= n) host_pump();
    timer_remove();
    DSB(SETUP_DS_timer_on) = 0;
}

/* SETUP 0237:0004: INT 33h installed and answering FFFFh. PORT: the host always has a mouse, so the answer is yes. */
int mouse_present(void) { return 1; }

/* SETUP 0237:002C: read the stick, then both axes: centre := min := max := raw */
void joy_calib_center(void)
{
    joy_read_raw();
    u16 *cal = (u16 *)DSP(plat_ds()->joy_cal);              /* 20 words: min +00, max +08, centre +10, raw at +28 (words 0x14, 0x15) */
    for (int axis = 0; axis < 2; axis++) {
        u16 raw = cal[0x14 + axis];
        cal[0x00 + axis] = raw;                              /* min  (DS:0DF4 + 2*axis) */
        cal[0x04 + axis] = raw;                              /* max  (DS:0DFC) */
        cal[0x08 + axis] = raw;                              /* centre (DS:0E04) */
    }
}

/* SETUP 0237:004D: read the stick and scale both axes (widens min / max, writes the scaled bytes to DS:0E24 / 0E25) */
void joy_calib_track(void)
{
    joy_read_raw();
    joy_scale(0);
    joy_scale(1);
}

/* SETUP 0237:0115: the 28h-byte calibration block (DS:0DF4) to comm+48h */
void joy_calib_get(u16 *dst) { memcpy(dst, DSP(plat_ds()->joy_cal), 0x28); }

/* The key loop of both menus: INT 21h/01 through MISC slot 92; 100h (Ctrl-Break, closed window) and Esc quit. */
static int ask_key(void)
{
    int c = getche_misc();
    break_check();
    if (c == 0x100 || c == 0x1B) setup_exit(0);              /* PORT: Esc quits like 100h (frontend.md section 5) */
    return c;
}

/* SETUP 0000:0010 (spec frontend.md section 1). tail = F117.COM's own command tail, unused. */
int setup_main(const char *tail)
{
    (void)tail;
    Comm *comm = comm_get();
    ds_far_wr(SETUP_DS_comm_off, far_make(rd16(0, COMM_SEG_PTR), 0));        /* [FD6/FD8] = comm = *(far*)0000:04F0 */
    /* [FD0/FD2] = comm:120E (unused); misc.exe is loaded and bound here in the original (0A9F, 0BA2): the MISC slots are built in. */
    break_hook_install();                                    /* 0C35: Ctrl-Break sets [0AC8] */
    tm_set_mode3();                                          /* INT 10h AH=0, AL=3 (DS:0FC0 = 3) */

    /* the banner: rows 0-8 (row 0 only sets the attribute 1Fh: white on blue), the NOTE and the floppy text */
    static const u16 banner[9] = { 0x00C8, 0x00CB, 0x011C, 0x016D, 0x01BE, 0x020F, 0x0260, 0x02B1, 0x0302 };
    for (int row = 0; row < 9; row++) text_puts_at(row, 0, (const char *)DSP(banner[row]));
    text_puts_at(0x0A, 0x20, (const char *)DSP(0x353));
    text_puts_at(0x0C, 0x12, (const char *)DSP(0x362));
    text_puts_at(0x0E, 0x15, (const char *)DSP(0x38F));
    text_puts_at(0x0F, 0x15, (const char *)DSP(0x3B6));
    text_puts_at(0x11, 0x0F, (const char *)DSP(0x3DD));
    text_puts_at(0x12, 0x12, (const char *)DSP(0x410));
    text_puts_at(0x14, 0x02, (const char *)DSP(0x43C));
    /* 0C84 fcb_find: the floppy test. PORT: always 0 (a hard disk). Otherwise: "You cannot play F117A off of 720K floppy disks!!" (DS:0489, row 22) and exit(0). */

    comm->clearance = 1;                                     /* +22h */
    DSB(SETUP_DS_joy) = 0;
    comm->joy_calib_byte = 0;                                /* +24h (and +25h: the word write) */
    comm->pad_25 = 0;
    comm->unused_70 = 0;                                     /* +70h */
    comm->adapter = 0xFFFF;                                  /* +32h */
    comm->unused_2a = 0;                                     /* +2Ah */

    /* the joystick question (row 22) */
    tm_gotoxy(0x16, 0);
    text_puts_at(0x16, 0, (const char *)DSP(0x4BA));
    for (;;) {
        int c = ask_key();
        if (c == 'Y' || c == 'y') { comm->input_devices = 1; break; }       /* +72h = 1 */
        if (c == 'N' || c == 'n') { comm->input_devices = 0; break; }
    }
    if (comm->input_devices & 1) {
        DSB(SETUP_DS_joy) = 1;
        timer_install();                                     /* 0FC0 */
        DSB(SETUP_DS_timer_on) = 1;
        text_puts_at(0x16, 0, (const char *)DSP(0x4F6));     /* "Center joystick, then press fire button 1" */
        do { break_check(); host_pump(); } while (!joy_button_misc(0));      /* every busy-wait pumps the host (PORTING.md) */
        joy_calib_center();
        timer_remove();                                      /* 0FFE */
        DSB(SETUP_DS_timer_on) = 0;
        wait_ticks(2);
        while (joy_button_misc(0)) { break_check(); host_pump(); }   /* wait for the button to be released */
        text_puts_at(0x16, 0, (const char *)DSP(0x536));     /* "Move joystick to upper left corner, then press fire button 1" */
        joy_calib_track();
        while (!joy_button_misc(0)) { joy_calib_track(); break_check(); host_pump(); }
        text_puts_at(0x16, 0, (const char *)DSP(0x57D));     /* "Now move joystick to lower right corner, then press fire button 2" */
        joy_calib_track();
        while (!joy_button_misc(1)) { joy_calib_track(); break_check(); host_pump(); }
        DSB(SETUP_DS_joy) = comm->joy_calib_byte;            /* quirk: nothing ever wrote +24h, so this is 0 (frontend.md section 1) */
        joy_calib_get((u16 *)comm->joy_calib);
    }

    DSB(SETUP_DS_scaled_x) = DSB(SETUP_DS_scaled_x + 1) = 0x80;
    if (mouse_present()) comm->input_devices |= 2;
    char gfx[13];
    strcpy(gfx, (const char *)DSP(0x5C7));                    /* "Mgraphic.exe" */
    strcpy(comm->gfx_driver, gfx);                           /* comm+0 */

    /* the sound driver menu: blank rows 9-22, then rows 13-18 */
    const char *blank = (const char *)DSP(DSW(SETUP_DS_blank_line));
    tm_gotoxy(9, 0);
    for (int i = 1; i <= 14; i++) dos_puts(blank);
    tm_gotoxy(9, 0);
    for (int i = 0; i < 4; i++) dos_puts(blank);
    text_puts_at(13, 0, (const char *)DSP(0x5D4));           /* "Select Sound Driver:" */
    dos_puts(blank);
    text_puts_at(15, 0, (const char *)DSP(0x624));           /* "1) IBM Sound" */
    text_puts_at(16, 0, (const char *)DSP(0x673));           /* "2) Ad Lib Sound Board" */
    text_puts_at(17, 0, (const char *)DSP(0x6C2));           /* "3) Roland Sound Board" */
    text_puts_at(18, 0, (const char *)DSP(0x711));           /* "4) No Sound" */
    for (int i = 0; i < 3; i++) dos_puts(blank);
    for (;;) {
        tm_gotoxy(13, 0x31);
        int c = ask_key();
        if (c >= '1' && c <= '4') {
            char drv[13];
            strcpy(drv, (const char *)DSP(DSW(0x716 + 2 * c)));         /* Isound.117 / Asound.117 / Rsound.117 / Nsound.117 */
            strcpy(comm->snd_driver, drv);                  /* comm+0Dh */
            break;
        }
    }
    tm_gotoxy(13, 0);
    for (int i = 1; i <= 7; i++) dos_puts(blank);
    text_puts_at(16, 0x1D, (const char *)DSP(0x760));        /* "One Moment Please..." */
    comm->unused_2a = DSW(SETUP_DS_zero_word);               /* +2Ah = [0776] = 0 */
    comm->mission_state = 1;                                 /* +26h */
    comm->nonscoring = DSB(SETUP_DS_joy);                    /* +30h = joy */
    if (comm->adapter == 0xFFFF) comm->adapter = (comm->gfx_driver[0] == 'M' || comm->gfx_driver[0] == 'm') ? 3 : 2;   /* +32h */
    setup_exit(EXIT_SETUP_OK);
    return EXIT_SETUP_OK;
}
