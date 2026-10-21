"""levelslim.py - the level's rendered vertices in the geometry pack without
their binormal and tangent (24 bytes each instead of the Xbox's 32).

The port reads only the position, the packed normal and the uv of a rendered
vertex (psp_ge.c's level copies) and the whole 8-byte lightmap vertex; the
binormal and tangent (bytes 16-23) are for the Xbox's bump passes, which the
port draws from the copies' normals instead. Dropping them is exact: the
copies are built from the same bytes.

The pack shrinks, so every pointer into it after the first slimmed block
moves. The map is scanned for every word that points into the pack (models'
vertices and indices, animation frame data, the level's descriptors), and
each is moved by the same table the pack was rewritten by. The scan refuses
the map (and leaves both files untouched) on anything it can't place: a
pointer into the dropped bytes, into the padding after a block, or one not
16-byte aligned; padding after a block that isn't zero.

The pack's header word 10 gains bit 4 (the runtime's stride, psp_geo.c);
word 3, the data size, and words 5-6, the animation range, are rewritten.

usage: levelslim.py <map> <pgeo>   (both rewritten in place)"""
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'bake', 'lib'))  # the bake's readers
import bspread

PACK_BASE = bspread.PACK_BASE
HEADER = bspread.HEADER
FORMAT_LEVEL_SLIM = 0x10  # psp_geo.c PGEO_LEVEL_SLIM
OLD_STRIDE, NEW_STRIDE, LIGHTMAP_STRIDE = 32, 24, 8


def up64(n):
    return (n + 63) & ~63


def refuse(why):
    print('levelslim: refused: %s; the map and pack are left as they were' % why)
    sys.exit(1)


def materials(map_path, pgeo_path):
    """every BSP material's block in the pack: {offset: (rendered, lightmap)}"""
    blocks = {}
    for which in range(bsp_count(map_path)):
        mats, _ = bspread.load(map_path, pgeo_path, which)
        for m in mats:
            if m['key'] < PACK_BASE:
                refuse('a material keeps its vertices in the tags (%08x)' % m['key'])
            o = m['key'] - PACK_BASE
            rl = (m['rcount'], m['lcount'])
            if m['lcount'] not in (0, m['rcount']):
                refuse('material at %08x has %d rendered and %d lightmap vertices' % (m['key'], rl[0], rl[1]))
            if blocks.get(o, rl) != rl:
                refuse('two materials share %08x with different counts' % m['key'])
            blocks[o] = rl
    return blocks


def bsp_count(map_path):
    m = bspread.CacheMap(map_path)
    d = bspread.Defs()
    scn = m.tags[m.scenario_id & 0xffff]
    n, _ = struct.unpack_from('<iI', m.data, m.off(scn.address) + bspread.fo(d, 'Scenario')['structure bsps'])
    return n


def main(map_path, pgeo_path):
    pack = bytearray(open(pgeo_path, 'rb').read())
    header = list(struct.unpack_from('<16I', pack, 0))
    if header[0] != 0x4F454750 or header[1] != 1:
        refuse('%s is not a geometry pack' % pgeo_path)
    if header[10] & FORMAT_LEVEL_SLIM:
        print('levelslim: %s is slim already' % pgeo_path)
        return
    size = header[3]
    data = pack[HEADER:HEADER + size]
    blocks = materials(map_path, pgeo_path)
    if not blocks:
        print('levelslim: no level vertices in the pack; nothing to do')
        return
    starts = sorted(blocks)
    # the new pack and the table: per block, old start, old end, old next
    # (64-aligned), new start; between blocks the bytes move unchanged
    out = bytearray()
    old_cursor = 0
    table = []  # (old start, old data end, old next, new start, rendered, lightmap)
    for o in starts:
        r, l = blocks[o]
        if o % 64 or o < old_cursor:
            refuse('block at %08x overlaps the one before or is not 64-aligned' % (PACK_BASE + o))
        old_len = OLD_STRIDE * r + LIGHTMAP_STRIDE * l
        old_next = up64(o + old_len)
        if old_next > size or any(data[o + old_len:old_next]):
            refuse('the bytes after the block at %08x are not padding' % (PACK_BASE + o))
        out += data[old_cursor:o]
        n = len(out)
        v = np.frombuffer(bytes(data[o:o + OLD_STRIDE * r]), np.uint8).reshape(r, OLD_STRIDE)
        out += np.concatenate([v[:, :16], v[:, 24:32]], 1).tobytes()
        out += data[o + OLD_STRIDE * r:o + old_len]
        out += bytes(up64(len(out)) - len(out))
        table.append((o, o + old_len, old_next, n, r, l))
        old_cursor = old_next
    out += data[old_cursor:]
    new_size = len(out)

    def move(a):
        """an old pack offset to the new one, None for one that can't be placed"""
        k = bisect_right(a) - 1
        if k < 0:
            return a
        o, end, nxt, n, r, l = table[k]
        if a == o:
            return n
        if l and a == o + OLD_STRIDE * r:
            return n + NEW_STRIDE * r
        if a < nxt and a != end:
            return None
        shift = nxt - up64(n + NEW_STRIDE * r + LIGHTMAP_STRIDE * l)
        return a - shift

    olds = np.array([t[0] for t in table], np.int64)
    lightmap_starts = {t[0] + OLD_STRIDE * t[4] for t in table if t[5]}

    def bisect_right(a):
        return int(np.searchsorted(olds, a, 'right'))

    # every word in the map that points into the pack
    mapdata = bytearray(open(map_path, 'rb').read())
    words = np.frombuffer(bytes(mapdata[:len(mapdata) // 4 * 4]), '<u4')
    hits = np.nonzero((words >= PACK_BASE) & (words < PACK_BASE + size))[0]
    unaligned = [int(i) for i in hits if words[i] % 16]
    if unaligned:
        refuse('%d words into the pack are not 16-byte aligned (first at file offset %08x: %08x)'
               % (len(unaligned), unaligned[0] * 4, int(words[unaligned[0]])))
    moved = 0
    level = 0
    for i in hits:
        a = int(words[i]) - PACK_BASE
        b = move(a)
        if b is None:
            refuse('the word at file offset %08x (%08x) points into a level block\'s dropped bytes'
                   % (i * 4, int(words[i])))
        if a in blocks or a in lightmap_starts:
            level += 1
        if b != a:
            struct.pack_into('<I', mapdata, int(i) * 4, PACK_BASE + b)
            moved += 1
    found = {int(w) - PACK_BASE for w in words[hits]}
    missing = [o for o in starts if o not in found]
    if missing:
        refuse('%d level blocks have no pointer in the map (first %08x)' % (len(missing), PACK_BASE + missing[0]))
    # the header: the data size, the animation range (offsets into the data)
    header[3] = new_size
    if header[4]:
        for w in (5, 6):
            b = move(header[w])
            if b is None:
                refuse('the animation range ends inside a level block')
            header[w] = b
    header[10] |= FORMAT_LEVEL_SLIM
    rendered = sum(r for r, l in blocks.values())
    open(map_path, 'wb').write(mapdata)
    open(pgeo_path, 'wb').write(struct.pack('<16I', *header) + bytes(out))
    print('levelslim: %d level blocks, %d rendered vertices: pack %d -> %d KB (-%d KB); %d pointers into the pack, '
          '%d moved, %d of them the level\'s' % (len(blocks), rendered, size // 1024, new_size // 1024,
                                                 (size - new_size) // 1024, len(hits), moved, level))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
