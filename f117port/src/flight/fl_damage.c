/* Damage (owner damage, spec port/spec/flight.md section 6, simflow.md Part 6 section 3): the player hit by a blast 74F6, a unit shot down 71B4, a ground object
 * destroyed 72A4 with the mission targets 7470 and the wreck test 780A, and the scenery lookup / state change 0D08 / 0F31 on render3d's terrain tables. 74F6 is
 * translated from port/notes/s38/s38_ref.py, the others from the disassembly read against the spec (they are differential-tested by sim_vgame). */
#include <string.h>

#include "flight/fl.h"

#define S(o) ((s16)DSW(o))
#define U(o) DSW(o)
#define SETW(o, v) (DSW(o) = (u16)(v))

/* 0000:74F6 dmg_player_blast: skill + 1 random subsystem hits, the damage lamps, the hit flash, sound 4; training missions are immune */
void dmg_player_blast(void)
{
    if (DSB(0x9B05) & 0x10) return;
    for (int n = 0; n <= S(0x3654); n++) {
        int b = fl_rand_n(8);
        SETW(0x3632, U(0x3632) | (1u << (b & 15)));
        SETW(0xC5C8, U(0xC5C8) + 1);
    }
    mfd_refresh(0x16);
    SETW(0x98EE, 1);
    snd_cue(4, 2);
}

/* 0000:7470 mission_target_done(j): 0 primary, 1 secondary; 1 when newly achieved */
int mission_target_done(int j)
{
    if (U(0x9B04) & (0x4000 >> j)) return 0;
    u16 kind = U(0xE2D8 + 0x12 * j);
    if (kind == 4 || kind == 3) mission_log_event((j < 1 ? 0x80 : 0x40) + 0x0B, 0);
    if (j != 0) { fl_strcpy(0x9876, 0x3EAE); SETW(0x2E7C, 1); DSB(0x9B05) |= 0x20; }
    else        { fl_strcpy(0x9876, 0x3EBD); SETW(0x2E7C, 2); DSB(0x9B05) |= 0x40; }
    if ((DSB(0x9B05) & 0x60) == 0x60) SETW(0x2E7C, 3);
    return 1;
}

/* 0000:71B4 dmg_unit_kill(u) */
void dmg_unit_kill(int u)
{
    SETW(0x98FE, u);
    u16 r = (u16)(UNIT_BASE + 0x24 * u);
    u16 f = U(r + 0x18);
    if (!(f & 0x20)) {
        u16 k = (u16)(0x2FE0 + (u16)(U(r + 0x16) << 5));
        SETW(k, U(k) + 1);                                                /* kills per aircraft type (no reader) */
        if (f & 0x800) SETW(0xDECE, U(0xDECE) - 1);
        if (U(0x3D6E) == (u16)u) SETW(0x3D6E, 0xFFFF);
        DSB(r + 0x18) |= 0x20;
        SETW(0x3D68, 0xFFFF);
        SETW(0xC0A6, U(r + 2));
        SETW(0xC0B8, U(r + 4));
        SETW(0xC602, U(r + 6));
        SETW(0xB756, 0x80);
        int code = 3;
        if (S(0xE2D8) >= 5 && u == 0) { mission_target_done(0); code = 0x83; }
        mission_log_event(code, ((DSB(r + 0x19) & 0x40) ? 0x80 : 0) + U(r + 0x16));
        if (U(r + 0x1A) == 0) SETW(r + 0x18, U(r + 0x18) & 0x1C1);       /* a unit at speed 0 (parked) is reset, not killed (Q-A6) */
    }
    fl_strcpy(0x9876, (u16)(AC_BASE + (u16)(U(r + 0x16) << 5)));
    snd_cue(5, 2);
    if (S(0xE2FE) == 1 && U(0x3D64) == (u16)u) SETW(0x9D0E, 1);
}

/* 0000:780A dmg_wobj_destroyable(o): the world object found last ([9F10]) is not yet the wreck shape */
int dmg_wobj_destroyable(int o)
{
    s16 wreck = obj_cls_soft(o) ? (s8)DSB(0xC0AC) : (s8)DSB(0xDEA4);
    u16 w = U(0x9F10);
    if (U(w) == (u16)wreck) return 0;
    u16 p = U(w + 0xC);
    if ((DSB(p + 6) & 0x80) && (DSB(p + 6) & 0x7F) == U(w)) return 0;
    return 1;
}

/* 0000:72A4 dmg_object_destroy(o): the mission-objective bookkeeping of a destroyed ground object */
void dmg_object_destroy(int o)
{
    msg_site_label(o);
    int code = 1;
    u16 r = (u16)(SITE_BASE + 16 * o);
    u16 f = U(r + 8);
    if (!(f & 0x80)) {
        if (f & 0x1000) SETW(0xDED2, U(0xDED2) - 1);
        SETW(0x9F10, terr_find_scenery_at((s32)(u32)U(r + 2) << 5, (0x8000 - (s32)(u32)U(r + 4)) << 5));
        u16 nw;
        if (o == 0) {                                                     /* the scratch record of an unregistered world object */
            if (!dmg_wobj_destroyable(o)) return;
            u16 w = U(0x9F10);
            DSB((u16)(0xC0BA + U(w))) += 1;
            mission_log_event(2, U(w));
            u16 v = r3d_scenery_id(U(r + 0xE));
            u16 tab = U((u16)(0x0A60 + 2 * ((s16)v >> 8)));
            if (DSB((u16)(tab + (v & 0xFF))) & 0x20) nw = U(r + 0xE);
            else nw = (u16)(obj_cls_soft(o) ? (s8)DSB(0xC0AC) : (s8)DSB(0xDEA4));
            nw |= 0x100;
            SETW(r + 0xE, nw);
        } else {
            if (U(r + 6) == 0) code = 0x0C;
            DSB(r + 8) |= 0x80;
            SETW(r + 6, 0);
            for (int k = 0; k < 2; k++) {
                if (U(0xE2D8 + 0x12 * k) != 2 || U(0xE2DA + 0x12 * k) != (u16)o) continue;
                mission_target_done(k);
                code |= k < 1 ? 0x80 : 0x40;
                snd_cue(4, 2);
                if (k == 0) SETW(0xDED8, U(0x3660) << 2);
                else SETW(0x9D0A, U(0x3660) << 2);
            }
            mission_log_event(code, o);
            nw = obj_wreck_shape((s16)o);
        }
        if (U(0x9F10) != 0) terr_set_scenery_state(U(0x9F10), nw);
    }
    SETW(0x3D68, o);
    snd_cue(5, 2);
    if (S(0xE2FE) == 2 && U(0x3D66) == (u16)o) SETW(0x9D0E, 1);
    if (U(0xDEDA) == 0) mfd_map_redraw(S(0xC0A4), S(0xC0B2));
    if (S(0x3654) < 2) det_alert_network();
}

/* 0000:0D08 terr_find_scenery_at(x, y'): the nearest destroyable scenery object around a world point (levels 1-2, 3x3 tiles, Manhattan distance); the hit record
 * is DS:BA14 {id, dist, x32, y32, tile record, key L k cx cy, new id}; 0 = none */
unsigned terr_find_scenery_at(long x, long y_inv)
{
    SETW(0xBA16, 0x7FFF);
    for (int L = 1; L <= 2; L++)
        for (int n = 0; n < 9; n++) {
            u32 lx = terr_to_level((s16)L, (u32)x);
            u16 cx = (u16)(lx >> 12), fx = (u16)lx & 0x0FFF;
            u32 ly = terr_to_level((s16)L, (u32)y_inv);
            u16 fy = (u16)ly & 0x0FFF;
            s16 ox = S(0x04CC + 2 * n), oy = S(0x04DE + 2 * n);
            u16 offx = (u16)(U((u16)(0x04F4 + 2 * ox)) - fx + 0x800);
            u16 offy = (u16)(U((u16)(0x04F4 + 2 * oy)) - fy + 0x800);
            u16 cy = (u16)(oy + (u16)(ly >> 12));
            cx = (u16)(cx + ox);
            s16 T = (s16)terr_tile_type((s16)L, (s16)cx, (s16)cy);
            if (T == -1) continue;
            u16 ti = (u16)(2 * (32 * L + T));
            SETW(0xE44A, U((u16)(0xE304 + ti)));
            for (u16 k = 0; U((u16)(0x061E + ti)) > k; k++, SETW(0xE44A, U(0xE44A) + 7)) {
                u16 p = U(0xE44A);
                if (DSB((u16)(0xC604 + (DSB(p + 6) & 0x7F))) == 0) continue;
                s16 rx = (s16)(U(p) + offx), ry = (s16)(U(p + 2) + offy);
                s16 d = (s16)(fl_abs16(ry) + fl_abs16(rx));
                if (L == 1) d = (s16)(d >> 2);
                else { rx = (s16)(rx << 2); ry = (s16)(ry << 2); }
                int flag = 0;
                u16 id = DSB(p + 6);
                if ((DSB(p + 6) & 0x80) && (u8)terr_scenery_override((s16)L, (s16)k, (s16)cx, (s16)cy) != 0) {
                    id = DSB((u16)(0xB776 + 5 * U(0x94DC)));
                    if (DSB((u16)(0x07E0 + (DSB(U(0xE44A) + 6) & 0x7F))) == 0) flag = 1;
                }
                p = U(0xE44A);
                if (d < S(0xBA16) && (DSB((u16)(0x07E0 + (DSB(p + 6) & 0x7F))) != 0 || U(0x3D76) != 0 || flag)) {
                    DSB(0xBA22) = (u8)L; DSB(0xBA23) = (u8)k; DSB(0xBA24) = (u8)cx; DSB(0xBA25) = (u8)cy;
                    SETW(0xBA20, p);
                    SETW(0xBA14, id);
                    SETW(0xBA16, d);
                    DSL(0xBA18) = (u32)((s32)rx + (s32)x);
                    DSL(0xBA1C) = (u32)((s32)ry + (s32)y_inv);
                }
            }
        }
    return S(0xBA16) != 0x7FFF ? 0xBA14 : 0;
}

/* 0000:0F31 terr_set_scenery_state(hit, newid): wreck ids go to the override table DS:B772, other ids into the tile record; the record is marked (bit 7) */
void terr_set_scenery_state(unsigned h, unsigned newid)
{
    u8 id = (u8)newid;
    DSB(h + 0x12) = id;
    if ((u8)terr_scenery_override(DSB(h + 0xE), DSB(h + 0xF), DSB(h + 0x10), DSB(h + 0x11)) != 0) {
        if ((s8)DSB(0xDEA4) == (s16)id || (s8)DSB(0xC0AC) == (s16)id) DSB((u16)(0xB776 + 5 * U(0x94DC))) = id;   /* sign-extended compare (Q-D1) */
        else DSB(U(h + 0xC) + 6) = id;
    } else {
        memmove(DSP((u16)(0xB772 + 5 * U(0x9902))), DSP(h + 0xE), 5);
        SETW(0x9902, U(0x9902) + 1);
    }
    DSB(U(h + 0xC) + 6) |= 0x80;
}
