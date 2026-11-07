"""sspr.py - lower levels of detail for the models that have none, made at
bake, each with its shape error (SSPR step 1: rigid parts; docs/sspr_bake.md).

Most models drawn small on the PSP have one level of detail: every region
permutation names the same geometry at all five levels, so the engine's pick
changes nothing. This tool decimates each rigid part of such a model into
levels 3 (high) to 0 (super low), with meshoptimizer's simplifier (Arseny
Kapoulkine, MIT; tools/third_party/meshoptimizer), and records each level's
largest error in model units. The cutoffs aren't written here: the runtime
makes them from the errors and the pixel budget its keep word asks for, so
one bake serves every budget of an A/B and the tags keep their own cutoffs
when sspr is off.

What is appended to the pack (after modelconvert's regions, so run it after
modelconvert):
- the level blocks, each a PCV block (modelconvert's header and 24-byte rigid
  records) with flag PCV_LEVEL (4): its own triangle list (16-bit indices)
  follows its records, padded to 16 bytes, and header word 4 is its index
  count;
- the directory: for each of modelconvert's baked buffers (its table's order),
  eight words, levels 0-3, each a level block's pack address and bytes (0, 0
  for none), so the runtime locks a level whole in one read;
- the models: for each model given levels, its tag id, its diameter (model
  units, the parts' bounding sphere at the top level) and its four levels'
  errors (float32, model units; 0 for a level no part of it has);
- a 64-byte trailer at the file's end: 'SSP1', the region's start (from the
  data's start, as pack addresses are), its bytes, the directory's offset and
  entries, the models' offset and count, the tool's version.

A part is left alone (and draws its top level at every distance, as today)
when it is skinned (step 2), not baked by modelconvert, or too small to
simplify. Leaf cards (many small disconnected pieces) are simplified by
dropping whole pieces (meshopt's sloppy simplifier), their own column in the
census, since edge collapses would merge cards into slabs.

usage: sspr.py <map> <pgeo>   (the pack rewritten in place; the map is read)"""
import ctypes
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'bake', 'lib'))
import modelconvert as mc  # noqa: E402  (the pack's format and the GE's conversion)

TRAILER_MAGIC = 0x31505353      # 'SSP1'
VERSION = 1
PCV_LEVEL = 4
LEVELS = 4                      # 3 high .. 0 super low; 4 (super high) is the top, untouched
RATIO = (1 / 16, 1 / 8, 1 / 4, 1 / 2)   # each level's triangles as a share of the top's
MIN_TRIANGLES = 24              # a part this small keeps its top level
LEAF_PIECES = 0.2               # pieces per triangle at or over which a part is leaf cards
F = np.float32


class Refused(Exception):
    pass


# ---------- meshoptimizer, built here for the host

def library():
    src = os.path.join(HERE, 'third_party', 'meshoptimizer')
    cache = os.environ.get('SSPR_LIB_DIR') or os.path.join(os.path.expanduser('~'), '.cache', 'sierra-sspr')
    os.makedirs(cache, exist_ok=True)
    lib = os.path.join(cache, 'libmeshopt-v1.3.so')
    if not os.path.exists(lib):
        units = [os.path.join(src, f) for f in ('simplifier.cpp', 'indexgenerator.cpp', 'allocator.cpp')]
        subprocess.check_call([os.environ.get('CXX', 'c++'), '-O2', '-fPIC', '-shared', '-o', lib + '.tmp'] + units)
        os.replace(lib + '.tmp', lib)
    m = ctypes.CDLL(lib)
    size, p_u32, p_f32, p_u8 = ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_float), \
        ctypes.POINTER(ctypes.c_ubyte)
    m.meshopt_simplifyWithAttributes.restype = size
    m.meshopt_simplifyWithAttributes.argtypes = [p_u32, p_u32, size, p_f32, size, size, p_f32, size, p_f32, size,
                                                 p_u8, size, ctypes.c_float, ctypes.c_uint, p_f32]
    m.meshopt_simplifySloppy.restype = size
    m.meshopt_simplifySloppy.argtypes = [p_u32, p_u32, size, p_f32, size, size, p_u8, size, ctypes.c_float, p_f32]
    m.meshopt_simplifyScale.restype = ctypes.c_float
    m.meshopt_simplifyScale.argtypes = [p_f32, size, size]
    return m


LOCK_BORDER = 1 << 0            # meshopt_SimplifyLockBorder: a part's open edges meet its neighbours'


def ptr(a, t):
    return a.ctypes.data_as(ctypes.POINTER(t))


def simplify(m, positions, attributes, indices, target, leaves):
    """(new indices, error in model units)"""
    out = np.empty_like(indices)
    err = ctypes.c_float(0)
    vcount = len(positions)
    if leaves:
        n = m.meshopt_simplifySloppy(ptr(out, ctypes.c_uint), ptr(indices, ctypes.c_uint), len(indices),
                                     ptr(positions, ctypes.c_float), vcount, 12, None, target, 1.0,
                                     ctypes.byref(err))
    else:
        # UVs weigh as much as a tenth of the part's size, normals less: a
        # seam (split vertices at one position) is kept by the simplifier
        weights = np.array([0.1, 0.1, 0.05, 0.05, 0.05], F)
        n = m.meshopt_simplifyWithAttributes(ptr(out, ctypes.c_uint), ptr(indices, ctypes.c_uint), len(indices),
                                             ptr(positions, ctypes.c_float), vcount, 12,
                                             ptr(attributes, ctypes.c_float), 20, ptr(weights, ctypes.c_float), 5,
                                             None, target, 1.0, LOCK_BORDER, ctypes.byref(err))
    scale = m.meshopt_simplifyScale(ptr(positions, ctypes.c_float), vcount, 12)
    return out[:n].copy(), float(err.value) * float(scale)


def pieces(indices, positions):
    """connected pieces of a triangle list, by shared position: a hard edge
    or a UV seam splits vertices at one place, and counting by index would
    take a faceted prop for a pile of cards (sspr1: one on b40 vanished at
    2 px, simplified as leaf cards)"""
    _, weld = np.unique(np.round(positions, 4), axis=0, return_inverse=True)
    weld = weld.reshape(-1)
    vcount = int(weld.max()) + 1 if len(weld) else 0
    parent = np.arange(vcount)
    indices = weld[indices.astype(np.int64)]

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for t in range(0, len(indices), 3):
        a, b, c = (find(int(x)) for x in indices[t:t + 3])
        parent[b] = a
        parent[find(c)] = a
    used = np.unique(indices)
    return len({find(int(v)) for v in used})


# ---------- the map's models

def read_models(map_path):
    """[(tag id, [geometry indices it draws]) for each model whose every
    region permutation names one geometry at all five levels], and
    {geometry part's vertex pack address: (model tag id, part's vertex count,
    vertex type, index address, triangle count, triangle buffer type)}"""
    from halomap import CacheMap, Defs
    m = CacheMap(map_path)
    d = Defs()
    data = m.data

    def fields(name):
        return {f.get('name'): x for x, f, s in d.fields(name)}
    mf, rf, pf, gf = fields('Model'), fields('ModelRegion'), fields('ModelRegionPermutation'), fields('ModelGeometry')
    rsize, psize = d.structs['ModelRegion']['size'], d.structs['ModelRegionPermutation']['size']
    gsize, partsize = d.structs['ModelGeometry']['size'], d.structs['ModelGeometryPart']['size']
    levels_at = [pf[n] for n in ('super low', 'low', 'medium', 'high', 'super high')]
    if levels_at != [0x40, 0x42, 0x44, 0x46, 0x48]:
        raise Refused('a region permutation\'s levels are at %r' % levels_at)
    one_level, parts = [], {}
    for tag in m.tags:
        if tag.group != 'mode' or not m.in_tags(tag.address):
            continue
        base = m.off(tag.address)
        rn, rp = struct.unpack_from('<iI', data, base + mf['regions'])
        geometries, single = set(), True
        for r in range(max(rn, 0)):
            pn, pp = struct.unpack_from('<iI', data, m.off(rp + rsize * r) + rf['permutations'])
            for k in range(max(pn, 0)):
                at = m.off(pp + psize * k)
                g = struct.unpack_from('<5h', data, at + levels_at[0])
                if len(set(g)) != 1:
                    single = False
                geometries.update(g)
        if not single or not geometries:
            continue
        one_level.append((tag.tag_id, sorted(geometries)))
        gn, gp = struct.unpack_from('<iI', data, base + mf['geometries'])
        for g in geometries:
            if not 0 <= g < gn:
                continue
            pn, pp = struct.unpack_from('<iI', data, m.off(gp + gsize * g) + gf['parts'])
            for k in range(max(pn, 0)):
                po = m.off(pp + partsize * k)
                ttype, = struct.unpack_from('<H', data, po + 0x44)
                tcount, = struct.unpack_from('<I', data, po + 0x48)
                thw, = struct.unpack_from('<I', data, po + 0x50)
                vtype, = struct.unpack_from('<H', data, po + 0x54)
                vcount, = struct.unpack_from('<I', data, po + 0x58)
                vhw, = struct.unpack_from('<I', data, po + 0x64)

                def entry(hw):
                    if not m.in_tags(hw) or not m.in_tags(hw + 11):
                        return None
                    return struct.unpack_from('<I', data, m.off(hw) + 4)[0]
                v, i = entry(vhw), entry(thw)
                if v is not None and i is not None:
                    parts.setdefault(v, (tag.tag_id, vcount, vtype, i, tcount, ttype))
    return one_level, parts


# ---------- the levels

def part_levels(m, data, v, vcount, vtype, i, tcount, ttype, census):
    """[(level, records, triangle list)] and [error per level], or None"""
    if vtype not in mc.VERTEX_SIZE:
        return None
    uncompressed = vtype == mc.TYPE_UNCOMPRESSED
    size = mc.VERTEX_SIZE[vtype] * vcount
    raw = data[v - mc.PACK_BASE:v - mc.PACK_BASE + size]
    strip = 1 if ttype == mc.STRIP else 0
    index_count = tcount + 2 if strip else 3 * tcount
    ind = np.frombuffer(data, '<u2', index_count, i - mc.PACK_BASE).astype(np.int64)
    if len(raw) != size or len(ind) != index_count:
        return None
    src = mc.Source(raw, vcount, uncompressed)
    if (src.n1 >= 0).any() or len(np.unique(src.n0)) > 1:
        census['skinned'] += 1
        return None
    tri = np.array(mc.triangle_list(ind, index_count, strip), np.uint32).reshape(-1)
    if len(tri) // 3 < MIN_TRIANGLES:
        census['small'] += 1
        return None
    positions = src.position.copy().view('<f4').reshape(vcount, 3).astype(F)
    uv = src.uv.copy().view('<f4').reshape(vcount, 2)
    normal = mc.unpack_normals(src.packed).astype(F)
    attributes = np.ascontiguousarray(np.concatenate([uv, normal], axis=1), F)
    leaves = pieces(tri, positions) >= LEAF_PIECES * (len(tri) // 3)
    census['leaf parts' if leaves else 'parts'] += 1
    levels, errors = [], [0.0] * LEVELS
    for level in range(LEVELS - 1, -1, -1):
        target = max(3, int(len(tri) * RATIO[level]) // 3 * 3)
        out, err = simplify(m, positions, attributes, tri, target, leaves)
        if len(out) < 3 or len(out) >= len(tri):
            continue
        # the level's own vertices, in first-use order, converted as the top's are
        used, first, inverse = np.unique(out, return_index=True, return_inverse=True)
        order = np.argsort(first)
        remap = np.empty(len(used), np.int64)
        remap[order] = np.arange(len(used))
        local = remap[inverse].astype('<u2')
        rows = used[order]
        stride = mc.VERTEX_SIZE[vtype]
        sub = b''.join(raw[int(r) * stride:(int(r) + 1) * stride] for r in rows)
        records, plan = mc.convert(sub, len(rows), uncompressed, local.astype(np.int64), len(local), 0,
                                   mc.new_census([]))
        if plan is not None:
            continue
        levels.append((level, records, local))
        errors[level] = err
        census['triangles'][level] += len(local) // 3
        census['vertices'][level] += len(rows)
    census['top triangles'] += len(tri) // 3
    census['top vertices'] += vcount
    return levels, errors, positions


def level_block(raw_address, index_address, uncompressed, records, local):
    count = len(records) // 24
    body = bytearray(records)
    body += bytes(mc.up(len(body), 16) - len(body))
    body += local.astype('<u2').tobytes()
    head = struct.pack('<8I5i2I', mc.MAGIC, PCV_LEVEL, count, 24, len(local), 0, uncompressed, raw_address,
                       0, 0, 0, 0, 0, 0, index_address)
    head += bytes(mc.BLOCK_HEADER - len(head))
    out = head + bytes(body)
    return out + bytes(mc.up(len(out), 64) - len(out))


def main(map_path, pgeo_path):
    pack = bytearray(open(pgeo_path, 'rb').read())
    header = struct.unpack_from('<16I', pack, 0)
    if header[:2] != (0x4F454750, 1) or header[15] != mc.MAGIC:
        raise Refused('%s has no baked model buffers (run modelconvert first)' % pgeo_path)
    if struct.unpack_from('<I', pack, len(pack) - 64)[0] == TRAILER_MAGIC:
        print('sspr: %s has its levels already' % pgeo_path)
        return
    start, nbytes, table_at, entries = header[11:15]
    data = bytes(pack[mc.HEADER:])
    table = struct.unpack_from('<%dI' % entries, data, table_at)
    raw_of = {struct.unpack_from('<I', data, a - mc.PACK_BASE + 28)[0]: k for k, a in enumerate(table)}
    one_level, parts = read_models(map_path)
    m = library()
    census = {'parts': 0, 'leaf parts': 0, 'skinned': 0, 'small': 0, 'not baked': 0, 'top triangles': 0,
              'top vertices': 0, 'triangles': [0] * LEVELS, 'vertices': [0] * LEVELS}
    region = mc.up(len(data), 64)
    blocks = bytearray()
    directory = [[0] * (2 * LEVELS) for _ in range(entries)]
    model_errors, model_points = {}, {}
    for v, (tag_id, vcount, vtype, i, tcount, ttype) in sorted(parts.items()):
        k = raw_of.get(v)
        if k is None:
            census['not baked'] += 1
            continue
        got = part_levels(m, data, v, vcount, vtype, i, tcount, ttype, census)
        if not got:
            continue
        levels, errors, positions = got
        for level, records, local in levels:
            b = level_block(v, i, int(vtype == mc.TYPE_UNCOMPRESSED), records, local)
            directory[k][2 * level] = mc.PACK_BASE + region + len(blocks)
            directory[k][2 * level + 1] = len(b)
            blocks += b
        tag_id &= 0xFFFFFFFF    # the map's datum, as the runtime compares it (unsigned)
        e = model_errors.setdefault(tag_id, [0.0] * LEVELS)
        for level in range(LEVELS):
            e[level] = max(e[level], errors[level])
        model_points.setdefault(tag_id, []).append(positions)
    if not blocks:
        print('sspr: no part given levels')
        return
    models = bytearray()
    for tag_id, errors in sorted(model_errors.items()):
        p = np.concatenate(model_points[tag_id])
        center, radius = mc.sphere(p)
        models += struct.pack('<I', tag_id) + np.array([2 * float(radius)] + errors, F).tobytes()
    directory_at = region + len(blocks)
    words = np.array(directory, '<u4').tobytes()
    models_at = directory_at + len(words)
    body = blocks + words + models
    trailer = struct.pack('<8I', TRAILER_MAGIC, region, len(body), directory_at, entries, models_at,
                          len(model_errors), VERSION)
    trailer += bytes(64 - len(trailer))
    pad = region - len(data)
    out = bytes(pack) + bytes(pad) + bytes(body)
    out += bytes(mc.up(len(out), 64) - len(out)) + trailer
    open(pgeo_path, 'wb').write(out)
    c = census
    print('sspr: %d one-level models, %d given levels; parts: %d simplified (%d leaf cards), %d skinned (step 2), '
          '%d too small, %d not baked; top %d triangles %d vertices; levels 3..0 triangles %s vertices %s; '
          'region %d KB' % (len(one_level), len(model_errors), c['parts'] + c['leaf parts'], c['leaf parts'],
                            c['skinned'], c['small'], c['not baked'], c['top triangles'], c['top vertices'],
                            '/'.join(str(x) for x in reversed(c['triangles'])),
                            '/'.join(str(x) for x in reversed(c['vertices'])), len(body) // 1024))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    try:
        main(sys.argv[1], sys.argv[2])
    except Refused as e:
        print('sspr: refused: %s; the pack is left as it was' % e)
        sys.exit(1)
