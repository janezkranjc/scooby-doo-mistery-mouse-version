"""Compare frames dumped by the port (scooby --frames N --dump-at ...) with the
reference emulator. For each port frame, finds the closest emulator frame in a
window of frame offsets and reports the pixel difference and the offset.

usage: compare_frames.py DUMPDIR [--window 60] [--script "300:START,..."]
"""
import sys, os, struct, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gpgx import Emu

def emu_frame(e):
    data, w, h, pitch = e.fb
    out = bytearray(w * h * 3)
    i = 0
    for y in range(h):
        for p in struct.unpack_from('<%dH' % w, data, y * pitch):
            out[i] = (p >> 13) & 7; out[i + 1] = (p >> 8) & 7; out[i + 2] = (p >> 2) & 7
            i += 3
    return w, bytes(out)

def diff(a, b):
    if len(a) != len(b): return 10**9
    if a == b: return 0
    return sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])

def bbox(a, b, w):
    xs = []; ys = []
    for i in range(0, len(a), 3):
        if a[i:i + 3] != b[i:i + 3]:
            n = i // 3; xs.append(n % w); ys.append(n // w)
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None

def main():
    d = sys.argv[1]
    window = 60
    script = {}
    args = sys.argv[2:]
    for i, a in enumerate(args):
        if a == '--window': window = int(args[i + 1])
        if a == '--script':
            for item in args[i + 1].split(','):
                f, b = item.split(':'); script[int(f)] = b.split('+')
    dumps = {}
    for path in sorted(glob.glob(os.path.join(d, 'f*.bin'))):
        n = int(os.path.basename(path)[1:7])
        raw = open(path, 'rb').read()
        w = raw[0] << 8 | raw[1]
        dumps[n] = (w, raw[4:4 + w * 224 * 3], raw[4 + w * 224 * 3:])
    last = max(dumps) + window
    want = set()
    for n in dumps:
        want.update(range(max(1, n - window), n + window + 1))
    e = Emu(trace=False)
    frames = {}
    held = []
    for f in range(1, last + 1):
        if f in script: held = script[f]
        if f - 5 in script: held = []
        e.run(1, held)
        if f in want: frames[f] = emu_frame(e)
    worst = 0
    for n in sorted(dumps):
        w, px, ram = dumps[n]
        best = (10**9, None)
        for f in range(max(1, n - window), n + window + 1):
            ew, epx = frames[f]
            if ew != w: continue
            dd = diff(px, epx)
            if dd < best[0] or (dd == best[0] and best[1] is not None and abs(f - n) < abs(best[1] - n)): best = (dd, f)
            if dd == 0 and f >= n: break
        same = diff(px, frames[n][1]) if frames[n][0] == w else -1
        print('port frame %6d: best emulator frame %s (offset %+d), differing pixels %d; same-frame diff %d'
              % (n, best[1], (best[1] - n) if best[1] else 0, best[0], same))
        if best[0] and best[1]:
            print('      differing region (x0,y0,x1,y1):', bbox(px, frames[best[1]][1], w))
        worst = max(worst, best[0])
    return 0 if worst == 0 else 1

sys.exit(main())
