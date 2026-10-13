"""Reference (Python) transcription of the VGAME helpers analysed in spec_R2.md; differential-tested against the real code with r2_emu.py."""
import math,struct
def i16(v):
    v&=0xffff
    return v-0x10000 if v&0x8000 else v
def u16(v): return v&0xffff
def i32(v):
    v&=0xffffffff
    return v-(1<<32) if v&0x80000000 else v
def tdiv(a,b):            # C / on ints (truncate toward zero)
    q=abs(a)//abs(b)
    return q if (a<0)==(b<0) else -q
def sar(v,n): return v>>n
T=[max(-32767,min(32767,round(32768*math.sin(2*math.pi*i/256)))) for i in range(257)]+[0]
def mul15(a,b): return i16((a*b+0x4000)>>15)
def sin16(a):
    a&=0xffff; idx=a>>8; fr=a&0xff
    d=i16(T[idx+1]-T[idx]); p=d*fr
    return i16(T[idx]+(p>>8)+((p>>7)&1))
def cos16(a): return sin16(a+0x4000)
def sin_mul(a,m): return mul15(m,sin16(a))      # caee
def cos_mul(a,m): return sin_mul(a+0x4000,m)    # cb07
def clamp_w(v,lo,hi):   # c94e (with the wrap quirk)
    v=i16(v)
    if hi<v: return hi
    if lo<=v: return v
    return hi if v<=-0x4000 else lo
def clamp(v,lo,hi):     # c96d
    v=i16(v)
    if hi<v: return hi
    if lo>v: return lo
    return v
def dist2(a,b):         # c987
    a=abs(i16(a)); b=abs(i16(b))
    mx,mn=(a,b) if b<a else (b,a)
    r=mx+(mn>>1)
    return min(r,0x7fff)
def heading(x,y):       # c9d6: 16 bit heading angle of vector (x east, y north); 0 = +y, 0x4000 = +x
    x=i16(x); y=i16(y)
    if x==0: return 0 if y>0 else 0x8000
    if y==0: return 0x4000 if x>0 else 0xC000
    ax,ay=abs(x),abs(y)
    if ay<ax: num=ay<<14; den=ax; flag=1
    else: num=ax<<14; den=ay; flag=0
    t=num//den
    e=(abs(0x1333-t)*0xb00)>>14
    r=(t*(0x2800-e))>>14
    r=i16(r)
    if x>0:
        if y>0: return u16(0x4000-r) if flag else u16(r)
        return u16(r+0x4000) if flag else u16(0x8000-r)
    else:
        if y>0: return u16(r-0x4000) if flag else u16(-r)
        return u16(0xC000-r) if flag else u16(r+0x8000)
def isqrt(n):           # 2f40
    n=abs(i16(n))
    if n<4: return 1
    g=n>>2
    while True:
        q=n//g
        g=(q+g)>>1
        if abs(g-q)<=1: return g
def asin_q15(v):        # 2ebe
    v=i16(v)
    if v==-0x8000: return 0xC000
    a=abs(v); i=(a>>9)+1
    while i>=0 and T[i]>a: i-=1
    d=i16(T[i+1]-T[i]); n=(a-T[i])<<8
    q=tdiv(n,d)
    ang=i16(q+((i&0xff)<<8))
    return u16(-ang) if v<0 else u16(ang)
def acos_q15(v): return u16(0x4000-asin_q15(v))   # 2f2f
def ratio_q15(x,y):     # 2e64 (|x|<=|y| expected)
    sx=-1 if i16(x)<0 else 1; sy=-1 if i16(y)<0 else 1
    q=((abs(i16(x))<<16)//abs(i16(y)))>>1
    return i16(i16(q)*sy*sx)
def mat_mul(A,B):       # e578 : C = A x B (row major, mul15 products, 16-bit wrap sums)
    C=[0]*9
    for i in range(3):
        for j in range(3):
            C[3*i+j]=i16(sum(mul15(A[3*i+k],B[3*k+j]) for k in range(3)))
    return C
def euler_from_mat(M):  # 2c9e -> (yaw,pitch,roll) as u16/ i16 raw
    pitch=asin_q15(-M[5]); c=cos16(pitch)
    if c!=0:
        if abs(M[2])<0x5a81: yaw=asin_q15(abs(ratio_q15(M[2],c)))
        else: yaw=acos_q15(abs(ratio_q15(M[8],c)))
        if M[2]<=0 and M[8]<0: yaw=u16(yaw+0x8000)
        if M[2]>0 and M[8]<0: yaw=u16(0x8000-yaw)
        if M[2]<0 and M[8]>0: yaw=u16(-yaw)
        if abs(M[3])<0x5a81: roll=asin_q15(abs(ratio_q15(M[3],c)))
        else: roll=acos_q15(abs(ratio_q15(M[4],c)))
        if M[3]<=0 and M[4]<0: roll=u16(roll+0x8000)
        if M[3]>0 and M[4]<0: roll=u16(0x8000-roll)
        if M[3]<0 and M[4]>0: roll=u16(-roll)
    else:
        roll=0; yaw=asin_q15(M[1])
        if M[3]<=0 and M[4]<0: yaw=u16(yaw+0x8000)
        if M[3]>0 and M[4]<0: yaw=u16(0x8000-yaw)
        if M[3]<0 and M[4]>0: yaw=u16(-yaw)
    return yaw,pitch,roll
def joy_scale(raw,mn,ce,mx,lo_rng,hi_rng):
    """10a8:0cff for one axis -> (out_byte, mn, mx, lo_rng, hi_rng). 16-bit unsigned compares."""
    d=(raw-ce)&0xffff
    if raw==ce: return 0x7f,mn,mx,lo_rng,hi_rng
    if raw<ce:
        if raw>mn:
            dd=(ce-raw)&0xffff; q=((dd<<16)//lo_rng)&0xffff   # div overflow (dd>=lo_rng) raises #DE in the original
            return ((~q&0xffff)>>1)>>8,mn,mx,lo_rng,hi_rng
        return 0,raw,mx,(ce-raw)&0xffff,hi_rng
    if raw<mx:
        q=((d<<16)//hi_rng)&0xffff
        return (((q>>1)>>8)+0x80)&0xff,mn,mx,lo_rng,hi_rng
    return 0xff,mn,raw,lo_rng,d
def build_mat(a,b,c):   # e26b: yaw a, pitch b, roll c -> 9 words (row major, Q15)
    sa,ca,sb,cb,sc,cc=sin16(a),cos16(a),sin16(b),cos16(b),sin16(c),cos16(c)
    M=[0]*9
    M[0]=i16(mul15(ca,cc)+mul15(sa,mul15(sb,sc)))
    M[1]=i16(mul15(sa,mul15(sb,cc))-mul15(ca,sc))
    M[2]=mul15(sa,cb)
    M[3]=mul15(cb,sc)
    M[4]=mul15(cb,cc)
    M[5]=i16(-sb)
    M[6]=i16(mul15(ca,mul15(sb,sc))-mul15(sa,cc))
    M[7]=i16(mul15(ca,mul15(sb,cc))+mul15(sc,sa))
    M[8]=mul15(cb,ca)
    return M
