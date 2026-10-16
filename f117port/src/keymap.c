/* PORT: key bindings (the launcher's Key Bindings dialog); not in the original, which has fixed keys.
 *
 * Keys are XT set-1 make codes as host.c sends them (KEYMAP_GREY = sent after an E0 prefix) with the
 * modifiers held with them (KEYMAP_SHIFT / CTRL / ALT). While VGAME runs, a key bound to an action is sent
 * to the game as the action's own key (with the modifiers that key needs); a key that is the default of
 * an action bound elsewhere is dropped; every other key goes through as it is. Every other program (menus, typed names)
 * always gets the keys as they are. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keymap.h"
#include "prog.h"

#define SHIFT KEYMAP_SHIFT
#define CTRL  KEYMAP_CTRL
#define ALT   KEYMAP_ALT
#define GREY  KEYMAP_GREY
#define MODS  (SHIFT | CTRL | ALT)

/* The launcher's list (launcher/keys.cpp): the same names and defaults. Every key is a key the original reads
 * from the BIOS (simflow.md Part 2 section 2.1 for 1E9E, Part 10 appendix A for CBA0; the stick keys are the
 * arrows of the keypad-as-stick hook, flight.md 2.4). */
typedef struct {
    const char *name;
    u16 def;          /* the key it has by default */
    u16 game;         /* what the game is sent */
    u16 key;          /* the key it has now (0 = none) */
} Action;

static Action actions[] = {
    /* flying */
    { "stick_up",        GREY | 0x48,  GREY | 0x48,  0 },
    { "stick_down",      GREY | 0x50,  GREY | 0x50,  0 },
    { "stick_left",      GREY | 0x4B,  GREY | 0x4B,  0 },
    { "stick_right",     GREY | 0x4D,  GREY | 0x4D,  0 },
    { "throttle_down",   0x0C,         0x0C,         0 },
    { "throttle_up",     0x0D,         0x0D,         0 },
    { "throttle_full",   SHIFT | 0x0D, SHIFT | 0x0D, 0 },
    { "throttle_idle",   SHIFT | 0x0C, SHIFT | 0x0C, 0 },
    { "wheel_brakes",    0x0B,         0x0B,         0 },
    { "gear",            0x07,         0x07,         0 },
    { "bay_doors",       0x09,         0x09,         0 },
    { "autopilot",       0x08,         0x08,         0 },
    { "flaps",           0x0A,         0x0A,         0 },
    { "time_fast",       SHIFT | 0x2C, SHIFT | 0x2C, 0 },
    { "time_normal",     SHIFT | 0x2D, SHIFT | 0x2D, 0 },
    { "kbd_sensitivity", GREY | 0x52,  GREY | 0x52,  0 },
    { "eject",           SHIFT | 0x44, SHIFT | 0x44, 0 },
    /* weapons and countermeasures */
    { "weapon_select",   0x39,         0x39,         0 },
    { "gun",             0x0E,         0x0E,         0 },
    { "release",         0x1C,         0x1C,         0 },
    { "flare",           0x02,         0x02,         0 },
    { "chaff",           0x03,         0x03,         0 },
    { "decoy",           0x06,         0x06,         0 },
    { "flare_mode",      0x04,         0x04,         0 },
    { "chaff_mode",      0x05,         0x05,         0 },
    { "drop_targets",    0x30,         0x30,         0 },
    { "master_mode",     0x3C,         0x3C,         0 },
    /* views */
    { "view_front",      0x3B,         0x3B,         0 },
    { "view_clean",      SHIFT | 0x35, SHIFT | 0x35, 0 },
    { "view_left",       SHIFT | 0x32, SHIFT | 0x32, 0 },
    { "view_right",      SHIFT | 0x33, SHIFT | 0x33, 0 },
    { "view_rear",       SHIFT | 0x34, SHIFT | 0x34, 0 },
    { "view_chase",      SHIFT | 0x3B, SHIFT | 0x3B, 0 },
    { "view_delayed",    SHIFT | 0x3C, SHIFT | 0x3C, 0 },
    { "view_side",       SHIFT | 0x3D, SHIFT | 0x3D, 0 },
    { "view_weapon",     SHIFT | 0x3E, SHIFT | 0x3E, 0 },
    { "view_target",     SHIFT | 0x3F, SHIFT | 0x3F, 0 },
    { "view_target_far", SHIFT | 0x40, SHIFT | 0x40, 0 },
    { "zoom_in",         0x2C,         0x2C,         0 },
    { "zoom_out",        0x2D,         0x2D,         0 },
    /* displays and game */
    { "look_left",       0x32,         0x32,         0 },
    { "look_right",      0x33,         0x33,         0 },
    { "look_rear",       0x34,         0x34,         0 },
    { "look_ahead",      0x35,         0x35,         0 },
    { "sensor_zoom_in",  0x1B,         0x1B,         0 },
    { "sensor_zoom_out", 0x1A,         0x1A,         0 },
    { "mfd_left",        0x3D,         0x3D,         0 },
    { "mfd_weapons",     0x3F,         0x3F,         0 },
    { "mfd_nav",         0x41,         0x41,         0 },
    { "mfd_nav_edit",    0x42,         0x42,         0 },
    { "mfd_page",        0x44,         0x44,         0 },
    { "flir",            0x40,         0x40,         0 },
    { "hud_mode",        0x2F,         0x2F,         0 },
    { "hud_style",       0x2E,         0x2E,         0 },
    { "pause",           ALT | 0x19,   ALT | 0x19,   0 },
    { "sound",           ALT | 0x2F,   ALT | 0x2F,   0 },
};
#define NACTIONS ((int)(sizeof actions / sizeof actions[0]))

static u16 mods;               /* the modifiers held now (every modifier key goes through) */
static u8 held_shift, held_ctrl, held_alt;   /* bit 0 = left, bit 1 = right */
/* per physical key (x & 0x1FF) while it is down: the game key sent for it, PASS = the key itself,
 * DROP = nothing; NONE = up, or down since before AP */
#define NONE 0xFFFF
#define PASS 0xFFFE
#define DROP 0
static u16 sent[0x200];
static bool enabled;           /* --keys was given: without it every key goes through untouched */
static bool force_flight;      /* the self-test: act as if VGAME were running */

static bool flight_active(void)
{
    return force_flight || prog_current_id() == (int)PROG_VGAME;
}

static void init_once(void)
{
    static bool done;
    if (done) return;
    done = true;
    for (int i = 0; i < NACTIONS; i++) actions[i].key = actions[i].def;
    for (int i = 0; i < 0x200; i++) sent[i] = NONE;
}

bool keymap_parse(const char *spec)
{
    init_once();
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *item = strtok(buf, ","); item; item = strtok(NULL, ",")) {
        char *eq = strchr(item, '=');
        if (!eq) return false;
        *eq = 0;
        char *end;
        unsigned long code = strtoul(eq + 1, &end, 0);
        if (*end || code > 0xFFFF || (code & ~(u16)(MODS | GREY | 0x7F))) return false;
        int i = 0;
        while (i < NACTIONS && strcmp(actions[i].name, item)) i++;
        if (i == NACTIONS) return false;
        actions[i].key = (u16)code;
        enabled = true;
    }
    return true;
}

static int push(u16 *out, int n, u16 key, bool down)
{
    out[n] = (u16)((key & (GREY | 0x7F)) | (down ? 0 : 0x80));
    return n + 1;
}

/* Keeps track of the modifiers; returns true for a modifier key. */
static bool modifier(u16 x, bool down)
{
    u8 *held = NULL, side = (x & GREY) ? 2 : 1;
    switch (x) {
    case 0x2A: held = &held_shift; side = 1; break;
    case 0x36: held = &held_shift; side = 2; break;
    case 0x1D: case GREY | 0x1D: held = &held_ctrl; break;
    case 0x38: case GREY | 0x38: held = &held_alt; break;
    default: return false;
    }
    if (down) *held |= side; else *held &= (u8)~side;
    mods = (u16)((held_shift ? SHIFT : 0) | (held_ctrl ? CTRL : 0) | (held_alt ? ALT : 0));
    return true;
}

/* The key `game` pressed with exactly its own modifiers: the ones held that it doesn't want are let
 * go around it, the ones it wants that aren't held are pressed around it (the game reads the
 * modifiers when the key goes down). */
static int press(u16 *out, u16 game)
{
    const u16 want = game & MODS;
    int n = 0;
    static const struct { u16 bit; u16 left, right; const u8 *held; } M[] = {
        { SHIFT, 0x2A, 0x36,        &held_shift },
        { CTRL,  0x1D, GREY | 0x1D, &held_ctrl },
        { ALT,   0x38, GREY | 0x38, &held_alt },
    };
    for (int i = 0; i < 3; i++) {
        if ((want & M[i].bit) && !*M[i].held) n = push(out, n, M[i].left, true);
        if (!(want & M[i].bit) && (*M[i].held & 1)) n = push(out, n, M[i].left, false);
        if (!(want & M[i].bit) && (*M[i].held & 2)) n = push(out, n, M[i].right, false);
    }
    n = push(out, n, game, true);
    for (int i = 0; i < 3; i++) {
        if ((want & M[i].bit) && !*M[i].held) n = push(out, n, M[i].left, false);
        if (!(want & M[i].bit) && (*M[i].held & 1)) n = push(out, n, M[i].left, true);
        if (!(want & M[i].bit) && (*M[i].held & 2)) n = push(out, n, M[i].right, true);
    }
    return n;
}

int keymap_map(u16 x, bool down, u16 *out)
{
    init_once();
    if (!enabled) return -1;
    if (modifier(x, down)) return -1;
    u16 *s = &sent[x & 0x1FF];
    if (!down) {                                   /* the key goes up: as it went down */
        u16 was = *s;
        *s = NONE;
        if (was == NONE || was == PASS) return -1;
        return was == DROP ? 0 : push(out, 0, was, false);
    }
    if (*s == PASS) return -1;                     /* typematic repeat */
    if (*s != NONE && *s != DROP) return press(out, *s);
    if (!flight_active()) return -1;
    const u16 key = (u16)(x | mods);
    for (int i = 0; i < NACTIONS; i++)
        if (actions[i].key == key) {
            *s = actions[i].game;
            return press(out, actions[i].game);
        }
    for (int i = 0; i < NACTIONS; i++)
        if (actions[i].def == key && actions[i].key != key) {
            *s = DROP;                             /* its action moved to another key */
            return 0;
        }
    *s = PASS;
    return -1;
}

void keymap_focus_lost(void)
{
    init_once();
    held_shift = held_ctrl = held_alt = 0;
    mods = 0;
    for (int i = 0; i < 0x200; i++) sent[i] = NONE;
}

/* ---------------------------------------------------------------- self-test (--keymap-test) */

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { failures++; fprintf(stderr, "keymap-test FAILED: %s\n", what); } } while (0)

static bool same(const u16 *got, int n, const u16 *want, int m)
{
    return n == m && !memcmp(got, want, (size_t)m * sizeof(u16));
}

int keymap_selftest(void)
{
    u16 out[16];
    int n;
    force_flight = true;

    /* no --keys: every key goes through, whatever the table holds */
    CHECK(keymap_map(0x22, true, out) == -1 && keymap_map(0x22, false, out) == -1, "without --keys keys pass");
    CHECK(keymap_map(0x07, true, out) == -1 && keymap_map(0x07, false, out) == -1, "without --keys defaults pass");

    /* the list */
    CHECK(!keymap_parse("nonsense=0x22"), "unknown action refused");
    CHECK(!keymap_parse("gear"), "missing = refused");
    CHECK(!keymap_parse("gear=0x80"), "bad code (bit 7) refused");
    CHECK(!keymap_parse("gear=0x22x"), "trailing junk refused");
    CHECK(!keymap_parse("gear=0x8000"), "stray bit refused");
    CHECK(!enabled, "a refused list changes nothing");
    CHECK(keymap_parse(""), "empty list accepted");
    CHECK(!enabled, "an empty list changes nothing");

    CHECK(keymap_parse("gear=0x22,throttle_full=0x24,flare=0x4025,chaff=0,stick_up=0x148"), "list accepted");
    CHECK(enabled, "--keys switches the map on");
    CHECK(prog_current_id() < 0, "no program runs in the test");

    /* G now lowers the gear: '6' (scan 07) down and up */
    n = keymap_map(0x22, true, out);
    CHECK(same(out, n, (u16[]){ 0x07 }, 1), "G sends the gear key");
    n = keymap_map(0x22, false, out);
    CHECK(same(out, n, (u16[]){ 0x87 }, 1), "G up sends the gear key up");
    /* the gear's own key is free now: dropped, make and break */
    CHECK(keymap_map(0x07, true, out) == 0 && keymap_map(0x07, false, out) == 0, "the old gear key does nothing");
    /* a key nobody uses goes through */
    CHECK(keymap_map(0x1E, true, out) == -1 && keymap_map(0x1E, false, out) == -1, "other keys pass");
    /* a repeat of a mapped key sends the key again */
    n = keymap_map(0x22, true, out);
    n = keymap_map(0x22, true, out);
    CHECK(same(out, n, (u16[]){ 0x07 }, 1), "typematic repeat of a mapped key");
    keymap_map(0x22, false, out);

    /* an action whose key needs Shift: Shift is pressed around it */
    n = keymap_map(0x24, true, out);
    CHECK(same(out, n, (u16[]){ 0x2A, 0x0D, 0xAA }, 3), "J = full throttle sends Shift + '='");
    n = keymap_map(0x24, false, out);
    CHECK(same(out, n, (u16[]){ 0x8D }, 1), "J up sends '=' up");

    /* modifiers held with the bound key are let go around the game key (Alt+F = flare = '1') */
    CHECK(keymap_map(0x38, true, out) == -1, "Alt goes through");
    n = keymap_map(0x25, true, out);
    CHECK(same(out, n, (u16[]){ 0xB8, 0x02, 0x38 }, 3), "Alt+K sends '1' without Alt");
    n = keymap_map(0x25, false, out);
    CHECK(same(out, n, (u16[]){ 0x82 }, 1), "Alt+K up");
    CHECK(keymap_map(0x38, false, out) == -1, "Alt up goes through");

    /* no key: the default key of the action does nothing; an arrow-key stick moves to the E0 key bound */
    CHECK(keymap_map(0x03, true, out) == 0 && keymap_map(0x03, false, out) == 0, "chaff with no key: '2' dropped");
    n = keymap_map(0x148, true, out);          /* Up arrow is still stick_up (bound to its default) */
    CHECK(same(out, n, (u16[]){ 0x148 }, 1), "grey key keeps its E0 prefix");
    keymap_map(0x148, false, out);

    /* losing the focus forgets what was down */
    keymap_map(0x22, true, out);
    keymap_focus_lost();
    n = keymap_map(0x22, false, out);
    CHECK(n == -1, "after focus loss a stale break passes");

    force_flight = false;
    if (failures) return 1;
    printf("keymap-test OK\n");
    return 0;
}
