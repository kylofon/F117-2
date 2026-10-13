import struct,sys
d=open('C:/Coding/F117-2/work/unpacked/VGAME.EXE','rb').read()
img=d[struct.unpack_from('<H',d,8)[0]*16:]
def ds(o,n): return img[0x1e230+o:0x1e230+o+n]
def w(o,n,fmt='h'): return struct.unpack('<%d%s'%(n,fmt),ds(o,2*n))
if __name__=='__main__':
    o=int(sys.argv[1],16);n=int(sys.argv[2]);k=sys.argv[3] if len(sys.argv)>3 else 'h'
    if k=='b': print(ds(o,n).hex(' '))
    elif k=='s': print(ds(o,n))
    else: print(w(o,n,k))
