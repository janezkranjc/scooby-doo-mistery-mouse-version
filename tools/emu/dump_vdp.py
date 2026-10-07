"""Capture reference VDP state + frame from the emulator at several points.

Each dump is: 32 register bytes, 64 CRAM words (0000BBB0GGG0RRR0), 40 VSRAM
words, 65536 VRAM bytes, 65536 work-RAM bytes, then the frame as w,h (u16)
and w*h bytes of packed 9-bit colour reduced to 3 bytes rgb (0..7 each).
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gpgx import Emu, ROOT

OUT = os.path.join(ROOT, 'work', 'vdp')
os.makedirs(OUT, exist_ok=True)

def dump(e, name):
    regs = e.vdpreg()
    cram = b''.join(struct.pack('>H', (b << 9) | (g << 5) | (r << 1)) for r, g, b in e.cram())
    vsram = b''.join(struct.pack('>H', v) for v in e.vsram())
    data, w, h, pitch = e.fb
    px = bytearray()
    for y in range(h):
        for p in struct.unpack_from('<%dH' % w, data, y * pitch):
            px += bytes(((p >> 13) & 7, (p >> 8) & 7, (p >> 2) & 7))
    with open(os.path.join(OUT, name + '.bin'), 'wb') as f:
        f.write(regs + cram + vsram + e.vram() + e.ram() + struct.pack('>HH', w, h) + bytes(px))
    e.shot(os.path.join(OUT, name + '.png'))
    print(name, 'frame', e.frame, w, h, 'regs', regs[:19].hex())

e = Emu(trace=False)
e.run(300); dump(e, 'licence')
e.run(600); dump(e, 'logo')
e.run(900); dump(e, 'title')
e.press('START', hold=5, after=120); dump(e, 'menu')
e.run(180)
e.press('START', hold=5, after=300); dump(e, 'menu2')
e.press('START', hold=5, after=300); dump(e, 'intro_title')
e.run(1020); dump(e, 'intro_van')
e.run(3600); dump(e, 'hotel_ext')
e.run(9000); dump(e, 'lobby')
e.save(os.path.join(ROOT, 'work', 'states', 'lobby.st'))
