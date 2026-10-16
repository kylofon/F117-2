"""S5.8: the ORIGINAL END.EXE under Unicorn (the DOS/BIOS machine of port/notes/s53/dos_emu.py and s54/shell_emu.py: MGRAPHIC and MISC as overlays,
a stub sound driver, a key queue, timer interrupts) running one debrief from a comm block, to give the port its references (ctest `debrief`).

The script (a list of steps, see end_ref.py) is read at two places: every entry of wait_input (0000:2092) and every entry of poll_input (0000:1CF5):
  * at each wait_input the screen (A000, 64000 bytes) and the DAC are CRC'd; a wait for a key (ticks = FFFFh) is answered with Enter, and with
    `skipmarks` the first wait of 8 ticks (the site icons appear one by one) is answered with Space;
  * at the 3rd poll after the previous step the screen and the DAC are CRC'd, the step's cursor position and key words are applied.
At exit(code) the comm block (with the pilot record END wrote) is saved.
"""
import os, struct, sys, zlib, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..', 's53'))
sys.path.insert(0, os.path.join(HERE, '..', 's54'))
import dos_emu
import shell_emu
from shell_emu import ShellMachine, SEG_MG, SEG_MISC, SEG_SND, COMM_SEG, SLICE
from dos_emu import A000
from unicorn import *
from unicorn.x86_const import *

DGROUP_REL = 0x5FA
POLL, WAIT, AFTER_SRAND = 0x1CF5, 0x2092, 0x3C3B
SEED = 12345


class EndMachine(ShellMachine):
    def __init__(self, game, workdir, comm_blob):
        super().__init__(game, workdir, b'', prog='END', ds_off=DGROUP_REL)
        comm = bytearray(comm_blob)
        struct.pack_into('<HHH', comm, 0x1A, SEG_MG, SEG_SND, SEG_MISC)
        self.wb(COMM_SEG, 0, comm)
        self.comm0 = bytes(comm)
        self.snaps = []
        self.steps = []
        self.skipmarks = False
        self.skipped = False
        self.since = 99
        self.armed = {}
        self.dump = None

    def snapshot(self):
        scr = self.rb(A000, 0, 64000)
        return zlib.crc32(scr) & 0xFFFFFFFF, zlib.crc32(bytes(self.dac)) & 0xFFFFFFFF

    def _snap(self):
        self.snaps.append(self.snapshot())
        if self.dump:
            open(os.path.join(self.dump, 'p%02d.scr' % (len(self.snaps) - 1)), 'wb').write(self.rb(A000, 0, 64000) + bytes(self.dac))

    # a code hook can fire twice for the same instruction when a slice ends right before it: the second instruction of the function re-arms it
    def _once(self, key, off, fn):
        self.armed[key] = True

        def entry(m):
            if self.armed[key]:
                self.armed[key] = False
                fn(m)

        def rearm(m):
            self.armed[key] = True
        self.hook_call(0x0000, off, entry)
        self.hook_call(0x0000, off + 1, rearm)

    def on_wait(self, m):
        ss, sp = self.reg(UC_X86_REG_SS), self.reg(UC_X86_REG_SP)
        ticks = struct.unpack('<H', self.rb(ss, sp + 2, 2))[0]
        self._snap()
        if ticks == 0xFFFF:
            self.keys.append(0x1C0D)
        elif ticks == 8 and self.skipmarks and not self.skipped:
            self.skipped = True
            self.keys.append(0x3920)

    def on_poll(self, m):
        self.since += 1
        if self.since < 3 or not self.steps:
            return
        self.since = 0
        self._snap()
        x, y, keys = self.steps.pop(0)
        if x is not None:
            self.ww(self.ds, 0x7214, x)
        if y is not None:
            self.ww(self.ds, 0x7216, y)
        self.keys.extend(keys)

    def on_seed(self, m):
        self.wb(self.ds, 0x5158, struct.pack('<I', self.seed))

    def run_all(self, limit_slices=2000000):
        self._once('poll', POLL, self.on_poll)
        self._once('wait', WAIT, self.on_wait)
        self.hook_call(0x0000, AFTER_SRAND, self.on_seed)
        uc = self.uc
        pos = self.entry
        n = 0
        while self.exit_code is None and n < limit_slices:
            uc.emu_start(pos, 0xFFFFF, count=SLICE)
            n += 1
            if self.exit_code is not None:
                break
            cs, ip = uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)
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
                cs, ip = self.vec[8][0], self.vec[8][1]
                uc.reg_write(UC_X86_REG_CS, cs)
            pos = cs * 16 + ip
        return self.exit_code


def run(comm_blob, steps, skipmarks, outdir, game=None, dump=False):
    os.makedirs(outdir, exist_ok=True)
    game = game or os.path.join(ROOT, 'Game')
    work = os.path.join(outdir, 'work')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    m = EndMachine(game, work, comm_blob)
    m.seed = SEED
    m.steps = list(steps)
    m.skipmarks = skipmarks
    if dump:
        m.dump = os.path.join(outdir, 'dump')
        os.makedirs(m.dump, exist_ok=True)
    code = m.run_all()
    m._snap()
    comm = m.rb(COMM_SEG, 0, 5120)
    open(os.path.join(outdir, 'exit.txt'), 'w').write('%d\n' % code)
    open(os.path.join(outdir, 'snap.bin'), 'wb').write(b''.join(struct.pack('<II', a, b) for a, b in m.snaps))
    open(os.path.join(outdir, 'comm.bin'), 'wb').write(comm)
    open(os.path.join(outdir, 'comm_in.bin'), 'wb').write(comm_blob)
    return code, m
