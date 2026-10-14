/* The DOS layer (platform.md §3.1 "MicroProse wrappers", §3.2 stdio/low-level I/O, simflow.md 2 file loader):
 * INT 21h file and memory calls over the game folder. The wrappers never return an error: they print the
 * original's message and end the program (here host_fatal, platform.md §3.1 "Port"). File names are DOS names
 * relative to the game folder and are found case-insensitively (host_game_path). Handles are small integers
 * from 5 up (0..4 are the standard devices). */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "gen/module_platform.h"
#include "host.h"
#include "platform/platform.h"

#define MAX_HANDLES 40
#define FIRST_HANDLE 5

static struct { FILE *f; bool text; } handles[MAX_HANDLES];

/* DOS names use backslashes and any case; the host wants '/'. */
static char *dos_path(const char *name, bool create)
{
    char tmp[300];
    size_t i = 0;
    for (; name[i] && i < sizeof tmp - 1; i++) tmp[i] = name[i] == '\\' ? '/' : name[i];
    tmp[i] = 0;
    return host_game_path(tmp, create);
}

/* The fatal tail of the wrappers (END 41CA, DS:1E77..1ECD): the message text, then the program ends. */
static _Noreturn void dos_fatal(const char *name, const char *msg, int doserr)
{
    if (name) host_fatal("%s%s", name, msg);
    if (doserr >= 0) host_fatal("%s%d", msg, doserr);
    host_fatal("%s", msg);
}

static int handle_new(FILE *f, bool text)
{
    for (int i = 0; i < MAX_HANDLES; i++)
        if (!handles[i].f) { handles[i].f = f; handles[i].text = text; return i + FIRST_HANDLE; }
    return -1;
}

static FILE *handle_get(int fd)
{
    int i = fd - FIRST_HANDLE;
    return i >= 0 && i < MAX_HANDLES ? handles[i].f : NULL;
}

void dos_close_all(void)
{
    for (int i = 0; i < MAX_HANDLES; i++)
        if (handles[i].f) { fclose(handles[i].f); handles[i].f = NULL; }
}

/* The 512-byte decoder buffer's read position word sits just before the handle word; open/creat reset it to
 * "empty" (200h). */
static void dosbuf_reset(void)
{
    const ProgDs *d = plat_ds();
    if (cur_dgroup && d->dosbuf_fd) DSW(d->dosbuf_fd - 2) = 0x200;
}

/* INT 21h/3Dh access mode 0 = read, 1 = write, 2 = read/write; a read-only file opened for writing falls back to read-only
 * (PORT: DOS would return error 5, which the wrapper turns into a fatal message). */
static FILE *open_host(const char *name, int mode, bool create)
{
    char *path = dos_path(name, create);
    if (!path) return NULL;
    FILE *f = NULL;
    if (create) f = fopen(path, "w+b");
    else if ((mode & 3) == 0) f = fopen(path, "rb");
    else {
        f = fopen(path, "r+b");
        if (!f) f = fopen(path, "rb");
    }
    host_free(path);
    return f;
}

/* END 0000:400F / START 8075 / SETUP 0DC7 / DSWAP 0599 / VGAME 1199:000C: INT 21h/3Dh; mode bits 0-1 = access */
int dos_open(char *name, int mode)
{
    FILE *f = open_host(name, mode, false);
    if (!f) dos_fatal(name, ":File not found", -1);               /* DOS errors 2, 3 */
    int fd = handle_new(f, false);
    if (fd < 0) { fclose(f); dos_fatal(name, ":No file buffers available", -1); }   /* error 4 */
    dosbuf_reset();
    return fd;
}

/* END 0000:4066 / START 80CC / SETUP 0E1E / DSWAP 05F0 / VGAME 1199:006A: INT 21h/3Ch (create or truncate) */
int dos_creat(char *name, int attr)
{
    (void)attr;
    FILE *f = open_host(name, 2, true);
    if (!f) dos_fatal(name, ":Open error 5", -1);
    int fd = handle_new(f, false);
    if (fd < 0) { fclose(f); dos_fatal(name, ":No file buffers available", -1); }
    dosbuf_reset();
    return fd;
}

/* END 0000:40BD / START 8123 / SETUP 0E75 / DSWAP 0647 */
void dos_close(int fd)
{
    FILE *f = handle_get(fd);
    if (!f) dos_fatal(NULL, "File closing error", 6);
    fclose(f);
    handles[fd - FIRST_HANDLE].f = NULL;
}

static int read_into(int fd, unsigned n, u8 *dst)
{
    FILE *f = handle_get(fd);
    if (!f) dos_fatal(NULL, "Read error", 6);                      /* invalid handle */
    return (int)fread(dst, 1, (u16)n, f);
}

/* END 0000:40DE / START 8144 / SETUP 0E97 / DSWAP 0668: INT 21h/3Fh into DS:buf; returns the bytes read */
int dos_read(int fd, unsigned n, void *buf) { return read_into(fd, n, buf); }

/* END 0000:4109 / START 816F / SETUP 0EC2 / DSWAP 0693 */
int dos_read_far(int fd, unsigned n, unsigned off, unsigned seg) { return read_into(fd, n, mp((u16)seg, (u16)off)); }

/* END 0000:4199 / START 81FF / SETUP 0F52 / DSWAP 0723: INT 21h/40h from seg:(off + add) */
int dos_write_far(int fd, unsigned n, unsigned off, unsigned seg, unsigned add)
{
    FILE *f = handle_get(fd);
    if (!f) dos_fatal(NULL, "Write error", 6);
    u8 *src = mp((u16)seg, (u16)(off + add));
    size_t w = fwrite(src, 1, (u16)n, f);
    fflush(f);
    return (int)w;
}

/* END 0000:4160 / START 81C6 / SETUP 0F19 / DSWAP 06EA / VGAME 1199:0157: refill the 512-byte decoder buffer from
 * the handle word (the PIC/PAN LZW reader, video.md) */
void dos_buf_fill(void)
{
    const ProgDs *d = plat_ds();
    if (!cur_dgroup || !d->dosbuf) host_fatal("dos_buf_fill: this program has no decoder buffer");
    read_into(DSW(d->dosbuf_fd), 0x200, DSP(d->dosbuf));
}

/* END 0000:4181 / START 81E7 / SETUP 0F3A / DSWAP 070B: [fd word] = fd, then dos_buf_fill */
void dos_buf_setfd(int fd)
{
    const ProgDs *d = plat_ds();
    if (!cur_dgroup || !d->dosbuf) host_fatal("dos_buf_setfd: this program has no decoder buffer");
    DSW(d->dosbuf_fd) = (u16)fd;
    dos_buf_fill();
}

/* ------------------------------------------------------------------ DOS memory (INT 21h/48h, 49h) */

/* END 0000:3FA4 / START 800A / DSWAP 04E0 / PLAYER 14A0 / MPS_LOGO 070C / VGAME F348 (bytes flavour): the block
 * size is rounded UP to paragraphs; returns the segment, or the DOS error 8 (insufficient memory) which the
 * checked wrapper below tells from a segment by "< 10h". */
unsigned dos_allocmem(unsigned bytes)
{
    u16 paras = (u16)(((u16)bytes >> 4) + (((u16)bytes & 0xF) ? 1 : 0));
    u16 seg;
    if (!dos_mem_alloc(paras, &seg)) return 8;
    return seg;
}

/* VGAME F348 / PLAYER / MPS_LOGO: dos_allocmem(paras, &seg) */
int dos_allocmem_paras(unsigned paras, u16 *out)
{
    u16 seg;
    bool ok = dos_mem_alloc((u16)paras, &seg);
    *out = seg;
    return ok ? 0 : 8;
}

/* END 0000:3FCA / START 8030 / DSWAP 0506 / MPS_LOGO 0748 / VGAME F384: 0, or DOS error 9 */
int dos_freemem(unsigned seg) { return dos_mem_free((u16)seg) ? 0 : 9; }

/* END 0000:2BA0 / START 40A4 / DSWAP 0494 */
unsigned mem_alloc_chk(unsigned bytes)
{
    unsigned r = dos_allocmem(bytes);
    if (r < 0x10) dos_fatal(NULL, "Insufficient system memory - AllocBuffer", -1);
    return r;
}

/* END 0000:2BCB / START 40CF / DSWAP 04BF */
void mem_free_chk(unsigned seg)
{
    if (dos_freemem(seg)) dos_fatal(NULL, "Buffer dealloc error", -1);
}

/* END 0000:4606 / START 8667 / DSWAP 08B3 / PLAYER 0FC5 */
void dos_freemem_chk(unsigned seg)
{
    if (dos_freemem(seg)) dos_fatal(NULL, "Error releasing overlay memory", -1);
}

/* ovl_bind: the drivers are static function tables in the port (platform.md §6) */
void ovl_bind(unsigned seg) { (void)seg; }

/* ------------------------------------------------------------------ whole-file loader */

/* simflow.md 2, VGAME 0000:EA7C file_load_paras: size, paragraphs, INT 21h/48h, then the read loop in chunks of
 * 0FFFFh bytes. ORIGINAL BUG kept: the offset starts at 0 and DEcreases by one per chunk (seg:0, seg:FFFF,
 * seg:FFFE ...) instead of the segment advancing, so a file above 2 * 65535 bytes would be corrupted (no
 * shipped file is). In the port the offset arithmetic wraps inside a 64 KB window of mem[] like the 16-bit
 * offset does. */
u16 platform_load_file_paras(const char *name, u32 *size)
{
    if (size) *size = 0;
    char *path = dos_path(name, false);
    if (!path) return 0;
    FILE *f = fopen(path, "rb");                      /* PORT: the original opens "r+b" (read/write) */
    host_free(path);
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return 0; }
    u32 need = ((u32)sz + 15) >> 4;
    u16 seg;
    if (need > 0xFFFF || !dos_mem_alloc((u16)need, &seg)) { fclose(f); return 0; }
    if (size) *size = (u32)sz;
    u32 left = (u32)sz;
    u16 off = 0;
    while (left > 0xFFFF) {
        if (fread(mp(seg, off), 1, 0xFFFF, f) != 0xFFFF) break;
        left -= 0xFFFF;
        off = (u16)(off - 1);
    }
    if (left <= 0xFFFF) fread(mp(seg, off), 1, left, f);
    fclose(f);
    return seg;
}

/* ------------------------------------------------------------------ stdio / low-level I/O */

/* DSWAP 0000:0F2E / START 923A / VGAME EF12: fopen on a game file. "r+" on a read-only file falls back to read-only
 * (PORT: the original fails, simflow.md 2). */
FILE *fopen_fn(char *name, char *mode)
{
    bool create = strchr(mode, 'w') || strchr(mode, 'a');
    char *path = dos_path(name, create);
    if (!path) return NULL;
    FILE *f = fopen(path, mode);
    if (!f && strchr(mode, '+') && mode[0] == 'r') f = fopen(path, "rb");
    host_free(path);
    return f;
}

/* MPS_LOGO 0146:03B6 / START 924E / VGAME EF26 */
unsigned fread_fn(void *buf, unsigned size, unsigned n, FILE *f)
{
    return (unsigned)fread(buf, size, n, f);
}

/* DSWAP 0000:1CA2 / END 5BA8 / VGAME F7BC: NULL = flush all streams */
int fflush_fn(FILE *f) { return fflush(f); }

/* END 0000:5096 / START A520 / VGAME F8D4 / DSWAP 0F7E: INT 21h/42h */
long lseek_fn(int fd, long off, int whence)
{
    FILE *f = handle_get(fd);
    if (!f) return -1;
    int w = whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END;
    if (fseek(f, off, w) != 0) return -1;
    return ftell(f);
}

/* MPS_LOGO 0146:17A2 / START A4B8 / VGAME FA98: INT 21h/41h */
int unlink_fn(char *name)
{
    char *path = dos_path(name, false);
    if (!path) return -1;
    int r = remove(path);
    host_free(path);
    return r == 0 ? 0 : -1;
}

/* O_ flags of the MSC _open (rtl_open): access 0-2, O_APPEND 8, O_CREAT 100h, O_TRUNC 200h, O_EXCL 400h, O_TEXT
 * 4000h, O_BINARY 8000h. */
int rtl_open(char *name, int oflag, int pmode)
{
    (void)pmode;
    bool create = (oflag & 0x100) != 0;
    char *path = dos_path(name, create);
    if (!path) { errno = ENOENT; return -1; }
    bool exists = true;
    {
        FILE *t = fopen(path, "rb");
        if (t) fclose(t); else exists = false;
    }
    FILE *f = NULL;
    if (exists && (oflag & 0x400) && create) { host_free(path); errno = EEXIST; return -1; }
    if (!exists && !create) { host_free(path); errno = ENOENT; return -1; }
    if (!exists || (oflag & 0x200)) f = fopen(path, "w+b");
    else f = fopen(path, (oflag & 3) == 0 ? "rb" : "r+b");
    host_free(path);
    if (!f) return -1;
    if (oflag & 8) fseek(f, 0, SEEK_END);
    int fd = handle_new(f, (oflag & 0x4000) != 0);
    if (fd < 0) { fclose(f); errno = EMFILE; return -1; }
    return fd;
}

int rtl_sopen(char *name, int oflag, int shflag, int pmode)
{
    (void)shflag;
    return rtl_open(name, oflag, pmode);
}

int rtl_close(int fd)
{
    FILE *f = handle_get(fd);
    if (!f) { errno = EBADF; return -1; }
    fclose(f);
    handles[fd - FIRST_HANDLE].f = NULL;
    return 0;
}

/* VGAME F94E: text mode drops the CR of a CR LF pair and ends at Ctrl-Z */
int rtl_read(int fd, void *buf, unsigned n)
{
    FILE *f = handle_get(fd);
    if (!f) { errno = EBADF; return -1; }
    u8 *b = buf;
    size_t got = fread(b, 1, (u16)n, f);
    if (!handles[fd - FIRST_HANDLE].text) return (int)got;
    size_t o = 0;
    for (size_t i = 0; i < got; i++) {
        if (b[i] == 0x1A) { fseek(f, (long)(i + 1) - (long)got, SEEK_CUR); break; }
        if (b[i] == '\r' && i + 1 < got && b[i + 1] == '\n') continue;
        b[o++] = b[i];
    }
    return (int)o;
}

/* VGAME FCA4: text mode turns LF into CR LF */
int rtl_write(int fd, void *buf, unsigned n)
{
    FILE *f = handle_get(fd);
    if (!f) { errno = EBADF; return -1; }
    const u8 *b = buf;
    if (!handles[fd - FIRST_HANDLE].text) return (int)fwrite(b, 1, (u16)n, f);
    for (u16 i = 0; i < (u16)n; i++) {
        if (b[i] == '\n') fputc('\r', f);
        fputc(b[i], f);
    }
    return (int)(u16)n;
}

/* VGAME F0BA: INT 21h/0Bh, FFh when a key is waiting */
int dos_kbhit(void) { return kbd_key_waiting() ? 0xFF : 0; }

/* VGAME F5EA / END 54A4: DOS error to errno */
void rtl_dosmaperr(int doserr)
{
    switch (doserr & 0xFF) {
    case 2: case 3: errno = ENOENT; break;
    case 4: errno = EMFILE; break;
    case 5: errno = EACCES; break;
    case 6: errno = EBADF; break;
    case 8: errno = ENOMEM; break;
    default: errno = EINVAL; break;
    }
}
void rtl_dosmaperr0(int doserr) { rtl_dosmaperr(doserr); }
