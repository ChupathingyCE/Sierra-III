#!/usr/bin/env python3
"""drivecmp.py - scripted Warthog drives ("drivetest") compared.

Reads the "drivetest:" lines the keep word drivetest writes (psp_fixups.py,
FIXES_DRIVE), from a PSP or emulator log (log.txt) or from the PC reference
rig's trace (sierra_trace.txt or .txt.gz: ChupathingyCE's desktop build with
the same hook). Each run gets a line per stretch of the schedule with what
the two vehicle complaints would show:

  ram     ticks 30-180, full ahead at the nearest scenery: how close the
          Warthog's origin came to the scenery's, level (closest), and how
          fast it then went away from it (bounce: the most level speed away
          from the scenery after the closest point);
  reverse ticks 180-240;
  left    ticks 240-540, full ahead at full left lock (a donut);
  right   ticks 540-840, the same to the right.

For each stretch: the mean speed, the mean turn rate (spin about the up
axis), the wobble (the root mean square of the spin about the two level
axes: roll and pitch, which a clean donut on flat ground keeps near zero),
and the lowest up.k (how far it tipped).

With two runs or more, each is then held against the first, step by step:
the first step whose place differs at all (bit-exact runs agree to the
printed 4 decimals all the way), and the largest gap in place per stretch.

usage: python3 drivecmp.py LOG [LOG ...]   (a label=path argument names a run)"""
import gzip
import math
import re
import sys

STEP = re.compile(r'drivetest: t=(\d+) step (\d+) throttle (-?\d+) steer (-?\d+) \| pos (\S+) (\S+) (\S+) '
                  r'vel (\S+) (\S+) (\S+) fwd (\S+) (\S+) (\S+) up (\S+) (\S+) (\S+) spin (\S+) (\S+) (\S+)')
MADE = re.compile(r'drivetest: t=(\d+) (\S+) #\w+ made at (\S+) (\S+) (\S+) heading \S+ \S+; target (\S+) at (\S+) (\S+) (\S+) radius (\S+)')
STRETCHES = [('ram', 30, 180), ('reverse', 180, 240), ('left', 240, 540), ('right', 540, 840)]


def lines(path):
    opener = gzip.open if path.endswith('.gz') else open
    with opener(path, 'rt', errors='replace') as f:
        for line in f:
            yield line


def read(path):
    steps, made = {}, None
    for line in lines(path):
        m = STEP.search(line)
        if m:
            v = [float(x) for x in m.groups()[4:]]
            steps[int(m.group(2))] = {'pos': v[0:3], 'vel': v[3:6], 'fwd': v[6:9], 'up': v[9:12], 'spin': v[12:15],
                                      'raw': m.group(0).split('|', 1)[1]}
            continue
        m = MADE.search(line)
        if m:
            made = {'start': int(m.group(1)), 'at': [float(m.group(k)) for k in (3, 4, 5)], 'target': m.group(6),
                    'target_at': [float(m.group(k)) for k in (7, 8, 9)], 'radius': float(m.group(10))}
    return steps, made


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def length(a):
    return math.sqrt(dot(a, a))


def stretch_stats(steps, made, first, end):
    picked = [steps[s] for s in range(first, end) if s in steps]
    if not picked:
        return None
    out = {'ticks': len(picked)}
    out['speed'] = sum(length(p['vel']) for p in picked) / len(picked) * 30.0
    yaw, wobble = [], []
    for p in picked:
        up = p['up']
        yaw.append(dot(p['spin'], up))
        level = [p['spin'][k] - dot(p['spin'], up) * up[k] for k in range(3)]
        wobble.append(dot(level, level))
    out['turn'] = sum(yaw) / len(yaw) * 30.0 * 180.0 / math.pi
    out['wobble'] = math.sqrt(sum(wobble) / len(wobble)) * 30.0 * 180.0 / math.pi
    out['upk'] = min(p['up'][2] for p in picked)
    if made and made['target'] != 'none':
        target = made['target_at']
        gaps = [math.dist(p['pos'][:2], target[:2]) for p in picked]
        nearest = gaps.index(min(gaps))
        out['closest'] = gaps[nearest]
        # speed away from the target, level, once the closest point is past
        bounce = 0.0
        for p in picked[nearest:]:
            away = [p['pos'][0] - target[0], p['pos'][1] - target[1]]
            norm = math.hypot(*away) or 1.0
            bounce = max(bounce, (p['vel'][0] * away[0] + p['vel'][1] * away[1]) / norm)
        out['bounce'] = bounce * 30.0
    return out


def main():
    runs = []
    for arg in sys.argv[1:]:
        label, _, path = arg.rpartition('=') if '=' in arg else (arg, '', arg)
        steps, made = read(path)
        runs.append((label, steps, made))
    if not runs:
        sys.exit(__doc__)
    for label, steps, made in runs:
        print('== %s: %d steps%s' % (label, len(steps), '' if not made else
              '; made at %.2f %.2f %.2f, target %s at %.2f %.2f (radius %.2f)' % (
                  made['at'][0], made['at'][1], made['at'][2], made['target'], made['target_at'][0],
                  made['target_at'][1], made['radius'])))
        for name, first, end in STRETCHES:
            s = stretch_stats(steps, made, first, end)
            if not s:
                print('   %-7s no steps' % name)
                continue
            extra = ''
            if name == 'ram' and 'closest' in s:
                extra = ', closest %.3f to the target\'s centre, bounce %.2f wu/s' % (s['closest'], s['bounce'])
            print('   %-7s %3d ticks: speed %5.2f wu/s, turn %7.1f deg/s, wobble %6.1f deg/s, lowest up.k %.3f%s' % (
                name, s['ticks'], s['speed'], s['turn'], s['wobble'], s['upk'], extra))
    base_label, base, _ = runs[0]
    for label, steps, _ in runs[1:]:
        common = sorted(set(base) & set(steps))
        differ = [s for s in common if base[s]['raw'] != steps[s]['raw']]
        print('-- %s against %s: %d steps in common, %s' % (
            label, base_label, len(common), 'identical to the printed digits' if not differ else
            'first differs at step %d' % differ[0]))
        for name, first, end in STRETCHES:
            gaps = [math.dist(base[s]['pos'], steps[s]['pos']) for s in common if first <= s < end]
            if gaps:
                print('   %-7s largest gap in place %.3f wu' % (name, max(gaps)))


if __name__ == '__main__':
    main()
