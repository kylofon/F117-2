/* Small pieces of the per-frame step (spec simflow.md Part 4 section 4 and Part 2 section 5): the sound gate, the frame-rate dependent constants, glide slope,
 * smoke puffs, the falling body, the training cheat, the cockpit widget reset, the target names and the key + signature call. */
#include <string.h>

#include "sim/sim.h"
#include "sim/sim_priv.h"
#include "sim/sim_stubs.h"

static u16 pilot_w(int off) { return *(u16 *)(far_mp(ds_far(0x98F4)) + off); }

/* 0000:D6E5 snd_engine_refresh: the engine drone (cmd 18h) only at the highest sound level, with throttle, not while ejecting; else 19h (off) */
void snd_engine_refresh(void)
{
    if (DSW(0x43C2) == 0 && DSW(0x2DFE) != 0 && DSW(0xC06E) == 0) sim_snd_cmd(0x18, 0, 0, 0);
    else sim_snd_cmd(0x19, 0, 0, 0);
}

/* 0000:D6BF snd_cue: the effect `cmd` if its priority is high enough for the sound level (and, while ejecting, above 1) */
void snd_cue(int cmd, int prio)
{
    if ((s16)prio >= (s16)DSW(0x43C2) && (DSW(0xC06E) == 0 || (s16)prio > 1)) sim_snd_cmd(cmd, 0, 0, 0);
    snd_engine_refresh();
}

/* 0000:D707 frame_params_update: the constants that follow the frame-rate estimate [3660] (clamped to [4 - [3D7A], 15]) */
void frame_params_update(void)
{
    s16 rate = (s16)DSW(0x3660);
    if (rate > 15) DSW(0x43B8) = (u16)fx_clamp_wrap(((s16)(-120 / rate) + 9) >> 1, 1, 4);
    else DSW(0x43B8) = 0;
    rate = (s16)fx_clamp_wrap(rate, 4 - (s16)DSW(0x3D7A), 15);
    DSW(0x3660) = (u16)rate;
    DSW(0xB16C) = (u16)fx_clamp_wrap(rate * 2, 3, 16);
    DSW(0xB286) = (u16)(rate * 250);
    DSW(0x9F64) = (u16)(rate * 200);
}

/* 0000:D7BB frame_rate_halve: cancels time acceleration (2 -> 1, the rate doubles) */
void frame_rate_halve(void)
{
    if (DSW(0x3D7A) == 2) {
        DSW(0x3D7A) = 1;
        DSW(0x3660) = (u16)(DSW(0x3660) << 1);
        frame_params_update();
    }
}

/* 0000:D76B view_range_setup (owner render3d, ported early: trivial and called from 39C4 and CBA0): the threat range table and the culling constants */
void view_range_setup(void)
{
    for (int i = 0; i < 6; i++) DSW((u16)(0x48A6 + 2 * i)) = (u16)(0x20 << ((i + DSB(0x98DE)) & 31));
    DSW(0x48B2) = (u16)(DSW(0x48AE) + DSW(0x48B0));
    DSW(0x48B4) = (u16)fx_clamp_wrap((s16)(DSW(0x48B0) << 1), 0x1000, 0x270F);
    DSW(0x48B6) = (u16)((DSW(0x98DE) + 1) * 0xD05);
}

/* 0000:4E0F hud_glideslope_update: [46C2] = 0 gear up, 1 / 3 off the slope by more than 7.98 degrees, 2 on it */
void hud_glideslope_update(void)
{
    if (DSW(0x9B04) & 1) { DSW(0x46C2) = 0; return; }
    s16 dev = (s16)(DSW(0x2DF0) - DSW(0x98C6));
    u16 site = (u16)(0xB2A2 + 16 * DSW(0xDFE0));
    s16 dy = (s16)(DSW(site + 4) - DSW(0xC0B2));
    if (dy < 0) dy = (s16)-dy;
    int strip = (DSB(site + 9) & 2) != 0;
    dy = (s16)(dy - (strip ? 0x18 : 0x38));
    s16 h = (s16)DSW(0x2DF4);
    if (strip) h = (s16)(h - 0x80);
    s16 ang = (s16)(fx_heading(-(h >> 5), dy) - dev);
    DSW(0x46C2) = 2;
    if (h == 0) return;
    if (ang < 0) {
        ang = (s16)-ang;
        if (ang > 0x5B0) DSW(0x46C2) = 1;
    } else if (ang > 0x5B0) DSW(0x46C2) = 3;
}

/* 0000:479C fx_smoke_update: the 16 smoke puffs (8 bytes: x y age count at DS:39DA) over a burning site rise; every 8th step a new one is born at the site */
void fx_smoke_update(void)
{
    if ((s16)DSW(0x3D68) == -1) return;
    for (int i = 0; i < 16; i++) {
        u16 r = (u16)(0x39DA + 8 * i);
        DSW(r + 4) = (u16)(DSW(r + 4) + 4);
        DSW(r + 2) = (u16)(DSW(r + 2) + ((s16)DSW(r + 4) >> 9));
    }
    if (DSW(0x3D60) & 7) return;
    u16 site = (u16)(0xB2A2 + 16 * DSW(0x3D68));
    int j = (DSB(0x3D60) & 0x78) >> 3;
    u16 r = (u16)(0x39DA + 8 * j);
    DSW(r) = DSW(site + 2);
    DSW(r + 2) = DSW(site + 4);
    DSW(r + 4) = 0x40;
    DSW(0x3A5A) = (u16)j;
    DSW(r + 6) = 0;
    for (int i = 0; i < 16; i++) {                       /* NB: it is record i whose count goes up, not record j's */
        u16 ri = (u16)(0x39DA + 8 * i);
        if (i != j && DSW(ri) != 0) DSW(ri + 6)++;
    }
}

/* 0000:483C fx_fall_update: the falling body (altitude [C602], vertical speed [B756]: gravity -12 per step down to -16) */
void fx_fall_update(void)
{
    if ((s16)DSW(0xC602) <= 0) return;
    if ((s16)DSW(0xB756) > -16) DSW(0xB756) = (u16)(DSW(0xB756) - 12);
    DSW(0xC602) = (u16)(DSW(0xC602) + DSW(0xB756));
}

/* 0000:48DD mission_cheat_rearm: the training cheat (Alt-R): no damage, bays 9 or 1, gun 1000, fuel 5000 */
void mission_cheat_rearm(void)
{
    DSW(0x3632) = 0;
    DSW(0xC5C8) = 0;
    for (int i = 0; i < 4; i++) DSW((u16)(0x3640 + 4 * i)) = (s16)DSW((u16)(0x363E + 4 * i)) < 0x10 ? 9 : 1;
    DSW(0x3650) = 1000;
    DSW(0x3634) = 5000;
}

/* 0000:784A cockpit_widgets_reset: MFD page cleared, lamps 16h, 1Dh and 0 lit, the right MFD back on the objectives page */
void cockpit_widgets_reset(void)
{
    mfd_clear(2, 0);
    hud_lamp_recolor(DSP(DSW(CTX_P0_PTR)), 0x16, 1);
    hud_lamp_recolor(DSP(DSW(CTX_P0_PTR)), 0x1D, 1);
    hud_lamp_recolor(DSP(DSW(CTX_P0_PTR)), 0, 1);
    hud_lamp_recolor(DSP(DSW(CTX_P2_PTR)), 0, 1);
    DSW(0x409C) = 0;
    DSW(0xDFDC) = 0x18;
}

/* 0000:4B57 mission_name_targets: each mission target gets a private name entry (the scenery object's class and name are cloned) */
void mission_name_targets(void)
{
    u16 n = DSW(0x0610);
    for (int k = 0; k < 2; k++) {
        u16 slot = (u16)(0xE2D8 + 0x12 * k);
        if (DSB(slot + 7) == 0) continue;
        u16 site = (u16)(0xB2A2 + 16 * DSW(slot + 2));
        s32 x = (s32)((u32)DSW(site + 2) << 5);
        s32 y = (s32)((u32)((s32)0x8000 - (s32)DSW(site + 4)) << 5);
        u16 cell = terr_find_scenery_at(x, y);
        DSW(0x9F10) = cell;
        if (cell) {
            u16 cls = DSB(cell) & 0x7F;
            DSB((u16)(0xC604 + n)) = DSB((u16)(0xC604 + cls));
            strcpy_fn(ds_str(DSW((u16)(0xDEDC + 2 * n))), ds_str(DSW((u16)(0xDEDC + 2 * cls))));
            DSW((u16)(0xDEDC + 2 * n + 2)) = (u16)(DSW((u16)(0xDEDC + 2 * n)) + strlen(ds_str(DSW((u16)(0xDEDC + 2 * n)))) + 1);
            terr_set_scenery_state(cell, (u16)(n + 0x100));
        }
        DSW((u16)(site + 0xE)) = (u16)(n + 0x100);
        n++;
    }
    DSW(0x3D76) = 0;
}

/* 0000:43EE sim_key_and_signature (owner detect, ported here with the key command call): cba0 with the pending key, then the radar signature [3D5C] */
void sim_key_and_signature(void)
{
    input_command_key(DSW(0xDE90));
    s16 bank = (s16)fx_sext8(DSB(0x2DF3));
    int t = bank < 0 ? -bank : bank;
    if (t >= 0x40) t = 0x80 - t;
    s16 skill = (s16)DSW(0x3654);
    u16 sig;
    if (pilot_w(0x42) != 0) {
        u16 v = (u16)fx_sext8(fx_heading((skill + 1) << 7, 0x800) >> 8);
        sig = (u16)(v + (DSW(0xB052) >> 10) + (t >> 1));
        if (DSW(0x9B04) == 0) sig = (u16)(sig + 0x14);
    } else {
        u16 v = (u16)fx_sext8(fx_heading((u16)((u16)(DSW(0x2DF4) / 3) * (skill + 1)), 0x800) >> 8);
        sig = (u16)(v + (DSW(0xB052) >> 10) + (t >> 1));
    }
    if (DSW(0x9B04) & 4) sig = (u16)(sig + 0x18);
    if (DSW(0x9B04) & 0x10) sig = (u16)(sig + 0x10);
    DSW(0x3D5C) = sig;
}
