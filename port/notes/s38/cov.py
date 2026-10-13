"""Line coverage of s38_ref.py during s38_test scenarios: python cov.py N scenario [scenario ...] (prints lines never executed, over the union of the runs)."""
import sys, s38_test as T, s38_ref
hit=set()
def tr(frame, ev, arg):
    if frame.f_code.co_filename.endswith('s38_ref.py'):
        if ev=='line': hit.add(frame.f_lineno)
        return tr
    return None
names=sys.argv[2:]; n=int(sys.argv[1])
sys.settrace(tr)
bad=0
for nm in names:
    for seed in range(n):
        r=T.run_one(nm,seed)
        if r[0] or not r[1] or not r[2]: bad+=1
sys.settrace(None)
src=open(s38_ref.__file__).read().split('\n')
import ast
tree=ast.parse('\n'.join(src))
fns={f.name:(f.lineno,f.end_lineno) for f in ast.walk(tree) if isinstance(f,ast.FunctionDef)}
want=sys.argv[2:]
M={'projfocus':['wpn_proj_update_all'],'relfocus':['wpn_release'],'proj':['wpn_proj_update_all'],'release':['wpn_release'],'ai':['ai_update_units','_steer','_scramble'],'aifocus':['ai_update_units','_steer','_scramble'],'radar':['det_radar_signal'],'site':['det_site_radar_update','det_alert_network'],'unit':['ai_unit_radar'],'cm':['wpn_release_countermeasure'],'gun':['wpn_gun_update'],'seeker':['wpn_seeker_check'],'sig':['signature'],'takeoff':['ai_unit_takeoff'],'cmtick':['wpn_cm_tick'],'blast':['dmg_player_blast']}
print('bad',bad)
fs=sorted({f for nm in names for f in M[nm]})
if True:
    for f in fs:
        a,b=fns[f]; miss=[]
        for L in range(a+1,b+1):
            s=src[L-1].strip()
            if not s or s.startswith('#') or s.startswith('else') or s.startswith('elif') : continue
            if L not in hit: miss.append(L)
        print(f, 'missed lines', miss)
