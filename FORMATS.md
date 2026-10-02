# F-117A 2.0 file formats

## .PIC (320x200, 8-bit indexed) — decoder `tools/picdec.py` (END.EXE 0000:4874-49CF, 48AA)
Header byte0: bit7 set = 8-bit pixels, maxbits = 256-byte0 (all shipped files: F5 -> 11); else 4-bit
pixels (two per byte, low nibble first, maxbits = byte0; none shipped). Then LSB-first LZW from bit 8:
code width 9..maxbits, **no clear/EOI code**, free entry starts at 256 and one entry is added after every
code (the first is a dummy), width grows when free > mask; when width would exceed maxbits the table is
cleared (width 9, free 256, previous code kept). Output goes through RLE-90 (`90 n` = repeat previous
byte n-1 more times, `90 00` = literal 90). Result is exactly 64000 pixels. 52/52 files decode.

## .PAL / PALETTES.PAL
768 bytes = 256 x RGB, VGA 6-bit (0-63). `PALETTES.PAL` = 9 palettes, indexed by theatre in the order
LIBYA, PERSIAN, N_CAPE, C_EUROPE, VIETNAM, MID_EAST, KUWAIT, CUBA, KOREA (START/END string order;
index mapping to be confirmed in U5). PICs without an own `.PAL` (cockpit, hangar, planes, weapons, ...)
are rendered with `ADV.PAL` as a guess; real palette source to find in S2.2/3 (likely set by the
caller or left from the previous screen).
