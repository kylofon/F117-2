"""Run an F-117A AdLib sound driver (ASOUND.117) under Unicorn and log its OPL writes.
  drvemu.py CMD [seconds] [pollhz]   -> work/sound/asound_cmdNN.opl  (then oplrender.exe -> .wav)
Driver protocol: RE_GUIDE.md §2/§S2.4. snd_poll (slot 103) is called by the game from a timer ISR; the
rate here is a parameter (default 300 Hz, from START's PIT reload 4DAEh/5)."""
import struct, sys
from unicorn import *
from unicorn.x86_const import *

OPL_RATE = 49716
L = 0x2000                      # load segment
STK = 0x9000
STUB = 0x9800


class Drv:
    def __init__(self, path, slotbase_ok=True):
        d = open(path, 'rb').read()
        hs = struct.unpack_from('<H', d, 8)[0] * 16
        nrel = struct.unpack_from('<H', d, 6)[0]
        relo = struct.unpack_from('<H', d, 0x18)[0]
        img = bytearray(d[hs:])
        for i in range(nrel):
            o, s = struct.unpack_from('<HH', d, relo + 4 * i)
            p = s * 16 + o
            struct.pack_into('<H', img, p, (struct.unpack_from('<H', img, p)[0] + L) & 0xFFFF)
        if path.upper().endswith('.LOG'):       # id at +10h; cseg, dseg, size, base, n, entries from +28h
            self.cseg, self.dseg, _, self.base, n = struct.unpack_from('<5H', img, 0x28)
            self.ents = struct.unpack_from('<%dH' % n, img, 0x32)
        else:
            self.cseg, self.dseg, self.base, _, _, n = struct.unpack_from('<6H', img, 0x18)
            self.ents = struct.unpack_from('<%dH' % n, img, 0x24)
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        uc.mem_write(L * 16, bytes(img))
        uc.mem_write(STUB * 16, b'\xf4')
        self.log, self.t, self.reg, self.stat = [], 0, 0, 0
        self.pit = []
        uc.hook_add(UC_HOOK_INSN, self.h_in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self.h_out, None, 1, 0, UC_X86_INS_OUT)

    def h_in(self, uc, port, size, ud):
        if port == 0x388:
            return self.stat
        if port == 0x40:                 # free-running PIT counter (lo, hi alternating), for the calibration loop
            self.pitc = getattr(self, 'pitc', 0) + 37
            return (self.pitc >> 1) & 0xFF if self.pitc & 1 else (self.pitc >> 9) & 0xFF
        return 0

    def h_out(self, uc, port, size, val, ud):
        if port == 0x388:
            self.reg = val
        elif port == 0x389:
            if self.reg == 4:
                self.stat = 0xC0 if val == 0x21 else 0     # detection: timer-1 overflow flags
            else:
                self.log.append((self.t, self.reg, val))
        elif port == 0x38a:
            self.reg2 = val
        elif port == 0x38b:
            self.log.append((self.t, 0x100 | self.reg2, val))
        elif port == 0x40:
            self.pit.append(val)

    def call(self, i, *args, maxins=5_000_000):
        uc = self.uc
        uc.reg_write(UC_X86_REG_SS, STK); uc.reg_write(UC_X86_REG_SP, 0xF000)
        uc.reg_write(UC_X86_REG_DS, L); uc.reg_write(UC_X86_REG_ES, L)
        sp = 0xF000
        for a in reversed(args):
            sp -= 2; uc.mem_write(STK * 16 + sp, struct.pack('<H', a & 0xFFFF))
        sp -= 4; uc.mem_write(STK * 16 + sp, struct.pack('<HH', 0, STUB))
        uc.reg_write(UC_X86_REG_SP, sp)
        cs, ip = self.cseg, self.ents[i]
        uc.reg_write(UC_X86_REG_CS, cs)
        uc.emu_start(cs * 16 + ip, STUB * 16, count=maxins)
        return uc.reg_read(UC_X86_REG_AX)


def render_log(path, cmd, secs, maxticks=200000):
    """?SOUND.LOG overlay (logo music): the driver hooks INT 8 itself (vector 0000:0020) and reprograms the
    PIT; we call that ISR at the PIT rate it programmed (IRET returns to a HLT stub)."""
    D = Drv(path)
    uc = D.uc
    uc.mem_write(0x20, struct.pack('<HH', 0, STUB))               # old INT 8 = stub
    D.call(0, 0)
    D.call(1, cmd)
    hz = 1193182.0 / (D.pit[-2] | D.pit[-1] << 8 if len(D.pit) >= 2 else 65536)
    isr_off, isr_seg = struct.unpack('<HH', bytes(uc.mem_read(0x20, 4)))
    ticks = int(secs * hz)
    for k in range(ticks):
        D.t = int(k * OPL_RATE / hz)
        uc.reg_write(UC_X86_REG_SS, STK); uc.reg_write(UC_X86_REG_DS, L); uc.reg_write(UC_X86_REG_ES, L)
        sp = 0xF000 - 6
        uc.mem_write(STK * 16 + sp, struct.pack('<HHH', 0, STUB, 0x202))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_CS, isr_seg)
        uc.emu_start(isr_seg * 16 + isr_off, STUB * 16, count=2_000_000)
    D.hz = hz
    return D


def render(cmd, secs, hz=None, drv='ASOUND'):
    """Poll rate follows the game's timer ISR (START 0000:8E6D): snd_poll returns -1 -> PIT back to the 60 Hz
    base, > 0 -> base/5 (300 Hz), 0 -> unchanged. Idle polls return -1, so music runs at 60 Hz."""
    D = Drv('Game/%s.117' % drv)
    D.call(0, 0)                       # snd_init(0)
    D.call(1, cmd)                     # snd_cmd
    fixed = hz is not None
    hz = hz or 60
    idle, n, t = 0, 0, 0.0
    while t < secs:
        D.t = int(t * OPL_RATE)
        r = D.call(3)                  # snd_poll
        if fixed:
            pass
        elif r == 0xFFFF:
            hz = 60
        elif r & 0x8000 == 0 and r:
            hz = 300
        t += 1.0 / hz
        idle = idle + 1 if len(D.log) == n else 0
        n = len(D.log)
        if idle > 2 * hz:              # nothing written for 2 s: sound finished
            break
    D.t = int(t * OPL_RATE)
    return D


if __name__ == '__main__':
    cmd = int(sys.argv[1]); secs = float(sys.argv[2]) if len(sys.argv) > 2 else 30
    hz = float(sys.argv[3]) if len(sys.argv) > 3 else 60
    D = render(cmd, secs, hz)
    out = 'work/sound/asound_cmd%02d.opl' % cmd
    with open(out, 'wb') as f:
        for t, r, v in D.log:
            f.write(struct.pack('<IHB', t, r, v))
    print(out, len(D.log), 'writes', D.t / OPL_RATE, 's')
