import struct, math, itertools
D = open('C:/Coding/F117-2/work/unpacked/VGAME.EXE', 'rb').read()
I = D[struct.unpack_from('<H', D, 8)[0] * 16:]
T = 0x14B20
asn = list(struct.unpack_from('<1024H', I, T + 0x3202))
hyp = list(struct.unpack_from('<257H', I, T + 0x3000))
print('hyp trunc-eps', sum(1 for i in range(257) if int((math.sqrt(1 + (i / 256) ** 2) - 1) * 65536 - 1e-7) != hyp[i]))
best = []
for scale in (16384, 16383, 16383.5, 16384.5, 16385, 32768/math.pi*math.pi/2):
    for den in (1024, 1023, 1023.5, 1024.5, 1025):
        for off in (0, 0.5, 1, -0.5):
            for mode in ('round', 'trunc', 'ceil'):
                f = {'round': lambda v: math.floor(v + .5), 'trunc': int, 'ceil': math.ceil}[mode]
                bad = 0
                for i in range(1023):
                    x = (i + off) / den
                    if x > 1: x = 1
                    if f(math.asin(x) * scale / (math.pi / 2)) != asn[i]: bad += 1
                best.append((bad, scale, den, off, mode))
best.sort(); print(best[:6])
# differences profile
d = [asn[i] - math.asin(i / 1024) * 16384 / (math.pi / 2) for i in range(1023)]
print('diff min/max', min(d), max(d), [round(x, 2) for x in d[:20]])
print([round(x, 2) for x in d[1000:1023]])
