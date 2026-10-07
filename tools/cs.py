#!/usr/bin/env python3
"""Linear 68000 disassembly of a ROM range with Capstone: cs.py START END (hex).

A second opinion for places where the Ghidra listing is fragmented.
Run with the project virtualenv: work/venv/bin/python tools/cs.py 1006C 10118
"""
import sys, os
import capstone
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rom = open(os.path.join(ROOT, 'rom', 'Scooby-Doo Mystery (USA).md'), 'rb').read()
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
a = lo
while a < hi:
    ins = next(md.disasm(rom[a:a + 12], a, 1), None)
    if ins is None:
        print('%06X  dc.w $%04X' % (a, int.from_bytes(rom[a:a + 2], 'big'))); a += 2; continue
    print('%06X  %-10s %s' % (a, ins.mnemonic, ins.op_str)); a += ins.size
