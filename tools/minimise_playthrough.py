#!/usr/bin/env python3
"""Shorten a playthrough list by dropping actions the ending does not need.

usage: minimise_playthrough.py EPISODE in.txt out.txt
Removes runs of actions, then single ones, keeping a removal only when the
strict replay (`scooby --replay`) still reaches the ending.
"""
import subprocess, sys, os, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ep, src, dst = int(sys.argv[1]), sys.argv[2], sys.argv[3]
press = '100:S,700:S,1000:S,1300:S,1600:S' if ep == 0 else '100:S,700:D,760:S,1000:S,1300:S,1600:S'
acts = [l.strip() for l in open(src) if l.strip() and not l.startswith('#')]
tmp = tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False).name

def ok(a):
    open(tmp, 'w').write('\n'.join(a) + '\n')
    try:
        r = subprocess.run([os.path.join(ROOT, 'build', 'scooby'), '--replay', tmp, '--mute', '--press', press],
                           capture_output=True, text=True, cwd=ROOT, timeout=120)
    except subprocess.TimeoutExpired:
        return False
    return 'episode reached its ending' in r.stderr

assert ok(acts), 'the input does not reach the ending'
size = len(acts) // 2
while size >= 1:
    i = 0
    while i < len(acts):
        trial = acts[:i] + acts[i + size:]
        if ok(trial):
            acts = trial
            print('%d left' % len(acts), flush=True)
        else:
            i += size
    size //= 2
open(dst, 'w').write('\n'.join(acts) + '\n')
print('kept %d actions' % len(acts))
