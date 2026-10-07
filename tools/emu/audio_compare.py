"""Compare the port's rendered sound (a WAV) with the reference emulator's.
Prints energy per frequency band for each. Run with work/venv/bin/python.
usage: audio_compare.py port.wav frames"""
import sys, os, struct, ctypes as C
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpgx
from gpgx import Emu

def bands(x, rate):
    x = x - x.mean()
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / rate)
    edges = [0, 150, 400, 1000, 2500, 6000, 12000, 30000]
    tot = spec.sum() or 1
    return ['%5.1f%%' % (100 * spec[(f >= a) & (f < b)].sum() / tot) for a, b in zip(edges, edges[1:])], float(np.sqrt((x ** 2).mean()))

port = np.frombuffer(open(sys.argv[1], 'rb').read()[44:], dtype='<i2').astype(float)[::2]
frames = int(sys.argv[2])
e = Emu(trace=False)
buf = bytearray()
def batch(data, n):
    buf.extend(C.string_at(data, n * 4)); return n
cb = gpgx.AUDIOB_FN(batch); e._keep.append(cb); e.core.retro_set_audio_sample_batch(cb)
e.run(frames)
emu = np.frombuffer(bytes(buf), dtype='<i2').astype(float)[::2]
print('bands Hz:     0-150  -400   -1k   -2.5k   -6k   -12k   -30k    rms')
for name, x, rate in (('emulator', emu, 44100), ('port', port, 53267)):
    for t0 in (2, 8, 14):
        seg = x[int(t0 * rate):int((t0 + 4) * rate)]
        b, rms = bands(seg, rate)
        print('%-8s t=%2d %s  %7.1f' % (name, t0, ' '.join(b), rms))
