"""S3.9 screen-level differential test: real END screens under Unicorn (drawing/file/sound calls stubbed and logged) vs s39_ref_ui.
usage: python s39_test_ui.py [N] [name ...]
names: event status replay summary awards quips scenes cbs main mapend      (default all)"""
import os, random, sys
_here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _here)
from s39_emu import Emu, DSB, CB, s16            # noqa: E402
import s39_ref as R                              # noqa: E402
import s39_ref_ui as U                           # noqa: E402
import s39_test as T                             # noqa: E402


def install(e, script=None):
    """near stubs for every drawing / file / wait call that END's screen code makes"""
    script = script if script is not None else {}

    def ctxn(c): return 'L' if c >= 0xE000 else c

    def rec_text(s, a):
        ctx, st, x, y, w, lh, c = a
        sv = s.cstr(st)
        if ctx >= 0xE000:
            s.w(ctx + 0xA, y + U.layout_height(sv))
        return ('text', ctx, sv, x, y, w, lh, c)[1:] if False else (ctxn(ctx), sv, x, y, w, lh, c)
    e.near_stub(0x1a75, 'text', 7, rec=rec_text)
    e.near_stub(0x112a, 'shtext_c', 4, rec=lambda s, a: (ctxn(a[0]), s.cstr(a[1]), a[2], a[3]))
    e.near_stub(0x19c3, 'shtext', 4, rec=lambda s, a: (ctxn(a[0]), s.cstr(a[1]), a[2], a[3]))
    e.near_stub(0x1901, 'screen', 2, rec=lambda s, a: (s.cstr(a[0]), a[1]))
    e.near_stub(0x49d2, 'fill', 5)
    e.near_stub(0x11a2, 'wait', 1)
    e.near_stub(0x4206, 'line', 6)
    e.near_stub(0x100d, 'loadpic', 2, rec=lambda s, a: (s.cstr(a[0]), a[1]))
    e.near_stub(0x0fa4, 'readfile', 3, rec=lambda s, a: (s.cstr(a[0]),))
    e.near_stub(0x0fda, 'readpal', 2, rec=lambda s, a: (s.cstr(a[0]),))
    e.near_stub(0x4722, 'decode', 2, rec=lambda s, a: ())
    e.near_stub(0x42be, 'setpal', 0)
    e.near_stub(0x2092, 'wait_input', 1)
    e.near_stub(0x1992, 'sprite', 4)
    e.near_stub(0x185c, 'palq', 3)
    if 'hit' in script:
        e.near_stub(0x1c3b, 'hit', 2, ret=lambda s_, a: script['hit'])
    if 'env' in script:
        env = script['env']
        e.near_stub(0x4393, 'pal', 0)
        e.near_stub(0x44b9, 'mpoll', 0)
        e.near_stub(0x44a4, 'mbtn', 1, ret=lambda s_, a: env.pop(env.mouse))
        e.near_stub(0x3e61, 'quit', 1)
        e.slot_ret[93] = lambda s_: env.pop(env.joy)
        e.slot_nargs[93] = 1
        e.slot_ret[90] = lambda s_: 1 if env.keys_empty() else 0
        e.slot_ret[91] = lambda s_: env.keys.pop(0)
    for addr, nm in script.get('names', {}).items():
        e.near_stub(addr, nm, 0)
    if 'exit' in script:
        e.near_stub(0x3f44, 'exit', 1)
    if 'menu' in script:
        e.near_stub(0x1baa, '1baa', 1, ret=lambda s, a: script['menu'].pop(0), rec=lambda s, a: ())
    if 'replay' in script:
        e.near_stub(0x0714, '0714', 1, ret=lambda s, a: script['replay'].pop(0))


def norm_calls(calls):
    out = []
    for c in calls:
        if c[0] == 'slot':
            sl = c[1]
            if sl == 42: out.append(('blit',) + tuple(c[2:]))
            elif sl == 101: out.append(('snd', c[2]))
            elif sl == 69: out.append(('screen_off',))
            elif sl == 70: out.append(('screen_on',))
            else: out.append(c)
        else:
            out.append(tuple(c))
    return out


def fix_prompt():
    """206a is not stubbed (it only calls 112a + 2092): redefine the reference accordingly"""
    def prompt(m, x, y):
        m.sw(0x6d98, 3)
        m.calls.append(('shtext_c', U.CTX0, m.cstr(0x2dc), x, y))
        m.calls.append(('wait_input', 0xffff))
    U.prompt = prompt


def rnd_ui_world(rng, m, event_ok=True):
    ns = T.rnd_world(rng, m)
    # name pool + pointer table
    pool = bytearray()
    for _ in range(140):
        pool += bytes(rng.choice(b'abcdefghijklmnopqrstuvwxyz ') for _ in range(rng.randint(1, 4))) + b'\0'
    m.ds[R.NAMES:R.NAMES + len(pool)] = pool
    U.name_table(m)
    nsites = m.w(R.NSITES)
    for i in range(nsites):
        o = R.SITES + 16 * i
        m.sw(o, rng.choice([0, rng.randint(1, 100)]))
        m.sw(o + 0xe, rng.randint(0, 127))
    nm = b'Capt. ' + bytes(rng.choice(b'ABCDEFGH') for _ in range(rng.randint(3, 9)))
    m.ds[0x7228:0x7228 + len(nm) + 1] = nm + b'\0'
    for j in range(2):
        m.sw(0x71e8 + 0x12 * j + 8, rng.randint(0, 81))
    m.sw(0x71ec, rng.randint(0, nsites - 1))
    # constrain event args so every string index stays inside its table
    for k in range(ns):
        o = R.LOG + 6 * k
        t = m.b(o + 4) & 0x3f
        a = m.b(o + 5)
        if t in (3, 6): m.sb(o + 5, (a & 0x80) | rng.randint(0, 28))
        elif t == 4: m.sb(o + 5, rng.randint(0, 3))
        elif t == 5: m.sb(o + 5, rng.randint(0, 7))
        elif t in (1, 12, 7, 10): m.sb(o + 5, rng.randint(0, nsites - 1))
        elif t == 2: m.sb(o + 5, rng.randint(0, 127))
        elif t == 11 and not (m.b(o + 4) & 0xc0): m.sb(o + 4, m.b(o + 4) | rng.choice([0x40, 0x80, 0xc0]))   # VGAME always logs cargo delivery as an objective
    for j in range(4):
        m.sc(0x38 + 2 * j, rng.randint(0, 18))
    m.ds[0x5158:0x515c] = rng.randint(0, 0xffffffff).to_bytes(4, 'little')
    m.seed = int.from_bytes(m.ds[0x5158:0x515c], 'little')
    for a in (0x6982, 0x55e7):
        m.sb(a, 0)
    return ns


def run(name, N, seed=3):
    fix_prompt()
    rng = random.Random(seed * 977 + sum(map(ord, name)))
    bad_total = 0
    for k in range(N):
        e = Emu()
        m = R.W()
        ns = rnd_ui_world(rng, m)
        T.fixup(m)
        if name in ('awards', 'quips'):
            m.sb(0x6d89, rng.choice([0, 1])); m.sb(0x720e, rng.choice([0, 1]))
            m.sb(0x6982, rng.choice([0, 0, 1]))
            if rng.random() < 0.5: m.sc(0x30, 0)
            if rng.random() < 0.7: m.sc(0x28, 0)
            m.sb(0x6440, rng.choice([0, 3, 1, 2]))
            m.sp(0x36, rng.choice([1, 5, 9, 10, 30, 60, 98, 99, 99]))
            m.sp(0x20, rng.randint(0, 6))
            if name == 'quips':
                for a in (0x55e6, 0x643a):
                    m.sb(a, rng.choice([0, 0, 1]))
        script = {}
        res = None
        ref_ret = None
        ok = True
        if name == 'main':
            script = {'names': {0x3bf4: '3bf4', 0x2bec: '2bec', 0x03d9: '03d9', 0x2265: '2265', 0x39d0: '39d0'}, 'exit': 1}
            m.sc(0x26, rng.choice([1, 2, 3]))
            m.sp(0x36, rng.choice([1, 50, 98, 99, 5]))
        if name == 'waitin':
            ticks = rng.choice([0, 1, 2, 5, 8, 0xffff, 0xffff])
            m.sc(0x72, rng.randint(0, 3))
            jl = [rng.choice([0, 0, 0, 1]) for _ in range(rng.randint(0, 12))]
            ml = [rng.choice([0, 0, 0, 1]) for _ in range(rng.randint(0, 12))]
            kl = [rng.choice([0x1c0d, 0x011b, 0x3920, 0x1000, 0x1e61, 0x0000]) for _ in range(rng.randint(0, 4))]
            env = U.Env(jl, ml, kl)
            script = {'names': {}, 'env': env}
        if name == 'marks':
            n = rng.randint(0, 12)
            m.sw(0x54b8, n)
            for q in range(n):
                m.sb(0x5440 + 2 * q, rng.randint(0, m.w(R.NSITES) - 1)); m.sb(0x5441 + 2 * q, rng.choice([0, 0, rng.randint(3, 30)]))
                m.sw(0x54ba + 2 * q, rng.randint(0, 319)); m.sw(0x5532 + 2 * q, rng.randint(0, 168))
            for a in (0x720c, 0x5da0, 0x7226):
                m.sw(a, rng.randint(0x2000, 0x2fff))
            pick = rng.choice([0, 0, 1, 2, 3])
            waits = [rng.choice([0, 0, 0, 1, 2, 3]) for _ in range(20)]
            script = {'waits': list(waits)}
        if name == 'hover':
            m.sb(0x5b9e, rng.choice([0, 1, 2]))
            script = {'hit': rng.choice([0, 1, 2])}
        if name == 'init':
            m.sc(0x72, rng.randint(0, 3)); m.sp(0x20, rng.randint(0, 6))
            nm = bytes(rng.choice(b'abcdefghij ') for _ in range(rng.randint(1, 29))) + bytes([0])
            m.comm[R.PIL + 2:R.PIL + 2 + len(nm)] = nm
            for a in (0x1e, 0x1a, 0x1c): m.sc(a, rng.randint(0, 0xffff))
            script = {'names': {0x3ca1: '3ca1', 0x00c9: '00c9', 0x3cd0: '3cd0', 0x3d4f: '3d4f', 0x4b8c: '4b8c', 0x44e7: '44e7', 0x4557: '4557'}}
        if name == 'mapend':
            script = {'names': {0x02a4: '02a4'}, 'menu': [], 'replay': []}
            for _ in range(rng.randint(1, 4)):
                script['menu'].append(1)
            script['menu'].append(rng.choice([2, 0x63]))
            script['replay'] = [rng.randint(0, 20) for _ in range(len(script['menu']))]
            ref_menu = list(script['menu']); ref_rep = list(script['replay'])
        T.load_into(e, m)
        install(e, script)
        if name == 'event':
            cand = [i for i in range(ns) if (m.b(R.LOG + 6 * i + 4) & 0x3f) in (1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12)]
            if not cand:
                continue
            i = rng.choice(cand)
            rec = R.LOG + 6 * i
            if (m.b(rec + 4) & 0x3f) == 11 and not (m.b(rec + 4) & 0xc0):
                m.sb(rec + 4, m.b(rec + 4) | 0x80)
            T.load_into(e, m)
            e.call(0x0880, (rec, i))
            e.call(0x0a98, (rec, i))
            U.event_icon(m, rec, i)
            U.event_text(m, rec, i)
        elif name == 'status':
            i = rng.randint(0, max(ns - 1, 0))
            e.call(0x0da3, (i,))
            U.status(m, i)
        elif name == 'replay':
            i = rng.choice([0, 0, rng.randint(0, max(ns, 1))])
            ret, _ = e.call(0x0714, (i,))
            ref_ret = U.replay_step(m, i)
            res = ret
        elif name == 'summary':
            e.call(0x2bec, ())
            msg = U.summary_text(m)
            if msg is None:
                continue
        elif name == 'awards':
            ret, _ = e.call(0x2265, ())
            ref_ret = U.awards_screen(m) or 0
            res = ret
        elif name == 'quips':
            e.call(0x39d0, ())
            U.quips(m)
        elif name == 'scenes':
            which = rng.choice(['370a', '3b9a', '3bbf', '3887'])
            e.call(int(which, 16), ())
            {'370a': U.cemetery, '3b9a': U.rescue, '3bbf': U.tass, '3887': U.landed}[which](m)
        elif name == 'cbs':
            which = rng.choice(['3682', '3b41', '372f', '2200'])
            if which == '3682':
                m.sc(0x28, rng.randint(1, 6)); T.load_into(e, m)
                e.call(0x3682, ())
                U.crash_cb(m)
            else:
                if which == '372f':
                    # make the delta path reachable: first type-8 record at a later index
                    if rng.random() < 0.5:
                        for q in range(ns):
                            if (m.b(R.LOG + 6 * q + 4) & 0x3f) == 8: m.sb(R.LOG + 6 * q + 4, 9)
                        pos = rng.randint(0, 20)
                        m.sb(R.LOG + 6 * pos + 4, 8); m.sw(R.LOG + 6 * pos, rng.randint(0, 3000)); m.sb(R.LOG + 6 * 21 + 4, 0)
                    T.load_into(e, m)
                e.call(int(which, 16), ())
                {'3b41': U.rescue_cb, '372f': U.landed_cb, '2200': U.awards_cb}[which](m)
        elif name == 'main':
            e.call(0x0010, ())
            U.main_flow(m)
        elif name == 'mapend':
            e.call(0x03d9, ())
            m.calls.append(('02a4',))
            idx = ref_rep.pop(0); m.calls.append(('0714', 0))
            while True:
                m.calls.append(('1baa',))
                r = ref_menu.pop(0)
                if r == 1:
                    m.calls.append(('0714', idx)); idx = ref_rep.pop(0)
                    continue
                if r in (2, 0x63):
                    break
            U.map_screen_end(m)
        got = norm_calls(e.calls)
        want = [tuple(c) for c in m.calls]
        bad = []
        if got != want:
            for q in range(max(len(got), len(want))):
                g = got[q] if q < len(got) else None
                w_ = want[q] if q < len(want) else None
                if g != w_:
                    bad.append('op %d: real %r != ref %r' % (q, g, w_))
                    break
        if res is not None and (res & 0xffff) != ((ref_ret or 0) & 0xffff):
            bad.append('ret %04x != ref %04x' % (res & 0xffff, (ref_ret or 0) & 0xffff))
        bad += T.compare(e, m)
        if bad:
            bad_total += 1
            if bad_total <= 4:
                print('  MISMATCH %s case %d: %s' % (name, k, '; '.join(bad)[:600]))
    print('%-8s %5d cases, %d mismatches' % (name, N, bad_total))
    return bad_total


if __name__ == '__main__':
    args = sys.argv[1:]
    N = 100
    if args and args[0].isdigit():
        N = int(args.pop(0))
    names = args or ['waitin', 'marks', 'hover', 'init', 'event', 'status', 'replay', 'summary', 'awards', 'quips', 'scenes', 'cbs', 'main', 'mapend']
    tot = sum(run(n, N) for n in names)
    sys.exit(1 if tot else 0)
