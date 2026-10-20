#!/usr/bin/env python3
"""footprint.py - what each call of a function touches in the tick, by name.

Reads the emulator's footprint list (the cache model's PSP_FOOTPRINT_OUT:
for each function, every 64-byte line its calls in the tick touched and how
many calls touched it) and names the lines as cachemap.py does: the
program's functions and variables (from the ELF), the game state's
allocations (the run's log), and the regions around them.

    python3 footprint.py FOOTPRINT_RAW halo.elf log.txt [--top N]

For each function: its calls a tick; a call's code (distinct instruction
lines) and data (distinct data lines, the stack apart), and how much of the
data an earlier call in the same tick had touched (shared); a tick's calls
together; then the data a call touches by allocation, with how many of those
lines most calls touch (shared by every object: tags, globals) and how many
few calls touch (each object's own).

What loop phasing (one function's calls over every object, then the next
function's) needs from the caches, 16 KB each (256 lines):
  - code: the tick's instruction lines together must fit for the pass to
    fetch its code once a tick rather than once a call;
  - data: each call's own lines are fetched again in the pass (they were
    warm from the object's other updates when the update ran whole); the
    shared lines stay warm for the whole pass if they fit beside the call's
    own."""
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cachemap  # noqa: E402

LINE = 64
CACHE_LINES = 16384 // LINE
SUMMARY = re.compile(r'^F ([0-9a-f]{8}) calls (\d+) ticks (\d+) icall ([\d.]+) imax (\d+) dcall ([\d.]+) dmax (\d+) '
                     r'dshared ([\d.]+) stack ([\d.]+) itick ([\d.]+) dtick ([\d.]+)')


def read(path):
    funcs = []
    for line in open(path):
        m = SUMMARY.match(line)
        if m:
            g = m.groups()
            funcs.append({'entry': int(g[0], 16), 'calls': int(g[1]), 'ticks': int(g[2]), 'icall': float(g[3]),
                          'imax': int(g[4]), 'dcall': float(g[5]), 'dmax': int(g[6]), 'dshared': float(g[7]),
                          'stack': float(g[8]), 'itick': float(g[9]), 'dtick': float(g[10]), 'I': {}, 'D': {}})
            continue
        parts = line.split()
        if len(parts) == 3 and parts[0] in ('I', 'D') and funcs:
            funcs[-1][parts[0]][int(parts[1], 16)] = int(parts[2])
    return funcs


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    raw, elf, log = sys.argv[1:4]
    top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 12
    symbols = cachemap.elf_symbols(elf)
    program_end = max(end for start, end, name, kind in symbols)
    data_namer = cachemap.Namer([s for s in symbols if s[3] in 'bdgsrv'] + cachemap.game_state_layout(log))
    code_namer = cachemap.Namer([s for s in symbols if s[3] == 't'])
    for f in read(raw):
        name, _ = code_namer.name(f['entry'])
        calls = max(f['calls'], 1)
        print('== %s (%08x): %d calls in %d ticks, %.1f a tick' % (
            name or '?', f['entry'], f['calls'], f['ticks'], f['calls'] / max(f['ticks'], 1)))
        print('  a call:  %6.1f code lines (%.1f KB; the largest %d), %6.1f data lines (%.1f KB; the largest %d), '
              'stack lines %.1f' % (f['icall'], f['icall'] * LINE / 1024, f['imax'], f['dcall'], f['dcall'] * LINE / 1024,
                                    f['dmax'], f['stack']))
        print('           of the data lines, %.1f (%.0f%%) an earlier call in the tick had touched' % (
            f['dshared'], 100.0 * f['dshared'] / max(f['dcall'], 1e-9)))
        print('  a tick:  %6.1f code lines (%.1f KB), %6.1f data lines (%.1f KB), every call together' % (
            f['itick'], f['itick'] * LINE / 1024, f['dtick'], f['dtick'] * LINE / 1024))
        own = f['dcall'] - f['dshared']
        print('  phasing: the code %s the 16 KB cache (%.0f of 256 lines a tick); each call brings %.1f lines of its own '
              '(%.0f a tick to fetch again in a pass of its own), beside %.0f shared lines a tick' % (
                  'fits' if f['itick'] <= CACHE_LINES * 0.75 else 'does not fit', f['itick'], own,
                  own * f['calls'] / max(f['ticks'], 1), f['dtick'] - own * f['calls'] / max(f['ticks'], 1)))
        # the data by allocation: lines a call (sum of calls over lines / calls)
        by_name = defaultdict(lambda: [0, 0, 0, 0])  # line-calls, lines, lines most calls touch, lines few touch
        for address, n in f['D'].items():
            nm, _ = data_namer.name(address)
            key = nm or '(%s, unnamed: %06x)' % (cachemap.region(address, program_end), (address >> 16) << 16)
            e = by_name[key]
            e[0] += n
            e[1] += 1
            if n >= 0.5 * calls:
                e[2] += 1
            elif n <= max(2, 0.05 * calls):
                e[3] += 1
        print('  the data a call touches, by allocation (lines a call; the allocation\'s lines touched; '
              'of those, by half the calls or more / by few):')
        for key, (lc, lines, most, few) in sorted(by_name.items(), key=lambda kv: -kv[1][0])[:top]:
            print('    %7.1f  %6d  %5d / %6d  %s' % (lc / calls, lines, most, few, key))
        # the code: the lines most calls run, by function
        by_func = defaultdict(lambda: [0, 0])
        for address, n in f['I'].items():
            nm, _ = code_namer.name(address)
            e = by_func[nm or '?']
            e[0] += n
            e[1] += 1
        print('  the code a call runs, by function (lines a call; the function\'s lines run):')
        for key, (lc, lines) in sorted(by_func.items(), key=lambda kv: -kv[1][0])[:top]:
            print('    %7.1f  %6d  %s' % (lc / calls, lines, key))
        print()


if __name__ == '__main__':
    main()
