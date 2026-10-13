"""S3.9: instruction-level diff of an END function against the START copy it was derived from (operands that look like addresses are masked).
usage (from the repo root): python port/notes/s39/cmpfn.py END_OFFSET START_OFFSET     e.g. 1cf5 39f7
Reads work/unpacked/END.EXE, START.EXE and port/end_functions.csv, start_functions.csv; needs capstone."""
import csv,struct,sys,re,difflib
from capstone import Cs,CS_ARCH_X86,CS_MODE_16
md=Cs(CS_ARCH_X86,CS_MODE_16)
def load(exe):
    d=open('work/unpacked/%s.EXE'%exe,'rb').read()
    img=d[struct.unpack_from('<H',d,8)[0]*16:]
    fn={}
    for r in csv.DictReader(open('port/%s_functions.csv'%exe.lower())):
        s,o=(int(x,16) for x in r['start'].split(':'))
        fn[(s,o)]=int(r['size'])
    return img,fn
E_,EF=load('END');S_,SF=load('START')
def dis(img,fn,off,seg=0):
    out=[]
    for i in md.disasm(img[seg*16+off:seg*16+off+fn[(seg,off)]],off):
        out.append((i.address,i.mnemonic+' '+i.op_str))
    return out
def norm(t):
    # mask 16-bit immediates that look like addresses/targets
    return re.sub(r'0x[0-9a-f]{3,4}\b','K',t)
def cmp(e,s):
    a=dis(E_,EF,e);b=dis(S_,SF,s)
    na=[norm(x[1]) for x in a];nb=[norm(x[1]) for x in b]
    sm=difflib.SequenceMatcher(None,na,nb,autojunk=False)
    print('== END %04x (%d insns) vs START %04x (%d)  ratio %.2f'%(e,len(a),s,len(b),sm.ratio()))
    for tag,i1,i2,j1,j2 in sm.get_opcodes():
        if tag=='equal': continue
        print(' ',tag)
        for x in a[i1:i2]: print('   E %04x %s'%x)
        for x in b[j1:j2]: print('   S %04x %s'%x)
if __name__=='__main__':
    cmp(int(sys.argv[1],16),int(sys.argv[2],16))
