#!/usr/bin/env python3
"""statecmp.py - two runs' game states compared tick by tick (build 112).

Reads the "state:" lines that the keep word statecrc writes (game_state.c,
psp_fixups.py): a hash of the whole game state after every tick, and every
30 ticks a hash of each allocation. With fixedticks and no input, a run
plays the same way on any build, so a change meant to leave the game exactly
as it was must give the same lines.

usage: python3 statecmp.py A/log.txt B/log.txt [--ignore name,name,...] [--objects]
  --ignore  allocations left out of the verdict (their names as the log
            gives them, e.g. "first person weapons"): parts that differ
            between two runs of the same build, so can't judge a change
  --objects the runs' per-object play hashes (keep word stateobjectsN,
            build 113) compared tick by tick: the first tick they differ,
            and which objects (by definition name) are only in one run or
            differ in place, motion, facing, health, parent or animation

Prints the first tick whose whole-state hash differs and the first whose
play hash differs, the first ticks the global and local random seeds differ
(build 113: "fixedseed" makes the local one start the same and keeps the
sound manager off it) (each object's kind, place, motion, facing, health, parent
and animation frame: what the game plays with, without what drawing writes
into objects), then for each 30-tick checkpoint the allocations that differ
(the first checkpoint and every change in the set), and a verdict on the
allocations not ignored."""
import re
import sys

args = [a for a in sys.argv[1:] if not a.startswith('--')]
ignore = set()
for a in sys.argv[1:]:
    if a.startswith('--ignore='):
        ignore |= {n.strip() for n in a[len('--ignore='):].split(',') if n.strip()}

ENTRY = re.compile(r' (.+?) ([0-9a-f]{8})(?= |$)')

def read(path):
    whole, records, play, seeds = {}, {}, {}, {}
    for line in open(path, errors='replace'):
        m = re.search(r'state: tick (\d+) ([0-9a-f]{8})(?: play ([0-9a-f]{8}))?(?: seeds ([0-9a-f]{8}) ([0-9a-f]{8}))?', line)
        if m:
            whole[int(m.group(1))] = m.group(2)
            if m.group(3):
                play[int(m.group(1))] = m.group(3)
            if m.group(4):
                seeds[int(m.group(1))] = (m.group(4), m.group(5))
            continue
        m = re.search(r'state: records at tick (\d+):(.*)$', line.rstrip('\n'))
        if m:
            tick = int(m.group(1))
            entries = records.setdefault(tick, [])
            entries.extend(ENTRY.findall(m.group(2)))
    return whole, records, play, seeds

def read_objects(path):
    """tick -> {object index: (definition, hash)}, and definition -> name"""
    ticks, names = {}, {}
    for line in open(path, errors='replace'):
        m = re.search(r'state: objects at tick (\d+):(.*)$', line.rstrip('\n'))
        if m:
            entries = ticks.setdefault(int(m.group(1)), {})
            for index, definition, value in re.findall(r' ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})', m.group(2)):
                entries[index] = (definition, value)
            continue
        m = re.search(r'state: definition ([0-9a-f]{8}) is (.*)$', line.rstrip('\n'))
        if m:
            names[m.group(1)] = m.group(2)
    return ticks, names


def compare_objects(path_a, path_b):
    oa, na = read_objects(path_a)
    ob, nb = read_objects(path_b)
    names = dict(na, **nb)
    common = sorted(set(oa) & set(ob))
    if not common:
        print('objects: no per-object dumps in common (keep word stateobjectsN with statecrc)')
        return
    print('objects: dumps at %d ticks in common (%d to %d)' % (len(common), common[0], common[-1]))
    shown = 0
    for tick in common:
        a, b = oa[tick], ob[tick]
        only_a = sorted(set(a) - set(b))
        only_b = sorted(set(b) - set(a))
        differ = sorted(i for i in set(a) & set(b) if a[i] != b[i])
        if not (only_a or only_b or differ):
            continue
        def label(index, entry):
            return '%s %s' % (index, names.get(entry[0], entry[0]))
        print('  tick %6d: %d only in A, %d only in B, %d differ' % (tick, len(only_a), len(only_b), len(differ)))
        for index in only_a[:8]:
            print('      only in A: ' + label(index, a[index]))
        for index in only_b[:8]:
            print('      only in B: ' + label(index, b[index]))
        for index in differ[:8]:
            print('      differs:   ' + label(index, a[index]))
        shown += 1
        if shown >= 6:
            break
    if not shown:
        print('  every object the same at every tick dumped')


def by_name(entries):
    """name -> hashes in order (some names repeat: "globals" of several kinds)"""
    named = {}
    for name, value in entries:
        named.setdefault(name, []).append(value)
    return named

if len(args) != 2:
    sys.exit(__doc__)
if '--objects' in sys.argv:
    compare_objects(args[0], args[1])
    sys.exit(0)
wa, ra, pa, sa = read(args[0])
wb, rb, pb, sb = read(args[1])
common = sorted(set(wa) & set(wb))
differ = [t for t in common if wa[t] != wb[t]]
print('ticks: %d and %d, %d in common; whole state differs at %d of them%s'
      % (len(wa), len(wb), len(common), len(differ), ', first at tick %d' % differ[0] if differ else ''))
play_common = sorted(set(pa) & set(pb))
if play_common:
    play_differ = [t for t in play_common if pa[t] != pb[t]]
    print('play (the objects as the game plays them): %s'
          % ('the same at all %d ticks' % len(play_common) if not play_differ else
             'differs at %d of %d ticks, first at tick %d' % (len(play_differ), len(play_common), play_differ[0])))
seed_common = sorted(set(sa) & set(sb))
if seed_common:
    for k, name in ((0, 'global'), (1, 'local')):
        part = [t for t in seed_common if sa[t][k] != sb[t][k]]
        print('%s random seed: %s' % (name, 'the same at all %d ticks' % len(seed_common) if not part else
              'differs at %d of %d ticks, first at tick %d' % (len(part), len(seed_common), part[0])))

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
