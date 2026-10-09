#!/usr/bin/env python3
"""anim_cache_sim.py - replay an animation trace against animation cache layouts.

The game's animation cache (psp/src/psp_anim.c) reads animation frame data
from the map's geometry pack in pages. How well it does depends on the cache
size, the page size and how the animations are laid out in the pack. A run
with "animtrace" in keep.txt writes log/animtrace.bin: every distinct range
of frame data the game asked for, frame by frame. This replays that trace
against other choices, with the cache's own algorithm, and estimates the
Memory Stick time on a PSP-1000.

usage: python3 tools/anim_cache_sim.py animtrace.bin MAP.map [--cache 256,384,512,768]
           [--page 4,8,16] [--layout built,paged]

  built   the animations where the map has them (one after another)
  paged   each animation moved, when needed, so it doesn't cross a page
          boundary (one no larger than a page then fits a single page)

Read cost model (halo_bench v10, Sheepdog01's PSP-1000): a random read costs about
1.65 ms plus its bytes at 16 MB/s (4 KB 2.1 ms, 16 KB 2.8 ms, 64 KB 6 ms)."""
import argparse, bisect, os, struct, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'bake', 'lib'))  # the bake's readers (halomap.py, ...)

RECENT_RANGES = 6
EXTERNAL_BASE = 0x0C000000


def read_cost_ms(size):
    return 1.65 + size / 16000.0


def load_trace(path):
    words = struct.unpack('<%dI' % (os.path.getsize(path) // 4), open(path, 'rb').read())
    frames, current, i = [], None, 0
    while i < len(words):
        if words[i] == 0xFFFFFFFF:
            if current is not None:
                frames.append(current)
            current = []
            i += 2
            continue
        if current is None:
            current = []
        current.append((words[i], words[i + 1]))
        i += 2
    if current:
        frames.append(current)
    return frames


def load_blobs(map_path):
    """(address, size) of every animation's frame data, in pack order"""
    from halomap import Defs, CacheMap
    d = Defs()
    m = CacheMap(map_path)
    fields = {f.get('name'): x for x, f, sz in d.fields('ModelAnimationsAnimation')}
    element = d.structs['ModelAnimationsAnimation']['size']
    blobs = []
    for t in m.tags:
        if t.group != 'antr' or not t.address:
            continue
        count, pointer = struct.unpack_from('<iI', m.data, m.off(t.address) + 0x74)
        for index in range(count):
            at = m.off(pointer) + element * index
            size, _, _, address = struct.unpack_from('<iIiI', m.data, at + fields['frame data'])
            if size and address >= EXTERNAL_BASE:
                blobs.append((address, size))
    return sorted(set(blobs))


def layout(blobs, page, mode):
    """old address -> new address of each blob's start"""
    moved, cursor = {}, 0
    for address, size in blobs:
        if mode == 'paged' and size <= page and cursor % page + size > page:
            cursor = (cursor + page - 1) // page * page
        moved[address] = EXTERNAL_BASE + cursor
        cursor = (cursor + size + 15) // 16 * 16
    return moved, cursor


class Cache:
    """psp_anim.c's slots: page-sized, LRU by lookup, the last ranges protected"""

    def __init__(self, cache_bytes, page, data_bytes, extend):
        self.page = page
        self.slots = cache_bytes // page
        self.slot_page = [-1] * self.slots
        self.slot_used = [0] * self.slots
        self.page_slot = {}
        self.recent = []
        self.stamp = 0
        self.extend = extend
        self.misses = self.reads = self.read_bytes = 0
        self.read_ms = 0.0

    def protected(self, slot):
        return any(first <= slot < first + count for first, count in self.recent)

    def remember(self, first, count):
        if self.recent and self.recent[0] == (first, count):
            return
        self.recent.insert(0, (first, count))
        del self.recent[RECENT_RANGES:]

    def choose(self, count):
        best, best_age = -1, None
        for start in range(0, self.slots - count + 1):
            window = range(start, start + count)
            if any(self.protected(s) for s in window):
                continue
            newest = max(self.slot_used[s] for s in window)
            if best_age is None or newest < best_age:
                best, best_age = start, newest
                if newest == 0:
                    break
        return best

    def load(self, first_page, count, slot):
        for k in range(count):
            old = self.slot_page[slot + k]
            if old >= 0 and self.page_slot.get(old) == slot + k:
                del self.page_slot[old]
            self.slot_page[slot + k] = -1
            dup = self.page_slot.get(first_page + k)
            if dup is not None:
                self.slot_page[dup] = -1
                del self.page_slot[first_page + k]
        for k in range(count):
            self.slot_page[slot + k] = first_page + k
            self.page_slot[first_page + k] = slot + k

    def lookup(self, offset, size):
        """the read cost in ms of this lookup (0 for a hit)"""
        first_page = offset // self.page
        count = (offset + size - 1) // self.page - first_page + 1
        self.stamp += 1
        slot = self.page_slot.get(first_page, -1)
        for k in range(1, count):
            if slot >= 0 and self.page_slot.get(first_page + k) != slot + k:
                slot = -1
        cost = 0.0
        if slot < 0:
            self.misses += 1
            done = False
            if self.extend:
                # the pages that are there, in place, with the rest read beside them
                anchor = self.page_slot.get(first_page)
                if anchor is not None and anchor + count <= self.slots:
                    window = range(anchor, anchor + count)
                    if all(self.slot_page[s] == first_page + (s - anchor) or not self.protected(s) for s in window):
                        missing = [k for k in range(count) if self.slot_page[anchor + k] != first_page + k]
                        for k in missing:
                            self.load(first_page + k, 1, anchor + k)
                        self.reads += 1
                        self.read_bytes += len(missing) * self.page
                        cost = read_cost_ms(len(missing) * self.page)
                        slot, done = anchor, True
            if not done:
                slot = self.choose(count)
                if slot < 0:
                    raise SystemExit('no room: the cache is too small for the ranges in use')
                self.load(first_page, count, slot)
                self.reads += 1
                self.read_bytes += count * self.page
                cost = read_cost_ms(count * self.page)
            self.read_ms += cost
        for k in range(count):
            self.slot_used[slot + k] = self.stamp
        self.remember(slot, count)
        return cost


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('trace')
    ap.add_argument('map')
    ap.add_argument('--cache', default='256,384,512,768,1024')
    ap.add_argument('--page', default='4,8,16')
    ap.add_argument('--layout', default='built,paged')
    ap.add_argument('--extend', action='store_true', help='fill a range\'s missing pages beside the ones there')
    a = ap.parse_args()
    frames = load_trace(a.trace)
    blobs = load_blobs(a.map)
    starts = [b[0] for b in blobs]
    lookups = sum(len(f) for f in frames)
    print('%d frames, %d ranges asked for, %d animations, %d KB of frame data' %
          (len(frames), lookups, len(blobs), sum(b[1] for b in blobs) // 1024))
    print('%-6s %5s %6s %8s %7s %9s %9s %8s %8s' % ('layout', 'page', 'cache', 'misses', 'MB read', 'ms total',
                                                 'worst ms', '>10 ms', '>33 ms'))
    for mode in a.layout.split(','):
        for page_kb in [int(x) for x in a.page.split(',')]:
            page = page_kb * 1024
            moved, end = layout(blobs, page, mode)
            for cache_kb in [int(x) for x in a.cache.split(',')]:
                cache = Cache(cache_kb * 1024, page, end, a.extend)
                worst, over10, over33 = 0.0, 0, 0
                for frame in frames:
                    spent = 0.0
                    for address, size in frame:
                        i = bisect.bisect_right(starts, address) - 1
                        if i < 0 or address >= starts[i] + blobs[i][1]:
                            continue
                        new = moved[starts[i]] + (address - starts[i]) - EXTERNAL_BASE
                        spent += cache.lookup(new, size)
                    worst = max(worst, spent)
                    over10 += spent > 10
                    over33 += spent > 33
                print('%-6s %5d %6d %8d %7.1f %9.0f %9.1f %8d %8d' % (mode, page_kb, cache_kb, cache.misses,
                      cache.read_bytes / 1e6, cache.read_ms, worst, over10, over33))


if __name__ == '__main__':
    main()
