import sys, glob, struct, os
sys.path.insert(0, 'tools')
import f3d
for p in sorted(glob.glob('Game/*.3DX')):
    b = f3d.load_bank(p); d = b['data']
    mx = 0; chained = 0; n0 = 0; condv = set(); negb = 0; nm = 0; maxnp = 0; maxnv = 0; maxne = 0; bspneg = 0; partsw_planes = 0; nvneg = 0; ptsel=0; lodn=0
    for o, m in b['models'].items():
        nm += 1
        npart = struct.unpack_from('<h', d, o + 2)[0]
        mx = max(mx, npart)
        maxnp = max(maxnp, len(m['planes'])); maxnv = max(maxnv, len(m['verts'])); maxne = max(maxne, len(m['edges']))
        if 'global_verts' in m: nvneg += 1
        if m['bsp']:
            for a, c in m['bsp'][1]:
                if a != 0xFF and a & 0x80: bspneg += 1
                if c != 0xFF and c & 0x80: bspneg += 1
        for fl in m['faces']:
            for e in fl:
                if len(e[3]) > 1: chained += 1
                for v, val in e[3]: condv.add(v)
                if e[0] == 'poly' and len(e[1]) == 0: n0 += 1
                if e[0] == 'points': ptsel += 1
    print(os.path.basename(p), 'models', nm, 'max_nparts', mx, 'maxplanes', maxnp, 'maxverts', maxnv, 'maxedges', maxne,
          'chained_conds', chained, 'poly_n0', n0, 'cond_vars', sorted(condv), 'bsp_neg_child', bspneg, 'shared_vert_models', nvneg, 'point_elems', ptsel)
