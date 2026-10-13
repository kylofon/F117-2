"""S3.8 differential test: real VGAME code under Unicorn vs s38_ref.S38 (Python), byte for byte over DGROUP + pilot/comm
blocks, plus the ordered list of stubbed calls (sound, text, map, lamps, terrain probe, callees tested on their own).
usage: python s38_test.py [N] [name ...]     (names: sig radar site unit takeoff ai cm cmtick gun seeker blast; default all)
Needs `pip install unicorn` and work/unpacked/VGAME.EXE (read at run time, nothing is stored here)."""
import os, random, struct, sys
_here = os.path.dirname(os.path.abspath(__file__))
_root = _here
while _root and not os.path.exists(os.path.join(_root, 'PLAN.md')):
    _root = os.path.dirname(_root)
sys.path.insert(0, os.path.join(_root, 'port', 'notes', 's36'))
sys.path.insert(0, _here)
from r2_emu import Emu, DSEG                          # noqa: E402
from unicorn.x86_const import UC_X86_REG_SP, UC_X86_REG_AX   # noqa: E402
import r2_ref as R                                    # noqa: E402
from s38_ref import S38, UNIT, SITE, PROJ, DECOY, TRACER   # noqa: E402

CSEG, PSEG = 0x9000, 0x9100
PB, PD = 0xEF00, 0xEF02                               # pointer targets for 5460's *bearing / *dist


def probe(x, y, z): return ((x * 7 + y * 3 + z) >> 6) & 1


def install(e, stubs):
    """near stubs: addr -> (name, nargs, kind); kind 'str' logs the string at arg0, '49e7' writes S<i> to 9876, 'probe' sets C686."""
    uc = e.uc; ssb = DSEG * 16
    def mk(name, n, kind):
        def hk(uc_, addr, size, ud):
            sp = uc_.reg_read(UC_X86_REG_SP)
            a = struct.unpack('<%dH' % n, uc_.mem_read(ssb + sp + 2, 2 * n)) if n else ()
            if kind == 'str':
                o = a[0]; t = b''
                while True:
                    c = bytes(uc_.mem_read(ssb + o, 1))
                    if c == b'\0': break
                    t += c; o += 1
                e.calls.append((name, t))
            else:
                e.calls.append((name,) + tuple(a))
            if kind == '49e7':
                uc_.mem_write(ssb + 0x9876, b'S%d\0' % R.i16(a[0]))
            if kind == 'bad1':
                rv = bad1_effect(lambda o, d: uc_.mem_write(ssb + o, d), a[0], a[1], e.ns)
                uc_.reg_write(UC_X86_REG_AX, rv); e._ret_near(); return
            if kind == 'probe':
                uc_.mem_write(ssb + 0xc686, bytes([probe(*a)]))
            uc_.reg_write(UC_X86_REG_AX, 0); e._ret_near()
        return hk
    for addr, (name, n, kind) in stubs.items():
        uc.hook_add(0x4, mk(name, n, kind), begin=addr, end=addr)   # UC_HOOK_CODE


BASE_STUBS = {0xd6bf: ('d6bf', 2, ''), 0x8316: ('8316', 4, ''), 0x8548: ('8548', 4, ''), 0x8b93: ('8b93', 1, 'str'),
              0x499e: ('499e', 2, ''), 0x49e7: ('49e7', 1, '49e7'), 0x9770: ('9770', 0, ''), 0x973c: ('973c', 0, ''),
              0x94a5: ('94a5', 0, ''), 0x898b: ('898b', 2, ''), 0x44c4: ('44c4', 1, ''), 0x829d: ('829d', 1, ''),
              0x77d9: ('77d9', 3, 'probe'), 0xcba0: ('cba0', 1, '')}


def bad1_effect(write, x, y, ns):
    """bad1 stub: deterministic object index (or -1) and a fake world object at DS:EE00 pointed to by [9F10]."""
    r = ((x ^ (y >> 1)) >> 3) % (ns + 2) - 1
    write(0x9f10, struct.pack('<H', 0xee00))
    write(0xee04, struct.pack('<I', (((x + 5) & 0xffff) << 5) & 0xffffffff))
    write(0xee08, struct.pack('<I', ((0x8000 - ((y - 3) & 0xffff)) << 5) & 0xffffffff))
    return r & 0xffff


def rnd_world(e, rng, P):
    w = e.w
    rate = rng.choice([4, 4, 4, 5, 6, 8, 10, 15]); w(0x3660, rate)
    w(0x3d7a, rng.choice([1, 1, 2])); w(0xb16c, R.clamp_w(2 * rate, 3, 16)); w(0xb286, 250 * rate); w(0x9f64, 200 * rate)
    w(0x3654, rng.randint(0, 3)); w(0x3656, rng.randint(0, 2)); w(0x3658, rng.randint(0, 1))
    w(0x3d60, rng.randint(0, 0x7fff)); w(0x98e2, rng.randint(0, 0x3fff))
    w(0x3d68, rng.choice([-1, -1, 0, 3]) & 0xffff)
    w(0x94f2, rng.randint(0, 3)); w(0xb054, rng.randint(0, 6))
    w(0x3d62, rng.choice([0, 0, 1, 100 * rate, 210 * rate, 250 * rate]))
    w(0xc0a4, rng.randint(0x100, 0x7e00)); w(0xc0b2, rng.randint(0x200, 0x7d00))
    a = rng.choice([0, rng.randint(0, 3000), rng.randint(0, 0x5a98)]); w(0x2df4, a)
    w(0x2dee, rng.randint(0, 0xffff)); w(0x2df0, rng.randint(-0x3000, 0x3000) & 0xffff); w(0x2df2, rng.randint(0, 0xffff))
    w(0xb052, rng.choice([0, rng.randint(0, 8000), rng.randint(0, 0xafc8)]))
    w(0x3d5c, rng.choice([0, 10, 40, 60, 90, 120]))
    w(0x9b04, rng.choice([0, 0, 1, 4, 5, 0x10, 0x14, 0x15, 0x30, rng.randint(0, 0xffff)]))
    w(0xb2a0, rng.randint(0, 0x7fff)); w(0xb75a, rng.randint(0, 0x7fff)); w(0xb76e, rng.randint(0, 25000))
    w(0x39b2, rng.choice([0, 0, 0, 5])); w(0x39aa, rng.randint(0, 0x7fff)); w(0x39ac, rng.randint(0, 0x7fff))
    w(0x98f2, rng.choice([0x100, 0x600, 0x700, 0x800, 0x7fff]))
    w(0xb16e, rng.choice([1, -1]) & 0xffff); w(0xe2d8, rng.randint(0, 8)); w(0x98de, rng.randint(0, 1))
    w(0x926e, rng.randint(0, 0xffff)); w(0x9270, rng.randint(0, 0xffff))
    w(0xdeda, rng.choice([0, 0, 1])); w(0x365e, rng.choice([0, 1, 1])); w(0xdfdc, rng.choice([0x13, 0x14, 0x15, 0x18]))
    for k in range(256): e.wb(0xb174 + k, [rng.randint(0, 15)])
    ns = rng.randint(4, 30); w(0xc0b4, ns); w(0xe540, rng.randint(3, ns)); w(0x98fc, rng.randint(1, ns))
    for i in range(ns + 1):
        r = SITE + 16 * i
        w(r, rng.randint(0, 20)); w(r + 2, rng.randint(0, 0x7fff)); w(r + 4, rng.randint(0, 0x7fff))
        w(r + 6, rng.choice([0, rng.randint(1, 22), rng.randint(1, 15)]))
        e.wb(r + 8, [rng.choice([0, 1, 1, 4, 0x10, 0x81, rng.randint(0, 255)]), rng.choice([0, 0, 1, 2, 4, 0x10, rng.randint(0, 255)])])
        w(r + 10, rng.choice([0, rng.randint(0, 255), rng.randint(0, 28), 0xc5, 0xfa])); w(r + 12, rng.randint(0, 50))
    w(0xdfe0, rng.randint(0, ns - 1))
    nu = rng.randint(1, 12); w(0xded0, nu)
    w(0x3d6e, rng.choice([-1, -1, rng.randint(0, nu - 1)]) & 0xffff)
    w(0x3d64, rng.choice([-1, rng.randint(0, nu - 1)]) & 0xffff)
    for u in range(nu):
        U = UNIT + 0x24 * u
        x = rng.randint(0x100, 0x7e00); y = rng.randint(0x100, 0x7e00)
        w(U, rng.randint(0, ns - 1)); w(U + 2, x); w(U + 4, y)
        w(U + 6, rng.choice([rng.randint(0, 30000), rng.randint(0, 400), 0x8c, 0x0c, rng.randint(-50, 30)]) & 0xffff)
        e.uc.mem_write(DSEG * 16 + U + 8, struct.pack('<II', (x << 5) + rng.randint(0, 31), (y << 5) + rng.randint(0, 31)))
        w(U + 0x10, rng.randint(0, 0xffff)); w(U + 0x12, rng.choice([0, rng.randint(-0x4000, 0x4000)]) & 0xffff)
        w(U + 0x14, rng.choice([0, rng.randint(-0x5000, 0x5000)]) & 0xffff)
        w(U + 0x16, rng.randint(0, 28))
        fl = 1 if rng.random() < 0.9 else 0
        for bit in (2, 2, 4, 8, 0x10, 0x20, 0x40, 0x80):
            if rng.random() < (0.6 if bit == 2 else 0.15): fl |= bit
        fl2 = 0
        for bit in (1, 2, 4, 8, 0x10, 0x20, 0x40):
            if rng.random() < 0.15: fl2 |= bit
        e.wb(U + 0x18, [fl, fl2])
        w(U + 0x1a, rng.choice([0, rng.randint(1, 150), rng.randint(100, 900)]))
        w(U + 0x1c, rng.choice([1, 2, rng.randint(1, 3000)])); w(U + 0x1e, rng.choice([0, rng.randint(16, 21), rng.randint(0, 39)]))
        w(U + 0x22, rng.randint(0, 255))
    for k in range(4):
        R_ = DECOY + 12 * k
        w(R_, rng.randint(0, 0x7fff)); w(R_ + 2, rng.randint(0, 0x7fff)); w(R_ + 6, rng.randint(0, 3)); w(R_ + 8, rng.choice([0, 0, 1, 2, 30]))
    for k in range(12):
        p = PROJ + 0x1c * k
        for f in range(0, 0x1c, 2): w(p + f, rng.randint(0, 0xffff))
        w(p + 0xe, rng.choice([0, 0, 5])); w(p + 6, rng.randint(0, 40)); w(p + 0x10, rng.randint(0, 39))
    for k in range(20):
        T = TRACER + 12 * k
        for f in range(0, 12, 2): w(T + f, rng.choice([0, rng.randint(0, 0xffff)]))
    for k in range(16): w(0x39da + 8 * k, rng.choice([0, rng.randint(1, 0x7fff)])); w(0x39de + 8 * k, rng.randint(0, 99))
    for n in (1, 2, 3): w(0x3636 + 2 * n, rng.choice([0, 1, 5]))
    w(0x3632, rng.choice([0, 0x40, rng.randint(0, 255)])); w(0xc5c8, rng.randint(0, 10))
    w(0x43be, rng.choice([0, 1])); w(0x3650, rng.choice([0, 30, 1000])); w(0xc06e, rng.choice([0, 0, 0, 1]))
    e.wb(0x9b05, [rng.choice([0, 0x10])])
    struct.pack_into('<H', P, 0x42, rng.choice([0, 0, 1]))


def focus(e, rng):
    """Steer the random world into the rare 5730 paths: gun attack, combat table, landing, roll-out, scramble."""
    r = lambda o: e.r(o); w = e.w
    rate = r(0x3660); nu = r(0xded0); ns = r(0xc0b4)
    w(0x3d60, r(0x3d60) & ~3 if rng.random() < 0.7 else r(0x3d60)); w(0x3d62, 250 * rate)
    for u in range(nu):
        U = UNIT + 0x24 * u; k = rng.randint(0, 4)
        x, y, z = r(U + 2), r(U + 4), rng.randint(1000, 20000); w(U + 6, z)
        h = rng.randint(0, 0xffff)
        if k == 0:                                     # fighter on the player's tail: mode 1, D < 0x200, on target
            e.wb(U + 0x18, [3, 0]); w(U + 0x10, h); w(U + 0x12, 0); w(U + 0x14, rng.choice([0, 0x800, 0x5000]))
            d = rng.randint(0x40, 0x3f0)
            w(0xb2a0, (x + R.sin_mul(h, d)) & 0xffff); w(0xb75a, (y - R.cos_mul(h, d)) & 0xffff); w(0xb76e, z)
        elif k == 1:                                   # landing approach near its base
            s_ = rng.randint(0, ns - 1); S = SITE + 16 * s_; w(U, s_)
            e.wb(U + 0x18, [7, rng.choice([0, 2])])
            ty = r(S + 4) + (R.i16(r(0xb16e)) * 0x280 if not (e.rb(U + 0x19, 1)[0] & 2) else 0)
            w(U + 2, (r(S + 2) + rng.randint(-8, 8)) & 0xffff); w(U + 4, (ty + rng.randint(-8, 8)) & 0xffff)
        elif k == 2:                                   # rolling out, speed may be negative
            e.wb(U + 0x18, [3, 0x10]); w(U + 0x1a, rng.choice([-5, 3, 50, 120]) & 0xffff)
        elif k == 3:                                   # on the ground, scramble candidate
            e.wb(U + 0x18, [rng.choice([1, 0x81]), 0])
            s_ = rng.randint(0, r(0xe540) - 1); S = SITE + 16 * s_
            e.wb(S + 8, [1, 0]); w(S + 10, r(U + 0x16)); w(S + 2, r(0xb2a0)); w(S + 4, r(0xb75a))
    w(0x98e2, rng.randint(0, 7) << (r(0x3656) + 4))


def weapons_world(e, rng):
    """Projectiles near their targets, valid types/stores, bays for 753f."""
    w = e.w; r = e.r; ns = r(0xc0b4); nu = r(0xded0)
    px, py = r(0xc0a4), r(0xc0b2)
    for k in range(12):
        p = PROJ + 0x1c * k
        if k < 8: typ = rng.choice([rng.randint(1, 21), 33, 39, rng.randint(1, 39)])
        else: typ = rng.choice([22, 23, 24, 25, 26, 27, 28, 29, 30, 38, 31])
        w(p + 0x10, typ); w(p + 0x12, rng.randint(0, 19))
        if k < 8: tx, ty = px, py
        else:
            if rng.random() < 0.5 and nu: U = UNIT + 0x24 * rng.randint(0, nu - 1); tx, ty = r(U + 2), r(U + 4)
            else: S = SITE + 16 * rng.randint(0, ns - 1); tx, ty = r(S + 2), r(S + 4)
        d = rng.choice([30, 100, 300, 600, 2000])
        a = rng.randint(0, 0xffff)
        w(p, (tx + R.sin_mul(a, d)) & 0xffff); w(p + 2, (ty - R.cos_mul(a, d)) & 0xffff)
        hb = R.heading(R.i16(tx - r(p)), R.i16(r(p + 2) - ty))
        w(p + 8, (hb + rng.choice([0, 0x200, -0x800, 0x3000, 0x8000])) & 0xffff)
        w(p + 4, rng.choice([r(0x2df4), rng.randint(0, 20000), 1, 5, -3 & 0xffff]))
        w(p + 0xa, rng.choice([0, 0x400, -0x400 & 0xffff, rng.randint(-0x2000, 0x2000) & 0xffff]))
        w(p + 6, rng.choice([1, 10, 20, 31, 40])); w(p + 0xe, rng.choice([0, 1, 3, 20, 200]))
        w(p + 0x14, rng.choice([-1, 0, rng.randint(1, ns - 1)]) & 0xffff)
        w(p + 0x16, rng.choice([0, -rng.randint(0, nu - 1), rng.randint(3, ns - 1), 0xe000]) & 0xffff)
        w(p + 0x18, rng.choice([r(0xb2a4), rng.randint(0, 0x7fff)])); w(p + 0x1a, rng.choice([r(0xb2a6), rng.randint(0, 0x7fff)]))
    w(0x3d66, rng.choice([-1, 0, rng.randint(1, ns - 1)]) & 0xffff); w(0xded6, rng.randint(0, 2))
    for k in range(4):
        w(0x363e + 4 * k, rng.choice([rng.randint(0, 19), 17, 19, 16])); w(0x3640 + 4 * k, rng.choice([0, 1, 3]))
    w(0x364e, rng.randint(0, 3)); w(0x2df6, rng.choice([0, 0x80, rng.randint(1, 50000)])); w(0xe55c, rng.randint(0, 1))
    w(0x2df2, rng.choice([0, 0x1000, 0x3800, 0xc800]))
    for k in range(128): e.wb(0xc604 + k, [rng.randint(0, 255)])
    for i in range(ns + 1): e.wb(SITE + 16 * i + 0xe, [rng.randint(0, 255)])


def weapons_focus(e, rng):
    """Player ground missiles on top of their target, bombs bursting next to the player, releases with a designated target."""
    w = e.w; r = e.r; ns = r(0xc0b4)
    w(0x3654, rng.choice([2, 3])); w(0x9b04, 4); w(0x2df2, 0); w(0x2df6, 3000); w(0x2df4, rng.choice([0, 10, 100]))
    t = rng.randint(1, ns - 1); S = SITE + 16 * t; w(0x3d66, t); w(0xe55c, 1)
    for k in range(8, 12):
        p = PROJ + 0x1c * k
        if rng.random() < 0.5:                       # ground missile 30 units short of the target, nose on
            typ = rng.choice([24, 25, 26, 27, 28]); w(p + 0x10, typ); w(p + 0x14, t); w(p + 0xe, 50)
            w(p + 0x18, r(S + 2)); w(p + 0x1a, r(S + 4)); w(S + 6, rng.choice([0, 5]))
            a = rng.randint(0, 0xffff); w(p, (r(S + 2) - R.sin_mul(a, 30)) & 0xffff); w(p + 2, (r(S + 4) + R.cos_mul(a, 30)) & 0xffff)
            w(p + 8, a); w(p + 4, rng.choice([5, 40])); w(p + 6, 40); w(p + 0xa, 0)
        else:                                        # bomb hitting the ground next to the player
            w(p + 0x10, rng.choice([28, 29, 30])); w(p + 0xe, 9); w(p + 4, -3 & 0xffff)
            w(p, (r(0xc0a4) + rng.randint(-40, 40)) & 0xffff); w(p + 2, (r(0xc0b2) + rng.randint(-40, 40)) & 0xffff)
    for k in range(4):
        w(0x363e + 4 * k, rng.choice([2, 3, 4, 5, 6, 9])); w(0x3640 + 4 * k, 2)
    if rng.random() < 0.5: w(PROJ + 0x1c * rng.randint(8, 11) + 0xe, 0)


def run_one(name, seed):
    rng = random.Random(seed * 7919 + sum(map(ord, name)))
    e = Emu()
    P = bytearray(0x100); C = bytearray(0x200)
    e.w(0xe548, 0); e.w(0xe54a, CSEG); e.w(0x98f4, 0); e.w(0x98f6, PSEG)
    stubs = dict(BASE_STUBS)
    if name in ('ai', 'aifocus'): stubs[0x62d0] = ('62d0', 1, ''); stubs[0x65d4] = ('65d4', 2, '')
    if name in ('proj', 'release', 'projfocus', 'relfocus'):
        stubs.update({0xbad1: ('bad1', 2, 'bad1'), 0x72a4: ('72a4', 1, ''), 0x71b4: ('71b4', 1, ''), 0x74f6: ('74f6', 0, ''),
                      0x81f6: ('81f6', 0, ''), 0x9208: ('9208', 0, ''), 0x7470: ('7470', 1, '')})
    install(e, stubs)
    rnd_world(e, rng, P)
    if name == 'aifocus': focus(e, rng)
    e.uc.mem_write(PSEG * 16, bytes(P)); e.uc.mem_write(CSEG * 16, bytes(C))
    if name in ('proj', 'release', 'projfocus', 'relfocus'): weapons_world(e, rng)
    if name in ('projfocus', 'relfocus'): weapons_focus(e, rng)
    e.ns = e.r(0xc0b4)
    m0 = bytearray(e.rb(0, 0x10000))
    F = S38(m0, bytearray(P), bytearray(C)); F.probe = probe
    nu = F.i(0xded0); ns = F.i(0xc0b4)
    if name == 'radar':
        x, y, z = rng.randint(0, 0x7fff), rng.randint(0, 0x7fff), rng.choice([0, rng.randint(0, 25000)])
        rt = rng.choice([-1, 0, rng.randint(1, 22), rng.randint(1, 22)])
        ax, _ = e.call(0x5460, (x, y, z, rt & 0xffff, PB, PD))
        sig, brg, D = F.det_radar_signal(x, y, z, rt)
        if brg is not None: F.sw(PB, brg); F.sw(PD, D)
        res_e, res_r = R.i16(ax), sig
    elif name == 'sig':
        e.call(0x43ee, ()); F.log.append(('cba0', F.u(0xde90))); F.signature(); res_e = res_r = 0
    elif name == 'site':
        i = rng.randint(0, ns - 1); F.m[SITE + 16 * i + 6] = rng.randint(1, 22); e.wb(SITE + 16 * i + 6, [F.m[SITE + 16 * i + 6]])
        e.call(0x516d, (i,)); F.det_site_radar_update(i); res_e = res_r = 0
    elif name == 'unit':
        u = rng.randint(0, nu - 1); t = F.u(UNIT + 0x24 * u + 0x16); F.sw(0x94f0, t); e.w(0x94f0, t)
        F.uninit62 = (F.u(0xf7f0), F.u(0xf7ea))   # 62d0 locals [bp-0Ah] bearing, [bp-10h] D with bp = F7FA
        e.call(0x62d0, (u,)); F.ai_unit_radar(u); res_e = res_r = 0
    elif name == 'takeoff':
        u = rng.randint(0, nu - 1); s_ = rng.randint(0, ns - 1)
        e.call(0x65d4, (u, s_)); F.ai_unit_takeoff(u, s_); res_e = res_r = 0
    elif name in ('ai', 'aifocus'):
        e.call(0x5730, ()); F.ai_update_units(); res_e = res_r = 0
    elif name == 'cm':
        a = rng.randint(1, 3); e.call(0x4512, (a,)); F.wpn_release_countermeasure(a); res_e = res_r = 0
    elif name == 'cmtick':
        e.call(0x461c, ()); F.wpn_cm_tick(); res_e = res_r = 0
    elif name == 'gun':
        e.call(0x465b, ()); F.wpn_gun_update(); res_e = res_r = 0
    elif name == 'seeker':
        sl = rng.randint(0, 11); cls = rng.choice([-1, 0, 1, 2, 3, 7])
        tx, ty = rng.randint(0, 0x7fff), rng.randint(0, 0x7fff)
        if rng.random() < 0.5:
            p = PROJ + 0x1c * sl; tx = (F.u(p) + rng.randint(-300, 300)) & 0xffff; ty = (F.u(p + 2) + rng.randint(-300, 300)) & 0xffff
        ax, _ = e.call(0x711f, (sl, tx, ty, 0, cls & 0xffff)); res_e = R.i16(ax); res_r = F.wpn_seeker_check(sl, tx, ty, 0, cls)
    elif name in ('proj', 'projfocus'):
        F.tz = R.i16(F.u(0xf7ca)); F.tgt = R.i16(F.u(0xf7ea))     # 671a locals [bp-32h] tz, [bp-12h] target (bp = F7FC)
        F.bad1 = lambda x, y: R.i16(bad1_effect(lambda o, d: F.m.__setitem__(slice(o, o + len(d)), d), x, y, ns))
        e.call(0x671a, ()); F.wpn_proj_update_all(); res_e = res_r = 0
    elif name in ('release', 'relfocus'):
        e.call(0x753f, ()); F.wpn_release(); res_e = res_r = 0
    elif name == 'blast':
        e.call(0x74f6, ()); F.dmg_player_blast(); res_e = res_r = 0
    m1 = e.rb(0, 0x10000)
    diffs = [o for o in range(0xef80) if F.m[o] != m1[o]]
    p1 = bytes(e.uc.mem_read(PSEG * 16, 0x100)); c1 = bytes(e.uc.mem_read(CSEG * 16, 0x200))
    if p1 != bytes(F.P): diffs.append('pilot')
    if c1 != bytes(F.C): diffs.append('comm')
    ecalls = [('snd', c[1]) if c[0] == 101 else c for c in e.calls]
    elog = [tuple(x if isinstance(x, (bytes, str)) else x & 0xffff for x in c) for c in ecalls]
    rlog = [tuple(x if isinstance(x, (bytes, str)) else x & 0xffff for x in c) for c in F.log]
    return diffs, elog == rlog, res_e == res_r, elog, rlog, (res_e, res_r)


ALL = ['radar', 'sig', 'site', 'unit', 'takeoff', 'ai', 'aifocus', 'cm', 'cmtick', 'gun', 'seeker', 'blast', 'proj', 'release', 'projfocus', 'relfocus']

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    names = sys.argv[2:] or ALL
    tot = 0
    for name in names:
        bad = 0
        for seed in range(n):
            diffs, ok_log, ok_res, el, rl, res = run_one(name, seed)
            if diffs or not ok_log or not ok_res:
                bad += 1
                if bad <= 3:
                    print(name, 'seed', seed, 'diffs', [hex(d) if isinstance(d, int) else d for d in diffs[:10]], len(diffs),
                          'log', ok_log, 'res', res)
                    if not ok_log:
                        for k, (a, b) in enumerate(zip(el + [None] * 9, rl + [None] * 9)):
                            if a != b: print('   #%d emu %s\n       ref %s' % (k, a, b)); break
        tot += bad
        print('%-8s %d cases, %d mismatching' % (name, n, bad))
    print('total mismatching', tot)
