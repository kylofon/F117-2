#pragma once
/* The front end (S5.3, port/spec/frontend.md): SETUP, MPS_LOGO, PLAYER, DSWAP and the MISC driver. The prototypes of the
 * functions the spec names are in gen/module_frontend.h; this header holds what the port adds around them: the DGROUP
 * offsets the ported code reads (the loaded image keeps the strings and tables, PORTING.md "DGROUP offsets"), the text
 * mode model, the sound call points and the developer/test hooks of the tests in fetest.c. */
#include "mem.h"
#include "types.h"

/* ---- Text mode 03h (textmode.c): the BIOS teletype and the DOS console of SETUP ------------------------------ */
void tm_set_mode3(void);                      /* INT 10h AH=00 AL=03: mode set, screen cleared to 20h/07h, cursor home */
void tm_gotoxy(int row, int col);             /* INT 10h AH=02 */
void tm_write_cell(u8 ch, u8 attr);           /* INT 10h AH=09 count 1 at the cursor (the cursor does not move) */
void tm_tty(u8 ch);                           /* INT 10h AH=0Eh: the char at the cursor keeping its attribute, the cursor advances, CR LF BS BEL, scroll */
void tm_dos_puts(const char *s);              /* INT 21h AH=09: '$'-terminated, through the teletype */
void tm_cursor(int *row, int *col);
u8  *tm_buffer(void);                         /* B800:0000 as 80 x 25 (char, attribute) pairs */
#define TM_ROWS 25
#define TM_COLS 80

/* ---- Sound: the call points of the front end (sndstub.c). S5.9 fills them in; all of them are no-ops until then ---------- */
int  fe_snd_init(unsigned speech_seg);        /* ASOUND.117 slot 100 snd_init (sound.md "6") */
int  fe_snd_cmd(int cmd, int arg);            /* slot 101 snd_cmd: PLAYER cue 3 = intro soundtrack, 0 = stop, 2 = "voices active?" (returns 0 when silent) */
void fe_snd_shutdown(void);                   /* slot 102 */
void fe_snd_poll_skipped(void);                   /* a skipped tick: only snd_noise in the fast state */
int  fe_snd_poll(void);                       /* slot 103 snd_poll: called by the timer ISR body; 0 = nothing, <0 / >0 = change the tick divisor (S5.9) */

/* ---- Developer / test hooks (fetest.c sets them; all NULL / 0 in a normal run) ------------------------------------------- */
typedef struct FeTest {
    int getche_n;                             /* SETUP: number of getche calls so far (the tests reset it) */
    int (*getche)(int n);                     /* SETUP: called before the n-th getche (1-based); returns a key (>= 0) or -1 to wait for the keyboard */
    bool logo_nowait;                         /* MPS_LOGO: do not wait for the BIOS clock between frames */
    void (*logo_frame)(int frame);            /* MPS_LOGO: after ss_draw of frame (1-based) */
    bool player_fast;                         /* PLAYER: no fade, one tick per frame, poll_input runs the tick callback itself */
    unsigned player_ring_paras;               /* PLAYER: use a ring buffer of this many paragraphs (>= 2000h) instead of all free memory */
    void (*player_cel)(int file, int cel);    /* PLAYER: after cel `cel` of the file `file` (0-based) was drawn */
    unsigned long player_callbacks;           /* PLAYER: how often the tick callback ran its body (not held) */
    unsigned long player_isr_ticks;           /* PLAYER: how often the ISR body was entered (every host tick) */
} FeTest;
extern FeTest fe_test;

/* ---- Entry points of the self test (fetest.c): f117 --frontend-test REFDIR  (77 = no game files) -------------------------- */
int frontend_selftest(const char *refdir);

/* ---- DGROUP offsets of the programs (read through DSB/DSW/DSL of the loaded image; names after the spec) -------------------- */
/* SETUP (DGROUP 024C) */
#define SETUP_DS_comm_off      0x0FD6         /* far pointer to the comm block (offset, segment) */
#define SETUP_DS_joy           0x0FD4         /* byte: the joystick flag written to comm+30h */
#define SETUP_DS_break         0x0AC8         /* byte: Ctrl-Break seen */
#define SETUP_DS_tick          0x0782         /* byte: counted by the timer tick, wait_ticks() */
#define SETUP_DS_timer_on      0x0E29         /* byte: timer_install() was called */
#define SETUP_DS_blank_line    0x0780         /* word: pointer to the blank line string (DS:006E, '$'-terminated) */
#define SETUP_DS_zero_word     0x0776         /* word, 0: copied to comm+2Ah */
#define SETUP_DS_snd_names     0x0778         /* words: pointers to the sound driver names Isound/Asound/Rsound/Nsound (keys '1'..'4' index 0x716 + 2*key) */
#define SETUP_DS_scaled_x      0x0E24         /* bytes: the scaled stick axes, reset to 80h */
/* PLAYER (DGROUP 02A3) */
#define PLAYER_DS_filebuf      0x01B8         /* 4 KB file buffer of the PAN bit reader */
#define PLAYER_DS_fileptr      0x01B6
#define PLAYER_DS_fileend      0x11B7         /* last byte of the buffer */
#define PLAYER_DS_handle       0x11B8
#define PLAYER_DS_bits         0x11BA         /* bit reader: current control word */
#define PLAYER_DS_bitsleft     0x11BC         /* bit reader: bits left + 1 (0 = refill) */
#define PLAYER_DS_eof          0x11BE         /* byte: the chunk ended with the control byte 0 */
#define PLAYER_DS_fade_idx     0x13CE
#define PLAYER_DS_fade_cnt     0x13D0
#define PLAYER_DS_fade_meas    0x13D2
#define PLAYER_DS_fade_steps   0x13D4
#define PLAYER_DS_fade_factor  0x13D6
#define PLAYER_DS_fade_step    0x13D8
#define PLAYER_DS_pal          0x13DA         /* 768 bytes: the target palette of the fade */
#define PLAYER_DS_slice        0x16DA         /* 768 bytes: the next slice of the faded palette */
#define PLAYER_DS_ncels        0x1DE8
#define PLAYER_DS_countdown    0x1DEA
#define PLAYER_DS_frame_ticks  0x1DEC
#define PLAYER_DS_tick_counter 0x1DEE
#define PLAYER_DS_idle         0x1DF0
#define PLAYER_DS_buf_end      0x1E60         /* dword E: end of the ring buffer (linear address) */
#define PLAYER_DS_cels_shown   0x1E64
#define PLAYER_DS_wrap_head    0x1E66         /* dword W */
#define PLAYER_DS_buf_start    0x1E6A         /* dword S */
#define PLAYER_DS_tail         0x1E6E         /* dword T: the cel to be shown next */
#define PLAYER_DS_cel_in_file  0x1E72
#define PLAYER_DS_old_mode     0x1E74
#define PLAYER_DS_old_1b       0x1E76
#define PLAYER_DS_hold         0x1E7A
#define PLAYER_DS_mseg         0x207C
#define PLAYER_DS_sizes        0x207E         /* dword: linear address of the cel size table (copied to the ring's front) */
#define PLAYER_DS_write        0x2082         /* dword A: write position */
#define PLAYER_DS_prev_head    0x2086         /* dword P */
#define PLAYER_DS_comm         0x208A         /* far pointer to the comm block */
#define PLAYER_DS_old_23       0x208E
#define PLAYER_DS_break        0x2092         /* word: Ctrl-Break seen (3) */
#define PLAYER_DS_head         0x2094         /* dword H: end of the last complete cel */
#define PLAYER_DS_timer_on     0x1A1C
#define PLAYER_DS_div0         0x1A0E
#define PLAYER_DS_class        0x1A10
#define PLAYER_DS_skip         0x1A1E         /* byte: the 7th-tick counter of timer_step */
/* MPS_LOGO (DGROUP 0332) */
#define LOGO_DS_fast           0x0E76         /* word: 1 while the animation is paced by the BIOS clock */
#define LOGO_DS_ss_error       0x0C74         /* word: the last ss_load error */
#define LOGO_DS_base_table     0x004A         /* bytes: first DAC slot of the series by mode byte (01 51 1F 38 1F 01 2E ...) */
