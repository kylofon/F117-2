"""R7b differential tests: real VGAME code (Unicorn) vs R7b_ref.py.
usage: python port/notes/s37/R7b_emu.py [ncases]"""
import random
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR
from unicorn.x86_const import *

sys.path.insert(0, 'C:/Coding/F117-2/port/notes/s37')
import R7b_ref as ref  # noqa: E402

D = open('C:/Coding/F117-2/work/unpacked/VGAME.EXE', 'rb').read()
IMG = D[struct.unpack_from('<H', D, 8)[0] * 16:]
DSEG = 0x1E23
PSEG = 0x4000
RETL = 0xF0000


class Emu:
    def __init__(s):
        s.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        s.uc.mem_map(0, 0x110000)
        s.uc.mem_write(0, IMG)
        s.uc.mem_write(RETL, b'\xf4')
        s.uc.hook_add(UC_HOOK_CODE, lambda uc, a, z, u: uc.emu_stop(), begin=RETL, end=RETL)
        s.intr = None
        s.uc.hook_add(UC_HOOK_INTR, s.hk_intr)
        s.base = bytes(s.uc.mem_read(DSEG * 16, 0x10000))

    def hk_intr(s, uc, intno, ud):
        s.intr = intno
        uc.emu_stop()

    def ds(s):
        return bytearray(s.uc.mem_read(DSEG * 16, 0x10000))

    def page(s):
        return bytearray(s.uc.mem_read(PSEG * 16, 0x10000))

    def set_ds(s, M):
        s.uc.mem_write(DSEG * 16, bytes(M))

    def set_page(s, P):
        s.uc.mem_write(PSEG * 16, bytes(P))

    def far(s, seg, off, si=0, ax=0, args=()):
        uc = s.uc
        for r in (UC_X86_REG_DS, UC_X86_REG_SS):
            uc.reg_write(r, DSEG)
        uc.reg_write(UC_X86_REG_ES, PSEG)
        sp = 0xF800
        ssb = DSEG * 16
        for a in reversed(args):
            sp -= 2
            uc.mem_write(ssb + sp, (a & 0xFFFF).to_bytes(2, 'little'))
        sp -= 4
        uc.mem_write(ssb + sp, struct.pack('<HH', 0, RETL >> 4))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_SI, si)
        uc.reg_write(UC_X86_REG_AX, ax)
        uc.reg_write(UC_X86_REG_BP, 0x1234)
        uc.reg_write(UC_X86_REG_CS, seg)
        s.intr = None
        uc.emu_start(seg * 16 + off, RETL + 1, count=5000000)
        cs = uc.reg_read(UC_X86_REG_CS)
        ip = uc.reg_read(UC_X86_REG_IP)
        return cs * 16 + ip == RETL


def rnd_coord(r):
    k = r.random()
    if k < 0.45:
        return r.randint(-400, 700)
    if k < 0.65:
        return r.randint(-40000, 40000)
    if k < 0.80:
        return r.randint(-0x7FFFFFFF, 0x7FFFFFFF)
    if k < 0.90:
        return r.choice([-0x8000, 0x7FFF, -0x8001, 0x8000, 0xFFFF, 0x10000, -0x10000, 0x10001,
                         -1, 0, 319, 320, 199, 200, -0x80000000, 0x7FFFFFFF, 0x00010001])
    return r.randint(-33000, 33000)


WINDOWS = [(0, 0, 319, 199), (0, 0, 319, 139), (8, 4, 311, 150), (100, 50, 220, 120)]


def setwin(M, w):
    for o, v in zip((0x85CA, 0x85CC, 0x85CE, 0x85D0), w):
        M[o:o + 2] = struct.pack('<h', v)


def test_clip(e, n, r):
    bad = 0
    div0 = 0
    bug = 0
    paths = {}
    for i in range(n):
        M = bytearray(e.base)
        setwin(M, r.choice(WINDOWS) if r.random() < 0.9 else (-20, -10, 300, 190))
        pts = [rnd_coord(r) for _ in range(4)]
        if r.random() < 0.2:   # degenerate: both ends equal
            pts[2], pts[3] = pts[0], pts[1]
        rec = 0x5EBA
        for k, v in enumerate(pts):
            M[rec + 4 * k:rec + 4 * k + 4] = struct.pack('<i', v)
        M[rec + 16:rec + 24] = bytes(r.randrange(256) for _ in range(8))
        e.set_ds(M)
        ok = e.far(0x12EF, 0x333, si=rec)
        G = e.ds()
        R = ref.R(bytearray(M))
        try:
            R.clip_edge(rec)
            rexc = None
        except ref.Div0:
            rexc = 'div0'
        except ref.NearRetBug:
            rexc = 'bug'
        if e.intr == 0 or rexc == 'div0':
            div0 += 1
            if not (e.intr == 0 and rexc == 'div0'):
                bad += 1
                print('div0 mismatch', pts, e.intr, rexc)
            continue
        if rexc == 'bug':
            bug += 1
            continue
        f = G[rec + 2]
        paths[f] = paths.get(f, 0) + 1
        if not ok or G[0x5EBA:0x5ED2] != R.M[0x5EBA:0x5ED2] or G[0x8568:0x85C2] != R.M[0x8568:0x85C2]:
            bad += 1
            if bad < 6:
                print('clip mismatch', pts, 'win', struct.unpack_from('<4h', M, 0x85CA))
                print('  emu', struct.unpack_from('<12h', G, rec), 'ok', ok)
                print('  ref', struct.unpack_from('<12h', R.M, rec))
    print('clip_edge: %d cases, %d mismatches, %d div0 (both), %d near-ret bug, flag values %d distinct'
          % (n, bad, div0, bug, len(paths)))
    return paths


def fresh_tables(M):
    for i in range(480):
        M[0x89AC + 2 * i:0x89AE + 2 * i] = b'\xff\x7f'
        M[0x8D6E + 2 * i:0x8D70 + 2 * i] = b'\x01\x80'
    M[0x9130:0x9132] = b'\xff\x7f'


def prep(e, r, win=None):
    M = bytearray(e.base)
    setwin(M, win or r.choice(WINDOWS))
    R = ref.R(M)
    R.rows_M()
    R.ww(0x85EA, PSEG)
    fresh_tables(M)
    return M


def test_dda(e, n, r):
    bad = 0
    for i in range(n):
        M = prep(e, r)
        nedges = r.randint(1, 4)
        recs = []
        for k in range(nedges):
            if r.random() < 0.85:
                xs = [r.randint(-5, 330) for _ in range(2)]
                ys = [r.randint(-3, 205) for _ in range(2)]
            else:
                x = r.randint(0, 319); y = r.randint(0, 199)
                xs = [x, x + r.choice([0, 0, 1, -1, 2])]
                ys = [y, y + r.choice([0, 0, 1, -1, 3])]
            recs.append(struct.pack('<hhhhhhhh', xs[0], 0, ys[0], 0, xs[1], 0, ys[1], 0))
        R = ref.R(bytearray(M))
        e.set_ds(M)
        for rb in recs:
            e.uc.mem_write(DSEG * 16 + 0x5EBA, rb)
            R.M[0x5EBA:0x5EBA + 16] = rb
            e.far(0x1359, 0x719, si=0x5EBA)
            R.edge_dda(0x5EBA)
        G = e.ds()
        if G[0x89AC:0x9132] != R.M[0x89AC:0x9132]:
            bad += 1
            if bad < 5:
                print('dda mismatch', [struct.unpack('<8h', x) for x in recs])
    print('edge_dda: %d cases, %d mismatches' % (n, bad))


def test_line(e, n, r):
    bad = 0
    for i in range(n):
        M = prep(e, r, (0, 0, 319, 199))
        x0, x1 = r.randint(0, 319), r.randint(0, 319)
        y0, y1 = r.randint(0, 199), r.randint(0, 199)
        if r.random() < 0.3:
            x1 = min(319, max(0, x0 + r.randint(-3, 3))); y1 = min(199, max(0, y0 + r.randint(-3, 3)))
        rb = struct.pack('<hhhhhhhh', x0, 0, y0, 0, x1, 0, y1, 0)
        M[0x5EBA:0x5ECA] = rb
        col = r.randrange(0x10000)
        M[0x85D6:0x85D8] = struct.pack('<H', col)
        P = bytearray(r.randrange(256) for _ in range(64000)) + bytearray(1536)
        e.set_ds(M); e.set_page(P)
        M[0x85C2:0x85C4] = b'\x70\x00'
        e.set_ds(M)
        e.far(0x1359, 0x49, si=0x5EBA)
        R = ref.R(bytearray(M), bytearray(P))
        R.line_M(0x5EBA)
        if e.page() != R.P:
            bad += 1
            if bad < 5:
                print('line mismatch', x0, y0, x1, y1)
    print('line_M: %d cases, %d mismatches' % (n, bad))


def rnd_poly(r):
    k = r.random()
    nv = r.randint(3, 7)
    if k < 0.5:
        cx, cy = r.randint(-100, 420), r.randint(-80, 280)
        rad = r.choice([5, 30, 120, 400, 3000])
        vs = [(cx + r.randint(-rad, rad), cy + r.randint(-rad, rad)) for _ in range(nv)]
    elif k < 0.8:
        vs = [(r.randint(-300, 620), r.randint(-200, 400)) for _ in range(nv)]
    else:
        vs = [(rnd_coord(r), rnd_coord(r)) for _ in range(nv)]
    return vs


def test_poly(e, n, r):
    bad = 0
    div0 = 0
    for i in range(n):
        M = prep(e, r)
        M[0x4894] = r.choice([0, 0, 3, 0x10, 0x25, 0xF0])
        P = bytearray(r.randrange(256) for _ in range(64000)) + bytearray(1536)
        R = ref.R(bytearray(M), bytearray(P))
        e.set_ds(M); e.set_page(P)
        vs = rnd_poly(r)
        near = set(r.sample(range(len(vs)), r.choice([1, 2, 2, 3]))) if r.random() < 0.25 else set()
        err = False
        for j in range(len(vs)):
            a, b = vs[j], vs[(j + 1) % len(vs)]
            rec = struct.pack('<iiii', a[0], a[1], b[0], b[1])
            rec += struct.pack('<ii', r.randint(-500, 800), r.randint(-300, 500))
            e.uc.mem_write(DSEG * 16 + 0x5EBA, rec)
            R.M[0x5EBA:0x5ED2] = rec
            e.far(0x12EF, 0x333, si=0x5EBA)
            try:
                R.clip_edge(0x5EBA)
            except ref.Div0:
                err = True
            if e.intr == 0 or err:
                break
            if j in near:
                for X in (e, None):
                    if X is e:
                        G = e.ds(); G[0x5EBC] |= 0x40; G[0x5EBD] |= 0x40; e.set_ds(G)
                    else:
                        R.M[0x5EBC] |= 0x40; R.M[0x5EBD] |= 0x40
            e.far(0x12EF, 0x3E, si=0x5EBA)
            R.add_edge(0x5EBA)
        if err or e.intr == 0:
            div0 += 1
            continue
        col = r.choice([r.randrange(256) | 0xFF00, r.randrange(0xA0, 0x100) | 0xFF00, r.randrange(0x10000),
                        r.randrange(256) | 0xFE00, r.randrange(256) | 0xFD00, r.randrange(256) | 0xFC00,
                        r.randrange(256) | 0xFB00])
        e.far(0x12EF, 0x10A, ax=0, args=(col,))
        R.poly_finish(col)
        G = e.ds()
        if e.page() != R.P or G[0x8588:0x85D8] != R.M[0x8588:0x85D8] or G[0x89AC:0x9142] != R.M[0x89AC:0x9142]:
            bad += 1
            if bad < 5:
                diffs = [k for k in range(0x10000) if G[k] != R.M[k]]
                pd = [k for k in range(64000) if e.page()[k] != R.P[k]]
                print('poly mismatch', vs, hex(col), 'ds diffs', [hex(k) for k in diffs[:8]], 'page diffs', len(pd), pd[:5])
    print('polygon (0333+003E+010A+1359 M): %d cases, %d mismatches, %d div0' % (n, bad, div0))


if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    r = random.Random(7)
    e = Emu()
    test_clip(e, n * 10, r)
    test_dda(e, n * 2, r)
    test_line(e, n // 2, r)
    test_poly(e, n, r)
    for k in sorted(ref.COV):
        print('  cov %-40s %d' % (k, ref.COV[k]))
