"""All .PIC (+ .PAL / PALETTES.PAL) -> work/pic/*.png, work/pic/sheet.png, work/pic/palettes.png.
Palette choice per PIC: own .PAL; theatre maps -> PALETTES.PAL index (START/END string order);
LANDED2-4 -> LANDED1.PAL; everything else -> ADV.PAL (guess, marked '?' on the sheet)."""
import glob, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from PIL import Image, ImageDraw
from picdec import decode

G, O = 'Game', 'work/pic'
os.makedirs(O, exist_ok=True)


def pal6(b):
    return [min(255, (c * 255 + 31) // 63) for c in b]


def load_pal(path):
    return pal6(open(path, 'rb').read()[:768])


allpals = open(f'{G}/PALETTES.PAL', 'rb').read()
PALS = [pal6(allpals[i * 768:(i + 1) * 768]) for i in range(len(allpals) // 768)]
THEATRES = ['LIBYA', 'PERSIAN', 'N_CAPE', 'C_EUROPE', 'VIETNAM', 'MID_EAST', 'KUWAIT', 'CUBA', 'KOREA']
# START.EXE 0000:826C (palette n, clamp 8) pairs with the screen table at DS:0CBA (START 3641/35FC):
SCREENS = ['AIR_ID', 'ARMSSCRN', 'BREFSCRN', 'FOLDER1', 'HANGSCRN', 'HOMESCRN', 'ORDRSCRN', 'ROSTSCRN', 'BREFICON']
SPRITES = {'FOLDER2': 'FOLDER1', 'ORDRSPRT': 'ORDRSCRN', 'ROSTSPRT': 'ROSTSCRN', 'HANGSPRT': 'HANGSCRN'}  # guess
EXTRA = {'LANDED2': 'LANDED1', 'LANDED3': 'LANDED1', 'LANDED4': 'LANDED1'}


def choose(name):
    if os.path.exists(f'{G}/{name}.PAL'):
        return load_pal(f'{G}/{name}.PAL'), name + '.PAL'
    if name in THEATRES:          # maps are shown on the briefing screen
        return PALS[2], 'PALETTES[2]'
    if name in SCREENS:
        i = SCREENS.index(name)
        return PALS[i], f'PALETTES[{i}]'
    if name in SPRITES:
        i = SCREENS.index(SPRITES[name])
        return PALS[i], f'PALETTES[{i}]?'
    if name in EXTRA:
        return load_pal(f'{G}/{EXTRA[name]}.PAL'), EXTRA[name] + '.PAL'
    return load_pal(f'{G}/ADV.PAL'), 'ADV.PAL?'


def render(data, pal):
    im = Image.frombytes('P', (320, 200), data)
    im.putpalette(pal)
    return im.convert('RGB')


tiles = []
for f in sorted(glob.glob(f'{G}/*.PIC')):
    name = os.path.basename(f)[:-4]
    data, nib = decode(f)
    pal, pname = choose(name)
    im = render(data, pal)
    im.save(f'{O}/{name}.png')
    tiles.append((name, pname, im))
    print(f'{name:10} {pname}')

TW, TH, COLS, LH = 320, 200, 5, 16
rows = (len(tiles) + COLS - 1) // COLS
sheet = Image.new('RGB', (COLS * TW, rows * (TH + LH)), (32, 32, 32))
dr = ImageDraw.Draw(sheet)
for i, (n, p, im) in enumerate(tiles):
    x, y = (i % COLS) * TW, (i // COLS) * (TH + LH)
    sheet.paste(im, (x, y + LH))
    dr.text((x + 2, y + 2), f'{n}  [{p}]', fill=(255, 255, 0))
sheet.save(f'{O}/sheet.png')

# palettes: every .PAL and each PALETTES.PAL slot as a 16x16 swatch grid
pals = [(os.path.basename(p), load_pal(p)) for p in sorted(glob.glob(f'{G}/*.PAL')) if not p.endswith('PALETTES.PAL')]
pals += [(f'PALETTES[{i}]', p) for i, p in enumerate(PALS)]
S, C = 8, 4
ps = Image.new('RGB', (C * (16 * S + 8), ((len(pals) + C - 1) // C) * (16 * S + 20)), (32, 32, 32))
pd = ImageDraw.Draw(ps)
for k, (n, p) in enumerate(pals):
    x, y = (k % C) * (16 * S + 8), (k // C) * (16 * S + 20)
    pd.text((x + 2, y + 2), n, fill=(255, 255, 0))
    for c in range(256):
        pd.rectangle([x + (c % 16) * S, y + 16 + (c // 16) * S, x + (c % 16) * S + S - 1, y + 16 + (c // 16) * S + S - 1],
                     fill=tuple(p[c * 3:c * 3 + 3]))
ps.save(f'{O}/palettes.png')
print(len(tiles), 'PICs,', len(pals), 'palettes')
