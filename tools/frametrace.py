#!/usr/bin/env python3
"""frametrace.py - the "frametrace" lines of a log, read (build 126).

The keep word frametrace (psp/src/psp_frametrace.c) times every frame and logs a
line every 300 frames: the window's average frame, its frames over 66.7 ms (the
only frames that lose game time at 30 ticks a second) and the time lost, its
three slowest frames split into ticks (t, with how many ran), drawing (d), the
frame's end (e: waiting for the GE, the swap) and the rest (r), with the
Memory Stick reads made meanwhile (s), and the run's 99th and 99.9th percentile
frame times so far.

This prints, for one log or several:
  - the run's frames, its 99% and 99.9% frame times (the last line's), and the
    share of frames over 66.7 ms with the game time they lost;
  - each window as a row (game time, average, frames over 66.7 ms, slowest);
  - the slow frames grouped by what took most of them: the ticks, the drawing,
    the frame's end, the rest, or the stick (when its reads were most of it).

usage: python3 frametrace.py LOG [LOG ...] [--windows]
  --windows  every window's row (otherwise only the summary and the causes)
"""
import re
import sys

LINE = re.compile(
    r"frametrace: (\d+) frames to game time (-?\d+): ([\d.]+) ms avg; over 66\.7 ms (\d+), "
    r"lost ([\d.]+) s of ([\d.]+) s; slowest (.*); run (\d+) frames, 99% (\d+) ms, 99\.9% (\d+) ms")
SLOW = re.compile(r"([\d.]+):t([\d.]+)/(\d+) d([\d.]+) e([\d.]+) r([\d.]+) s([\d.]+)")


def read(path):
    windows = []
    for text in open(path, errors="replace"):
        m = LINE.search(text)
        if not m:
            continue
        slowest = []
        for s in SLOW.finditer(m.group(7)):
            wall, tick, ticks, draw, end, rest, stick = s.groups()
            slowest.append({"wall": float(wall), "tick": float(tick), "ticks": int(ticks), "draw": float(draw),
                            "end": float(end), "rest": float(rest), "stick": float(stick)})
        windows.append({"frames": int(m.group(1)), "game_time": int(m.group(2)), "avg": float(m.group(3)),
                        "over": int(m.group(4)), "lost": float(m.group(5)), "wall": float(m.group(6)),
                        "slowest": slowest, "run_frames": int(m.group(8)), "p99": int(m.group(9)),
                        "p999": int(m.group(10))})
    return windows


def cause(f):
    """what took most of a slow frame; the stick when its reads were over half of it"""
    if f["stick"] > f["wall"] / 2:
        return "stick reads"
    parts = {"ticks": f["tick"], "drawing": f["draw"], "frame end": f["end"], "the rest": f["rest"]}
    return max(parts, key=parts.get)


def report(path, show_windows):
    windows = read(path)
    print("== %s" % path)
    if not windows:
        print("no frametrace lines (keep word frametrace)")
        return
    last = windows[-1]
    frames = sum(w["frames"] for w in windows)
    over = sum(w["over"] for w in windows)
    lost = sum(w["lost"] for w in windows)
    wall = sum(w["wall"] for w in windows)
    print("frames %d (the run's count %d); 99%% %d ms, 99.9%% %d ms; over 66.7 ms %d (%.1f%%); "
          "game time lost %.1f s of %.1f s (%.0f%% of real time played)"
          % (frames, last["run_frames"], last["p99"], last["p999"], over, 100.0 * over / frames,
             lost, wall, 100.0 * (wall - lost) / wall if wall else 0.0))
    if show_windows:
        print("%10s %8s %6s %8s  %s" % ("game time", "avg ms", "over", "lost s", "slowest"))
        for w in windows:
            s = w["slowest"][0] if w["slowest"] else None
            print("%10d %8.1f %6d %8.1f  %s" % (w["game_time"], w["avg"], w["over"], w["lost"],
                  "%.1f ms (%s)" % (s["wall"], cause(s)) if s else ""))
    slow = [f for w in windows for f in w["slowest"] if f["wall"] > 66.7]
    if slow:
        by = {}
        for f in slow:
            by.setdefault(cause(f), []).append(f)
        print("the windows' slowest frames over 66.7 ms (%d), by what took most of each:" % len(slow))
        for name, fs in sorted(by.items(), key=lambda kv: -len(kv[1])):
            worst = max(fs, key=lambda f: f["wall"])
            print("  %-12s %4d  worst %.1f ms = ticks %.1f (%d) + drawing %.1f + end %.1f + rest %.1f; stick %.1f"
                  % (name, len(fs), worst["wall"], worst["tick"], worst["ticks"], worst["draw"], worst["end"],
                     worst["rest"], worst["stick"]))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        sys.exit(__doc__)
    for path in args:
        report(path, "--windows" in sys.argv)


if __name__ == "__main__":
    main()
