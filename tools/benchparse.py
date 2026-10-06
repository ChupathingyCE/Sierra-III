"""benchparse.py - the table from a "benchrun" log (build 87): the
"bench: RESULT" lines (one per view and switch, the measured window).
usage: python3 benchparse.py log.txt [more logs...]"""
import re, sys, collections

pat = re.compile(r'bench: RESULT view (\d+) \((.*?)\) \| switch (\d+) \((.*?)\) \| ([\d.]+) ms a frame, ([\d.]+) fps \| '
                 r'ticks ([\d.]+) ms \| frame ([\d.]+) ms \(renderer CPU ([\d.]+), GE wait ([\d.]+)\) \| ([\d.]+) ticks/s \| ME ([\d.]+)% busy')
for path in sys.argv[1:]:
    rows = [m.groups() for m in (pat.search(l) for l in open(path, errors='replace')) if m]
    print('%s: %d results' % (path, len(rows)))
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
