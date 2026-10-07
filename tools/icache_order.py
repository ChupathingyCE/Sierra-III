#!/usr/bin/env python3
"""icache_order.py - the I-cache experiment (build 102).

PPSSPP's interpreter (local patch, PSP_ICACHE=1) models the Allegrex's
instruction cache (16 KB, 2-way, 64-byte lines) on every fetch and writes,
per 64-byte line of the game's text, its fetches and misses (icache.bin).

    python3 icache_order.py report icache.bin halo.elf
        the hottest functions, their share of fetches and misses

    python3 icache_order.py map icache.bin halo.elf map.txt [--hot-kb 96]
        a reordering: the functions that take most fetches, hottest first,
        packed from the start of the text, the rest after in their order.
        map.txt holds "old_start old_end new_start" (hex) for PPSSPP's
        PSP_ICACHE_MAP, which measures the reordered program's misses on
        the same run without building it; and order.txt, the section names
        in that order, for the linker script if it is worth building

The text is loaded at 0x08804000 (PROGRAM_BASE in psp_build.py)."""
import struct, subprocess, sys, os, bisect

BASE = 0x08804000
LO, HI = 0x08804000, 0x08C00000


def load_counts(path):
    d = open(path, 'rb').read()
    fetches, misses, misses_b, remap = struct.unpack_from('<QQQQ', d, 0)
    n = (HI - LO) >> 6
    import array
    lf = array.array('I'); lf.frombytes(d[32:32 + 4 * n])
    lm = array.array('I'); lm.frombytes(d[32 + 4 * n:32 + 8 * n])
    lb = array.array('I'); lb.frombytes(d[32 + 8 * n:32 + 12 * n])
    return dict(fetches=fetches, misses=misses, misses_b=misses_b, remap=remap, lf=lf, lm=lm, lb=lb)


def functions(elf):
    """[(start, end, name)] of the text's functions, at their load address"""
    nm = os.environ.get('NM', 'psp-nm')
    out = subprocess.run([nm, '-S', '-n', '--defined-only', elf], capture_output=True, text=True).stdout
    funcs = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2] in 'tTW':
            start, size = int(parts[0], 16), int(parts[1], 16)
            if size:
                funcs.append((BASE + start, BASE + start + size, parts[3]))
    funcs.sort()
    return funcs


def per_function(c, funcs):
    starts = [f[0] for f in funcs]
    heat = {}
    for line in range(len(c['lf'])):
        f = c['lf'][line]
        if not f:
            continue
        addr = LO + line * 64
        i = bisect.bisect_right(starts, addr) - 1
        # a line may hold the ends of two functions: charged to the one at its start
        key = i if i >= 0 and addr < funcs[i][1] + 64 else -1
        h = heat.setdefault(key, [0, 0, 0])
        h[0] += f
        h[1] += c['lm'][line]
        h[2] += c['lb'][line]
    return heat


def report(c, funcs, top=40):
    heat = per_function(c, funcs)
    print('fetches %d, misses %d (%.3f%%)%s' % (c['fetches'], c['misses'], 100.0 * c['misses'] / max(1, c['fetches']),
          ', remapped layout %d misses (%+.1f%%)' % (c['misses_b'], 100.0 * (c['misses_b'] - c['misses']) / max(1, c['misses']))
          if c['remap'] else ''))
    total_f = sum(h[0] for h in heat.values())
    total_m = sum(h[1] for h in heat.values())
    rows = sorted(heat.items(), key=lambda kv: -kv[1][0])
    cumulative = 0
    size = 0
    print('%-48s %8s %7s %7s %8s %7s' % ('function', 'bytes', 'fetch%', 'cum%', 'misses%', 'cum KB'))
    for k, h in rows[:top]:
        name = funcs[k][2] if k >= 0 else '?'
        length = funcs[k][1] - funcs[k][0] if k >= 0 else 0
        cumulative += h[0]
        size += length
        print('%-48s %8d %6.2f%% %6.1f%% %7.2f%% %7.1f' % (name[:48], length, 100.0 * h[0] / total_f, 100.0 * cumulative / total_f,
              100.0 * h[1] / max(1, total_m), size / 1024))
    # how much code takes 90 / 99% of the fetches
    for share in (0.5, 0.9, 0.99):
        acc = 0; nbytes = 0
        for k, h in rows:
            acc += h[0]
            nbytes += funcs[k][1] - funcs[k][0] if k >= 0 else 0
            if acc >= share * total_f:
                break
        print('%.0f%% of fetches in %d KB of functions' % (share * 100, nbytes // 1024))


def make_map(c, funcs, path, hot_kb, cold_last=False):
    """hot_kb of the hottest functions first (0: none); cold_last: every
    function's .cold part (GCC's -freorder-blocks-and-partition) after the
    rest, instead of next to its function"""
    heat = per_function(c, funcs)
    rows = sorted([kv for kv in heat.items() if kv[0] >= 0 and not (cold_last and funcs[kv[0]][2].endswith('.cold'))],
                  key=lambda kv: -kv[1][0])
    hot, size = [], 0
    for k, h in rows:
        length = funcs[k][1] - funcs[k][0]
        if size + length > hot_kb * 1024:
            continue
        hot.append(k)
        size += length
    hot_set = set(hot)
    rest = [i for i in range(len(funcs)) if i not in hot_set]
    if cold_last:
        rest = [i for i in rest if not funcs[i][2].endswith('.cold')] + [i for i in rest if funcs[i][2].endswith('.cold')]
    order = hot + rest
    lines, at = [], LO
    for i in order:
        s, e, n = funcs[i]
        at = (at + 3) & ~3
        lines.append((s, e, at))
        at += e - s
    lines.sort()
    with open(path, 'w') as f:
        for s, e, n in lines:
            f.write('%08x %08x %08x\n' % (s, e, n))
    with open(os.path.join(os.path.dirname(os.path.abspath(path)), 'order.txt'), 'w') as f:
        for i in hot:
            f.write('.text.%s\n' % funcs[i][2])
    print('%d hot functions (%d KB) first; map of %d functions written to %s' % (len(hot), size // 1024, len(lines), path))


if __name__ == '__main__':
    what, counts_path, elf = sys.argv[1], sys.argv[2], sys.argv[3]
    c = load_counts(counts_path)
    funcs = functions(elf)
    if what == 'report':
        report(c, funcs)
    else:
        hot_kb = int(sys.argv[sys.argv.index('--hot-kb') + 1]) if '--hot-kb' in sys.argv else 96
        make_map(c, funcs, sys.argv[4], hot_kb, '--cold-last' in sys.argv)
