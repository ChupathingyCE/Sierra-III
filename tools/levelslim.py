"""levelslim.py - the level's rendered vertices in the geometry pack without
their binormal and tangent (24 bytes each instead of the Xbox's 32).

The port reads only the position, the packed normal and the uv of a rendered
vertex (psp_ge.c's level copies) and the whole 8-byte lightmap vertex; the
binormal and tangent (bytes 16-23) are for the Xbox's bump passes, which the
port draws from the copies' normals instead. Dropping them is exact: the
copies are built from the same bytes.

What points into the pack is known from how invader-build -G lays it out
(upstream/invader/psp-fork.patch), so the pointers are read from the
structures that hold them, never by scanning the map for likely words (the
map's sound, bitmap and collision data are full of words that look like pack
addresses):
- the animations' frame data, first, in the header's range (words 5-6);
- the models' vertices and indices, one struct each, reached through the tag
  data header's per-part arrays (12 bytes a part, the pointer at 4); the parts'
  own triangle offsets hold the same addresses as the index entries;
- per BSP, its materials' vertex blocks (rendered, then lightmap), reached
  through the BSP header's two arrays of 12-byte entries (the pointer at 4),
  one entry a material in lightmap order. These come last, BSP after BSP.
Only the level's blocks change, and everything before the first of them stays
where it is, so the animation and model pointers never move; the tool checks
that rather than assuming it. Each block keeps its 64-byte alignment. A block's
bytes run to the next block's start: whatever follows its lightmap vertices
(trailing zeros aside) is kept as it is.

It refuses, leaving both files untouched, on anything it can't place: a BSP or
array outside the map, a material whose lightmap count is neither 0 nor its
own, a block shorter than its vertices or not 64-aligned, a level pointer into
a block's vertices, animation or model data after the first level block, or
far more of the level blocks' addresses found elsewhere in the map than
chance gives (a pointer this tool doesn't know of: it would be left behind).

The pack's header word 10 gains bit 4 (the runtime's stride, psp_geo.c);
word 3, the data size, is rewritten.

usage: levelslim.py <map> <pgeo>   (both rewritten in place)"""
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'bake', 'lib'))  # the bake's readers

PACK_BASE = 0x0C000000
HEADER = 64  # psp_geo.c HEADER_BYTES
FORMAT_LEVEL_SLIM = 0x10  # psp_geo.c PGEO_LEVEL_SLIM
OLD_STRIDE, NEW_STRIDE, LIGHTMAP_STRIDE = 32, 24, 8
ENTRY = 12  # MemeBSPPointer / CacheFileModelPart{Vertices,Indices}Xbox: {u32, pointer, u32}
SBSP = 0x73627370  # the BSP header's signature, 'sbsp'


class Refused(Exception):
    pass


def up64(n):
    return (n + 63) & ~63


def u32(data, o):
    if o < 0 or o + 4 > len(data):
        raise Refused('a read at file offset %d is outside the map' % o)
    return struct.unpack_from('<I', data, o)[0]


def read_map(map_path):
    """the map's bytes and what points into the pack:
    level: [(file offset of the rendered word, of the lightmap word, rendered count, lightmap count)],
    others: [pack addresses of the model entries]"""
    import bspread
    from halomap import CacheMap, Defs
    m = CacheMap(map_path)
    d = Defs()
    data = m.data
    fo = bspread.fo
    others = []
    for count, ptr in ((m.vb_count, m.vb_ptr), (m.ib_count, m.ib_ptr)):
        if count > 0:
            if not (m.in_tags(ptr) and m.in_tags(ptr + ENTRY * count - 1)):
                raise Refused('the model part array at %08x is outside the tags' % ptr)
            o = m.off(ptr)
            others += [u32(data, o + ENTRY * i + 4) for i in range(count)]
    scn = m.tags[m.scenario_id & 0xffff]
    n, p = struct.unpack_from('<iI', data, m.off(scn.address) + fo(d, 'Scenario')['structure bsps'])
    bf, lmf, mf = fo(d, 'ScenarioStructureBSP'), fo(d, 'ScenarioStructureBSPLightmap'), fo(d, 'ScenarioStructureBSPMaterial')
    lm_size = d.structs['ScenarioStructureBSPLightmap']['size']
    mat_size = d.structs['ScenarioStructureBSPMaterial']['size']
    level = []
    for which in range(max(n, 0)):
        start, size, addr = struct.unpack_from('<III', data, m.off(p) + 32 * which)

        def tr(a, length=4, which=which, start=start, size=size, addr=addr):
            o = start + (a - addr)
            if a < addr or o + length > start + size or o + length > len(data):
                raise Refused('BSP %d: address %08x is outside it' % (which, a))
            return o

        base, count, rendered, count2, lightmap, sig = struct.unpack_from('<6I', data, start)
        if sig != SBSP or count != count2:
            raise Refused('BSP %d: its header is not an Xbox BSP header' % which)
        if not count:
            continue
        so = tr(base)
        ln, lp = struct.unpack_from('<iI', data, so + bf['lightmaps'])
        k = 0
        for li in range(max(ln, 0)):
            lo = tr(lp + lm_size * li, lm_size)
            mn, mp = struct.unpack_from('<iI', data, lo + lmf['materials'])
            for mi in range(max(mn, 0)):
                mo = tr(mp + mat_size * mi, mat_size)
                rcount, = struct.unpack_from('<I', data, mo + mf['rendered vertices count'])
                lcount, = struct.unpack_from('<I', data, mo + mf['lightmap vertices count'])
                ip, = struct.unpack_from('<I', data, mo + mf['rendered vertices index pointer'])
                if k >= count or ip != rendered + ENTRY * k:
                    raise Refused('BSP %d: material %d/%d is not entry %d of the header\'s arrays' % (which, li, mi, k))
                level.append((tr(rendered + ENTRY * k + 4), tr(lightmap + ENTRY * k + 4), rcount, lcount))
                k += 1
        if k != count:
            raise Refused('BSP %d: %d materials, its header says %d' % (which, k, count))
    return bytearray(data), level, others


def slim(pack, mapdata, level, others):
    """the new pack and the moved map words, from what read_map found.
    Returns (new pack, {file offset: new word}, census)"""
    header = list(struct.unpack_from('<16I', pack, 0))
    size = header[3]
    data = bytes(pack[HEADER:HEADER + size])
    if len(data) != size:
        raise Refused('the pack is shorter than its header says')

    def offset(word, what):
        if not PACK_BASE <= word <= PACK_BASE + size:
            raise Refused('%s %08x is not in the pack' % (what, word))
        return word - PACK_BASE

    # the level's blocks, by start; a material with no rendered vertices has
    # no block of its own (its pointers are moved like any other address)
    blocks = {}
    for rw, lw, r, l in level:
        if not r:
            continue
        if l not in (0, r):
            raise Refused('a material has %d rendered and %d lightmap vertices' % (r, l))
        o = offset(u32(mapdata, rw), 'a material\'s vertices')
        if blocks.get(o, (r, l)) != (r, l):
            raise Refused('two materials share %08x with different counts' % (PACK_BASE + o))
        blocks[o] = (r, l)
    if not blocks:
        return None, {}, 'no level vertices in the pack; nothing to do'
    starts = sorted(blocks)
    first = starts[0]
    # everything before the first block stays put: check nothing else lies after it
    for a in others:
        if a >= PACK_BASE and a - PACK_BASE >= first:
            raise Refused('model data at %08x lies after the first level block (%08x)' % (a, PACK_BASE + first))
    if header[4] and header[6] > first:
        raise Refused('the animations end at %08x, after the first level block (%08x)' % (header[6], first))

    out = bytearray(data[:first])
    table = []  # (old start, old end, new start, rendered, kept tail length)
    extra = 0
    for k, o in enumerate(starts):
        r, l = blocks[o]
        end = starts[k + 1] if k + 1 < len(starts) else size
        if o % 64:
            raise Refused('the block at %08x is not 64-aligned' % (PACK_BASE + o))
        if end - o < OLD_STRIDE * r + LIGHTMAP_STRIDE * l:
            raise Refused('the block at %08x is shorter than its vertices (shared or overlapping)' % (PACK_BASE + o))
        tail = data[o + OLD_STRIDE * r:end].rstrip(b'\0')
        tail = data[o + OLD_STRIDE * r:o + OLD_STRIDE * r + max(len(tail), LIGHTMAP_STRIDE * l)]
        extra += len(tail) - LIGHTMAP_STRIDE * l
        n = len(out)
        v = np.frombuffer(data[o:o + OLD_STRIDE * r], np.uint8).reshape(r, OLD_STRIDE)
        out += np.concatenate([v[:, :16], v[:, 24:32]], 1).tobytes()
        out += tail
        out += bytes(up64(len(out)) - len(out))
        table.append((o, end, n, r, len(tail)))
    new_size = len(out)
    olds = np.array(starts, np.int64)

    def move(a):
        """an old pack offset to the new one, None for one that can't be placed"""
        if a < first:
            return a
        if a == size:
            return new_size
        o, end, n, r, kept = table[int(np.searchsorted(olds, a, 'right')) - 1]
        if a == o:
            return n
        rendered_end = o + OLD_STRIDE * r
        if rendered_end <= a <= rendered_end + kept:
            return n + NEW_STRIDE * r + (a - rendered_end)
        return None

    moved = {}
    sites = set()
    for rw, lw, r, l in level:
        for at in (rw, lw):
            a = offset(u32(mapdata, at), 'a level pointer')
            b = move(a)
            if b is None:
                raise Refused('the word at file offset %08x (%08x) points into a level block\'s vertices'
                              % (at, PACK_BASE + a))
            sites.add(at)
            if b != a:
                moved[at] = PACK_BASE + b
    # a pointer this tool doesn't know of would be left behind. The map's data
    # holds words in the pack's range by chance (packed 16-bit pairs, sound,
    # bitmaps: a10 has ~330,000), so a few equal a block's address; a kind of
    # pointer would hit most blocks. Refuse when far more blocks are hit than
    # chance gives.
    targets = np.array(sorted({PACK_BASE + o for o in starts} |
                              {PACK_BASE + o + OLD_STRIDE * blocks[o][0] for o in starts if blocks[o][1]}), np.uint32)
    words = np.frombuffer(bytes(mapdata[:len(mapdata) // 4 * 4]), '<u4')
    in_range = int(np.count_nonzero((words >= PACK_BASE) & (words < PACK_BASE + size)))
    hit_at = [int(i) for i in np.nonzero(np.isin(words, targets))[0] if int(i) * 4 not in sites]
    hit_blocks = len({int(words[i]) for i in hit_at})
    chance = in_range * len(targets) / max(size, 1)
    if hit_blocks > max(16, 4 * chance):
        raise Refused('%d other words in the map equal %d of the level blocks\' %d addresses (chance: ~%.0f; first at '
                      'file offset %08x: %08x): a pointer this tool does not know of'
                      % (len(hit_at), hit_blocks, len(targets), chance, hit_at[0] * 4, int(words[hit_at[0]])))
    header[3] = new_size
    header[10] |= FORMAT_LEVEL_SLIM
    new_pack = struct.pack('<16I', *header) + bytes(out)
    rendered = sum(r for r, l in blocks.values())
    census = ('%d level blocks, %d rendered vertices: pack %d -> %d KB (-%d KB); %d level pointers, %d moved; '
              '%d bytes kept after lightmap vertices; %d chance words at %d block addresses (~%.0f expected)'
              % (len(blocks), rendered, size // 1024, new_size // 1024, (size - new_size) // 1024, len(sites),
                 len(moved), extra, len(hit_at), hit_blocks, chance))
    return new_pack, moved, census


def main(map_path, pgeo_path):
    pack = open(pgeo_path, 'rb').read()
    if len(pack) < HEADER or struct.unpack_from('<2I', pack, 0) != (0x4F454750, 1):
        raise Refused('%s is not a geometry pack' % pgeo_path)
    if struct.unpack_from('<I', pack, 40)[0] & FORMAT_LEVEL_SLIM:
        print('levelslim: %s is slim already' % pgeo_path)
        return
    mapdata, level, others = read_map(map_path)
    new_pack, moved, census = slim(pack, mapdata, level, others)
    if new_pack is None:
        print('levelslim: %s' % census)
        return
    for at, word in moved.items():
        struct.pack_into('<I', mapdata, at, word)
    open(map_path, 'wb').write(mapdata)
    open(pgeo_path, 'wb').write(new_pack)
    print('levelslim: %s' % census)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    try:
        main(sys.argv[1], sys.argv[2])
    except Refused as e:
        print('levelslim: refused: %s; the map and pack are left as they were' % e)
        sys.exit(1)
