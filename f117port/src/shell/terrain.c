/* The terrain tile map and object records of a theatre (.3DG / .3DT, spec shell.md Part D section 4: START 0000:7C05 .. 7E88), used by the
 * mission generator to snap positions to objects (map_nearest_object) and to pick an object of a type (map_pick_object). The tables stay in
 * the DGROUP of the loaded image at their original offsets: tile maps B3A0..BAA0, record pool BAB0, record pointers C85C, counts F0A, the
 * ObjHit result CA8E. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell/shell.h"

#define HIT 0xCA8E

/* START 0000:7C05 map_level_scale: world units to the units of a level */
long map_level_scale(int level, long v)
{
    switch ((s16)level) {
    case 0: return (long)((u32)v << 1);
    case 1: return v;
    case 2: return (long)rtl_aFulshr((unsigned long)v, 2);
    case 3: return (long)rtl_aFulshr((unsigned long)v, 4);
    case 4: return (long)rtl_aFulshr((unsigned long)v, 6);
    default: return level;                                      /* the original falls out with AX = level */
    }
}

/* START 0000:7C56 map_tile_type: the tile type (0..255) of cell (x, y) of a level, or -1 outside the level */
int map_tile_type(int level, int x, int y)
{
    s16 lv = (s16)level, xx = (s16)x, yy = (s16)y;
    if (xx < 0 || yy < 0) return -1;
    if ((s16)DSW((u16)(START_DS_map_level_size + 2 * lv)) <= xx) return -1;
    if ((s16)DSW((u16)(START_DS_map_level_size + 2 * lv)) <= yy) return -1;
    switch (lv) {
    case 0: case 1: case 2: {
        static const u16 tabs[3] = { START_DS_map_tile_l0, START_DS_map_tile_l1, START_DS_map_tile_l2 };
        int parent = map_tile_type(lv + 1, xx >> 2, yy >> 2);
        return DSB((u16)(tabs[lv] + (u16)((parent << 4) + ((yy & 3) << 2) + (xx & 3))));
    }
    case 3: return DSB((u16)(START_DS_map_tile_l3 + (yy << 4) + xx));
    case 4: return DSB((u16)(START_DS_map_tile_l4 + (yy << 2) + xx));
    default: return lv;
    }
}

/* START 0000:7D71 map_load_3dg: the five tile maps; a missing file gives identity / empty maps. Always returns 0. */
int map_load_3dg(char *name)
{
    FILE *f = fopen_fn(name, ds_str(0x1088));
    if (!f) {
        for (int i = 0; i < 16; i++) DSB((u16)(START_DS_map_tile_l4 + i)) = (u8)i;
        memset(DSP(START_DS_map_tile_l3), 0, 0x100);
        memset(DSP(START_DS_map_tile_l2), 0, 0x200);
        memset(DSP(START_DS_map_tile_l1), 0, 0x200);
        memset(DSP(START_DS_map_tile_l0), 0, 0x200);
        return 0;
    }
    fread_fn(DSP(0x104C), 2, 1, f);
    if (DSW(0x104C) != 0x3232) { fclose(f); return 0; }
    fread_fn(DSP(START_DS_map_tile_l4), 0x10, 1, f);
    fread_fn(DSP(START_DS_map_tile_l3), 0x100, 1, f);
    fread_fn(DSP(START_DS_map_tile_l2), 0x200, 1, f);
    fread_fn(DSP(START_DS_map_tile_l1), 0x200, 1, f);
    fread_fn(DSP(START_DS_map_tile_l0), 0x200, 1, f);
    fclose(f);
    return 0;
}

/* START 0000:7E88 map_load_3dt: the object records of every (level, tile type) into the 7-byte record pool */
int map_load_3dt(char *name)
{
    FILE *f = fopen_fn(name, ds_str(0x108B));
    if (!f) return 0;
    fread_fn(DSP(0x104E), 2, 1, f);
    if (DSW(0x104E) != 0x3131) { fclose(f); return 0; }
    fread_fn(DSP(START_DS_map_type_count), 2, 5, f);
    for (int i = 0; i < 5; i++) {
        if (DSW((u16)(START_DS_map_type_count + 2 * i)) > 0x20) return 0;
        fread_fn(DSP((u16)(START_DS_map_rec_count + 64 * i)), 2, DSW((u16)(START_DS_map_type_count + 2 * i)), f);
    }
    u16 p = 0;
    for (int i = 0; i < 5; i++) {
        for (u16 j = 0; DSW((u16)(START_DS_map_type_count + 2 * i)) > j; j++) {
            DSW((u16)(START_DS_map_rec_ptr + 2 * (i * 32 + j))) = (u16)(START_DS_map_rec_pool + p);
            for (u16 k = 0; DSW((u16)(START_DS_map_rec_count + 2 * (i * 32 + j))) > k; k++) {
                if ((s16)p > 0xDAC) return 0;
                u16 obj;
                fread_fn(DSP((u16)(START_DS_map_rec_pool + p)), 2, 1, f);
                fread_fn(DSP((u16)(START_DS_map_rec_pool + 2 + p)), 2, 1, f);
                fread_fn(DSP((u16)(START_DS_map_rec_pool + 4 + p)), 2, 1, f);
                fread_fn(&obj, 2, 1, f);
                DSB((u16)(START_DS_map_rec_pool + 6 + p)) = (u8)obj;
                p = (u16)(p + 7);
            }
        }
    }
    fclose(f);
    return 0;
}

/* START 0000:7744 map_nearest_object: the nearest object record (levels 1 and 2, the cell and its 8 neighbours) of a world position;
 * the result is the ObjHit at DS:CA8E, NULL when nothing was found */
u8 *map_nearest_object(long wx, long wy)
{
    DSW(HIT + 2) = 0x7FFF;
    for (int L = 1; L <= 2; L++) {
        for (int k = 0; k < 9; k++) {
            long lx = map_level_scale(L, wx), ly = map_level_scale(L, wy);
            u16 cx0 = (u16)rtl_aFulshr((unsigned long)lx, 12);
            u16 lxlow = (u16)lx & 0x0FFF;
            u16 ylow = (u16)ly & 0x0FFF;
            s16 dxk = (s16)DSW((u16)(START_DS_map_nb_dx + 2 * k)), dyk = (s16)DSW((u16)(START_DS_map_nb_dy + 2 * k));
            s16 off_x = (s16)(DSW((u16)(START_DS_map_cell_step + 2 * dxk)) - lxlow + 0x800);
            s16 off_y = (s16)(DSW((u16)(START_DS_map_cell_step + 2 * dyk)) - ylow + 0x800);
            u16 cy = (u16)(dyk + (u16)rtl_aFulshr((unsigned long)ly, 12));
            u16 cx = (u16)(cx0 + dxk);
            int t = map_tile_type(L, (s16)cx, (s16)cy);
            if (t == -1) continue;
            u16 rec = DSW((u16)(START_DS_map_rec_ptr + 2 * (L * 32 + t)));
            u16 cnt = DSW((u16)(START_DS_map_rec_count + 2 * (L * 32 + t)));
            for (u16 j = 0; cnt > j; j++, rec = (u16)(rec + 7)) {
                if (DSB((u16)(START_DS_wld_tbl_category + DSB((u16)(rec + 6)))) == 0) continue;
                s16 px = (s16)(DSW(rec) + off_x), py = (s16)(DSW((u16)(rec + 2)) + off_y);
                s16 d = (s16)(abs_fn(py) + abs_fn(px));
                if (L == 1) d = (s16)(d >> 2);
                else { px = (s16)(px << 2); py = (s16)(py << 2); }
                if (d < (s16)DSW(HIT + 2)) {
                    DSB(HIT + 0xE) = (u8)L;
                    DSB(HIT + 0xF) = (u8)j;
                    DSB(HIT + 0x10) = (u8)cx;
                    DSB(HIT + 0x11) = (u8)cy;
                    DSW(HIT + 0xC) = rec;
                    DSW(HIT) = DSB((u16)(rec + 6));
                    DSW(HIT + 2) = (u16)d;
                    DSL(HIT + 4) = (u32)((s32)px + (s32)wx);
                    DSL(HIT + 8) = (u32)((s32)py + (s32)wy);
                }
            }
        }
    }
    return DSW(HIT + 2) != 0x7FFF ? DSP(HIT) : NULL;
}

/* START 0000:7904 map_pick_object: a random record of the object type `obj_id` (a list of candidates is built in the file buffer, 8 bytes
 * each: record offset, segment, x, y, level); the ObjHit gets the object id and the absolute position. NULL when there is none. */
u8 *map_pick_object(int obj_id)
{
    u8 *list = mp(DSW(START_DS_file_buf_seg), DSW(START_DS_file_buf_off));
    u8 *cand = list;
    int n = 0;
    s16 oid = (s8)obj_id;
    DSW(HIT + 2) = 0x7FFF;
    if (DSB((u16)(START_DS_wld_tbl_category + oid)) == 0x10) {
        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 16; y++) {
                if (DSB((u16)(0xCBC8 + x - (y << 4))) & 3) continue;
                int t = map_tile_type(3, x, y);
                if (t == -1) continue;
                u16 rec = DSW((u16)(START_DS_map_rec_ptr + 2 * (3 * 32 + t)));
                for (u16 j = 0; DSW((u16)(START_DS_map_rec_count + 2 * (3 * 32 + t))) > j; j++, rec = (u16)(rec + 7)) {
                    if (DSB((u16)(rec + 6)) != oid) continue;
                    cand[0] = (u8)rec; cand[1] = (u8)(rec >> 8); cand[2] = (u8)cur_dgroup; cand[3] = (u8)(cur_dgroup >> 8);
                    cand[6] = 3;
                    cand[4] = (u8)(x << 4);
                    cand[5] = (u8)(y << 4);
                    cand += 8;
                    n++;
                }
            }
        }
    } else {
        int step = 1;
        for (int L = 1; L <= 2; L++, step += 3) {
            for (int a = step; a < 0x100; a += step) {
                for (int e = step; e < 0x100; e += step) {
                    if (DSB((u16)(0xCBC8 + (a >> 4) - (e & 0xF0))) & 3) continue;
                    int t = L == 1 ? map_tile_type(1, a, e) : map_tile_type(2, a >> 2, e >> 2);
                    if (t == -1) continue;
                    u16 rec = DSW((u16)(START_DS_map_rec_ptr + 2 * (L * 32 + t)));
                    for (u16 j = 0; DSW((u16)(START_DS_map_rec_count + 2 * (L * 32 + t))) > j; j++, rec = (u16)(rec + 7)) {
                        if (DSB((u16)(rec + 6)) != oid) continue;
                        cand[0] = (u8)rec; cand[1] = (u8)(rec >> 8); cand[2] = (u8)cur_dgroup; cand[3] = (u8)(cur_dgroup >> 8);
                        cand[6] = (u8)L;
                        cand[4] = (u8)a;
                        cand[5] = (u8)e;
                        cand += 8;
                        n++;
                    }
                }
            }
        }
    }
    if (n == 0) return NULL;
    u8 *p = list;
    if (n > 1) p += (u16)(ui_rand_mod(n) << 3);
    u16 rec = (u16)(p[0] | p[1] << 8);
    DSW(HIT) = DSB((u16)(rec + 6));
    DSL(HIT + 4) = (u32)p[4] << 12;
    DSL(HIT + 8) = (u32)p[5] << 12;
    s32 ax, ay;
    switch (p[6]) {
    case 1:
        ax = (s16)(DSW(rec) + 0x800); ay = (s16)(DSW((u16)(rec + 2)) + 0x800);
        break;
    case 2:
        ax = (s16)((u16)(DSW(rec) + 0x800) << 2); ay = (s16)((u16)(DSW((u16)(rec + 2)) + 0x800) << 2);
        break;
    case 3:
        ax = (s32)(s16)(u16)(DSW(rec) << 4) + 0x8000; ay = (s32)(s16)(u16)(DSW((u16)(rec + 2)) << 4) + 0x8000;
        break;
    default: return DSP(HIT);
    }
    DSL(HIT + 4) += (u32)ax;
    DSL(HIT + 8) += (u32)ay;
    return DSP(HIT);
}
