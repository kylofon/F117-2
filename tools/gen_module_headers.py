"""Generate the module interface headers f117port/src/gen/module_<spec>.h from port/symbols.csv (run after
tools/merge_symbols.py and tools/gen_symbols.py). One header per port module, named after its spec: platform,
video, sound, frontend, shell, simflow, render3d, flight, debrief (the spec column of the CSV).

Each live function whose spec is the module gets one prototype. Its name is the C name of tools/gen_symbols.py
(the same identifier as in symbols_<image>.h); a name that several images of the module share with different
signatures gets the image as a suffix. The comment above it gives the image, the original address and the spec
section. Signatures come from the CSV's type column when that is plain C (types below, and the parameter names
are the column's names); otherwise the prototype is

    void name(void); /* TODO signature: spec <module>.md "<section>" (type column: "...") */

A name that several modules document (each spec describes its own copy of a routine) is declared once, in the
first module in MODULES order that has a plain C signature for it; the other modules say so in a comment. Header
clashes between modules are therefore impossible by construction.

A TODO is fixed in the spec's symbols CSV (port/spec/<module>_symbols.csv, the type column), then
tools/merge_symbols.py and this script run again. Dead functions are not ported and are listed in comments at the
end of their module. Labels are not functions and are not listed here (see symbols_<image>.h).

--check regenerates in memory and fails (exit 1) if a file in f117port/src/gen differs or is missing.
The ctest "gen_module_headers_current" runs it.

usage: python tools/gen_module_headers.py [--check]
"""
import os, re, sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_symbols import (RESERVED, assign_names, cmt, emit, is_dead, load, parse_addr,  # noqa: E402
                         spec_section, ident)

MODULES = ['platform', 'video', 'sound', 'frontend', 'shell', 'simflow', 'render3d', 'flight', 'debrief']
# Plain C types a prototype may use. Everything else (registers, near/far, pascal, varargs, struct names) is a TODO.
CTYPE_WORDS = {'void', 'bool', 'char', 'int', 'unsigned', 'long', 'short', 'signed', 'u8', 'u16', 'u32', 's8', 's16', 's32',
               'u64', 's64', 'size_t', 'FILE', 'const'}


def c_type(t):
    """A normalised C type ('const char *', 'u16', 'void *') for a plain type string, or None."""
    toks = t.replace('*', ' * ').split()
    stars = toks.count('*')
    words = [w for w in toks if w != '*']
    if not words or stars > 2 or any(w not in CTYPE_WORDS for w in words) or all(w == 'const' for w in words):
        return None
    return ' '.join(words) + (' ' + '*' * stars if stars else '')


def parse_sig(t):
    """(return type, [(type, name)]) for a type-column string such as 'void(int code)' or 'void *(unsigned n)',
    or None when it is not a plain C signature."""
    s = re.sub(r'/\*.*?\*/', ' ', t).strip()
    m = re.fullmatch(r'(.*?)\s*\((.*)\)', s)
    ret_s, plist = (m.group(1), m.group(2).strip()) if m else (s, 'void')
    ret = c_type(ret_s)
    if ret is None:
        return None
    if plist in ('', 'void'):
        return ret, []
    params = []
    for i, p in enumerate(plist.split(',')):
        p = p.strip()
        whole = c_type(p)                               # an unnamed parameter: 'int'
        if whole is not None:
            params.append((whole, 'a%d' % (i + 1)))
            continue
        toks = p.replace('*', ' * ').split()
        if len(toks) < 2 or not re.fullmatch(r'[A-Za-z_]\w*', toks[-1]) or toks[-1] in CTYPE_WORDS:
            return None
        typ = c_type(' '.join(toks[:-1]))
        if typ is None:
            return None
        params.append((typ, toks[-1] + ('_' if toks[-1] in RESERVED else '')))
    return ret, params


def decl(ret, name, params):
    ps = ', '.join('%s%s%s' % (t, '' if t.endswith('*') else ' ', n) for t, n in params) or 'void'
    return '%s%s%s(%s);' % (ret, '' if ret.endswith('*') else ' ', name, ps)


def split_entry(r, cname, base, rows):
    """One prototype (typed, or a TODO) for the rows that share cname within this module."""
    sig = parse_sig(r['type'])
    if sig is None:
        proto = 'void %s(void); /* TODO signature: spec %s.md "%s" (type column: "%s") */' % (
            cname, r['spec'], cmt(spec_section(r, *parse_addr(r)) or '(no heading)', 40), cmt(r['type'], 40))
        return {'owner': r['owner'], 'cname': cname, 'base': base, 'rows': rows, 'proto': proto, 'todo': True}
    return {'owner': r['owner'], 'cname': cname, 'base': base, 'rows': rows,
            'proto': decl(sig[0], cname, sig[1]), 'todo': False}


def sig_key(r):
    sig = parse_sig(r['type'])
    return ('SIG', repr(sig)) if sig is not None else ('TODO', r['type'])


def module_header(mod, rows, names, home, stats):
    funcs = [r for r in rows if r['spec'] == mod and r['kind'] == 'func' and r['_row'] in names]
    live = [r for r in funcs if not is_dead(r)]
    dead = [r for r in funcs if is_dead(r)]
    by_name = defaultdict(list)
    for r in live:
        by_name[names[r['_row']]].append(r)

    entries = []
    for cname, rs in by_name.items():
        if len({sig_key(r) for r in rs}) > 1:          # same name, different objects in this module: image suffix
            for r in rs:
                entries.append(split_entry(r, '%s_%s' % (cname, ident(r['exe'])), cname, [r]))
        else:
            entries.append(split_entry(rs[0], cname, cname, rs))

    head = ['/* Generated by tools/gen_module_headers.py from port/symbols.csv (module %s, spec port/spec/%s.md).'
            % (mod, mod),
            ' * Do not edit: fix the type column in the spec symbols CSV, then run tools/merge_symbols.py,'
            ' tools/gen_symbols.py and this script',
            ' * (f117port/PORTING.md, "Symbol pipeline"). One prototype per live function of the module; the comment'
            ' gives the image,',
            ' * the original address and the spec section. A TODO means the type column is not a plain C signature'
            ' yet.',
            ' * Dead functions are not ported and are listed at the end. */',
            '#pragma once', '#include "types.h"']
    body = []
    last_owner = None
    for e in sorted(entries, key=lambda e: (e['owner'], e['cname'])):
        if e['owner'] != last_owner:
            body.append('')
            body.append('/* ---- owner %s */' % cmt(e['owner']))
            last_owner = e['owner']
        r0 = e['rows'][0]
        where = '; '.join('%s %04x:%04x' % (r['exe'].upper(), *parse_addr(r)) for r in e['rows'])
        sec = spec_section(r0, *parse_addr(r0))
        h = home.get(e['base'], mod)
        if h != mod:
            body.append('/* %s  spec %s.md "%s": same name as module_%s.h, declared there */'
                        % (where, mod, sec or '(no heading)', h))
            continue
        body.append('/* %s  spec %s.md "%s" */' % (where, mod, sec or '(no heading)'))
        body.append(e['proto'])
        stats['todo'] += e['todo']
        stats['protos'] += 1
    if dead:
        body.append('')
        body.append('/* ---- dead: not ported (listed for the record) */')
        for r in sorted(dead, key=lambda r: (r['exe'], parse_addr(r))):
            body.append('/* DEAD %s %04x:%04x %s */' % (r['exe'].upper(), *parse_addr(r), names[r['_row']]))
    stats['dead'] += len(dead)
    if re.search(r'\bFILE\b', '\n'.join(body)):
        head.append('#include <stdio.h>')
    return '\n'.join(head + body) + '\n'


def main(argv):
    check = '--check' in argv
    rows = load()
    names, skipped, _ = assign_names(rows)
    live = defaultdict(set)                  # module -> C names of its live functions
    cand = defaultdict(list)                 # C name -> [(no plain signature, module index, module)]
    for r in rows:
        if r['spec'] in MODULES and r['kind'] == 'func' and r['_row'] in names and not is_dead(r):
            live[r['spec']].add(names[r['_row']])
            cand[names[r['_row']]].append((parse_sig(r['type']) is None, MODULES.index(r['spec']), r['spec']))
    home = {n: min(c)[2] for n, c in cand.items()}
    shared = {n for n in cand if len({m for _, _, m in cand[n]}) > 1}
    files, lines = {}, []
    for mod in MODULES:
        stats = defaultdict(int)
        files['module_%s.h' % mod] = module_header(mod, rows, names, home, stats)
        lines.append('  %-9s module_%s.h: %3d prototypes (%d TODO signatures), %d dead listed'
                     % (mod, mod, stats['protos'], stats['todo'], stats['dead']))
    problems = emit(files, 'module_', check)
    print('gen_module_headers: %d headers in f117port/src/gen%s' % (len(files), ' (check)' if check else ''))
    print('\n'.join(lines))
    print('names documented by several modules: %d (each declared once, in the first module with a plain C'
          ' signature)' % len(shared))
    unknown = sorted({r['spec'] for r in rows} - set(MODULES))
    if unknown:
        print('  rows with a spec that is no module (not emitted): %s' % ', '.join(unknown))
        skipped = skipped + [(r, 'spec %r is no module' % r['spec']) for r in rows if r['spec'] in unknown]
    print('skipped rows: %d' % len(skipped))
    for r, why in skipped:
        print('  SKIPPED csv line %d (%s %s %s): %s' % (r['_line'], r['exe'], r['kind'], r['name'], why))
    for p in problems:
        print('  CHECK FAILED: %s (run: python tools/gen_module_headers.py)' % p)
    return 1 if (skipped or problems) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
