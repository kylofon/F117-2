"""S5.6a: vectors for the render3d leaf routines of f117port/src/render3d, from the Python references of S3.7 (port/notes/s37/R7b_ref.py: 12EF clipper and
1359 raster; R7c_ref.py: 1433 maths, 1001 hz_draw, 1442 sky/ground), which match the original under Unicorn with 0 mismatches.

usage: r3d_leaf_ref.py <f117 executable> [workdir]
Writes <workdir>/r3d_leaf.vec (format: f117port/src/render3d/r3dtest.c), runs `f117 --headless --r3d-test` on it and passes its exit code on.
Exit 77 (skip) when work/unpacked/VGAME.EXE (the tables the references read) is not there."""
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 's37'))
try:
    import R7b_ref as B
    import R7c_ref as C
except (OSError, ImportError) as e:
    print('r3d-leaf: no game tables (%s), skipped' % e)
    sys.exit(77)

N = int(os.environ.get('R3D_CASES', '3000'))
rnd = random.Random(0x5E56A)
S16 = B.s16
S32 = B.s32

# ----------------------------------------------------------------------------------------------- the base state every case starts from
def w16(M, o, v):
    struct.pack_into('<H', M, o & 0xFFFF, v & 0xFFFF)


def r16(M, o):
    return struct.unpack_from('<H', M, o & 0xFFFF)[0]


G_M = bytearray(65536)
for i in range(200):
    w16(G_M, 0x85EC + 2 * i, i * 320)
for i in range(480):
    w16(G_M, 0x89AC + 2 * i, 0x7FFF)
    w16(G_M, 0x8D6E + 2 * i, 0x8001)
w16(G_M, 0x9130, 0x7FFF)
for i, v in enumerate(C.BASE9178):
    w16(G_M, 0x9178 + 2 * i, v)
for i, v in enumerate(C.TAB_D22):
    w16(G_M, 0xD22 + 2 * i, v)
for i in range(220):
    w16(G_M, 0x22A1 + 2 * i, 0xFFFF)
    w16(G_M, 0x2459 + 2 * i, 0)
w16(G_M, 0x2615, 0xFFFF)
w16(G_M, 0x2617, 0)
for o, v in ((0x85CA, 0), (0x85CC, 0), (0x85CE, 319), (0x85D0, 199), (0x2293, 319), (0x2295, 199), (0xD1E, 160), (0xD20, 100)):
    w16(G_M, o, v)
G_P = bytearray(rnd.getrandbits(8) for _ in range(65536))
TABS = bytes(C.IMG[C.T14B2:C.T14B2 + 0x3C00])


def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


class Skip(Exception):
    pass


def regions(old, new):
    out, n, i = [], len(old), 0
    while i < n:
        if old[i:i + 256] == new[i:i + 256]:                # skip equal blocks fast
            i += 256
            continue
        for k in range(i, min(i + 256, n)):
            pass
        break
    # block-skipping scan: find the changed positions, then merge them into runs
    pos = []
    for blk in range(0, n, 256):
        if old[blk:blk + 256] != new[blk:blk + 256]:
            pos.extend(k for k in range(blk, min(blk + 256, n)) if old[k] != new[k])
    j = 0
    while j < len(pos):
        e = j
        while e + 1 < len(pos) and pos[e + 1] == pos[e] + 1:
            e += 1
        out.append('%x:%s' % (pos[j], bytes(new[pos[j]:pos[e] + 1]).hex()))
        j = e + 1
    return ' '.join(out)


class Out:
    def __init__(s, path):
        s.f = open(path, 'w', newline='\n')
        s.f.write('G M 0 %s\n' % G_M.hex())
        s.f.write('G P 0 %s\n' % G_P.hex())
        s.f.write('G T 0 %s\n' % TABS.hex())
        s.counts = {}

    def case(s, name, args, inp, fn):
        """inp: list of (offset, bytes) written into the DGROUP; fn(M, P) runs the reference and returns (ret, log events). Skip = not a valid case."""
        M = bytearray(G_M)
        P = bytearray(G_P)
        for off, b in inp:
            M[off:off + len(b)] = b
        in_s = ' '.join('%x:%s' % (o, bytes(b).hex()) for o, b in inp)
        M0, P0 = bytes(M), bytes(P)
        try:
            ret, log = fn(M, P)
        except (B.Div0, B.NearRetBug, Skip):
            return False
        s.f.write('CASE %s %s\nIN %s\nRET %s\nOUT %s\nPOUT %s\nLOG %s\nEND\n' % (
            name, ' '.join('%x' % (a & 0xFFFFFFFF) for a in args), in_s, ret, regions(M0, M), regions(P0, P), '/'.join(log)))
        s.counts[name] = s.counts.get(name, 0) + 1
        return True

    def many(s, name, n, gen):
        done, tries = 0, 0
        while done < n and tries < n * 4:
            tries += 1
            if gen(s):
                done += 1


def b16(*vs):
    return struct.pack('<%dH' % len(vs), *[v & 0xFFFF for v in vs])


def b32(*vs):
    return struct.pack('<%dI' % len(vs), *[v & 0xFFFFFFFF for v in vs])


def window(big=False):
    r = rnd.random()
    if r < 0.6:
        return 0, 0, 319, 199
    xmin = rnd.randint(0, 120)
    ymin = rnd.randint(0, 80)
    xmax = rnd.randint(xmin, 330)
    ymax = rnd.randint(ymin, 215)
    if big and rnd.random() < 0.1:
        xmax, ymax = rnd.randint(xmin, 0x7FFF), rnd.randint(ymin, 0x7FFF)
    return xmin, ymin, xmax, ymax


def win_in(w):
    return [(0x85CA, b16(*w))]


def coord16(w):
    """a coordinate near / around the window"""
    r = rnd.random()
    if r < 0.4:
        return rnd.randint(w[0] - 40, w[2] + 40)
    if r < 0.6:
        return rnd.choice([w[0] - 1, w[0], w[2], w[2] + 1, w[1] - 1, w[1], w[3], w[3] + 1])
    if r < 0.9:
        return rnd.randint(-32768, 32767)
    return rnd.choice([-32768, 32767, -32767, 32766, 0])


def coord32(w):
    r = rnd.random()
    if r < 0.3:
        return coord16(w)
    if r < 0.6:
        return rnd.randint(-70000, 70000)
    if r < 0.8:
        return rnd.randint(-(1 << 24), 1 << 24)
    return rnd.randint(-(1 << 31), (1 << 31) - 1)


# ----------------------------------------------------------------------------------------------- 12EF
def gen_oc16(o):
    w = window(True)
    x, y = coord16(w), coord16(w)
    if rnd.random() < 0.1:
        w = (-rnd.randint(0, 30000), -rnd.randint(0, 30000), w[2], w[3])
    def fn(M, P):
        r = B.R(M, P)
        return '%02x' % r.oc16(x, y), []
    return o.case('outcode16', (x, y), win_in(w), fn)


def gen_oc32(o):
    w = window(True)
    x, y = coord32(w), coord32(w)
    def fn(M, P):
        r = B.R(M, P)
        return '%02x' % r.oc32(x & 0xFFFFFFFF, y & 0xFFFFFFFF), []
    return o.case('outcode32', (x, y), win_in(w), fn)


def gen_border(o):
    w = window(True)
    def pick(lo, hi):
        return rnd.choice([lo, hi, rnd.randint(lo, hi), rnd.randint(0, 70000)])
    x, y = pick(w[0], w[2]), pick(w[1], w[3])
    if rnd.random() < 0.2:
        x += rnd.choice([1 << 16, -(1 << 16)])
    def fn(M, P):
        r = B.R(M, P)
        return '%d' % (1 if r.on_border(x & 0xFFFFFFFF, y & 0xFFFFFFFF) else 0), []
    return o.case('on_border', (x, y), win_in(w), fn)


def rec_junk():
    return bytes(rnd.getrandbits(8) for _ in range(24))


def gen_flags16(o):
    w = window()
    rec = 0x5EBA + 24 * rnd.randint(0, 5)
    c0, c1 = rnd.randint(0, 15), rnd.randint(0, 15)
    y0, y1 = coord16(w), coord16(w)
    def fn(M, P):
        r = B.R(M, P)
        r.flags16(rec, c0, c1, y0, y1)
        return '-', []
    return o.case('flags16', (rec, c0, c1, y0, y1), win_in(w) + [(rec, rec_junk())], fn)


def gen_flags32(o):
    w = window()
    rec = 0x5EBA + 24 * rnd.randint(0, 5)
    c0, c1 = rnd.randint(0, 15), rnd.randint(0, 15)
    def fn(M, P):
        r = B.R(M, P)
        return '%02x' % r.flags32(rec, c0, c1), []
    return o.case('flags32', (rec, c0, c1), win_in(w) + [(rec, rec_junk())], fn)


def gen_intercept(o):
    w = window()
    x0, y0, x1, y1 = coord16(w), coord16(w), coord16(w), coord16(w)
    dx, dy = S16(x1 - x0), S16(y1 - y0)
    code = rnd.choice([0, 1, 2, 4, 5, 6, 8, 9, 10])
    if rnd.random() < 0.5:
        r0 = B.R(bytearray(G_M), None)
        r0.M[0x85CA:0x85D2] = b16(*w)
        code = r0.oc16(x0, y0)
    def fn(M, P):
        r = B.R(M, P)
        ok, x, y, f = r.intercept(code, x0, y0)
        return '%d %04x %04x %02x' % (0 if ok else 1, x & 0xFFFF, y & 0xFFFF, f), []
    return o.case('intercept', (code, x0, y0), win_in(w) + [(0x85AC, b16(dx, dy))], fn)


def gen_bisect(o, kind):
    w = window()
    p0 = (coord32(w), coord32(w))
    p1 = (coord32(w), coord32(w))
    if kind == 'bisect_out':
        p0 = (rnd.randint(w[0], w[2]), rnd.randint(w[1], w[3]))      # inside
    r0 = B.R(bytearray(G_M), None)
    r0.M[0x85CA:0x85D2] = b16(*w)
    c0 = r0.oc32(p0[0] & 0xFFFFFFFF, p0[1] & 0xFFFFFFFF)
    c1 = r0.oc32(p1[0] & 0xFFFFFFFF, p1[1] & 0xFFFFFFFF)
    inp = win_in(w) + [(0x8568, b32(p0[0], p0[1], p1[0], p1[1])), (0x85A8, b16(c0, c1))]
    def fn(M, P):
        r = B.R(M, P)
        if kind == 'bisect_in':
            m = r.bisect_in(0x8568, 0x8570)
            if m is None:
                return '1 %02x' % r.last_bp, []
            return '0 %08x %08x %02x' % (m[0] & 0xFFFFFFFF, m[1] & 0xFFFFFFFF, r.last_bp), []
        m = r.bisect_out(0x8568, 0x8570)
        return '%08x %08x' % (m[0] & 0xFFFFFFFF, m[1] & 0xFFFFFFFF), []
    return o.case(kind, (0x8568, 0x8570), inp, fn)


def gen_clip(o):
    w = window(True)
    rec = 0x5EBA + 24 * rnd.randint(0, 5)
    pts = [coord32(w) if rnd.random() < 0.35 else coord16(w) for _ in range(4)]
    if rnd.random() < 0.15:
        pts[2], pts[3] = pts[0], pts[1] if rnd.random() < 0.5 else pts[3]
    x0, y0, x1, y1 = pts
    def fn(M, P):
        r = B.R(M, P)
        r.clip_edge(rec)
        return '-', []
    return o.case('clip_edge', (rec,), win_in(w) + [(rec, b32(x0, y0, x1, y1) + rec_junk()[:8])], fn)


def gen_note(o, left):
    a, b = rnd.randint(-300, 300), rnd.randint(-300, 300)
    base = 0x85B4 if left else 0x85B8
    init = [(base, b16(rnd.choice([0x7FFF, rnd.randint(-100, 300)]), rnd.choice([0x8000, rnd.randint(-100, 300)])))]
    if not left:
        pass
    def fn(M, P):
        r = B.R(M, P)
        (r.note_left if left else r.note_right)(a, b)
        return '-', []
    # the walls' (lo, hi) pairs sit at 85B4,85B6 / 85B8,85BA; the pair is written as (lo, hi)
    return o.case('note_left' if left else 'note_right', (a, b), init, fn)


def gen_zclip(o):
    rec = 0x5EBA + 24 * rnd.randint(0, 3)
    n = rnd.randint(0, 4)

    def fn(M, P):                                           # 00E0 alone
        r = B.R(M, P)
        bx = 0x8588 if r.rw(0x85BE) == 0 else 0x8590
        r.ww(0x85BE, r.rw(0x85BE) + 1)
        M[bx:bx + 8] = M[rec + 0x10:rec + 0x18]
        return '-', []
    return o.case('note_zclip', (rec,), [(rec, rec_junk()), (0x85BE, b16(n))], fn)


def gen_add_edge(o):
    w = window()
    rec = 0x5EBA + 24 * rnd.randint(0, 5)
    x0, y0, x1, y1 = [rnd.randint(-30, 340) if i % 2 == 0 else rnd.randint(-30, 230) for i in range(4)]
    flags = rnd.choice([0, 0, 1, 2, 4, 8, 3, 5, 0xA, 0xC, 0x85, 0x8A, 0x80, 0x45, 0x40, 0x8585, 0x0F0F])
    junk = rec_junk()[:8]
    def fn(M, P):
        r = B.R(M, P)
        r.add_edge(rec)
        return '-', []
    inp = win_in(w) + [(rec, b16(x0, flags, y0, rnd.randint(-5, 205), x1, 0, y1, rnd.randint(-5, 205)) + junk), (0x85B2, b16(rnd.choice([0, 4])))]
    return o.case('add_edge', (rec,), inp, fn)


def shade_colour():
    t = rnd.random()
    hi = rnd.choice([0xFF, 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, rnd.randint(0, 0xFA), 0])
    lo = rnd.randint(0xA0, 0xFF) if (hi == 0xFF and t < 0.7) else rnd.randint(0, 255)
    return hi << 8 | lo


def gen_polygon(o):
    w = window()
    w = (w[0], w[1], w[2], min(w[3], 199))
    n = rnd.randint(2, 6)
    big = rnd.random()
    verts = []
    for _ in range(n):
        if big < 0.15:
            verts.append((coord32(w), coord32(w)))
        elif big < 0.5:
            verts.append((rnd.randint(-80, 400), rnd.randint(-60, 260)))
        else:
            verts.append((rnd.randint(w[0] - 10, w[2] + 10), rnd.randint(w[1] - 10, w[3] + 10)))
    zfl = [1 if rnd.random() < 0.15 else 0 for _ in range(n)]
    colour = shade_colour()
    inp = win_in(w) + [(0x4894, bytes([rnd.randint(0, 40)]))]
    for i in range(n):
        a, b = verts[i], verts[(i + 1) % n]
        inp.append((0x5EBA + 24 * i, b32(a[0], a[1], b[0], b[1]) + b32(rnd.randint(-50, 400), rnd.randint(-50, 250))))
    def fn(M, P):
        r = B.R(M, P)
        r.set_colour_M(colour)
        # set_colour_M is also run by poly_finish; running it first only matches the order of the real sequence: poly_finish sets the colour again
        M[0x85D6:0x85D8] = b'\0\0'
        for i in range(n):
            rec = 0x5EBA + 24 * i
            r.clip_edge(rec)
            if zfl[i]:
                r.ww(rec + 2, r.rw(rec + 2) | 0x4040)
            r.add_edge(rec)
        r.poly_finish(colour)
        return '-', []
    return o.case('polygon', [n, colour] + zfl, inp, fn)


# ----------------------------------------------------------------------------------------------- 1359
def gen_setcolor(o):
    ax = shade_colour() if rnd.random() < 0.8 else rnd.getrandbits(16)
    sh = rnd.randint(0, 255) if rnd.random() < 0.3 else rnd.randint(0, 40)
    def fn(M, P):
        B.R(M, P).set_colour_M(ax)
        return '-', []
    return o.case('setcolor', (ax,), [(0x4894, bytes([sh]))], fn)


def gen_dda(o):
    w = window()
    rec = 0x5EBA + 24 * rnd.randint(0, 5)
    x0, x1 = rnd.randint(w[0] - 5, w[2] + 5), rnd.randint(w[0] - 5, w[2] + 5)
    y0, y1 = rnd.randint(w[1] - 3, w[3] + 3), rnd.randint(w[1] - 3, w[3] + 3)
    if rnd.random() < 0.2:
        x1 = x0 + rnd.randint(-3, 3)
    if rnd.random() < 0.2:
        y1 = y0 + rnd.randint(-3, 3)
    inp = win_in(w) + [(rec, b16(x0, 0, y0, 0, x1, 0, y1, 0))]
    if rnd.random() < 0.5:                                  # tables with earlier content
        for _ in range(rnd.randint(1, 30)):
            row = rnd.randint(0, 199)
            inp.append((0x89AC + 2 * row, b16(rnd.randint(0, 300))))
            inp.append((0x8D6E + 2 * row, b16(rnd.randint(0, 300))))
        inp.append((0x9130, b16(rnd.randint(0, 220))))
    def fn(M, P):
        B.R(M, P).edge_dda(rec)
        return '-', []
    return o.case('edge_dda', (rec,), inp, fn)


def gen_wall(o):
    w = window()
    a, b = rnd.randint(-20, 225), rnd.randint(-20, 225)
    x = rnd.choice([w[0] - 1, w[2] + 1, rnd.randint(-5, 330)])
    inp = win_in(w)
    if rnd.random() < 0.4:
        for _ in range(rnd.randint(1, 30)):
            row = rnd.randint(0, 199)
            inp.append((0x89AC + 2 * row, b16(rnd.randint(0, 300))))
            inp.append((0x8D6E + 2 * row, b16(rnd.randint(0, 300))))
    def fn(M, P):
        B.R(M, P).wall_fill(a, b, x)
        return '-', []
    return o.case('wall_fill', (a, b, x), inp, fn)


def gen_fill(o):
    w = window()
    w = (w[0], w[1], w[2], min(w[3], 199))
    colour = shade_colour() if rnd.random() < 0.3 else (rnd.choice([0xFF, 0xFE, 0xFD, 0xFC, 0xFB, rnd.randint(0, 0xFA)]) << 8 | rnd.randint(0, 255))
    inp = win_in(w) + [(0x85D6, b16(colour))]
    miny = rnd.randint(0, 195)
    if rnd.random() < 0.05:
        miny = rnd.choice([0x7FFF, 0x7FFE, 0x8000, 0xFFFF])
    if miny >= 0x7FFE and rnd.random() < 0.6:                # the first-row test of the stipple fill is unsigned
        inp[1] = (0x85D6, b16(0xFC00 | rnd.randint(0, 255)))
    inp.append((0x9130, b16(miny)))
    if miny < 200:
        k = rnd.randint(1, 60)
        gap = rnd.randint(1, k) if rnd.random() < 0.3 else None
        for i in range(k):
            row = miny + i
            if row > 199 or i == gap:
                continue
            L = rnd.randint(w[0] - 10, w[2] + 10)
            R = rnd.randint(L, w[2] + 20) if rnd.random() < 0.85 else rnd.randint(w[0] - 20, w[2] + 20)
            inp.append((0x89AC + 2 * row, b16(L)))
            inp.append((0x8D6E + 2 * row, b16(R)))
    def fn(M, P):
        B.R(M, P).fill_M()
        return '-', []
    return o.case('fill_M', (), inp, fn)


def gen_line(o):
    w = window()
    rec = 0x5EBA + 24 * rnd.randint(0, 5)
    x0, x1 = rnd.randint(w[0], w[2]), rnd.randint(w[0], w[2])
    y0, y1 = rnd.randint(w[1], min(w[3], 199)), rnd.randint(w[1], min(w[3], 199))
    if rnd.random() < 0.1:
        x1, y1 = x0, y0
    if rnd.random() < 0.1:
        y1 = y0
    if rnd.random() < 0.1:
        x1 = x0
    col = rnd.randint(0, 255)
    inp = win_in(w) + [(rec, b16(x0, 0, y0, 0, x1, 0, y1, 0)), (0x85D6, b16(col))]
    def fn(M, P):
        B.R(M, P).line_M(rec)
        return '-', []
    return o.case('line_M', (rec,), inp, fn)


# ----------------------------------------------------------------------------------------------- 1433
def ang():
    r = rnd.random()
    if r < 0.1:
        return rnd.choice([0, 0x4000, 0x8000, 0xC000, 0xFFFF, 0x3FFF, 0x4001, 0x7FFF, 0x8001, 0xBFFF, 0xC001])
    return rnd.getrandbits(16)


def gen_sc(o, name, fn_):
    a = ang()
    return o.case(name, (a,), [], lambda M, P: ('%04x' % fn_(a), []))


def val16():
    r = rnd.random()
    if r < 0.15:
        return rnd.choice([0, 1, -1, 0x7FFF, -0x8000, -0x7FFF, 0x4000, -0x4000, 0x2000])
    if r < 0.5:
        return rnd.randint(-300, 300)
    return rnd.randint(-32768, 32767)


def gen_atan2(o):
    y, x = val16(), val16()
    return o.case('atan2', (y, x), [], lambda M, P: ('%04x' % C.atan2_(y, x), []))


def gen_asin(o):
    x = val16()
    return o.case('asin', (x,), [], lambda M, P: ('%04x' % C.asin_(x), []))


def gen_hypot(o):
    a, b = val16(), val16()
    if rnd.random() < 0.1:
        b = a
    return o.case('hypot2', (a, b), [], lambda M, P: ('%04x' % C.hypot2(a, b), []))


MA = 0x7000


def mat_words(n):
    return [val16() for _ in range(n)]


def gen_vec16(o):
    v, m = mat_words(3), mat_words(9)
    def fn(M, P):
        out = C.mat_vec16([x & 0xFFFF for x in v], [x & 0xFFFF for x in m])
        M[MA:MA + 6] = b16(*out)
        return '-', []
    return o.case('mat_vec16', (MA, MA + 0x10), [(MA, b16(*v)), (MA + 0x10, b16(*m))], fn)


def gen_matmul(o):
    a, b = mat_words(9), mat_words(9)
    mode = rnd.randint(0, 4)
    ao, bo = MA, MA + 0x20
    co = [MA + 0x40, ao, bo, ao + 2, bo + 6][mode]
    def fn(M, P):
        C.mat_mul_mem(M, ao, bo, co)
        return '-', []
    return o.case('mat_mul', (ao, bo, co), [(ao, b16(*a)), (bo, b16(*b))], fn)


def gen_transpose(o):
    m = mat_words(9)
    dst = rnd.choice([MA + 0x20, MA, MA + 2, MA + 6])
    def fn(M, P):
        C.transpose_mem(M, MA, dst)
        return '-', []
    return o.case('mat_transpose', (MA, dst), [(MA, b16(*m))], fn)


def gen_copy(o):
    m = mat_words(9)
    dst = rnd.choice([MA + 0x20, MA, MA + 2, MA - 4])
    def fn(M, P):
        for j in range(9):
            w16(M, dst + 2 * j, r16(M, MA + 2 * j))
        return '-', []
    return o.case('mat_copy', (MA, dst), [(MA, b16(*m))], fn)


def gen_vec32(o):
    v, m = mat_words(3), mat_words(9)
    def fn(M, P):
        out = C.vec_mat32([x & 0xFFFF for x in v], [x & 0xFFFF for x in m])
        M[MA + 0x30:MA + 0x3C] = b32(*out)
        return '-', []
    return o.case('vec_mat32', (MA, MA + 0x10, MA + 0x30), [(MA, b16(*v)), (MA + 0x10, b16(*m))], fn)


def gen_euler(o):
    a, b, c = ang(), ang(), ang()
    def fn(M, P):
        m, cache = C.mat_from_euler(a, b, c)
        M[MA:MA + 18] = b16(*m)
        M[0x916C:0x916C + 12] = b16(*cache)
        return '-', []
    return o.case('mat_from_euler', (a, b, c, MA), [], fn)


# ----------------------------------------------------------------------------------------------- 1001 hz_draw, 1442 sky
def view_state(w2h2=True):
    """the camera sin / cos words and flags the 3D view stores before the sky is drawn"""
    r = rnd.random()
    if r < 0.8:
        pa, ra = ang(), ang()
        if rnd.random() < 0.5:
            pa = rnd.randint(-0x3000, 0x3000)
        sB, cB, sC, cC = C.sin_(pa), C.cos_(pa), C.sin_(ra), C.cos_(ra)
    else:
        sB, cB, sC, cC = [rnd.getrandbits(16) for _ in range(4)]
    inp = [(0x4986, b16(sB, cB, sC, cC)), (0x433A, bytes([rnd.choice([0, 0, 1, 2, 3, 17, 31])])), (0x294B, bytes([rnd.choice([0, 1])]))]
    if w2h2:
        inp.append((0xD1E, b16(rnd.choice([160, 160, 120, rnd.randint(40, 200)]), rnd.choice([100, 100, 75, rnd.randint(30, 140)]))))
    return inp


def trace_events(M):
    ev = []
    for e in M.log:
        if e[0] == 31:
            ev.append('31 %04x %04x %04x %04x' % e[1:5])
        elif e[0] == 32:
            ev.append('32 %02x' % e[1])
        elif e[0] == 37:
            ev.append('37 %04x %04x %x %08x' % (e[1], e[2], e[3], fnv(e[4])))
        else:
            raise Skip()
    return ev


def gen_hz(o):
    xmax, ymax = rnd.choice([(319, 199), (319, 199), (rnd.randint(100, 319), rnd.randint(60, 199))])
    inp = view_state() + [(0x2293, b16(xmax, ymax)), (0xD46, bytes([rnd.randint(0, 255)]))]
    def fn(M, P):
        mm = C.Mem(M)
        mm.snap = True
        C.hz_draw(mm)
        return '-', trace_events(mm)
    return o.case('hz_draw', (), inp, fn)


def gen_sky(o):
    xmax, ymax = rnd.choice([(319, 199), (319, 199), (rnd.randint(100, 319), rnd.randint(60, 199))])
    ctx = 0x3FCC
    detail = rnd.choice([0, 1, 2, 3, 3, 4, 5, 0xFF, 0xFE, 0x80, 0x7F])
    inp = view_state() + [(0x2293, b16(xmax, ymax)), (ctx + 4, b16(rnd.choice([0x88, rnd.getrandbits(16)]))), (0x98DE, bytes([detail])),
                          (0x4360, b16(rnd.choice([0, 1]))), (0x43B2, b16(rnd.choice([0, 1]))), (0x4980, b16(rnd.choice([rnd.randint(0, 3000), rnd.randint(-32768, 32767), 0])))]
    def fn(M, P):
        mm = C.Mem(M)
        mm.snap = True
        calls = C.sky_ground(mm, ctx)
        if calls and calls[0][0] == 'fill':
            M[0xD46] = calls[0][1] & 0xFF
            C.hz_draw(mm)
            return '-', trace_events(mm)
        ev = []
        for c in calls:
            ev.append('100 %02x ' % c[1] + ' '.join('%04x %04x' % (x & 0xFFFF, y & 0xFFFF) for x, y in c[2]))
        return '-', ev
    return o.case('sky', (ctx,), inp, fn)


def main():
    exe = os.path.abspath(sys.argv[1])
    work = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(exe), 'r3d_leaf')
    os.makedirs(work, exist_ok=True)
    vec = os.path.join(work, 'r3d_leaf.vec')
    o = Out(vec)
    o.many('mat_from_euler', N, gen_euler)
    o.many('sin', N, lambda o: gen_sc(o, 'sin', lambda a: C.sin_(a)))
    o.many('cos', N, lambda o: gen_sc(o, 'cos', lambda a: C.cos_(a)))
    o.many('atan2', N, gen_atan2)
    o.many('asin', N, gen_asin)
    o.many('hypot2', N, gen_hypot)
    o.many('mat_vec16', N, gen_vec16)
    o.many('mat_mul', N, gen_matmul)
    o.many('mat_transpose', N, gen_transpose)
    o.many('mat_copy', N, gen_copy)
    o.many('vec_mat32', N, gen_vec32)
    o.many('outcode16', N, gen_oc16)
    o.many('outcode32', N, gen_oc32)
    o.many('on_border', N, gen_border)
    o.many('flags16', N, gen_flags16)
    o.many('flags32', N, gen_flags32)
    o.many('intercept', N, gen_intercept)
    o.many('bisect_in', N, lambda o: gen_bisect(o, 'bisect_in'))
    o.many('bisect_out', N, lambda o: gen_bisect(o, 'bisect_out'))
    o.many('clip_edge', 2 * N, gen_clip)
    o.many('note_left', N // 3, lambda o: gen_note(o, True))
    o.many('note_right', N // 3, lambda o: gen_note(o, False))
    o.many('note_zclip', N // 3, gen_zclip)
    o.many('add_edge', N, gen_add_edge)
    o.many('setcolor', N // 2, gen_setcolor)
    o.many('edge_dda', N, gen_dda)
    o.many('wall_fill', N, gen_wall)
    o.many('fill_M', N, gen_fill)
    o.many('line_M', N, gen_line)
    o.many('polygon', N, gen_polygon)
    o.many('hz_draw', N, gen_hz)
    o.many('sky', N, gen_sky)
    o.f.close()
    print('r3d-leaf: %d cases written to %s' % (sum(o.counts.values()), vec))
    sys.stdout.flush()
    return subprocess.call([exe, '--headless', '--r3d-test', vec])


if __name__ == '__main__':
    sys.exit(main())
