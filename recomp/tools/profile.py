#!/usr/bin/env python3
"""profile.py <binary> <W32_PROFILE file> [from seconds] [top N] : where the time went, per thread and per function,
counting samples taken from that many seconds after launch on. Samples are 1 ms of CPU each (runtime/win32/prof.c);
the binary must be the one that ran (its load address comes from the file's first record)."""
import sys, struct, subprocess, bisect, collections
binary, path = sys.argv[1], sys.argv[2]
skip = float(sys.argv[3]) if len(sys.argv) > 3 else 0
top = int(sys.argv[4]) if len(sys.argv) > 4 else 40
raw = open(path, 'rb').read()
samples = [struct.unpack_from('<IIQQ', raw, k) for k in range(0, len(raw) - 23, 24)]   # tid, ms, pc, lr (runtime/win32/prof.c)
syms = []
for line in subprocess.run(['nm', '-C', '--defined-only', binary], capture_output=True, text=True).stdout.splitlines():
    p = line.split(None, 2)
    if len(p) == 3 and p[1] in 'tTwW': syms.append((int(p[0], 16), p[2][1:] if p[2].startswith('_') else p[2]))   # Mach-O: C names start with _
syms.sort(); addrs = [a for a, _ in syms]
assert samples[0][0] == 0xffffffff, 'no address record'
base = samples[0][2] - next(a for a, n in syms if n == 'prof_init')
samples = [(t, ms, r - base, l - base) for t, ms, r, l in samples[1:] if ms >= skip * 1000]
import os
maps = []   # <file>.maps (runtime/win32/prof.c): other images' text ranges, absolute addresses
if os.path.exists(path + '.maps'):
    for line in open(path + '.maps'):
        lo, hi, img = line.split(None, 2); maps.append((int(lo, 16), int(hi, 16), os.path.basename(img.strip())))
text_end = max(addrs) + 0x10000 if addrs else 0
def name(rip):
    if maps and not (addrs[0] <= rip < text_end):
        for lo, hi, img in maps:
            if lo <= rip + base < hi: return '[%s]' % img
    i = bisect.bisect_right(addrs, rip) - 1
    return syms[i][1] if i >= 0 else '?%x' % rip
threads = collections.Counter(s[0] for s in samples)
print('%d samples (%.1f CPU s)' % (len(samples), len(samples) / 1000))
for t, n in threads.most_common(6): print('  thread %d: %d samples (%.0f%%)' % (t, n, 100 * n / len(samples)))
main = threads.most_common(1)[0][0]
for label, sel in (('busiest thread', [s for s in samples if s[0] == main]), ('all threads', samples)):
    c = collections.Counter(name(s[2]) + (' <- ' + name(s[3]) if name(s[2]).startswith('[') else '') for s in sel)   # a system library: and its caller
    print('\n%s: %d samples' % (label, len(sel)))
    for f, n in c.most_common(top): print('  %5.1f%%  %s' % (100 * n / len(sel), f))
