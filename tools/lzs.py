"""LZEXE-style LZ stream (as used by F-117A .PAN files, PLAYER.EXE 0000:0BE2)."""
import struct

def lz_decode(data, pos=0):
    out = bytearray()
    n = len(data)
    bits = 0; nbits = 0
    if n < 2: return b'', []
    def getbit():
        nonlocal bits, nbits, pos
        # PLAYER.EXE fetches the next control word while taking the 16th bit
        # of the old one (before any following literal byte is read)
        if nbits == 0:
            bits = struct.unpack_from('<H', data, pos)[0]; pos += 2; nbits = 16
        b = bits & 1; bits >>= 1; nbits -= 1
        if nbits == 0 and pos + 2 <= n:
            bits = struct.unpack_from('<H', data, pos)[0]; pos += 2; nbits = 16
        return b
    ends = []
    try:
        while True:
            if getbit():
                out.append(data[pos]); pos += 1; continue
            if not getbit():
                cnt = (getbit() << 1 | getbit()) + 2
                off = 0x100 - data[pos]; pos += 1
            else:
                w = data[pos] | data[pos+1] << 8; pos += 2
                off = 0x2000 - (((w >> 8) >> 3) << 8 | (w & 0xff))
                cnt = (w >> 8 & 7)
                if cnt: cnt += 2
                else:
                    b = data[pos]; pos += 1
                    if b <= 1:
                        ends.append((len(out), b)); continue
                    cnt = b + 1
            for _ in range(cnt):
                out.append(out[-off])
    except (EOFError, IndexError, struct.error):
        pass
    return bytes(out), ends
