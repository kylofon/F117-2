# R7e: statistics of .3DT records relevant to 0FFB (probe) and 11F2 (map).
import sys, collections
sys.path.insert(0, 'tools')
import f3d
for th in ['LB','PG','NC','CE','VN','ME','KU','CU','KO']:
    L = f3d.load_3dt(th); b = f3d.load_bank(f'{f3d.G}/{th}.3DX'); fl = [o['flags'] for o in b['objs']]
    n = len(fl)
    s = collections.Counter()
    for (lev, t), recs in L.items():
        for (x, y, z, o) in recs:
            s['L%d' % lev] += 1
            if o & 0x80: s['L%d_b7' % lev] += 1
            if o >= n: s['L%d_ge_n' % lev] += 1
            if lev == 4:
                f = fl[o] if o < n else 0
                if (o & 0x50) == 0 and (f & 0x10): s['probe_ok'] += 1
                if f & 0x10: s['L4_solid'] += 1
            if z == 0: s['L%d_z0' % lev] += 1
            if lev >= 3 and z == 0 and o < n and (fl[o] & 0x80): s['L%d_z0_f80' % lev] += 1
    print(th, 'nobj', n, 'flag10', sum(1 for f in fl if f & 0x10), 'flag80', sum(1 for f in fl if f & 0x80), dict(sorted(s.items())))
