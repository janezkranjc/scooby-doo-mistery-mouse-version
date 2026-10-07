"""Record every script instruction the original executes (exec loop dispatch
at 0x2430, script pointer in A5) for a scripted input sequence.
usage: script_trace.py OUT FRAMES "1900:START,2200:START"
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gpgx import Emu, DEFAULT_ROM
rom = open(DEFAULT_ROM, 'rb').read()
out, frames = sys.argv[1], int(sys.argv[2])
script = {}
for item in (sys.argv[3].split(',') if len(sys.argv) > 3 and sys.argv[3] else []):
    f, b = item.split(':'); script[int(f)] = b.split('+')
e = Emu()
e.bp(0x2430)
with open(out, 'w') as fh:
    held = []
    for f in range(1, frames + 1):
        if f in script: held = script[f]
        if f - 5 in script: held = []
        e.run(1, held)
        for h in e.bp_hits():
            a5 = h['a'][5] & 0xFFFFFF
            op = (rom[a5] << 8 | rom[a5 + 1]) if a5 < len(rom) else 0xFFFF
            fh.write('%06X %02X\n' % (a5, op))
