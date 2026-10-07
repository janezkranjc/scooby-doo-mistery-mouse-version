#!/usr/bin/env python3
"""Condensed Capstone listing: collapses runs of structurally identical
instruction blocks (same mnemonics/operands apart from branch targets).
usage: cscond.py START END [BLOCKLEN]"""
import sys, os, re
import capstone
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rom = open(os.path.join(ROOT, 'rom', 'Scooby-Doo Mystery (USA).md'), 'rb').read()
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
ins = []
a = lo
while a < hi:
    i = next(md.disasm(rom[a:a + 12], a, 1), None)
    if i is None:
        ins.append((a, 'dc.w', '$%04X' % int.from_bytes(rom[a:a + 2], 'big'), 2)); a += 2; continue
    ins.append((a, i.mnemonic, i.op_str, i.size)); a += i.size
def key(x):
    m, o = x[1], x[2]
    if m.startswith('b') or m.startswith('db') or m == 'jsr' or m == 'jmp':
        o = re.sub(r'\$[0-9a-f]+$', '$T', o)
    return (m, o)
keys = [key(x) for x in ins]
n = len(ins); i = 0
while i < n:
    best = (0, 0)
    for L in range(2, 40):
        if i + 2 * L > n: break
        reps = 1
        while i + (reps + 1) * L <= n and keys[i + reps * L:i + (reps + 1) * L] == keys[i:i + L]: reps += 1
        if reps >= 3 and reps * L > best[0] * best[1]: best = (reps, L)
    if best[0]:
        reps, L = best
        print('        ; ---- block of %d instructions repeated %d times (%06X-%06X) ----' % (L, reps, ins[i][0], ins[i + reps * L - 1][0]))
        for j in range(L): print('%06X    %-10s %s' % (ins[i + j][0], ins[i + j][1], ins[i + j][2]))
        print('        ; ---- end of repeated block ----')
        i += reps * L
    else:
        print('%06X  %-10s %s' % (ins[i][0], ins[i][1], ins[i][2])); i += 1
