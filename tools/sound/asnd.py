"""Reference models of the F-117A AdLib sequencer, decoded in port/spec/sound.md.
  Game  = ASOUND.117 (in-game driver, state 0 = plain AdLib; polled by the game's timer ISR)
  Logo  = ASOUND.LOG (logo-jingle overlay; installs its own INT 8 ISR)
Both read their data (tracks, instruments, tables) from the original files at run time and log every OPL write
as (reg, value).  Needs the Game/ junction and (for verify) `pip install unicorn`.
  asnd.py verify [polls]     compare Game against the Unicorn run of the real driver (tools/sound/drvemu.py)
  asnd.py verifylog [ticks]  compare Logo against the real overlay
  asnd.py CMD [polls]        run Game command CMD, print the write count
"""
import os, struct, sys


def s8(x): return x - 256 if x & 0x80 else x


class Engine:
    """The sequencer shared by both drivers.  Subclasses give the memory layout (field offsets, table
    addresses) and the few places where the two builds differ."""
    LCG_K = 0x9248

    def __init__(self, path):
        d = open(path, 'rb').read()
        self.m = bytearray(d[struct.unpack_from('<H', d, 8)[0] * 16:])
        self.log = []

    # ---- memory ---------------------------------------------------------------------------
    def b(self, o): return self.m[o]
    def w(self, o): return self.m[o] | self.m[o + 1] << 8
    def sb(self, o, v): self.m[o] = v & 0xFF
    def sw(self, o, v): self.m[o] = v & 0xFF; self.m[o + 1] = (v >> 8) & 0xFF
    def fb(self, v, f): return self.m[v + self.V[f]]
    def fw(self, v, f): return self.w(v + self.V[f])
    def sfb(self, v, f, x): self.sb(v + self.V[f], x)
    def sfw(self, v, f, x): self.sw(v + self.V[f], x)

    # ---- OPL / helpers ----------------------------------------------------------------------
    def opl(self, reg, val):
        reg &= 0xFF; val &= 0xFF
        self.m[self.SHADOW + reg] = val
        self.log.append((reg, val))

    def sh(self, reg): return self.m[self.SHADOW + reg]

    def freq(self, ch, bx):
        self.opl(0xA0 + ch, bx & 0xFF)
        self.opl(0xB0 + ch, ((bx >> 8) & 0xFF) | 0x20)

    def rnd(self):
        v = (self.LCG_K + self.w(self.G_lcg)) & 0xFFFF
        v = ((v >> 3) | (v << 13)) & 0xFFFF
        self.sw(self.G_lcg, v)
        return v

    # ---- one voice ----------------------------------------------------------------------
    def keyoff(self):
        ch = self.b(self.G_cur)
        self.opl(0xB0 + ch, self.sh(0xB0 + ch) & 0xDF)

    def vol(self):
        ch = self.b(self.G_cur)
        reg = 0x40 + self.b(self.OPOFF + self.b(self.OPCH + 2 * ch + 1))
        v = self.cur
        t = (self.fb(v, 'vol') + self.fb(v, 'vofs')) & 0x3F
        self.opl(reg, (0x3F - t) | (self.sh(reg) & 0xC0))

    def pitch(self):
        ch = self.b(self.G_cur); v = self.cur
        q, r = divmod(self.fb(v, 'pitch'), 12)
        f = (self.w(self.FNUM + 2 * r) + s8(self.fb(v, 'tune'))) & 0xFFFF
        self.opl(0xA0 + ch, f & 0xFF)
        self.opl(0xB0 + ch, (self.sh(0xB0 + ch) & 0x20) | (((f >> 8) | (q << 2)) & 0xFF))

    def slide(self):
        ch = self.b(self.G_cur); v = self.cur
        val = (((self.sh(0xB0 + ch) & 0x1F) << 8) + self.sh(0xA0 + ch) + s8(self.fb(v, 'slide'))) & 0xFFFF
        self.opl(0xA0 + ch, val & 0xFF)
        self.opl(0xB0 + ch, (self.sh(0xB0 + ch) & 0x20) | (val >> 8))

    # register groups of an instrument half (p = patch address, op = operator offset, ch = channel)
    def r_bd(self):
        bd = (self.sh(0xBD) & 0x3F) | (0x80 if self.b(self.F_trem) else 0) | (0x40 if self.b(self.F_vib) else 0)
        self.opl(0xBD, bd)
        self.opl(0x08, 0x40 if self.b(self.F_csm) else 0)

    def r_level(self, op, p): self.opl(0x40 + op, ((0x3F - (self.b(p + 6) & 0x3F)) | (self.b(p + 7) << 6)) & 0xFF)
    def r_fb(self, ch, p): self.opl(0xC0 + ch, (self.b(p + 0xA) << 1) | (0 if self.b(p + 0xD) else 1))
    def r_ad(self, op, p): self.opl(0x60 + op, (self.b(p) << 4) | (self.b(p + 1) & 0xF))
    def r_sr(self, op, p): self.opl(0x80 + op, (self.b(p + 2) << 4) | (self.b(p + 3) & 0xF))
    def r_mult(self, op, p):
        self.opl(0x20 + op, (0x80 if self.b(p + 0xB) else 0) + (0x40 if self.b(p + 0xC) else 0)
                 + (0x20 if self.b(p + 4) else 0) + (0x10 if self.b(p + 5) else 0) + (self.b(p + 9) & 0xF))
    def r_wave(self, op, p): self.opl(0xE0 + op, self.b(p + 8) & 3)

    def load_inst(self, n):
        ch = self.b(self.G_cur)
        self.opl(0xB0 + ch, self.sh(0xB0 + ch) & 0xDF)
        p = self.INST + 0x2C * (n & 0xFF)
        self.setop(self.b(self.OPOFF + self.b(self.OPCH + 2 * ch)), p, ch)
        e = self.EFF + 8 * ch
        self.sb(e, self.b(p + 0xE)); self.sw(e + 2, self.w(p + 0x10))
        self.sw(e + 4, self.w(p + 0x12)); self.sw(e + 6, self.w(p + 0x14))
        self.setop(self.b(self.OPOFF + self.b(self.OPCH + 2 * ch + 1)), p + 0x16, ch)

    def note_on(self):
        self.vol()
        ch = self.b(self.G_cur)
        e = self.EFF + 8 * ch
        if self.b(e) == 0:
            self.pitch()
            self.opl(0xB0 + ch, self.sh(0xB0 + ch) | 0x20)
            return
        if ch == self.b(self.G_u0): self.sb(self.G_f03, 0)
        if ch == self.b(self.G_u1): self.sb(self.G_f03, 1)
        if self.b(self.G_f03) == 0:                       # unit 0
            self.sb(self.G_f03, 1)
            if self.b(self.G_c0):
                o = self.b(self.G_u0) + 0xB0
                self.opl(o, self.sh(o) & 0xDF)
            self.sb(self.G_u0, ch); self.sb(self.G_c0, self.b(e))
            self.sw(self.G_m0, self.w(e + 2)); self.sw(self.G_b0, self.w(e + 4)); self.sw(self.G_d0, self.w(e + 6))
        else:                                             # unit 1
            self.sb(self.G_f03, 0)
            if self.b(self.G_c0):                         # sic: tests unit 0's counter
                o = self.b(self.G_u1) + 0xB0
                self.opl(o, self.sh(o) & 0xDF)
            self.sb(self.G_u1, ch); self.sb(self.G_c1, self.b(e))
            self.sw(self.G_m1, self.w(e + 2)); self.sw(self.G_b1, self.w(e + 4)); self.sw(self.G_d1, self.w(e + 6))
        self.units_started()

    # ---- the per-voice tick (1360 / 0D1A) ---------------------------------------------------
    def update_all(self):
        self.sb(self.G_cur, 0)
        for v in self.VOICES:
            self.cur = v
            self.voice_tick()

    def advance(self, v, n): self.sfw(v, 'ptr', self.fw(v, 'ptr') + n)

    def voice_tick(self):
        v = self.cur
        if self.fb(v, 't') == 0:
            self.sb(self.G_cur, self.b(self.G_cur) + 1); return
        if self.fb(v, 'rel'):
            self.sfb(v, 'rel', self.fb(v, 'rel') - 1)
            if self.fb(v, 'rel') == 0: self.keyoff()
        self.sfb(v, 't', self.fb(v, 't') - 1)
        if self.fb(v, 't') == 0:
            self.read_events(v)
        self.tail(v)
        self.sb(self.G_cur, self.b(self.G_cur) + 1)

    def read_events(self, v):
        while True:
            p = self.fw(v, 'ptr'); c = self.b(p)
            if c >= self.EV_MIN:
                self.event(v, p, c)
                continue
            self.before_note()
            p = self.fw(v, 'ptr')
            self.sfb(v, 'pitch', self.b(p)); self.sfb(v, 't', self.b(p + 1))
            self.advance(v, 2)
            if self.fb(v, 'pitch') == 0 or self.fb(v, 't') == 0:
                self.keyoff()
            else:
                self.sfb(v, 'rel', self.fb(v, 't') - self.fb(v, 'gate'))
                self.note_on()
            return

    def event(self, v, p, c):
        a1 = self.b(p + 1); S = self.sfb
        if c == 0xFF:                                     # loop 1
            if self.fw(v, 'cnt1') == 0:
                if a1 == 0:
                    self.advance(v, 2); self.sfw(v, 'ls1', self.fw(v, 'ptr')); self.sfw(v, 'cnt1', 0)
                else:
                    self.sfw(v, 'cnt1', a1); self.sfw(v, 'ptr', self.fw(v, 'ls1'))
            else:
                self.sfw(v, 'cnt1', self.fw(v, 'cnt1') - 1)
                if self.fw(v, 'cnt1') == 0:
                    self.advance(v, 2); self.sfw(v, 'ls1', self.fw(v, 'ptr'))
                else:
                    self.sfw(v, 'ptr', self.fw(v, 'ls1'))
        elif c == 0xFE:                                   # loop 2
            if self.fw(v, 'cnt2') == 0:
                if a1 == 0:
                    self.advance(v, 2)
                    self.sfw(v, 'ls2', self.fw(v, 'ptr')); self.sfw(v, 'ls1', self.fw(v, 'ptr'))
                    self.sfw(v, 'cnt1', 0); self.sfw(v, 'cnt2', 0)
                else:
                    self.sfw(v, 'cnt2', a1); self.sfw(v, 'ls1', self.fw(v, 'ls2')); self.sfw(v, 'ptr', self.fw(v, 'ls2'))
            else:
                self.sfw(v, 'cnt2', self.fw(v, 'cnt2') - 1)
                self.sfw(v, 'ls1', self.fw(v, 'ls2'))
                if self.fw(v, 'cnt2') == 0:
                    self.advance(v, 2); self.sfw(v, 'ls2', self.fw(v, 'ptr'))
                self.sfw(v, 'ptr', self.fw(v, 'ls2'))
        elif c == 0xFD:                                   # end of track / jump to loop pointer
            if self.fw(v, 'lptr') == 0:
                self.sfw(v, 'ptr', self.fw(v, 'start'))
            else:
                x = self.fw(v, 'lptr')
                for f in ('start', 'ptr', 'ls1', 'ls2'): self.sfw(v, f, x)
        elif c == 0xFC: S(v, 'inst', a1); self.advance(v, 2); self.load_inst(a1)
        elif c == 0xFB: S(v, 'gate', a1); self.advance(v, 2)
        elif c == 0xFA: S(v, 'slide', a1); self.advance(v, 2)
        elif c == 0xF9: S(v, 'vol', a1); self.advance(v, 2); self.vol_changed()
        elif c == 0xF8:
            S(v, 'rrel', a1); S(v, 'rstep', self.b(p + 2)); S(v, 'rctr', 1); self.advance(v, 3)
        elif c == 0xF7: S(v, 'tune', a1); self.advance(v, 2)
        elif c == 0xF6:
            n = a1; r = self.rnd() & (n - 1) & 0xFFFF
            self.sb(p + 2 + n + 1 + self.b(p + 2 + n), self.b(p + 2 + r))
            self.advance(v, n + 3)
        elif c == 0xF5: S(v, 'vofs', a1); self.advance(v, 2)
        else:
            self.event_extra(v, p, c, a1)

    # ---- effect units: per-poll tail and noise ----------------------------------------------
    def unit_tail(self):
        if self.b(self.G_c0):
            self.sw(self.G_b0, self.w(self.G_b0) + self.w(self.G_d0))
            self.sb(self.G_c0, self.b(self.G_c0) - 1)
            if self.b(self.G_c0) == 0:
                al = self.b(self.G_u0)
                if not (self.b(self.G_c1) and al == self.b(self.G_u1)):
                    self.opl(0xA0 + al, 0); self.opl(0xB0 + al, 0)
        if self.b(self.G_c1):
            self.sw(self.G_b1, self.w(self.G_b1) + self.w(self.G_d1))
            self.sb(self.G_c1, self.b(self.G_c1) - 1)
            if self.b(self.G_c1) == 0:
                al = self.b(self.G_u1)
                if self.b(self.G_c0) or al != self.b(self.G_u0):
                    self.opl(0xA0 + al, 0); self.opl(0xB0 + al, 0)

    def noise(self):
        v = self.rnd()
        if self.b(self.G_c0):
            self.freq(self.b(self.G_u0), ((v ^ 0xFFFF) & self.w(self.G_m0)) + self.w(self.G_b0))
        if self.b(self.G_c1):
            self.freq(self.b(self.G_u1), (v & self.w(self.G_m1)) + self.w(self.G_b1))


# =============================================================================================
class Game(Engine):
    """ASOUND.117.  Voice records 1Eh bytes at DS:190A.. ; DS = image 0..1B5Fh, cs variables = image 1B60h+."""
    CS = 0x1B60
    VOICES = [0x190A + 0x1E * i for i in range(7)]
    V = dict(t=0, slide=1, rstep=2, pstep=3, pitch=4, inst=5, vol=6, gate=7, rel=8, rctr=9, rrel=0xA, pctr=0xB,
             prel=0xC, pan=0xD, start=0xE, ptr=0x10, ls1=0x12, ls2=0x14, cnt1=0x16, cnt2=0x18, lptr=0x1A,
             tune=0x1C, vofs=0x1D)
    G_cur, G_f03, G_u0, G_u1 = 0x1A02, 0x1A03, 0x1A04, 0x1A05
    G_c0, G_c1, G_m0, G_m1 = 0x19DC, 0x19FC, 0x19FE, 0x1A00
    G_b0, G_b1, G_d0, G_d1 = 0x1902, 0x1904, 0x1906, 0x1908
    G_lcg, SHADOW, EFF = 0x17E4, 0x1A06, 0x1B06
    OPCH, OPOFF, FNUM, INST = 0x1896, 0x18A8, 0x187A, 0x2E
    F_trem, F_vib, F_csm = 0x1892, 0x1893, 0x1894
    EV_MIN = 0xF3
    FLAG = 0x17D0            # stack slot of the uninitialised local 'volume dirty' in 1360 (see spec)

    def __init__(self, path='Game/ASOUND.117'):
        super().__init__(path)

    # init / commands ---------------------------------------------------------------------
    def init(self):
        self.sw(0x17F2, 0)
        self.reset()

    def reset(self):                                      # 18F
        old = self.w(0x17EC); self.sw(0x17EC, 0xFFFF)
        for v in self.VOICES:
            for k in (0, 1, 2, 3): self.sb(v + k, 0)
        for a in (self.G_c0, self.G_c1): self.sb(a, 0)
        for a in (self.G_m0, self.G_m1, self.G_b0, self.G_b1, self.G_d0, self.G_d1): self.sw(a, 0)
        for r in range(0x5F, 0x3F, -1): self.opl(r, 0x3F)       # 210: MOV BX,3Fh first
        for r in range(0xFF, 0x5F, -1): self.opl(r, 0)
        for r in range(0x3F, 0, -1): self.opl(r, 0)
        self.opl(1, 0x20)
        self.sw(0x17EC, old)

    def spawn(self, vi, cx, ax=0):                        # 5E3..5C0 family
        old = self.w(0x17EC); self.sw(0x17EC, 0)
        v = self.VOICES[vi]
        for f in ('slide', 'rstep', 'pstep', 'tune', 'vofs'): self.sfb(v, f, 0)
        self.sfb(v, 'rrel', 0xFF); self.sfb(v, 'pan', 0x40)
        for f in ('start', 'ptr', 'ls1', 'ls2'): self.sfw(v, f, cx)
        self.sfw(v, 'cnt1', 0); self.sfw(v, 'cnt2', 0); self.sfb(v, 'gate', 0)
        self.sfw(v, 'lptr', ax); self.sfb(v, 't', 1)
        self.sw(0x17EC, old)

    def cmd(self, n, arg=0):
        self.ret = 0
        if n <= 0x2D:
            self.cmd_tab[n](self, n, arg)
        return self.ret

    def units_started(self):                              # 5A5
        if self.w(0x17DE) != 1:
            self.sw(0x17DE, 1); self.sw(0x17DC, 1)

    def c_reset(self, n, a): self.reset()
    def c_stopsoft(self, n, a):
        for v in self.VOICES: self.sfw(v, 'ls1', 0xE9A)
    def c_query(self, n, a):
        r = 0
        for v in self.VOICES: r |= self.fb(v, 't')
        self.ret = r
    def c_seq(lst, extra=None):
        def f(self, n, a):
            for vi, tr in lst: self.spawn(vi, tr)
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
            self.spawn(vi, self.w(self.CS + tbl + ((n * 2) >> shift & 6)))
        return f
    def c9(self, n, a):
        if self.w(0x1972) == 0xF3C and self.b(0x1964) != 0: return
        self.spawn(3, 0xF3C)
    def c11(self, n, a):
        if self.w(0x17F4) == 0: self.set_engine(1)
        self.spawn(3, 0xFA2)
    def set_engine(self, bx):                             # 3D8
        bx = min(bx, 0x301) if bx < 0x8000 else bx
        self.sw(0x17F4, bx)
        if bx == 0: self.sw(0x17E6, 0); return
        self.sw(0x17E8, (bx & 0xFFFE) + 0x883)
    def c23(self, n, a): self.set_engine(a)
    def c24(self, n, a): self.sb(0x17F6, 0xFF)
    def c25(self, n, a): self.sb(0x17F6, 0); self.sw(0x17E6, 0xFFFF)
    def c21(self, n, a):
        self.sb(0x17F0, self.b(self.G_c0)); self.sb(self.G_c0, 0)
        self.sb(0x17F1, self.b(self.G_c1)); self.sb(self.G_c1, 0)
        self.sw(0x17EC, 0xFFFF)
        for r in (0x43, 0x44, 0x45, 0x4B, 0x4C, 0x4D, 0x53): self.opl(r, self.sh(r) | 0x3F)
    def c22(self, n, a):
        for r, vi in ((0x43, 0), (0x44, 1), (0x45, 2), (0x4B, 3), (0x4C, 4), (0x4D, 5), (0x53, 6)):
            self.opl(r, (self.sh(r) & 0xC0) | (self.fb(self.VOICES[vi], 'vol') ^ 0x3F))
        self.sb(self.G_c0, self.b(0x17F0)); self.sb(self.G_c1, self.b(0x17F1))
        if self.b(0x17F0) | self.b(0x17F1): self.units_started()
        self.sw(0x17EC, 0)
    def c40(self, n, a): self.sw(0x17E2, a)
    def c_speech(self, n, a): pass                        # 41..45: sample playback, spec Â§11

    # engine hooks ------------------------------------------------------------------------
    def setop(self, op, p, ch):                           # 1048
        self.opl(0x40 + op, 0x3F)
        self.r_bd(); self.r_fb(ch, p); self.r_ad(op, p); self.r_sr(op, p); self.r_mult(op, p)
        self.r_wave(op, p); self.r_level(op, p)

    def before_note(self):
        if self.b(self.FLAG): self.vol()

    def vol_changed(self): self.sb(self.FLAG, 1)

    def event_extra(self, v, p, c, a1):
        if c == 0xF4: self.sfb(v, 'pan', a1); self.advance(v, 2); self.sb(self.FLAG, 1)
        elif c == 0xF3:
            self.sfb(v, 'prel', a1); self.sfb(v, 'pstep', self.b(p + 2)); self.sfb(v, 'pctr', 1); self.advance(v, 3)

    def event(self, v, p, c):
        self.sb(self.FLAG, 0)                             # 13C8: every event clears the flag
        super().event(v, p, c)

    def tail(self, v):                                    # 17D1
        if self.fb(v, 'slide'): self.slide()
        self.sb(self.FLAG, 0)
        if self.fb(v, 'rctr') == 0 and self.fb(v, 'pctr') == 0:
            return
        self.sfb(v, 'rctr', self.fb(v, 'rctr') - 1)
        if self.fb(v, 'rctr') == 0:
            self.sfb(v, 'rctr', self.fb(v, 'rrel'))
            if self.fb(v, 'rstep'):
                self.sfb(v, 'vofs', self.fb(v, 'vofs') + self.fb(v, 'rstep')); self.sb(self.FLAG, 1)
        self.sfb(v, 'pctr', self.fb(v, 'pctr') - 1)
        if self.fb(v, 'pctr') == 0:
            self.sfb(v, 'pctr', self.fb(v, 'prel'))
            if self.fb(v, 'pstep'):
                self.sfb(v, 'pan', self.fb(v, 'pan') + self.fb(v, 'pstep')); self.sb(self.FLAG, 1)
        if self.b(self.FLAG): self.vol()

    def poll(self):                                       # C08 -> 430
        self.sw(0x17DC, 0)
        self.sw(self.CS + 0xE, self.w(self.CS + 0xE) + 1)
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
        if (self.b(self.G_c1) | self.b(self.G_c0)) == 0:
            if self.w(0x17DE) != 0xFFFF:
                self.sw(0x17DE, 0xFFFF); self.sw(0x17DC, 0xFFFF)
            return self.w(0x17DC)
        self.unit_tail()
        return self.w(0x17DC)

    def engine_vol(self):                                 # 3FB
        if self.b(0x17F6) == 0 or self.w(0x17F4) == 0: return
        ax = self.w(0x17EA)
        self.opl(0x50, 0x3F - ax); self.opl(0x53, ax)


Game.cmd_tab = {
    0: Game.c_reset, 1: Game.c_stopsoft, 2: Game.c_query,
    3: Game.c_seq([(0, 0xFE0), (1, 0x11AC), (2, 0x121A), (3, 0x12FC), (4, 0x1448), (5, 0x154C)]),
    4: Game.c_tab(1, 0x86E, 3), 5: Game.c_tab(0, 0x857, 2),
    6: Game.c_seq([(4, 0xF28)]), 7: Game.c_seq([(3, 0xF52)]), 8: Game.c_seq([(3, 0xF4A)]), 9: Game.c9,
    10: Game.c_seq([(5, 0xF94)]), 11: Game.c11, 12: Game.c_seq([(3, 0xFB4)]), 13: Game.c_seq([(0, 0xF34)]),
    14: Game.c_seq([(5, 0xF8A)]), 15: Game.c_seq([(0, 0xF78)]), 16: Game.c_seq([(0, 0xF70)]),
    17: Game.c_seq([(1, 0xF68)]), 18: Game.c_seq([(5, 0xF80)]), 19: Game.c_seq([(3, 0xFC8), (2, 0xFD4)]),
    20: Game.c_seq([(5, 0xF5A)]), 21: Game.c21, 22: Game.c22, 23: Game.c23, 24: Game.c24, 25: Game.c25,
    26: Game.c_seq([(5, 0xE9C)]), 27: Game.c_seq([(0, 0xEB4)]), 28: Game.c_rnd(0, 0xEC0, 6, 0x21),
    29: Game.c_seq([(0, 0xED2)]), 30: Game.c_seq([(0, 0xECA)]), 31: Game.c_seq([(2, 0xEE4)]),
    32: Game.c_seq([(2, 0xEEC)]), 33: Game.c_rnd(0, 0xEDA, 6, 1), 34: Game.c_seq([(5, 0xEAC)]),
    35: Game.c_seq([(5, 0xEA4)]),
    36: Game.c_seq([(0, 0x876), (1, 0x992), (2, 0xA9A), (3, 0xB2E)]),
    37: Game.c_seq([(0, 0xB66), (1, 0xBA0), (2, 0xBDE), (3, 0xC1C), (4, 0xB5E), (5, 0xB5E)],
                   lambda s: (s.sw(0x1982 + 0x1A, 0xB6A), s.sw(0x19A0 + 0x1A, 0xBA6))),
    38: Game.c_seq([(0, 0xC76), (1, 0xD10), (2, 0xDB2), (3, 0xE24)]),
    39: Game.c_seq([(0, 0x766), (1, 0x7B4), (2, 0x7CE), (3, 0x7EE), (4, 0x830), (5, 0x85C)]),
    40: Game.c40, 41: Game.c_speech, 42: Game.c_speech, 43: Game.c_speech, 44: Game.c_speech, 45: Game.c_speech,
}


# =============================================================================================
class Logo(Engine):
    """ASOUND.LOG: the same sequencer, older build.  Voice records 1Ah bytes at DS:43C (DS = image 11C0h)."""
    DS = 0x11C0
    VOICES = [0x43C + 0x1A * i for i in range(7)]
    V = dict(t=0, slide=1, rstep=2, pitch=3, inst=4, vol=5, gate=6, rel=7, rctr=8, rrel=9, start=0xA, ptr=0xC,
             ls1=0xE, ls2=0x10, cnt1=0x12, cnt2=0x14, lptr=0x16, tune=0x18, vofs=0x19)
    G_cur, G_f03, G_u0, G_u1 = 0x512, 0x513, 0x514, 0x515
    G_c0, G_c1, G_m0, G_m1 = 0x50C, 0x50D, 0x50E, 0x510
    G_b0, G_b1, G_d0, G_d1 = 0x432, 0x434, 0x436, 0x438
    G_lcg, SHADOW, EFF = 0x3CE, 0x516, 0x616
    OPCH, OPOFF, FNUM, INST = 0x404, 0x416, 0x3E8, 0x50
    F_trem, F_vib, F_csm = 0x400, 0x401, 0x402
    EV_MIN = 0xF5
    PLAYING = 0x43A
    TRACKS = [0x10E, 0x120, 0x14C, 0x17A, 0x1A6, 0x1B6]

    def __init__(self, path='Game/ASOUND.LOG'):
        super().__init__(path)
        self.m = bytearray(self.m[self.DS:]) + bytearray(0x400)   # DS as address 0 (+ BSS up to size 680h)
        self.fast = False                                # INT 8 mode: slow (61 Hz) / fast (300 Hz)
        self.div = 3

    def init(self):                                       # 19A + 220
        self.sw(0x3DC, 0); self.sw(0x3DA, 0); self.sw(0x3DE, 0); self.sb(self.PLAYING, 0)
        for v in self.VOICES:
            for k in (0, 1, 2): self.sb(v + k, 0)
        for a in (self.G_c0, self.G_c1): self.sb(a, 0)
        for a in (self.G_m0, self.G_m1, self.G_b0, self.G_b1, self.G_d0, self.G_d1): self.sw(a, 0)
        self.sb(self.PLAYING, 0xFF)
        for r in range(0xFF, 0, -1): self.opl(r, 0)
        self.opl(1, 0x20)

    def spawn(self, v, cx, ax=0):                         # 515
        keep = self.b(self.PLAYING); self.sb(self.PLAYING, 0)
        self.sfb(v, 'slide', 0); self.sfb(v, 'rrel', 0xFF); self.sfb(v, 'rstep', 0)
        self.sfb(v, 'tune', 0); self.sfb(v, 'vofs', 0)
        for f in ('start', 'ptr', 'ls1', 'ls2'): self.sfw(v, f, cx)
        self.sfw(v, 'cnt1', 0); self.sfw(v, 'cnt2', 0); self.sfb(v, 'gate', 0)
        self.sfw(v, 'lptr', ax); self.sfb(v, 't', 1)
        self.sb(self.PLAYING, keep)

    def cmd(self, n):
        if n == 0:
            self.init()
        elif n == 1:                                      # pause: every stream -> DS:0100
            for v in self.VOICES: self.sfw(v, 'ptr', 0x100)
        elif n == 2:
            for v, tr in zip(self.VOICES, self.TRACKS): self.spawn(v, tr)

    # hooks -------------------------------------------------------------------------------
    def setop(self, op, p, ch):                           # BE2: BD, 08, level, C0, 60, 80, 20, E0
        self.r_bd(); self.r_level(op, p); self.r_fb(ch, p); self.r_ad(op, p); self.r_sr(op, p)
        self.r_mult(op, p); self.r_wave(op, p)

    def before_note(self): pass
    def vol_changed(self): self.vol()
    def event_extra(self, v, p, c, a1): pass
    def units_started(self): self.fast = True             # 4AE: INT 8 -> 42F, PIT F88h (300 Hz), sequencer /5

    def tail(self, v):                                    # 1113
        if self.fb(v, 'slide'): self.slide()
        if self.fb(v, 'rctr'):
            self.sfb(v, 'rctr', self.fb(v, 'rctr') - 1)
            if self.fb(v, 'rctr') == 0:
                self.sfb(v, 'rctr', self.fb(v, 'rrel'))
                if self.fb(v, 'rstep'):
                    self.sfb(v, 'vofs', self.fb(v, 'vofs') + self.fb(v, 'rstep')); self.vol()

    def poll(self):                                       # 344: the sequencer step
        self.rnd()
        if self.b(self.PLAYING) == 0:
            return
        self.update_all()
        if (self.b(self.G_c1) | self.b(self.G_c0)) == 0:
            self.fast = False                             # 3F8: back to the slow ISR
            return
        self.unit_tail()

    def isr(self):
        """One INT 8.  Slow mode: 60.9 Hz (PIT 4C90h), sequencer every tick.  Fast mode: 300 Hz (PIT 0F88h),
        noise every tick, sequencer every 5th."""
        if not self.fast:
            self.div = 3
            self.poll(); return
        self.noise()
        self.div -= 1
        if self.div == 0:
            self.div = 5; self.poll()


# =============================================================================================
def _drv():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from drvemu import Drv
    return Drv


def run_game(cmd, polls, arg=0):
    s = Game(); s.init(); s.log.clear()
    s.cmd(cmd, arg)
    out = [(-1, r, v) for r, v in s.log]; s.log.clear()
    for i in range(polls):
        s.poll(); s.noise()
        out += [(i, r, v) for r, v in s.log]; s.log.clear()
    return s, out


def emu_game(cmd, polls, arg=0):
    D = _drv()('Game/ASOUND.117')
    D.call(0, 0); D.t = -1; D.log.clear()
    D.call(1, cmd, arg)
    for i in range(polls):
        D.t = i
        D.call(3); D.call(4)
    return [(t, r, v) for t, r, v in D.log]


def run_logo(ticks, cmd=2):
    s = Logo(); s.init(); s.log.clear()
    s.cmd(cmd)
    for i in range(ticks): s.isr()
    return [x for x in s.log if x[0] != 4]


def emu_logo(ticks, cmd=2):
    D = _drv()('Game/ASOUND.LOG')
    uc = D.uc
    from unicorn.x86_const import UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SP, UC_X86_REG_CS
    import drvemu
    uc.mem_write(0x20, struct.pack('<HH', 0, drvemu.STUB))
    D.call(0, 0); D.log.clear(); D.call(1, cmd)
    for k in range(ticks):
        isr_off, isr_seg = struct.unpack('<HH', bytes(uc.mem_read(0x20, 4)))
        uc.reg_write(UC_X86_REG_SS, drvemu.STK); uc.reg_write(UC_X86_REG_DS, drvemu.L); uc.reg_write(UC_X86_REG_ES, drvemu.L)
        sp = 0xF000 - 6
        uc.mem_write(drvemu.STK * 16 + sp, struct.pack('<HHH', 0, drvemu.STUB, 0x202))
        uc.reg_write(UC_X86_REG_SP, sp); uc.reg_write(UC_X86_REG_CS, isr_seg)
        uc.emu_start(isr_seg * 16 + isr_off, drvemu.STUB * 16, count=2_000_000)
    return [(r, v) for t, r, v in D.log]


OPL_RATE = 49716


def steered(cmd, secs, frame_hz=70.086, cls=4, arg=0):
    """Game command rendered with the game's ISR rule (spec §7): one poll per video frame while idle, `cls` polls
    per frame (280 Hz at 70 Hz) while snd_poll returns > 0, back to idle on -1; slot 104 only in the fast mode.
    Returns [(t_opl, reg, val)] and the end time."""
    s = Game(); s.init(); s.log.clear(); s.cmd(cmd, arg)
    out = []; t = 0.0; fast = False; quiet = 0
    while t < secs:
        n = len(s.log)
        r = s.poll()
        if r == 0xFFFF: fast = False
        elif r != 0 and not (r & 0x8000): fast = True
        if fast: s.noise()
        out += [(int(t * OPL_RATE), a, b) for a, b in s.log]; s.log.clear()
        t += 1.0 / (frame_hz * (cls if fast else 1))
        quiet = quiet + 1 if len(out) == n and not fast else 0
        if quiet > 2 * frame_hz and not any(s.fb(v, 't') for v in s.VOICES): break
    return out, t


if __name__ == '__main__':
    if sys.argv[1] == 'steer':             # steer CMD [secs] -> work/sound/asound_cmdNN_steered.wav
        import subprocess
        c = int(sys.argv[2]); secs = float(sys.argv[3]) if len(sys.argv) > 3 else 90
        out, t = steered(c, secs)
        name = 'work/sound/asound_cmd%02d_steered' % c
        with open(name + '.opl', 'wb') as f:
            for tt, r, v in out: f.write(struct.pack('<IHB', tt, r, v))
        subprocess.check_call([os.path.abspath('work/oplrender.exe'), name + '.opl', name + '.wav'])
        os.remove(name + '.opl')
        print(name + '.wav', len(out), 'writes', '%.1f s' % (out[-1][0] / OPL_RATE if out else 0))
    elif sys.argv[1] == 'verify':
        polls = int(sys.argv[2]) if len(sys.argv) > 2 else 1500
        bad = 0
        for c in range(0, 40):
            a = [x for x in run_game(c, polls)[1] if x[1] != 4]; e = emu_game(c, polls)   # emulator hides reg 4
            if a != e:
                bad += 1
                i = next((k for k in range(min(len(a), len(e))) if a[k] != e[k]), min(len(a), len(e)))
                print('cmd %2d MISMATCH at %d: model %s  emu %s  (len %d vs %d)' % (c, i, a[i:i + 2], e[i:i + 2], len(a), len(e)))
            else:
                print('cmd %2d ok  %d writes' % (c, len(a)))
        print('mismatches:', bad)
    elif sys.argv[1] == 'verifylog':
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 1500
        a = run_logo(n); e = emu_logo(n)
        i = next((k for k in range(min(len(a), len(e))) if a[k] != e[k]), None)
        print('logo %d ticks: %d vs %d writes, %s' % (n, len(a), len(e), 'IDENTICAL' if a == e else 'MISMATCH at %s: %s / %s' % (i, a[i:i + 3] if i is not None else '', e[i:i + 3] if i is not None else '')))
    else:
        s, out = run_game(int(sys.argv[1]), int(sys.argv[2]) if len(sys.argv) > 2 else 600)
        print(len(out), 'writes')
