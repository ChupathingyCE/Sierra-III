#!/usr/bin/env python3
"""CAP1 captures (psp/src/psp_cap1.h; bench/halo_box/README.md): look into one, or
make the self-test box's.

  cap1.py info FILE [--ticks]        the header, the static records, each tick's size
  cap1.py selftest FILE [--ticks N] [--entities N] [--wrong TICK:ENTITY]
                                     a capture for bench/halo_box's selftest box
                                     (numbers only); --wrong spoils one output, to
                                     show the harness's comparison catches it
  cap1.py posing FILE [--ticks N] [--entities N] [--wrong TICK:ENTITY]
                                     a made-up capture for the posing box (random
                                     models and poses, not the game's), its
                                     outputs worked out by the engine's own walk
                                     restated here (objects.c's node stack)

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


# ---- the posing box's made-up capture (docs/box_posing.md, "The capture";
# bench/halo_box/adapters/posing.c has the records). Its "old code" is the
# engine's object_compute_node_matrices restated: the node stack walk over the
# tag's links, matrix4x3_multiply term for term, in double precision (the box's
# tolerance covers the difference)

def m_mul(a, b):
    out = [a[0] * b[0]] + [0.0] * 12
    for row in range(4):
        for col in range(3):
            v = b[1 + row * 3] * a[1 + col] + b[2 + row * 3] * a[4 + col] + b[3 + row * 3] * a[7 + col]
            if row == 3:
                v = v * a[0] + a[10 + col]
            out[1 + row * 3 + col] = v
    return out


def m_from_orientation(o):
    i, j, k, w, tx, ty, tz, scale = o
    s = i * i + j * j + k * k + w * w
    s = 2.0 / s if s else 0.0
    x, y, z = s * i, s * j, s * k
    wx, wy, wz, xx, xy, xz, yy, yz, zz = w * x, w * y, w * z, i * x, i * y, i * z, j * y, j * z, k * z
    return [scale, 1 - (yy + zz), xy - wz, xz + wy, xy + wz, 1 - (xx + zz), yz - wx, xz - wy, yz + wx, 1 - (xx + yy), tx, ty, tz]


def m_translation(p):
    return [1.0, 1, 0, 0, 0, 1, 0, 0, 0, 1] + list(p)


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def engine_pose(links, obj, orientations):
    flags, pos, fwd, up, center, offset, boff, brad, scale, parent = obj
    n = len(links)
    nodes = [None] * max(n, 1)
    if flags & 32 or n == 0:
        root = [1.0] + list(fwd) + cross(up, fwd) + list(up) + list(pos)
        nodes[0] = root
    else:
        stack, top, filled = [0], 0, 1
        while True:
            at = stack[top]
            top += 1
            if at == 0:
                node = m_from_orientation(orientations[0])
                if flags & 1:
                    nodes[0] = node
                else:
                    t = m_translation(pos)
                    r = [1.0] + list(fwd) + cross(up, fwd) + list(up) + [0.0, 0.0, 0.0]
                    if flags & 2:
                        r[4:7] = [-v for v in r[4:7]]
                    if flags & 16:
                        r = m_mul(r, m_translation(center))
                    r = m_mul(r, m_translation(offset))
                    if flags & 4:
                        p = list(parent)
                        if p[0] != 1.0:
                            t[10:13] = [v * p[0] for v in t[10:13]]
                            p[0] = 1.0
                        if flags & 8:
                            p[4:7] = [-v for v in p[4:7]]
                        nodes[0] = m_mul(m_mul(m_mul(p, t), r), node)
                    else:
                        nodes[0] = m_mul(m_mul(t, r), node)
            else:
                par = links[at][0]
                if not 0 <= par < n:
                    par = 0
                nodes[at] = m_mul(nodes[par], m_from_orientation(orientations[at]))
            sib, child = links[at][2], links[at][1]
            if 0 <= sib < n and filled < 64:
                stack.append(sib)
                filled += 1
            if 0 <= child < n and filled < 64:
                stack.append(child)
                filled += 1
            if top == filled:
                break
    r = nodes[0]
    x, y, z = boff
    if r[0] != 1.0:
        x, y, z = x * r[0], y * r[0], z * r[0]
    sphere = [r[7 + k] * z + r[4 + k] * y + r[1 + k] * x + r[10 + k] for k in range(3)]
    sphere.append(brad * scale if scale > 0 else brad)
    out = []
    for m in nodes:
        out += m if m is not None else [0.0] * 13
    return out + sphere


def random_model(rng, count):
    """a tree of `count` nodes as the tag links it: parent, first child, next sibling"""
    parent = [-1] + [rng.randrange(k) for k in range(1, count)]
    children = [[c for c in range(count) if parent[c] == k] for k in range(count)]
    links = []
    for k in range(count):
        sibs = children[parent[k]] if k else []
        nxt = sibs[sibs.index(k) + 1] if k and sibs.index(k) + 1 < len(sibs) else -1
        links.append((parent[k], children[k][0] if children[k] else -1, nxt))
    return links


def unit(rng):
    v = [rng.gauss(0, 1) for _ in range(3)]
    n = sum(x * x for x in v) ** 0.5
    return [x / n for x in v]


def posing(args):
    rng = random.Random(2)
    models = [(200 + k, random_model(rng, c)) for k, c in enumerate([1, 3, 12, 19, 26, 40, 64])]
    wrong = tuple(int(x) for x in args.wrong.split(":")) if args.wrong else None
    statics = [(key, struct.pack("<HH", len(links), 0) + b"".join(struct.pack("<hhhh", p, c, s, 0) for p, c, s in links))
               for key, links in models]
    ticks = []
    for t in range(args.ticks):
        entities = []
        for i in range(args.entities):
            key, links = models[rng.randrange(len(models))]
            flags = rng.choice([0, 0, 0, 2, 4, 4 | 8, 16, 16 | 4, 1, 32])
            if flags & 32:
                links_used = []
            else:
                links_used = links
            fwd = unit(rng)
            side = unit(rng)
            up = cross(fwd, side)
            n = sum(x * x for x in up) ** 0.5
            up = [x / n for x in up]
            pos = [rng.uniform(-50, 50) for _ in range(3)]
            center = [rng.uniform(-0.5, 0.5) for _ in range(3)]
            offset = [rng.uniform(-0.3, 0.3) for _ in range(3)]
            boff = [rng.uniform(-1, 1) for _ in range(3)]
            brad, scale = rng.uniform(0.2, 3), rng.choice([0.0, 1.0, 1.2])
            pq = unit(rng) + [rng.uniform(-1, 1)]
            parent = m_from_orientation(pq + [rng.uniform(-20, 20) for _ in range(3)] + [rng.choice([1.0, 0.8])])
            orientations = []
            for _ in links_used:
                q = [rng.gauss(0, 1) for _ in range(4)]
                nq = sum(x * x for x in q) ** 0.5
                orientations.append([x / nq for x in q] + [rng.uniform(-0.5, 0.5) for _ in range(3)] + [rng.choice([1.0, 1.0, 0.9])])
            obj = (flags, pos, fwd, up, center, offset, boff, brad, scale, parent)
            out = engine_pose(links_used, obj, orientations)
            if wrong == (t, i):
                out[17] += 0.5
            inp = struct.pack("<II", key if links_used else 0, flags) + struct.pack(
                "<%df" % 33, *(pos + fwd + up + center + offset + boff + [brad, scale] + parent)) + struct.pack(
                "<I", len(links_used)) + b"".join(struct.pack("<8f", *o) for o in orientations)
            kind = rng.randrange(12)
            entities.append((5000 + i, kind, 0, inp, struct.pack("<%df" % len(out), *out)))
        ticks.append((30 * t, entities))
    size = write(args.file, "posing", "none", "made-up", 0, 0, statics, ticks)
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
    p = sub.add_parser("posing")
    p.add_argument("file")
    p.add_argument("--ticks", type=int, default=16)
    p.add_argument("--entities", type=int, default=91)
    p.add_argument("--wrong")
    p.set_defaults(run=posing)
    args = ap.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
