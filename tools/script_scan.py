#!/usr/bin/env python3
"""Walk every script in both episodes and report which opcodes and native
functions they use, and anything the walker cannot decode."""
import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rom import ROM, u16, u32, s16

SIZE = {0x00: 2, 0x01: 8, 0x02: 0x0E, 0x03: 16, 0x04: 16, 0x05: 6, 0x06: 6, 0x07: 4, 0x08: 12, 0x09: 6, 0x0A: 6,
        0x0B: 6, 0x0C: 4, 0x0D: 4, 0x0E: 10, 0x0F: 12, 0x10: 8, 0x11: 8, 0x12: 6, 0x13: 4, 0x14: 6, 0x15: 4,
        0x16: 4, 0x17: 4, 0x18: 8, 0x19: 4, 0x1A: 10, 0x1B: 12, 0x1C: 4, 0x1D: 4, 0x1E: 6, 0x1F: 4, 0x20: 4}

def walk(pc, end, ops, natives, notes, where):
    while pc < end:
        op = u16(pc)
        if op == 0x21: n = 8 + s16(pc + 2)
        elif op in SIZE: n = SIZE[op]
        else:
            notes.append('%s: undecodable word %04X at %06X' % (where, op, pc)); return
        ops[op] += 1
        if op == 0x0C: natives[u16(pc + 2)].append(where)
        if op == 0x08 and (u16(pc + 0x0A) & 1) and not (u16(pc + 0x0A) & 0x10): notes.append('%s: flag toggle form of assign at %06X' % (where, pc))
        if op in (0x11, 0x18): rooms_to.add((ep, u16(pc + 2)))
        pc += n
    if pc != end: notes.append('%s: walk ended at %06X, expected %06X' % (where, pc, end))

for ep in range(2):
    rooms_to = set()
    hdr = u32(0x31AF2 + ep * 4)
    ops = collections.Counter(); natives = collections.defaultdict(list); notes = []
    scripts = u32(hdr + 0x2C)
    start = u32(hdr + 0x30)
    a = start + 6; n = s16(start + 4)
    walk(a, a + n, ops, natives, notes, 'init-1')
    b = a + n; walk(b + 2, b + 2 + s16(b), ops, natives, notes, 'init-2')
    nrooms = (u32(hdr + 0x0C) - u32(hdr + 8)) // 0x14
    for r in range(nrooms):
        e = u32(hdr + 8) + r * 0x14
        for f in (0x0C, 0x10):
            p = scripts + u32(e + f)
            walk(p + 4, p + 4 + s16(p), ops, natives, notes, 'room %d script +%X' % (r + 2, f))
    nobj = (u32(hdr + 8) - u32(hdr + 4))
    # object count from the script table size up to the script base is not stored; walk until offsets stop making sense
    tbl = u32(hdr + 0x28); count = 0
    defs = u32(hdr + 4); p = defs
    while p != u32(hdr + 8):
        flags = u16(p + 12); p += 14 + (4 if flags & 0x0200 else 0); count += 1
    for o in range(count):
        blk = scripts + u32(tbl + o * 8)
        walk(blk + 6, blk + 6 + u16(blk), ops, natives, notes, 'object %d' % (o + 3))
    print('=== episode %d: %d rooms, %d objects ===' % (ep, nrooms, count))
    print('opcodes used:', ' '.join('%02X:%d' % (k, v) for k, v in sorted(ops.items())))
    print('natives used:', ' '.join('%02X:%d' % (k, len(v)) for k, v in sorted(natives.items())))
    for k in (0x1E, 0x27, 0x32, 0x05, 0x0C, 0x21):
        if k in natives: print('  native %02X called from: %s' % (k, ', '.join(sorted(set(natives[k]))[:8])))
    print('notes (%d):' % len(notes)); [print('  ', x) for x in notes[:12]]
