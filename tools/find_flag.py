#!/usr/bin/env python3
"""Find every script instruction that writes or tests one story flag bit.
usage: find_flag.py BYTE BIT [EPISODE]"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rom import ROM, u16, u32, s16
SIZE = {0: 2, 1: 8, 2: 14, 3: 16, 4: 16, 5: 6, 6: 6, 7: 4, 8: 12, 9: 6, 0xA: 6, 0xB: 6, 0xC: 4, 0xD: 4, 0xE: 10, 0xF: 12,
        0x10: 8, 0x11: 8, 0x12: 6, 0x13: 4, 0x14: 6, 0x15: 4, 0x16: 4, 0x17: 4, 0x18: 8, 0x19: 4, 0x1A: 10, 0x1B: 12,
        0x1C: 4, 0x1D: 4, 0x1E: 6, 0x1F: 4, 0x20: 4}
byte, bit = int(sys.argv[1]), int(sys.argv[2]); ep = int(sys.argv[3]) if len(sys.argv) > 3 else 0
hdr = u32(0x31AF2 + ep * 4); scripts = u32(hdr + 0x2C); tbl = u32(hdr + 0x28); text = u32(hdr + 0x20)
def name(o):
    a = text + u32(tbl + o * 8 + 4); return ROM[a:ROM.index(b'\0', a)].decode('ascii', 'replace')
def walk(pc, end, where):
    verb = None
    while pc < end:
        op = u16(pc); n = 8 + s16(pc + 2) if op == 0x21 else SIZE.get(op, 0)
        if not n: return
        if op == 1: verb = (u16(pc + 2), u16(pc + 4))
        if op == 8 and (u16(pc + 10) & 1) and u16(pc + 2) == byte and u16(pc + 4) == bit:
            print('%s: at %06X sets it to %s (under verb %s)' % (where, pc, u16(pc + 6) if not (u16(pc + 10) & 0x8C) else 'a computed value', verb))
        if op in (3, 4):
            f = u16(pc + 14)
            if ((f & 1) and u16(pc + 6) == byte and u16(pc + 8) == bit) or ((f & 4) and u16(pc + 10) == byte and u16(pc + 12) == bit):
                print('%s: at %06X tests it (under verb %s)' % (where, pc, verb))
        pc += n
count = 0; p = u32(hdr + 4)
while p != u32(hdr + 8):
    p += 14 + (4 if u16(p + 12) & 0x200 else 0); count += 1
for o in range(count):
    blk = scripts + u32(tbl + o * 8); walk(blk + 6, blk + 6 + u16(blk), 'object %d %s' % (o + 3, name(o)))
for r in range((u32(hdr + 0x0C) - u32(hdr + 8)) // 0x14):
    e = u32(hdr + 8) + r * 0x14
    for f in (0x0C, 0x10):
        q = scripts + u32(e + f); walk(q + 4, q + 4 + s16(q), 'room %d script +%X' % (r + 2, f))
st = u32(hdr + 0x30); a = st + 6; n = s16(st + 4); walk(a, a + n, 'init-1'); b = a + n; walk(b + 2, b + 2 + s16(b), 'init-2')
