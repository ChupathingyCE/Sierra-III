#!/usr/bin/env python3
"""benchreport.py - the opening bench's stretches, for one run or several side by side.

The opening bench (Sheepdog01, 2026-10-11) is b30 from the start to the first
bunker: the scripted Pelican flight (the fair A/B stretch), the landing,
the beach fight, and the walk down into the bunker. This splits a run's log
by game time into those stretches and prints, for each, the figures the
hardware notes compare, weighted by frames (a slow second counts for the
frames it drew, not as one second):

  frame ms     by the clock: 1000 / fps, so ticks and drawing together
  drawing ms   the heartbeats' frame time: the frame's own work, ticks left out
  tick ms      a tick's own cost
  fps          frames a second
  GE busy      the GE's own time a frame and its share of the clock (keep word
               gebusy; "-" without it)

and, over the whole run: draws skipped with the display list full,
see-through draws dropped with their table full, sounds that failed for
room, and the heap's peak.

usage: benchreport.py LOG [LOG ...] [--names A,B,...] [--stretches NAME:FROM-TO,...]
  LOG: a log.txt, or the folder holding it (the run's halo/log as pulled)
  python3 tools/benchreport.py ~/psp/logs/b113_test1 ~/psp/logs/b114_test1 --names b113,b114
Prints Markdown tables, ready for a hardware_bNNN.md. Labels: a PSP log's
figures are [M]; an emulator log's are [E] and its milliseconds aren't the PSP's.
"""
import argparse, os, re, sys

NUMBER = r"(-?[0-9]+(?:\.[0-9]+)?)"
STRETCHES = "flight:300-2000,landing:2000-2700,beach:2700-6000,bunker:6000-8100"


def log_path(arg):
    if os.path.isdir(arg):
        for candidate in (os.path.join(arg, "log.txt"), os.path.join(arg, "log", "log.txt")):
            if os.path.exists(candidate):
                return candidate
        sys.exit("benchreport: no log.txt in %s" % arg)
    return arg


def read(path):
    """heartbeats (game time, frame ms, tick ms, fps, heap top KB), frame-end GE
    figures (game time of the next heartbeat, frames, GE busy ms a frame, share %),
    and the run's totals"""
    beats, ge, pending_ge = [], [], []
    totals = {"build": "?", "skipped": 0, "dropped": 0, "sound failed": None, "heap top KB": 0}
    for line in open(path, errors="replace"):
        line = re.sub(r"^\[t [0-9.]+\] ", "", line.rstrip("\n"))
        m = re.match(r"Halo PSP bring-up build (\d+)", line)
        if m:
            totals["build"] = m.group(1)
            continue
        m = re.match(r"heartbeat: game time " + NUMBER + r" \(" + NUMBER + r" ticks/s\) \| tick " + NUMBER + " ms avg, " + NUMBER +
                     r" max \| frame " + NUMBER + " ms avg, " + NUMBER + " max, " + NUMBER + r" fps .*?heap top " + NUMBER, line)
        if m:
            g, ticks_s, tick, frame, fps, heap = (float(m.group(k)) for k in (1, 2, 3, 5, 7, 8))
            if ticks_s > 0:          # a heartbeat with no ticks is a pause or a load
                beats.append((g, frame, tick, fps))
                for frames, busy, share in pending_ge:
                    ge.append((g, frames, busy, share))
            pending_ge = []
            totals["heap top KB"] = max(totals["heap top KB"], heap)
            continue
        m = re.match(r"frameend: (\d+) frames;.*?GE busy " + NUMBER + r" ms a frame, " + NUMBER + "% of the clock", line)
        if m:
            pending_ge.append((int(m.group(1)), float(m.group(2)), float(m.group(3))))
            continue
        m = re.search(r"draws skipped with the display list full[^0-9]*(\d+)", line) or \
            re.search(r"(\d+) draws? skipped with the (?:display )?list full", line)
        if m:
            totals["skipped"] = max(totals["skipped"], int(m.group(1)))
        m = re.search(r"see-through draws, (\d+) dropped", line)
        if m:   # a count since the start
            totals["dropped"] = max(totals["dropped"], int(m.group(1)))
        m = re.search(r"(\d+) failed for room", line)
        if m and line.startswith("sound cache:"):
            totals["sound failed"] = int(m.group(1))
    return beats, ge, totals


def weighted(rows, value, weight):
    total = sum(weight(r) for r in rows)
    return sum(value(r) * weight(r) for r in rows) / total if total else None


def fmt(v, digits=1):
    return "-" if v is None else ("%.*f" % (digits, v))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--names", help="column names, comma separated (default: the build numbers)")
    ap.add_argument("--stretches", default=STRETCHES, help="NAME:FROM-TO game ticks, comma separated (default: the opening bench's)")
    a = ap.parse_args()
    runs = [read(log_path(p)) for p in a.logs]
    names = a.names.split(",") if a.names else ["b" + r[2]["build"] for r in runs]
    if len(names) != len(runs):
        sys.exit("benchreport: %d names for %d logs" % (len(names), len(runs)))
    stretches = []
    for part in a.stretches.split(","):
        name, span = part.split(":")
        lo, hi = (int(x) for x in span.split("-"))
        stretches.append((name, lo, hi))

    head = "| Stretch (game time) | " + " | ".join(names) + " |"
    rule = "|---|" + "---:|" * len(names)
    for title, pick, digits in (
            # each heartbeat is a second: the clock's frame is the seconds over the frames
            ("Frame by the clock, ms", lambda rows: 1000.0 * len(rows) / sum(r[3] for r in rows) if rows and sum(r[3] for r in rows) else None, 1),
            ("The frame's own work (drawing), ms, weighted by frames", lambda rows: weighted(rows, lambda r: r[1], lambda r: r[3]), 1),
            ("A tick's own cost, ms, weighted by frames", lambda rows: weighted(rows, lambda r: r[2], lambda r: r[3]), 1),
            ("Frames a second", lambda rows: weighted(rows, lambda r: r[3], lambda r: 1.0), 2)):
        print("**%s**\n\n%s\n%s" % (title, head, rule))
        for name, lo, hi in stretches:
            cells = [fmt(pick([b for b in r[0] if lo <= b[0] < hi]), digits) for r in runs]
            print("| %s (%d-%d) | %s |" % (name, lo, hi, " | ".join(cells)))
        print()
    print("**The GE busy a frame, ms (share of the clock)**\n\n%s\n%s" % (head, rule))
    for name, lo, hi in stretches:
        cells = []
        for r in runs:
            rows = [g for g in r[1] if lo <= g[0] < hi]
            busy = weighted(rows, lambda g: g[2], lambda g: g[1])
            share = weighted(rows, lambda g: g[3], lambda g: g[1])
            cells.append("-" if busy is None else "%.0f (%.0f%%)" % (busy, share))
        print("| %s (%d-%d) | %s |" % (name, lo, hi, " | ".join(cells)))
    print()
    print("**Over the whole run**\n\n| | " + " | ".join(names) + " |\n" + "|---|" + "---:|" * len(names))
    for label, key in (("Game time reached", None), ("Draws skipped, display list full", "skipped"),
                       ("See-through draws dropped, table full", "dropped"), ("Sounds failed for room", "sound failed"),
                       ("Heap top, KB (peak)", "heap top KB")):
        if key is None:
            cells = ["%d" % max((b[0] for b in r[0]), default=0) for r in runs]
        else:
            cells = ["-" if r[2][key] is None else "%d" % r[2][key] for r in runs]
        print("| %s | %s |" % (label, " | ".join(cells)))


if __name__ == "__main__":
    main()
