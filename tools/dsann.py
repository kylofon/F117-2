"""Annotate fndis output with DGROUP strings.
usage: python tools/dsann.py EXE DGROUP_SEG FROM TO   (hex; DGROUP seg from RE_GUIDE §2.3)
Runs tools/fndis.py and appends '; "text"' to every line whose immediate operand points at a
printable NUL-terminated string in DGROUP (len >= 3)."""
import os, re, struct, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
exe, dseg, lo, hi = sys.argv[1].upper(), int(sys.argv[2], 16), sys.argv[3], sys.argv[4]
d = open(os.path.join(ROOT, 'work', 'unpacked', exe + '.EXE'), 'rb').read()
img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
base = dseg * 16
out = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'fndis.py'), exe, lo, hi],
                     capture_output=True, text=True).stdout
for ln in out.splitlines():
    m = re.search(r'\b(?:mov [a-z]{2}|push|cmp [a-z]{2}), (0x[0-9a-f]+)$', ln)
    if m and not ln.startswith('=='):
        o = int(m.group(1), 16)
        s = img[base + o:base + o + 70].split(b'\0')[0]
        if len(s) >= 3 and all(32 <= c < 127 or c in (9, 10, 13) for c in s):
            ln += '   ; "%s"' % s.decode('latin1').replace('\n', '\n').replace('\r', '\r')
    print(ln)
