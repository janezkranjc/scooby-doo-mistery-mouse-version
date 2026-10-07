#!/usr/bin/env python3
"""Print a slice of the exported Ghidra disassembly: lst.py START END (hex)."""
import sys, re, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
on = False
for line in open(os.path.join(ROOT, 'work', 'out', 'disasm.txt')):
    m = re.match(r'^([0-9A-F]{6})  ', line)
    if m:
        a = int(m.group(1), 16)
        on = lo <= a <= hi
        if a > hi: break
    if on: sys.stdout.write(line)
