"""Census of accesses to the shared F117.COM block (segment stored at 0000:04F0).
  commscan.py            scans work/unpacked/*.EXE (run tools/unlzexe.py first)
Finds each EXE's global far pointers to the block (comm:0000 and comm:1272/120e), then every
`les bx,[ptr]` ... `es:[bx+N]` use in a linear sweep. Writes work/s12/comm_<EXE>.txt (one line per
access) and prints a per-offset summary: offset -> R/W letters per program.
"""
import collections, glob, os, re, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

PROGS = ['SETUP', 'PLAYER', 'START', 'VGAME', 'END', 'DSWAP']


def image(path):
    d = open(path, 'rb').read()
    return d[struct.unpack_from('<H', d, 8)[0] * 16:]


def comm_ptrs(img):
    """{global offset: base offset in block} from `mov [g+2],ax(comm seg); mov word [g],imm`."""
    ptrs = {}
    for m in re.finditer(rb'\xa3(..)\xc7\x06(..)(..)', img, re.S):
        hi, lo, base = (struct.unpack('<H', x)[0] for x in m.groups())
        if hi == lo + 2 and base in (0, 0x120e, 0x1272):
            ptrs[lo] = base
    return ptrs


def sweep(img):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = False
    pos = 0
    while pos < len(img):
        got = False
        for ins in md.disasm(img[pos:], pos):
            yield ins
            pos = ins.address + ins.size
            got = True
        if not got or pos < len(img):
            pos += 1


def main():
    os.makedirs('work/s12', exist_ok=True)
    summary = collections.defaultdict(dict)
    for p in PROGS:
        img = image('work/unpacked/%s.EXE' % p)
        ptrs = comm_ptrs(img)
        out = open('work/s12/comm_%s.txt' % p, 'w')
        out.write('pointers: %s\n' % {('%04x' % k): ('%04x' % v) for k, v in ptrs.items()})
        cur = None                       # base of block the current ES:BX points at
        for ins in sweep(img):
            op = ins.op_str
            if ins.mnemonic == 'les' and op.startswith('bx, ptr [0x'):
                g = int(op[op.index('[0x') + 3:-1], 16)
                cur = ptrs.get(g)
                continue
            if cur is None:
                continue
            if re.search(r'\bbx\b', op.split(',')[0]) and ins.mnemonic not in ('push', 'cmp', 'test') \
                    and 'es:' not in op.split(',')[0]:
                cur = None               # BX redefined
                continue
            if ins.mnemonic in ('call', 'lcall', 'ret', 'retf', 'jmp', 'ljmp'):
                cur = None
                continue
            mm = re.search(r'es:\[bx(?: \+ 0x([0-9a-f]+))?\]', op)
            if not mm:
                continue
            off = cur + int(mm.group(1) or '0', 16)
            dst = op.split(',')[0]
            rw = 'W' if 'es:[' in dst and ins.mnemonic in ('mov', 'add', 'sub', 'inc', 'dec', 'or',
                                                          'and', 'xor', 'neg', 'not', 'shl', 'shr') \
                else 'R'
            if ins.mnemonic in ('inc', 'dec', 'add', 'sub', 'or', 'and') and 'es:[' in dst:
                rw = 'M'
            size = 'b' if 'byte' in op else 'w'
            out.write('%05x %s %04x%s  %s %s\n' % (ins.address, rw, off, size, ins.mnemonic, op))
            s = summary[(off, size)]
            s[p] = ''.join(sorted(set(s.get(p, '') + rw)))
        out.close()
        print('%-7s ptrs %s' % (p, ' '.join('%04x->%04x' % kv for kv in ptrs.items())))
    with open('work/s12/comm_summary.txt', 'w') as f:
        for (off, size), s in sorted(summary.items()):
            f.write('%04x%s  %s\n' % (off, size, '  '.join('%s:%s' % kv for kv in sorted(s.items()))))
    print('%d distinct offsets -> work/s12/comm_summary.txt' % len(summary))


if __name__ == '__main__':
    main()
