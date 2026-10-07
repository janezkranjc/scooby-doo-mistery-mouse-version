"""Shared ROM access helpers for the analysis scripts."""
import os, struct
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM_PATH = os.path.join(ROOT, 'rom', 'Scooby-Doo Mystery (USA).md')
ROM = open(ROM_PATH, 'rb').read()
def u8(a): return ROM[a]
def u16(a): return struct.unpack_from('>H', ROM, a)[0]
def s16(a): return struct.unpack_from('>h', ROM, a)[0]
def u32(a): return struct.unpack_from('>I', ROM, a)[0]
def hexdump(a, n, width=16):
    out = []
    for o in range(0, n, width):
        chunk = ROM[a + o:a + o + width]
        out.append('%06X  %-*s  %s' % (a + o, width * 3, ' '.join('%02X' % c for c in chunk),
                                       ''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk)))
    return '\n'.join(out)
