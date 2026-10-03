"""Reference model of the F-117A AdLib sound driver (ASOUND.117, state 0 = plain AdLib) as decoded in
port/spec/sound.md.  Reads the driver image from Game/ASOUND.117 (data segment = image bytes 0..1B5Fh,
code-segment variables = image 1B60h+), reproduces snd_init/snd_cmd/snd_poll/snd_noise and logs the OPL writes.
  asnd.py CMD [polls]           -> prints write count
  asnd.py verify [polls]        -> compares against the Unicorn run of the real driver (tools/sound/drvemu.py)
"""
import struct, sys

CS = 0x1B60          # cs:0000 == image offset CS
VOICES = [0x190A + 0x1E * i for i in range(7)]


def u16(m, o): return m[o] | m[o + 1] << 8
def s8(x): return x - 256 if x & 0x80 else x


class Snd:
    def __init__(self, path='Game/ASOUND.117'):
        d = open(path, 'rb').read()
        self.m = bytearray(d[struct.unpack_from('<H', d, 8)[0] * 16:])
        self.log = []          # (reg, value)
        self.busy_init = bytes(self.m)

    # ---- memory helpers -------------------------------------------------------------------
    def b(self, o): return self.m[o]
    def w(self, o): return self.m[o] | self.m[o + 1] << 8
    def sb(self, o, v): self.m[o] = v & 0xFF
    def sw(self, o, v): self.m[o] = v & 0xFF; self.m[o + 1] = (v >> 8) & 0xFF

    # ---- OPL ------------------------------------------------------------------------------
    def opl(self, reg, val):                      # 36b: shadow + port write (state < 2)
        reg &= 0xFF; val &= 0xFF
        self.m[0x1A06 + reg] = val
        self.log.append((reg, val))

    def sh(self, reg): return self.m[0x1A06 + reg]

    def freq(self, ch, bx):                       # 30b
        self.opl(0xA0 + ch, bx & 0xFF)
        self.opl(0xB0 + ch, ((bx >> 8) & 0xFF) | 0x20)

    def rnd(self):                                # 3c4
        v = (0x9248 + self.w(0x17E4)) & 0xFFFF
        v = ((v >> 3) | (v << 13)) & 0xFFFF       # three ror 1
        self.sw(0x17E4, v)
        return v

    # ---- init -----------------------------------------------------------------------------
    def init(self):
        self.sw(0x17F2, 0)
        self.reset()

    def reset(self):                              # 18f
        old = self.w(0x17EC); self.sw(0x17EC, 0xFFFF)
        for v in VOICES:
            for k in (0, 1, 2, 3):
                self.sb(v + k, 0)
        for a in (0x19DC, 0x19FC):
            self.sb(a, 0)
        for a in (0x19FE, 0x1A00, 0x1902, 0x1904, 0x1906, 0x1908):
            self.sw(a, 0)
        self.hwinit()
        self.sw(0x17EC, old)

    def hwinit(self):                             # 210 (starts with MOV BL,3Fh)
        for r in range(0x5F, 0x3F, -1):
            self.opl(r, 0x3F)
        for r in range(0xFF, 0x5F, -1):
            self.opl(r, 0)
        for r in range(0x3F, 0, -1):
            self.opl(r, 0)
        self.opl(1, 0x20)

    # ---- commands -------------------------------------------------------------------------
    def spawn(self, vi, cx, ax=0):                # 5e7 family
        old = self.w(0x17EC); self.sw(0x17EC, 0)
        v = VOICES[vi]
        m = self
        m.sb(v + 1, 0); m.sb(v + 0xA, 0xFF); m.sb(v + 2, 0); m.sb(v + 3, 0)
        m.sb(v + 0x1C, 0); m.sb(v + 0x1D, 0); m.sb(v + 0xD, 0x40)
        for k in (0xE, 0x10, 0x12, 0x14):
            m.sw(v + k, cx)
        m.sw(v + 0x16, 0); m.sw(v + 0x18, 0); m.sb(v + 7, 0)
        m.sw(v + 0x1A, ax); m.sb(v, 1)
        self.sw(0x17EC, old)

    def cmd(self, n, arg=0):
        if n > 0x2D:
            return
        self.cmd_tab[n](self, n, arg)

    def a5a5(self):
        if self.w(0x17DE) != 1:
            self.sw(0x17DE, 1); self.sw(0x17DC, 1)

    def c_reset(self, n, a): self.reset()
    def c_stopsoft(self, n, a):
        for v in VOICES:
            self.sw(v + 0x12, 0xE9A)
    def c_query(self, n, a):
        r = 0
        for v in VOICES: r |= self.b(v)
        self.ret = r
    def c_seq(lst, extra=None):
        def f(self, n, a):
            for vi, tr in lst:
                self.spawn(vi, tr)
            if extra: extra(self)
        return f
    def c_rnd(vi, tr, off, base):
        def f(self, n, a):
            r = self.rnd() & 3
            self.sb(tr + off, r + base)
            self.spawn(vi, tr, ax=r + base)
        return f
    def c_tab(vi, tbl, shift):
        def f(self, n, a):
            bx = (n * 2) >> shift & 6
            self.spawn(vi, u16(self.m, CS + tbl + bx))
        return f
    def c9(self, n, a):
        if self.w(0x1972) == 0xF3C and self.b(0x1964) != 0:
            return
        self.spawn(3, 0xF3C)
    def c11(self, n, a):
        if self.w(0x17F4) == 0:
            self.set_engine(1)
        self.spawn(3, 0xFA2)
    def set_engine(self, bx):                     # 3d8
        bx = min(bx, 0x301) if bx < 0x8000 else bx
        self.sw(0x17F4, bx)
        if bx == 0:
            self.sw(0x17E6, 0); return
        self.sw(0x17E8, (bx & 0xFFFE) + 0x883)
    def c23(self, n, a): self.set_engine(a)
    def c24(self, n, a): self.sb(0x17F6, 0xFF)
    def c25(self, n, a): self.sb(0x17F6, 0); self.sw(0x17E6, 0xFFFF)
    def c21(self, n, a):                          # mute carriers, park effect units
        self.sb(0x17F0, self.b(0x19DC)); self.sb(0x19DC, 0)
        self.sb(0x17F1, self.b(0x19FC)); self.sb(0x19FC, 0)
        self.sw(0x17EC, 0xFFFF)
        for r in (0x43, 0x44, 0x45, 0x4B, 0x4C, 0x4D, 0x53):
            self.opl(r, self.sh(r) | 0x3F)
    def c22(self, n, a):
        for r, vi in ((0x43, 0), (0x44, 1), (0x45, 2), (0x4B, 3), (0x4C, 4), (0x4D, 5), (0x53, 6)):
            self.opl(r, (self.sh(r) & 0xC0) | (self.b(VOICES[vi] + 6) ^ 0x3F))
        self.sb(0x19DC, self.b(0x17F0)); self.sb(0x19FC, self.b(0x17F1))
        if self.b(0x17F0) | self.b(0x17F1):
            self.a5a5()
        self.sw(0x17EC, 0)
    def c40(self, n, a): self.sw(0x17E2, a)
    def c_speech(self, n, a): pass               # 41..45: not modelled here (spec §10)

    # ---- voice engine ---------------------------------------------------------------------
    def update_all(self):                         # 1882
        self.sb(0x1A02, 0)
        for v in VOICES:
            self.cur = v
            self.voice_tick()

    def keyoff(self):                             # e5e
        ch = self.b(0x1A02)
        self.opl(0xB0 + ch, self.sh(0xB0 + ch) & 0xDF)

    def vol(self):                                # c2e (state 0)
        ch = self.b(0x1A02)
        op = self.b(0x18A8 + self.b(0x1896 + 2 * ch + 1))
        reg = 0x40 + op
        di = self.sh(reg) & 0xC0
        v = self.cur
        t = ((self.b(v + 6) + self.b(v + 0x1D)) & 0x3F)
        self.opl(reg, (0x3F - t) | di)

    def pitch(self):                              # d1c
        ch = self.b(0x1A02); v = self.cur
        n = self.b(v + 4)
        q, r = divmod(n, 12)
        tune = s8(self.b(v + 0x1C))
        f = (self.w(0x187A + 2 * r) + tune) & 0xFFFF
        self.opl(0xA0 + ch, f & 0xFF)
        hi = ((f >> 8) | (q << 2)) & 0xFF
        self.opl(0xB0 + ch, (self.sh(0xB0 + ch) & 0x20) | hi)

    def slide(self):                              # dce
        ch = self.b(0x1A02); v = self.cur
        lo = self.sh(0xA0 + ch)
        val = (((self.sh(0xB0 + ch) & 0x1F) << 8) + lo + s8(self.b(v + 1))) & 0xFFFF
        self.opl(0xA0 + ch, val & 0xFF)
        self.opl(0xB0 + ch, (self.sh(0xB0 + ch) & 0x20) | (val >> 8))

    def setop(self, op, p, ch):                   # 1048; p = patch offset
        self.opl(0x40 + op, 0x3F)
        bd = (self.sh(0xBD) & 0x3F) | (0x80 if self.b(0x1892) else 0) | (0x40 if self.b(0x1893) else 0)
        self.opl(0xBD, bd)
        self.opl(0x08, 0x40 if self.b(0x1894) else 0)
        self.opl(0xC0 + ch, (self.b(p + 0xA) << 1) | (0 if self.b(p + 0xD) else 1))
        self.opl(0x60 + op, (self.b(p) << 4) | (self.b(p + 1) & 0xF))
        self.opl(0x80 + op, (self.b(p + 2) << 4) | (self.b(p + 3) & 0xF))
        self.opl(0x20 + op, (0x80 if self.b(p + 0xB) else 0) + (0x40 if self.b(p + 0xC) else 0)
                 + (0x20 if self.b(p + 4) else 0) + (0x10 if self.b(p + 5) else 0) + (self.b(p + 9) & 0xF))
        self.opl(0xE0 + op, self.b(p + 8) & 3)
        self.opl(0x40 + op, ((0x3F - (self.b(p + 6) & 0x3F)) | (self.b(p + 7) << 6)) & 0xFF)

    def load_inst(self, n):                       # 123e
        ch = self.b(0x1A02)
        self.opl(0xB0 + ch, self.sh(0xB0 + ch) & 0xDF)
        p = 0x2E + 0x2C * (n & 0xFF)
        self.setop(self.b(0x18A8 + self.b(0x1896 + 2 * ch)), p, ch)
        e = 0x1B06 + 8 * ch
        self.sb(e, self.b(p + 0xE)); self.sw(e + 2, self.w(p + 0x10))
        self.sw(e + 4, self.w(p + 0x12)); self.sw(e + 6, self.w(p + 0x14))
        p += 0x16
        self.setop(self.b(0x18A8 + self.b(0x1896 + 2 * ch + 1)), p, ch)

    def note_on(self):                            # e8a
        self.vol()
        ch = self.b(0x1A02)
        e = 0x1B06 + 8 * ch
        if self.b(e) == 0:
            self.pitch()
            self.opl(0xB0 + ch, self.sh(0xB0 + ch) | 0x20)
            return
        if ch == self.b(0x1A04): self.sb(0x1A03, 0)
        if ch == self.b(0x1A05): self.sb(0x1A03, 1)
        if self.b(0x1A03) == 0:                   # unit 0
            self.sb(0x1A03, 1)
            if self.b(0x19DC):
                o = self.b(0x1A04) + 0xB0
                self.opl(o, self.sh(o) & 0xDF)
            self.sb(0x1A04, ch); self.sb(0x19DC, self.b(e))
            self.sw(0x19FE, self.w(e + 2)); self.sw(0x1902, self.w(e + 4)); self.sw(0x1906, self.w(e + 6))
            self.a5a5()
        else:                                     # unit 1
            self.sb(0x1A03, 0)
            if self.b(0x19DC):                    # sic: f86 tests [19DC], not [19FC]
                o = self.b(0x1A05) + 0xB0
                self.opl(o, self.sh(o) & 0xDF)
            self.sb(0x1A05, ch); self.sb(0x19FC, self.b(e))
            self.sw(0x1A00, self.w(e + 2)); self.sw(0x1904, self.w(e + 4)); self.sw(0x1908, self.w(e + 6))
            self.a5a5()

    def voice_tick(self):                         # 1360
        v = self.cur; m = self
        if m.b(v) == 0:
            m.sb(0x1A02, m.b(0x1A02) + 1); return
        if m.b(v + 8):
            m.sb(v + 8, m.b(v + 8) - 1)
            if m.b(v + 8) == 0: self.keyoff()
        m.sb(v, m.b(v) - 1)
        # the 'volume dirty' flag is the uninitialised local [bp-4] of 1360: its stack slot (DS:17D0) keeps the
        # value left by the previous non-skipped voice call, so it is modelled as a memory byte.
        if m.b(v) == 0:
            while True:
                p = m.w(v + 0x10)
                c = m.b(p)
                if c >= 0xF3:
                    m.sb(0x17D0, self.event(v, p, c, 0))
                    continue
                if m.b(0x17D0): self.vol()
                p = m.w(v + 0x10)
                m.sb(v + 4, m.b(p)); m.sb(v, m.b(p + 1))
                m.sw(v + 0x10, m.w(v + 0x10) + 2)
                if m.b(v + 4) == 0 or m.b(v) == 0:
                    self.keyoff()
                else:
                    m.sb(v + 8, m.b(v) - m.b(v + 7))
                    self.note_on()
                break
        # 17d1
        if m.b(v + 1): self.slide()
        b4 = 0; m.sb(0x17D0, 0)
        if m.b(v + 9) == 0 and m.b(v + 0xB) == 0:
            m.sb(0x1A02, m.b(0x1A02) + 1); return
        m.sb(v + 9, m.b(v + 9) - 1)
        if m.b(v + 9) == 0:
            m.sb(v + 9, m.b(v + 0xA))
            if m.b(v + 2): m.sb(v + 0x1D, m.b(v + 0x1D) + m.b(v + 2)); b4 = 1
        m.sb(v + 0xB, m.b(v + 0xB) - 1)
        if m.b(v + 0xB) == 0:
            m.sb(v + 0xB, m.b(v + 0xC))
            if m.b(v + 3): m.sb(v + 0xD, m.b(v + 0xD) + m.b(v + 3)); b4 = 1
        m.sb(0x17D0, b4)
        if b4: self.vol()
        m.sb(0x1A02, m.b(0x1A02) + 1)

    def event(self, v, p, c, b4):
        m = self; a1 = m.b(p + 1); b4 = 0              # 13c8: every event clears the 'volume dirty' flag
        if c == 0xFF:                              # loop 1
            if m.w(v + 0x16) == 0:
                if a1 == 0:
                    m.sw(v + 0x10, m.w(v + 0x10) + 2); m.sw(v + 0x12, m.w(v + 0x10)); m.sw(v + 0x16, 0)
                else:
                    m.sw(v + 0x16, a1); m.sw(v + 0x10, m.w(v + 0x12))
            else:
                m.sw(v + 0x16, m.w(v + 0x16) - 1)
                if m.w(v + 0x16) == 0:
                    m.sw(v + 0x10, m.w(v + 0x10) + 2); m.sw(v + 0x12, m.w(v + 0x10))
                else:
                    m.sw(v + 0x10, m.w(v + 0x12))
        elif c == 0xFE:                            # loop 2
            if m.w(v + 0x18) == 0:
                if a1 == 0:
                    m.sw(v + 0x10, m.w(v + 0x10) + 2)
                    m.sw(v + 0x14, m.w(v + 0x10)); m.sw(v + 0x12, m.w(v + 0x10))
                    m.sw(v + 0x16, 0); m.sw(v + 0x18, 0)
                else:
                    m.sw(v + 0x18, a1); m.sw(v + 0x12, m.w(v + 0x14)); m.sw(v + 0x10, m.w(v + 0x14))
            else:
                m.sw(v + 0x18, m.w(v + 0x18) - 1)
                m.sw(v + 0x12, m.w(v + 0x14))
                if m.w(v + 0x18) == 0:
                    m.sw(v + 0x10, m.w(v + 0x10) + 2); m.sw(v + 0x14, m.w(v + 0x10))
                else:
                    pass
                m.sw(v + 0x10, m.w(v + 0x14))
        elif c == 0xFD:                            # end / loop to pointer
            if m.w(v + 0x1A) == 0:
                m.sw(v + 0x10, m.w(v + 0xE))
            else:
                x = m.w(v + 0x1A)
                for k in (0xE, 0x10, 0x12, 0x14): m.sw(v + k, x)
        elif c == 0xFC:
            m.sb(v + 5, a1); m.sw(v + 0x10, m.w(v + 0x10) + 2); self.load_inst(a1)
        elif c == 0xFB: m.sb(v + 7, a1); m.sw(v + 0x10, m.w(v + 0x10) + 2)
        elif c == 0xFA: m.sb(v + 1, a1); m.sw(v + 0x10, m.w(v + 0x10) + 2)
        elif c == 0xF9: m.sb(v + 6, a1); m.sw(v + 0x10, m.w(v + 0x10) + 2); b4 = 1
        elif c == 0xF8:
            m.sb(v + 0xA, a1); m.sb(v + 2, m.b(p + 2)); m.sb(v + 9, 1); m.sw(v + 0x10, m.w(v + 0x10) + 3)
        elif c == 0xF7: m.sb(v + 0x1C, a1); m.sw(v + 0x10, m.w(v + 0x10) + 2)
        elif c == 0xF6:
            n = a1; r = self.rnd() & (n - 1) & 0xFFFF
            o = m.b(p + 2 + r)
            m.sb(p + 2 + n + 1 + m.b(p + 2 + n), o)
            m.sw(v + 0x10, m.w(v + 0x10) + n + 3)
        elif c == 0xF5: m.sb(v + 0x1D, a1); m.sw(v + 0x10, m.w(v + 0x10) + 2)
        elif c == 0xF4: m.sb(v + 0xD, a1); m.sw(v + 0x10, m.w(v + 0x10) + 2); b4 = 1
        elif c == 0xF3:
            m.sb(v + 0xC, a1); m.sb(v + 3, m.b(p + 2)); m.sb(v + 0xB, 1); m.sw(v + 0x10, m.w(v + 0x10) + 3)
        return b4

    # ---- poll -----------------------------------------------------------------------------
    def poll(self):                               # c08/430
        self.sw(0x17DC, 0)
        self.sw(CS + 0xE, self.w(CS + 0xE) + 1)
        self.rnd()
        if self.w(0x17EC) != 0:
            return self.w(0x17DC)
        eng = self.b(0x17F6) != 0 and self.w(0x17F4) != 0
        if eng:
            if self.b(0x19BE) == 0:
                self.sw(0x19CE, 0xE94); self.sb(0x19BE, 1)
            else:
                self.sb(0x19BE, 2); self.sb(0x19C6, 2)
        self.update_all()
        if eng:
            ax = self.w(0x17F4)
            if ax != self.w(0x17E6):
                self.sw(0x17E6, ax)
                self.freq(6, self.w(0x17E8))
                self.sw(0x17EA, (((ax % 0x55) << 8) | (ax // 0x55)) + 0x36)
                self.engine_vol()
        if (self.b(0x19FC) | self.b(0x19DC)) == 0:
            if self.w(0x17DE) != 0xFFFF:
                self.sw(0x17DE, 0xFFFF); self.sw(0x17DC, 0xFFFF)
            return self.w(0x17DC)
        if self.b(0x19DC):
            self.sw(0x1902, self.w(0x1902) + self.w(0x1906))
            self.sb(0x19DC, self.b(0x19DC) - 1)
            if self.b(0x19DC) == 0:
                al = self.b(0x1A04)
                if not (self.b(0x19FC) and al == self.b(0x1A05)):
                    self.opl(0xA0 + al, 0); self.opl(0xB0 + al, 0)
        if self.b(0x19FC):
            self.sw(0x1904, self.w(0x1904) + self.w(0x1908))
            self.sb(0x19FC, self.b(0x19FC) - 1)
            if self.b(0x19FC) == 0:
                al = self.b(0x1A05)
                if self.b(0x19DC) or al != self.b(0x1A04):
                    self.opl(0xA0 + al, 0); self.opl(0xB0 + al, 0)
        return self.w(0x17DC)

    def engine_vol(self):                         # 3fb
        if self.b(0x17F6) == 0 or self.w(0x17F4) == 0:
            return
        ax = self.w(0x17EA)
        self.opl(0x50, 0x3F - ax)
        self.opl(0x53, ax)

    def noise(self):                              # slot 104 -> 562
        r = self.rnd() if False else None
        v = (0x9248 + self.w(0x17E4)) & 0xFFFF
        v = ((v >> 3) | (v << 13)) & 0xFFFF
        self.sw(0x17E4, v)
        if self.b(0x19DC):
            self.freq(self.b(0x1A04), ((v ^ 0xFFFF) & self.w(0x19FE)) + self.w(0x1902))
        if self.b(0x19FC):
            self.freq(self.b(0x1A05), (v & self.w(0x1A00)) + self.w(0x1904))


Snd.cmd_tab = {
    0: Snd.c_reset, 1: Snd.c_stopsoft, 2: Snd.c_query,
    3: Snd.c_seq([(0, 0xFE0), (1, 0x11AC), (2, 0x121A), (3, 0x12FC), (4, 0x1448), (5, 0x154C)]),
    4: Snd.c_tab(1, 0x86E, 3), 5: Snd.c_tab(0, 0x857, 2),
    6: Snd.c_seq([(4, 0xF28)]), 7: Snd.c_seq([(3, 0xF52)]), 8: Snd.c_seq([(3, 0xF4A)]), 9: Snd.c9,
    10: Snd.c_seq([(5, 0xF94)]), 11: Snd.c11, 12: Snd.c_seq([(3, 0xFB4)]), 13: Snd.c_seq([(0, 0xF34)]),
    14: Snd.c_seq([(5, 0xF8A)]), 15: Snd.c_seq([(0, 0xF78)]), 16: Snd.c_seq([(0, 0xF70)]),
    17: Snd.c_seq([(1, 0xF68)]), 18: Snd.c_seq([(5, 0xF80)]), 19: Snd.c_seq([(3, 0xFC8), (2, 0xFD4)]),
    20: Snd.c_seq([(5, 0xF5A)]), 21: Snd.c21, 22: Snd.c22, 23: Snd.c23, 24: Snd.c24, 25: Snd.c25,
    26: Snd.c_seq([(5, 0xE9C)]), 27: Snd.c_seq([(0, 0xEB4)]), 28: Snd.c_rnd(0, 0xEC0, 6, 0x21),
    29: Snd.c_seq([(0, 0xED2)]), 30: Snd.c_seq([(0, 0xECA)]), 31: Snd.c_seq([(2, 0xEE4)]),
    32: Snd.c_seq([(2, 0xEEC)]), 33: Snd.c_rnd(0, 0xEDA, 6, 1), 34: Snd.c_seq([(5, 0xEAC)]),
    35: Snd.c_seq([(5, 0xEA4)]),
    36: Snd.c_seq([(0, 0x876), (1, 0x992), (2, 0xA9A), (3, 0xB2E)]),
    37: Snd.c_seq([(0, 0xB66), (1, 0xBA0), (2, 0xBDE), (3, 0xC1C), (4, 0xB5E), (5, 0xB5E)],
                  lambda s: (s.sw(0x1982 + 0x1A, 0xB6A), s.sw(0x19A0 + 0x1A, 0xBA6))),
    38: Snd.c_seq([(0, 0xC76), (1, 0xD10), (2, 0xDB2), (3, 0xE24)]),
    39: Snd.c_seq([(0, 0x766), (1, 0x7B4), (2, 0x7CE), (3, 0x7EE), (4, 0x830), (5, 0x85C)]),
    40: Snd.c40, 41: Snd.c_speech, 42: Snd.c_speech, 43: Snd.c_speech, 44: Snd.c_speech, 45: Snd.c_speech,
}


def run(cmd, polls, arg=0):
    s = Snd(); s.init(); s.log.clear()
    s.cmd(cmd, arg)
    out = [(-1, r, v) for r, v in s.log]; s.log.clear()
    for i in range(polls):
        s.poll(); s.noise()
        out += [(i, r, v) for r, v in s.log]; s.log.clear()
    return s, out


def emu(cmd, polls, arg=0):
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from drvemu import Drv
    D = Drv('Game/ASOUND.117')
    D.call(0, 0); D.t = -1; D.log.clear()
    D.call(1, cmd, arg)
    out = []
    for i in range(polls):
        D.t = i
        D.call(3); D.call(4)
    return [(t, r, v) for t, r, v in D.log]


if __name__ == '__main__':
    if sys.argv[1] == 'verify':
        polls = int(sys.argv[2]) if len(sys.argv) > 2 else 1500
        bad = 0
        for c in range(0, 40):
            a = [x for x in run(c, polls)[1] if x[1] != 4]; e = emu(c, polls)   # emulator hides reg 4 (timer detect)
            # emulator log of the cmd call itself carries t=-1 set above
            ok = a == e
            if not ok:
                bad += 1
                i = next((k for k in range(min(len(a), len(e))) if a[k] != e[k]), min(len(a), len(e)))
                print('cmd %2d MISMATCH at %d: model %s  emu %s  (len %d vs %d)' % (c, i, a[i:i + 2], e[i:i + 2], len(a), len(e)))
            else:
                print('cmd %2d ok  %d writes' % (c, len(a)))
        print('mismatches:', bad)
    else:
        s, out = run(int(sys.argv[1]), int(sys.argv[2]) if len(sys.argv) > 2 else 600)
        print(len(out), 'writes')
