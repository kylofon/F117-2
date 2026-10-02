"""LABSLOGO.SS ('MADSS1' sprite series, MPS_LOGO.EXE 006d:0112 + blitter 0146:0f70) ->
   work/pan/LABSLOGO/sNN.png (RGBA sprite) + LABSLOGO_sheet.png (each sprite at its x,y on 320x200)."""
import struct, sys, os
from PIL import Image, ImageDraw

OUT = 'work/pan'
W, H = 320, 200


def decode(d, p, w, h):
    """Row stream: FF = end, FE = end of row; row marker FA = literal mode (FD n v = run, FC = skip),
    any other byte = (count, value) pair mode (value FC = skip)."""
    px = [[0] * w for _ in range(h)]   # 0 = transparent
    y = x = 0

    def run(n, v):
        nonlocal x
        if v != 0xfc and y < h:
            for i in range(n):
                if x + i < w: px[y][x + i] = v
        x += n
    while True:
        a = d[p]; p += 1
        if a == 0xff: return px, p
        if a == 0xfe: y += 1; x = 0; continue
        if a == 0xfa:
            while True:
                a = d[p]; p += 1
                if a == 0xfe: y += 1; x = 0; break
                if a == 0xfd: run(d[p], d[p + 1]); p += 2
                elif a == 0xfc: x += 1
                else: run(1, a)
        else:
            while True:
                a = d[p]; p += 1
                if a == 0xfe: y += 1; x = 0; break
                run(a, d[p]); p += 1


def main():
    d = open('Game/LABSLOGO.SS', 'rb').read()
    assert d[:7] == b'MADSS1\x1a'
    nfr = struct.unpack_from('<H', d, 0x14)[0]
    recs = [struct.unpack_from('<IHHHHH', d, 0x1f + 14 * k) for k in range(nfr)]  # off, flag, x, y, w-1, h-1
    ncol = d[0x1f + 14 * nfr]
    pbase = 0x1f + 14 * nfr + 1
    pal = [tuple((v << 2 | v >> 4) & 255 for v in d[pbase + 4 * i:pbase + 4 * i + 3]) for i in range(ncol)]
    base = pbase + 4 * ncol
    out = os.path.join(OUT, 'LABSLOGO')
    os.makedirs(out, exist_ok=True)
    sprites = []
    for k, (off, flag, x, y, w1, h1) in enumerate(recs[:-1]):
        w, h = w1 + 1, h1 + 1
        px, end = decode(d, base + off, w, h)
        nxt = base + recs[k + 1][0]
        if end != nxt: print('sprite %d: ends %d, next starts %d' % (k, end, nxt))
        im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        for yy in range(h):
            for xx in range(w):
                v = px[yy][xx]
                if v: im.putpixel((xx, yy), pal[v - 1] + (255,) if v <= ncol else (255, 0, 255, 255))
        im.save(os.path.join(out, 's%02d.png' % k))
        sprites.append((im, x, y, flag))
    cols, tw, th = 8, W // 2, H // 2
    rows = (len(sprites) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * (tw + 4) + 4, rows * (th + 16) + 4), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for k, (im, x, y, flag) in enumerate(sprites):
        c = Image.new('RGB', (W, H), (0, 0, 80))
        c.paste(im, (x, y), im)
        sx, sy = 4 + (k % cols) * (tw + 4), 4 + (k // cols) * (th + 16)
        sheet.paste(c.resize((tw, th), Image.NEAREST), (sx, sy + 12))
        dr.text((sx, sy), 's%d (%d,%d) f%d' % (k, x, y, flag), fill=(255, 255, 0))
    sheet.save(os.path.join(OUT, 'LABSLOGO_sheet.png'))
    print('LABSLOGO: %d sprites, %d colours, base %d' % (len(sprites), ncol, base))


main()
