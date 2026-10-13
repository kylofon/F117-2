"""R7a differential test of 11EB:0565 r3d_planes (random plane streams, 80pp / 8100 prefixes, BSP)."""
import struct, random
exec(open('C:/Coding/F117-2/port/notes/s37/R7a_emu.py').read().split('res = {}')[0])
rnd = random.Random(11); mm = 0; n = 0
for it in range(4000):
    cnt = rnd.randrange(1, 30); bsp = rnd.random() < .3; nparts = rnd.randrange(1, 8)
    eyes = [tuple(rs16() for _ in range(3)) for _ in range(nparts)]
    lights = [tuple(rs16() for _ in range(3)) for _ in range(nparts)]
    varsw = [rnd.choice([0, 1, 2, 0xFFFF, 0xFF, rnd.randrange(0x10000)]) for _ in range(32)]
    mode = 0 if rnd.random() < .85 else rnd.choice([0x0E, 0x12])
    nverts = rnd.randrange(1, 120)
    b = bytearray(struct.pack('<h', -cnt if bsp else cnt))
    for i in range(cnt):
        if rnd.random() < .2: b += struct.pack('<H', 0x8000 | rnd.randrange(nparts))
        if rnd.random() < .2: b += bytes([0x00, 0x81, rnd.randrange(256), rnd.choice([0, 1, 2, 0xFF, rnd.randrange(256)])])
        nx = rs16()
        while nx & 0xFFFF == 0x8100 or (nx & 0xFF00) == 0x8000: nx = rs16()
        b += struct.pack('<3hi', nx, rs16(), rs16(), rs32() if rnd.random() < .5 else rnd.randrange(-0x40000000, 0x40000000))
    for i in range(cnt):
        k = rnd.randrange(0, 8); b += bytes([k] + [rnd.randrange(nverts) for _ in range(k)])
    if bsp:
        nodes = list(range(cnt)); rnd.shuffle(nodes); pairs = [[0xFF, 0xFF] for _ in range(cnt)]
        for k in range(1, cnt):
            while True:
                par = nodes[rnd.randrange(k)]; s = rnd.randrange(2)
                if pairs[par][s] == 0xFF: pairs[par][s] = nodes[k]; break
        b += bytes([nodes[0]]) + b''.join(bytes(p) for p in pairs)
    b += b'\x99' * 8
    w(0x4000, bytes(b)); w(0x7C3C, b''.join(struct.pack('<3h', *e) for e in eyes)); w(0x7C6C, b''.join(struct.pack('<3h', *l) for l in lights))
    w(0x4800, struct.pack('<32H', *varsw)); w(0x7D1E, struct.pack('<H', 0x4800)); w(0x7D5E, bytes([mode]))
    w(0x7D2C, struct.pack('<h', nverts)); w(0x7D5C, b'\0')
    w(0x76D2, b'\xEE' * 256); w(0x77D2, b'\xEE' * 256); w(0x79D2, b'\xEE' * 256); w(0x78D2, b'\xEE' * 256)
    run(0x0565, {UC_X86_REG_SI: 0x4000})
    e = R.planes(bytes(b), eyes, lights, varsw, mode, nverts)
    ok = uc.reg_read(UC_X86_REG_SI) - 0x4000 == e['si'] and r(0x7D5C, 1)[0] == e['7D5C']
    vis = r(0x76D2, 256); sh = r(0x77D2, 256); mk = r(0x79D2, 256)
    ok &= all(vis[i] == e['vis'][i] for i in range(cnt))
    ok &= all(sh[i] == e['shade'].get(i, 0xEE) for i in range(cnt))
    clr = (nverts + 2) >> 1 << 1
    ok &= all(mk[i] == (1 if i in e['mark'] else (0 if i < clr else 0xEE)) for i in range(256))
    ok &= struct.unpack('<H', r(0x7D4C, 2))[0] == 0x7C6C + 6 * e['7D4C_part']
    if bsp: ok &= list(r(0x78D2, len(e['bsp']) + 1)) == e['bsp'] + [0xFF]
    n += 1
    if not ok:
        mm += 1
        if mm < 4: print('0565 mismatch', it, hex(uc.reg_read(UC_X86_REG_SI) - 0x4000), e['si'], r(0x7D5C,1), e['7D5C'])
print('0565 cases', n, 'mismatches', mm)
