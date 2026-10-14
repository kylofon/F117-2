/* The BIOS text-mode services SETUP uses (INT 10h AH=00/02/09/0E, INT 21h AH=09; frontend.md section 1, 8.2 bios_int4): an 80 x 25
 * screen of (char, attribute) pairs at B800:0000 in mem[] that platform/vgatext.c puts on the window. */
#include <string.h>

#include "frontend/fe.h"
#include "platform/vga.h"

static int cur_row, cur_col;

u8 *tm_buffer(void) { return mem + VGA_TEXT_BASE; }

/* INT 10h AH=00 AL=03: the BIOS clears the screen with blanks of attribute 07 and homes the cursor. */
void tm_set_mode3(void)
{
    vga_set_mode(0x03);
    u8 *b = tm_buffer();
    for (int i = 0; i < TM_ROWS * TM_COLS; i++) { b[2 * i] = 0x20; b[2 * i + 1] = 0x07; }
    cur_row = cur_col = 0;
}

void tm_gotoxy(int row, int col)
{
    cur_row = row;
    cur_col = col;
}

void tm_cursor(int *row, int *col)
{
    if (row) *row = cur_row;
    if (col) *col = cur_col;
}

void tm_write_cell(u8 ch, u8 attr)
{
    if (cur_row < 0 || cur_row >= TM_ROWS || cur_col < 0 || cur_col >= TM_COLS) return;
    u8 *c = tm_buffer() + 2 * (cur_row * TM_COLS + cur_col);
    c[0] = ch;
    c[1] = attr;
}

static void scroll_up(void)
{
    u8 *b = tm_buffer();
    memmove(b, b + 2 * TM_COLS, 2u * TM_COLS * (TM_ROWS - 1));
    for (int i = 0; i < TM_COLS; i++) { b[2 * (TM_COLS * (TM_ROWS - 1) + i)] = 0x20; b[2 * (TM_COLS * (TM_ROWS - 1) + i) + 1] = 0x07; }
}

void tm_tty(u8 ch)
{
    switch (ch) {
    case 0x07: return;                                        /* bell */
    case 0x08: if (cur_col > 0) cur_col--; return;
    case 0x0D: cur_col = 0; return;
    case 0x0A: break;                                          /* handled below: next row */
    default:
        if (cur_row >= 0 && cur_row < TM_ROWS && cur_col >= 0 && cur_col < TM_COLS)
            tm_buffer()[2 * (cur_row * TM_COLS + cur_col)] = ch;      /* the attribute stays */
        cur_col++;
        if (cur_col < TM_COLS) return;
        cur_col = 0;                                           /* wrap to the next line */
    }
    cur_row++;
    if (cur_row >= TM_ROWS) { scroll_up(); cur_row = TM_ROWS - 1; }
}

void tm_dos_puts(const char *s)
{
    for (; *s && *s != '$'; s++) tm_tty((u8)*s);
}
