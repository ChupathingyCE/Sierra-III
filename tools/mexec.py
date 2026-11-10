#!/usr/bin/env python3
"""MEXEC's table (docs/mexec.md): which of the linked program's functions the
Media Engine copies into its eDRAM, where each goes, and every patch the copy
needs, written into the linked ELF's psp_mexec_table before psp-prxgen.

  tools/mexec.py <halo.elf> <list.txt>      write the table into the ELF
  tools/mexec.py --dry <halo.elf> <list.txt> only say what it would write

The list holds one function name a line (# starts a comment), the hottest
first; the first must be the ME's entry, psp_ge_me_replay. A function that
isn't in the program, or whose name two functions share, is left out with a
line saying so.

The ELF is the port's PRX (linked at 0 with -q, so every relocation the link
resolved is still listed). What the copy needs, from the relocations inside
each copied function:
  - R_MIPS_26 (jal, j): a target inside a copied function is re-aimed at its
    copy; any other is left as the loader relocates it (the ME runs code
    through the cached alias, and j keeps the PC's top bits, so a main-RAM
    target is reached from eDRAM as it is);
  - R_MIPS_PC16 (a branch out of the function: -freorder-blocks-and-partition's
    jumps to a .cold part): re-aimed at a trampoline in the copy, j target;
    nop, the target its copy if copied, else main RAM.
  - a jump table (a run of .rodata words naming the inside of one copied
    function): copied after the code with the copy's addresses, and the
    lui/%lo pair that loads its address re-aimed at the copy (a table whose
    pair isn't found stays in main RAM, its switch running on there).
Everything else (HI16/LO16 pairs naming code: function pointers) is left
alone: it reaches the originals in main RAM, correct and slower.

The table (32-bit words, all offsets in bytes):
  0 magic 'MEXC', 1 version, 2 the entry's link address (the ME takes the
    program's load base from its own address of psp_ge_me_replay less this),
  3 copy bytes (functions and trampolines), 4 functions, 5 call patches,
  6 trampolines, 7 branch patches, 8 immediate patches, 9 jump-table words
    left in main RAM, 10 jump tables copied, 11 the trampolines' place,
  12-15 0;
  then per function: link address, bytes, place in the copy;
  per call patch: the word's place in the copy, the target's place;
  per trampoline: 1 if its target is a place in the copy else 0, the target
    (a place, or a link address);
  per branch patch: the word's place in the copy, the trampoline's number;
  per immediate patch: the word's place, its new low 16 bits;
  per jump table: its place, its words, then the words (eDRAM addresses).
"""
import struct
import sys

MAGIC = 0x4D455843  # 'MEXC'
VERSION = 1
HEADER_WORDS = 16
ROOM = 128 * 1024   # docs/mexec.md: eDRAM 0x40000-0x60000
ALIGN = 64

R_MIPS_32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16, R_MIPS_PC16 = 2, 4, 5, 6, 10
EDRAM = 0x80040000  # the copy's address on the ME (docs/mexec.md)
SHT_SYMTAB, SHT_REL = 2, 9
STT_OBJECT, STT_FUNC = 1, 2


class Elf:
    def __init__(self, path):
        self.path = path
        self.data = bytearray(open(path, "rb").read())
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
            sys.exit("mexec: %s isn't a 32-bit little-endian ELF" % path)
        shoff, = struct.unpack_from("<I", d, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
        self.sections = []
        for k in range(shnum):
            name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(
                "<10I", d, shoff + k * shentsize)
            self.sections.append(dict(name=name, type=typ, addr=addr, offset=off, size=size, link=link,
                                      info=info, entsize=entsize))
        strtab = self.sections[shstrndx]
        for s in self.sections:
            s["name"] = self.cstr(strtab["offset"] + s["name"])
        self.symbols = []
        for s in self.sections:
            if s["type"] != SHT_SYMTAB:
                continue
            names = self.sections[s["link"]]
            for k in range(s["size"] // 16):
                name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", d, s["offset"] + k * 16)
                self.symbols.append(dict(name=self.cstr(names["offset"] + name), value=value, size=size,
                                         type=info & 15, shndx=shndx))

    def cstr(self, at):
        end = self.data.index(b"\0", at)
        return self.data[at:end].decode("latin-1")

    def section_at(self, addr):
        for s in self.sections:
            if s["addr"] <= addr < s["addr"] + s["size"] and s["type"] != 8 and s["addr"]:  # not NOBITS
                return s
        return None

    def word(self, addr):
        s = self.section_at(addr)
        return struct.unpack_from("<I", self.data, s["offset"] + addr - s["addr"])[0]

    def relocations(self, target_name):
        """(address, type) of every relocation applied to the section named"""
        out = []
        for s in self.sections:
            if s["type"] != SHT_REL or self.sections[s["info"]]["name"] != target_name:
                continue
            for k in range(s["size"] // 8):
                offset, info = struct.unpack_from("<II", self.data, s["offset"] + k * 8)
                out.append((offset, info & 255))
        return out


def sext16(v):
    return v - 0x10000 if v & 0x8000 else v


def build(elf, names, log):
    funcs = {}
    for sym in elf.symbols:
        if sym["type"] == STT_FUNC and sym["size"]:
            funcs.setdefault(sym["name"], []).append(sym)
    chosen = []
    for name in names:
        found = funcs.get(name, [])
        if len(found) != 1:
            log("mexec: %s left out: %s" % (name, "not in the program" if not found else
                                              "%d functions share the name" % len(found)))
            continue
        chosen.append(found[0])
    if not chosen or chosen[0]["name"] != "psp_ge_me_replay":
        sys.exit("mexec: the list's first function must be the ME's entry, psp_ge_me_replay, and be in the program")
    place, at = {}, 0
    for f in chosen:
        at = (at + ALIGN - 1) // ALIGN * ALIGN
        place[f["value"]] = at
        at += f["size"]
    code_bytes = at

    def copy_place(target):
        for f in chosen:
            if f["value"] <= target < f["value"] + f["size"]:
                return place[f["value"]] + target - f["value"]
        return None

    def owner(site):
        for f in chosen:
            if f["value"] <= site < f["value"] + f["size"]:
                return f
        return None

    calls, branches, trampolines, tramp_of = [], [], [], {}
    other_kinds = {}
    for site, typ in elf.relocations(".text"):
        f = owner(site)
        if f is None:
            continue
        w = elf.word(site)
        site_place = place[f["value"]] + site - f["value"]
        if typ == R_MIPS_26:
            target = ((site + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
            p = copy_place(target)
            if p is not None:
                calls.append((site_place, p))
        elif typ == R_MIPS_PC16:
            target = site + 4 + (sext16(w & 0xFFFF) << 2)
            if f["value"] <= target < f["value"] + f["size"]:
                continue  # inside its own function: moves with it
            p = copy_place(target)
            key = (1, p) if p is not None else (0, target)
            if key not in tramp_of:
                tramp_of[key] = len(trampolines)
                trampolines.append(key)
            branches.append((site_place, tramp_of[key]))
        else:
            other_kinds[typ] = other_kinds.get(typ, 0) + 1
    tramp_at = (code_bytes + 15) // 16 * 16
    total = tramp_at + 8 * len(trampolines)
    for site_place, k in branches:
        reach = tramp_at + 8 * k - (site_place + 4)
        if not -0x20000 <= reach < 0x20000:
            sys.exit("mexec: a branch at copy %#x can't reach its trampoline (%d bytes)" % (site_place, reach))
    # jump tables: runs of .rodata words naming the inside of one copied
    # function. Each is copied after the trampolines, its words the eDRAM
    # addresses of the copy, and the lui/lo pair in the function that loads
    # its address re-aimed at it (the pair found as the lui's register used as
    # the base of a %lo within 8 instructions, its address the table's). A
    # table whose pair can't be found that way stays in main RAM, and its
    # switch runs on in main RAM
    rod = sorted((site, owner(elf.word(site)), elf.word(site)) for site, typ in elf.relocations(".rodata")
                 if typ == R_MIPS_32 and owner(elf.word(site)) is not None)
    tables, run = [], []
    for site, f, target in rod:
        if run and (site != run[-1][0] + 4 or f is not run[-1][1]):
            tables.append(run)
            run = []
        run.append((site, f, target))
    if run:
        tables.append(run)
    text_relocs = {}
    for site, typ in elf.relocations(".text"):
        f = owner(site)
        if f is not None and typ in (R_MIPS_HI16, R_MIPS_LO16):
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
        new = EDRAM + data_at
        for h, l in pairs:
            imm_patches.append((place[f["value"]] + h - f["value"], ((new + 0x8000) >> 16) & 0xFFFF))
            imm_patches.append((place[f["value"]] + l - f["value"], new & 0xFFFF))
        data_blocks.append((data_at, [EDRAM + place[g["value"]] + t - g["value"] for _, g, t in run]))
        data_at += 4 * len(run)
    jump_words = sum(len(r) for r in tables)
    total = data_at
    if total > ROOM:
        sys.exit("mexec: the copy is %d bytes, over the %d of its room: shorten the list" % (total, ROOM))
    words = [MAGIC, VERSION, chosen[0]["value"], total, len(chosen), len(calls), len(trampolines), len(branches),
             len(imm_patches), jump_left, len(data_blocks), tramp_at] + [0] * (HEADER_WORDS - 12)
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
    log("mexec: %d functions, %d bytes of code, %d trampolines (%d bytes in all, of %d); %d calls re-aimed at copies, "
        "%d branches through trampolines; jump tables: %d of %d words copied (%d tables, %d immediates re-aimed), %d left in main RAM; "
        "other relocations left: %s" % (len(chosen), code_bytes, len(trampolines), total, ROOM, len(calls), len(branches),
                                        jump_words - jump_left, jump_words, len(data_blocks), len(imm_patches), jump_left, ", ".join("type %d x%d" % kv for kv in sorted(other_kinds.items())) or "none"))
    return words


def main(argv):
    dry = "--dry" in argv
    args = [a for a in argv if a != "--dry"]
    if len(args) != 2:
        sys.exit(__doc__)
    elf = Elf(args[0])
    names = [l.split("#")[0].strip() for l in open(args[1])]
    names = [n for n in names if n]
    words = build(elf, names, print)
    table = [s for s in elf.symbols if s["name"] == "psp_mexec_table" and s["type"] == STT_OBJECT]
    if len(table) != 1:
        sys.exit("mexec: psp_mexec_table isn't in the program (built without PSP_MEXEC?)")
    t = table[0]
    if 4 * len(words) > t["size"]:
        sys.exit("mexec: the table is %d words, over psp_mexec_table's %d" % (len(words), t["size"] // 4))
    s = elf.section_at(t["value"])
    if s is None:
        sys.exit("mexec: psp_mexec_table has no bytes in the file (in .bss?)")
    print("mexec: the table %d words of %d" % (len(words), t["size"] // 4))
    if dry:
        return
    struct.pack_into("<%dI" % len(words), elf.data, s["offset"] + t["value"] - s["addr"], *words)
    open(elf.path, "wb").write(elf.data)


if __name__ == "__main__":
    main(sys.argv[1:])
