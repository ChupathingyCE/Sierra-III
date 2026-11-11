#!/usr/bin/env python3
"""CAP1 captures (psp/src/psp_cap1.h; bench/halo_box/README.md): look into one, or
make the self-test box's.

  cap1.py info FILE [--ticks]        the header, the static records, each tick's size
  cap1.py selftest FILE [--ticks N] [--entities N] [--wrong TICK:ENTITY]
                                     a capture for bench/halo_box's selftest box
                                     (numbers only); --wrong spoils one output, to
                                     show the harness's comparison catches it

A capture of the game is game data: it stays out of this repository.
"""
import argparse
import random
import struct
import sys

HEADER = struct.Struct("<II8s8s20sIIIII")
STATIC = struct.Struct("<II")
TICK = struct.Struct("<IIII")
ENTITY = struct.Struct("<IHHII")
MAGIC, TICK_MAGIC = 0x31504143, 0x4B434954


def pad4(b):
    return b + b"\0" * (-len(b) % 4)


def write(path, box, game_map, bench, commit, tick_first, statics, ticks):
    """statics: [(key, bytes)]; ticks: [(tick, [(id, kind, flags, input, output)])]"""
    static = b"".join(STATIC.pack(key, len(b)) + pad4(b) for key, b in statics)
    out = bytearray(HEADER.pack(MAGIC, 1, box.encode(), game_map.encode(), bench.encode(), commit, tick_first, len(ticks),
                                sum(len(e) for t, e in ticks), len(static)))
    out += static
    for tick, entities in ticks:
        out += b"\0" * (-len(out) % 64)
        body = b"".join(ENTITY.pack(i, kind, flags, len(a), len(b)) + pad4(a) + pad4(b) for i, kind, flags, a, b in entities)
        out += TICK.pack(TICK_MAGIC, tick, len(entities), len(body)) + body
    open(path, "wb").write(out)
    return len(out)


def read(path):
    data = open(path, "rb").read()
    magic, version, box, game_map, bench, commit, tick_first, ticks, entities, static_bytes = HEADER.unpack_from(data, 0)
    if magic != MAGIC or version != 1:
        sys.exit("%s: not a CAP1 version 1 file" % path)
    at, statics = HEADER.size, []
    while at < HEADER.size + static_bytes:
        key, length = STATIC.unpack_from(data, at)
        statics.append((key, data[at + 8:at + 8 + length]))
        at += 8 + length + (-length % 4)
    sections = []
    for _ in range(ticks):
        at += -at % 64
        magic, tick, count, size = TICK.unpack_from(data, at)
        if magic != TICK_MAGIC:
            sys.exit("%s: no tick section at %d" % (path, at))
        body, records, p = data[at + 16:at + 16 + size], [], 0
        for _ in range(count):
            i, kind, flags, a, b = ENTITY.unpack_from(body, p)
            p += ENTITY.size
            records.append((i, kind, flags, body[p:p + a], body[p + a + (-a % 4):p + a + (-a % 4) + b]))
            p += a + (-a % 4) + b + (-b % 4)
        sections.append((tick, records))
        at += 16 + size
    name = lambda b: b.split(b"\0")[0].decode("ascii", "replace")
    return dict(box=name(box), map=name(game_map), bench=name(bench), commit=commit, tick_first=tick_first, entities=entities,
                statics=statics, ticks=sections, bytes=len(data))


def info(args):
    c = read(args.file)
    print("%s: box %s, map %s, bench %s, build %08x, from tick %d: %d ticks, %d entities, %d bytes" % (
        args.file, c["box"], c["map"], c["bench"], c["commit"], c["tick_first"], len(c["ticks"]), c["entities"], c["bytes"]))
    print("statics: %d records, %d bytes" % (len(c["statics"]), sum(len(b) for k, b in c["statics"])))
    sizes = [sum(ENTITY.size + len(a) + len(b) for i, k, f, a, b in e) for t, e in c["ticks"]]
    if sizes:
        print("a tick: %d entities and %d bytes on average, %d bytes at most; inputs %d and outputs %d bytes in all" % (
            c["entities"] / len(sizes), sum(sizes) / len(sizes), max(sizes),
            sum(len(a) for t, e in c["ticks"] for i, k, f, a, b in e), sum(len(b) for t, e in c["ticks"] for i, k, f, a, b in e)))
    kinds = {}
    for t, e in c["ticks"]:
        for i, k, f, a, b in e:
            kinds[k] = kinds.get(k, 0) + 1
    print("kinds: " + ", ".join("%d: %d" % kv for kv in sorted(kinds.items())))
    if args.ticks:
        for (t, e), size in zip(c["ticks"], sizes):
            print("  tick %d: %d entities, %d bytes" % (t, len(e), size))


def selftest(args):
    rng = random.Random(1)
    f32 = lambda values: struct.pack("<%df" % len(values), *values)
    tables = [(100 + k, [rng.uniform(-4, 4) for _ in range(16 + 8 * k)]) for k in range(4)]
    wrong = tuple(int(x) for x in args.wrong.split(":")) if args.wrong else None
    ticks = []
    for t in range(args.ticks):
        entities = []
        for i in range(args.entities):
            key, table = tables[rng.randrange(4)]
            kind = rng.randrange(2)
            values = [rng.uniform(-100, 100) for _ in table]
            # the box works in single precision: so does the capture's "old code"
            single = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
            result = [single(single(single(v) * single(m)) + (1.0 if kind == 1 else 0.0)) for v, m in zip(values, table)]
            if wrong == (t, i):
                result[3] += 0.5
            entities.append((1000 + i, kind, 0, struct.pack("<I", key) + f32(values), f32(result)))
        ticks.append((30 * t, entities))
    size = write(args.file, "selftest", "none", "selftest", 0, 0, [(k, f32(v)) for k, v in tables], ticks)
    print("%s: %d ticks of %d entities, %d bytes%s" % (args.file, args.ticks, args.entities, size,
                                                       "; tick %d entity %d spoiled" % wrong if wrong else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("info")
    p.add_argument("file")
    p.add_argument("--ticks", action="store_true")
    p.set_defaults(run=info)
    p = sub.add_parser("selftest")
    p.add_argument("file")
    p.add_argument("--ticks", type=int, default=32)
    p.add_argument("--entities", type=int, default=40)
    p.add_argument("--wrong")
    p.set_defaults(run=selftest)
    args = ap.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
