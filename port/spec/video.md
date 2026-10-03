# Video: the `MGRAPHIC.EXE` driver, blitters, fonts, PIC loader (S3.2)

**Status: S3.2 delivered.** Symbols: `video_symbols.csv` (84 driver slots + 10 program-side routines; regenerate
with `python tools/video_syms.py`). Fonts: `python tools/mgfont.py` (`ascii N 'text'` or `sheet`).
Source of truth: `work/s32/mg.asm` = `python tools/rawdis.py Game/MGRAPHIC.EXE 019c:0000 1300` (the whole code
segment was read; 2085 lines, 5.0 KB code). Driver binding, slot numbers, header: `port/RE_GUIDE.md` §2.2-2.3
(not repeated). Confidence: **verified** = read in the disassembly; items marked *(U)* need a visual check.

## Key findings

* MGRAPHIC is a **flat 320x200x256 (mode 13h) 2D library**, ~5 KB: pages, rectangle copy, key-colour blits,
  line, span polygon fill, bitmap-font text, RLE sprites (grab + scaled draw), palette/blank helpers. There is
  **no 3D, no clipping in line/polygon, no palette API** (the DAC is written by the programs themselves,
  see §9). It is the only video driver shipped; slots 53-55, 34/35, 61, 80-83 are empty hooks (`retf`).
* **Pages are real-mode segments.** A *page* is a 64000-byte buffer; page numbers index a 5-word table in the
  driver (`cs:[0787+2*page]`, page 0 = `A000h`, the others are set by the programs with slot 57/75; page
  numbers above 4 would overwrite code). The only allocation is `gfx_alloc(2)` by F117.COM → `comm[20h]`
  (= page 1 in all programs). The off-screen page is flipped to `A000` with slot 44.
* **Two ways to name the destination.** Text, `blit_rect`, `rect_recolor`, `sprite_scaled` and the
  `blit_key*` family take a **page number** (table lookup). `line`, `pixel`, `poly_fill`, `page_load`, `clear_es`
  and `mode`-less helpers use the **draw segment** `cs:[0194]` + **draw offset** `cs:[0196]` set by slots
  12-16/24-27 (the 3D view is drawn through the draw offset: VGAME slot 26 x9). Port: keep both globals.
* Everything is **8-bit linear**; colour 0 is the transparent key for sprites/blit_key.
* All structures are passed as **near pointers into the caller's data segment** (DS = SS); the driver reads and
  (text) writes them through `SS:`. A port keeps the structs in guest memory (mem layer) or marshals them.
* Strings given to the right-clipped text calls are **modified temporarily** (a NUL is written after the last
  visible glyph and restored before return). The buffer must be writable.
* The driver keeps its working variables in its own data segment (`DS = load segment`, image offsets
  `17F8..1866`) and, for the page table etc., in the **code segment** (`cs:[...]`). Nothing is exported
  except through the slots, so the port may use ordinary statics.

## 1 Driver memory map (verified)

| Where | What |
|---|---|
| `cs:0004 + 2*y` | row offset table, `y*320`, y = 0..199 (`cs:0004..0193`); used by every blitter |
| `cs:0194` | draw segment (slots 13-16, initial 0, **programs always select a page before drawing**) |
| `cs:0196` | draw offset (slots 24-27, 30), initially 0 |
| `cs:0198` | last-flip flag (slot 45) |
| `cs:0472..047E` | constants returned by slots 23/49/66/67 (FA00, FA00, 0, 1; 0 and 1 unused) |
| `cs:0787` | page table, 5 words: `A000, 0, 0, 0, 0` |
| `cs:082A..083C` | sprite-pool state (§7) |
| `cs:0840..0866` | scaled-sprite working area; `cs:0868` row expansion buffer (≥ source width bytes) |
| `cs:11DC` | word, 0: when non-zero slot 44 does nothing (never set by the programs) |
| `cs:1250` | shake counter (slot 79 → 46) |
| data `00CC/00CE` | words set by slots 38/39/64/65, **never read** |
| data `00D0` | font count 7, then font pointers at `00D2 + 2*i` (index 7 is a bogus guard entry) |
| data `17F8..180C` | text working variables (§3), `1866` current colour (default 0Fh) |
| data `1867`, `1871..1960` | five 16-colour palette tables (§8) |
| data `1962..1965` | text colours/mode/visible-width of the glyph being drawn |
| data `1966..1979` | HUD tick-strip constants (§6) |

## 2 Calling conventions

* **C** slots: far call, arguments pushed right to left, caller pops (`add sp,n`); the first argument is at
  `[sp+4]` on entry. All integers are 16-bit, signed unless noted. Result in AX (DX:AX for far pointers).
  Wrappers save SI, DI, BP; DS/ES are preserved by every slot.
* **R** slots take registers (listed in the table); they exist because VGAME's HUD code (segment 10A8,
  hand-written assembly) calls them directly. A port implements them as plain functions with the same
  arguments (`text_noclip_r(bp_struct, bx_string)` etc.) and may ignore the register/C duplication: slot 5 ≡ 4,
  7 ≡ 1, 8 ≡ 2, 9 ≡ 6, 10 ≡ 3 (the C wrapper loads `bx = s`, `bp = ctx` and jumps in).
* Return values of "void" slots are undefined (AX stays whatever it was); do not rely on them. Exception:
  the text slots write the end x back into the context (`ctx->x`).

## 3 Text (slots 1-10, 47) — verified

```c
typedef struct TextCtx {      /* near struct in the caller's data segment */
    int page;                 /* +00 destination page number (NOT the draw segment) */
    int flags;                /* +02 low byte: bit0 = add the font's extra rows to the height; ==1 exactly = opaque background */
    int fg;                   /* +04 foreground colour */
    int bg;                   /* +06 background colour (flags bit0 only) */
    int x, y;                 /* +08, +0A top-left of the first glyph; x is updated to the end x */
    int font;                 /* +0C font index 0..6 */
    int ymin, ymax;           /* +0E, +10  clip rows, inclusive (relative to nothing: absolute page rows) */
    int xmin, xmax;           /* +12, +14  clip columns, inclusive */
} TextCtx;                    /* clip fields are read only by the clipping variants */
```

Font data lives in the driver image (`tools/mgfont.py`); `font_ptr = word at 00D2+2*font`; header bytes
below `p`: `p-9-(last-first)..p-9` advance widths (proportional only), `p-8 first`, `p-7 last`, `p-6` =
bytes-per-glyph-row+1 (always 1 → 8 px), `p-5` fixed width (0 = proportional), `p-4` height, `p-3` spacing,
`p-2` extra rows, glyph c row r = `byte[p + r*(last-first+1) + (c-first)]`, MSB = leftmost pixel.

| Font | first..last | width | height | spacing | extra | Likely use |
|---|---|---|---|---|---|---|
| 0 | 0..127 | fixed 3 | 5 | 1 | 1 | tiny HUD digits |
| 1 | 0..127 | proportional | 8 | 1 | 0 | |
| 2 | 0..127 | fixed 3 | 4 | 1 | 1 | tiniest |
| 3 | 0..127 | proportional | 6 | 1 | 0 | |
| 4 | 32..127 | proportional | 7 | 1 | 0 | |
| 5 | 32..127 | proportional | 6 | 1 | 0 | |
| 6 | 32..127 | proportional | 8 | 1 | 1 | |

(Font 0/2 draw `fixed`-wide glyphs; advance = fixed + spacing. U-check *(U-font)*: `work/s32/fonts_sheet.png`.)

**Algorithm** (`text_init` 03E8, `draw` 061A):

1. `x,y := ctx`; if `s[0]==0` or `font > 7` (unsigned) → return (x unchanged). `p`, `height = p[-4] + (flags&1 ? p[-2] : 0)`,
   `spacing = p[-3]`, `fixed = p[-5] ? p[-5]+spacing : 0`, `stride = (last-first+1)`.
2. Clipping (only in the variants; order **right, left, vertical**):
   * **right** (31D): if `x >= xmax` return at once (nothing drawn, ctx unchanged). Walk the string adding glyph
     advances (glyphs outside first..last and colour bytes >= 80h add 0); at the first glyph where
     `x_end >= xmax+1`: `trim_r = x_end-(xmax+1)`, the byte after that glyph is replaced by NUL (saved, restored on
     exit). If the string ends first it is drawn unchanged (`trim_r = 0`).
   * **left** (2B9): if `xmax < xmin` return nothing. If `x >= xmin` no change. Else `x := xmin` and skip whole glyphs
     whose right edge is `<= xmin` (they are not drawn, their colour bytes are ignored); the first partially
     visible glyph gets `skip_l = width - (x_end - xmin)` pixels cut from its left. If the string ends first,
     nothing is drawn and the function returns with x = xmin.
   * **vertical** (37F): if `y > ymax` or `y+height-1 < ymin` nothing; if `y < ymin` then `font rows skipped = ymin-y`,
     `y := ymin`, `height := bottom-ymin+1`; if `bottom > ymax` then `height := ymax-y+1`.
3. Draw loop over the (possibly cut) string, with `fg_cur = ctx->fg`, `mode = ctx->flags & 0xFF`:
   * `c == 0` end; **`c & 0x80`: `fg_cur := c & 0x7F`**, no advance; `c` outside `first..last`: ignored.
   * `adv = fixed ? fixed : width[c-first] + spacing`; `vis = adv`; if `c` is the **last char of the string**
     (next byte is NUL) `vis -= trim_r` else (none); `vis -= skip_l` (first glyph only, then `skip_l := 0`).
   * rows `0..height-1`, glyph row bits `b` (MSB left, shifted left by `skip_l` first):
     * `mode == 1` (opaque): `vis` pixels per row: bit set → `fg_cur`, clear → `bg` (so the spacing column is painted bg).
     * otherwise transparent: `w = vis`, and for glyphs that are **not** last in the string `w -= spacing`;
       only set bits are written (`fg_cur`).
   * destination row `rowoff[y_row] + x` on `page_table[ctx->page]` (the **draw offset is not used**).
   * `x += vis`; `di += vis`. Glyphs whose `vis <= 0` are skipped without moving.
4. `ctx->x := x` (end x, after the clipped widths). The string is restored.

`font_char_width(ch, font)` (023A, slot 47): returns `adv(ch)` as above; 0 for `ch < 0`, `font` out of range, or
`ch` outside `first..last` (the caller of this exact routine uses it to centre text).

## 4 Pages, flipping, simple fills (verified)

* `page_select(n)` etc.: `draw_seg := page_table[n]` (no range check). `seg_set/seg_get` use a raw segment.
* `flip(ax)` (11F1): if `cs:[11DC]==0`: flag := 1, copy `ax` **words** from `page_table[1]:0000` to `A000:0000`
  (`rep movsw`, up to 32000 words); else flag := 0, copy nothing. Callers pass `rows*0A0h` (VGAME
  `1BDB`: `imul 0A0h`), i.e. they can flip only the top `rows` scanlines.
* `page_load(src)` (11DE): copy 32000 words from `src:0000` to `draw_seg:0000`.
* `clear_screen` zeroes `A000:0000..F9FF`; `clear_es` zeroes `ES:0000..F9FF` (set ES with slot 56 first).
* `blit_rect(sp,sx,sy,dp,dx,dy,w,h)` (1131): for each of `h` rows `memcpy(dst_page+rowoff[dy+r]+dx, src_page+rowoff[sy+r]+sx, w)`;
  **no clipping, no overlap handling** (forward copy). `w=0` copies nothing; `h=0` copies 65536 rows (never used).
* `rect_recolor(&page, x0,y0,x1,y1, old, new)` (1188): note the first argument is a **pointer to the page number**;
  every pixel equal to `old` in the inclusive rect becomes `new`. No clipping.
* `pixel_offset(x,y)` = `x + y*320`; `row_offset(y)` = `y*320`.

## 5 Lines and span fills (verified)

Both draw on `draw_seg:draw_off` with colour `cs`/data `[1866]` (`color_set`), **without clipping** (pixels wrap
inside the 64 KB segment).

```c
void line(int x1,int y1,int x2,int y2) {            /* 0599 */
    if (x1 > x2) swap both endpoints;                /* so x is non-decreasing */
    last_x = x1; last_y = y1;                        /* [185A],[185C] (slot 36 plots here) */
    if (x1==x2 && y1==y2) { put(x1,y1); return; }
    int dx = x2-x1, dy = y2-y1, sx = 1, sy = 320;
    if (dy < 0) { sy = -320; dy = -dy; }
    if (dx < dy) { swap(sx, sy); swap(dx, dy); }     /* major axis = larger extent */
    int major = dx, minor = dy;
    unsigned o = draw_off + y1*320 + x1;
    int e = -((major + 1) >> 1);
    for (int n = major; n >= 0; n--) {               /* major+1 pixels */
        seg[o] = colour;
        o += sx;                                     /* major step */
        e += minor;
        if (e >= 0) { e -= major; o += sy; }         /* minor step */
    }
}
```
(The original adds `sx-1` after `stosb`; net effect as above. `o` is 16-bit.)

`poly_fill(ymin, tbl, ymax, mode)` (0F3D; `poly_fill_solid` = mode 0): if `ymin < 0` return. For `y = ymax` **down to** `ymin`:
`L = *(int*)(SS:tbl + 2y)`, `R = *(int*)(SS:tbl + 0x1B8 + 2y)` (two 220-word edge tables in the caller's
data, 440 bytes apart); skip the row if `R < L`, or if `R == L && (R == 0 || R == 319)`; else fill
`R-L+1` pixels at `draw_off + y*320 + L`:

| mode | per pixel |
|---|---|
| 0 | `= colour` |
| 1 | `\|= colour` |
| 2 | `&= colour` |
| ≥3 | `= ((old & 0x0E) >> 1) | 0x98` (maps to the 98h-9Fh green MFD ramp) |

(modes 0-2 are done word-wise with an odd-byte fix-up; the result is the byte-wise rule above.) START's
`gfx_fill_rect` (`8A32`, shared copy in END/DSWAP/VGAME-11CE) builds such tables with `L = x0`, `R = x1` for rows y0..y1.

## 6 HUD tick strip (slot 11, `071A`) — verified, semantics for S3.6

`hud_tick_strip(si, bx, cl, dl)`: draws on the **draw segment** (no draw offset: `es:=cs:[0194]`, row table only)
with colour `[1866]`. Registers: `SI` selects the strip (VGAME passes 0 or 2; `cl != 0` adds 4), `BX` start row,
`CL` flag, `DL` bias.

```c
dir = (si == 0) ? -1 : +1;           /* STD when si == 0: pixels are written leftwards from the column */
if (dl >= 1) bx += 0x14;  bx -= 1;   /* (bx = bx-1, +20 when dl>=1) */
if (cl) { si += 4; bx += 1; }
col  = word [0x1966 + si];           /* si=0:71, 2:248, 4:120, 6:200 (columns) */
rmin = word [0x196E + si];           /* 22, 22, 34, 34 */
rmax = word [0x1976 + si];           /* 82, 82, 74, 74 */
k = 10;
for (;; bx -= 2) {                   /* rows climb by 2 */
    if (bx < rmin) break;
    if (bx <= rmax) {                /* inside the visible window */
        o = rowoff[bx] + col;
        if (k == 5)  put 2 px;
        else if (k == 10) { put 2 px; if (cl == 0) put 1 more px; }   /* ticks go along `dir` */
    }
    k = (k == 10) ? 1 : k + 1;       /* 10-row period: major tick every 20 px, minor every 10 */
}
```
It is a vertical tape scale (the four columns are the two flight-data tapes of the cockpit). Port it verbatim.

## 7 RLE sprites and the pool (slots 22, 76-78, 77, 21) — verified

**Sprite record** (all in one segment, paragraph aligned, offset 0): `w:u16, h:u16`, then `h` rows of
`{len:u16, lead:u16, len bytes}` (`lead` = number of leading colour-0 pixels, `len` = literal bytes from the first to
the last non-zero pixel, zeros inside the literal run are stored; `len==0` means an all-transparent row).
Trailing transparent pixels are implied (`w - lead - len`). Row stride = `len + 4`.

* `pool_init` (09A8): `n = mem_paras_free()`; if 0 → return 0. DOS-allocate `n` paragraphs (INT 21/48; the pool takes
  **all** free memory), state: `cur_off=0`, `cur_seg=base_seg=ax`, `end_seg=ax+n`. Returns `ax` (0 on failure).
* `sprite_grab(page, x, y, w, h)` (0B0C): `|page|` (sign ignored) selects the source; if the pool has no room → 0 (a zero `w` or `h` writes only the header). Writes `w,h` at `cur_seg:cur_off`; for each source row (`si = rowoff[y]+x`, +320 per row):
  encode (0A6A) — leading zeros → `lead`, literal from first non-zero to last non-zero → `len`. Before each row, if
  `w + 4 + cur_off > limit` (limit = `min(avail_paras,0xFFF)*16` bytes) the sprite's stored `h` is decremented,
  the pool is **disabled** (`end_seg := 0`) and the row is dropped. Finally `cur_seg += (cur_off+15)>>4`, `cur_off := 0`
  and the **sprite's segment** is returned.
* `pool_free` (09D4): `used = cur_seg - base_seg` paragraphs; `used==0` → INT 21/49 free, else INT 21/4A shrink
  to `used`; state cleared; returns the base segment (ES).
* `sprite_scaled(ctx, x, y, dw, dh, off, seg)` (0B70). `ctx` is a window: `+0 page`, `+0E ymin`, `+10 ymax`,
  `+12 xmin`, `+14 xmax` (the same layout as TextCtx); `x,y` are **relative to (xmin,ymin)**; `|dw|,|dh|` = destination
  size, **a negative `dw` mirrors horizontally, a negative `dh` flips vertically**. Source = sprite at `seg:off`
  (an `off` of 0 for pool sprites). Steps:
  1. `|dw|,|dh|`; if the sprite header `w` or `h` is 0 → draw nothing.
  2. Clip against `0..xmax-xmin`, `0..ymax-ymin` (0BC0): `[856]` = rows cut at the top (or when `y<0`: `-y`), `[858]` cols cut left,
     `[85A]` rows cut at the bottom, `[85C]` cols cut right; entirely outside → nothing.
  3. Returns far pointer `cs:084E` → words `{x', y', w', h'}` = visible rectangle (relative to the window), all zero if not drawn.
  4. Drawing (0C95): dest `di := rowoff[y+ymin+...] + x+xmin` on `page_table[ctx->page]`. Both axes use a DDA:
     x: `acc = -dw`; per dest pixel `acc += sw; while acc >= 0 { src++; acc -= dw }`-style advance exactly as in
     0DD3..0DF7 (advance after the pixel is written; a source pixel 0 is **transparent** and skipped without writing);
     y: `acc = -dh; acc += sh` per dest row, `while acc >= 0 { next source row (si += len+4); acc -= dh }` (0E0F..0E26).
     Left/top clip is realised by pre-running the same accumulators over the cut columns/rows (0CFF-0D5F). Each
     source row is first expanded into the buffer `cs:0868` (`lead` zeros, literal, trailing zeros) so the left
     skip is a plain index.
     Mirroring: the code patches its own `cld/inc di` (0DDB/0DEC) into `std/dec di`, starts at the right
     edge and uses the opposite clip counter. Vertical flip: start at the last row, stride `-320`.
  The port may implement this as an ordinary scaler with identical accumulator maths (it is not performance critical).
* Slot 21 = the drawing half (`0C95`) with registers; not called.

## 8 Key-colour blits, mode, blanking, palette, effects

* `blit_key(b)` (0EE6) with
  ```c
  typedef struct BlitCtx { unsigned seg;      /* +00 SOURCE SEGMENT (not a page number) */
      int sx, sy;                             /* +02, +04 */
      int dpage, dx, dy;                      /* +06, +08, +0A destination page/position */
      int w, h;                               /* +0C, +0E */
      int ymin, ymax, xmin, xmax; } BlitCtx;  /* +10,+12,+14,+16 clip, inclusive; only blit_key_clip */
  ```
  copies `w x h` bytes, **source colour 0 is skipped**; source `seg:rowoff[sy+r]+sx`. Clipped variant (0E41) clips
  `dx..dx+w-1` to `xmin..xmax` and `dy..dy+h-1` to `ymin..ymax`, moving `sx,sy` by the cut amounts, then calls the
  unclipped code; **it modifies `b->w,b->h` temporarily but restores them** (pushed/popped), whereas `dx,dy,sx,sy`
  are left modified (the context is consumed).
* `scanline_put` (51/52): `memcpy(ES:DI, SS:BP, 320)` — used by the .PIC decoder row by row.
* `mode13_set` (04B6): set mode 13h. Port: create the 320x200 8-bit surface; check = always OK.
* `screen_off/on` (04CA/04E4): wait for vertical retrace, blank/unblank (CGA mode-control port 3D8h and sequencer
  clocking-mode bit 5). Port: toggle a "blank" flag (render black); the retrace wait = present one frame.
* `palette16_set(n)` (0536): n = 0..4; `DAC[0..15] := table[n]` (INT 10h/1012h), then `screen_on`. Tables are
  16 x RGB triples (6-bit) at `1871 + 30h*n`: n=0 standard VGA/EGA colours (`000000 00002A 002A00 002A2A 2A0000 2A002A 2A1500
  2A2A2A 151515 15153F 153F15 153F3F 3F1515 3F153F 3F3F15 3F3F3F`); n=1 same with colour 5 black; n=2 colour 13 black;
  n=3 all black; n=4 colours 1 and 10 black. **Not called by any program.**
* `explosion_fx_tick` (125C, slot 46; `shake_set(n)` slot 79 loads the counter). Each call:
  `rng = rng*5 + 1` (16-bit, seed 04D2h, in the driver data `1982`); `i = rng & 3`; colour index `c = {0Ch,04h,0Ch,0Eh}[i]`
  (`197E`); `DAC[13] := table0[c]` (red/yellow flicker; the programs use colour 13 for muzzle/explosion flashes);
  if `shake > 0`: `shake--`, CRTC register 0Dh (start address low) := `(rng>>8) & 3` (0 when the counter reaches 0), i.e.
  the whole display shifts by 0..3 CRTC units (in mode 13h a unit is 4 pixels *(U)* — port: shift the presented image
  by `4*k` px horizontally and wrap, or skip as an enhancement). Only VGAME calls slot 46 (1 site) and slot 79 (2 sites).
* Constants: slots 23/49 = 64000 (page bytes), 28 = 5580h, 29 = 1450h (VGAME view offsets: row 68 col 128 / row 16 col 80,
  used with `offset_set`), 63 = 3, 66 = 0, 67 = 1.

## 9 Program-side video code (not in the driver)

* **.PIC decoder** (LZW + RLE-90, format in `FORMATS.md`): START `86A8-8A31`, the same code in END `47FF-49D1`, DSWAP
  `0AAB-0C7D`, VGAME `11CE:000A-012A` (`shared_matches.csv`). Layers: `lzw_reset`, `lzw_next` (code reader over a
  512-byte file buffer `DS:6235`, string stack on a private SP), `rle90_fill`, `pic_row` (320 pixels into `DS:8C20`),
  `pic_begin` (header: byte0 bit 7 set → 8-bit pixels, `maxbits = 256-byte0`; else 4-bit nibble pairs), and four
  **front ends** (`pic_to_page`, `pic_to_seg_a/b/c`) that call `clear_es` and push each row with slots 51-55. All four
  produce the same pixels; differences: destination (page number vs segment), file-reader callback (`81C6` / `819D`),
  scanline slots. The port needs one `pic_decode(file, out64000)` (`tools/picdec.py` is the reference).
* **.PAN** frames: PLAYER `0000:1006` (RLE delta on the screen, `RE_GUIDE` §4); **.SS** sprites: MPS_LOGO `006D:0112`.
  Both blit straight to `A000` (they do not use MGRAPHIC).
* **DAC**: START `8427` (`bios_vga_palette`, INT 10h/1012h), END `42BE/4393`, DSWAP `0848`; VGAME `14A2:000E`
  (`set_dac`, retrace wait; grey-scale path when BIOS 0489h bit 1) and `14A2:00C5` (`get_dac`); flight palettes in
  `FORMATS.md`. `PALETTES.PAL`/`.PAL` loading = START `0000:826C`. Port: one `dac_set(first,count,rgb6)`.
* `gfx_fill_rect` (START `8A32`): `Win` pointer: `+0` page, `+4` colour byte; saves the draw segment (slot 16), selects the page
  (13), sets the colour (32), builds the edge tables, `poly_fill_solid` (40), restores the segment (15).

## 10 Per-program slot use (`vectors.txt`)

START: 0,5,13-16,22,32,40,42x88,47,51-56,58-60,69,70,75-78. END: same set (42x48; no 60, 79). VGAME: 0-6,11-16,18,22,24-27,29,31x21,32x11,33,37,40-42x26,
44-46,50-56,58,59,62,63,65,69-71,73,75-79. DSWAP: 0,14,42x3,51-56,58,59,69,70,75. SETUP: 5, 47 (library code it
does not reach). Slot 42 (`blit_rect`) is by far the most used call.

## 11 Port plan (video module `video.c`)

1. Host: one `uint8_t screen[64000]` (page 0 / `A000`) plus a palette[256][3] (6→8 bit by `v*255/63` or `v<<2|v>>4`),
   scaled by SDL; `flip` marks the frame dirty and presents it; `screen_off` shows black.
2. Guest memory: pages and sprite pool must be real guest segments (the programs copy the segment numbers around: `comm[20h]`,
   `[E558]`, sprite handles from `sprite_grab`). Use the Aces `mem` layer (`seg*16+off`) and let `gfx_alloc` / `pool_init`
   obtain paragraphs from its DOS-allocation emulation. `A000` maps to the screen buffer.
3. Implement the 84 slots as C functions with the signatures in `video_symbols.csv`; register-convention slots take the
   same values as parameters. Empty/`nop` slots stay empty.
4. Test order (user, after S4.x): title screens (`pic` + flip), a text screen (fonts 0-6, clipping), cockpit with HUD
   (line, poly modes, tick strip, sprites).

## 12 Open items

* `U-font` (pending): `work/s32/fonts_sheet.png` — are the 7 fonts correct (no shifted/garbled rows)?
* Shake unit (slot 46) and the DAC 13 flash colours need a runtime look *(U)*; harmless to postpone to the enhancement phase.
* `cs:11DC` is a never-set patch point; the port treats flip as always enabled.
* Text clipping variants (§3) are verified from the code only; no screen using them has been identified yet.

## 13 Slot table (generated: `python tools/video_syms.py` → `work/s32/slot_table.md`)

| Slot | Code | Name | Conv | Signature | Meaning |
|---|---|---|---|---|---|
| 0 | 04fe | `gfx_alloc` | C | `unsigned gfx_alloc(int n)` | n=0: returns A000h. else DOS-allocs FA00h bytes (one page), returns its segment; failure prints "Insufficient memory for MCGA graphics" and exits |
| 1 | 0224 | `text_clip_y_r` | R | `BX=str, BP=TextCtx*` | text with vertical clip (ymin/ymax of ctx); register entry of 7 |
| 2 | 020e | `text_clip_xl_r` | R | `BX=str, BP=TextCtx*` | text with left clip (xmin); register entry of 8 |
| 3 | 0219 | `text_clip_xr_r` | R | `BX=str, BP=TextCtx*` | text with right clip (xmax); register entry of 10 |
| 4 | 022f | `text_noclip_r` | R | `BX=str, BP=TextCtx*` | text without clipping; register entry of 5 |
| 5 | 019a | `text_noclip` | C | `void text_noclip(TextCtx *c, char *s)` | draw string, no clipping; c->x updated to the end x |
| 6 | 0203 | `text_clip_all_r` | R | `BX=str, BP=TextCtx*` | text with right+left+vertical clip; register entry of 9 |
| 7 | 01c7 | `text_clip_y` | C | `void text_clip_y(TextCtx *c, char *s)` | vertical clip only |
| 8 | 01db | `text_clip_xl` | C | `void text_clip_xl(TextCtx *c, char *s)` | left clip only |
| 9 | 01b3 | `text_clip_all` | C | `void text_clip_all(TextCtx *c, char *s)` | right + left + vertical clip |
| 10 | 01ef | `text_clip_xr` | C | `void text_clip_xr(TextCtx *c, char *s)` | right clip only |
| 11 | 071a | `hud_tick_strip` | R | `SI=0|2, BX=y, CL=flag, DL=sel` | draws a vertical tick scale (HUD tape) at one of 4 fixed columns on the draw segment (see spec) |
| 12 | 07a3 | `page_select_1` | R | `-` | draw segment := page table[1] |
| 13 | 07b2 | `page_select_r` | R | `AX=page` | draw segment := page table[AX] |
| 14 | 07ac | `page_select` | C | `void page_select(int page)` | draw segment := page table[page] |
| 15 | 07c3 | `seg_set` | R | `AX=seg` | draw segment := AX |
| 16 | 07c8 | `seg_get` | R | `-> AX` | AX := draw segment |
| 17 | 0ed5 | `blit_key` | C | `void blit_key(BlitCtx *b)` | copy rect from segment b->seg to page b->dpage, source byte 0 = transparent, no clipping |
| 18 | 0ee6 | `blit_key_r` | R | `BP=BlitCtx*` | register form of 17 |
| 19 | 0e30 | `blit_key_clip` | C | `void blit_key_clip(BlitCtx *b)` | as 17 but clipped to b->clip |
| 20 | 0e41 | `blit_key_clip_r` | R | `BP=BlitCtx*` | register form of 19 |
| 21 | 0c95 | `sprite_scaled_r` | R | `BP=SpriteCtx*, DS:SI=sprite, DX=x, DI=y` | draw part of 22 (after clip); not called by the programs |
| 22 | 0b70 | `sprite_scaled` | C | `RectP *sprite_scaled(SpriteCtx *w, int x, int y, int dw, int dh, unsigned off, unsigned seg)` | clipped scaled/mirrored draw of a pool sprite; returns far ptr to visible rect (x,y,w,h words) |
| 23 | 0480 | `get_page_bytes` | R | `-> AX` | returns 0FA00h |
| 24 | 07cd | `offset_clear` | R | `-` | draw offset := 0 (alias of 25) |
| 25 | 07cd | `offset_clear` | R | `-` | draw offset := 0 |
| 26 | 07d5 | `offset_set` | C | `void offset_set(unsigned off)` | draw offset := off (used by line, pixel, poly, 48; NOT by text/blits/sprites) |
| 27 | 07db | `offset_set_r` | R | `AX=off` | draw offset := AX |
| 28 | 07e0 | `get_view_off_a` | R | `-> AX` | returns constant 5580h |
| 29 | 07e4 | `get_view_off_b` | R | `-> AX` | returns constant 1450h |
| 30 | 07e8 | `offset_get` | R | `-> AX` | AX := draw offset |
| 31 | 0599 | `line` | R | `AX=x1 BX=y1 CX=x2 DX=y2` | Bresenham line in current colour on draw segment+offset, no clipping |
| 32 | 0561 | `color_set_r` | R | `AH=colour` | current colour := AH |
| 33 | 055b | `color_set` | C | `void color_set(int c)` | current colour := (byte)c |
| 34 | 056d | `nop` | R | `-` | retf |
| 35 | 056d | `nop` | R | `-` | retf |
| 36 | 056e | `pixel_last` | R | `-` | plot one pixel at the last line() start point (unused by programs) |
| 37 | 0f3d | `poly_fill` | R | `AX=ymin BX=tbl CX=ymax DX=mode` | span fill between edge tables; mode 0 solid, 1 OR, 2 AND, else shade 98h+ |
| 38 | 07f3 | `data_cc_set_r` | R | `AX=v` | driver data word [00CC] := AX (never read) |
| 39 | 0804 | `data_ce_set_r` | R | `AX=v` | driver data word [00CE] := AX (never read) |
| 40 | 0f3b | `poly_fill_solid` | R | `AX=ymin BX=tbl CX=ymax` | = 37 with mode 0 |
| 41 | 1188 | `rect_recolor` | C | `void rect_recolor(int *page, int x0, int y0, int x1, int y1, int old, int new)` | replace colour old by new inside the rect of page *page |
| 42 | 1131 | `blit_rect` | C | `void blit_rect(int spage, int sx, int sy, int dpage, int dx, int dy, int w, int h)` | opaque rectangle copy between pages (row by row, no overlap handling) |
| 43 | 1122 | `clear_screen` | R | `-` | zero A000:0000..F9FF |
| 44 | 11f1 | `flip` | R | `AX=words` | copy AX words page[1] -> A000 (unless the disable word 019C:11DC is non-zero); sets flag 45 |
| 45 | 080f | `flip_flag` | R | `-> AL` | AL := 1 if the last 44 copied, 0 if it was disabled |
| 46 | 125c | `explosion_fx_tick` | R | `-` | per-frame: DAC[13] := random red/yellow, CRTC start-address-low shake while counter 79 > 0 |
| 47 | 023a | `font_char_width` | C | `int font_char_width(int ch, int font)` | advance width incl. spacing, 0 if the glyph does not exist |
| 48 | 11de | `page_load` | C | `void page_load(unsigned srcseg)` | copy 64000 bytes from srcseg to the draw segment (offset 0, ignores draw offset) |
| 49 | 0486 | `get_page_bytes_b` | R | `-> AX` | returns 0FA00h |
| 50 | 04a4 | `mem_paras_free` | R | `-> AX` | largest free DOS block in paragraphs (INT 21/48 probe); 0 if none |
| 51 | 12e0 | `scanline_put` | R | `ES:DI, SS:BP=320 bytes` | copy 320 bytes (160 words) from SS:BP to ES:DI |
| 52 | 12e0 | `scanline_put` | R | `ES:DI, SS:BP=320 bytes` | same as 51 |
| 53 | 12dd | `nop` | R | `-` | retf (post-scanline hook of other drivers) |
| 54 | 12de | `nop` | R | `-` | retf |
| 55 | 12df | `nop` | R | `-` | retf |
| 56 | 12d5 | `page_es` | R | `SI=page` | ES := page table[SI] |
| 57 | 12c2 | `page_seg_set` | C | `void page_seg_set(int page, unsigned seg)` | page table[page] := seg |
| 58 | 12ba | `row_offset` | R | `DI=y -> AX` | AX := y*320 |
| 59 | 1118 | `clear_es` | R | `ES` | zero ES:0000..F9FF |
| 60 | 04b6 | `mode13_set` | R | `-` | INT 10h AX=0013h; verifies INT 10h/0Fh = 13h else prints "MCGA is not supported on your machine" and exits |
| 61 | 12d4 | `nop` | R | `-` | retf |
| 62 | 0814 | `pixel_offset` | C | `unsigned pixel_offset(int x, int y)` | x + y*320 |
| 63 | 0826 | `get_const_3` | R | `-> AX` | returns 3 |
| 64 | 07ed | `data_cc_set` | C | `void data_cc_set(int v)` | driver data word [00CC] := v (never read) |
| 65 | 07fe | `data_ce_set` | C | `void data_ce_set(int v)` | driver data word [00CE] := v (never read) |
| 66 | 048b | `get_const_0` | R | `-> AX` | returns 0 |
| 67 | 0490 | `get_const_1` | R | `-> AX` | returns 1 |
| 68 | 0536 | `palette16_set` | C | `void palette16_set(int n)` | DAC 0..15 := table n (0..4) via INT 10h/1012h, then screen on (70); not called by the programs |
| 69 | 04ca | `screen_off` | R | `-` | wait retrace, blank the display (3D8h:=2, sequencer reg1 bit5) |
| 70 | 04e4 | `screen_on` | R | `-` | wait retrace, unblank (3D8h:=0Ah, sequencer reg1 bit5 cleared) |
| 71 | 0e30 | `blit_key_clip` | C | `alias of 19` |  |
| 72 | 0e41 | `blit_key_clip_r` | R | `alias of 20` |  |
| 73 | 0ed5 | `blit_key` | C | `alias of 17` |  |
| 74 | 0ee6 | `blit_key_r` | R | `alias of 18` |  |
| 75 | 0791 | `page_seg_set_b` | C | `void page_seg_set_b(unsigned seg, int page)` | page table[page] := seg (argument order differs from 57) |
| 76 | 09a8 | `pool_init` | R | `-> AX` | take ALL free DOS memory as the sprite pool; AX = base segment, 0 if none |
| 77 | 0b0c | `sprite_grab` | C | `unsigned sprite_grab(int page, int x, int y, int w, int h)` | RLE-compress a page rectangle into the pool; returns the sprite segment, 0 if the pool is exhausted |
| 78 | 09d4 | `pool_free` | R | `-> AX` | shrink the pool to what was used (free it if nothing); clears pool state |
| 79 | 1251 | `shake_set` | C | `void shake_set(int frames)` | shake counter := (byte)frames for 46 |
| 80 | 046f | `nop` | R | `-` | retf |
| 81 | 0470 | `nop` | R | `-` | retf |
| 82 | 0471 | `nop` | R | `-` | retf |
| 83 | 07c2 | `nop` | R | `-` | retf |
