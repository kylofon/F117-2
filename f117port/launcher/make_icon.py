"""Writes app.ico: the F-117A over the night sky drawn in icon.cpp, for Explorer. Needs Pillow.

    python make_icon.py
"""
from pathlib import Path

from PIL import Image

PLANE = [
    "................",
    ".......##.......",
    "......####......",
    "......####......",
    ".....##oo##.....",
    "....########....",
    "...##########...",
    "..############..",
    ".##############.",
    ".##.########.##.",
    ".#...######...#.",
    "......####......",
    ".......##.......",
    "................",
    "................",
    "................",
]
COLOURS = {"#": 0x3A4048, "o": 0x7088A8}

tile = Image.new("RGBA", (16, 16))
for y in range(16):
    for x in range(16):
        c = 0xC8D4F0 if (x * 7 + y * 13) % 29 == 0 else 0x0B1530 if y < 9 else 0x142448
        c = COLOURS.get(PLANE[y][x], c)
        tile.putpixel((x, y), (c >> 16, (c >> 8) & 0xFF, c & 0xFF, 255))

sizes = [16, 20, 24, 32, 48, 64, 256]
big = tile.resize((256, 256), Image.NEAREST)
big.save(Path(__file__).with_name("app.ico"), sizes=[(s, s) for s in sizes])
