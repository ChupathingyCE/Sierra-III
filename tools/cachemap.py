#!/usr/bin/env python3
"""cachemap.py - who misses the caches, by name (2026-10-09).

Reads the emulator's cache-model dump (PSP_DCACHE=1, tools/ppsspp_local_patches.diff:
16 KB 2-way caches with 64-byte lines, data and instructions, counted per
64-byte line and per phase: 0 other, 1 tick, 2 drawing, 3 frame end) and
names every line: the program's functions and variables (from the ELF), the
game state's allocations ("game state layout:" lines in the run's log), the
tags in the tag window (from the map the run loaded), and the memory regions
around them (heap, volatile memory, stacks).

    python3 cachemap.py DCACHE.bin halo.elf log.txt [MAP.map] [--top N] [--lines N]

--lines N: the hottest lines of the N functions with the most fetches in the
tick and the drawing, disassembled (file:line too when built with
PSP_CFLAGS=-g1, which leaves the code as it is)

What it prints, per phase:
  - data misses by region and by name, with each name's size and misses per
    KB (small and hot = a scratchpad candidate: 16 KB, always hits, off the bus)
  - data misses by the function whose load or store missed (DCM3 dumps)
  - instruction fetches and misses by function, and how much code makes up
    half and nine tenths of the fetches (the hot path against the 16 KB cache)

The model sees what the game's own code loads and stores; memory touched by
emulated system calls and function replacements (memcpy, memset) is not in
it. Misses are counts, not time: on the PSP-1000 a miss costs ~216 ns, ~424
while the GE reads main RAM (psp_hardware_reference.md)."""
import bisect
import os
import re
import struct
import subprocess
import sys
from collections import defaultdict

LO, HI = 0x08000000, 0x0A000000
LINES = (HI - LO) >> 6
PROGRAM_BASE = 0x08804000
PHASE_NAMES = ['other', 'tick', 'drawing', 'frame end']
TAG_WINDOW = (0x09600000, 0x09F00000)
# the campaign build's; the multiplayer build's starts at 0x09340000, a
# PSP_POOLS_XBOX=1 build's (and one from before the campaign's pools were cut)
# at 0x092A0000, one from R4 to the memory census at 0x092E4000, one from the
# census to B92 at 0x09304000 (psp_capacity.h)
GAME_STATE = (0x09300000, 0x09600000)


def load_dump(path):
    data = open(path, 'rb').read()
    magic, phases = struct.unpack_from('<QQ', data, 0)
    if magic not in (0x44434D32, 0x44434D33):
        sys.exit('%s: not a DCM2 or DCM3 dump' % path)
    array_count = 5 if magic == 0x44434D33 else 4
    header = struct.unpack_from('<%dQ' % (2 + 7 * phases), data, 0)
    totals = {name: header[2 + k * phases: 2 + (k + 1) * phases] for k, name in
              enumerate(['accesses', 'misses', 'uncached', 'scratchpad', 'stores', 'fetches', 'imisses'])}
    at = 8 * (2 + 7 * phases)
    arrays = []
    for _ in range(array_count):
        arrays.append([memoryview(data)[at + p * LINES * 4: at + (p + 1) * LINES * 4].cast('I') for p in range(phases)])
        at += phases * LINES * 4
    if array_count == 4:
        arrays.append(None)
    return phases, totals, arrays  # arrays: accesses, misses, ifetches, imisses, data misses by the missing instruction's line


def elf_symbols(elf):
    """(start, end, name, kind) for the program's sized symbols, at their
    loaded addresses"""
    nm = os.environ.get('PSP_NM', '/opt/psp/pspdev/bin/psp-nm')
    out = subprocess.run([nm, '-n', '-S', elf], capture_output=True, text=True).stdout
    symbols = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 4:
            continue
        address, size, kind, name = parts
        start = int(address, 16) + PROGRAM_BASE
        symbols.append((start, start + int(size, 16), name, kind.lower()))
    return symbols


def game_state_layout(log):
    spans = []
    for line in open(log, errors='replace'):
        m = re.search(r'game state layout: ([0-9a-f]{8}) (\d+) (.*)$', line)
        if m:
            start = int(m.group(1), 16)
            spans.append((start, start + int(m.group(2)), 'game state: ' + m.group(3).strip(), 'g'))
    return spans


def scratch_spans(log):
    """the spans psp_scratch.c names ("scratch span:" lines): the scratchpad's
    pieces when they come from the heap (noscratch), and the level's collision
    BSP array by array ("scratchcensus")"""
    spans = []
    for line in open(log, errors='replace'):
        m = re.search(r'scratch span: ([0-9a-f]{8}) (\d+) (.*)$', line)
        if m:
            start = int(m.group(1), 16)
            spans.append((start, start + int(m.group(2)), 'span: ' + m.group(3).strip(), 's'))
    return spans


def scratch_topsets(log):
    """the level BSP's top levels ("scratch topset: <depth> <n|p> <lines>"):
    for each depth, the node and plane lines of every node down to it, the
    BSPs of one run together"""
    sets = defaultdict(set)
    for line in open(log, errors='replace'):
        m = re.search(r'scratch topset: (\d+) ([np]) ([0-9a-f,]+)', line)
        if m:
            sets[(int(m.group(1)), m.group(2))].update(int(a, 16) for a in m.group(3).split(','))
    return sets


def hottest_curve(misses_p, start, end, budgets=(1024, 2048, 4096, 8192, 16384)):
    """the misses the hottest lines of one span hold, for each budget of bytes:
    what that span would hand the scratchpad if its best lines were there"""
    lines = [misses_p[(a - LO) >> 6] for a in range(start & ~63, end, 64) if LO <= a < HI]
    counts = sorted(lines, reverse=True)
    total = sum(counts)
    return total, [(b, sum(counts[:b // 64])) for b in budgets], [(b, sum(lines[:b // 64])) for b in budgets]


def tag_spans(map_path):
    """the tags' data in the tag window, from an Xbox cache file (the baked,
    decompressed map): each tag runs to the next tag's start"""
    data = open(map_path, 'rb').read()
    if data[:4] not in (b'daeh', b'head'):
        return []
    tag_data_offset, tag_data_size = struct.unpack_from('<II', data, 0x10)
    base = TAG_WINDOW[0]
    array_address, _, _, count = struct.unpack_from('<IIII', data, tag_data_offset)

    def at(address):
        return tag_data_offset + (address - base)

    tags = []
    for i in range(count):
        entry = at(array_address) + 32 * i
        primary = data[entry:entry + 4][::-1].decode('latin-1')
        path_address, data_address = struct.unpack_from('<II', data, entry + 16)
        if not (base <= data_address < base + tag_data_size):
            continue
        p = at(path_address)
        path = data[p:data.index(b'\0', p)].decode('latin-1') if base <= path_address < base + tag_data_size else '?'
        tags.append((data_address, '%s.%s' % (path, primary.strip())))
    tags.sort()
    spans = []
    for i, (start, name) in enumerate(tags):
        end = tags[i + 1][0] if i + 1 < len(tags) else base + tag_data_size
        spans.append((start, end, 'tag: ' + name, 't'))
    spans.append((base, tags[0][0] if tags else base, 'tag: (the tag array and header)', 't'))
    spans.append((base + tag_data_size, TAG_WINDOW[1], 'tag window: BSP and free space', 't'))
    return spans


def region(address, program_end):
    if PROGRAM_BASE <= address < program_end:
        return 'program'
    if 0x08400000 <= address < 0x08800000:
        return 'volatile memory (texture, geometry, animation caches)'
    if program_end <= address < GAME_STATE[0]:
        return 'heap and early allocations'
    if GAME_STATE[0] <= address < GAME_STATE[1]:
        return 'game state'
    if TAG_WINDOW[0] <= address < TAG_WINDOW[1]:
        return 'tag window'
    if TAG_WINDOW[1] <= address < HI:
        return 'thread stacks'
    return 'other'


class Namer:
    def __init__(self, spans):
        self.spans = sorted(spans)
        self.starts = [s[0] for s in self.spans]

    def name(self, address):
        i = bisect.bisect_right(self.starts, address) - 1
        while i >= 0:
            start, end, name, kind = self.spans[i]
            if start <= address < end:
                return name, end - start
            if start < address - 0x100000:
                break
            i -= 1
        return None, 0


def hot_lines(p, ifetches, imisses, dmiss_pc, code_namer, elf, names, total, per_function=12):
    """the hottest 64-byte lines of each named function, each with its
    fetches, its instruction misses, the data misses of its loads and stores
    (DCM3 dumps) and its 16 instructions (psp-objdump beside psp-nm). On the
    PSP-1000 a miss costs ~216 ns, so a line's misses can outweigh its
    fetches"""
    objdump = os.environ.get('PSP_NM', '/opt/psp/pspdev/bin/psp-nm')
    objdump = objdump[:-2] + 'objdump' if objdump.endswith('nm') else 'psp-objdump'
    for want in names:
        lines = []
        for line in range(LINES):
            f = ifetches[p][line]
            if f and code_namer.name(LO + (line << 6))[0] == want:
                lines.append((f, imisses[p][line], dmiss_pc[p][line] if dmiss_pc is not None else 0, LO + (line << 6)))
        if not lines:
            continue
        lines.sort(reverse=True)
        print('\nhot lines of %s (%s):' % (want, PHASE_NAMES[p]))
        for f, m, dm, address in lines[:per_function]:
            print('  -- %08x  fetches %d (%.2f%% of the phase), instruction misses %d, data misses %d' % (
                address, f, 100.0 * f / total, m, dm))
            start = address - PROGRAM_BASE
            out = subprocess.run([objdump, '-d', '-l', '--no-show-raw-insn', '--start-address=0x%x' % start,
                                  '--stop-address=0x%x' % (start + 64), elf], capture_output=True, text=True).stdout
            for text in out.splitlines():
                text = text.rstrip()
                if not text or text.startswith(('Disassembly', elf)) or 'file format' in text:
                    continue
                print('     ' + text)


def spad_lines(dump):
    """the anatomy's per-line file beside the dump (emu.sh's ANATOMY=1: the
    tick phase's fills, words used and ticks touched a line), or None"""
    path = os.path.join(os.path.dirname(os.path.abspath(dump)), 'dcanat_raw.txt.lines')
    if not os.path.exists(path):
        return None
    data = open(path, 'rb').read()
    magic, lines, ticks = struct.unpack_from('<QQQ', data, 0)
    if magic != 0x314C414344 or lines != LINES:
        return None
    arrays = [memoryview(data)[24 + k * lines * 4: 24 + (k + 1) * lines * 4].cast('I') for k in range(3)]
    return ticks, arrays  # fills, words, ticks touched


def spad_sets(phases, totals, accesses, misses, data_namer, program_end, log, dump):
    """spadphase's sets (a PSP_SPADPHASE build, docs/spad_staging.md): in the
    tick and drawing phases, every data set by name with its lines touched,
    accesses, misses and what its hottest 1/2/4/8 KB hold, then the phase's
    hottest lines, for the lab's spadstage.py. Tags by group only. With the
    anatomy's file (tick phase only) each set's line-ticks, fills and words
    used too: under spadphase its 'ticks' are the counted part's entries"""
    part = 'none'
    for line in open(log, errors='replace'):
        m = re.search(r'spadphase: (\w+) \(the cache model', line)
        if m:
            part = m.group(1)
    anat = spad_lines(dump)
    print('\nspadsets: part %s (the tick column is %s); anatomy %s' % (
        part, 'that part alone' if part not in ('none', 'drawing') else
        'the drawing, the tick counted as other' if part == 'drawing' else 'the whole tick',
        'entries %d' % anat[0] if anat else 'absent'))

    def key_of(address):
        name, size = data_namer.name(address)
        if name and name.startswith('tag: '):
            group = name.rsplit('.', 1)[-1] if '.' in name else '?'
            return 'tags: ' + group, 0
        if name:
            return name, size
        return '(%s, unnamed: %06x)' % (region(address, program_end), (address >> 16) << 16), 65536

    for p in (1, 2):
        if p >= phases or not totals['accesses'][p]:
            continue
        sets = {}  # name: lines, accesses, misses, (accesses, misses) a line, size, line-ticks, fills, words
        hot = []
        for line in range(LINES):
            a = accesses[p][line]
            if not a:
                continue
            m = misses[p][line]
            address = LO + (line << 6)
            key, size = key_of(address)
            row = sets.setdefault(key, [0, 0, 0, [], size, 0, 0, 0])
            row[0] += 1
            row[1] += a
            row[2] += m
            row[3].append((a, m))
            seg = 0
            if anat and p == 1:
                seg = anat[1][2][line]
                row[5] += seg
                row[6] += anat[1][0][line]
                row[7] += anat[1][1][line]
            hot.append((m, a, seg, address, key))
        ta, tm = totals['accesses'][p], max(totals['misses'][p], 1)
        print('spadsets %s: accesses %d misses %d scratchpad %d uncached %d' % (
            PHASE_NAMES[p], ta, totals['misses'][p], totals['scratchpad'][p], totals['uncached'][p]))
        print('# dset phase lines accesses misses size acc_hot1K acc_hot2K acc_hot4K acc_hot8K mis_hot1K mis_hot2K mis_hot4K mis_hot8K '
              'linesegs fills words name (hot by accesses, then by misses; linesegs: the line-ticks of the anatomy)')
        for key, (n, a, m, per, size, seg, fills, words) in sorted(sets.items(), key=lambda kv: -kv[1][2]):
            if a * 2000 < ta and m * 2000 < tm:
                continue
            by_a = sorted((x[0] for x in per), reverse=True)
            by_m = sorted((x[1] for x in per), reverse=True)
            print('dset %s %d %d %d %d %s %s %d %d %d %s' % (
                PHASE_NAMES[p].replace(' ', '_'), n, a, m, size,
                ' '.join(str(sum(by_a[:b // 64])) for b in (1024, 2048, 4096, 8192)),
                ' '.join(str(sum(by_m[:b // 64])) for b in (1024, 2048, 4096, 8192)),
                seg, fills, words, key))
        # the phase's hottest lines, by misses and by accesses (512 each)
        chosen = {h[3]: h for h in sorted(hot, reverse=True)[:512]}
        for h in sorted(hot, key=lambda h: -h[1])[:512]:
            chosen.setdefault(h[3], h)
        print('# hot phase address accesses misses linesegs name')
        for m, a, seg, address, key in sorted(chosen.values(), reverse=True):
            print('hot %s %08x %d %d %d %s' % (PHASE_NAMES[p].replace(' ', '_'), address, a, m, seg, key))


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    dump, elf, log = sys.argv[1:4]
    map_path = sys.argv[4] if len(sys.argv) > 4 and not sys.argv[4].startswith('--') else None
    top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 25
    phases, totals, (accesses, misses, ifetches, imisses, dmiss_pc) = load_dump(dump)
    symbols = elf_symbols(elf)
    program_end = max(end for start, end, name, kind in symbols)
    spans_named = scratch_spans(log)
    topsets = scratch_topsets(log)
    data_spans = [s for s in symbols if s[3] in 'bdgsrv' or s[3] == 'b'] + game_state_layout(log) + spans_named
    if map_path:
        data_spans += tag_spans(map_path)
    data_namer = Namer(data_spans)
    code_namer = Namer([s for s in symbols if s[3] == 't'])

    print('totals by phase (the model: 16 KB 2-way, 64-byte lines):')
    for p in range(phases):
        a, m = totals['accesses'][p], totals['misses'][p]
        f, im = totals['fetches'][p], totals['imisses'][p]
        if not (a or f):
            continue
        print('  %-9s data: %11d cached accesses, %9d misses (%.2f%%); %d uncached, %d scratchpad | '
              'instructions: %11d fetches, %9d misses (%.2f%%)' % (
                  PHASE_NAMES[p], a, m, 100.0 * m / max(a, 1), totals['uncached'][p], totals['scratchpad'][p],
                  f, im, 100.0 * im / max(f, 1)))

    for p in range(phases):
        if not totals['misses'][p] and not totals['imisses'][p]:
            continue
        print('\n== %s' % PHASE_NAMES[p])
        by_region = defaultdict(lambda: [0, 0])
        by_name = defaultdict(lambda: [0, 0, 0])
        for line in range(LINES):
            a = accesses[p][line]
            if not a:
                continue
            m = misses[p][line]
            address = LO + (line << 6)
            r = region(address, program_end)
            by_region[r][0] += a
            by_region[r][1] += m
            name, size = data_namer.name(address)
            key = name or '(%s, unnamed: %06x)' % (r, (address >> 16) << 16)
            by_name[key][0] += a
            by_name[key][1] += m
            by_name[key][2] = size or 65536
        total_misses = sum(v[1] for v in by_region.values()) or 1
        print('data misses by region:')
        for r, (a, m) in sorted(by_region.items(), key=lambda x: -x[1][1]):
            print('  %9d  %5.1f%%  of %10d accesses  %s' % (m, 100.0 * m / total_misses, a, r))
        print('data misses by name (size, misses per KB):')
        for key, (a, m, size) in sorted(by_name.items(), key=lambda x: -x[1][1])[:top]:
            print('  %9d  %5.1f%%  %8d B  %8.0f/KB  %s' % (m, 100.0 * m / total_misses, size, m * 1024.0 / max(size, 1), key))
        # the scratchpad census: each named span's hottest lines
        if spans_named:
            print('scratchpad census, each named span: misses held by its hottest 1/2/4/8/16 KB of lines | by its first 1/2/4/8/16 KB:')
            ceilings = [(TAG_WINDOW[0], TAG_WINDOW[1], 'ceiling: the whole tag window (lines need not sit together)', 'r'),
                        (GAME_STATE[0], GAME_STATE[1], 'ceiling: the whole game state', 'r'),
                        (LO, HI, 'ceiling: all of main RAM', 'r')]
            for start, end, name, kind in sorted(spans_named + ceilings, key=lambda s: -hottest_curve(misses[p], s[0], s[1])[0]):
                total, curve, prefix = hottest_curve(misses[p], start, end)
                if total:
                    print('  %9d  %8d B at %08x  %s | hottest %s | first %s' % (total, end - start, start, name,
                          ' '.join('%dK:%d' % (b // 1024, m) for b, m in curve),
                          ' '.join('%dK:%d' % (b // 1024, m) for b, m in prefix)))
        if spans_named:
            # the hottest 64-byte lines of all of main RAM, each with every
            # symbol or span that overlaps it (a line of small globals is
            # named by all of them, not only by the one at its start)
            hot = sorted(((misses[p][line], line) for line in range(LINES) if misses[p][line]), reverse=True)[:512]
            overlap_spans = sorted(data_spans)
            starts = [s[0] for s in overlap_spans]
            print('scratchpad census, the hottest lines of main RAM (misses, cumulative, address, region: what overlaps the line):')
            running = 0
            for rank, (m, line) in enumerate(hot):
                running += m
                address = LO + (line << 6)
                i = bisect.bisect_right(starts, address + 63)
                names = []
                while i > 0 and len(names) < 6:
                    i -= 1
                    start, end, name, kind = overlap_spans[i]
                    if end > address and start < address + 64:
                        names.append(name)
                    if start < address - 0x40000:
                        break
                if rank < 200 or rank in (255, 383, 511):
                    print('  %4d %9d %10d  %08x  %s: %s' % (rank + 1, m, running, address, region(address, program_end),
                          ', '.join(reversed(names)) or '-'))
        if topsets:
            print('scratchpad census, the level BSP\'s top levels (nodes down to depth d and the planes they test: lines, misses):')
            nodes_so_far, planes_so_far = set(), set()
            for d in range(max(k[0] for k in topsets) + 1):
                nodes_so_far |= topsets.get((d, 'n'), set())
                planes_so_far |= topsets.get((d, 'p'), set())
                mn = sum(misses[p][(a - LO) >> 6] for a in nodes_so_far if LO <= a < HI)
                mp = sum(misses[p][(a - LO) >> 6] for a in planes_so_far if LO <= a < HI)
                print('  depth %2d: node lines %5d (%6d B) misses %9d | plane lines %5d (%6d B) misses %9d | together %9d in %6d B' % (
                    d, len(nodes_so_far), 64 * len(nodes_so_far), mn, len(planes_so_far), 64 * len(planes_so_far), mp,
                    mn + mp, 64 * (len(nodes_so_far) + len(planes_so_far))))
        if spans_named:
            print('scratchpad census, small named data (up to 8 KB) by misses per KB:')
            small = [(m * 1024.0 / size, m, size, key) for key, (a, m, size) in by_name.items()
                     if m and size <= 8192 and not key.startswith('(')]
            for per_kb, m, size, key in sorted(small, reverse=True)[:40]:
                print('  %9d  %6d B  %9.0f/KB  %s' % (m, size, per_kb, key))
        # every line of the program's constants and variables that missed, for a
        # data layout by heat (psp_build.py's PSP_DATA_LAYOUT): the
        # hottest-lines list above stops at 200 lines of all of RAM
        data_start = min((s[0] for s in symbols if s[3] in 'rbdv'), default=program_end)
        print('program data lines (address, accesses, misses, the variable at the line\'s start):')
        for line in range((data_start - LO) >> 6, (program_end + 63 - LO) >> 6):
            m = misses[p][line]
            if m >= 100:
                address = LO + (line << 6)
                print('  %08x %10d %8d  %s' % (address, accesses[p][line], m, data_namer.name(address)[0] or '-'))
        # the code whose loads and stores missed (DCM3 dumps)
        if dmiss_pc is not None:
            by_code = defaultdict(int)
            for line in range(LINES):
                m = dmiss_pc[p][line]
                if m:
                    name, size = code_namer.name(LO + (line << 6))
                    by_code[name or '(unnamed %06x)' % (((LO + (line << 6)) >> 12) << 12)] += m
            print('data misses by the function that missed:')
            for key, m in sorted(by_code.items(), key=lambda x: -x[1])[:top]:
                print('  %9d  %5.1f%%  %s' % (m, 100.0 * m / total_misses, key))
        # instructions
        by_function = defaultdict(lambda: [0, 0, 0])
        line_fetches = []
        for line in range(LINES):
            f = ifetches[p][line]
            if not f:
                continue
            address = LO + (line << 6)
            name, size = code_namer.name(address)
            key = name or '(unnamed %06x)' % ((address >> 12) << 12)
            by_function[key][0] += f
            by_function[key][1] += imisses[p][line]
            by_function[key][2] = size
            line_fetches.append(f)
        if line_fetches:
            line_fetches.sort(reverse=True)
            total = float(sum(line_fetches))
            running, half, ninety = 0, None, None
            for i, f in enumerate(line_fetches):
                running += f
                if half is None and running >= 0.5 * total:
                    half = (i + 1) * 64
                if ninety is None and running >= 0.9 * total:
                    ninety = (i + 1) * 64
                    break
            total_imisses = sum(v[1] for v in by_function.values()) or 1
            print('instructions: half the fetches from %d KB of code, nine tenths from %d KB' % (half // 1024, ninety // 1024))
            print('instruction misses by function (fetches, size):')
            for key, (f, m, size) in sorted(by_function.items(), key=lambda x: -x[1][1])[:top]:
                print('  %9d  %5.1f%%  fetches %10d  %6d B  %s' % (m, 100.0 * m / total_imisses, f, size, key))
            # 2026-10-13: by fetches too (the list above, by misses, hides a
            # function that runs a lot from the cache), and the hottest lines of
            # the leaders, disassembled (with file:line when the build has -g1)
            print('instruction fetches by function (share of the phase\'s fetches, misses):')
            ranked = sorted(by_function.items(), key=lambda x: -x[1][0])
            for key, (f, m, size) in ranked[:top]:
                print('  %11d  %5.1f%%  misses %9d  %6d B  %s' % (f, 100.0 * f / total, m, size, key))
            if '--lines' in sys.argv and PHASE_NAMES[p] in ('tick', 'drawing'):
                hot_lines(p, ifetches, imisses, dmiss_pc, code_namer, elf, [k for k, v in ranked[:int(sys.argv[sys.argv.index('--lines') + 1])]], total)
    # last, so the sections above stay where their readers look for them
    if any(name == 'psp_spad_class' for start, end, name, kind in symbols):
        spad_sets(phases, totals, accesses, misses, data_namer, program_end, log, dump)


if __name__ == '__main__':
    main()
