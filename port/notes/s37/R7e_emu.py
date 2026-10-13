"""R7e differential tests under Unicorn: 13AC, 1353, 11F2 (13E3 stubbed), 13E3 (11EB:00D3 stubbed),
0FFB (11EB:01B8 stubbed), 77D9 (0FFB stubbed), BC12 (+BC77, C6B8), BCD7."""
import struct, random, sys
sys.path.insert(0, 'C:/Coding/F117-2/port/notes/s37')
from unicorn import *
from unicorn.x86_const import *
import R7d_ref as D
import R7e_ref as R

DSEG = 0x1e23; DSB = DSEG * 16; RETIP = 0xFFF0
uc = Uc(UC_ARCH_X86, UC_MODE_16)
uc.mem_map(0, 0x110000); uc.mem_write(0, D.IMG)
uc.hook_add(UC_HOOK_INTR, lambda u, i, d: u.emu_stop())


def w(off, data): uc.mem_write(DSB + off, bytes(data))
def r(off, n): return bytes(uc.mem_read(DSB + off, n))
def ww(off, v): w(off, struct.pack('<H', v & 0xFFFF))
def rw(off): return struct.unpack('<H', r(off & 0xFFFF, 2))[0]
def rb(off): return r(off & 0xFFFF, 1)[0]


class M:
    rw = staticmethod(rw); rb = staticmethod(rb)


def run(seg, off, stack=()):
    for rg in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
        uc.reg_write(rg, DSEG)
    uc.reg_write(UC_X86_REG_CS, seg)
    sp = 0xF800
    for a in reversed(stack):
        sp -= 2; ww(sp, a)
    sp -= 2; ww(sp, RETIP)
    uc.reg_write(UC_X86_REG_SP, sp)
    uc.emu_start(seg * 16 + off, seg * 16 + RETIP, count=5000000)
    return uc.reg_read(UC_X86_REG_AX)


rnd = random.Random(1170)
def rs16(): return rnd.choice([0, 1, -1, 0x7FFF, -0x8000, 0x10, 0x20]) if rnd.random() < .1 else rnd.randrange(-0x8000, 0x8000)
def rbytes(n, hi=256): return bytes(rnd.randrange(hi) for _ in range(n))
res = {}
calls = []


def stub(lin, patch, nargs, far):
    """Replace the routine at linear lin by ret/retf and record its stack args."""
    uc.mem_write(lin, bytes([patch]))
    def cb(u, a, s, d):
        sp = u.reg_read(UC_X86_REG_SP)
        base = sp + (4 if far else 2)
        calls.append(tuple(rw(base + 2 * k) for k in range(nargs)) + (rw(0x7D5E), rw(0x94E6), rw(0xE2FC), rw(0x8560)))
    return uc.hook_add(UC_HOOK_CODE, cb, begin=lin, end=lin)


def unstub(lin, h):
    uc.mem_write(lin, D.IMG[lin:lin + 1]); uc.hook_del(h)


# ---- 13AC / 1353
N = 20000; mm = 0; mm2 = 0
for n in range(N):
    T = rnd.choice([0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, rnd.randrange(1, 0x8000), -rnd.randrange(2, 0x8000)])
    for o, v in ((0x94DE, T), (0x94E2, rnd.choice([rnd.randrange(0x4000), rs16()])), (0x94E4, rnd.choice([rnd.randrange(0x4000), rs16()])),
                 (0x0D1E, rnd.choice([38, 159, rnd.randrange(-200, 400)])), (0x0D20, rnd.choice([29, 100, 52, 60, rnd.randrange(-200, 400)])),
                 (0x2293, rnd.choice([76, 319, rnd.randrange(400)])), (0x2295, rnd.choice([59, 199, rnd.randrange(300)])),
                 (0x94E0, rnd.choice([4, 16, 64, rs16()]))):
        ww(o, v)
    px, py = rnd.choice([rnd.randrange(-50, 400), rs16()]), rnd.choice([rnd.randrange(-50, 300), rnd.randrange(-0x2000, 0x2000)])
    run(0, 0x13AC, (px, py, 0xF000, 0xF002))
    got = (D.s16(rw(0xF000)), D.s16(rw(0xF002)))
    if got != R.t13ac(M, px, py):
        mm += 1
        if mm < 5: print('13AC mm', T, px, py, got, R.t13ac(M, px, py))
    for k in range(4): ww(0xF010 + 2 * k, 0x5A5A)
    run(0, 0x1353, (0xF010, 0xF012, 0xF014, 0xF016))
    got = tuple(D.s16(rw(0xF010 + 2 * k)) for k in range(4))
    if got != R.t1353(M):
        mm2 += 1
        if mm2 < 5: print('1353 mm', got, R.t1353(M))
res['13AC'] = (N, mm); res['1353'] = (N, mm2)

# ---- 11F2 with 13E3 stubbed (0865 real)
POOL = 0xA06A
def setup_tiles():
    tabs = rbytes(0x40, 32), rbytes(0x100, 32), rbytes(0x200, 32), rbytes(0x200, 32), rbytes(0x200, 32)
    for base, t in zip((0xB00C, 0x9F68, 0x9D10, 0x9B08, 0x9904), tabs): w(base, t)
    recs = b''
    for k in range(200):
        z = 0 if rnd.random() < .7 else rs16()
        recs += struct.pack('<hhhB', rnd.randrange(-0x900, 0x900), rnd.randrange(-0x900, 0x900), z, rnd.randrange(256))
    w(POOL, recs)
    for k in range(5 * 32):
        ww(0x061E + 2 * k, rnd.choice([0, 0, 1, 2, 3, 4, 6]))
        ww(0xE304 + 2 * k, POOL + 7 * rnd.randrange(190))

N = 3000; mm = 0; ncalls = 0
h = stub(0x13E3, 0xC3, 3, False)
for n in range(N):
    if n % 50 == 0: setup_tiles()
    vp = rnd.choice([(38, 29, 76, 59), (159, 100, 319, 199), (rnd.randrange(160), rnd.randrange(100), rnd.randrange(320), rnd.randrange(200))])
    for o, v in zip((0x0D1E, 0x0D20, 0x2293, 0x2295), vp): ww(o, v)
    px, py, sh = rnd.randrange(0x4000), rnd.randrange(0x4000), rnd.randrange(8)
    calls.clear(); ww(0x7D5E, 0)
    run(0, 0x11F2, (px, py, sh))
    exp, g = R.t11f2(M, px, py, sh)
    got = [c[:6] for c in calls]
    ok = got == exp and all(rw(o) == v for o, v in g.items() if o != 0x7D5E)
    ncalls += len(got)
    if not ok:
        mm += 1
        if mm < 4: print('11F2 mm', px, py, sh, vp, len(got), len(exp), got[:3], exp[:3])
unstub(0x13E3, h)
res['11F2'] = (N, mm, ncalls)

# ---- 13E3 with 11EB:00D3 stubbed (1433:03A9, E54B real)
N = 5000; mm = 0
LIN = 0x11EB0 + 0xD3
h = stub(LIN, 0xCB, 6, True)
w(0x07E0, rbytes(0x200)); ww(0xB760, 0x2345)
for n in range(N):
    e2fc, f, oid, x, y = rnd.choice([1, 2, 3, 4, 5, -1]), rnd.choice([0, 0x80]), rnd.randrange(0x10000), rs16(), rs16()
    ww(0xE2FC, e2fc); ww(0x94E6, f); ww(0x8560, 0x1111); calls.clear()
    for k in range(9): ww(0x49A0 + 2 * k, 0x5A5A)
    run(0, 0x13E3, (oid, x, y))
    e = R.t13e3(M, e2fc, f, oid, x, y)
    if e is None: ok = calls == [] and rw(0x8560) == 0x1111
    else:
        ok = len(calls) == 1 and calls[0][:6] == e[0] and calls[0][9] == e[1]
        ok = ok and [rw(0x49A0 + 2 * k) for k in range(9)] == [0x7FFE, 0, 0, 0, 0x7FFE, 0, 0, 0, 0x7FFE]
    if not ok:
        mm += 1
        if mm < 4: print('13E3 mm', e2fc, f, oid, x, y, calls, e)
unstub(LIN, h)
res['13E3'] = (N, mm)

# ---- 0FFB with 11EB:01B8 stubbed
N = 20000; mm = 0; nfound = 0
LIN = 0x11EB0 + 0x1B8
h = stub(LIN, 0xCB, 5, True)
SENT = {0xBA20: 0x1357, 0x4978: 0x2468, 0x8560: 0x1111, 0x4964: 0x5A5A, 0x4966: 0x5A5A, 0x4968: 0x5A5A, 0xC686: 0x0080}
for n in range(N):
    if n % 100 == 0:
        setup_tiles()
        w(0x0960, rbytes(0x80)); w(0x0510, rbytes(0x100)); w(0xB75E, rbytes(8))
        for k in range(32): ww(0x061E + 2 * (128 + k), rnd.choice([0, 1, 2, 3, 5, 8]))
    for o, v in SENT.items(): ww(o, v)
    def r32():
        c = rnd.random()
        if c < .7: return rnd.randrange(0x100000)
        if c < .8: return rnd.randrange(-0x40, 0x40) & 0xFFFFFFFF
        return rnd.randrange(1 << 32)
    x, y = r32(), r32()
    z = rnd.choice([rs16() & 0xFFFFFFFF, rnd.randrange(0x2000), r32()])
    calls.clear()
    run(0, 0x0FFB, (x & 0xFFFF, x >> 16, y & 0xFFFF, y >> 16, z & 0xFFFF, z >> 16))
    e = R.t0ffb(M, x, y, z)
    chk = {'C686': rw(0xC686), '4980': rw(0x4980), '497C': rw(0x497C), '497E': rw(0x497E), 'E44A': rw(0xE44A), 'BA16': rw(0xBA16)}
    ok = all(chk[k] == e[k] for k in chk)
    ok = ok and rw(0x4978) == (SENT[0x4978] if e['4978'] is None else e['4978'])
    if e['call'] is None:
        ok = ok and calls == [] and rw(0xBA20) == SENT[0xBA20] and rw(0x8560) == SENT[0x8560]
    else:
        nfound += 1
        ok = ok and rw(0xBA20) == e['BA20'] and rw(0x8560) == e['8560'] and len(calls) == 1 and calls[0][:5] == e['call']
        ok = ok and all(rw(o) == e['%04X' % o] for o in (0x4964, 0x4966, 0x4968))
        ok = ok and [rw(0x49A0 + 2 * k) for k in range(9)] == [0x7FFE, 0, 0, 0, 0x7FFE, 0, 0, 0, 0x7FFE]
    if not ok:
        mm += 1
        if mm < 4: print('0FFB mm', hex(x), hex(y), hex(z), chk, rw(0x4978), calls, e)
unstub(LIN, h)
res['0FFB'] = (N, mm, nfound)

# ---- 77D9 with 0FFB stubbed
N = 5000; mm = 0
h = stub(0x0FFB, 0xC3, 6, False)
for n in range(N):
    a = (rs16(), rs16(), rs16()); calls.clear()
    run(0, 0x77D9, a)
    e = R.t77d9(*a)
    got = tuple(calls[0][2 * k] | calls[0][2 * k + 1] << 16 for k in range(3))
    if got != e:
        mm += 1
        if mm < 4: print('77D9 mm', a, got, e)
unstub(0x0FFB, h)
res['77D9'] = (N, mm)

# ---- BC12 (BC77, C6B8 real)
N = 20000; mm = 0
for n in range(N):
    if n % 200 == 0:
        w(0x0760, rbytes(0x300)); w(0x0510, rbytes(0x100)); w(0xB2A2, rbytes(16 * 64)); w(0xC604, rbytes(0x80))
        w(0xC0AC, rbytes(1)); w(0xDEA4, rbytes(1))
    o = rnd.randrange(64)
    if rnd.random() < .5: w(0xB2AA + 16 * o, bytes([rnd.randrange(256) | 0x80]))
    ax = run(0, 0xBC12, (o,))
    if ax != R.tbc12(M, o):
        mm += 1
        if mm < 4: print('BC12 mm', o, hex(ax), hex(R.tbc12(M, o)))
res['BC12'] = (N, mm)

# ---- BCD7
N = 20000; mm = 0
for n in range(N):
    if n % 200 == 0:
        w(0xC13E, rbytes(0x24 * 48)); w(0xB174, rbytes(256))
    ww(0xDED0, rnd.choice([rnd.randrange(48), rnd.randrange(0x10000)])); ww(0x3D62, rnd.choice([0, 0, 1, rs16()]))
    u = rnd.randrange(-2, 48); ww(0x49C6, 0x5A5A)
    run(0, 0xBCD7, (u,))
    if rw(0x49C6) != R.tbcd7(M, u):
        mm += 1
        if mm < 4: print('BCD7 mm', u, rw(0x49C6), R.tbcd7(M, u))
res['BCD7'] = (N, mm)

for k, v in res.items(): print(k, v)
