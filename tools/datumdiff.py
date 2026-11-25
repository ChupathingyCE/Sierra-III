"""datumdiff.py - two runs' whole objects compared, word by word (build 133).

With statecrc, "stateobjectsN statefullall" logs every object's whole datum at
tick N (psp_fixups.py, objects.c's psp_objects_play_dump). Two runs whose
objects allocation parts while their play stays the same (statecmp.py's
verdict) say here which fields: for each object that differs, its words that
differ (byte offset, A's value, B's), and a count by definition and offset.
psp_offsets.py names an offset.

usage: python3 datumdiff.py A/log.txt B/log.txt [objects to list, default 40]"""
import re
import sys
from collections import defaultdict

DATUM = re.compile(r"state: datum of object ([0-9a-f]{8}) at tick (\d+), word (\d+):((?: [0-9a-f]{8})+)")
DEFN = re.compile(r"state: definition ([0-9a-f]{8}) is (.*)")
# each object: index, definition, play hash, whole hash (psp_fixups.py, objects.c's psp_objects_play_dump)
OBJS = re.compile(r"state: objects at tick (\d+):((?: [0-9a-f]{8} [0-9a-f]{8} [0-9a-f]{8} [0-9a-f]{8})+)")


def load(path):
    datums = defaultdict(dict)
    names, defs = {}, {}
    with open(path, errors="replace") as f:
        for line in f:
            m = DATUM.search(line)
            if m:
                index, word = m.group(1), int(m.group(3))
                for k, w in enumerate(m.group(4).split()):
                    datums[index][word + k] = int(w, 16)
                continue
            m = DEFN.search(line)
            if m:
                names[m.group(1)] = m.group(2).strip()
                continue
            m = OBJS.search(line)
            if m:
                parts = m.group(2).split()
                for k in range(0, len(parts), 4):
                    defs[parts[k]] = parts[k + 1]
    return datums, names, defs


def main():
    a, b = sys.argv[1], sys.argv[2]
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    da, na, fa = load(a)
    db, nb, fb = load(b)
    names = {**na, **nb}
    defs = {**fa, **fb}
    only_a = sorted(set(da) - set(db))
    only_b = sorted(set(db) - set(da))
    print(f"objects dumped: {len(da)} and {len(db)}; only in A {len(only_a)}, only in B {len(only_b)}")
    shown = 0
    by_word = defaultdict(int)
    differing = 0
    for index in sorted(set(da) & set(db)):
        wa, wb = da[index], db[index]
        words = sorted(w for w in set(wa) | set(wb) if wa.get(w) != wb.get(w))
        if not words:
            continue
        differing += 1
        name = names.get(defs.get(index, ""), defs.get(index, "?"))
        for w in words:
            by_word[(name, w)] += 1
        if shown < limit:
            shown += 1
            text = ", ".join(f"+0x{w * 4:x} {wa.get(w, 0):08x}/{wb.get(w, 0):08x}" for w in words[:12])
            more = f" (+{len(words) - 12} more)" if len(words) > 12 else ""
            print(f"{index} {name}: {len(words)} words: {text}{more}")
    print(f"objects differing: {differing}")
    print("by definition and offset (count of objects):")
    for (name, w), n in sorted(by_word.items(), key=lambda kv: (-kv[1], kv[0]))[:40]:
        print(f"  {name} +0x{w * 4:x}: {n}")


if __name__ == "__main__":
    main()
