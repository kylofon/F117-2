"""R7b Python reference of VGAME 12EF (clipper / polygon assembler) and 1359 mode-M back end.
Operates on M = bytearray(65536) (DGROUP image) and P = bytearray(65536) (page segment ES).
Written from the disassembly (see port/spec/render3d.md Part 5); compared with the real code by R7b_emu.py."""

XMIN, YMIN, XMAX, YMAX = 0x85CA, 0x85CC, 0x85CE, 0x85D0
LT, RT, MINY, ROWT = 0x89AC, 0x8D6E, 0x9130, 0x85EC


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


COV = {}


def cov(k):
    COV[k] = COV.get(k, 0) + 1


class Div0(Exception):
    pass


class NearRetBug(Exception):
    pass


class R:
    def __init__(s, M, P=None):
        s.M = M
        s.P = P if P is not None else bytearray(65536)

    # memory helpers
    def rw(s, o):
        o &= 0xFFFF
        return s.M[o] | (s.M[(o + 1) & 0xFFFF] << 8)

    def rs(s, o):
        return s16(s.rw(o))

    def ww(s, o, v):
        o &= 0xFFFF
        v &= 0xFFFF
        s.M[o] = v & 0xFF
        s.M[(o + 1) & 0xFFFF] = v >> 8

    def rd(s, o):
        return s32(s.rw(o) | (s.rw(o + 2) << 16))

    def wd(s, o, v):
        s.ww(o, v)
        s.ww(o + 2, (v & 0xFFFFFFFF) >> 16)

    def win(s):
        return s.rs(XMIN), s.rs(YMIN), s.rs(XMAX), s.rs(YMAX)

    # ---------------- 12EF: out-codes ----------------
    def oc16(s, x, y):                      # 12EF:02D1 (bits 1 y<ymin, 2 y>ymax, 4 x<xmin, 8 x>xmax)
        xmin, ymin, xmax, ymax = s.win()
        c = 0
        if not -0x8000 <= x - xmin <= 0x7FFF:
            s.M[0x85C0] = 1
        if x < xmin:
            c |= 4
        if x > xmax:
            c |= 8
        if not -0x8000 <= y - ymin <= 0x7FFF:
            s.M[0x85C0] = 1
        if y < ymin:
            c |= 1
        if y > ymax:
            c |= 2
        return c

    def oc32(s, x, y):                      # 12EF:0665 (unsigned compare of the lo word when hi == 0)
        xmin, ymin, xmax, ymax = (s.rw(XMIN), s.rw(YMIN), s.rw(XMAX), s.rw(YMAX))
        c = 0
        xh, xl = s16(x >> 16), x & 0xFFFF
        if xh < 0 or (xh == 0 and xl < xmin):
            c |= 4
        elif xh > 0 or xl > xmax:
            c |= 8
        yh, yl = s16(y >> 16), y & 0xFFFF
        if yh < 0 or (yh == 0 and yl < ymin):
            c |= 1
        elif yh > 0 or yl > ymax:
            c |= 2
        return c

    def on_border(s, x, y):                 # 12EF:063E
        if (x >> 16) & 0xFFFF or (y >> 16) & 0xFFFF:
            return False
        xl, yl = x & 0xFFFF, y & 0xFFFF
        return xl in (s.rw(XMIN), s.rw(XMAX)) or yl in (s.rw(YMIN), s.rw(YMAX))

    # ---------------- 12EF: flags ----------------
    @staticmethod
    def side_bits(c0, c1):
        b = 0
        if c0 & 8: b |= 1
        if c0 & 4: b |= 2
        if c1 & 8: b |= 4
        if c1 & 4: b |= 8
        return b

    def flags16(s, si, c0, c1, y0, y1):     # 12EF:020B
        v = y0
        if c0 & 1: v = s.rw(YMIN)
        if c0 & 2: v = s.rw(YMAX)
        s.ww(si + 6, v)
        v = y1
        if c1 & 1: v = s.rw(YMIN)
        if c1 & 2: v = s.rw(YMAX)
        s.ww(si + 0xE, v)
        b = s.side_bits(c0, c1)
        s.ww(si + 2, b * 0x101)

    def flags32(s, si, c0, c1):             # 12EF:05D9 -> al
        s.ww(si + 6, s.rw(YMIN) if c0 & 1 else s.rw(YMAX) if c0 & 2 else s.rw(si + 4))
        s.ww(si + 0xE, s.rw(YMIN) if c1 & 1 else s.rw(YMAX) if c1 & 2 else s.rw(si + 0xC))
        return s.side_bits(c0, c1)

    # ---------------- 12EF:0265 16-bit intercept ----------------
    def idiv(s, num, den):
        if den == 0:
            raise Div0()
        q = abs(num) // abs(den)
        if (num < 0) != (den < 0):
            q = -q
        if not -0x8000 <= q <= 0x7FFF:
            raise Div0()
        return q

    def intercept(s, al, bx, cx):
        """returns (ok, x, y, failcode)"""
        xmin, ymin, xmax, ymax = s.win()
        dxe, dye = s.rs(0x85AC), s.rs(0x85AE)
        if al == 0:
            return True, bx, cx, 0
        if al & 0xC:
            bp = xmin if al & 4 else xmax
            q = s.idiv(s16(bp - bx) * dye, dxe)
            y = s16(q + cx)
            if y < ymin:
                border = ymin
            elif y > ymax:
                border = ymax
            else:
                return True, bp, y, 0
        else:
            border = ymin if al & 1 else ymax
        q = s.idiv(s16(border - cx) * dxe, dye)
        x = s16(q + bx)
        if x < xmin:
            return False, x, border, 0x0A
        if x > xmax:
            return False, x, border, 0x05
        return True, x, border, 0

    # ---------------- 12EF:04EF / 056B bisection ----------------
    def pt(s, o):
        return s.rd(o), s.rd(o + 4)

    def setpt(s, o, p):
        s.wd(o, p[0]); s.wd(o + 4, p[1])

    def bisect_in(s, si, di):
        s.ww(0x85B0, 0x20)
        a = s.pt(di)
        s.last_bp = None
        while True:
            s.ww(0x85B0, s.rw(0x85B0) - 1)
            if s.rw(0x85B0) & 0x8000:
                cov('bisect_in 32 iters fail')
                return None
            b = s.pt(si)
            m = (s32(a[0] + b[0]) >> 1, s32(a[1] + b[1]) >> 1)
            c = s.oc32(m[0] & 0xFFFFFFFF, m[1] & 0xFFFFFFFF)
            s.last_bp = c
            if c == 0:
                return m
            if s.rw(0x85AA) & c:
                if s.rw(0x85A8) & c:
                    cov('bisect_in corner fail')
                    return None
                s.setpt(di, m)
                a = m
            else:
                s.setpt(si, m)
                a = s.pt(di)

    @staticmethod
    def half_q(v):                          # 058D/0553 quirk
        v &= 0xFFFFFFFF
        lo, hi = v & 0xFFFF, v >> 16
        if lo == hi:
            cov('half quirk %s' % ('-1' if lo == 0xFFFF else 'zero' if lo == 0 else 'unhalved'))
            return 0 if lo == 0xFFFF else s32(v)
        return s32(v) >> 1

    def bisect_out(s, si, di):
        s.ww(0x85B0, 0x20)
        a = s.pt(di)
        while True:
            s.ww(0x85B0, s.rw(0x85B0) - 1)
            if s.rw(0x85B0) & 0x8000:
                cov('bisect_out 32 iters')
                return s.pt(si)
            b = s.pt(si)
            m = (s.half_q(a[0] + b[0]), s.half_q(a[1] + b[1]))
            c = s.oc32(m[0] & 0xFFFFFFFF, m[1] & 0xFFFFFFFF)
            if c:
                s.setpt(di, m)
                a = m
            else:
                if s.on_border(m[0] & 0xFFFFFFFF, m[1] & 0xFFFFFFFF):
                    cov('bisect_out on border')
                    return m
                s.setpt(si, m)
                a = s.pt(di)

    # ---------------- 12EF:0333 clip_edge ----------------
    def clip_edge(s, si):
        fits = all(s.rw(si + o + 2) == (0xFFFF if s.rw(si + o) & 0x8000 else 0) for o in (0, 8, 4, 0xC))
        if fits:
            if s.fast16(si):
                return
            for o in (0, 4, 8, 0xC):        # 0315
                s.ww(si + o + 2, 0xFFFF if s.rw(si + o) & 0x8000 else 0)
        s.path32(si)

    def fast16(s, si):                      # 0465; returns False to fall into 0315
        s.M[0x85C0] = 0
        x0, y0, x1, y1 = s.rs(si), s.rs(si + 4), s.rs(si + 8), s.rs(si + 0xC)
        c1 = s.oc16(x1, y1)
        c0 = s.oc16(x0, y0)
        if c0 == 0 and c1 == 0:
            s.ww(si + 2, 0)
            return True
        if c0 & c1:
            if c0 & c1 & 3:
                cov('f16 reject y')
                s.ww(si + 2, 0x8080)
                return True
            cov('f16 reject x side')
            s.flags16(si, c0, c1, y0, y1)
            s.ww(si + 2, 0x8585 if c0 & 8 else 0x8A8A)
            return True
        s.flags16(si, c0, c1, y0, y1)
        d = x1 - x0
        if not -0x8000 <= d <= 0x7FFF:
            cov('f16 dx overflow -> 32')
            return False
        s.ww(0x85AC, d)
        d = y1 - y0
        if not -0x8000 <= d <= 0x7FFF:
            return False
        s.ww(0x85AE, d)
        s.M[0x85C1] = c1
        if s.M[0x85C0] & 1:
            cov('f16 85C0 -> 32')
            return False
        ok, x, y, f = s.intercept(c0, x0, y0)
        if not ok:
            cov('f16 fail end0 %x' % f)
            s.ww(si + 2, (f | 0x80) * 0x101)
            return True
        s.ww(si, x); s.ww(si + 4, y)
        ok, x, y, f = s.intercept(s.M[0x85C1], s.rs(si + 8), s.rs(si + 0xC))
        if not ok:
            cov('f16 fail end1 %x' % f)
            s.ww(si + 2, (f | 0x80) * 0x101)
            return True
        cov('f16 clipped')
        s.ww(si + 8, x); s.ww(si + 0xC, y)
        return True

    def path32(s, si):                      # 0359
        x0, y0, x1, y1 = (s.rd(si + o) & 0xFFFFFFFF for o in (0, 4, 8, 0xC))
        c1 = s.oc32(x1, y1)
        c0 = s.oc32(x0, y0)
        if c0 == 0 and c1 == 0:
            s.ww(si + 2, 0)
            raise NearRetBug()
        if c0 & c1:
            cov('p32 trivial reject')
            if c0 & c1 & 3:
                s.ww(si + 2, 0x8080)
            else:
                al = s.flags32(si, c0, c1) | 0x80
                s.ww(si + 2, al * 0x101)
            return
        s.M[0x8568:0x8578] = s.M[si:si + 16]
        s.M[0x8578:0x8588] = s.M[si:si + 16]
        al = s.flags32(si, c0, c1)
        s.ww(si + 2, al * 0x101)
        if c1 == 0:
            cov('p32 end0 out')
            p = s.bisect_out(0x8570, 0x8568)
            s.ww(si, p[0]); s.ww(si + 4, p[1])
            return
        if c0 == 0:
            cov('p32 end1 out')
            p = s.bisect_out(0x8568, 0x8570)
            s.ww(si + 8, p[0]); s.ww(si + 0xC, p[1])
            return
        s.ww(0x85A8, c0); s.ww(0x85AA, c1)
        m = s.bisect_in(0x8568, 0x8570)
        if m is None:
            al = 0x8A if s.last_bp & 4 else 0x85   # BP = out-code of the LAST midpoint (quirk)
            s.ww(si + 2, al * 0x101)
            return
        cov('p32 both out ok')
        s.setpt(0x8568, m)
        p = s.bisect_out(0x8568, 0x8578)
        s.ww(si, p[0]); s.setpt(0x8568, p); s.ww(si + 4, p[1])
        p = s.bisect_out(0x8568, 0x8580)
        s.ww(si + 8, p[0]); s.ww(si + 0xC, p[1])

    # ---------------- 12EF: polygon assembly ----------------
    def note(s, lo_addr, hi_addr, a, b):    # 00AA (85B4/85B6) / 00C5 (85B8/85BA)
        a, b = s16(a), s16(b)
        if b < a:
            a, b = b, a
        if b >= s.rs(hi_addr):
            s.ww(hi_addr, b)
        if a < s.rs(lo_addr):
            s.ww(lo_addr, a)

    def note_left(s, a, b):
        s.note(0x85B4, 0x85B6, a, b)

    def note_right(s, a, b):
        s.note(0x85B8, 0x85BA, a, b)

    def add_edge(s, si):                    # 12EF:003E
        f = s.rw(si + 2)
        s.ww(0x85B2, s.rw(0x85B2) | f)
        al = f & 0xFF
        if al & 0x40:                       # 00E0
            bx = 0x8588 if s.rw(0x85BE) == 0 else 0x8590
            s.ww(0x85BE, s.rw(0x85BE) + 1)
            s.M[bx:bx + 8] = s.M[si + 0x10:si + 0x18]
        if al & 0x80:
            if al & 3:
                (s.note_left if al & 2 else s.note_right)(s.rw(si + 6), s.rw(si + 0xE))
            return
        s.ww(0x85BC, s.rw(0x85BC) | 4)
        s.edge_dda(si)
        al = s.rw(si + 2) & 0xFF
        if al & 3:
            (s.note_left if al & 2 else s.note_right)(s.rw(si + 4), s.rw(si + 6))
        if al & 0xC:
            (s.note_left if al & 8 else s.note_right)(s.rw(si + 0xC), s.rw(si + 0xE))

    def poly_finish(s, colour):             # 12EF:010A
        s.set_colour_M(colour)
        if s.rw(0x85BE) == 2:
            s.clip_edge(0x8588)
            s.add_edge(0x8588)
        f = s.rw(0x85B2)
        cov('finish closing edge' if s.rw(0x85BE) == 2 else 'finish 85BE=%d' % s.rw(0x85BE))
        if s.rw(0x85BC):
            if f & 5:
                s.wall_fill(s.rw(0x85BA), s.rw(0x85B8), s.rw(XMAX) + 1)
            if f & 0xA:
                s.wall_fill(s.rw(0x85B6), s.rw(0x85B4), s.rw(XMIN) - 1)
            s.fill_M()
        elif f & 5 and f & 0xA:
            cov('finish nothing rasterised, both walls')
            s.wall_fill(s.rw(0x85BA), s.rw(0x85B8), s.rw(XMAX) + 1)
            s.wall_fill(s.rw(0x85B6), s.rw(0x85B4), s.rw(XMIN) - 1)
            s.fill_M()
        for o, v in ((0x85B8, 0x7FFF), (0x85B4, 0x7FFF), (0x85BA, 0x8000), (0x85B6, 0x8000),
                     (0x85BC, 0), (0x85B2, 0), (0x85BE, 0)):
            s.ww(o, v)

    # ---------------- 1359 ----------------
    def rows_M(s):                          # 1359:010A
        for i in range(200):
            s.ww(ROWT + 2 * i, i * 320)
        s.ww(0x85C2, 0x70)

    def set_colour_M(s, ax):                # 1359:01D4
        s.ww(0x85D6, ax)
        al, ah = ax & 0xFF, ax >> 8
        if ah == 0xFF and al >= 0xA0:
            ah = al & 0xF0
            al = (al - s.M[0x4894]) & 0xFF
            if al <= ah:
                al = ah
            s.M[0x85D6] = al

    def edge_dda(s, si):                    # 1359:0719
        ax, dx, bx, sv = s.rw(si), s.rw(si + 8), s.rw(si + 4), s.rw(si + 0xC)
        if s16(ax) > s16(dx):
            ax, dx = dx, ax
            sv, bx = bx, sv
        di = (bx * 2) & 0xFFFF
        dx = (dx - ax) & 0xFFFF
        bp = 2
        if s16(sv) < s16(bx):
            bp = -2
            bx, sv = sv, bx
        ymin, ymax = s.rs(YMIN), s.rs(YMAX)
        if s16(bx) < ymin or s16(bx) > ymax or s16(sv) < ymin or s16(sv) > ymax:
            return
        if s16(bx) < s.rs(MINY):
            s.ww(MINY, bx)
        sv = (sv - bx) & 0xFFFF
        L = lambda: s.rs(LT + di)
        Rr = lambda: s.rs(RT + di)
        if sv <= dx:                        # x-major
            cx = (dx + 1) & 0xFFFF
            err = (-(cx >> 1)) & 0xFFFF
            if s16(ax) < L(): s.ww(LT + di, ax)
            while True:
                err = (err + sv) & 0xFFFF
                if not err & 0x8000:
                    if s16(ax) > Rr(): s.ww(RT + di, ax)
                    err = (err - dx) & 0xFFFF
                    cx = (cx - 1) & 0xFFFF
                    if cx == 0:
                        return
                    ax = (ax + 1) & 0xFFFF
                    di = (di + bp) & 0xFFFF
                    if s16(ax) < L(): s.ww(LT + di, ax)
                else:
                    ax = (ax + 1) & 0xFFFF
                    cx = (cx - 1) & 0xFFFF
                    if cx == 0:
                        ax = (ax - 1) & 0xFFFF
                        if s16(ax) > Rr(): s.ww(RT + di, ax)
                        return
        else:                               # y-major
            cx = (sv + 1) & 0xFFFF
            err = (-(cx >> 1)) & 0xFFFF
            while True:
                if s16(ax) < L(): s.ww(LT + di, ax)
                if s16(ax) > Rr(): s.ww(RT + di, ax)
                err = (err + dx) & 0xFFFF
                if not err & 0x8000:
                    cx = (cx - 1) & 0xFFFF
                    if cx == 0:
                        return
                    err = (err - sv) & 0xFFFF
                    ax = (ax + 1) & 0xFFFF
                    di = (di + bp) & 0xFFFF
                else:
                    di = (di + bp) & 0xFFFF
                    cx = (cx - 1) & 0xFFFF
                    if cx == 0:
                        return

    def wall_fill(s, a, b, x):              # 1359:07D6(a=[bp+6], b=[bp+8], x=[bp+0Ah])
        bx, cx, x = s16(a), s16(b), s16(x)
        if cx < bx:
            bx, cx = cx, bx
        ymin, ymax = s.rs(YMIN), s.rs(YMAX)
        if cx <= ymin or bx >= ymax:
            return
        bx = max(bx, ymin)
        cx = min(cx, ymax)
        n = cx - bx
        if n == 0:
            cov('wall single row skipped')
            return
        cov('wall drawn')
        if bx < s.rs(MINY):
            s.ww(MINY, bx)
        for row in range(bx, bx + n + 1):
            o = (2 * row) & 0xFFFF
            if x < s.rs(LT + o): s.ww(LT + o, x)
            if x > s.rs(RT + o): s.ww(RT + o, x)

    def spans(s, start_check_unsigned=False):
        """yields (row, L, R) clamped or None (skipped), resetting the tables; mirrors the row loop"""
        r = s.rw(MINY)
        if r == 0x7FFF or (start_check_unsigned and r >= 0x7FFF):
            s.ww(MINY, 0x7FFF)
            return
        si = (2 * r) & 0xFFFF
        xmin, xmax = s.rs(XMIN), s.rs(XMAX)
        while True:
            L, Rr = s.rs(LT + si), s.rs(RT + si)
            if L == 0x7FFF:
                s.ww(MINY, 0x7FFF)
                return
            if not (L > xmax or Rr < xmin):
                yield si, s.rw(ROWT + si), max(L, xmin), min(Rr, xmax)
            s.ww(LT + si, 0x7FFF); s.ww(RT + si, 0x8001)
            si = (si + 2) & 0xFFFF

    def fill_M(s):                          # 1359:0AF7
        ax = s.rw(0x85D6)
        al, ah = ax & 0xFF, ax >> 8
        if ah < 0xFB or ah == 0xFF:
            if ah == 0xFF:
                ah = al
            s.ww(0x9140, al | ah << 8)
            for si, base, L, Rr in s.spans():
                for x in range(L, Rr + 1):
                    s.P[(base + x) & 0xFFFF] = ah if (x ^ (si >> 1)) & 1 else al
        elif ah == 0xFE or ah == 0xFD:
            for si, base, L, Rr in s.spans():
                for x in range(L, Rr + 1):
                    o = (base + x) & 0xFFFF
                    s.P[o] = s.P[o] & al if ah == 0xFE else s.P[o] | al
        elif ah == 0xFC:
            for si, base, L, Rr in s.spans(True):
                for x in range(L, Rr + 1):
                    if (x ^ (si >> 1)) & 1:
                        s.P[(base + x) & 0xFFFF] = al
        else:                               # FB: clear only
            r = s.rw(MINY)
            if r == 0x7FFF:
                return
            for _ in s.spans():
                pass

    def line_M(s, si):                      # 1359:045D
        cx, di, bp, bx = s.rs(si + 0xC), s.rs(si + 4), s.rs(si + 8), s.rs(si)
        ady = abs(s16(cx - di)) & 0xFFFF
        adx = abs(s16(bp - bx)) & 0xFFFF
        c = s.M[0x85D6]
        if ady <= adx:
            if bp < bx:
                bx, bp = bp, bx
                di, cx = cx, di
            step = -320 if cx < di else 320
            o = (s.rw(ROWT + 2 * di) + bx) & 0xFFFF
            n = (bp - bx + 1) & 0xFFFF
            err = (-(n >> 1)) & 0xFFFF
            while True:
                s.P[o] = c
                o = (o + 1) & 0xFFFF
                err = (err + ady) & 0xFFFF
                if not err & 0x8000:
                    o = (o + step) & 0xFFFF
                    err = (err - adx) & 0xFFFF
                n = (n - 1) & 0xFFFF
                if n == 0:
                    return
        else:
            if cx < di:
                bx, bp = bp, bx
                di, cx = cx, di
            step = -1 if bp < bx else 1
            n = (cx - di + 1) & 0xFFFF
            o = (s.rw(ROWT + 2 * di) + bx) & 0xFFFF
            err = (-(n >> 1)) & 0xFFFF
            while True:
                s.P[o] = c
                o = (o + 320) & 0xFFFF
                err = (err + adx) & 0xFFFF
                if not err & 0x8000:
                    o = (o + step) & 0xFFFF
                    err = (err - ady) & 0xFFFF
                n = (n - 1) & 0xFFFF
                if n == 0:
                    return
