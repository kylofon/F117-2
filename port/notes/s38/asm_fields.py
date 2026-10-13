"""Annotate tools/dsann.py output with VGAME struct field names: [reg-disp] -> {u.x}, {s.fl2}, {p.life}, {d.timer}, {t.dz}.
usage: python tools/dsann.py VGAME 1e23 5730 62d0 | python port/notes/s38/asm_fields.py"""
import re,sys
U={0:'u.site',2:'u.x',4:'u.y',6:'u.z',8:'u.x32',0xa:'u.x32h',0xc:'u.y32',0xe:'u.y32h',0x10:'u.hdg',0x11:'u.hdg_h',0x12:'u.pitch',0x13:'u.pitch_h',0x14:'u.bank',0x15:'u.bank_h',0x16:'u.type',0x18:'u.fl',0x19:'u.fl2',0x1a:'u.spd',0x1c:'u.endur',0x1e:'u.wpn',0x20:'u.20',0x22:'u.level'}
S={0:'s.name',2:'s.x',4:'s.y',6:'s.type',8:'s.fl',9:'s.fl2',0xa:'s.level',0xc:'s.timer',0xe:'s.nidx',0xf:'s.f'}
P={0:'p.x',2:'p.y',4:'p.z',6:'p.spd',8:'p.hdg',0xa:'p.pitch',0xc:'p.bank',0xe:'p.life',0x10:'p.type',0x12:'p.store',0x14:'p.tgt',0x16:'p.aux',0x18:'p.tx',0x1a:'p.ty'}
D={0:'d.x',2:'d.y',4:'d.4',6:'d.type',8:'d.timer',10:'d.10'}
T={0:'t.x',2:'t.y',4:'t.z',6:'t.dx',8:'t.dy',10:'t.dz'}
AC={0x12:'ac.maxspd',0x14:'ac.range',0x16:'ac.agil',0x18:'ac.radar',0x1a:'ac.1a',0x1c:'ac.1c',0x1e:'ac.kills'}
TABS=[(0xC13E,0x24,U),(0xB2A2,0x10,S),(0x3C0C,0x1c,P),(0x39AA,12,D),(0xE44E,12,T)]
def name(a):
    for base,sz,f in TABS:
        o=a-base
        if 0<=o<sz and o in f: return f[o]
    return None
def sub(m):
    reg,sign,d=m.group(1),m.group(2),int(m.group(3),16)
    a=(-d if sign=='-' else d)&0xffff
    n=name(a)
    return m.group(0)+('{%s}'%n if n else '')
for ln in sys.stdin:
    ln=re.sub(r'\[(bx|si|di|bp)(?: \+ (?:si|di|bx))? ([+-]) 0x([0-9a-f]+)\]',sub,ln.rstrip('\n'))
    print(ln)
