#!/usr/bin/env python3
"""Enter every room of both episodes in the port and run it for ten seconds.
Reports rooms that crash, hang, or print script errors."""
import subprocess, sys, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
exe = os.path.join(ROOT, 'build', 'scooby')
out = os.path.join(ROOT, 'work', 'rooms'); os.makedirs(out, exist_ok=True)
bad = 0
for ep, rooms in ((0, 21), (1, 30)):
    press = '100:S,700:S,1000:S,1300:S,1600:S' if ep == 0 else '100:S,700:D,760:S,1000:S,1300:S,1600:S'
    for room in range(2, rooms + 2):
        d = os.path.join(out, 'e%d_r%02d' % (ep, room)); os.makedirs(d, exist_ok=True)
        try:
            r = subprocess.run([exe, '--frames', '2800', '--press', press, '--room', str(room), '--dump-dir', d, '--dump-at', '2790'],
                               cwd=ROOT, capture_output=True, text=True, timeout=60)
            status = 'ok' if r.returncode == 0 else 'exit %d' % r.returncode
            err = r.stderr.strip().splitlines()
        except subprocess.TimeoutExpired:
            status, err = 'HANG', []
        if status != 'ok' or err:
            bad += 1
            print('episode %d room %2d: %s %s' % (ep, room, status, '; '.join(err[:3])))
print('rooms with problems:', bad)
