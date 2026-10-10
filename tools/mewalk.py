#!/usr/bin/env python3
"""mewalk.py - the code a Media Engine job can reach, from a disassembly
(psp-objdump -d halo.elf), and what in it the ME can't run. Job code may do
integer and FPU work only: no VFPU, no system calls, no library calls
(media_engine.md).

usage: python3 mewalk.py halo.dis [root ...] [--summary]
  root       the functions a job starts from (default meLibOnProcess, the
             ME's job loop, as build 87 checked it)
  --summary  per kind of problem, the functions that have it, each with one
             call chain from a root, instead of every flagged line

Flagged:
  syscall        a system call; imported firmware functions are stubs
                 holding one, so calling them is flagged too
  VFPU           a VFPU instruction (the ME has none)
  indirect call  jalr: a call through a pointer, which the walk can't follow,
                 so whatever it reaches is unknown
  indirect jump  jr other than a return, usually a switch's jump table inside
                 the same function (harmless to the walk)
The walk follows jal, j and bal to named functions."""
import re, sys
from collections import deque

args = [a for a in sys.argv[1:] if not a.startswith('--')]
summary = '--summary' in sys.argv
roots = args[1:] or ['meLibOnProcess']

funcs, starts = {}, {}
cur = None
for line in open(args[0], errors='replace'):
    line = line.rstrip('\n')
    m = re.match(r'^([0-9a-f]+) <(.+)>:', line)
    if m:
        cur = m.group(2)
        funcs[cur] = []
        starts[cur] = int(m.group(1), 16)
        continue
    if cur:
        funcs[cur].append(line)

CALL = re.compile(r'\t(jal|j|bal)\s+[0-9a-f]+ <([^>+]+)')
VFPU = re.compile(r'\t(v[a-z0-9]+\.[spqt]\b|lv\.|sv\.|lvl\.|lvr\.|svl\.|svr\.|ulv\.|usv\.|mtv|mfv|vsync|bvf|bvt)')
KINDS = [('syscall', re.compile(r'\tsyscall\b')), ('VFPU', VFPU),
         ('indirect call', re.compile(r'\tjalr\b')), ('indirect jump', re.compile(r'\tjr\s+(?!ra\b)'))]

parent = {r: None for r in roots}
order, seen, missing = [], set(), set()
queue = deque(roots)
while queue:
    f = queue.popleft()
    if f in seen:
        continue
    if f not in funcs:
        missing.add(f)
        continue
    seen.add(f)
    order.append(f)
    for line in funcs[f]:
        m = CALL.search(line)
        if m and m.group(2) != f and m.group(2) not in parent:
            parent[m.group(2)] = f
            queue.append(m.group(2))

def chain(f):
    path = []
    while f is not None:
        path.append(f)
        f = parent.get(f)
    return ' <- '.join(path)

sorted_starts = sorted(starts.values())
def size_of(f):
    i = sorted_starts.index(starts[f])
    return sorted_starts[i + 1] - starts[f] if i + 1 < len(sorted_starts) else 0

found = {kind: {} for kind, _ in KINDS}
lines_flagged = 0
for f in order:
    for line in funcs[f]:
        for kind, pattern in KINDS:
            if pattern.search(line):
                found[kind][f] = found[kind].get(f, 0) + 1
                lines_flagged += 1
                if not summary:
                    print("CHECK", kind, f, line.strip())

if not summary:
    print("reached:", sorted(order))
    print("flagged:", lines_flagged)
    sys.exit(0)

print("roots: %s" % ' '.join(roots))
print("reached: %d functions, %d KB of code" % (len(order), sum(size_of(f) for f in order) // 1024))
if missing:
    print("not in the disassembly: %s" % ' '.join(sorted(missing)))
for kind, _ in KINDS:
    where = found[kind]
    print("\n%s: %d sites in %d functions" % (kind, sum(where.values()), len(where)))
    for f, n in sorted(where.items(), key=lambda x: -x[1])[:40]:
        print("  %-40s %4d   %s" % (f[:40], n, chain(f)))
    if len(where) > 40:
        print("  ... and %d more functions" % (len(where) - 40))
