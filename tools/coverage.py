#!/usr/bin/env python3
"""Which of the program's code ever ran, and which data only unrun code uses.

Joins the emulator's block coverage (PSP_COVERAGE=<file>, a local PPSSPP
patch: tools/ppsspp_local_patches.diff) with the link map (psp_build.py
writes OUT/halo.map every link) and the object files' relocations, and
prints, per source directory and per object file:

  code        bytes of functions that ran in a campaign run, ran only in a
              multiplayer run, or never ran in any run given
  data        bytes of .rodata, .data and .bss, split the same way by who
              uses them: a variable is "used" when a function that ran
              refers to it, or a used variable does (pointer tables)

"Never ran" means not in these runs: a map, a vehicle, an error path or a
menu the runs didn't reach would run other code. The numbers say where to
look; cutting anything still needs the code read (docs/dead_code_inventory.md).
The map, the ELF and the object files (OUT/obj, which the map names) must be
of the build the runs ran.

usage: coverage.py [--map OUT/halo.map] [--elf OUT/halo.elf] [--top N]
                   [--csv FILE] KIND:FILE ...
   KIND is campaign or mp; FILE is one run's PSP_COVERAGE output, e.g.
   coverage.py campaign:/tmp/cov_b30.txt campaign:/tmp/cov_c20.txt mp:/tmp/cov_bg.txt
"""
import argparse, bisect, collections, os, re, subprocess, sys

PROGRAM_BASE = 0x08804000   # psp_build.py: where the kernel loads the module
OUT = os.environ.get("PSP_OUT", "/tmp/psp_halo")

# ---------------------------------------------------------------- link map

class Item:
    """One input section placed by the linker: a function (.text.<name>),
    a variable (.bss.<name>, .data.<name>, .rodata.<name>), a COMMON block,
    or a whole section of a library member."""
    __slots__ = ("section", "addr", "size", "obj", "kind", "name", "ran", "users", "merged")

    def __init__(self, section, addr, size, obj, kind):
        self.section, self.addr, self.size, self.obj, self.kind = section, addr, size, obj, kind
        self.name = section
        for prefix in (".text.unlikely.", ".text.startup.", ".text.hot.", ".text.",
                       ".rodata.", ".data.rel.ro.", ".data.rel.", ".data.", ".sdata.", ".bss.", ".sbss."):
            if section.startswith(prefix):
                self.name = section[len(prefix):]
                break
        self.merged = False   # strings and constants the linker merges across files
        self.ran = set()      # kinds of run ("campaign", "mp") that executed it (code)
        self.users = set()    # items that refer to it (data)

def output_kind(name):
    """The kind of an output section, from its name; None for sections that
    aren't loaded into the program's memory (debug, relocations, notes)."""
    if name.startswith((".text", ".init", ".fini", ".sceStub.text", "_me_section")):
        return "code"
    if name.startswith((".rodata", ".lib.", ".eh_frame", ".MIPS.abiflags", ".ctors", ".dtors")):
        return "rodata"
    if name.startswith((".data", ".sdata", ".tm_clone_table")):
        return "data"
    if name.startswith((".bss", ".sbss")):
        return "bss"
    return None

def read_map(path):
    """The input sections in address order, and each kind's real total (the
    output sections' sizes)."""
    items = []
    totals = collections.Counter()
    out_kind = None
    pending = None          # an input section whose address is on the next line
    started = False
    two = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
    one = re.compile(r"^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        if not started:
            started = line.startswith("Linker script and memory map")
            continue
        if line and not line.startswith(" "):           # an output section
            parts = line.split()
            out_kind = output_kind(parts[0])
            if out_kind and len(parts) >= 3 and parts[2].startswith("0x"):
                totals[out_kind] += int(parts[2], 16)
            pending = None
            continue
        if out_kind is None:
            continue
        if pending:
            m = two.match(line)
            pending_name, pending = pending, None
            if m:
                add(items, pending_name, m, 1, out_kind)
                continue
        m = one.match(line)
        if m and not m.group(1).startswith("*"):        # "*fill*", "*(.text ...)"
            add(items, m.group(1), m, 2, out_kind)
            continue
        if re.match(r"^ (\.\S+|COMMON)$", line):
            pending = line.strip()
    items.sort(key=lambda i: i.addr)
    return items, totals

def add(items, section, m, first, out_kind):
    addr, size = int(m.group(first), 16), int(m.group(first + 1), 16)
    if size:
        items.append(Item(section, addr, size, m.group(first + 2).strip(), out_kind))

# ---------------------------------------------------------------- coverage

def read_coverage(path):
    """Merged [start, end) ranges of executed code, in the ELF's addresses."""
    ranges = []
    for line in open(path):
        parts = line.split()
        if len(parts) == 2:
            start = int(parts[0], 16) - PROGRAM_BASE
            ranges.append((start, start + 4 * int(parts[1])))
    ranges.sort()
    merged = []
    for s, e in ranges:
        if merged and s <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], e)
        else:
            merged.append([s, e])
    return merged

def mark_ran(code, ranges, kind):
    starts = [i.addr for i in code]
    for s, e in ranges:
        k = max(bisect.bisect_right(starts, s) - 1, 0)
        while k < len(code) and code[k].addr < e:
            if code[k].addr + code[k].size > s:
                code[k].ran.add(kind)
            k += 1

# ---------------------------------------------------------------- references

def read_references(items, elf, totals):
    """Fills each item's users from the object files' relocations: the
    relocations of .text.f in x.o that name .bss.v (a static), v (a global)
    or a label in .rodata.f.str1.4 (a string) make f a user of that item.

    Also sizes the merged sections. The linker merges strings and constants
    (SHF_MERGE) of every file into one block and the map lists each file's
    section at its size before merging, so each one's size is scaled to its
    share of what the block really takes: an estimate, marked as one."""
    by_section = {(i.obj, i.section): i for i in items}
    starts = [i.addr for i in items]
    globals_ = {}
    for line in subprocess.run(["psp-nm", elf], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1].isupper():
            globals_[parts[2]] = int(parts[0], 16)

    def at(addr):
        k = bisect.bisect_right(starts, addr) - 1
        if k >= 0 and items[k].addr <= addr < items[k].addr + items[k].size:
            return items[k]
        return None

    header = re.compile(r"^\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([A-Z]*)\s+\d+\s+\d+\s+\d+$")
    symbol = re.compile(r"^\s*\d+:\s+[0-9a-f]+\s+\d+\s+\S+\s+\S+\s+\S+\s+(\S+)\s+(\S+)$")
    plain = sorted(set(i.obj for i in items if "(" not in i.obj and os.path.exists(i.obj)))
    for obj in plain:
        r = subprocess.run(["psp-readelf", "-S", "-s", "-r", "-W", obj], capture_output=True, text=True)
        sections, sizes, symbols, relocs = {}, {}, {}, []
        user = None
        for line in r.stdout.splitlines():
            m = header.match(line)
            if m:
                sections[m.group(1)] = m.group(2)
                if "M" in m.group(4):
                    sizes[m.group(2)] = int(m.group(3), 16)
                continue
            m = re.match(r"Relocation section '\.rel(\..*)' at", line)
            if m:
                user = by_section.get((obj, m.group(1)))
                continue
            m = symbol.match(line)
            if m:
                symbols[m.group(2)] = m.group(1)
                continue
            parts = line.split()
            if user is not None and len(parts) >= 5 and parts[2].startswith("R_"):
                relocs.append((user, parts[4]))
        for name, size in sizes.items():
            item = by_section.get((obj, name))
            if item is not None:
                item.merged, item.size = True, size
        for user, name in relocs:
            if name.startswith("."):
                target = by_section.get((obj, name))
            elif symbols.get(name, "UND").isdigit():
                target = by_section.get((obj, sections.get(symbols[name])))
            else:
                target = at(globals_[name]) if name in globals_ else None
            if target is not None and target is not user:
                target.users.add(user)

    for i in items:                     # library members aren't read: known by name
        if "(" in i.obj and re.search(r"\.(str|cst)\d", i.section):
            i.merged = True

    # the merged sections' estimate: their share of what's left of each kind
    for kind in ("rodata", "data"):
        merged = [i for i in items if i.kind == kind and i.merged]
        rest = sum(i.size for i in items if i.kind == kind and not i.merged)
        listed = sum(i.size for i in merged)
        if listed:
            scale = max(totals[kind] - rest, 0) / float(listed)
            for i in merged:
                i.size = int(round(i.size * scale))

# ---------------------------------------------------------------- report

def unit_of(obj):
    """An object's source directory: source/ai, port/linux, psp/src, or the
    library's name for an archive member."""
    m = re.match(r".*/lib([^/]+)\.a\(", obj)
    if m:
        return "lib" + m.group(1)
    rel = obj.replace(OUT + "/obj/", "")
    parts = rel.split("/")
    if rel.startswith("/") or len(parts) < 2:
        return "other"
    return "/".join(parts[:2]) if parts[0] in ("source", "port", "psp") else parts[0]

def status_of_data(items):
    """campaign / mp / unused / unknown for each data item: used by code that
    ran (in that kind of run), through pointer tables too."""
    status = {}
    for kind in ("campaign", "mp"):
        live = set()
        work = [i for i in items if i.kind == "code" and kind in i.ran]
        live.update(work)
        users_of = collections.defaultdict(set)
        for i in items:
            for u in i.users:
                users_of[u].add(i)
        while work:                        # through variables only: a call
            u = work.pop()                 # to a function doesn't run it
            for t in users_of[u]:
                if t.kind != "code" and t not in live:
                    live.add(t)
                    work.append(t)
        for i in items:
            if i.kind != "code" and i in live and i not in status:
                status[i] = kind
    for i in items:
        if i.kind != "code" and i not in status:
            status[i] = "unused" if i.users else "unknown"
    return status

def code_status(i):
    return "campaign" if "campaign" in i.ran else "mp" if "mp" in i.ran else "unused"

def kb(n):
    return "%7.1f" % (n / 1024.0)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", default=os.path.join(OUT, "halo.map"))
    ap.add_argument("--elf", default=os.path.join(OUT, "halo.elf"), help="the program the map is of (its globals)")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--csv", help="every item: kind, status, size, unit, object, name")
    ap.add_argument("--who", action="append", default=[], metavar="NAME",
                    help="who refers to NAME (a function, a variable, or a library member's file name): repeatable")
    ap.add_argument("runs", nargs="+", metavar="KIND:FILE")
    a = ap.parse_args()

    items, totals = read_map(a.map)
    code = [i for i in items if i.kind == "code"]
    for run in a.runs:
        kind, path = run.split(":", 1)
        if kind not in ("campaign", "mp"):
            sys.exit("coverage.py: run kind must be campaign or mp: " + run)
        mark_ran(code, read_coverage(path), kind)
    read_references(items, a.elf, totals)
    dstatus = status_of_data(items)
    status = {i: (code_status(i) if i.kind == "code" else dstatus[i]) for i in items}

    cols = ("campaign", "mp", "unused", "unknown")
    def table(key, title):
        rows = collections.defaultdict(lambda: collections.Counter())
        for i in items:
            rows[key(i)][(i.kind, status[i])] += i.size
        print("\n## %s (KB)\n" % title)
        print("%-34s %s | %s | %s" % ("", "  code: ran   mp-only   never",
                                      "data+bss: used   mp-only    unused   unknown", "never-run code + unused data"))
        total = collections.Counter()
        order = sorted(rows, key=lambda r: -sum(v for (k, s), v in rows[r].items() if s in ("unused",)))
        for r in order:
            c = rows[r]
            total.update(c)
            code_cols = [c[("code", s)] for s in ("campaign", "mp", "unused")]
            data_cols = [sum(c[(k, s)] for k in ("rodata", "data", "bss")) for s in cols]
            print("%-34s %s %s %s | %s %s %s %s | %s" % ((r[:34],) + tuple(kb(x) for x in code_cols + data_cols)
                                                        + (kb(code_cols[2] + data_cols[2]),)))
        code_cols = [total[("code", s)] for s in ("campaign", "mp", "unused")]
        data_cols = [sum(total[(k, s)] for k in ("rodata", "data", "bss")) for s in cols]
        print("%-34s %s %s %s | %s %s %s %s | %s" % (("all",) + tuple(kb(x) for x in code_cols + data_cols)
                                                    + (kb(code_cols[2] + data_cols[2]),)))

    print("# Coverage of %s, runs: %s" % (a.map, " ".join(a.runs)))
    print("\nprogram: " + ", ".join("%s %.1f KB" % (k, totals[k] / 1024.0) for k in ("code", "rodata", "data", "bss")) +
          " (merged strings and constants: each file's share estimated)")
    table(lambda i: unit_of(i.obj), "By source directory")
    short = lambda obj: obj.replace(OUT + "/obj/", "")
    by_file = collections.defaultdict(list)
    for i in items:
        by_file[i.obj].append(i)
    worst = sorted(by_file, key=lambda f: -sum(i.size for i in by_file[f] if status[i] == "unused"))[:a.top]
    print("\n## The %d files with the most never-run code and unused data (KB)\n" % len(worst))
    print("%-62s %8s %8s %8s %8s" % ("", "code ran", "never", "data", "unused"))
    for f in worst:
        c = collections.Counter()
        for i in by_file[f]:
            c[("code" if i.kind == "code" else "data", status[i])] += i.size
        print("%-62s %s %s %s %s" % (short(f)[-62:], kb(c[("code", "campaign")] + c[("code", "mp")]), kb(c[("code", "unused")]),
                                     kb(sum(v for (k, st), v in c.items() if k == "data")), kb(c[("data", "unused")])))
    print("\n## Files none of whose code ran (code KB, data KB)")
    idle = [f for f in by_file if sum(i.size for i in by_file[f]) >= 256
            and not any(i.kind == "code" and i.ran for i in by_file[f])]
    print("(256 bytes or more)")
    for f in sorted(idle, key=lambda f: -sum(i.size for i in by_file[f])):
        print("%s %s  %s" % (kb(sum(i.size for i in by_file[f] if i.kind == "code")),
                             kb(sum(i.size for i in by_file[f] if i.kind != "code")), short(f)))

    print("\n## Largest functions that never ran\n")
    for i in sorted((i for i in code if status[i] == "unused"), key=lambda i: -i.size)[:a.top]:
        print("%7d  %-48s %s" % (i.size, i.name, i.obj.replace(OUT + "/obj/", "")))
    print("\n## Largest functions that ran only in multiplayer\n")
    for i in sorted((i for i in code if status[i] == "mp"), key=lambda i: -i.size)[:a.top]:
        print("%7d  %-48s %s" % (i.size, i.name, i.obj.replace(OUT + "/obj/", "")))
    for want, title in (("unused", "used only by code that never ran"), ("mp", "used only in multiplayer")):
        print("\n## Largest variables %s\n" % title)
        for i in sorted((i for i in items if i.kind != "code" and status[i] == want), key=lambda i: -i.size)[:a.top]:
            print("%7d  %-6s %-41s %s" % (i.size, i.kind, i.name, i.obj.replace(OUT + "/obj/", "")))

    # a variable counts as used when any function that ran refers to it, and
    # that may only be its initializer clearing it: so the big ones are
    # listed with the functions that ran and refer to them, to be read
    print("\n## Variables of 8 KB or more that are used, and the functions that ran and use them\n")
    for i in sorted((i for i in items if i.kind != "code" and status[i] in ("campaign", "mp") and i.size >= 8192),
                    key=lambda i: -i.size):
        users = sorted(u.name for u in i.users if u.kind == "code" and u.ran)
        via = sorted(u.name for u in i.users if u.kind != "code" and status[u] in ("campaign", "mp"))
        print("%7d  %-6s %-36s %s" % (i.size, i.kind, i.name, i.obj.replace(OUT + "/obj/", "")))
        print("         ran: %s%s" % (", ".join(users[:12]) + (" (+%d)" % (len(users) - 12) if len(users) > 12 else ""),
                                     ("; through " + ", ".join(via[:4])) if via else ""))

    for name in a.who:
        found = [i for i in items if i.name == name or ("(" in i.obj and name in i.obj.split("(")[-1])]
        print("\n## Who refers to %s\n" % name)
        for i in found:
            print("%s %s (%d bytes, %s), %s" % (i.kind, i.name, i.size, status[i], i.obj.replace(OUT + "/obj/", "")))
            for u in sorted(i.users, key=lambda u: (status[u], u.name)):
                print("    %-9s %s  %s" % (status[u], u.name, u.obj.replace(OUT + "/obj/", "")))

    if a.csv:
        with open(a.csv, "w") as f:
            f.write("kind,status,size,unit,object,name\n")
            for i in items:
                f.write("%s,%s,%d,%s,%s,%s\n" % (i.kind, status[i], i.size, unit_of(i.obj),
                                                  i.obj.replace(OUT + "/obj/", ""), i.name))

if __name__ == "__main__":
    main()
