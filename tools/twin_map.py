"""Map uncovered functions of the F-117A programs to their twins in another program (S3.10).

A twin is a function whose code is an exact or near-identical copy of a function that a spec already
covers in another program (the same compiler RTL and MicroProse library are linked into every EXE, and
the front end shares the START UI toolkit). Matching works on instruction shape: every immediate,
displacement and branch target is masked, so relocated segments and DGROUP offsets do not matter.
Register and operand structure must still agree.

  exact  : the masked instruction sequences are equal
  near   : difflib ratio >= NEAR_RATIO over the masked sequences, same length within 15 %, among
           candidates that share at least half of their 6-instruction windows

Partners are only functions that a spec covers with a real spec row (not the phase-1 seed names).
Accepted twins are appended as `func` rows to the owning spec CSV (frontend_symbols.csv for SETUP,
MPS_LOGO, PLAYER, DSWAP; shell_symbols.csv for START), with the partner's name and signature and a
note `(= EXE SEG:OFF ...)`, the same way debrief_symbols.csv maps END's START-toolkit copies. Only
addresses that are still uncovered get a row, so re-running is safe.

Writes work/coverage/twins.csv (every uncovered function: status, partner, alternatives, callers) and
work/coverage/remaining.txt (functions still missing after mapping: address, size, first callers, best
non-accepted partner).

usage: python tools/twin_map.py [--apply]     (without --apply: report only, nothing is written to port/spec)
"""
import csv, difflib, glob, os, re, struct, sys
from collections import defaultdict
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(ROOT, 'port', 'spec')
COV = os.path.join(ROOT, 'work', 'coverage')

PROGS = ['setup', 'mps_logo', 'player', 'start', 'dswap', 'vgame', 'end']
TARGETS = ['setup', 'mps_logo', 'player', 'start', 'dswap']  # programs with uncovered functions
EXE_FILE = {p: p.upper().replace('MPS_LOGO', 'MPS_LOGO') + '.EXE' for p in PROGS}
TARGET_SPEC = {'setup': 'frontend', 'mps_logo': 'frontend', 'player': 'frontend', 'dswap': 'frontend',
               'start': 'shell'}
NEAR_RATIO = 0.9
GRAM = 6
NUM = re.compile(r'0x[0-9a-f]+|\b\d+\b')
md = Cs(CS_ARCH_X86, CS_MODE_16)
md.detail = False


def norm(a):
    a = a.strip().lower().replace('0x', '')
    s, o = a.split(':', 1)
    return int(s, 16), int(o, 16)


def fmt(seg, off):
    return '%04x:%04x' % (seg, off)


def load_image(prog):
    d = open(os.path.join(ROOT, 'work', 'unpacked', EXE_FILE[prog]), 'rb').read()
    return d[struct.unpack_from('<H', d, 8)[0] * 16:]


def covered_rows():
    """(exe, seg, off) -> row dict, from every spec file except the phase-1 seed names."""
    rows = {}
    for path in sorted(glob.glob(os.path.join(SPEC, '*_symbols.csv'))):
        stem = os.path.basename(path)[:-len('_symbols.csv')]
        with open(path, newline='', encoding='utf-8', errors='replace') as fh:
            for r in csv.DictReader(fh):
                if (r.get('kind') or '').strip().lower() not in ('func', 'function'):
                    continue
                exe = (r.get('exe') or '').strip().lower()
                try:
                    s, o = norm(r.get('address') or '')
                except ValueError:
                    continue
                r = dict(r, _spec=stem)
                if stem == 'seed':
                    rows.setdefault((exe, s, o, 'seed'), r)
                else:
                    rows.setdefault((exe, s, o), r)
    return rows


def covered_any(rows, exe, s, o):
    return (exe, s, o) in rows or (exe, s, o, 'seed') in rows


def masked_text(i):
    return NUM.sub('K', i.mnemonic + ' ' + i.op_str)


def main():
    apply = '--apply' in sys.argv[1:]
    rows = covered_rows()
    funcs, texts, calls = {}, {}, defaultdict(list)
    for prog in PROGS:
        img = load_image(prog)
        fl = list(csv.DictReader(open(os.path.join(ROOT, 'port', prog + '_functions.csv'), newline='')))
        for r in fl:
            s, o = norm(r['start'])
            size = int(r['size'])
            funcs[(prog, s, o)] = {'size': size, 'source': r.get('source', ''), 'strings': r.get('strings', '')}
            code = img[s * 16 + o:s * 16 + o + size]
            ins = list(md.disasm(code, o))
            texts[(prog, s, o)] = [masked_text(i) for i in ins]
            for i in ins:
                if i.mnemonic.startswith('call') and i.op_str and not i.op_str.startswith('word') \
                        and 'ptr' not in i.op_str and '[' not in i.op_str:
                    m = re.fullmatch(r'(?:0x)?([0-9a-f]+)(?::(?:0x)?([0-9a-f]+))?', i.op_str.strip())
                    if not m:
                        continue
                    if m.group(2):  # far call seg:off
                        tgt = (int(m.group(1), 16), int(m.group(2), 16))
                    else:
                        tgt = (s, int(m.group(1), 16))
                    calls[(prog,) + tgt].append((prog, s, o))

    def is_cov(prog, s, o):  # target side: any row counts, as in spec_coverage.py
        return covered_any(rows, prog, s, o)

    def is_partner(k):  # partner side: only real spec rows, never the phase-1 seed names
        return k in rows

    # gram index over every function, for the near prefilter
    grams = {k: set(tuple(v[i:i + GRAM]) for i in range(max(1, len(v) - GRAM + 1))) for k, v in texts.items()}
    exact_idx = defaultdict(list)
    for k, v in texts.items():
        exact_idx[tuple(v)].append(k)

    def find_partner(key, pred):
        """Exact partner first, then the best near partner >= NEAR_RATIO (ratio, status, partner, alts)."""
        ex = [k for k in exact_idx[tuple(texts[key])] if k[0] != key[0] and pred(k)]
        if ex:
            return 1.0, 'exact', ex[0], ex[1:]
        best = (0.0, None)
        gk = grams[key]
        for k in funcs:
            if k[0] == key[0] or not pred(k) or not gk:
                continue
            inter = len(gk & grams[k])
            if inter == 0 or inter / len(gk | grams[k]) < 0.5:
                continue
            a, b = texts[key], texts[k]
            if abs(len(a) - len(b)) > 0.15 * max(len(a), len(b)):
                continue
            r = difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()
            if r > best[0]:
                best = (r, k)
        ratio, pk = best
        if pk is not None and ratio >= NEAR_RATIO:
            return ratio, 'near', pk, []
        return ratio, 'none', None, []

    def partner_name(k):
        pr = rows.get(k)
        return (pr.get('name', ''), pr.get('type', ''), pr.get('_spec', '')) if pr else ('', '', '')

    results, remaining = [], []
    for prog in TARGETS:
        for (p, s, o), meta in sorted(funcs.items()):
            if p != prog or is_cov(p, s, o):
                continue
            key = (p, s, o)
            ratio, status, partner, alts = find_partner(key, is_partner)
            name, ptype, pspec = partner_name(partner) if partner else ('', '', '')
            cs = sorted(set(calls.get(key, [])))
            row = {'exe': p, 'addr': fmt(s, o), 'size': meta['size'], 'status': status,
                   'ratio': '%.3f' % ratio, 'partner_exe': partner[0] if partner else '',
                   'partner_addr': fmt(partner[1], partner[2]) if partner else '',
                   'partner_name': name, 'partner_type': ptype, 'partner_spec': pspec,
                   'alt': ' '.join(fmt(a[1], a[2]) + '/' + a[0] for a in alts),
                   'callers': ' '.join(fmt(c[1], c[2]) for c in cs[:3]),
                   'source': meta['source'], 'strings': meta['strings'],
                   'other_uncovered': ''}
            results.append(row)
            if status == 'none':
                # same code as an uncovered function elsewhere: one spec pass can cover both
                r2, st2, p2, _ = find_partner(key, lambda k: k in funcs)
                if p2 is not None:
                    row['other_uncovered'] = '%s %s (%s, %.3f)' % (p2[0], fmt(p2[1], p2[2]), st2, r2)
                remaining.append(row)

    os.makedirs(COV, exist_ok=True)
    cols = ['exe', 'addr', 'size', 'status', 'ratio', 'partner_exe', 'partner_addr', 'partner_name', 'partner_type',
            'partner_spec', 'alt', 'callers', 'other_uncovered', 'source', 'strings']
    with open(os.path.join(COV, 'twins.csv'), 'w', newline='', encoding='utf-8') as fh:
        w = csv.DictWriter(fh, fieldnames=cols)
        w.writeheader()
        w.writerows(results)

    accepted = [r for r in results if r['status'] in ('exact', 'near')]
    with open(os.path.join(COV, 'remaining.txt'), 'w', encoding='utf-8') as fh:
        fh.write('# functions still missing after twin mapping (tools/twin_map.py); callers = first call sites\n')
        for r in remaining:
            fh.write('%-9s %s %5s  callers: %-24s same code as uncovered: %s\n' % (
                r['exe'], r['addr'], r['size'], r['callers'] or '-', r['other_uncovered'] or '-'))

    if apply:
        added = defaultdict(list)
        for r in accepted:
            p = r['exe']
            spec = TARGET_SPEC[p]
            added[spec].append(r)
        for spec, items in added.items():
            path = os.path.join(SPEC, spec + '_symbols.csv')
            with open(path, newline='', encoding='utf-8') as fh:
                reader = csv.DictReader(fh)
                fieldnames = reader.fieldnames
                existing = list(reader)
            have = set()
            for r in existing:
                try:
                    have.add((r['exe'], norm(r['address'])))
                except ValueError:  # DS:xxxx data rows are not code addresses
                    pass
            new = []
            for r in items:
                s, o = norm(r['addr'])
                if (r['exe'], (s, o)) in have or covered_any(rows, r['exe'], s, o):
                    continue
                pn = r['partner_name'] or ('(unnamed)')
                note = '(= %s %s, %s copy%s)' % (r['partner_exe'].upper(), r['partner_addr'],
                                                 'byte-shape' if r['status'] == 'exact' else 'near',
                                                 '' if r['status'] == 'exact' else ' %s' % r['ratio'])
                if r['partner_spec'] and r['partner_spec'] != 'platform':
                    note = note[:-1] + '; partner in %s_symbols.csv)' % r['partner_spec']
                new.append({'exe': r['exe'], 'kind': 'func', 'address': r['addr'], 'name': pn,
                            'type': r['partner_type'], 'owner': spec, 'other_names': '',
                            'notes': note})
            if new:
                eol = '\r\n' if b'\r\n' in open(path, 'rb').read() else '\n'  # keep the file's own line endings
                with open(path, 'a', newline='', encoding='utf-8') as fh:
                    w = csv.DictWriter(fh, fieldnames=fieldnames, lineterminator=eol)
                    for n in new:
                        w.writerow({k: n.get(k, '') for k in fieldnames})
                print('%s_symbols.csv: +%d twin rows' % (spec, len(new)))

    counts = defaultdict(lambda: defaultdict(int))
    for r in results:
        counts[r['exe']][r['status']] += 1
    for p in TARGETS:
        c = counts[p]
        print('%-9s uncovered %3d: exact %3d, near %3d, none %3d' % (p, sum(c.values()), c['exact'], c['near'], c['none']))
    print('-> work/coverage/twins.csv, work/coverage/remaining.txt')


if __name__ == '__main__':
    main()
