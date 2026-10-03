"""MGRAPHIC.EXE bitmap fonts (S3.2).  usage: python tools/mgfont.py [sheet|ascii FONT 'text']
Font table: image offset D0h = max index (7), then word pointers at D2h+2*i (i = 0..6; entry 7 is a
bogus guard).  A pointer p addresses the glyph data; the header sits below it:
  p-9-(last-first)..p-9  advance width per glyph (proportional fonts only, last glyph at p-9)
  p-8 first char, p-7 last char, p-6 bytes per glyph row + 1 (always 1 = 8 px), p-5 fixed width
  (0 = proportional), p-4 height, p-3 spacing added to every advance, p-2 extra rows when the
  text flag bit 0 is set, p-1 unused.  Glyph c, row r = byte p + r*(last-first+1) + (c-first), MSB = left."""
import os, struct, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = open(os.path.join(ROOT, 'Game', 'MGRAPHIC.EXE'), 'rb').read()
img = d[struct.unpack_from('<H', d, 8)[0] * 16:]


def fonts():
    n = struct.unpack_from('<H', img, 0xd0)[0]
    out = []
    for i in range(n):
        p = struct.unpack_from('<H', img, 0xd2 + 2 * i)[0]
        first, last, sh, fixed, h, sp, extra = img[p - 8], img[p - 7], img[p - 6], img[p - 5], img[p - 4], img[p - 3], img[p - 2]
        cnt = last - first + 1
        wid = None if fixed else list(img[p - 9 - (last - first):p - 9 + 1])
        out.append(dict(i=i, p=p, first=first, last=last, fixed=fixed, h=h, sp=sp, extra=extra, cnt=cnt, wid=wid))
    return out


def glyph(f, c, rows=None):
    rows = rows if rows is not None else f['h'] + f['extra']
    k = c - f['first']
    return [img[f['p'] + r * f['cnt'] + k] for r in range(rows)]


def adv(f, c):
    if not f['first'] <= c <= f['last']:
        return 0
    return (f['fixed'] or f['wid'][c - f['first']]) + f['sp']


def main():
    fs = fonts()
    if len(sys.argv) > 1 and sys.argv[1] == 'ascii':
        f = fs[int(sys.argv[2])]
        for ch in sys.argv[3]:
            w = adv(f, ord(ch)) - f['sp']
            print(repr(ch), 'adv', adv(f, ord(ch)))
            for b in glyph(f, ord(ch)):
                print(''.join('#' if b >> (7 - x) & 1 else '.' for x in range(max(w, 1))))
        return
    for f in fs:
        print({k: v for k, v in f.items() if k != 'wid'}, 'widths' if f['wid'] else 'fixed')
    from PIL import Image
    W, y = 640, 0
    sheet = Image.new('P', (W, 7 * 120 + 10), 0)
    sheet.putpalette([0, 0, 0, 255, 255, 255] + [0] * 762)
    for f in fs:
        for row in range(2):
            x = 4
            for c in range(max(f['first'], 32) + row * 48, min(f['last'] + 1, max(f['first'], 32) + row * 48 + 48)):
                w = adv(f, c)
                for r, b in enumerate(glyph(f, c)):
                    for xx in range(max(w - f['sp'], 0)):
                        if b >> (7 - xx) & 1:
                            sheet.putpixel((x + xx, y + r), 1)
                x += max(w, 1) + 4
            y += f['h'] + f['extra'] + 3
        y += 8
    os.makedirs(os.path.join(ROOT, 'work', 's32'), exist_ok=True)
    sheet.crop((0, 0, W, y)).resize((W * 2, y * 2), Image.NEAREST).save(os.path.join(ROOT, 'work', 's32', 'fonts_sheet.png'))
    print('work/s32/fonts_sheet.png')


main()
