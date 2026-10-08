#!/usr/bin/env python3
"""abcompare.py - two runs' heartbeats side by side, by stretches of game time.

usage: python3 tools/abcompare.py A/log.txt B/log.txt [--windows 600,1500,3000,...]

For an A/B in the emulator: the same map, the same route.txt (so the player
stands at the same spot looking the same way) and no input, one build each.
The game's own clock (ticks) lines the runs up; each stretch shows the tick's
and the frame's average ms and the frames a second, A then B."""
import argparse, re


def series(path):
    out = []
    for line in open(path, errors='replace'):
        m = re.search(r'heartbeat: game time (\d+) .*tick ([0-9.]+) ms avg.*frame ([0-9.]+) ms avg, [0-9.]+ max, ([0-9.]+) fps', line)
        if m:
            out.append((int(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(4))))
    return out


def window(s, a, b):
    sel = [x for x in s if a <= x[0] < b]
    if not sel:
        return None
    n = len(sel)
    return tuple(sum(x[k] for x in sel) / n for k in (1, 2, 3))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--windows', default='600,1500,3000,4500,6000,9000,12000,16000')
    args = ap.parse_args()
    edges = [int(x) for x in args.windows.split(',')]
    sa, sb = series(args.a), series(args.b)
    print('%-13s %-22s %-22s %s' % ('game ticks', 'A tick / frame ms, fps', 'B tick / frame ms, fps', 'B against A (tick, frame)'))
    for lo, hi in zip(edges, edges[1:]):
        wa, wb = window(sa, lo, hi), window(sb, lo, hi)
        if not wa or not wb:
            continue
        print('%5d-%-7d %5.1f / %5.1f, %4.1f    %5.1f / %5.1f, %4.1f    %+.0f%%, %+.0f%%' % (lo, hi, wa[0], wa[1], wa[2], wb[0], wb[1], wb[2],
              100.0 * (wb[0] / wa[0] - 1), 100.0 * (wb[1] / wa[1] - 1)))


if __name__ == '__main__':
    main()
