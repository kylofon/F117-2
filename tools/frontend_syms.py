"""S3.4: front-end program symbols (MPS_LOGO, SETUP, PLAYER, DSWAP, MISC) -> port/spec/frontend_symbols.csv.
Addresses are image offsets in work/unpacked/<EXE>.EXE (seg:off as in port/<exe>_functions.csv). Hand-curated
from the disassembly (see port/spec/frontend.md); RTL/MicroProse-layer rows stay in platform_symbols.csv."""
import csv, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
R = []  # exe, address, name, signature, note


def a(exe, addr, name, sig, note):
    R.append((exe, addr, name, sig, note))


# ---- MPS_LOGO (LABS.EXE Version 1.32) ----
a('mps_logo', '0006:0072', 'logo_main', 'int logo_main(int argc, char **argv)', 'argv[1][0] = driver letter R/A/T/I/N; plays LABSLOGO.SS; returns 0 no key, 1 Esc, 2 Alt-Q, 3 other key')
a('mps_logo', '0006:000c', 'logo_fatal', 'void logo_fatal(char *msg)', 'prints "LABS.EXE Version 1.32" and "ERROR: msg", exit(-1)')
a('mps_logo', '0006:0044', 'text_clear_b800', 'void text_clear_b800(void)', 'fills B800:0000, 7D00h words, with 0')
a('mps_logo', '0006:033a', 'pal_set', 'void pal_set(uint8_t far *rgb768)', 'DAC write: port 3C8=0, 256x3 bytes to 3C9 (6-bit)')
a('mps_logo', '0006:038e', 'pal_get', 'void pal_get(uint8_t far *rgb768)', 'DAC read: 3C7=0, 256x3 from 3C9')
a('mps_logo', '0006:03e4', 'set_video_mode', 'void set_video_mode(int mode)', 'INT 10h AH=0')
a('mps_logo', '0045:0062', 'bios_ticks', 'uint32_t bios_ticks(void)', '0040:006C dword (18.2 Hz BIOS tick count)')
a('mps_logo', '0045:00bc', 'huge_alloc', 'void far *huge_alloc(uint32_t bytes)', 'DOS alloc (bytes>>4)+1 paragraphs; NULL on failure')
a('mps_logo', '0045:0102', 'huge_free', 'int huge_free(void far *p)', 'DOS free')
a('mps_logo', '0045:0116', 'huge_fread', 'uint32_t huge_fread(FILE *f, void far *dst, uint32_t n)', 'reads in 512-byte chunks, normalising the far pointer')
a('mps_logo', '006d:0112', 'ss_load', 'SS far *ss_load(char *name, int mode, uint8_t *pal768)', '.SS loader ("MADSS1"); name without extension; merges the sprite palette into pal768; error code in [0C74] 1..5; see frontend.md 2.3')
a('mps_logo', '006d:05a2', 'ss_free', 'void ss_free(SS far **p)', 'frees the series')
a('mps_logo', '006d:05be', 'ss_draw', 'void ss_draw(SS far *s, int frame, int x, int y, Screen scr)', 'x==3E7h or y==3E7h -> frame own x,y; key-colour blit (value FCh skipped); Screen = {h=200,w=320,off=0,seg=A000h}')
a('mps_logo', '006d:0002', 'ss_remap_rows_a', 'void ss_remap_rows_a(uint8_t far *p, int base)', 'adds base-1 to every non-FCh pixel (row markers F9/FE/FF)')
a('mps_logo', '006d:0084', 'ss_remap_rows_b', 'void ss_remap_rows_b(uint8_t far *p, int base)', 'second variant of the same walk (FA/FD/FC grammar)')
a('mps_logo', '00da:0352', 'ovl_load_log', 'unsigned ovl_load_log(char *name, char *name2)', 'overlay loader (RE_GUIDE 2.2); returns load segment, 0 on failure; used for ?SOUND.LOG')
a('mps_logo', '00da:0491', 'ovl_bind_log', 'void ovl_bind_log(unsigned seg)', 'fills 7-byte JMP FAR thunks at 00DA:0049+7*slot (slot base 100); entries from header +32h, cseg +28h, n +30h')
a('mps_logo', '00da:04c4', 'ovl_free_log', 'void ovl_free_log(unsigned seg)', 'INT 21h/49h')
a('mps_logo', '00da:0307', 'snd_init_log', 'int snd_init_log(int,int)', 'thunk to LOG slot 100; main repeats until AX==0')
a('mps_logo', '00da:030e', 'snd_cmd_log', 'void snd_cmd_log(int cmd)', 'thunk to LOG slot 101; 2 = play jingle, 0 = stop')
a('mps_logo', '00da:0315', 'snd_shutdown_log', 'void snd_shutdown_log(void)', 'thunk to LOG slot 102')
# ---- SETUP ----
a('setup', '0000:0010', 'setup_main', 'int setup_main(void)', 'text-mode configuration; exit(24h) = configured, exit(0) = abort')
a('setup', '0000:0503', 'break_check', 'void break_check(void)', 'if Ctrl-Break flag [0AC8] set -> exit(0)')
a('setup', '0000:0512', 'text_gotoxy', 'void text_gotoxy(int row, int col)', 'INT 10h AH=2 (via int86 0x1387)')
a('setup', '0000:053b', 'setup_exit', 'void setup_exit(int code)', 'break_hook_remove, release MISC overlay, timer_remove if installed, exit(code)')
a('setup', '0000:0a4b', 'wait_ticks', 'void wait_ticks(int n)', 'timer_install; wait until [0782] > n; timer_remove ([0782] is bumped by the timer tick)')
a('setup', '0000:0a9f', 'ovl_load', 'unsigned ovl_load(char *name)', 'overlay loader (RE_GUIDE 2.2), returns segment')
a('setup', '0000:14be', 'text_puts_at', 'void text_puts_at(int row, int col, char *s)', 'BIOS teletype writer; byte FFh in the string switches the attribute to the next byte (kept in cs:14BD)')
a('setup', '0000:1509', 'dos_puts', 'void dos_puts(char *s)', 'INT 21h AH=9 ($-terminated)')
a('setup', '0237:0004', 'mouse_present', 'int mouse_present(void)', 'INT 33h/0 after checking the vector is neither 0 nor IRET; 1 if the driver answers FFFFh')
a('setup', '0237:002c', 'joy_calib_center', 'void joy_calib_center(void)', 'joy_read_raw then both axes: centre := min := max := raw')
a('setup', '0237:004d', 'joy_calib_track', 'void joy_calib_track(void)', 'joy_read_raw then joy_scale for X and Y (widens min/max, writes scaled 0..FF to DS:E24/E25)')
a('setup', '0237:0115', 'joy_calib_get', 'void joy_calib_get(uint16_t far dst[20])', 'copies the 28h-byte calibration block (DS:0DF4) to comm+48h')
# ---- PLAYER ----
a('player', '0000:0010', 'player_main', 'int player_main(int argc, char **argv)', 'argv[1..] = .PAN names without extension; usage text "player pan_file(s)"')
a('player', '0000:005c', 'player_init', 'void player_init(void)', 'allocates all free memory (>= 2000h paragraphs) as ring buffer, comm=[0:4F0], binds sound, timer_install, video mode 13h only if not already 13h')
a('player', '0000:019a', 'player_run', 'void player_run(int argc)', 'loop poll_input until all cels are shown; if argc>2 waits for snd_cmd(2)==0; then player_exit(0)')
a('player', '0000:01f6', 'player_exit', 'void player_exit(int code)', 'snd_cmd(0), restore INT 8/1B/23, video mode restore (only if it was not 13h), free buffer, exit(code)')
a('player', '0000:0244', 'pan_play_file', 'void pan_play_file(char *name)', 'name + ".PAN"; streams/decodes (LZ bit stream, RE_GUIDE 4) into the ring buffer while the tick callback consumes cels')
a('player', '0000:0583', 'break_hooks_install', 'void break_hooks_install(void)', 'INT 1Bh and INT 23h -> ISR 0560 (sets [2092]=3)')
a('player', '0000:0696', 'poll_input', 'void poll_input(void)', 'first call starts the sound (snd_cmd 3) and arms the tick callback ([1E7A]:=0); polls joystick buttons 0/1 (comm+72h bit 0), mouse buttons (bit 1), keyboard; any input -> player_exit(key or 1Bh)')
a('player', '0000:0738', 'tick_callback', 'void far tick_callback(void)', 'called from the timer ISR each tick: fade step, or countdown [1DEA] then cel_show; ++[1DEE] tick counter; ++[1DF0] after the last cel')
a('player', '0000:07ea', 'cel_show', 'uint32_t cel_show(uint32_t p)', "'Z' header: speed -> [1DEC]=60/speed, palette (cel base - 768) loaded with the screen blanked, then fade in; 'B' cel: asserts bytes 42h,0,1,0; RLE-draws the delta onto A000 (1006); returns the next cel pointer")
a('player', '0000:0be2', 'pan_decode_chunk', 'int pan_decode_chunk(uint32_t dst)', 'LZ-stream decoder step (see tools/lzs.py)')
a('player', '0000:0d0c', 'pal_load', 'void pal_load(int first, int count, uint8_t far *rgb)', 'writes DAC; while a fade is pending ([13D4]!=0) writes black and keeps the target palette in [13DA]')
a('player', '0000:0db0', 'screen_off', 'void screen_off(void)', 'wait vblank; port 3C4 reg 1 |= 20h (screen off)')
a('player', '0000:0dca', 'screen_on', 'void screen_on(void)', 'wait vblank; reg 1 &= ~20h')
a('player', '0000:0e0c', 'fade_init', 'void fade_init(int secs)', 'steps = |secs|*60 -> [13D4]; builds per-tick colour slice from the vblank-measured loop speed')
a('player', '0000:0ee3', 'fade_step', 'void fade_step(void)', 'writes one slice of the scaled palette, --[13D4]')
a('player', '0000:1006', 'cel_rle_draw', 'uint32_t cel_rle_draw(uint32_t src, unsigned dseg, unsigned doff)', 'PAN cel RLE (RE_GUIDE 4): 00 n v fill, 1..7F literal, 81..FF skip, 80 w long op')
# ---- DSWAP ----
a('dswap', '0000:0010', 'dswap_main', 'int dswap_main(int argc, char **argv)', 'argv[1] = "0"/"1"/"2" -> start.exe/vgame.exe/end.exe; exit(1) when the program is reachable')
a('dswap', '0000:023e', 'swap_screen_show', 'void swap_screen_show(int msg)', 'SWAP.PIC + SWAP.PAL, then the prompt text')
# ---- MISC.EXE (code segment 0007 = image 0070h; entry offsets +0Ah,+05h,+00h,+29h,+18h,+18h) ----
for slot, off, nm, sig, note in [
    (90, '0007:000a', 'kbd_empty_misc', 'int kbd_empty(void)', 'INT 16h AH=1 -> 0 if a key is waiting else FFFFh'),
    (91, '0007:0005', 'kbd_get_misc', 'int kbd_get(void)', 'INT 16h AH=0 -> scan:ascii'),
    (92, '0007:0000', 'getche_misc', 'int getche(void)', 'INT 21h AH=1; AX returned as DOS leaves it (AH not cleared) - see frontend.md 6'),
    (93, '0007:0029', 'joy_button_misc', 'int joy_button(int n)', 'port 201h bit n+4 inverted: 1 = pressed'),
    (94, '0007:0018', 'kbd_locks_clear', 'void kbd_locks_clear(void)', '0040:0017 &= F0h (slots 94 and 95 are the same routine)'),
]:
    a('misc', off, nm, sig, 'slot %d: %s' % (slot, note))
out = os.path.join(ROOT, 'port', 'spec', 'frontend_symbols.csv')
import sys
if os.path.exists(out) and '--force' not in sys.argv:
    sys.exit('refusing to overwrite %s: it now also holds the S3.10 twin rows and the S3.10b closing-pass rows (hand edited); use --force only to rebuild the first 56 rows' % out)
with open(out, 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['exe', 'kind', 'address', 'name', 'type', 'owner', 'other_names', 'notes'])
    for exe, ad, nm, sig, note in R:
        w.writerow([exe, 'func', ad, nm, sig, 'frontend', '', note])
print('%d rows -> %s' % (len(R), out))
