"""S5.3: a small real-mode DOS/BIOS machine under Unicorn that runs the ORIGINAL front-end programs (work/unpacked/<NAME>.EXE, from
tools/unlzexe.py; the game files themselves are read from Game/ and never committed) so the port can be compared with them.

Emulated: the MZ loader with relocations, a PSP, the memory arena (INT 21h 48h/49h/4Ah), files (3Dh 3Eh 3Fh 42h 44h), console (01h 02h
09h 40h), vectors (25h 35h), version (30h), FCB find (11h: not found), overlay load (4B03h), exit (4Ch); INT 10h text services (00h 02h
09h 0Eh 0Fh) on an 80 x 25 buffer, INT 16h key services from a script, and the VGA DAC ports. A hook at the entry of a chosen function
can bump the BIOS tick (so the logo does not wait).
"""
import os, struct, sys, zlib
from unicorn import *
from unicorn.x86_const import *

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
PSP = 0x0800
A000 = 0xA000
MEMTOP = 0xA000


class Exit(Exception):
    pass


class Machine:
    def __init__(self, name, game=None, tail=b'', keys=(), on_getche=None):
        self.game = game or os.path.join(ROOT, 'Game')
        self.files = {f.upper(): os.path.join(self.game, f) for f in os.listdir(self.game)}
        d = open(os.path.join(ROOT, 'work', 'unpacked', name + '.EXE'), 'rb').read()
        h = struct.unpack_from('<14H', d, 0)
        hdr = h[4] * 16
        size = ((h[2] - 1) * 512 + (h[1] or 512)) - hdr
        img = bytearray(d[hdr:hdr + size])
        self.load = PSP + 0x10
        for i in range(h[3]):
            off, seg = struct.unpack_from('<HH', d, h[12] + 4 * i)
            p = seg * 16 + off
            struct.pack_into('<H', img, p, (struct.unpack_from('<H', img, p)[0] + self.load) & 0xFFFF)
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x110000)
        uc.mem_write(self.load * 16, bytes(img))
        uc.mem_write(PSP * 16, b'\xcd\x20' + struct.pack('<H', MEMTOP))
        uc.mem_write(PSP * 16 + 0x80, bytes([len(tail)]) + tail + b'\r')
        for r, v in ((UC_X86_REG_CS, self.load + h[11]), (UC_X86_REG_IP, h[10]), (UC_X86_REG_SS, self.load + h[7]),
                     (UC_X86_REG_SP, h[8]), (UC_X86_REG_DS, PSP), (UC_X86_REG_ES, PSP), (UC_X86_REG_EFLAGS, 0x202)):
            uc.reg_write(r, v)
        self.entry = (self.load + h[11]) * 16 + h[10]
        self.block_end = MEMTOP                      # the program's block (shrunk by 4Ah)
        self.next_free = MEMTOP
        self.blocks = {}
        self.handles = {}
        self.next_handle = 5
        self.vec = {}
        self.exit_code = None
        self.out = bytearray()
        # text screen
        self.text = bytearray(b' \x07' * 2000)
        self.row = self.col = 0
        self.mode = 3
        self.keys = list(keys)                       # bytes returned by INT 21h/01 in turn
        self.on_getche = on_getche                   # called with (machine, n) before the n-th getche
        self.getche_n = 0
        self.snaps = []
        # DAC
        self.dac = bytearray(768)
        self.dac_w = 0
        self.dac_wsub = 0
        self.dac_r = 0
        self.dac_rsub = 0
        self.int_log = []
        uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        self.code_hooks = {}

    # ------------------------------------------------------------------ memory helpers
    def rb(self, seg, off, n):
        return bytes(self.uc.mem_read(((seg << 4) + off) & 0xFFFFF, n))

    def wb(self, seg, off, data):
        self.uc.mem_write(((seg << 4) + off) & 0xFFFFF, bytes(data))

    def rw(self, seg, off):
        return struct.unpack('<H', self.rb(seg, off, 2))[0]

    def ww(self, seg, off, v):
        self.wb(seg, off, struct.pack('<H', v & 0xFFFF))

    def asciiz(self, seg, off):
        out = bytearray()
        while True:
            b = self.rb(seg, off + len(out), 1)[0]
            if b == 0:
                return bytes(out)
            out.append(b)

    def reg(self, r):
        return self.uc.reg_read(r)

    def setreg(self, r, v):
        self.uc.reg_write(r, v)

    # ------------------------------------------------------------------ text mode (INT 10h)
    def set_mode(self, mode):
        self.mode = mode
        if mode == 3:
            self.text[:] = b' \x07' * 2000
            self.row = self.col = 0
        elif mode == 0x13:
            self.wb(A000, 0, bytes(65536))
            self.dac[:] = bytes(768)

    def tty(self, ch):
        if ch == 7:
            return
        if ch == 8:
            if self.col > 0:
                self.col -= 1
            return
        if ch == 0x0D:
            self.col = 0
            return
        if ch != 0x0A:
            if 0 <= self.row < 25 and 0 <= self.col < 80:
                self.text[2 * (self.row * 80 + self.col)] = ch
            self.col += 1
            if self.col < 80:
                return
            self.col = 0
        self.row += 1
        if self.row >= 25:
            self.text[:3840] = self.text[160:]
            self.text[3840:] = b' \x07' * 80
            self.row = 24

    def text_rows(self):
        return [bytes(self.text[160 * r:160 * r + 160:2]).decode('cp437') for r in range(25)]

    # ------------------------------------------------------------------ ports
    def _out(self, uc, port, size, value, _):
        value &= 0xFF
        if port == 0x3C8:
            self.dac_w, self.dac_wsub = value, 0
        elif port == 0x3C9:
            self.dac[self.dac_w * 3 + self.dac_wsub] = value & 0x3F
            self.dac_wsub += 1
            if self.dac_wsub == 3:
                self.dac_wsub = 0
                self.dac_w = (self.dac_w + 1) & 0xFF
        elif port == 0x3C7:
            self.dac_r, self.dac_rsub = value, 0

    def _in(self, uc, port, size, _):
        if port == 0x3C9:
            v = self.dac[self.dac_r * 3 + self.dac_rsub]
            self.dac_rsub += 1
            if self.dac_rsub == 3:
                self.dac_rsub = 0
                self.dac_r = (self.dac_r + 1) & 0xFF
            return v
        if port == 0x3DA:
            return 0x08
        return 0xFF if size == 1 else 0xFFFF

    # ------------------------------------------------------------------ interrupts
    def _flags(self, cf=None, zf=None):
        f = self.reg(UC_X86_REG_EFLAGS)
        if cf is not None:
            f = (f | 1) if cf else (f & ~1)
        if zf is not None:
            f = (f | 0x40) if zf else (f & ~0x40)
        self.setreg(UC_X86_REG_EFLAGS, f)

    def _intr(self, uc, n, _):
        ax = self.reg(UC_X86_REG_AX)
        ah, al = ax >> 8, ax & 0xFF
        bx, cx, dx = self.reg(UC_X86_REG_BX), self.reg(UC_X86_REG_CX), self.reg(UC_X86_REG_DX)
        ds, es = self.reg(UC_X86_REG_DS), self.reg(UC_X86_REG_ES)
        if n == 0x10:
            if ah == 0:
                self.set_mode(al & 0x7F)
            elif ah == 2:
                self.row, self.col = dx >> 8, dx & 0xFF
            elif ah == 9:
                for _i in range(cx):
                    c = self.col + _i
                    if 0 <= self.row < 25 and c < 80:
                        self.text[2 * (self.row * 80 + c)] = al
                        self.text[2 * (self.row * 80 + c) + 1] = bx & 0xFF
            elif ah == 0x0E:
                self.tty(al)
            elif ah == 0x0F:
                self.setreg(UC_X86_REG_AX, 0x5000 | self.mode)
                self.setreg(UC_X86_REG_BX, bx & 0x00FF)
            return
        if n == 0x16:
            if ah == 1:
                if self.keys:
                    self.setreg(UC_X86_REG_AX, (self.keys[0] & 0xFF) | 0x1E00)
                    self._flags(zf=False)
                else:
                    self._flags(zf=True)
            elif ah == 0:
                k = self.keys.pop(0) if self.keys else 0x011B
                self.setreg(UC_X86_REG_AX, k if k > 0xFF else (k | 0x1E00))
            return
        if n == 0x1A:
            return
        if n != 0x21:
            self.int_log.append(('int', n, ah))
            return
        if ah == 0x30:
            self.setreg(UC_X86_REG_AX, 0x0005)
        elif ah == 0x4A:
            self.block_end = es + bx
            self.next_free = max(self.next_free, 0) if self.next_free > self.block_end else self.block_end
            self.next_free = self.block_end
            self._flags(cf=False)
        elif ah == 0x48:
            avail = MEMTOP - self.next_free
            if bx > avail:
                self.setreg(UC_X86_REG_BX, avail)
                self.setreg(UC_X86_REG_AX, 8)
                self._flags(cf=True)
            else:
                seg = self.next_free
                self.next_free += bx
                self.blocks[seg] = bx
                self.setreg(UC_X86_REG_AX, seg)
                self._flags(cf=False)
        elif ah == 0x49:
            self.blocks.pop(es, None)
            self._flags(cf=False)
        elif ah == 0x25:
            self.vec[al] = (ds, dx)
        elif ah == 0x35:
            v = self.vec.get(al, (0, 0))
            self.setreg(UC_X86_REG_ES, v[0])
            self.setreg(UC_X86_REG_BX, v[1])
        elif ah == 0x09:
            s = bytearray()
            off = dx
            while True:
                c = self.rb(ds, off, 1)[0]
                if c == 0x24:
                    break
                s.append(c)
                off += 1
            for c in s:
                self.tty(c)
        elif ah == 0x01:
            self.getche_n += 1
            if self.on_getche:
                self.on_getche(self, self.getche_n)
            k = self.keys.pop(0) if self.keys else 0x1B
            self.tty(k)
            self.setreg(UC_X86_REG_AX, k & 0xFF)
        elif ah == 0x02:
            self.tty(dx & 0xFF)
        elif ah == 0x1A:
            pass
        elif ah == 0x11:
            self.setreg(UC_X86_REG_AX, 0x00FF)            # FCB find first: not found (no floppy)
        elif ah == 0x3D:
            name = self.asciiz(ds, dx).decode('latin-1').upper().replace('\\', '/').split('/')[-1]
            path = self.files.get(name)
            if not path:
                self.setreg(UC_X86_REG_AX, 2)
                self._flags(cf=True)
            else:
                h = self.next_handle
                self.next_handle += 1
                self.handles[h] = [open(path, 'rb'), path]
                self.setreg(UC_X86_REG_AX, h)
                self._flags(cf=False)
        elif ah == 0x3E:
            f = self.handles.pop(bx, None)
            if f:
                f[0].close()
            self._flags(cf=False)
        elif ah == 0x3F:
            f = self.handles.get(bx)
            data = f[0].read(cx) if f else b''
            self.wb(ds, dx, data)
            self.setreg(UC_X86_REG_AX, len(data))
            self._flags(cf=False)
        elif ah == 0x42:
            f = self.handles.get(bx)
            off = (cx << 16) | dx
            if f:
                f[0].seek(off, al)
                pos = f[0].tell()
            else:
                pos = 0
            self.setreg(UC_X86_REG_AX, pos & 0xFFFF)
            self.setreg(UC_X86_REG_DX, pos >> 16)
            self._flags(cf=False)
        elif ah == 0x44:
            self.setreg(UC_X86_REG_DX, 0)
            self._flags(cf=False)
        elif ah == 0x40:
            self.out += self.rb(ds, dx, cx)
            self.setreg(UC_X86_REG_AX, cx)
            self._flags(cf=False)
        elif ah == 0x4B and al == 3:                       # load overlay: DS:DX name, ES:BX = (segment, relocation factor)
            name = self.asciiz(ds, dx).decode('latin-1').upper()
            path = self.files.get(name)
            seg, factor = self.rw(es, bx), self.rw(es, bx + 2)
            d = open(path, 'rb').read()
            h = struct.unpack_from('<14H', d, 0)
            hdr = h[4] * 16
            size = ((h[2] - 1) * 512 + (h[1] or 512)) - hdr
            img = bytearray(d[hdr:hdr + size])
            for i in range(h[3]):
                off, rseg = struct.unpack_from('<HH', d, h[12] + 4 * i)
                p = rseg * 16 + off
                struct.pack_into('<H', img, p, (struct.unpack_from('<H', img, p)[0] + factor) & 0xFFFF)
            self.wb(seg, 0, img)
            self._flags(cf=False)
        elif ah == 0x4C:
            self.exit_code = al
            self.uc.emu_stop()
        else:
            self.int_log.append(('21', ah, al))

    # ------------------------------------------------------------------ running
    def run(self, count=200000000):
        self.uc.emu_start(self.entry, 0xFFFFF, count=count)
        return self.exit_code

    def hook_call(self, seg, off, fn):
        """Calls fn(machine) every time the instruction at seg:off is about to run."""
        a = ((self.load + seg) << 4) + off
        self.uc.hook_add(UC_HOOK_CODE, lambda uc, addr, size, _: fn(self), None, a, a)
