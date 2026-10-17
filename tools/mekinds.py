#!/usr/bin/env python3
"""mekinds.py - reads the GE layer's "mekinds" lines (keep word mekinds:
each record kind timed as it's carried out, on the CPU that carried it out)
and prints each kind's time a frame on the main CPU and on the ME, its time a
record on each, and the ME's ratio. The ratio a record is what says where the
ME's extra time goes: a kind the ME runs 3x slower a record is the place to
look, whatever its share.

    tools/mekinds.py LOG_OR_FOLDER [...]   (a folder: every log.txt under it)
    tools/mekinds.py --skip 1 results/     (leave out each log's first N reports: the load)

Reports from every log are pooled per CPU. Times are the PSP's own clocks on
hardware; in the emulator they're its clock, good for shares only.
"""
import argparse
import glob
import os
import re
import sys

HEAD = re.compile(r"GE: mekinds, (the ME|the main CPU), us a frame over (\d+) frames \(([\d.]+) runs a frame\): all (\d+);(.*)")
MORE = re.compile(r"GE: mekinds, (the ME|the main CPU) \(more\):(.*)")
ENTRY = re.compile(r" ([^;]+?) (\d+)(?: \(([\d.]+) a frame, ([\d.]+) us each\))?;")
SIDES = ("the main CPU", "the ME")


def logs(paths):
    for path in paths:
        if os.path.isdir(path):
            yield from sorted(glob.glob(os.path.join(path, "**", "log.txt"), recursive=True))
        else:
            yield path


def read(path, skip, pool):
    """pool[side]: frames, all us, and per kind [us, records]"""
    seen = {side: 0 for side in SIDES}
    side = None
    keep = False
    with open(path, errors="replace") as f:
        for line in f:
            m = HEAD.search(line)
            if m:
                side, frames, rest = m.group(1), int(m.group(2)), m.group(5)
                seen[side] += 1
                keep = seen[side] > skip
                if keep:
                    p = pool[side]
                    p["frames"] += frames
                    p["all"] += int(m.group(4)) * frames
                    p["reports"] += 1
                    take(rest, frames, p)
                continue
            m = MORE.search(line)
            if m and keep and m.group(1) == side:
                take(m.group(2), frames, pool[side])


def take(text, frames, p):
    for name, us, per_frame, _each in ENTRY.findall(text):
        kind = p["kinds"].setdefault(name, [0.0, 0.0])
        kind[0] += int(us) * frames
        if per_frame:
            kind[1] += float(per_frame) * frames


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--skip", type=int, default=1, help="each log's first N reports per CPU left out (default 1)")
    args = parser.parse_args()

    pool = {side: {"frames": 0, "all": 0, "reports": 0, "kinds": {}} for side in SIDES}
    files = list(logs(args.paths))
    for path in files:
        read(path, args.skip, pool)
    if not any(pool[s]["reports"] for s in SIDES):
        sys.exit("mekinds: no mekinds reports in %d log(s)" % len(files))

    main_cpu, me = pool["the main CPU"], pool["the ME"]
    for side in SIDES:
        p = pool[side]
        if p["frames"]:
            print("%s: %d reports, %d frames, all %.0f us a frame" % (side, p["reports"], p["frames"], p["all"] / p["frames"]))
    names = sorted(set(main_cpu["kinds"]) | set(me["kinds"]),
                   key=lambda n: -max(main_cpu["kinds"].get(n, [0])[0] / max(main_cpu["frames"], 1),
                                      me["kinds"].get(n, [0])[0] / max(me["frames"], 1)))

    def per(p, name):
        us, records = p["kinds"].get(name, [0.0, 0.0])
        frames = max(p["frames"], 1)
        return us / frames, records / frames, (us / records if records else None)

    print("%-28s %10s %10s %9s %9s %9s %7s" % ("kind", "main us/f", "ME us/f", "records/f", "main us/r", "ME us/r", "ME/main"))
    for name in names:
        mf, mr, mu = per(main_cpu, name)
        ef, er, eu = per(me, name)
        ratio = "%.2f" % (eu / mu) if mu and eu else "-"
        print("%-28s %10.0f %10.0f %9.1f %9s %9s %7s" % (name[:28], mf, ef, mr or er,
              "%.1f" % mu if mu else "-", "%.1f" % eu if eu else "-", ratio))


if __name__ == "__main__":
    main()
