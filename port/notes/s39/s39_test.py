"""S3.9 differential test: real END code under Unicorn vs s39_ref (Python), byte for byte over DGROUP + the comm block.
usage: python s39_test.py [N] [name ...]     names: time landing score promo awards icons   (default all)
Needs `pip install unicorn` and work/unpacked/END.EXE (read at run time; nothing of the game is stored here)."""
import os, random, sys
_here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _here)
from s39_emu import Emu, DSB, CB, s16            # noqa: E402
import s39_ref as R                              # noqa: E402

LIM = 0xF000


def rnd_world(rng, m, nsites=None):
    """random mission state in the Python world `m` (same bytes are then copied into the emulator)."""
    ds = m.ds
    ns = nsites if nsites is not None else rng.randint(3, 40)
    m.sw(R.NSITES, ns)
    for i in range(ns):
        o = R.SITES + 16 * i
        m.sw(o, rng.choice([0, 0, rng.randint(1, 100), rng.randint(1, 5)]))          # name idx
        m.sw(o + 2, rng.randint(0, 0x7fff)); m.sw(o + 4, rng.randint(0, 0x7fff))
        m.sw(o + 6, rng.choice([0, 0, rng.randint(1, 16)]))
        m.sw(o + 8, rng.choice([0, 1, 8, 0x100, 0x400, 0x408, 0x200, 0x800, 0x1000, 0x1001, 0x4000, rng.randint(0, 0xffff)]))
        m.sw(o + 0xa, rng.randint(0, 30)); m.sw(o + 0xc, rng.randint(0, 5))
        m.sw(o + 0xe, rng.randint(0, 0x1ff))
    for i in range(128):
        ds[R.OBJCLS + i] = rng.choice([0, 0x0d, 0x40, 0x80, 0xc0, rng.randint(0, 255)])
    for i in range(256):
        ds[R.GRID + i] = rng.randint(0, 255)
    # unit type radar words (+0x18 of the 32-byte records at 0x179e)
    for i in range(0, 29):
        m.sw(0x179e + 32 * i + 0x18, rng.choice([0xffff, 0, rng.randint(0, 20), rng.randint(0, 60)]))
    # pilot / comm
    m.sp(0x20, rng.randint(0, 6)); m.sp(0x22, rng.choice([0, 0, 1])); m.sp(0x24, rng.randint(0, 9))
    for o in (0x26, 0x28, 0x2a, 0x2c):
        m.sp(o, rng.choice([0, 0, 1, 2, 3]))
    m.sp(0x2e, rng.randint(0, 3000)); m.sp(0x30, rng.choice([0, rng.randint(0, 3000), 0x4b1, 0x4b0, 899, 900, 1799, 1800]))
    sc = rng.choice([0, rng.randint(0, 30000), rng.randint(0, 3000000)])
    m.sp(0x32, sc & 0xffff); m.sp(0x34, sc >> 16)
    m.sp(0x36, rng.choice([1, 2, 5, 9, 10, 29, 30, 59, 60, 98, 99, rng.randint(1, 120)]))
    m.sp(0x38, rng.randint(0, 8)); m.sp(0x3a, rng.randint(0, 2)); m.sp(0x3c, rng.randint(0, 3))
    m.sp(0x3e, rng.randint(0, 3)); m.sp(0x40, rng.randint(0, 2)); m.sp(0x42, rng.randint(0, 1))
    for o in (0x44, 0x46, 0x48, 0x4a, 0x4c):
        m.sp(o, rng.choice([0, 0, 1]))
    m.sc(0x26, rng.choice([1, 2, 3])); m.sc(0x28, rng.choice([0, 0, 1, 2, 5, 6]))
    m.sc(0x2c, rng.randint(0, 6)); m.sc(0x2e, rng.choice([0, rng.randint(0, 0x3000), 0x1b58, 0x2328, 0x2cec]))
    m.sc(0x30, rng.choice([0, 0, 1]))
    m.comm[0x33] = rng.choice([0, 1]); m.sc(0x34, rng.randint(0, 0xffff)); m.sc(0x36, rng.randint(0, 12))
    m.sc(0x74, rng.randint(0, 0x7fff)); m.sc(0x76, rng.randint(0, 0x7fff))
    m.sb(0x6982, rng.choice([0, 0, 1])); m.sb(0x6440, rng.randint(0, 3))
    m.sw(0x71f0, rng.randint(0, 81)); m.sw(0x7202, rng.randint(0, 81))
    for a in (0x55e6, 0x55e7, 0x643a, 0x55dd, 0x55e2, 0x55da, 0x6d89, 0x720e):
        m.sb(a, 0)
    # event log
    n = rng.randint(0, 40)
    types = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 9, 9, 3, 1]
    for k in range(n):
        o = R.LOG + 6 * k
        t = rng.choice(types)
        if t in (1, 2, 3, 10, 11, 12) and rng.random() < 0.3:
            t |= rng.choice([0x40, 0x80, 0xc0])
        m.sw(o, rng.randint(0, 3000)); ds[o + 2] = rng.randint(0, 255); ds[o + 3] = rng.randint(0, 255)
        ds[o + 4] = t
        ds[o + 5] = rng.choice([rng.randint(0, ns - 1), rng.randint(0, 255), rng.randint(0, 40) | rng.choice([0, 0x80])])
        if t & 0x3f == 3:                                  # unit type must index the 29-entry table (the harness patches the driver stubs behind it)
            ds[o + 5] = (ds[o + 5] & 0x80) | rng.randint(0, 28)
    ds[R.LOG + 6 * n + 4] = 0
    return n


def load_into(e, m):
    e.wb(0, bytes(m.ds[:LIM]))
    e.uc.mem_write(CB, bytes(m.comm))
    e.write_slots()
    e.setup_far_ptrs()


def compare(e, m, ax=None, rax=None, what=''):
    ds, comm = e.snapshot()
    bad = []
    if ax is not None and (ax & 0xffff) != (rax & 0xffff):
        bad.append('ax %04x != ref %04x' % (ax & 0xffff, rax & 0xffff))
    mds = bytearray(m.ds[:LIM])
    mds[0x23fa:0x23fa + 550] = ds[0x23fa:0x23fa + 550]        # driver call stubs (retf in the harness)
    mds[0x55c2:0x55cc] = ds[0x55c2:0x55cc]                    # sprintf's static FILE (RTL scratch)
    mds = bytes(mds)
    if ds != mds:
        diffs = [i for i in range(LIM) if ds[i] != mds[i]]
        bad.append('DGROUP differs at %s' % ' '.join('%04x:%02x/%02x' % (i, ds[i], mds[i]) for i in diffs[:8]))
    if comm != bytes(m.comm):
        diffs = [i for i in range(len(comm)) if comm[i] != m.comm[i]]
        bad.append('comm differs at %s' % ' '.join('%04x:%02x/%02x' % (i, comm[i], m.comm[i]) for i in diffs[:8]))
    return bad


def fixup(m):
    # far pointer cells exist in the image already; the python world needs the same values
    m.sw(0x7222, 0); m.sw(0x7224, 0x3000); m.sw(0x55de, 0x1272); m.sw(0x55e0, 0x3000)


def run(name, N, seed=1):
    rng = random.Random(seed * 1000 + sum(map(ord, name)))
    e = Emu()
    bad_total = 0
    for k in range(N):
        m = R.W()
        n = rnd_world(rng, m)
        fixup(m)
        load_into(e, m)
        if name == 'time':
            t = rng.choice([0, rng.randint(0, 0xffff), rng.randint(0, 12000)])
            ax, _ = e.call(0x0ef3, (t, 0x7a00))
            ref = R.fmt_time(m, t)
            got = e.rb(0x7a00, 8)
            # buffer is not part of the ref world: copy it
            m.ds[0x7a00:0x7a08] = ref
            bad = compare(e, m, ax, 0x7a00)
            if got != ref:
                bad.append('time %r != %r' % (got, ref))
        elif name == 'landing':
            ax, _ = e.call(0x00a1, ())
            rax = R.landing_class(m)
            bad = compare(e, m, ax, rax)
        elif name == 'score':
            lim = rng.choice([0x100, n, max(n - 1, 0), rng.randint(0, 45)])
            ax, _ = e.call(0x3021, (lim,))
            rax = R.score(m, lim)
            bad = compare(e, m, ax, rax)
        elif name == 'promo':
            e.call(0x2afa, ())
            R.promotion(m)
            bad = compare(e, m)
        elif name == 'awards':
            e.call(0x2476, ())
            R.awards(m)
            bad = compare(e, m)
        elif name == 'icons':
            e.call(0x05ca, ())
            R.place_icons(m)
            bad = compare(e, m)
        else:
            raise SystemExit('unknown test ' + name)
        if bad:
            bad_total += 1
            if bad_total <= 5:
                print('  MISMATCH %s case %d: %s' % (name, k, '; '.join(bad)))
    print('%-8s %5d cases, %d mismatches' % (name, N, bad_total))
    return bad_total


if __name__ == '__main__':
    args = sys.argv[1:]
    N = 300
    if args and args[0].isdigit():
        N = int(args.pop(0))
    names = args or ['time', 'landing', 'score', 'promo', 'awards', 'icons']
    tot = sum(run(n, N) for n in names)
    sys.exit(1 if tot else 0)
