#!/usr/bin/env python3
"""hwreport.py - a hardware run's log, summed up; or two runs side by side.

Reads a run's ms0:/halo/log folder as Sheepdog01 copies it to ~/psp/logs (log.txt,
and profile.txt beside it when there is one) and prints:

  the run         build, keep words, maps loaded, how long it played
  the clock       tick and frame times, fps, the budget line's split (ticks,
                  the frame, the GE renderer's CPU, waiting for the GE)
  memory          heap and game-state peaks, the sound cache, file handles
  the bus         the hardware profiler's lines (keep word hwprofile): time
                  waiting on memory, cache misses and writebacks per million
                  cycles
  objects         objects_update's time per tick by object type
  threads         each thread's CPU share (build 106 on)
  steps           the profile's steps, time-weighted over its windows
  trouble         lines that report a failure, counted by kind

Every figure is over the heartbeats in play: game time past --from (default
150 ticks, after the load) and before --to, on a map other than the menu's
(ui), and with the game running (a heartbeat with no ticks is the pause menu
or a stalled game: left out, and counted). The profile's steps likewise skip
its windows with no ticks. Two runs give two columns and
the change; a hand-played route differs run to run, so compare the same
stretch (--from/--to, or --near X,Y,Z,R: only heartbeats with the player
within R of that point).

usage: hwreport.py LOGDIR [LOGDIR2] [--from TICKS] [--to TICKS] [--near X,Y,Z,R]
                   [--steps N] [--series]
   python3 tools/hwreport.py ~/psp/logs/b105_run1 ~/psp/logs/b106_run1
"""
import argparse, collections, math, os, re, sys

NUMBER = r"(-?[0-9]+(?:\.[0-9]+)?)"

# ---------------------------------------------------------------- reading

class Run:
    def __init__(self, folder):
        self.folder = folder
        self.build = "?"
        self.keep = []
        self.maps = []
        self.beats = []          # one dict per heartbeat, with what came since the one before
        self.windows = []        # (seconds, {step: ms a frame}) from profile.txt, windows that ran ticks
        self.paused_windows = 0  # windows with no ticks (the pause menu, a stall), left out
        self.trouble = collections.Counter()
        self.peaks = {}
        self.pools = {}          # game-state pool: (highest peak, its limit)

def read_log(path, run):
    pending = {}                 # lines since the last heartbeat
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = re.match(r"Halo PSP bring-up build (\d+)", line)
        if m:
            run.build = m.group(1)
            continue
        m = re.match(r"null path keeps: (.*?)\s+\(keep\.txt", line)
        if m:
            run.keep = m.group(1).split()
            continue
        m = re.match(r"debug\.txt: init: map_name (\S+)", line) or re.match(r"map d:\\maps\\(\S+)\.map: opened", line)
        if m and (not run.maps or run.maps[-1] != m.group(1)):
            run.maps.append(m.group(1))
            continue
        m = re.match(r"player: at " + NUMBER + " " + NUMBER + " " + NUMBER, line)
        if m:
            pending["at"] = tuple(float(m.group(k)) for k in (1, 2, 3))
            continue
        m = re.match(r"budget: " + NUMBER + r" ms a frame by the clock .*?: ticks " + NUMBER + r" ms a frame \(" + NUMBER +
                     r" ticks\), frame " + NUMBER + r" \(the GE renderer's CPU " + NUMBER + r" of it, waiting for the GE " +
                     NUMBER + r"\), the rest " + NUMBER, line)
        if m:
            for k, name in enumerate(("clock ms a frame", "ticks ms a frame", "ticks a frame", "frame ms",
                                      "GE renderer CPU ms", "waiting for the GE ms", "the rest ms"), 1):
                pending[name] = float(m.group(k))
            continue
        m = re.match(r"hardware: " + NUMBER + r" M CPU cycles .*?waiting on memory " + NUMBER + r"%, internal " + NUMBER +
                     r"%, VFPU " + NUMBER + r"%, coprocessors " + NUMBER + r"%, asleep " + NUMBER + r"% \| per million cycles: (.*)", line)
        if m:
            for k, name in enumerate(("M CPU cycles", "waiting on memory %", "internal stalls %", "VFPU %", "coprocessors %", "asleep %"), 1):
                pending["hw " + name] = float(m.group(k))
            rest = m.group(7)
            # "uncached loads A stores B, cached loads C stores D": the stores
            # are named by the loads before them
            rest = re.sub(r"(uncached|cached) loads " + NUMBER + " stores", r"\1 loads \2, \1 stores", rest)
            for name, value in re.findall(r"([A-Za-z][A-Za-z /-]*?) " + NUMBER, rest):
                pending["hw " + name.strip(" ,") + " per M cycles"] = float(value)
            continue
        m = re.match(r"\s*objects_update by type, ms per tick \(updates per tick\): (.*)", line)
        if m:
            text = m.group(1)
            # a type's own split: "(of the bipeds: type update A (n); damage B (n); node matrices) C (n);"
            for m3 in list(re.finditer(r"\(of the (\w+?)s: (.*?)\) " + NUMBER + r" \(" + NUMBER + r"\);", text))[::-1]:
                parts = m3.group(2) + " " + m3.group(3) + " (" + m3.group(4) + ");"
                for name, ms, count in re.findall(r"([a-z][a-z ]*?) " + NUMBER + r" \(" + NUMBER + r"\);", parts):
                    pending["objects %s: %s" % (m3.group(1), name)] = float(ms)
                text = text[:m3.start()] + text[m3.end():]
            for name, ms, count in re.findall(r"([a-z][a-z ]*?) " + NUMBER + r" \(" + NUMBER + r"\);", text):
                pending["objects " + name] = float(ms)
            m2 = re.search(r"asleep " + NUMBER + " of " + NUMBER, m.group(1))
            if m2:
                pending["objects asleep a tick"] = float(m2.group(1))
            continue
        m = re.match(r"threads \(priority, CPU share since the last line, times woken\): (.*)", line)
        if m:
            for name, share, woken in re.findall(r"([A-Za-z_][\w ]*?) 0x[0-9a-f]+ " + NUMBER + r"% " + NUMBER, m.group(1)):
                key = re.sub(r"__[0-9a-f]+$", "", name.strip())   # a pthread's address changes run to run
                pending["thread " + key + " %"] = float(share)
                pending["thread " + key + " woken"] = float(woken)
            continue
        m = re.match(r"heap: " + NUMBER + r" KB in use, .*?; " + NUMBER + " requests failed", line)
        if m:
            peak(run, "heap in use KB", float(m.group(1)))
            if float(m.group(2)):
                run.trouble["heap requests failed (the count so far)"] = max(run.trouble["heap requests failed (the count so far)"], int(float(m.group(2))))
            continue
        m = re.match(r"game state: " + NUMBER + " KB allocated of " + NUMBER, line)
        if m:
            peak(run, "game state KB", float(m.group(1)))
            continue
        m = re.match(r"game state: (.*)", line)
        if m:
            # each pool's "name N KB (peak P of L)": the highest seen
            for part in m.group(1).split(";"):
                m2 = re.match(r"\s*(.+?) " + NUMBER + r" KB \(peak " + NUMBER + " of " + NUMBER, part)
                if m2:
                    name = m2.group(1).strip()
                    top, limit = float(m2.group(3)), float(m2.group(4))
                    if top > run.pools.get(name, (0, limit))[0]:
                        run.pools[name] = (top, limit)
            continue
        m = re.match(r"sound cache: " + NUMBER + " KB occupied of " + NUMBER, line)
        if m:
            peak(run, "sound cache KB", float(m.group(1)))
            continue
        m = re.match(r"files: " + NUMBER + r" handles open \(peak " + NUMBER, line)
        if m:
            peak(run, "file handles", float(m.group(2)))
            continue
        m = re.match(r"heartbeat: game time " + NUMBER + r" \(" + NUMBER + r" ticks/s\) \| tick " + NUMBER + " ms avg, " + NUMBER +
                     r" max \| frame " + NUMBER + " ms avg, " + NUMBER + " max, " + NUMBER + r" fps \| cpu " + NUMBER +
                     r" MHz \| free " + NUMBER + r" KB \| heap top " + NUMBER, line)
        if m:
            beat = dict(pending)
            pending = {"at": pending["at"]} if "at" in pending else {}
            for k, name in enumerate(("game time", "ticks/s", "tick ms", "tick max ms", "frame ms", "frame max ms", "fps",
                                      "cpu MHz", "free KB", "heap top KB"), 1):
                beat[name] = float(m.group(k))
            beat["map"] = run.maps[-1] if run.maps else "?"
            run.beats.append(beat)
            peak(run, "heap top KB", beat["heap top KB"])
            continue
        if re.search(r"\bfail(ed|s|ure)?\b|\bunderrun|\bout of\b|\bcrash(ed)?\b|\bhang\b|\bgave up\b", line, re.I) and \
                not re.search(r"0 (requests )?failed|failed: 0|0 failures|crash handler|a hang here|opens failed: 0", line):
            run.trouble[re.sub(NUMBER, "N", line)[:110]] += 1

def peak(run, name, value):
    run.peaks[name] = max(run.peaks.get(name, value), value)

def read_profile(path, run):
    seconds, ticks, steps = None, 0, {}
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = re.match(r"window \d+: " + NUMBER + r" s, \d+ frames, (\d+) ticks", line)
        if m:
            if seconds is not None and ticks:
                run.windows.append((seconds, steps))
            elif seconds is not None:
                run.paused_windows += 1
            seconds, ticks, steps = float(m.group(1)), int(m.group(2)), {}
            continue
        m = re.match(r"(\S.*?)\s+" + NUMBER + r"\s+" + NUMBER + r"\s+" + NUMBER + r"\s+" + NUMBER + "$", line)
        if m and seconds is not None and not line.startswith("step "):
            steps[m.group(1)] = float(m.group(2))
    if seconds is not None and ticks:
        run.windows.append((seconds, steps))
    elif seconds is not None:
        run.paused_windows += 1

def load(folder):
    run = Run(folder)
    log = os.path.join(folder, "log.txt") if os.path.isdir(folder) else folder
    if not os.path.exists(log) and os.path.isdir(os.path.join(folder, "log")):
        folder = os.path.join(folder, "log")
        log = os.path.join(folder, "log.txt")
    read_log(log, run)
    profile = os.path.join(os.path.dirname(log), "profile.txt")
    if os.path.exists(profile):
        read_profile(profile, run)
    return run

# ---------------------------------------------------------------- summing up

def running(b):
    """the game was running: ticks since the last heartbeat, not in the menu's map"""
    return b["ticks/s"] > 0 and b.get("map") != "ui"

def in_play(run, a, paused=False):
    """the heartbeats counted; paused=True: the ones left out for having no ticks instead"""
    beats = [b for b in run.beats if b["game time"] >= a.from_ticks and (a.to is None or b["game time"] <= a.to)]
    beats = [b for b in beats if running(b) != paused]
    if a.near:
        x, y, z, r = a.near
        beats = [b for b in beats if "at" in b and math.dist(b["at"], (x, y, z)) <= r]
    return beats

def mean(values):
    values = list(values)
    return sum(values) / len(values) if values else None

def percentile(values, p):
    values = sorted(values)
    if not values:
        return None
    k = (len(values) - 1) * p / 100.0
    lo, hi = int(math.floor(k)), int(math.ceil(k))
    return values[lo] + (values[hi] - values[lo]) * (k - lo)

def summary(run, a):
    """name -> value, in the order printed"""
    beats = in_play(run, a)
    out = collections.OrderedDict()
    out["heartbeats in play"] = len(beats)
    out["heartbeats left out: no ticks"] = len(in_play(run, a, paused=True))
    # each heartbeat's own ticks (a checkpoint revert or a new map sends game time back)
    out["game time covered, s"] = sum(max(0.0, b["game time"] - p["game time"]) for p, b in zip(beats, beats[1:])) / 30.0
    for name, key in (("tick ms, mean", "tick ms"), ("frame ms, mean", "frame ms"), ("fps, mean", "fps")):
        out[name] = mean(b[key] for b in beats)
    out["tick ms, median"] = percentile([b["tick ms"] for b in beats], 50)
    out["tick ms, 90th percentile"] = percentile([b["tick ms"] for b in beats], 90)
    out["tick max ms, 90th percentile"] = percentile([b["tick max ms"] for b in beats], 90)
    out["fps, 10th percentile"] = percentile([b["fps"] for b in beats], 10)
    out["ticks/s, mean"] = mean(b["ticks/s"] for b in beats)
    keys = []
    for b in beats:
        keys += [k for k in b if k not in keys and isinstance(b[k], float)]
    groups = (("budget", ("clock ms a frame", "ticks ms a frame", "ticks a frame", "frame ms", "GE renderer CPU ms", "waiting for the GE ms", "the rest ms")),)
    for title, names in groups:
        for n in names:
            if n in keys and n != "frame ms":
                out["%s: %s" % (title, n)] = mean(b[n] for b in beats if n in b)
    for prefix in ("hw ", "objects ", "thread "):
        for k in keys:
            if k.startswith(prefix):
                out[k] = mean(b[k] for b in beats if k in b)
    for k, v in sorted(run.peaks.items()):
        out["peak " + k] = v
    return out

def steps_table(run):
    total = sum(s for s, _ in run.windows)
    sums = collections.Counter()
    for seconds, steps in run.windows:
        for name, ms in steps.items():
            sums[name] += ms * seconds
    return {name: v / total for name, v in sums.items()} if total else {}

def fmt(v):
    if v is None:
        return "-"
    if isinstance(v, int):
        return "%d" % v
    return "%.0f" % v if abs(v) >= 1000 else "%.2f" % v

def change(a, b):
    """B against A, in percent; nothing for values too small to say"""
    if a is None or b is None or max(abs(a), abs(b)) < 0.05:
        return ""
    if a == 0:
        return "new"
    return "%+.0f%%" % ((b - a) / abs(a) * 100.0)

# ---------------------------------------------------------------- printing

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+", help="a run's log folder (or its log.txt); two for a comparison")
    ap.add_argument("--from", dest="from_ticks", type=float, default=150, help="first game tick counted (default 150)")
    ap.add_argument("--to", type=float, default=None, help="last game tick counted")
    ap.add_argument("--near", default=None, help="X,Y,Z,R: only heartbeats with the player within R world units")
    ap.add_argument("--steps", type=int, default=25, help="profile steps shown (default 25)")
    ap.add_argument("--series", action="store_true", help="also every heartbeat in play, one line each")
    a = ap.parse_args()
    if len(a.logs) > 2:
        sys.exit("hwreport.py: one run, or two to compare")
    if a.near:
        a.near = tuple(float(v) for v in a.near.split(","))
    runs = [load(f) for f in a.logs]

    for k, r in enumerate(runs):
        print("run %s: %s; build %s; maps %s; keep %s; %d heartbeats, %d profile windows" % (
            "AB"[k] if len(runs) > 1 else "", r.folder, r.build, " ".join(r.maps) or "?", " ".join(r.keep) or "-",
            len(r.beats), len(r.windows)))
    sums = [summary(r, a) for r in runs]
    names = list(sums[0])
    for s in sums[1:]:
        names += [n for n in s if n not in names]
    width = max(len(n) for n in names) + 2
    print("\n## In play (game time %s to %s%s)\n" % (fmt(a.from_ticks), fmt(a.to) if a.to is not None else "the end",
                                                    ", near %s within %s" % (a.near[:3], a.near[3]) if a.near else ""))
    if len(runs) == 1:
        for n in names:
            print("%-*s %10s" % (width, n, fmt(sums[0].get(n))))
    else:
        print("%-*s %10s %10s %8s" % (width, "", "A", "B", "change"))
        for n in names:
            print("%-*s %10s %10s %8s" % (width, n, fmt(sums[0].get(n)), fmt(sums[1].get(n)), change(sums[0].get(n), sums[1].get(n))))

    tables = [steps_table(r) for r in runs]
    if any(tables):
        print("\n## Profile steps, ms a frame (time-weighted over the windows that ran ticks; windows left out: %s)\n"
              % ", ".join("%s %d" % ("AB"[k] if len(runs) > 1 else "", r.paused_windows) for k, r in enumerate(runs)).strip())
        order = sorted(set().union(*[set(t) for t in tables]), key=lambda n: -max(t.get(n, 0) for t in tables))[:a.steps]
        sw = max([len(n) for n in order] + [10]) + 2
        if len(runs) == 1:
            for n in order:
                print("%-*s %8.2f" % (sw, n, tables[0][n]))
        else:
            print("%-*s %8s %8s %8s" % (sw, "", "A", "B", "change"))
            for n in order:
                print("%-*s %8s %8s %8s" % (sw, n, fmt(tables[0].get(n)), fmt(tables[1].get(n)), change(tables[0].get(n), tables[1].get(n))))

    pools = sorted(set().union(*[set(r.pools) for r in runs]),
                   key=lambda n: -max(r.pools[n][0] / r.pools[n][1] if n in r.pools and r.pools[n][1] else 0 for r in runs))
    if pools:
        print("\n## Game-state pools, the highest peak of the whole run (count or KB, of the limit)\n")
        pw = max(len(n) for n in pools) + 2
        for n in pools:
            cells = ["%6d of %-6d %4.0f%%" % (r.pools[n][0], r.pools[n][1], 100.0 * r.pools[n][0] / r.pools[n][1])
                     if n in r.pools and r.pools[n][1] else "%22s" % "-" for r in runs]
            print("%-*s %s" % (pw, n, "   ".join(cells)))

    for k, r in enumerate(runs):
        if r.trouble:
            print("\n## Trouble%s (lines that report a failure, by kind)\n" % (" in run " + "AB"[k] if len(runs) > 1 else ""))
            for line, count in r.trouble.most_common(20):
                print("%5d  %s" % (count, line))

    if a.series:
        for k, r in enumerate(runs):
            print("\n## Heartbeats in play%s\n" % (" of run " + "AB"[k] if len(runs) > 1 else ""))
            print("%8s %-24s %7s %7s %7s %6s %8s" % ("tick", "player at", "tick ms", "max", "frame", "fps", "heap top"))
            for b in in_play(r, a):
                at = "%.1f %.1f %.1f" % b["at"] if "at" in b else "-"
                print("%8d %-24s %7.2f %7.2f %7.1f %6.1f %8d" % (b["game time"], at, b["tick ms"], b["tick max ms"],
                                                                b["frame ms"], b["fps"], b["heap top KB"]))

if __name__ == "__main__":
    main()
