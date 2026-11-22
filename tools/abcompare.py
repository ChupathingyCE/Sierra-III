#!/usr/bin/env python3
"""abcompare.py - two runs' heartbeats side by side, by stretches of game time.

usage: python3 tools/abcompare.py A/log.txt B/log.txt [--windows 600,1500,3000,...]

For an A/B in the emulator: the same map, the same route.txt (so the player
stands at the same spot looking the same way) and no input, one build each.
The game's own clock (ticks) lines the runs up; each stretch shows the tick's
and the frame's average ms and the frames a second, A then B."""
import argparse, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from benchreport import cover, drawing_ms, fps_of, read, tick_ms  # noqa: E402  (the same reader as the bench tables)


def series(path):
    return read(path)[0]


def window(s, a, b):
    """the tick's and the frame's ms and the fps over game ticks a..b, each
    heartbeat counted for its own span and cut at the edges (benchreport.cover)"""
    rows = cover(s, a, b, b)
    if tick_ms(rows) is None or fps_of(rows) is None:
        return None
    return tick_ms(rows), drawing_ms(rows), fps_of(rows)


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
