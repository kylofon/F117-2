/* The mission generator and the world data it works on (spec shell.md Part C: START 0000:4239 mission_generate, 40F0, the WLD helpers
 * 512E .. 5466, 54BB wld_read_file, 5625 wld_export_to_comm, and the small helpers 5743 .. 59FE). The generator is a straight translation of
 * the 3.8 KB function (its variable names are the stack slots of the original in comments); every rand() call is made in the original's order so
 * a seed reproduces the mission. Site table DS:CBDE (16 bytes each), unit table DS:D3BE (36 bytes each), mission slots DS:E05E and E070. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell/shell.h"

#define MTYPE (PW(sh_pilot(), PIL_MTYPE))
#define SLOT(s) ((u16)(0xE05E + 0x12 * (s)))
#define TEMPLATE(t) ((u16)(START_DS_mission_templates + 12 * (t)))

/* ------------------------------------------------------------------------------------------------------ small helpers */

/* START 0000:5413 dist_approx_xy: max + min/2 of |dx| and |dy|, limited to 7FFFh */
int dist_approx_xy(int dx, int dy)
{
    s16 a = (s16)abs_fn((s16)dx), b = (s16)abs_fn((s16)dy);
    s32 mx, mn;
    if (b < a) { mx = a; mn = b; } else { mx = b; mn = a; }
    s32 sum = (s32)((s16)mn >> 1) + mx;
    u16 lo = (u16)sum;
    s16 hi = (s16)(sum >> 16);
    if (hi < 0) return (s16)lo;
    if (hi > 0 || lo > 0x7FFF) return 0x7FFF;
    return (s16)lo;
}

/* START 0000:532A wld_site_dist */
int wld_site_dist(int a, int b)
{
    u16 sa = SITE(a), sb = SITE(b);
    return dist_approx_xy((s16)(SW(sa, 2) - SW(sb, 2)), (s16)(SW(sa, 4) - SW(sb, 4)));
}

/* START 0000:59D8 mission_clamp_hi: the original's clamp (a value far below lo wraps to hi) */
int mission_clamp_hi(int v, int lo, int hi)
{
    if ((s16)v > (s16)hi) return hi;
    if ((s16)v >= (s16)lo) return v;
    if ((s16)v > (s16)0xC000) return lo;
    return hi;
}

/* START 0000:5743 mission_bearing: 16-bit bearing of (dx, dy), 0 = +dy, 4000h = +dx */
unsigned mission_bearing(int dx, int dy)
{
    s16 x = (s16)dx, y = (s16)dy;
    if (x == 0) return y > 0 ? 0 : 0x8000;
    if (y == 0) return x > 0 ? 0x4000 : 0xC000;
    s16 ay = (s16)abs_fn(y), ax = (s16)abs_fn(x);
    long num, den;
    int swap;
    if (ax > ay) { num = rtl_aFlshl((s16)abs_fn(y), 14); den = (s16)abs_fn(x); swap = 1; }
    else { num = rtl_aFlshl((s16)abs_fn(x), 14); den = (s16)abs_fn(y); swap = 0; }
    s16 t = (s16)rtl_aFldiv(num, den);
    s16 d = (s16)abs_fn((s16)(0x1333 - t));
    long prod = rtl_aFlshr((long)d * 0xB00, 14);
    long v = 0x2800L - prod;
    s16 r = (s16)rtl_aFlshr(rtl_aFlmul((long)t, v), 14);
    u16 q;
    if (x > 0) {
        if (y > 0) q = swap ? (u16)(0x4000 - r) : (u16)r;
        else q = swap ? (u16)(r + 0x4000) : (u16)(0x8000 - r);
    } else {
        if (y > 0) q = swap ? (u16)(r - 0x4000) : (u16)-r;
        else q = swap ? (u16)(0xC000 - r) : (u16)(r + 0x8000);
    }
    return q;
}

/* START 0000:58DF map_grid_ref: the ONC chart reference of a world position, e.g. "TD00" (into DS:B394) */
char *map_grid_ref(int x, int y, int theatre)
{
    (void)theatre;
    int th = PW(sh_pilot(), PIL_THEATRE);
    static const u8 off[9][2] = { { 6, 4 }, { 0, 0 }, { 0, 0 }, { 3, 5 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
    int colOff = 0, rowOff = 0;
    if ((unsigned)th <= 8) {
        strcpy(ds_str(START_DS_map_grid_buf), ds_str((u16)(0x0DA5 + 5 * th)));
        colOff = off[th][0];
        rowOff = off[th][1];
    }
    u8 *g = DSP(START_DS_map_grid_buf);
    s16 col = (s16)((s16)(((s16)x >> 5) * 0x14) >> 10) + colOff;
    while (col > 9) { col -= 10; g[0]++; }
    g[2] = (u8)(g[2] + (u8)col);
    s16 row = (s16)((s16)(((s16)y >> 5) * 0x14) >> 10) + rowOff;
    while (row > 9) { row -= 10; g[1]--; }
    g[3] = (u8)(g[3] + (u8)(9 - row));
    return (char *)g;
}

/* START 0000:58BE map_site_grid_ref */
char *map_site_grid_ref(int site)
{
    u16 s = SITE(site);
    return map_grid_ref((s16)SW(s, 2), (s16)SW(s, 4), (s16)PW(sh_pilot(), PIL_THEATRE));
}

/* START 0000:59FE brief_clock_string: "HH:MM" of a tick count (30 ticks a minute), the launch hour is 06 (day) or 18 (night) */
void brief_clock_string(char *buf, int ticks)
{
    Comm *comm = comm_get();
    strcpy(buf, "00:00");
    buf[0] = (char)(buf[0] + comm->night);
    buf[1] = (char)(buf[1] + (char)(((s16)ticks / 0x708) % 10));
    buf[1] = (char)(buf[1] + (comm->night == 1 ? 8 : 6));
    if ((s8)buf[1] > 0x39) { buf[1] = (char)(buf[1] - 10); buf[0]++; }
    int m = (((s16)ticks / 0x1E) % 0x3C);
    int ax = (m / 5) * 5;
    buf[3] = (char)(buf[3] + (char)(ax / 10));
    buf[4] = (char)(buf[4] + (char)(ax % 10));
}

/* START 0000:5356 mission_unit_init: a unit of the unit table placed at a site */
void mission_unit_init(int u, int site)
{
    u16 U = UNIT(u), s = SITE(site);
    u16 type = SW(U, 0x16);
    u16 ac = (u16)(START_DS_mission_aircraft_tbl + (type << 5));
    SW(U, 2) = (u16)(SW(s, 2) + 9);
    SW(U, 4) = (u16)(SW(s, 4) - 0xC);
    DSL((u16)(U + 8)) = (u32)SW(U, 2) << 5;
    DSL((u16)(U + 0xC)) = (u32)SW(U, 4) << 5;
    SW(U, 6) = (SB(s, 9) & 2) ? 0x8C : 0x0C;
    SW(U, 0x1A) = SW(ac, 0);
    SW(U, 0x10) = 0xFC00;
    SW(U, 0x12) = 0;
    SW(U, 0x14) = 0;
    SW(U, 0x18) |= 0x403;
    SW(U, 0) = (u16)site;
    SW(U, 0x1C) = (u16)rtl_aFldiv(rtl_aFlshl((s16)SW(ac, 2), 13), (s16)SW(U, 0x1A));
}

/* -------------------------------------------------------------------------------------------------- the world file */

/* START 0000:54BB wld_read_file: the theatre's .WLD into DGROUP, and the name pointer table */
void wld_read_file(const char *path)
{
    FILE *f = fopen_fn((char *)path, "rb");
    if (!f) return;
    fread_fn(DSP(START_DS_wld_id), 2, 1, f);
    fread_fn(DSP(START_DS_wld_site_count), 2, 1, f);
    fread_fn(DSP(START_DS_wld_nthreat_sites), 2, 1, f);
    fread_fn(DSP(START_DS_wld_first_nontarget), 2, 1, f);
    fread_fn(DSP(START_DS_wld_sites), 0x10, DSW(START_DS_wld_site_count), f);
    fread_fn(DSP(START_DS_wld_nunits), 2, 1, f);
    fread_fn(DSP(START_DS_wld_units), 0x24, DSW(START_DS_wld_nunits), f);
    fread_fn(DSP(START_DS_wld_tbl_weapon_class), 1, 0x80, f);
    fread_fn(DSP(START_DS_wld_tbl_b), 1, 0x80, f);
    fread_fn(DSP(START_DS_wld_tbl_category), 1, 0x80, f);
    fread_fn(DSP(START_DS_map_hostile_grid), 1, 0x100, f);
    fread_fn(DSP(START_DS_wld_names), 1, 0x2EE, f);
    fclose(f);
    DSW(START_DS_wld_loc_names) = START_DS_wld_names;
    int k = 1;
    for (int i = 0; i < 0x2EE; i++)
        if (DSB((u16)(START_DS_wld_names + i)) == 0 && k < 0x80) DSW((u16)(START_DS_wld_loc_names + 2 * k++)) = (u16)(START_DS_wld_names + 1 + i);
}

/* START 0000:5466 wld_load_theatre: "<prefix>.wld", ".3dG" and ".3dT" */
void wld_load_theatre(const char *prefix)
{
    char name[0x10];
    strcpy(name, prefix);
    strcpy(name + 2, ds_str(0xD93));
    wld_read_file(name);
    strcpy(name + 2, ds_str(0xD98));
    map_load_3dg(name);
    strcpy(name + 2, ds_str(0xD9D));
    map_load_3dt(name);
}

/* START 0000:588D wld_pack_put / 585C wld_pack_get: the cursor (DS:B390 off, B392 seg) over comm+7Ah */
static void pack_put(const void *src, int size, int count)
{
    u16 n = (u16)(size * count);
    memmove(mp(DSW(START_DS_wld_pack_seg), DSW(START_DS_wld_pack_off)), src, n);
    DSW(START_DS_wld_pack_off) = (u16)(DSW(START_DS_wld_pack_off) + n);
}
void wld_pack_put(void *src, int size, int count) { pack_put(src, size, count); }
void wld_pack_get(void *dst, int size, int count)
{
    u16 n = (u16)(size * count);
    memmove(dst, mp(DSW(START_DS_wld_pack_seg), DSW(START_DS_wld_pack_off)), n);
    DSW(START_DS_wld_pack_off) = (u16)(DSW(START_DS_wld_pack_off) + n);
}

/* START 0000:5625 wld_export_to_comm: the world and the mission into the comm block for VGAME (layout: HANDOFFS C6) */
void wld_export_to_comm(char *name)
{
    (void)name;
    FarPtr comm = ds_far(START_DS_ptr_comm);
    DSW(START_DS_wld_pack_off) = (u16)(comm.off + 0x7A);
    DSW(START_DS_wld_pack_seg) = comm.seg;
    pack_put(DSP(START_DS_wld_id), 2, 1);
    pack_put(DSP(START_DS_wld_site_count), 2, 1);
    pack_put(DSP(START_DS_wld_nthreat_sites), 2, 1);
    pack_put(DSP(START_DS_wld_first_nontarget), 2, 1);
    pack_put(DSP(START_DS_wld_sites), 0x10, DSW(START_DS_wld_site_count));
    pack_put(DSP(START_DS_wld_nunits), 2, 1);
    pack_put(DSP(START_DS_wld_units), 0x24, DSW(START_DS_wld_nunits));
    pack_put(DSP(START_DS_wld_tbl_weapon_class), 1, 0x80);
    pack_put(DSP(START_DS_wld_tbl_b), 1, 0x80);
    pack_put(DSP(START_DS_wld_names), 1, 0x2EE);
    pack_put(DSP(START_DS_map_hostile_grid), 1, 0x100);
    pack_put(DSP(START_DS_mission_flight_ticks), 2, 1);
    pack_put(DSP(START_DS_mission_special_unit), 2, 1);
    pack_put(DSP(START_DS_mission_waypoints), 4, 4);
    pack_put(DSP(START_DS_objective_rec), 0x12, 2);
}

/* START 0000:512E wld_site_at_pos: the site at a position (a new one in scratch slot `slot` when none is there); 0xFFFF = nothing found */
int wld_site_at_pos(int x, int y, int slot)
{
    long wy = rtl_aFlshl(0x8000L - (s16)y, 5), wx = rtl_aFlshl((s16)x, 5);
    u8 *hit = map_nearest_object(wx, wy);
    if (!hit) return 0xFFFF;
    u16 nx = (u16)rtl_aFlshr((s32)PD(hit, 4), 5);
    u16 ny = (u16)(0x8000 - (u16)rtl_aFlshr((s32)PD(hit, 8), 5));
    for (int i = 3; (s16)DSW(START_DS_wld_site_count) > i; i++)
        if (SW(SITE(i), 2) == nx && SW(SITE(i), 4) == ny) return i;
    u16 s = SITE(slot);
    SW(s, 2) = nx;
    SW(s, 4) = ny;
    SW(s, 0xE) = (u16)(PW(hit, 0) + 0x100);
    return slot;
}

/* START 0000:51E8 wld_site_of_type: the same for a random object of the given type */
int wld_site_of_type(int slot, int type)
{
    u8 *hit = map_pick_object((s8)type);
    if (!hit) return 0xFFFF;
    u16 nx = (u16)rtl_aFlshr((s32)PD(hit, 4), 5);
    u16 ny = (u16)(0x8000 - (u16)rtl_aFlshr((s32)PD(hit, 8), 5));
    for (int i = 3; (s16)DSW(START_DS_wld_site_count) > i; i++)
        if (SW(SITE(i), 2) == nx && SW(SITE(i), 4) == ny) return i;
    u16 s = SITE(slot);
    SW(s, 2) = nx;
    SW(s, 4) = ny;
    SW(s, 0xE) = (u16)(PW(hit, 0) + 0x100);
    return slot;
}

/* START 0000:5282 arm_pick_weapon: the best weapon (highest suitability) for a target class among the available ones; ties at random */
int arm_pick_weapon(int cls)
{
    int count = 0;
    u8 best = 0;
    u8 list[16];
    int k = PW(sh_pilot(), PIL_MODEL) ? 2 : 0;
    for (; (u16)k < 0x10; k++) {
        if (DSB((u16)(START_DS_arm_unavail + k)) != 0) continue;
        int v = (s8)DSB((u16)(START_DS_arm_suit_tbl + 16 * k + cls));
        if (v <= (int)best) {
            if (v == (int)best) list[count++] = (u8)k;
            continue;
        }
        best = (u8)v;
        list[0] = (u8)k;
        count = 1;
    }
    if (count == 1) return list[0];
    return list[ui_rand_mod(count)];
}

/* --------------------------------------------------------------------------------------------- the 40F0 set-up */

/* START 0000:40F0 mission_site_setup: spec shell.md "6" -- generate the mission for the pilot's orders, then the random events on the map */
void mission_site_setup(void)
{
    u8 *p = sh_pilot();
    Comm *comm = comm_get();
    DSW(START_DS_mission_difficulty) = PW(p, PIL_SKILL);
    DSW(START_DS_mission_theatre) = PW(p, PIL_THEATRE);
    DSW(START_DS_mission_war_level) = PW(p, PIL_WAR);
    wld_load_theatre(ds_str(DSW((u16)(START_DS_theatre_prefix_ptrs + 2 * PW(p, PIL_THEATRE)))));
    mission_generate();
    DSB(0xCAD2) = (u8)comm->weapon_id[0];
    DSB(0xCAD3) = (u8)comm->weapon_id[1];
    DSB(0xCAD4) = (u8)comm->weapon_id[2];
    DSB(0xCAD5) = (u8)comm->weapon_id[3];
    DSB(START_DS_map_flag10_any) = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            if (DSB((u16)(START_DS_map_hostile_grid + j + (i << 4))) & 0x10) DSB(START_DS_map_flag10_any) = 1;
    for (int i = 0; (s16)DSW(START_DS_wld_site_count) >= i; i++) SB(SITE(i), 8) &= 0x3F;
    int k = (int)ui_rand_mod(3);
    while (k != 0) {
        int j = (int)ui_rand_mod(DSW(START_DS_wld_site_count));
        u16 s = SITE(j);
        if (j == (s16)DSW(START_DS_mission_target1_site) || j == (s16)DSW(START_DS_mission_target2_site)) continue;
        if (SW(s, 6) == 0) continue;
        if ((SW(s, 8) >> 8) & 4) continue;
        if ((SW(s, 8) >> 8) & 1) continue;
        SB(s, 8) |= 0x40;
        k--;
    }
    for (int i = 0; (s16)DSW(START_DS_wld_site_count) >= i; i++) {
        u16 s = SITE(i);
        if ((SB(s, 8) & 0x40) && ui_rand_mod(3) == 0) {
            SB(s, 8) |= 0x80;
            SW(s, 6) = 0;
        }
    }
}

/* ---------------------------------------------------------------------------------------------- the generator */

/* a 16-bit signed idiv (the compiler's `cdq; idiv`): a zero divisor or an overflowing quotient is the divide fault */
static int idiv16(int a, int b)
{
    if ((s16)b == 0) rtl_divide_error();
    int q = (s16)a / (s16)b;
    if (q > 32767 || q < -32768) rtl_divide_error();
    return q;
}

/* START 0000:4239 mission_generate: spec shell.md Part C section 1 (P0 .. P13) */
void mission_generate(void)
{
    u8 *pilot = sh_pilot();
    Comm *comm = comm_get();
    const u16 D2A6 = START_DS_wld_tbl_category;
    int attempts, base, min_route;                     /* bp-2, bp-24h, bp-34h */
    int best[2];                                       /* bp-3Eh, bp-3Ch */
    u8 target_type = 0;                                /* bp-32h */
    int score, i;

    DSW(START_DS_mission_flight_ticks) = 0;
    min_route = 0x1C2;
    attempts = base = 0x384;

reroll:                                                /* 4256 */
    attempts = (s16)(attempts + 1);
    if (attempts > 0x32) {
        u8 list[0x80];
        int cnt = 0;
        attempts = 0;
        for (i = 0; i < 0x80; i++) if (DSB((u16)(D2A6 + i)) != 0) list[cnt++] = (u8)i;
        do target_type = list[ui_rand_mod(cnt)]; while (DSB((u16)(D2A6 + (s8)target_type)) == 0x15);
    }

pick_target:                                           /* 42A8 */
    if (!(PW(pilot, PIL_MTYPE) & 1)) {
        int r;
        do r = (int)ui_rand_mod((u16)(DSW(START_DS_wld_first_nontarget) - 3)) + 3; while ((SW(SITE(r), 8) & 0xD01) != 1);
        DSW(START_DS_mission_target1_site) = (u16)r;
    } else {
        for (;;) {
            DSW(START_DS_mission_target1_site) = (u16)wld_site_of_type(1, (s8)target_type);
            if (DSW(START_DS_mission_target1_site) == 0xFFFF) { attempts = 0x384; goto reroll; }
            u16 s = SITE(DSW(START_DS_mission_target1_site));
            if ((SW(s, 8) & 0x801) == 1) continue;
            if ((SB(s, 0xE) & 0x7F) != target_type) continue;
            break;
        }
    }

    {                                                  /* P3: a second site within the 6400-unit box around the first */
        u16 site60 = SITE(DSW(START_DS_mission_target1_site));
        int tries = 0;
        for (;;) {                                     /* 4343 */
            s16 x = (s16)(ui_rand_mod(0x3200) + SW(site60, 2) - 0x1900);
            s16 y = (s16)(ui_rand_mod(0x3200) + SW(site60, 4) - 0x1900);
            if (x < 0x840) x = 0x840; else if (x > 0x77BF) x = 0x77BF;
            if (y < 0x840) y = 0x840; else if (y > 0x77BF) y = 0x77BF;
            if (DSB((u16)(START_DS_map_hostile_grid + (x >> 11) + ((y >> 11) << 4))) & 3) continue;
            int e = wld_site_at_pos(x, y, 2);
            DSW(START_DS_mission_target2_site) = (u16)e;
            bool ok;
            if (e == 0xFFFF) ok = false;
            else if (PW(pilot, PIL_MTYPE) & 1) ok = (SW(SITE(e), 8) & 0x801) != 1;
            else ok = true;
            tries++;
            if (tries > 0x64) goto reroll;
            if (!ok) continue;
            break;
        }
        u16 s72 = SITE(DSW(START_DS_mission_target2_site));
        bool flag;
        if (SW(s72, 2) == SW(site60, 2) && SW(s72, 4) == SW(site60, 4)) flag = false;
        else flag = (s16)(wld_site_dist(DSW(START_DS_mission_target1_site), DSW(START_DS_mission_target2_site)) & 0xFFC0) <= 0x6400;
        if (!flag) goto pick_target;
    }

    for (int pass = 0; pass < 2; pass++) {             /* P4: the defended site nearest to each target (4455) */
        best[pass] = 0x7FFF;
        u16 tgt_off = (u16)(0xE060 + 0x12 * pass);
        for (i = DSW(START_DS_wld_first_nontarget); (s16)DSW(START_DS_wld_site_count) > i; i++) {
            u16 site = SITE(i);
            u16 f = SW(site, 8);
            if (!(f & 0x500) || !(f & 0x201)) continue;
            if (PW(pilot, PIL_THEATRE) == 7 && SW(site, 0) == 0x11) continue;
            if (PW(pilot, PIL_MODEL) != 0 && (SB(site, 9) & 2)) continue;
            int cost;
            if (pass == 0) {
                cost = wld_site_dist(DSW(tgt_off), i);
                cost = (s16)(cost + (s16)((ui_rand_mod(0x96) - 0x4B) << 6));
            } else {
                cost = wld_site_dist(DSW(tgt_off), i);
            }
            if (SB(site, 9) & 1) cost = (s16)(cost + (s16)((ui_rand_mod(100) + 0x32) << 6));
            cost = (s16)mission_clamp_hi(cost, 0, 0x7FFF);
            if (cost >= 0x7FFF) continue;
            int cost2 = (s16)(ui_rand_mod(0x500) + cost);
            int bonus = (SB(site, 9) & 2) ? 0xC80 : 0;
            if ((s16)(bonus + best[pass]) > cost2) {
                DSW((u16)(0xE062 + 0x12 * pass)) = (u16)i;
                best[pass] = cost;
            }
        }
    }
    if ((PW(pilot, PIL_MTYPE) & 2) && DSW(START_DS_mission_theatre) == 0) {            /* the scripted training missions */
        if (PW(pilot, PIL_MTYPE) & 1) {
            DSW(START_DS_mission_target1_site) = 3; DSW(START_DS_mission_target2_site) = 0xF;
            DSW(START_DS_mission_start_site) = 0x22; DSW(START_DS_mission_land_site) = 0x21;
        } else {
            DSW(START_DS_mission_target1_site) = 0x16; DSW(START_DS_mission_target2_site) = 8;
            DSW(START_DS_mission_start_site) = 0x22; DSW(START_DS_mission_land_site) = 0x23;
        }
        best[0] = wld_site_dist(DSW(START_DS_mission_target1_site), DSW(START_DS_mission_start_site));
        best[1] = wld_site_dist(DSW(START_DS_mission_target2_site), DSW(START_DS_mission_land_site));
    }
    score = (s16)((s16)(wld_site_dist(DSW(START_DS_mission_target1_site), DSW(START_DS_mission_target2_site)) >> 6)
                  + (s16)(best[1] >> 6) + (s16)(best[0] >> 6));
    if (score > (s16)(base + attempts) || score < min_route) {
        min_route = (s16)(min_route + (s16)DSW(START_DS_mission_difficulty) - 5);
        goto reroll;
    }

    for (int s = 0; s < 2; s++) {                      /* P5: one mission template per slot (two scans, two rand() calls) */
        DSW(SLOT(s)) = 0;
        int pick = 0;
        for (int pass = 0; pass < 2; pass++) {
            int count = 0;
            for (int t = 0; t < 0x52; t++) {
                u16 tp = TEMPLATE(t);
                if (!(SW(tp, 0) & (u16)(1 << (u8)PW(pilot, PIL_THEATRE)))) continue;
                if (!(SW(tp, 2) & (u16)(1 << (u8)PW(pilot, PIL_WAR)))) continue;
                u16 tsite = SITE(DSW((u16)(0xE060 + 0x12 * s)));
                if ((s16)(s8)DSB((u16)(D2A6 + (SB(tsite, 0xE) & 0x7F))) != (s16)SW(tp, 6)) continue;
                if (s != 0 && (s16)DSW(0xE066) == t) continue;
                if (pass == 0) { count++; continue; }
                if (count == pick) {
                    DSW(SLOT(s)) = SW(tp, 4);
                    DSW((u16)(SLOT(s) + 8)) = (u16)t;
                    u16 fl = SW(tp, 8);
                    DSW((u16)(SLOT(s) + 6)) = fl;
                    if ((s16)SW(tp, 10) > 0) DSW((u16)(SLOT(s) + 6)) = (u16)(fl + ((SW(tp, 10) & 0xFF) << 8));
                }
                count++;
            }
            pick = count != 0 ? (int)ui_rand_mod(count) : 0;
        }
    }

    if ((PW(pilot, PIL_MTYPE) & 2) && DSW(START_DS_mission_theatre) == 0) {            /* P6 scripted training templates */
        int t = (PW(pilot, PIL_MTYPE) & 1) ? 5 : 0x37;
        DSW(SLOT(0)) = SW(TEMPLATE(t), 4);
        DSW(0xE066) = (u16)t;
        DSW(0xE064) = SW(TEMPLATE(t), 8);
    }
    if (PW(pilot, PIL_MTYPE) & 1) {
        if ((s16)DSW(SLOT(0)) >= 5 || (s16)DSW(SLOT(1)) >= 5) { attempts = 0x384; goto reroll; }
    }
    if (DSW(SLOT(0)) == 0 || DSW(SLOT(1)) == 0) { attempts = 0x384; goto reroll; }
    if (DSW(0xE066) == DSW(0xE078)) goto reroll;
    if (best[1] > best[0] && !(PW(pilot, PIL_MTYPE) & 2)) {          /* the cheaper route first */
        u16 tmp[9];
        memcpy(tmp, DSP(SLOT(0)), 18);
        memcpy(DSP(SLOT(0)), DSP(SLOT(1)), 18);
        memcpy(DSP(SLOT(1)), tmp, 18);
        int b0 = best[0];
        best[0] = best[1];
        best[1] = b0;
    }
    if (DSB(0xE076) & 2) goto reroll;
    {
        int k1 = (s16)DSW(SLOT(1)) - 5;
        if (!(k1 < 0) && !(k1 - 3 > 0)) goto reroll;
    }
    if (DSW(SLOT(0)) == 4 && DSW(START_DS_mission_difficulty) == 0) goto reroll;
    if ((DSB(0xE064) & 8) && (DSB(0xE076) & 8)) goto reroll;
    if (DSB(0xE064) & 2)
        DSW(START_DS_mission_flight_ticks) = (u16)((wld_site_dist(DSW(START_DS_mission_start_site), DSW(START_DS_mission_target1_site)) >> 4) + 0x1C2);
    if (DSB(0xE076) & 2)
        DSW(START_DS_mission_flight_ticks) = (u16)((wld_site_dist(DSW(START_DS_mission_start_site), DSW(START_DS_mission_target2_site)) >> 4) + 0x1C2);
    DSW(START_DS_mission_special_unit) = 0xFFFF;
    if ((s16)SW(TEMPLATE(DSW(0xE066)), 10) < 0) DSW(0xD3D4) = (u16)-(s16)SW(TEMPLATE(DSW(0xE066)), 10);

    if (DSW(SLOT(0)) == 5) {                           /* P7: the moving unit, per kind */
        int mn = 0x7FFF;
        DSW(START_DS_mission_special_site) = 0xFFFF;
        for (i = 0; i < (s16)DSW(START_DS_wld_first_nontarget); i++) {
            int d = abs_fn((s16)(wld_site_dist(DSW(START_DS_mission_target1_site), i) - best[0]));
            if (d < mn) {
                u16 f = SW(SITE(i), 8);
                if ((f & 1) && !((f >> 8) & 1)) { DSW(START_DS_mission_special_site) = (u16)i; mn = d; }
            }
        }
        if (DSW(START_DS_mission_special_site) == 0xFFFF) goto reroll;
        mission_unit_init(0, DSW(START_DS_mission_special_site));
        DSW(UNIT(0)) = DSW(START_DS_mission_target1_site);
        SW(UNIT(0), 0x18) |= 4;
        DSW(START_DS_mission_special_unit) = 0;
        strcpy(ds_str(START_DS_brief_grid_special), map_site_grid_ref(DSW(START_DS_mission_special_site)));
        int d = wld_site_dist(DSW(START_DS_mission_special_site), DSW(START_DS_mission_target1_site));
        DSW(START_DS_mission_flight_ticks) = (u16)idiv16(d, 3 * ((s16)DSW(UNIT(0) + 0x1A) >> 6));
    }
    if (DSW(SLOT(0)) == 7 || DSW(SLOT(0)) == 6) {
        int mn = 0x7FFF;
        for (i = 3; (s16)DSW(START_DS_wld_site_count) > i; i++) {
            u16 f = SW(SITE(i), 8);
            if (!((f >> 8) & 5) || ((f >> 8) & 0xA)) continue;
            int d = abs_fn((s16)(wld_site_dist(DSW(START_DS_mission_target1_site), i) - best[0]));
            if (d < mn && DSW(START_DS_mission_start_site) != i) { DSW(START_DS_mission_special_site) = (u16)i; mn = d; }
        }
        strcpy(ds_str(START_DS_brief_grid_special), map_site_grid_ref(DSW(START_DS_mission_special_site)));
        if (DSW(SLOT(0)) == 7) {
            mission_unit_init(0, DSW(START_DS_mission_target1_site));
            DSW(UNIT(0)) = DSW(START_DS_mission_special_site);
            SW(UNIT(0), 0x18) |= 4;
            DSW(START_DS_mission_special_unit) = 0;
            DSW(START_DS_mission_special_site) = DSW(START_DS_mission_target1_site);
        } else {
            mission_unit_init(0, DSW(START_DS_mission_special_site));
            DSW(UNIT(0)) = DSW(START_DS_mission_target1_site);
            SW(UNIT(0), 0x18) |= 4;
            DSW(START_DS_mission_special_unit) = 0;
            int d = wld_site_dist(DSW(START_DS_mission_special_site), DSW(START_DS_mission_target1_site));
            DSW(START_DS_mission_flight_ticks) = (u16)idiv16(d, ((s16)(DSW(UNIT(0) + 0x1A) & 0xFFDF)) >> 5);
        }
    }
    if (DSW(SLOT(0)) == 8) {
        mission_unit_init(0, DSW(START_DS_mission_target1_site));
        if (DSW(0xD3D4) == 2 && DSW(START_DS_mission_theatre) == 1) DSW(0xD3D4) = 0xC;
        SW(UNIT(0), 0x18) |= 0x40;
        DSW(START_DS_mission_special_unit) = 0;
        DSW(START_DS_mission_special_site) = DSW(START_DS_mission_target1_site);
        DSW(UNIT(0)) = DSW(START_DS_mission_target1_site);
    }
    if (DSW(START_DS_mission_special_unit) == 0) DSW(UNIT(0) + 0x1C) = 0x4E1F;

    for (int s = 0; s < 2; s++) {                      /* P8: names for the scratch target sites, grid references */
        u16 tgt = DSW((u16)(0xE060 + 0x12 * s));
        strcpy(ds_str((u16)(0xE068 + 0x12 * s)), map_site_grid_ref(tgt));
        if ((s16)tgt >= 3) continue;
        int mn = 0x7FFF;
        for (int j = 3; (s16)DSW(START_DS_wld_site_count) > j; j++) {
            u16 sj = SITE(j);
            if (SB(sj, 9) & 5) continue;
            if (wld_site_dist(j, tgt) >= mn || SW(sj, 0) == 0) continue;
            mn = wld_site_dist(j, tgt);
            SW(SITE(tgt), 0) = SW(sj, 0);
        }
    }
    DSW(0xE06E) = (u16)((s16)DSW(START_DS_mission_flight_ticks) >> 4);
    {
        u16 a = SITE(DSW(START_DS_mission_start_site));
        DSL(START_DS_mission_anchor_x) = (u32)SW(a, 2) << 5;
        long t = 0x8000L - SW(a, 4);
        long adj = (SB(a, 9) & 2) ? 0 : 0x708;
        DSL(START_DS_mission_anchor_y) = (u32)(rtl_aFlshl(t, 5) - adj);
        u16 b = SITE(DSW(START_DS_mission_target1_site));
        DSW(0x15B6) = SW(b, 2);
        DSW(0x15B8) = SW(b, 4);
        DSW(0x15B2) = (u16)((SW(a, 2) >> 1) + (SW(b, 2) >> 1));
        DSW(0x15B4) = (u16)((SW(a, 4) >> 1) + (SW(b, 4) >> 1));
        u16 d = SITE(DSW(START_DS_mission_land_site)), c = SITE(DSW(START_DS_mission_target2_site));
        DSW(0x15BE) = SW(d, 2);
        DSW(0x15C0) = SW(d, 4);
        DSW(0x15BA) = SW(c, 2);
        DSW(0x15BC) = SW(c, 4);
        if (DSB(0xE064) & 0x10) {
            DSW(0x15B6) = (u16)((SW(b, 2) & 0xFC00) + 0x200);
            DSW(0x15B8) = (u16)((SW(b, 4) & 0xFC00) + 0x200);
        }
    }

    {                                                  /* P9: place the units around the route */
        int wanted = 0, bestdev = 0;
        for (int u = 0; (s16)(DSW(START_DS_wld_nunits) - 4) > u; u++) {
            u16 U = UNIT(u);
            if (SW(U, 0x18) & 0x80) {
                s16 b0 = (s16)best[0];
                int sgn = b0 < 0 ? -1 : 0;
                int lim = (s16)((((b0 ^ sgn) - sgn) >> 2) ^ sgn) - sgn;
                bestdev = (s16)((4 - (s16)DSW(START_DS_mission_difficulty)) * lim);
                if (SW(U, 0x18) & 0x40) bestdev = (s16)(b0 << 1);
                int cand;
                u16 sc;
                do {
                    do cand = (int)ui_rand_mod((u16)(DSW(START_DS_wld_first_nontarget) - 3)) + 3; while (SB(SITE(cand), 9) & 1);
                    sc = SITE(cand);
                    bestdev = (s16)(bestdev + 0x10);
                } while (dist_approx_xy((s16)(DSW(0x15B2) - SW(sc, 2)), (s16)(DSW(0x15B4) - SW(sc, 4))) > bestdev);
                mission_unit_init(u, cand);
                bestdev = 0x3000;
                u16 a = SITE(DSW(START_DS_mission_start_site));
                wanted = (s16)mission_bearing((s16)(SW(a, 2) - SW(U, 2)), (s16)(SW(U, 4) - SW(a, 4)));
                for (int j = 0; j < 8; j++) {
                    int c2 = (int)ui_rand_mod(DSW(START_DS_wld_first_nontarget)) + 1;
                    u16 s2 = SITE(c2);
                    if (SB(s2, 9) & 4) continue;
                    int brg = (s16)mission_bearing((s16)(SW(s2, 2) - SW(U, 2)), (s16)(SW(U, 4) - SW(s2, 4)));
                    int dev = (s16)(wanted - brg);
                    if (abs_fn(dev) >= bestdev) continue;
                    bestdev = abs_fn(dev);
                    DSW(U) = (u16)c2;
                    break;
                }
            }
            if ((SB(U, 0x19) & 1) && DSW(START_DS_mission_special_unit) != 0xFFFF) {
                mission_unit_init(u, DSW(START_DS_mission_special_site));
                SW(U, 0x1C) = 0x4E1F;
            }
            if (u != 0) {
                int tries = 0, c3;
                for (;;) {
                    c3 = (int)ui_rand_mod((u16)(DSW(START_DS_wld_first_nontarget) - 3)) + 3;
                    u16 s3 = SITE(c3);
                    if ((SW(s3, 8) & 0x801) == 1 && SW(s3, 0xC) == 0) break;
                    int t = tries++;
                    if (t >= 0x14) break;
                }
                SW(SITE(c3), 0xA) = SW(U, 0x16);
                SW(SITE(c3), 0xC) = (u16)(ui_rand_mod(DSW(START_DS_mission_theatre) + 1) + 1);
            }
        }
    }

    for (i = 0; (s16)DSW(START_DS_wld_nthreat_sites) > i; i++) {      /* P10: threat levels */
        u16 s = SITE(i);
        int lvl = (s16)SW(s, 6);
        if (lvl == 0 || lvl == 0x15) continue;
        int roll = (int)ui_rand_mod(5) + (PW(pilot, PIL_WAR) >= 1 ? 1 : 0) + (s16)DSW(START_DS_mission_difficulty);
        switch (roll) {
        case 0: case 1: case 3: lvl = (s16)DSW((u16)(START_DS_mission_threat_remap + 4 * lvl + 2)); break;
        case 5: case 7: case 8: lvl = (s16)DSW((u16)(START_DS_mission_threat_remap + 4 * lvl)); break;
        default: break;
        }
        SW(s, 6) = (u16)lvl;
        if (SB(s, 8) & 8) {
            int r10 = (int)ui_rand_mod(10);
            if ((s16)(PW(pilot, PIL_WAR) + (s16)DSW(START_DS_mission_difficulty) + 2) < r10) SW(s, 6) = 0;
        }
    }
    for (int a = 0; a < 16; a++)                       /* P11: the hostile-concentration cells survive with probability difficulty / 5 */
        for (int b = 0; b < 16; b++) {
            u16 cell = (u16)(START_DS_map_hostile_grid + a + (b << 4));
            if (!(DSB(cell) & 0x10)) continue;
            if ((int)ui_rand_mod(5) < (s16)DSW(START_DS_mission_difficulty)) continue;
            DSB(cell) &= 0xEF;
        }

    /* P12: the loadout */
    comm->weapon_id[0] = 1;
    comm->weapon_id[2] = ui_rand_mod(2) == 0 ? 5 : 9;
    comm->weapon_id[3] = 0;
    comm->weapon_id[1] = 2;
    comm->fuel_lbs = (u16)(12 * score);
    if ((s16)comm->fuel_lbs > 0x2710) comm->weapon_id[2] = 0x11;
    {
        int first = 0;
        for (int s = 0; s < 2; s++) {
            int id = -1;
            int kind = (s16)DSW(SLOT(s));
            if (kind == 1 && first == 0) { id = 0x10; first = 1; }
            if (kind == 4 || kind == 3) id = 0x12;
            if (kind == 2) {
                u16 tsite = SITE(DSW((u16)(0xE060 + 0x12 * s)));
                id = arm_pick_weapon(DSB((u16)(START_DS_wld_tbl_weapon_class + (SB(tsite, 0xE) & 0x7F))) & 0xF);
            }
            if (id != -1) comm->weapon_id[s] = (u16)id;
        }
    }
    if ((PW(pilot, PIL_MTYPE) & 2) && DSW(START_DS_mission_theatre) == 0) {
        if (PW(pilot, PIL_MTYPE) & 1) { comm->weapon_id[0] = 5; comm->weapon_id[2] = 1; }
        else { comm->weapon_id[0] = 1; comm->weapon_id[2] = 5; }
        comm->weapon_id[3] = 0;
        if ((s16)(12 * score) > 0x2710) comm->weapon_id[3] = 0x11;
    }
    for (i = 0; i < 4; i++) comm->weapon_qty[i] = DSB((u16)(START_DS_mission_qty_tbl + comm->weapon_id[i]));

    /* P13: the night flag and the times */
    int bias = (s16)(((DSB(0xE078) + DSB(0xE066)) << 8) & 0xF00);
    if (DSW(SLOT(0)) == 4 || DSW(SLOT(1)) == 4) comm->night = 1;
    else if (PW(pilot, PIL_MODEL) != 0) comm->night = ui_rand_mod(10) != 0;
    else comm->night = ui_rand_mod(10) > 2;
    brief_clock_string(ds_str(START_DS_brief_time_launch), bias);
    brief_clock_string(ds_str(START_DS_brief_time_tot), (s16)(DSW(START_DS_mission_flight_ticks) + bias));
    brief_clock_string(ds_str(START_DS_brief_time_tot15), (s16)(DSW(START_DS_mission_flight_ticks) + bias + 0x1C2));
    DSW(START_DS_mission_flight_ticks) = (u16)((s16)DSW(START_DS_mission_flight_ticks) - ((s16)(DSW(START_DS_mission_flight_ticks) + bias) % 0x96));
}
