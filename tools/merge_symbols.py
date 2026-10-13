"""Merge port/spec/*_symbols.csv into port/symbols.csv (all F-117A EXEs) and report conflicts.

Row key: (exe, kind, address). kind is `func` (address SSSS:OOOO, image-relative segment:offset of that exe) or
`global` (DS:xxxx in that exe's DGROUP, or SSSS:OOOO for a code/far-data segment variable; `data`
is accepted as a synonym). When several specs name the same key, the owner with the highest
priority wins (a module's own name beats one given in passing by another module); ties go to the
first file in sorted order. The seed file (phase 1 names) has the lowest priority.

Writes port/symbols.csv (columns exe,kind,address,name,type,owner,other_names,notes,spec, where
other_names lists the losing names and spec is the file stem that won, e.g. `shell`, `seed`) and
work/coverage/symbol_clashes.txt:
  * duplicates: the same key listed by several spec files (the winner is written, the rest are listed),
  * the same key named differently by different specs,
  * the same name used for different addresses within one exe (gen_symbols.py disambiguates them;
    this is how a copied function such as a START twin in FRONTEND shows up).
Rows that are not an exe/kind/address/name tuple are listed as skipped.

usage: python tools/merge_symbols.py          (regenerates both files; safe to re-run)
"""
import csv, glob, os, re
from collections import defaultdict

EXES = ('vgame', 'start', 'end', 'setup', 'player', 'dswap', 'mps_logo', 'misc', 'mgraphic', 'f117',
        'asound117', 'asound.log')  # ASOUND.117 / ASOUND.LOG: the sound driver images (sound.md)
KINDS = ('func', 'global', 'label')  # label = code address inside a function (not a call target)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(ROOT, 'port', 'spec')

# Lower number = higher priority. File stems are matched by their module prefix.
PRIORITY = ['platform', 'video', 'sound', 'render3d', 'flight', 'simflow', 'shell', 'frontend', 'debrief']


def prio(stem):
    """Lower wins. Listed specs in PRIORITY order, then unknown stems, then the phase-1 seed names last."""
    if stem == 'seed':
        return 10 ** 6
    for i, p in enumerate(PRIORITY):
        if stem == p or stem.startswith(p + '_'):
            return i
    return len(PRIORITY)


def norm_addr(kind, a):
    a = a.strip().replace('0x', '').replace('0X', '')
    m = re.fullmatch(r'(?i)DS:([0-9A-F]{1,4})', a)
    if m:
        return 'DS:%04X' % int(m.group(1), 16)
    m = re.fullmatch(r'(?i)([0-9A-F]{1,4}):([0-9A-F]{1,4})', a)
    if m:
        return '%04x:%04x' % (int(m.group(1), 16), int(m.group(2), 16))
    return None


def main():
    entries = defaultdict(list)  # key -> [(prio, stem, row)]
    skipped = []
    for path in sorted(glob.glob(os.path.join(SPEC, '*_symbols.csv'))):
        stem = os.path.basename(path)[:-len('_symbols.csv')]
        with open(path, newline='', encoding='utf-8', errors='replace') as fh:
            for n, r in enumerate(csv.DictReader(fh), 2):
                exe = (r.get('exe') or '').strip().lower()
                kind = (r.get('kind') or '').strip().lower()
                kind = {'data': 'global', 'function': 'func', 'var': 'global', 'slot': 'func'}.get(kind, kind)  # MGRAPHIC slot = API entry
                name = re.sub(r'\W', '_', (r.get('name') or '').strip())
                addr = norm_addr(kind, r.get('address') or '')
                if exe not in EXES or kind not in KINDS or not addr or not name:
                    skipped.append('%s:%d %s' % (os.path.basename(path), n, ','.join((r.get(k) or '') for k in ('exe', 'kind', 'address', 'name'))))
                    continue
                if kind == 'func' and addr.startswith('DS:'):
                    skipped.append('%s:%d func with DS address %s' % (os.path.basename(path), n, addr))
                    continue
                entries[(exe, kind, addr)].append((prio(stem), stem, r, name))

    out, conflicts, dups = [], [], []
    byname = defaultdict(set)
    for key in sorted(entries, key=lambda k: (k[0], k[1], k[2])):
        cands = sorted(entries[key], key=lambda c: c[0])
        p, stem, r, name = cands[0]
        others = sorted({c[3] for c in cands[1:] if c[3] != name and c[1] != 'seed'} |
                        {c[3] for c in cands[1:] if c[3] != name and c[1] == 'seed'})
        owner = (r.get('owner') or '').strip() or stem
        if stem == 'seed':
            owner = (r.get('owner') or 'seed').strip()
        if len(cands) > 1:
            dups.append('%s %s %s: winner %s; also %s' % (key[0], key[1], key[2], stem,
                                                       ', '.join(c[1] for c in cands[1:])))
        if len({c[3] for c in cands if c[1] != 'seed'}) > 1:
            conflicts.append('%s %s %s: %s' % (key[0], key[1], key[2], '; '.join('%s=%s' % (c[1], c[3]) for c in cands)))
        notes = (r.get('notes') or '').strip()
        out.append({'exe': key[0], 'kind': key[1], 'address': key[2], 'name': name,
                    'type': (r.get('type') or '').strip(), 'owner': owner,
                    'other_names': ' '.join(others), 'notes': notes, 'spec': stem})
        byname[(key[0], name)].add(key[2])

    for (exe, name), addrs in sorted(byname.items()):
        if len(addrs) > 1:
            conflicts.append('%s name %s used for %s' % (exe, name, ' '.join(sorted(addrs))))

    fields = ['exe', 'kind', 'address', 'name', 'type', 'owner', 'other_names', 'notes', 'spec']
    with open(os.path.join(ROOT, 'port', 'symbols.csv'), 'w', newline='', encoding='utf-8') as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(out)
    os.makedirs(os.path.join(ROOT, 'work', 'coverage'), exist_ok=True)
    with open(os.path.join(ROOT, 'work', 'coverage', 'symbol_clashes.txt'), 'w', encoding='utf-8') as fh:
        fh.write('# name clashes (different names or addresses for one key, or one name at several addresses)\n')
        fh.write('\n'.join(conflicts) + '\n\n# duplicate keys (listed in several spec files; winner = lowest priority number)\n')
        fh.write('\n'.join(dups) + '\n\n# skipped rows\n' + '\n'.join(skipped) + '\n')
    counts = defaultdict(int)
    for o in out:
        counts[(o['exe'], o['kind'])] += 1
    print('symbols: %d rows (%s); %d clash lines, %d duplicate keys, %d skipped rows -> work/coverage/symbol_clashes.txt' % (
        len(out), ', '.join('%s %s %d' % (e, k, n) for (e, k), n in sorted(counts.items())), len(conflicts), len(dups), len(skipped)))


if __name__ == '__main__':
    main()
