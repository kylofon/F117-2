"""Unicorn harness for END.EXE near functions (code segment 0 = linear 0, DGROUP = 0x05FA, comm block at 0x3000:0).
Reads work/unpacked/END.EXE at run time (nothing of the game is stored in the repo)."""
import os, struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn.x86_const import *

_here = os.path.dirname(os.path.abspath(__file__))
_root = _here
while _root and not os.path.exists(os.path.join(_root, 'PLAN.md')):
    _root = os.path.dirname(_root)
D = open(os.path.join(_root, 'work', 'unpacked', 'END.EXE'), 'rb').read()
IMG = D[struct.unpack_from('<H', D, 8)[0] * 16:]
DSEG = 0x05FA
DSB = DSEG * 16
CSEG = 0x3000          # comm block segment
CB = CSEG * 16
RET = 0xFFFF
SLOT_TABLE = 0x23FA    # DS offset of the 5-byte driver stubs


def s16(v):
    v &= 0xffff
    return v - 0x10000 if v & 0x8000 else v


class Emu:
    def __init__(s):
        s.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc = s.uc
        uc.mem_map(0, 0x110000)
        uc.mem_write(0, IMG)
        for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            uc.reg_write(r, DSEG)
        uc.reg_write(UC_X86_REG_CS, 0)
        s.calls = []
        s.slotaddr = {}
        for slot in range(0, 110):
            a = DSB + SLOT_TABLE + 5 * slot
            s.slotaddr[a] = slot
            uc.hook_add(UC_HOOK_CODE, s._hk_slot, begin=a, end=a)
        s.write_slots()
        s.slot_ret = {}      # slot -> return value (default 0)
        s.slot_nargs = {42: 8, 101: 1, 5: 2, 22: 7, 47: 2}

    def write_slots(s):
        """the driver call stubs live inside DGROUP (retf); rewrite them after DGROUP was overwritten"""
        for a in s.slotaddr:
            s.uc.mem_write(a, b'\xcb\x90\x90\x90\x90')

    # --- stubs ---
    def _hk_slot(s, uc, addr, size, ud):
        slot = s.slotaddr[addr]
        sp = uc.reg_read(UC_X86_REG_SP)
        n = s.slot_nargs.get(slot, 0)
        a = struct.unpack('<%dH' % n, uc.mem_read(DSB + sp + 4, 2 * n)) if n else ()
        s.calls.append(('slot', slot) + a)
        v = s.slot_ret.get(slot, 0)
        if callable(v):
            v = v(s)
        uc.reg_write(UC_X86_REG_AX, v & 0xffff)

    def near_stub(s, addr, name, nargs, ret=0, rec=None):
        """replace the near function at addr by `ret`; logs (name, args...). rec(s, args) returns the logged tuple tail."""
        def hk(uc, a, size, ud):
            sp = uc.reg_read(UC_X86_REG_SP)
            args = struct.unpack('<%dH' % nargs, uc.mem_read(DSB + sp + 2, 2 * nargs)) if nargs else ()
            if rec:
                s.calls.append((name,) + tuple(rec(s, args)))
            else:
                s.calls.append((name,) + tuple(args))
            v = ret(s, args) if callable(ret) else ret
            uc.reg_write(UC_X86_REG_AX, v & 0xffff)
            s._ret_near()
        s.uc.hook_add(UC_HOOK_CODE, hk, begin=addr, end=addr)

    def _ret_near(s):
        uc = s.uc
        sp = uc.reg_read(UC_X86_REG_SP)
        ip = struct.unpack('<H', uc.mem_read(DSB + sp, 2))[0]
        uc.reg_write(UC_X86_REG_SP, sp + 2)
        uc.reg_write(UC_X86_REG_IP, ip)

    # --- memory ---
    def w(s, off, val, sz=2):
        s.uc.mem_write(DSB + off, (val & ((1 << (8 * sz)) - 1)).to_bytes(sz, 'little'))

    def r(s, off, sz=2, signed=False):
        return int.from_bytes(s.uc.mem_read(DSB + off, sz), 'little', signed=signed)

    def wb(s, off, b):
        s.uc.mem_write(DSB + off, bytes(b))

    def rb(s, off, n):
        return bytes(s.uc.mem_read(DSB + off, n))

    def cstr(s, off):
        t = b''
        while True:
            c = s.rb(off, 1)
            if c == b'\0':
                return t
            t += c
            off += 1

    def cw(s, off, val):
        s.uc.mem_write(CB + off, (val & 0xffff).to_bytes(2, 'little'))

    def cr(s, off):
        return int.from_bytes(s.uc.mem_read(CB + off, 2), 'little')

    def setup_far_ptrs(s):
        s.w(0x7222, 0); s.w(0x7224, CSEG); s.w(0x55de, 0x1272); s.w(0x55e0, CSEG)

    def call(s, addr, args=(), maxins=5000000):
        uc = s.uc
        sp = 0xF800
        for a in reversed(args):
            sp -= 2
            uc.mem_write(DSB + sp, (a & 0xffff).to_bytes(2, 'little'))
        sp -= 2
        uc.mem_write(DSB + sp, RET.to_bytes(2, 'little'))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_BP, 0)
        uc.reg_write(UC_X86_REG_CS, 0)
        for r_ in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            uc.reg_write(r_, DSEG)
        uc.emu_start(addr, RET, count=maxins)
        return uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX)

    def snapshot(s):
        return bytes(s.uc.mem_read(DSB, 0xF000)), bytes(s.uc.mem_read(CB, 0x1400))
