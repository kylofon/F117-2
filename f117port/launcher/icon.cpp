// icon.cpp -- the app icon: the F-117A seen from above against a night sky (dark blue, with a few stars), drawn at 16x16 and scaled by whole pixels so every size shows
// the same picture. app.ico holds the same drawing for Explorer; make_icon.py writes it.
#include "icon.h"

#include <wx/image.h>

namespace {

// The plane: '#' the faceted body, 'o' the cockpit glass.
const char* const PLANE[16] = {
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
};

wxImage PlaneTile() {
    wxImage tile(16, 16);
    auto put = [&](int x, int y, unsigned long rgb) { tile.SetRGB(x, y, rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF); };
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            unsigned long c = (x * 7 + y * 13) % 29 == 0 ? 0xC8D4F0 : y < 9 ? 0x0B1530 : 0x142448;
            switch (PLANE[y][x]) {
            case '#': c = 0x3A4048; break;
            case 'o': c = 0x7088A8; break;
            }
            put(x, y, c);
        }
    return tile;
}

}  // namespace

wxBitmap AppBitmap(int size) { return wxBitmap(PlaneTile().Scale(size, size, wxIMAGE_QUALITY_NEAREST)); }

wxIconBundle AppIcons() {
    wxIconBundle icons;
    for (int size : {16, 20, 24, 32, 48, 64, 256}) {
        wxIcon icon;
        icon.CopyFromBitmap(AppBitmap(size));
        icons.AddIcon(icon);
    }
    return icons;
}
