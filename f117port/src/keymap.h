#pragma once
/* PORT: key bindings (keymap.c; the launcher's Key Bindings dialog).
 * A key is an XT set-1 make code, KEYMAP_GREY = sent after an E0 prefix, with the modifiers held with it. */
#include "types.h"

#define KEYMAP_GREY  0x0100
#define KEYMAP_SHIFT 0x1000
#define KEYMAP_CTRL  0x2000
#define KEYMAP_ALT   0x4000

/* main.c --keys "name=code,...": the actions not on their default keys (code 0 = no key). False if the
 * list can't be read. */
bool keymap_parse(const char *spec);
/* host.c key_event: key x went down / up. Returns -1 to send x as it is, or the number of keys put in
 * out[] (at most 16; 0x80 = break, KEYMAP_GREY = E0 prefix) to send instead. */
int keymap_map(u16 x, bool down, u16 *out);
void keymap_focus_lost(void);          /* kbd.c: every key counts as up */
int keymap_selftest(void);             /* --keymap-test: 0 = all checks passed */
