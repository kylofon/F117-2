/* The 3DX object renderer, segment 11EB (spec port/spec/render3d.md Part 4): camera matrix 000C, the object entry 00D3 (and the probe 01B8), frustum test,
 * parts, planes, BSP order, vertices, projection with the INT 0 saturation of 0975, the near-plane bisection, edges and the face-list interpreter. ES is the
 * 3DX bank segment word DS:8560 throughout; every pointer argument is a DS offset or an ES offset as in the original, so the work buffers keep their
 * adjacency (part slots 7B1C.. run into the eye vectors 7C3C exactly as in the original, spec "Part limit"). */
#include <string.h>

#include "render3d/r3d.h"

#define BANK()  DSW(0x8560)
#define ESW(o)  rd16(BANK(), (u16)(o))
#define ESS(o)  ((s16)rd16(BANK(), (u16)(o)))
#define ESB(o)  rd8(BANK(), (u16)(o))

static inline u16 abs16(s16 v) { return (u16)(v < 0 ? (u16)-(u16)v : (u16)v); }        /* 0000:F302: abs16(-32768) = 8000h */
static inline u32 abs32(s32 v) { return v < 0 ? (u32)0 - (u32)v : (u32)v; }

/* 11EB:000C r3d_set_camera: spec render3d.md "11EB:000C r3d_set_camera" */
void r3d_set_camera(u16 mat9, s16 zoom)
{
    u16 k = abs16((s16)(zoom << 1));
    if (k < 0x100) k = 0x100;
    DSW(0x7D2E) = (u16)(0x007FFF00UL / k);
    mat_copy(mat9, 0x7AF8);
    mat_transpose(0x7AF8, 0x7B0A);
    s16 c = (s16)-(s16)DSW(0x7D2E);
    for (int i = 2; i <= 8; i += 3) {
        u16 o = (u16)(0x7AF8 + 2 * i);
        DSW(o) = (u16)(((u32)((s32)c * DSS(o)) << 1) >> 16);
    }
    static const u8 half[6] = { 0, 3, 6, 1, 4, 7 };
    for (int j = 0; j < 6; j++) { u16 o = (u16)(0x7AF8 + 2 * half[j]); DSS(o) = (s16)(DSS(o) >> 1); }
    for (int i = 1; i <= 7; i += 3) {
        u16 o = (u16)(0x7AF8 + 2 * i);
        if (DSW(0x7D3A)) DSS(o) = (s16)-DSS(o);
        else { s16 a = (s16)-DSS(o); DSS(o) = (s16)(a - (a >> 2)); }
    }
}

/* 11EB:02B6 r3d_frustum_test: 1 = visible (AX = 0, ZF = 1) */
int r3d_frustum_test(s16 radius)
{
    s16 zh = DSS(0x7CAC);
    if ((u16)((u16)(abs16(zh) + 1) << 1) >= DSW(0x7D2E)) return 0;
    s16 t = (s16)(zh + radius + zh);
    if (t < 0) return 0;
    if ((u16)(abs32(DSSL(0x7CA2)) >> 16) > (u16)t) return 0;
    if ((u16)(abs32(DSSL(0x7CA6)) >> 16) > (u16)t) return 0;
    return 1;
}

/* 11EB:04CD r3d_light_root / 11EB:04F9 r3d_light_part: the light vector in the part frame (only feeds the dead shade bytes) */
void r3d_light_root(u16 mat)
{
    DSW(0x7D4C) = 0x7C6C;
    DSW(0x7C6C) = DSW(0x7D54); DSW(0x7C6E) = DSW(0x7D56); DSW(0x7C70) = DSW(0x7D58);
    mat_vec16(0x7C6C, mat);
    DSW(0x7D4C) += 6;
}

void r3d_light_part(u16 mat)
{
    u16 l = DSW(0x7D4C);
    DSW(l) = DSW(0x7C6C); DSW((u16)(l + 2)) = DSW(0x7C6E); DSW((u16)(l + 4)) = DSW(0x7C70);
    mat_vec16(l, mat);
    DSW(0x7D4C) += 6;
}

/* 11EB:0440 r3d_part_pivot (BX = eye slot, DI = slot + 12h, ES:SI = pivot): origin_k = origin + pivot*M_0 - pivot*M_k */
void r3d_part_pivot(u16 eye, u16 di, u16 si)
{
    for (int c = 0; c < 3; c++) DSW((u16)(eye + 2 * c)) = ESW(si + 2 * c);
    vec_mat32(eye, DSW(0x7D20), 0x7D0E);
    for (int c = 0; c < 3; c++) DSW((u16)(eye + 2 * c)) = (u16)-DSW((u16)(eye + 2 * c));
    u16 org = DSW(0x7D26);
    vec_mat32(eye, (u16)(di - 0x12), org);
    for (int c = 0; c < 3; c++) {
        u16 o = (u16)(org + 4 * c);
        DSL(o) = DSL(o) + DSL((u16)(0x7CA2 + 4 * c)) + DSL((u16)(0x7D0E + 4 * c));
    }
}

/* 11EB:031B r3d_setup_parts (CX = nparts, ES:SI after it): part matrices, eyes, lights, part origins; returns SI after the pivots */
u16 r3d_setup_parts(u16 si, s16 nparts)
{
    DSW(0x7D28) = (u16)nparts;
    DSW(0x7C3C) = (u16)-DSW(0x7C9C); DSW(0x7C3E) = (u16)-DSW(0x7C9E); DSW(0x7C40) = (u16)-DSW(0x7CA0);
    if (nparts <= 0) { DSW(0x7D20) = 0x7AF8; return si; }               /* quirk Q1: the object matrix is ignored */
    DSW(0x7D20) = 0x7B1C;
    mat_mul_1433_0286(DSW(0x7D1C), 0x7AF8, 0x7B1C);
    mat_transpose(DSW(0x7D1C), 0x7B1C + 0x12);
    DSW(0x7D20) += 0x24;
    for (int k = 1; k < nparts; k++) {
        u8 var = ESB(si);
        s8 axis = (s8)ESB(si + 1);
        si = (u16)(si + 2);
        u16 ang = DSW((u16)(DSW(0x7D1E) + 2 * var));
        u16 a = 0, b = 0, c = 0;
        if (axis == 1) b = ang; else if (axis < 1) a = ang; else c = ang;
        u16 t = (u16)(DSW(0x7D20) + 0x24);
        mat_from_euler(a, b, c, t);
        mat_mul_1433_0286(t, 0x7B1C, DSW(0x7D20));
        mat_transpose(t, (u16)(DSW(0x7D20) + 0x12));
        DSW(0x7D20) += 0x24;
    }
    DSW(0x7D20) = 0x7B1C;
    for (int i = 0; i < 12; i += 2) DSW((u16)(0x7CAE + i)) = DSW((u16)(0x7CA2 + i));
    DSW(0x7D26) = 0x7CAE;
    mat_vec16(0x7C3C, 0x7B2E);
    r3d_light_root(0x7B2E);
    u16 bx = 0x7C3C, di = 0x7B2E;
    for (int k = 1; k < nparts; k++) {
        DSW(0x7D26) += 0x0C;
        bx = (u16)(bx + 6); di = (u16)(di + 0x24);
        r3d_part_pivot(bx, di, si);
        for (int c = 0; c < 3; c++) DSW((u16)(bx + 2 * c)) = (u16)(DSW((u16)(0x7C3C + 2 * c)) - ESW(si + 2 * c));
        si = (u16)(si + 6);
        mat_vec16(bx, di);
        r3d_light_part(di);
    }
    DSW(0x7D26) += 0x0C;
    return si;
}

/* 11EB:0658 r3d_clear_vertex_flags */
void r3d_clear_vertex_flags(void)
{
    u16 n = (u16)((u16)(abs16(DSS(0x7D2C)) + 2) >> 1);
    for (u16 i = 0; i < n; i++) DSW((u16)(0x79D2 + 2 * i)) = 0;
}

/* 11EB:0678 r3d_plane_shade (normal at ES:nrm, plane index di): bits 14..21 of the light dot product; the byte is never read (dead in the original) */
void r3d_plane_shade(u16 nrm, u16 plane)
{
    u16 l = DSW(0x7D4C);
    u32 s = (u32)((s32)ESS(nrm) * DSS(l)) + (u32)((s32)ESS(nrm + 2) * DSS((u16)(l + 2))) + (u32)((s32)ESS(nrm + 4) * DSS((u16)(l + 4)));
    DSB((u16)(0x77D2 + plane)) = (u8)(s >> 14);
}

/* 11EB:0E5F r3d_bsp_order: the recursion equivalent of the state machine (spec); vis is read when a node is entered */
static u16 bsp_out;
static void bsp_visit(u16 pairs, u16 vis, u8 n)
{
    u8 b0 = ESB(pairs + 2 * n), b1 = ESB(pairs + 2 * n + 1);
    if (DSB((u16)(vis + n)) != 0) {
        if (b1 != 0xFF) bsp_visit(pairs, vis, b1);
        DSB(bsp_out++) = n;
        if (b0 != 0xFF) { if (b0 & 0x80) DSB(bsp_out++) = b0; else bsp_visit(pairs, vis, b0); }
    } else {
        if (b0 != 0xFF) { if (b0 & 0x80) DSB(bsp_out++) = b0; else bsp_visit(pairs, vis, b0); }
        if (b1 != 0xFF) bsp_visit(pairs, vis, b1);
    }
}

void r3d_bsp_order(u16 si, u16 out, u16 vis)
{
    DSW(0x7D4A) = vis;
    bsp_out = out;
    bsp_visit((u16)(si + 1), vis, ESB(si));
    DSB(bsp_out) = 0xFF;
}

/* 11EB:0565 r3d_planes (ES:SI = nplanes): visibility 76D2, shade 77D2, vertex marks 79D2, BSP order 78D2; returns SI at the vertex list */
u16 r3d_planes(u16 si)
{
    s16 np = ESS(si);
    si = (u16)(si + 2);
    DSW(0x7D2A) = (u16)np;
    if (np == 0) return si;
    u16 cnt = abs16(np), di = 0, eye = 0x7C3C;
    DSW(0x7D4C) = 0x7C6C;
    do {
        DSB(0x7D38) = 0xFF;
        for (;;) {
            u16 w = ESW(si);
            si = (u16)(si + 2);
            if ((w >> 8) == 0x80) { u16 pp = w & 0xFF; eye = (u16)(0x7C3C + 6 * pp); DSW(0x7D4C) = (u16)(0x7C6C + 6 * pp); continue; }
            if (w == 0x8100) {
                u16 v = ESB(si) & 0x1F;
                s16 val = (s8)ESB(si + 1);
                si = (u16)(si + 2);
                if ((u16)val != DSW((u16)(DSW(0x7D1E) + 2 * v))) DSB(0x7D38) = 0x7F;
                continue;
            }
            u16 nrm = (u16)(si - 2);
            u32 s = (u32)((s32)(s16)w * DSS(eye)) + (u32)((s32)ESS(si) * DSS((u16)(eye + 2))) + (u32)((s32)ESS(si + 2) * DSS((u16)(eye + 4)))
                  + ((u32)ESW(si + 4) | (u32)ESW(si + 6) << 16);
            si = (u16)(si + 8);
            u8 dl = (s32)s >= 0 ? 0xFF : 0x00;
            if (DSB(0x7D5E) != 0) dl = 0xFF;
            dl &= DSB(0x7D38);
            DSB((u16)(0x76D2 + di)) = dl;
            if (dl & 0x80) {
                DSB(0x7D5C) |= dl;
                r3d_plane_shade(nrm, di);
            }
            break;
        }
        di++;
    } while (--cnt);
    r3d_clear_vertex_flags();
    for (u16 p = 0; p < abs16(np); p++) {
        u8 n = ESB(si);
        si = (u16)(si + 1);
        if (n == 0) continue;
        if (DSB((u16)(0x76D2 + p)) > 0x7F) { while (n--) { DSB((u16)(0x79D2 + ESB(si))) = 1; si = (u16)(si + 1); } }
        else si = (u16)(si + n);
    }
    if (np < 0) {
        r3d_bsp_order(si, 0x78D2, 0x76D2);
        si = (u16)(si + 2 * abs16(np) + 1);
    }
    return si;
}

/* 11EB:0906 r3d_project (DI = s32 x,y,z, BX = s32 x,y out) with the INT 0 handler 0975 as a checked division */
static s32 proj_div(s32 n, s16 d)
{
    s32 q = n / d;
    if (q < -32768 || q > 32767) return (s32)(s16)((n >> 8) / d) * 256;        /* 11EB:0975 */
    return (s16)q;
}

void r3d_project(u16 di, u16 bx)
{
    s8 m = (s8)DSB(0x7D5E);
    if (m != 0) {
        for (int k = 0; k < 2; k++) {
            s16 h = DSS((u16)(di + 4 * k + 2)), r;
            if (m >= 0x10) {
                int c = m & 0x0F;
                r = (s16)(h >> c);
                if (c && ((h >> (c - 1)) & 1)) r++;
            } else {
                int c = (u8)(0x10 - m) & 0x1F;
                r = c < 16 ? (s16)(u16)((u16)h << c) : 0;
            }
            DSSL((u16)(bx + 4 * k)) = r;
        }
        return;
    }
    s16 zh = DSS((u16)(di + 0xA));
    if (zh >= 0x100) {
        for (int k = 0; k < 2; k++) DSSL((u16)(bx + 4 * k)) = proj_div(DSSL((u16)(di + 4 * k)) >> 8, zh);
        return;
    }
    if (zh >= 1) {
        s16 d = (s16)(u16)(DSW((u16)(di + 9)) >> 1);
        for (int k = 0; k < 2; k++) DSSL((u16)(bx + 4 * k)) = proj_div(DSSL((u16)(di + 4 * k)) >> 1, d);
        return;
    }
    DSW((u16)(bx + 2)) = 0x8000;
}

/* 11EB:0819 r3d_xform_one: one vertex (after any part words) times the matrix BX plus the origin, then projected */
static void xform_one(u16 *si, u16 *bx, u16 *di, u16 *bp)
{
    for (;;) {
        u16 w = ESW(*si);
        if (w >= 0x8000 && w < 0x8010) {
            u16 pp = w & 0x0F;
            *bx = (u16)(DSW(0x7D20) + 0x24 * pp);
            for (int i = 0; i < 12; i += 2) DSW((u16)(0x7CA2 + i)) = DSW((u16)(0x7CAE + 0x0C * pp + i));
            *si = (u16)(*si + 2);
            continue;
        }
        break;
    }
    s32 x = ESS(*si), y = ESS(*si + 2), z = ESS(*si + 4);
    u16 m = *bx;
    for (int k = 0; k < 3; k++)
        DSL((u16)(*di + 4 * k)) = (u32)(x * DSS((u16)(m + 2 * k))) + (u32)(y * DSS((u16)(m + 6 + 2 * k))) + (u32)(z * DSS((u16)(m + 12 + 2 * k)))
                                 + DSL((u16)(0x7CA2 + 4 * k));
    r3d_project(*di, *bp);
    *bp = (u16)(*bp + 8); *di = (u16)(*di + 16); *si = (u16)(*si + 6);
}

/* 11EB:06B9 r3d_xform_vertices: all vertices (nplanes 0) or those marked in 79D2; part words switch the matrix and the origin */
u16 r3d_xform_vertices(u16 si)
{
    u16 bx = DSW(0x7D20);
    s16 n = DSS(0x7D2C);
    if (n == 0) return si;
    if (n < 0) {                                                        /* 075E: the shared-vertex path (count 0 in all shipped banks) */
        u16 cx = (u16)-n;
        DSW(0x7D1A) = 0xC688;
        do {
            u8 i = ESB(si);
            u16 src = (u16)(0x4EB6 + 16 * i), dst = (u16)(0xC688 + 16 * i);
            for (int k = 0; k < 3; k++) DSL((u16)(dst + 4 * k)) = DSL((u16)(src + 4 * k)) + DSL((u16)(0x7CA2 + 4 * k));
            r3d_project(dst, (u16)(0xD688 + 8 * i));
            si = (u16)(si + 1);
        } while (--cx);
        return si;
    }
    u16 cx = (u16)n, di = 0xC688, bp = 0xD688;
    DSW(0x7D1A) = 0xC688;
    if (DSW(0x7D2A) == 0) {
        do xform_one(&si, &bx, &di, &bp); while (--cx);
        return si;
    }
    u16 dx = 0;
    for (;;) {
        u16 w = ESW(si);
        if (w >= 0x8000 && w < 0x8010) {
            u16 pp = w & 0x0F;
            bx = (u16)(DSW(0x7D20) + 0x24 * pp);
            for (int i = 0; i < 12; i += 2) DSW((u16)(0x7CA2 + i)) = DSW((u16)(0x7CAE + 0x0C * pp + i));
            si = (u16)(si + 2);
            continue;
        }
        if (DSB((u16)(0x79D2 + dx)) == 0) { bp = (u16)(bp + 8); di = (u16)(di + 16); si = (u16)(si + 6); }
        else xform_one(&si, &bx, &di, &bp);
        dx++;
        if (--cx == 0) return si;
    }
}

/* 11EB:0990 r3d_near_bisect(p0 front, p1 behind; DS offsets of s32 x,y,z, copied as the stack arguments): the near-plane point -> 7D42 / 7D46 */
void r3d_near_bisect(u16 p0o, u16 p1o)
{
    s32 p0[3], p1[3], m[3];
    for (int c = 0; c < 3; c++) { p0[c] = DSSL((u16)(p0o + 4 * c)); p1[c] = DSSL((u16)(p1o + 4 * c)); }
    for (;;) {
        for (int c = 0; c < 3; c++) m[c] = (s32)((u32)p0[c] + (u32)p1[c] + 1u) >> 1;
        DSSL(0x7D42) = m[0]; DSSL(0x7D46) = m[1];
        u16 h = (u16)((u32)m[2] >> 16);
        if (h <= 1) return;
        if ((s16)(h - 1) < 0) memcpy(p1, m, sizeof m);
        else memcpy(p0, m, sizeof m);
    }
}

/* 11EB:0B3F r3d_make_edge (BP = record, DI = 8*v0, BX = 8*v1) */
void r3d_make_edge(u16 rec, u16 v0x8, u16 v1x8)
{
    u16 P0 = (u16)(0xD688 + v0x8), P1 = (u16)(0xD688 + v1x8);
    u32 cx = DSW(0x85D2), cy = DSW(0x85D4);
    bool b0 = DSW((u16)(P0 + 2)) == 0x8000, b1 = DSW((u16)(P1 + 2)) == 0x8000;
    if (!b0 && !b1) {
        DSL(rec) = DSL(P0) + cx; DSL((u16)(rec + 4)) = DSL((u16)(P0 + 4)) + cy;
        DSL((u16)(rec + 8)) = DSL(P1) + cx; DSL((u16)(rec + 0xC)) = DSL((u16)(P1 + 4)) + cy;
        r3d_clip_edge(rec);
        return;
    }
    if (b0 && b1) { DSW((u16)(rec + 2)) = 0x8080; return; }
    u16 V0 = (u16)(0xC688 + 2 * v0x8), V1 = (u16)(0xC688 + 2 * v1x8);
    if (b1) {
        DSL(rec) = DSL(P0) + cx; DSL((u16)(rec + 4)) = DSL((u16)(P0 + 4)) + cy;
        r3d_near_bisect(V0, V1);
    } else {
        DSL((u16)(rec + 8)) = DSL(P1) + cx; DSL((u16)(rec + 0xC)) = DSL((u16)(P1 + 4)) + cy;
        r3d_near_bisect(V1, V0);
    }
    u32 nx = ((u32)(DSSL(0x7D42) >> 8) << 1) + cx, ny = ((u32)(DSSL(0x7D46) >> 8) << 1) + cy;
    u16 o = b1 ? 8 : 0;
    DSL((u16)(rec + o)) = nx; DSL((u16)(rec + 0x10)) = nx;
    DSL((u16)(rec + o + 4)) = ny; DSL((u16)(rec + 0x14)) = ny;
    r3d_clip_edge(rec);
    DSW((u16)(rec + 2)) |= 0x4040;
}

/* 11EB:0A37 r3d_build_edges (ES:SI = nedges): the edges whose two vertices are needed (all when nplanes is 0); the others keep stale records */
u16 r3d_build_edges(u16 si)
{
    u16 bp = R3D_EDGES, cx = ESW(si);
    si = (u16)(si + 2);
    if (cx == 0) return si;
    do {
        u8 v0 = ESB(si), v1 = ESB(si + 1);
        si = (u16)(si + 2);
        if (DSW(0x7D2A) == 0 || (DSB((u16)(0x79D2 + v0)) & DSB((u16)(0x79D2 + v1))) != 0) r3d_make_edge(bp, (u16)(8 * v0), (u16)(8 * v1));
        bp = (u16)(bp + 0x18);
    } while (--cx);
    return si;
}

/* 11EB:0CED r3d_face_list: the element stream (end FFh, condition 80h..FEh, points 20h bit, lines 40h bit, polygon) */
void r3d_face_list(u16 si)
{
    for (;;) {
        u8 b = ESB(si);
        si = (u16)(si + 1);
        if (b == 0xFF) return;
        if (b & 0x80) {                                                  /* 0CCA */
            u16 v = b & 0x7F;
            s16 val = (s8)ESB(si);
            si = (u16)(si + 1);
            if ((u16)val == DSW((u16)(DSW(0x7D1E) + 2 * v))) continue;
            u8 l = ESB(si);
            si = (u16)(si + 1);
            if (l == 0xFF) return;
            si = (u16)(si + (l & 0x1F) + 1);
            continue;
        }
        if (b & 0x20) { si = (u16)(si + 1 + (b & 0x1F)); continue; }     /* 0DFE: points, computed and dropped */
        if (b & 0x40) {                                                  /* 0D5F: lines */
            u16 n = b & 0x1F;
            u8 col = ESB(si);
            si = (u16)(si + 1);
            gfxlib_dispatch_a(DSW((u16)(DSW(0x7D50) + 2 * col)));
            do {
                u16 r = (u16)(R3D_EDGES + 0x18 * ESB(si));
                si = (u16)(si + 1);
                DSW(0x7D4E) = r;
                if (!(DSB((u16)(r + 2)) & 0x80)) gfxlib_dispatch_b(r);
            } while (--n);
            continue;
        }
        u16 n = b;                                                       /* 0D04: polygon */
        DSW(0x7D3E) = n; DSW(0x7D40) = si;
        if (ESB(si + n) == 0xFF) { si = (u16)(si + n + 1); continue; }
        do {
            r3d_poly_add_edge((u16)(R3D_EDGES + 0x18 * ESB(si)));
            si = (u16)(si + 1);
        } while (--n);
        u8 col = ESB(si);
        si = (u16)(si + 1);
        r3d_poly_finish((s16)DSW((u16)(DSW(0x7D50) + 2 * col)));
    }
}

/* 11EB:0CAC r3d_draw_face_plane (BX = plane, ES:SI = offset table) */
void r3d_draw_face_plane(u16 plane, u16 si)
{
    if (DSB((u16)(0x76D2 + plane)) > 0x7F) {
        DSB(0x7D5A) = DSB((u16)(0x77D2 + plane));
        r3d_face_list(ESW(si + 2 * plane));
    }
}

/* 11EB:0C7A r3d_draw_faces: file order, the single list of a model without planes, or the BSP order */
void r3d_draw_faces(u16 si)
{
    s16 n = DSS(0x7D2A);
    if (n > 0) { for (s16 p = 0; p < n; p++) r3d_draw_face_plane((u16)p, si); }
    else if (n == 0) r3d_face_list(si);
    else for (u16 q = 0x78D2; DSB(q) != 0xFF; q++) r3d_draw_face_plane(DSB(q), si);
}

/* 11EB:0281 r3d_hook_div0 / 02A2 r3d_unhook_div0: the original saves IVT[0] in DS:5EB6 and points it at 0975; the port divides with a check instead
 * (r3d_project), so only the saved copy is written (PORT: IVT[0] itself is left alone) */
void r3d_hook_div0(void) { DSW(0x5EB6) = rd16(0, 0); DSW(0x5EB8) = rd16(0, 2); }
void r3d_unhook_div0(void) { }

/* the common head of 00D3 and 01B8: LOD, radius, frustum, parts, planes. Returns SI after the planes, 0 when the frustum test failed. */
static u16 object_head(u16 obj, s16 x, s16 y, s16 z, u16 mat9, bool frustum_always)
{
    DSW(0x7C9C) = (u16)x; DSW(0x7C9E) = (u16)y; DSW(0x7CA0) = (u16)z; DSW(0x7D1C) = mat9;
    vec_mat32(0x7C9C, 0x7AF8, 0x7CA2);
    u16 si = ESW(2 * obj + 2);
    u16 az = abs16(DSS(0x7CAC)), md, mdl;
    do { md = ESW(si); mdl = ESW(si + 2); si = (u16)(si + 4); } while (az > md);
    si = mdl;
    u16 radius = ESW(si);
    si = (u16)(si + 2);
    if (!(radius > az)) DSB(0x7D5C) = 0x80;
    if ((frustum_always || DSB(0x7D5E) == 0) && !r3d_frustum_test((s16)radius)) { DSB(0x7D5C) = 0x80; return 0; }
    s16 np = ESS(si);
    si = r3d_setup_parts((u16)(si + 2), np);
    DSW(0x7D2C) = ESW(si);
    si = r3d_planes((u16)(si + 2));
    if (abs16(DSS(0x7D2A)) < 4) DSB(0x7D5C) = 0x80;
    return si;
}

/* 11EB:00D3 r3d_draw_object(obj, x, y, z, mat9, vars): x, y, z = object origin minus camera in model axes; mat9 / vars = DS offsets */
void r3d_draw_object(u16 obj, s16 x, s16 y, s16 z, u16 mat9, u16 vars)
{
    DSB(0x7D5C) = 0;
    if (!(ESW(0) > obj)) return;
    DSW(0x7D1E) = vars;
    u16 si = object_head(obj, x, y, z, mat9, false);
    if (si) {
        r3d_hook_div0();
        si = r3d_xform_vertices(si);
        si = r3d_build_edges(si);
        r3d_draw_faces(si);
        r3d_unhook_div0();
    }
    DSW(0xC686) |= (u16)((DSW(0x7D5C) & 0x80) ^ 0x80);
}

/* 11EB:01B8 r3d_probe_object: as 00D3 up to the planes, no drawing; 7D1E (vars) stays stale and the frustum test always runs */
void r3d_probe_object(u16 obj, s16 x, s16 y, s16 z, u16 mat9)
{
    DSB(0x7D5C) = 0;
    if (!(ESW(0) > obj)) return;
    object_head(obj, x, y, z, mat9, true);
    DSW(0xC686) |= (u16)((DSW(0x7D5C) & 0x80) ^ 0x80);
}

/* 11EB:0F9B r3d_shared_verts: the shared vertices of the bank times the camera matrix (count 0 in all shipped banks) */
void r3d_shared_verts(void)
{
    u16 si = ESW(2 * ESW(0) + 2), cnt = ESW(si);
    si = (u16)(si + 2);
    for (u16 k = 0; k < cnt; k++, si = (u16)(si + 6)) {
        s32 x = ESS(si), y = ESS(si + 2), z = ESS(si + 4);
        for (int c = 0; c < 3; c++)
            DSL((u16)(0x4EB6 + 16 * k + 4 * c)) = (u32)(x * DSS((u16)(0x7AF8 + 2 * c))) + (u32)(y * DSS((u16)(0x7AFE + 2 * c))) + (u32)(z * DSS((u16)(0x7B04 + 2 * c)));
    }
}
