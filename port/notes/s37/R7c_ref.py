"""R7c Python reference of VGAME far segments 1433 (fixed-point math), 1001 (2D edge tables) and 0000:1442
(sky/ground bands). Bit-exact model used by R7c_emu.py (Unicorn differential test)."""
import struct

D = open('C:/Coding/F117-2/work/unpacked/VGAME.EXE', 'rb').read()
IMG = D[struct.unpack_from('<H', D, 8)[0] * 16:]
T14B2 = 0x14B20
DSB = 0x1E230


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def u16(v):
    return v & 0xFFFF


def s32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


def tw(off):  # word of segment 14B2
    return struct.unpack_from('<H', IMG, T14B2 + off)[0]


BASE9178 = struct.unpack_from('<8H', IMG, DSB + 0x9178)
TAB_D22 = struct.unpack_from('<17h', IMG, DSB + 0xD22)


# ---------------------------------------------------------------- 1433 math
def sin_(a):
    a = u16(a)
    bx = a >> 2
    if a & 0x4000:
        bx = ~bx
    v = tw(bx & 0xFFE)
    if a & 0x8000:
        v = -v
    return u16(v)


def cos_(a):
    a = u16(a)
    bx = a >> 2
    if not a & 0x4000:
        bx = ~bx
    v = tw(bx & 0xFFE)
    if (a + 0x4000) & 0x8000:
        v = -v
    return u16(v)


def q15(a, b):  # (int16)((a*b) >> 15), floor; = shl ax,1 / rcl dx,1
    return u16((s16(a) * s16(b)) >> 15)


def mul_sin(k, a):
    return q15(k, sin_(a))


def mul_cos(k, a):
    return q15(k, cos_(a))


def atan2_(y, x):
    cx, dx, bl = u16(x), u16(y), 0
    if cx & 0x8000:
        bl += 8
        cx = u16(-cx)
    if dx & 0x8000:
        bl += 4
        dx = u16(-dx)
    if cx < dx:
        bl += 2
        cx, dx = dx, cx
    base = BASE9178[bl >> 1]
    if dx >= cx or cx == 0:
        idx2 = 0x1FFE
    else:
        ratio = (dx << 16) // cx
        idx2 = (ratio >> 4) << 1
    t = tw(0x1000 + idx2)
    if base & 0x80:
        return u16((base & 0xFF00) - t)
    return u16((base & 0xFF00) + t)


def asin_(x):
    x = u16(x)
    bx = u16(-x) if x & 0x8000 else x
    v = tw(0x3202 + ((bx >> 4) & 0x7FE))
    return u16(-v) if x & 0x8000 else v


def iabs16(v):  # cwd/xor/sub: 0x8000 stays 0x8000
    v = u16(v)
    return u16(-v) if v & 0x8000 else v


def hypot2(a, b):
    ax, bx = iabs16(a), iabs16(b)
    if ax == bx:
        cx = bx
        t = tw(0x3200)
    else:
        L, S = max(ax, bx), min(ax, bx)
        cx = L
        t = tw(0x3000 + (((S << 16) // L) >> 8) * 2)
        bx = L
    r = u16(((t * bx) >> 16) + bx)
    return cx if r < cx else r


def hypot3(x, y, z):
    h = hypot2(x, y)
    if h & 0x8000:
        return u16(h + (iabs16(z) >> 1))
    return hypot2(h, z)


def hi(p):
    return u16(p >> 16)


def mat_vec16(v, M):  # in place on v (list of 3 u16), M 9 words
    x, y, z = (s16(t) for t in v)
    m = [s16(t) for t in M]
    out = []
    for k in range(3):
        out.append(u16((hi(x * m[k]) + hi(y * m[3 + k]) + hi(z * m[6 + k])) << 1))
    return out


def mat_mul_mem(mem, a, b, c):
    """1433:0286 on a word-addressable dict/bytearray-like memory (aliasing exact). mem: bytearray DS."""
    def rd(o):
        return s16(struct.unpack_from('<H', mem, o & 0xFFFF)[0])

    def wr(o, v):
        struct.pack_into('<H', mem, o & 0xFFFF, u16(v))
    for i in range(3):
        si = a + 6 * i
        for k in range(3):
            s = rd(si) * rd(b + 2 * k) + rd(si + 2) * rd(b + 6 + 2 * k) + rd(si + 4) * rd(b + 12 + 2 * k)
            s = s32(s)
            wr(0x916A, (s << 1))
            wr(c + 6 * i + 2 * k, (s >> 15))


def transpose_mem(mem, src, dst):
    order = (0, 6, 12, 2, 8, 14, 4, 10, 16)
    for j in range(9):
        v = struct.unpack_from('<H', mem, src + 2 * j)[0]
        struct.pack_into('<H', mem, dst + order[j], v)


def vec_mat32(v, M):
    x, y, z = (s16(t) for t in v)
    m = [s16(t) for t in M]
    return [s32(x * m[k] + y * m[3 + k] + z * m[6 + k]) & 0xFFFFFFFF for k in range(3)]


def mat_from_euler(a, b, c):
    SA, CA, SC, CC, SB, CB = sin_(a), cos_(a), sin_(c), cos_(c), sin_(b), cos_(b)
    q = q15
    m = [0] * 9
    m[0] = u16(q(CC, CA) - q(q(SC, SB), SA))
    m[1] = u16(q(q(CC, SB), SA) + q(SC, CA))
    m[2] = u16(-q(CB, SA))
    m[3] = u16(-q(SC, CB))
    m[4] = q(CC, CB)
    m[5] = SB
    m[6] = u16(q(q(SC, SB), CA) + q(CC, SA))
    m[7] = u16(q(SC, SA) - q(q(CC, SB), CA))
    m[8] = q(CB, CA)
    cache = (SA, CA, SB, CB, SC, CC)   # 916C 916E 9170 9172 9174 9176
    return m, cache


def mat_to_euler(M):
    b = asin_(M[5])
    if iabs16(b) > 0x3E80:
        a = atan2_(M[6], M[0])
        c = 0
    else:
        a = atan2_(u16(-M[2]), M[8])
        c = atan2_(u16(-M[3]), M[4])
    return a, b, c


def mulq(a, b):
    r = q15(a, b)
    return r, (0xFFFF if r & 0x8000 else 0)


def muldiv(a, b, c):
    """returns (ax, dx) or None for #DE (idiv overflow)"""
    a, b, c = s16(a), s16(b), s16(c)
    cx = iabs16(c)
    if cx == 0:
        c = 1
    if iabs16(b) >= cx:
        if b == -32768 and c == -1:
            return None
        q = int(b / c)
        p = s32(q * a)
        return u16(p), u16(p >> 16)
    n = a * b
    q = int(n / c)
    if not -32768 <= q <= 32767:
        return None
    return u16(q), (0xFFFF if q < 0 else 0)


def div32sat(lo, hi_, d):
    hi_s = s16(hi_)
    cx = iabs16(d)
    sgn = 0xFFFF if hi_s < 0 else 0
    ax = u16((iabs16(hi_) + 1) << 1)
    if ax > cx:
        return u16((0x7FFF ^ sgn) - sgn), sgn
    n = s32((u16(hi_) << 16) | u16(lo))
    dv = s16(cx)
    if dv == 0:
        return None
    q = int(n / dv)
    if not -32768 <= q <= 32767:
        return None
    return u16(q), (0xFFFF if q < 0 else 0)


def imul_(a, b):
    p = s32(s16(a) * s16(b))
    return u16(p), u16(p >> 16)


def mat_from_euler_b(p6, p8, pa):
    S1, C1 = sin_(pa), cos_(pa)
    S3, C3 = sin_(p6), cos_(p6)
    S2, C2 = sin_(p8), cos_(p8)
    q = q15
    m = [0] * 9
    m[0] = u16(q(q(S1, S2), S3) + q(C1, C3))
    m[1] = q(C2, S3)
    m[2] = u16(q(q(C1, S2), S3) - q(S1, C3))
    m[3] = u16(q(q(S1, S2), C3) - q(C1, S3))
    m[4] = q(C2, C3)
    m[5] = u16(q(q(C1, S2), C3) + q(S1, S3))
    m[6] = q(S1, C2)
    m[7] = u16(-S2)
    m[8] = q(C1, C2)
    return m, (S1, C1, S2, C2, S3, C3)


def mul15r(a, b):  # 109D:0006
    return u16((s16(a) * s16(b) + 0x4000) >> 15)


# ---------------------------------------------------------------- 1001 edge tables
class Mem:
    """DS memory + log of driver slot calls."""

    def __init__(s, ds):
        s.m = ds
        s.log = []
        s.clobber = False
        s.snap = False

    def slot37(s, mode=0):
        e = (37, s.r(0x2615), s.r(0x2617), mode)
        if s.snap:
            e = e + (bytes(s.m[0x22A1:0x22A1 + 0x370]),)
        s.log.append(e)

    def r(s, o):
        return struct.unpack_from('<H', s.m, o & 0xFFFF)[0]

    def w(s, o, v):
        struct.pack_into('<H', s.m, o & 0xFFFF, u16(v))

    def rb(s, o):
        return s.m[o & 0xFFFF]


L_TBL, R_TBL = 0x22A1, 0x2459


def outcode(M, x, y):
    x, y = s16(x), s16(y)
    al = 0xF
    if x >= 0:
        al &= ~8
    if x <= s16(M.r(0x2293)):
        al &= ~1
    if y >= 0:
        al &= ~4
    if y <= s16(M.r(0x2295)):
        al &= ~2
    return al


def tables_clear(M):
    di = M.r(0x2615)
    if di & 0x8000:
        return
    cnt = u16(M.r(0x2617) + 1 - di)
    if cnt > 0x400:
        M.clobber = True
        cnt = 0
    for k in range(cnt):
        M.w(L_TBL + 2 * di + 2 * k, 0xFFFF)
    M.w(0x2615, 0xFFFF)
    for k in range(cnt):
        M.w(R_TBL + 2 * di + 2 * k, 0)
    M.w(0x2617, 0)


def edge_column(M, si, ya, yb):
    si = u16(si)
    if si == 0:
        tbl = L_TBL
    elif si == M.r(0x2293):
        tbl = R_TBL
    else:
        return
    cx, dx = u16(ya), u16(yb)
    if s16(cx) < s16(dx):
        cx, dx = dx, cx
    if s16(dx) < 0:
        dx = 0
    if s16(cx) > s16(M.r(0x2295)):
        cx = M.r(0x2295)
    if cx > M.r(0x2617):
        M.w(0x2617, cx)
    if dx < M.r(0x2615):
        M.w(0x2615, dx)
    cnt = u16(cx - dx + 1)
    if cnt > 0x400:
        M.clobber = True
        return
    for k in range(cnt):
        M.w(tbl + 2 * dx + 2 * k, si)


def edge_column_x(M, bx, ya, yb):  # 1001:05AA
    bx = s16(bx)
    if bx < 0:
        edge_column(M, 0, ya, yb)
    elif bx > s16(M.r(0x2293)):
        edge_column(M, M.r(0x2293), ya, yb)


def edge_walk(M):  # 1001:05B9
    x1, x2 = M.r(0x2299), M.r(0x229B)
    if x2 < x1:
        M.w(0x229B, x1)
        M.w(0x2299, x2)
        y1, y2 = M.r(0x229D), M.r(0x229F)
        M.w(0x229F, y1)
        M.w(0x229D, y2)
    DX = u16(M.r(0x229B) - M.r(0x2299))
    M.w(0x2611, DX)
    y2 = M.r(0x229F)
    y1 = M.r(0x229D)
    ady = u16(y2 - y1)
    hi_, lo_ = y2, y1
    step = 2
    if ady & 0x8000:
        hi_, lo_ = lo_, hi_
        step = -2
        ady = u16(-ady)
    M.w(0x2613, ady)
    if hi_ > M.r(0x2617):
        M.w(0x2617, hi_)
    if lo_ < M.r(0x2615):
        M.w(0x2615, lo_)
    di = u16(M.r(0x229D) << 1)
    ax = M.r(0x2299)

    def Lmin(di, ax):
        if ax < M.r(L_TBL + di):
            M.w(L_TBL + di, ax)

    def Rmax(di, ax):
        if ax > M.r(R_TBL + di):
            M.w(R_TBL + di, ax)
    guard = 0
    if ady < DX:
        n = DX
        bx = u16(-(u16(DX + 1) >> 1))
        si = ady
        Lmin(di, ax)
        while True:
            guard += 1
            if guard > 70000:
                M.clobber = True
                return
            n = u16(n - 1)
            if n & 0x8000:
                Rmax(di, ax)
                return
            bx = u16(bx + si)
            if not bx & 0x8000:
                Rmax(di, ax)
                ax = u16(ax + 1)
                bx = u16(bx - DX)
                di = u16(di + step)
                Lmin(di, ax)
            else:
                ax = u16(ax + 1)
    else:
        DY = ady
        n = DY
        bx = u16(-(u16(DY + 1) >> 1))
        si = DX
        while True:
            guard += 1
            if guard > 70000:
                M.clobber = True
                return
            Lmin(di, ax)
            Rmax(di, ax)
            n = u16(n - 1)
            if n & 0x8000:
                return
            di = u16(di + step)
            bx = u16(bx + si)
            if bx & 0x8000:
                continue
            bx = u16(bx - DY)
            ax = u16(ax + 1)


def _sub_ovf(a, b):
    """16-bit a-b: returns (stored delta after the jo/cmc/rcr logic is applied later, overflow, true)"""
    t = s16(a) - s16(b)
    return t, not -32768 <= t <= 32767


def idiv_round(num, D, h, hib_D):
    """the idiv + rounding block (0432..0453 etc). num int32, D int16 stored divisor, h = D>>1 stored.
    hib_D: high byte of the stored divisor. returns q (u16)"""
    hiw = u16(num >> 16)
    Ds = s16(D)
    q = None
    if Ds != 0:
        qq = int(num / Ds)
        if -32768 <= qq <= 32767:
            q = u16(qq)
            r = u16(num - qq * Ds)
    if q is None:  # INT 0 handler 04E3 / 0854
        q = 0x7F00 if not (hiw ^ u16(D)) & 0x8000 else u16(-0x7F00)
        r = 0
    bh = hiw >> 8
    if ((bh ^ hib_D) & 0x80):
        r = u16(-r)
        q = u16(q - 1)
    r = u16(r - h)
    if not ((r >> 8) ^ bh) & 0x80:
        q = u16(q + 1)
    return q


def clip_core(M, kind):
    """kind '0336' (line_clip_draw) or '06AC' (edge_to_tables). Returns CF for 0336."""
    if kind == '0336':
        OC, DXA, DYA, HXA, HYA = 0x228A, 0x228B, 0x228D, 0x228F, 0x2291
    else:
        OC, DXA, DYA, HXA, HYA = 0x261A, 0x261B, 0x261D, 0x261F, 0x2621
    XMAX, YMAX = s16(M.r(0x2293)), s16(M.r(0x2295))

    def accept():
        if kind == '0336':
            M.log.append((31, M.r(0x2299), M.r(0x229D), M.r(0x229B), M.r(0x229F)))
            return 0
        edge_walk(M)
        return None

    cx, dx, si, di = M.r(0x2299), M.r(0x229D), M.r(0x229B), M.r(0x229F)
    oc = outcode(M, cx, dx)
    M.m[OC] = oc
    al = outcode(M, si, di)
    if al == 0:
        if oc == 0:
            return accept()
        si, cx = cx, si
        di, dx = dx, di
        al, oc = oc, al
        M.m[OC] = oc
        M.w(0x2299, cx)
        M.w(0x229D, dx)
    bp = dx
    if M.m[OC] & al:
        if kind == '0336':
            return 1
        al &= M.m[OC]
        if al & 6:
            return None
        edge_column_x(M, cx, di, bp)
        return None
    t, ov = _sub_ovf(si, cx)
    if ov:
        Dx = u16(t >> 1)
        hx = u16(s16(Dx) >> 1)
        ty, _ = _sub_ovf(di, bp)
        Dy = u16(ty >> 1)
    else:
        Dx = u16(t)
        hx = u16(s16(Dx) >> 1)
        ty, ov2 = _sub_ovf(di, bp)
        if ov2:
            Dy = u16(ty >> 1)
            Dx = u16(s16(Dx) >> 1)
            hx = u16(s16(hx) >> 1)
        else:
            Dy = u16(ty)
    hy = u16(s16(Dy) >> 1)
    M.w(DXA, Dx)
    M.w(HXA, hx)
    M.w(DYA, Dy)
    M.w(HYA, hy)
    while True:
        got = None
        ycl = None
        if al & 9:
            tx = 0 if s16(si) < 0 else u16(XMAX)
            num = s16(tx - cx) * s16(Dy)
            q = idiv_round(num, Dx, hx, Dx >> 8)
            y = u16(q + bp)
            if s16(y) < 0:
                ycl = 0
            elif s16(y) <= YMAX:
                got = (tx, y)
        if got is None:
            if ycl is None:
                ycl = u16(YMAX) if s16(di) > YMAX else 0
            num = s16(ycl - bp) * s16(Dx)
            q = idiv_round(num, Dy, hy, Dy >> 8)
            x = u16(q + cx)
            if s16(x) < 0 or s16(x) > XMAX:
                if kind == '0336':
                    return 1
                edge_column_x(M, x, di, bp)
                return None
            got = (x, ycl)
        rx, ry = got
        if kind == '06AC':
            edge_column(M, rx, ry, di)
        if M.m[OC] == 0:
            M.w(0x229F, ry)
            M.w(0x229B, rx)
            return accept()
        M.w(0x229D, ry)
        M.w(0x2299, rx)
        si, cx = cx, si
        di, bp = bp, di
        al = M.m[OC]
        M.m[OC] = 0


def line_clip_draw(M):
    return clip_core(M, '0336')


def edge_to_tables(M):
    clip_core(M, '06AC')


def poly_fill(M, n, pts, colour):  # 1001:01EC
    M.log.append((32, colour & 0xFF))
    tables_clear(M)
    x0, y0 = pts[0]
    M.w(0x2299, x0)
    M.w(0x229D, y0)
    px, py = x0, y0
    for k in range(1, u16(n - 1) + 1 if u16(n - 1) else 0x10000):
        x, y = pts[k]
        M.w(0x229B, x)
        M.w(0x229F, y)
        edge_to_tables(M)
        M.w(0x229D, y)
        M.w(0x2299, x)
    M.w(0x229F, y0)
    M.w(0x229B, x0)
    edge_to_tables(M)
    M.slot37()


def hz_draw(M):  # 1001:0018 ([0D46] already set)
    nsC = u16(-M.r(0x498A))
    M.w(0xD44, nsC)
    sB, cB, cC = s16(M.r(0x4986)), s16(M.r(0x4988)), s16(M.r(0x498C))
    if cB > 0x1F0B:
        H = u16(int((sB * 256) / cB))
    else:
        H = 0x3FF
    z = M.rb(0x433A)
    if z:
        H = u16(H << (z & 0x1F)) if (z & 0x1F) < 16 else 0
    if M.rb(0x294B):
        H = u16(s16(H) >> 1)
    w2, h2 = M.r(0xD1E), M.r(0xD20)
    P1 = s32(s16(H) * s16(nsC)) << 1
    P2 = s32(s16(w2 << 1) * cC) << 1

    def rnd(v):  # hi word + bit 15 of the low word
        v &= 0xFFFFFFFF
        return u16((v >> 16) + ((v >> 15) & 1))
    M.w(0x2299, rnd(P2 - P1) + w2)
    M.w(0x229B, w2 - rnd(P1 + P2))
    Q1 = s32(s16(H) * cC) << 1
    Q2 = s32(s16(w2 << 1) * s16(nsC)) << 1
    r = rnd(Q2 - Q1)
    M.w(0x229F, u16(r - (s16(r) >> 2) + h2))
    s = rnd(Q1 + Q2)
    M.w(0x229D, u16(h2 - (s - (s16(s) >> 2))))
    M.w(0xD48, 0)
    M.log.append((32, M.rb(0xD46)))
    tables_clear(M)
    cf = line_clip_draw(M)
    y1, y2 = M.r(0x229D), M.r(0x229F)
    full = cf == 1 or (y1 == y2 and (y1 == 0 or y1 == M.r(0x2295)))
    if full:
        if not M.r(0x4986) & 0x8000:
            M.log.append((32, 0x78))
        edge_column(M, 0, 0, M.r(0x2295))
        edge_column(M, M.r(0x2293), 0, M.r(0x2295))
        M.slot37()
        return
    while True:
        sv = [M.r(o) for o in (0x2299, 0x229D, 0x229B, 0x229F)]
        edge_to_tables(M)
        for o, v in zip((0x2299, 0x229D, 0x229B, 0x229F), sv):
            M.w(o, v)
        cxr = M.r(0x2293) if (M.r(0xD48) ^ M.r(0xD44)) & 0x8000 else 0
        M.w(0xD42, cxr)
        bx, bp = 0, M.r(0x2295)
        if (M.r(0xD48) ^ M.r(0x498C)) & 0x8000:
            bx, bp = bp, bx
        for xo, yo in ((0x2299, 0x229D), (0x229B, 0x229F)):
            si, di = M.r(xo), M.r(yo)
            if di == bx:
                continue
            if di == bp:
                si = M.r(0xD42)
            edge_column(M, si, di, bx)
        M.slot37()
        v = M.r(0xD48) ^ 0xFFFF
        M.w(0xD48, v)
        if v == 0:
            return
        M.log.append((32, 0x78))
        tables_clear(M)


# ---------------------------------------------------------------- 0000:1442
def band_points(M, t):
    nsC, cC = M.r(0x98C8), M.r(0x498C)
    w2, h2 = M.r(0xD1E), M.r(0xD20)
    si = mul15r(cC, 0xA7B8)
    di = mul15r(nsC, t)
    X1 = u16(w2 - di + si)
    X2 = u16(w2 - di - si)
    si = mul15r(cC, t)
    di = mul15r(nsC, 0xA7B8)
    s = u16(di + si)
    Y1 = u16((s16(s) >> 2) - di - si + h2)
    u = u16(si - di)
    Y2 = u16((s16(u) >> 2) - si + h2 + di)
    return X1, Y1, X2, Y2


def sky_ground(M, ctx):
    """returns list of calls: ('fill', colour) or ('poly', colour, pts) ; updates M (98C8.., C080)"""
    calls = []
    M.w(0x98C8, -M.r(0x498A))
    M.w(0x98E4, M.r(0x498C))
    M.w(0x98F8, M.r(0x4986))
    M.w(0x9F12, M.r(0x4988))
    if s16(M.rb(0x98DE) | (0xFF00 if M.rb(0x98DE) & 0x80 else 0)) < 3:
        calls.append(('fill', M.r(ctx + 4)))
        return calls
    flag = 1 if (M.r(0x4360) and M.r(0x43B2)) else 0
    for i in range(16):
        M.w(0xC080 + 2 * i, M.r(0xD22 + 2 * i))
    cB = s16(M.r(0x4988))
    if cB < 0x200:
        cB = 0x200
    H = u16(int((s16(M.r(0x4986)) * 256) / cB))
    z = M.rb(0x433A)
    if z:
        H = u16(H << (z & 0x1F)) if (z & 0x1F) < 16 else 0
    if M.rb(0x294B):
        H = u16(s16(H) >> 1)

    def bands(tfun, col0, colflag):
        P = [band_points(M, tfun(i)) for i in range(17)]
        for i in range(16):
            pts = [(P[i][0], P[i][1]), (P[i][2], P[i][3]), (P[i + 1][2], P[i + 1][3]), (P[i + 1][0], P[i + 1][1])]
            calls.append(('poly', colflag if flag else col0 + i, pts))
    bands(lambda i: u16(M.r(0xC080 + 2 * i) + H) if i < 16 else 0x5848, 0x80, 0x88)
    alt = s16(M.r(0x4980))
    c0 = u16(int(alt / 0x200))
    M.w(0xC080, c0)
    for i in range(1, 16):
        M.w(0xC080 + 2 * i, u16(int(alt / ((16 - i) << 5))) - c0)
    M.w(0xC080, 0)
    bands(lambda i: u16(H - M.r(0xC080 + 2 * i)) if i < 16 else 0xA7B8, 0x70, 0x7C)
    return calls


def fill_rows(M, vp, xl, ymin, xr, ymax, colour_off=4):  # 1001:024C (colour vp+4); 02BA uses vp+6
    M.log.append((16, None))
    M.log.append((13, M.r(vp)))
    M.log.append((32, M.rb(vp + colour_off + 1) if False else M.r(vp + colour_off) & 0xFF))
    tables_clear(M)
    M.w(0x2617, ymax)
    M.w(0x2615, ymin)
    cnt = u16(ymax - ymin + 1)
    for k in range(cnt):
        M.w(L_TBL + 2 * ymin + 2 * k, xl)
    for k in range(cnt):
        M.w(R_TBL + 2 * ymin + 2 * k, xr)
    M.log.append((40, M.r(0x2615)))
