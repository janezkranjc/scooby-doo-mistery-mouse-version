#!/usr/bin/env python3
"""List the instructions of one object's script. usage: script_dump.py OBJECT [EPISODE]
OBJECT is the script object number (3 and up). Text operands are shown as offsets only."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rom import ROM, u16, u32, s16

SIZE = {0x00: 2, 0x03: 16, 0x04: 16, 0x05: 6, 0x06: 6, 0x07: 4, 0x08: 12, 0x09: 6, 0x0A: 6, 0x0B: 6, 0x0C: 4,
        0x0D: 4, 0x0E: 10, 0x0F: 12, 0x10: 8, 0x11: 8, 0x12: 6, 0x14: 6, 0x15: 4, 0x16: 4, 0x17: 4, 0x18: 8,
        0x19: 4, 0x1A: 10, 0x1B: 12, 0x1C: 4, 0x1D: 4, 0x1E: 6, 0x1F: 4, 0x20: 4}
NAME = {0x01: 'VERB', 0x02: 'CHOICE', 0x03: 'if', 0x04: 'if-block', 0x05: 'move-object', 0x06: 'anim', 0x07: 'redraw',
        0x08: 'assign', 0x09: 'set-byte', 0x0A: 'set-icon', 0x0B: 'set-state', 0x0C: 'native', 0x0D: 'sound',
        0x0E: 'move-to-point', 0x0F: 'walk-to-object', 0x10: 'say', 0x11: 'room-fade', 0x12: 'set-hotspot',
        0x13: 'TICK', 0x14: 'set-hotspot', 0x15: 'wait', 0x16: 'hide', 0x17: 'wait-anim', 0x18: 'room',
        0x19: 'show', 0x1A: 'pal-cycle', 0x1B: 'move-to-xy', 0x1C: 'copy-walking', 0x1D: 'copy-animdone',
        0x1E: 'face', 0x1F: 'wait-walk', 0x20: 'stop', 0x21: 'routine'}

def cstr(a):
    e = ROM.index(b'\0', a)
    return ROM[a:e].decode('ascii', 'replace')

def verb_name(v):
    a = 0x2FE5E
    for _ in range(v):
        a = ROM.index(b'\0', a) + 1
    return cstr(a).strip()

def cond(pc):
    f = u16(pc + 0x0E)
    def side(a, b, flagbit, objbit):
        if f & flagbit: return 'flag[%d].%d' % (a, b)
        if f & objbit: return ('room' if (objbit == 2 and b == 1 and a == 6) else 'obj%d+%d' % (b, a))
        return str(a)
    op = '==' if f & 0x10 else '>' if f & 0x20 else '<' if f & 0x40 else '!='
    return '%s %s %s' % (side(u16(pc + 6), u16(pc + 8), 1, 2), op, side(u16(pc + 10), u16(pc + 12), 4, 8))

def main():
    if sys.argv[1] == 'room':
        room = int(sys.argv[2]); ep = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        hdr = u32(0x31AF2 + ep * 4)
        e = u32(hdr + 8) + (room - 2) * 0x14
        for f in (0x0C, 0x10):
            p = u32(hdr + 0x2C) + u32(e + f)
            print('room %d script +%X at %06X length %04X' % (room, f, p, u16(p)))
            dump_range(p + 4, p + 4 + s16(p))
        return
    obj = int(sys.argv[1]); ep = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    hdr = u32(0x31AF2 + ep * 4)
    ent = u32(hdr + 0x28) + (obj - 3) * 8
    blk = u32(hdr + 0x2C) + u32(ent)
    print('object %d "%s" script at %06X length %04X flags %04X' % (obj, cstr(u32(hdr + 0x20) + u32(ent + 4)), blk, u16(blk), u16(blk + 2)))
    dump_range(blk + 6, blk + 6 + u16(blk))

def dump_range(pc, end):
    while pc < end:
        op = u16(pc)
        args = lambda n: ' '.join('%04X' % u16(pc + 2 + 2 * i) for i in range(n))
        if op == 0x01:
            print('%06X VERB %d (%s) second=%d  block to %06X' % (pc, u16(pc + 2), verb_name(u16(pc + 2)), u16(pc + 4), pc + s16(pc + 6))); pc += 8
        elif op == 0x13:
            print('%06X TICK block to %06X' % (pc, pc + s16(pc + 2))); pc += 4
        elif op == 0x02:
            print('%06X CHOICE block to %06X' % (pc, pc + s16(pc + 0x0A))); pc += 0x0E
        elif op == 0x21:
            n = 8 + s16(pc + 2); print('%06X   routine who=%d (%d bytes)' % (pc, u16(pc + 4), n)); pc += n
        elif op == 0x03:
            print('%06X   if %s  else jump to %06X' % (pc, cond(pc), pc + s16(pc + 2))); pc += 16
        elif op == 0x04:
            print('%06X   if-block %s  then to %06X, else skips %04X after' % (pc, cond(pc), pc + s16(pc + 2), u16(pc + 4))); pc += 16
        elif op in SIZE:
            n = SIZE[op]; print('%06X   %-15s %s' % (pc, NAME.get(op, '%02X' % op), args((n - 2) // 2))); pc += n
        else:
            print('%06X ?? %04X' % (pc, op)); break

main()
