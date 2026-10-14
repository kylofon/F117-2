"""S5.2: reference vectors for the video module, computed by running the ORIGINAL MGRAPHIC.EXE under Unicorn.

Each scenario fills the pages with a deterministic pattern, calls driver slots through the harness (mg_harness.py) and hashes
what came out (the four pages, plus the structures and return values the call may change). The port's `f117 --video-selftest`
(f117port/src/video/videotest.c) runs the same scenarios against its own functions and compares the hashes bit for bit.
Writes f117port/src/video/video_vectors.h (numbers and strings only, no game data). Needs Game/MGRAPHIC.EXE.

Re-run: python port/notes/s52/video_emu.py
"""
import os, random, struct, sys, zlib

sys.path.insert(0, os.path.dirname(__file__))
from mg_harness import *  # noqa: E402,F401

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
OUT = os.path.join(ROOT, 'f117port', 'src', 'video', 'video_vectors.h')

OPNAMES = ['OP_LINE', 'OP_POLY', 'OP_BLITRECT', 'OP_RECOLOR', 'OP_BLITKEY', 'OP_TEXT', 'OP_FCW', 'OP_HUD', 'OP_SPRITE', 'OP_FX',
           'OP_MISC', 'OP_CONST', 'OP_GFXLINE', 'OP_PALW']
(OP_LINE, OP_POLY, OP_BLITRECT, OP_RECOLOR, OP_BLITKEY, OP_TEXT, OP_FCW, OP_HUD, OP_SPRITE, OP_FX, OP_MISC, OP_CONST,
 OP_GFXLINE, OP_PALW) = range(14)

# structure addresses in the emulated SS (the port uses the same offsets inside a scratch DOS block)
A_PAGEW, A_BLIT, A_TEXT, A_STR, A_WIN, A_SCAN, A_TBL = 0x100, 0x200, 0x300, 0x400, 0x600, 0x800, 0x1000

STRINGS = []   # byte strings (without the NUL), indexes used by OP_TEXT
SPRITES = []   # (grabs, draws) of OP_SPRITE
BLIT_CLIPS = [(0, 199, 0, 319), (20, 150, 30, 250), (50, 60, 100, 110), (0, 300, -5, 400), (100, 120, 200, 210), (0, 40, 280, 319),
              (170, 199, 0, 30), (30, 20, 30, 20)]


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def u16(v):
    return v & 0xFFFF


def setup(d, seed):
    for n in (1, 2, 3):
        d.call(57, [n, PAGE_SEG[n]])
    for n in range(4):
        d.w(d.page_seg(n), 0, lcg_fill(seed * 7 + n))


def crc_aux(c, data):
    return zlib.crc32(bytes(data), c) & 0xFFFFFFFF


# ---------------------------------------------------------------------------------------------------- operations
def op_line(d, a):
    x1, y1, x2, y2, col, page, off, seed = a[:8]
    setup(d, seed)
    d.call(14, [page]); d.call(26, [off]); d.call(33, [col])
    d.call(31, [], {UC_X86_REG_AX: u16(x1), UC_X86_REG_BX: u16(y1), UC_X86_REG_CX: u16(x2), UC_X86_REG_DX: u16(y2)})
    d.call(33, [col ^ 0x5A]); d.call(36)
    return d.crc_pages(), 0


class Lcg:
    """Small generator shared with videotest.c (the C side cannot reproduce Python's Mersenne twister)."""
    def __init__(self, s):
        self.s = s & 0xFFFFFFFF

    def rnd(self, n):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.s >> 16) % n


def make_edges(seed):
    g = Lcg(seed)
    t = bytearray(0x1B8 * 2)
    for y in range(220):
        L = g.rnd(340)
        k = g.rnd(20)
        if k == 0:
            R = L - (1 + g.rnd(4))
        elif k == 1:
            R = L
        elif k == 2:
            L = R = (0, 319)[g.rnd(2)]
        elif k == 3:
            L = g.rnd(4); R = 315 + g.rnd(10)
        else:
            R = L + g.rnd(90)
        struct.pack_into('<H', t, 2 * y, L & 0xFFFF)
        struct.pack_into('<H', t, 0x1B8 + 2 * y, R & 0xFFFF)
    return bytes(t)


def op_poly(d, a):
    ymin, ymax, mode, col, page, off, tseed, seed = a[:8]
    setup(d, seed)
    d.w(SEG_SS, A_TBL, make_edges(tseed))
    d.call(14, [page]); d.call(26, [off]); d.call(33, [col])
    regs = {UC_X86_REG_AX: u16(ymin), UC_X86_REG_BX: A_TBL, UC_X86_REG_CX: u16(ymax), UC_X86_REG_DX: u16(max(mode, 0))}
    d.call(40 if mode < 0 else 37, [], regs)
    return d.crc_pages(), 0


def op_blitrect(d, a):
    sp, sx, sy, dp, dx, dy, w, h, seed = a[:9]
    setup(d, seed)
    d.call(42, [sp, sx, sy, dp, dx, dy, w, h])
    return d.crc_pages(), 0


def op_recolor(d, a):
    page, x0, y0, x1, y1, old, new, seed = a[:8]
    setup(d, seed)
    d.w16(SEG_SS, A_PAGEW, page)
    d.call(41, [A_PAGEW, x0, y0, x1, y1, old, new])
    return d.crc_pages(), 0


def op_blitkey(d, a):
    clip, sp, sx, sy, dp, dx, dy, w, h, c0, _, seed = a[:12]
    setup(d, seed)
    cl = BLIT_CLIPS[c0]
    ctx = struct.pack('<Hhhhhhhhhhhh', d.page_seg(sp), sx, sy, dp, dx, dy, w, h, *cl)
    d.w(SEG_SS, A_BLIT, ctx)
    d.call(19 if clip else 17, [A_BLIT])
    return crc_aux(d.crc_pages(), d.r(SEG_SS, A_BLIT + 2, 22)), 0   # not the source segment: it differs between the two runs


def op_text(d, a):
    variant, font, flags, fg, bg, x, y, ymin, ymax, xmin, xmax, strid = a[:12]
    setup(d, strid + 11)
    s = STRINGS[strid]
    ctx = struct.pack('<hhhhhhhhhhh', 1, flags, fg, bg, x, y, font, ymin, ymax, xmin, xmax)
    # the fields are page, flags, fg, bg, x, y, font, ymin, ymax, xmin, xmax = 11 words
    d.w(SEG_SS, A_TEXT, ctx)
    d.w(SEG_SS, A_STR, s + b'\0')
    slot = (5, 7, 8, 9, 10)[variant]
    d.call(slot, [A_TEXT, A_STR])
    c = crc_aux(d.crc_pages(), d.r(SEG_SS, A_TEXT, 22))
    c = crc_aux(c, d.r(SEG_SS, A_STR, len(s) + 1))
    c = crc_aux(c, d.r(IMG, 0x1866, 1))
    return c, 0


def op_fcw(d, a):
    font = a[0]
    c = 0
    for ch in range(-3, 260):
        ax, _ = d.call(47, [ch & 0xFFFF, font])
        c = crc_aux(c, struct.pack('<h', s16(ax)))
    return c, 0


def op_hud(d, a):
    si, bx, cl, dl, col, page, seed = a[:7]
    setup(d, seed)
    d.call(14, [page]); d.call(33, [col])
    d.call(11, [], {UC_X86_REG_SI: si, UC_X86_REG_BX: u16(bx), UC_X86_REG_CX: cl, UC_X86_REG_DX: dl & 0xFF})
    return d.crc_pages(), 0


def sprite_size(blob):
    h = struct.unpack_from('<H', blob, 2)[0]
    n = 4
    for _ in range(h):
        n += 4 + struct.unpack_from('<H', blob, n)[0]
    return n


def op_sprite(d, a):
    seed, idx = a[0], a[1]
    grabs, draws = SPRITES[idx]
    setup(d, seed)
    base, _ = d.call(76)
    c = zlib.crc32(struct.pack('<H', 1 if base else 0))
    segs = []
    for (page, x, y, w, h) in grabs:
        seg, _ = d.call(77, [page & 0xFFFF, x & 0xFFFF, y & 0xFFFF, w, h])
        segs.append(seg)
        c = crc_aux(c, struct.pack('<H', (seg - base) & 0xFFFF if seg else 0xFFFF))
        if seg:
            n = sprite_size(d.r(seg, 0, 0x10000 - 16))
            c = crc_aux(c, d.r(seg, 0, n))
    for (si, wpage, ymin, ymax, xmin, xmax, x, y, dw, dh) in draws:
        d.w(SEG_SS, A_WIN, struct.pack('<hhhhhhhhhhh', wpage, 0, 0, 0, 0, 0, 0, ymin, ymax, xmin, xmax))
        ax, dx = d.call(22, [A_WIN, x & 0xFFFF, y & 0xFFFF, dw & 0xFFFF, dh & 0xFFFF, 0, segs[si]])
        c = crc_aux(c, d.r(dx, ax, 8))
    c = crc_aux(c, d.crc_pages().to_bytes(4, 'little'))
    freed, _ = d.call(78)
    c = crc_aux(c, struct.pack('<H', 1 if freed == base else 0))
    return c, 0


def op_fx(d, a):
    shake, n = a[:2]
    d.call(79, [shake])
    c = 0
    crtc = 0
    dac = (0, 0, 0)
    for _ in range(n):
        d.out_log.clear()
        d.call(46)
        log = d.out_log
        for i, (port, size, val) in enumerate(log):
            if port == 0x3C8 and val == 13:
                dac = tuple(v for p, s, v in log[i + 1:i + 4])
            if port == 0x3D4 and size == 2:
                assert (val & 0xFF) == 0x0D
                crtc = (val >> 8) & 0xFF
        c = crc_aux(c, bytes(dac) + bytes([crtc]))
    return c, 0


def op_misc(d, a):
    kind, p0, p1, p2, p3, seed = a[:6]
    setup(d, seed)
    if kind == 0:                                   # page_load(src page) into the draw page
        d.call(14, [p1]); d.call(48, [d.page_seg(p0)])
    elif kind == 1:                                 # flip(words) and the flag
        d.call(44, [], {UC_X86_REG_AX: p0})
        return crc_aux(d.crc_pages(), bytes([d.r(d.cs, 0x198, 1)[0]])), 0
    elif kind == 2:                                 # clear_screen
        d.call(43)
    elif kind == 3:                                 # scanline_put to ES:DI from a 320-byte buffer at BP
        d.w(SEG_SS, A_SCAN, lcg_fill(seed + 5, 320))
        d.call(51, [], {UC_X86_REG_ES: d.page_seg(p0), UC_X86_REG_DI: p1, UC_X86_REG_BP: A_SCAN})
    elif kind == 4:                                 # clear_es
        d.call(59, [], {UC_X86_REG_ES: d.page_seg(p0)})
    return d.crc_pages(), 0


def op_const(d, a):
    c = 0

    def put(ax):
        nonlocal c
        if os.environ.get('VEC_DBG'):
            print('aux16 %04X' % (ax & 0xFFFF))
        c = crc_aux(c, struct.pack('<H', ax & 0xFFFF))

    for slot, args in [(23, []), (28, []), (29, []), (30, []), (45, []), (49, []), (62, [5, 7]), (63, []), (66, []), (67, [])]:
        put(d.call(slot, args)[0])
    d.call(27, [], {UC_X86_REG_AX: 0x1234}); put(d.call(30)[0])
    d.call(25); put(d.call(30)[0])
    d.call(26, [0xABCD]); put(d.call(30)[0])
    for y in (0, 1, 100, 199):
        put(d.call(58, [], {UC_X86_REG_DI: y})[0])
    d.call(57, [2, 0x4321]); d.call(56, [], {UC_X86_REG_SI: 2}); put(d.es_out)
    d.call(75, [0x1357, 3]); d.call(56, [], {UC_X86_REG_SI: 3}); put(d.es_out)
    d.call(15, [], {UC_X86_REG_AX: 0x4444}); put(d.call(16)[0])
    d.call(12); put(d.call(16)[0])
    d.call(13, [], {UC_X86_REG_AX: 2}); put(d.call(16)[0])
    return c, 0


# ---------------------------------------------------------------------------------------------------- the programs' own library
class ProgImg:
    """The unpacked START.EXE (work/unpacked; python tools/unlzexe.py): its library routines gfx_line (0000:829A) and
    pal_write_vsync (0000:8378) are plain near procedures. DGROUP = A95h."""
    DSEG = 0xA95

    def __init__(self, name='START.EXE'):
        d = open(os.path.join(ROOT, 'work', 'unpacked', name), 'rb').read()
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x110000)
        uc.mem_write(0, d[struct.unpack_from('<H', d, 8)[0] * 16:])
        self.out_log = []
        uc.hook_add(UC_HOOK_INSN, lambda u, port, size, val, _: self.out_log.append((port, size, val)), None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INSN, lambda u, port, size, _: 0x09 if port == 0x3DA else 0xFF, None, 1, 0, UC_X86_INS_IN)

    def w(self, lin, data):
        self.uc.mem_write(lin, bytes(data))

    def r(self, lin, n):
        return bytes(self.uc.mem_read(lin, n))

    def call(self, addr, args=(), regs=None):
        uc = self.uc
        base = self.DSEG * 16
        sp = 0xF800
        for a in reversed(args):
            sp -= 2
            uc.mem_write(base + sp, (a & 0xFFFF).to_bytes(2, 'little'))
        sp -= 2
        uc.mem_write(base + sp, (0xFFFF).to_bytes(2, 'little'))
        for r_ in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            uc.reg_write(r_, self.DSEG)
        uc.reg_write(UC_X86_REG_CS, 0)
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_BP, 0)
        for r_, v in (regs or {}).items():
            uc.reg_write(r_, v)
        uc.emu_start(addr, 0xFFFF, count=20000000)


A_PROG_PAGEW, A_PROG_PAL = 0xE000, 0xD000      # DS offsets of the page-number word and the palette data in the Unicorn run and in the port


def op_gfxline(a):
    page, x1, y1, x2, y2, col, seed = a[:7]
    p = ProgImg()
    base = p.DSEG * 16
    segs = [A000, PAGE_SEG[1], PAGE_SEG[2], PAGE_SEG[3]]
    for n in range(4):
        p.w(segs[n] * 16, lcg_fill(seed * 7 + n))
        p.w(segs[n] * 16 + 64000, bytes(65536 - 64000))
        p.w(base + 0xCAB0 + 2 * n, struct.pack('<H', segs[n]))
    p.w(base + A_PROG_PAGEW, struct.pack('<H', page))
    p.call(0x829A, [A_PROG_PAGEW, u16(x1), u16(y1), u16(x2), u16(y2), col])
    c = 0
    for n in range(4):
        c = crc_aux(c, p.r(segs[n] * 16, 64000))
    return c, 0


def op_palw(a):
    first, count, grey, seed = a[:4]
    p = ProgImg()
    base = p.DSEG * 16
    rgb = bytes(b & 0x3F for b in lcg_fill(seed, 768))
    p.w(base + A_PROG_PAL, rgb)
    p.w(0x489, bytes([2 if grey else 0]))
    p.call(0x8378, [], {UC_X86_REG_DX: A_PROG_PAL, UC_X86_REG_BX: first, UC_X86_REG_CX: count})
    dac = [[0, 0, 0] for _ in range(256)]
    idx, sub, cur = 0, 0, [0, 0, 0]
    # 4 port writes per entry (index, r, g, b). The grey-scale path returns with an unbalanced stack (no `pop bp`) and runs on
    # into garbage in the original; only the entries it wrote before are compared.
    for port, size, val in p.out_log[:4 * count]:
        if port == 0x3C8:
            idx, sub = val & 0xFF, 0
        elif port == 0x3C9:
            cur[sub] = val & 0x3F
            sub += 1
            if sub == 3:
                dac[idx] = list(cur)
                idx = (idx + 1) & 0xFF
                sub = 0
    return crc_aux(0, bytes(v for e in dac for v in e)), 0


OPS = {OP_LINE: op_line, OP_POLY: op_poly, OP_BLITRECT: op_blitrect, OP_RECOLOR: op_recolor, OP_BLITKEY: op_blitkey,
       OP_TEXT: op_text, OP_FCW: op_fcw, OP_HUD: op_hud, OP_SPRITE: op_sprite, OP_FX: op_fx, OP_MISC: op_misc, OP_CONST: op_const}
PROG_OPS = {OP_GFXLINE: op_gfxline, OP_PALW: op_palw}


# ---------------------------------------------------------------------------------------------------- scenario lists
def build():
    rnd = random.Random(52)
    sc = []

    def add(op, *a):
        sc.append((op, list(a) + [0] * (12 - len(a))))

    # lines: random, axis-parallel, diagonals, points, reversed, out of the screen (wraps), draw offsets
    for i in range(70):
        x1, y1, x2, y2 = rnd.randrange(0, 320), rnd.randrange(0, 200), rnd.randrange(0, 320), rnd.randrange(0, 200)
        if i % 9 == 0: y2 = y1
        if i % 9 == 1: x2 = x1
        if i % 9 == 2: x2, y2 = x1, y1
        if i % 9 == 3: x2, y2 = x1 + 37, y1 + 37
        if i % 9 == 4: x2, y2 = x1 + 20, y1 - 5
        off = rnd.choice((0, 0, 0x1450, 0x5580, 7, 0xFF00))
        add(OP_LINE, x1, y1, x2, y2, rnd.randrange(1, 256), rnd.choice((1, 2, 3, 0)), off, i + 1)
    for (x1, y1, x2, y2) in ((-10, 5, 30, 40), (300, 3, 330, 20), (5, 195, 340, 199), (319, 0, 0, 199), (0, 0, 319, 199)):
        add(OP_LINE, x1, y1, x2, y2, 77, 1, 0, 99)
    # polygons: all modes, ranges, draw offsets
    for i in range(48):
        mode = (-1, 0, 1, 2, 3, 4, 7)[i % 7]
        ymin = rnd.randrange(0, 150)
        ymax = min(ymin + rnd.randrange(0, 60), 199)
        add(OP_POLY, ymin, ymax, mode, rnd.randrange(1, 256), rnd.choice((1, 2, 3, 0)), rnd.choice((0, 0, 0x5580, 0x1450, 3)), 1000 + i, i + 3)
    add(OP_POLY, -1, 10, 0, 5, 1, 0, 5, 5)           # ymin < 0: nothing
    add(OP_POLY, 20, 10, 0, 5, 1, 0, 5, 5)           # ymax < ymin
    # rectangle copy / recolour
    for i in range(40):
        w, h = rnd.randrange(0, 120), rnd.randrange(1, 80)
        sp, dp = rnd.choice(((1, 2), (2, 1), (1, 0), (0, 3), (3, 3), (2, 2), (1, 1)))
        add(OP_BLITRECT, sp, rnd.randrange(0, 320 - w + 1), rnd.randrange(0, 200 - h + 1), dp, rnd.randrange(0, 320 - w + 1),
            rnd.randrange(0, 200 - h + 1), w, h, i + 7)
    for i in range(14):
        seed = i + 21
        x0, y0 = rnd.randrange(0, 250), rnd.randrange(0, 150)
        x1, y1 = x0 + rnd.randrange(0, 60), y0 + rnd.randrange(0, 40)
        pg = rnd.choice((1, 2, 3, 0))
        pix = lcg_fill(seed * 7 + pg)
        old = pix[y0 * 320 + x0 + 3] or pix[y0 * 320 + x0 + 5] or 9
        add(OP_RECOLOR, pg, x0, y0, x1, y1, old, rnd.randrange(1, 256), seed)
    # key-colour blits, plain and clipped
    for i in range(60):
        clip = i % 2
        w, h = rnd.randrange(1, 100), rnd.randrange(1, 60)
        sp, dp = rnd.choice(((1, 2), (2, 1), (3, 0), (0, 1), (2, 3)))
        sx, sy = rnd.randrange(0, 320 - w + 1), rnd.randrange(0, 200 - h + 1)
        if clip:
            dx, dy = rnd.randrange(-40, 330), rnd.randrange(-30, 200 - h + 1)    # rows 0..199 only (cs:0004 holds the row table)
            add(OP_BLITKEY, 1, sp, sx, sy, dp, dx, dy, w, h, (i // 2) % len(BLIT_CLIPS), 0, i + 13)
        else:
            add(OP_BLITKEY, 0, sp, sx, sy, dp, rnd.randrange(0, 320 - w + 1), rnd.randrange(0, 200 - h + 1), w, h, 0, 0, i + 13)
    # HUD tick strips
    for i in range(24):
        add(OP_HUD, (0, 2)[i % 2], rnd.randrange(0, 110), (0, 1)[(i // 2) % 2], rnd.choice((-1, 0, 1, 5)), rnd.randrange(1, 256), rnd.choice((1, 2)), i + 31)
    # constants / simple getters, effects, misc page ops
    add(OP_CONST)
    for shake, n in ((0, 6), (3, 8), (40, 12), (1, 3)):
        add(OP_FX, shake, n)
    add(OP_MISC, 0, 1, 2, 0, 0, 3)
    add(OP_MISC, 0, 3, 0, 0, 0, 4)
    add(OP_MISC, 1, 0x7D00, 0, 0, 0, 5)
    add(OP_MISC, 1, 0x1450, 0, 0, 0, 6)
    add(OP_MISC, 2, 0, 0, 0, 0, 7)
    add(OP_MISC, 3, 1, 1280, 0, 0, 8)
    add(OP_MISC, 3, 0, 320 * 199, 0, 0, 9)
    add(OP_MISC, 4, 2, 0, 0, 0, 10)
    # sprites
    for i in range(22):
        grabs, draws = [], []
        for g in range(rnd.choice((1, 1, 2))):
            grabs.append((rnd.choice((1, 2, 3, 0, -1)), rnd.randrange(0, 200), rnd.randrange(0, 120), rnd.randrange(1, 100), rnd.randrange(1, 70)))
        for k in range(rnd.randrange(1, 4)):
            ymin, xmin = rnd.randrange(0, 60), rnd.randrange(0, 80)
            ymax, xmax = ymin + rnd.randrange(30, 140), xmin + rnd.randrange(40, 230)
            dw = rnd.choice((1, 1, 1, -1, 2, -2, 3)) * rnd.randrange(5, 160) // rnd.choice((1, 1, 2, 3))
            dh = rnd.choice((1, 1, -1)) * rnd.randrange(5, 110)
            draws.append((rnd.randrange(len(grabs)), rnd.choice((1, 2, 3, 0)), ymin, ymax, xmin, xmax,
                          rnd.randrange(-50, xmax - xmin + 1), rnd.randrange(-40, ymax - ymin + 1), dw or 7, dh or 7))
        SPRITES.append((grabs, draws))
        add(OP_SPRITE, 300 + i, len(SPRITES) - 1)
    SPRITES.append(([(1, 0, 0, 0, 10), (2, 0, 0, 10, 0), (3, 5, 5, 8, 8)],
                    [(2, 1, 0, 199, 0, 319, 10, 10, 16, 16), (2, 1, 0, 199, 0, 319, 50, 50, 80, 4), (0, 1, 0, 199, 0, 319, 0, 0, 5, 5)]))
    add(OP_SPRITE, 330, len(SPRITES) - 1)           # empty sprites
    big = [(1, 0, 0, 320, 100), (2, 0, 0, 320, 100), (3, 0, 0, 320, 100), (1, 0, 100, 320, 100), (2, 0, 100, 300, 100)]
    SPRITES.append((big, [(0, 1, 0, 199, 0, 319, 0, 0, 160, 100), (4, 1, 0, 199, 0, 319, 0, 0, 160, 100), (3, 1, 0, 199, 0, 319, 100, 20, 100, 70)]))
    add(OP_SPRITE, 331, len(SPRITES) - 1)           # the pool runs out
    # text: strings, fonts, variants, clip windows
    STRINGS.extend([b'F-117A Nighthawk', b'Hello, World 0123456789', b'AaBbCcDdEeFf \x8Cred\x8Eyellow\x8Agreen end',
                    b'\x81\x82x', b'W', b'iiiiillllMMMMMWWWW', b'A' * 60, b'~}|{ zyx [] ()', b'\x01\x02\x7f\x7e ctrl', b'low \x90 colour \x9f z',
                    b' leading and trailing  ', b'Mixed: CAPS lower 123 !@#$%^&*'])
    for i in range(160):
        variant = i % 5
        font = rnd.choice((0, 1, 2, 3, 4, 5, 6, 6, 0, 1, 7)) if i % 23 else 8
        flags = rnd.choice((0, 0, 1, 1, 2, 3, 0x100, 0x101))
        x, y = rnd.randrange(-20, 300), rnd.randrange(0, 195)
        ymin = rnd.randrange(0, 80); ymax = ymin + rnd.randrange(0, 120)
        xmin = rnd.randrange(0, 150); xmax = xmin + rnd.randrange(-5, 170)
        if i % 4 == 0: x = rnd.randrange(xmin - 15, xmin + 25)
        if i % 4 == 1: x = rnd.randrange(xmax - 60, xmax + 5)
        add(OP_TEXT, variant, font, flags, rnd.randrange(1, 256), rnd.randrange(0, 256), x, y, ymin, ymax, xmin, xmax,
            rnd.randrange(len(STRINGS)))
    for f in range(0, 10):
        add(OP_FCW, f)
    # the programs' own gfx_line (clips per pixel: x 0..319, y 0..167) and pal_write_vsync (plain and grey-scale paths)
    for i in range(48):
        x1, y1, x2, y2 = rnd.randrange(-30, 350), rnd.randrange(-20, 190), rnd.randrange(-30, 350), rnd.randrange(-20, 190)
        if i % 8 == 0: y2 = y1
        if i % 8 == 1: x2 = x1
        if i % 8 == 2: x2, y2 = x1, y1
        add(OP_GFXLINE, rnd.choice((0, 1, 2, 3)), x1, y1, x2, y2, rnd.randrange(1, 256), i + 3)
    for i in range(14):
        add(OP_PALW, rnd.randrange(0, 256), rnd.randrange(1, 257 - 0) if i % 3 else 256, i % 2, 700 + i)
    return sc


def main():
    sc = build()
    rows = []
    stat = {}
    for n, (op, a) in enumerate(sc):
        if op in PROG_OPS:
            crc, ret = PROG_OPS[op](a)
        else:
            d = Driver()
            crc, ret = OPS[op](d, a)
        rows.append((op, a, crc, ret))
        stat[op] = stat.get(op, 0) + 1
        if n % 100 == 0:
            print(n, len(sc), file=sys.stderr)
    out = ['/* Generated by port/notes/s52/video_emu.py from the original MGRAPHIC.EXE under Unicorn. Do not edit. */',
           'typedef struct { unsigned char op; short a[12]; unsigned crc; int ret; } VideoVec;',
           'enum { ' + ', '.join('%s = %d' % (OPNAMES[i], i) for i in range(len(OPNAMES))) + ' };',
           'static const unsigned char vec_str[][64] = {']
    for s in STRINGS:
        out.append('    { %s },' % ', '.join(str(b) for b in s + b'\0'))
    out.append('};')
    out.append('static const int vec_str_len[] = { %s };' % ', '.join(str(len(s)) for s in STRINGS))
    out.append('typedef struct { short page, x, y, w, h; } VecGrab;')
    out.append('typedef struct { short idx, wpage, ymin, ymax, xmin, xmax, x, y, dw, dh; } VecDraw;')
    for i, (g, dr) in enumerate(SPRITES):
        out.append('static const VecGrab vec_grab%d[] = { %s };' % (i, ', '.join('{%d,%d,%d,%d,%d}' % t for t in g)))
        out.append('static const VecDraw vec_draw%d[] = { %s };' % (i, ', '.join('{%s}' % ','.join(map(str, t)) for t in dr)))
    out.append('typedef struct { const VecGrab *g; int ng; const VecDraw *d; int nd; } VecSprite;')
    out.append('static const VecSprite vec_sprites[] = {')
    for i, (g, dr) in enumerate(SPRITES):
        out.append('    { vec_grab%d, %d, vec_draw%d, %d },' % (i, len(g), i, len(dr)))
    out.append('};')
    out.append('static const short vec_blit_clip[][4] = { %s };' % ', '.join('{%d,%d,%d,%d}' % c for c in BLIT_CLIPS))
    out.append('static const VideoVec video_vec[] = {')
    for op, a, crc, ret in rows:
        out.append('    { %s, { %s }, 0x%08XU, %d },' % (OPNAMES[op], ', '.join(map(str, a)), crc, ret))
    out.append('};')
    open(OUT, 'w').write('\n'.join(out) + '\n')
    print('video_emu: %d scenarios -> %s %s' % (len(rows), OUT, {OPNAMES[k]: v for k, v in stat.items()}))


if __name__ == '__main__':
    main()
