#!/usr/bin/env python3
"""hwphases.py - hwprofile's phases against the clock, over a stretch of play
(docs/plan_measurement_round.md, M4: "the missing third").

hwprofile's "hardware by phase" line gives each phase's share of the CPU's
cycles. That counter stops while the idle thread has the CPU halted (a
frame-rate cap, a wait on the stick or the GE), so a window that waits shows
its working phases as larger shares than their share of the clock; the
budget line's split (ticks, the frame, the rest) is by the clock. This puts
them side by side, in milliseconds a frame:

  cycles     each phase's active cycles a frame (at 333 MHz), from the
             "hardware" line's CPU cycles and the phase shares
  halted     the CPU's halted time a frame: the system counter (counter 0,
             always running) less the CPU's
  clock      with the "of the clock" line (this branch on): each phase's
             share of the system counter, so of the clock, and how much of
             it the CPU was halted
  threads    with "hwthreads": each phase's clock split between the game's
             thread, the other threads and the idle thread
  budget     the budget line: ticks, the frame (render and frame end) and
             the rest, by the game's own clock

usage: hwphases.py LOGDIR --map b30 [--from 90] [--to 450]
"""
import argparse, os, re

NUMBER = r"(-?[0-9]+(?:\.[0-9]+)?)"
PHASES = ["other", "tick", "drawing", "frame end"]
MHZ = 333.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--map")
    ap.add_argument("--from", dest="start", type=float, default=5.0)
    ap.add_argument("--to", dest="end", type=float, default=1e9)
    a = ap.parse_args()
    current, last, inside, fps, tps = "?", None, False, 0.0, 0.0
    frames = cpu = system = 0.0
    active = dict.fromkeys(PHASES, 0.0)
    clock = dict.fromkeys(PHASES, 0.0)
    halted = dict.fromkeys(PHASES, 0.0)
    threads = {p: [0.0, 0.0, 0.0] for p in PHASES}   # game, others, idle (ms)
    have_clock = have_threads = False
    budget = [0.0, 0.0, 0.0, 0.0]  # clock, ticks, frame, rest (ms, summed over frames)
    window_cycles = window_system = 0.0
    for line in open(os.path.join(a.folder, "log.txt"), errors="replace"):
        m = re.search(r"map d:\\maps\\(\w+)\.map: opened", line)
        if m:
            current, last = m.group(1), None
            continue
        m = re.match(r"heartbeat: game time (-?\d+) \(" + NUMBER + r" ticks/s\).*?" + NUMBER + r" fps", line)
        if m:
            t = int(m.group(1))
            tps, fps = float(m.group(2)), float(m.group(3))
            inside = (a.map is None or current == a.map) and last is not None and a.start * 30 < t <= a.end * 30 and \
                last >= a.start * 30 - 1 and t > last
            last = t
            continue
        if not inside:
            continue
        m = re.match(r"budget: " + NUMBER + r" ms a frame by the clock.*ticks " + NUMBER + r" ms a frame.*frame " + NUMBER +
                     r" \(.*the rest " + NUMBER, line)
        if m:
            budget_line = [float(v) for v in m.groups()]
            continue
        m = re.match(r"hardware: " + NUMBER + r" M CPU cycles \(" + NUMBER + r" M system\)", line)
        if m:
            window_cycles, window_system = float(m.group(1)) * 1e6, float(m.group(2)) * 1e6
            n = fps * window_system / (MHZ * 1e6)
            frames += n
            cpu += window_cycles
            system += window_system
            try:
                for k in range(4):
                    budget[k] += budget_line[k] * n
            except NameError:
                pass
            continue
        if line.startswith("hardware by phase") and "of the clock" not in line:
            for p in PHASES:
                m = re.search(r"\b" + p + r" " + NUMBER + r"% \(memory", line)
                if m:
                    active[p] += window_cycles * float(m.group(1)) / 100.0
            continue
        if line.startswith("hardware by phase, of the clock"):
            have_clock = True
            for p in PHASES:
                m = re.search(r"\b" + p + r" " + NUMBER + r"% \(halted " + NUMBER + r"%\)", line)
                if m:
                    c = window_system * float(m.group(1)) / 100.0
                    clock[p] += c
                    halted[p] += c * float(m.group(2)) / 100.0
            continue
        if line.startswith("hardware by thread"):
            have_threads = True
            for p in PHASES:
                m = re.search(r"\b" + p + r" " + NUMBER + r"% \(game " + NUMBER + r"%, others " + NUMBER + r"%, idle " + NUMBER, line)
                if m:
                    c = window_system * float(m.group(1)) / 100.0
                    for k in range(3):
                        threads[p][k] += c * float(m.group(2 + k)) / 100.0
    if not frames:
        raise SystemExit("no hardware lines in the stretch (keep word hwprofile)")
    ms = lambda c: c / frames / (MHZ * 1e3)
    print("stretch: map %s, game time %.0f-%s s; %.0f frames; the CPU halted %.1f%% of the clock" % (
        a.map or "any", a.start, "end" if a.end >= 1e9 else "%.0f" % a.end, frames, 100.0 * (system - cpu) / system))
    print("budget (ms a frame, the game's clock): clock %.1f = ticks %.1f + frame %.1f + rest %.1f" % tuple(b / frames for b in budget))
    print()
    head = "%-10s %10s %8s" % ("phase", "active ms", "share")
    if have_clock:
        head += " %10s %8s %10s" % ("clock ms", "share", "halted ms")
    if have_threads:
        head += " %9s %9s %9s" % ("game ms", "others", "idle")
    print(head)
    for p in PHASES:
        row = "%-10s %10.2f %7.1f%%" % (p, ms(active[p]), 100.0 * active[p] / cpu)
        if have_clock:
            row += " %10.2f %7.1f%% %10.2f" % (ms(clock[p]), 100.0 * clock[p] / system, ms(halted[p]))
        if have_threads:
            row += " %9.2f %9.2f %9.2f" % tuple(ms(v) for v in threads[p])
        print(row)
    print("%-10s %10.2f %8s" % ("all", ms(cpu), "") + (" %10.2f" % ms(system) if have_clock else ""))
    print("\nactive: the CPU's own cycles (the counter hwprofile's shares are of); clock: the system counter, by the phase it ran in")


if __name__ == "__main__":
    main()
