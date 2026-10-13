"""R7a differential tests: 11EB:0906 (+INT0 0975), 0990, 02B6, 000C, 0E5F, 0819, 0678 vs R7a_ref.py."""
import struct, random, sys
sys.path.insert(0, 'C:/Coding/F117-2/port/notes/s37')
from unicorn import *
from unicorn.x86_const import *
import R7a_ref as R
D = open('C:/Coding/F117-2/work/unpacked/VGAME.EXE', 'rb').read()
IMG = D[struct.unpack_from('<H', D, 8)[0] * 16:]
DSEG = 0x1e23; SEG = 0x11eb; RETIP = 0xFFF0; DSB = DSEG * 16
uc = Uc(UC_ARCH_X86, UC_MODE_16)
uc.mem_map(0, 0x110000); uc.mem_write(0, IMG)
uc.mem_write(0, struct.pack('<HH', 0x0975, SEG))      # INT 0 -> 11EB:0975 (as 0281 does)
ndiv = [0]


def hk_intr(uc, intno, ud):
    if intno != 0:
        uc.emu_stop(); return
    ndiv[0] += 1
    sp = uc.reg_read(UC_X86_REG_SP); ss = uc.reg_read(UC_X86_REG_SS) * 16
    fl = uc.reg_read(UC_X86_REG_EFLAGS) & 0xFFFF; cs = uc.reg_read(UC_X86_REG_CS); ip = uc.reg_read(UC_X86_REG_IP)
    sp -= 6; uc.mem_write(ss + sp, struct.pack('<HHH', ip, cs, fl)); uc.reg_write(UC_X86_REG_SP, sp)
    off, seg = struct.unpack('<HH', uc.mem_read(0, 4))
    uc.reg_write(UC_X86_REG_CS, seg); uc.reg_write(UC_X86_REG_IP, off)


uc.hook_add(UC_HOOK_INTR, hk_intr)


def hk_idiv(uc, addr, size, ud):
    # Unicorn raises #DE reliably only once; emulate the 386 fault here: if 'idiv cx' would overflow,
    # vector through IVT[0] with the return IP = the idiv itself (286+ semantics).
    dx = uc.reg_read(UC_X86_REG_DX); ax = uc.reg_read(UC_X86_REG_AX); cx = uc.reg_read(UC_X86_REG_CX)
    dv = R.s32((dx << 16) | ax)
    if R.s16(cx) != 0 and R.idiv16(dv, cx) is not None:
        return
    hk_intr(uc, 0, None)


for a in (0x091E, 0x0930, 0x094F, 0x0963):
    uc.hook_add(UC_HOOK_CODE, hk_idiv, begin=SEG * 16 + a, end=SEG * 16 + a)
def w(off, data): uc.mem_write(DSB + off, data)
def r(off, n): return bytes(uc.mem_read(DSB + off, n))


def run(off, regs=None, stack=(), far=False, cnt=2000000):
    for rg in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
        uc.reg_write(rg, DSEG)
    uc.reg_write(UC_X86_REG_CS, SEG)
    sp = 0xF800
    for a in reversed(stack):
        sp -= 2; w(sp, struct.pack('<H', a & 0xFFFF))
    if far:
        sp -= 2; w(sp, struct.pack('<H', SEG))
    sp -= 2; w(sp, struct.pack('<H', RETIP))
    uc.reg_write(UC_X86_REG_SP, sp)
    for k, v in (regs or {}).items():
        uc.reg_write(k, v & 0xFFFF)
    uc.emu_start(SEG * 16 + off, SEG * 16 + RETIP, count=cnt)


rnd = random.Random(7)
BITS = [8, 16, 20, 24, 31]


def rs32():
    k = rnd.randrange(6)
    if k == 0:
        return rnd.choice([0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x10000, 0xFFFF, -0x10000, 0x20000, 0x1FFFF, 0x8000, -0x8000])
    b = BITS[k - 1]
    return rnd.randrange(-(1 << b), 1 << b)


def rs16():
    return rnd.choice([0, 1, -1, 0x7FFF, -0x8000, 0x100, 0xFF]) if rnd.random() < .1 else rnd.randrange(-0x8000, 0x8000)


res = {}
# ---- 0906 project
mm = 0; n = 0
for i in range(30000):
    x, y = rs32(), rs32()
    zc = rnd.randrange(5)
    z = [rs32(), rnd.randrange(0x10000, 0x1000000), rnd.randrange(0x1000000, 0x80000000), rnd.randrange(-0x80000000, 0x10000),
         rnd.choice([0x10000, 0xFFFFFF, 0x1000000, 0xFFFF, 0x7FFFFFFF])][zc]
    mode = 0 if rnd.random() < .7 else rnd.choice([0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x01, 0x08, rnd.randrange(256)])
    old = [rnd.randrange(0x10000) for _ in range(4)]
    w(0x7D5E, bytes([mode])); w(0xC688, struct.pack('<iii', x, y, z)); w(0xD688, struct.pack('<4H', *old))
    run(0x0906, {UC_X86_REG_DI: 0xC688, UC_X86_REG_BX: 0xD688})
    got = struct.unpack('<4H', r(0xD688, 8)); exp = R.project(x, y, z, mode, old)
    n += 1
    if got != exp:
        mm += 1
        if mm < 5: print('0906 mismatch', hex(x), hex(y), hex(z), hex(mode), got, exp)
res['0906'] = (n, mm, ndiv[0])
# ---- 0990 near bisect
mm = 0; n = 0
for i in range(20000):
    p0 = [rs32(), rs32(), rnd.choice([rnd.randrange(0x10000, 0x7FFFFFFF), rnd.randrange(0x20000, 0x400000), 0x10000, 0x1FFFF, 0x20000])]
    p1 = [rs32(), rs32(), rnd.choice([rnd.randrange(-0x80000000, 0x10000), rnd.randrange(-0x40000, 0x10000), -1, 0, 0xFFFF])]
    words = []
    for p in (p0, p1):
        for v in p:
            words += [v & 0xFFFF, (v >> 16) & 0xFFFF]
    run(0x0990, stack=words, far=True)
    got = struct.unpack('<ii', r(0x7D42, 8)); exp = R.near_bisect(p0, p1); n += 1
    if got != exp:
        mm += 1
        if mm < 5: print('0990 mismatch', p0, p1, got, exp)
res['0990'] = (n, mm)
# ---- 02B6 frustum
mm = 0; n = 0
for i in range(30000):
    x, y = rs32(), rs32(); z = rs32() if rnd.random() < .5 else rnd.randrange(-0x800000, 0x2000000)
    rad = rs16() & 0xFFFF if rnd.random() < .3 else rnd.randrange(0, 0x400)
    far = rnd.choice([rnd.randrange(0x100, 0x8000), 0x3FFF, 0x7FFF, 0xFF])
    w(0x7CA2, struct.pack('<iii', x, y, z)); w(0x7D2E, struct.pack('<H', far))
    run(0x02B6, {UC_X86_REG_AX: rad})
    ax = uc.reg_read(UC_X86_REG_AX); zf = (uc.reg_read(UC_X86_REG_EFLAGS) >> 6) & 1
    assert (ax == 0) == (zf == 1)
    gv = ax == 0
    ev = R.frustum(R.s16(rad), x, y, z, far); n += 1
    if gv != ev:
        mm += 1
        if mm < 5: print('02B6 mismatch', hex(rad), hex(x), hex(y), hex(z), hex(far), gv, ev)
res['02B6'] = (n, mm)
# ---- 000C set_camera (far, calls 1433 copy/transpose)
mm = 0; n = 0
for i in range(5000):
    m9 = [rs16() for _ in range(9)]; zoom = rs16() if rnd.random() < .5 else rnd.randrange(0, 0x800)
    flag = 0 if rnd.random() < .8 else rnd.choice([1, 0x8000])
    w(0x4000, struct.pack('<9h', *m9)); w(0x7D3A, struct.pack('<H', flag))
    run(0x000C, stack=[0x4000, zoom], far=True)
    got = (struct.unpack('<H', r(0x7D2E, 2))[0], list(struct.unpack('<9h', r(0x7AF8, 18))))
    exp = R.set_camera(m9, zoom, flag); n += 1
    tr = list(struct.unpack('<9h', r(0x7B0A, 18)))
    if got != (exp[0], exp[1]) or tr != [m9[3 * (k % 3) + k // 3] for k in range(9)]:
        mm += 1
        if mm < 5: print('000C mismatch', m9, zoom, got, exp, tr)
res['000C'] = (n, mm)
# ---- 0E5F BSP order (random trees in an ES=DS buffer at DS:4000)
mm = 0; n = 0
for i in range(5000):
    np_ = rnd.randrange(1, 40)
    nodes = list(range(np_)); rnd.shuffle(nodes)
    pairs = [[0xFF, 0xFF] for _ in range(np_)]
    for k in range(1, np_):
        while True:
            par = nodes[rnd.randrange(k)]; s = rnd.randrange(2)
            if pairs[par][s] == 0xFF:
                pairs[par][s] = nodes[k]; break
    if rnd.random() < .2:                          # leaf code in b0 (bit 7 set): emitted verbatim
        for p in pairs:
            if p[0] == 0xFF and rnd.random() < .3: p[0] = rnd.randrange(0x80, 0xFF)
    vis = [rnd.choice([0, 0xFF, 0x7F]) for _ in range(256)]
    buf = bytes([nodes[0]]) + b''.join(bytes(p) for p in pairs)
    w(0x4000, buf); w(0x76D2, bytes(vis)); w(0x78D2, b'\xEE' * 256)
    run(0x0E5F, {UC_X86_REG_SI: 0x4000, UC_X86_REG_DI: 0x78D2, UC_X86_REG_BX: 0x76D2})
    exp = R.bsp_order(nodes[0], [tuple(p) for p in pairs], vis) + [0xFF]
    got = list(r(0x78D2, len(exp))); n += 1
    if got != exp:
        mm += 1
        if mm < 5: print('0E5F mismatch', got, exp)
res['0E5F'] = (n, mm)
# ---- 0819 xform_one + project (single vertex, no part word)
mm = 0; n = 0
for i in range(10000):
    v = [rs16() for _ in range(3)]
    v[0] = 0x7FFF if 0x8000 <= v[0] & 0xFFFF < 0x8010 else v[0]
    M = [rs16() for _ in range(9)]; org = [rs32() for _ in range(3)]
    w(0x4000, struct.pack('<3h', *v)); w(0x4100, struct.pack('<9h', *M)); w(0x7CA2, struct.pack('<iii', *org)); w(0x7D5E, b'\0')
    w(0xD688, b'\0' * 8)
    run(0x0819, {UC_X86_REG_SI: 0x4000, UC_X86_REG_BX: 0x4100, UC_X86_REG_DI: 0xC688, UC_X86_REG_BP: 0xD688, UC_X86_REG_CX: 5})
    o3 = R.xform_one(v, M, org); got3 = list(struct.unpack('<iii', r(0xC688, 12)))
    gp = struct.unpack('<4H', r(0xD688, 8)); ep = R.project(*o3, 0, (0, 0, 0, 0))
    regs = tuple(uc.reg_read(q) for q in (UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_BX, UC_X86_REG_CX))
    n += 1
    if got3 != o3 or gp != ep or regs != (0x4006, 0xC698, 0xD690, 0x4100, 5):
        mm += 1
        if mm < 5: print('0819 mismatch', v, M, org, got3, o3, gp, ep, [hex(q) for q in regs])
res['0819'] = (n, mm)
# ---- 0678 plane shade (SI after d of the plane, DI = plane index)
mm = 0; n = 0
for i in range(10000):
    nn = [rs16() for _ in range(3)]; L = [rs16() for _ in range(3)]; d = rs32()
    w(0x4000, struct.pack('<3hi', *nn, d)); w(0x4100, struct.pack('<3h', *L)); w(0x7D4C, struct.pack('<H', 0x4100))
    run(0x0678, {UC_X86_REG_SI: 0x400A, UC_X86_REG_DI: 3})
    got = r(0x77D5, 1)[0]; n += 1
    if got != R.plane_shade(nn, L) or uc.reg_read(UC_X86_REG_SI) != 0x400A:
        mm += 1
        if mm < 5: print('0678 mismatch', nn, L, got, R.plane_shade(nn, L), hex(uc.reg_read(UC_X86_REG_SI)))
res['0678'] = (n, mm)
for k, v in res.items():
    print(k, 'cases', v[0], 'mismatches', v[1], *(['div0 handled', v[2]] if len(v) > 2 else []))
