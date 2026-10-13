#!/usr/bin/env python3
"""ladderreport.py - the ladder's maps (keep word ladderN), one row each.

The ladder (build 117) plays each campaign map's opening for N seconds of
game time, one after another, in one session. This reads a run's log and
prints, for each map, the heartbeats' averages over the second half of its
turn by game time (the first half holds the load and the opening's settling;
the build docs' tables have always used the second half):

  fps            frames a second
  tick ms        a tick's own cost
  ticks a second the game's speed (30 is real time)

fps and ticks a second are rates over equal heartbeat windows, so their
plain averages are the stretch's own; the tick is weighted by frames, as
benchreport.py does.

usage: ladderreport.py LOG [--names]
  LOG: a log.txt, or the folder holding it
  --names: the maps' titles beside their build names
Prints a Markdown table, ready for a build_bNNN.md or hardware_bNNN.md.
"""
import os
import re
import sys

TITLES = {"a10": "The Pillar of Autumn", "a30": "Halo", "a50": "Truth and Reconciliation",
          "b30": "The Silent Cartographer", "b40": "Assault on the Control Room",
          "c10": "343 Guilty Spark", "c20": "The Library", "c40": "Two Betrayals",
          "d20": "Keyes", "d40": "The Maw"}
TURN = re.compile(r"^ladder: (\w+) for (\d+) s of game time")
BEAT = re.compile(r"^heartbeat: game time (\d+) \(([\d.]+) ticks/s\) \| tick ([\d.]+) ms avg.*?\| frame ([\d.]+) ms avg.*?([\d.]+) fps")


def read(path):
    """Yields (map, seconds, [(game_time, ticks_s, tick_ms, fps)]) for each turn."""
    level, seconds, beats = None, 0, []
    for line in open(path, errors="replace"):
        m = TURN.match(line)
        if m:
            if level:
                yield level, seconds, beats
            level, seconds, beats = m.group(1), int(m.group(2)), []
            continue
        m = BEAT.match(line)
        if m and level:
            beats.append((int(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(5))))
    if level:
        yield level, seconds, beats


def second_half(seconds, beats):
    """The turn's heartbeats from half its game time on, its own clock starting at its first heartbeat."""
    if not beats:
        return []
    start = beats[0][0]
    return [b for b in beats if b[0] - start >= seconds * 30 // 2]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        sys.exit(__doc__)
    path = args[0]
    if os.path.isdir(path):
        path = os.path.join(path, "log.txt")
    names = "--names" in sys.argv
    print("| Level | fps | Tick (ms) | Ticks a second | Heartbeats |")
    print("|---|---:|---:|---:|---:|")
    for level, seconds, beats in read(path):
        half = second_half(seconds, beats)
        label = f"{TITLES.get(level, level)} ({level})" if names else level
        if not half:
            print(f"| {label} | - | - | - | 0 (the turn ended before its second half) |")
            continue
        # the windows are of equal length, so a window's fps is its frames' weight
        frames = sum(b[3] for b in half)
        fps = frames / len(half)
        tick = sum(b[2] * b[3] for b in half) / frames
        ticks_s = sum(b[1] for b in half) / len(half)
        print(f"| {label} | {fps:.1f} | {tick:.1f} | {ticks_s:.1f} | {len(half)} |")


if __name__ == "__main__":
    main()
