#!/usr/bin/env python3
"""Convert a frame dump written by `scooby --dump-at` into a PNG.
usage: dump2png.py dump.bin out.png"""
import sys, struct, zlib
LV = [0, 52, 87, 116, 144, 172, 206, 255]
raw = open(sys.argv[1], 'rb').read()
w = raw[0] << 8 | raw[1]; h = 224
px = raw[4:4 + w * h * 3]
rows = b''.join(b'\x00' + bytes(LV[c] for c in px[y * w * 3:(y + 1) * w * 3]) for y in range(h))
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                              + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))
