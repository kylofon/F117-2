/* END's copies of the START toolkit whose state lives at END's own DGROUP offsets (spec debrief.md section 10): the exit and the Ctrl-Break test, the
 * cursor and the input engine, the menu loop, the waits, the palette queue and the fades, the screen loader, the Alt-Q prompt. The stateless toolkit
 * functions (text, wrapped text, centred text, sprite draw, wait_ticks, the file loaders) are START's, reused from src/shell (ui_text ... in module_shell.h).
 * The algorithms are the ones of src/shell/ui.c (compared instruction by instruction in S3.9); only the offsets differ:
 *   cursor x/y/old x/old y/sprite/w/h   START E08A.. = END 7214, 7216, 7218, 721A, 7212, 721C, 721D     palette buffer 64E3 = 1F25
 *   pal queue 64A6/64E2 = 1EE8/1F24    fade calibration B304 = 55BA     last key B306 = 55BC     joystick bytes 69B2/69B3 = 23F4/23F5
 *   contexts DC02/DC18 = 6D8C/6DA2 (the cursor is clamped to the window of context 1) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debrief/debrief.h"

EndTest end_test;

#define E_CUR_SEG  0x7212
#define E_CUR_X    0x7214
#define E_CUR_Y    0x7216
#define E_CUR_OX   0x7218
#define E_CUR_OY   0x721A
#define E_CUR_W    0x721C
#define E_CUR_H    0x721D
#define E_PAL      0x1F25
#define E_PALQ     0x1EE8
#define E_PALQ_N   0x1F24

void end_snd(int cmd) { fe_snd_cmd(cmd, 0); }                /* slot 101 snd_cmd */

/* the ISR body of END (4C55..4D51): the tick flag the palette queue waits for (DS:50CC) and the sound poll */
void end_tick(void)
{
    DSB(0x50CC)++;
    if (++DSB(0x50CD) == 7) { DSB(0x50CD) = 0; fe_snd_poll_skipped(); }       /* 4D51: every 7th tick skips the poll (60.08 Hz at 70.09 Hz) */
    else fe_snd_poll();                                      /* slot 103 snd_poll, then snd_noise in the fast state */
    if (host_quit_requested()) end_exit(0);                  /* PORT: a closed window ends END wherever it waits */
}

/* 0000:3F44 end_exit */
void end_exit(int code)
{
    end_snd(0);
    if (DSW(0x7220)) mem_free_chk(DSW(0x7220));
    if (DSB(0x2608)) break_hook_remove();
    if (DSB(0x50CB)) timer_remove();
    if ((s16)code == 0) video_set_mode(3);
    kbd_locks_clear();
    exit_fn(code);
}

/* 0000:2023 check_break */
void check_break(void)
{
    if (DSB(0x2608) || host_quit_requested()) end_exit(0);
}

/* ---------------------------------------------------------------------------------------------------------------- the cursor */

/* 0000:1C3B hit_test: rects of 4 words (x0, y0, x1, y1), unsigned compares, 1-based hit or 0 */
int hit_test(void *rects, int n)
{
    u8 *r = rects;
    u16 cx = DSW(E_CUR_X), cy = DSW(E_CUR_Y);
    int i;
    for (i = 0; (s16)i < (s16)n; i++, r += 8) {
        if (PW(r, 0) > cx) continue;
        if (PW(r, 4) < cx) continue;
        if (PW(r, 2) > cy) continue;
        if (PW(r, 6) < cy) continue;
        break;
    }
    return (s16)i < (s16)n ? i + 1 : 0;
}

/* the clean working page over the screen where the cursor was drawn (unreferenced code at 1C87) */
static void cursor_erase(void)
{
    blit_rect(1, (s16)DSW(E_CUR_OX), (s16)DSW(E_CUR_OY), 0, (s16)DSW(E_CUR_OX), (s16)DSW(E_CUR_OY), DSB(E_CUR_W), DSB(E_CUR_H));
}

/* 0000:1FE5 cursor_init */
void cursor_init(unsigned seg)
{
    DSW(E_CUR_SEG) = (u16)seg;
    DSB(E_CUR_W) = rd8((u16)seg, 0);
    DSB(E_CUR_H) = rd8((u16)seg, 2);
    DSB(0x721E) = 2;
    DSB(0x721F) = 2;
    DSW(E_CUR_OY) = 0;
    DSW(E_CUR_Y) = 0;
    DSW(E_CUR_OX) = 0;
    DSW(E_CUR_X) = 0;
}

/* 0000:2049 cursor_set_pos */
void cursor_set_pos(int x, int y)
{
    DSW(E_CUR_X) = (u16)x;
    DSW(E_CUR_Y) = (u16)y;
    if (CW(0x72) & 2) mouse_set_pos();
}

/* 0000:1CF5 poll_input: the input and cursor engine of the menu: joystick and mouse buttons, the key word, the arrow keys that jump to the next button
 * rectangle, the clamp to context 1's window, the cursor sprite. Returns 1 (Enter / button 0), 2 (Esc / button 1), 62h (Del / Backspace) or the key. */
int end_poll_input(void *mv)
{
    u8 *m = mv;
    Comm *comm = comm_get();
    if (end_test.on_poll) { end_test.polls++; end_test.on_poll(end_test.polls); }
    int a = 0, b = 0, key = 0;
    int old_x = 0, old_y = 0;
    if (comm->input_devices & 1) { a = joy_button_misc(0); b = joy_button_misc(1); }
    if (comm->input_devices & 2) {
        mouse_poll();
        old_x = (s16)DSW(E_CUR_X);
        old_y = (s16)DSW(E_CUR_Y);
        a |= mouse_button(0);
        b |= mouse_button(1);
    }
    if (kbd_empty_misc() == 0) key = (u16)kbd_get_misc();
    else if (a) key = 0x0D;
    else if (b) key = 0x1B;
    switch (key) {
    case 0:
        if (comm->input_devices & 1) {
            DSW(E_CUR_X) = (u16)(DSW(E_CUR_X) + ((int)DSB(0x23F4) - 0x80) / 0x1E);
            DSW(E_CUR_Y) = (u16)(DSW(E_CUR_Y) + ((int)DSB(0x23F5) - 0x80) / 0x1E);
        }
        break;
    case 0x1000: quit_prompt(0); break;                       /* (B) 0: the screen is not saved */
    case 0x4800: case 0x4B00: case 0x4D00: case 0x5000: {
        int best = 1000, bx = -1, by = 0;
        int n = m[3];
        u8 *r = m + 4;
        for (int i = 0; i < n; i++, r += 8) {
            int cx = (u16)(PW(r, 4) + PW(r, 0)) >> 1, cy = (u16)(PW(r, 2) + PW(r, 6)) >> 1;
            int mx = (s16)DSW(E_CUR_X), my = (s16)DSW(E_CUR_Y);
            bool cand = false;
            int adx = abs(mx - cx), ady = abs(my - cy);
            switch (key) {
            case 0x4800: cand = my > cy && ady >= adx; break;
            case 0x5000: cand = my < cy && ady >= adx; break;
            case 0x4B00: cand = mx > cx && ady <= adx; break;
            case 0x4D00: cand = mx < cx && ady <= adx; break;
            }
            if (cand) {
                int dd = dist_approx(mx, my, cx, cy);
                if (dd < best) { best = dd; bx = cx; by = cy; }
            }
        }
        if (bx != -1) { DSW(E_CUR_X) = (u16)bx; DSW(E_CUR_Y) = (u16)by; }
        break;
    }
    default: break;
    }
    const TextCtx *c1 = tctx(END_CTX1);
    if ((s16)DSW(E_CUR_X) < c1->xmin) DSW(E_CUR_X) = (u16)c1->xmin;
    else if ((s16)DSW(E_CUR_X) > c1->xmax) DSW(E_CUR_X) = (u16)c1->xmax;
    if ((s16)DSW(E_CUR_Y) < c1->ymin) DSW(E_CUR_Y) = (u16)c1->ymin;
    else if ((s16)DSW(E_CUR_Y) > c1->ymax) DSW(E_CUR_Y) = (u16)c1->ymax;
    if ((comm->input_devices & 2) && ((s16)DSW(E_CUR_X) != old_x || (s16)DSW(E_CUR_Y) != old_y)) mouse_set_pos();
    cursor_erase();
    ui_sprite_draw(tctx(END_CTX0), (s16)DSW(E_CUR_X), (s16)DSW(E_CUR_Y), DSW(E_CUR_SEG));
    DSW(E_CUR_OX) = DSW(E_CUR_X);
    DSW(E_CUR_OY) = DSW(E_CUR_Y);
    check_break();
    int result = 0;
    int k = key;
    if (k == 0x5300) {
        result = 0x62;
    } else {
        k &= 0xFF;
        if (k == 8) result = 0x62;
        else if (k == 0x0D && DSW(0x55BC) != 0x0D) result = 1;
        else if (k == 0x1B && DSW(0x55BC) != 0x1B) result = 2;
    }
    DSW(0x55BC) = (u16)k;
    if (result == 0) result = k;
    return result;
}

/* 0000:1BAA menu_loop: returns the button 1..n, 63h for Esc / the right button, 100h + key for the hot key. The descriptor: hook (word), hot key,
 * count, rectangles; the only hook END has is map_hover (046E). */
int menu_loop(void *mv)
{
    u8 *m = mv;
    int result = 0;
    do {
        if (comm_get()->input_devices & 1) joy_poll();
        pal_apply_queue();
        if (PW(m, 0)) map_hover();
        u8 k = (u8)end_poll_input(m);
        if (k == 0) continue;
        if (k == 1) {
            int i = hit_test(m + 4, m[3]);
            if (i) { result = i; end_snd(0x1A); }
            else end_snd(0x22);
        } else if (k == 2) {
            result = 0x63;
            end_snd(0x23);
        } else if (m[2] == k) {
            result = (int)(s8)k + 0x100;
        }
    } while (result == 0);
    return result;
}

/* ------------------------------------------------------------------------------------------------------------------ the waits */

/* 0000:2092 wait_input: joystick / mouse / keyboard wait with the click sounds; 0 = nothing, 1 = Enter or button 0, 2 = Esc or button 1 (the click
 * sounds are played here), 1 is also returned for Space. ticks == FFFFh never counts down; (B) ticks == 0 polls the keyboard once. */
int wait_input(unsigned ticks)
{
    u16 t = (u16)ticks;
    int r = 0;
    Comm *comm = comm_get();
    if (end_test.on_wait) { end_test.waits++; end_test.on_wait(t); }
    if (t != 0) {
        do {
            pal_apply_queue();
            if (t != 0xFFFF) t--;
            r = 0;
            if (comm->input_devices & 1) { if (joy_button_misc(0) || joy_button_misc(1)) r = 1; }
            if (comm->input_devices & 2) {
                mouse_poll();
                if (mouse_button(0) || mouse_button(1)) r = 1;
            }
        } while (r != 0 && t != 0);
    }
    r = 0;
    bool first = true;
    for (;;) {
        if (!first) {
            if (r) break;
            pal_apply_queue();
            if (t != 0xFFFF) t--;
            if (comm->input_devices & 1) {
                if (joy_button_misc(0)) r = 1;
                if (joy_button_misc(1)) r = 2;
            }
            if (comm->input_devices & 2) {
                mouse_poll();
                if (mouse_button(0)) r = 1;
                if (mouse_button(1)) r = 2;
            }
        }
        first = false;
        if (kbd_empty_misc() == 0) {
            int key = kbd_get_misc();
            if (key == 0x1000) quit_prompt(1);
            key &= 0xFF;
            if (key == 0x0D) r = 1;
            if (key == 0x1B) r = 2;
            if (key == 0x20) r = 3;
        }
        if (t == 0) break;
    }
    switch (r) {
    case 1: end_snd(0x1A); return 0;
    case 2: end_snd(0x23); return 0;
    case 3: end_snd(0x1A); return 1;
    default: return 0;
    }
}

/* 0000:206A prompt_continue: "Press Selector to continue", shadowed and centred, then a key or click */
int prompt_continue(int cx, int y)
{
    tctx(END_CTX0)->font = 3;
    text_shadow_centered(tctx(END_CTX0), e_str(0x2DC), cx, y);
    return wait_input(0xFFFF);
}

/* 0000:112A text_shadow_centered: START's ui_text_shadow, centred */
void text_shadow_centered(void *c, char *s, int cx, int y)
{
    TextCtx *t = c;
    if (t->bg != -1) {
        TextCtx copy = *t;
        copy.fg = copy.bg;
        ui_text_centered(&copy, s, cx + 1, y);
        ui_text_centered(&copy, s, cx, y + 1);
        ui_text_centered(&copy, s, cx + 1, y + 1);
    }
    ui_text_centered(c, s, cx, y);
}

/* ------------------------------------------------------------------------------------------------- palette queue and fades */

/* 0000:185C pal_queue_push: `rgb` is a pointer into DGROUP; the record keeps its offset */
void end_pal_queue_push(int first, void *rgb, int count)
{
    u16 e = (u16)(E_PALQ + 6 * DSB(E_PALQ_N));
    DSW(e) = (u16)first;
    DSW(e + 2) = ds_off(rgb);
    DSW(e + 4) = (u16)count;
    DSB(E_PALQ_N)++;
}

/* 0000:1708 pal_fade_calibrate. PORT: no measuring; the fade is paced by the retrace (one pal_set per tick), 15 per speed unit (as src/shell/ui.c). */
void end_pal_fade_calibrate(u8 *buf)
{
    (void)buf;
    DSB(0x55BA) = 15;
}

/* 0000:1607 pal_fade: out[i] = src[i] * cur^2 >> 16 for cur = from .. to over steps. (B) unlike START's it does not wait for the palette queue first.
 * PORT: the timer ISR is not stopped and restarted around it. */
void end_pal_fade(u8 *pal, int from, int to, int speed)
{
    u8 out[0x300];
    const s8 *src = (const s8 *)pal;
    s32 cur = (s16)from, delta = 0, steps = 0;
    if ((s16)speed == 0) cur = (s16)to;
    for (;;) {
        for (int i = 0; i < 0x300; i++) out[i] = (u8)(((s32)src[i] * cur * cur) >> 16);
        if (DSB(0x55BA) == 0) end_pal_fade_calibrate(out);
        pal_set(out);
        if ((s16)to == cur) break;
        if (steps == 0) {
            steps = (s32)(s8)DSB(0x55BA) * (s16)speed;
            s32 div = steps ? steps : 0xFF;
            delta = ((s16)to - (s16)from) / div;
        }
        steps--;
        if (steps > 0) cur += delta; else cur = (s16)to;
    }
}

/* 0000:15D7 / 15EF */
void fade_in(int speed) { end_pal_fade(DSP(E_PAL), 0, 0x100, speed); }
void fade_out(int speed) { end_pal_fade(DSP(E_PAL), 0x100, 0, speed); }

/* -------------------------------------------------------------------------------------------------------- files and screens */

/* 0000:100D load_pic: open, decode into the page, close */
void load_pic(char *name, int page)
{
    int fh = dos_open(name, 0);
    pic_to_page(fh, page);
    dos_close(fh);
}

/* 0000:1901 show_screen_pic: fade out, name.pic through the file buffer into page 1, name.pal into the palette buffer, page 1 to the screen, the screen's
 * own callback, fade in */
void show_screen_pic(char *name, void *cb)
{
    char f[0x40];
    snprintf(f, sizeof f, e_str(0x2CE), name);               /* "%s.pic" */
    fade_out(1);
    ui_load_file_far(f, DSW(0x6442), DSW(0x6444));
    pic_to_seg_b(DSW(0x7220), D_PAGE_SEG(1));
    snprintf(f, sizeof f, e_str(0x2D5), name);               /* "%s.pal" */
    ui_load_file_near(f, DSP(E_PAL));
    blit_rect(1, 0, 0, 0, 0, 0, 320, 200);
    if (cb) ((void (*)(void))cb)();
    fade_in(1);
    screen_on();
}

/* 0000:3E61 quit_prompt: Alt-Q. adv.pic asks the question; Y quits (exit 0), anything else restores the screen from page 1 */
void quit_prompt(int save_screen)
{
    u8 save[0x300];
    end_snd(0);
    memcpy(save, DSP(E_PAL), 0x300);
    fade_out(1);
    if (save_screen) blit_rect(0, 0, 0, 1, 0, 0, 320, 200);
    ui_load_file_far("adv.pic", DSW(0x6442), DSW(0x6444));
    pic_to_seg_b(DSW(0x7220), D_PAGE_SEG(0));
    ui_load_file_near("adv.pal", DSP(E_PAL));
    fade_in(1);
    while (kbd_empty_misc() != 0) host_pump();
    int c = kbd_get_misc() & 0x5F;
    if (c == 0x59) { end_exit(0); return; }
    fade_out(1);
    blit_rect(1, 0, 0, 0, 0, 0, 320, 200);
    memcpy(DSP(E_PAL), save, 0x300);
    fade_in(1);
}
