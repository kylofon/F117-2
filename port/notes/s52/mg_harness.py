"""S5.2: Unicorn harness for the ORIGINAL MGRAPHIC.EXE driver (Game/MGRAPHIC.EXE; the game files are never committed).

The driver image is loaded at segment IMG with its MZ relocations applied; the slots are called through the entry table in the
driver header (RE_GUIDE 2.2/2.3) with the C or register conventions of port/spec/video.md.
Layout of the emulated 1 MB: image at IMG:0 (code segment IMG+19Ch), SS = SEG_SS (structures, strings, edge tables,
the stack at the top), off-screen pages P1..P3, the DOS memory the sprite pool takes (INT 21h/48h/49h/4Ah are emulated).
"""
import os, struct, sys, zlib
from unicorn import *
from unicorn.x86_const import *

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
IMG = 0x1000
SEG_SS = 0x2000
PAGE_SEG = {1: 0x3000, 2: 0x4000, 3: 0x5000}
POOL_SEG, POOL_PARAS = 0x6000, 0x2000
RET_SEG, RET_OFF = 0x0F00, 0x0000          # the far return address of every call: F00:0000 = F000 linear
A000 = 0xA000


class Driver:
    def __init__(self, path=None):
        path = path or os.path.join(ROOT, 'Game', 'MGRAPHIC.EXE')
        d = open(path, 'rb').read()
        hdr = struct.unpack_from('<H', d, 8)[0] * 16
        nrel, relo = struct.unpack_from('<H', d, 6)[0], struct.unpack_from('<H', d, 0x18)[0]
        img = bytearray(d[hdr:])
        for i in range(nrel):
            off, seg = struct.unpack_from('<HH', d, relo + 4 * i)
            p = seg * 16 + off
            struct.pack_into('<H', img, p, (struct.unpack_from('<H', img, p)[0] + IMG) & 0xFFFF)
        self.img = bytes(img)
        self.cs = struct.unpack_from('<H', img, 0x18)[0]
        self.nent = struct.unpack_from('<H', img, 0x22)[0]
        self.entry = struct.unpack_from('<%dH' % self.nent, img, 0x24)
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x110000)
        uc.mem_write(IMG * 16, bytes(img))
        self.out_log = []
        self.int21_log = []
        self.pool_alloc = None            # (seg, paras) once allocated
        self.mem_blocks = {}
        uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        self.in_values = {}

    # ---- DOS / port hooks
    def _intr(self, uc, n, _):
        if n != 0x21:
            return
        ah = (uc.reg_read(UC_X86_REG_AX) >> 8) & 0xFF
        bx = uc.reg_read(UC_X86_REG_BX)
        flags = uc.reg_read(UC_X86_REG_EFLAGS)
        if ah == 0x48:
            if bx == 0xFFFF:
                uc.reg_write(UC_X86_REG_BX, POOL_PARAS if self.pool_alloc is None else 0)
                uc.reg_write(UC_X86_REG_AX, 8)
                flags |= 1
            elif self.pool_alloc is None and bx <= POOL_PARAS:
                self.pool_alloc = (POOL_SEG, bx)
                uc.reg_write(UC_X86_REG_AX, POOL_SEG)
                flags &= ~1
            else:
                uc.reg_write(UC_X86_REG_AX, 8)
                flags |= 1
        elif ah == 0x4A:
            es = uc.reg_read(UC_X86_REG_ES)
            self.int21_log.append(('resize', es, bx))
            if self.pool_alloc and es == self.pool_alloc[0]:
                self.pool_alloc = (es, bx)
            flags &= ~1
        elif ah == 0x49:
            es = uc.reg_read(UC_X86_REG_ES)
            self.int21_log.append(('free', es))
            self.pool_alloc = None
            flags &= ~1
        uc.reg_write(UC_X86_REG_EFLAGS, flags)

    def _out(self, uc, port, size, value, _):
        self.out_log.append((port, size, value))

    def _in(self, uc, port, size, _):
        return self.in_values.get(port, 0xFF if size == 1 else 0xFFFF) & (0xFF if size == 1 else 0xFFFF)

    # ---- memory helpers
    def w(self, seg, off, data):
        self.uc.mem_write(seg * 16 + off, bytes(data))

    def r(self, seg, off, n):
        return bytes(self.uc.mem_read(seg * 16 + off, n))

    def w16(self, seg, off, v):
        self.w(seg, off, struct.pack('<H', v & 0xFFFF))

    def r16(self, seg, off):
        return struct.unpack('<H', self.r(seg, off, 2))[0]

    def page_seg(self, n):
        return A000 if n == 0 else PAGE_SEG[n]

    # ---- calls
    def call(self, slot, args=(), regs=None, ds=None):
        """Far call of a slot; `args` are C arguments (16-bit words, first = leftmost). Returns (AX, DX)."""
        uc = self.uc
        sp = 0xFE00
        for a in reversed(args):
            sp -= 2
            self.w16(SEG_SS, sp, a)
        sp -= 4
        self.w16(SEG_SS, sp, RET_OFF)
        self.w16(SEG_SS, sp + 2, RET_SEG)
        for r, v in ((UC_X86_REG_SS, SEG_SS), (UC_X86_REG_DS, SEG_SS), (UC_X86_REG_ES, SEG_SS), (UC_X86_REG_SP, sp),
                     (UC_X86_REG_BP, 0), (UC_X86_REG_CS, self.cs), (UC_X86_REG_EFLAGS, 0x202)):
            uc.reg_write(r, v)
        for r in (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI):
            uc.reg_write(r, 0)
        for r, v in (regs or {}).items():
            uc.reg_write(r, v)
        ip = self.entry[slot]
        uc.reg_write(UC_X86_REG_IP, ip)
        uc.emu_start(self.cs * 16 + ip, RET_SEG * 16 + RET_OFF, count=20000000)
        self.es_out = uc.reg_read(UC_X86_REG_ES)
        return uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX)

    # ---- driver variables
    def cs_w(self, off):
        return self.r16(self.cs, off)

    def data_w(self, off):
        return self.r16(IMG, off)

    def crc_pages(self, pages=(0, 1, 2, 3)):
        c = 0
        for p in pages:
            c = zlib.crc32(self.r(self.page_seg(p), 0, 64000), c)
        return c & 0xFFFFFFFF


# ---- the deterministic page fill shared with the port's selftest (videotest.c page_fill)
def lcg_fill(seed, n=64000):
    s = seed & 0xFFFFFFFF
    out = bytearray(n)
    for i in range(n):
        s = (s * 1103515245 + 12345) & 0xFFFFFFFF
        b = (s >> 16) & 0xFF
        out[i] = 0 if (b & 3) == 0 else b
    return bytes(out)
