"""Unicorn harness for VGAME.EXE near functions (image offset == linear address, DS=SS=0x1e23)."""
import struct
from unicorn import *
from unicorn.x86_const import *
D=open('C:/Coding/F117-2/work/unpacked/VGAME.EXE','rb').read()
IMG=D[struct.unpack_from('<H',D,8)[0]*16:]
DSEG=0x1e23
RET=0xFFFF
class Emu:
    def __init__(s):
        s.uc=Uc(UC_ARCH_X86,UC_MODE_16)
        s.uc.mem_map(0,0x110000)   # 1MB+64K
        s.uc.mem_write(0,IMG)
        s.calls=[]; s.keys=[]
        uc=s.uc
        for r in (UC_X86_REG_DS,UC_X86_REG_ES,UC_X86_REG_SS): uc.reg_write(r,DSEG)
        uc.reg_write(UC_X86_REG_CS,0)
        # slot stubs -> retf, logged
        s.slots={}
        for slot in range(0,110):
            a=DSEG*16+0x11a+5*slot
            uc.mem_write(a,b'\xcb\x90\x90\x90\x90')
            s.slots[a]=slot
        for a in s.slots:
            uc.hook_add(UC_HOOK_CODE,s.hk_slot,begin=a,end=a)
        # kbd stubs
        uc.hook_add(UC_HOOK_CODE,s.hk_kbhit,begin=0xf0ba,end=0xf0ba)
        uc.hook_add(UC_HOOK_CODE,s.hk_bioskey,begin=0xf360,end=0xf360)
        s.patched=set()
    def retf_patch(s,seg,off):
        a=seg*16+off; s.uc.mem_write(a,b'\xcb'); 
    def retn_patch(s,off):
        s.uc.mem_write(off,b'\xc3')
    def _ret_near(s):
        uc=s.uc; sp=uc.reg_read(UC_X86_REG_SP); ssb=DSEG*16
        ip=struct.unpack('<H',uc.mem_read(ssb+sp,2))[0]
        uc.reg_write(UC_X86_REG_SP,sp+2); uc.reg_write(UC_X86_REG_IP,ip)
    def hk_kbhit(s,uc,addr,size,ud):
        uc.reg_write(UC_X86_REG_AX,1 if s.keys else 0); s._ret_near()
    def hk_bioskey(s,uc,addr,size,ud):
        k=s.keys.pop(0) if s.keys else 0
        uc.reg_write(UC_X86_REG_AX,k); s._ret_near()
    def hk_slot(s,uc,addr,size,ud):
        slot=s.slots[addr]; sp=uc.reg_read(UC_X86_REG_SP); ssb=DSEG*16
        a=struct.unpack('<8h',uc.mem_read(ssb+sp+4,16))
        s.calls.append((slot,)+a)
        uc.reg_write(UC_X86_REG_AX,0)
    def w(s,off,val,sz=2):
        s.uc.mem_write(DSEG*16+off,int(val).to_bytes(sz,'little',signed=val<0))
    def r(s,off,sz=2,signed=False):
        return int.from_bytes(s.uc.mem_read(DSEG*16+off,sz),'little',signed=signed)
    def wb(s,off,b): s.uc.mem_write(DSEG*16+off,bytes(b))
    def rb(s,off,n): return bytes(s.uc.mem_read(DSEG*16+off,n))
    def call(s,addr,args=(),cs=0,maxins=3000000):
        uc=s.uc; sp=0xF800; ssb=DSEG*16
        for a in reversed(args):
            sp-=2; uc.mem_write(ssb+sp,(a&0xffff).to_bytes(2,'little'))
        sp-=2; uc.mem_write(ssb+sp,RET.to_bytes(2,'little'))
        uc.reg_write(UC_X86_REG_SP,sp); uc.reg_write(UC_X86_REG_BP,0)
        uc.reg_write(UC_X86_REG_CS,cs)
        uc.emu_start(addr,RET,count=maxins)
        return uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX)
def s16(v): v&=0xffff; return v-0x10000 if v&0x8000 else v

def add_stubs(e):
    """near stubs that log and return (04bb wait, 4926 end-mission); far stubs for 10a8 joystick/HUD glue"""
    uc=e.uc
    def mk_near(name,nargs):
        def hk(uc_,addr,size,ud):
            sp=uc_.reg_read(UC_X86_REG_SP); ssb=DSEG*16
            a=struct.unpack('<h',uc_.mem_read(ssb+sp+2,2))[0] if nargs else None
            e.calls.append((name,a)); uc_.reg_write(UC_X86_REG_AX,0); e._ret_near()
        return hk
    for addr,name,n in ((0x04bb,'wait',1),(0x4926,'end',1)):
        uc.hook_add(UC_HOOK_CODE,mk_near(name,n),begin=addr,end=addr)
    def mk_far(name):
        def hk(uc_,addr,size,ud):
            sp=uc_.reg_read(UC_X86_REG_SP); ssb=DSEG*16
            ip,cs=struct.unpack('<HH',uc_.mem_read(ssb+sp,4))
            uc_.reg_write(UC_X86_REG_SP,sp+4); uc_.reg_write(UC_X86_REG_CS,cs); uc_.reg_write(UC_X86_REG_IP,ip)
            e.calls.append(('far',name))
        return hk
    for seg,off in ((0x10a8,0x08),(0x10a8,0xc87),(0x10a8,0xca8)):
        a=seg*16+off
        uc.hook_add(UC_HOOK_CODE,mk_far('%04x:%04x'%(seg,off)),begin=a,end=a)
