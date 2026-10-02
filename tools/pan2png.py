"""PAN animations -> work/pan/<NAME>/NNN.png (every frame) + <NAME>_sheet.png contact sheet.
   pan2png.py [Game/X.PAN ...]   (default: all)"""
import sys, os, glob
sys.path.insert(0, os.path.dirname(__file__))
from PIL import Image, ImageDraw
from panlib import load_pan, frames, W, H

OUT = 'work/pan'
COLS, MAXTHUMBS = 6, 24


def main():
    files = sys.argv[1:] or sorted(glob.glob('Game/*.PAN'))
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        pan = load_pan(f)
        d = os.path.join(OUT, name)
        os.makedirs(d, exist_ok=True)
        pal = list(pan['pal'])
        n = pan['n']
        pick = sorted({round(i * (n - 1) / (MAXTHUMBS - 1)) for i in range(MAXTHUMBS)}) if n > MAXTHUMBS else list(range(n))
        thumbs = {}
        for i, scr in frames(pan):
            im = Image.frombytes('P', (W, H), scr)
            im.putpalette(pal)
            im.save(os.path.join(d, '%03d.png' % i))
            if i in pick:
                thumbs[i] = im.convert('RGB').resize((W // 2, H // 2), Image.NEAREST)
        rows = (len(pick) + COLS - 1) // COLS
        sheet = Image.new('RGB', (COLS * (W // 2 + 4) + 4, rows * (H // 2 + 16) + 4), (40, 40, 40))
        dr = ImageDraw.Draw(sheet)
        for k, i in enumerate(pick):
            x = 4 + (k % COLS) * (W // 2 + 4)
            y = 4 + (k // COLS) * (H // 2 + 16)
            sheet.paste(thumbs[i], (x, y + 12))
            dr.text((x, y), '%s #%d' % (name, i), fill=(255, 255, 0))
        sheet.save(os.path.join(OUT, name + '_sheet.png'))
        print('%s: %d frames, speed byte %d (60/%d = %d ticks), sheet %d thumbs' % (
            name, n, pan['speed'], pan['speed'], 60 // pan['speed'], len(pick)))


main()
