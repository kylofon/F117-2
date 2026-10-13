"""R7f differential tests under Unicorn: 0000:B85D, BA13, BA67 vs R7f_ref."""
import struct, random, sys
sys.path.insert(0, 'C:/Coding/F117-2/port/notes/s37')
from unicorn import *
from unicorn.x86_const import *
import R7f_ref as R

D = open('C:/Coding/F117-2/work/unpacked/VGAME.EXE', 'rb').read()
IMG = D[struct.unpack_from('<H', D, 8)[0] * 16:]
DSEG = 0x1e23; DSB = DSEG * 16; RETIP = 0xFFF0
uc = Uc(UC_ARCH_X86, UC_MODE_16)
uc.mem_map(0, 0x110000); uc.mem_write(0, IMG)
uc.hook_add(UC_HOOK_INTR, lambda u, i, d: u.emu_stop())


def w(off, data): uc.mem_write(DSB + off, data)
def r(off, n): return bytes(uc.mem_read(DSB + off, n))
def ww(off, v): w(off, struct.pack('<H', v & 0xFFFF))
def wd(off, v): w(off, struct.pack('<I', v & 0xFFFFFFFF))
def rw(off): return struct.unpack('<H', r(off, 2))[0]


def run(seg, off, stack=()):
    for rg in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
        uc.reg_write(rg, DSEG)
    uc.reg_write(UC_X86_REG_CS, seg)
    sp = 0xF800
    for a in reversed(stack):
        sp -= 2; ww(sp, a)
    sp -= 2; ww(sp, RETIP)
    uc.reg_write(UC_X86_REG_SP, sp)
    uc.emu_start(seg * 16 + off, seg * 16 + RETIP, count=2000000)
    return uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX)


rnd = random.Random(7)
def rs16(): return rnd.choice([0, 1, -1, 0x7FFF, -0x8000, 0x4000, -0x4000]) if rnd.random() < .1 else rnd.randrange(-0x8000, 0x8000)
def small(): return rnd.randrange(-0x800, 0x800)
res = {}

# rotation-like matrix (Q15)
import math
def rotm():
    a, b, c = [rnd.random() * 2 * math.pi for _ in range(3)]
    ca, sa, cb, sb, cc, sc = math.cos(a), math.sin(a), math.cos(b), math.sin(b), math.cos(c), math.sin(c)
    m = [[ca * cc + sa * sb * sc, sa * sb * cc - ca * sc, sa * cb], [sc * cb, cc * cb, -sb], [ca * sb * sc - sa * cc, ca * sb * cc + sc * sa, ca * cb]]
    return [R.s16(int(round(m[k // 3][k % 3] * 32767))) for k in range(9)]

# ---- BA13
mm = 0; N = 20000
for n in range(N):
    M = [rs16() for _ in range(9)]
    for k in range(9): ww(0xB156 + 2 * k, M[k])
    row = rnd.randrange(3); dx, dy, dz = rs16(), rs16(), rs16()
    ax, dx_ = run(0, 0xBA13, (row, dx, dy, dz))
    got = R.s32(ax | (dx_ << 16)); exp = R.ba13(M, row, dx, dy, dz)
    if got != exp:
        mm += 1
        if mm < 5: print('BA13', M, row, dx, dy, dz, got, exp)
res['BA13'] = (N, mm)

# ---- BA67
mm = 0; N = 20000
for n in range(N):
    Mc = [rs16() for _ in range(3)]
    ww(0x4992, Mc[0]); ww(0x4998, Mc[1]); ww(0x499E, Mc[2])
    a = [rs16() for _ in range(6)]
    run(0, 0xBA67, a)
    got = R.s16(rw(0xDE92)); exp = R.ba67(Mc, *a)
    if got != exp:
        mm += 1
        if mm < 5: print('BA67', Mc, a, got, exp)
res['BA67'] = (N, mm)

# ---- B85D
mm = 0; N = 60000; vis = 0
SENT = 0x5A5A
for n in range(N):
    kind = rnd.randrange(3)
    M = [rs16() for _ in range(9)] if kind == 0 else rotm()
    g = {'M': M}
    if kind == 2:   # realistic: coordinates near the camera
        g['C0A4'] = rnd.randrange(0, 0x10000); g['C0B2'] = rnd.randrange(0, 0x10000)
        x = R.s16(g['C0A4'] + small()); y = R.s16(g['C0B2'] + small()); z = rnd.randrange(0, 0x7000)
        g['2DF4'] = rnd.randrange(0, 0x7000)
    else:
        g['C0A4'] = rs16(); g['C0B2'] = rs16(); g['2DF4'] = rs16(); x, y, z = rs16(), rs16(), rs16()
    g['C07A'] = rnd.choice([0, 0x8B, 0x80, 0x01, 0x40, 0x81])
    for k in ('BA10', 'B288', 'C02A', 'B752'):
        g[k] = rnd.randrange(0, 1 << 32) if kind == 0 else rnd.randrange(0, 1 << 24)
    if kind and rnd.random() < .7:
        g['B288'] = (g['BA10'] + rnd.randrange(-0x4000, 0x4000)) & 0xFFFFFFFF
        g['B752'] = (g['C02A'] + rnd.randrange(-0x4000, 0x4000)) & 0xFFFFFFFF
    g['B75C'] = rs16() if kind == 0 else rnd.randrange(0, 0x7000)
    g['294B'] = rnd.choice([0, 1, 0, 2, 0x100])
    g['365E'] = rnd.choice([0, 1, 0, 0x100])
    for k in range(9): ww(0xB156 + 2 * k, M[k])
    ww(0xC0A4, g['C0A4']); ww(0xC0B2, g['C0B2']); ww(0x2DF4, g['2DF4']); w(0xC07A, bytes([g['C07A']]))
    wd(0xBA10, g['BA10']); wd(0xB288, g['B288']); wd(0xC02A, g['C02A']); wd(0xB752, g['B752'])
    ww(0xB75C, g['B75C']); ww(0x294B, g['294B']); ww(0x365E, g['365E'])
    for o in (0x49E0, 0x49E8, 0xDE92, 0x9500): ww(o, SENT)
    run(0, 0xB85D, (x, y, z))
    got = {k: rw(int(k, 16)) for k in ('49E0', '49E8', 'DE92', '9500')}
    e = R.b85d(g, x, y, z)
    exp = {k: e.get(k, SENT) for k in ('49E0', '49E8', 'DE92', '9500')}
    if exp['49E0'] != 0xFFFF: vis += 1
    if got != exp:
        mm += 1
        if mm < 5: print('B85D', g, x, y, z, got, exp)
res['B85D'] = (N, mm, 'visible', vis)
print(res)
