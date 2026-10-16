/* END.EXE's main: start-up (3BF4 end_init, 3CA1, 3CD0, 3D4F, the mission blob 0113 / 00C9), the program body (0010) and the exit (3F44 in end_ui.c).
 * END runs after VGAME and DSWAP " 2": it replays the finished mission from the comm block (the event log), updates the active pilot record and exits
 * with 23h so F117.COM starts START again. spec debrief.md sections 0, 2, 3. */
#include <stdio.h>
#include <string.h>

#include "debrief/debrief.h"
#include "prog.h"

/* 0000:3CA1 init_pointers: the comm block and the pilot record */
void init_pointers(void)
{
    DSW(0x7224) = DSW(0x55E0) = rd16(0, COMM_SEG_PTR);
    DSW(0x7222) = 0;
    DSW(0x55DE) = 0x1272;
}

/* 0000:3CD0 init_pages: the 35000-byte file buffer, the five pages (page 0 = the screen) and their contexts at DS:6D8C */
void init_pages(void)
{
    DSW(0x7220) = DSW(0x6444) = (u16)mem_alloc_chk(0x88B8);
    DSW(0x6442) = 0;
    page_select(0);
    for (int i = 0; i < 5; i++) {
        u16 seg = (u16)gfx_alloc(i);
        D_PAGE_SEG(i) = seg;
        if (i != 0) page_seg_set_b(seg, i);
        TextCtx *c = tctx((u16)(END_CTX0 + 0x16 * i));
        c->page = (s16)i;
        c->ymin = 0;
        c->ymax = 0xC7;
        c->xmin = 0;
        c->xmax = 0x13F;
    }
}

/* 0000:3D4F init_sprites: the small sprites cut out of requestr.pic (page 2), the mouse cursor */
void init_sprites(void)
{
    load_pic("requestr.pic", 2);
    if (pool_init() == 0) end_exit(1);
    DSW(0x5DA0) = (u16)sprite_grab(2, 312, 161, 3, 5);
    DSW(0x5DB6) = (u16)sprite_grab(2, 234, 104, 86, 20);
    DSW(0x643E) = (u16)sprite_grab(2, 309, 184, 11, 16);
    DSW(0x5B9C) = (u16)sprite_grab(2, 315, 161, 5, 5);          /* unused */
    DSW(0x6D86) = (u16)sprite_grab(2, 317, 156, 3, 5);          /* unused */
    DSW(0x720C) = (u16)sprite_grab(2, 306, 161, 3, 5);
    DSW(0x7226) = (u16)sprite_grab(2, 309, 161, 3, 5);
    if (pool_free() == 0) end_exit(1);
    cursor_init(DSW(0x643E));
}

/* 0000:0242 blob_get: movedata from the blob cursor (DS:543C), the cursor advances */
void blob_get(void *dst, int size, int count)
{
    FarPtr cur = ds_far(0x543C);
    unsigned n = (unsigned)(u16)size * (u16)count;
    memcpy(dst, far_mp(cur), n);
    ds_far_wr(0x543C, far_add(cur, (u16)n));
}

/* 0000:0113 blob_import: the mission world of comm+7Ah in the field order of VGAME's wld_blob_fields (spec section 1.3) */
void blob_import(void)
{
    ds_far_wr(0x543C, far_make(rd16(0, COMM_SEG_PTR), 0x7A));
    blob_get(DSP(0x6983), 2, 1);
    blob_get(DSP(0x6446), 2, 1);                                 /* site_count */
    blob_get(DSP(0x55E4), 2, 1);
    blob_get(DSP(0x7210), 2, 1);
    blob_get(DSP(E_SITES), 16, DSW(0x6446));
    blob_get(DSP(0x6D8A), 2, 1);                                 /* unit_count */
    blob_get(DSP(0x64CA), 36, DSW(0x6D8A));
    blob_get(DSP(E_OBJCLS), 128, 1);
    blob_get(DSP(0x6448), 128, 1);
    blob_get(DSP(E_NAMEPOOL), 750, 1);
    blob_get(DSP(E_GRID), 256, 1);
    blob_get(DSP(0x543A), 2, 1);
    blob_get(DSP(0x5438), 2, 1);
    blob_get(DSP(0x5DA4), 16, 1);
    blob_get(DSP(0x71E8), 36, 1);
    blob_get(DSP(E_LOG), 6, 256);
}

/* 0000:00C9 name_table_build: the blob, then the pointer table of the NUL separated name pool */
void name_table_build(void)
{
    blob_import();
    DSW(E_NAMES) = E_NAMEPOOL;
    int j = 1;
    for (int i = 0; i < 0x2EE; i++)
        if (DSB((u16)(E_NAMEPOOL + i)) == 0 && j < 128) DSW((u16)(E_NAMES + 2 * j++)) = (u16)(E_NAMEPOOL + i + 1);
}

/* 0000:3BF4 end_init */
void end_init(void)
{
    init_pointers();
    /* ovl_bind x3: the drivers are built in (platform.md section 6) */
    /* PORT: the original allocates the 35000-byte buffer here and again in init_pages (the first block leaks); once is enough */
    srand_fn(bios_timer());
    if (end_test.use_seed) DSL(0x5158) = end_test.seed;          /* the test scripts: a fixed seed (the original is set to the same value) */
    if (CW(0x72) & 1) joy_set_calib(e_comm() + 0x48);
    if (CW(0x72) & 2) mouse_init();
    break_hook_install();
    name_table_build();
    char *disp = (char *)DSP(0x7228);
    strcpy(disp, e_str_at(0x1B8A, EP(0x20)));                    /* the short rank, e.g. "Capt. " */
    str_copy_far(disp + strlen(disp), (const char *)(e_pilot() + 2));
    init_pages();
    init_sprites();
    timer_set_hook(end_tick);                                    /* 4B8C timer_install */
    timer_install();
    DSB(0x50CB) = 1;
}

/* 0000:0010 debrief_main: the whole debrief; the pilot record is the only thing written back (spec sections 0 and 9) */
void debrief_main(void)
{
    end_test_from_env();
    end_init();
    if (CW(0x30) == 0) EP(0x36)++;                               /* missions flown, not for a training mission */
    D_LANDCLS = 3;
    switch (CW(0x26)) {
    case 1: cemetery_screen(); break;
    case 2:
        if (landing_class() != 0) rescue_screen();
        else captured_screen();
        break;
    case 3: landed_screen(); break;
    default: break;
    }
    summary_screen();
    map_screen();
    awards_screen();
    ending_scene();
    if (EP(0x36) == 99) EP(0x4E) = 1;                            /* retired */
    if (CW(0x26) == 1 && CW(0x30) == 0) EP(0x4E) = 2;            /* killed in action */
    prog_log("[END] pilot record: missions=%u last=%u best=%u career=%u status=%u rank=%u", EP(0x36), EP(0x30), EP(0x2E),
             (unsigned)(EP(0x32) | (u32)EP(0x34) << 16), EP(0x4E), EP(0x20));
    end_exit(0x23);
}

/* END.EXE: the chain runs it as a program; exit(n) is exit_fn(n) from anywhere */
int end_main(const char *tail)
{
    (void)tail;
    debrief_main();
    return 0;
}
