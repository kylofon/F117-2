"""Render one PIC with every known palette -> work/pic/cand_<NAME>.png (to pick the right palette in U5)."""
import glob, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from PIL import Image, ImageDraw
from picdec import decode

name = sys.argv[1].upper()
data, _ = decode(f'Game/{name}.PIC')
pals = []
for p in sorted(glob.glob('Game/*.PAL')):
    b = open(p, 'rb').read()
    for i in range(len(b) // 768):
        pals.append((os.path.basename(p) + (f'[{i}]' if len(b) > 768 else ''), b[i * 768:(i + 1) * 768]))
TW, TH, C = 320, 200, 4
rows = (len(pals) + C - 1) // C
sh = Image.new('RGB', (C * TW, rows * (TH + 14)), (32, 32, 32))
d = ImageDraw.Draw(sh)
for k, (n, b) in enumerate(pals):
    im = Image.frombytes('P', (320, 200), data)
    im.putpalette([min(255, (c * 255 + 31) // 63) for c in b])
    x, y = (k % C) * TW, (k // C) * (TH + 14)
    sh.paste(im.convert('RGB'), (x, y + 14))
    d.text((x + 2, y + 1), n, fill=(255, 255, 0))
sh.save(f'work/pic/cand_{name}.png')
print(name, len(pals), 'palettes')
