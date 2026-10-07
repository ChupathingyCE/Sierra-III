#!/usr/bin/env python3
"""route_from_log.py - a route.txt (build 102) from a hardware log's
"player: at x y z" lines: one spot per distinct position, `--step` seconds
apart from `--start`, facing the way the player moved next.

usage: python3 route_from_log.py log.txt route.txt [--start 12] [--step 2]"""
import argparse, math, re

ap = argparse.ArgumentParser()
ap.add_argument('log')
ap.add_argument('out')
ap.add_argument('--start', type=float, default=12.0)
ap.add_argument('--step', type=float, default=2.0)
a = ap.parse_args()
pat = re.compile(r'player: at (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)')
spots = []
for line in open(a.log, errors='replace'):
    m = pat.search(line)
    if m:
        p = tuple(float(v) for v in m.groups())
        if not spots or math.dist(p, spots[-1]) > 0.5:
            spots.append(p)
lines = []
for i, p in enumerate(spots):
    q = spots[i + 1] if i + 1 < len(spots) else (spots[i - 1] if i else p)
    d = [q[k] - p[k] for k in range(3)] if i + 1 < len(spots) else [p[k] - q[k] for k in range(3)]
    d[2] = 0.0
    n = math.hypot(d[0], d[1]) or 1.0
    lines.append('%.1f %.2f %.2f %.2f %.3f %.3f 0.000' % (a.start + i * a.step, p[0], p[1], p[2] + 0.05, d[0] / n, d[1] / n))
open(a.out, 'w').write('\n'.join(lines) + '\n')
print('%d spots, %.0f s' % (len(lines), a.start + len(lines) * a.step))
