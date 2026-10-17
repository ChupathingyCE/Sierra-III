#!/usr/bin/env python3
"""doublecalls.py - which functions in a build call double-precision helpers.

The PSP's FPU is single precision: every double add, multiply, divide or
conversion is a libgcc call, and a double sqrt costs ~2,390 cycles against 31
(engine_performance.md section 1). psp-gcc doesn't narrow (float)sqrt(x),
floor, ceil or fmod to their float versions, so each new engine pin can bring
double maths back silently (research_sh4zam_crosscheck.md, open item 1).
This lists every function that calls one, and marks those the build's
profile placed in .text.hot (the link map's input sections).

    tools/doublecalls.py <out>/build/halo.elf [<out>/build/halo.map] [--hot-only] [--baseline FILE]

With --baseline (a file of hot function names, one a line: tools/doublecalls_hot.txt
holds b137's), the exit status is 1 when a hot function calls a double
helper and isn't in the baseline: what a new engine pin or change brought in.
Many baseline entries are a float passed to a log line's printf (promoted to
double by C's rules), on a cold path inside a hot function; the ones worth a
look are the divides and conversions in the tick. Needs psp-objdump on PATH.
"""
import os
import re
import subprocess
import sys

HELPERS = ("__adddf3", "__subdf3", "__muldf3", "__divdf3", "__extendsfdf2", "__truncdfsf2",
           "__fixdfsi", "__fixunsdfsi", "__floatsidf", "__floatunsidf", "__ltdf2", "__gtdf2",
           "__ledf2", "__gedf2", "__eqdf2", "__nedf2", "__unorddf2",
           "sqrt", "floor", "ceil", "fmod", "pow", "exp", "log", "sin", "cos", "tan", "atan2", "atan")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    hot_only = "--hot-only" in sys.argv
    baseline = None
    if "--baseline" in sys.argv:
        path = sys.argv[sys.argv.index("--baseline") + 1]
        args = [a for a in args if a != path]
        baseline = set(l.strip() for l in open(path) if l.strip() and not l.startswith("#"))
    if not args:
        sys.exit(__doc__)
    elf = args[0]
    linkmap = args[1] if len(args) > 1 else os.path.join(os.path.dirname(elf), "halo.map")
    hot = set()
    if os.path.exists(linkmap):
        for line in open(linkmap, errors="replace"):
            m = re.match(r"\s\.text\.hot\.(\S+)", line)
            if m:
                hot.add(m.group(1))
    out = subprocess.run(["psp-objdump", "-d", elf], capture_output=True, text=True, check=True).stdout
    calls = {}
    function = None
    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <([^>]+)>:$", line)
        if m:
            function = m.group(1)
            continue
        m = re.search(r"\sjal\s+[0-9a-f]+ <([^>+]+)>", line)
        if m and function and m.group(1) in HELPERS:
            calls.setdefault(function, {}).setdefault(m.group(1), 0)
            calls[function][m.group(1)] += 1
    hot_hits = 0
    new_hot = []
    rows = []
    for function, helpers in sorted(calls.items()):
        is_hot = function in hot
        hot_hits += is_hot
        if is_hot and baseline is not None and function not in baseline:
            new_hot.append(function)
        if hot_only and not is_hot:
            continue
        rows.append("%s%s: %s" % ("HOT " if is_hot else "    ", function,
                                  ", ".join("%s x%d" % (h, n) for h, n in sorted(helpers.items()))))
    print("\n".join(rows))
    print("doublecalls: %d functions call a double helper, %d of them in .text.hot (%d hot functions in the map)"
          % (len(calls), hot_hits, len(hot)))
    if baseline is not None:
        print("doublecalls: %d hot functions not in the baseline%s" % (len(new_hot), (": " + " ".join(new_hot)) if new_hot else ""))
        sys.exit(1 if new_hot else 0)
    sys.exit(0)


if __name__ == "__main__":
    main()
