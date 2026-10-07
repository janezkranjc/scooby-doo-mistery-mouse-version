#!/usr/bin/env python3
"""Write a readable solution from the action lists in tests/solution_ep*.txt.

usage: solution_text.py out.txt
Object and verb names are read from the ROM, so the output is for the ROM's
owner and is not kept in the repository.
"""
import sys, os, subprocess
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = open(os.path.join(ROOT, 'rom', 'Scooby-Doo Mystery (USA).md'), 'rb').read()
u32 = lambda a: int.from_bytes(ROM[a:a + 4], 'big')
def cstr(a): return ROM[a:ROM.index(b'\0', a)].decode('ascii', 'replace').strip()
def verb(v):
    a = 0x2FE5E
    for _ in range(v): a = ROM.index(b'\0', a) + 1
    return cstr(a)
def name(ep, obj):
    hdr = u32(0x31AF2 + ep * 4)
    n = cstr(u32(hdr + 0x20) + u32(u32(hdr + 0x28) + (obj - 3) * 8 + 4))
    return n or 'object %d' % obj
ORD = ['first', 'second', 'third', 'fourth', 'fifth']
WAY = ['left', 'right', 'down']
TITLE = ["Blake's Hotel", 'Ha Ha Carnival']
out = ['Scooby-Doo Mystery (Genesis): solution',
       '',
       'Found by search in the port and checked by replaying it to the credits.',
       'It is a route that works, not the shortest one. "Walk to" means walk',
       'onto that exit or spot. Lines to say are counted among the lines still',
       'on offer, from the top. Room numbers are the game\'s own and only mark',
       'where the room changes.', '']
for ep in (0, 1):
    out += ['', 'Episode %d: %s' % (ep + 1, TITLE[ep]), '-' * 32]
    acts = [tuple(map(int, l.split())) for l in open(os.path.join(ROOT, 'tests', 'solution_ep%d.txt' % ep)) if l.strip() and l[0] != '#']
    press = '100:S,700:S,1000:S,1300:S,1600:S' if ep == 0 else '100:S,700:D,760:S,1000:S,1300:S,1600:S'
    subprocess.run([os.path.join(ROOT, 'build', 'scooby'), '--replay', 'tests/solution_ep%d.txt' % ep, '--mute', '--press', press], cwd=ROOT, capture_output=True)
    notes = {}
    for l in open(os.path.join(ROOT, 'work', 'out', 'replay_ep%d.txt' % ep)):
        if l.startswith('#@ '):
            k, room, rank, count = map(int, l.split()[1:])
            notes[k] = (room, rank, count)
    room = None
    for i, (v, o, s) in enumerate(acts, 1):
        r, rank, count = notes.get(i - 1, (None, 0, 0))
        if r != room:
            room = r
            out.append('  [room %s]' % r)
        if v == 15: t = 'Wait a few seconds'
        elif v == 11:
            t = 'Walk to %s' % name(ep, o)
            if s: t += ', then choose %s' % WAY[s % 6]
        elif v == 8:
            t = '%s %s' % (verb(v), name(ep, o))
            picks, p = [], s
            while p: picks.append(p % 6); p //= 6
            if picks: t += ' (say the %s line)' % ', then the '.join(ORD[min(k, 4)] for k in picks)
        elif s:
            t = '%s %s %s %s' % (verb(v), name(ep, o), 'to' if v == 6 else 'with', name(ep, s))
        else:
            t = '%s %s' % (verb(v), name(ep, o))
        if count > 1 and v != 15:
            t += ' (number %d of the %d called %s here, counting from the left)' % (rank, count, name(ep, o))
        out.append('%3d. %s' % (i, t))
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
