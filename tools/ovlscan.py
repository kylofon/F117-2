"""Overlay drivers and the far-jump vector table (RE_GUIDE.md §2).
  ovlscan.py      -> work/s12/vectors.txt  (run tools/unlzexe.py first)
1. Each driver (MGRAPHIC.EXE, MISC.EXE, ?SOUND.117): header id, segments, sizes, slot base and the
   entry table (slot = base + i, target = code seg : offset).
2. Each game EXE: DGROUP, address of the bind routine, and how many `call far DGROUP:table+5*slot`
   sites use every slot.
"""
import collections, re, struct

DRIVERS = ['MGRAPHIC.EXE', 'MISC.EXE', 'ASOUND.117', 'RSOUND.117', 'ISOUND.117', 'NSOUND.117']
PROGS = ['SETUP', 'MPS_LOGO', 'PLAYER', 'START', 'VGAME', 'END', 'DSWAP']


def image(path):
    d = open(path, 'rb').read()
    return d, d[struct.unpack_from('<H', d, 8)[0] * 16:]


def dgroup(img, d):
    ip = struct.unpack_from('<H', d, 0x14)[0]
    e = struct.unpack_from('<H', d, 0x16)[0] * 16 + ip
    m = re.search(rb'\xbf(..)', img[e:e + 0x20], re.S)     # MSC startup: mov di, DGROUP
    return struct.unpack('<H', m.group(1))[0]


def main():
    out = open('work/s12/vectors.txt', 'w')
    for f in DRIVERS:
        d, img = image('Game/' + f)
        ident = img[:0x18].split(b'\0')[0].decode('latin1')
        cseg, dseg, base, size_a, size_b, n = struct.unpack_from('<6H', img, 0x18)
        ents = struct.unpack_from('<%dH' % n, img, 0x24)
        out.write('%s  id=%r codeseg=%04x dataseg=%04x base=%d sizeA=%04x sizeB=%04x n=%d\n'
                  % (f, ident, cseg, dseg, base, size_a, size_b, n))
        for i, o in enumerate(ents):
            out.write('  slot %3d  [%2d]  %04x:%04x\n' % (base + i, i, cseg, o))
        print('%-13s base %3d  n %2d  %s' % (f, base, n, ident))
    for p in PROGS:
        d, img = image('work/unpacked/%s.EXE' % p)
        dg = dgroup(img, d)
        dgb = struct.pack('<H', dg)
        m = re.search(rb'\xbb(..)\xbf\x1c\x00\x26\x8b\x05\xb2\x05', img, re.S)
        if not m:
            out.write('%s DGROUP=%04x no bind routine\n' % (p, dg))
            print('%-8s DGROUP %04x  no bind routine' % (p, dg))
            continue
        table, bind = struct.unpack('<H', m.group(1))[0], m.start()
        use = collections.Counter()
        for m in re.finditer(rb'\x9a(..)' + re.escape(dgb), img, re.S):
            o = struct.unpack('<H', m.group(1))[0]
            if o >= table and (o - table) % 5 == 0 and o < table + 5 * 110:
                use[(o - table) // 5] += 1
        ok = img[dg * 16 + table] == 0xea
        out.write('%s DGROUP=%04x table@%04x:%04x %s bind@%05x\n  slots used: %s\n'
                  % (p, dg, dg, table, 'jmpfar-stubs' if ok else 'NO STUBS', bind,
                     ' '.join('%d:%d' % kv for kv in sorted(use.items()))))
        print('%-8s DGROUP %04x  table %04x  stubs %-3s  slots used %3d  calls %4d' % (
            p, dg, table, 'yes' if ok else 'no', len(use), sum(use.values())))


if __name__ == '__main__':
    main()
