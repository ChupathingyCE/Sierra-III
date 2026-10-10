#!/usr/bin/env python3
"""statecmp.py - two runs' game states compared tick by tick (build 112).

Reads the "state:" lines that the keep word statecrc writes (game_state.c,
psp_fixups.py): a hash of the whole game state after every tick, and every
30 ticks a hash of each allocation. With fixedticks and no input, a run
plays the same way on any build, so a change meant to leave the game exactly
as it was must give the same lines.

usage: python3 statecmp.py A/log.txt B/log.txt [--ignore name,name,...]
  --ignore  allocations left out of the verdict (their names as the log
            gives them, e.g. "first person weapons"): parts that differ
            between two runs of the same build, so can't judge a change

Prints the first tick whose whole-state hash differs, then for each 30-tick
checkpoint the allocations that differ (the first few checkpoints and every
change in the set), and a verdict on the allocations not ignored."""
import re
import sys

args = [a for a in sys.argv[1:] if not a.startswith('--')]
ignore = set()
for a in sys.argv[1:]:
    if a.startswith('--ignore='):
        ignore |= {n.strip() for n in a[len('--ignore='):].split(',') if n.strip()}

ENTRY = re.compile(r' (.+?) ([0-9a-f]{8})(?= |$)')

def read(path):
    whole, records = {}, {}
    for line in open(path, errors='replace'):
        m = re.search(r'state: tick (\d+) ([0-9a-f]{8})', line)
        if m:
            whole[int(m.group(1))] = m.group(2)
            continue
        m = re.search(r'state: records at tick (\d+):(.*)$', line.rstrip('\n'))
        if m:
            tick = int(m.group(1))
            entries = records.setdefault(tick, [])
            entries.extend(ENTRY.findall(m.group(2)))
    return whole, records

def by_name(entries):
    """name -> hashes in order (some names repeat: "globals" of several kinds)"""
    named = {}
    for name, value in entries:
        named.setdefault(name, []).append(value)
    return named

if len(args) != 2:
    sys.exit(__doc__)
wa, ra = read(args[0])
wb, rb = read(args[1])
common = sorted(set(wa) & set(wb))
differ = [t for t in common if wa[t] != wb[t]]
print('ticks: %d and %d, %d in common; whole state differs at %d of them%s'
      % (len(wa), len(wb), len(common), len(differ), ', first at tick %d' % differ[0] if differ else ''))

checkpoints = sorted(set(ra) & set(rb))
last, judged = None, set()
for tick in checkpoints:
    na, nb = by_name(ra[tick]), by_name(rb[tick])
    names = sorted(set(na) | set(nb))
    different = [n for n in names if na.get(n) != nb.get(n)]
    judged |= {n for n in different if n not in ignore}
    if different != last or tick == checkpoints[0]:
        print('  tick %6d: %s' % (tick, ', '.join(different) if different else 'all the same'))
        last = different
if checkpoints:
    print('allocations compared: %d, at %d checkpoints (ticks %d to %d)'
          % (len(by_name(ra[checkpoints[0]])), len(checkpoints), checkpoints[0], checkpoints[-1]))
print('verdict: %s' % ('the same' if not judged else 'different: ' + ', '.join(sorted(judged)))
      + (' (ignoring %s)' % ', '.join(sorted(ignore)) if ignore else ''))
