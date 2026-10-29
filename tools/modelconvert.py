"""modelconvert.py - the models' vertex buffers in the GE's own form, appended
to the geometry pack, so the port converts none of them at run time
(docs/plan_preconvert.md).

The renderer rewrites a model part's Xbox vertices for the GE the first time
the part is drawn (psp_ge.c, model_convert): 24-byte rigid records, or for a
part hung on several nodes a skinning plan (each node on one of 8 bone slots,
the triangles in batches whose nodes sit on distinct slots) and records
carrying their weights by slot. That costs the main CPU every time a model's
block is read into the geometry cache, and a draw replayed by the Media
Engine before its buffer was converted is left out. Every input of that work
is in the pack and the map: the vertices, the part's index buffer, its strip
flag and counts. So this tool does the same arithmetic here, in float32 in
the C's order, and the runtime draws the result as it is read.

What is written: after the pack's data (header word 3, which old runtimes
read up to and no further, so they still work), one block per model vertex
buffer an opaque draw converts, each:
- a 64-byte header: 'PCV1', flags (1: skinned), the vertex count, the record
  stride, the index count and strip flag the draw passes (the runtime checks
  them), whether the source was the 68-byte form, the raw buffer's pack
  address, then the skinning plan's struct skin_part (weights, stride, batch
  count, the batches' offset, its bytes);
- the records, at the header's end (the GE reads them there);
- for a skinned buffer, its struct skin_batch array (slot to node, the
  triangle list's offset and length, the batch's sphere, the runs' spheres'
  offset and count), the triangle lists, and the runs' spheres
  (psp_ge.c, batch_sphere and near_clusters_make: 16 triangles a run).
Offsets in the plan are from the block's start; the runtime adds the block's
address once it is read (psp_ge_pcv_relocate). Then a table: each block's
pack address, in the raw buffers' address order, and one more that ends the
last block.

The pack's header words 11-15 (zero before): the region's start (an offset
from the data's start, as pack addresses are), its bytes, the table's offset,
its entries, and 'PCV1', the format's version. Each model part's
vertex_buffer pad word in the map (unused by the engine) takes its buffer's
table index + 1, so the runtime finds a part's block without a search.

Left to run time, counted: a buffer two parts draw with different triangles
or counts (whichever drew first decided its plan), a vertex form other than
the model's two, and anything the pack can't hold whole. A see-through draw
still converts its raw buffer as before (the runtime doesn't take these
blocks for it).

usage: modelconvert.py <map> <pgeo>   (both rewritten in place)"""
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'bake', 'lib'))  # the bake's readers

PACK_BASE = 0x0C000000
HEADER = 64                  # psp_geo.c HEADER_BYTES
MAGIC = 0x31564350           # 'PCV1': each block's first word, and header word 15 (psp_geo.c PCV_VERSION)
BLOCK_HEADER = 64
SKIN_SLOTS, SKIN_NODES, MAX_BATCHES = 8, 128, 64
NEAR_CLUSTER_TRIANGLES = 16  # psp_ge.c
BATCH_BYTES = 40             # struct skin_batch on the PSP
TYPE_UNCOMPRESSED, TYPE_COMPRESSED = 4, 5   # _rasterizer_vertex_type_model_*
STRIP = 1                                   # _triangle_buffer_type_precompiled_strip
VERTEX_SIZE = {TYPE_UNCOMPRESSED: 68, TYPE_COMPRESSED: 32}

F = np.float32


class Refused(Exception):
    pass


def up(n, a):
    return (n + a - 1) & ~(a - 1)


# ---------- the map: every model part's buffers

def read_parts(map_path):
    """the map's bytes and [(vertex pack address, vertex count, vertex type,
    index pack address, triangle count, triangle buffer type, file offset of
    the vertex buffer's pad word)], one per model geometry part"""
    from halomap import CacheMap, Defs
    m = CacheMap(map_path)
    d = Defs()
    data = m.data

    def fields(name):
        return {f.get('name'): x for x, f, s in d.fields(name)}

    mf, gf, pf = fields('Model'), fields('ModelGeometry'), fields('ModelGeometryPart')
    part_size, geometry_size = d.structs['ModelGeometryPart']['size'], d.structs['ModelGeometry']['size']
    # the engine's struct model_geometry_part: its triangle_buffer {type, count,
    # base_address, hardware_format} and vertex_buffer {type, count, offset,
    # base_address, hardware_format}, as the definitions name them
    want = {'triangle buffer type': 0x44, 'triangle count': 0x48, 'triangle offset 2': 0x50,
            'vertex type': 0x54, 'vertex count': 0x58, 'vertex offset': 0x64}
    for name, at in want.items():
        if pf.get(name) != at:
            raise Refused('the model part\'s %r is at %r, not %#x' % (name, pf.get(name), at))
    if part_size != 0x68:
        raise Refused('a model part is %d bytes, not 104' % part_size)

    def entry_data(hardware_format):
        """the pack address in a buffer's 12-byte {Common, Data, Lock} entry"""
        if not m.in_tags(hardware_format) or not m.in_tags(hardware_format + 11):
            return None
        return struct.unpack_from('<I', data, m.off(hardware_format) + 4)[0]

    parts = []
    for tag in m.tags:
        if tag.group != 'mode' or not m.in_tags(tag.address):
            continue
        gn, gp = struct.unpack_from('<iI', data, m.off(tag.address) + mf['geometries'])
        for g in range(max(gn, 0)):
            go = m.off(gp + geometry_size * g)
            pn, pp = struct.unpack_from('<iI', data, go + gf['parts'])
            for k in range(max(pn, 0)):
                po = m.off(pp + part_size * k)
                ttype, = struct.unpack_from('<H', data, po + 0x44)
                tcount, = struct.unpack_from('<I', data, po + 0x48)
                thw, = struct.unpack_from('<I', data, po + 0x50)
                vtype, = struct.unpack_from('<H', data, po + 0x54)
                vcount, = struct.unpack_from('<I', data, po + 0x58)
                vhw, = struct.unpack_from('<I', data, po + 0x64)
                parts.append((entry_data(vhw), vcount, vtype, entry_data(thw), tcount, ttype, po + 0x56))
    return bytearray(data), parts


# ---------- psp_ge.c's conversion, in float32 in the C's order

def unpack_normals(packed):
    """unpack_normal: 11, 11 and 10 signed bits"""
    p = packed.astype(np.uint32)
    out = np.empty((len(p), 3), F)
    v = (p << np.uint32(21)).view(np.int32).astype(F)
    out[:, 0] = (v * F(1.0 / 1048576.0) + F(1.0)) * (F(1.0) / F(2047.0))
    p = p >> np.uint32(11)
    v = (p << np.uint32(21)).view(np.int32).astype(F)
    out[:, 1] = (v * F(1.0 / 1048576.0) + F(1.0)) * (F(1.0) / F(2047.0))
    p = p >> np.uint32(11)
    v = (p << np.uint32(22)).view(np.int32).astype(F)
    out[:, 2] = (v * F(1.0 / 2097152.0) + F(1.0)) * (F(1.0) / F(1023.0))
    return out


def trunc_int(x):
    """C's float to int (toward zero), for values in int range"""
    return np.trunc(x.astype(np.float64)).astype(np.int64)


def pack_normals(n):
    """pack_normal, for the 68-byte form"""
    with np.errstate(invalid='ignore'):
        i = trunc_int(n[:, 0] * F(1023.0)) & 0x7FF
        j = trunc_int(n[:, 1] * F(1023.0)) & 0x7FF
        k = trunc_int(n[:, 2] * F(511.0)) & 0x3FF
    return (i | (j << 11) | (k << 22)).astype(np.uint32)


class Source:
    """the fields model_convert reads from each vertex"""

    def __init__(self, raw, count, uncompressed):
        if uncompressed:
            v = np.frombuffer(raw, np.uint8, count * 68).reshape(count, 68)
            self.position = v[:, 0:12].copy()
            normal = v[:, 12:24].copy().view('<f4').reshape(count, 3)
            self.packed = pack_normals(normal)
            self.uv = v[:, 48:56].copy()
            ni = v[:, 56:60].copy().view('<i2').reshape(count, 2).astype(np.int64)
            n0 = ni[:, 0]
            self.node_byte = (np.where((n0 >= 0) & (n0 < 85), n0 * 3, 0) & 0xFF).astype(np.uint8)
            w0 = v[:, 60:64].copy().view('<f4').reshape(count)
            n1 = ni[:, 1]
        else:
            v = np.frombuffer(raw, np.uint8, count * 32).reshape(count, 32)
            self.position = v[:, 0:12].copy()
            self.packed = v[:, 12:16].copy().view('<u4').reshape(count)
            tc = v[:, 24:28].copy().view('<i2').reshape(count, 2)
            uv = np.empty((count, 2), F)
            uv[:, 0] = tc[:, 0].astype(F) * (F(1.0) / F(32767.0))
            uv[:, 1] = tc[:, 1].astype(F) * (F(1.0) / F(32767.0))
            self.uv = uv.view(np.uint8).reshape(count, 8)
            self.node_byte = v[:, 28].copy()
            n0 = (v[:, 28] // 3).astype(np.int64)
            n1 = (v[:, 29] // 3).astype(np.int64)
            w0 = v[:, 30:32].copy().view('<i2').reshape(count).astype(F) * (F(1.0) / F(32767.0))
        # vertex_nodes
        n0 = np.where((n0 < 0) | (n0 >= SKIN_NODES), 0, n0)
        with np.errstate(invalid='ignore'):
            single = (n1 < 0) | (n1 >= SKIN_NODES) | (n1 == n0) | ~(w0 < F(0.995))
        self.n0 = n0
        self.n1 = np.where(single, -1, n1)
        self.w0 = np.where(single, F(1.0), w0).astype(F)
        normal = unpack_normals(self.packed)
        with np.errstate(invalid='ignore'):
            self.normal_bytes = (trunc_int(normal * F(127.0)) & 0xFF).astype(np.uint8)
        with np.errstate(invalid='ignore'):
            self.weight0 = (trunc_int(self.w0 * F(128.0) + F(0.5)) & 0xFF).astype(np.int64)
        self.count = count


def triangle_list(indices, index_count, strip):
    if strip:
        out = []
        for i in range(index_count - 2):
            a, b, c = int(indices[i]), int(indices[i + 1]), int(indices[i + 2])
            if a == b or b == c or a == c:
                continue
            if i & 1:
                a, b = b, a
            out.append((a, b, c))
        return out
    n = index_count // 3
    return [tuple(int(x) for x in indices[3 * t:3 * t + 3]) for t in range(n)]


def skin_plan(src, indices, index_count, strip, census):
    """skin_plan: None leaves the part rigid; else (slots, slot_of, batches),
    batches as [(node[8], [index, ...])]"""
    count = src.count
    n0, n1 = src.n0, src.n1
    used = np.zeros(SKIN_NODES, bool)
    used[n0] = True
    used[n1[n1 >= 0]] = True
    node_count = int(used.sum())
    if node_count <= 1 or indices is None or index_count < 3:
        return None
    tris = triangle_list(indices, index_count, strip)
    if not tris:
        return None
    n0l, n1l = n0.tolist(), n1.tolist()

    def nodes_of(t):
        out = []
        for v in t:
            out.append(n0l[v])
            if n1l[v] >= 0:
                out.append(n1l[v])
        return out

    conflict = [set() for _ in range(SKIN_NODES)]
    for t in tris:
        for v in t:
            if v >= count:
                return None
        ns = nodes_of(t)
        for a in ns:
            for b in ns:
                if a != b:
                    conflict[a].add(b)
    degree = [len(c) for c in conflict]
    slot_of = [-1] * SKIN_NODES
    slot_use = [0] * SKIN_SLOTS
    slots = 0
    used_l = used.tolist()
    for _ in range(node_count):
        best = -1
        for node in range(SKIN_NODES):
            if used_l[node] and slot_of[node] < 0 and (best < 0 or degree[node] > degree[best]):
                best = node
        taken = 0
        for other in conflict[best]:
            if slot_of[other] >= 0:
                taken |= 1 << slot_of[other]
        choice = -1
        for slot in range(SKIN_SLOTS):
            if not taken & (1 << slot) and (choice < 0 or slot_use[slot] < slot_use[choice]):
                choice = slot
        if choice < 0:
            census['plan failed'] += 1
            return None
        slot_of[best] = choice
        slot_use[choice] += 1
        slots = max(slots, choice + 1)
    batches = []      # [node[8]]
    batch_tris = []   # [[triangle]]
    for t in tris:
        ns = nodes_of(t)
        found = -1
        for b, held in enumerate(batches):
            if all(held[slot_of[x]] == 0xFF or held[slot_of[x]] == x for x in ns):
                found = b
                break
        if found < 0:
            if len(batches) == MAX_BATCHES:
                census['plan failed'] += 1
                return None
            batches.append([0xFF] * SKIN_SLOTS)
            batch_tris.append([])
            found = len(batches) - 1
        for x in ns:
            batches[found][slot_of[x]] = x
        batch_tris[found].append(t)
    return slots, slot_of, [(batches[b], [v for t in batch_tris[b] for v in t]) for b in range(len(batches))]


def first_extreme(p, smallest):
    """the C loop's low (or high) over p in order: the first value, then any
    strictly smaller (larger); NaN never replaces and is kept if first"""
    if np.isnan(p[0]):
        return p[0]
    q = p[~np.isnan(p)]
    m = q.min() if smallest else q.max()
    return q[np.argmax(q == m)]   # the first occurrence of the value (0.0 before -0.0, as the C keeps)


def sphere(points):
    """batch_sphere / near_clusters_make: (centre[3], radius) of points in order"""
    low = [first_extreme(points[:, a], True) for a in range(3)]
    high = [first_extreme(points[:, a], False) for a in range(3)]
    center = np.array([(low[a] + high[a]) * F(0.5) for a in range(3)], F)
    dx = points[:, 0] - center[0]
    dy = points[:, 1] - center[1]
    dz = points[:, 2] - center[2]
    d = dx * dx + dy * dy + dz * dz
    with np.errstate(invalid='ignore'):
        bigger = d[d > F(0.0)]
    reach = bigger.max() if len(bigger) else F(0.0)
    return center, F(np.sqrt(F(reach)))


def convert(raw, count, uncompressed, indices, index_count, strip, census):
    """model_convert for an opaque draw (a skinning plan asked for, model14
    off): (records bytes, plan or None)"""
    src = Source(raw, count, uncompressed)
    plan = skin_plan(src, indices, index_count, strip, census)
    if plan is None:
        rec = np.zeros((count, 24), np.uint8)
        rec[:, 0:8] = src.uv
        rec[:, 8:11] = src.normal_bytes
        rec[:, 11] = src.node_byte
        rec[:, 12:24] = src.position
        return rec.tobytes(), None
    slots, slot_of, batches = plan
    stride = (4 if slots <= 4 else 8) + 24
    uv_at = stride - 24
    rec = np.zeros((count, stride), np.uint8)
    slot = np.array(slot_of + [0], np.int64)   # (index -1 never used below)
    rows = np.arange(count)
    two = src.n1 >= 0
    # the C writes the first node's weight, then the second's: on one slot
    # (a vertex in no triangle), the second write stands
    rec[rows[two], slot[src.n0[two]]] = src.weight0[two].astype(np.uint8)
    rec[rows[two], slot[src.n1[two]]] = ((128 - src.weight0[two]) & 0xFF).astype(np.uint8)
    rec[rows[~two], slot[src.n0[~two]]] = 128
    rec[:, uv_at:uv_at + 8] = src.uv
    rec[:, uv_at + 8:uv_at + 11] = src.normal_bytes
    rec[:, uv_at + 12:uv_at + 24] = src.position
    positions = src.position.copy().view('<f4').reshape(count, 3)
    out = []
    for nodes, tri in batches:
        p = positions[np.array(tri, np.int64)]
        center, radius = sphere(p)
        runs = []
        for c in range(0, len(tri), 3 * NEAR_CLUSTER_TRIANGLES):
            rc, rr = sphere(p[c:c + 3 * NEAR_CLUSTER_TRIANGLES])
            runs.append((rc, rr))
        out.append((nodes, tri, center, radius, runs))
    return rec.tobytes(), (slots, stride, out)


# ---------- the blocks, the table, the pack

def block(raw_address, index_address, count, uncompressed, index_count, strip, records, plan):
    """one block, offsets from its start"""
    body = bytearray(records)
    body += bytes(up(len(body), 16) - len(body))
    if plan is None:
        stride = 24
        skin = (0, 0, 0, 0, 0)
        flags = 0
    else:
        slots, stride, batches = plan
        flags = 1
        at = BLOCK_HEADER + len(body)                # the batch array
        lists_at = at + BATCH_BYTES * len(batches)
        lists = bytearray()
        list_offsets = []
        for nodes, tri, center, radius, runs in batches:
            list_offsets.append(lists_at + len(lists))
            lists += np.array(tri, '<u2').tobytes()
        runs_at = up(lists_at + len(lists), 16)
        run_bytes = bytearray()
        array = bytearray()
        for (nodes, tri, center, radius, runs), lo in zip(batches, list_offsets):
            ro = runs_at + len(run_bytes) if runs else 0
            # floats as their float32 bytes (struct's 'f' would round-trip
            # through a double)
            for rc, rr in runs:
                run_bytes += np.array(list(rc) + [rr], F).tobytes()
            array += (struct.pack('<8BIi', *nodes, lo, len(tri)) + np.array(list(center) + [radius], F).tobytes() +
                      struct.pack('<Ii', ro, len(runs)))
        body += array + lists
        body += bytes(runs_at - BLOCK_HEADER - len(body))
        body += run_bytes
        # struct skin_part: weights, stride, batch_count, batches, bytes (the
        # plan's own size, as the heap's plan counts it)
        skin = (slots, stride, len(batches), at, 20 + BATCH_BYTES * len(batches) + 2 * sum(len(b[1]) for b in batches))
    # word 13: 0 (the runtime's preconvertcheck marks a checked block there);
    # word 14: the raw triangles' pack address, for that check
    head = struct.pack('<8I5i2I', MAGIC, flags, count, stride, index_count, strip, uncompressed, raw_address, *skin,
                       0, index_address)
    head += bytes(BLOCK_HEADER - len(head))
    out = head + bytes(body)
    return out + bytes(up(len(out), 64) - len(out))


def bake(pack, parts):
    header = list(struct.unpack_from('<16I', pack, 0))
    size = header[3]
    data = bytes(pack[HEADER:HEADER + size])
    if len(data) != size:
        raise Refused('the pack is shorter than its header says')
    census = {'parts': len(parts), 'buffers': 0, 'skinned': 0, 'rigid': 0, 'plan failed': 0, 'shared': 0,
              'other form': 0, 'outside': 0, 'raw bytes': 0, 'baked bytes': 0, 'triangle list bytes': 0,
              'vertices': 0}
    by_vertices = {}
    for v, vcount, vtype, i, tcount, ttype, pad in parts:
        if v is None or i is None:
            census['outside'] += 1
            continue
        strip = 1 if ttype == STRIP else 0
        index_count = tcount + 2 if strip else 3 * tcount
        by_vertices.setdefault(v, set()).add((vcount, vtype, i, index_count, strip, ttype))
    region = up(size, 64)
    blocks = bytearray()
    table = []
    for v in sorted(by_vertices):
        uses = by_vertices[v]
        if len(uses) > 1:
            census['shared'] += 1
            continue
        vcount, vtype, i, index_count, strip, ttype = next(iter(uses))
        if vtype not in VERTEX_SIZE or ttype not in (0, STRIP) or vcount <= 0 or vcount > 65535:
            census['other form'] += 1
            continue
        vbytes = vcount * VERTEX_SIZE[vtype]
        if not (PACK_BASE <= v and v - PACK_BASE + vbytes <= size and PACK_BASE <= i and
                i - PACK_BASE + 2 * index_count <= size):
            census['outside'] += 1
            continue
        raw = data[v - PACK_BASE:v - PACK_BASE + vbytes]
        indices = np.frombuffer(data, '<u2', index_count, i - PACK_BASE) if index_count > 0 else None
        records, plan = convert(raw, vcount, vtype == TYPE_UNCOMPRESSED, indices, index_count, strip, census)
        b = block(v, i, vcount, int(vtype == TYPE_UNCOMPRESSED), index_count, strip, records, plan)
        table.append((v, PACK_BASE + region + len(blocks)))
        blocks += b
        census['buffers'] += 1
        census['skinned' if plan else 'rigid'] += 1
        census['raw bytes'] += vbytes
        census['baked bytes'] += len(b)
        census['vertices'] += vcount
        if plan:
            census['triangle list bytes'] += 2 * sum(len(x[1]) for x in plan[2])
    if not table:
        return None, census, {}
    end_address = PACK_BASE + region + len(blocks)
    table_at = region + len(blocks)
    table_bytes = b''.join(struct.pack('<I', a) for r, a in table) + struct.pack('<I', end_address)
    region_bytes = len(blocks) + len(table_bytes)
    header[11], header[12], header[13], header[14], header[15] = region, region_bytes, table_at, len(table), MAGIC
    new = struct.pack('<16I', *header) + data + bytes(region - size) + bytes(blocks) + table_bytes
    index_of = {raw: k for k, (raw, a) in enumerate(table)}
    return new, census, index_of


def main(map_path, pgeo_path):
    pack = open(pgeo_path, 'rb').read()
    if len(pack) < HEADER or struct.unpack_from('<2I', pack, 0) != (0x4F454750, 1):
        raise Refused('%s is not a geometry pack' % pgeo_path)
    if struct.unpack_from('<I', pack, 60)[0]:
        print('modelconvert: %s is preconverted already' % pgeo_path)
        return
    mapdata, parts = read_parts(map_path)
    pads = [pad for *rest, pad in parts]
    taken = [pad for pad in pads if struct.unpack_from('<H', mapdata, pad)[0]]
    if taken:
        raise Refused('%d model parts\' vertex buffer pad words are not 0 (first at file offset %08x)'
                      % (len(taken), taken[0]))
    new, c, index_of = bake(pack, parts)
    line = ('%d model parts, %d vertex buffers baked (%d skinned, %d rigid, %d plans failed: rigid as at run time), '
            '%d vertices; left to run time: %d drawn with two shapes, %d of another form, %d outside the pack; '
            'raw %d KB, baked %d KB (triangle lists %d KB)'
            % (c['parts'], c['buffers'], c['skinned'], c['rigid'], c['plan failed'], c['vertices'], c['shared'],
               c['other form'], c['outside'], c['raw bytes'] // 1024, c['baked bytes'] // 1024,
               c['triangle list bytes'] // 1024))
    if new is None:
        print('modelconvert: nothing to bake; %s' % line)
        return
    if len(index_of) > 65535:
        raise Refused('%d baked buffers: more than a 16-bit index holds' % len(index_of))
    marked = 0
    for v, vcount, vtype, i, tcount, ttype, pad in parts:
        if v in index_of:
            struct.pack_into('<H', mapdata, pad, index_of[v] + 1)
            marked += 1
    line += '; %d parts marked in the map' % marked
    open(map_path, 'wb').write(mapdata)
    open(pgeo_path, 'wb').write(new)
    print('modelconvert: %s; pack %d -> %d KB' % (line, (len(pack) - HEADER) // 1024, (len(new) - HEADER) // 1024))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    try:
        main(sys.argv[1], sys.argv[2])
    except Refused as e:
        print('modelconvert: refused: %s; the pack is left as it was' % e)
        sys.exit(1)
