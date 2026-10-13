"""Annotate fndis output with DGROUP strings.
usage: python tools/dsann.py EXE DGROUP_SEG FROM TO   (hex; DGROUP seg from RE_GUIDE §2.3)
Also (S3.6): near call/jmp targets are masked to 16 bits (calls above 0x8000 printed as 0xffffXXXX),
call targets that have a name in port/symbols.csv or port/spec/*_symbols.csv get '; name', and far calls
into the DGROUP driver-slot table get '; slot N name' (table base per EXE below).
Runs tools/fndis.py and appends '; "text"' to every line whose immediate operand points at a
printable NUL-terminated string in DGROUP (len >= 3)."""
import csv, glob, os, re, struct, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
exe, dseg, lo, hi = sys.argv[1].upper(), int(sys.argv[2], 16), sys.argv[3], sys.argv[4]
d = open(os.path.join(ROOT, 'work', 'unpacked', exe + '.EXE'), 'rb').read()
img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
base = dseg * 16

TABLES = {'VGAME': 0x11A, 'START': 0x69B8, 'END': 0x23FA}
SLOTS = {}
try:
    for ln in open(os.path.join(ROOT, 'port', 'spec', 'video.md'), encoding='utf-8', errors='replace'):
        m = re.match(r'\| (\d+) \| [0-9a-f]{4} \| `(\w+)`', ln)
        if m:
            SLOTS[int(m.group(1))] = m.group(2)
except OSError:
    pass
SLOTS.update({90: 'kbd_empty', 91: 'kbd_get', 92: 'getche', 93: 'joy_button', 94: 'misc94', 95: 'misc95',
              100: 'snd_init', 101: 'snd_cmd', 102: 'snd_shutdown', 103: 'snd_poll', 104: 'snd_noise'})
NAMES = {}
for pth in [os.path.join(ROOT, 'port', 'symbols.csv')] + glob.glob(os.path.join(ROOT, 'port', 'spec', '*_symbols.csv')):
    try:
        for r in csv.DictReader(open(pth, newline='', encoding='utf-8', errors='replace')):
            if (r.get('exe') or '').lower() == exe.lower() and (r.get('kind') or '') == 'func' and r.get('name'):
                a = r['address'].lower()
                if ':' in a:
                    NAMES[(int(a.split(':')[0], 16), int(a.split(':')[1], 16))] = r['name']
    except OSError:
        pass


CURSEG = [0]


def annotate(ln):
    m = re.match(r'^[0-9a-f]+ (call|jmp|j\w+|loop\w*) (0x[0-9a-f]+)$', ln)
    if m:
        v = int(m.group(2), 16) & 0xFFFF
        ln = ln[:m.start(2)] + '0x%x' % v
        if m.group(1) == 'call' and (CURSEG[0], v) in NAMES:
            ln += '   ; ' + NAMES[(CURSEG[0], v)]
        return ln
    m = re.match(r'^[0-9a-f]+ lcall (0x[0-9a-f]+), (0x[0-9a-f]+)$', ln)
    if m:
        sg, of = int(m.group(1), 16), int(m.group(2), 16)
        lin = sg * 16 + of - dseg * 16        # segment 1d50 etc. address the same DGROUP thunk table
        if exe in TABLES and 0 <= lin - TABLES[exe] < 5 * 110 and (lin - TABLES[exe]) % 5 == 0:
            n = (lin - TABLES[exe]) // 5
            ln += '   ; slot %d %s' % (n, SLOTS.get(n, ''))
        elif (sg, of) in NAMES:
            ln += '   ; ' + NAMES[(sg, of)]
    return ln


out = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'fndis.py'), exe, lo, hi],
                     capture_output=True, text=True).stdout
for ln in out.splitlines():
    if ln.startswith('=='):
        h = ln.split()[1]
        CURSEG[0] = int(h.split(':')[0], 16) if ':' in h else 0
    if not ln.startswith('=='):
        ln = annotate(ln)
    m = re.search(r'\b(?:mov [a-z]{2}|push|cmp [a-z]{2}), (0x[0-9a-f]+)$', ln)
    if m and not ln.startswith('=='):
        o = int(m.group(1), 16)
        s = img[base + o:base + o + 70].split(b'\0')[0]
        if len(s) >= 3 and all(32 <= c < 127 or c in (9, 10, 13) for c in s):
            ln += '   ; "%s"' % s.decode('latin1').replace('\n', '\n').replace('\r', '\r')
    print(ln)
