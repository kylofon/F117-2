"""S3.1: names for the platform library (MSC 5/6 RTL + MicroProse system layer).

usage: python tools/platform_syms.py     -> port/spec/platform_symbols.csv + work/s31/unassigned.txt

END.EXE holds the whole library in one contiguous block (0000:3E45..5F63), read function by function
in S3.1 (verified in the disassembly). The same routines exist in the other six programs; port/
shared_matches.csv links them, so names are propagated through it. Library routines that END does not
contain (the stdio file layer: fopen/fread/_open/_read ...) are named from VGAME (EXTRA below,
confidence "likely": MSC library, recognised by structure, not read line by line).
Row format = port/symbols.csv (exe,kind,address,name,type,owner,other_names,notes).
"""
import csv, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXES = ['vgame', 'start', 'end', 'setup', 'player', 'dswap', 'mps_logo']

# addr (END) -> (name, type, owner, notes)
END = {
 '3e45': ('rand_n', 'int(int n)', 'platform', '(rand() * n) >> 15 via lmul/lshr; random number 0..n-1'),
 '3fa4': ('dos_allocmem', 'unsigned(unsigned bytes)', 'platform', 'INT 21/48, paragraphs = ceil(bytes/16); returns segment (AX unchanged on CF: caller tests)'),
 '3fca': ('dos_freemem', 'int(unsigned seg)', 'platform', 'INT 21/49; 0 on success'),
 '400f': ('dos_open', 'int(char *name, int mode)', 'platform', 'INT 21/3D; on error prints message and exits (fatal tail 41CA); resets disk-buffer pos [1EDF]=200h'),
 '4066': ('dos_creat', 'int(char *name, int attr)', 'platform', 'INT 21/3C; same fatal tail as dos_open'),
 '40bd': ('dos_close', 'void(int fd)', 'platform', 'INT 21/3E; error -> fatal print + exit'),
 '40de': ('dos_read', 'int(int fd, unsigned n, void *buf)', 'platform', 'INT 21/3F into DS:buf; error -> fatal'),
 '4109': ('dos_read_far', 'int(int fd, unsigned n, unsigned off, unsigned seg)', 'platform', 'INT 21/3F into seg:off'),
 '4160': ('dos_buf_fill', 'void(void)', 'platform', 'reads 200h bytes from handle [1EE1] into the disk buffer DS:1C77 (used by the PIC/PAN readers)'),
 '4181': ('dos_buf_setfd', 'void(int fd)', 'platform', '[1EE1] = fd; dos_buf_fill()'),
 '4199': ('dos_write_far', 'int(int fd, unsigned n, unsigned off, unsigned seg, unsigned add)', 'platform', 'INT 21/40 from seg:(off+add); error -> fatal. Contains the shared fatal tail 41CA: mode 3, print message + optional DOS error digit, exit 4C00'),
 '43c6': ('joy_poll', 'void(void)', 'platform', 'joy_read_raw() then joy_scale(0), joy_scale(1)'),
 '43d5': ('joy_read_raw', 'void(void)', 'platform', 'game port 201h: write, count loops until axis bits fall (CX max 65535, interrupts off); [23EC]/[23EE] = X/Y counts of stick A, FFFFh on timeout'),
 '441d': ('joy_scale', 'void(int axis /*SI*/)', 'platform', 'raw count -> byte [23F4+axis]: 0..7F below centre, 80..FF above; tracks min/max in the calibration table [23C4..23E4]'),
 '448e': ('joy_set_calib', 'void(void far *tbl)', 'platform', 'copies 20 words (40 bytes) to the calibration table DS:23C4 (comm block +48h)'),
 '44a4': ('mouse_button', 'int(int n)', 'platform', 'returns [23F8] (n=0 left) or [23F9] (right)'),
 '44b9': ('mouse_poll', 'void(void)', 'platform', 'INT 33/03: [7214]=x/2, [7216]=y, [23F8]/[23F9] = left/right pressed'),
 '44e7': ('mouse_init', 'void(void)', 'platform', 'INT 33/00, range x 0..280h y 0..C7h (07/08), ratio 8:16 (0F), position (04) from [7214],[7216]'),
 '451b': ('mouse_set_pos', 'void(void)', 'platform', 'INT 33/04 with x = [7214]*2, y = [7216]'),
 '452c': ('dist_approx', 'int(int x1, int y1, int x2, int y2)', 'platform', '|dx|,|dy| -> max + min/4'),
 '4557': ('break_hook_install', 'void(void)', 'platform', 'saves vector INT 1Bh (read from IVT 0:006C) in [2609], installs break_isr (INT 21/25)'),
 '457a': ('break_hook_remove', 'void(void)', 'platform', 'restores INT 1Bh from [2609]'),
 '458b': ('ivt_read', 'unsigned long(int off /*SI*/)', 'platform', 'reads the vector at 0:SI -> BX=off, AX=seg'),
 '4597': ('break_isr', 'void interrupt(void)', 'platform', 'INT 1Bh handler: [2608] = 1 (polled by the game loop, which then exits)'),
 '45a6': ('bios_timer', 'unsigned(void)', 'platform', 'INT 1A/00, returns the low word of the BIOS tick count'),
 '45ad': ('ovl_bind', 'void(unsigned seg)', 'platform', 'fills the 5-byte JMP FAR stubs from the driver header at seg (RE_GUIDE 2.3): stub = table + 5*(hdr[1C]+i); table is DS:23FA in END'),
 '4606': ('dos_freemem_chk', 'void(unsigned seg)', 'platform', 'INT 21/49; on failure prints a message (INT 21/09) and exits 4C00'),
 '4ade': ('mem_zero', 'void(void *dst, unsigned n)', 'platform', 'rep stosb, zero fill, near'),
 '4af5': ('mem_zero_w', 'void(void *dst, unsigned nwords)', 'platform', 'rep stosw 0'),
 '4b0c': ('mem_copy', 'void(void *src, void *dst, unsigned n)', 'platform', 'rep movsb; NOTE argument order src, dst'),
 '4b26': ('mem_copy_w', 'void(void *src, void *dst, unsigned nwords)', 'platform', 'rep movsw'),
 '4b40': ('mem_fill', 'void(void *dst, int val, unsigned n)', 'platform', 'rep stosb but AL is cleared first (xor al,al): ALWAYS fills 0, val ignored (original quirk)'),
 '4b5a': ('mem_fill_w', 'void(void *dst, int val, unsigned nwords)', 'platform', 'rep stosw with val'),
 '4b72': ('str_copy_far', 'void(char *dst, char far *src)', 'platform', 'copies up to and including the NUL; ES must equal DS (caller)'),
 '4b8c': ('timer_install', 'void(void)', 'platform', 'sets [50D8]=1,[50E2]=1,[50CE:50D0]=0, calls timer_calibrate, saves INT 8 in cs:4C51 and installs timer_isr (cs:4BEC); [50CB]=1'),
 '4bca': ('timer_remove', 'void(void)', 'platform', 'PIT ch0 back to mode 3 count 0 (18.2 Hz), restores INT 8; [50CB]=0'),
 '4cbc': ('timer_calibrate', 'void(void)', 'platform', 'measures PIT counts per vertical retrace (16 samples via pit_read), derives the retrace rate (4..6 -> 56..70 Hz class else fallback 5) and the PIT reload [50D4]/[50D2], tick divisor [50D8]'),
 '4da3': ('pit_read', 'unsigned(void)', 'platform', 'waits for a vertical retrace edge on 3DAh, latches and reads PIT channel 0 (40h/43h); 0 on timeout'),
 '4dd2': ('c0_start', 'void(void)', 'platform', 'MSC c0: DOS>=2 check, SS=DGROUP, stack size from PSP[2], INT 21/4A shrink, BSS clear (5438..7250), _cinit, main(argc,argv,envp) at 0000:0010, exit(); carries the divide-error ISR at 4E8A = _amsg_exit(3) target'),
 '4ebc': ('rtl_cinit', 'void(void)', 'platform', 'saves INT 0 vector, installs the divide handler, runs init tables (_initterm 5038/5047), parses env "_C_FILE_INFO=" into handle flags, queries INT 21/4400 for the 3 std handles'),
 '4f8a': ('exit', 'void(int code)', 'platform', 'doexit(0): atexit/term tables, flushall, _ctermsub, INT 21/4C. 4F91 = _exit (quick), 4F99 = _cexit, 4FA3 = _c_exit (same body, CL flags)'),
 '4f91': ('rtl_exit_quick', 'void(int code)', 'platform', '_exit: doexit(1) entry (alias of the exit body)'),
 '4f99': ('rtl_cexit', 'void(void)', 'platform', '_cexit: doexit(0x100): run terminators, return to caller'),
 '4fa3': ('rtl_c_exit', 'void(void)', 'platform', '_c_exit: doexit(0x101)'),
 '500b': ('rtl_ctermsub', 'void(void)', 'platform', 'restore INT 0 vector, optional cs:[5154] vector, math/overlay shutdown call [5348]'),
 '5038': ('rtl_initterm', 'void(void(**start)(void), void(**end)(void))', 'platform', 'calls near function pointers from end-1 down to start (SI/DI), skips NULL'),
 '5047': ('rtl_initterm_far', 'void(...)', 'platform', 'same for far pointer table (4 bytes per entry)'),
 '505a': ('printf', 'int(char *fmt, ...)', 'platform', 'stdout (FILE at 517A): _stbuf, _output(5678), _ftbuf'),
 '5096': ('lseek', 'long(int fd, long off, int whence)', 'platform', 'INT 21/42; sets/handles EOF flag in _osfile (end of text file)'),
 '5110': ('strcat', 'char *(char *d, char *s)', 'platform', 'MSC strcat (scasb + movs)'),
 '5150': ('strcpy', 'char *(char *d, char *s)', 'platform', 'MSC strcpy'),
 '5182': ('strlen', 'unsigned(char *s)', 'platform', 'MSC strlen'),
 '519e': ('int86', 'int(int intno, union REGS *in, union REGS *out)', 'platform', 'builds "CD nn CB" on the stack and calls it (INT 25h/26h variant keeps the flags word); out.cflag = CF; error -> _dosmaperr'),
 '521c': ('sprintf', 'int(char *buf, char *fmt, ...)', 'platform', 'string FILE (flags 42h, cnt 7FFFh) + _output + NUL'),
 '5272': ('movedata', 'void(unsigned sseg, unsigned soff, unsigned dseg, unsigned doff, unsigned n)', 'platform', 'rep movsb with ds:si -> es:di'),
 '5290': ('memcpy', 'void *(void *d, void *s, unsigned n)', 'platform', 'MSC memcpy (word copy)'),
 '52bc': ('abs', 'int(int)', 'platform', ''),
 '52ca': ('srand', 'void(unsigned seed)', 'platform', '[5158:515A] = seed'),
 '52dc': ('rand', 'int(void)', 'platform', 'seed = seed*343FDh + 269EC3h; returns (seed>>16)&7FFF'),
 '5302': ('rtl_aFldiv', 'long(long a, long b) /*pascal*/', 'platform', 'MSC long divide (C operator `/` on longs)'),
 '539c': ('rtl_aFlmul', 'long(long a, long b) /*pascal*/', 'platform', 'MSC long multiply (operator `*`)'),
 '53ce': ('rtl_aFlshr', 'long(long ax, cl)', 'platform', 'sar dx:ax, cl (operator `>>` on long)'),
 '53da': ('rtl_FF_MSGBANNER', 'void(void)', 'platform', 'writes message 252 ("\\r\\nrun-time error "), calls hook [515C], writes message 255'),
 '5400': ('rtl_nullcheck', 'int(void)', 'platform', 'xor-sums DS:0..41h and compares with 55h ("NULL assignment" check at exit); mismatch prints the banner'),
 '5422': ('rtl_NMSG_TEXT', 'char *(int msgno)', 'platform', 'looks msgno up in the table at DS:535E'),
 '544d': ('rtl_NMSG_WRITE', 'void(int msgno)', 'platform', 'INT 21/40 to handle 2'),
 '54a4': ('rtl_dosmaperr0', 'void(int doserr)', 'platform', 'AH=0 entry of _dosmaperr'),
 '54aa': ('rtl_dosmaperr', 'void(int doserr)', 'platform', 'AL = DOS error -> _doserrno [512D], errno [5122] via table 515E'),
 '54d8': ('rtl_flsbuf', 'int(int c, FILE *f)', 'platform', 'MSC _flsbuf (flag bits at FILE+6, handle at FILE+7, _osfile at 5212 index)'),
 '55b8': ('rtl_stbuf', 'int(FILE *f)', 'platform', 'temporary buffer for stdout/stderr/stdprn (buffers at 52B4/52B6/52B8)'),
 '5629': ('rtl_ftbuf', 'void(int flag, FILE *f)', 'platform', ''),
 '5678': ('rtl_output', 'int(FILE *f, char *fmt, va_list)', 'platform', 'printf engine (state machine). Internals in the same frame: 5A82 get int arg, 5A8A get long arg, 5A96 get string arg, 5AB3 putc, 5ADC write string, 5AFA write padding, 5B16 ultoa digits'),
 '5a82': ('rtl_output_getint', 'internal', 'platform', 'sub-entry of rtl_output (uses its stack frame)'),
 '5a8a': ('rtl_output_getlong', 'internal', 'platform', 'sub-entry of rtl_output'),
 '5a96': ('rtl_output_getstr', 'internal', 'platform', 'sub-entry of rtl_output'),
 '5ab3': ('rtl_output_putc', 'internal', 'platform', 'sub-entry of rtl_output'),
 '5adc': ('rtl_output_write', 'internal', 'platform', 'sub-entry of rtl_output'),
 '5afa': ('rtl_output_pad', 'internal', 'platform', 'sub-entry of rtl_output'),
 '5b16': ('rtl_output_digits', 'internal', 'platform', 'sub-entry of rtl_output (long -> digits, base in CX)'),
 '5b4e': ('rtl_chkstk', 'void(void)', 'platform', 'AX = frame size; stack overflow -> _amsg_exit(0) (4E9A)'),
 '5b66': ('rtl_getbuf', 'void(FILE *f)', 'platform', 'allocates a 200h stream buffer (malloc) or falls back to the 1-byte buffer'),
 '5ba8': ('fflush', 'int(FILE *f)', 'platform', 'f == NULL -> flushall'),
 '5c22': ('rtl_flushall', 'int(int mode)', 'platform', 'walks the FILE table (8 bytes each from 5172 to [52B2])'),
 '5c6c': ('rtl_write', 'int(int fd, void *buf, unsigned n)', 'platform', 'MSC _write: text-mode LF -> CR LF through a stack buffer (5D1D flushes it with INT 21/40), Ctrl-Z handling, appends via INT 21/4202'),
 '5d1d': ('rtl_write_flush', 'internal', 'platform', 'sub-entry of rtl_write (flush the translate buffer)'),
 '5daa': ('malloc', 'void *(unsigned n)', 'platform', 'jmp _nmalloc'),
 '5dae': ('stackavail', 'unsigned(void)', 'platform', '[531E] stack limit - SP'),
 '5dc0': ('rtl_nfree', 'void(void *p)', 'platform', 'marks the near-heap block free and lowers the rover'),
 '5de1': ('rtl_nmalloc', 'void *(unsigned n)', 'platform', 'near heap: search (5E0A), then grow (5E86), search again'),
 '5e0a': ('rtl_nh_search', 'internal', 'platform', 'first-fit walk of the heap, coalesces free neighbours (sets CF on failure)'),
 '5e86': ('rtl_nh_grow', 'internal', 'platform', 'extends the heap via 5F12 in 4 KB-rounded steps'),
 '5f12': ('rtl_nh_setblock', 'internal', 'platform', 'INT 21/4A resize of the DGROUP block'),
 '5f63': ('rtl_nh_lastfree', 'internal', 'platform', 'finds the last block before the 0xFFFE sentinel'),
 '2ba0': ('mem_alloc_chk', 'unsigned(unsigned bytes)', 'platform', 'dos_allocmem; < 10h paragraphs returned -> printf("Insufficient system memory - A...") and quit (3F44 exit path)'),
 '2bcb': ('mem_free_chk', 'void(unsigned seg)', 'platform', 'dos_freemem; non-zero -> printf("Buffer dealloc error") and quit'),
 # --- video (found while reading; owner is S3.2) ---
 '4206': ('gfx_line', 'void(int *page, int x1, int y1, int x2, int y2, int colour)', 'video', 'Bresenham line into page seg [55D0+2*page], clip x<320 y<168 (A8h), row offsets table DS:2234'),
 '42be': ('pal_set_default', 'void(void)', 'video', 'INT 10/1012: loads 256 DAC entries from DS:1F25'),
 '42d0': ('pal_set', 'void(void *pal)', 'video', 'es=ds; pal_write_vsync(ptr, 0, 256)'),
 '42e4': ('pal_write_vsync', 'void(void far *rgb, int first, int count)', 'video', 'writes DAC 3C8/3C9 during retrace (3DAh); [0:0489] bit1 set (VGA flag) uses the scaled 6-bit path'),
 '4393': ('pal_apply_queue', 'void(void)', 'video', 'sets [50CC]=0, waits for the timer ISR flag, then applies queued DAC changes (INT 10/1012, 6-byte records at DS:1EE8, count [1F24])'),
}

# VGAME address -> same, for library routines END does not contain (stdio file layer; "likely")
EXTRA = {
 'ee3c': ('rtl_fclose_core', 'int(FILE *f)', 'platform', 'flushes and closes a stream (uses fflush f7BC, _freebuf, close); likely MSC fclose body'),
 'eeea': ('rtl_fsopen', 'FILE *(char *name, char *mode, int shflag)', 'platform', 'finds a free stream (f880) and calls _openfile (f6D6)'),
 'ef12': ('fopen', 'FILE *(char *name, char *mode)', 'platform', 'rtl_fsopen(name, mode, 0)'),
 'ef26': ('fread', 'unsigned(void *buf, unsigned size, unsigned n, FILE *f)', 'platform', 'MSC fread (buffered, direct _read for large blocks)'),
 'f618': ('rtl_filbuf', 'int(FILE *f)', 'platform', 'refill a read buffer'),
 'f6ac': ('rtl_freebuf', 'void(FILE *f)', 'platform', 'frees the stream buffer'),
 'f6d6': ('rtl_openfile', 'FILE *(char *name, char *mode, int shflag, FILE *f)', 'platform', 'parses the mode string r/w/a, +, b/t and calls _open'),
 'f880': ('rtl_getstream', 'FILE *(void)', 'platform', 'first unused FILE in the table at 9288 (8 bytes each)'),
 'f94e': ('rtl_read', 'int(int fd, void *buf, unsigned n)', 'platform', 'MSC _read: INT 21/3F, text mode CR LF translation, Ctrl-Z'),
 'fb02': ('rtl_open', 'int(char *name, int oflag, int pmode)', 'platform', 'MSC _open: INT 21/3D, /3C (O_CREAT) /3E /3F /40 /42 /43 /44 (device test)'),
 'fc93': ('rtl_umask_attr', 'int(int pmode)', 'platform', '(~umask) & pmode -> DOS attribute (read-only bit)'),
 'fa98': ('unlink', 'int(char *name)', 'platform', 'INT 21/41'),
 'fae8': ('rtl_sopen', 'int(char *name, int oflag, int shflag, int pmode)', 'platform', 'sharing-flag entry that jumps into rtl_open (fb0A)'),
 'f8b4': ('rtl_close', 'int(int fd)', 'platform', 'INT 21/3E, error -> _dosmaperr'),
 'f0ba': ('dos_kbhit', 'int(void)', 'platform', 'INT 21/0B'),
 'f0e0': ('rtl_dosret_ah', 'internal', 'platform', 'INT 21 helper: DH=8 function in AH of [926C]; used by getch-style input'),
 'f348': ('dos_allocmem', 'unsigned(unsigned bytes)', 'platform', 'INT 21/48 (PLAYER/MPS_LOGO share it)'),
 'f360': ('bios_keyboard', 'int(int cmd)', 'platform', 'INT 16h (MPS_LOGO/PLAYER copy of MISC slot 90/91)'),
 'f384': ('dos_freemem', 'int(unsigned seg)', 'platform', 'INT 21/49'),
 'f45e': ('rtl_aFlshl', 'long(long ax, cl)', 'platform', 'shl dx:ax, cl (operator `<<` on long)'),
 'f50e': ('rtl_aFulshr', 'long(long ax, cl)', 'platform', 'shr dx:ax, cl (unsigned long `>>`)'),
 'f476': ('rtl_lshl_mem', 'void(long *p, int n)', 'platform', '*p <<= n'),
 'f094': ('rtl_ltoa_head', 'internal', 'platform', 'long/ulong -> string helper head (radix 10 test); part of itoa/ltoa'),
}


# 'exe:offset' -> same, for routines found only in programs other than END/VGAME
OTHER = {
 'start:804e': ('fcb_find', 'int(void)', 'platform', 'INT 21/1A set DTA (DS:61B3), INT 21/11 FCB search (FCB DS:6187), then compares 0-terminated name [617E] with [61BB]; returns 0 when equal (disk/file presence test). Also in SETUP 0C84'),
 'setup:19b6': ('rtl_setargv', 'void(void)', 'platform', 'MSC _setargv + _setenvp: DOS version check (INT 21/30), scans the environment block (PSP:2C) and splits the command tail (PSP:81) into argc/argv. END has only empty stubs (END 00C7/00C8, called from c0_start before _cinit)'),
 'setup:1b44': ('rtl_setenvp', 'void(void)', 'platform', 'likely: builds envp / handles "_C_FILE_INFO=" (second routine carrying that string)'),
 'setup:1c22': ('malloc', 'void *(unsigned n)', 'platform', 'sets _amblksiz = 400h around _nmalloc, falls back to the failure path'),
 'player:18c4': ('fprintf', 'int(FILE *f, char *fmt, ...)', 'platform', '_stbuf + _output + _ftbuf on the given stream (also MPS_LOGO 0B94)'),
}


def short(a):
    return a.split(':')[1].lower() if a else ''


def main():
    rows = list(csv.DictReader(open(os.path.join(ROOT, 'port', 'shared_matches.csv'))))
    out, seen = [], set()

    def emit(exe, addr, name, typ, owner, notes, other=''):
        key = (exe, addr)
        if key in seen:
            return
        seen.add(key)
        out.append({'exe': exe, 'kind': 'func', 'address': addr, 'name': name, 'type': typ,
                    'owner': owner, 'other_names': other, 'notes': notes})

    for r in rows:
        info = END.get(short(r['end'])) or EXTRA.get(short(r['vgame']))
        if info is None:
            for exe in EXES:
                if r[exe] and '%s:%s' % (exe, short(r[exe])) in OTHER:
                    info = OTHER['%s:%s' % (exe, short(r[exe]))]
        if info is None:
            continue
        name, typ, owner, notes = info
        for exe in EXES:
            if r[exe]:
                emit(exe, r[exe], name, typ, owner, notes if exe in ('end', 'vgame') else '', r['name'])
    for a, (name, typ, owner, notes) in END.items():
        emit('end', '0000:' + a, name, typ, owner, notes)
    for a, (name, typ, owner, notes) in EXTRA.items():
        emit('vgame', '0000:' + a, name, typ, owner, notes)
    out.sort(key=lambda x: (EXES.index(x['exe']), x['address']))
    path = os.path.join(ROOT, 'port', 'spec', 'platform_symbols.csv')
    with open(path, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['exe', 'kind', 'address', 'name', 'type', 'owner', 'other_names', 'notes'])
        w.writeheader()
        w.writerows(out)
    by = {}
    for x in out:
        by[x['exe']] = by.get(x['exe'], 0) + 1
    print('rows', len(out), by)

    os.makedirs(os.path.join(ROOT, 'work', 's31'), exist_ok=True)
    with open(os.path.join(ROOT, 'work', 's31', 'unassigned.txt'), 'w') as f:
        n = 0
        for r in rows:
            if short(r['end']) in END or short(r['vgame']) in EXTRA or any(
                    r[e] and '%s:%s' % (e, short(r[e])) in OTHER for e in EXES):
                continue
            n += 1
            f.write(' '.join('%s=%s' % (e, r[e]) for e in EXES if r[e]) + ' insns=%s %s\n' % (r['insns'], r['hint']))
        print('shared rows not named here:', n)


if __name__ == '__main__':
    main()
