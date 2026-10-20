#!/usr/bin/env python3
"""memcensus.py - the memory census of one or more logs (docs/memory_census.md)

    python3 -I tools/memcensus.py [--tsv] [--map a10] log.txt [log2.txt ...]

Reads the lines the game already writes (and the "memcensus", "poolpeaks" and
"heapmap" keep words' when they were on) and prints, per map, each pool's,
cache's and arena's most against its size, then the bus's traffic by source
for the map's busiest windows. A log that holds several maps (a ladder, a
hand test) is split where each level's load begins; figures before the first
load (the main menu) are filed under "ui".

Each figure is the most any window of that map reached, so two logs of the
same map (a route and a ladder, say) can be given together: the larger wins.
Sources and how far to trust them:
- "poolpeaks" (recon builds): each game-state array's count high-water, caught
  as it grows: exact.
- "game state:" (every build): sampled every heartbeat, so a short burst
  between two can be missed; for a memory pool, before memcensus's fix, it
  printed the pool's use at that moment, not its peak.
- display list: the fullest frame of each 300-frame window, and the draws
  skipped with it full.
- the caches (geometry, textures, sound, the level copies' arena and carve,
  vrammodels, animations): their own 300-frame or heartbeat lines.
- bandwidth: hwprofile's counters (a PSP only), gereads (the GE's bytes by
  where they live), gebusy, the ME's line, io's.
"""
import re
import sys
from collections import OrderedDict, defaultdict

LINE = 64  # the Allegrex's cache line: a miss or a write-back moves 64 bytes


def num(text):
    return float(text.replace(',', ''))


class Section:
    def __init__(self, name):
        self.name = name
        self.pools = OrderedDict()      # name -> [peak, maximum, unit, source]
        self.peak = defaultdict(float)  # metric -> most
        self.size = {}                  # metric -> its size
        self.total = defaultdict(float) # metric -> sum over windows
        self.last = {}                  # cumulative counters: metric -> last value seen
        self.windows = []               # (game_seconds, dict) per heartbeat window, for bandwidth
        self.notes = []
        self.game_time = 0.0

    def most(self, metric, value, size=None):
        if value > self.peak[metric]:
            self.peak[metric] = value
        # a menu after the level (a smaller cache) mustn't shrink the level's size
        if size is not None and size > self.size.get(metric, 0):
            self.size[metric] = size

    def pool(self, name, peak, maximum, unit, source):
        old = self.pools.get(name)
        # poolpeaks is exact; it replaces a heartbeat sample, never the other way round
        if old and old[3] == 'poolpeaks' and source != 'poolpeaks':
            return
        if old and old[3] == source and old[0] >= peak:
            return
        if old and old[3] != source and source == 'poolpeaks':
            peak = max(peak, 0)
        self.pools[name] = [peak, maximum, unit, source]

    def counter(self, metric, value):
        """a counter that grows over the map (and restarts with it): the
        growth since the last line, added to the total"""
        before = self.last.get(metric)
        self.last[metric] = value
        if before is not None and value >= before:
            self.total[metric] += value - before
        elif before is None:
            self.total[metric] += value


def parse(paths, only_map=None):
    sections = OrderedDict()
    list_kb = None
    for path in paths:
        current = sections.setdefault('ui', Section('ui'))
        window = {}
        for raw in open(path, errors='replace'):
            line = raw.rstrip('\n')
            m = re.match(r'loading: levels\\\w+\\(\w+) begins', line)
            if m:
                current = sections.setdefault(m.group(1), Section(m.group(1)))
                current.last = {}
                continue
            s = current
            m = re.match(r'heartbeat: game time (\d+) \(([\d.]+) ticks/s\) \| tick ([\d.]+) ms avg.*?frame ([\d.]+) ms avg.*?([\d.]+) fps', line)
            if m:
                s.game_time = int(m.group(1)) / 30.0
                if window:
                    window['tick_ms'] = num(m.group(3))
                    window['frame_ms'] = num(m.group(4))
                    window['fps'] = num(m.group(5))
                    window['game_speed'] = num(m.group(2))
                    s.windows.append((s.game_time, window))
                window = {}
                m2 = re.search(r'free (\d+) KB \| heap top (\d+) of (\d+) KB', line)
                if m2:
                    s.most('heap_top_kb', num(m2.group(2)), num(m2.group(3)))
                    s.most('user_ram_free_least_kb_neg', -num(m2.group(1)))
                continue
            # ---------- the game state
            if line.startswith('poolpeaks ('):
                for name, maximum, size, peak in re.findall(r' ([^;:]+?) (\d+) x (\d+): peak (\d+), now', line):
                    s.pool(name.strip(), int(peak), int(maximum), 'x%s' % size, 'poolpeaks')
                for name, size, peak, end in re.findall(r' ([^;:]+?) pool (\d+) KB: peak (\d+) KB in use, furthest end (\d+) KB', line):
                    s.pool(name.strip(), int(peak), int(size), 'KB', 'poolpeaks')
                    s.pool(name.strip() + ' (furthest end)', int(end), int(size), 'KB', 'poolpeaks')
                continue
            if line.startswith('game state:'):
                for name, kb, peak, maximum, unit in re.findall(r' ([^;:]+?) (\d+) KB \(peak (\d+) of (\d+)( KB)?\)', line):
                    # the records are never reset: a later map's peak is the most since boot
                    s.pool(name.strip(), int(peak), int(maximum), 'KB' if unit else '%s KB' % kb,
                           'heartbeat, now' if unit else 'heartbeat, since boot')
                m = re.match(r'game state: (\d+) KB allocated of (\d+) KB', line)
                if m:
                    s.most('game_state_kb', num(m.group(1)), num(m.group(2)))
                continue
            # ---------- the heap
            m = re.match(r'heap: (\d+) KB in use, (\d+) KB free inside in (\d+) pieces, (\d+) KB taken from the system; (\d+) requests failed', line)
            if m:
                s.most('heap_in_use_kb', num(m.group(1)))
                s.most('heap_taken_kb', num(m.group(4)))
                s.most('heap_pieces', num(m.group(3)))
                s.counter('heap_failed', num(m.group(5)))
                continue
            m = re.match(r'heapmap \(([^)]*)\): (\d+) KB heap at \w+; (\d+) KB free in (\d+) gaps, the largest (\d+) KB', line)
            if m:
                s.notes.append('heapmap %s: %s KB free in %s gaps, the largest %s KB' % (m.group(1), m.group(3), m.group(4), m.group(5)))
                continue
            m = re.match(r'heap: (?:memalign|malloc|calloc|realloc) of (\d+) bytes FAILED', line)
            if m or ('FAILED' in line and line.startswith('heap:')):
                s.notes.append(line[:160])
                continue
            # ---------- the display list
            m = re.match(r'GE: display list (\d+) KB at', line)
            if m:
                list_kb = num(m.group(1))
                continue
            m = re.match(r'GE: fullest frame (\d+) KB', line)
            if m:
                s.most('display_list_kb', num(m.group(1)), list_kb)
                continue
            m = re.search(r'draws skipped with the display list full (\d+) in all', line)
            if m:
                s.counter('display_list_skips', num(m.group(1)))
                continue
            m = re.match(r'dynamic geometry, frame peaks: (.*)', line)
            if m:
                for kind, used, size in re.findall(r'type (\d+): \d+ of \d+ vertices \((\d+) of (\d+) KB\)', line):
                    s.most('dynamic_type%s_kb' % kind, num(used), num(size))
                m2 = re.search(r'triangles \d+ of \d+ \((\d+) of (\d+) KB\)', line)
                if m2:
                    s.most('dynamic_triangles_kb', num(m2.group(1)), num(m2.group(2)))
                continue
            # ---------- the caches
            m = re.match(r'geometry: (\d+) hits, (\d+) misses \((\d+) frames missed something, (\d+) draws left out\); (\d+) loaded \((\d+) KB\), (\d+) evicted.*?(\d+) blocks resident \((\d+) KB of (\d+)\); reading (\d+) ms in all, worst frame (\d+) ms', line)
            if m:
                v = [num(x) for x in m.groups()]
                s.most('geometry_resident_kb', v[8], v[9])
                for k, name in ((1, 'geometry_misses'), (2, 'geometry_miss_frames'), (3, 'geometry_left_out'), (4, 'geometry_loads'),
                                (5, 'geometry_load_kb'), (6, 'geometry_evicted'), (10, 'geometry_read_ms')):
                    s.counter(name, v[k])
                window['geometry_load_kb'] = window.get('geometry_load_kb', 0)
                s.most('geometry_worst_frame_ms', v[11])
                continue
            m = re.match(r'memcensus: geometry working set, KB touched in the last (.*)', line)
            if m:
                for kb, span in re.findall(r'(\d+) KB in ([\d.]+ s)', line):
                    s.most('geometry_set_%s' % span.replace(' ', ''), num(kb))
                for over, count in re.findall(r'(\d+) KB more: (\d+)', line):
                    pass
                continue
            m = re.match(r'memcensus: geometry re-reads by bytes loaded since the block was last read: (.*)', line)
            if m:
                for label, count in re.findall(r'([<>]=? ?[\d.]+ MB|cold|forgotten): (\d+)', line):
                    s.counter('geometry_reread %s' % label, num(count))
                continue
            m = re.match(r'memcensus: tag window: (.*)', line)
            if m:
                s.notes.append('tag window: ' + m.group(1)[:220])
                mm = re.search(r'free span (\d+) KB', line)
                if mm:
                    s.most('tag_window_free_kb', num(mm.group(1)))
                continue
            m = re.match(r'textures: (\d+) loaded \((\d+) KB\), (\d+) requests waited for the next frame, (\d+) for room; (\d+) evicted.*?(\d+) frames drew something untextured', line)
            if m:
                v = [num(x) for x in m.groups()]
                s.counter('texture_loads', v[0])
                s.counter('texture_load_kb', v[1])
                s.counter('texture_evicted', v[4])
                s.counter('texture_untextured_frames', v[5])
                continue
            m = re.match(r'sound cache: (\d+) KB occupied of (\d+); used in the last second \d+ KB \(peak (\d+) KB\), playing (\d+) KB; (\d+) loads \((\d+) KB.*?(\d+) failed for room', line)
            if m:
                v = [num(x) for x in m.groups()]
                s.most('sound_occupied_kb', v[0], v[1])
                s.most('sound_used_second_kb', v[2])
                s.most('sound_playing_kb', v[3], v[1])
                s.counter('sound_loads', v[4])
                s.counter('sound_load_kb', v[5])
                s.counter('sound_failed', v[6])
                continue
            m = re.match(r"GE: the copies' arena: (\d+) of (\d+) KB used, largest free (\d+) KB; let go for room (\d+); pieces to the heap (\d+) \((\d+) there now\)", line)
            if m:
                v = [num(x) for x in m.groups()]
                s.most('copies_arena_kb', v[0], v[1])
                s.most('copies_on_heap_kb_pieces', v[5])
                s.counter('copies_let_go', v[3])
                s.counter('copies_to_heap', v[4])
                continue
            m = re.match(r'GE: the level carve: (\d+) of (\d+) KB taken.*?peak (\d+) KB', line)
            if m:
                s.most('level_carve_kb', num(m.group(3)), num(m.group(2)))
                continue
            m = re.match(r'GE: vrammodels: (\d+) buffers in (\d+) of (\d+) KB of VRAM; draws from a copy (\d+) of (\d+).*?(\d+) times full', line)
            if m:
                s.most('vram_models_kb', num(m.group(2)), num(m.group(3)))
                s.counter('vram_models_full', num(m.group(6)))
                continue
            m = re.match(r'memcensus: VRAM: (.*)', line)
            if m:
                for name, used, size in re.findall(r'([a-z ]+?) (\d+) of (\d+) KB', m.group(1)):
                    s.most('vram_%s_kb' % name.strip().replace(' ', '_'), num(used), num(size))
                continue
            m = re.match(r'animations: (\d+) lookups .*?(\d+) hits, (\d+) misses, (\d+) reads \((\d+) KB\) in (\d+) ms', line)
            if m:
                s.counter('animation_misses', num(m.group(3)))
                s.counter('animation_read_kb', num(m.group(5)))
                continue
            m = re.match(r'animations: .*?cache (\d+) KB in pages of \d+ KB: (\d+) KB at \w+ \(the tag window', line)
            if m:
                s.most('animation_cache_kb', num(m.group(1)))
                s.most('animation_in_window_kb', num(m.group(2)))
                continue
            m = re.match(r'tags: levels\\\w+\\\w+, (\d+) bytes', line)
            if m:
                s.most('tags_kb', num(m.group(1)) / 1024)
                continue
            # ---------- bandwidth: hwprofile, gereads, gebusy, the ME, io
            m = re.match(r'hardware: (\d+) M CPU cycles.*?waiting on memory ([\d.]+)%.*?D-cache misses (\d+), writebacks (\d+), I-cache misses (\d+), uncached loads (\d+) stores (\d+), cached loads (\d+) stores (\d+), bus (\d+), FPU ops \d+, VFPU ops \d+, local bus (\d+)', line)
            if m:
                v = [num(x) for x in m.groups()]
                window.update(cycles_m=v[0], memwait=v[1], dmiss=v[2], wb=v[3], imiss=v[4], uload=v[5], ustore=v[6],
                              bus=v[9], localbus=v[10])
                continue
            m = re.match(r'hardware by phase.*?: (.*)', line)
            if m:
                for phase, share, mem in re.findall(r' (other|tick|drawing|frame end) ([\d.]+)% \(memory ([\d.]+)%', line):
                    window['phase_%s' % phase.replace(' ', '_')] = (num(share), num(mem))
                continue
            m = re.match(r'GE: what the draws made the GE read, KB a frame, by where it lives: (.*)', line)
            if m:
                for place, kb in re.findall(r'([A-Za-z\' ()]+?) ([\d.]+)(?:;|$| \()', m.group(1)):
                    window['gereads %s' % place.strip()] = num(kb)
                continue
            m = re.match(r'frameend: \d+ frames; a frame: .*?waiting for the GE ([\d.]+)', line)
            if m:
                window['ge_wait_ms'] = num(m.group(1))
                mm = re.search(r'GE busy ([\d.]+) ms', line)
                if mm:
                    window['ge_busy_ms'] = num(mm.group(1))
                continue
            m = re.match(r'ME: running, ([\d.]+)% busy', line)
            if m:
                window['me_busy'] = num(m.group(1))
                continue
            m = re.match(r'io: textures (\d+) reads, ([\d.]+) ms.*?geometry (\d+) reads, ([\d.]+) ms.*?animations (\d+) reads, ([\d.]+) ms.*?over ([\d.]+) s', line)
            if m:
                v = [num(x) for x in m.groups()]
                window['io_ms_per_s'] = (v[1] + v[3] + v[5]) / v[6] if v[6] else 0
                continue
            m = re.match(r'mebus: work (-?\d+) \| .*?the main CPU ([+-][\d.]+)%.*?alone: ([+-][\d.]+)%', line)
            if m:
                s.notes.append('mebus %s: the main CPU %s%%, the ME %s%%' % m.groups())
                continue
            m = re.match(r'bus probe \(([^)]*)\): (.*)', line)
            if m:
                s.notes.append('bus probe (%s): %s' % (m.group(1), m.group(2)[:200]))
                continue
    if only_map:
        sections = OrderedDict((k, v) for k, v in sections.items() if k == only_map)
    return sections


CACHES = [
    # metric, label, how it's read
    ('display_list_kb', 'display list (fullest frame)', 'KB'),
    ('heap_top_kb', 'heap (top)', 'KB'),
    ('heap_in_use_kb', 'heap (in use)', 'KB'),
    ('game_state_kb', 'game state (allocated)', 'KB'),
    ('geometry_resident_kb', 'geometry cache (resident)', 'KB'),
    ('sound_occupied_kb', 'sound cache (occupied)', 'KB'),
    ('sound_playing_kb', 'sound cache (playing)', 'KB'),
    ('copies_arena_kb', "level copies' arena", 'KB'),
    ('level_carve_kb', 'level carve', 'KB'),
    ('vram_models_kb', "VRAM: the models' share", 'KB'),
    ('vram_textures_kb', "VRAM: the textures' share", 'KB'),
    ('animation_cache_kb', 'animation cache', 'KB'),
    ('animation_in_window_kb', '  of it in the tag window', 'KB'),
    ('tags_kb', 'tag data (the map file)', 'KB'),
    ('tag_window_free_kb', 'tag window free span', 'KB'),
    ('dynamic_triangles_kb', 'dynamic geometry: triangles', 'KB'),
    ('dynamic_type6_kb', 'dynamic geometry: type 6', 'KB'),
]
CHURN = ['display_list_skips', 'heap_failed', 'geometry_misses', 'geometry_miss_frames', 'geometry_left_out', 'geometry_loads',
         'geometry_load_kb', 'geometry_evicted', 'geometry_read_ms', 'texture_loads', 'texture_load_kb', 'texture_evicted',
         'texture_untextured_frames', 'sound_loads', 'sound_load_kb', 'sound_failed', 'copies_let_go', 'copies_to_heap',
         'animation_misses', 'animation_read_kb', 'vram_models_full']


def bandwidth(s):
    """the map's busiest windows by memory wait, with the bus's traffic by
    source in MB a second: the CPU's misses and write-backs (64 bytes each),
    its uncached accesses (4 bytes; an over-count for byte stores), the GE's
    reads by where they live (gereads, KB a frame times fps) and the stick's
    reads (io)"""
    rows = []
    for t, w in s.windows:
        if 'memwait' not in w and not any(k.startswith('gereads') for k in w):
            continue
        cycles_s = 333e6
        row = OrderedDict(t=t, fps=w.get('fps'), frame=w.get('frame_ms'), tick=w.get('tick_ms'))
        if 'memwait' in w:
            per_s = cycles_s / 1e6
            row['memwait'] = w['memwait']
            row['cpu_miss_MBs'] = (w['dmiss'] + w['imiss']) * per_s * LINE / 1e6
            row['cpu_wb_MBs'] = w['wb'] * per_s * LINE / 1e6
            row['cpu_uncached_MBs'] = (w['uload'] + w['ustore']) * per_s * 4 / 1e6
            row['imiss_share'] = w['imiss'] / (w['imiss'] + w['dmiss']) if w['imiss'] + w['dmiss'] else 0
            row['bus'] = w['bus']
            row['localbus'] = w['localbus']
            for phase in ('tick', 'drawing'):
                if 'phase_' + phase in w:
                    row['%s_mem' % phase] = w['phase_' + phase][1]
        ge = sum(v for k, v in w.items() if k.startswith('gereads') and 'VRAM' not in k)
        if ge and w.get('fps'):
            row['ge_ram_MBs'] = ge * w['fps'] / 1024
        if 'me_busy' in w:
            row['me_busy'] = w['me_busy']
        if 'io_ms_per_s' in w:
            row['io_ms_s'] = w['io_ms_per_s']
        rows.append(row)
    return rows


def report(sections, tsv=False):
    out = []
    for name, s in sections.items():
        if not s.pools and not s.peak and not s.windows:
            continue
        out.append('## %s\n' % name)
        if s.pools:
            out.append('| Array or pool | Peak | Size | Use | Source |\n|---|---:|---:|---:|---|')
            for pool, (peak, maximum, unit, source) in s.pools.items():
                use = 100.0 * peak / maximum if maximum else 0
                out.append('| %s | %d | %d %s | %.0f%% | %s |' % (pool, peak, maximum, unit, use, source))
            out.append('')
        out.append('| Cache or arena | Most | Size | Use |\n|---|---:|---:|---:|')
        for metric, label, unit in CACHES:
            if metric in s.peak:
                size = s.size.get(metric)
                use = '%.0f%%' % (100.0 * s.peak[metric] / size) if size else ''
                out.append('| %s | %.0f %s | %s | %s |' % (label, s.peak[metric], unit, '%.0f %s' % (size, unit) if size else '', use))
        out.append('')
        churn = [(k, s.total[k]) for k in CHURN if s.total.get(k)]
        churn += sorted((k, v) for k, v in s.total.items() if k.startswith('geometry_reread'))
        if churn:
            out.append('Over the map: ' + '; '.join('%s %.0f' % kv for kv in churn) + '\n')
        sets = sorted((k, v) for k, v in s.peak.items() if k.startswith('geometry_set_'))
        if sets:
            out.append('Geometry working set (most): ' + '; '.join('%s %.0f KB' % (k[13:], v) for k, v in sets) + '\n')
        rows = bandwidth(s)
        if rows:
            rows.sort(key=lambda r: -(r.get('memwait') or r.get('ge_ram_MBs') or 0))
            keys = [k for k in rows[0].keys()]
            out.append('Busiest windows (by memory wait):\n')
            out.append('| ' + ' | '.join(keys) + ' |\n|' + '---:|' * len(keys))
            for r in rows[:5]:
                out.append('| ' + ' | '.join(('%.1f' % r[k]) if isinstance(r.get(k), float) else str(r.get(k, '')) for k in keys) + ' |')
            out.append('')
        for note in s.notes[:12]:
            out.append('- ' + note)
        out.append('')
    return '\n'.join(out)


def main(argv):
    tsv = '--tsv' in argv
    only = None
    args = [a for a in argv if a != '--tsv']
    if '--map' in args:
        k = args.index('--map')
        only = args[k + 1]
        del args[k:k + 2]
    if not args:
        print(__doc__)
        return 2
    print(report(parse(args, only), tsv))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
