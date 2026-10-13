"""R7c differential tests: real VGAME code (Unicorn) vs R7c_ref.py.
usage: python port/notes/s37/R7c_emu.py [ncases]   (prints one summary line per test)"""
import random
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR
from unicorn.x86_const import *

sys.path.insert(0, 'C:/Coding/F117-2/port/notes/s37')
import R7c_ref as ref  # noqa: E402

IMG = ref.IMG
DSEG = 0x1E23
DSB = DSEG * 16
RETL = 0xF0000
SLOTBASE = 0x11A


class Emu:
    def __init__(s):
        s.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x110000)
        uc.mem_write(0, IMG)
        uc.mem_write(RETL, b'\xf4')
        uc.hook_add(UC_HOOK_CODE, lambda u, a, z, d: u.emu_stop(), begin=RETL, end=RETL)
        uc.hook_add(UC_HOOK_INTR, s.hk_intr)
        s.log = []
        s.ints = 0
        s.snap = False
        for slot in (13, 15, 16, 31, 32, 37, 40):
            a = DSB + SLOTBASE + 5 * slot
            uc.mem_write(a, b'\xcb')
            uc.hook_add(UC_HOOK_CODE, s.mk_slot(slot), begin=a, end=a)
        # #DE: Unicorn turns a second hooked #DE into int 8, so divide overflow is detected before the idiv
        # executes and dispatched through the IVT exactly like a 286+ CPU (saved IP = the idiv itself)
        for off, dvo in ((0x043C, 0x228B), (0x0474, 0x228D), (0x0796, 0x261B), (0x07CE, 0x261D)):
            a = 0x10010 + off
            uc.hook_add(UC_HOOK_CODE, s.mk_div(dvo), begin=a, end=a)
        s.base = bytes(uc.mem_read(DSB, 0x10000))

    def mk_slot(s, slot):
        def hk(uc, addr, size, ud):
            r = lambda R: uc.reg_read(R)
            if slot == 32:
                s.log.append((32, r(UC_X86_REG_AX) >> 8))
            elif slot == 31:
                s.log.append((31, r(UC_X86_REG_AX), r(UC_X86_REG_BX), r(UC_X86_REG_CX), r(UC_X86_REG_DX)))
            elif slot == 37:
                e = (37, r(UC_X86_REG_AX), r(UC_X86_REG_CX), r(UC_X86_REG_DX))
                if s.snap:
                    e = e + (bytes(uc.mem_read(DSB + 0x22A1, 0x370)),)
                s.log.append(e)
            else:
                s.log.append((slot, r(UC_X86_REG_AX)))
        return hk

    def mk_div(s, dvo):
        def hk(uc, addr, size, ud):
            d = ref.s16(s.r(dvo))
            n = ref.s32(uc.reg_read(UC_X86_REG_DX) << 16 | uc.reg_read(UC_X86_REG_AX))
            if d == 0 or not -32768 <= int(n / d) <= 32767:
                s.hk_intr(uc, 0, None)
        return hk

    def hk_intr(s, uc, intno, ud):
        if intno != 0:
            raise RuntimeError('int %d' % intno)
        s.ints += 1
        # real-mode dispatch through the IVT (286+ semantics: saved IP = faulting instruction)
        ip, cs = uc.reg_read(UC_X86_REG_IP), uc.reg_read(UC_X86_REG_CS)
        fl = uc.reg_read(UC_X86_REG_EFLAGS) & 0xFFFF
        sp = uc.reg_read(UC_X86_REG_SP)
        ss = uc.reg_read(UC_X86_REG_SS) * 16
        sp -= 6
        uc.mem_write(ss + sp, struct.pack('<HHH', ip, cs, fl))
        uc.reg_write(UC_X86_REG_SP, sp)
        off, seg = struct.unpack('<HH', uc.mem_read(0, 4))
        uc.reg_write(UC_X86_REG_CS, seg)
        uc.reg_write(UC_X86_REG_IP, off)

    def w(s, o, v):
        s.uc.mem_write(DSB + (o & 0xFFFF), struct.pack('<H', v & 0xFFFF))

    def wb(s, o, v):
        s.uc.mem_write(DSB + o, bytes([v & 0xFF]))

    def r(s, o):
        return struct.unpack('<H', s.uc.mem_read(DSB + o, 2))[0]

    def ds(s):
        return bytearray(s.uc.mem_read(DSB, 0x10000))

    def set_ds(s, M):
        s.uc.mem_write(DSB, bytes(M))

    def call(s, seg, off, args=(), near=False, regs=None):
        uc = s.uc
        for R in (UC_X86_REG_DS, UC_X86_REG_SS, UC_X86_REG_ES):
            uc.reg_write(R, DSEG)
        sp = 0xF800
        for a in reversed(args):
            sp -= 2
            uc.mem_write(DSB + sp, struct.pack('<H', a & 0xFFFF))
        if near:
            # near return into segment `seg`: put a far jump to RETL at seg:FFF0 is not possible; use RETL-relative
            ret_off = 0xFFF0
            stop = seg * 16 + ret_off
            sp -= 2
            uc.mem_write(DSB + sp, struct.pack('<H', ret_off))
        else:
            stop = RETL
            sp -= 4
            uc.mem_write(DSB + sp, struct.pack('<HH', 0, RETL >> 4))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_BP, 0x1234)
        for k, v in (regs or {}).items():
            uc.reg_write(k, v)
        uc.reg_write(UC_X86_REG_CS, seg)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x0002)
        s.log = []
        h = None
        if stop != RETL:
            h = uc.hook_add(UC_HOOK_CODE, lambda u, a, z, d: u.emu_stop(), begin=stop, end=stop)
        uc.emu_start(seg * 16 + off, RETL + 1, count=20000000)
        if h is not None:
            uc.hook_del(h)
        cs, ip = uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)
        if cs * 16 + ip != stop:
            raise RuntimeError('did not return: %04x:%04x' % (cs, ip))
        return uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX), uc.reg_read(UC_X86_REG_EFLAGS)


def rnd16(r):
    k = r.random()
    if k < 0.15:
        return r.choice([0, 1, 2, 0xFFFF, 0xFFFE, 0x7FFF, 0x8000, 0x8001, 0x4000, 0xC000, 0x3FFF, 0x4001, 0x2000, 0x7FFE])
    if k < 0.35:
        return r.randint(-300, 300) & 0xFFFF
    return r.randint(0, 0xFFFF)


def report(name, n, bad, extra=''):
    print('%-22s cases %6d  mismatches %d %s' % (name, n, bad, extra))


def t_unary(e, name, off, fn, exhaustive=True):
    bad = 0
    vals = range(0x10000) if exhaustive else []
    for a in vals:
        ax, dx, _ = e.call(0x1433, off, (a,))
        if ax != fn(a):
            bad += 1
            if bad < 4:
                print('  %s(%04x): emu %04x ref %04x' % (name, a, ax, fn(a)))
    report(name, len(vals), bad)


def t_binary(e, name, off, fn, n, r, cmp_dx=False):
    bad = 0
    for _ in range(n):
        a, b = rnd16(r), rnd16(r)
        ax, dx, _ = e.call(0x1433, off, (a, b))
        exp = fn(a, b)
        got = (ax, dx) if cmp_dx else ax
        if got != exp:
            bad += 1
            if bad < 4:
                print('  %s(%04x,%04x): emu %s ref %s' % (name, a, b, got, exp))
    report(name, n, bad)


def test_math(e, n, r):
    t_unary(e, 'sin 0004', 0x0004, ref.sin_)
    t_unary(e, 'cos 0031', 0x0031, ref.cos_)
    t_unary(e, 'asin 015E', 0x015E, ref.asin_)
    # cos == sin(a+0x4000)
    print('  cos(a)==sin(a+4000h) for all a:', all(ref.cos_(a) == ref.sin_(a + 0x4000) for a in range(0x10000)))
    t_binary(e, 'mul_sin 00D9', 0x00D9, ref.mul_sin, n, r)
    t_binary(e, 'mul_cos 011A', 0x011A, ref.mul_cos, n, r)
    t_binary(e, 'atan2 0061', 0x0061, ref.atan2_, n, r)
    t_binary(e, 'hypot2 018E', 0x018E, ref.hypot2, n, r)
    t_binary(e, 'mulq 0542', 0x0542, ref.mulq, n, r, cmp_dx=True)
    t_binary(e, 'imul 05C6', 0x05C6, ref.imul_, n, r, cmp_dx=True)
    bad = 0
    for _ in range(n):
        a, b, c = rnd16(r), rnd16(r), rnd16(r)
        ax, dx, _ = e.call(0x1433, 0x01E0, (a, b, c))
        if ax != ref.hypot3(a, b, c):
            bad += 1
    report('hypot3 01E0', n, bad)
    # muldiv / div32sat: skip cases where the ref predicts #DE
    for name, off, fn in (('muldiv 0554', 0x0554, ref.muldiv), ('div32sat 058F', 0x058F, ref.div32sat)):
        bad = de = 0
        for _ in range(n):
            a, b, c = rnd16(r), rnd16(r), rnd16(r)
            exp = fn(a, b, c)
            if exp is None:
                de += 1
                continue
            ax, dx, _ = e.call(0x1433, off, (a, b, c))
            if (ax, dx) != exp:
                bad += 1
                if bad < 4:
                    print('  %s(%04x,%04x,%04x): emu %04x %04x ref %s' % (name, a, b, c, ax, dx, exp))
        report(name, n - de, bad, '(%d #DE cases skipped)' % de)
    # matrices
    A, B, C, V, O = 0xA000, 0xA020, 0xA040, 0xA060, 0xA080
    bad = [0] * 8
    for _ in range(n):
        M = [rnd16(r) for _ in range(9)]
        N = [rnd16(r) for _ in range(9)]
        v = [rnd16(r) for _ in range(3)]
        for k in range(9):
            e.w(A + 2 * k, M[k])
            e.w(B + 2 * k, N[k])
        for k in range(3):
            e.w(V + 2 * k, v[k])
        e.call(0x1433, 0x0219, (V, A))
        if [e.r(V + 2 * k) for k in range(3)] != ref.mat_vec16(v, M):
            bad[0] += 1
        e.call(0x1433, 0x032E, (V, A, O))
        vv = [e.r(V + 2 * k) for k in range(3)]
        got = [e.r(O + 4 * k) | e.r(O + 4 * k + 2) << 16 for k in range(3)]
        if got != ref.vec_mat32(vv, M):
            bad[1] += 1
        # mat_mul, including aliasing C==A and C==B
        for dst in (C, A, B):
            for k in range(9):
                e.w(A + 2 * k, M[k])
                e.w(B + 2 * k, N[k])
            mem = e.ds()
            ref.mat_mul_mem(mem, A, B, dst)
            e.call(0x1433, 0x0286, (A, B, dst))
            g = e.ds()
            if g[0x9160:0x9180] != mem[0x9160:0x9180] or g[A:A + 0x60] != mem[A:A + 0x60]:
                bad[2] += 1
        for dst in (C, A):
            for k in range(9):
                e.w(A + 2 * k, M[k])
            mem = e.ds()
            ref.transpose_mem(mem, A, dst)
            e.call(0x1433, 0x02E2, (A, dst))
            if e.ds()[A:A + 0x60] != mem[A:A + 0x60]:
                bad[3] += 1
        for k in range(9):
            e.w(B + 2 * k, N[k])
        e.call(0x1433, 0x0314, (B, C))
        if [e.r(C + 2 * k) for k in range(9)] != N:
            bad[4] += 1
        a, b, c = rnd16(r), rnd16(r), rnd16(r)
        e.call(0x1433, 0x03A9, (a, b, c, O))
        m, cache = ref.mat_from_euler(a, b, c)
        if [e.r(O + 2 * k) for k in range(9)] != m or tuple(e.r(0x916C + 2 * k) for k in range(6)) != cache:
            bad[5] += 1
        e.call(0x1433, 0x05D1, (a, b, c, O))
        m2, cache2 = ref.mat_from_euler_b(a, b, c)
        if [e.r(O + 2 * k) for k in range(9)] != m2 or tuple(e.r(0x916C + 2 * k) for k in range(6)) != cache2:
            bad[6] += 1
        # mat_to_euler on a random matrix and on the euler matrix
        for MM in (M, m):
            for k in range(9):
                e.w(A + 2 * k, MM[k])
            e.call(0x1433, 0x04D7, (A, V, V + 2, V + 4))
            if tuple(e.r(V + 2 * k) for k in range(3)) != ref.mat_to_euler(MM):
                bad[7] += 1
    for nm, b in zip(('mat_vec16 0219', 'vec_mat32 032E', 'mat_mul 0286 (x3 alias)', 'transpose 02E2 (x2)',
                      'mat_copy 0314', 'mat_from_euler 03A9', 'mat_from_euler_b 05D1', 'mat_to_euler 04D7 (x2)'), bad):
        report(nm, n, b)


# ---------------------------------------------------------------- 1001
def rnd_coord(r, wide):
    k = r.random()
    if k < 0.5:
        return r.randint(-60, wide + 60)
    if k < 0.75:
        return r.randint(-2000, 2000)
    if k < 0.85:
        return r.choice([0, wide, -1, wide + 1, 0x7FFF, -0x8000, 0x7FFE, -0x7FFF, 32000, -32000])
    return r.randint(-0x8000, 0x7FFF)


WINDOWS = [(319, 199), (319, 139), (0x20, 0x1C), (159, 99), (219, 219), (1, 1)]


def fresh_tables(M, r):
    """random but consistent table state (either cleared, or a dirty range filled with random spans)"""
    if r.random() < 0.5:
        for y in range(220):
            struct.pack_into('<H', M, 0x22A1 + 2 * y, 0xFFFF)
            struct.pack_into('<H', M, 0x2459 + 2 * y, 0)
        struct.pack_into('<HH', M, 0x2615, 0xFFFF, 0)
    else:
        ymax = struct.unpack_from('<H', M, 0x2295)[0]
        a = r.randint(0, ymax)
        b = r.randint(a, ymax)
        for y in range(220):
            if a <= y <= b:
                struct.pack_into('<HH', M, 0x22A1 + 2 * y, r.randint(0, 400), 0)
                struct.pack_into('<H', M, 0x2459 + 2 * y, r.randint(0, 400))
            else:
                struct.pack_into('<H', M, 0x22A1 + 2 * y, 0xFFFF)
                struct.pack_into('<H', M, 0x2459 + 2 * y, 0)
        struct.pack_into('<HH', M, 0x2615, a, b)


def cmp_ds(g, m, ranges):
    for lo, hi in ranges:
        if g[lo:hi] != m[lo:hi]:
            for k in range(lo, hi):
                if g[k] != m[k]:
                    return k
    return None


R1001 = [(0x228A, 0x22A1), (0x22A1, 0x2611 + 4), (0x2615, 0x2622), (0x0D42, 0x0D4A)]


def test_1001(e, n, r):
    st = {}
    for name, off, near in (('line_clip_draw 032A', 0x032A, False), ('edge_to_tables 06A0', 0x06A0, False),
                            ('edge_walk 05B9', 0x05B9, True)):
        bad = clob = 0
        ints0 = e.ints
        for i in range(n):
            M = bytearray(e.base)
            xm, ym = r.choice(WINDOWS)
            struct.pack_into('<HH', M, 0x2293, xm, ym)
            fresh_tables(M, r)
            if near:   # walk wants clipped end points
                pts = [r.randint(0, xm), r.randint(0, ym), r.randint(0, xm), r.randint(0, ym)]
            else:
                pts = [rnd_coord(r, xm), rnd_coord(r, ym), rnd_coord(r, xm), rnd_coord(r, ym)]
            for o, v in zip((0x2299, 0x229D, 0x229B, 0x229F), pts):
                struct.pack_into('<H', M, o, v & 0xFFFF)
            RM = ref.Mem(bytearray(M))
            if off == 0x032A:
                cf = ref.line_clip_draw(RM)
            elif off == 0x06A0:
                ref.edge_to_tables(RM)
            else:
                ref.edge_walk(RM)
            if RM.clobber:
                clob += 1
                continue
            e.set_ds(M)
            ax, dx, fl = e.call(0x1001, off, near=near)
            g = e.ds()
            k = cmp_ds(g, RM.m, R1001 + [(0x2293, 0x22A1)])
            logok = [x for x in e.log] == RM.log
            cfok = (off != 0x032A) or ((fl & 1) == cf)
            if k is not None or not logok or not cfok:
                bad += 1
                if bad < 4:
                    print('  %s win=%s pts=%s first diff DS:%s emu %s ref %s cf %s' % (
                        name, (xm, ym), pts, None if k is None else hex(k), e.log, RM.log, cfok))
        report(name, n - clob, bad, '(%d INT0 overflows hit, %d ref-clobber cases skipped)' % (e.ints - ints0, clob))
    # poly_fill 01EC with random polygons, and hz_draw via 0006
    e.snap = True
    bad = clob = 0
    ints0 = e.ints
    PTS = 0xA100
    for i in range(n // 4):
        M = bytearray(e.base)
        xm, ym = r.choice(WINDOWS[:4])
        struct.pack_into('<HH', M, 0x2293, xm, ym)
        fresh_tables(M, r)
        nv = r.choice([3, 4, 4, 5, 6])
        pts = [(rnd_coord(r, xm) & 0xFFFF, rnd_coord(r, ym) & 0xFFFF) for _ in range(nv)]
        for k, (x, y) in enumerate(pts):
            struct.pack_into('<HH', M, PTS + 4 * k, x, y)
        col = r.randint(0, 255)
        RM = ref.Mem(bytearray(M))
        RM.snap = True
        ref.poly_fill(RM, nv, pts, col)
        if RM.clobber:
            clob += 1
            continue
        e.set_ds(M)
        e.call(0x1001, 0x01EC, (0x032E, nv, PTS, col))
        g = e.ds()
        k = cmp_ds(g, RM.m, R1001)
        if k is not None or e.log != RM.log:
            bad += 1
            if bad < 4:
                print('  poly_fill win=%s pts=%s diff %s' % ((xm, ym), pts, None if k is None else hex(k)))
    report('poly_fill 01EC', n // 4 - clob, bad, '(%d INT0, %d clobber skipped)' % (e.ints - ints0, clob))
    bad = clob = 0
    for i in range(n // 4):
        M = bytearray(e.base)
        xm, ym = r.choice(WINDOWS[:4])
        struct.pack_into('<HH', M, 0x2293, xm, ym)
        struct.pack_into('<HH', M, 0xD1E, (xm + 1) // 2, (ym + 1) // 2)
        fresh_tables(M, r)
        a, b, c = rnd16(r), rnd16(r), rnd16(r)
        for o, v in zip((0x4986, 0x4988, 0x498A, 0x498C), (ref.sin_(b), ref.cos_(b), ref.sin_(c), ref.cos_(c))):
            struct.pack_into('<H', M, o, v)
        M[0x433A] = r.choice([0, 0, 1, 2])
        M[0x294B] = r.choice([0, 1])
        col = r.randint(0, 255)
        RM = ref.Mem(bytearray(M))
        RM.m[0xD46] = col
        RM.snap = True
        ref.hz_draw(RM)
        if RM.clobber:
            clob += 1
            continue
        e.set_ds(M)
        e.call(0x1001, 0x0006, (col,))
        g = e.ds()
        k = cmp_ds(g, RM.m, R1001)
        if k is not None or e.log != RM.log:
            bad += 1
            if bad < 4:
                print('  hz_draw b=%04x c=%04x win=%s diff %s\n   emu %s\n   ref %s' % (
                    b, c, (xm, ym), None if k is None else hex(k), [x[:4] for x in e.log], [x[:4] for x in RM.log]))
    report('hz_draw via 0006', n // 4 - clob, bad, '(%d clobber skipped)' % clob)
    e.snap = False


def test_1442(e, n, r):
    """hook 1001:01EC / 1001:0006 (log args, retf) and run 0000:1442"""
    calls = []

    def hk_poly(uc, a, z, d):
        sp = uc.reg_read(UC_X86_REG_SP)
        ip, cs, ctx, nv, pts, col = struct.unpack('<6H', uc.mem_read(DSB + sp, 12))
        P = struct.unpack('<%dH' % (2 * nv), uc.mem_read(DSB + pts, 4 * nv))
        calls.append(('poly', col, [(P[2 * k], P[2 * k + 1]) for k in range(nv)]))
        uc.reg_write(UC_X86_REG_SP, sp + 4)
        uc.reg_write(UC_X86_REG_CS, cs)
        uc.reg_write(UC_X86_REG_IP, ip)

    def hk_fill(uc, a, z, d):
        sp = uc.reg_read(UC_X86_REG_SP)
        ip, cs, col = struct.unpack('<3H', uc.mem_read(DSB + sp, 6))
        calls.append(('fill', col))
        uc.reg_write(UC_X86_REG_SP, sp + 4)
        uc.reg_write(UC_X86_REG_CS, cs)
        uc.reg_write(UC_X86_REG_IP, ip)
    h1 = e.uc.hook_add(UC_HOOK_CODE, hk_poly, begin=0x101FC, end=0x101FC)
    h2 = e.uc.hook_add(UC_HOOK_CODE, hk_fill, begin=0x10016, end=0x10016)
    bad = 0
    CTX = 0x032E
    for i in range(n):
        M = bytearray(e.base)
        a, b, c = rnd16(r), rnd16(r), rnd16(r)
        if r.random() < 0.3:   # random raw words too
            vals = [rnd16(r) for _ in range(4)]
        else:
            vals = [ref.sin_(b), ref.cos_(b), ref.sin_(c), ref.cos_(c)]
        for o, v in zip((0x4986, 0x4988, 0x498A, 0x498C), vals):
            struct.pack_into('<H', M, o, v)
        xm, ym = r.choice(WINDOWS[:4])
        struct.pack_into('<HH', M, 0xD1E, (xm + 1) // 2, (ym + 1) // 2)
        struct.pack_into('<H', M, 0x4980, rnd16(r))
        M[0x98DE] = r.choice([0, 1, 2, 3, 3, 3, 0xFF])
        M[0x433A] = r.choice([0, 0, 1, 2])
        M[0x294B] = r.choice([0, 1])
        struct.pack_into('<HH', M, 0x4360, r.choice([0, 1]), 0)
        struct.pack_into('<H', M, 0x43B2, r.choice([0, 1]))
        struct.pack_into('<H', M, CTX + 4, r.randint(0, 0xFFFF))
        RM = ref.Mem(bytearray(M))
        exp = ref.sky_ground(RM, CTX)
        e.set_ds(M)
        calls.clear()
        e.call(0, 0x1442, (CTX,), near=True)
        g = e.ds()
        k = cmp_ds(g, RM.m, [(0x98C8, 0x98CA), (0x98E4, 0x98E6), (0x98F8, 0x98FA), (0x9F12, 0x9F14), (0xC080, 0xC0A0)])
        if calls != exp or k is not None:
            bad += 1
            if bad < 4:
                print('  1442 case %d diff DS %s; emu %s\n ref %s' % (i, k, calls[:2], exp[:2]))
    e.uc.hook_del(h1)
    e.uc.hook_del(h2)
    report('sky_ground 1442', n, bad)


def test_fillrows(e, n, r):
    bad = 0
    VP = 0x032E
    for i in range(n):
        M = bytearray(e.base)
        struct.pack_into('<HH', M, 0x2293, 319, 199)
        fresh_tables(M, r)
        struct.pack_into('<HHH', M, VP, r.randint(0, 3), 0, r.randint(0, 0xFFFF))
        ymin = r.randint(0, 199)
        ymax = r.randint(ymin, 199)
        xl, xr = r.randint(0, 319), r.randint(0, 319)
        RM = ref.Mem(bytearray(M))
        ref.fill_rows(RM, VP, xl, ymin, xr, ymax)
        e.set_ds(M)
        e.call(0x1001, 0x024C, (VP, xl, ymin, xr, ymax))
        log = [(a, None) if a == 16 else (a, b) for a, b, *rest in [x + (None,) for x in e.log] if a != 15]
        if cmp_ds(e.ds(), RM.m, R1001) is not None or log != RM.log:
            bad += 1
            if bad < 3:
                print('  fill_rows', e.log, RM.log)
    report('fill_rows 024C', n, bad)
    bad = 0
    for i in range(n):
        M = bytearray(e.base)
        struct.pack_into('<HH', M, 0x2293, 319, 199)
        fresh_tables(M, r)
        RM = ref.Mem(bytearray(M))
        ref.tables_clear(RM)
        e.set_ds(M)
        e.call(0x1001, 0x051F)
        if cmp_ds(e.ds(), RM.m, R1001) is not None:
            bad += 1
    report('tables_clear 051F', n, bad)


if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
    which = sys.argv[2] if len(sys.argv) > 2 else 'all'
    r = random.Random(7)
    e = Emu()
    if which in ('all', 'math'):
        test_math(e, n, r)
    if which in ('all', '1001'):
        test_1001(e, n, r)
    if which in ('all', 'fill'):
        test_fillrows(e, n, r)
    if which in ('all', '1442'):
        test_1442(e, n, r)
