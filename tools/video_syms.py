"""S3.2: MGRAPHIC slot table -> port/spec/video_symbols.csv and work/s32/slot_table.md (pasted into video.md).
Sources: work/s12/vectors.txt (slot -> code offset), work/s32/mg.asm (tools/rawdis.py Game/MGRAPHIC.EXE 019c:0 1300).
Calling convention tags: C = C far call, args on the stack (arg1 at [sp+4] on entry, caller pops); R = registers."""
import csv, os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
vec = {}
cur = None
for l in open(os.path.join(ROOT, 'work', 's12', 'vectors.txt')):
    if l.startswith('MGRAPHIC'):
        cur = True
    elif l[0] != ' ':
        cur = False
    m = re.match(r'\s+slot\s+(\d+)\s+\[\s*\d+\]\s+019c:([0-9a-f]{4})', l)
    if cur and m:
        vec[int(m.group(1))] = m.group(2)

# slot: (name, conv, signature, note)
S = {
    0: ('gfx_alloc', 'C', 'unsigned gfx_alloc(int n)', 'n=0: returns A000h. else DOS-allocs FA00h bytes (one page), returns its segment; failure prints "Insufficient memory for MCGA graphics" and exits'),
    1: ('text_clip_y_r', 'R', 'BX=str, BP=TextCtx*', 'text with vertical clip (ymin/ymax of ctx); register entry of 7'),
    2: ('text_clip_xl_r', 'R', 'BX=str, BP=TextCtx*', 'text with left clip (xmin); register entry of 8'),
    3: ('text_clip_xr_r', 'R', 'BX=str, BP=TextCtx*', 'text with right clip (xmax); register entry of 10'),
    4: ('text_noclip_r', 'R', 'BX=str, BP=TextCtx*', 'text without clipping; register entry of 5'),
    5: ('text_noclip', 'C', 'void text_noclip(TextCtx *c, char *s)', 'draw string, no clipping; c->x updated to the end x'),
    6: ('text_clip_all_r', 'R', 'BX=str, BP=TextCtx*', 'text with right+left+vertical clip; register entry of 9'),
    7: ('text_clip_y', 'C', 'void text_clip_y(TextCtx *c, char *s)', 'vertical clip only'),
    8: ('text_clip_xl', 'C', 'void text_clip_xl(TextCtx *c, char *s)', 'left clip only'),
    9: ('text_clip_all', 'C', 'void text_clip_all(TextCtx *c, char *s)', 'right + left + vertical clip'),
    10: ('text_clip_xr', 'C', 'void text_clip_xr(TextCtx *c, char *s)', 'right clip only'),
    11: ('hud_tick_strip', 'R', 'SI=0|2, BX=y, CL=flag, DL=sel', 'draws a vertical tick scale (HUD tape) at one of 4 fixed columns on the draw segment (see spec)'),
    12: ('page_select_1', 'R', '-', 'draw segment := page table[1]'),
    13: ('page_select_r', 'R', 'AX=page', 'draw segment := page table[AX]'),
    14: ('page_select', 'C', 'void page_select(int page)', 'draw segment := page table[page]'),
    15: ('seg_set', 'R', 'AX=seg', 'draw segment := AX'),
    16: ('seg_get', 'R', '-> AX', 'AX := draw segment'),
    17: ('blit_key', 'C', 'void blit_key(BlitCtx *b)', 'copy rect from segment b->seg to page b->dpage, source byte 0 = transparent, no clipping'),
    18: ('blit_key_r', 'R', 'BP=BlitCtx*', 'register form of 17'),
    19: ('blit_key_clip', 'C', 'void blit_key_clip(BlitCtx *b)', 'as 17 but clipped to b->clip'),
    20: ('blit_key_clip_r', 'R', 'BP=BlitCtx*', 'register form of 19'),
    21: ('sprite_scaled_r', 'R', 'BP=SpriteCtx*, DS:SI=sprite, DX=x, DI=y', 'draw part of 22 (after clip); not called by the programs'),
    22: ('sprite_scaled', 'C', 'RectP *sprite_scaled(SpriteCtx *w, int x, int y, int dw, int dh, unsigned off, unsigned seg)', 'clipped scaled/mirrored draw of a pool sprite; returns far ptr to visible rect (x,y,w,h words)'),
    23: ('get_page_bytes', 'R', '-> AX', 'returns 0FA00h'),
    24: ('offset_clear', 'R', '-', 'draw offset := 0 (alias of 25)'),
    25: ('offset_clear', 'R', '-', 'draw offset := 0'),
    26: ('offset_set', 'C', 'void offset_set(unsigned off)', 'draw offset := off (used by line, pixel, poly, 48; NOT by text/blits/sprites)'),
    27: ('offset_set_r', 'R', 'AX=off', 'draw offset := AX'),
    28: ('get_view_off_a', 'R', '-> AX', 'returns constant 5580h'),
    29: ('get_view_off_b', 'R', '-> AX', 'returns constant 1450h'),
    30: ('offset_get', 'R', '-> AX', 'AX := draw offset'),
    31: ('line', 'R', 'AX=x1 BX=y1 CX=x2 DX=y2', 'Bresenham line in current colour on draw segment+offset, no clipping'),
    32: ('color_set_r', 'R', 'AH=colour', 'current colour := AH'),
    33: ('color_set', 'C', 'void color_set(int c)', 'current colour := (byte)c'),
    34: ('nop', 'R', '-', 'retf'),
    35: ('nop', 'R', '-', 'retf'),
    36: ('pixel_last', 'R', '-', 'plot one pixel at the last line() start point (unused by programs)'),
    37: ('poly_fill', 'R', 'AX=ymin BX=tbl CX=ymax DX=mode', 'span fill between edge tables; mode 0 solid, 1 OR, 2 AND, else shade 98h+'),
    38: ('data_cc_set_r', 'R', 'AX=v', 'driver data word [00CC] := AX (never read)'),
    39: ('data_ce_set_r', 'R', 'AX=v', 'driver data word [00CE] := AX (never read)'),
    40: ('poly_fill_solid', 'R', 'AX=ymin BX=tbl CX=ymax', '= 37 with mode 0'),
    41: ('rect_recolor', 'C', 'void rect_recolor(int *page, int x0, int y0, int x1, int y1, int old, int new)', 'replace colour old by new inside the rect of page *page'),
    42: ('blit_rect', 'C', 'void blit_rect(int spage, int sx, int sy, int dpage, int dx, int dy, int w, int h)', 'opaque rectangle copy between pages (row by row, no overlap handling)'),
    43: ('clear_screen', 'R', '-', 'zero A000:0000..F9FF'),
    44: ('flip', 'R', 'AX=words', 'copy AX words page[1] -> A000 (unless the disable word 019C:11DC is non-zero); sets flag 45'),
    45: ('flip_flag', 'R', '-> AL', 'AL := 1 if the last 44 copied, 0 if it was disabled'),
    46: ('explosion_fx_tick', 'R', '-', 'per-frame: DAC[13] := random red/yellow, CRTC start-address-low shake while counter 79 > 0'),
    47: ('font_char_width', 'C', 'int font_char_width(int ch, int font)', 'advance width incl. spacing, 0 if the glyph does not exist'),
    48: ('page_load', 'C', 'void page_load(unsigned srcseg)', 'copy 64000 bytes from srcseg to the draw segment (offset 0, ignores draw offset)'),
    49: ('get_page_bytes_b', 'R', '-> AX', 'returns 0FA00h'),
    50: ('mem_paras_free', 'R', '-> AX', 'largest free DOS block in paragraphs (INT 21/48 probe); 0 if none'),
    51: ('scanline_put', 'R', 'ES:DI, SS:BP=320 bytes', 'copy 320 bytes (160 words) from SS:BP to ES:DI'),
    52: ('scanline_put', 'R', 'ES:DI, SS:BP=320 bytes', 'same as 51'),
    53: ('nop', 'R', '-', 'retf (post-scanline hook of other drivers)'),
    54: ('nop', 'R', '-', 'retf'),
    55: ('nop', 'R', '-', 'retf'),
    56: ('page_es', 'R', 'SI=page', 'ES := page table[SI]'),
    57: ('page_seg_set', 'C', 'void page_seg_set(int page, unsigned seg)', 'page table[page] := seg'),
    58: ('row_offset', 'R', 'DI=y -> AX', 'AX := y*320'),
    59: ('clear_es', 'R', 'ES', 'zero ES:0000..F9FF'),
    60: ('mode13_set', 'R', '-', 'INT 10h AX=0013h; verifies INT 10h/0Fh = 13h else prints "MCGA is not supported on your machine" and exits'),
    61: ('nop', 'R', '-', 'retf'),
    62: ('pixel_offset', 'C', 'unsigned pixel_offset(int x, int y)', 'x + y*320'),
    63: ('get_const_3', 'R', '-> AX', 'returns 3'),
    64: ('data_cc_set', 'C', 'void data_cc_set(int v)', 'driver data word [00CC] := v (never read)'),
    65: ('data_ce_set', 'C', 'void data_ce_set(int v)', 'driver data word [00CE] := v (never read)'),
    66: ('get_const_0', 'R', '-> AX', 'returns 0'),
    67: ('get_const_1', 'R', '-> AX', 'returns 1'),
    68: ('palette16_set', 'C', 'void palette16_set(int n)', 'DAC 0..15 := table n (0..4) via INT 10h/1012h, then screen on (70); not called by the programs'),
    69: ('screen_off', 'R', '-', 'wait retrace, blank the display (3D8h:=2, sequencer reg1 bit5)'),
    70: ('screen_on', 'R', '-', 'wait retrace, unblank (3D8h:=0Ah, sequencer reg1 bit5 cleared)'),
    71: ('blit_key_clip', 'C', 'alias of 19', ''),
    72: ('blit_key_clip_r', 'R', 'alias of 20', ''),
    73: ('blit_key', 'C', 'alias of 17', ''),
    74: ('blit_key_r', 'R', 'alias of 18', ''),
    75: ('page_seg_set_b', 'C', 'void page_seg_set_b(unsigned seg, int page)', 'page table[page] := seg (argument order differs from 57)'),
    76: ('pool_init', 'R', '-> AX', 'take ALL free DOS memory as the sprite pool; AX = base segment, 0 if none'),
    77: ('sprite_grab', 'C', 'unsigned sprite_grab(int page, int x, int y, int w, int h)', 'RLE-compress a page rectangle into the pool; returns the sprite segment, 0 if the pool is exhausted'),
    78: ('pool_free', 'R', '-> AX', 'shrink the pool to what was used (free it if nothing); clears pool state'),
    79: ('shake_set', 'C', 'void shake_set(int frames)', 'shake counter := (byte)frames for 46'),
    80: ('nop', 'R', '-', 'retf'),
    81: ('nop', 'R', '-', 'retf'),
    82: ('nop', 'R', '-', 'retf'),
    83: ('nop', 'R', '-', 'retf'),
}
rows = []
md = ['| Slot | Code | Name | Conv | Signature | Meaning |', '|---|---|---|---|---|---|']
for s in range(84):
    n, cv, sig, note = S[s]
    rows.append(['mgraphic', 'slot', '019c:' + vec[s], n, sig, 'video', 'slot %d' % s, note])
    md.append('| %d | %s | `%s` | %s | `%s` | %s |' % (s, vec[s], n, cv, sig, note))
# program-side video code (shared copies; address in each program)
P = [
    ('start', '0000:86a8', 'pic_to_page', 'void pic_to_page(int fh, int page)', 'decode a .PIC into page `page` (slots 56/59, rows via 51+53); reader callback 81C6'),
    ('start', '0000:871a', 'pic_to_seg_a', 'void pic_to_seg_a(int fh, unsigned seg)', 'decode a .PIC into segment (rows via 51+55)'),
    ('start', '0000:8782', 'pic_to_seg_b', 'void pic_to_seg_b(int fh, unsigned seg)', 'as above, reader 819D with buffer pos 200h'),
    ('start', '0000:87f6', 'pic_to_seg_c', 'void pic_to_seg_c(int fh, unsigned seg)', 'as above, rows via 52+54'),
    ('start', '0000:885f', 'pic_row', 'void pic_row(int row)', 'decode 320 pixels into the row buffer (DS:8C20)'),
    ('start', '0000:8889', 'pic_begin', 'void pic_begin(void)', 'read header byte, maxbits, pixel width; LZW reset'),
    ('start', '0000:88d4', 'lzw_reset', 'void lzw_reset(void)', 'width 9, free 256, clears the 2048-entry table'),
    ('start', '0000:890a', 'rle90_fill', 'void rle90_fill(unsigned n)', 'RLE-90 expander; writes n pixels (nibbles x2 when 4-bit)'),
    ('start', '0000:8983', 'lzw_next', 'int lzw_next(void)', 'next decoded byte; private stack for the string, reload via callback at [8D6A]'),
    ('start', '0000:8a32', 'gfx_fill_rect', 'void gfx_fill_rect(Win *w, int x0, int y0, int x1, int y1)', 'solid rect via edge tables + slot 40; saves/restores the draw segment; w->page, w->colour at +4'),
]
for ex, a, n, sig, note in P:
    rows.append([ex, 'func', a, n, sig, 'video', '', note])
w = csv.writer(open(os.path.join(ROOT, 'port', 'spec', 'video_symbols.csv'), 'w', newline=''))
w.writerow(['exe', 'kind', 'address', 'name', 'type', 'owner', 'other_names', 'notes'])
w.writerows(rows)
os.makedirs(os.path.join(ROOT, 'work', 's32'), exist_ok=True)
open(os.path.join(ROOT, 'work', 's32', 'slot_table.md'), 'w').write('\n'.join(md) + '\n')
print(len(rows), 'rows')
