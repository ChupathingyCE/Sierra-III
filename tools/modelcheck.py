#!/usr/bin/env python3
"""modelcheck.py - the cache model against the PSP, phase by phase, over one
stretch of play (docs/plan_measurement_round.md, M3).

The PSP's side is a hardware run's log with the keep word hwprofile: each
heartbeat's "hardware" lines give the CPU's cycles, its share by phase (other,
tick, drawing, frame end), the time waiting on memory and the I- and D-cache
misses per million cycles. The heartbeats whose game time falls in the
stretch are summed.

The model's side is two emulator runs with the cache model and its timing
(the lab's batch fields cache = 1, pfmap = 1), one ending at the stretch's
start and one at its end (exitatN), with fixedticks so both play the same
way up to the first's end: the stretch is their difference. cache.txt's
DCTIME lines give cycles and the I- and D-miss stalls by phase, its phase
table the misses; fixedticks makes every frame two ticks.

For each phase it prints, on the PSP and in the model: cycles a tick (the
tick) or a frame (the others) in milliseconds at 333 MHz, the share of the
cycles waiting on memory, and the I- and D-misses per million cycles; then
the model's error, model over PSP. The model's clock is PPSSPP's cycle
estimate plus the stalls (VARIANTS.md item 12), so its error is what a
ranking taken from the model inherits.

usage: modelcheck.py PSP_LOGDIR EMU_TO_A EMU_TO_B --map b30 --from 90 --to 170
"""
import argparse, os, re

NUMBER = r"(-?[0-9]+(?:\.[0-9]+)?)"
PHASES = ["other", "tick", "drawing", "frame end"]
MHZ = 333.0


def psp_side(folder, mapname, start, end):
    """sums over the heartbeats in the stretch: cycles, memory-wait cycles,
    I and D misses by phase; ticks; frames"""
    sums = {p: {"cycles": 0.0, "memory": 0.0, "i": 0.0, "d": 0.0} for p in PHASES}
    ticks = frames = 0.0
    beats = 0
    current_map, game_time, last_time, fps, cycles, seconds, inside = "?", -1, None, 0.0, 0.0, 0.0, False
    for line in open(os.path.join(folder, "log.txt"), errors="replace"):
        m = re.search(r"map d:\\maps\\(\w+)\.map: opened", line)
        if m:
            current_map, last_time = m.group(1), None
            continue
        m = re.match(r"heartbeat: game time (-?\d+) .*?" + NUMBER + r" fps", line)
        if m:
            game_time = int(m.group(1))
            fps = float(m.group(2))
            inside = (mapname is None or current_map == mapname) and last_time is not None and \
                start * 30 < game_time <= end * 30 and last_time >= start * 30 - 1
            if inside:
                ticks += game_time - last_time
            last_time = game_time
            continue
        if not inside:
            continue
        m = re.match(r"hardware: " + NUMBER + r" M CPU cycles \(" + NUMBER + r" M system\)", line)
        if m:
            cycles = float(m.group(1)) * 1e6
            seconds = float(m.group(2)) * 1e6 / (MHZ * 1e6)
            frames += fps * seconds
            beats += 1
            continue
        if line.startswith("hardware by phase") and "of the clock" not in line:
            for p in PHASES:
                m = re.search(r"\b" + p + r" " + NUMBER + r"% \(memory " + NUMBER + r"%, asleep " + NUMBER + r"%, I " + NUMBER +
                              r", D " + NUMBER, line)
                if m:
                    c = cycles * float(m.group(1)) / 100.0
                    sums[p]["cycles"] += c
                    sums[p]["memory"] += c * float(m.group(2)) / 100.0
                    sums[p]["i"] += c / 1e6 * float(m.group(4))
                    sums[p]["d"] += c / 1e6 * float(m.group(5))
    return sums, ticks, frames, beats


def model_run(folder):
    text = open(os.path.join(folder, "cache.txt"), errors="replace").read()
    phases = {}
    for k, p in enumerate(PHASES):
        m = re.search(r"DCTIME: phase %d: (\d+) cycles, (\d+) D-miss stall, (\d+) I-miss stall" % k, text)
        if not m:
            raise SystemExit("%s: no DCTIME line for phase %d (the cache model's timing: pfmap = 1)" % (folder, k))
        cycles, dstall, istall = (float(v) for v in m.groups())
        # the phase table: accesses, D misses, fetches, I misses
        m = re.search(r"^" + p + r"\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s", text, re.M)
        dmiss = float(m.group(2)) if m else 0.0
        imiss = float(m.group(4)) if m else 0.0
        phases[p] = {"cycles": cycles, "memory": dstall + istall, "i": imiss, "d": dmiss}
    m = re.search(r"DCACHE: .*; (\d+) ticks", text)
    ticks = float(m.group(1)) if m else 0.0
    return phases, ticks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("psp")
    ap.add_argument("emu_a")
    ap.add_argument("emu_b")
    ap.add_argument("--map")
    ap.add_argument("--from", dest="start", type=float, required=True, help="game time, s: the stretch's start (run A's end)")
    ap.add_argument("--to", dest="end", type=float, required=True, help="game time, s: the stretch's end (run B's end)")
    a = ap.parse_args()
    psp, psp_ticks, psp_frames, beats = psp_side(a.psp, a.map, a.start, a.end)
    run_a, ticks_a = model_run(a.emu_a)
    run_b, ticks_b = model_run(a.emu_b)
    model = {p: {k: run_b[p][k] - run_a[p][k] for k in run_b[p]} for p in PHASES}
    model_ticks = ticks_b - ticks_a
    model_frames = model_ticks / 2.0  # fixedticks: two ticks a frame
    print("stretch: map %s, game time %.0f-%.0f s" % (a.map or "any", a.start, a.end))
    print("PSP: %s, %d heartbeats, %.0f ticks, %.0f frames (%.2f ticks a frame)" % (a.psp, beats, psp_ticks, psp_frames,
          psp_ticks / psp_frames if psp_frames else 0.0))
    print("model: %s minus %s, %.0f ticks, %.0f frames (fixedticks)" % (a.emu_b, a.emu_a, model_ticks, model_frames))
    print()
    print("%-10s %-6s %10s %8s %9s %9s | %10s %8s %9s %9s | %6s %6s %6s %6s" % (
        "phase", "per", "PSP ms", "memory%", "I/Mcyc", "D/Mcyc", "model ms", "memory%", "I/Mcyc", "D/Mcyc",
        "ms", "mem", "I", "D"))
    for p in PHASES:
        per, n_psp, n_model = ("tick", psp_ticks, model_ticks) if p == "tick" else ("frame", psp_frames, model_frames)
        row = []
        for side, n in ((psp[p], n_psp), (model[p], n_model)):
            c = side["cycles"]
            row.append((c / n / (MHZ * 1e3) if n else 0.0, 100.0 * side["memory"] / c if c else 0.0,
                        side["i"] / (c / 1e6) if c else 0.0, side["d"] / (c / 1e6) if c else 0.0))
        ratio = [m / s if s else 0.0 for s, m in zip(row[0], row[1])]
        print("%-10s %-6s %10.2f %8.1f %9.0f %9.0f | %10.2f %8.1f %9.0f %9.0f | %6.2f %6.2f %6.2f %6.2f" % (
            (p, per) + row[0] + row[1] + tuple(ratio)))
    print("\nms: the phase's cycles a tick or a frame at 333 MHz (the PSP's are active cycles: hwprofile's counter stops while the CPU is "
          "halted); memory%: cycles waiting on memory (PSP) or in the model's miss stalls; the last four columns are model / PSP.")


if __name__ == "__main__":
    main()
