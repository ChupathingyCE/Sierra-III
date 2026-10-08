#!/usr/bin/env python3
"""pcprofile.py - where the game's instructions go, from the emulator's PC samples.

The local PPSSPP patch (tools/ppsspp_local_patches.diff, CoreTiming.cpp)
samples the running thread's PC and return address every 100 us of emulated
time when PSP_PCSAMPLE=<file> is set, and writes "thread pc ra count" lines.
This maps them to the game's functions with the ELF's symbols.

usage: python3 tools/pcprofile.py SAMPLES halo.elf [--thread user_main] [--top 40] [--callers 8]
           [--since EARLIER_SAMPLES] [--step TEXT] [--steps]

The file holds every sample since the emulator started; --since takes away an
earlier copy of it, so the report covers only the time between the two (copy
the file now and then during a run to cut it into windows).

With PSP_PCSAMPLE_STEP=<hex address of psp_platform.c's last_step> (b106;
the ELF's symbol plus 0x08804000) each sample also carries the name of the
psp_step running: --steps lists the steps by samples (every thread's: time
another thread took during a step counts toward it), --step TEXT reports
only the samples taken during steps whose names contain TEXT.

Emulated cycles, not the PSP's: the emulator models no cache, so a function
that misses the cache a lot (collision walks, big tables) is cheaper here than
on the PSP-1000, which runs the game's tick about 4x slower than this. Use it
to find where the work is, and the hardware's own profile (hwprofile) to
weigh it."""
import argparse, bisect, collections, os, shutil, subprocess, sys

PROGRAM_BASE = 0x08804000   # psp_build.py: where the kernel loads the module


def symbols(elf):
    nm = shutil.which('psp-nm') or '/opt/psp/pspdev/bin/psp-nm'
    out = subprocess.run([nm, '-n', '-S', '--defined-only', elf], capture_output=True, text=True).stdout
    starts, names, sizes = [], [], []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2] in 'tTwW':
            starts.append(int(parts[0], 16) + PROGRAM_BASE)
            sizes.append(int(parts[1], 16))
            names.append(parts[3])
    return starts, names, sizes


def name_of(address, table):
    starts, names, sizes = table
    i = bisect.bisect_right(starts, address) - 1
    if i >= 0 and address < starts[i] + max(sizes[i], 4):
        return names[i]
    if address < PROGRAM_BASE:
        return '(kernel or firmware)'
    return '(%08x)' % address


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('samples')
    ap.add_argument('elf')
    ap.add_argument('--thread', default='user_main')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--callers', type=int, default=0, help='for the top functions, their callers by return address')
    ap.add_argument('--since', help='an earlier copy of the samples file: only what came after it')
    ap.add_argument('--step', help='only samples taken while a step whose name contains this ran')
    ap.add_argument('--steps', action='store_true', help='the steps by samples, every thread\'s')
    a = ap.parse_args()

    def fields(line):
        parts = line.split()
        if len(parts) < 4:
            return None   # a line cut short (the file copied while the emulator wrote it)
        if len(parts) == 4:
            parts.append('-')
        thread, pc, ra, count, step = parts[:5]
        return thread, pc, ra, int(count), step

    earlier = collections.Counter()
    if a.since:
        for line in open(a.since):
            f = fields(line)
            if f:
                thread, pc, ra, count, step = f
                earlier[(thread, pc, ra, step)] = count
    table = symbols(a.elf)
    by_thread = collections.Counter()
    self_count = collections.Counter()
    callers = collections.defaultdict(collections.Counter)
    steps = collections.defaultdict(collections.Counter)
    total = 0
    for line in open(a.samples):
        f = fields(line)
        if not f:
            continue
        thread, pc, ra, count, step = f
        count -= earlier[(thread, pc, ra, step)]
        if count <= 0:
            continue
        steps[step][thread] += count
        if a.step and a.step not in step.replace('_', ' '):
            continue
        by_thread[thread] += count
        if thread != a.thread:
            continue
        function = name_of(int(pc, 16), table)
        self_count[function] += count
        callers[function][name_of(int(ra, 16), table)] += count
        total += count
    if a.steps:
        everything = sum(sum(c.values()) for c in steps.values())
        print('steps by samples (all threads), %d samples:' % everything)
        for step, c in sorted(steps.items(), key=lambda kv: -sum(kv[1].values()))[:a.top]:
            n = sum(c.values())
            print('%6.2f%% %7d  %s  (%s)' % (100.0 * n / everything, n, step.replace('_', ' '),
                  ', '.join('%s %d' % kv for kv in c.most_common(3))))
        return
    print('samples by thread: ' + ', '.join('%s %d' % kv for kv in by_thread.most_common(8)))
    print('%s: %d samples (100 us of emulated time each)\n' % (a.thread, total))
    for function, count in self_count.most_common(a.top):
        print('%6.2f%% %7d  %s' % (100.0 * count / total, count, function))
        if a.callers:
            for caller, n in callers[function].most_common(a.callers):
                print('            %5.1f%%  from %s' % (100.0 * n / count, caller))


if __name__ == '__main__':
    main()
