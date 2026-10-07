"""Replay boot -> first playable room under the tracer and summarise coverage."""
import sys, os, json, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gpgx import Emu, ROOT

def ranges(flags, gap=1):
    out = []; s = None; last = None
    for i, f in enumerate(flags):
        if f:
            if s is None: s = i
            elif i - last > gap: out.append((s, last)); s = i
            last = i
    if s is not None: out.append((s, last))
    return out

def main():
    e = Emu()
    e.run(1800)
    for _ in range(3):
        e.press('START', hold=5, after=300)
    e.run(15600)
    ec = e.exec_cov(); rc = e.read_cov(); rp = e.read_pc()
    out = os.path.join(ROOT, 'work', 'out')
    open(os.path.join(out, 'exec_cov.bin'), 'wb').write(ec)
    open(os.path.join(out, 'read_cov.bin'), 'wb').write(rc)
    print('frame', e.frame)
    ex = ranges(ec, gap=16)
    print('executed code ranges (%d):' % len(ex))
    for s, t in ex: print('  %06X-%06X' % (s, t))
    # data read regions grouped by reader pc
    by_pc = collections.defaultdict(list)
    s = None
    for i in range(0x200000):
        if rc[i]:
            if s is None: s = i; pc = rp[i]
            elif rp[i] != pc: by_pc[pc].append((s, i - 1)); s = i; pc = rp[i]
        elif s is not None:
            by_pc[pc].append((s, i - 1)); s = None
    print('reader PCs:', len(by_pc))
    rows = sorted(by_pc.items(), key=lambda kv: -sum(b - a + 1 for a, b in kv[1]))
    for pc, rs in rows[:60]:
        tot = sum(b - a + 1 for a, b in rs)
        print('  pc %06X  bytes %7d  regions %5d  span %06X-%06X' % (pc, tot, len(rs), min(a for a, _ in rs), max(b for _, b in rs)))
    json.dump({('%06X' % pc): rs for pc, rs in by_pc.items()}, open(os.path.join(out, 'read_by_pc.json'), 'w'))
    dm = e.dma_hits()
    agg = collections.Counter((d['pc'], d['src'] >> 16, d['code']) for d in dm)
    print('DMA transfers:', len(dm))
    for (pc, bank, code), n in sorted(agg.items()): print('  pc %06X src bank %02X code %02X  x%d' % (pc, bank, code, n))
    json.dump(dm, open(os.path.join(out, 'dma_log.json'), 'w'))

main()
