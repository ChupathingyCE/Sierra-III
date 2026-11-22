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

A heartbeat stands for the seconds since the one before it, and heartbeats
are not the same length (a run's first 20 are a second long, the later ones
five), so each counts for its own span, as benchreport.py counts them: fps
is the half's frames over its seconds, ticks a second its ticks over its
seconds, and the tick is averaged over the ticks.

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
    """Yields (map, seconds, [(game_time, ticks_s, tick_ms, fps, ticks)]) for each turn.

    A heartbeat's ticks are its game time less the heartbeat's before it (any
    heartbeat's, in the turn or not); the first in the log, and one after
    game time went back, is taken as a second long."""
    level, seconds, beats, before = None, 0, [], None
    for line in open(path, errors="replace"):
        m = TURN.match(line)
        if m:
            if level:
                yield level, seconds, beats
            level, seconds, beats = m.group(1), int(m.group(2)), []
            continue
        m = BEAT.match(line)
        if m:
            g, ticks_s = int(m.group(1)), float(m.group(2))
            ticks = g - before if before is not None and g > before else min(g, ticks_s)
            before = g
            if level and ticks_s > 0:
                beats.append((g, ticks_s, float(m.group(3)), float(m.group(5)), ticks))
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
        # each heartbeat for its own span: its seconds are its ticks over its ticks a second
        wall = sum(b[4] / b[1] for b in half)
        ticks = sum(b[4] for b in half)
        fps = sum(b[3] * b[4] / b[1] for b in half) / wall
        tick = sum(b[2] * b[4] for b in half) / ticks
        ticks_s = ticks / wall
        print(f"| {label} | {fps:.1f} | {tick:.1f} | {ticks_s:.1f} | {len(half)} |")


if __name__ == "__main__":
    main()
