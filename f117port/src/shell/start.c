/* START.EXE's main: start-up (7312 shell_init, 7387, 73CE, 74B1), the program body ui_main (0010) and the exit (76D1). START runs after
 * DSWAP " 0": the roster and the ready room, then it leaves the planned mission in the comm block for VGAME (exit code 0Ch, or 0Dh when the
 * departure site has no hangar) or quits with 0. spec shell.md Part A section 1 and Part D section 3. */
#include <stdio.h>
#include <string.h>

#include "shell/shell.h"

/* the ISR body of START (8D0C..8E6D): the tick flag the palette queue waits for (DS:ADFC) and the sound poll */
static void start_tick(void)
{
    DSB(0xADFC)++;
    fe_snd_poll();                                              /* TODO(S5.9): slot 103 snd_poll; its result steers the tick divisor */
    if (host_quit_requested()) shell_exit(0);                   /* PORT: a closed window ends START wherever it waits (the original has Ctrl-Break only) */
}

/* START 0000:7387 shell_comm_setup: the pointers to the comm block and the BIOS words, the active pilot record */
void shell_comm_setup(void)
{
    DSW(START_DS_ptr_first_run_flag + 2) = 0;
    DSW(START_DS_ptr_first_run_flag) = COMM_FIRST_RUN;
    DSW(START_DS_ptr_gfx_init_flag + 2) = 0;
    DSW(START_DS_ptr_gfx_init_flag) = COMM_INIT_DONE;
    u16 seg = rd16(0, COMM_SEG_PTR);
    DSW(START_DS_ptr_comm + 2) = seg;
    DSW(START_DS_ptr_comm) = 0;
    DSW(START_DS_ptr_pilot + 2) = seg;
    DSW(START_DS_ptr_pilot) = 0x1272;
}

/* START 0000:76D1 shell_exit: stops the sound, frees the buffer, text mode for the codes 0 and 1, exit(code); code 1 = out of memory */
void shell_exit(int code)
{
    sh_snd(0);
    if (DSW(START_DS_file_buf_seg2)) mem_free_chk(DSW(START_DS_file_buf_seg2));
    if (DSB(0x73F2)) break_hook_remove();
    if (DSB(0xADFB)) timer_remove();
    if ((s16)code <= 1) video_set_mode(3);
    kbd_locks_clear();
    if (shell_test.leave_space && (code == EXIT_START_FLY || code == EXIT_START_FLY_NOHANGAR)) kbd_inject(0x3920);
    if ((s16)code == 1) { fprintf(stderr, "Not enough memory!\n"); code = 0; }
    exit_fn(code);
}

/* START 0000:73CE shell_gfx_init: the working buffer, the video mode (once per session), the five pages and their contexts, PALETTES.PAL */
void shell_gfx_init(void)
{
    Comm *comm = comm_get();
    DSW(START_DS_file_buf_seg2) = DSW(START_DS_file_buf_seg) = (u16)mem_alloc_chk(0x5DC0);
    DSW(START_DS_file_buf_off) = 0;
    if (rd16(0, COMM_INIT_DONE) == 0) {
        screen_off();
        mode13_set();                                           /* the original passes comm+24h, the joystick calibration byte */
        (void)comm->joy_calib_byte;
        wr16(0, COMM_INIT_DONE, 1);
    }
    page_select(0);
    for (int i = 0; i < 5; i++) {
        u16 seg = (u16)gfx_alloc(i);
        DSW((u16)(START_DS_ui_page_seg + 2 * i)) = seg;
        if (seg == 0) shell_exit(1);
        if (i != 0) page_seg_set_b(seg, i);
        TextCtx *c = tctx((u16)(SH_CTX0 + 0x16 * i));
        c->page = (s16)i;
        c->xmin = 0;
        c->ymin = 0;
        c->ymax = 0xC7;
        c->xmax = 0x13F;
    }
    u16 pseg = (u16)mem_alloc_chk(0x1B00);
    DSW(START_DS_palettes_ptr + 2) = pseg;
    if (pseg == 0 && DSW(START_DS_palettes_ptr) == 0) shell_exit(1);
    DSW(START_DS_palettes_ptr) = 0;
    ui_load_file_far(ds_str(0xED8), 0, pseg);                   /* "palettes.pal" */
}

/* START 0000:74B1 shell_sprites_init: the ten small sprites cut out of requestr.pic (page 2) */
void shell_sprites_init(void)
{
    ui_load_screen_pic(0xB, 2);
    if (pool_init() == 0) shell_exit(1);
    DSW(0xD29E) = (u16)sprite_grab(2, 0x138, 0xA1, 3, 5);
    DSW(0xD330) = (u16)sprite_grab(2, 0xE8, 0, 0x58, 0x65);
    DSW(START_DS_ui_sprite_seg) = (u16)sprite_grab(2, 0x134, 0xB0, 0xC, 8);
    DSW(START_DS_spr_cursor) = (u16)sprite_grab(2, 0x135, 0xB8, 0xB, 0x10);
    DSW(0xD08E) = (u16)sprite_grab(2, 0x13B, 0xA1, 5, 5);
    DSW(0xD88E) = (u16)sprite_grab(2, 0x13D, 0x9C, 3, 5);
    DSW(0xE082) = (u16)sprite_grab(2, 0x132, 0xA1, 3, 5);
    DSW(0xE09A) = (u16)sprite_grab(2, 0x135, 0xA1, 3, 5);
    DSW(0xE09C) = (u16)sprite_grab(2, 0x13A, 0x9E, 3, 3);
    DSW(0xCAC4) = (u16)sprite_grab(2, 0x132, 0xA6, 0xE, 0xA);
    if (pool_free() == 0) shell_exit(1);
    ui_cursor_init(DSW(START_DS_spr_cursor));
}

/* START 0000:7312 shell_init */
void shell_init(void)
{
    Comm *comm;
    shell_comm_setup();
    /* ovl_bind(MISC / MGRAPHIC / sound): the drivers are built in (platform.md section 6) */
    /* PORT: the original allocates the 24000-byte buffer here and again in shell_gfx_init (the first block leaks); once is enough */
    shell_gfx_init();
    shell_sprites_init();
    timer_set_hook(start_tick);                                 /* 8CA8 timer_install */
    timer_install();
    DSB(0xADFB) = 1;
    comm = comm_get();
    if (comm->input_devices & 1) joy_set_calib(comm->joy_calib);
    if (comm->input_devices & 2) mouse_init();
    srand_fn(bios_timer());
    if (shell_test.use_seed) DSL(0xAE8C) = shell_test.seed;      /* the test scripts: a fixed seed (the original is set to the same value) */
    DSB(START_DS_mission_loaded) = 0;
}

/* START 0000:0010 ui_main: init, roster sync, the arming roll, the ready room, the mission into the comm block, exit 0Ch / 0Dh */
void ui_main(void)
{
    shell_test_from_env();
    shell_init();
    /* 0017: the ID quiz (copy protection) call is patched out in this version (three NOPs): never reached, nothing to skip */
    roster_startup_sync();
    arm_roll_unavailable();
    ui_ready_room();
    wld_export_to_comm(ds_str(START_DS_str_temp_wld));
    Comm *comm = comm_get();
    comm->mission_state = 3;
    comm->end_code = 0;
    comm->missiles_launched = 0;
    comm->nonscoring = PW(sh_pilot(), PIL_MTYPE) > 1;
    shell_exit((SB(SITE(DSW(START_DS_mission_start_site)), 9) & 2) ? EXIT_START_FLY_NOHANGAR : EXIT_START_FLY);
}

/* START.EXE: the chain runs it as a program; exit(n) is exit_fn(n) from anywhere */
int start_main(const char *tail)
{
    (void)tail;
    ui_main();
    return 0;
}
