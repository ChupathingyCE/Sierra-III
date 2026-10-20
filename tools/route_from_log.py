#!/usr/bin/env python3
"""route_from_log.py - a route.txt (build 102) from a hardware log's
"player: at x y z" lines: one spot per distinct position, facing the way the
player moved next.

When each spot is reached:
  default     `--step` seconds apart from `--start`. Standing still and slow
              stretches are squeezed out, so the level goes by faster than
              it was played.
  --timed     at the game time the log had reached when the player got there
              (the heartbeat before the position line), so the replay spends
              as long in each place as the hardware run did. Enemies, effects
              and bodies then come and go on the run's clock, which matters
              when the question is memory (b108: the heap's fragmentation).
              --start and --step are ignored.
--from TICKS  leaves out the positions logged before that game time. In b30,
              2600 lets the Pelican flight and the landing play as scripted
              (teleporting a seated player along the flight doesn't follow
              it) and starts the route on the beach.
--map NAME    reads only the stretch of the log played on that map (from its
              "map d:\\maps\\NAME.map: opened" line to the next map's), for a
              log that runs through several levels. Game time starts again
              at 0 on each map, so without it a multi-level log mixes them.

A death and its revert to the last checkpoint take game time back; the spots
reached after the checkpoint on the lost try are dropped, so the route follows
the try that got through.

usage: python3 route_from_log.py log.txt route.txt [--start 12] [--step 2]
                                 [--timed] [--from TICKS] [--map NAME]"""
import argparse, math, re

ap = argparse.ArgumentParser()
ap.add_argument('log')
ap.add_argument('out')
ap.add_argument('--start', type=float, default=12.0)
ap.add_argument('--step', type=float, default=2.0)
ap.add_argument('--timed', action='store_true')
ap.add_argument('--from', dest='from_ticks', type=int, default=0)
ap.add_argument('--map')
a = ap.parse_args()
opened = re.compile(r'map d:\\maps\\(\w+)\.map: opened')
position = re.compile(r'player: at (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)')
heartbeat = re.compile(r'heartbeat: game time (\d+)')
spots = []   # (x, y, z), game time in ticks when the player got there
ticks = 0
on_map = a.map is None
reverts = 0
for line in open(a.log, errors='replace'):
    m = opened.search(line)
    if m and a.map is not None:
        on_map = m.group(1) == a.map
        ticks = 0
        continue
    if not on_map:
        continue
    m = heartbeat.search(line)
    if m:
        now = int(m.group(1))
        if now < ticks:
            # a revert: the lost try's spots go, the player's back at the checkpoint
            reverts += 1
            while spots and spots[-1][1] > now:
                spots.pop()
        ticks = now
        continue
    m = position.search(line)
    if m and ticks >= a.from_ticks:
        p = tuple(float(v) for v in m.groups())
        if not spots or math.dist(p, spots[-1][0]) > 0.5:
            spots.append((p, ticks))
lines = []
for i, (p, t) in enumerate(spots):
    q = spots[i + 1][0] if i + 1 < len(spots) else (spots[i - 1][0] if i else p)
    d = [q[k] - p[k] for k in range(3)] if i + 1 < len(spots) else [p[k] - q[k] for k in range(3)]
    d[2] = 0.0
    n = math.hypot(d[0], d[1]) or 1.0
    seconds = t / 30.0 if a.timed else a.start + i * a.step
    lines.append('%.1f %.2f %.2f %.2f %.3f %.3f 0.000' % (seconds, p[0], p[1], p[2] + 0.05, d[0] / n, d[1] / n))
open(a.out, 'w').write('\n'.join(lines) + '\n')
end = spots[-1][1] / 30.0 if a.timed and spots else a.start + len(lines) * a.step
print('%d spots, %.0f s; %d reverts' % (len(lines), end, reverts))
