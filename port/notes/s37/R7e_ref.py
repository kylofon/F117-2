"""R7e Python reference: 0000:13AC, 1353, 11F2 (calls to 13E3), 13E3, 0FFB, 77D9, BC12 (+BC77), BCD7.
mem: object with rb(off) / rw(off) (unsigned) over DGROUP; results as dicts / lists."""
import sys
sys.path.insert(0, 'C:/Coding/F117-2/port/notes/s37')
import R7d_ref as D

s16, s32, s8 = D.s16, D.s32, D.s8
def u16(v): return v & 0xFFFF


def idiv16(n, d):                 # cwd; idiv word: trunc toward 0 (no overflow in the tested ranges)
    n, d = s16(n), s16(d)
    q = abs(n) // abs(d)
    return s16(q if (n >= 0) == (d >= 0) else -q)


def t13ac(m, px, py):             # -> (tx, ty)
    tx = idiv16(m.rw(0x94E2) - m.rw(0x0D1E) + px, m.rw(0x94DE))
    t = idiv16(u16((py - m.rw(0x0D20)) << 2), 3)
    ty = idiv16(t + m.rw(0x94E4), m.rw(0x94DE))
    return tx, ty


def t1353(m):                     # -> (x0, x1, y0, y1)
    x0, y0 = t13ac(m, 0, 0)
    x0 = 0 if x0 < 0 else x0
    y0 = 0 if y0 < 0 else y0
    x1, y1 = t13ac(m, m.rw(0x2293), m.rw(0x2295))
    n = s16(m.rw(0x94E0))
    if x1 >= n: x1 = s16(n - 1)
    if y1 >= n: y1 = s16(n - 1)
    return x0, x1, y0, y1


class Over:                       # memory overlay for the globals 11F2 writes
    def __init__(s, m): s.m, s.w = m, {}
    def rw(s, o): return s.w.get(o, s.m.rw(o))
    def rb(s, o): return s.m.rb(o)


def t11f2(m, px, py, shift):      # -> list of 13E3 calls (id, x, y, 7D5E, 94E6, E2FC) and final globals
    g = Over(m); calls = []
    cl = shift & 0xFF
    g.w[0x94E2] = u16(s16(px) >> (cl & 0x1F)); g.w[0x94E4] = u16(s16(py) >> (cl & 0x1F))
    for k in (4, 3, 2, 1, 0):
        g.w[0x94E8] = k
        g.w[0x94E6] = 0x80 if k <= 1 else 0
        L = m.rw(0x0CFC + 2 * k); g.w[0xE2FC] = L
        decc = u16((-(L - 4)) * 2 + shift); g.w[0xDECC] = decc
        T = s16(0x1000 >> (decc & 0x1F)) if (decc & 0x1F) < 16 else 0
        g.w[0x94DE] = u16(T)
        if T <= 0x10: continue
        c = ((4 - (L & 0xFF)) * 2) & 0xFF
        g.w[0x94E0] = u16(4 << (c & 0x1F))
        x0, x1, y0, y1 = t1353(g)
        ty = y0
        while not (y1 < ty):
            tx = x0
            while not (tx > x1):
                sx = u16(s16(tx * T) - s16(g.rw(0x94E2)) + (T >> 1))
                sy = u16((T >> 1) + s16(T * ty) - s16(g.rw(0x94E4)))
                tile = D.t0865(s16(L), tx, ty, m.rb)
                if tile != 0xFFFF:
                    idx = 2 * u16(u16(L << 5) + tile)
                    p = m.rw(u16(0xE304 + idx)); j = 0
                    while m.rw(u16(0x061E + idx)) > j:
                        if m.rw(u16(p + 4)) == 0:
                            g.w[0x7D5E] = u16(decc + 0xE)
                            dc = decc & 0x1F
                            y = u16((sy << dc) + m.rw(u16(p + 2)))
                            x = u16((sx << dc) + m.rw(p))
                            calls.append((m.rb(u16(p + 6)), x, y, g.w[0x7D5E], g.rw(0x94E6), L))
                        p = u16(p + 7); j += 1
                    g.w[0xE44A] = p
                tx = s16(tx + 1)
            ty = s16(ty + 1)
    g.w[0x94E8] = 0xFFFF
    return calls, g.w


def t13e3(m, e2fc, f94e6, oid, x, y):   # -> None (not drawn) or (args of 00D3, bank)
    if s16(e2fc) >= 3:
        if (m.rb(u16(m.rw(0x0A62) + (oid & 0xFF))) & 0x80) != f94e6: return None
    return (oid & 0xFF, u16(x), 0, u16(-y), 0x49A0, 0xC02E), m.rw(0xB760)


def t0ffb(m, x, y, z):            # x, y, z: uint32 -> dict of results
    r = {'C686': 0}
    v = D.t0804(4, x); cx = (v >> 12) & 0xFFFF; fx = v & 0x0FFF
    v = D.t0804(4, y); cy = (v >> 12) & 0xFFFF; fy = v & 0x0FFF
    r['4980'] = D.t0804(4, z) & 0xFFFF
    ax_ = u16(0x800 - fx); ay_ = u16(0x800 - fy)
    r['497C'] = u16(fx - 0x800); r['497E'] = u16(fy - 0x800)
    best = 0x7FFF; bp = None; d4978 = None
    tile = D.t0865(4, cx, cy, m.rb)
    p = m.rw(u16(0xE404 + 2 * tile)); i = 1
    while m.rw(u16(0x061E + 2 * (4 * 32 + tile))) > i:          # QUIRK: starts at 1 -> last record never tested
        o = m.rb(u16(p + 6))
        if not (o & 0x50) and (m.rb(0x0960 + o) & 0x10):
            dx = u16(m.rw(p) + ax_); dy = u16(m.rw(u16(p + 2)) + ay_)
            a = lambda v: u16(-s16(v)) if v & 0x8000 else v
            d = u16(a(dx) + a(dy)); d4978 = d
            if s16(d) < best: best = s16(d); bp = p
        p = u16(p + 7); i += 1
    r['E44A'] = p; r['BA16'] = u16(best); r['4978'] = d4978; r['BA20'] = bp; r['call'] = None
    if best != 0x7FFF:
        p = bp; r['E44A'] = p
        r['4964'] = u16(m.rw(p) - r['497C']); r['4966'] = u16(m.rw(u16(p + 2)) - r['497E'])
        r['4968'] = u16(m.rw(u16(p + 4)) - r['4980'])
        o = m.rb(u16(p + 6))
        r['8560'] = m.rw(u16(0xB75E + 2 * m.rb(u16(0x0511 + 2 * o))))
        r['call'] = (m.rb(u16(0x0510 + 2 * o)), r['4964'], r['4968'], u16(-s16(r['4966'])), 0x49A0)
    return r


def t77d9(cx, cy, alt):           # -> args of 0FFB as three uint32
    return ((s16(cx) << 5) & 0xFFFFFFFF, ((0x8000 - s16(cy)) << 5) & 0xFFFFFFFF, s16(alt) & 0xFFFFFFFF)


def tc6b8(m, i):
    return (m.rw(u16(0x0510 + 2 * (i & 0x7F))) | (i & 0x8000)) if i & 0x0100 else i


def tbc77(m, o):
    c = m.rb(u16(0xC604 + (m.rb(u16(0xB2B0 + 16 * o)) & 0x7F))) & 0x0F
    return 1 if c in (0xC, 0xD, 9, 0xB) else 0


def tbc12(m, o):
    rec = u16(16 * o)
    sh = m.rw(u16(0xB2B0 + rec))
    i = tc6b8(m, sh)
    if not (m.rb(u16(0xB2AA + rec)) & 0x80): return sh
    tb = m.rw(u16(0x0A60 + 2 * s16(s16(i) >> 8)))
    if m.rb(u16(tb + (i & 0xFF))) & 0x20: return sh | 0x0100
    b = m.rb(0xC0AC) if tbc77(m, o) else m.rb(0xDEA4)
    return u16(s8(b) + 0x100)                                   # cbw; add ah,1: ids >= 80h lose the bank


def tbcd7(m, u):                  # -> new [49C6]
    rec = u16(0x24 * u)
    c = m.rb(u16(0xB174 + ((m.rw(u16(0xC142 + rec)) >> 11) << 4) + (m.rw(u16(0xC140 + rec)) >> 11))) & 3
    if not (s16(m.rw(0xDED0) - 4) > s16(u)): return c
    return 1 if (c == 0 and m.rw(0x3D62) == 0) else 0
