"""S5.4: the ORIGINAL START.EXE under Unicorn (a small DOS/BIOS machine, port/notes/s53/dos_emu.py, plus the three drivers F117.COM would have
loaded) driven by a script, to give the port its references (ctest `shell`).

What is emulated beyond dos_emu: MGRAPHIC.EXE and MISC.EXE are loaded as overlays (the 4B03h loader's relocation) at fixed segments and
bound by START's own ovl_bind through the comm words 1A/1C/1E; the sound driver is a stub (every slot is `xor ax,ax; retf`); the keyboard is a
queue of BIOS key words; INT 10h AX=1012h (the palette queue), INT 1Ah, file creation/writing; a timer interrupt (INT 8 on START's own
ISR) injected every few thousand instructions; port 3DAh toggles so the retrace waits terminate.

The script (shell_*.txt) is read at the entry of ui_poll_input (0000:39F7): the screen (A000, 64000 bytes) and the DAC are CRC'd, then the
step's cursor position and key words are applied.  At exit(code) the comm block and ROSTER.FIL are saved.

usage: python shell_emu.py <script.txt> <outdir> [game dir]
"""
import os, struct, sys, zlib, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..', 's53'))
import dos_emu
dos_emu.MEMTOP = 0x8800
from dos_emu import Machine, A000
from unicorn import *
from unicorn.x86_const import *

COMM_SEG = 0x8800
SEG_MG, SEG_MISC, SEG_SND = 0x8A00, 0x9400, 0x9500
SEED = 12345
SLICE = 3000


def load_overlay(m, path, seg):
    d = open(path, 'rb').read()
    h = struct.unpack_from('<14H', d, 0)
    hdr = h[4] * 16
    size = ((h[2] - 1) * 512 + (h[1] or 512)) - hdr
    img = bytearray(d[hdr:hdr + size])
    for i in range(h[3]):
        off, rseg = struct.unpack_from('<HH', d, h[12] + 4 * i)
        p = rseg * 16 + off
        struct.pack_into('<H', img, p, (struct.unpack_from('<H', img, p)[0] + seg) & 0xFFFF)
    m.wb(seg, 0, img)
    return len(img)


def make_sound_stub(m, seg):
    img = bytearray(0x50)
    struct.pack_into('<HHHHH', img, 0x18, seg, seg, 100, 0, 0)       # code seg, data seg, slot base, bytes before code, code bytes
    struct.pack_into('<H', img, 0x22, 5)
    for i in range(5):
        struct.pack_into('<H', img, 0x24 + 2 * i, 0x40)
    img[0x40:0x43] = b'\x31\xC0\xCB'                                   # xor ax,ax ; retf
    m.wb(seg, 0, img)


def make_roster():
    """A synthetic ROSTER.FIL (802 bytes): ten pilots, the selected one is 3; pilots 8 and 9 are retired / killed."""
    out = struct.pack('<H', 3)
    for i in range(10):
        r = bytearray(0x50)
        struct.pack_into('<H', r, 0, i)
        name = b'Test Pilot %d' % i
        r[2:2 + len(name)] = name
        struct.pack_into('<H', r, 0x20, i % 7)                         # rank
        struct.pack_into('<6H', r, 0x22, i % 3, (i + 1) % 4, 0, i % 2, 0, 0)   # PH AM DFC SS AFC CMH
        struct.pack_into('<HH', r, 0x2E, 100 + 10 * i, 200 + i)        # best, last
        struct.pack_into('<I', r, 0x32, 12345 * (i + 1))               # career score
        struct.pack_into('<H', r, 0x36, 3 * i)                         # missions
        struct.pack_into('<HHHHHH', r, 0x38, 0, 1, 1, 1, 1, 0)         # theatre war mtype skill crash model
        struct.pack_into('<H', r, 0x4E, 1 if i == 8 else 2 if i == 9 else 0)
        out += bytes(r)
    return out


class ShellMachine(Machine):
    def __init__(self, game, workdir, roster, prog='START', ds_off=0xA95):
        super().__init__(prog, game=game, tail=b'')
        m = self
        uc = self.uc
        self.workdir = workdir
        self.roster_path = os.path.join(workdir, 'ROSTER.FIL')
        open(self.roster_path, 'wb').write(roster)
        self.files['ROSTER.FIL'] = self.roster_path
        # environment block with a program name (the C start-up reads it)
        env = 0x0700
        uc.mem_write(env * 16, b'\0\0\1\0C:\\' + prog.encode() + b'.EXE\0')
        uc.mem_write(dos_emu.PSP * 16 + 0x2C, struct.pack('<H', env))
        uc.mem_write(0x46C, struct.pack('<I', 0))
        # the iret that stands for the BIOS INT 8
        uc.mem_write(0xF0100, b'\xCF')
        # F117.COM: the comm block and the drivers
        uc.mem_write(0x4F0, struct.pack('<HHH', COMM_SEG, 1, 0))
        comm = bytearray(5120)
        comm[0:13] = b'Mgraphic.exe\0'
        comm[0x0D:0x0D + 11] = b'Asound.117\0'
        struct.pack_into('<H', comm, 0x22, 1)                         # clearance
        struct.pack_into('<H', comm, 0x32, 3)                         # adapter: MCGA
        struct.pack_into('<H', comm, 0x72, 0)                         # no joystick, no mouse
        load_overlay(m, os.path.join(self.game, 'MGRAPHIC.EXE'), SEG_MG)
        load_overlay(m, os.path.join(self.game, 'MISC.EXE'), SEG_MISC)
        make_sound_stub(m, SEG_SND)
        struct.pack_into('<HHH', comm, 0x1A, SEG_MG, SEG_SND, SEG_MISC)
        uc.mem_write(COMM_SEG * 16, bytes(comm))
        self.comm0 = bytes(comm)
        self.trace = None
        self.keys = []
        self.ticks = 0
        self.files_w = {}
        self.polls = 0
        self.snaps = []
        self.script = []
        self.since = 99
        self.ds = self.load + ds_off

    # ---------------------------------------------------------------------------------------------- interrupts
    def _intr(self, uc, n, _):
        ax = self.reg(UC_X86_REG_AX)
        ah, al = ax >> 8, ax & 0xFF
        if self.trace is not None and n == 0x21:
            self.trace.append((hex(ah), hex(al), self.reg(UC_X86_REG_BX), self.reg(UC_X86_REG_CX),
                               self.asciiz(self.reg(UC_X86_REG_DS), self.reg(UC_X86_REG_DX)) if ah in (0x3D, 0x3C, 0x43) else ''))
        if n == 0x10 and ah == 0x10:
            bx, cx, dx = self.reg(UC_X86_REG_BX), self.reg(UC_X86_REG_CX), self.reg(UC_X86_REG_DX)
            es = self.reg(UC_X86_REG_ES)
            if al == 0x12:
                data = self.rb(es, dx, 3 * cx)
                for i in range(3 * cx):
                    if (bx * 3 + i) < 768:
                        self.dac[bx * 3 + i] = data[i] & 0x3F
            elif al == 0x10:
                self.dac[bx * 3:bx * 3 + 3] = bytes([dx >> 8 & 0x3F, cx >> 8 & 0x3F, cx & 0x3F])
            return
        if n == 0x16:
            if ah == 1:
                if self.keys:
                    self.setreg(UC_X86_REG_AX, self.keys[0])
                    self._flags(zf=False)
                else:
                    self._flags(zf=True)
            elif ah == 0:
                self.setreg(UC_X86_REG_AX, self.keys.pop(0) if self.keys else 0x011B)
            elif ah == 2:
                self.setreg(UC_X86_REG_AX, 0)
            return
        if n == 0x1A:
            t = struct.unpack('<I', self.rb(0, 0x46C, 4))[0]
            self.setreg(UC_X86_REG_CX, t >> 16)
            self.setreg(UC_X86_REG_DX, t & 0xFFFF)
            self.setreg(UC_X86_REG_AX, 0)
            return
        if n == 0x21:
            bx, cx, dx = self.reg(UC_X86_REG_BX), self.reg(UC_X86_REG_CX), self.reg(UC_X86_REG_DX)
            ds = self.reg(UC_X86_REG_DS)
            if ah == 0x35 and al == 8 and 8 not in self.vec:
                self.setreg(UC_X86_REG_ES, 0xF000)
                self.setreg(UC_X86_REG_BX, 0x0100)
                return
            if ah == 0x3C:                                            # create
                name = self.asciiz(ds, dx).decode('latin-1').upper().replace('\\', '/').split('/')[-1]
                path = os.path.join(self.workdir, name)
                h = self.next_handle
                self.handles[h] = [open(path, 'w+b'), path]
                self.files[name] = path
                self.setreg(UC_X86_REG_AX, h)
                self._flags(cf=False)
                return
            if ah == 0x40 and bx in self.handles:                     # write to a file
                data = self.rb(ds, dx, cx)
                f = self.handles[bx][0]
                f.write(data)
                f.flush()
                self.setreg(UC_X86_REG_AX, cx)
                self._flags(cf=False)
                return
            if ah == 0x3D and (al & 3) != 0:                          # open for writing: read/write on the copy
                name = self.asciiz(ds, dx).decode('latin-1').upper().replace('\\', '/').split('/')[-1]
                path = self.files.get(name)
                if path and path.startswith(self.workdir):
                    h = self.next_handle
                    self.handles[h] = [open(path, 'r+b'), path]
                    self.setreg(UC_X86_REG_AX, h)
                    self._flags(cf=False)
                    return
            if ah == 0x1A:                                            # set DTA
                return
            if ah == 0x43:                                            # get / set file attributes
                name = self.asciiz(ds, dx).decode('latin-1').upper().replace('\\', '/').split('/')[-1]
                if al == 0 and name not in self.files:
                    self.setreg(UC_X86_REG_AX, 2)
                    self._flags(cf=True)
                else:
                    self.setreg(UC_X86_REG_CX, 0x20)
                    self._flags(cf=False)
                return
        super()._intr(uc, n, _)
        if n == 0x21:
            h = 5
            while h in self.handles:
                h += 1
            self.next_handle = h                                      # DOS gives the lowest free handle

    def _in(self, uc, port, size, _):
        if port == 0x3DA:
            self.vr = getattr(self, 'vr', 0) ^ 9                      # retrace bit and display-enable bit alternate on every read
            return self.vr
        return super()._in(uc, port, size, _)

    # ----------------------------------------------------------------------------------------------- the script
    def load_script(self, path):
        steps = []
        for ln in open(path):
            ln = ln.split('#')[0].strip()
            if not ln:
                continue
            f = ln.split()
            if f[0] == 'seed':
                self.seed = int(f[1])
                continue
            x = None if f[0] == '-' else int(f[0])
            y = None if f[1] == '-' else int(f[1])
            steps.append((x, y, [int(k, 16) for k in f[2:]]))
        self.script = steps

    def snapshot(self):
        scr = self.rb(A000, 0, 64000)
        return zlib.crc32(scr) & 0xFFFFFFFF, zlib.crc32(bytes(self.dac)) & 0xFFFFFFFF

    def on_poll(self, mach):
        self.polls += 1
        self.since += 1
        if self.since < 3 or not self.script:
            return
        self.since = 0
        self.snaps.append(self.snapshot())
        if getattr(self, 'dump', None):
            open(os.path.join(self.dump, 'p%02d.scr' % (len(self.snaps) - 1)), 'wb').write(self.rb(A000, 0, 64000) + bytes(self.dac))
        x, y, keys = self.script.pop(0)
        if x is not None:
            self.ww(self.ds, 0xE08A, x)
        if y is not None:
            self.ww(self.ds, 0xE08C, y)
        self.keys.extend(keys)

    def on_seed(self, mach):
        self.wb(self.ds, 0xAE8C, struct.pack('<I', self.seed))

    # -------------------------------------------------------------------------------------------------- running
    def run_all(self, limit_slices=400000):
        self.hook_call(0x0000, 0x39F7, self.on_poll)
        self.hook_call(0x0000, 0x0020, self.on_seed)
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

    def ww(self, seg, off, v):
        self.wb(seg, off, struct.pack('<H', v & 0xFFFF))


def run(script, outdir, game=None, roster=None, dump=False):
    os.makedirs(outdir, exist_ok=True)
    game = game or os.path.join(ROOT, 'Game')
    work = os.path.join(outdir, 'work')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    roster = roster or make_roster()
    m = ShellMachine(game, work, roster)
    m.dump = os.path.join(outdir, 'dump') if dump else None
    if m.dump:
        os.makedirs(m.dump, exist_ok=True)
    m.seed = SEED
    m.load_script(script)
    code = m.run_all()
    m.snaps.append(m.snapshot())
    if m.dump:
        open(os.path.join(m.dump, 'p%02d.scr' % (len(m.snaps) - 1)), 'wb').write(m.rb(A000, 0, 64000) + bytes(m.dac))
    shutil.copy(script, os.path.join(outdir, 'script.txt'))
    comm = m.rb(COMM_SEG, 0, 5120)
    open(os.path.join(outdir, 'exit.txt'), 'w').write('%d' % code + chr(10))
    open(os.path.join(outdir, 'snap.bin'), 'wb').write(b''.join(struct.pack('<II', a, b) for a, b in m.snaps))
    open(os.path.join(outdir, 'comm.bin'), 'wb').write(comm)
    open(os.path.join(outdir, 'roster_in.fil'), 'wb').write(roster)
    shutil.copy(m.roster_path, os.path.join(outdir, 'roster_out.fil'))
    # DGROUP after exit, for debugging
    open(os.path.join(outdir, 'dgroup.bin'), 'wb').write(m.rb(m.ds, 0, 0x10000))
    return code, m


if __name__ == '__main__':
    code, m = run(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    print('where', hex(m.uc.reg_read(UC_X86_REG_CS)), hex(m.uc.reg_read(UC_X86_REG_IP)), 'exit code', code, 'steps left', len(m.script), 'polls', m.polls, 'snaps', len(m.snaps), 'unhandled', m.int_log[:8])
