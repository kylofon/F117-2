"""Compact per-function disassembly of an unpacked EXE using the function index.
usage: python tools/fndis.py EXE FROM TO   (hex offsets in segment 0000, e.g. END 3e45 4dd2)
Writes to stdout: '== start size ints strings' header per function, then 'off  insn' lines."""
import csv, os, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
exe = sys.argv[1].upper(); lo = int(sys.argv[2], 16); hi = int(sys.argv[3], 16)
d = open(os.path.join(ROOT, 'work', 'unpacked', exe + '.EXE'), 'rb').read()
img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
md = Cs(CS_ARCH_X86, CS_MODE_16)
for r in csv.DictReader(open(os.path.join(ROOT, 'port', exe.lower() + '_functions.csv'))):
    seg, off = (int(x, 16) for x in r['start'].split(':'))
    if seg != 0 or not lo <= off < hi:
        continue
    print('== %04x %s ints=%s ports=%s %s' % (off, r['size'], r['ints'], r['ports'], r['strings'][:60]))
    for i in md.disasm(img[off:off + int(r['size'])], off):
        print('%04x %s %s' % (i.address, i.mnemonic, i.op_str))
