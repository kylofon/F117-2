"""Function index for an unpacked F-117A executable (work/unpacked/*.EXE, see tools/unlzexe.py).

The programs are Microsoft C (5.x/6) medium/large model: several code segments reached through far
calls, one DGROUP. Segments are taken from the MZ relocation table (every relocated word that holds a
segment below DGROUP starts a code segment). Recursive-descent disassembly from the entry point, every
relocated `call far` target, near call targets, `push bp; mov bp,sp` prologues, far code pointers
(`mov r,off / mov r,seg`, `mov [m],off / mov [m+2],seg`), assembly entry points that load DGROUP into
DS, and `jmp cs:[reg+table]` jump tables.

Addresses are `SSSS:OOOO` with the segment as stored in the file (load segment 0); image offset =
SSSS*16 + OOOO, and Ghidra (loaded at 1000:0000) shows segment SSSS+1000. DS-relative operands are
DGROUP offsets (functions that load DS themselves are flagged `sets_ds`).

INT numbers carry the AH value when it is a known immediate (`21.3d`); I/O ports carry the `mov dx,imm`
value when known.

usage: f117index.py work/unpacked/<NAME>.EXE port/<name> [gaps]
       writes port/<name>_functions.json/.csv and work/<name>_starts.txt, prints the segment map.
"""
import bisect, json, os, re, struct, sys
from collections import defaultdict
from capstone import Cs, CS_ARCH_X86, CS_MODE_16
from capstone.x86 import (X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_INVALID, X86_REG_BP, X86_REG_SP,
                          X86_REG_DS, X86_REG_CS)

exe, out_base = sys.argv[1], sys.argv[2]
d = open(exe, 'rb').read()
hdr = struct.unpack_from('<H', d, 8)[0] * 16
nrel = struct.unpack_from('<H', d, 6)[0]
relo_off = struct.unpack_from('<H', d, 0x18)[0]
entry_ip, entry_cs = struct.unpack_from('<HH', d, 0x14)
img = d[hdr:]
entry = entry_cs * 16 + entry_ip

# MSC startup: mov ah,30h / int 21h / cmp al,2 / jae +N / (xor ax,ax / push es / push ax / retf | int 20h)
# / mov di,DGROUP
m = re.match(rb'\xb4\x30\xcd\x21\x3c\x02\x73[\x02\x05](?:\xcd\x20|\x33\xc0\x06\x50\xcb)\xbf(..)',
             img[entry:entry + 16], re.S)
if not m:
    sys.exit('%s: MSC startup pattern not found at entry %05x' % (exe, entry))
DGROUP = struct.unpack('<H', m.group(1))[0]
DS_BASE = DGROUP * 16

relocs = set()
seg_values = set()
for i in range(nrel):
    o, s = struct.unpack_from('<HH', d, relo_off + 4 * i)
    p = s * 16 + o
    relocs.add(p)
    seg_values.add(struct.unpack_from('<H', img, p)[0])
SEGS = sorted({0} | {s for s in seg_values if s * 16 < DS_BASE})


def seg_of(lin):
    return SEGS[bisect.bisect_right(SEGS, lin >> 4) - 1]


def seg_end_of(lin):
    return next((x * 16 for x in SEGS if x * 16 > lin), DS_BASE)


def fmt(lin):
    s = seg_of(lin)
    return '%04x:%04x' % (s, lin - s * 16)


def string_at(off):
    p = DS_BASE + off
    if not (DS_BASE <= p < len(img)) or (p > DS_BASE and img[p - 1] != 0):
        return None
    mm = re.match(rb'[\x20-\x7e\r\n]{4,}', img[p:p + 80])
    if mm and p + len(mm.group(0)) < len(img) and img[p + len(mm.group(0))] == 0:
        return mm.group(0).decode('latin-1')
    return None


# ---- seeds ----
seeds = {entry}
for mm in re.finditer(rb'\x55\x8b\xec', img[:DS_BASE]):
    seeds.add(mm.start())
for p in relocs:
    if not 4 <= p < DS_BASE:
        continue
    sv = struct.unpack_from('<H', img, p)[0]
    if sv * 16 >= DS_BASE:
        continue
    if img[p - 3] == 0x9A:                      # call far off:seg (reloc on the seg word)
        seeds.add(sv * 16 + struct.unpack_from('<H', img, p - 2)[0])
    elif 0xB8 <= img[p - 1] <= 0xBF and 0xB8 <= img[p - 4] <= 0xBF:
        seeds.add(sv * 16 + struct.unpack_from('<H', img, p - 3)[0])   # mov r16,off / mov r16,seg
    elif img[p - 4:p - 2] == b'\xc7\x06' and img[p - 10:p - 8] == b'\xc7\x06' \
            and struct.unpack_from('<H', img, p - 2)[0] == struct.unpack_from('<H', img, p - 8)[0] + 2:
        seeds.add(sv * 16 + struct.unpack_from('<H', img, p - 6)[0])   # mov [m],off / mov [m+2],seg
# Assembly entry points that load DGROUP themselves: [cli] [push ...] mov ax,DGROUP / mov ds,ax,
# directly after the end of the previous routine.
PUSHES = (0xFA, 0xFB, 0xFC, 0x9C, 0x1E, 0x06, 0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57)
ENDS = (0xC3, 0xCB, 0xCF, 0x90)
for mm in re.finditer(re.escape(b'\xb8' + struct.pack('<H', DGROUP) + b'\x8e\xd8'), img[:DS_BASE]):
    q = mm.start()
    while q > 0 and img[q - 1] in PUSHES:
        q -= 1
    if q > 0 and img[q - 1] in ENDS:
        seeds.add(q)
seeds = {s for s in seeds if s < DS_BASE}
func_starts = set(seeds)

# ---- recursive descent ----
md = Cs(CS_ARCH_X86, CS_MODE_16)
md.detail = True
insn_at = {}
calls_from = defaultdict(set)       # insn linear -> targets
jump_tables = defaultdict(list)     # table linear -> targets
work, seen = list(seeds), set()


def descend():
    while work:
        a = work.pop()
        if a in seen or a >= DS_BASE:
            continue
        base = seg_of(a) * 16
        seg_end = seg_end_of(a)
        # Disassemble at segment-relative addresses so capstone computes 16-bit branch targets.
        for ins in md.disasm(img[a:min(a + 0x1000, seg_end)], a - base):
            lin = base + ins.address
            if lin in seen:
                break
            seen.add(lin)
            insn_at[lin] = ins
            mn = ins.mnemonic
            op0 = ins.operands[0] if ins.operands else None
            if mn == 'lcall' and ins.bytes[0] == 0x9A:
                if lin + 3 in relocs:
                    t = struct.unpack_from('<H', img, lin + 3)[0] * 16 + struct.unpack_from('<H', img, lin + 1)[0]
                    calls_from[lin].add(t)
                    func_starts.add(t)
                    work.append(t)
            elif mn == 'call' and op0 is not None and op0.type == X86_OP_IMM:
                t = base + (op0.imm & 0xFFFF)
                calls_from[lin].add(t)
                func_starts.add(t)
                work.append(t)
            elif mn.startswith('j') and op0 is not None and op0.type == X86_OP_IMM:
                work.append(base + (op0.imm & 0xFFFF))
            elif mn == 'jmp' and op0 is not None and op0.type == X86_OP_MEM and op0.mem.segment == X86_REG_CS \
                    and (op0.mem.base != X86_REG_INVALID or op0.mem.index != X86_REG_INVALID) \
                    and op0.mem.disp != 0:
                # jmp cs:[reg + table]: take entries while they point inside this segment
                tbl = base + (op0.mem.disp & 0xFFFF)
                for k in range(128):
                    e = tbl + 2 * k
                    if e + 2 > seg_end or e in seen:
                        break
                    t = base + struct.unpack_from('<H', img, e)[0]
                    if not base <= t < seg_end or tbl <= t <= e:
                        break
                    jump_tables[tbl].append(t)
                    work.append(t)
            if mn in ('ret', 'retf', 'iret', 'jmp', 'ljmp', 'hlt'):
                break
            if mn == 'int' and op0 is not None and op0.imm == 0x20:
                break


descend()


# ---- functions ----
starts = sorted(func_starts)
funcs = {}
for i, s in enumerate(starts):
    nxt = min(starts[i + 1] if i + 1 < len(starts) else DS_BASE, seg_end_of(s))
    body = [a for a in range(s, nxt) if a in insn_at]
    if not body:
        continue
    f = dict(start=fmt(s), image='%05x' % s, far=False, calls=set(), callers=set(), ds_reads=set(),
             ds_writes=set(), strings={}, ints=set(), ports=set(), sets_ds=False, insns=len(body))
    end = s
    reg = {}                        # last immediate in ah / dx, straight-line order
    for a in body:
        ins = insn_at[a]
        end = max(end, a + ins.size)
        mn = ins.mnemonic
        if mn == 'retf':
            f['far'] = True
        f['calls'] |= calls_from.get(a, set())
        if mn == 'int':
            n = ins.operands[0].imm & 0xFF
            ah = reg.get('ah')
            f['ints'].add('%02x' % n if ah is None or n == 0x3f else '%02x.%02x' % (n, ah))
        if mn in ('in', 'out'):
            ops = ins.op_str.split(', ')
            pr = ops[1] if mn == 'in' else ops[0]
            f['ports'].add(('%xh' % reg['dx'] if 'dx' in reg else 'dx') if pr == 'dx' else '%xh' % int(pr, 0))
        if mn in ('insb', 'outsb', 'insw', 'outsw'):
            f['ports'].add('%xh' % reg['dx'] if 'dx' in reg else 'dx')
        if len(ins.operands) == 2 and ins.operands[0].type == X86_OP_REG:
            r = ins.reg_name(ins.operands[0].reg)
            if r in ('ax', 'ah', 'dx'):
                src = ins.operands[1]
                if mn == 'mov' and src.type == X86_OP_IMM:
                    v = src.imm & 0xFFFF
                    if r == 'dx':
                        reg['dx'] = v
                    else:
                        reg['ah'] = v >> 8 if r == 'ax' else v & 0xFF
                else:
                    reg.pop('dx' if r == 'dx' else 'ah', None)
        if mn in ('call', 'lcall') or mn.startswith('j'):
            reg = {}
        if mn in ('mov', 'pop') and ins.operands and ins.operands[0].type != X86_OP_MEM \
                and ins.reg_name(ins.operands[0].reg) == 'ds':
            f['sets_ds'] = True
        for k, op in enumerate(ins.operands):
            if op.type == X86_OP_MEM:
                mem = op.mem
                if mn == 'lea' or mem.base in (X86_REG_BP, X86_REG_SP):
                    continue
                if mem.segment not in (X86_REG_INVALID, X86_REG_DS):
                    continue
                off = '%04x' % (mem.disp & 0xFFFF)
                is_dest = k == 0 and mn not in ('cmp', 'test', 'push', 'call', 'jmp', 'ljmp', 'lcall')
                if is_dest:
                    f['ds_writes'].add(off)
                if not is_dest or mn != 'mov':
                    f['ds_reads'].add(off)
            elif op.type == X86_OP_IMM and (mn == 'push' or (mn == 'mov' and ins.reg_name(ins.operands[0].reg) != 'dx')):
                v = op.imm & 0xFFFF
                sv = string_at(v) if v >= 2 else None
                if sv:
                    f['strings']['%04x' % v] = sv
    f['end'] = fmt(end)
    f['size'] = end - s
    funcs[s] = f

for s, f in funcs.items():
    for t in f['calls']:
        if t in funcs:
            funcs[t]['callers'].add(s)

rows = []
for s in sorted(funcs):
    f = funcs[s]
    f['calls'] = [fmt(t) for t in sorted(f['calls'])]
    f['callers'] = [fmt(t) for t in sorted(f['callers'])]
    for k in ('ds_reads', 'ds_writes', 'ints', 'ports'):
        f[k] = sorted(f[k])
    rows.append(f)

json.dump(dict(exe=exe, dgroup='%04x' % DGROUP, entry=fmt(entry),
               segments=['%04x' % s for s in SEGS],
               jump_tables={fmt(t): [fmt(x) for x in v] for t, v in sorted(jump_tables.items())},
               functions=rows),
          open(out_base + '_functions.json', 'w'), indent=1)
with open(out_base + '_functions.csv', 'w') as o:
    o.write('start,image,end,size,far,insns,ncalls,ncallers,ints,ports,sets_ds,strings\n')
    for f in rows:
        o.write('%s,%s,%s,%d,%d,%d,%d,%d,%s,%s,%d,"%s"\n' % (
            f['start'], f['image'], f['end'], f['size'], f['far'], f['insns'], len(f['calls']),
            len(f['callers']), ' '.join(f['ints']), ' '.join(f['ports']), f['sets_ds'],
            ' | '.join(f['strings'].values()).replace('"', "'").replace('\r', '').replace('\n', ' ')[:120]))
name = os.path.basename(out_base)
os.makedirs('work', exist_ok=True)
with open(os.path.join('work', name + '_starts.txt'), 'w', newline='\n') as o:
    for f in rows:
        o.write(f['image'] + '\n')

print('DGROUP %04x (image %05x), entry %s, %d code segments, %d functions, %d instructions, %d jump tables'
      % (DGROUP, DS_BASE, fmt(entry), len(SEGS), len(rows), len(insn_at), len(jump_tables)))
for i, sg in enumerate(SEGS):
    end = SEGS[i + 1] * 16 if i + 1 < len(SEGS) else DS_BASE
    n = sum(1 for f in rows if seg_of(int(f['image'], 16)) == sg)
    print('  seg %04x  image %05x-%05x  %6d bytes  %3d functions' % (sg, sg * 16, end, end - sg * 16, n))

cov = bytearray(DS_BASE)
for a, ins in insn_at.items():
    cov[a:a + ins.size] = b'\x01' * ins.size
for t, v in jump_tables.items():
    cov[t:t + 2 * len(v)] = b'\x01' * (2 * len(v))
print('coverage: %d of %d code bytes (%.0f%%)' % (sum(cov), DS_BASE, 100.0 * sum(cov) / DS_BASE))
if len(sys.argv) > 3:
    run = None
    for p in range(DS_BASE + 1):
        if p < DS_BASE and not cov[p]:
            run = p if run is None else run
        elif run is not None:
            if p - run >= 32 and any(img[run:p]):
                print('  gap %s-%s %5d bytes  %s' % (fmt(run), fmt(p), p - run, img[run:run + 12].hex(' ')))
            run = None
