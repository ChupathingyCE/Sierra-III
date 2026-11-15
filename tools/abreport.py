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
  LOG HOLES     a run's log dropped lines ('(log: N lines dropped ...)'): its
                heartbeat windows or reports may be missing, so a stretch's
                figures can rest on fewer windows than the other side's
                (2026-11-10: rtop6's b30 lost 61 lines; the batch now grows)
  NOT USED      a side's runs say a feature was 'not used' where the first
                side's don't (the build or the bake lacks what its keep word
                asks for), so that side may be the first side again
                (2026-11-10: SSPR's pair 42874194898 ran on a bake without
                levels; 'geometry: sspr: not used: the pack has no levels')

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
from benchreport import log_path, reach, read, stretches_for, weighted, within  # noqa: E402  (the same reader as the bench tables)

METRICS = (
    ("Frame by the clock, ms", lambda rows: 1000.0 * len(rows) / sum(r[3] for r in rows) if rows and sum(r[3] for r in rows) else None),
    ("Drawing, ms", lambda rows: weighted(rows, lambda r: r[1], lambda r: r[3])),
    ("Tick, ms", lambda rows: weighted(rows, lambda r: r[2], lambda r: r[3])),
)
HEAP_FAILED = re.compile(r"^heap: .*?(\d+) requests failed")
LOG_DROPPED = re.compile(r"^\(log: (\d+) lines dropped")
NOT_USED = re.compile(r": not used(:|$| )")
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


def log_dropped(path):
    """the lines a run's log says it dropped, summed (0 without any)"""
    total = 0
    try:
        with open(path, errors="replace") as f:
            for line in f:
                m = LOG_DROPPED.match(line)
                if m:
                    total += int(m.group(1))
    except OSError:
        pass
    return total


def not_used(path):
    """the run's 'not used' lines, numbers and addresses blanked, so runs
    compare by kind"""
    found = set()
    try:
        with open(path, errors="replace") as f:
            for line in f:
                if NOT_USED.search(line):
                    found.add(re.sub(r"[0-9a-fx]*[0-9][0-9a-fx]*", "#", line.strip())[:160])
    except OSError:
        pass
    return found


MIN_BEATS = 3   # heartbeats a run needs in a stretch before its gap is read
PART_UNITS = 5.0   # world units between the sides' players, two samples running, that mean play has parted (a ride's own spread between sides is 1-4)
PLAYER_AT = re.compile(r"player: at (-?[0-9.]+) (-?[0-9.]+) (-?[0-9.]+)")
HEARTBEAT_TIME = re.compile(r"heartbeat: game time ([0-9]+)")


def player_path(path):
    """the player's place at each heartbeat: (game time, x, y, z), the place
    taken from the 'player: at' line that follows the heartbeat"""
    path_out, now = [], None
    try:
        with open(path, errors="replace") as f:
            for line in f:
                line = re.sub(r"^\[t [0-9.]+\] ", "", line)
                m = HEARTBEAT_TIME.match(line)
                if m:
                    now = int(m.group(1))
                    continue
                m = PLAYER_AT.match(line)
                if m and now is not None:
                    path_out.append((now,) + tuple(float(m.group(k)) for k in (1, 2, 3)))
                    now = None
    except OSError:
        pass
    return path_out


def parted_at(base_paths, other_paths):
    """the game time from which the sides' players stay more than PART_UNITS
    apart to the run's end, on two samples or more (a change that isn't exact makes a pad replay play
    a different game from there on, so later stretches compare different
    games); None if never. The other run's place is interpolated to the base
    run's game time, since their heartbeats fall at different ticks"""
    def place(path, g):
        for k in range(1, len(path)):
            g0, g1 = path[k - 1][0], path[k][0]
            if g0 <= g <= g1 and g1 > g0:
                f = (g - g0) / float(g1 - g0)
                return tuple(path[k - 1][j] + f * (path[k][j] - path[k - 1][j]) for j in (1, 2, 3))
        return None

    first = None
    for a in base_paths:
        for b in other_paths:
            # the parting is the start of the last run of samples apart, if it lasts to the
            # run's end: a ride both sides follow (the Pelican moves ~10 units a heartbeat, so
            # the interpolation strays) comes back together, a parted game doesn't
            streak_start, streak = None, 0
            for g, x, y, z in a:
                q = place(b, g)
                if q is None:
                    continue
                if ((q[0] - x) ** 2 + (q[1] - y) ** 2 + (q[2] - z) ** 2) ** 0.5 > PART_UNITS:
                    if streak == 0:
                        streak_start = g
                    streak += 1
                else:
                    streak = 0
            if streak >= 2:
                first = streak_start if first is None else min(first, streak_start)
    return first

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
    ap.add_argument("--stretches", default=None, help="NAME:FROM-TO game ticks, comma separated (default: the logs' map's own, else b30's opening bench)")
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
    for n in names:
        drops = [log_dropped(p) for p in sides[n]]
        if any(drops):
            print("**LOG HOLES: side %s dropped log lines** (a run: %s): some heartbeat windows or reports may be "
                  "missing from its figures.\n" % (n, ", ".join(str(d) for d in drops)))
    unused = {n: set().union(*(not_used(p) for p in sides[n])) for n in names}
    for n in names[1:]:
        extra = sorted(unused[n] - unused[base])
        if extra:
            print("**NOT USED: side %s's runs say what its keep words ask for wasn't used**, where %s's don't; "
                  "it may be %s again:\n%s\n" % (n, base, base, "\n".join("- `%s`" % x for x in extra)))
    paths = {n: [player_path(p) for p in sides[n]] for n in names}
    parted = {}
    for n in names[1:]:
        parted[n] = parted_at(paths[base], paths[n])
        if parted[n] is not None:
            print("**PLAY PARTED: side %s's player was over %.0f units from %s's by game time %d**: from there the pad "
                  "replay plays a different game on each side, so stretches after it compare different games and get "
                  "no verdict.\n" % (n, PART_UNITS, base, parted[n]))
    print("Sides: " + "; ".join("%s (%d runs)" % (n, len(sides[n])) for n in names) + ". Gaps are against **%s**.\n" % base)
    spec, why = stretches_for([p for logs in sides.values() for p in logs], a.stretches)
    print("_Stretches: %s._\n" % why)
    for part in spec.split(","):
        title, span = part.split(":")
        lo, hi = (int(x) for x in span.split("-"))
        # every run on every side ends the stretch at one reach (benchreport.reach)
        end, cut, differs = reach([run for n in names for run in beats[n]], lo, hi)
        figures = {}   # (metric, side) -> [one figure a run]
        for metric, pick in METRICS:
            for n in names:
                figures[metric, n] = [v for v in (pick(within(run, lo, hi, end)) for run in beats[n]) if v is not None]
        if not any(figures.values()):
            continue
        # A stretch the runs only graze (they end inside it, or a beat or two
        # falls in it) is one heartbeat's window, not the stretch: its gap says
        # where each run's last beat landed, so it gets no verdict.
        counts = [len(within(run, lo, hi, end)) for n in names for run in beats[n]]
        ends = [run[-1][0] for n in names for run in beats[n] if run]
        short = min(counts) < MIN_BEATS
        if not cut:
            print("**%s (game time %d-%d)**\n" % (title, lo, hi))
        else:
            print("**%s (game time %d-%d: cut to common reach)**\n" % (title, lo, end))
            if differs:
                print("REACH DIFFERS: the runs end inside %s more than a beat apart (game time %d-%d); every run is cut "
                      "to the earliest, so the figure covers %d-%d, not the whole stretch.\n" % (title, min(ends), max(ends), lo, end))
        if short:
            print("_Only %d-%d heartbeats a run in this stretch (the runs end at game time %d-%d): too short for a verdict._\n"
                  % (min(counts), max(counts), min(ends), max(ends)))
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
                    word = "INVALID" if base in invalid or n in invalid else ("too short" if short else (
                        "play parted" if parted.get(n) is not None and parted[n] < hi else verdict(figures[metric, base], figures[metric, n])))
                label = metric if len(names) == 2 else "%s, %s" % (metric, n)
                print("| %s | %s | %s | %s |" % (label, " | ".join(cells), gap, word))
        print()


if __name__ == "__main__":
    main()
