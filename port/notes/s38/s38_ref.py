"""S3.8 Python reference of the VGAME threat / AI / countermeasure / damage code, written from the disassembly.
Functions: 5460 det_radar_signal, 43ee (signature part), 5588, 559a, 516d, 62d0, 65d4, 5730 ai_update_units,
4512, 461c, 465b, 711f, 74f6, d707, d7bb.
State = a 64 KB bytearray = DGROUP (+ the pilot / comm blocks as separate bytearrays), so it can be diffed byte for
byte against the Unicorn run (s38_test.py). Calls that draw, play sound or post text are appended to `log`.
Uses the helpers of port/notes/s36/r2_ref.py (sin/cos/mul15, c94e, c987, c9d6)."""
import os, sys
_here = os.path.dirname(os.path.abspath(__file__))
_root = _here
while _root and not os.path.exists(os.path.join(_root, 'PLAN.md')):
    _root = os.path.dirname(_root)
sys.path.insert(0, os.path.join(_root, 'port', 'notes', 's36'))
from r2_ref import i16, u16, tdiv, sin_mul, cos_mul, clamp_w, dist2, heading, isqrt  # noqa: E402

UNIT, SITE, PROJ, DECOY, TRACER, AC, WPN = 0xC13E, 0xB2A2, 0x3C0C, 0x39AA, 0xE44E, 0x2FC2, 0x3362


def sx8(v):
    v &= 0xff
    return v - 256 if v & 0x80 else v


def abs16(v):
    v = i16(v)
    return i16(-v) if v < 0 else v          # RTL abs (f302): abs(-32768) stays -32768


class S38:
    def __init__(s, mem, pilot, comm):
        s.m = mem; s.P = pilot; s.C = comm; s.log = []
        s.probe = None                      # callable(x, y, z) -> byte for 77d9 ([C686])

    # --- memory ---------------------------------------------------------------------------------------------
    def u(s, o): return s.m[o & 0xffff] | (s.m[(o + 1) & 0xffff] << 8)
    def i(s, o): return i16(s.u(o))
    def b(s, o): return s.m[o & 0xffff]
    def sb(s, o, v): s.m[o & 0xffff] = v & 0xff

    def sw(s, o, v):
        v &= 0xffff; s.m[o & 0xffff] = v & 0xff; s.m[(o + 1) & 0xffff] = v >> 8

    def l(s, o): return s.u(o) | (s.u(o + 2) << 16)

    def sl(s, o, v):
        v &= 0xffffffff; s.sw(o, v & 0xffff); s.sw(o + 2, v >> 16)

    def pw(s, o): return i16(s.P[o] | (s.P[o + 1] << 8))           # pilot record word (far ptr DS:98F4)

    def cadd(s, o, n):                                               # comm word += n (far ptr DS:E548)
        v = (s.C[o] | (s.C[o + 1] << 8)) + n
        s.C[o] = v & 0xff; s.C[o + 1] = (v >> 8) & 0xff

    def cstr(s, o):
        e = o
        while s.m[e]: e += 1
        return bytes(s.m[o:e])

    def strcpy(s, d, src):
        t = s.cstr(src) if isinstance(src, int) else src
        s.m[d:d + len(t) + 1] = t + b'\0'

    def strcat(s, d, src):
        s.strcpy(d + len(s.cstr(d)), src)

    # --- library -------------------------------------------------------------------------------------------
    def rand(s):
        seed = (s.l(0x926e) * 0x343fd + 0x269ec3) & 0xffffffff
        s.sl(0x926e, seed)
        return (seed >> 16) & 0x7fff

    def rand_n(s, n): return i16((s.rand() * i16(n)) >> 15)           # cb62

    def rate(s): return s.i(0x3660)

    def d707(s):                                                      # frame_params_update
        r = s.rate()
        s.sw(0x43b8, clamp_w((tdiv(-120, r) + 9) >> 1, 1, 4) if r > 15 else 0)
        r = clamp_w(r, 4 - s.i(0x3d7a), 15); s.sw(0x3660, r)
        s.sw(0xb16c, clamp_w(i16(r << 1), 3, 16))
        s.sw(0xb286, r * 250); s.sw(0x9f64, r * 200)

    def d7bb(s):                                                      # frame_rate_halve (time acceleration off)
        if s.i(0x3d7a) == 2:
            s.sw(0x3d7a, 1); s.sw(0x3660, s.u(0x3660) << 1); s.d707()

    # stubs (logged; the Unicorn harness hooks the same addresses)
    def snd_cue(s, c, p): s.log.append(('d6bf', c, p))
    def threat_bar(s, v, c, y0, y1): s.log.append(('8316', v & 0xffff, c & 0xffff, y0, y1))
    def map_plot(s, x, y, c, big): s.log.append(('8548', x & 0xffff, y & 0xffff, c & 0xffff, big))
    def msg_post(s, a): s.log.append(('8b93', s.cstr(a)))
    def log_event(s, c, a): s.log.append(('499e', c & 0xffff, a & 0xffff))

    def site_label(s, i):                                             # 49e7 stub: writes "S<i>" to DS:9876
        s.log.append(('49e7', i & 0xffff)); s.strcpy(0x9876, b'S%d' % i16(i))

    def lamp(s, n, v): s.log.append(('898b', n, v & 0xffff))

    # --- detection -----------------------------------------------------------------------------------------
    def det_radar_signal(s, x, y, z, rt):                             # 5460 -> (sig, bearing, dist) ; bearing/dist None = not written
        rt = i16(rt)
        if rt == 0 or rt == -1: return 0, None, None
        dx = i16(s.u(0xc0a4) - x); dy = i16(s.u(0xc0b2) - y)
        D = dist2(dx, dy) >> 6
        R = 0x2E80 + 14 * rt
        maxr = s.i(R + 8)
        cell = s.b(0xB174 + ((s.u(0xc0b2) >> 11) << 4) + (s.u(0xc0a4) >> 11)) & 0x0C
        k = i16(2 * s.i(0x3654) + s.i(R + 10) + 1)
        num = (maxr - D) * (cell * k)
        sig0 = i16(int(num / maxr))                                   # __aFldiv truncates toward zero
        brg = heading(dx, i16(-dy))
        hb = u16(abs16(brg - s.u(0x2dee))) >> 8
        a = hb - 256 if hb & 0x80 else hb                             # cbw of the high byte (abs(0x8000) -> -128)
        if a > 0x40: a = 0x80 - a
        f = a
        if s.b(R + 12) & 1:
            f = (u16((0x60 - a) * (s.u(0xb052) >> 5))) >> 9
        sig = i16(i16((i16(f + 0x20) >> 1) * (sig0 >> 1)) >> 4)
        if u16(abs16(i16(z - s.u(0x2df4))) >> 10) > D: sig = 0
        if i16(sig + s.u(0x3d5c)) > 100: s.d7bb()
        if s.pw(0x42) != 0: sig >>= 1
        return sig, brg, D

    def signature(s):                                                 # 43ee after cba0
        t = abs16(i16(s.b(0x2df3) - 256 if s.b(0x2df3) & 0x80 else s.b(0x2df3)))
        if t >= 0x40: t = 0x80 - t
        sk = s.i(0x3654)
        if s.pw(0x42) != 0:
            v = heading(i16((sk + 1) << 7), 0x800) >> 8
            v = v - 256 if v & 0x80 else v
            sig = v + (s.u(0xb052) >> 10) + (t >> 1)
            if s.u(0x9b04) == 0: sig += 0x14
        else:
            v = heading(i16(u16((s.u(0x2df4) // 3) * (sk + 1))), 0x800) >> 8
            v = v - 256 if v & 0x80 else v
            sig = v + (s.u(0xb052) >> 10) + (t >> 1)
        if s.b(0x9b04) & 4: sig += 0x18
        if s.b(0x9b04) & 0x10: sig += 0x10
        s.sw(0x3d5c, sig)

    def det_radar_warning(s):                                         # 5588
        s.sw(0x3d92, 2 * s.rate()); s.snd_cue(8, 1)

    def det_alert_network(s):                                         # 559a
        s.sw(0x3d62, s.u(0xb286))
        if s.u(0x39b2) != 0: s.sw(0xb2a0, s.u(0x39aa)); s.sw(0xb75a, s.u(0x39ac))
        else: s.sw(0xb2a0, s.u(0xc0a4)); s.sw(0xb75a, s.u(0xc0b2))
        s.sw(0xb76e, s.u(0x2df4)); s.sw(0x98f0, s.u(0x2dee)); s.sw(0xc0b6, 0xff)
        lo = i16((s.i(0x3656) + s.i(0x3654) - 1) << 4)
        for i in range(max(0, s.i(0xe540))):
            r = SITE + 16 * i
            if s.u(r + 6) != 0: s.sw(r + 10, clamp_w(s.i(r + 10), lo, 0xff))

    def det_site_radar_update(s, site):                               # 516d
        r = SITE + 16 * site; t = s.i(r + 6)
        sig, brg, D = s.det_radar_signal(s.u(r + 2), s.u(r + 4), 0, t)
        s.sw(0x3d6c, 0)
        sg = s.i(0x3d5c)
        if sig > 0:
            shown = min(sig, 99)
            c = 0x2A
            if sg + sig > 50: c = 0x29
            if sg + sig > 100: c = 0x11
            s.sw(0x3d6c, c)
            s.threat_bar(shown, c, 1, 4)
            s.sw(0xc070, i16(int(i16(sg + sig) * D / 100)))
            s.sw(0x3d6a, s.u(0x3660)); s.sw(0xc07e, site)
            s.sw(0xe54c, s.b(0x2E80 + 14 * t + 12) & 1)
            if s.u(r + 10) != 0:
                hb = sx8(brg >> 8)
                s.sw(0xc13a, hb - 0x20); s.sw(0xc13c, hb + 0x20)
            s.sw(0x9872, c)
            if not (s.b(r + 8) & 4):
                if s.u(0xdeda) == 0 and s.u(0x365e) != 0:
                    s.log.append(('9770',)); s.map_plot(s.u(r + 2), s.u(r + 4), 0, 1); s.log.append(('973c',))
                    if s.u(0xdfdc) == 0x14: s.log.append(('94a5',))
                s.sb(r + 8, s.b(r + 8) | 4)
                s.site_label(site); s.strcat(0x9876, 0x3dda); s.msg_post(0x9876)
        if i16(sg + sig) > 100:
            s.det_radar_warning()
            inc = ((s.i(0x3656) + s.i(0x3654) + 1) << 4) >> (1 if s.b(0x9b04) & 0x10 else 0)
            lv = i16(s.u(r + 10) + inc)
            if lv > 0xff: lv = 0xff
            s.sw(r + 10, lv)
            if not (s.b(r + 9) & 1) and s.u(0x39b2) == 0 and lv > 0x7f: s.det_alert_network()
            lv = s.i(r + 10)
            if s.i(0x3654) >= s.i(0xb054) and lv > 0xc0 and t != 0x15 and s.i(0x98f2) > 0x500:
                s.sw(0xded6, s.u(0xded6) + 1)
                if not (s.b(r + 9) & 0x10) and lv >= 0xfa:
                    s.sb(r + 9, s.b(r + 9) | 0x10); s.sw(0xded2, s.u(0xded2) + 1)
                    s.site_label(site); s.strcat(0x9876, 0x3de4); s.msg_post(0x9876)
                    s.snd_cue(8, 1); s.log_event(7, site)
                else:
                    m = PROJ + 0x1c * (site & 7); W = WPN + 0x12 * t
                    if s.u(m + 0xe) == 0 and s.i(W + 8) > i16(D):
                        s.sw(m, s.u(r + 2) + 8); s.sw(m + 2, s.u(r + 4)); s.sw(m + 4, 0); s.sw(m + 6, 1)
                        s.sw(m + 8, brg); s.sw(m + 0xa, 0x4000)
                        s.sw(m + 0xe, int((s.i(W + 8) * s.i(0x3660) << 4) / s.i(m + 6)))
                        s.sw(m + 0x10, t); s.sw(m + 0x16, site)
                        s.site_label(site); s.strcat(0x9876, 0x3df0); s.strcat(0x9876, W); s.msg_post(0x9876)
                        s.cadd(0x2c, 1)
            s.sb(r + 8, s.b(r + 8) | 0x10)
        else:
            s.sb(r + 8, s.b(r + 8) & ~0x10)
            lv = i16(s.u(r + 10) - 0x10)
            s.sw(r + 10, 0 if lv < 0 else lv)

    # --- enemy aircraft ------------------------------------------------------------------------------------
    def ai_unit_radar(s, u):                                          # 62d0
        U = UNIT + 0x24 * u; A = AC + 32 * s.i(0x94f0); rt = s.i(A + 0x18)
        sig, brg, D = s.det_radar_signal(s.u(U + 2), s.u(U + 4), s.u(U + 6), rt)
        if brg is None: brg, D = s.uninit62           # no radar: 5460 leaves the caller's locals unwritten (stack garbage)
        s.sw(0x3d6c, 0x2a)
        sg = s.i(0x3d5c)
        R = 0x2E80 + 14 * rt
        if sig > 0:
            s.sw(0xc070, i16(int(i16(sig + sg) * D / 100)))
            if not (s.b(R + 12) & 2):
                hh = sx8(s.u(U + 0x10) >> 8)
                s.sw(0xc13a, hh - 0x30); s.sw(0xc13c, hh + 0x30)
                if sx8(u16(abs16(brg - s.u(U + 0x10))) >> 8) > 0x30: sig = 0
            if s.b(U + 0x18) & 8:
                hb = sx8(brg >> 8)
                s.sw(0xc13a, hb - 0x20); s.sw(0xc13c, hb + 0x20)
            c = 0x0f if i16(sig + sg) > 100 else 0x0b
            s.sw(0x3d6c, c)
            s.threat_bar(min(sig, 99), c, 6, 9)
            s.sw(0x3d6a, s.u(0x3660)); s.sw(0xc07e, -1 - u); s.sw(0x9872, c)
            s.sw(0xe54c, s.b(R + 12) & 1)
        if i16(sig + sg) > 100:
            s.det_radar_warning()
            inc = ((s.i(0x3656) + s.i(0x3654) + 2) << 4) >> (1 if s.b(0x9b04) & 0x10 else 0)
            s.sw(U + 0x22, s.u(U + 0x22) + inc)
            if s.i(U + 0x22) > 0xc0:
                s.sw(0xded6, s.u(0xded6) + 1); s.sb(U + 0x19, s.b(U + 0x19) | 0x40); s.det_alert_network()
                if not (s.b(U + 0x19) & 8) and (8 >> s.i(0x3658)) > i16(D):
                    s.sb(U + 0x19, s.b(U + 0x19) | 8); s.sw(0xdece, s.u(0xdece) + 1)
                    s.strcpy(0x9876, 0x3e17); s.strcat(0x9876, A); s.msg_post(0x9876)
                    s.snd_cue(7, 1); s.log_event(6, s.u(0x94f0))
                m = PROJ + 0x1c * (u & 7); w = s.i(U + 0x1e); W = WPN + 0x12 * w
                if (2 * s.i(0x3654) >= s.i(0xb054) and s.u(m + 0xe) == 0 and abs16(brg - s.u(U + 0x10)) < 0x1800
                        and s.i(W + 8) > i16(D) and (4 - s.i(0x3654)) * 2 < i16(D) and w != 0):
                    s.sw(m, s.u(U + 2)); s.sw(m + 2, s.u(U + 4)); s.sw(m + 4, s.u(U + 6) - 0x19)
                    s.sw(m + 6, s.i(W + 10) >> 6)
                    s.sw(m + 8, s.u(U + 0x10)); s.sw(m + 0xa, s.u(U + 0x12)); s.sw(m + 0xc, s.u(U + 0x14))
                    s.sw(m + 0xe, int((s.i(W + 8) * s.i(0x3660) << 5) / s.i(m + 6)))
                    s.sw(m + 0x10, w); s.sw(m + 0x16, -u)
                    s.strcpy(0x9876, W); s.strcat(0x9876, 0x3e24); s.strcat(0x9876, A); s.msg_post(0x9876)
                    s.cadd(0x2c, 1)
            if not (s.b(U + 0x18) & 8): s.snd_cue(7, 1)
            s.sb(U + 0x18, s.b(U + 0x18) | 8)
        else:
            s.sb(U + 0x18, s.b(U + 0x18) & ~8)
            s.sw(U + 0x22, s.u(U + 0x22) - 0x20)
        s.sw(U + 0x22, clamp_w(s.i(U + 0x22), 0, 0xff))

    def ai_unit_takeoff(s, u, site):                                  # 65d4
        U = UNIT + 0x24 * u; T = s.i(U + 0x16); A = AC + 32 * T; r = SITE + 16 * site; d = s.i(0xb16e)
        s.sw(U + 0x10, 0 if d == 1 else 0x8000)
        if s.b(r + 9) & 2:
            s.sw(U + 2, s.u(r + 2) + 3 * d); s.sw(U + 4, -(12 * d - s.u(r + 4))); s.sw(U + 6, 0x8c); s.sw(U + 0x1a, 0x64)
            s.sw(U + 0x10, s.u(U + 0x10) - 0x400)
        else:
            s.sw(U + 2, s.u(r + 2)); s.sw(U + 4, s.u(r + 4) + 30 * d); s.sw(U + 6, 0x0c); s.sw(U + 0x1a, 0x0a)
        s.sl(U + 8, s.u(U + 2) << 5); s.sl(U + 0xc, s.u(U + 4) << 5)
        s.sw(U + 0x12, 0); s.sw(U + 0x14, 0)
        s.sw(U + 0x18, s.u(U + 0x18) | 0x403); s.sw(U, site)
        s.sw(U + 0x1c, int((s.i(A + 0x14) * s.i(0x3660) << 11) / s.i(A + 0x12)))
        if s.i(0x3d6e) == -1: s.sb(U + 0x19, s.b(U + 0x19) & ~1)
        s.site_label(site); s.strcat(0x9876, 0x3e2f); s.strcat(0x9876, A); s.strcat(0x9876, 0x3e33)
        if u < s.i(0xded0) - 4: s.msg_post(0x9876)

    def ai_update_units(s):                                           # 5730
        st = s.u(0x3d60); rate = s.rate()
        if not (st & 1) and s.i(0x3d68) == -1: s.sw(0x39da + 4 * (st & 0x1e), 0)
        s.sw(TRACER + 12 * i16(((st & 0xc) >> 2) + s.u(0xb16c)), 0)
        s.sw(0xb054, s.u(0x94f2)); s.sw(0x94f2, 0)
        sk = s.i(0x3654)
        u = 0
        while u < s.i(0xded0):
            U = UNIT + 0x24 * u
            fl = s.u(U + 0x18)
            if not (fl & 1): u += 1; continue
            s.sw(0x94f0, s.u(U + 0x16)); A = AC + 32 * s.i(0x94f0)
            if not (fl & 2) or s.u(U + 0x1a) == 0:
                s._scramble(u, U, A)
                u += 1; continue
            # ---- target selection ----
            if fl & 4:
                mode = 2
                si = SITE + 16 * s.i(U)
                tx = s.u(si + 2)
                if s.b(U + 0x19) & 2:
                    ty = s.u(si + 4)
                    dxu = i16(-(tx - s.u(U + 2)))
                    tx = i16(tx - i16(dxu << 1))
                    tz = 0x0c if (u16(abs16(dxu) + s.u(si + 8)) & 0x200) == 0 else 0x8c
                else:
                    ty = i16(s.i(0xb16e) * 0x280 + s.u(si + 4))
                    tz = i16(dist2(i16(s.u(U + 2) - tx), i16(s.u(U + 4) - ty)) + 0x7d0)
            elif s.u(0x3d62) == 0 or ((fl & 0x140) and s.i(0x3d62) <= s.i(0x9f64)):
                mode = 3
                L = s.i(0x3d6e)
                if (s.b(U + 0x19) & 1) and L != -1:
                    LU = UNIT + 0x24 * L; k = u & 7
                    a = u16((k << 11) + s.u(LU + 0x10) - 0x1800)
                    tx = i16(sin_mul(a, s.i(LU + 0x1a)) + s.u(LU + 2))
                    ty = i16(-(cos_mul(a, s.i(LU + 0x1a)) - s.u(LU + 4)))
                    tz = i16((k << 6) + s.u(LU + 6))
                else:
                    if ((((u << 3) + s.u(0x98e2)) & 0xff) & 0xbf) == 0 and not (s.b(U + 0x18) & 0x40):
                        best = 0x7fff
                        bp = heading(i16(s.u(0xc0a4) - s.u(U + 2)), i16(s.u(U + 4) - s.u(0xc0b2)))
                        for n in range(8):
                            c = s.rand_n(s.u(0xc0b4)) + 1
                            sr = SITE + 16 * c
                            if s.b(sr + 9) & 4: continue
                            d = i16(-i16(heading(i16(s.u(sr + 2) - s.u(U + 2)), i16(s.u(U + 4) - s.u(sr + 4))) - bp))
                            if abs16(d) >= best: continue
                            best = abs16(d); s.sw(U, c)
                            if i16((4 - sk) << 12) > best: break
                        dd = dist2(i16(s.u(0xc0a4) - s.u(U + 2)), i16(s.u(0xc0b2) - s.u(U + 4)))
                        if (dd & 0xffc0) > 0x5780 and u != 0:
                            s.sw(U + 0x18, s.u(U + 0x18) & 0x1c1); s.sw(U + 0x1c, 0)
                    si = SITE + 16 * s.i(U)
                    tx = s.u(si + 2); ty = s.u(si + 4)
                    tz = clamp_w(i16(s.u(0x2df4) + 1000), 5000, 20000)
            else:
                mode = 1
                tx = s.u(0xb2a0); ty = s.u(0xb75a); tz = s.u(0xb76e)
                if s.u(0x39b2) != 0:
                    tx = s.u(0x39aa); ty = s.u(0x39ac); tz = clamp_w(s.u(0x2df4), 1000, 30000)
            s._steer(u, U, A, mode, tx, ty, tz)
            u += 1

    def _steer(s, u, U, A, mode, tx, ty, tz):
        st = s.u(0x3d60); rate = s.rate(); sk = s.i(0x3654)
        if mode == 3 and (s.b(U + 0x18) & 8):
            tx = s.u(0xc0a4); ty = s.u(0xc0b2); tz = s.u(U + 6)
        dx = i16(tx - s.u(U + 2)); dy = i16(ty - s.u(U + 4))
        brg = heading(dx, i16(-dy))
        D = dist2(dx, dy)
        pt = clamp_w(heading(i16(tz - s.u(U + 6)) >> 5, D), i16(0xe000), 0x1000)
        if mode == 1 and D < 0x400:
            s.sw(0x94f2, s.u(0x94f2) + 1)
            if (D < 0x200 and not (st & 3) and s.i(0x3d62) > s.i(0x9f64)
                    and abs16(s.u(U + 0x10) - brg) < 0x400 and abs16(s.u(U + 0x12) - pt) < 0x400):
                k = i16(((st & 0xc) >> 2) + s.u(0xb16c)); T = TRACER + 12 * k
                sp = tdiv(0xe0, rate)
                s.sw(T + 10, i16(sin_mul(u16(-s.i(U + 0x12)), sp) << 5))
                h = cos_mul(s.u(U + 0x12), sp)
                s.sw(T + 6, sin_mul(s.u(U + 0x10), h))
                s.sw(T + 8, -cos_mul(s.u(U + 0x10), h))
                s.sw(T, s.u(U + 2)); s.sw(T + 2, s.u(U + 4)); s.sw(T + 4, s.u(U + 6))
            row = clamp_w((u & 3) + sk, 0, 2)
            if u == 0: row = 1
            h = s.u(U + 0x10)
            if abs16(s.u(U + 0x14)) < 0x4000: h = u16(h + (s.i(U + 0x14) >> 2))
            b = (i16(brg - h) >> 13) & 7
            h2 = s.u(0x2dee)
            if abs16(s.u(0x2df2)) < 0x4000: h2 = u16(h2 + (s.i(0x2df2) >> 1))
            a = ((i16(s.u(U + 0x10) - h2) >> 13) + 4) & 7
            cmd = s.i(0x3a8c + 2 * ((row * 8 + a) * 8 + b))
            bt = i16(cmd << 12)
            hi3d61 = s.b(0x3d61) & 8
            if cmd == 0x100:
                pt = 0x6000; bt = 0x4000 if hi3d61 else i16(0xc000)
            if cmd == 0x200:
                pt = i16(0xa000); bt = i16(0xc000) if hi3d61 else 0x4000
            if pt == i16(0xa000):
                p = s.i(U + 0x12)
                if i16(3000 - (p >> 3)) > s.i(U + 6): pt = i16(p + 0x1000)
            if abs16(s.u(U + 0x14)) > 0x4000: bt = 0; pt = 0
        else:
            bt = i16(clamp_w(i16(brg - s.u(U + 0x10)), i16(0xe000), 0x2000) << 1)
            if mode == 1 and 2 * sk < s.i(0xb054): bt = 0x3000
        if mode == 1 and (s.b(SITE + 16 * s.i(0xdfe0) + 9) & 4) and s.i(0x98f2) < 0x780: bt = 0x3000
        ag = s.i(A + 0x16)
        lim = i16(ag << 12)
        bt = clamp_w(bt, i16(-lim), lim)
        lim2 = i16((ag & 0xff) << 8)
        rb = clamp_w(i16(bt - s.u(U + 0x14)), i16(-lim2), lim2)
        if s.b(U + 0x19) & 4:
            if s.i(U + 0x1a) < 0x96: s.sw(U + 0x12, 0)
            else: s.sb(U + 0x13, s.b(U + 0x13) + 1)
            rb = 0
            if s.i(U + 0x1a) < s.i(A + 0x12): s.sw(U + 0x1a, s.u(U + 0x1a) + tdiv(0x28, s.rate()))
            elif s.i(U + 6) > 0x12c: s.sb(U + 0x19, s.b(U + 0x19) & ~4)
        if s.b(U + 0x18) & 0x30: rb = 0x400
        if ((u ^ st) & 3) == 0:
            s.log.append(('77d9', s.u(U + 2), s.u(U + 4), s.u(U + 6)))
            s.sb(0xc686, s.probe(s.u(U + 2), s.u(U + 4), s.u(U + 6)))
            if s.b(0xc686): s.sb(U + 0x19, s.b(U + 0x19) | 0x20)
            else: s.sb(U + 0x19, s.b(U + 0x19) & ~0x20)
        if s.b(U + 0x19) & 0x20: pt = 0x3000
        s.sw(U + 0x14, s.u(U + 0x14) + tdiv(i16(rb << 2), rate))
        s.sw(U + 0x10, s.u(U + 0x10) + tdiv(s.i(U + 0x14) >> 3, rate))
        dp = i16(pt - s.u(U + 0x12))
        if s.b(U + 0x18) & 0x20:
            dp = i16(0xfe00)
            if not (st & 1):
                j = (st & 0x1e) >> 1; P = 0x39da + 8 * j
                s.sw(P, s.u(U + 2)); s.sw(P + 2, s.u(U + 4)); s.sw(P + 4, s.u(U + 6))
                s.sw(0x3a5a, j); s.sw(P + 6, 0)
                for n in range(16):
                    if n != j and s.u(0x39da + 8 * n) != 0: s.sw(0x39da + 8 * n + 6, s.u(0x39da + 8 * n + 6) + 1)
        if s.i(U + 0x12) < 0:
            if s.i(U + 6) < i16(200 - sin_mul(s.u(U + 0x12), 2000)) and not (s.u(U + 0x18) & 0x220): dp = 0x400
        dp = clamp_w(dp, i16(0xfc00), 0x400)
        s.sw(U + 0x12, s.u(U + 0x12) + tdiv(i16(dp << 2), rate))
        if abs16(s.u(U + 0x12)) > 0x4000:
            s.sb(U + 0x11, s.b(U + 0x11) + 0x80); s.sb(U + 0x15, s.b(U + 0x15) + 0x80)
            s.sw(U + 0x12, 0x8000 - s.u(U + 0x12))
        s.sb(U + 0x18, s.b(U + 0x18) & ~0x10)
        v = i16((s.i(U + 0x1a) * u16(0x8000 - s.u(U + 0x12))) >> 15)
        w = i16(v - (abs16(sin_mul(s.u(U + 0x14), v)) >> 1))
        if mode == 1 and sk >= s.i(0xb054): w = i16(w + (w >> 1))
        w = tdiv(i16(w << 2), rate) >> 2
        h = cos_mul(s.u(U + 0x12), w)
        s.sl(U + 8, s.l(U + 8) + sin_mul(s.u(U + 0x10), h))
        s.sl(U + 0xc, s.l(U + 0xc) - cos_mul(s.u(U + 0x10), h))
        s.sw(U + 6, s.u(U + 6) + sin_mul(s.u(U + 0x12), w))
        x32 = s.l(U + 8); x32 = x32 - (1 << 32) if x32 & 0x80000000 else x32
        y32 = s.l(U + 0xc); y32 = y32 - (1 << 32) if y32 & 0x80000000 else y32
        s.sw(U + 2, x32 >> 5); s.sw(U + 4, y32 >> 5)
        if s.i(U + 6) > 30000: s.sw(U + 0x12, 0)
        if s.i(U + 6) < 0:
            s.sw(U + 0x18, s.u(U + 0x18) & (0x1c1 if u >= 1 else 0))
            s.sw(0xc0a0, s.u(U + 2)); s.sw(0xc0aa, s.u(U + 4)); s.sw(0xc0b0, s.u(U + 6)); s.sw(0x9d0c, 0xfff0)
            if s.i(0x3d64) == u: s.sw(0x3d64, 0xffff)
        if D < 0x10 and mode == 2:
            s.site_label(s.u(U)); s.strcat(0x9876, 0x3e00); s.strcat(0x9876, A)
            if s.b(U + 0x19) & 2: s.sb(U + 0x19, s.b(U + 0x19) | 0x10); s.strcat(0x9876, 0x3e04)
            else: s.sb(U + 0x19, s.b(U + 0x19) | 2); s.strcat(0x9876, 0x3e0d)
            if s.i(0xded0) - 4 > u: s.msg_post(0x9876)
        if s.b(U + 0x19) & 0x10:
            s.sw(U + 0x12, 0); s.sw(U + 0x14, 0)
            s.sw(U + 0x10, 0 if s.i(0xb16e) == 1 else 0x8000)
            s.sw(U + 6, 0x8c if (s.b(SITE + 16 * s.i(0xdfe0) + 9) & 2) else 0x0c)
            if s.i(U + 0x1a) > 0: s.sw(U + 0x1a, s.u(U + 0x1a) - tdiv(0x3c, s.rate()))
            else:
                s.sw(U + 0x18, s.u(U + 0x18) & 0x1c1)
                if u == 0 and s.i(0xe2d8) >= 5: s.sw(U + 0x18, 0)
            if not (s.i(0xded0) - 4 > u) and s.i(U + 0x1a) < 0x64:
                s.sw(U + 0x18, (s.u(U + 0x18) & 0x1c1) | 0x406)
        s.sw(U + 0x1c, s.u(U + 0x1c) - 1)
        if s.u(U + 0x1c) == 0:
            s.sb(U + 0x18, s.b(U + 0x18) | 4)
            best = 0x7fff
            for i in range(3, s.i(0xe540)):
                sr = SITE + 16 * i
                if (s.u(sr + 8) & 0x101) == 1:
                    d = dist2(i16(s.u(U + 2) - s.u(sr + 2)), i16(s.u(U + 4) - s.u(sr + 4)))
                    if d < best: s.sw(U, i); best = d
        if s.b(U + 0x18) & 2:
            c = 0x29
            if u == 0 and s.i(0xe2d8) >= 5 and (st & 1): c = 0
            if s.u(0x98de) != 0 or u == 0: s.map_plot(s.u(U + 2), s.u(U + 4), c, 1)
            if (i16(st) % (rate << 4) if i16(st) >= 0 else -((-i16(st)) % (rate << 4))) == i16((((u & 8) >> 3) + 2 * (u & 7)) * rate):
                if not (s.b(U + 0x18) & 0x20): s.log.append(('62d0', u))

    def _scramble(s, u, U, A):
        cl = (s.i(0x3656) + 4) & 0x1f
        if (((s.i(0x98e2) >> cl) ^ u) & 7) != 0: return
        if not (s.i(0xded0) - 4 > u) or u == 0: return
        r = s.rand_n(s.u(0xe540))
        if not (s.i(0x3d62) > s.i(0x9f64) or (s.b(U + 0x18) & 0x80)): return
        sr = SITE + 16 * r
        if (s.u(sr + 8) & 0x181) != 1: return
        if s.u(U + 0x16) != s.u(sr + 10): return
        D = dist2(i16(s.u(0xb2a0) - s.u(sr + 2)), i16(s.u(0xb75a) - s.u(sr + 4))) >> 6
        if s.u(A + 0x14) > D: s.log.append(('65d4', u, r))

    # --- countermeasures, gun, seeker, blast ----------------------------------------------------------------
    def wpn_release_countermeasure(s, a):                             # 4512
        o = 0x3636 + 2 * a
        old = s.i(o); s.sw(o, old - 1)
        if old <= 0:
            s.sw(o, 0); s.msg_post(0x3da7); return
        slot = -1
        if a == 3:
            if s.u(DECOY + 8) == 0 and not (s.b(0x3632) & 0x40): slot = 0
        else:
            for k in range(1, 4):
                if s.u(DECOY + 12 * k + 8) == 0: slot = k
        if slot != -1:
            R = DECOY + 12 * slot
            s.sw(R, s.u(0xc0a4)); s.sw(R + 2, s.u(0xc0b2)); s.sw(R + 6, a)
            s.sw(R + 8, (9 - 2 * s.i(0x3654)) * s.i(0x3660))
            if a == 1: s.strcpy(0x9876, 0x3db8); s.sw(0x3d90, 3 * s.i(0x3660))
            elif a == 2: s.strcpy(0x9876, 0x3dbe); s.sw(0x3d8e, 3 * s.i(0x3660))
            elif a == 3: s.strcpy(0x9876, 0x3dc4); s.sw(R + 8, s.u(R + 8) << 3)
            s.strcat(0x9876, 0x3dca); s.msg_post(0x9876)
        s.log.append(('44c4', a)); s.snd_cue(0xf, 2)

    def wpn_seeker_check(s, slot, tx, ty, tz, cls):                    # 711f
        p = PROJ + 0x1c * slot
        dx = i16(tx - s.u(p)); dy = i16(ty - s.u(p + 2)); dist = dist2(dx, dy)
        brg = heading(dx, i16(-dy)); s.sw(0x94f6, brg)
        a = abs16(brg - s.u(p + 8))
        if a > 0x1000 and cls != 3:
            if a > 0x6000 and slot < 8 and s.i(p + 6) < dist: s.sw(p + 0xe, 0)
            return 0
        if cls == 0 and abs16(s.u(p + 8) - s.u(0x2dee)) > 0x2000: return 0
        s.sw(0x94f4, dist); return 1

    def dmg_player_blast(s):                                          # 74f6
        if s.b(0x9b05) & 0x10: return
        for n in range(s.i(0x3654) + 1):
            bb = s.rand_n(8); s.sw(0x3632, s.u(0x3632) | (1 << bb)); s.sw(0xc5c8, s.u(0xc5c8) + 1)
        s.log.append(('829d', 0x16)); s.sw(0x98ee, 1); s.snd_cue(4, 2)

    def wpn_cm_tick(s):                                               # 461c: only the FIRST busy slot ticks (returns after it)
        for k in range(4):
            R = DECOY + 12 * k
            if s.u(R + 8) == 0: continue
            s.sw(R + 8, s.u(R + 8) - 1)
            if s.u(R + 6) == 3: s.lamp(7, 0x29 if s.u(R + 8) >= 1 else 0)
            return

    def input_button(s, n):                                           # cb72 (comm[72h] bit0 = joystick: slot 93, not modelled)
        return s.u(0x43be + 2 * n)

    def wpn_gun_update(s):                                            # 465b
        n = s.i(0xb16c)
        if n == 0: return
        for k in range(n + 4):
            T = TRACER + 12 * k
            if s.u(T) != 0:
                s.sw(T, s.u(T) + s.u(T + 6)); s.sw(T + 2, s.u(T + 2) + s.u(T + 8)); s.sw(T + 4, s.u(T + 4) + s.u(T + 10))
        st = s.i(0x3d60)
        if not (st & 1) or s.pw(0x42) != 0: return
        q = st >> 1
        k = q - n * tdiv(q, n)                                        # idiv remainder (sign of the dividend)
        T = TRACER + 12 * k
        if s.input_button(0) != 0 and s.i(0x3650) > 0 and s.u(0xc06e) == 0:
            s.sw(0x3650, clamp_w(i16(tdiv(-40, s.rate()) + s.i(0x3650)), 0, 1000))
            s.snd_cue(6, 2)
            sp = tdiv(0x70, s.rate())
            s.sw(T + 10, i16(sin_mul(s.u(0x2df0), sp) << 5))
            h = cos_mul(s.u(0x2df0), sp)
            s.sw(T + 6, sin_mul(s.u(0x2dee), h)); s.sw(T + 8, -cos_mul(s.u(0x2dee), h))
            s.sw(T, s.u(T + 6) + s.u(0xc0a4)); s.sw(T + 2, s.u(T + 8) + s.u(0xc0b2)); s.sw(T + 4, s.u(T + 10) + s.u(0x2df4) - 2)
            s.sw(0x98ea, 1)
        else:
            s.sw(T, 0); s.sw(0x98ea, 0)

    # --- weapons: projectile update and release -------------------------------------------------------------
    def wpn_suit(s, store, o):                                        # 8bab: lethality of store vs object class
        c = s.b(0xc604 + (s.b(SITE + 16 * o + 0xe) & 0x7f)) & 0xf
        return sx8(s.b(0x386a + 16 * store + c))

    def obj_cls_ship(s, o):                                           # bcac
        return 1 if (s.b(0xc604 + (s.b(SITE + 16 * o + 0xe) & 0x7f)) & 0xf) in (0xc, 0xd) else 0

    def obj_find_at(s, x, y):                                         # bad1 stub (test world): log, scripted result
        s.log.append(('bad1', x & 0xffff, y & 0xffff))
        return s.bad1(x, y)

    def fm_bomb_dive_angle(s):                                        # bbed
        num = i16(0x4000 - abs16(s.u(0x2df0))) << 12
        den = u16(s.u(0x2df4) + 0x1000)
        return u16(int(num / den) - 0x4000)

    def wpn_proj_update_all(s):                                       # 671a
        s.m[0xb290:0xb2a0] = s.m[0xb2a2:0xb2b2]
        s.lamp(0, 0); s.lamp(1, 0)
        s.lamp(8, 0x11 if (s.u(0xded6) != 0 and (s.b(0x3d60) & 1)) else 3)
        if s.u(0x39b2) != 0: ax, ay = s.u(0x39aa), s.u(0x39ac)
        else: ax, ay = s.u(0xc0a4), s.u(0xc0b2)
        sk = s.i(0x3654)
        for i in range(12):
            p = PROJ + 0x1c * i
            if s.u(p + 0xe) == 0: continue
            lock = 0; brg = 0
            typ = s.i(p + 0x10); Wp = WPN + 0x12 * typ; cls = s.i(Wp + 0xc)
            st = s.u(0x3d60); rate = s.rate()
            if i < 8:
                tz = s.u(0x2df4); s.tz = tz
                lock = s.wpn_seeker_check(i, ax, ay, tz, cls)
                dist = s.u(0x94f4); brg = s.u(0x94f6)
                cm = ((s.b(0x9b04) & 0x10) and cls > 0) or ((s.b(0x9b04) & 0x20) and cls <= 0) or \
                     (s.i(0x9f64) > s.i(0x3d62) and s.i(p + 0x10) != 0x21)
                if cm and (u16((sk << 8) + 0x180) < dist or cls == 1 or cls == -1):
                    brg = s.u(p + 8); lock = 0
                for j in range(1, 4):
                    D_ = DECOY + 12 * j; t = s.i(D_ + 6); cand = False
                    if t == 1 and cls <= 0: cand = True
                    elif t == 2 and (cls == 1 or cls == 2 or (cls == 3 and
                            abs16(abs16(sx8(u16(brg - s.u(0x2dee)) >> 8)) - 0x40) < i16(0x40 - sk * 12))):
                        cand = True
                    if cand:
                        r = s.wpn_seeker_check(i, s.u(D_), s.u(D_ + 2), s.u(0x2df4), cls)
                        if r != 0: brg = r; lock = 0
                if dist > 0x200:
                    aux = s.i(p + 0x16)
                    if aux > 2 and not (s.b(SITE + 16 * aux + 8) & 0x10): lock = 0
                    if aux <= 0 and not (s.b(UNIT + 0x24 * (-aux) + 0x18) & 8): lock = 0
                if (s.i(Wp + 10) >> 6) > s.i(p + 6) and (st & 1): s.sw(p + 6, s.u(p + 6) + 1)
            else:
                dist = 0x7fff; tz = s.tz; tgt = s.tgt
                if cls == 7:
                    for j in range(s.i(0xded0)):
                        U = UNIT + 0x24 * j
                        if not (s.b(U + 0x18) & 2) or s.u(U + 0x1a) == 0: continue
                        r = s.wpn_seeker_check(i, s.u(U + 2), s.u(U + 4), s.u(U + 6), cls)
                        if dist > s.u(0x94f4) and r != 0:
                            brg = s.u(0x94f6); dist = s.u(0x94f4); tgt = j; tz = s.u(U + 6); lock = 1
                            if dist < 0x180: s.sb(U + 0x18, s.b(U + 0x18) | 0x10)
                if (s.i(Wp + 10) >> 6) > s.i(p + 6) and (st & 1):
                    s.sw(p + 6, s.u(p + 6) + 1); brg = s.u(p + 8)
                if cls in (4, 5, 6) and s.i(p + 0x14) != -1:
                    t = s.i(p + 0x14)
                    if t == 0 and (s.u(p + 0x18) != s.u(0xb2a4) or s.u(p + 0x1a) != s.u(0xb2a6)):
                        s.obj_find_at(s.u(p + 0x18), s.u(p + 0x1a)); s.sw(0xb2aa, 0)
                    r = s.wpn_seeker_check(i, s.u(SITE + 16 * t + 2), s.u(SITE + 16 * t + 4), 0, cls)
                    if r: brg = s.u(0x94f6); dist = s.u(0x94f4); tgt = t; tz = 0; lock = 1
                if typ == 0x1c and s.i(0x3d66) != -1:
                    t = s.i(0x3d66)
                    r = s.wpn_seeker_check(i, s.u(SITE + 16 * t + 2), s.u(SITE + 16 * t + 4), 0, cls)
                    if r: brg = s.u(0x94f6); dist = s.u(0x94f4); tgt = t; tz = 0; lock = 1
                s.tz = tz; s.tgt = tgt
            # ---- common: warnings, steering ----
            if lock and i < 8 and abs16(s.u(0x94f6) - s.u(p + 8)) < 0x1000 and not (s.b(0x3632) & 1) and s.u(0x39b2) == 0:
                if cls <= 0 and (st & 2): s.lamp(1, 0x29)
                if cls != 0 and not (st & 2): s.lamp(0, 0x11)
                if not (st & 3) and u16(s.u(p + 6) << 4) > dist: s.snd_cue(9, 1)
            if brg != 0 and lock != 0:
                dh = i16(brg - s.u(p + 8))
                if i < 8: dh = clamp_w(dh, i16(-((sk + 1) << 8)), (sk + 1) << 8)
                tl = i16(s.i(Wp + 0xe) << 7)
                dh = clamp_w(dh, i16(-tl), tl)
                s.sw(p + 8, s.u(p + 8) + tdiv(i16(dh << 2), rate)); s.sw(p + 0xc, dh << 1)
                if i < 8 and dist < 0x400:
                    pa = heading(i16(tz - s.u(p + 4)) >> 4, abs16(dist))
                else:
                    lob = abs16(dist) >> 3 if abs16(dist) > 0x140 else 0
                    pa = heading(i16((i16(tz - s.u(p + 4)) >> 5) + lob), abs16(dist))
                pe = i16(pa - s.u(p + 0xa))
                if i < 8: pe = clamp_w(pe, i16(u16(0xffff - sk) << 9), i16((sk + 1) << 9))
                pl = i16((s.u(Wp + 0xe) & 0xff) << 8)
                pe = clamp_w(pe, i16(-pl), pl)
                s.sw(p + 0xa, s.u(p + 0xa) + tdiv(i16(pe << 2), rate))
            elif s.i(p + 0xa) > 0 and typ != 0x1e:
                s.sw(p + 0xa, s.u(p + 0xa) - tdiv(0x800, rate))
            if typ == 0x1c and s.i(p + 0xa) > i16(0xf800): s.sw(p + 0xa, 0xf800)
            if typ == 0x1e or s.i(p + 4) == 1:
                s.sw(p + 0xa, s.u(p + 0xa) - tdiv(0x800, rate))
                if s.i(p + 0x16) > s.i(p + 0xa): s.sw(p + 0xa, s.u(p + 0x16))
            # ---- move ----
            h = tdiv(i16(cos_mul(s.u(p + 0xa), s.i(p + 6)) << 2), rate)
            s.sw(p, s.u(p) + sin_mul(s.u(p + 8), h)); s.sw(p + 2, s.u(p + 2) - cos_mul(s.u(p + 8), h))
            s.sw(p + 4, s.u(p + 4) + sin_mul(s.u(p + 0xa), tdiv(i16(s.u(p + 6) << 7), rate)))
            s.sw(p + 0xe, s.u(p + 0xe) - 1); s.sb(0xc686, 0)
            if ((i ^ st) & 3) == 0:
                s.log.append(('77d9', s.u(p), s.u(p + 2), s.u(p + 4)))
                s.sb(0xc686, s.probe(s.u(p), s.u(p + 2), s.u(p + 4)))
            # ---- impact ----
            if s.i(p + 4) < 0 or s.b(0xc686):
                s.sw(0xc0a0, s.u(p)); s.sw(0xc0aa, s.u(p + 2)); s.sw(0xc0b0, s.u(p + 4)); s.sw(0x9d0c, 0xfffa)
                s.sw(p + 0xe, 0); s.log.append(('81f6',))
                SN = 0x366c + 0x1a * s.i(p + 0x12)
                s.strcpy(0x9876, SN)
                if typ in (0x1e, 0x1d, 0x1c):                         # (the type == 0x26 test inside is unreachable)
                    s.snd_cue(5, 2); s.strcat(0x9876, 0x3e40)
                    o = s.obj_find_at(s.u(0xc0a0), s.u(0xc0aa))
                    if o != -1 and not (s.b(SITE + 16 * o + 8) & 0x80):
                        wo = s.u(0x9f10)
                        ox = i16(sl32(s.l(wo + 4)) >> 5)
                        oy = i16(0x8000 - u16(sl32(s.l(wo + 8)) >> 5))
                        miss = dist2(i16(s.u(0xc0a0) - ox), i16(s.u(0xc0aa) - oy))
                        if u16(tdiv(i16(s.wpn_suit(s.i(p + 0x12), o) << 6), sk + 2)) > miss:
                            s.log.append(('72a4', o)); s.strcat(0x9876, 0x3e49); s.strcat(0x9876, SN)
                            s.sw(0x9d0c, 0x10); s.sw(0xc0b0, 0)
                    s.msg_post(0x9876)
                    d = u16(dist2(i16(s.u(0xc0a4) - s.u(p)), i16(s.u(0xc0b2) - s.u(p + 2))) + (s.u(0x2df4) >> 5))
                    if u16(sk << 5) > d:
                        s.log.append(('74f6',)); s.strcpy(0x9876, 0x3e58)
                elif i >= 8:
                    s.strcat(0x9876, 0x3e82); s.msg_post(0x9876)
            # ---- proximity ----
            slant = u16((abs16(i16(tz - s.u(p + 4))) >> 5) + dist)
            if u16(tdiv(i16(s.u(p + 6) << 3), rate)) > slant and lock:
                if i < 8:
                    if s.i(p + 0xe) > 4 * rate:
                        s.sw(0xc0a0, s.u(p)); s.sw(0xc0aa, s.u(p + 2)); s.sw(0xc0b0, s.u(p + 4)); s.sw(0x9d0c, 0x10)
                        s.sw(p + 0xe, 2 * rate)
                        if s.u(0x39b2) == 0:
                            s.strcpy(0x9876, 0x3e91); s.strcat(0x9876, Wp); s.msg_post(0x9876)
                            s.log.append(('74f6',))
                            if not (s.b(0x9b05) & 0x10): s.log_event(5, typ)
                elif cls == 7:
                    U = UNIT + 0x24 * s.tgt
                    for o_, a_ in ((0, 0xc0a0), (2, 0xc0aa), (4, 0xc0b0)):
                        s.sw(p + o_, s.u(U + 2 + o_)); s.sw(a_, s.u(U + 2 + o_))
                    s.sw(0x9d0c, 0x10); s.sw(p + 0xe, 2 * rate)
                    s.log.append(('71b4', s.tgt & 0xffff))
                    s.strcat(0x9876, 0x3ea5); s.strcat(0x9876, Wp); s.msg_post(0x9876)
                else:
                    s.log.append(('81f6',))
                    t = s.tgt; S_ = SITE + 16 * t
                    s.sw(0xc0a0, s.u(S_ + 2)); s.sw(0xc0aa, s.u(S_ + 4)); s.sw(p + 0xe, 0); s.sw(0xc0b0, 0); s.sw(0x9d0c, 0x10)
                    r = s.rand_n(4)
                    if s.wpn_suit(s.i(p + 0x12), t) > r: s.log.append(('72a4', t & 0xffff))
                    else: s.strcpy(0x9876, 0x3e99)
                    s.sw(0x3d62, s.u(0xb286)); s.sw(0xb2a0, s.u(0xc0a0)); s.sw(0xb75a, s.u(0xc0aa)); s.sw(0xb76e, 0xbb8)
                    s.strcat(0x9876, 0x3ea5); s.strcat(0x9876, Wp); s.msg_post(0x9876)
            if i < 8 and s.u(p + 0xe) != 0 and (s.b(0x3d60) & 1):
                s.log.append(('8548', s.u(p), s.u(p + 2), 0x11, 0))   # 4th arg = 671a's saved SI (caller's SI, nonzero in the game)
        s.m[0xb2a2:0xb2b2] = s.m[0xb290:0xb2a0]

    def wpn_release(s):                                               # 753f
        if abs16(s.u(0x2df2)) > 0x3000: return
        b = s.i(0x364e); B = 0x363e + 4 * b
        if s.u(B + 2) == 0: return
        store = s.i(B); typ = s.i(0x3678 + 0x1a * store)
        if typ == 0 or typ == -1: return
        if not (s.b(0x9b04) & 4) and typ != -2:
            s.log.append(('snd', 0x20)); s.msg_post(0x3ecc); return
        if s.u(0x2df6) == 0 or ((s.b(SITE + 16 * s.i(0xdfe0) + 9) & 2) and s.u(0x2df6) == 0x80):
            s.log.append(('snd', 0x20)); s.msg_post(0x3ed7); return
        s.sw(B + 2, s.u(B + 2) - 1)
        if typ == -2:
            s.sw(0x3634, s.u(0x3634) + 0x76c); s.msg_post(0x3eed)
            if s.u(0xdfdc) == 0x14: s.log.append(('9208',))
            return
        s.log_event(4, b); s.d7bb()
        sl = -1
        for k in range(8, 12):
            if s.u(PROJ + 0x1c * k + 0xe) == 0: sl = k
        if sl != -1:
            p = PROJ + 0x1c * sl; Wp = WPN + 0x12 * typ
            s.sw(p, s.u(0xc0a4)); s.sw(p + 2, s.u(0xc0b2)); s.sw(p + 4, s.u(0x2df4) - 0x14); s.sw(p + 6, s.u(0xb052) >> 10)
            s.sw(p + 8, s.u(0x2dee)); s.sw(p + 0xa, s.u(0x2df0)); s.sw(p + 0xc, s.u(0x2df2))
            num = (s.i(Wp + 8) * s.i(0x3660)) << 5
            life = i16(int(num / ((s.i(Wp + 10) >> 6) + 1)) + 6)
            s.sw(p + 0xe, life if life > 6 else 0x3e7)
            s.sw(p + 0x10, typ); s.sw(p + 0x12, store); s.sw(p + 0x1a, 0xffff); s.sw(p + 0x18, 0xffff); s.sw(p + 0x14, 0xffff)
            if typ == 0x1b: s.sw(0x43b4, 0)
            if typ in (0x1e, 0x1d, 0x1c, 0x26): s.sw(p + 6, s.i(p + 6) >> 1)
            if typ == 0x1e: s.sw(p + 0x16, s.fm_bomb_dive_angle() - 0x400)
            t = s.i(0x3d66)
            if t >= 0:
                if s.u(0xe55c) != 0:
                    c = s.i(Wp + 0xc)
                    if c == 6 or (c == 4 and s.u(SITE + 16 * t + 6) != 0) or (c == 5 and s.obj_cls_ship(t)):
                        s.sw(p + 0x14, t); s.sw(p + 0x18, s.u(SITE + 16 * t + 2)); s.sw(p + 0x1a, s.u(SITE + 16 * t + 4))
                else:
                    s.sw(p + 0x14, 0xffff)
            if typ == 0x1d: s.sw(p + 0xa, 0xc000); s.sw(p + 6, 1)
            s.sw(0xb04c, sl)
            s.strcpy(0x9876, 0x366c + 0x1a * store); s.strcat(0x9876, 0x3efb); s.msg_post(0x9876)
            s.snd_cue(0x10 if s.u(Wp + 8) == 0 else 0x0d, 2)
        if s.u(0xdfdc) == 0x15: s.log.append(('829d', 0x15))


def sl32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v
