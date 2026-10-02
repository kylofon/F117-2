"""First names for shared functions: groups in port/shared_matches.csv (tools/apmatch.py) whose only
DOS/BIOS call is a single well-known service get a `dos_*`/`bios_*` name. Writes
port/spec/seed_symbols.csv (lowest-priority seed, read by tools/merge_symbols.py).

usage: seed_names.py   (then merge_symbols.py, apmatch.py)
"""
import csv, os, re
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXES = ['vgame', 'start', 'end', 'setup', 'player', 'dswap', 'mps_logo']
SVC = {'21.25': 'dos_setvect', '21.35': 'dos_getvect', '21.30': 'dos_getversion', '21.3c': 'dos_creat',
       '21.3d': 'dos_open', '21.3e': 'dos_close', '21.3f': 'dos_read', '21.40': 'dos_write',
       '21.41': 'dos_unlink', '21.42': 'dos_lseek', '21.43': 'dos_chmod', '21.44': 'dos_ioctl',
       '21.48': 'dos_allocmem', '21.49': 'dos_freemem', '21.4a': 'dos_setblock', '21.4c': 'dos_exit',
       '21.09': 'dos_puts', '21.0b': 'dos_kbhit', '33.00': 'mouse_reset', '1a': 'bios_timer',
       '16': 'bios_keyboard', '10.10': 'bios_vga_palette', '10.00': 'bios_setmode'}

rows = list(csv.DictReader(open(os.path.join(ROOT, 'port', 'shared_matches.csv'))))
count = defaultdict(int)
out = []
for r in rows:
    m = re.match(r'int ([0-9a-f. ]+?)(?:;|$)', r['hint'])
    if not m or len(m.group(1).split()) != 1 or int(r['insns']) > 120:
        continue
    base = SVC.get(m.group(1))
    if not base:
        continue
    count[base] += 1
    name = base if count[base] == 1 else '%s_%d' % (base, count[base])
    for e in EXES:
        if r[e]:
            out.append(dict(exe=e, kind='func', address=r[e], name=name, type='', owner='seed',
                            notes='RTL/lib, single %s call' % m.group(1)))
os.makedirs(os.path.join(ROOT, 'port', 'spec'), exist_ok=True)
with open(os.path.join(ROOT, 'port', 'spec', 'seed_symbols.csv'), 'w', newline='', encoding='utf-8') as fh:
    w = csv.DictWriter(fh, ['exe', 'kind', 'address', 'name', 'type', 'owner', 'other_names', 'notes'])
    w.writeheader()
    for o in out:
        w.writerow(o)
print('%d seed rows, %d names' % (len(out), len(count)))
