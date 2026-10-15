#!/usr/bin/env python3
"""framecmp.py - two runs' pictures compared frame by frame.

Reads the "framecrc FRAME GAMETIME HASH" lines that the keep word framecrc
writes (psp_ge.c: each finished picture hashed before the debug overlay).
With fixedticks and fixedseed and no input, a run plays the same way on any
build, so a change meant to draw exactly as before must give the same hash at
every frame both runs drew at the same game time.

usage: python3 framecmp.py A/log.txt B/log.txt [--list N]
  --list N  print the first N frames whose pictures differ (default 10)

Prints how many frames the runs share (by frame number and game time), how
many pictures differ, the first that differs, and the game times where they
differ (in runs of consecutive frames)."""
import re
import sys


def load(path):
    frames = {}
    for line in open(path, errors='replace'):
        m = re.search(r'framecrc (\d+) (-?\d+) ([0-9a-f]{8})', line)
        if m:
            frames[(int(m.group(1)), int(m.group(2)))] = m.group(3)
    return frames


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    show = 10
    for a in sys.argv[1:]:
        if a.startswith('--list'):
            show = int(a.split('=', 1)[1]) if '=' in a else 10
    if len(args) != 2:
        sys.exit(__doc__)
    a, b = load(args[0]), load(args[1])
    common = sorted(set(a) & set(b))
    print('frames: %d and %d, %d in common (the same frame number and game time)' % (len(a), len(b), len(common)))
    if not common:
        print('verdict: nothing to compare (no framecrc lines, or the runs played differently)')
        return
    differ = [k for k in common if a[k] != b[k]]
    if not differ:
        print('verdict: the same picture at all %d common frames' % len(common))
        return
    print('pictures differ at %d of %d common frames; the first at frame %d (game time %d)' % (
        len(differ), len(common), differ[0][0], differ[0][1]))
    runs, start, last = [], None, None
    index = {k: i for i, k in enumerate(common)}
    for k in differ:
        if start is None or index[k] != index[last] + 1:
            if start is not None:
                runs.append((start, last))
            start = k
        last = k
    runs.append((start, last))
    print('runs of differing frames (game time): %s%s' % (
        ', '.join('%d-%d' % (s[1], e[1]) if s != e else '%d' % s[1] for s, e in runs[:show]),
        ' ...' if len(runs) > show else ''))
    print('verdict: different')


if __name__ == '__main__':
    main()
