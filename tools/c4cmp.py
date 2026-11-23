#!/usr/bin/env python3
"""c4cmp.py - C4's play-the-same comparer: two builds' biped traces, field by field.

Reads the "c4:" and "c4n:" lines a PSP_C4TRACE=1 build writes (keep words
c4traceN, c4spanM and, for the pose, c4pose; psp_fixups.py's c4_trace) and
compares the new build's run (B) with the old build's (A) on the same pad and
seed. Discrete state (which bipeds live, dead or alive, parent, held weapon)
must match exactly: the first tick it doesn't is the parting tick. Before it,
each continuous field's drift is summed up as p50 / p99 / max, with the worst
biped. The lab's tick_rewrite_plan.md section 9 sets the bands and the rule.

usage: python3 c4cmp.py A/log.txt B/log.txt [--ref RA/log.txt RB/log.txt]
  --ref  the calibration pair (the old build against itself with one change no
         player could see): B passes when its p99 per field is within 2x the
         reference's and it parts no sooner than the reference
"""
import math
import re
import struct
import sys

LINE = re.compile(r"c4: (\d+) ([0-9a-f]{8}) ([01]) ([0-9a-f]{8}) ([0-9a-f]{8}) \| (\S+) (\S+) (\S+) \| (\S+) (\S+) (\S+) \| (\S+) (\S+) (\S+)")
NODE = re.compile(r"c4n: (\d+) ([0-9a-f]{8}) (\d+) \| (\S+) (\S+) (\S+) \| (\S+) (\S+) (\S+) \| (\S+) (\S+) (\S+)")
# the provisional bands (tick_rewrite_plan.md section 9), shown beside the drift
BANDS = {"position": 0.01, "velocity": 0.002, "facing": 0.5, "node position": 0.01, "node rotation": 1.0}
CEILING = 3.5  # a biped's position past the host's acceptance: a failure, never the pass line


def real(bits):
    return struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]


def read(path):
    bipeds, nodes = {}, {}
    with open(path, errors="replace") as log:
        for text in log:
            m = LINE.search(text)
            if m:
                g = m.groups()
                bipeds[(int(g[0]), g[1])] = {
                    "discrete": (g[2], g[3], g[4]),
                    "position": [real(x) for x in g[5:8]],
                    "velocity": [real(x) for x in g[8:11]],
                    "forward": [real(x) for x in g[11:14]],
                }
                continue
            m = NODE.search(text)
            if m:
                g = m.groups()
                nodes[(int(g[0]), g[1], int(g[2]))] = (
                    [real(x) for x in g[3:6]], [real(x) for x in g[6:9]], [real(x) for x in g[9:12]])
    return bipeds, nodes


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def angle(a, b):
    la, lb = math.sqrt(sum(x * x for x in a)), math.sqrt(sum(x * x for x in b))
    if not la or not lb:
        return 0.0
    c = sum(x * y for x, y in zip(a, b)) / (la * lb)
    return math.degrees(math.acos(max(-1.0, min(1.0, c))))


def percentile(values, q):
    if not values:
        return 0.0
    values = sorted(values)
    return values[min(len(values) - 1, int(q * (len(values) - 1) + 0.5))]


def compare(path_a, path_b):
    a, an = read(path_a)
    b, bn = read(path_b)
    ticks = sorted({t for t, _ in a} | {t for t, _ in b})
    if not ticks:
        return None
    parting, parted_by = None, None
    for t in ticks:
        live_a = {i for tt, i in a if tt == t}
        live_b = {i for tt, i in b if tt == t}
        if live_a != live_b:
            parting, parted_by = t, "the live bipeds (%d only in A, %d only in B)" % (len(live_a - live_b), len(live_b - live_a))
            break
        for i in sorted(live_a):
            if a[(t, i)]["discrete"] != b[(t, i)]["discrete"]:
                names = ("dead", "parent", "held weapon")
                which = [n for n, x, y in zip(names, a[(t, i)]["discrete"], b[(t, i)]["discrete"]) if x != y]
                parting, parted_by = t, "biped %s's %s" % (i, ", ".join(which))
                break
        if parting is not None:
            break
    drift = {name: [] for name in BANDS}
    worst = {name: (0.0, None, None) for name in BANDS}
    breaches = []

    def note(name, value, t, i):
        drift[name].append(value)
        if value > worst[name][0]:
            worst[name] = (value, t, i)

    for (t, i), old in a.items():
        if parting is not None and t >= parting or (t, i) not in b:
            continue
        new = b[(t, i)]
        d = distance(old["position"], new["position"])
        note("position", d, t, i)
        if d > CEILING:
            breaches.append((t, i, d))
        speed = math.sqrt(sum(x * x for x in old["velocity"]))
        # velocity's band is the larger of 0.002 units a tick and 2% of the old speed: drift is shown in band units
        note("velocity", distance(old["velocity"], new["velocity"]) / max(0.002, 0.02 * speed) * BANDS["velocity"], t, i)
        note("facing", angle(old["forward"], new["forward"]), t, i)
    for (t, i, k), (pa, fa, ua) in an.items():
        if parting is not None and t >= parting or (t, i, k) not in bn:
            continue
        pb, fb, ub = bn[(t, i, k)]
        note("node position", distance(pa, pb), t, i)
        note("node rotation", max(angle(fa, fb), angle(ua, ub)), t, i)
    return {"ticks": (ticks[0], ticks[-1]), "parting": parting, "parted_by": parted_by,
            "drift": drift, "worst": worst, "breaches": breaches}


def report(result, label):
    first, last = result["ticks"]
    print("%s: ticks %d to %d" % (label, first, last))
    if result["parting"] is None:
        print("  tracking to the end: no parting")
    else:
        print("  parting tick %d: %s" % (result["parting"], result["parted_by"]))
    for name, band in BANDS.items():
        values = result["drift"][name]
        if not values:
            print("  %-14s no samples" % name)
            continue
        value, t, i = result["worst"][name]
        print("  %-14s p50 %.6g  p99 %.6g  max %.6g (band %g)%s" % (
            name, percentile(values, 0.5), percentile(values, 0.99), max(values), band,
            "  worst: biped %s at tick %d" % (i, t) if t is not None else ""))
    if result["breaches"]:
        t, i, d = result["breaches"][0]
        print("  CEILING: %d ticks past %.1f units, first biped %s at tick %d (%.3f)" % (len(result["breaches"]), CEILING, i, t, d))


def main(argv):
    if len(argv) not in (2, 5) or (len(argv) == 5 and argv[2] != "--ref"):
        print(__doc__)
        return 2
    result = compare(argv[0], argv[1])
    if result is None:
        print("no c4: lines in common (keep word c4traceN on a PSP_C4TRACE=1 build)")
        return 1
    report(result, "new against old")
    if len(argv) == 5:
        reference = compare(argv[3], argv[4])
        if reference is None:
            print("the reference pair has no c4: lines")
            return 1
        report(reference, "reference (the old build against itself)")
        verdict = []
        if result["parting"] is not None and (reference["parting"] is None or result["parting"] < reference["parting"]):
            verdict.append("parts sooner than the reference")
        for name in BANDS:
            ours, theirs = result["drift"][name], reference["drift"][name]
            # a reference with no drift at all in a field holds it to the provisional band instead
            limit = 2 * percentile(theirs, 0.99) if theirs and percentile(theirs, 0.99) > 0 else BANDS[name]
            if ours and percentile(ours, 0.99) > limit:
                verdict.append("%s's p99 past %s" % (name, "2x the reference's" if limit != BANDS[name] else "its band"))
        if result["breaches"]:
            verdict.append("past the ceiling")
        print("verdict: %s" % ("FAIL: " + "; ".join(verdict) if verdict else "pass (inside 2x the reference, parting no sooner)"))
        if reference["parting"] is not None and reference["parting"] - reference["ticks"][0] < 100:
            print("note: the reference parts within 100 ticks of the trace's start: the outcome census decides this stretch")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
