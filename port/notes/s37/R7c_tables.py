"""R7c: dump 14B2 math tables and fit closed formulas."""
import struct, math
D = open('C:/Coding/F117-2/work/unpacked/VGAME.EXE', 'rb').read()
I = D[struct.unpack_from('<H', D, 8)[0] * 16:]
T = 0x14B20
def w(o, n, signed=False): return list(struct.unpack_from('<%d%s' % (n, 'h' if signed else 'H'), I, T + o))
sin = w(0, 2048); atan = w(0x1000, 4096); hyp = w(0x3000, 257); asn = w(0x3202, 1024)
print('sin first', sin[:8], 'last', sin[-4:])
print('atan first', atan[:8], 'last', atan[-4:])
print('hyp first', hyp[:8], 'last', hyp[-4:])
print('asin first', asn[:8], 'last', asn[-4:])
print('after asin', w(0x3A02, 8))
DS = 0x1E230
print('DS:9178', [hex(x) for x in struct.unpack_from('<8H', I, DS + 0x9178)])
print('DS:0D22', struct.unpack_from('<17h', I, DS + 0xD22))
def fit(name, tab, fns):
    for fname, f in fns:
        bad = [i for i, v in enumerate(tab) if f(i) != v]
        print(name, fname, 'mismatches', len(bad), bad[:10], [(tab[i], f(i)) for i in bad[:5]])
fit('sin', sin, [
    ('round(32767*sin(i*pi/4096))', lambda i: int(math.floor(32767 * math.sin(i * math.pi / 4096) + 0.5))),
    ('round(32768*sin) clamp', lambda i: min(32767, int(math.floor(32768 * math.sin(i * math.pi / 4096) + 0.5)))),
    ('round(32767*sin((i+.5)))', lambda i: int(math.floor(32767 * math.sin((i + .5) * math.pi / 4096) + 0.5))),
    ('round(32768*sin((i+.5))) clamp', lambda i: min(32767, int(math.floor(32768 * math.sin((i + .5) * math.pi / 4096) + 0.5)))),
    ('trunc 32768 sin(i+.5)', lambda i: min(32767, int(32768 * math.sin((i + .5) * math.pi / 4096)))),
    ('trunc 32767 sin', lambda i: int(32767 * math.sin(i * math.pi / 4096))),
    ('trunc 32768 sin clamp', lambda i: min(32767, int(32768 * math.sin(i * math.pi / 4096)))),
])
K = 32768 / math.pi
R = lambda v: int(math.floor(v + 0.5))
fit('atan', atan, [
    ('round(atan(i/4096)*32768/pi)', lambda i: R(math.atan(i / 4096) * K)),
    ('trunc', lambda i: int(math.atan(i / 4096) * K)),
    ('round (i+.5)', lambda i: R(math.atan((i + .5) / 4096) * K)),
    ('trunc (i+.5)', lambda i: int(math.atan((i + .5) / 4096) * K)),
    ('round(atan(i/4095))', lambda i: R(math.atan(i / 4095) * K)),
])
fit('hyp', hyp, [
    ('round((sqrt(1+(i/256)^2)-1)*65536)', lambda i: R((math.sqrt(1 + (i / 256) ** 2) - 1) * 65536)),
    ('trunc', lambda i: int((math.sqrt(1 + (i / 256) ** 2) - 1) * 65536)),
    ('round (i+.5)', lambda i: R((math.sqrt(1 + ((i + .5) / 256) ** 2) - 1) * 65536)),
    ('trunc (i+.5)', lambda i: int((math.sqrt(1 + ((i + .5) / 256) ** 2) - 1) * 65536)),
])
K2 = 32768 / math.pi
fit('asin', asn, [
    ('round(asin(i/1024)*32768/pi)', lambda i: R(math.asin(i / 1024) * K2)),
    ('trunc', lambda i: int(math.asin(i / 1024) * K2)),
    ('round (i+.5)', lambda i: R(math.asin(min(1, (i + .5) / 1024)) * K2)),
    ('round(asin(i/1023))', lambda i: min(16383, R(math.asin(i / 1023) * K2))),
    ('round((i+1)/1024)', lambda i: min(16383, R(math.asin((i + 1) / 1024) * K2))),
])
