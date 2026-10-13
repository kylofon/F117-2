"""S3.9 Python reference, screen level: the debrief screens as lists of UI operations (m.calls), plus every text that is built.
Ops (same tuples as the Unicorn stubs in s39_test_ui.py record):
  ('blit', spage, sx, sy, dpage, dx, dy, w, h)     MGRAPHIC slot 42
  ('text', ctx, bytes, x, y, width, line_h, centre) 1a75 ui_text_wrap  (ctx 'L' = a local copy used for measuring)
  ('prompt', x, y)   206a   ('screen', name, cb)   1901   ('fill', ctx, x0, y0, x1, y1)  49d2   ('line', ctx, x0, y0, x1, y1, col)  4206
  ('shtext', ctx, bytes, x, y)  19c3     ('shtext_c', ctx, bytes, cx, y)  112a     ('wait', n)  11a2     ('snd', n)  slot 101
  ('screen_off',) ('screen_on',) ('readfile', name) ('readpal', name) ('decode',) ('setpal',) ('loadpic', name, page) ('wait_input', ticks)
"""
import struct
from s39_ref import *

CTX0, CTX1 = 0x6d8c, 0x6da2


def fmt(f, *a):
    """printf subset: %s (bytes), %d (signed 16 bit)."""
    out = bytearray()
    i = 0
    k = 0
    while i < len(f):
        c = f[i:i + 1]
        if c == b'%':
            d = f[i + 1:i + 2]
            if d == b's':
                out += a[k]; k += 1
            elif d == b'd':
                out += str(s16(a[k])).encode(); k += 1
            i += 2
        else:
            out += c; i += 1
    return bytes(out)


def layout_height(s):
    """test device: what the 1a75 stub writes into the local measuring context (+0A) = y + 8 * lines"""
    return 8 * (s.count(b'\n') + 1)


def blit(m, *a):
    m.calls.append(('blit',) + tuple(x & 0xffff for x in a))


def text(m, ctx, s, x, y, w, lh, c):
    m.calls.append(('text', ctx, s, x & 0xffff, y & 0xffff, w, lh, c))


def prompt(m, x, y):
    """206a: font 3, 'Press Selector to continue' centred (shadow), wait for a key"""
    m.sw(0x6d98, 3)
    m.calls.append(('prompt', x, y))


def name_ptr(m, idx):
    return m.w(0x6dfa + 2 * idx)


def name_str(m, idx):
    return m.cstr(name_ptr(m, idx))


def name_table(m):
    """00c9: name pointer table [6dfa] from the NUL separated pool [6efa]"""
    m.sw(0x6dfa, NAMES)
    j = 1
    for i in range(0x2ee):
        if m.b(NAMES + i) == 0 and j < 128:
            m.sw(0x6dfa + 2 * j, NAMES + i + 1)
            j += 1


# ------------------------------------------------------------------ 0e82 / 0880 / 0a98 / 0da3 / 0714
def erase_icon(m):
    blit(m, 2, 0, 0xa0, 1, m.w(0x55ae), m.w(0x55b0), 0x30, 0x28)
    blit(m, 2, 0, 0xa0, 0, m.w(0x55ae), m.w(0x55b0), 0x30, 0x28)


def event_icon(m, rec, idx):
    """0880 draw_event_icon(rec, idx)"""
    r4 = m.b(rec + 4); t = r4 & 0x3f
    icon = None
    if t == 1:
        icon = 0xf if m.b(SITES + 16 * (m.b(rec + 5) & 0x7f) + 8) & 8 else 0
    elif t in (2, 12):
        icon = 2
    elif t == 3: icon = 1
    elif t == 4: icon = 6
    elif t == 5: icon = 3
    elif t == 6: icon = 5
    elif t == 7: icon = 4
    elif t == 8:
        if idx == 0: icon = 8
        elif m.c(0x26) == 3: icon = 7
        elif m.c(0x26) == 1: icon = 0xe
        elif m.b(0x6440) == 0: icon = 0xb
        else: icon = 0xd
    elif t == 10: icon = 0xa
    elif t == 11:
        j = 0 if r4 & 0x80 else 1
        icon = 9 if m.w(0x1456 + 12 * m.w(0x71e8 + 0x12 * j + 8) + 4) == 3 else 0xc
    X = map_x(m.b(rec + 2)); Y = map_y(m.b(rec + 3))
    if X < 0x73:
        if Y < 0x59: X += 10; Y += 10
        else: X += 10; Y -= 0x28
    else:
        if Y < 0x59: X -= 0x3a; Y += 10
        else: X -= 0x3a; Y -= 0x28
    X &= 0xffff; Y &= 0xffff
    m.sw(0x55ae, X); m.sw(0x55b0, Y)
    blit(m, 1, X, Y, 2, 0, 0xa0, 0x30, 0x28)
    blit(m, 2, (icon % 6) * 0x30, (icon // 6) * 0x28, 1, X, Y, 0x30, 0x28)
    blit(m, 1, X, Y, 0, X, Y, 0x30, 0x28)


def event_text(m, rec, idx):
    """0a98 draw_event_text(rec, idx): text of one timeline event in the 110x16 box at (140,180)"""
    r4 = m.b(rec + 4); t = r4 & 0x3f
    o = m.b(rec + 5) & 0x7f
    site = SITES + 16 * o
    unit = 0x179e + 32 * o
    if r4 & 0x80: noun = m.cstr(0xe0)
    elif r4 & 0x40: noun = m.cstr(0xf2)
    else: noun = name_str(m, m.b(site + 0xe))
    place = m.w(site)
    buf = b''
    if t in (1, 12):
        if (m.b(OBJCLS + (m.b(site + 0xe) & 0x7f)) & 0xf) == 0xd:
            buf = fmt(m.cstr(0x106), name_str(m, place))
        elif place != 0:
            buf = fmt(m.cstr(0x111), noun, name_str(m, place))
        else:
            buf = fmt(m.cstr(0x124), noun)
    elif t == 2:
        if r4 & 0x80: buf = m.cstr(0x141)
        elif r4 & 0x40: buf = m.cstr(0x15d)
        elif (m.b(OBJCLS + o) & 0xf) == 0xd:
            buf = fmt(m.cstr(0x17b), name_str(m, o))
        else:
            buf = fmt(m.cstr(0x186), name_str(m, o))
    elif t == 3:
        buf = fmt(m.cstr(0x131), m.cstr(unit), m.cstr(unit + 7))
    elif t == 4:
        wid = m.c(0x38 + 2 * o)
        buf = fmt(m.cstr(0x242), m.cstr(m.w(0x1b98 + 2 * wid)))
    elif t == 5:
        buf = fmt(m.cstr(0x205), m.cstr(m.w(0x1b3e + 2 * o)))
    elif t == 6:
        buf = fmt(m.cstr(0x232), m.cstr(unit), m.cstr(unit + 7))
    elif t == 7:
        if place != 0:
            buf = fmt(m.cstr(0x217), name_str(m, place), noun)
        else:
            buf = fmt(m.cstr(0x223), noun)
    elif t == 8:
        if idx == 0:
            sl = m.w(0x71ec)
            nm = m.w(SITES + 16 * sl)
            if nm == 0:
                nm = m.b(SITES + 16 * sl + 0xe)
            buf = fmt(m.cstr(0x24e), name_str(m, nm))
        else:
            buf = m.cstr(0x260)
            c26 = m.c(0x26)
            if c26 == 1: buf += m.cstr(0x26e)
            elif c26 == 2:
                if m.c(0x28) == 0 and m.b(0x6440) != 0: buf += m.cstr(0x276)
                elif m.c(0x28) == 0 and m.b(0x6440) == 0: buf += m.cstr(0x283)
                else: buf += m.cstr(0x28c)
            elif c26 == 3: buf += m.cstr(0x29a)
    elif t == 10:
        if place != 0:
            buf = fmt(m.cstr(0x1df), noun, name_str(m, place))
        else:
            buf = fmt(m.cstr(0x1f5), noun)
    elif t == 11:
        buf = m.cstr(0x193)
        if r4 & 0x80: buf += m.cstr(0x1a3)
        elif r4 & 0x40: buf += m.cstr(0x1c0)
    m.sw(CTX1 + 4, 0xe2)
    m.calls.append(('fill', CTX1, 0x8c, 0xb4, 0xee, 0xc3))
    m.sw(CTX1 + 0xc, 3); m.sw(CTX1 + 4, 0xf); m.sw(CTX1 + 6, 8)
    text(m, CTX1, buf, 0x8c, 0xb4, 0x6a, 8, 1)
    blit(m, 1, 0x8c, 0xb4, 0, 0x8c, 0xb4, 0x6e, 0x10)


def status(m, idx):
    """0da3 draw_status(idx): clock, running score, 'In Flight'"""
    m.sw(CTX1 + 4, 0xe2)
    m.calls.append(('fill', CTX1, 0x4b, 0xb4, 0xf8, 0xc3))
    m.sw(CTX1 + 0xc, 3); m.sw(CTX1 + 4, 0xf); m.sw(CTX1 + 6, 8)
    tk = m.w(LOG + 6 * idx)
    m.calls.append(('shtext', CTX1, fmt(m.cstr(0x2a7), fmt_time(m, tk)), 0x4e, 0xb4))
    sc = score(m, idx)
    m.calls.append(('shtext', CTX1, fmt(m.cstr(0x2b0), sc), 0x4e, 0xbc))
    if idx != 0:
        m.calls.append(('shtext_c', CTX1, m.cstr(0x2bb), 0xc3, 0xb8))
    blit(m, 1, 0x4b, 0xb4, 0, 0x4b, 0xb4, 0xae, 0x11)


def replay_step(m, idx):
    """0714 replay_step(idx) -> next idx (0 = log finished and map restored)"""
    if idx != 0:
        erase_icon(m)
    rec = (LOG + 6 * idx) & 0xffff
    while True:
        t = m.b(rec + 4) & 0x3f
        stop = 1
        if t == 9: stop = 0
        elif m.p(0x3a) == 2 and t in (6, 7): stop = 0
        if idx == 0:
            m.sw(0x55aa, map_x(m.b(rec + 2))); m.sw(0x55ac, map_y(m.b(rec + 3)))
        elif t != 0:
            x = map_x(m.b(rec + 2)); y = map_y(m.b(rec + 3))
            m.calls.append(('line', CTX0, m.w(0x55aa), m.w(0x55ac), x, y, 0))
            m.calls.append(('line', CTX1, m.w(0x55aa), m.w(0x55ac), x, y, 0))
            m.sw(0x55aa, x); m.sw(0x55ac, y)
        status(m, idx)
        m.calls.append(('wait', 8))
        if not stop:
            rec = (rec + 6) & 0xffff; idx += 1
        else:
            break
    if t != 0:
        event_icon(m, rec, idx)
        old = idx
        idx += 1
        event_text(m, rec, old)
        return idx
    blit(m, 4, 0, 0, 0, 0, 0, 320, 169)
    blit(m, 4, 0, 0, 1, 0, 0, 320, 169)
    status(m, 0)
    return 0


# ------------------------------------------------------------------ 2bec summary text
def summary_text(m):
    """2bec: builds the debrief paragraph and shows it on flag.pic; returns the message bytes (or None if the original would print stack garbage)"""
    rating = score(m, 0x100)
    m.calls += [('screen_off',), ('readfile', b'flag.pic'), ('decode',), ('readpal', b'flag.pal'), ('setpal',), ('screen_on',)]
    obj = (m.b(0x6d89) << 1) | m.b(0x720e)
    on = lambda: m.cstr(m.w(0xdae + 2 * obj))
    name = m.cstr(0x7228)
    c26, c28, c30 = m.c(0x26), m.c(0x28), m.c(0x30)
    if c28 != 0 and c30 != 0:
        tmp = fmt(m.cstr(0x7d1), on()) if obj else m.cstr(0x7f5)
        buf = fmt(m.cstr(0x817), name, tmp)
    elif c28 != 0:
        buf = fmt(m.cstr(0x860 if obj else 0x89b), name)
    elif c26 != 1 and m.b(0x6440) == 0:
        tmp = fmt(m.cstr(0x8ce), on()) if obj else m.cstr(0x907)
        buf = fmt(m.cstr(0x964), name, tmp)
    elif c26 == 2 and m.b(0x6440) != 0:
        buf = fmt(m.cstr(0x9b3), name, on()) if obj else fmt(m.cstr(0x9f1), name)
    elif c26 == 3:
        buf = fmt(m.cstr(0xa44), name, on()) if obj else fmt(m.cstr(0xa71), name)
    else:
        return None
    msg = buf + m.cstr(0xab8)
    tmp = b'' if c30 == 0 else m.cstr(0xabb)
    msg += fmt(m.cstr(0xaf5), rating, tmp)
    air = m.b(0x64c8) + m.b(0x55dc)
    if air > 0:
        if 1 < air < 12: msg += fmt(m.cstr(0xb29), air)
        elif air >= 12: msg += fmt(m.cstr(0xb4d), air)
        else: msg += m.cstr(0xba1)
    gnd = m.b(0x6984) + m.b(0x6d88)
    if gnd > 0:
        msg += m.cstr(0xbc2) if gnd == 1 else fmt(m.cstr(0xbeb), gnd)
    war = m.p(0x3a)
    civ = m.b(0x5b9a)
    if war == 0:
        mil = m.b(0x55dc) + m.b(0x6d88)
        if civ or mil:
            if civ and mil: tmp = fmt(m.cstr(0xc1b), civ, mil)
            elif mil: tmp = fmt(m.cstr(0xc44), mil)
            else: tmp = fmt(m.cstr(0xc5e), civ)
            b2 = fmt(m.cstr(0xc6b), tmp)
            b2 += m.cstr(0xc98) if (civ + mil) > 1 else m.cstr(0xcb2)
            msg += b2
    elif war == 1 and civ:
        b2 = fmt(m.cstr(0xcca), civ)
        b2 += m.cstr(0xd01) if civ > 1 else m.cstr(0xd1b)
        msg += b2
    if m.b(0x55db) or m.b(0x55e8):
        msg += m.cstr(0xd32)
        if m.b(0x55db) and m.b(0x55e8): msg += m.cstr(0xd77)
        elif m.b(0x55db): msg += m.cstr(0xd93)
        else: msg += m.cstr(0xd9d)
    m.ds[0x6986:0x6986 + len(msg) + 1] = msg + b'\0'
    m.sw(0x6d98, 4); m.sw(0x6d90, 0xfd); m.sw(0x6d92, 0xfe)
    h = layout_height(msg)
    text(m, 'L', msg, 0, 0, 0x104, 8, 0)
    text(m, CTX0, msg, 0x1e, 0xb8 - h, 0x104, 8, 1)
    prompt(m, 0xa0, 0xc0)
    m.calls.append(('snd', 0))
    return msg


# ------------------------------------------------------------------ awards screen texts
def show_award_text(m, s):
    """26a9"""
    blit(m, 1, 0x98, 0x13, 0, 0x98, 0x13, 0x9e, 0x34)
    m.sw(0x6d98, 4)
    text(m, CTX0, s, 0x98, 0x13, 0x9e, 8, 1)
    prompt(m, 0xe7, 0x3f)


def rank_picture(m):
    """28ed"""
    blit(m, 3, 0xb4, 0, 1, 0xdb, 0x4f, 0x50, 0x70)
    r = m.p(0x20)
    if r in (1, 2, 3, 4, 6):
        sy = (r - 1) * 0x1d
        blit(m, 3, 0x11e, sy, 1, 0xf3, 0x62, 0x22, 0x1d)
        blit(m, 3, 0x11e, sy, 1, 0xf3, 0x8d, 0x22, 0x1d)
    elif r == 5:
        blit(m, 3, 0x119, 0x74, 1, 0xf1, 0x62, 0x27, 0x1d)
        blit(m, 3, 0xf2, 0x74, 1, 0xf1, 0x8d, 0x27, 0x1d)
    blit(m, 1, 0xdb, 0x4f, 0, 0xdb, 0x4f, 0x50, 0x70)


def promo_text(m):
    """2a01"""
    shown = 0
    buf = b''
    name = m.cstr(0x7228)
    if m.b(0x55e6):
        pre = m.cstr(0x703) if m.c(0x28) == 0 else m.cstr(0x704)
        rk = m.cstr(m.w(0x5f2 + 2 * m.p(0x20)))
        buf = fmt(m.cstr(m.w(0x4f0f)), name, pre, rk)
        rank_picture(m)
        shown = 1
    elif m.b(0x55e7):
        rk = m.cstr(m.w(0x5f4 + 2 * m.p(0x20)))
        buf = fmt(m.cstr(m.w(0x4f0d)), name, rk)
        shown = 1
    elif not (m.b(0x643a) or m.b(0x55dd) or m.b(0x55e2) or m.b(0x55da)):
        buf = fmt(m.cstr(0x712), name)
        shown = 1
    if shown:
        show_award_text(m, buf)
    if m.p(0x36) == 0x63 and m.c(0x28) == 0:
        show_award_text(m, m.cstr(m.w(0x4f11)))
    if m.b(0x55e6) and m.b(0x643a) == 1:
        m.sb(0x643a, 0)


def medal_text(m):
    """2703"""
    blit(m, 4, 0xdb, 0x4f, 0, 0xdb, 0x4f, 0x50, 0x70)
    blit(m, 4, 0xdb, 0x4f, 1, 0xdb, 0x4f, 0x50, 0x70)
    name = m.cstr(0x7228)
    md = m.b(0x643a)
    if md:
        pre = m.cstr(0x61f) if m.c(0x28) != 0 else m.cstr(0x62d)
        buf = fmt(m.cstr(0x62e), name, pre, m.cstr(m.w(0x5d2 + 2 * md)))
        blit(m, 3, 0xb4, 0, 1, 0xdb, 0x4f, 0x50, 0x70)
        blit(m, 3, ((md - 1) % 3) * 0x3c, ((md - 1) // 3) * 100, 1, 0xe7, 0x57, 0x3c, 0x60)
        blit(m, 1, 0xdb, 0x4f, 0, 0xdb, 0x4f, 0x50, 0x70)
        show_award_text(m, buf)
    if m.b(0x55dd):
        show_award_text(m, fmt(m.cstr(0x647), name))
    if m.b(0x55e2):
        f = 0x6a4 if (md or m.b(0x55dd)) else 0x6d6
        show_award_text(m, fmt(m.cstr(f), name, m.cstr(m.w(0x5ea + 2 * m.b(0x55e2))), m.p(0x36)))
    if m.b(0x55da):
        f = m.w(0x4f09) if m.c(0x30) != 0 else m.w(0x4f0b)
        show_award_text(m, fmt(m.cstr(f), m.cstr(m.w(0x5de + 2 * m.b(0x55da)))))


def awards_screen(m):
    """2265"""
    if m.c(0x30) != 0 and m.b(0x6d89) == 0 and m.b(0x720e) == 0:
        return 0
    for a in (0x55e2, 0x55dd, 0x643a, 0x55e7, 0x55da, 0x55e6):
        m.sb(a, 0)
    m.calls.append(('screen', b'awards', 0x2200))
    m.sw(0x6d90, 0xfd); m.sw(0x6d92, 0xfe)
    promotion(m)
    awards(m)
    if m.b(0x643a) or m.b(0x55dd) or m.b(0x55e2) or m.b(0x55e6):
        m.calls.append(('snd', 0x24))
    promo_text(m)
    medal_text(m)
    m.calls.append(('snd', 0))


# ------------------------------------------------------------------ ending scenes
CRASH = {1: 0xe90, 2: 0xec2, 3: 0xef1, 4: 0xf24, 5: 0xf68, 6: 0xfa6}


def crash_cb(m):
    """3682 (callback of the 'cemetary' screen); None for codes without a text (the original prints stack garbage)"""
    c = m.c(0x28)
    if c not in CRASH:
        return None
    m.sw(0x6d98, 4); m.sw(0x6d90, 0xfd); m.sw(0x6d92, 0xfe)
    text(m, CTX0, m.cstr(CRASH[c]), 0xaf, 0x28, 0x78, 10, 1)
    return 1


def quips(m):
    """39d0 ending_scene (the funny last screen); uses rand_n"""
    if m.c(0x28) != 0 or m.c(0x30) != 0:
        return
    m.sw(0x6d98, 4); m.sw(0x6d90, 0xfd); m.sw(0x6d92, 0xfe)
    n = m.p(0x36); rk = m.p(0x20)
    if not m.b(0x55e6) and not m.b(0x643a) and not m.b(0x6d89) and not m.b(0x720e) and n != 0x63:
        tab, cnt, scr, cb = 0x48e7, 8, 0, 0x398f
    elif (m.b(0x6d89) or m.b(0x720e)) and m.b(0x6440) == 0 and not m.b(0x55e6) and not m.b(0x643a):
        tab, cnt, scr, cb = 0x4510, 7, 0, 0x398f
    else:
        cb = 0
        if n == 0x63 and rk == 6: tab, cnt, scr = 0x4a4a, 4, 2
        elif n == 0x63 and rk != 6: tab, cnt, scr = 0x4e53, 4, 2
        elif m.b(0x55e6) or m.b(0x643a): tab, cnt, scr = 0x4c32, 5, 1
        elif m.c(0x2c) < 3: tab, cnt, scr = 0x4aeb, 5, 1
        else: tab, cnt, scr = 0x464e, 6, 1
    m.sb(0x55be, cnt); m.sw(0x55c0, tab)
    m.calls.append(('screen', m.cstr(m.w(0x1036 + 2 * scr)), cb))
    s = m.cstr(m.w(tab + 2 * m.rand_n(cnt)))
    text(m, CTX0, s, m.w(0x103c + 2 * scr), m.w(0x1042 + 2 * scr), m.w(0x1048 + 2 * scr), 8, 1)
    prompt(m, m.w(0x104e + 2 * scr), m.w(0x1054 + 2 * scr))


def cemetery(m):
    """370a: crash screen"""
    m.calls.append(('screen', b'cemetary', 0x3682)); m.calls.append(('snd', 0x25)); prompt(m, 0xeb, 0xac)


def rescue_cb(m):
    """3b41 (callback of 'rescue')"""
    msg = m.cstr(0x105a) + m.cstr(0x1096)
    m.sw(0x6d98, 4); m.sw(0x6d90, 0xfd); m.sw(0x6d92, 0xfe)
    text(m, CTX0, msg, 0xc8, 0x6d, 0x64, 8, 1)


def rescue(m):
    """3b9a"""
    m.calls.append(('screen', b'rescue', 0x3b41)); m.calls.append(('snd', 0x26)); prompt(m, 0xfa, 0xc0)


def tass(m):
    """3bbf"""
    m.sb(0x6982, 1)
    m.calls.append(('screen', b'tass', 0)); m.calls.append(('snd', 0x26))
    m.sw(0x6d90, 0xf); m.sw(0x6d92, 0)
    prompt(m, 0xa0, 0xc0)


def landed(m):
    """3887"""
    m.calls.append(('screen', b'landed1', 0x372f)); m.calls.append(('snd', 0x27)); m.calls.append(('wait', 0x78))
    blit(m, 2, 0, 0, 0, 0x33, 0x2c, 0xd2, 0x70); m.calls.append(('wait', 0xc))
    blit(m, 3, 0, 0, 0, 0x33, 0x1e, 0xd4, 0x60); m.calls.append(('wait', 0xc))
    blit(m, 4, 0, 0, 0, 0x34, 4, 0xd4, 0x6c); m.calls.append(('wait', 0xc))
    for i in range(4):
        blit(m, 4, 0xd6, i * 16, 0, 0x80, 0x7e, 0x26, 0x10); m.calls.append(('wait', 0xc))
    m.sw(0x6d90, 0xf); m.sw(0x6d92, 0)
    prompt(m, 0xa0, 0xac)
    m.calls.append(('snd', 0))


def landed_cb(m):
    """372f (callback of 'landed1'): loads landed2..4 and rebuilds the sky ramp (palette buffer entries 192..223 at DS:2165)"""
    for nm, pg in ((b'landed2.pic', 2), (b'landed3.pic', 3), (b'landed4.pic', 4)):
        m.calls.append(('loadpic', nm, pg))
    i = 0
    while i < 0x100 and (m.b(LOG + 6 * i + 4) & 0x3f) != 8:
        i += 1
    d = (m.w(LOG + 6 * i) - m.w(LOG)) & 0xffff
    if d > 0x708:
        d = 0x708
    if d == 0:
        for k in range(0x60):
            m.sb(0x2165 + k, m.b(0x1396 + k))
        return
    tg = 0x13f6 if m.cb(0x33) else 0x1336
    for k in range(32):
        for c in range(3):
            diff = m.b(tg + c) - m.b(0x1396 + 3 * k + c)
            m.sb(0x2165 + 3 * k + c, tdiv(diff * 0x708, d))


def ribbon_ids(m):
    arr = []
    k = 0
    for j in range(6):
        if m.p(0x22 + 2 * j) != 0: arr.append(k)
        k += 1
    for j in range(5):
        if m.p(0x44 + 2 * j) != 0: arr.append(k)
        k += 1
    j = 0
    while True:
        if len(arr) >= 12: break
        arr.append(k); k += 1; j += 1
        if j >= 3: break
    return arr


def draw_ribbon(m, rid, j, n):
    q = n // 3
    if n % 3 == 0: q -= 1
    if 3 * q > j: base = 0x82
    else: base = {0: 0x82, 1: 0x8d, 2: 0x88}[n % 3]
    dy = 0x95 + 2 * (j // 3 - q)
    dx = base + (j % 3) * 11
    blit(m, 3, 0x104, 2 * rid, 1, dx, dy, 0xb, 2)


def awards_cb(m):
    """2200 (callback of 'awards') + 22eb"""
    blit(m, 0, 0, 0, 1, 0, 0, 320, 200)
    blit(m, 0, 0, 0, 4, 0, 0, 320, 200)
    m.calls.append(('loadpic', b'medals.pic', 3))
    arr = ribbon_ids(m)
    for j, rid in enumerate(arr):
        draw_ribbon(m, rid, j, len(arr))
    blit(m, 1, 0, 0, 0, 0, 0, 320, 200)


def main_flow(m):
    """0010 with 3bf4, 2bec, 03d9, 2265, 39d0 and the exit replaced by ops"""
    m.calls.append(('3bf4',))
    if m.c(0x30) == 0:
        m.sp(0x36, m.p(0x36) + 1)
    m.sb(0x6440, 3)
    c26 = m.c(0x26)
    if c26 == 1:
        cemetery(m)
    elif c26 == 2:
        if landing_class(m) != 0:
            rescue(m)
        else:
            tass(m)
    elif c26 == 3:
        landed(m)
    for n in ('2bec', '03d9', '2265', '39d0'):
        m.calls.append((n,))
    if m.p(0x36) == 0x63:
        m.sp(0x4e, 1)
    if m.c(0x26) == 1 and m.c(0x30) == 0:
        m.sp(0x4e, 2)
    m.calls.append(('exit', 0x23))


def map_screen_end(m):
    """03d9 tail: stores the score in the pilot record (the menu loop is scripted by the test)"""
    sc = score(m, 0x100)
    if m.c(0x30) == 0:
        m.sp(0x30, sc)
        if m.p(0x2e) < sc:
            m.sp(0x2e, sc)
        lo = m.p(0x32) + sc
        m.sp(0x32, lo)
        m.sp(0x34, m.p(0x34) + (lo >> 16))
    else:
        m.sp(0x30, 0)
    return sc


# ------------------------------------------------------------------ 04d2 site marks / 046e hover / 3bf4 init
def site_marks(m, pick, waits):
    """04d2 draw_site_marks(pick): the icons appear one by one (pick 0 = still animating; any key/click in the 8-tick wait skips it)"""
    n = m.w(0x54b8)
    k = 0
    while k < n:
        if (pick & 0xff) == 0:
            m.calls.append(('wait_input', 8))
            pick = waits.pop(0) & 0xff
            m.calls.append(('snd', 0x1c))
        q = SITES + 16 * m.b(0x5440 + 2 * k)
        x = m.w(0x54ba + 2 * k); y = m.w(0x5532 + 2 * k)
        if m.b(0x5441 + 2 * k) == 0:
            if m.w(q + 6) != 0:
                seg = m.w(0x7226) if (m.b(q + 8) & 8) else m.w(0x720c)
            else:
                seg = m.w(0x5da0)
            m.calls.append(('sprite', CTX1, x, y, seg))
            m.calls.append(('sprite', CTX0, x, y, seg))
        else:
            x1 = (x - 1) & 0xffff; x2 = (x1 + 3) & 0xffff
            m.calls.append(('sprite', CTX1, x1, y, m.w(0x720c)))
            m.calls.append(('sprite', CTX1, x2, y, m.w(0x5da0)))
            m.calls.append(('sprite', CTX0, x1, y, m.w(0x720c)))
            m.calls.append(('sprite', CTX0, x2, y, m.w(0x5da0)))
        k += 1


def hover(m, hit):
    """046e: highlights the button under the cursor by rewriting its palette entry"""
    new = hit
    m.calls.append(('hit', 0x50b9, m.b(0x50b8)))
    prev = m.b(0x5b9e)
    if prev != new:
        if prev != 0:
            m.calls.append(('palq', (0x100 - prev) & 0xffff, 0x1324, 1))
        if new != 0:
            m.calls.append(('palq', (0x100 - new) & 0xffff, 0x131e, 1))
    m.sb(0x5b9e, new)


def init_names(m):
    """3bf4 tail: display name = short rank + name, at DS:7228"""
    s = m.cstr(m.w(0x1b8a + 2 * m.p(0x20)))
    nm = bytes(m.comm[PIL + 2:PIL + 2 + 30]).split(b'\0')[0]
    full = s + nm
    m.ds[0x7228:0x7228 + len(full) + 1] = full + b'\0'


# ------------------------------------------------------------------ 2092 wait_input
class Env:
    """scripted input devices for the wait_input test (each list is consumed in call order)"""
    def __init__(s, joy, mouse, keys):
        s.joy, s.mouse, s.keys = list(joy), list(mouse), list(keys)

    def keys_empty(s):
        return len(s.keys) == 0

    def pop(s, lst):
        return lst.pop(0) if lst else 1


def wait_input(m, ticks, env):
    """2092 int wait_input(unsigned ticks): 0 = nothing, 1 = Enter/button 0, 2 = Esc/button 1, 3 = Space; click sounds; 0xFFFF = no timeout"""
    ops = m.calls
    f72 = m.c(0x72)

    def pal(): ops.append(('pal',))

    def joybtn(n):
        ops.append(('slot', 93, n)); return env.pop(env.joy)

    def mouse_poll():
        ops.append(('mpoll',))

    def mbtn(n):
        ops.append(('mbtn', n)); return env.pop(env.mouse)

    if ticks != 0:                       # phase 1: wait until every button is released
        while True:
            pal()
            if ticks != 0xffff:
                ticks = (ticks - 1) & 0xffff
            r = 0
            if f72 & 1:
                if joybtn(0) or joybtn(1): r = 1
            if f72 & 2:
                mouse_poll()
                if mbtn(0) or mbtn(1): r = 1
            if r == 0 or ticks == 0:
                break
    r = 0
    first = True
    while True:                          # phase 2 (the first pass only polls the keyboard)
        if not first:
            if r != 0:
                break
            pal()
            if ticks != 0xffff:
                ticks = (ticks - 1) & 0xffff
            if f72 & 1:
                if joybtn(0): r = 1
                if joybtn(1): r = 2
            if f72 & 2:
                mouse_poll()
                if mbtn(0): r = 1
                if mbtn(1): r = 2
        first = False
        ops.append(('slot', 90))
        if not env.keys_empty():
            ops.append(('slot', 91))
            k = env.keys.pop(0)
            if k == 0x1000:
                ops.append(('quit', 1))
            k &= 0xff
            if k == 0x0d: r = 1
            if k == 0x1b: r = 2
            if k == 0x20: r = 3
        if ticks == 0:
            break
    if r == 1: ops.append(('slot', 101, 0x1a)); return 0
    if r == 2: ops.append(('slot', 101, 0x23)); return 0
    if r == 3: ops.append(('slot', 101, 0x1a)); return 1
    return 0
