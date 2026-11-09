#!/usr/bin/env python3
"""abreport.py - an A/B with repeats: each side's runs, and whether the gap is real.

A lever is weighed by its off word in one build, two or more runs a side
(build 119's rule: two runs of the same build can differ by a few percent, so
one run a side can't tell a small gain from noise). For each stretch of game
time this prints, per side, every run's figure and their mean, then the gap
against the first side and a verdict:

  real          the gap is larger than the spread (highest minus lowest)
                within either side, so the two sides' runs don't overlap, and
                larger than 0.5% of the figure (the logs round to 0.1 ms, so
                repeats that agree exactly would make any wiggle look real)
  in the noise  the gap is within a side's own spread, or under that floor
  no verdict    a side has a single run
  INVALID       a run of either side refused heap requests (its `heap:` lines):
                refused allocations drop drawing, so that side reads faster
                than it is (step 3's R-on pair "gained" 3-8% with 62 requests
                failed, lab findings r_loop_heap_full_under_r)

The figures are benchreport.py's (the same reader): the frame by the clock,
the frame's own work (drawing) and a tick's own cost, ms, weighted by frames.

usage:
  abreport.py RESULTS                      a rig results folder with keep sets: its
                                           runs.txt names each run's set; runN/log.txt
  abreport.py NAME=LOG,LOG NAME=LOG,LOG    sides given by hand (logs or their folders)
  options: --stretches NAME:FROM-TO,...    game ticks (default: benchreport.py's b30 bench)

  python3 tools/abreport.py ~/results/sierra-hw/38446588223 --stretches early:300-2400,late:2400-4600
  python3 tools/abreport.py on=a/run1,a/run2 off=b/run1,b/run2 --stretches cinematic:150-570
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from benchreport import STRETCHES, log_path, read, weighted  # noqa: E402  (the same reader as the bench tables)

METRICS = (
    ("Frame by the clock, ms", lambda rows: 1000.0 * len(rows) / sum(r[3] for r in rows) if rows and sum(r[3] for r in rows) else None),
    ("Drawing, ms", lambda rows: weighted(rows, lambda r: r[1], lambda r: r[3])),
    ("Tick, ms", lambda rows: weighted(rows, lambda r: r[2], lambda r: r[3])),
)
HEAP_FAILED = re.compile(r"^heap: .*?(\d+) requests failed")
RUN_LABEL = re.compile(r"^run (\d+) \(set (\d+): (.*?)\): ")


def sides_from_results(folder):
    """{side name: [log paths]} from a rig results folder's runs.txt"""
    runs = os.path.join(folder, "runs.txt")
    if not os.path.exists(runs):
        sys.exit("abreport: no runs.txt in %s (a single run has nothing to compare)" % folder)
    sides = {}
    for line in open(runs):
        m = RUN_LABEL.match(line)
        if not m:
            sys.exit("abreport: %s has no keep sets (labels like 'run 1 (set 1: keep alone)'); give the sides by hand" % runs)
        sides.setdefault(m.group(3), []).append(log_path(os.path.join(folder, "run" + m.group(1))))
    return sides


def sides_from_args(specs):
    sides = {}
    for spec in specs:
        if "=" not in spec:
            sys.exit("abreport: a side is NAME=LOG,LOG (or give one results folder)")
        name, logs = spec.split("=", 1)
        sides[name] = [log_path(p) for p in logs.split(",")]
    return sides


def heap_failed(path):
    """the most requests a run's heap: lines say were refused (0 without any)"""
    most = 0
    try:
        with open(path, errors="replace") as f:
            for line in f:
                m = HEAP_FAILED.match(line)
                if m:
                    most = max(most, int(m.group(1)))
    except OSError:
        pass
    return most


def verdict(base, other):
    if len(base) < 2 or len(other) < 2:
        return "no verdict"
    mean = sum(base) / len(base)
    gap = abs(sum(other) / len(other) - mean)
    spread = max(max(base) - min(base), max(other) - min(other))
    return "**real**" if gap > max(spread, 0.005 * mean) else "in the noise"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("sides", nargs="+")
    ap.add_argument("--stretches", default=STRETCHES)
    a = ap.parse_args()
    if len(a.sides) == 1 and os.path.isdir(a.sides[0]) and "=" not in a.sides[0]:
        sides = sides_from_results(a.sides[0])
    else:
        sides = sides_from_args(a.sides)
    if len(sides) < 2:
        sys.exit("abreport: two sides or more")
    beats = {name: [read(p)[0] for p in logs] for name, logs in sides.items()}
    names = list(sides)
    base = names[0]
    failed = {n: [heap_failed(p) for p in sides[n]] for n in names}
    invalid = {n for n in names if any(failed[n])}
    for n in names:
        if n in invalid:
            print("**INVALID: side %s refused heap requests** (most a run: %s): its drawing dropped work, so its times "
                  "read faster than they are. No verdict on it until both sides fit.\n"
                  % (n, ", ".join(str(f) for f in failed[n])))
    print("Sides: " + "; ".join("%s (%d runs)" % (n, len(sides[n])) for n in names) + ". Gaps are against **%s**.\n" % base)
    for part in a.stretches.split(","):
        title, span = part.split(":")
        lo, hi = (int(x) for x in span.split("-"))
        figures = {}   # (metric, side) -> [one figure a run]
        for metric, pick in METRICS:
            for n in names:
                figures[metric, n] = [v for v in (pick([b for b in run if lo <= b[0] < hi]) for run in beats[n]) if v is not None]
        if not any(figures.values()):
            continue
        print("**%s (game time %d-%d)**\n" % (title, lo, hi))
        print("| Metric | " + " | ".join(names) + " | Gap | Verdict |")
        print("|---|" + "---:|" * len(names) + "---:|---|")
        for metric, _ in METRICS:
            cells, means = [], {}
            for n in names:
                vals = figures[metric, n]
                means[n] = sum(vals) / len(vals) if vals else None
                cells.append("-" if not vals else "%.1f (%s)" % (means[n], ", ".join("%.1f" % v for v in vals)))
            for n in names[1:]:
                if means[base] is None or means[n] is None:
                    gap, word = "-", "-"
                else:
                    gap = "%+.1f (%+.1f%%)" % (means[n] - means[base], 100.0 * (means[n] - means[base]) / means[base])
                    word = "INVALID" if base in invalid or n in invalid else verdict(figures[metric, base], figures[metric, n])
                label = metric if len(names) == 2 else "%s, %s" % (metric, n)
                print("| %s | %s | %s | %s |" % (label, " | ".join(cells), gap, word))
        print()


if __name__ == "__main__":
    main()
