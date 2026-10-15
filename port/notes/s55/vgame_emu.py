"""S5.5: the ORIGINAL VGAME.EXE under Unicorn (the DOS/BIOS machine of port/notes/s53/dos_emu.py + s54/shell_emu.py, here with the comm block
START leaves) as the oracle of the sim-flow port (ctest `sim`).

Two ways to use it:
  * `run_to(pc)`   : the real program from its entry (c0 start-up, main, sim_init) up to the entry of a function, e.g. sim_run (0233) for the
                     start-up state;
  * `call(...)`    : one function of the original called from a snapshot of the whole machine (calling convention cdecl, SS = DS = DGROUP),
                     with the functions the port leaves as stubs (render3d, flight, weapons, AI ...) hooked to `return 0`, exactly as the
                     port's stubs do.

The memory map is the one the port's test mode loads (mem[] = Unicorn's 0..9FFFFh, A000 = VGA page):
  free DOS memory 0x3700..MEMTOP, comm block at COMM_SEG, page 2 at PAGE2_SEG, MGRAPHIC / MISC / sound stub overlays above.
"""
import os, struct, sys, zlib, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..', 's53'))
sys.path.insert(0, os.path.join(HERE, '..', 's54'))
import dos_emu
dos_emu.MEMTOP = 0x7C00
import shell_emu
shell_emu.COMM_SEG = COMM_SEG = 0x7C00
shell_emu.SEG_MG, shell_emu.SEG_MISC, shell_emu.SEG_SND = 0x8E00, 0x9800, 0x9900
SEG_MG, SEG_MISC, SEG_SND = 0x8E00, 0x9800, 0x9900
PAGE2_SEG = 0x7D80
from dos_emu import A000
from shell_emu import ShellMachine, load_overlay, make_sound_stub
from unicorn import *
from unicorn.x86_const import *

NEAR_TRAP = 0xFFF0                          # where a near call returns to (unused code at the end of segment 0)
TRAP_SEG, TRAP_OFF = 0xF000, 0x0200          # `hlt` here: where a called function returns to
DGROUP_REL = 0x1E23                          # DGROUP segment relative to the load segment
MEM_SNAP = 0xA0000                           # bytes of conventional memory in a snapshot


class VG(ShellMachine):
    def __init__(self, game, workdir, comm_blob):
        os.makedirs(workdir, exist_ok=True)
        super().__init__(game, workdir, b'', prog='VGAME', ds_off=DGROUP_REL)
        self.stubs = {}
        comm = bytearray(comm_blob)
        struct.pack_into('<HHHH', comm, 0x1A, SEG_MG, SEG_SND, SEG_MISC, PAGE2_SEG)
        self.wb(COMM_SEG, 0, comm)
        self.comm0 = bytes(comm)
        self.wb(TRAP_SEG, TRAP_OFF, b'\xF4')
        self.wb(PAGE2_SEG, 0, bytes(64000))
        self.stop_at = {}
        self.vr = 0
        self.int0_count = 0
        # #DE of the clip core of segment 1001 (0336 / 06AC install their own INT 0 handler): Unicorn has no divide-error dispatch for 16-bit real mode,
        # so an idiv that would overflow is detected before it runs and dispatched through the IVT as a 286+ does (saved IP = the idiv itself)
        for off, dvo in ((0x043C, 0x228B), (0x0474, 0x228D), (0x0796, 0x261B), (0x07CE, 0x261D)):
            a = self.lin(0x1001, off)
            self.uc.hook_add(UC_HOOK_CODE, self._mk_div(dvo), None, a, a)
        # the same for the four `idiv cx` of 11EB:0906 (render3d projection; 11EB:0281 points IVT[0] at 11EB:0975), as port/notes/s37/R7a_emu.py does (S5.6b)
        for off in (0x091E, 0x0930, 0x094F, 0x0963):
            a = self.lin(0x11EB, off)
            self.uc.hook_add(UC_HOOK_CODE, self._mk_div(None), None, a, a)

    def _mk_div(self, dvo):
        def hk(uc, addr, size, _):
            if dvo is None:                                                         # divisor in CX
                d = uc.reg_read(UC_X86_REG_CX)
                d = d - 0x10000 if d & 0x8000 else d
            else:
                d = struct.unpack('<h', self.dg(dvo, 2))[0]
            n = (uc.reg_read(UC_X86_REG_DX) << 16) | uc.reg_read(UC_X86_REG_AX)
            if n & 0x80000000:
                n -= 1 << 32
            if d == 0 or not -32768 <= int(n / d) <= 32767:
                self.int0_count += 1
                ip, cs = uc.reg_read(UC_X86_REG_IP), uc.reg_read(UC_X86_REG_CS)
                fl = uc.reg_read(UC_X86_REG_EFLAGS) & 0xFFFF
                sp = (uc.reg_read(UC_X86_REG_SP) - 6) & 0xFFFF
                ss = uc.reg_read(UC_X86_REG_SS)
                self.wb(ss, sp, struct.pack('<HHH', ip, cs, fl))
                uc.reg_write(UC_X86_REG_SP, sp)
                off, seg = struct.unpack('<HH', self.rb(0, 0, 4))
                uc.reg_write(UC_X86_REG_CS, seg)
                uc.reg_write(UC_X86_REG_IP, off)
        return hk

    # ------------------------------------------------------------------------------------------ addresses
    def lin(self, seg, off):
        """Linear address of a VGAME code address SSSS:OOOO (SSSS relative to the load segment)."""
        return ((self.load + seg) << 4) + off

    def dg(self, off, n):
        return self.rb(self.ds, off, n)

    def dw(self, off):
        return self.rw(self.ds, off)

    def setw(self, off, v):
        self.ww(self.ds, off, v)

    # ------------------------------------------------------------------------------------------ running the program
    def run_to(self, seg, off, limit_slices=100000, slice_n=3000):
        """From the program entry (or where it stopped) until the instruction at seg:off is about to run."""
        uc = self.uc
        target = self.lin(seg, off)
        hit = []

        def h(uc_, addr, size, _):
            hit.append(addr)
            uc.emu_stop()
        hk = uc.hook_add(UC_HOOK_CODE, h, None, target, target)
        pos = getattr(self, 'pos', self.entry)
        n = 0
        while self.exit_code is None and not hit and n < limit_slices:
            uc.emu_start(pos, 0xFFFFF, count=slice_n)
            n += 1
            cs, ip = uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)
            pos = cs * 16 + ip
            if hit:
                break
            if n % 4 == 0:
                t = struct.unpack('<I', self.rb(0, 0x46C, 4))[0]
                self.wb(0, 0x46C, struct.pack('<I', t + 1))
            fl = uc.reg_read(UC_X86_REG_EFLAGS)
            if 8 in self.vec and (fl & 0x200):
                sp = uc.reg_read(UC_X86_REG_SP)
                ss = uc.reg_read(UC_X86_REG_SS)
                sp = (sp - 6) & 0xFFFF
                self.wb(ss, sp, struct.pack('<HHH', ip, cs, fl & 0xFFFF))
                uc.reg_write(UC_X86_REG_SP, sp)
                uc.reg_write(UC_X86_REG_EFLAGS, fl & ~0x300)
                uc.reg_write(UC_X86_REG_CS, self.vec[8][0])
                pos = self.vec[8][0] * 16 + self.vec[8][1]
        uc.hook_del(hk)
        self.pos = pos
        return bool(hit)

    # ------------------------------------------------------------------------------------------ snapshots
    def snapshot(self):
        mem = bytes(self.uc.mem_read(0, MEM_SNAP))
        a000 = bytes(self.uc.mem_read(A000 * 16, 65536))
        return mem, a000, bytes(self.dac)

    def restore(self, snap):
        mem, a000, dac = snap
        self.uc.mem_write(0, mem)
        self.uc.mem_write(A000 * 16, a000)
        self.dac[:] = dac
        self.dac_w = self.dac_r = self.dac_wsub = self.dac_rsub = 0

    # ------------------------------------------------------------------------------------------ calling one function
    def hook_stub(self, seg, off, far=False):
        """Make the function at seg:off return at once with AX = DX = 0 (a stub of the port)."""
        a = self.lin(seg, off)
        if a in self.stubs:
            return
        self.stubs[a] = far

        def h(uc, addr, size, _):
            sp = uc.reg_read(UC_X86_REG_SP)
            ss = uc.reg_read(UC_X86_REG_SS)
            if far:
                ip, cs = struct.unpack('<HH', self.rb(ss, sp, 4))
                uc.reg_write(UC_X86_REG_SP, (sp + 4) & 0xFFFF)
                uc.reg_write(UC_X86_REG_CS, cs)
            else:
                ip = struct.unpack('<H', self.rb(ss, sp, 2))[0]
                uc.reg_write(UC_X86_REG_SP, (sp + 2) & 0xFFFF)
            uc.reg_write(UC_X86_REG_IP, ip)
            uc.reg_write(UC_X86_REG_AX, 0)
            uc.reg_write(UC_X86_REG_DX, 0)
        self.uc.hook_add(UC_HOOK_CODE, h, None, a, a)

    def call(self, seg, off, args=(), far=False, max_ins=50000000):
        """Calls seg:off (cdecl, word args) and returns (AX, DX)."""
        uc = self.uc
        sp = 0xF000                                # a clean stack well above the data, below the C stack of the real program
        ss = self.ds
        # push args right to left
        for a in reversed(args):
            sp -= 2
            self.ww(ss, sp, a & 0xFFFF)
        if far:
            sp -= 4
            self.wb(ss, sp, struct.pack('<HH', TRAP_OFF, TRAP_SEG))
        else:
            # near call inside segment `seg`: the return address must be in the code segment of the callee
            sp -= 2
            self.ww(ss, sp, NEAR_TRAP)
            # the trap for near calls: `hlt` at <load+seg>:0200 is not available, so use a far-style trap through a dedicated segment
        uc.reg_write(UC_X86_REG_SS, ss)
        uc.reg_write(UC_X86_REG_DS, self.ds)
        uc.reg_write(UC_X86_REG_ES, self.ds)
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x202)
        cs = self.load + seg
        if far:
            until = TRAP_SEG * 16 + TRAP_OFF
        else:
            until = (cs << 4) + NEAR_TRAP
        # near returns land at cs:0200: put a hlt there (restored afterwards)
        saved = None
        if not far:
            saved = self.rb(cs, NEAR_TRAP, 1)
            self.wb(cs, NEAR_TRAP, b'\xF4')
        uc.reg_write(UC_X86_REG_CS, cs)
        uc.emu_start((cs << 4) + off, until, count=max_ins)
        if saved is not None:
            self.wb(cs, NEAR_TRAP, saved)
        if (uc.reg_read(UC_X86_REG_CS) * 16 + uc.reg_read(UC_X86_REG_IP)) & 0xFFFFF != until:
            raise RuntimeError('call %04X:%04X did not return (at %04X:%04X)' % (seg, off, uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)))
        return uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX)

    def _intr(self, uc, n, _):
        if n == 0x16:                               # bioskey(0x10 | cmd): the enhanced-keyboard functions behave as the plain ones
            ax = self.reg(UC_X86_REG_AX)
            if (ax >> 8) in (0x10, 0x11, 0x12):
                self.setreg(UC_X86_REG_AX, (ax & 0xFF) | (((ax >> 8) & 0xF) << 8))
        if n == 0x21 and (self.reg(UC_X86_REG_AX) >> 8) == 0x0B:        # kbhit(): AL = FFh when a key is waiting
            ax = self.reg(UC_X86_REG_AX)
            self.setreg(UC_X86_REG_AX, (ax & 0xFF00) | (0xFF if self.keys else 0))
            return
        super()._intr(uc, n, _)

    def _in(self, uc, port, size, _):
        if port == 0x3DA:
            self.vr ^= 9
            return self.vr
        return super()._in(uc, port, size, _)


def load_comm(path):
    return open(path, 'rb').read()


if __name__ == '__main__':
    game = os.path.join(ROOT, 'Game')
    comm = load_comm(sys.argv[1])
    wd = os.path.join(ROOT, 'work', 's55', 'emu')
    shutil.rmtree(wd, ignore_errors=True)
    m = VG(game, wd, comm)
    ok = m.run_to(0, 0x0233)
    print('reached sim_run', ok, 'exit', m.exit_code, 'DGROUP', hex(m.ds), 'int_log', m.int_log[:8])
