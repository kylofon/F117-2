import re,struct,sys
lines=open('C:/Coding/F117-2/work/s36/all.txt').read().split('\n')
ins={}
order=[]
for l in lines:
    m=re.match(r'^([0-9a-f]{4}) (.*?)(\s+;.*)?$',l)
    if m and not l.startswith('=='):
        a=int(m.group(1),16); ins[a]=m.group(2); order.append(a)
nxt={a:order[i+1] for i,a in enumerate(order[:-1])}
d=open('C:/Coding/F117-2/work/unpacked/VGAME.EXE','rb').read()
img=d[struct.unpack_from('<H',d,8)[0]*16:]
def sim(key,start,maxsteps=400):
    ax=key; zf=cf=sf=of=0; pc=start; steps=0
    while steps<maxsteps:
        steps+=1
        t=ins.get(pc)
        if t is None: return ('?',pc)
        mn,_,ops=t.partition(' ')
        if mn in('cmp','sub'):
            a,b=[x.strip() for x in ops.split(',')]
            if a!='ax': return ('unsup',pc,t)
            imm=int(b,16) if b.startswith('0x') else int(b)
            imm&=0xffff
            r=(ax-imm)&0xffff
            cf=1 if ax<imm else 0; zf=1 if r==0 else 0; sf=r>>15
            sa=ax>>15; sb=imm>>15
            of=1 if (sa!=sb and sf!=sa) else 0
            if mn=='sub': ax=r
            pc=nxt[pc]; continue
        if mn.startswith('j') and mn!='jmp':
            tgt=int(ops,16)
            cc={'je':zf,'jne':not zf,'ja':(not cf and not zf),'jae':not cf,'jb':cf,'jbe':cf or zf,'jg':(not zf and sf==of),'jge':sf==of,'jl':sf!=of,'jle':zf or sf!=of,'js':sf,'jns':not sf}[mn]
            pc=tgt if cc else nxt[pc]; continue
        if mn=='jmp':
            if ops.startswith('0x'): pc=int(ops,16); continue
            return ('ind',pc,t,ax)
        return ('stop',pc,t,ax)
    return ('loop',pc)
if __name__=='__main__':
    res={}
    for k in range(65536):
        r=sim(k,0xd133)
        res.setdefault((r[0],r[1]) if r[0] in('stop','?','unsup','loop') else (r[0],r[1]),[]).append(k)
    for (kind,pc),ks in sorted(res.items(),key=lambda x:x[0][1]):
        if len(ks)>200: print(kind,hex(pc),len(ks),'keys (default)'); continue
        print(kind,hex(pc),' '.join('%04x'%k for k in ks))
