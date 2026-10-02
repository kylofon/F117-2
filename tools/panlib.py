"""F-117A .PAN animation reader (PLAYER.EXE): LZ stream (lzs.py) -> palette + delta-RLE cels (0000:1006)."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from lzs import lz_decode

W, H = 320, 200


def rle_apply(src, p, scr):
    """PLAYER.EXE 0000:1006. Applies one RLE record stream to scr (bytearray 64000); returns (src pos, last dest)."""
    di = 0
    n = len(scr)

    def put(b):
        nonlocal di
        if di + len(b) > n:
            b = b[:max(0, n - di)]
        scr[di:di + len(b)] = b
        di += len(b)  # may pass n; harmless
    cl = src[p]; p += 1
    while True:
        if cl == 0:                      # 1033: fill pairs
            while cl == 0:
                cnt, val = src[p], src[p + 1]; p += 2
                put(bytes([val]) * cnt)
                cl = src[p]; p += 1
            continue
        if cl < 0x80:                    # 1028: literal run
            put(src[p:p + cl]); p += cl
            cl = src[p]; p += 1
            continue
        cl -= 0x80                       # 1018
        if cl:
            di += cl
            cl = src[p]; p += 1
            continue
        ax = src[p] | src[p + 1] << 8; p += 2   # 1048: long ops
        if ax == 0:
            return p
        if ax < 0x8000:
            di += ax
        else:
            ch = (ax >> 8) - 0x80
            cnt = (ch & 0x3f) << 8 | (ax & 0xff)
            if ch < 0x40:
                put(src[p:p + cnt]); p += cnt
            else:
                put(bytes([src[p]]) * cnt); p += 1
        cl = src[p]; p += 1


def load_pan(path):
    d = open(path, 'rb').read()
    o, ends = lz_decode(d)
    assert o[0] == 0x5a
    n = struct.unpack_from('<H', o, 2)[0]
    tab = struct.unpack_from('<%dH' % n, o, 4)
    hdr = 4 + 2 * n + 768
    pal6 = o[4 + 2 * n:hdr]
    pal = bytes((v << 2 | v >> 4) & 255 for v in pal6)
    cels = []
    pos = hdr
    end = 0
    for i, sz in enumerate(tab):
        end += sz
        cels.append((pos, end))
        pos = end
    return dict(speed=o[1], n=n, pal=pal, data=o, cels=cels, tab=tab, ends=ends)


def frames(pan, check=False):
    """Yield (index, screen bytes) after each cel."""
    o = pan['data']
    scr = bytearray(W * H)
    for i, (s, e) in enumerate(pan['cels']):
        assert o[s:s + 4] == b'B\0\1\0', (i, s, o[s:s + 4])
        p = rle_apply(o, s + 4, scr)
        if check and p != e:
            print('cel %d: consumed to %d, expected %d' % (i, p, e))
        yield i, bytes(scr)
