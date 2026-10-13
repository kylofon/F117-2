"""R7d Python reference for 0000:E8B5/E8A2 cull, E26B, E3E2, E578, 0804, 0865, DDFE (+1088:0120), DCC0 (key parts)."""
import struct
D = open('C:/Coding/F117-2/work/unpacked/VGAME.EXE', 'rb').read()
IMG = D[struct.unpack_from('<H', D, 8)[0] * 16:]
DSB = 0x1E230
SIN = struct.unpack_from('<257h', IMG, DSB + 0x2084)


def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - 0x100000000 if v & 0x80000000 else v
def s8(v): v &= 0xFF; return v - 0x100 if v & 0x80 else v


def mq(a, b):                      # 109D:0006 mul_q15
    return s16((s16(a) * s16(b) + 0x4000) >> 15)


def trig(a):                       # 109D:0090 core
    a &= 0xFFFF; i = a >> 8; f = a & 0xFF
    p = s16(SIN[i + 1] - SIN[i]) * f
    return s16(SIN[i] + ((p + 0x80) >> 8))


def sin16(a): return trig(a)
def cos16(a): return trig(a + 0x4000)


def e26b(A, B, C):
    s1, c1, s2, c2, s3, c3 = sin16(A), cos16(A), sin16(B), cos16(B), sin16(C), cos16(C)
    m = [s16(mq(c1, c3) + mq(s1, mq(s2, s3))),
         s16(mq(s1, mq(s2, c3)) - mq(c1, s3)),
         mq(s1, c2),
         mq(s3, c2),
         mq(c3, c2),
         s16(-s2),
         s16(mq(c1, mq(s2, s3)) - mq(s1, c3)),
         s16(mq(c1, mq(s2, c3)) + mq(s3, s1)),
         mq(c1, c2)]
    return m, [s1, c1, s2, c2, s3, c3]


def e3e2(a1, a2, a3):
    s1, c1, s2, c2, s3, c3 = sin16(a1), cos16(a1), sin16(a2), cos16(a2), sin16(a3), cos16(a3)
    m = [0] * 9
    m[0] = s16(mq(c1, c3) - mq(s1, mq(s2, s3)))
    m[3] = s16(mq(c1, s3) + mq(s1, mq(s2, c3)))
    m[6] = s16(-mq(s1, c2))
    m[1] = s16(-mq(s3, c2))
    m[4] = mq(c3, c2)
    m[7] = s16(s2)
    m[2] = s16(mq(s1, c3) + mq(c1, mq(s2, s3)))
    m[5] = s16(mq(s1, s3) - mq(c1, mq(s2, c3)))
    m[8] = mq(c2, c1)
    return m, [s1, c1, s2, c2, s3, c3]


def e578(A, B):                    # C = A x B, row-major 3x3 Q15
    return [s16(sum(mq(A[r * 3 + k], B[k * 3 + c]) for k in range(3))) for r in range(3) for c in range(3)]


def e8b5(m, x, y, z, cls, g):
    """g: dict with keys 48B6, 4360, 294B (byte), 365E (byte), E53E, E544 (table base), tab (callable off->s16).
    returns (ax, A32, B32, C32, d4978 or None)"""
    def view(a, b, c):             # a*y + b*x... see spec: (mY*BX + mZ*CX + mX*BP)
        return s32(((m[a] * y + m[b] * z + m[c] * x) << 1))
    A = view(6, 3, 0); Bv = view(7, 4, 1); C = view(8, 5, 2)
    Ch = s16(C >> 16); Ah = s16(A >> 16); Bh = s16(Bv >> 16)
    ab = lambda v: s16(abs(v))     # abs16: abs(-32768) = -32768
    out = (1, A, Bv, C, None)
    if Ch > s16(g['48B6']):
        return out
    if Ch < g['tab'](0x48C6 + 2 * cls):
        return out
    if g['4360'] != 0:
        s = s16(ab(Ch) + g['tab'](0x4936 + 2 * cls)) >> 3
        t = s >> 3
        sx = s16(s + t); t >>= 1; sx = s16(sx + t)
        if sx < ab(Ah):
            return out
        s2 = s16(ab(Ch) + g['tab'](0x4946 + 2 * cls)) >> 2
        sy = s16(s2 + (s2 >> 3))
        if not (sy > ab(Bh)):
            return out
        d = s16((s16(sy + ab(Bh)) >> 2) + Ch)
    else:
        cl = ((g['294B'] & 0xFF) ^ 1) & 0x1F
        aC = ab(Ch)
        t = s16(aC + g['tab'](g['E53E'] + 2 * cls)) >> cl
        sx = s16((t >> 2) + t)
        if ab(Ah) > sx:
            return out
        u = s16(aC + g['tab'](g['E544'] + 2 * cls)) >> cl
        by = u
        if g['365E'] & 0xFF:
            by = s16((u >> 3) + u) >> 1
        if ab(Bh) > by:
            return out
        d = s16((s16(ab(Ah) + ab(Bh)) >> 2) + aC)
    if d > s16(g['48B6']):
        return (1, A, Bv, C, d)
    return (0, A, Bv, C, d)


def t0804(level, v):
    v &= 0xFFFFFFFF
    if level == 0: return (v << 1) & 0xFFFFFFFF
    if level == 1: return v
    if level == 2: return ((v + 2) & 0xFFFFFFFF) >> 2
    if level == 3: return ((v + 8) & 0xFFFFFFFF) >> 4
    if level == 4: return ((v + 0x20) & 0xFFFFFFFF) >> 6
    return None


def t0865(level, x, y, mem):       # mem(off) -> byte
    x = s16(x); y = s16(y)
    dim = {0: 1024, 1: 256, 2: 64, 3: 16, 4: 8}
    if level == 4: x = s16(x + 2); y = s16(y + 2)
    if x < 0 or y < 0 or not (dim[level] > x) or not (dim[level] > y):
        return 0
    if level == 4: return mem(0xB00C + y * 8 + x)
    if level == 3: return mem(0x9F68 + y * 16 + x)
    base = {2: 0x9D10, 1: 0x9B08, 0: 0x9904}[level]
    p = t0865(level + 1, x >> 2, y >> 2, mem)
    return mem(base + p * 16 + (y & 3) * 4 + (x & 3))


def shade(v7d5e, v43b6, b4963, cls):   # 1088:0120
    if v7d5e & 0xFF: return 0
    v = max(0, s8(b4963)) >> 1
    h = (v - (cls & 0xFF)) & 0xFF
    if h & 0x80: h = 0
    if s8(h) > 0x1F: h = 0x1F
    if (v43b6 & 0xFF) == 0: return 0
    return h >> 1


def ddfe(q, n, key32, e2fc, cls, shadev, fields):
    """q: list of record dicts (pointer order), n: count. fields: dict 6,8,a,c,e,10,12,14. returns new q (list) and rec."""
    q = list(q)
    if n == 50:
        rec = q.pop(0); n -= 1
    else:
        rec = {'addr': 0x49F0 + 0x16 * n}
    sh = ((4 - (e2fc & 0xFF)) * 2) & 0xFF
    k = s32(key32) >> sh if sh < 32 else (-1 if s32(key32) < 0 else 0)
    k = s32(k)
    if e2fc == 2 and cls == 5:
        k = s32(k + 0x200000)
    rec = dict(rec); rec['key'] = k; rec['shade'] = shadev
    rec.update(fields)
    i = n - 1
    while i > -1:
        r = q[i]['key']
        if (r >> 16) < (k >> 16): i -= 1; continue
        if (r >> 16) > (k >> 16): break
        if (r & 0xFFFF) <= (k & 0xFFFF): i -= 1; continue
        break
    q.insert(i + 1, rec)
    return q, rec
