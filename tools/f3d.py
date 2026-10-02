"""F-117A 2.0 3D data (FORMATS.md "3D"): .3DX object banks, .3DG/.3DT terrain tiles, .WLD world layout.
Ground truth: VGAME 11EB:00D3 (object renderer), 0000:0994/0AF9/0C2C (loaders), 0000:04DA (scenery),
0000:0865 (tile lookup), 0000:E7AC (object placement); START 0000:54BB (.WLD loader).

  f3d.py check                  parse every .3DX, report byte coverage and odd cases
  f3d.py dump FILE.3DX [N]      text dump of object N (all if omitted)
  f3d.py obj                    -> work/3d/obj/<BANK>/oNNN.obj + <BANK>.mtl (LOD 0, model coords, +z = south)
  f3d.py sheets                 -> work/3d/sheet_<BANK>.png (top-down/oblique renders, LOD 0)
  f3d.py maps                   -> work/3d/map_<TH>.png (top-down theatre: terrain levels 4..1 + .WLD sites)
                                   + work/3d/maps_sheet.png (all nine, 512 px each)
  f3d.py all                    everything above except dump
"""
import glob, os, struct, sys
from PIL import Image, ImageDraw

G, O = 'Game', 'work/3d'
THEATRES = ['LB', 'PG', 'NC', 'CE', 'VN', 'ME', 'KU', 'CU', 'KO']   # START string order = theatre index
VGAME, DGROUP = 'work/unpacked/VGAME.EXE', 0x1E23


def u16(d, o): return struct.unpack_from('<H', d, o)[0]
def s16(d, o): return struct.unpack_from('<h', d, o)[0]


# ---------------------------------------------------------------- .3DX
def parse_faces(d, o):
    """Face list at o (11EB:0CED). Elements until FFh: poly / line / points, each with an optional
    chain of conditions (var, value) from preceding 80h|var bytes. Returns (elements, end)."""
    els, cond = [], []
    while True:
        b = d[o]; o += 1
        if b == 0xFF:
            return els, o
        if b & 0x80:                                   # 0CCA: if var[b&7F] != next byte, skip next element
            cond.append((b & 0x7F, d[o])); o += 1
            continue
        if b & 0x20:                                   # 0DFE: points (computed, never drawn)
            n = b & 0x1F; col = d[o]
            els.append(('points', list(d[o + 1:o + 1 + n]), col, cond)); o += 1 + n
        elif b & 0x40:                                 # 0D5F: lines, one per edge
            n = b & 0x1F; col = d[o]
            els.append(('line', list(d[o + 1:o + 1 + n]), col, cond)); o += 1 + n
        else:                                          # 0D04: filled polygon from n edges; colour FFh = skip
            n = b
            els.append(('poly', list(d[o:o + n]), d[o + n], cond)); o += n + 1
        cond = []


def parse_model(d, o):
    m = {'at': o}
    m['rad'], nparts = s16(d, o), s16(d, o + 2); o += 4
    m['parts'] = [(0, 0)]                              # 031B: (var, axis 0/1/2) per extra part, then pivots
    for i in range(max(nparts, 1) - 1):
        m['parts'].append((d[o], d[o + 1])); o += 2
    m['pivots'] = [(0, 0, 0)]
    for i in range(max(nparts, 1) - 1):
        m['pivots'].append(struct.unpack_from('<3h', d, o)); o += 6
    nv = s16(d, o); o += 2
    npl = s16(d, o); o += 2                            # 0565
    planes, part, cond = [], 0, None
    while len(planes) < abs(npl):
        w = u16(d, o)
        if w >> 8 == 0x80:                             # select part for the following planes
            part = w & 0xFF; o += 2; continue
        if w == 0x8100:                                # plane hidden unless var[d[o+2]&1F] == d[o+3]
            cond = (d[o + 2] & 0x1F, struct.unpack_from('b', d, o + 3)[0]); o += 4; continue
        nx, ny, nz, dd = struct.unpack_from('<3hi', d, o); o += 10
        planes.append({'n': (nx, ny, nz), 'd': dd, 'part': part, 'cond': cond})
        cond = None
    for p in planes:                                   # vertices used by each plane
        n = d[o]; p['verts'] = list(d[o + 1:o + 1 + n]); o += 1 + n
    m['bsp'] = None
    if npl < 0:                                        # 0E5F: root + (front, back) per plane, FFh = none
        m['bsp'] = (d[o], [(d[o + 1 + 2 * i], d[o + 2 + 2 * i]) for i in range(-npl)]); o += 1 - 2 * npl
    verts, part = [], 0
    if nv < 0:                                         # 075E: bytes indexing the shared vertex table [4EB6]
        m['global_verts'] = list(d[o:o - nv]); o -= nv
    else:
        while len(verts) < nv:
            w = u16(d, o)
            if 0x8000 <= w < 0x8010:
                part = w & 0xF; o += 2; continue
            x, y, z = struct.unpack_from('<3h', d, o); o += 6
            verts.append((x, y, z, part))
    m['verts'] = verts
    ne = u16(d, o); o += 2                             # 0A37: (v0, v1) bytes
    m['edges'] = [(d[o + 2 * i], d[o + 2 * i + 1]) for i in range(ne)]; o += 2 * ne
    m['faces'] = []
    if npl == 0:                                       # 0C7A: one face list, no pointer table
        els, o = parse_faces(d, o); m['faces'].append(els)
    else:
        ptrs = [u16(d, o + 2 * i) for i in range(abs(npl))]; o += 2 * abs(npl)
        for p in ptrs:
            els, e = parse_faces(d, p); m['faces'].append(els); o = max(o, e)
    m['planes'], m['end'] = planes, o
    return m


def load_bank(path):
    d = open(path, 'rb').read()
    n = u16(d, 0)
    offs = [u16(d, 2 + 2 * i) for i in range(n + 1)]     # offs[n] = end of data
    flags = d[4 + 2 * n:4 + 3 * n]
    objs, models = [], {}
    for i in range(n):
        o, lods = offs[i], []
        while True:                                     # 00D3: (max depth, model) until bit 15
            dist, p = struct.unpack_from('<HH', d, o); o += 4
            lods.append((dist, p))
            if dist & 0x8000:
                break
        for _, p in lods:
            if p not in models:
                models[p] = parse_model(d, p)
        objs.append({'flags': flags[i], 'lods': lods, 'hdr_end': o})
    return {'name': os.path.basename(path)[:-4], 'data': d, 'objs': objs, 'models': models, 'offs': offs}


def coverage(b):
    d = b['data']; cov = bytearray(len(d))
    n = len(b['objs'])
    cov[0:4 + 3 * n] = b'\1' * (4 + 3 * n)
    for i, ob in enumerate(b['objs']):
        cov[b['offs'][i]:ob['hdr_end']] = b'\1' * (ob['hdr_end'] - b['offs'][i])
    for m in b['models'].values():
        cov[m['at']:m['end']] = b'\1' * (m['end'] - m['at'])
    return sum(cov), len(d)


# ---------------------------------------------------------------- colours
def vgame_image():
    d = open(VGAME, 'rb').read()
    return d[u16(d, 8) * 16:]


def colour_tables():
    """Four 256-word remap tables in VGAME DGROUP ([7D50] selects one): 7D60 (set after the 3D pass),
    7F60 (day), 8160 (day when [43B2]), 8360 (when detail [98DE] < 3). Low byte = palette index."""
    img = vgame_image()
    return {t: struct.unpack_from('<256H', img, DGROUP * 16 + t) for t in (0x7D60, 0x7F60, 0x8160, 0x8360)}


def palette(name='ADV.PAL'):
    """Flight palette: VGAME never programs the DAC, it inherits START's. ADV.PAL fits the engine
    (16-entry shading ramps A0-FF: grey, green, blue-greys; 94h whitish lights); to confirm in U5.
    (PALETTES.PAL holds START's UI-screen palettes, not per-theatre ones.)"""
    p = open(f'{G}/{name}', 'rb').read()[:768]
    return [tuple(min(255, (p[3 * i + k] * 255 + 31) // 63) for k in range(3)) for i in range(256)]


BASE, ALT, SHADE6F = (150, 145, 95), (50, 90, 160), (110, 115, 70)   # land, water, land 2 (colour_map)


def colour_map(cols, pal):
    """3DX colour byte -> RGB (None = not drawn) via a remap table word (fill 1359:0AF7, colour 1359:01D4):
    hi FF solid lo; FB no fill; FC 50% stipple of lo; FE AND lo onto the backdrop (7Fh, 6Fh);
    FD OR lo (80h); other hi = 2x2 dither of lo/hi. The ground backdrop colour (drawn through the
    graphics driver, not found yet) decides what the AND/OR ops look like, so they are shown as fixed
    colours: bit 7 clear (backdrop = land, AND 7Fh) = BASE, OR 80h = water (U5) = ALT, AND 6Fh = SHADE6F."""
    ct = []
    for c in range(256):
        w = cols[c]; lo, hi = w & 0xFF, w >> 8
        if hi == 0xFF: ct.append(pal[lo])
        elif hi == 0xFB: ct.append(None)
        elif hi == 0xFC: ct.append(tuple((a + b) // 2 for a, b in zip(pal[lo], BASE)))
        elif hi == 0xFE: ct.append(BASE if lo | 0x80 == 0xFF else SHADE6F)
        elif hi == 0xFD: ct.append(ALT)
        else: ct.append(tuple((a + b) // 2 for a, b in zip(pal[lo], pal[hi])))
    return ct


# ---------------------------------------------------------------- geometry helpers
def loops(edges, ids):
    """Chain a face's edges into closed vertex loops (the game scan-converts the edge set)."""
    rest = [edges[i] for i in ids if i < len(edges)]
    out = []
    while rest:
        a, b = rest.pop(0); loop = [a, b]
        while True:
            for k, (p, q) in enumerate(rest):
                if p == loop[-1] or q == loop[-1]:
                    nxt = q if p == loop[-1] else p; rest.pop(k); break
            else:
                break
            if nxt == loop[0]:
                break
            loop.append(nxt)
        out.append(loop)
    return out


def default_ok(cond):
    return all(v == 0 for _, v in cond)


def model_polys(m, ct, want=lambda c: True):
    """[(plane, [(x,y,z)...], rgb)] for drawable polygons whose conditions pass `want`."""
    res = []
    for pi, els in enumerate(m['faces']):
        pl = m['planes'][pi] if pi < len(m['planes']) else None
        if pl and pl['cond'] and not want([pl['cond']]):
            continue
        for kind, ids, col, cond in els:
            if kind != 'poly' or col == 0xFF or ct[col] is None or not want(cond):
                continue
            rgb = ct[col]
            for lp in loops(m['edges'], ids):
                if len(lp) >= 3 and all(v < len(m['verts']) for v in lp):
                    res.append((pl, [m['verts'][v][:3] for v in lp], rgb))
    return res


# ---------------------------------------------------------------- OBJ export
def export_obj(b, ct):
    out = f'{O}/obj/{b["name"]}'
    os.makedirs(out, exist_ok=True)
    used = set()
    for i, ob in enumerate(b['objs']):
        m = b['models'][ob['lods'][0][1]]
        L = [f'# {b["name"]}.3DX object {i}  flags {ob["flags"]:02X}  LODs ' +
             ' '.join(f'{dist:04X}->{p:04X}' for dist, p in ob['lods']),
             f'# model {m["at"]:04X} radius {m["rad"]} parts {len(m["parts"])} planes {len(m["planes"])}'
             f'{" bsp" if m["bsp"] else ""}; OBJ = model coords: x east, y up, z south',
             f'mtllib {b["name"]}.mtl']
        if 'global_verts' in m:
            L.append('# vertices index the shared table [4EB6]: ' + ' '.join(map(str, m['global_verts'])))
        for x, y, z, part in m['verts']:
            L.append(f'v {x} {y} {z}')
        for pi, els in enumerate(m['faces']):
            pl = m['planes'][pi] if pi < len(m['planes']) else None
            for j, (kind, ids, col, cond) in enumerate(els):
                c = list(cond) + ([pl['cond']] if pl and pl['cond'] else [])
                L.append(f'g p{pi}_{j}{"".join(f"_if_v{v}eq{x}" for v, x in c)}')
                L.append(f'usemtl c{col:02X}'); used.add(col)
                if kind == 'points':
                    L.append('p ' + ' '.join(str(v + 1) for v in ids))
                elif kind == 'line':
                    for e in ids:
                        if e < len(m['edges']):
                            L.append('l %d %d' % (m['edges'][e][0] + 1, m['edges'][e][1] + 1))
                elif col != 0xFF:
                    for lp in loops(m['edges'], ids):
                        if len(lp) >= 3:
                            L.append('f ' + ' '.join(str(v + 1) for v in reversed(lp)))
        open(f'{out}/o{i:03d}.obj', 'w').write('\n'.join(L) + '\n')
    M = []
    for c in sorted(used):
        r, g, bl = ct[c] or (128, 128, 128)
        M += [f'newmtl c{c:02X}', f'Kd {r / 255:.3f} {g / 255:.3f} {bl / 255:.3f}'] +              (['d 0'] if ct[c] is None else []) + ['']
    open(f'{out}/{b["name"]}.mtl', 'w').write('\n'.join(M))
    return len(b['objs'])


# ---------------------------------------------------------------- renders
def project_oblique(x, y, z):
    return x + 0.5 * z, -y + 0.35 * z            # x east, y up, z south; viewer south-up


def render_model(m, ct, size, view='oblique'):
    polys = model_polys(m, ct, default_ok)
    pts = [v[:3] for v in m['verts']] or [(0, 0, 0)]
    if view == 'top':
        pr = lambda x, y, z: (x, z)
        polys = [p for p in polys if p[0] is None or p[0]['n'][1] > 0 or p[0]['n'] == (0, 0, 0)]
        key = lambda p: sum(v[1] for v in p[1]) / len(p[1])
    else:
        pr = project_oblique
        polys = [p for p in polys if p[0] is None or p[0]['n'] == (0, 0, 0) or
                 p[0]['n'][1] + p[0]['n'][2] > 0]       # facing the (up, south) viewer
        key = lambda p: sum(v[1] + v[2] for v in p[1]) / len(p[1])
    P = [pr(*v) for v in pts]
    x0, x1 = min(p[0] for p in P), max(p[0] for p in P)
    y0, y1 = min(p[1] for p in P), max(p[1] for p in P)
    s = (size - 8) / max(x1 - x0, y1 - y0, 1)
    im = Image.new('RGB', (size, size), (24, 24, 40)); dr = ImageDraw.Draw(im)
    cx, cy = (size - s * (x1 + x0)) / 2, (size - s * (y1 + y0)) / 2
    for pl, vs, rgb in sorted(polys, key=key):
        dr.polygon([(cx + s * pr(*v)[0], cy + s * pr(*v)[1]) for v in vs], fill=rgb, outline=None)
    for els in m['faces']:
        for kind, ids, col, cond in els:
            if kind == 'line' and default_ok(cond) and ct[col]:
                for e in ids:
                    if e < len(m['edges']) and max(m['edges'][e]) < len(m['verts']):
                        a, c = (pr(*m['verts'][v][:3]) for v in m['edges'][e])
                        dr.line([(cx + s * a[0], cy + s * a[1]), (cx + s * c[0], cy + s * c[1])],
                                fill=ct[col])
    return im


def sheet(b, ct):
    T, C, LH = 128, 10, 12
    n = len(b['objs']); rows = (n + C - 1) // C
    sh = Image.new('RGB', (C * T, rows * (T + LH)), (0, 0, 0)); dr = ImageDraw.Draw(sh)
    for i, ob in enumerate(b['objs']):
        m = b['models'][ob['lods'][0][1]]
        im = render_model(m, ct, T, 'oblique')
        x, y = (i % C) * T, (i // C) * (T + LH)
        sh.paste(im, (x, y + LH))
        dr.text((x + 2, y), f'{i} f{ob["flags"]:02X} r{m["rad"]}', fill=(255, 255, 255))
    sh.save(f'{O}/sheet_{b["name"]}.png')


# ---------------------------------------------------------------- .3DG / .3DT / .WLD
def load_3dg(th):
    """0C2C: word id '22'; 16 bytes (overwritten); 16x16 level-3 tile types; then 3 x 512 bytes =
    32 types x 4x4 child types for levels 2, 1, 0 (index = parent*16 + (y&3)*4 + (x&3))."""
    g = open(f'{G}/{th}.3DG', 'rb').read()
    return {'l3': g[18:274], 'sub': {2: g[274:786], 1: g[786:1298], 0: g[1298:1810]}}


def load_3dt(th):
    """0AF9: word id '11'; 5 words (types per level, 32); 5 x 32 words object counts per (level, type);
    then records (x, y, z, type) words, level-major. In memory 7 bytes (type keeps its low byte)."""
    d = open(f'{G}/{th}.3DT', 'rb').read()
    nt = struct.unpack_from('<5H', d, 2)
    cnt = [struct.unpack_from('<32H', d, 12 + 64 * k) for k in range(5)]
    o, lists = 332, {}
    for k in range(5):
        for t in range(nt[k]):
            L = []
            for i in range(cnt[k][t]):
                x, y, z, ty = struct.unpack_from('<3hH', d, o); o += 8
                L.append((x, y, z, ty & 0xFF))
            lists[k, t] = L
    return lists


def load_wld(th):
    """START 0000:54BB. Words: id 'BN', nSites, ?, ?; nSites x 16-byte sites; word nUnits, nUnits x 36;
    3 x 128 bytes; 256 bytes (16x16 level-3 grid); up to 750 bytes of NUL-separated names."""
    d = open(f'{G}/{th}.WLD', 'rb').read()
    ns = u16(d, 2)
    sites = [struct.unpack_from('<8H', d, 8 + 16 * i) for i in range(ns)]
    o = 8 + 16 * ns; nu = u16(d, o); o += 2
    units = [d[o + 36 * i:o + 36 * i + 36] for i in range(nu)]; o += 36 * nu
    tabs = [d[o + 128 * i:o + 128 * i + 128] for i in range(3)]; o += 384
    grid = d[o:o + 256]; o += 256
    names = d[o:o + 750].split(b'\0')
    return {'hdr': struct.unpack_from('<4H', d, 0), 'sites': sites, 'units': units, 'tabs': tabs,
            'grid': grid, 'names': [n.decode('latin-1') for n in names]}


def tile(g, l4, level, x, y):
    """0000:0865: tile type of cell (x, y) at `level`; -1 outside."""
    n = {4: 8, 3: 16, 2: 64, 1: 256, 0: 1024}[level]
    if level == 4:
        x += 2; y += 2
    if not (0 <= x < n and 0 <= y < n):
        return -1
    if level == 4:
        return l4[y * 8 + x]
    if level == 3:
        return g['l3'][y * 16 + x]
    p = tile(g, l4, level + 1, x >> 2, y >> 2)
    return g['sub'][level][p * 16 + (y & 3) * 4 + (x & 3)] if p >= 0 else -1


SCALE = {4: 64, 3: 16, 2: 4, 1: 1}                # world units per level unit (0000:0804)
WORLD = 0x100000                                    # 16 level-3 cells of 0x1000 level units


def theatre_map(th, ct, px=2048):
    ti = THEATRES.index(th)
    img = vgame_image()
    l4 = img[DGROUP * 16 + 0xA68 + 64 * ti:DGROUP * 16 + 0xA68 + 64 * ti + 64]
    b = load_bank(f'{G}/{th}.3DX'); g = load_3dg(th); t = load_3dt(th); w = load_wld(th)
    k = px / WORLD
    im = Image.new('RGB', (px, px), BASE); dr = ImageDraw.Draw(im)
    flat, solid = [], []
    for level in (4, 3, 2, 1):
        s = SCALE[level]; n = WORLD // (0x1000 * s)
        for cy in range(-2 if level == 4 else 0, n + (2 if level == 4 else 0)):
            for cx in range(-2 if level == 4 else 0, n + (2 if level == 4 else 0)):
                ty = tile(g, l4, level, cx, cy)
                for rx, ry, rz, ot in t.get((level, ty), []):
                    if ot >= len(b['objs']):
                        continue
                    # terrain y runs north: t = (8000h - WLD y) * 32 (0000:77D9); image Y = WORLD - t
                    X = (cx * 0x1000 + 0x800 + rx) * s; Y = WORLD - (cy * 0x1000 + 0x800 + ry) * s
                    ob = b['objs'][ot]
                    (solid if ob['flags'] & 0x80 else flat).append((level, X, Y, s, ot))
    for site in w['sites']:
        nm, x, y, oid = site[0], site[1], site[2], site[7]
        if (x or y) and oid >> 8 == 1 and (oid & 0xFF) < len(b['objs']):
            ob = b['objs'][oid & 0xFF]
            (solid if ob['flags'] & 0x80 else flat).append((0, x * 32, y * 32, 1, oid & 0xFF))

    def draw(level, X, Y, s, ot):
        m = b['models'][b['objs'][ot]['lods'][0][1]]
        ps = [p for p in model_polys(m, ct, default_ok)
              if p[0] is None or p[0]['n'][1] > 0 or p[0]['n'] == (0, 0, 0)]
        for pl, vs, rgb in sorted(ps, key=lambda p: sum(v[1] for v in p[1]) / len(p[1])):
            dr.polygon([((X + vx * s) * k, (Y + vz * s) * k) for vx, vy, vz in vs], fill=rgb)   # model z = south

    for o in flat:
        draw(*o)
    tops = {}
    for o in solid:
        m = b['models'][b['objs'][o[4]]['lods'][0][1]]
        tops[o] = max((v[1] for v in m['verts']), default=0) * o[3]
    for o in sorted(solid, key=lambda o: tops[o]):
        draw(*o)
    seen = set()
    for site in w['sites']:
        nm, x, y = site[0], site[1], site[2]
        if not (x or y):
            continue
        X, Y = x * 32 * k, y * 32 * k
        dr.rectangle([X - 2, Y - 2, X + 2, Y + 2], outline=(255, 255, 0))
        name = w['names'][nm] if nm < len(w['names']) else '?'
        if name and name not in seen:
            seen.add(name); dr.text((X + 4, Y - 5), name, fill=(255, 255, 0))
    for i, (rgb, txt) in enumerate([(BASE, 'land: backdrop / AND 7Fh'), (ALT, 'water: OR 80h'),
                                    (SHADE6F, 'AND 6Fh (land 2?)'), ((255, 255, 0), '.WLD site')]):
        dr.rectangle([8, 8 + 14 * i, 18, 18 + 14 * i], fill=rgb, outline=(0, 0, 0))
        dr.text((24, 8 + 14 * i), txt, fill=(0, 0, 0))
    dr.text((8, 70), f'{th}  north up, 1 px = {WORLD // px} units, level-3 cell = {0x10000 * px // WORLD} px',
            fill=(0, 0, 0))
    im.save(f'{O}/map_{th}.png')
    return len(flat), len(solid)


# ---------------------------------------------------------------- CLI
def bank_name(f):
    return os.path.basename(f)[:-4]


def banks():
    return sorted(glob.glob(f'{G}/*.3DX'))


def cmd_check():
    for f in banks():
        b = load_bank(f); c, n = coverage(b)
        odd = []
        for p, m in b['models'].items():
            if 'global_verts' in m: odd.append(f'{p:04X}:gverts')
            if len(m['parts']) > 1: odd.append(f'{p:04X}:parts{len(m["parts"])}')
            if m['bsp']: odd.append(f'{p:04X}:bsp')
            nl = sum(len(lp) > 1 for els in m['faces'] for k, ids, *_ in els if k == 'poly'
                     for lp in [loops(m['edges'], ids)])
            if nl: odd.append(f'{p:04X}:multiloop{nl}')
        print(f'{b["name"]:6} objs {len(b["objs"]):3} models {len(b["models"]):3} '
              f'coverage {c}/{n}  {" ".join(odd[:12])}{" ..." if len(odd) > 12 else ""}')


def cmd_dump(path, which=None):
    b = load_bank(path)
    for i, ob in enumerate(b['objs']):
        if which is not None and i != which:
            continue
        print(f'obj {i} flags {ob["flags"]:02X} lods {[(hex(a), hex(p)) for a, p in ob["lods"]]}')
        m = b['models'][ob['lods'][0][1]]
        print(f'  rad {m["rad"]} parts {m["parts"]} pivots {m["pivots"]} bsp {m["bsp"]}')
        print(f'  verts {len(m["verts"])} {m["verts"][:8]}{"..." if len(m["verts"]) > 8 else ""}')
        for pi, pl in enumerate(m['planes']):
            print(f'  plane {pi} n {pl["n"]} d {pl["d"]} part {pl["part"]} cond {pl["cond"]} '
                  f'verts {pl["verts"]}')
            for el in m['faces'][pi]:
                print('    ', el)
        if not m['planes'] and m['faces']:
            for el in m['faces'][0]:
                print('    ', el)


def main():
    a = sys.argv[1:] or ['all']
    os.makedirs(O, exist_ok=True)
    if a[0] == 'check' or a[0] == 'all':
        cmd_check()
    if a[0] == 'dump':
        cmd_dump(a[1], int(a[2]) if len(a) > 2 else None); return
    cols = colour_tables()[0x7F60]
    ct = lambda name: colour_map(cols, palette())
    if a[0] in ('obj', 'all'):
        print('obj:', sum(export_obj(load_bank(f), ct(bank_name(f))) for f in banks()), 'files')
    if a[0] in ('sheets', 'all'):
        for f in banks():
            sheet(load_bank(f), ct(bank_name(f)))
        print('sheets:', len(banks()))
    if a[0] in ('maps', 'all'):
        sh = Image.new('RGB', (3 * 512, 3 * 528), (0, 0, 0)); dr = ImageDraw.Draw(sh)
        for i, th in enumerate(THEATRES):
            print('map', th, 'flat/solid objects', theatre_map(th, ct(th)))
            x, y = (i % 3) * 512, (i // 3) * 528
            sh.paste(Image.open(f'{O}/map_{th}.png').resize((512, 512), Image.LANCZOS), (x, y + 16))
            dr.text((x + 4, y + 2), f'{th} (theatre {i})', fill=(255, 255, 255))
        sh.save(f'{O}/maps_sheet.png')


if __name__ == '__main__':
    main()
