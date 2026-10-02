"""MicroProse F-117A .PIC decoder (port of END.EXE 0000:4874-49CF + RLE at 48AA).

Stream: byte0 = header. Bit7 set -> 8-bit pixels, else 4-bit (two pixels/byte, low nibble first);
maxbits = abs(signed byte0) (F5 -> 11). Then LSB-first LZW from bit 8: width 9..maxbits, no clear
code, first free entry 256, entry added after every code (the very first one is a dummy), table
reset (width 9, free 256, prev kept) when the free entry exceeds the max mask. LZW output goes through
RLE-90 (0x90 n: repeat previous byte n-1 more times; 0x90 0: literal 0x90).
"""
import sys


def lzw(d):
    b0 = d[0]
    nib = not (b0 & 0x80)
    maxb = b0 if nib else 256 - b0
    n = len(d) * 8
    p = 8
    w, mask, nxt = 9, 0x1FF, 0x100
    pre = {}
    chr_ = {}
    prev = 0xFFFF
    first = 0
    out = bytearray()

    def expand(code):
        s = []
        while code < 256 or code in pre:
            if code < 256:
                s.append(code)
                break
            s.append(chr_[code])
            code = pre[code]
        return s  # reversed

    while p + w <= n:
        v = 0
        q = p
        bi = 0
        while bi < w:
            take = min(8 - (q & 7), w - bi)
            v |= ((d[q >> 3] >> (q & 7)) & ((1 << take) - 1)) << bi
            bi += take
            q += take
        p += w
        if v >= nxt:                      # KwKwK
            s = expand(prev)
            fc = s[-1]
            cur = list(reversed(s)) + [fc]
            cur.reverse()
        else:
            cur = expand(v)
        fc = cur[-1]
        out += bytes(reversed(cur))
        pre[nxt] = prev
        chr_[nxt] = fc
        nxt += 1
        if nxt > mask:
            w += 1
            mask = (mask << 1) | 1
        if w > maxb:
            w, mask, nxt = 9, 0x1FF, 0x100
            pre.clear()
            chr_.clear()
        prev = v if v < 0x10000 else prev
    return out, nib


def rle90(o):
    r = bytearray()
    i = 0
    while i < len(o):
        b = o[i]
        i += 1
        if b == 0x90 and i < len(o):
            c = o[i]
            i += 1
            if c == 0:
                r.append(0x90)
            else:
                r += bytes([r[-1]]) * (c - 1)
        else:
            r.append(b)
    return r


def decode(path):
    d = open(path, 'rb').read()
    o, nib = lzw(d)
    r = rle90(o)
    if nib:
        r = bytes(x for b in r for x in (b & 15, b >> 4))
    return bytes(r), nib


if __name__ == '__main__':
    for f in sys.argv[1:]:
        r, nib = decode(f)
        print(f, len(r), 'nibble' if nib else 'byte')
