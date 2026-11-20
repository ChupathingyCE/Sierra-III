#!/usr/bin/env python3
"""benchreport.py - the opening bench's stretches, for one run or several side by side.

The opening bench (Sheepdog01, 2026-10-11) is b30 from the start to the first
bunker: the Pelican flight, the landing, the beach fight, and the walk down
into the bunker. On the PSP only the flight's opening cinematic (game time
150-570, the camera scripted) repeats run to run: six runs of b113 and b114
fell within 1% there, while the rest of the flight, where the player looks
around inside the Pelican, varied by 7% (hardware_b114.md). So "cinematic"
is the PSP's fair A/B stretch; the emulator's scripted runs repeat
everywhere. This splits a run's log
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
STRETCHES = "cinematic:150-570,flight:300-2000,landing:2000-2700,beach:2700-6000,bunker:6000-8100"
# A log that names no pad (a route or a hands-off run) is read by its map's
# windows here, as before. These were cut on each map's first pads (the lab's
# projects/sierra/findings/stretches.tsv "<map>-pad-<name>" rows, the *_play1
# recordings) and are kept for logs without a pad so that earlier reads
# repeat; a pad's own windows are PAD_STRETCHES below. b30 has none here: its
# route-based reads (ledger1's cinematic, flight, landing, beach) follow the
# route and keep the opening bench's above.
MAP_STRETCHES = {
    "a30": "opening:300-3600,early:3600-12000,middle:12000-22000,late:22000-31318",
    "b40": "early:2521-5008,heavyA:9123-10272,heavyB:10272-11661,snowfield:13922-14876",
    "c10": "opening:300-6000,early:6000-15000,middle:15000-30000,late:30000-44859",
    "c20": "first:300-3600,play:3600-9000",
    "c40": "opening:300-6000,early:6000-15000,banshee:15000-30000,middle:30000-45000,late:45000-57017",
    "d20": "opening:300-3000,swamp:3000-9000,interior:9000-18000,late:18000-29234",
}
# Each pad's own windows (game ticks), cut on that recording's own events: the
# cutscenes, the seat changes and the first fight, read from its .path (the
# player's seat and control by tick, the deaths he caused) in the lab's
# projects/sierra/scripts/pads/. A replay keeps its recording's events only to the tick it
# tracks him; a held replay (padhold) holds his route and seat, not the fight.
# Windows stop where the pad's benches stop; past them, give --stretches.
# Why by pad and not by map: one map's pads are different plays, and b30's
# windows, cut on b30_play2, put b30_leg2's whole first fight into its "flight"
# (the lab's findings b30_leg2_seat_timeline, stretches_by_pad).
PAD_STRETCHES = {
    # the Pelican's cutscene to 1,066, seated to 1,098; on foot 1,098-4,386
    # (his kills 1,183-2,298 down the beach, then 2,596-3,378 and the wait at
    # the ridge); the Warthog 4,386-9,826
    "b30_leg2": "opening:78-634,flight:634-1098,fight:1098-2400,beach:2400-4386,warthog:4386-9826",
    # the opening cutscene to 1,069, then on foot to the end: the first room's
    # fight (kills 1,238-3,599), then the walk south and the second
    "c20_leg2": "opening:300-1069,first:1069-3600,play:3600-9000",
    # the Pelican's cutscene to 912, seated to 944; on foot to the first
    # Warthog (16,386-19,338); on foot at the bridge 19,338-21,842; the
    # snowfield seated, 23,899-27,924. early, heavyA and the snowfield are
    # where b40_play1's windows lie on this route (the same ground within
    # 3 units). The path is proven to 44,170 only
    "b40_leg1": "opening:300-944,early:944-8128,cliff:8128-14015,heavyA:14015-16386,warthog:16386-19338,"
                "foot2:19338-21842,snowfield:23899-27924",
    # the opening cutscene to 6,979; on foot to the first drive 16,195-20,326
    "c40_leg2": "opening:300-6979,early:6979-16195,drive:16195-20326,foot2:20326-23136",
    # the opening cutscene to 881; on foot to the first drive 17,236-20,394
    "a30_leg1": "opening:300-881,early:881-17236,drive:17236-20394",
    # seated in the opening cutscene to 849; on foot to 19,506; a seat
    # 19,506-19,933; on foot to the cutscene at 23,272
    "a50_leg1": "opening:300-849,early:849-19506,foot2:19933-23272",
    # cutscenes 1-1,484, 2,905-3,085, 12,694-12,829 and 23,027-24,327 left out
    # of the windows; seated from 28,587
    "d20_play1": "opening:300-1484,walk:1484-2905,swamp:3085-9000,interior:9000-12694,deep:12829-18000,"
                 "late:18000-23027,end:24327-28587",
    # MAP_STRETCHES' windows were cut on these and are kept, except that
    # heavyB ("the warthog") now starts where he takes the Warthog (10,780):
    # its first 508 ticks were on foot
    "b40_play1": "early:2521-5008,heavyA:9123-10780,heavyB:10780-11661,snowfield:13922-14876",
    # opening is the whole cutscene (to 6,974); banshee is 15% seated
    "c40_play1": "opening:300-6974,early:6974-15000,banshee:15000-30000,middle:30000-45000,late:45000-57017",
}
# A log names its pad only by its size ("pad: replaying ms0:/halo/pad.pad,
# N bytes": the rig copies every pad to pad.pad). The sizes of the lab's
# projects/sierra/scripts/pads/ (all different); a re-recorded pad gets its line here.
PAD_BYTES = {
    4513757: "b30_leg2", 4852157: "b30_leg1", 4486369: "c20_leg2", 5577602: "c20_leg1",
    8452169: "b40_leg1", 3392953: "b40_leg1_part", 4433040: "b40_play1", 6977168: "c40_leg2",
    6195887: "c40_leg1", 5280736: "c40_play1", 4126153: "a30_leg1", 2661944: "a30_play1",
    6778679: "a50_leg1", 2518401: "d20_play1", 4496913: "c10_play1",
    505208: "b30_play1", 649592: "b30_play2", 1236152: "c20_play1",
}


def map_of(path):
    """the first map the log loads ('loading: levels\\<x>\\<map> begins'), or None;
    a job whose map input was escaped logs each backslash doubled, so one or more"""
    for line in open(path, errors="replace"):
        m = re.search(r"loading: levels\\+[^\\]+\\+(\w+) begins", line)
        if m:
            return m.group(1)
    return None


def pad_of(path):
    """the pad the log replays, by its size; None for a log with no replay,
    '<N bytes>' for a pad not in PAD_BYTES"""
    for line in open(path, errors="replace"):
        m = re.search(r"pad: replaying \S+, (\d+) bytes", line)
        if m:
            size = int(m.group(1))
            return PAD_BYTES.get(size, "%d bytes" % size)
    return None


def stretches_for(paths, given):
    """the stretches to read: --stretches if given, else the logs' pad's own
    (every log replaying the same pad in PAD_STRETCHES), else, for logs that
    replay no pad, their map's (MAP_STRETCHES), else the opening bench's;
    with a line saying which"""
    if given:
        return given, "as given"
    pads = set(pad_of(p) for p in paths)
    maps = set(map_of(p) for p in paths)
    if len(pads) == 1 and None not in pads:
        (pad,) = pads
        if pad in PAD_STRETCHES:
            return PAD_STRETCHES[pad], "pad %s's own (stretches.tsv)" % pad
        return STRETCHES, "b30's opening bench (pad %s has no stretches of its own here: give --stretches)" % pad
    if len(pads) > 1:
        return STRETCHES, "b30's opening bench (the logs replay %s: give --stretches)" % (
            ", ".join(sorted(str(p) for p in pads)))
    if len(maps) == 1:
        (name,) = maps
        if name in MAP_STRETCHES:
            return MAP_STRETCHES[name], "%s's own, no pad (stretches.tsv)" % name
        return STRETCHES, "b30's opening bench" + ("" if name == "b30" else " (%s has no stretches of its own here)" % name)
    return STRETCHES, "b30's opening bench (the logs name %s)" % (", ".join(sorted(str(m) for m in maps)))


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


def reach(runs, lo, hi):
    """Where a stretch ends so that every run's figure covers the same game time.

    A heartbeat stands for the second that ends at its game time. When a run's
    stop (exitatN, wall) falls inside the stretch, its last beat lands anywhere
    in the last beat's span, and where the cost slopes the figure follows that
    phase, not the build (findings d20_bunker_window_reach: up to ~5% in one
    job). So the stretch ends at the earliest last beat among the runs that
    reach it, and a beat that ends past that is left out of every run.

    runs: each run's heartbeats (game time first). Returns (end, cut, differs):
    end is inclusive (hi when no run ends inside, so the figure is as before);
    differs says the runs' last beats inside are more than one beat apart."""
    lasts, spans = [], []
    for beats in runs:
        inside = [b[0] for b in beats if lo <= b[0] < hi]
        if not inside:
            continue    # a run that never reaches the stretch has no figure there
        if beats[-1][0] < hi:
            lasts.append(beats[-1][0])
        spans += [b - a for a, b in zip(inside, inside[1:])][-4:]
    if not lasts:
        return hi, False, False
    span = sorted(spans)[len(spans) // 2] if spans else 0
    return min(lasts), True, max(lasts) - min(lasts) > span


def within(beats, lo, hi, end):
    return [b for b in beats if lo <= b[0] < hi and b[0] <= end]


def weighted(rows, value, weight):
    total = sum(weight(r) for r in rows)
    return sum(value(r) * weight(r) for r in rows) / total if total else None


def fmt(v, digits=1):
    return "-" if v is None else ("%.*f" % (digits, v))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--names", help="column names, comma separated (default: the build numbers)")
    ap.add_argument("--stretches", default=None, help="NAME:FROM-TO game ticks, comma separated (default: the logs' pad's own, else their map's, else the opening bench's)")
    a = ap.parse_args()
    runs = [read(log_path(p)) for p in a.logs]
    names = a.names.split(",") if a.names else ["b" + r[2]["build"] for r in runs]
    if len(names) != len(runs):
        sys.exit("benchreport: %d names for %d logs" % (len(names), len(runs)))
    spec, why = stretches_for([log_path(p) for p in a.logs], a.stretches)
    print("_Stretches: %s._\n" % why)
    stretches = []
    for part in spec.split(","):
        name, span = part.split(":")
        lo, hi = (int(x) for x in span.split("-"))
        stretches.append((name, lo, hi))

    head = "| Stretch (game time) | " + " | ".join(names) + " |"
    rule = "|---|" + "---:|" * len(names)
    reaches = {}    # name -> (end, cut, differs), the same across the four tables
    for name, lo, hi in stretches:
        reaches[name] = reach([r[0] for r in runs], lo, hi)

    def label(name, lo, hi):
        end, cut, differs = reaches[name]
        if not cut:
            return "%s (%d-%d)" % (name, lo, hi)
        return "%s (%d-%d: cut to common reach%s)" % (name, lo, end, ", REACH DIFFERS" if differs else "")

    for title, pick, digits in (
            # each heartbeat is a second: the clock's frame is the seconds over the frames
            ("Frame by the clock, ms", lambda rows: 1000.0 * len(rows) / sum(r[3] for r in rows) if rows and sum(r[3] for r in rows) else None, 1),
            ("The frame's own work (drawing), ms, weighted by frames", lambda rows: weighted(rows, lambda r: r[1], lambda r: r[3]), 1),
            ("A tick's own cost, ms, weighted by frames", lambda rows: weighted(rows, lambda r: r[2], lambda r: r[3]), 1),
            ("Frames a second", lambda rows: weighted(rows, lambda r: r[3], lambda r: 1.0), 2)):
        print("**%s**\n\n%s\n%s" % (title, head, rule))
        for name, lo, hi in stretches:
            cells = [fmt(pick(within(r[0], lo, hi, reaches[name][0])), digits) for r in runs]
            print("| %s | %s |" % (label(name, lo, hi), " | ".join(cells)))
        print()
    print("**The GE busy a frame, ms (share of the clock)**\n\n%s\n%s" % (head, rule))
    for name, lo, hi in stretches:
        cells = []
        for r in runs:
            rows = within(r[1], lo, hi, reaches[name][0])
            busy = weighted(rows, lambda g: g[2], lambda g: g[1])
            share = weighted(rows, lambda g: g[3], lambda g: g[1])
            cells.append("-" if busy is None else "%.0f (%.0f%%)" % (busy, share))
        print("| %s | %s |" % (label(name, lo, hi), " | ".join(cells)))
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
