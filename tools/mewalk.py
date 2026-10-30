#!/usr/bin/env python3
"""mewalk.py - the code a Media Engine job can reach, from a disassembly
(psp-objdump -d halo.elf), and what in it the ME can't run. Job code may do
integer and FPU work only: no VFPU, no system calls, no library calls
(media_engine.md).

usage: python3 mewalk.py halo.dis [root ...] [--summary] [--each]
                         [--stop=f,g,...]
  root       the functions a job starts from (default meLibOnProcess, the
             ME's job loop, as build 87 checked it); f+g+h is one root of
             several functions (one walk, with --each too)
  --summary  per kind of problem, the functions that have it, each with one
             call chain from a root, instead of every flagged line
  --each     one walk and one report per root, instead of one walk from all
  --stop=    functions the walk doesn't enter: the boundary a job would
             handle another way (an assert that stops the job and lets the
             main CPU report it, a debug print left out). Each is listed with
             the call chain that reached it
  --calls=   functions whose call lines (jal, j, bal, jalr) are printed as
             they are in the disassembly: to see why a walk stops early
  --list=    name prefixes, comma-separated: every reached function whose
             name starts with one is listed with one call chain (to see which
             library code, libm's or libc's, a job reaches: the walk follows it
             like our own, so nothing flags it)
  --syms=    the symbol table (psp-objdump -t halo.elf): objdump starts a new
             block at every symbol, and some fall inside a function (labels the
             link keeps), which cut the function short and hid its calls; with
             the table, each such block is put back into the function holding
             it, and a call to it counts as a call to that function

Flagged:
  firmware call  a call into an imported firmware function (the stubs in
                 .sceStub.text: the loader turns each into a system call or a
                 jump into a module, so there is no syscall instruction to see
                 in the file). Locks, file reads, the GE's queue, the clock:
                 all firmware
  syscall        a syscall instruction in our own code (rare)
  VFPU           a VFPU instruction (the ME has none)
  indirect call  jalr: a call through a pointer, which the walk can't follow,
                 so whatever it reaches is unknown
  indirect jump  jr other than a return, usually a switch's jump table inside
                 the same function (harmless to the walk)
The walk follows jal, j and bal to named functions."""
import re, sys
from collections import deque

args = [a for a in sys.argv[1:] if not a.startswith('--')]
flags = [a for a in sys.argv[1:] if a.startswith('--')]
summary = '--summary' in flags
each = '--each' in flags
stops = set()
for f in flags:
    if f.startswith('--stop='):
        stops |= {s for s in f[len('--stop='):].split(',') if s}
roots = args[1:] or ['meLibOnProcess']
show_calls = []
list_prefixes = []
for f in flags:
    if f.startswith('--list='):
        list_prefixes += [c for c in f[len('--list='):].split(',') if c]
for f in flags:
    if f.startswith('--calls='):
        show_calls += [c for c in f[len('--calls='):].split(',') if c]

STUB_SECTION = '.sceStub.text'
funcs, starts, section_of = {}, {}, {}
cur, section = None, ''
for line in open(args[0], errors='replace'):
    line = line.rstrip('\n')
    m = re.match(r'^Disassembly of section (\S+):', line)
    if m:
        section, cur = m.group(1), None
        continue
    m = re.match(r'^([0-9a-f]+) <(.+)>:', line)
    if m:
        cur = m.group(2)
        funcs[cur] = []
        starts[cur] = int(m.group(1), 16)
        section_of[cur] = section
        continue
    if cur:
        funcs[cur].append(line)

# objdump's blocks back into whole functions, from the symbol table
syms_path = next((f[len('--syms='):] for f in flags if f.startswith('--syms=')), None)
owner_of = {}
if syms_path:
    fsyms = []
    for line in open(syms_path, errors='replace'):
        m = re.match(r'^([0-9a-f]+) (.{7}) (\S+)\s+([0-9a-f]+) (.+)$', line.rstrip('\n'))
        if m and 'F' in m.group(2) and int(m.group(4), 16) > 0:
            fsyms.append((int(m.group(1), 16), int(m.group(4), 16), m.group(5).split()[-1]))
    fsyms.sort()
    import bisect
    fstarts = [a for a, _, _ in fsyms]
    for name in list(funcs):
        i = bisect.bisect_right(fstarts, starts[name]) - 1
        if i < 0:
            continue
        address, size, fname = fsyms[i]
        if fname != name and fname in funcs and starts[name] < address + size and section_of[name] == section_of[fname]:
            owner_of[name] = fname
    for name in sorted(owner_of, key=lambda n: starts[n]):
        funcs[owner_of[name]].extend(funcs.pop(name))
    print("symbols: %d blocks put back into the functions holding them" % len(owner_of))

CALL = re.compile(r'\t(jal|j|bal)\s+[0-9a-f]+ <([^>+]+)')
VFPU = re.compile(r'\t(v[a-z0-9]+\.[spqt]\b|lv\.|sv\.|lvl\.|lvr\.|svl\.|svr\.|ulv\.|usv\.|mtv|mfv|vsync|bvf|bvt)')
LINE_KINDS = [('syscall', re.compile(r'\tsyscall\b')), ('VFPU', VFPU),
              ('indirect call', re.compile(r'\tjalr\b')), ('indirect jump', re.compile(r'\tjr\s+(?!ra\b)'))]
KINDS = ['firmware call'] + [k for k, _ in LINE_KINDS]

INSN = re.compile(r'^\s*[0-9a-f]+:\t')
def size_of(f):
    """Bytes of code: one instruction word a disassembled line."""
    return 4 * sum(1 for line in funcs[f] if INSN.match(line))

def walk(walk_roots):
    """Breadth first from the roots: each function's first caller is kept, so
    chain() gives one shortest call chain back to a root."""
    parent = {r: None for r in walk_roots}
    order, seen, missing, stopped = [], set(), set(), set()
    queue = deque(walk_roots)
    while queue:
        f = queue.popleft()
        if f in seen:
            continue
        if f in stops and parent.get(f) is not None:
            stopped.add(f)
            continue
        if f not in funcs:
            missing.add(f)
            continue
        seen.add(f)
        order.append(f)
        if section_of[f] == STUB_SECTION:
            continue  # a firmware import: nothing of ours behind it
        for line in funcs[f]:
            m = CALL.search(line)
            if not m:
                continue
            target = owner_of.get(m.group(2), m.group(2))
            if target != f and target not in parent:
                parent[target] = f
                queue.append(target)
    return parent, order, missing, stopped

def chain(parent, f):
    path = []
    while f is not None:
        path.append(f)
        f = parent.get(f)
    return ' <- '.join(path)

def report(walk_roots):
    parent, order, missing, stopped = walk(walk_roots)
    found = {kind: {} for kind in KINDS}
    for f in order:
        if section_of[f] == STUB_SECTION:
            found['firmware call'][f] = 1
            if not summary:
                print("CHECK firmware call", f, "<-", parent.get(f))
            continue
        for line in funcs[f]:
            for kind, pattern in LINE_KINDS:
                if pattern.search(line):
                    found[kind][f] = found[kind].get(f, 0) + 1
                    if not summary:
                        print("CHECK", kind, f, line.strip())
    ours = [f for f in order if section_of[f] != STUB_SECTION]
    if not summary:
        print("reached:", sorted(ours))
        print("flagged:", sum(sum(w.values()) for w in found.values()))
        return
    print("roots: %s" % ' '.join(walk_roots))
    print("reached: %d functions, %d KB of code" % (len(ours), sum(size_of(f) for f in ours) // 1024))
    if stops:
        print("stops: %s" % ' '.join(sorted(stops)))
    if missing:
        print("not in the disassembly: %s" % ' '.join(sorted(missing)))
    if stopped:
        print("\nstopped at: %d functions" % len(stopped))
        for f in sorted(stopped):
            print("  %-40s        %s" % (f[:40], chain(parent, f)))
    for kind in KINDS:
        where = found[kind]
        if kind == 'firmware call':
            print("\n%s: %d firmware functions" % (kind, len(where)))
        else:
            print("\n%s: %d sites in %d functions" % (kind, sum(where.values()), len(where)))
        for f, n in sorted(where.items(), key=lambda x: (-x[1], x[0]))[:40]:
            print("  %-40s %4d   %s" % (f[:40], n, chain(parent, f)))
        if len(where) > 40:
            print("  ... and %d more functions" % (len(where) - 40))
    if list_prefixes:
        listed = sorted(f for f in ours if any(f.startswith(p) for p in list_prefixes))
        print("\nlisted (%s): %d functions" % (','.join(list_prefixes), len(listed)))
        for f in listed:
            print("  %-40s %5d B   %s" % (f[:40], size_of(f), chain(parent, f)))

CALL_LINE = re.compile(r'\t(jal|j|bal|jalr)\s')
for f in show_calls:
    print("calls in %s (%s, %d bytes):" % (f, section_of.get(f, 'not in the disassembly'), size_of(f) if f in funcs else 0))
    for line in funcs.get(f, []):
        if CALL_LINE.search(line):
            print("  " + line.strip())
    print()

groups = [r.split('+') for r in roots]
if each:
    for i, group in enumerate(groups):
        if i:
            print("\n" + "=" * 78 + "\n")
        report(group)
else:
    report([f for group in groups for f in group])
