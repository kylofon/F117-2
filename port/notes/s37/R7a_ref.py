"""R7a Python references for 11EB (render3d object renderer). Bit-exact models of the 8086 code."""
def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - 0x100000000 if v & 0x80000000 else v
def s8(v): v &= 0xFF; return v - 0x100 if v & 0x80 else v
def tdiv(a, b):  # idiv: truncation toward 0
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q

def idiv16(dividend, cx):
    """idiv cx with dx:ax = dividend (s32). Returns quotient or None on #DE."""
    q = tdiv(dividend, s16(cx))
    return None if q < -32768 or q > 32767 else q

def div0_handler(dividend, cx):
    """11EB:0975: retry with dividend>>8, result = q*256 as s32."""
    d2 = s32(dividend) >> 8                    # bytes 1..3 of dx:ax, sign-extended
    q = idiv16(d2, cx)
    assert q is not None, 'nested #DE'
    return q * 256

def project(x, y, z, mode_byte, old=(0, 0, 0, 0)):
    """11EB:0906. x,y,z s32 view-space; mode_byte = byte DS:7D5E. Returns (xlo,xhi,ylo,yhi) words.
    old = previous content of the output (untouched words in the behind case)."""
    def w32(v): v &= 0xFFFFFFFF; return (v & 0xFFFF, v >> 16)
    m = s8(mode_byte)
    if m != 0:
        res = []
        for v in (x, y):
            h = s16(s32(v) >> 16)
            if m >= 0x10:
                c = m & 0xF
                r = h >> c
                if c and (h >> (c - 1)) & 1: r += 1
                r = s16(r)
            else:
                c = (0x10 - m) & 0xFF & 0x1F
                r = s16((h << c) & 0xFFFF) if c < 16 else 0
            res += list(w32(r))
        return tuple(res)
    zh = s16(s32(z) >> 16)
    if zh >= 0x100:
        out = []
        for v in (x, y):
            dv = s32(v) >> 8
            q = idiv16(dv, zh)
            r = q if q is not None else div0_handler(dv, zh)
            out += list(w32(r))
        return tuple(out)
    if zh >= 1:
        cx = ((s32(z) >> 8) & 0xFFFF) >> 1
        out = []
        for v in (x, y):
            dv = s32(v) >> 1
            q = idiv16(dv, cx)
            r = q if q is not None else div0_handler(dv, cx)
            out += list(w32(r))
        return tuple(out)
    return (old[0], 0x8000, old[2], old[3])

def near_bisect(p0, p1):
    """11EB:0990. p0 = in-front point, p1 = behind point, each (x,y,z) s32. Returns (mx, my) s32."""
    p0 = list(p0); p1 = list(p1)
    while True:
        m = [s32(p0[i] + p1[i] + 1) >> 1 for i in range(3)]
        h = (m[2] >> 16) & 0xFFFF
        if h <= 1:
            return m[0], m[1]
        if s16(h - 1) < 0: p1 = m
        else: p0 = m

def frustum(radius, x, y, z, far):
    """11EB:02B6. Returns True if visible (AX=0, ZF=1)."""
    zh = s16(s32(z) >> 16)
    a = abs(zh) & 0xFFFF
    if (((a + 1) & 0xFFFF) << 1) & 0xFFFF >= far: return False
    t = s16(2 * zh + radius)
    if t < 0: return False
    for v in (x, y):
        if (abs(s32(v)) >> 16) & 0xFFFF > t: return False   # |v| of -2^31 = 2^31 -> hi 8000h
    return True

def bsp_order(root, pairs, vis):
    """11EB:0E5F. pairs[n] = (b0, b1) bytes; vis[n] byte (0 = back-facing). Returns emitted list (without FFh)."""
    out = []
    def visit(n):
        b0, b1 = pairs[n]
        if vis[n] != 0:
            if b1 != 0xFF: visit(b1)
            out.append(n)
            if b0 != 0xFF:
                if b0 & 0x80: out.append(b0)
                else: visit(b0)
        else:
            if b0 != 0xFF:
                if b0 & 0x80: out.append(b0)
                else: visit(b0)
            if b1 != 0xFF: visit(b1)
    visit(root)
    return out

def set_camera(m9, zoom, flag7d3a=0):
    """11EB:000C. Returns (far 7D2E, scaled matrix 9 words s16)."""
    k = abs(s16(zoom * 2)) & 0xFFFF
    if k < 0x100: k = 0x100
    far = 0x7FFF00 // k
    m = list(m9)
    c = s16(-far)
    for i in (2, 5, 8):
        m[i] = s16((c * m[i] * 2) >> 16)          # imul, shl ax/rcl dx, take dx
    for i in (0, 3, 6, 1, 4, 7):
        m[i] = m[i] >> 1
    for i in (1, 4, 7):
        if flag7d3a: m[i] = s16(-m[i])
        else:
            a = s16(-m[i]); m[i] = s16(a - (a >> 2))
    return far, m

def plane_test(n, d, eye, mode_byte, mask):
    """11EB:0565 per plane: returns the 76D2 byte."""
    v = s32(n[0] * eye[0] + n[1] * eye[1] + n[2] * eye[2] + d)
    dl = 0xFF if v >= 0 else 0
    if mode_byte: dl = 0xFF
    return dl & mask

def plane_shade(n, L):
    """11EB:0678: byte = bits 14..21 of the 32-bit sum."""
    v = (n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) & 0xFFFFFFFF
    return ((v << 2) >> 16) & 0xFF

def xform_one(v, M, org):
    """11EB:0819 3D part: v s16 x3, M 9 s16, org s32 x3 -> s32 x3 (no shift)."""
    return [s32(v[0] * M[k] + v[1] * M[3 + k] + v[2] * M[6 + k] + org[k]) for k in range(3)]

def planes(buf, eyes, lights, varsw, mode, nverts, m7d5c=0):
    """11EB:0565 (+0658 clear, 0678 shade, 0E5F for nplanes<0). buf = bytes at ES:SI. eyes/lights: lists of (x,y,z)
    indexed by part; varsw: 32 words. Returns dict of results."""
    import struct
    si = 0
    def word():
        nonlocal si
        v = struct.unpack_from('<H', buf, si)[0]; si += 2; return v
    np_ = s16(word())
    out = {'7D2A': np_ & 0xFFFF, 'vis': {}, 'shade': {}, 'mark': set(), '7D5C': m7d5c, 'si': si}
    if np_ == 0:
        return out
    cnt = abs(np_); eye = eyes[0]; light = lights[0]; part_l = 0
    for di in range(cnt):
        mask = 0xFF
        while True:
            w = word()
            if w >> 8 == 0x80:
                pp = w & 0xFF; eye = eyes[pp]; light = lights[pp]; part_l = pp; continue
            if w == 0x8100:
                var = buf[si] & 0x1F; val = s8(buf[si + 1]); si += 2
                if (val & 0xFFFF) != varsw[var]: mask = 0x7F
                continue
            break
        n = (s16(w), s16(word()), s16(word())); d = s32(struct.unpack_from('<I', buf, si)[0]); si += 4
        dl = plane_test(n, d, eye, mode, mask)
        out['vis'][di] = dl
        if dl & 0x80:
            out['7D5C'] |= dl
            out['shade'][di] = plane_shade(n, light)
    out['7D4C_part'] = part_l
    for di in range(cnt):
        k = buf[si]; si += 1
        if k and out['vis'][di] > 0x7F:
            out['mark'] |= set(buf[si:si + k])
        si += k
    if np_ < 0:
        root = buf[si]
        pairs = [(buf[si + 1 + 2 * i], buf[si + 2 + 2 * i]) for i in range(cnt)]
        vis = [out['vis'].get(i, 0) for i in range(256)]
        out['bsp'] = bsp_order(root, pairs, vis)
        si += 2 * cnt + 1
    out['si'] = si
    return out
