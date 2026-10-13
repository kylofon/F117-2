"""Differential test: real 1e9e (Unicorn) vs r2_fm.FM (Python)."""
import sys, random, struct
sys.path.insert(0, 'C:/Coding/F117-2/work/s36')
from r2_emu import *
import r2_ref as R
from r2_fm import FM

CSEG, PSEG = 0x9000, 0x9100


def setup(e):
    add_stubs(e)
    e.w(0xe548, 0); e.w(0xe54a, CSEG); e.w(0x98f4, 0); e.w(0x98f6, PSEG)
    e.uc.mem_write(CSEG * 16, bytes(0x200)); e.uc.mem_write(PSEG * 16, bytes(0x100))


def rnd_state(e, rng, joy=False, ground=False, crash=False, first=False):
    w = e.w
    rate = rng.choice([2, 3, 4, 4, 4, 5, 6, 8, 12, 15])
    w(0x3660, rate); w(0x3d7a, 0)
    w(0x43ba, rng.randint(0, 1))
    if joy:
        e.uc.mem_write(CSEG * 16 + 0x72, b'\x01'); w(0x2cd2, rng.randint(0, 255), 1); w(0x2cd3, rng.randint(0, 255), 1)
    else:
        e.uc.mem_write(CSEG * 16 + 0x72, b'\x00')
        w(0x2cd6, rng.choice([0x80, 0x80, rng.randint(0, 255), 0x5a, 0xa6]), 1)
        w(0x2cd7, rng.choice([0x80, 0x80, rng.randint(0, 255), 0x5a, 0xa6]), 1)
    yaw = rng.randint(0, 65535); pitch = rng.choice([0, rng.randint(-0x3800, 0x3800), rng.randint(-0x800, 0x800)])
    roll = rng.choice([0, rng.randint(0, 65535), rng.randint(-0x1800, 0x1800) & 0xffff])
    w(0x2dee, yaw); w(0x2df0, pitch); w(0x2df2, roll)
    M = R.build_mat(yaw, pitch, roll)
    for k in range(9): w(0x2d94 + 2 * k, M[k])
    w(0x2dfb, 0, 1); w(0x2df8, rng.randint(0, 20)); w(0x2dfa, rng.choice([0, 1]), 1)
    c0a2 = rng.choice([0, 0, 0x80, 30, 100])
    w(0xc0a2, c0a2)
    if ground:
        alt = c0a2
    else:
        alt = rng.choice([rng.randint(0, 3000), rng.randint(0, 20000), rng.randint(0, 60000), c0a2 + 5])
    w(0x2df6, alt)
    a = alt
    sc = a if a < 0x2000 else (((a - 0x2000) >> 1) + 0x2000 if a < 0x4000 else ((a - 0x4000) >> 2) + 0x3000)
    w(0x2df4, sc)
    w(0xb052, rng.choice([0, rng.randint(0, 400), rng.randint(0, 3000), rng.randint(0, 7000), rng.randint(0, 0xffff)]))
    w(0xb168, e.r(0xb052) // 27)
    w(0xb170, rng.randint(0, 100)); w(0x2dfe, rng.randint(0, 100))
    w(0x3634, rng.choice([rng.randint(-100, 9999), 0, 5000]))
    w(0x9b04, rng.randint(0, 255), 1); w(0x3632, rng.randint(0, 255), 1); w(0xc5c8, rng.randint(0, 12))
    w(0x3d74, rng.randint(0, 1)); w(0xe2fe, rng.choice([0, 1, 1, 2]))
    w(0xc06e, rng.choice([0, 0, 0, 1, 5, 14, 15, 16, 17, 19, 20, 40]) if crash else 0)
    w(0x3d68, rng.choice([0, 0xffff])); w(0x98e0, rng.choice([0, 0, 0, 1])); w(0x98ea, rng.choice([0, 0, 1]))
    w(0x3652, rng.choice([0, 0, 0, rng.randint(500, 3000)])); w(0xc078, rng.randint(0, 65535))
    w(0x43c2, rng.randint(0, 3)); w(0xde8a, 0); w(0xe446, 0)
    P = bytearray(0x100)
    struct.pack_into('<H', P, 0x38, rng.randint(0, 5)); struct.pack_into('<H', P, 0x40, rng.randint(0, 2))
    e.uc.mem_write(PSEG * 16, bytes(P))
    w(0x3d60, rng.randint(0, 0xffff)); w(0xdfe0, rng.randint(0, 3)); w(0xe2dc, rng.randint(0, 3))
    for n in range(4): e.wb(0xb2a4 + 16 * n + 7, [rng.choice([0, 2, 8, 10])])
    w(0xc0a4, rng.randint(0, 0xffff)); w(0xc0b2, rng.randint(0, 0xffff))
    w(0x3d64, rng.choice([-1, 0, 1, 2]) & 0xffff)
    for n in range(4):
        w(0xc140 + 0x24 * n, rng.randint(0, 0xffff)); w(0xc142 + 0x24 * n, rng.randint(0, 0xffff))
    w(0x98e2, rng.randint(0, 255)); w(0x2e02, rng.choice([0, 0, 5]))
    w(0x926e, rng.randint(0, 0xffff)); w(0x9270, rng.randint(0, 0xffff))
    w(0xba10, rng.randint(0, 0xffff)); w(0xba12, rng.randint(0, 0xffff)); w(0xc02a, rng.randint(0, 0xffff)); w(0xc02c, rng.randint(0, 0xffff))
    w(0xb00a, rng.randint(0, 8000)); w(0x98c6, rng.randint(-2000, 2000)); w(0x98dc, 16)
    w(0xc0ae, 0 if first else 1); w(0x365e, 0)
    w(0x294b, rng.randint(0, 1), 1)
    w(0xdeca, rng.randint(0, 200)); w(0xe542, 0, 1)


def run_one(seed, keys=(), **kw):
    rng = random.Random(seed)
    e = Emu(); setup(e)
    rnd_state(e, rng, **kw)
    mem0 = bytearray(e.rb(0, 0x10000)); comm0 = bytearray(e.uc.mem_read(CSEG * 16, 0x200)); pil0 = bytearray(e.uc.mem_read(PSEG * 16, 0x100))
    e.keys = list(keys)
    e.call(0x1e9e, ())
    mem1 = e.rb(0, 0x10000)
    F = FM(mem0, comm0, pil0, keys)
    try:
        F.frame()
    except NotImplementedError:
        return 'skip'
    diffs = [o for o in range(0xf000) if mem0[o] != mem1[o] and not (0x9876 <= o < 0x9890)]
    # emulator log -> comparable list
    elog = []
    for c in e.calls:
        if c[0] == 101: elog.append(('snd', c[1], c[2]))
        elif c[0] == 41: elog.append(('recolor',) + tuple(x & 0xffff for x in c[2:8]))
        elif c[0] == 'end': elog.append(('end', c[1]))
        elif c[0] == 'wait': elog.append(('wait', c[1]))
        elif c[0] == 'far': elog.append(('far', c[1]))
        else: elog.append(('other', c))
    rlog = []
    for c in F.log:
        rlog.append(c)
    ok_log = len(elog) == len(rlog)
    if ok_log:
        for a, b in zip(elog, rlog):
            if a[0] == 'snd':
                if not (b[0] == 'snd' and a[1] == b[1] and (b[2] is None or a[2] == b[2])): ok_log = False
            elif a != b: ok_log = False
    return diffs, ok_log, elog, rlog, mem0, mem1


if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    bad = 0
    modes = [dict(), dict(joy=True), dict(ground=True), dict(crash=True), dict(ground=True, crash=True), dict(first=True)]
    keys_pool = [(), (), (0x0b30,), (0x0c2d,), (0x0d3d,), (0x0d2b,), (0x0c5f,), (0x2e63,), (0x011b,), (0x1f00,)]
    for seed in range(n):
        kw = modes[seed % len(modes)]
        keys = keys_pool[(seed // 6) % len(keys_pool)]
        r = run_one(seed, keys, **kw)
        if r == 'skip': continue
        diffs, ok_log, elog, rlog, m0, m1 = r
        if diffs or not ok_log:
            bad += 1
            if bad <= 6:
                print('seed', seed, kw, keys, 'diffs', [hex(d) for d in diffs[:12]], 'n=', len(diffs), 'log ok', ok_log)
                if not ok_log: print('  emu', elog[:8], '\n  ref', rlog[:8])
    print('mismatching runs', bad, 'of', n)
