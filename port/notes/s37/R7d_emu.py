"""R7d differential tests under Unicorn: E8A2/E8B5, E26B, E3E2, E578, 0804, 0865, 1088:0120, DDFE."""
import struct, random, sys
sys.path.insert(0, 'C:/Coding/F117-2/port/notes/s37')
from unicorn import *
from unicorn.x86_const import *
import R7d_ref as R
IMG = R.IMG
DSEG = 0x1e23; DSB = DSEG * 16; RETIP = 0xFFF0
uc = Uc(UC_ARCH_X86, UC_MODE_16)
uc.mem_map(0, 0x110000); uc.mem_write(0, IMG)
uc.hook_add(UC_HOOK_INTR, lambda u, i, d: u.emu_stop())


def w(off, data): uc.mem_write(DSB + off, data)
def r(off, n): return bytes(uc.mem_read(DSB + off, n))
def ww(off, v): w(off, struct.pack('<H', v & 0xFFFF))
def rw(off): return R.s16(struct.unpack('<H', r(off, 2))[0])


def run(seg, off, stack=(), far=False, regs=None):
    for rg in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
        uc.reg_write(rg, DSEG)
    uc.reg_write(UC_X86_REG_CS, seg)
    sp = 0xF800
    for a in reversed(stack):
        sp -= 2; ww(sp, a)
    if far:
        sp -= 2; ww(sp, seg)
    sp -= 2; ww(sp, RETIP)
    uc.reg_write(UC_X86_REG_SP, sp)
    for k, v in (regs or {}).items():
        uc.reg_write(k, v & 0xFFFF)
    uc.emu_start(seg * 16 + off, seg * 16 + RETIP, count=2000000)
    return uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX), uc.reg_read(UC_X86_REG_EFLAGS)


rnd = random.Random(117)
def rs16(): return rnd.choice([0, 1, -1, 0x7FFF, -0x8000, 0x4000, -0x4000]) if rnd.random() < .1 else rnd.randrange(-0x8000, 0x8000)
res = {}
orig = {o: rw(o) for o in range(0x48C6, 0x4956, 2)}
tab = lambda o: orig[o]

# ---- E8A2 / E8B5
mm = 0; N = 60000
for n in range(N):
    kind = rnd.randrange(4)
    if kind == 0:
        m = [rs16() for _ in range(9)]
    else:                          # realistic rotation matrix
        m, _ = R.e26b(rnd.randrange(65536), rnd.randrange(65536), rnd.randrange(65536))
    sc = rnd.choice([1, 1, 4, 16, 64, 256, 2048]) if kind else 1
    x, y, z = [R.s16(rnd.randrange(-0x8000, 0x8000) // sc) for _ in range(3)]
    cls = rnd.randrange(8)
    g = {'48B6': rnd.choice([3333, 6666, 9999, 13332, 10000, rs16()]), '4360': rnd.choice([0, 0, 1, 2]),
         '294B': rnd.choice([0, 1, 0, 1, 2]), '365E': rnd.choice([0, 1, 0x100, 0]), 'tab': tab}
    tb = rnd.choice([(0x48D6, 0x48F6), (0x48E6, 0x4906)]); g['E53E'] = tb[0]
    g['E544'] = tb[1] + (0x20 if rnd.random() < .5 else 0)
    for k, o in enumerate(range(0x498E, 0x49A0, 2)): ww(o, m[k])
    ww(0x497A, cls); ww(0x48B6, g['48B6']); ww(0x4360, g['4360']); w(0x294B, bytes([g['294B']]))
    ww(0x365E, g['365E']); ww(0xE53E, g['E53E']); ww(0xE544, g['E544']); ww(0x4978, 0x5A5A)
    ax, dx, fl = run(0, 0xE8A2, (x, y, z))
    exp = R.e8b5(m, x, y, z, cls, g)
    got = (ax, R.s32(struct.unpack('<I', r(0x4958, 4))[0]), R.s32(struct.unpack('<I', r(0x495C, 4))[0]),
           R.s32(struct.unpack('<I', r(0x4960, 4))[0]), rw(0x4978))
    ok = got[0] == exp[0] and got[1:4] == exp[1:4] and ((exp[4] is None and got[4] == 0x5A5A) or got[4] == exp[4])
    ok = ok and (bool(fl & 0x40) == (exp[0] == 0))
    if not ok:
        mm += 1
        if mm < 5: print('E8B5 mm', m, x, y, z, cls, g['48B6'], g['4360'], g['294B'], g['365E'], hex(g['E53E']), hex(g['E544']), got, exp)
res['E8B5'] = (N, mm)

# ---- E26B
mm = 0; N = 20000
for n in range(N):
    A, B, C = [rnd.randrange(65536) for _ in range(3)]
    run(0, 0xE26B, (0x498E, A, B, C))
    got = [rw(0x498E + 2 * k) for k in range(9)], [rw(0x4982 + 2 * k) for k in range(6)]
    if got != R.e26b(A, B, C):
        mm += 1
        if mm < 5: print('E26B mm', A, B, C, got, R.e26b(A, B, C))
res['E26B'] = (N, mm)

# ---- E3E2
mm = 0; N = 20000
for n in range(N):
    a = [rnd.randrange(65536) for _ in range(3)]
    for k in range(3): ww(0x496A + 2 * k, a[k])
    run(0, 0xE3E2)
    got = [rw(0x49A0 + 2 * k) for k in range(9)], [rw(0x4982 + 2 * k) for k in range(6)]
    if got != R.e3e2(*a):
        mm += 1
        if mm < 5: print('E3E2 mm', a, got, R.e3e2(*a))
res['E3E2'] = (N, mm)

# ---- E578
mm = 0; N = 10000
for n in range(N):
    A = [rs16() for _ in range(9)]; B = [rs16() for _ in range(9)]
    for k in range(9): ww(0x9000 + 2 * k, A[k]); ww(0x9020 + 2 * k, B[k])
    run(0, 0xE578, (0x9000, 0x9020, 0x9040))
    got = [rw(0x9040 + 2 * k) for k in range(9)]
    if got != R.e578(A, B):
        mm += 1
res['E578'] = (N, mm)

# ---- 0804
mm = 0; N = 20000
for n in range(N):
    lv = rnd.randrange(5); v = rnd.choice([rnd.randrange(1 << 32), rnd.randrange(1 << 21), 0xFFFFFFFF - rnd.randrange(64)])
    ax, dx, _ = run(0, 0x0804, (lv, v & 0xFFFF, v >> 16))
    if (dx << 16 | ax) != R.t0804(lv, v):
        mm += 1
res['0804'] = (N, mm)

# ---- 0865
mm = 0; N = 20000
tabs = bytes(rnd.randrange(256) for _ in range(0x40)), bytes(rnd.randrange(32) for _ in range(0x100)), \
    bytes(rnd.randrange(32) for _ in range(0x200)), bytes(rnd.randrange(32) for _ in range(0x200)), bytes(rnd.randrange(256) for _ in range(0x200))
for base, t in zip((0xB00C, 0x9F68, 0x9D10, 0x9B08, 0x9904), tabs): w(base, t)
mem = {}
for base, t in zip((0xB00C, 0x9F68, 0x9D10, 0x9B08, 0x9904), tabs):
    for k, b in enumerate(t): mem[base + k] = b
for n in range(N):
    lv = rnd.randrange(5); dim = (1024, 256, 64, 16, 8)[lv]
    x = rnd.randrange(-4, dim + 4) if rnd.random() < .9 else rs16(); y = rnd.randrange(-4, dim + 4) if rnd.random() < .9 else rs16()
    ax, _, _ = run(0, 0x0865, (lv, x, y))
    if ax != R.t0865(lv, x, y, lambda o: mem[o]):
        mm += 1
        if mm < 5: print('0865 mm', lv, x, y, ax, R.t0865(lv, x, y, lambda o: mem[o]))
res['0865'] = (N, mm)

# ---- 1088:0120
mm = 0; N = 5000
for n in range(N):
    a, b, c, d = rnd.choice([0, 0, 0, 1, 0x80]), rnd.choice([0, 1, 1, 0x40]), rnd.randrange(256), rnd.choice(list(range(8)) + [rnd.randrange(65536)])
    w(0x7D5E, bytes([a])); w(0x43B6, bytes([b])); w(0x4963, bytes([c])); ww(0x497A, d)
    ax, _, _ = run(0x1088, 0x0120, far=True)
    if ax != R.shade(a, b, c, d):
        mm += 1
        if mm < 5: print('shade mm', a, b, c, d, ax, R.shade(a, b, c, d))
res['1088:0120'] = (N, mm)

# ---- DDFE (sequences of inserts from an empty queue, up to and beyond 50)
mm = 0; N = 400; ins = 0
for n in range(N):
    q = []; cnt = 0; ww(0x49C4, 0)
    w(0x7D5E, b'\0'); w(0x43B6, b'\1')
    for step in range(rnd.randrange(1, 90)):
        key = rnd.choice([rnd.randrange(1 << 32), rnd.randrange(1 << 20), rnd.choice([0, 0x10000, 0xFFFF0000, 0x12345678])])
        e2fc = rnd.choice([1, 2, 3, 4, 2]); cls = rnd.choice([0, 5, 7, 2])
        f = {k: rnd.randrange(65536) for k in (6, 8, 0xA, 0xC, 0xE, 0x10, 0x12, 0x14)}
        w(0x4960, struct.pack('<I', key)); ww(0xE2FC, e2fc); ww(0x497A, cls)
        for k, o in ((6, 0x49C6), (8, 0x4956), (0xA, 0x4964), (0xC, 0x4966), (0xE, 0x4968), (0x10, 0x496A), (0x12, 0x496C), (0x14, 0x496E)):
            ww(o, f[k])
        sh = R.shade(0, 1, (key >> 24) & 0xFF, cls)
        q, rec = R.ddfe(q, cnt, key, e2fc, cls, sh, f); cnt = len(q)
        run(0, 0xDDFE); ins += 1
        n2 = rw(0x49C4)
        ptrs = [struct.unpack('<H', r(0x4E3C + 2 * k, 2))[0] for k in range(n2)]
        ok = n2 == cnt and ptrs == [e['addr'] for e in q]
        b = r(rec['addr'], 0x16)
        vals = struct.unpack('<iH8H', b)
        ok = ok and vals[0] == rec['key'] and vals[1] == rec['shade'] and list(vals[2:]) == [f[k] for k in (6, 8, 0xA, 0xC, 0xE, 0x10, 0x12, 0x14)]
        if not ok:
            mm += 1
            if mm < 5: print('DDFE mm', n, step, n2, cnt, vals[:2], rec['key'], rec['shade'])
            break
res['DDFE'] = (ins, mm)
for k, v in res.items():
    print('%-10s cases %6d mismatches %d' % (k, v[0], v[1]))
