"""Cross references for VGAME from the function index (port/vgame_functions.json).
usage: python tools/vgxref.py g E548 [F2E4 ...]   who reads / writes each DGROUP offset (direct [imm] accesses)
       python tools/vgxref.py f 1E9E [...]         callers, callees, DGROUP offsets read/written of a function
       python tools/vgxref.py name PATTERN          functions whose name (port/symbols.csv + spec csv) matches
Addresses: segment-0 offsets as hex ('1e9e'), others as 'SSSS:OOOO'. Prints names where known."""
import csv, glob, json, os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = json.load(open(os.path.join(ROOT, 'port', 'vgame_functions.json')))
F = {f['start']: f for f in d['functions']}
NAMES = {}
for p in [os.path.join(ROOT, 'port', 'symbols.csv')] + sorted(glob.glob(os.path.join(ROOT, 'port', 'spec', '*_symbols.csv'))) + \
        sorted(glob.glob(os.path.join(ROOT, 'work', 's36', 'syms_*.csv'))):
    try:
        for r in csv.DictReader(open(p, newline='', encoding='utf-8', errors='replace')):
            if (r.get('exe') or '').lower() == 'vgame' and (r.get('kind') or '') == 'func' and r.get('name'):
                NAMES[r['address'].lower()] = r['name']
    except OSError:
        pass


def key(a):
    a = a.lower().replace('0x', '')
    if ':' not in a:
        a = '0000:%04x' % int(a, 16)
    s, o = a.split(':')
    return '%04x:%04x' % (int(s, 16), int(o, 16))


def lab(a):
    n = NAMES.get(a, '')
    return (a[5:] if a.startswith('0000:') else a) + (' ' + n if n else '')


def main():
    mode, args = sys.argv[1], sys.argv[2:]
    if mode == 'g':
        for g in args:
            g = '%04x' % int(g, 16)
            rd = [lab(a) for a, f in F.items() if g in f['ds_reads']]
            wr = [lab(a) for a, f in F.items() if g in f['ds_writes']]
            print('DS:%s  read by %d: %s' % (g, len(rd), ', '.join(rd)))
            print('        written by %d: %s' % (len(wr), ', '.join(wr)))
    elif mode == 'f':
        for a in args:
            a = key(a)
            f = F.get(a)
            if not f:
                print(a, 'not in index')
                continue
            print('%s size %d %s' % (lab(a), f['size'], 'FAR' if f['far'] else ''))
            print('  callers: ' + ', '.join(lab(x) for x in f['callers']))
            print('  calls:   ' + ', '.join(lab(x) for x in f['calls']))
            print('  ds_reads:  ' + ' '.join(f['ds_reads']))
            print('  ds_writes: ' + ' '.join(f['ds_writes']))
    elif mode == 'name':
        for a, n in sorted(NAMES.items()):
            if re.search(args[0], n):
                print(lab(a))


main()
