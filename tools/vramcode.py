#!/usr/bin/env python3
"""VRAMCODE's table (docs/vramcode.md): which of the main CPU's hot functions are
copied into a room in VRAM, where each goes, the patches the copy needs, and
the calls in main RAM re-aimed at the copies, written into the linked ELF's
psp_vramcode_table before psp-prxgen.

  tools/vramcode.py <halo.elf> <list.txt> <me.txt>        write the table
  tools/vramcode.py --dry <halo.elf> <list.txt> <me.txt>  only say what it would write

list.txt holds one function name a line (# starts a comment), the most worth
copying first, and '# @room N' (KB; 64 if none). The functions are taken in
order while the room holds them; one that isn't in the program, whose name two
functions share, or that no call can enter is left out with a line saying so.

me.txt names the functions the Media Engine runs (from the cache model's ME
side and MEXEC's lists). The ME can't see VRAM: at VRAM's addresses it reads
memory of its own (hardware_facts.md, VRAM). So no call the ME can make is
ever re-aimed: the tool adds to me.txt's set everything it reaches by a direct
call (R_MIPS_26) or a branch out of a function (R_MIPS_PC16), one step out
(HOPS), and a function's .cold part goes with it. A copy is then entered only by:
  - a call (jal, or a tail call's j) in main RAM, from a function outside the
    ME's set, to the copied function's first word: re-aimed at the copy (jal
    keeps the PC's top four bits, and VRAM's 0x04xxxxxx and main RAM's
    0x08xxxxxx share them);
  - a call between copies (as tools/mexec.py re-aims them).
The originals stay whole: the ME, function pointers and the calls left alone
reach them, correct and slower. The copy itself is made as MEXEC's is
(tools/mexec.py): branches out of a copied function through trampolines in
the copy, jump tables copied with the copy's addresses and their lui/%lo
pairs re-aimed.

The room's place is fixed: VRAM 0xCC000, the motion sensor's 16 KB that
blipdepth frees (on by default), the texture cache starting after the room
(psp_tex.c). The runtime refuses the copy where blipdepth is off.

The table (32-bit words):
  0 magic 'VRMC', 1 version, 2 psp_vramcode_install's link address (the
    runtime takes the load base from its own address of it less this),
  3 copy bytes (code, trampolines, jump tables), 4 room bytes (the copy
    rounded up to 4 KB), 5 the copy's VRAM address, 6 functions, 7 calls
    between copies, 8 trampolines, 9 branch patches, 10 immediate patches,
    11 jump tables copied, 12 the trampolines' place, 13 caller patches,
    14 jump-table words left in main RAM, 15 a sum of the copy's code words
    as linked;
  then per function: link address, bytes, place in the copy;
  per call between copies: the word's place, the target's place;
  per trampoline: 1 if its target is a place in the copy else 0, the target;
  per branch patch: the word's place, the trampoline's number;
  per immediate patch: the word's place, its new low 16 bits;
  per jump table: its place, its words, then the words (VRAM addresses);
  per caller patch, one word: the call's link address / 4 in bits 0-19, the
    function's number in bits 20-29.
"""
import os
import struct
import sys
from bisect import bisect_right

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mexec import Elf, sext16, R_MIPS_26, R_MIPS_PC16, R_MIPS_32, R_MIPS_HI16, R_MIPS_LO16, STT_FUNC, STT_OBJECT  # noqa: E402

MAGIC = 0x56524D43  # 'VRMC'
VERSION = 1
HEADER_WORDS = 16
VRAM_AT = 0x040CC000  # psp_vramcode.c's VRAMCODE_AT
ALIGN = 16
# how far past the functions the ME was seen to run the set reaches by direct
# calls: the whole closure (every error path's callees, the main loop among
# them) took 3,082 functions and left the hot ones nothing to enter by
HOPS = 1


def base_name(name):
    return name.split(".cold")[0]


def build(elf, names, room_kb, me_names, log):
    funcs = sorted((s for s in elf.symbols if s["type"] == STT_FUNC and s["size"]), key=lambda s: s["value"])
    starts = [f["value"] for f in funcs]
    by_name = {}
    for f in funcs:
        by_name.setdefault(f["name"], []).append(f)

    def owner(addr):
        k = bisect_right(starts, addr) - 1
        if k >= 0 and addr < funcs[k]["value"] + funcs[k]["size"]:
            return funcs[k]
        return None

    text = elf.relocations(".text")
    out_edges = {}
    for site, typ in text:
        f = owner(site)
        if f is None:
            continue
        w = elf.word(site)
        if typ == R_MIPS_26:
            target = ((site + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
        elif typ == R_MIPS_PC16:
            target = site + 4 + (sext16(w & 0xFFFF) << 2)
        else:
            continue
        g = owner(target)
        if g is not None and g is not f:
            out_edges.setdefault(f["value"], set()).add(g["value"])

    # the ME's set: its seeds, their .cold parts, and everything they reach
    seeds = set(me_names) | {base_name(n) for n in me_names}
    me = {f["value"] for f in funcs if f["name"] in seeds or base_name(f["name"]) in seeds}
    for _ in range(HOPS):
        me |= {b for a in list(me) for b in out_edges.get(a, ())}
    for f in funcs:  # a .cold part of a function in the set
        if f["value"] not in me and base_name(f["name"]) != f["name"]:
            parent = by_name.get(base_name(f["name"]), [])
            if any(p["value"] in me for p in parent):
                me.add(f["value"])
    log("vramcode: the ME's set: %d functions (%d named in its list)" % (len(me), len(seeds)))

    # entries: who calls each function's first word, from outside the ME's set
    entry_sites = {}
    for site, typ in text:
        if typ != R_MIPS_26:
            continue
        f = owner(site)
        if f is None or f["value"] in me:
            continue
        w = elf.word(site)
        target = ((site + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
        g = owner(target)
        if g is not None and target == g["value"]:
            entry_sites.setdefault(target, []).append(site)

    room = room_kb * 1024
    chosen, place, at = [], {}, 0
    for name in names:
        found = by_name.get(name, [])
        if len(found) != 1:
            log("vramcode: %s left out: %s" % (name, "not in the program" if not found else
                                                 "%d functions share the name" % len(found)))
            continue
        f = found[0]
        if f["value"] in place:
            continue
        if not entry_sites.get(f["value"]):
            log("vramcode: %s left out: no call outside the ME's set enters it" % name)
            continue
        start = (at + ALIGN - 1) // ALIGN * ALIGN
        # leave a sixteenth of the room for trampolines and jump tables
        if start + f["size"] > room - room // 16:
            continue
        place[f["value"]] = start
        chosen.append(f)
        at = start + f["size"]
    code_bytes = at
    index = {f["value"]: k for k, f in enumerate(chosen)}

    def copy_place(target):
        g = owner(target)
        if g is not None and g["value"] in place:
            return place[g["value"]] + target - g["value"]
        return None

    calls, branches, trampolines, tramp_of, callers = [], [], [], {}, []
    for site, typ in text:
        f = owner(site)
        if f is None:
            continue
        w = elf.word(site)
        if f["value"] in place:
            site_place = place[f["value"]] + site - f["value"]
            if typ == R_MIPS_26:
                target = ((site + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                p = copy_place(target)
                if p is not None:
                    calls.append((site_place, p))
            elif typ == R_MIPS_PC16:
                target = site + 4 + (sext16(w & 0xFFFF) << 2)
                if f["value"] <= target < f["value"] + f["size"]:
                    continue
                p = copy_place(target)
                key = (1, p) if p is not None else (0, target)
                if key not in tramp_of:
                    tramp_of[key] = len(trampolines)
                    trampolines.append(key)
                branches.append((site_place, tramp_of[key]))
        if typ == R_MIPS_26 and f["value"] not in me:
            target = ((site + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
            if target in index:
                callers.append((site, index[target]))
    tramp_at = (code_bytes + 15) // 16 * 16
    total = tramp_at + 8 * len(trampolines)
    for site_place, k in branches:
        reach = tramp_at + 8 * k - (site_place + 4)
        if not -0x20000 <= reach < 0x20000:
            sys.exit("vramcode: a branch at copy %#x can't reach its trampoline" % site_place)

    def chosen_owner(addr):
        g = owner(addr)
        return g if g is not None and g["value"] in place else None

    rod = sorted((site, chosen_owner(elf.word(site)), elf.word(site)) for site, typ in elf.relocations(".rodata")
                 if typ == R_MIPS_32 and chosen_owner(elf.word(site)) is not None)
    tables, run = [], []
    for site, f, target in rod:
        if run and (site != run[-1][0] + 4 or f is not run[-1][1]):
            tables.append(run)
            run = []
        run.append((site, f, target))
    if run:
        tables.append(run)
    text_relocs = {}
    for site, typ in text:
        f = owner(site)
        if f is not None and f["value"] in place and typ in (R_MIPS_HI16, R_MIPS_LO16):
            text_relocs.setdefault(f["value"], []).append((site, typ))
    imm_patches, data_blocks, jump_left = [], [], 0
    data_at = total
    for run in tables:
        start, f = run[0][0], run[0][1]
        pairs = []
        rel = sorted(text_relocs.get(f["value"], []))
        for k, (h, typ) in enumerate(rel):
            if typ != R_MIPS_HI16:
                continue
            hw = elf.word(h)
            rt = (hw >> 16) & 31
            hi = hw & 0xFFFF
            for l, typ2 in rel[k + 1:]:
                if l > h + 32:
                    break
                lw_ = elf.word(l)
                if typ2 != R_MIPS_LO16 or (lw_ >> 21) & 31 != rt:
                    continue
                if ((hi << 16) + sext16(lw_ & 0xFFFF)) & 0xFFFFFFFF == start:
                    pairs.append((h, l))
                    break
        if not pairs:
            jump_left += len(run)
            continue
        new = VRAM_AT + data_at
        for h, l in pairs:
            imm_patches.append((place[f["value"]] + h - f["value"], ((new + 0x8000) >> 16) & 0xFFFF))
            imm_patches.append((place[f["value"]] + l - f["value"], new & 0xFFFF))
        data_blocks.append((data_at, [VRAM_AT + place[g["value"]] + t - g["value"] for _, g, t in run]))
        data_at += 4 * len(run)
    total = data_at
    room_bytes = (total + 4095) // 4096 * 4096
    if total > room:
        sys.exit("vramcode: the copy is %d bytes, over its room's %d" % (total, room))
    linked_sum = 0
    for f in chosen:
        for k in range(f["size"] // 4):
            linked_sum = (linked_sum + elf.word(f["value"] + 4 * k)) & 0xFFFFFFFF
    install = [s for s in elf.symbols if s["name"] == "psp_vramcode_install" and s["type"] == STT_FUNC]
    if len(install) != 1:
        sys.exit("vramcode: psp_vramcode_install isn't in the program (built without PSP_VRAMCODE?)")
    words = [MAGIC, VERSION, install[0]["value"], total, room_bytes, VRAM_AT, len(chosen), len(calls),
             len(trampolines), len(branches), len(imm_patches), len(data_blocks), tramp_at, len(callers), jump_left,
             linked_sum]
    for f in chosen:
        words += [f["value"], f["size"], place[f["value"]]]
    for site_place, p in calls:
        words += [site_place, p]
    for kind, value in trampolines:
        words += [kind, value]
    for site_place, k in branches:
        words += [site_place, k]
    for site_place, imm in imm_patches:
        words += [site_place, imm]
    for at, block in data_blocks:
        words += [at, len(block)] + block
    for site, k in callers:
        if site >= 1 << 22 or k >= 1 << 10:
            sys.exit("vramcode: a caller patch doesn't pack (site %#x, function %d)" % (site, k))
        words.append((site >> 2) | (k << 20))
    entered = {}
    for _, k in callers:
        entered[k] = entered.get(k, 0) + 1
    for k, f in enumerate(chosen):
        log("vramcode: %08x %6d %-48s at VRAM %08x, %d calls re-aimed" % (
            f["value"], f["size"], f["name"], VRAM_AT + place[f["value"]], entered.get(k, 0)))
    log("vramcode: %d functions, %d bytes of code, %d trampolines (%d bytes in all, room %d KB of %d); "
        "%d calls between copies, %d branches through trampolines, %d calls in main RAM re-aimed; "
        "jump tables: %d copied (%d immediates), %d words left in main RAM" % (
            len(chosen), code_bytes, len(trampolines), total, room_bytes // 1024, room_kb, len(calls), len(branches),
            len(callers), len(data_blocks), len(imm_patches), jump_left))
    return words


def main(argv):
    dry = "--dry" in argv
    args = [a for a in argv if a != "--dry"]
    if len(args) != 3:
        sys.exit(__doc__)
    elf = Elf(args[0])
    names, room_kb = [], 64
    for line in open(args[1]):
        if line.strip().startswith("# @room"):
            room_kb = int(line.split()[2])
        n = line.split("#")[0].strip()
        if n:
            names.append(n)
    me_names = [l.split("#")[0].strip() for l in open(args[2])]
    me_names = [n for n in me_names if n]
    words = build(elf, names, room_kb, me_names, print)
    table = [s for s in elf.symbols if s["name"] == "psp_vramcode_table" and s["type"] == STT_OBJECT]
    if len(table) != 1:
        sys.exit("vramcode: psp_vramcode_table isn't in the program (built without PSP_VRAMCODE?)")
    t = table[0]
    if 4 * len(words) > t["size"]:
        sys.exit("vramcode: the table is %d words, over psp_vramcode_table's %d" % (len(words), t["size"] // 4))
    s = elf.section_at(t["value"])
    if s is None:
        sys.exit("vramcode: psp_vramcode_table has no bytes in the file (in .bss?)")
    print("vramcode: the table %d words of %d" % (len(words), t["size"] // 4))
    if dry:
        return
    struct.pack_into("<%dI" % len(words), elf.data, s["offset"] + t["value"] - s["addr"], *words)
    open(elf.path, "wb").write(elf.data)


if __name__ == "__main__":
    main(sys.argv[1:])
