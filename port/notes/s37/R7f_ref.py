"""R7f Python reference: 0000:B85D r3d_project_point, BA13 r3d_matrix_row_dot, BA67 r3d_view_depth."""


def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - 0x100000000 if v & 0x80000000 else v


def mq(a, b):                      # 109D:0006 mul_q15
    return s16((s16(a) * s16(b) + 0x4000) >> 15)


def ldiv(a, b):                    # F392 rtl_aFldiv: signed 32-bit, truncates toward 0
    a, b = s32(a), s32(b)
    q = abs(a) // abs(b)
    return s32(-q if (a < 0) != (b < 0) else q)


def ba13(M, row, dx, dy, dz):
    # M = 9 words at DS:B156; note col1 (B15C) multiplies dz, col2 (B162) multiplies dy
    return s32(mq(M[row], dx) + mq(M[3 + row], dz) + mq(M[6 + row], dy))


def ba67(Mc, a, b, c, d, e, f):
    # Mc = ([4992],[4998],[499E]); returns new [DE92]
    t = s32(mq(Mc[0], s16(d - a)) + mq(Mc[1], s16(c - f) >> 5) + mq(Mc[2], s16(b - e)))
    return s16(t >> 3)


def b85d(g, x, y, z):
    """g: dict of globals (ints). returns dict of written globals {49E0,49E8,DE92,9500} (only those written)."""
    out = {}
    dx = s16(g['C0A4'] - x); dy = s16(y - g['C0B2']); dz = s16(z - g['2DF4']) >> 5
    if g['C07A'] & 0x80:
        dx = s16(dx - (s32(g['BA10'] - g['B288']) >> 5))
        dy = s16(dy - (s32(g['C02A'] - g['B752']) >> 5))
        dz = s16(dz - (s32(s16(g['B75C']) - (g['2DF4'] & 0xFFFF)) >> 5))
    M = g['M']
    X = ba13(M, 0, dx, dy, dz); Y = ba13(M, 1, dx, dy, dz); Z = ba13(M, 2, dx, dy, dz)
    if Z >= 0:
        out['49E0'] = 0xFFFF; return out
    if g['294B'] & 0xFF:
        X >>= 1; Y >>= 1
    nZ = s32(-Z)
    if not (Z <= X <= nZ):
        out['49E0'] = 0xFFFF; return out
    sx = s16(ldiv(s32(X << 8), Z) + 0xA0)
    sy = s16(ldiv(s32(Y << 8), Z))
    sy = s16(sy - (sy >> 2))
    if g['365E']:
        sy = s16(sy + (60 if (g['294B'] & 0xFF) else 52))
    else:
        sy = s16(sy + 100)
    out['49E8'] = sy & 0xFFFF
    out['DE92'] = s16(Z >> 3) & 0xFFFF
    p49e0 = sx & 0xFFFF
    if sx < 0 or sx > 0x13F:
        out['9500'] = p49e0; p49e0 = 0xFFFF
    lim = 0x60 if g['365E'] else 0xC7
    if sy < 0 or sy > lim:
        out['9500'] = p49e0; p49e0 = 0xFFFF
    out['49E0'] = p49e0
    return out
