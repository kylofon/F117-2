"""Python reference of VGAME 1e9e (per-frame input + player flight integration), written from the disassembly.
State is a 64 KB bytearray = DGROUP (+ comm / pilot blocks) so it can be diffed byte for byte against the Unicorn run."""
from r2_ref import *


def abs16(v):
    v = i16(v)
    return i16(-v) if v < 0 else v          # abs() of the RTL (0x8000 stays 0x8000)


class FM:
    def __init__(s, mem, comm, pilot, keys=()):
        s.m = mem; s.C = comm; s.P = pilot; s.keys = list(keys); s.log = []

    def u(s, o): return s.m[o] | (s.m[o + 1] << 8)
    def i(s, o): return i16(s.u(o))
    def b(s, o): return s.m[o]

    def sw(s, o, v):
        v &= 0xffff; s.m[o] = v & 0xff; s.m[o + 1] = v >> 8

    def sb(s, o, v): s.m[o] = v & 0xff
    def l(s, o): return s.u(o) | (s.u(o + 2) << 16)

    def sl(s, o, v):
        v &= 0xffffffff; s.sw(o, v & 0xffff); s.sw(o + 2, v >> 16)

    def pu(s, o): return s.P[o] | (s.P[o + 1] << 8)

    def rand(s):
        seed = (s.l(0x926e) * 0x343fd + 0x269ec3) & 0xffffffff
        s.sl(0x926e, seed)
        return (seed >> 16) & 0x7fff

    def rand_n(s, n): return i16((s.rand() * i16(n)) >> 15)                 # cb62

    def snd(s, cmd, arg=None): s.log.append(('snd', cmd, arg))

    def d6e5(s):                                                              # engine drone on/off
        s.snd(0x18 if (s.i(0x43c2) == 0 and s.i(0x2dfe) != 0 and s.i(0xc06e) == 0) else 0x19)

    def d6bf(s, c, prio):                                                     # sound cue with priority
        if prio >= s.i(0x43c2) and (s.i(0xc06e) == 0 or prio > 1): s.snd(c)
        s.d6e5()

    def lamp(s, n, on):                                                       # d83e on the page-0 context
        o = 0x46d8 + 12 * n
        x, y, w, h, st = s.u(o), s.u(o + 2), s.u(o + 4), s.u(o + 6), s.u(o + 8)
        fr, to = (0, 1) if on else (1, 0)
        base = 0x4672 + 16 * st
        s.log.append(('recolor', x, y, x + w, y + h, s.u(base + 2 * fr), s.u(base + 2 * to)))
        s.log.append(('recolor', x, y, x + w, y + h, s.u(base + 2 * fr + 4), s.u(base + 2 * to + 4)))
        if st == 4:
            s.log.append(('recolor', x, y, x + w, y + h, s.u(base + 2 * fr + 8), s.u(base + 2 * to + 8)))
            s.log.append(('recolor', x, y, x + w, y + h, s.u(base + 2 * fr + 12), s.u(base + 2 * to + 12)))
        s.sw(o + 10, to)

    def need_no_panel(s):
        assert s.u(0x365e) == 0, 'test assumes instrument panel off'

    def rebuild(s):                                                           # 2e43
        a, b, c = s.u(0x2dee), s.i(0x2df0), s.u(0x2df2)
        for k, ang in enumerate((a, b, c)):
            s.sw(0x4982 + 4 * k, sin16(ang)); s.sw(0x4984 + 4 * k, cos16(ang))
        M = build_mat(a, b, c)
        for k in range(9): s.sw(0x2d94 + 2 * k, M[k])
        s.sb(0x2dfb, 0); s.sw(0x2df8, 0)

    def rot(s, A, B):                                                         # 2c6e(A,B): mat <- A x B
        s.sw(0x2df8, s.u(0x2df8) + 1)
        if s.u(0x2df8) & 7 == 0: s.sb(0x2dfb, 1)
        a = [s.i(A + 2 * k) for k in range(9)]
        b = [s.i(B + 2 * k) for k in range(9)]
        C = mat_mul(a, b)
        for k in range(9): s.sw(0x2ddc + 2 * k, C[k])
        for k in range(9): s.sw(0x2d94 + 2 * k, C[k])

    def euler(s):                                                             # 2c9e
        M = [s.i(0x2d94 + 2 * k) for k in range(9)]
        yaw, pitch, roll = euler_from_mat(M)
        s.sw(0x2df0, pitch); s.sw(0x2dee, yaw); s.sw(0x2df2, roll)
        p = s.i(0x2df0)
        if 0x38e3 < p < 0x4001: s.sb(0x2dfb, 1)
        if i16(0xbfff) < p < i16(0xc71d): s.sb(0x2dfb, 1)
        if s.b(0x2dfa) != 0 and s.i(0x2df2) == 0: s.sb(0x2dfb, 1)

    def cstr(s, o):
        e = o
        while s.m[e]: e += 1
        return e

    def strcpy(s, o, t):
        for k, ch in enumerate(t + '\0'): s.m[o + k] = ord(ch)

    def strcat(s, o, t): s.strcpy(s.cstr(o), t)

    def itoa(s, v):
        t = str(v); s.strcpy(0x9876, t); return t

    def frame(s):
        S = s
        if S.i(0xc0ae) == 0:
            for o in (0xb052, 0x2df6, 0x2df4, 0xb170, 0x2dfe, 0x2df2, 0x2df0): S.sw(o, 0)
            th = S.pu(0x38)
            S.sw(0x2dee, 0 if S.i(0x3d7c + 2 * th) > 0 else 0x8000)
            if S.b((0xb2ab + ((S.u(0xe2dc) << 4) & 0xffff)) & 0xffff) & 2: S.sb(0x2def, S.b(0x2def) + 4)
            S.rebuild(); S.need_no_panel(); S.sw(0xc0ae, 1)
        S.sw(0xde90, 0)
        key = 0
        if S.keys:
            key = S.keys[0]; S.sw(0xde90, key)
        S.keys = []
        if key == 0x011b:
            if S.i(0xc06e) != 0: S.sw(0xdeca, 0)
        elif key == 0x0b30:                                                   # '0' wheel brakes
            S.sb(0x9b04, S.b(0x9b04) ^ 8); S.lamp(0x23, S.b(0x9b04) & 8)
            if not S.b(0x9b04) & 8:
                if S.u(0xc0a2) != 0 and S.i(0x2dfe) == 0x64:
                    S.sw(0xb052, 0x546); S.d6bf(0x11, 2)
        elif key in (0x0c2d, 0x0c5f, 0x0d2b, 0x0d3d):                         # throttle keys
            if key == 0x0c2d: S.sw(0x2dfe, clamp_w(S.i(0x2dfe) - 10, 0, 100))
            elif key == 0x0c5f:
                S.sw(0x2dfe, 0); S.d6bf(0xc, 0)
            elif key == 0x0d2b: S.sw(0x2dfe, 0x64)
            else: S.sw(0x2dfe, clamp_w(S.i(0x2dfe) + (5 if S.i(0x2dfe) < 10 else 10), 0, 100))
            S.need_no_panel()
        elif key == 0x2e63:                                                   # 'c'
            S.sb(0x294b, 1 if S.b(0x294b) == 0 else 0); S.log.append(('far', '10a8:0008'))
        elif key == 0x1f00:                                                   # Alt-S joystick recentre
            if S.u(0x2e02) == 0:
                S.log.append(('far', '10a8:0c87')); S.sw(0x2e02, 0x28)
        elif key in (0x1000, 0x1900, 0x3000):
            raise NotImplementedError
        # 1f89
        if S.u(0x2e02): S.sw(0x2e02, S.u(0x2e02) - 1)
        if S.i(0x2dfe) != 0 and S.i(0xb170) == 0: S.d6bf(0xb, 2)
        if S.C[0x72] & 1:
            S.log.append(('far', '10a8:0ca8'))
        else:
            cx = S.u(0x43ba) + 1
            for src, dst in ((0x2cd6, 0x2cd2), (0x2cd7, 0x2cd3)):
                ax = S.b(src) - 0x80
                ax = tdiv(i16(ax * cx), 3)
                S.sb(dst, ax + 0x80)
        S.sw(0xe446, (S.b(0x2cd3) >> 4) - 8)
        S.sw(0xde8a, (S.b(0x2cd2) >> 4) - 8)
        if S.i(0xde8a) < 0: S.sw(0xde8a, S.i(0xde8a) + 1)
        if S.i(0xe446) < 0: S.sw(0xe446, S.i(0xe446) + 1)
        r = S.i(0xde8a)
        S.sw(0xde8a, i16(i16(-(abs16(r) + 2)) * r) * 2)
        p = i16(6 * S.i(0xe446))
        if p < 0: p = (p + 1) >> 1
        S.sw(0xe446, p)
        if S.u(0x2df4) == S.u(0xc0a2) and S.i(0xe446) < 0 and S.i(0x2df0) <= 0: S.sw(0xe446, 0)
        if S.i(0xde8a) != 0 or S.i(0xe446) != 0: S.sw(0x3652, 0)
        S.need_no_panel()
        if S.u(0x3652) != 0:
            a = clamp(S.i(0xc078) - S.i(0x2dee), i16(0xec00), 0x1400)
            bp28 = i16(a << 1)
            S.sw(0xde8a, -clamp_w(i16(bp28 - S.i(0x2df2)) >> 6, -0x18, 0x18))
            bp10 = clamp(i16((i16(S.i(0x3652) - S.i(0x2df4)) << 4) - S.i(0x98c6)), i16(0xec00), 0xc00)
            S.sw(0xe446, clamp_w(i16(bp10 - S.i(0x2df0)) >> 7, -8, 8))
        cm = S.pu(0x40)
        bp24 = i16((i16(500 - S.i(0x2df4)) * S.i(0xb168)) >> 13) if cm != 0 else 0
        if not S.b(0x9b04) & 1: bp24 = i16(bp24 + clamp_w(i16(S.i(0xb168) - 0xc8) >> 4, 0, 0x20))
        if S.b(0x9b04) & 2: bp24 >>= 1
        if S.b(0x3632) & 4: bp24 = i16(bp24 + clamp_w(i16(S.i(0xc5c8) << 2), 0, 0x20))
        if bp24 > 0 and S.u(0xc0a2) < S.u(0x2df4):
            S.sw(0xde8a, S.i(0xde8a) + S.rand_n(bp24) - (bp24 >> 1))
            S.sw(0xe446, S.i(0xe446) + ((S.rand_n(bp24) - (bp24 >> 1)) >> 1))
        if (S.b(0x9b04) & 1) and S.i(0xe446) <= 0 and S.u(0xb052) > S.u(0xb00a) and cm < 2 \
                and abs16(S.i(0x2df2)) < 0x3000 and S.i(0x98ea) == 0:
            t = i16(i16(i16(i16(S.i(0x98c6) - S.i(0x2df0)) >> 3) - S.i(0x2df4) + 0xc8) >> 3)
            if t > 0: S.sw(0xe446, clamp_w(t, 0, 0x20))
        rate = S.i(0x3660)
        if S.i(0xc06e) != 0:
            S.sw(0xde8a, 0x40)
            S.sw(0xe446, 0x10 if abs16(S.i(0x2df2)) > 0x4000 else 0xfff8)
            hi = tdiv(0x80, rate); lo = tdiv(-0x60, rate)
            S.sw(0xc06e, S.i(0xc06e) + 1)
            v = i16(0x20 - S.i(0xc06e))
            S.sw(0xdeca, S.i(0xdeca) + clamp_w(v, lo, hi))
            if S.i(0xdeca) < 0:
                S.sw(0xdeca, 0)
                if S.u(0x98e2) & 7 == 0: S.log.append(('end', 0))
            if S.u(0x2df4) == 0 and S.i(0x3d68) == -1:
                S.sw(0xb052, 0); S.sw(0x2dfe, 0); S.sw(0xc0b0, 0); S.sw(0x3d68, 0)
                S.sw(0xc0a0, S.u(0xc0a4)); S.sw(0xb2a4, S.u(0xc0a4)); S.sw(0xc0aa, S.u(0xc0b2)); S.sw(0xb2a6, S.u(0xc0b2))
                S.sw(0x9d0c, 0xfff0); S.d6bf(5, 2)
            if (S.u(0xc06e) & 0xff00) == 0 and (S.u(0xc06e) & 0xfc) == 0x10 and (S.b(0x3d60) & 3) == 1:
                S.sw(0x3d68, 0xffff); ax = S.b(0x3d60) & 0xf; S.sw(0x3a5a, ax)
                S.sw(0xc0a0, S.u(0xc0a4)); S.sw(0x39da + 8 * ax, S.u(0xc0a4))
                S.sw(0xc0aa, S.u(0xc0b2)); S.sw(0x39dc + 8 * ax, S.u(0xc0b2))
                S.sw(0xc0b0, S.u(0x2df4)); S.sw(0x39de + 8 * ax, S.u(0x2df4))
                S.sw(0x9d0c, 0xfff0); S.d6bf(4, 2); S.sw(0x2df0, 0xc000); S.sb(0x2dfb, 1)
        if S.b(0x3632) & 2:
            m = i16(i16(0x19 - S.i(0xc5c8)) << 2)
            if m < S.i(0x2dfe):
                S.sw(0x2dfe, m)
                if m < 0: S.sw(0x2dfe, 0)
        d = i16(S.i(0x2dfe) - S.i(0xb170))
        S.sw(0xb170, S.i(0xb170) + tdiv(tdiv(d, 4), rate))
        if S.i(0xb170) < S.i(0x2dfe): S.sw(0xb170, S.i(0xb170) + 1)
        if S.i(0x2dfe) < S.i(0xb170): S.sw(0xb170, S.i(0x2dfe))
        if S.u(0x3d60) % ((S.u(0x3660) << 1) & 0xffff) == 0 and S.i(0x2dfe) != 0:
            cxx = S.u(0xc5c8) if S.b(0x3632) & 0x20 else 0
            t = S.i(0x2dfe); ax = tdiv(i16(t * t), -1000)
            S.sw(0x3634, S.i(0x3634) + i16(ax - cxx - 2))
        if S.i(0x3634) <= 0:
            S.sw(0xb170, 0); S.sw(0x3634, 0)
        idx = (abs16(S.i(0x2df2)) & 0x7f00) >> 8
        S.sw(0x98dc, S.b(0x2d14 + idx))
        if S.u(0xc0a2) < S.u(0x2df4): S.sw(0x98dc, S.i(0x98dc) + S.i(0xe446))
        if S.i(0x98dc) > 0x80:
            S.sw(0x98dc, 0x80)
            idx = (abs16(S.i(0x2df2)) & 0x7f00) >> 8
            S.sw(0xe446, clamp_w(0x80 - S.b(0x2d14 + idx), 0, S.i(0xe446)))
        g = S.i(0x98dc)
        S.strcpy(0x98d0, S.itoa(tdiv(g, 16))); S.strcat(0x98d0, '.')
        S.strcat(0x98d0, S.itoa((abs16(g) & 0xe) >> 1)); S.strcat(0x98d0, 'G')
        # --- speed / lift model
        A = sin_mul(S.i(0x2df0), 0x78)
        bp2c = i16(tdiv(i16(S.i(0xb170) - A) * 720, 100))
        ax = (((S.u(0x2df6) >> 6) + 0x400) & 0xffff) * 0x78
        S.sw(0xe448, i16(ax >> 10)); e448 = S.i(0xe448)
        b1 = ((S.u(0x2df4) >> 7) + 0x400) & 0xffff
        ax = i16((bp2c * b1) >> 10)
        cx = i16(0x64 - (S.i(0x3634) >> 9))
        q = i16(tdiv(ax * cx, 90))
        bp2c = i16((q * i16(0x80 - S.i(0x98dc))) >> 7)
        t = isqrt(i16(S.i(0x98dc) << 2))
        e = i16((e448 * t) >> 3)
        S.sw(0xe448, abs16(e)); e = abs16(e)
        if S.b(0x9b04) & 2:
            S.sw(0xe448, S.i(0xe448) - (e >> 2)); bp2c = i16(bp2c - (bp2c >> 4))
        if not S.b(0x9b04) & 1: bp2c = i16(bp2c - (bp2c >> 3))
        S.sw(0xb00a, i16(S.i(0xe448) * 0x1b))
        bp18 = i16(clamp_w(bp2c, 0, 0x383) * 0x1b)
        diff = i16(bp18) - S.i(0xb052)
        q2 = tdiv(tdiv(diff, 16), rate)
        S.sw(0xb052, S.u(0xb052) + q2)
        bp16 = clamp_w(i16(bp18 - S.i(0xb052)) >> 2, 0, S.i(0xb00a))
        den = abs16(S.i(0xb052)) + 1
        S.sw(0xb758, tdiv(i16(S.i(0xb00a) - bp16) * 0xc00, den))
        if S.u(0xb758) > 0x2000: S.sw(0xb758, 0x2000)
        S.sw(0x98c6, cos_mul(S.i(0x2df2), i16(S.u(0xb758) - 0x200)))
        if S.b(0x9b04) & 8:
            if S.u(0x2df4) == S.u(0xc0a2):
                ax = i16((4 - cm) * 0xd8) & 0xffff
                S.sw(0xb052, S.u(0xb052) - ax // S.u(0x3660))
                if S.u(0xc0a2) != 0 and S.u(0xb052) < 0x1b0: S.sw(0xb052, 0)
            else:
                S.sw(0xb052, S.u(0xb052) - (S.u(0xb052) >> 4) // S.u(0x3660))
        if S.u(0xb052) > 0xafc8: S.sw(0xb052, 0)
        bp22 = cos_mul(S.i(0x2df0), S.i(0xb052))
        S.sw(0xb168, S.u(0xb052) // 27); S.snd(0x17, S.u(0xb168))
        D = S.b(0xb053) + 0x20
        ls = sin_mul(S.i(0x2df2), i16(S.i(0x98dc) << 4))
        bp14 = cos_mul(S.i(0x2df0), i16(tdiv(ls << 7, D)))
        if S.u(0x2df4) == S.u(0xc0a2):
            bp14 = i16(-i16(S.i(0xde8a) << 6)); S.sw(0xde8a, 0)
            if S.i(0xb168) < S.i(0xe448): S.sw(0xe446, 0)
        if S.i(0x98e0) != 0:
            S.sw(0xe446, 0xfc00 - S.i(0x2df0)); S.sw(0xb052, 0); S.sw(0x2dfe, 0)
        bp26 = i16(tdiv(S.i(0xde8a) << 7, rate))
        if bp26 != 0:
            c = cos16(bp26); sn = sin16(bp26)
            S.sw(0x2dd2, c); S.sw(0x2dca, c); S.sw(0x2dcc, sn); S.sw(0x2dd0, -sn)
            S.rot(0x2d94, 0x2dca)
        bp20 = tdiv(i16(S.i(0xe446) << 7), rate)
        if bp20 != 0:
            c = cos16(bp20); sn = sin16(bp20)
            S.sw(0x2dc8, c); S.sw(0x2dc0, c); S.sw(0x2dc6, sn); S.sw(0x2dc2, -sn)
            S.rot(0x2d94, 0x2db8)
        bp12 = tdiv(bp14, rate)
        if bp12 != 0:
            c = cos16(bp12); sn = sin16(bp12)
            S.sw(0x2db6, c); S.sw(0x2da6, c); S.sw(0x2daa, sn); S.sw(0x2db2, -sn)
            S.rot(0x2da6, 0x2d94)
        S.euler()
        if S.u(0xb052) < S.u(0xb00a) and S.u(0xc0a2) < S.u(0x2df4):
            n = 1 if (cm == 2 or S.i(0xc5c8) > 8) else 2
            S.sb(0x2dfb, 1)
            S.sw(0x2df0, S.u(0x2df0) - (((S.u(0xb00a) - S.u(0xb052)) & 0xffff) >> n))
            if S.i(0x2df0) < 0 or S.u(0x2df4) < 0xc8: S.d6bf(0xe, 1)
        if S.u(0x2df4) == S.u(0xc0a2):
            if S.i(0x2df2) != 0:
                S.sw(0x2df2, 0); S.sb(0x2dfb, 1)
            p = S.i(0x2df0)
            if p < 0 or (p > 0 and S.i(0xb168) < S.i(0xe448)):
                if S.i(0x98e0) == 0: S.sw(0x2df0, 0)
                S.sb(0x2dfb, 1)
        S.sw(0x98e0, 0)
        if S.i(0xe2fe) != 0:
            si = abs16(S.i(0x2df2)); si = (si + (1 if si < 0 else 0)) >> 1
            S.sb(0xe542, 1 if (abs16(S.i(0x2df0)) - si) > 0x1000 else 0)
        else:
            S.sb(0xe542, 1)
        if S.b(0x2dfb): S.rebuild()
        bp0c = S.i(0x2df6)
        e55a = mul15(S.u(0xb052) >> 4, sin16(S.i(0x2df0) - S.i(0x98c6)))
        S.sw(0xe55a, e55a)
        S.sw(0x2df6, S.u(0x2df6) + tdiv(e55a, rate))
        v1 = mul15(bp22, sin16(S.u(0x2dee)))
        S.sl(0xba10, S.l(0xba10) + tdiv(tdiv(v1, 16), rate))
        v2 = mul15(bp22, cos16(S.u(0x2dee)))
        S.sl(0xc02a, S.l(0xc02a) + tdiv(tdiv(v2, 16), rate))
        if S.u(0x2df6) > 0xf230 or S.u(0xc0a2) > S.u(0x2df6): S.sw(0x2df6, S.u(0xc0a2))
        if S.u(0x2df6) > 0xea60: S.sw(0x2df6, 0xea60)
        a = S.u(0x2df6)
        if a < 0x2000: v = a
        elif a < 0x4000: v = ((a - 0x2000) >> 1) + 0x2000
        else: v = (((a - 0x4000) >> 2) + 0x3000) & 0xffff
        S.sw(0x2df4, v)
        if S.u(0x2df4) == S.u(0xc0a2):
            if bp0c > S.i(0xc0a2) and S.i(0x3d74) != 0:
                if S.i(0xc06e) == 0: S.d6bf(0xa, 2)
                lim = 0x100 if S.b((0xb2ab + ((S.u(0xdfe0) << 4) & 0xffff)) & 0xffff) & 2 else 0x80
                ax = (-(cm * S.u(0xe55a))) & 0xffff
                crash = False
                if lim < ax: crash = True
                elif cm == 0: pass
                elif S.b(0x9b04) & 1: crash = True
                elif (((0x18 // cm) & 0xff) << 8) < (abs16(S.i(0x2df2)) & 0xffff): crash = True
                if crash:
                    S.d6bf(4, 2); S.log.append(('wait', 0x3c)); S.log.append(('end', 5))
            S.sw(0xe55a, 0)
        idx = S.b(0x3d60) & 0xf; base = 0xb056 + 16 * idx
        S.sl(base, S.l(0xba10)); S.sl(base + 4, S.l(0xc02a)); S.sw(base + 8, S.u(0x2df4))
        S.sw(base + 10, S.u(0x2dee)); S.sw(base + 12, S.u(0x2df0)); S.sw(base + 14, S.u(0x2df2))
        if S.i(0xe2fe) == 1:
            if S.i(0x3d64) >= 0:
                o = (S.i(0x3d64) * 0x24) & 0xffff
                dsx = dist2(i16(S.i(0xc0a4) - S.i((0xc140 + o) & 0xffff)), i16(S.i(0xc0b2) - S.i((0xc142 + o) & 0xffff)))
                n = clamp_w(i16(dsx * S.i(0x3660)) >> 7, 0, 0xc)
            else:
                n = S.i(0x3660) - 1
            rec = 0xb056 + 16 * ((S.b(0x3d60) - n) & 0xf)
            dy = i16(S.i(0x2dee) - S.i(rec + 10)); dp = i16(S.i(0x2df0) - S.i(rec + 12))
            S.sw(0xe546, sin_mul(S.i(0x2df2), dp >> 2) + cos_mul(S.i(0x2df2), i16(-dy) >> 2))
            S.sw(0xe550, sin_mul(S.i(0x2df2), dy >> 2) + cos_mul(S.i(0x2df2), dp >> 1))
