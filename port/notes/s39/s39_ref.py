"""S3.9 Python reference for END.EXE (debrief). Every function is a transcription of the disassembly named in the docstring;
`W` holds DGROUP (64 KB) and the comm block. Verified against the real code by s39_test.py (Unicorn).
Offsets are END DGROUP offsets (see port/spec/debrief.md)."""
import os, struct, sys
_here = os.path.dirname(os.path.abspath(__file__))
_root = _here
while _root and not os.path.exists(os.path.join(_root, 'PLAN.md')):
    _root = os.path.dirname(_root)
_D = open(os.path.join(_root, 'work', 'unpacked', 'END.EXE'), 'rb').read()
_IMG = _D[struct.unpack_from('<H', _D, 8)[0] * 16:]
DSB = 0x5FA * 16
PIL = 0x1272           # pilot record offset inside the comm block

SITES, UNITS, OBJCLS, GRID, LOG, NAMES = 0x56ea, 0x64ca, 0x6902, 0x55ea, 0x5e38, 0x6efa
NSITES, NUNITS = 0x6446, 0x6d8a


def s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


def tdiv(a, b):
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


class W:
    def __init__(s):
        s.ds = bytearray(0x10000)
        n = min(len(_IMG) - DSB, 0x10000)
        s.ds[0:n] = _IMG[DSB:DSB + n]
        s.comm = bytearray(0x1400)
        s.calls = []
        s.seed = 1

    # DGROUP
    def b(s, o): return s.ds[o & 0xffff]
    def w(s, o): o &= 0xffff; return s.ds[o] | (s.ds[(o + 1) & 0xffff] << 8)
    def sb(s, o, v): s.ds[o & 0xffff] = v & 0xff
    def sw(s, o, v): o &= 0xffff; s.ds[o] = v & 0xff; s.ds[(o + 1) & 0xffff] = (v >> 8) & 0xff
    def cstr(s, o):
        t = bytearray()
        while s.ds[o]:
            t.append(s.ds[o]); o += 1
        return bytes(t)
    # comm / pilot
    def c(s, o): return s.comm[o] | (s.comm[o + 1] << 8)
    def sc(s, o, v): s.comm[o] = v & 0xff; s.comm[o + 1] = (v >> 8) & 0xff
    def cb(s, o): return s.comm[o]
    def p(s, o): return s.c(PIL + o)
    def sp(s, o, v): s.sc(PIL + o, v)
    # rand (platform.md: LCG; the test passes the seed explicitly)
    def rand(s):
        seed = int.from_bytes(s.ds[0x5158:0x515c], 'little')
        seed = (seed * 0x343FD + 0x269EC3) & 0xffffffff
        s.ds[0x5158:0x515c] = seed.to_bytes(4, 'little')
        return (seed >> 16) & 0x7fff
    def rand_n(s, n): return (s.rand() * n) >> 15


# ---------------------------------------------------------------- 0ecd / 0ee0 / 0ef3 / 00a1
def map_x(v): return ((v << 7) & 0xffff) // 0x92
def map_y(v): return ((v << 7) & 0xffff) // 0xc3


def fmt_time(m, ticks):
    """0ef3 fmt_time(ticks, buf): 'hh:mm:ss' (hours are not wrapped at 24)."""
    ticks = (ticks + (((m.w(0x71f0) + m.w(0x7202)) & 0xf) << 8)) & 0xffff
    night = m.cb(0x33)
    buf = bytearray(b'00:00:00')
    buf[0] = (buf[0] + night) & 0xff
    v = ticks // 0x708
    buf[1] = (buf[1] + v % 10) & 0xff
    buf[1] = (buf[1] + (6 if night == 0 else 8)) & 0xff
    if buf[1] > 0x39:
        buf[1] = (buf[1] - 10) & 0xff
        buf[0] = (buf[0] + 1) & 0xff
    mi = (ticks // 30) % 60
    buf[3] = (buf[3] + mi // 10) & 0xff
    buf[4] = (buf[4] + mi % 10) & 0xff
    se = ((ticks << 1) & 0xffff) % 60
    buf[6] = (buf[6] + se // 10) & 0xff
    buf[7] = (buf[7] + se % 10) & 0xff
    return bytes(buf)


def landing_class(m):
    """00a1: grid class (0..3) of the final position; also stored in [6440]."""
    y = m.c(0x76) >> 11
    x = m.c(0x74) >> 11
    v = m.b(GRID + ((y << 4) & 0xffff) + x) & 3
    m.sb(0x6440, v)
    return v


# ---------------------------------------------------------------- 3021 score
T_DB6, T_DBC, T_DC2, T_DC8, T_DCE, T_DD4, T_DDA, T_DE0, T_DE6, T_DEC, T_DF2, T_DF8 = (
    0xdb6, 0xdbc, 0xdc2, 0xdc8, 0xdce, 0xdd4, 0xdda, 0xde0, 0xde6, 0xdec, 0xdf2, 0xdf8)
T_DFE, T_E00, T_E02, T_E08 = 0xdfe, 0xe00, 0xe02, 0xe08


def score(m, limit):
    """3021 int score(unsigned limit): replay of the event log up to record `limit`; returns AX (low 16 bits of the score)."""
    for a in (0x720e, 0x6d89, 0x5b9a, 0x55e8, 0x55db, 0x6984, 0x64c8, 0x6d88, 0x55dc):
        m.sb(a, 0)
    S = 0
    ng = 0                       # bp-6 enemy-territory sample counter
    fe = 0                       # bp-2 "end record seen"
    i = 0
    war = m.p(0x3a)
    ws = war * 2

    def tw(base):                # signed word of a war-indexed table
        return s16(m.w(base + ws))

    while True:
        if i > (limit & 0xffff):
            break
        rec = (LOG + 6 * i) & 0xffff
        r4 = m.b(rec + 4)
        if r4 == 0:
            break
        r5 = m.b(rec + 5)
        arg = r5 - 256 if r5 >= 128 else r5          # cbw
        site = (SITES + (((arg & 0x7f) << 4) & 0xffff)) & 0xffff
        ob = m.b(site + 0xe)
        cls = m.b(OBJCLS + ob)
        t = r4 & 0x3f

        def sflags_hi(): return m.b((SITES + ((arg << 4) & 0xffff) + 9) & 0xffff)
        def sflags(): return m.w((SITES + ((arg << 4) & 0xffff) + 8) & 0xffff)

        def obj_primary():
            nonlocal S
            S += tw(T_DEC)
            m.sb(0x6d89, 1)
            if m.b(0x720e):
                S += s16(m.w(T_E00)) - s16(m.w(T_DFE))

        def obj_secondary(idx_words):
            nonlocal S
            S += s16(m.w(T_DFE + idx_words * 2))
            m.sb(0x720e, 1)

        def inc(a): m.sb(a, m.b(a) + 1)

        if t == 1 or t == 12:
            if sflags_hi() & 0x10:
                S -= tw(T_DF2)
            if r4 & 0x80:
                obj_primary(); inc(0x6984)
            elif r4 & 0x40:
                obj_secondary(m.b(0x6d89)); inc(0x6984)
            elif cls & 0x40:
                S += tw(T_DCE); inc(0x55e8)
            else:
                fl = sflags()
                if fl & 0x0500:
                    S += tw(T_DCE); inc(0x55e8)
                elif (fl & 0x1000) or war != 0:
                    S += tw(T_DF8); inc(0x6984)
                else:
                    v = tw(T_DE0)
                    S += v
                    if v == 0:
                        inc(0x6d88)
        elif t == 2:
            if r4 & 0x80:
                S += tw(T_DEC); m.sb(0x6d89, 1); inc(0x6984)
            elif r4 & 0x40:
                S += tw(T_DFE); m.sb(0x720e, 1); inc(0x6984)       # war-indexed (not [6d89]) in this case
            elif cls & 0x40:
                S += tw(T_DCE); inc(0x55e8)
            else:
                tile = m.b(GRID + (m.b(rec + 3) & 0xf0) + (m.b(rec + 2) >> 4))
                if tile & 3:
                    S += tw(T_DCE); inc(0x55e8)
                elif cls & 0x80:
                    S += tw(T_DBC); inc(0x5b9a)
                else:
                    S += s16(m.w(T_E08 + (((war << 4) + (cls & 0xf)) * 2)))
                    if s16(m.w(T_DD4 + ws)) < 0:
                        inc(0x6d88)
                    else:
                        inc(0x6984)
        elif t == 3:
            if arg & 0x80:
                S -= tw(T_DE6)
            if r4 & 0x80:
                obj_primary(); inc(0x64c8)
            elif r4 & 0x40:
                obj_secondary(m.b(0x6d89)); inc(0x64c8)
            else:
                u = (0x179e + ((arg & 0x7f) << 5)) & 0xffff
                radar = m.w(u + 0x18)
                if radar == 0xffff:
                    S += tw(T_DC8); inc(0x55db)
                elif (arg & 0x80) or war != 0:
                    if radar == 0:
                        S += s16(m.w(T_E02 + ws))
                    elif m.b((SITES + ((radar << 4) & 0xffff) + 8) & 0xffff) & 4:
                        S += tw(T_DB6)
                    else:
                        S += tw(T_DC2)
                    inc(0x64c8)
                else:
                    S += tw(T_DDA); inc(0x55dc)
        elif t == 6:
            S += tw(T_DE6)
        elif t == 7:
            S += tw(T_DF2)
        elif t == 8:
            if S < 0:
                S = 0
            if i != 0:
                fe = 1
        elif t == 9:
            cell = m.b(GRID + (m.b(rec + 3) & 0xf0) + (m.b(rec + 2) >> 4))
            if (cell & 3) == 0:
                if war == 0:
                    ng = (ng + 1) & 0xffff; S += 1
                elif war == 1:
                    ng = (ng + 1) & 0xffff
                    if m.p(0x42) != 0 and (ng & 7) == 0:
                        S += 1
                    if (ng & 3) == 0:
                        S += 1
        elif t == 10 or t == 11:
            if r4 & 0x80:
                obj_primary()
            elif r4 & 0x40:
                obj_secondary(m.b(0x6d89))
        # t = 0, 4, 5 or > 12: nothing
        S = s32(S)
        i += 1

    def mul8(S, tab):
        return s32(tdiv(s32(S * tab), 8))
    S = mul8(S, s16(m.w(0xe68 + 2 * m.p(0x38))))
    S = mul8(S, s16(m.w(0xe72 + 2 * m.p(0x3c))))
    S = mul8(S, s16(m.w(0xe7a + 2 * m.p(0x3e))))
    S = mul8(S, s16(m.w(0xe82 + 2 * m.p(0x40))))
    if fe and m.c(0x26) == 2:
        S = mul8(S, s16(m.w(0xe88 + 2 * m.b(0x6440))))
    r = m.c(0x2e)
    if 0x1b58 <= r < 0x2328:
        S = mul8(S, 9)
    elif 0x2328 <= r < 0x2cec:
        S = mul8(S, 10)
    elif r >= 0x2cec:
        S = mul8(S, 11)
    return S & 0xffff


# ---------------------------------------------------------------- 2afa promotion / 2476 awards
def promotion(m):
    """2afa: sets [55e6] (promoted, rank++) or [55e7] (promotion withheld because captured)."""
    if m.c(0x30) != 0:
        return
    i = 5
    while True:
        rank = s16(m.p(0x20))
        if rank > i:
            return
        need = m.w(0x5b0 + 2 * i)
        hi = s16(m.p(0x34))
        sc = m.p(0x32) | (m.p(0x34) << 16)
        if hi < 0 or (hi == 0 and need > m.p(0x32)):
            i -= 1; continue
        miss = m.p(0x36)
        if m.w(0x5c8 + 2 * i) > miss:
            i -= 1; continue
        q = tdiv(s32(sc), miss)
        if q < 0 or (q < 0x10000 and q < m.w(0x5bc + 2 * i)):
            i -= 1; continue
        if m.p(0x20) == 5 and miss == 0x63:
            m.sb(0x55e6, 1); m.sp(0x20, m.p(0x20) + 1); return
        if m.p(0x20) >= 5:
            return
        if m.b(0x6982) == 1:
            m.sb(0x55e7, 1); return
        m.sb(0x55e6, 1); m.sp(0x20, m.p(0x20) + 1); return


def awards(m):
    """2476: ribbons and medals. Sets [55dd] [55e2] [643a] [55da], updates pilot +22..+4c."""
    def dead(): return m.b(0x6982) != 0 or m.c(0x30) != 0
    if m.p(0x44) == 0 and m.c(0x30) != 0 and (m.b(0x6d89) != 0 or m.b(0x720e) != 0):
        m.sp(0x44, 1); m.sb(0x55dd, 1)
    if m.c(0x30) == 0:
        n = m.p(0x36)
        if m.p(0x46) == 0 and 5 <= n < 10:
            m.sp(0x46, 1); m.sb(0x55e2, 1)
        if m.p(0x48) == 0 and n >= 10:
            m.sp(0x46, 0); m.sp(0x48, 1); m.sb(0x55e2, 2)
        if m.p(0x4a) == 0 and 30 <= n < 60:
            m.sp(0x4a, 1); m.sb(0x55e2, 3)
        if m.p(0x4c) == 0 and n >= 60:
            m.sp(0x4a, 0); m.sp(0x4c, 1); m.sb(0x55e2, 4)
    if m.p(0x22) == 0 and m.c(0x36) >= 7 and (m.c(0x34) & 8) and (m.c(0x34) & 4):
        m.sp(0x22, 1); m.sb(0x643a, 1)
    last = m.p(0x30)
    if m.p(0x2c) == 0 and last > 0x4b0:
        if dead():
            m.sb(0x55da, 6)
        else:
            m.sb(0x643a, 6); m.sp(0x2c, 1)
        return
    for off, mult, code in ((0x2a, 900, 5), (0x28, 600, 4), (0x26, 300, 3)):
        if (((m.p(off) + 1) * mult) & 0xffff) <= last:
            if dead():
                m.sb(0x55da, code)
            else:
                m.sb(0x643a, code); m.sp(off, m.p(off) + 1)
            return
    if m.p(0x24) < 9 and (((m.p(0x24) + 1) * 100) & 0xffff) <= last:
        if dead():
            m.sb(0x55da, 2)
        else:
            m.sb(0x643a, 2); m.sp(0x24, m.p(0x24) + 1)


# ---------------------------------------------------------------- 05ca icon placement
def place_icons(m):
    """05ca: builds the icon list [54b8] n, [5440+2k] site, [5441+2k] partner, [54ba+2k] x, [5532+2k] y."""
    n_sites = m.w(NSITES)
    m.sw(0x54b8, 0)

    def included(q):
        if m.w(q + 6) == 0:
            fl = m.w(q + 8)
            if not (fl & 1) and not (fl >> 8 & 2):
                return False
        return not (m.b(q + 9) & 8)

    i = 0
    p = SITES
    while i < n_sites:
        if included(p):
            if m.w(p + 6) != 0:
                m.sb(p + 9, m.b(p + 9) & 0xbf)
            fl = m.w(p + 8)
            j = n_sites
            if (not (fl & 8)) or (fl >> 8 & 4):
                q = SITES
                j = 0
                while j < n_sites:
                    if included(q) and m.w(q) == m.w(p) and j != i:
                        break
                    j += 1
                    q += 16
            if j == n_sites:
                x = m.w(p + 2) // 0x92; y = m.w(p + 4) // 0xc3
            elif j > i:
                q = SITES + 16 * j
                x = ((m.w(q + 2) + m.w(p + 2)) & 0xffff) // 0x124
                y = ((m.w(q + 4) + m.w(p + 4)) & 0xffff) // 0x186
            if j == n_sites or j > i:
                k = m.w(0x54b8)
                m.sw(0x54ba + 2 * k, x - 1); m.sw(0x5532 + 2 * k, y - 2)
                m.sb(0x5440 + 2 * k, i)
                m.sb(0x5441 + 2 * k, j if n_sites > j else 0)
                m.sw(0x54b8, k + 1)
        i += 1
        p += 16
