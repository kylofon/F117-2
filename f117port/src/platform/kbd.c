/* Keyboard (platform.md §5.1): the XT set-1 byte stream of the host (host.h) is one IRQ 1 per byte. A program's own
 * INT 9 handler runs first (VGAME's keypad-as-stick hook, flight.md 2.4; it ends with `jmp far old INT 9`), then the
 * BIOS INT 9 model below fills the type-ahead ring at 0040:001E..003D and the shift flags at 0040:0017, as the IBM
 * AT BIOS does. Programs read it through INT 16h (bios_keyboard) or MISC.EXE slots 90/91 (kbd_key_waiting /
 * kbd_key_get): the word is scan << 8 | ascii, 0 as ascii for the extended keys. The F-117A programs use only
 * the legacy (non-enhanced) INT 16h functions 0, 1 and 2, so F11/F12 give nothing and the grey cursor block
 * returns the same words as the keypad cursor keys (E0 prefix dropped). */
#include <string.h>

#include "gen/module_platform.h"
#include "host.h"
#include "keymap.h"   /* PORT: key bindings */
#include "platform/platform.h"

#define BDA 0x40
#define SHIFT_FLAGS 0x17
#define BUF_HEAD 0x1A
#define BUF_TAIL 0x1C
#define BUF_START 0x80
#define BUF_END 0x82

enum { F_RSHIFT = 1, F_LSHIFT = 2, F_CTRL = 4, F_ALT = 8, F_SCROLL = 0x10, F_NUM = 0x20, F_CAPS = 0x40, F_INS = 0x80 };

static Int9Hook int9_hook, observer;
static bool e0_prefix;
static int e1_skip;
static bool down[256];                   /* bit 7 = grey (E0) key */

void kbd_set_int9_hook(Int9Hook h) { int9_hook = h; }
void kbd_set_observer(Int9Hook h) { observer = h; }

/* unshifted / shifted ASCII of the main block, indexed by scan code 00h..39h (0 = no ASCII) */
static const u8 ascii_n[0x3A] = {
    0, 0x1B, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
};
static const u8 ascii_s[0x3A] = {
    0, 0x1B, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, 0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13, 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
};
/* keypad 47h..53h: numeric (NumLock xor Shift) */
static const u8 pad_num[13] = { '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.' };
/* Ctrl + cursor block (legacy codes): Left 73h, Right 74h, End 75h, PgDn 76h, Home 77h, PgUp 84h */
static u16 ctrl_cursor(int sc)
{
    switch (sc) {
    case 0x4B: return 0x7300;
    case 0x4D: return 0x7400;
    case 0x4F: return 0x7500;
    case 0x51: return 0x7600;
    case 0x47: return 0x7700;
    case 0x49: return 0x8400;
    }
    return (u16)(sc << 8);
}

/* The key word for a make code; 0 = the key produces nothing. */
static u16 translate(int sc, bool ext, u8 fl)
{
    bool shift = (fl & (F_LSHIFT | F_RSHIFT)) != 0, ctrl = (fl & F_CTRL) != 0, alt = (fl & F_ALT) != 0;
    if (sc >= 0x3B && sc <= 0x44) {                                    /* F1..F10 */
        int i = sc - 0x3B;
        int base = alt ? 0x68 : ctrl ? 0x5E : shift ? 0x54 : 0x3B;
        return (u16)((base + i) << 8);
    }
    if (sc >= 0x47 && sc <= 0x53 && sc != 0x4A && sc != 0x4E) {         /* cursor block / keypad digits */
        bool numeric = !ext && (((fl & F_NUM) != 0) != shift);
        if (numeric && !ctrl && !alt) return (u16)(sc << 8 | pad_num[sc - 0x47]);
        if (alt) return 0;
        if (ctrl) return ctrl_cursor(sc);
        return (u16)(sc << 8);
    }
    if (sc == 0x4A || sc == 0x4E) return (u16)(sc << 8 | (sc == 0x4A ? '-' : '+'));   /* keypad - + (grey or not) */
    if (ext) {
        if (sc == 0x1C) return (u16)(0x1C00 | (ctrl ? 0x0A : 0x0D));   /* keypad Enter */
        if (sc == 0x35) return 0x352F;                                  /* keypad / */
        return 0;                                                       /* Print Screen etc. */
    }
    if (sc >= 0x3A || sc == 0) return 0;
    u8 a = shift ? ascii_s[sc] : ascii_n[sc];
    if (alt) {
        if (sc >= 2 && sc <= 0x0D) return (u16)((0x76 + sc) << 8);      /* Alt-1.. Alt-0 Alt-- Alt-= : 78h..83h */
        return (u16)(sc << 8);
    }
    if (ctrl) {
        if (a >= 'a' && a <= 'z') return (u16)(sc << 8 | (a & 0x1F));
        switch (sc) {
        case 0x01: return 0x011B;
        case 0x1A: return 0x1A1B;
        case 0x1B: return 0x1B1D;
        case 0x2B: return 0x2B1C;
        case 0x03: return 0x0300;                                       /* Ctrl-2 = NUL */
        case 0x07: return 0x071E;
        case 0x0C: return 0x0C1F;
        case 0x0E: return 0x0E7F;
        case 0x1C: return 0x1C0A;
        case 0x39: return 0x3920;
        case 0x0F: return 0x9400;
        }
        return (u16)(sc << 8 | a);
    }
    if (sc == 0x0F && shift) return 0x0F00;
    if ((fl & F_CAPS) && a >= 'a' && a <= 'z') a = (u8)(a - 0x20);       /* Caps Lock + letter (Shift lowers it back) */
    else if ((fl & F_CAPS) && a >= 'A' && a <= 'Z') a = (u8)(a + 0x20);
    if (a == 0 && sc != 0x01) return 0;
    return (u16)(sc << 8 | a);
}

static void buf_put(u16 w)
{
    u16 head = rd16(BDA, BUF_HEAD), tail = rd16(BDA, BUF_TAIL);
    u16 next = (u16)(tail + 2);
    if (next == rd16(BDA, BUF_END)) next = rd16(BDA, BUF_START);
    if (next == head) return;                                           /* full: the BIOS beeps and drops the key */
    wr16(BDA, tail, w);
    wr16(BDA, BUF_TAIL, next);
}

/* INT 1Bh: the program's Ctrl-Break handler, if any (break_hook_install) */
void break_isr_dispatch(void);

static void bios_int9(u8 raw)
{
    if (e1_skip) { e1_skip--; return; }
    if (raw == 0xE1) { e1_skip = 5; return; }                           /* Pause: E1 1D 45 E1 9D C5 */
    if (raw == 0xE0) { e0_prefix = true; return; }
    bool ext = e0_prefix, brk = (raw & 0x80) != 0;
    e0_prefix = false;
    int sc = raw & 0x7F;
    if (ext && (sc == 0x2A || sc == 0x36)) return;                       /* the fake shifts of Print Screen / Insert */
    u8 fl = rd8(BDA, SHIFT_FLAGS);
    bool was_down = down[(ext ? 0x80 : 0) | sc];
    down[(ext ? 0x80 : 0) | sc] = !brk;

    int bit = sc == 0x36 ? F_RSHIFT : sc == 0x2A ? F_LSHIFT : sc == 0x1D ? F_CTRL : sc == 0x38 ? F_ALT : 0;
    if (bit) { wr8(BDA, SHIFT_FLAGS, brk ? (u8)(fl & ~bit) : (u8)(fl | bit)); return; }
    if (brk) return;
    if (sc == 0x3A && !ext) { if (!was_down) wr8(BDA, SHIFT_FLAGS, fl ^ F_CAPS); return; }
    if (sc == 0x45 && !ext) { if (!was_down) wr8(BDA, SHIFT_FLAGS, fl ^ F_NUM); return; }
    if (sc == 0x46) {
        if (ext && (fl & F_CTRL)) { buf_put(0); break_isr_dispatch(); return; }   /* Ctrl-Break */
        if (!ext && !was_down) wr8(BDA, SHIFT_FLAGS, fl ^ F_SCROLL);
        return;
    }
    if (sc == 0x52 && !was_down) wr8(BDA, SHIFT_FLAGS, fl ^ F_INS);
    u16 w = translate(sc, ext, rd8(BDA, SHIFT_FLAGS));
    if (w) buf_put(w);
}

void kbd_host_byte(u8 raw)
{
    timer_poll_bios_clock();                                            /* the hook reads 0040:006C */
    if (observer) observer(raw);
    if (int9_hook) int9_hook(raw);
    bios_int9(raw);
}

/* Keys released outside the window never send their break code: release every key that is down. */
static void focus_lost(void)
{
    keymap_focus_lost();                                  /* PORT: key bindings */
    for (int i = 0; i < 256; i++) {
        if (!down[i]) continue;
        if (i & 0x80) kbd_host_byte(0xE0);
        kbd_host_byte((u8)((i & 0x7F) | 0x80));
    }
}

void kbd_init(void)
{
    e0_prefix = false;
    e1_skip = 0;
    memset(down, 0, sizeof down);
    int9_hook = observer = NULL;
    wr8(BDA, SHIFT_FLAGS, 0);
    wr16(BDA, BUF_HEAD, 0x1E);
    wr16(BDA, BUF_TAIL, 0x1E);
    wr16(BDA, BUF_START, 0x1E);
    wr16(BDA, BUF_END, 0x3E);
    host_set_kbd_handler(kbd_host_byte);
    host_set_focus_lost_handler(focus_lost);
}

void kbd_flush(void) { wr16(BDA, BUF_HEAD, rd16(BDA, BUF_TAIL)); }

/* A test aid (S5.4 shell test): a key word (scan << 8 | ascii) into the BIOS type-ahead buffer as if it had been typed. */
void kbd_inject(u16 word) { buf_put(word); }

bool kbd_key_waiting(void)
{
    host_pump();
    return rd16(BDA, BUF_HEAD) != rd16(BDA, BUF_TAIL);
}

static u16 buf_take(void)
{
    u16 head = rd16(BDA, BUF_HEAD);
    u16 w = rd16(BDA, head);
    head = (u16)(head + 2);
    if (head == rd16(BDA, BUF_END)) head = rd16(BDA, BUF_START);
    wr16(BDA, BUF_HEAD, head);
    return w;
}

u16 kbd_key_get(void)
{
    while (!kbd_key_waiting()) {
        if (host_quit_requested()) return 0xFFFF;                       /* PORT: the window was closed while waiting */
    }
    return buf_take();
}

/* MPS_LOGO 0146:0724 / VGAME F360 (PLAYER, MPS_LOGO and VGAME's copy of MISC slots 90/91): INT 16h with AH = cmd.
 * cmd 0 = wait for a key, 1 = peek (0 when none), 2 = shift flags. A key word of 0 (Ctrl-Break) is returned as
 * 0FFFFh (the wrapper does `dec ax` when AX is 0, except for cmd 2). */
int bios_keyboard(int cmd)
{
    int dl = cmd & 0x0F;
    if (dl == 2) return rd8(BDA, SHIFT_FLAGS);
    u16 ax;
    if (dl == 1) {
        if (!kbd_key_waiting()) return 0;
        ax = rd16(BDA, rd16(BDA, BUF_HEAD));
    } else if (dl == 0) {
        ax = kbd_key_get();
    } else {
        return 0;
    }
    return (s16)(ax == 0 ? 0xFFFF : ax);        /* a 16-bit int, as the callers' C compares it (F-keys above 8000h are negative) */
}
