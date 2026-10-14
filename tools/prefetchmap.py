#!/usr/bin/env python3
"""prefetchmap.py - where a "cache 0x1E" prefetch would pay, by measurement.

Reads the emulator's prefetch sites (the cache model with PSP_DCACHE_SITES:
the lab's PPSSPP patch, a sierra-batch run with pfmap = 1 brings back
pfsites.txt) and ranks the loads and stores that miss by the cycles a
prefetch could hide there, names their functions, and reads the code around
them.

    python3 prefetchmap.py SITES halo.elf [--phase 1] [--top 30] [--near 20]
                           [--inject N [--inject-out FILE]]
    python3 prefetchmap.py SITES --map halo.map ...   (names only, no code)

Why a window: on the PSP-1000 a miss stalls ~72 cycles and two never
overlap; cache 0x1E doesn't stall and hides 80-90% of a miss 64-96 cycles
ahead, but every load or store issued while its fill is in flight waits for
it (halo_bench v7b, v7c [M]). So a prefetch pays only across a stretch of
register-only work (integer, FPU, VFPU, no spills) between the moment its
address is known and the load. The emulator measures, at every miss, the
longest such stretch since the base register was written; a prefetch at its
start hides min(stretch, miss).

What it prints, for one phase (1: the tick, by default):
  - totals: cycles, stalls and misses a tick, and how much of the data stall
    the measured windows could hide;
  - the sites ranked by cycles a tick a prefetch would hide (misses x the
    hideable fraction), each with its misses a tick, whether it is in a loop
    (in the code, and how often it ran again within 4,096 cycles), how many
    cycles before the access its base register was known, the window, and
    the lever it fits (X5 compact loops, X9 collision walks, VFPU node
    matrices, perception's sphere tests);
  - for the top sites, the code from the base register's write to the
    access, the memory operations in the way, and where the prefetch goes;
  - the near misses: sites where up to three memory operations (often $sp
    spills, or loads that could move up) close a window, the candidates for
    "load everything into registers first, then compute, prefetching the next
    item meanwhile";
  - loop sites whose next address is a steady step ahead, with the window
    the previous iteration had (sites format 2 on);
  - the totals by lever.

--inject N writes the static placement of the top N sites as the
emulator's PSP_PF_INJECT lines (sierra-batch's pfinject): the run's model
then issues those prefetches without changing the code, and its DCTIME
lines score them against a run without.

Cycles here are the model's: PPSSPP's estimate per instruction plus the
measured stalls, not a PSP's clock. Treat the hideable cycles as [E] and
the ms as [E] at 333 MHz."""
import argparse
import bisect
import os
import re
import subprocess
import sys
from collections import defaultdict

# the game's ELF is linked at 0 and loaded here; an ELF linked where it
# loads (a plain homebrew test) needs no offset, which Names notices
PROGRAM_BASE = 0x08804000
MHZ = 333.0
PHASE_NAMES = ['other', 'tick', 'drawing', 'frame end']

SITE_FIELDS = ['phase', 'pc', 'kind', 'execs', 'misses', 'stall', 'waited', 'hide_any', 'hide0', 'hide1', 'hide2',
               'hide3', 'dist', 'far', 'recur', 'ops', 'sp_ops', 'hide_next', 'stride', 'delta', 'hd1', 'hd2', 'hd3']
TOTAL_FIELDS = ['phase', 'cycles', 'dstall', 'istall', 'waits', 'pf_issued', 'pf_fills', 'pf_used', 'pf_unused',
                'misses', 'imisses']

# Which known lever a function's name fits (optimization_ranking.md): the
# first pattern that matches wins
LEVERS = [
    ('perception spheres', re.compile(r'perception|sphere|_in_sphere|visible|line_of_sight|actor_.*_test', re.I)),
    ('X9 collision', re.compile(r'collision|bsp|leaf|surface|plane|_ray|vector_test|pill|edge|obstacle|psp_surf', re.I)),
    ('VFPU node matrices', re.compile(r'node|matri|orientation|animation|vfpu|skeleton|bone', re.I)),
    ('X5 compact loops', re.compile(r'iterator|datum|data_|cluster_get|object|_list|actor|unit_|biped_|ai_|update', re.I)),
]

MEMORY_OPS = {'lb', 'lbu', 'lh', 'lhu', 'lw', 'lwl', 'lwr', 'll', 'lwc1', 'lv.s', 'lv.q', 'lvl.q', 'lvr.q',
              'sb', 'sh', 'sw', 'swl', 'swr', 'sc', 'swc1', 'sv.s', 'sv.q', 'svl.q', 'svr.q', 'cache'}
STORES = {'sb', 'sh', 'sw', 'swl', 'swr', 'sc', 'swc1', 'sv.s', 'sv.q', 'svl.q', 'svr.q', 'cache'}
# instructions whose first operand isn't a general register they write
NO_GPR_WRITE = STORES | {'j', 'jr', 'jal', 'jalr', 'mtc1', 'ctc1', 'mtv', 'mtvc', 'sync', 'syscall', 'break', 'nop',
                         'mult', 'multu', 'div', 'divu', 'madd', 'maddu', 'msub', 'msubu', 'mthi', 'mtlo',
                         'lwc1', 'lv.s', 'lv.q', 'lvl.q', 'lvr.q'}
UNCONDITIONAL = {'j', 'jr', 'b'}
CALL_CLOBBERS = {'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
                 't8', 't9', 'ra'}
REG_ALIASES = {'s8': 'fp'}


def read_sites(path):
    """(header dict, totals by phase, site dicts)"""
    header, totals, sites = {}, {}, []
    with open(path) as f:
        for line in f:
            if line.startswith('# prefetch sites'):
                m = re.match(r'# prefetch sites (\d+):(.*)', line)
                header['format'] = int(m.group(1))
                words = m.group(2).split()
                for k, v in zip(words[0::2], words[1::2]):
                    header[k] = int(v)
                continue
            parts = line.split()
            if not parts or parts[0] == '#':
                continue
            if parts[0] == 'T':
                t = dict(zip(TOTAL_FIELDS, [int(x) for x in parts[1:]]))
                totals[t['phase']] = t
            elif parts[0] == 'S':
                s = {}
                for name, value in zip(SITE_FIELDS, parts[1:]):
                    if name == 'pc':
                        s[name] = int(value, 16)
                    elif name == 'kind':
                        s[name] = value
                    else:
                        s[name] = int(value)
                for name in SITE_FIELDS:
                    s.setdefault(name, 0)
                sites.append(s)
    if 'format' not in header:
        sys.exit('%s: not a prefetch sites file' % path)
    return header, totals, sites


# --- names and code ---------------------------------------------------------------

def tool(name):
    nm = os.environ.get('PSP_NM', '/opt/pspdev/bin/psp-nm')
    if name == 'nm':
        return nm
    return nm[:-2] + name if nm.endswith('nm') else 'psp-' + name


class Names:
    """Functions by loaded address, from the ELF (psp-nm) or the link map"""

    def __init__(self, elf=None, linkmap=None):
        spans = []
        global PROGRAM_BASE
        if elf:
            out = subprocess.run([tool('nm'), '-n', '-S', elf], capture_output=True, text=True).stdout
            rows = [line.split() for line in out.splitlines()]
            rows = [p for p in rows if len(p) == 4 and p[2] in 'tTwW']
            if rows and min(int(p[0], 16) for p in rows) >= 0x08000000:
                PROGRAM_BASE = 0
            for parts in rows:
                start = int(parts[0], 16) + PROGRAM_BASE
                spans.append((start, start + int(parts[1], 16), parts[3]))
        elif linkmap:
            spans = self.from_map(linkmap)
        spans.sort()
        self.starts = [s[0] for s in spans]
        self.spans = spans

    @staticmethod
    def from_map(path):
        """.text.<name> input sections with their final addresses (the
        line may wrap after a long section name)"""
        spans, pending = [], None
        rx = re.compile(r'\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+\S')
        with open(path, errors='replace') as f:
            for line in f:
                m = re.match(r' \.text\.(\S+)(.*)', line)
                if m:
                    pending = m.group(1)
                    rest = m.group(2)
                else:
                    rest = line if pending else ''
                n = rx.match(rest) if rest else None
                if pending and n:
                    start, size = int(n.group(1), 16), int(n.group(2), 16)
                    if start and size:
                        spans.append((start + PROGRAM_BASE, start + PROGRAM_BASE + size, pending))
                    pending = None
                elif not m:
                    pending = None
        return spans

    def find(self, pc):
        i = bisect.bisect_right(self.starts, pc) - 1
        if i >= 0 and self.spans[i][0] <= pc < self.spans[i][1]:
            return self.spans[i]
        return None

    def name(self, pc):
        span = self.find(pc)
        if not span:
            return '?%08x' % pc
        return '%s+0x%x' % (span[2], pc - span[0])


def lever(function):
    for name, rx in LEVERS:
        if rx.search(function):
            return name
    return 'other'


class Code:
    """A function's disassembly, read once (psp-objdump)"""
    line_rx = re.compile(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)')

    def __init__(self, elf):
        self.elf = elf
        self.cache = {}

    def function(self, span):
        if span in self.cache:
            return self.cache[span]
        start, end, _ = span
        out = subprocess.run([tool('objdump'), '-d', '--no-show-raw-insn', '--start-address=0x%x' % (start - PROGRAM_BASE),
                              '--stop-address=0x%x' % (end - PROGRAM_BASE), self.elf],
                             capture_output=True, text=True).stdout
        insns = []
        for line in out.splitlines():
            m = self.line_rx.match(line)
            if m and not line.rstrip().endswith('>:'):
                address = int(m.group(1), 16) + PROGRAM_BASE
                operands = [o.strip() for o in re.sub(r'\s*<[^>]*>', '', m.group(3)).split(',')] if m.group(3) else []
                insns.append((address, m.group(2), operands))
        targets = set()
        for address, op, operands in insns:
            if (op.startswith('b') and op not in ('break',)) or op in ('j', 'jal'):
                if operands and re.fullmatch(r'[0-9a-f]+', operands[-1]):
                    targets.add(int(operands[-1], 16) + PROGRAM_BASE)
        self.cache[span] = (insns, targets)
        return self.cache[span]


def base_of(operands):
    """(offset, base register) of a memory operand like 8(a0)"""
    if not operands:
        return None
    m = re.fullmatch(r'(-?(?:0x)?[0-9a-f]*)\((\w+)\)', operands[-1])
    if not m:
        return None
    off = m.group(1) or '0'
    return int(off, 0), REG_ALIASES.get(m.group(2), m.group(2))


def writes(op, operands):
    """the general registers an instruction writes"""
    if op in ('jal', 'jalr', 'bal', 'bltzal', 'bgezal', 'bltzall', 'bgezall'):
        return CALL_CLOBBERS if op in ('jal', 'jalr') else {'ra'}
    if op in NO_GPR_WRITE or op.startswith('b') or op.startswith('c.') or not operands:
        return set()
    if op.startswith('mt') and op not in ('mthi', 'mtlo'):
        return set()
    first = REG_ALIASES.get(operands[0], operands[0])
    if first.startswith('$') or re.fullmatch(r'[SCRMEsc]\d+', first):
        return set()   # an FPU or VFPU register
    return {first}


def is_branch(op):
    return (op.startswith('b') and op != 'break') or op in ('j', 'jr', 'jal', 'jalr')


def walk_back(code, span, pc, longest=96):
    """From the access at pc back through the code that falls through to it,
    to its base register's write: what lies between, and where a prefetch
    would go. Branches that land on the way are only noted: a prefetch
    placed above one serves the path that falls through"""
    insns, targets = code.function(span)
    index = {a: i for i, (a, _, _) in enumerate(insns)}
    if pc not in index:
        return None
    _, op, operands = insns[index[pc]]
    mem = base_of(operands)
    if op not in MEMORY_OPS or not mem:
        return None
    offset, base = mem
    result = {'insn': '%s %s' % (op, ','.join(operands)), 'base': base, 'offset': offset, 'def': None,
              'stop': None, 'blockers': [], 'path': [], 'joins': []}
    i = index[pc]
    in_delay = i > 0 and is_branch(insns[i - 1][1])
    result['delay_slot'] = in_delay
    steps = 0
    while True:
        if insns[i][0] in targets:
            result['joins'].append(insns[i][0])
        if i == 0:
            result['stop'] = 'function start'
            break
        if i >= 2 and insns[i - 2][1] in UNCONDITIONAL:
            result['stop'] = 'after an unconditional jump'
            break
        i -= 1
        steps += 1
        address, op2, ops2 = insns[i]
        result['path'].append(insns[i])
        if base in writes(op2, ops2):
            result['def'] = (address, '%s %s' % (op2, ','.join(ops2)))
            break
        if op2 in MEMORY_OPS:
            b = base_of(ops2)
            result['blockers'].append((address, '%s %s' % (op2, ','.join(ops2)), bool(b and b[1] == 'sp')))
        if steps >= longest:
            result['stop'] = 'more than %d instructions' % longest
            break
    # the longest stretch with no memory operation between the base's write
    # (or, when the walk stopped at a block's start, that start) and the
    # access: the prefetch goes before its first instruction
    path = list(reversed(result['path']))  # oldest first
    starts = []      # (first instruction of a stretch, its length)
    if result['def']:
        start, rest = path[0][0] + 4, path[1:]
    elif result['stop'] in ('function start', 'after an unconditional jump'):
        start, rest = (path[0][0] if path else pc), path
    else:
        start, rest = None, path   # the walk gave up: what came before is unknown
    run = 0
    for address, op2, ops2 in rest:
        if op2 in MEMORY_OPS:
            if start is not None:
                starts.append((start, run))
            start, run = address + 4, 0
        else:
            run += 1
    if start is not None:
        starts.append((start, run))
    best = max(starts, key=lambda x: x[1]) if starts else None
    result['static_run'] = best[1] if best else 0
    result['place_before'] = best[0] if best and best[1] > 0 else None
    if result['place_before'] is not None:
        k = index.get(result['place_before'])
        result['place_in_delay'] = k is not None and k > 0 and is_branch(insns[k - 1][1])
        result['branchy'] = (any(is_branch(o) for a, o, _ in path if result['place_before'] <= a < pc)
                             or any(result['place_before'] < j <= pc for j in result['joins']))
    return result


# --- the report -----------------------------------------------------------------------

def per_tick(x, ticks):
    return x / ticks if ticks else 0.0


def ms(cycles):
    return cycles / (MHZ * 1000.0)


ADDITIVE = ['execs', 'misses', 'stall', 'waited', 'hide_any', 'hide0', 'hide1', 'hide2', 'hide3', 'dist', 'far',
            'recur', 'ops', 'sp_ops', 'hide_next', 'stride', 'hd1', 'hd2', 'hd3']


def is_elf(path):
    with open(path, 'rb') as f:
        return f.read(4) == b'\x7fELF'


def combine(runs, phase):
    """Each run's sites for one phase as figures a tick, then the mean over
    the runs (a site missing from a run counts 0 there): one map of a level
    each, so no level outweighs another for having played longer"""
    merged = {}
    for header, totals, sites in runs:
        ticks = header.get('ticks', 0) if phase == 1 else 0
        ticks = ticks or 1
        for site in sites:
            if site['phase'] != phase:
                continue
            key = (site['pc'], site['kind'])
            m = merged.setdefault(key, dict({k: 0.0 for k in ADDITIVE}, pc=site['pc'], kind=site['kind'],
                                            delta=site['delta'], best=-1))
            for k in ADDITIVE:
                m[k] += site[k] / ticks / len(runs)
            if site['misses'] > m['best']:
                m['best'], m['delta'] = site['misses'], site['delta']
    return list(merged.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('inputs', nargs='+', help='one or more sites files (several: their mean a tick) and the ELF')
    ap.add_argument('--map', help='halo.map, for names when there is no ELF (no code then)')
    ap.add_argument('--phase', type=int, default=1, help='0 other, 1 tick (default), 2 drawing, 3 frame end')
    ap.add_argument('--top', type=int, default=30)
    ap.add_argument('--near', type=int, default=20)
    ap.add_argument('--detail', type=int, default=12, help='top sites shown with their code')
    ap.add_argument('--inject', type=int, default=0, help='write the top N sites\' placements as PSP_PF_INJECT lines')
    ap.add_argument('--inject-out', help='file for --inject (default: printed at the end)')
    args = ap.parse_args()

    elfs = [p for p in args.inputs if is_elf(p)]
    paths = [p for p in args.inputs if p not in elfs]
    if len(elfs) > 1 or not paths:
        sys.exit('prefetchmap: give one or more sites files and at most one ELF')
    elf = elfs[0] if elfs else None
    runs = [read_sites(p) for p in paths]
    header = runs[0][0]
    fmt = min(r[0]['format'] for r in runs)
    miss = header.get('miss', 72)
    unit = 'a tick' if args.phase == 1 else 'in the run'
    ticks = 1   # combine() leaves every figure a tick (a run, outside the tick)
    names = Names(elf, args.map) if (elf or args.map) else Names()
    code = Code(elf) if elf else None
    all_sites = combine(runs, args.phase)
    sites = [s for s in all_sites if s['kind'] in 'ls']
    injected = [s for s in all_sites if s['kind'] == 'p']
    for s in sites:
        span = names.find(s['pc']) if names.spans else None
        s['span'] = span
        s['function'] = span[2] if span else '?'
        s['where'] = names.name(s['pc']) if names.spans else '%08x' % s['pc']
        s['lever'] = lever(s['function'])
        m = s['misses'] or 1
        s['score'] = s['hide0']
        s['net'] = s['hide0'] - s['execs']
        s['near_gain'] = s['hide3'] - s['hide0']
        # what moving up to three data operations could add, the I-misses left
        # where they are (format 3; before it, a bound: neither more than
        # taking out three events of either kind nor than taking out every I-miss)
        if fmt >= 3:
            s['near_data'] = max(s['hd3'] - s['hide0'], 0)
        else:
            s['near_data'] = max(min(s['hide3'], s['hide_any']) - s['hide0'], 0)
        s['next_gain'] = max(s['hide_next'] - s['hide0'], 0)
        s['window'] = s['hide0'] / m
        s['window_any'] = s['hide_any'] / m
        s['dist_mean'] = s['dist'] / m
        s['far_frac'] = s['far'] / m
        s['recur_frac'] = s['recur'] / m
        s['stride_frac'] = s['stride'] / m
        s['ops_mean'] = s['ops'] / m
        s['sp_mean'] = s['sp_ops'] / m

    print('prefetchmap: phase %d (%s), sites format %d; miss %d cycles, I-miss %d, I-misses %s for fills'
          % (args.phase, PHASE_NAMES[args.phase], min(r[0]['format'] for r in runs), miss, header.get('imiss', 0),
             'wait' if header.get('iwait', 1) else "don't wait"))
    for path, (h, totals, _) in zip(paths, runs):
        t = totals.get(args.phase)
        n = (h.get('ticks', 0) if args.phase == 1 else 0) or 1
        if not t:
            continue
        print('%s: %d ticks. %s [E]: %.0f cycles (%.2f ms), data-miss stall %.0f (%.2f ms; %.0f misses), '
              'instruction-miss stall %.0f (%.2f ms; %.0f misses), waits on fills %.0f'
              % (path, h.get('ticks', 0), unit.capitalize(), t['cycles'] / n, ms(t['cycles'] / n), t['dstall'] / n,
                 ms(t['dstall'] / n), t['misses'] / n, t['istall'] / n, ms(t['istall'] / n), t['imisses'] / n,
                 t['waits'] / n))
        if t['pf_issued']:
            print('  prefetches %s: %.1f issued, %.1f filled, %.1f used, %.1f evicted unused'
                  % (unit, t['pf_issued'] / n, t['pf_fills'] / n, t['pf_used'] / n, t['pf_unused'] / n))
    if len(runs) > 1:
        print('the figures below are the mean of the %d runs, %s' % (len(runs), unit))
    listed = sum(s['misses'] for s in sites)
    hide_all = sum(s['hide0'] for s in sites)
    hide_any = sum(s['hide_any'] for s in sites)
    hide3 = sum(s['hide3'] for s in sites)
    near_data = sum(s['near_data'] for s in sites)
    net_all = sum(max(s['hide0'] - s['execs'], 0) for s in sites)
    print('%d sites (each with at least %d misses in its run), %.0f misses %s' % (len(sites), header.get('min', 0),
                                                                              listed, unit))
    print('a prefetch at every site, each at its best window, would hide [E] %.0f cycles %s (%.3f ms; %.1f%% of '
          'their stall), %.0f (%.3f ms) after the prefetch\'s own cycle at each run, at the sites where that pays'
          % (hide_all, unit, ms(hide_all), 100.0 * hide_all / max(listed * miss, 1), net_all, ms(net_all)))
    print('  if I-misses don\'t wait for a fill (unmeasured [U]): %.0f (%.3f ms)' % (hide_any, ms(hide_any)))
    print('  with up to three memory events (data operations or I-misses) out of each window: %.0f (%.3f ms)'
          % (hide3, ms(hide3)))
    print('  with up to three data operations moved out of each window, the I-misses left: %.0f (%.3f ms)%s'
          % (hide_all + near_data, ms(hide_all + near_data), '' if fmt >= 3 else ' (an upper bound before format 3)'))
    print()

    # --- ranked sites
    ranked = sorted(sites, key=lambda s: -s['score'])
    print('== sites ranked by cycles a prefetch would hide %s [E]' % unit)
    print('%-4s %-9s %-46s %-2s %8s %6s %-9s %7s %5s %6s %6s %8s %8s  %s'
          % ('#', 'pc', 'function', 'k', 'miss/t', 'm/run', 'loop', 'known', 'far', 'win', 'win*', 'hide/t',
             'net/t', 'lever'))
    details = {}
    for n, s in enumerate(ranked[:args.top], 1):
        info = walk_back(code, s['span'], s['pc']) if code and s['span'] else None
        details[s['pc']] = info
        loop = ''
        if info is not None:
            insns, targets = code.function(s['span'])
            loop = 'L' if any(is_branch(o) and ops and re.fullmatch(r'[0-9a-f]+', ops[-1])
                              and int(ops[-1], 16) + PROGRAM_BASE <= s['pc'] < a for a, o, ops in insns) else '-'
        loop += ' %3.0f%%' % (100 * s['recur_frac'])
        print('%-4d %08x  %-46s %-2s %8.1f %6.2f %-9s %7.0f %4.0f%% %6.1f %6.1f %8.0f %8.0f  %s'
              % (n, s['pc'], s['where'][:46], s['kind'], per_tick(s['misses'], ticks),
                 s['misses'] / (s['execs'] or 1), loop, s['dist_mean'], 100 * s['far_frac'], s['window'],
                 s['window_any'], s['score'], s['net'], s['lever']))
    print('  miss/t: misses a tick; m/run: misses per run of the instruction; loop: L when a backward branch in the '
          'function spans it, then how often it ran again within 4,096 cycles; known: mean cycles from the base '
          'register\'s write to the access; far: written before the 64 events the model keeps; win: mean hideable '
          'cycles a miss (I-misses in the way), win*: with only data operations in the way; hide/t: cycles a tick '
          'it would hide; net/t: less a cycle for each run of the prefetch')
    print()

    if code:
        print('== the top %d with their code' % args.detail)
        for n, s in enumerate(ranked[:args.detail], 1):
            info = details.get(s['pc'])
            print('%d. %08x %s: %s' % (n, s['pc'], s['where'], info['insn'] if info else '(no code)'))
            if not info:
                continue
            if info['def']:
                print('   base %s written at %08x (%s), %d instructions before'
                      % (info['base'], info['def'][0], info['def'][1], len(info['path'])))
            else:
                print('   base %s not written in the straight-line code before it (%s)' % (info['base'], info['stop']))
            if info['blockers']:
                print('   in the way: ' + '; '.join('%08x %s%s' % (a, txt, ' (spill)' if sp else '')
                                                  for a, txt, sp in reversed(info['blockers'])))
            if info.get('place_before') is not None:
                note = ''
                if info.get('place_in_delay'):
                    note = ' (that spot is a delay slot: in the code it goes before the branch)'
                elif info.get('branchy'):
                    note = ' (a branch leaves or lands between: the window is on the path that falls through)'
                print('   place: cache 0x1E, %d(%s) before %08x: %d instructions with no memory operation follow%s'
                      % (info['offset'], info['base'], info['place_before'], info['static_run'], note))
            else:
                print('   place: no stretch free of memory operations in the straight-line code')
            print('   measured: window %.1f cycles a miss (%.1f with data operations only), %.1f memory operations '
                  'in the span (%.1f on $sp)' % (s['window'], s['window_any'], s['ops_mean'], s['sp_mean']))
        print()

    # --- near misses
    near = sorted([s for s in sites if s['near_data'] > 0], key=lambda s: -s['near_data'])
    print('== near misses: cycles %s more if up to three data operations moved out of the window [E]' % unit)
    print('%-4s %-9s %-46s %8s %6s %6s %6s %6s %6s %6s %6s %8s  %s'
          % ('#', 'pc', 'function', 'miss/t', 'win', 'win-1', 'win-2', 'win-3', 'win*', 'ops', 'on sp', 'gain/t',
             'lever'))
    for n, s in enumerate(near[:args.near], 1):
        m = s['misses'] or 1
        k = ['hd1', 'hd2', 'hd3'] if fmt >= 3 else ['hide1', 'hide2', 'hide3']
        print('%-4d %08x  %-46s %8.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %8.0f  %s'
              % (n, s['pc'], s['where'][:46], s['misses'], s['window'], s[k[0]] / m, s[k[1]] / m,
                 s[k[2]] / m, s['window_any'], s['ops_mean'], s['sp_mean'], s['near_data'], s['lever']))
        info = walk_back(code, s['span'], s['pc']) if code and s['span'] else None
        if info and info['blockers']:
            print('       in the way: ' + '; '.join('%08x %s%s' % (a, txt, ' (spill)' if sp else '')
                                                for a, txt, sp in list(reversed(info['blockers']))[:6]))
    print('  win-N: the window with N data operations taken out, the I-misses left (before format 3: N memory '
          'events of either kind); win*: with only data operations in the way; gain/t: what taking out up to three '
          'data operations adds')
    print()

    # --- loops with a steady step: the next item, a run ahead
    if header['format'] >= 2:
        pipes = sorted([s for s in sites if s['recur_frac'] >= 0.5 and s['next_gain'] > 0],
                       key=lambda s: -(s['next_gain'] * (0.5 + s['stride_frac'])))
        print('== loops: prefetching the next run\'s line in this run (software pipelining) [E]')
        print('%-4s %-9s %-46s %8s %6s %7s %7s %6s %8s  %s'
              % ('#', 'pc', 'function', 'miss/t', 'win', 'win-it', 'steady', 'step', 'gain/t', 'lever'))
        for n, s in enumerate(pipes[:args.near], 1):
            m = s['misses'] or 1
            print('%-4d %08x  %-46s %8.1f %6.1f %7.1f %6.0f%% %6d %8.0f  %s'
                  % (n, s['pc'], s['where'][:46], per_tick(s['misses'], ticks), s['window'], s['hide_next'] / m,
                     100 * s['stride_frac'], s['delta'], s['next_gain'], s['lever']))
        print('  win-it: mean hideable cycles anywhere in the previous run\'s iteration; steady: misses whose '
              'address stepped as the last did (step: the last step, bytes); a pointer chase has no steady step '
              'but its next address is still known once this item is loaded')
        print()

    # --- by lever
    print('== by lever: hideable cycles %s [E]' % unit)
    by = defaultdict(lambda: [0, 0, 0, 0, 0])
    for s in sites:
        b = by[s['lever']]
        b[0] += 1
        b[1] += s['misses']
        b[2] += s['hide0']
        b[3] += s['hide3']
        b[4] += max(s['hide_next'], s['hide0'])
    print('%-20s %6s %10s %10s %10s %10s %10s' % ('lever', 'sites', 'miss/t', 'hide/t', 'ms', 'hide-3/t', 'loop/t'))
    for name, b in sorted(by.items(), key=lambda kv: -kv[1][2]):
        print('%-20s %6d %10.1f %10.0f %10.3f %10.0f %10.0f' % (name, b[0], per_tick(b[1], ticks), per_tick(b[2], ticks),
                                                            ms(per_tick(b[2], ticks)), per_tick(b[3], ticks),
                                                            per_tick(b[4], ticks)))
    print()

    if injected:
        print('== prefetches the model issued (PSP_PF_INJECT or the code\'s own)')
        for s in sorted(injected, key=lambda s: -s['misses']):
            print('  %08x %-46s %10.1f issued %s, %8.1f cycles waited %s'
                  % (s['pc'], names.name(s['pc']) if names.spans else '', per_tick(s['misses'], ticks), unit,
                     per_tick(s['waited'], ticks), unit))
        print()

    if args.inject:
        lines = []
        for s in ranked[:args.inject]:
            info = details.get(s['pc']) or (walk_back(code, s['span'], s['pc']) if code and s['span'] else None)
            if not info or info.get('place_before') is None:
                lines.append('# %08x %s: no straight-line placement' % (s['pc'], s['where']))
                continue
            lines.append('%08x %s %d  # for %08x %s' % (info['place_before'], info['base'], info['offset'],
                                                      s['pc'], s['where']))
        text = '\n'.join(lines) + '\n'
        if args.inject_out:
            with open(args.inject_out, 'w') as f:
                f.write(text)
        else:
            print('== PSP_PF_INJECT lines for the top %d' % args.inject)
            print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
