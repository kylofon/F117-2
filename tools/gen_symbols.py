"""Generate f117port/src/gen/symbols_<prog>.h from port/symbols.csv (run after tools/merge_symbols.py).

One header per image in the CSV's exe column: the seven programs of F117.COM (setup, mps_logo, player, start,
vgame, end, dswap) and the driver images the chain loads (asound117 = sound driver, mgraphic, misc, asound.log =
MPS_LOGO's sound driver). Per header, with P = the image name in capitals (ASOUND_LOG for asound.log):

  DGROUP globals      #define P_DS_<name> 0xOOOO        /* type; owner; spec */   (DSB/DSW/DSS/DSL, mem.h)
  code-segment vars   #define P_CS_<name> 0xOOOO         and  #define P_CSSEG_<name> 0xSSSS
  functions           commented index: address, C name, owner, spec file and section; DEAD marked
  code labels         commented index (jump targets inside functions, not prototypes)

Prototypes of the functions are in gen/module_<spec>.h (tools/gen_module_headers.py).

Names: one namespace per image. A name used at several addresses of the image (any kind) gets the address as a
suffix: _OOOO for a DGROUP offset, _SSSS_OOOO for a segment:offset. A name that is not a C identifier gets a
leading underscore; a function whose name is a C keyword or a common libc name gets _fn. Every rename is printed.
Nothing is dropped: a row that cannot be emitted is printed as SKIPPED with its reason, and the run fails.

--check regenerates everything in memory and fails (exit 1) if a file in f117port/src/gen differs or is missing.
The ctest "gen_symbols_current" runs it.

usage: python tools/gen_symbols.py [--check]     (from any directory)
"""
import csv, os, re, sys
from collections import Counter, defaultdict
from functools import lru_cache

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.path.join(ROOT, 'port', 'symbols.csv')
SPEC_DIR = os.path.join(ROOT, 'port', 'spec')
GEN_DIR = os.path.join(ROOT, 'f117port', 'src', 'gen')
PROG_ORDER = ['setup', 'mps_logo', 'player', 'start', 'vgame', 'end', 'dswap',     # the seven programs
              'asound117', 'mgraphic', 'misc', 'asound.log']                        # driver images they load

# C keywords and common libc names: a function with one of these names would clash with a system header.
RESERVED = set('''
auto break case char const continue default do double else enum extern float for goto if inline int long register
restrict return short signed sizeof static struct switch typedef union unsigned void volatile while bool true false
 fprintf printf sprintf snprintf vprintf vfprintf fopen fclose fread fwrite fgets fputs fputc fgetc getc putc
getchar putchar gets puts scanf fscanf sscanf remove rename tmpfile perror fseek ftell rewind feof ferror clearerr
fflush setvbuf
 abs labs atoi atol atof strtol strtoul malloc calloc realloc free exit abort atexit system getenv rand srand
qsort bsearch div ldiv
 strcpy strncpy strcat strncat strcmp strncmp strchr strrchr strstr strlen strtok memcpy memmove memset memcmp
memchr strerror strdup index
 isalnum isalpha isdigit isspace isupper islower isprint ispunct isxdigit toupper tolower iscntrl isgraph
 sin cos tan asin acos atan atan2 sinh cosh tanh exp log log10 pow sqrt floor ceil fabs fmod ldexp frexp modf
 time clock difftime mktime signal raise sleep
 open close read write lseek tell eof access unlink creat filelength dup mkdir getch getche
putch kbhit
'''.split())


def ascii_(s):
    return s.encode('ascii', 'replace').decode('ascii')


def cmt(s, limit=None):
    """Text for a C comment on one line: ASCII, no comment delimiters, optionally cut."""
    s = ' '.join(ascii_(s).split()).replace('*/', '* /').replace('/*', '/ *')
    return s[:limit] if limit and len(s) > limit else s


def ident(name):
    n = re.sub(r'\W', '_', name)
    return '_' + n if n[:1].isdigit() else n


def load():
    with open(CSV_PATH, encoding='utf-8', newline='') as fh:
        rows = list(csv.DictReader(fh))
    for i, r in enumerate(rows):
        r['_row'] = i                       # unique key for this run
        r['_line'] = i + 2                  # line in the CSV (the header is line 1)
    return rows


def parse_addr(r):
    """(seg, off): seg is None for a DGROUP 'DS:xxxx' address. None if the address does not parse."""
    a = r['address'].strip()
    try:
        if a[:3].upper() == 'DS:':
            return None, int(a[3:], 16)
        s, o = a.split(':')
        return int(s, 16), int(o, 16)
    except ValueError:
        return None


def group_of(r, seg):
    if r['kind'] == 'global':
        return 'DS' if seg is None else 'CS'
    return {'func': 'FN', 'label': 'LB'}.get(r['kind'])


def is_dead(r):
    """A row the spec marks dead: 'dead' in its notes, or in its type (a label inside dead code)."""
    return re.search(r'\bdead\b', r['notes'] + ' ' + r['type'], re.I) is not None


def tag(r):
    return 'DEAD; ' if is_dead(r) else ''


def assign_names(rows):
    """Returns (names, skipped, renamed). names[row index] = C identifier; skipped = [(row, reason)];
    renamed = [(exe, kind, source name, address, C name, reason)]."""
    names, skipped, renamed = {}, [], []
    per_exe = defaultdict(list)
    for r in rows:
        pa = parse_addr(r)
        if pa is None:
            skipped.append((r, 'address %r does not parse' % r['address']))
            continue
        seg, off = pa
        g = group_of(r, seg)
        if g is None:
            skipped.append((r, 'kind %r is not global, func or label' % r['kind']))
            continue
        if not r['name'].strip():
            skipped.append((r, 'empty name'))
            continue
        per_exe[r['exe']].append((r, seg, off, g))
    for exe, items in per_exe.items():
        counts = Counter(ident(r['name'].strip()) for r, _, _, _ in items)
        used = set()
        order = sorted(items, key=lambda t: (-1 if t[1] is None else t[1], t[2], t[0]['_row']))
        for r, seg, off, g in order:
            raw = r['name'].strip()
            base = ident(raw)
            n, why = base, []
            if base != raw:
                why.append('not a C identifier')
            if counts[base] > 1:
                n = ('%s_%04X' % (base, off)) if seg is None else ('%s_%04X_%04X' % (base, seg, off))
                why.append('name at %d addresses in %s' % (counts[base], exe))
            if g == 'FN' and n in RESERVED:
                n += '_fn'
                why.append('C keyword or libc name')
            m, k = n, 2
            while m in used:
                m = '%s_%d' % (n, k)
                k += 1
                why.append('collision')
            used.add(m)
            names[r['_row']] = m
            if m != raw:
                renamed.append((exe, g, raw, r['address'], m, ', '.join(dict.fromkeys(why))))
    return names, skipped, renamed


# ---- spec sections (for the index comments): the spec heading that talks about the function or label

_SPEC_LINES = {}


def spec_lines(spec):
    if spec not in _SPEC_LINES:
        p = os.path.join(SPEC_DIR, spec + '.md')
        with open(p, encoding='utf-8', errors='replace') as fh:
            _SPEC_LINES[spec] = fh.read().split('\n') if os.path.exists(p) else []
    return _SPEC_LINES[spec]


def heading_text(line):
    return cmt(line.lstrip('#').strip().replace('`', '').replace('**', ''), 60)


def spec_section(r, seg, off):
    return _section(r['spec'], r['name'].strip(), seg, off)


@lru_cache(maxsize=None)
def _section(spec, name, seg, off):
    """Heading text of the spec section for this row, or None. Order: a heading with the address, a heading
    with the name, then the nearest heading above the first body line with the address, then with the name.
    Addresses match as SSSS:OOOO, as cs:OOOO, and (segment 0000 only, as in ASOUND.LOG) as a bare OOOO."""
    lines = spec_lines(spec)
    name_rx = re.compile(r'(?<![\w.])%s(?!\w)' % re.escape(name))
    addr_rxs = []
    if seg is not None:
        addr_rxs.append(re.compile(r'%04x:%04x' % (seg, off), re.I))
        addr_rxs.append(re.compile(r'cs:%04x(?![0-9a-f])' % off, re.I))
        if seg == 0:
            addr_rxs.append(re.compile(r'(?<![0-9a-f])%04x(?![0-9a-f])' % off, re.I))
    def addr_hit(line):
        return any(rx.search(line) for rx in addr_rxs)
    for want_addr in (True, False):
        for line in lines:
            if line.startswith('#') and (addr_hit(line) if want_addr else name_rx.search(line)):
                return heading_text(line)
    for want_addr in (True, False):
        for i, line in enumerate(lines):
            if addr_hit(line) if want_addr else name_rx.search(line):
                j = i
                while j >= 0 and not lines[j].startswith('#'):
                    j -= 1
                return heading_text(lines[j]) if j >= 0 else '(top of file)'
    return None


# ---- writing

def prog_header(exe, rows, names, stats):
    P = ident(exe).upper()
    ds, cs, fn, lb = [], [], [], []
    for r in rows:
        pa = parse_addr(r)
        if pa is None or r['_row'] not in names:
            continue                                  # reported as SKIPPED by assign_names
        seg, off = pa
        {'DS': ds, 'CS': cs, 'FN': fn, 'LB': lb}[group_of(r, seg)].append((r, seg, off))
    out = ['/* Generated by tools/gen_symbols.py from port/symbols.csv (image %s, %d rows). Do not edit.'
           % (exe.upper(), len(rows)),
           ' * Regenerate after tools/merge_symbols.py changes port/symbols.csv (f117port/PORTING.md,'
           ' "Symbol pipeline").',
           ' * A name used at several addresses of this image carries the address as a suffix. */',
           '#pragma once', '']

    out.append('/* ---- DGROUP globals: DS offsets, for DSB/DSW/DSS/DSL while this image is current (mem.h) */')
    for r, seg, off in sorted(ds, key=lambda t: t[2]):
        out.append('#define %-44s 0x%04X  /* %s%s; owner %s; spec %s */' % (
            '%s_DS_%s' % (P, names[r['_row']]), off, tag(r), cmt(r['type'], 40) or '-', cmt(r['owner']),
            cmt(r['spec'])))
        stats['dead'] += is_dead(r)
    out.append('')

    if cs:
        out.append('/* ---- Code-segment variables: offset in their segment, and the file segment */')
        for r, seg, off in sorted(cs, key=lambda t: (t[1], t[2])):
            n = names[r['_row']]
            out.append('#define %-44s 0x%04X  /* %s%s; owner %s; spec %s */' % (
                '%s_CS_%s' % (P, n), off, tag(r), cmt(r['type'], 40) or '-', cmt(r['owner']), cmt(r['spec'])))
            stats['dead'] += is_dead(r)
            out.append('#define %-44s 0x%04X' % ('%s_CSSEG_%s' % (P, n), seg))
        out.append('')

    out.append('/* ---- Functions: index only (prototypes: gen/module_<spec>.h). Columns: address, C name, owner,'
               ' spec file and section. */')
    for r, seg, off in sorted(fn, key=lambda t: (t[1], t[2])):
        sec = spec_section(r, seg, off)
        stats['fn_nosec' if sec is None else 'fn_sec'] += 1
        out.append('/* %04x:%04x  %-30s %-10s %s.md "%s"%s */' % (
            seg, off, names[r['_row']], cmt(r['owner']), cmt(r['spec']), sec or '(no heading)',
            '  DEAD' if is_dead(r) else ''))
        stats['dead'] += is_dead(r)
    out.append('')

    if lb:
        out.append('/* ---- Code labels: jump targets inside functions (not functions, no prototype) */')
        for r, seg, off in sorted(lb, key=lambda t: (t[1], t[2])):
            sec = spec_section(r, seg, off)
            out.append('/* %04x:%04x  %-30s %s  %s.md "%s"%s */' % (
                seg, off, names[r['_row']], cmt(r['type'], 30), cmt(r['spec']), sec or '(no heading)',
                '  DEAD' if is_dead(r) else ''))
            stats['dead'] += is_dead(r)
        out.append('')
    stats.update(fn=len(fn), ds=len(ds), cs=len(cs), lb=len(lb))
    return '\n'.join(out) + '\n'


def emit(files, prefix, check):
    """Writes files (name -> text) into GEN_DIR and removes stale generated files with that prefix. With check,
    writes nothing and returns the problems (missing, stale, unexpected)."""
    problems = []
    existing = sorted(f for f in os.listdir(GEN_DIR) if f.startswith(prefix) and f.endswith('.h')) \
        if os.path.isdir(GEN_DIR) else []
    for fn in existing:
        if fn not in files:
            problems.append('unexpected %s' % fn)
    for fn, text in files.items():
        p = os.path.join(GEN_DIR, fn)
        old = None
        if os.path.exists(p):
            with open(p, encoding='utf-8') as fh:           # universal newlines: a CRLF checkout compares equal
                old = fh.read()
        if check:
            if old is None:
                problems.append('missing %s' % fn)
            elif old != text:
                problems.append('stale %s' % fn)
        elif old != text:
            os.makedirs(GEN_DIR, exist_ok=True)
            with open(p, 'w', encoding='utf-8', newline='\n') as fh:
                fh.write(text)
    if not check:
        for fn in existing:
            if fn not in files:
                os.remove(os.path.join(GEN_DIR, fn))
    return problems


def main(argv):
    check = '--check' in argv
    rows = load()
    names, skipped, renamed = assign_names(rows)
    exes = [e for e in PROG_ORDER if any(r['exe'] == e for r in rows)]
    exes += sorted({r['exe'] for r in rows} - set(PROG_ORDER))
    files, lines = {}, []
    for exe in exes:
        stats = Counter()
        mine = [r for r in rows if r['exe'] == exe]
        files['symbols_%s.h' % ident(exe)] = prog_header(exe, mine, names, stats)
        lines.append('  %-10s symbols_%s.h: %3d functions (%d DEAD), %3d DGROUP, %2d CS vars, %3d labels; '
                     'spec section found for %d of %d' % (
                         exe.upper(), ident(exe), stats['fn'], stats['dead'], stats['ds'], stats['cs'],
                         stats['lb'], stats['fn_sec'], stats['fn']))
    problems = emit(files, 'symbols_', check)
    print('gen_symbols: %d rows from port/symbols.csv -> %d headers in f117port/src/gen%s'
          % (len(rows), len(files), ' (check)' if check else ''))
    print('\n'.join(lines))
    print('renamed identifiers: %d' % len(renamed))
    by_why = defaultdict(list)
    for exe, g, raw, addr, new, why in renamed:
        by_why[why].append((exe, g, raw, addr, new))
    for why in sorted(by_why, key=lambda w: -len(by_why[w])):
        items = by_why[why]
        if 'name at' in why:
            print('  %s (%d rows):' % (why, len(items)))
            for exe, g, raw, addr, new in items:
                print('    %-10s %s %-18s %-10s -> %s' % (exe.upper(), g, raw, addr, new))
        else:
            names_ = sorted({raw for _, _, raw, _, _ in items})
            print('  %s (%d rows): %s' % (why, len(items), ', '.join(names_)[:300]))
    print('skipped rows: %d' % len(skipped))
    for r, why in skipped:
        print('  SKIPPED csv line %d (%s %s %s): %s' % (r['_line'], r['exe'], r['kind'], r['name'], why))
    for p in problems:
        print('  CHECK FAILED: %s (run: python tools/gen_symbols.py)' % p)
    return 1 if (skipped or problems) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
