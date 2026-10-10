"""benchparse.py - the table from a bench log: benchrun (build 87) or
ceilingbench (build 112), whose "bench: RESULT" lines are one per view and
switch, the measured window. ceilingbench's views are its cycles.
usage: python3 benchparse.py [--by-switch] log.txt [more logs...]
--by-switch: one row per switch, averaged over the views (or cycles), each
with its difference from the baseline's average: what switching that part off
saved, so the most any lever on it could save."""
import re, sys, collections

pat = re.compile(r'bench: RESULT view (\d+) \((.*?)\) \| switch (\d+) \((.*?)\) \| ([\d.]+) ms a frame, ([\d.]+) fps \| '
                 r'ticks ([\d.]+) ms \| frame ([\d.]+) ms \(renderer CPU ([\d.]+), GE wait ([\d.]+)\) \| ([\d.]+) ticks/s \| ME ([\d.]+)% busy')
by_switch = '--by-switch' in sys.argv
for path in [a for a in sys.argv[1:] if a != '--by-switch']:
    rows = [m.groups() for m in (pat.search(l) for l in open(path, errors='replace')) if m]
    print('%s: %d results' % (path, len(rows)))
    if by_switch:
        # ms a frame, ticks, frame, renderer CPU, ticks/s: averaged per switch
        sums = collections.OrderedDict()
        for v, vn, sw, sn, ms, fps, ticks, frame, rend, wait, tps, me in rows:
            s = sums.setdefault((int(sw), sn), [0.0] * 5 + [0])
            for k, x in enumerate((ms, ticks, frame, rend, tps)):
                s[k] += float(x)
            s[5] += 1
        avg = {key: [x / s[5] for x in s[:5]] + [s[5]] for key, s in sums.items()}
        base = next((a for (sw, _), a in avg.items() if sw == 0), None)
        print('  %-26s %3s %7s %7s %7s %7s %6s   %s' % ('switch', 'n', 'ms', 'ticks', 'frame', 'rend', 'tps', 'saved against the baseline (ms a frame: all, ticks, frame)'))
        for (sw, sn), a in sorted(avg.items()):
            saved = ''
            if base and sw != 0:
                saved = '%+6.1f  %+6.1f  %+6.1f' % (base[0] - a[0], base[1] - a[1], base[2] - a[2])
            print('  %-26s %3d %7.1f %7.1f %7.1f %7.1f %6.1f   %s' % (sn[:26], a[5], a[0], a[1], a[2], a[3], a[4], saved))
        continue
    print('  %-26s %-26s %6s %5s %6s %6s %6s %6s %5s %5s' % ('view', 'switch', 'ms', 'fps', 'ticks', 'frame', 'rend', 'GEwt', 'tps', 'ME%'))
    base = {}
    for r in rows:
        v, vn, sw, sn, ms, fps, ticks, frame, rend, wait, tps, me = r
        if sw == '0':
            base[v] = float(ms)
        delta = ''
        if sw != '0' and v in base:
            delta = '%+5.1f' % (float(ms) - base[v])
        print('  %-26s %-26s %6s %5s %6s %6s %6s %6s %5s %5s %s' % (vn[:26], sn[:26], ms, fps, ticks, frame, rend, wait, tps, me, delta))
