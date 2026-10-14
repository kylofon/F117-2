"""S5.2: reference output of the Phase 2 Python decoders for `f117 --decoder-test` (ctest `video_decoders`).

Decodes every .PIC (tools/picdec.py), reads every .PAL, plays every .PAN (tools/panlib.py: palette + a CRC32 of the 64000-byte
screen after each cel) and dumps the MGRAPHIC fonts (the parsing of tools/mgfont.py, which cannot be imported: it runs a
main() on import), writes them to work/s52/ref and runs the port's decoders against them.

usage: python port/notes/s52/decoder_ref.py <f117 executable> <game dir> [ref dir]
exit 77 (ctest SKIP) when the game folder has no MGRAPHIC.EXE.
"""
import os, struct, subprocess, sys, zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import picdec, panlib  # noqa: E402


def fonts(game):
    d = open(os.path.join(game, 'MGRAPHIC.EXE'), 'rb').read()
    img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
    n = struct.unpack_from('<H', img, 0xd0)[0]
    out = []
    for i in range(n):
        p = struct.unpack_from('<H', img, 0xd2 + 2 * i)[0]
        first, last, fixed, h, sp, extra = img[p - 8], img[p - 7], img[p - 5], img[p - 4], img[p - 3], img[p - 2]
        cnt = last - first + 1
        wid = None if fixed else list(img[p - 9 - (last - first):p - 9 + 1])
        out.append((i, p, first, last, fixed, h, sp, extra, cnt, wid))
    return img, out


def main():
    exe, game = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    ref = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else os.path.join(ROOT, 'work', 's52', 'ref')
    if not os.path.isfile(os.path.join(game, 'MGRAPHIC.EXE')):
        print('no game files in', game)
        return 77
    os.makedirs(ref, exist_ok=True)
    index = []
    names = sorted(os.listdir(game), key=str.upper)
    for f in names:
        stem, ext = os.path.splitext(f)
        ext = ext.upper()
        path = os.path.join(game, f)
        if ext == '.PIC':
            px, nib = picdec.decode(path)
            assert len(px) == 64000, f
            open(os.path.join(ref, stem + '.pix'), 'wb').write(px)
            index.append('pic ' + stem)
        elif ext == '.PAL':
            d = open(path, 'rb').read()
            if len(d) >= 768:
                open(os.path.join(ref, stem + '.pal'), 'wb').write(d[:768])
                index.append('pal ' + stem)
        elif ext == '.PAN':
            pan = panlib.load_pan(path)
            rec = struct.pack('<HH', pan['speed'], pan['n']) + pan['pal']
            for i, scr in panlib.frames(pan):
                rec += struct.pack('<I', zlib.crc32(scr))
            open(os.path.join(ref, stem + '.pan'), 'wb').write(rec)
            index.append('pan ' + stem)
    img, fl = fonts(game)
    lines = []
    for (i, p, first, last, fixed, h, sp, extra, cnt, wid) in fl:
        lines.append('F %d %d %d %d %d %d %d' % (i, first, last, fixed, h, sp, extra))
        if wid:
            lines.append('W ' + ' '.join(map(str, wid)))
        for c in range(first, last + 1):
            rows = [img[p + r * cnt + (c - first)] for r in range(h + extra)]
            lines.append('G %d ' % c + ' '.join('%02x' % b for b in rows))
    open(os.path.join(ref, 'fonts.txt'), 'w').write('\n'.join(lines) + '\n')
    index.append('fonts')
    open(os.path.join(ref, 'index.txt'), 'w').write('\n'.join(index) + '\n')
    print('decoder_ref: %d pic, %d pal, %d pan, fonts -> %s' % (
        sum(l.startswith('pic') for l in index), sum(l.startswith('pal') for l in index), sum(l.startswith('pan') for l in index), ref))
    return subprocess.call([exe, '--headless', '--decoder-test', ref, '--game-dir', game])


if __name__ == '__main__':
    sys.exit(main())
