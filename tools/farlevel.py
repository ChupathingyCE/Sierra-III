"""farlevel.py - each model's far level as one part, baked (docs/box_far_level.md, slice 1).

A model drawn under farlevelN pixels is drawn as one part, untextured, lit by
the GE with the model's own lights on its vertex colours.
This tool makes that part for each model it can:

- the source is each region's lowest level (the tag's super low);
- the parts are merged into one mesh;
- each vertex is coloured from its part's base map, sampled at its uv in a
  small mip;
- the mesh is simplified with meshoptimizer (Arseny Kapoulkine, MIT;
  tools/third_party/meshoptimizer, as tools/sspr.py builds it) until its
  shape error reaches a pixel at the size it's first drawn at
  (FAR_PIXELS);
- it's packed as a strip, with positions quantised to 16 bits in its box
  and colours as 5650.

It takes rigid models (rocks, trees, crates, most scenery, and since slice
2 rigid models of several nodes, one group a node; since slice 3 regions of
several permutations, one group a region's permutation and node), their opaque
parts model shaders (soso) or environment shaders (senv, not alpha-tested); their see-through parts are left out of the far
level, as the far ruling allows. Projectiles' models are left out whole: the
ruling keeps projectile visuals to the horizon. The others are counted by reason and left alone, so they
keep their parts at every distance.

What is appended to the pack (after modelconvert's region, and after
sspr's levels when they're there):
- the far blocks, each a PCV block (modelconvert's 64-byte header) flagged
  PCV_FAR (8):
  - header words: 0 'PCV1', 1 flags, 2 vertex count, 3 stride (14), 4 index
    count, 5-7 the box's centre (float), 8-10 its scale (float, the three
    alike: the box's largest half extent, at least 1), 11
    the node, 12 the merged shape error (float, model units), 13 1 when the
    indices are a triangle list (the strip wasn't shorter), 0 for a strip,
    14 the bytes from this block to the model's next node group (another
    such block), 0 for its last, 15 the group's key: 0xFFFF when it's
    always drawn, else its region | its permutation << 8, drawn when the
    object shows that permutation of that region (version 3);
  - then the vertices, in the GE's order: u16 colour 5650, 3 x s16 normal
    (divided by the scale), 3 x s16 position (centre + q / 32767 x scale);
  - then the strip or the list, u16 indices, padded to 64 bytes;
- the directory, sorted by tag id: for each model, its tag id, its far
  block's pack address and bytes, and its diameter (float, model units);
- a 64-byte trailer: 'FAR1', the region's start (from the data's start, as
  pack addresses are), its bytes, the directory's offset and entries, the
  version, and FAR_PIXELS.

The trailer goes at the file's end. If the pack already ends with sspr's
'SSP1' trailer, that trailer is moved to stay last, and 'FAR1' sits just
before it. So the runtime reads the last 64 bytes, and when they're 'SSP1'
it reads the 64 before them.

usage: farlevel.py <map> <pgeo>   (the pack rewritten in place; the map is read)"""
import ctypes
import os
import struct
import sys
from collections import Counter

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'bake', 'lib'))
import modelconvert as mc  # noqa: E402
import sspr  # noqa: E402  (meshoptimizer, as built for the host)

TRAILER_MAGIC = 0x31524146      # 'FAR1'
SSPR_MAGIC = 0x31505353         # 'SSP1'
VERSION = 3      # 3: groups keyed by region and permutation (word 15); 2: a chain of node groups (word 14)
PCV_FAR = 8
FAR_PIXELS = 32                 # the size the far level is first drawn at (keep word farlevelN's default)
SCREEN_LINES = 272              # the pick's pixels are LODS's
COLOUR_MIP_TEXELS = 16          # the base map's mip sampled: the first at or under this width
MAX_VERTICES = 65535
OBJECT_MODEL = 40                # the object definition's model reference (its tag id at +12)
MAX_GROUPS = 64                  # the draw's mask of groups is 64 bits
ALWAYS = 0xFFFF                  # a group drawn whatever the object's permutations
# meshopt_SimplifyErrorAbsolute | Prune | Permissive (meshoptimizer 1.3)
SIMPLIFY_OPTIONS = (1 << 2) | (1 << 3) | (1 << 5)
F = np.float32

# the definitions' offsets this tool reads, checked against Invader's at the start
MODEL = {'base map u scale': 48, 'base map v scale': 52, 'nodes': 184, 'regions': 196, 'geometries': 208,
         'shaders': 220}
PART = {'shader index': 4, 'triangle buffer type': 0x44, 'triangle count': 0x48, 'triangle offset 2': 0x50,
        'vertex type': 0x54, 'vertex count': 0x58, 'vertex offset': 0x64}
SOSO_BASE_MAP_ID = 176          # shader_model's base map dependency's tag id
SOSO_MAP_SCALE = (156, 160)     # shader_model's map u and v scale
SENV_BASE_MAP_ID = 148          # shader_environment's base map dependency's tag id
SENV_FLAGS = 40                 # shader_environment's flags (bit 0: alpha tested)
ALPHA_TESTED = set()            # alpha-tested senv tag ids (base_colours fills it): see-through
# the see-through shaders' groups (transparent chicago and its extended form,
# generic, glass, meter, plasma, water): their parts aren't drawn far
SEE_THROUGH = {'schi', 'scex', 'sotr', 'sgla', 'smet', 'spla', 'swat'}


class Refused(Exception):
    pass


def check_layouts(d):
    def fields(name):
        return {f.get('name'): x for x, f, s in d.fields(name)}
    for struct_name, want in (('Model', MODEL), ('ModelGeometryPart', PART)):
        got = fields(struct_name)
        for name, at in want.items():
            if got.get(name) != at:
                raise Refused('%s\'s %r is at %r, not %#x' % (struct_name, name, got.get(name), at))
    if fields('Projectile').get('model') != OBJECT_MODEL:
        raise Refused('the projectile\'s model reference moved')
    se = fields('ShaderEnvironment')
    if se.get('base map') != SENV_BASE_MAP_ID - 12 or se.get('shader environment flags') != SENV_FLAGS:
        raise Refused('the shader_environment layout moved')
    sm = fields('ShaderModel')
    if sm.get('base map') != SOSO_BASE_MAP_ID - 12 or (sm.get('map u scale'), sm.get('map v scale')) != SOSO_MAP_SCALE:
        raise Refused('the shader_model layout moved')


# ---------- the maps' colours

def base_colours(m, d, texpack):
    """{soso or opaque senv tag id: (RGBA image of a small mip, map u scale, map v scale)}"""
    by_tag = {}
    for b in texpack.bitmap_list(m, d):
        by_tag.setdefault(b['tag'], []).append(b)
    out = {}
    for t in m.tags:
        if t.group not in ('soso', 'senv') or not t.address:
            continue
        o = m.off(t.address)
        if t.group == 'senv':
            # an environment shader on a model: its base map at the vertex
            # uv; an alpha-tested one is see-through, so left out
            flags, = struct.unpack_from('<H', m.data, o + SENV_FLAGS)
            if flags & 1:
                ALPHA_TESTED.add(t.tag_id & 0xFFFFFFFF)
                continue
            base_id, = struct.unpack_from('<i', m.data, o + SENV_BASE_MAP_ID)
            us = vs = 1.0
        else:
            base_id, = struct.unpack_from('<i', m.data, o + SOSO_BASE_MAP_ID)
            us, vs = struct.unpack_from('<ff', m.data, o + SOSO_MAP_SCALE[0])
        base = m.by_id.get(base_id)
        if not base or base.name not in by_tag:
            continue
        b = by_tag[base.name][0]
        k, w = 0, b['w']
        while w > COLOUR_MIP_TEXELS and k < b['mips']:
            k, w = k + 1, max(1, w // 2)
        img = texpack.decode_level(m, b, k)
        if img is None:
            continue
        out[t.tag_id & 0xFFFFFFFF] = (np.asarray(img, np.float32), us or 1.0, vs or 1.0)
    return out


def sample(img, uv):
    """nearest texels at uv (wrapped), RGB 0..255"""
    h, w = img.shape[:2]
    x = (np.floor(uv[:, 0] * w).astype(np.int64)) % w
    y = (np.floor(uv[:, 1] * h).astype(np.int64)) % h
    return img[y, x, :3]


# ---------- the models

def models(m, d):
    """for each model: (tag id, base map scales, shaders' tag ids, [(geometry
    part's offset, its region and permutation key) for each region's and
    permutation's lowest level]), or (tag id, None, reason)"""
    data = m.data
    rsize = d.structs['ModelRegion']['size']
    psize = d.structs['ModelRegionPermutation']['size']
    gsize = d.structs['ModelGeometry']['size']
    rf = {f.get('name'): x for x, f, s in d.fields('ModelRegion')}
    pf = {f.get('name'): x for x, f, s in d.fields('ModelRegionPermutation')}
    gf = {f.get('name'): x for x, f, s in d.fields('ModelGeometry')}
    for tag in m.tags:
        if tag.group != 'mode' or not m.in_tags(tag.address):
            continue
        base = m.off(tag.address)
        tag_id = tag.tag_id & 0xFFFFFFFF
        us, vs = struct.unpack_from('<ff', data, base + MODEL['base map u scale'])
        sn, sp = struct.unpack_from('<iI', data, base + MODEL['shaders'])
        shaders = [struct.unpack_from('<i', data, m.off(sp + 32 * k) + 12)[0] & 0xFFFFFFFF for k in range(max(sn, 0))]
        rn, rp = struct.unpack_from('<iI', data, base + MODEL['regions'])
        gn, gp = struct.unpack_from('<iI', data, base + MODEL['geometries'])
        parts, why = [], None
        for r in range(max(rn, 0)):
            pn, pp = struct.unpack_from('<iI', data, m.off(rp + rsize * r) + rf['permutations'])
            if pn > 1 and r > 0xFE or pn > 0xFF:
                why = 'permutations'
                break
            # a region of one permutation is always drawn (ALWAYS); one of
            # several keeps each permutation's parts apart, keyed by region
            # and permutation, so the draw takes the object's own
            for q in range(max(pn, 0)):
                g, = struct.unpack_from('<h', data, m.off(pp + psize * q) + pf['super low'])
                if not 0 <= g < gn:
                    why = 'geometry index'
                    break
                key = ALWAYS if pn == 1 else (r | q << 8)
                n, at = struct.unpack_from('<iI', data, m.off(gp + gsize * g) + gf['parts'])
                parts += [(m.off(at + 104 * k), key) for k in range(max(n, 0))]
            if why:
                break
        if why is None and not parts:
            why = 'no parts'
        yield tag_id, (us or 1.0, vs or 1.0), shaders, parts, why


def entry(m, hw):
    if not m.in_tags(hw) or not m.in_tags(hw + 11):
        return None
    return struct.unpack_from('<I', m.data, m.off(hw) + 4)[0]


def merged(m, pack_data, offsets, shaders, colours, scales, census):
    """(positions, normals, colours, triangles, each vertex's node) of the model's merged lowest level, or a reason"""
    P, N, C, T, nodes, keys = [], [], [], [], [], []
    base = 0
    for po, key in offsets:
        data = m.data
        shader_index, = struct.unpack_from('<h', data, po + PART['shader index'])
        ttype, = struct.unpack_from('<H', data, po + PART['triangle buffer type'])
        tcount, = struct.unpack_from('<I', data, po + PART['triangle count'])
        vtype, = struct.unpack_from('<H', data, po + PART['vertex type'])
        vcount, = struct.unpack_from('<I', data, po + PART['vertex count'])
        v = entry(m, struct.unpack_from('<I', data, po + PART['vertex offset'])[0])
        i = entry(m, struct.unpack_from('<I', data, po + PART['triangle offset 2'])[0])
        if v is None or i is None or vtype not in mc.VERTEX_SIZE:
            return 'buffers'
        if not 0 <= shader_index < len(shaders):
            return 'shader index'
        if shaders[shader_index] not in colours:
            tag = m.by_id.get(shaders[shader_index]) or m.by_id.get(shaders[shader_index] - (1 << 32))
            group = tag.group if tag else '?'
            # see-through parts are left out of the far level: the far
            # ruling drops see-through parts under the threshold
            if group in SEE_THROUGH or (shaders[shader_index] & 0xFFFFFFFF) in ALPHA_TESTED:
                census['see-through parts left out'] += 1
                continue
            return 'shader %s' % group
        uncompressed = vtype == mc.TYPE_UNCOMPRESSED
        size = mc.VERTEX_SIZE[vtype] * vcount
        raw = pack_data[v - mc.PACK_BASE:v - mc.PACK_BASE + size]
        strip = ttype == mc.STRIP
        count = tcount + 2 if strip else 3 * tcount
        ind = np.frombuffer(pack_data, '<u2', count, i - mc.PACK_BASE).astype(np.int64)
        if len(raw) != size or len(ind) != count:
            return 'buffers'
        src = mc.Source(raw, vcount, uncompressed)
        if (src.n1 >= 0).any():
            return 'skinned'
        nodes.append(np.asarray(src.n0, np.int64).copy())
        keys.append(np.full(vcount, key, np.int64))
        positions = src.position.copy().view('<f4').reshape(vcount, 3).astype(F)
        normals = mc.unpack_normals(src.packed).astype(F)
        uv = src.uv.copy().view('<f4').reshape(vcount, 2).astype(F)
        img, sus, svs = colours[shaders[shader_index]]
        uv = uv * np.array([scales[0] * sus, scales[1] * svs], F)
        P.append(positions)
        N.append(normals)
        C.append(sample(img, uv))
        T.append(np.array(mc.triangle_list(ind, count, strip), np.int64).reshape(-1, 3) + base)
        base += vcount
    if not P:
        return 'see-through only'
    census['source triangles'] += sum(len(t) for t in T)
    census['source vertices'] += base
    # a group is a node's vertices under one region and permutation key
    group = np.concatenate(keys) << 16 | np.concatenate(nodes)
    return np.concatenate(P), np.concatenate(N), np.concatenate(C), np.concatenate(T).reshape(-1), group


def far_mesh(lib, positions, normals, colours, tri, diameter):
    """(vertex rows used, indices into them, shape error) after simplifying to a pixel at FAR_PIXELS of
    the whole model's diameter (a node's group is simplified alone, to the model's pixel)"""
    if diameter <= 0:
        return None
    limit = diameter / FAR_PIXELS     # one pixel at the size the far level is first drawn at, in model units
    attributes = np.ascontiguousarray(np.concatenate([colours / 255.0, normals[:, :2]], 1), F)
    pos = np.ascontiguousarray(positions, F)
    idx = np.ascontiguousarray(tri, np.uint32)
    out = np.empty_like(idx)
    err = ctypes.c_float(0)
    weights = np.array([0.5, 0.5, 0.5, 0.25, 0.25], F)
    # the error in model units; pieces under it pruned whole, and edges
    # collapsed across colour and normal seams (the far ruling: approximate)
    n = lib.meshopt_simplifyWithAttributes(sspr.ptr(out, ctypes.c_uint), sspr.ptr(idx, ctypes.c_uint), len(idx),
                                           sspr.ptr(pos, ctypes.c_float), len(pos), 12,
                                           sspr.ptr(attributes, ctypes.c_float), 20,
                                           sspr.ptr(weights, ctypes.c_float), 5, None, 3,
                                           F(limit), SIMPLIFY_OPTIONS, ctypes.byref(err))
    if n < 3:
        return None
    out = out[:n].astype(np.int64)
    used, first, inverse = np.unique(out, return_index=True, return_inverse=True)
    order = np.argsort(first)
    remap = np.empty(len(used), np.int64)
    remap[order] = np.arange(len(used))
    return used[order], remap[inverse], float(err.value)


def far_block(positions, normals, colours, local, node, error, key):
    lo, hi = positions.min(0), positions.max(0)
    center = (lo + hi) * 0.5
    # one scale for the three axes, at least 1: the GE lights with the
    # normals as the model matrix turns them, scale included, so they're
    # stored divided by it (and fit while it's at least 1)
    k = max(float(((hi - lo) * 0.5).max()), 1.0)
    half = np.array([k, k, k], F)
    q = np.clip(np.round((positions - center) / k * 32767.0), -32767, 32767).astype('<i2')
    nq = np.clip(np.round(normals / k * 32767.0), -32767, 32767).astype('<i2')
    c = np.clip(np.round(colours), 0, 255).astype(np.int64)
    rgb565 = ((c[:, 2] >> 3) << 11) | ((c[:, 1] >> 2) << 5) | (c[:, 0] >> 3)   # the GE's 5650: red low
    verts = np.zeros(len(positions), dtype=[('c', '<u2'), ('n', '<i2', 3), ('p', '<i2', 3)])   # the GE's order
    verts['n'] = nq
    verts['p'] = q
    verts['c'] = rgb565.astype('<u2')
    tri = [int(x) for x in local]
    strip = mc.strip_of(tri)
    as_list = strip is None    # the strip isn't shorter: drawn as the list
    indices = tri if as_list else strip
    head = struct.pack('<5I', mc.MAGIC, PCV_FAR, len(positions), 14, len(indices))
    head += np.array(list(center) + list(half), '<f4').tobytes()
    head += struct.pack('<IfI', node, error, int(as_list))
    head += struct.pack('<I', 0)   # word 14: bytes to the next node's group, 0 for the last (main fills it)
    head += struct.pack('<I', key)   # word 15: ALWAYS, or region | permutation << 8
    head += bytes(mc.BLOCK_HEADER - len(head))
    body = verts.tobytes() + np.array(indices, '<u2').tobytes()
    out = head + body
    return out + bytes(mc.up(len(out), 64) - len(out)), len(indices)


def main(map_path, pgeo_path):
    from halomap import CacheMap, Defs
    import texpack
    pack = bytearray(open(pgeo_path, 'rb').read())
    header = struct.unpack_from('<16I', pack, 0)
    if header[:2] != (0x4F454750, 1) or header[15] != mc.MAGIC:
        raise Refused('%s has no baked model buffers (run modelconvert first)' % pgeo_path)
    tail = bytes(pack[-64:])
    sspr_trailer = None
    if struct.unpack_from('<I', tail)[0] == SSPR_MAGIC:
        sspr_trailer, pack = tail, pack[:-64]
    if struct.unpack_from('<I', pack, len(pack) - 64)[0] == TRAILER_MAGIC:
        print('farlevel: %s has its far levels already' % pgeo_path)
        return
    m, d = CacheMap(map_path), Defs()
    check_layouts(d)
    data = bytes(pack[mc.HEADER:])
    colours = base_colours(m, d, texpack)
    lib = sspr.library()
    census = Counter()
    region = mc.up(len(data), 64)
    blocks, directory, sizes = bytearray(), [], []
    projectile_models = set()
    for t in m.tags:
        # projectile visuals stay to the horizon (the far ruling), so a
        # projectile's model keeps its parts at every distance
        if t.group == 'proj' and m.in_tags(t.address):
            model_id = struct.unpack_from('<I', m.data, m.off(t.address) + OBJECT_MODEL + 12)[0]
            projectile_models.add(model_id & 0xFFFFFFFF)
    for tag_id, scales, shaders, offsets, why in models(m, d):
        census['models'] += 1
        if tag_id in projectile_models:
            why = 'projectile'
        if why is None:
            got = merged(m, data, offsets, shaders, colours, scales, census)
            why = got if isinstance(got, str) else None
        groups = []
        if why is None:
            positions, normals, cols, tri, vnode = got   # vnode: key << 16 | node
            center, radius = mc.sphere(positions)
            diameter = 2.0 * float(radius)
            tri3 = tri.reshape(-1, 3)
            tnode = vnode[tri3]
            if (tnode != tnode[:, :1]).any():
                why = 'a triangle across nodes'
            elif len(np.unique(tnode[:, 0])) > MAX_GROUPS:
                why = 'groups'
        if why is None:
            # one group a node: each simplified alone and drawn with its
            # node's matrix, so a moving node's parts move as they do near
            for node in np.unique(tnode[:, 0]):
                keep = tnode[:, 0] == node
                mesh = far_mesh(lib, positions, normals, cols, tri3[keep].reshape(-1), diameter)
                if mesh is None:
                    continue   # simplified away at the model's pixel
                if len(mesh[0]) > MAX_VERTICES:
                    why = 'simplifier'
                    break
                groups.append((int(node), mesh))
            if why is None and not groups:
                why = 'simplifier'
        if why is not None:
            census['left: ' + why] += 1
            continue
        model = bytearray()
        for g, (node, (rows, local, error)) in enumerate(groups):
            b, strip_count = far_block(positions[rows], normals[rows], cols[rows], local, node & 0xFFFF, error, node >> 16)
            if g + 1 < len(groups):
                b = bytearray(b)
                struct.pack_into('<I', b, 56, len(b))
            model += b
            census['far vertices'] += len(rows)
            census['far triangles'] += len(local) // 3
            census['far indices'] += strip_count
        directory.append((tag_id, mc.PACK_BASE + region + len(blocks), len(model), diameter))
        tag = m.by_id.get(tag_id) or m.by_id.get(tag_id - (1 << 32))
        sizes.append((len(model), tag.name if tag else '%08x' % tag_id, len(groups)))
        blocks += model
        census['far models'] += 1
        if len(groups) > 1:
            census['far models of several nodes'] += 1
            census['far node groups'] += len(groups)
        if any(k >> 16 != ALWAYS for k, _ in groups):
            census['far models of several permutations'] += 1
    if not directory:
        print('farlevel: no model given a far level (%s)' % dict(census))
        return
    directory.sort()
    words = b''.join(struct.pack('<3If', *e) for e in directory)
    directory_at = region + len(blocks)
    body = blocks + words
    trailer = struct.pack('<6I', TRAILER_MAGIC, region, len(body), directory_at, len(directory), VERSION)
    trailer += struct.pack('<I', FAR_PIXELS)
    trailer += bytes(64 - len(trailer))
    out = bytes(pack) + bytes(region - len(data)) + bytes(body)
    out += bytes(mc.up(len(out), 64) - len(out)) + trailer
    if sspr_trailer:
        out += sspr_trailer
    open(pgeo_path, 'wb').write(out)
    c = census
    left = ', '.join('%s %d' % (k[6:], v) for k, v in sorted(c.items()) if k.startswith('left: '))
    print('farlevel: %d models, %d given a far level at %d px (left: %s; see-through parts left out of them: %d); source %d triangles %d vertices -> '
          'far %d triangles %d vertices, %d indices; %d models of several nodes (%d groups), %d of several permutations; region %d KB' %
          (c['models'], c['far models'], FAR_PIXELS, left or 'none', c['see-through parts left out'], c['source triangles'], c['source vertices'],
           c['far triangles'], c['far vertices'], c['far indices'], c['far models of several nodes'], c['far node groups'], c['far models of several permutations'],
           len(body) // 1024))
    sizes.sort(reverse=True)
    print('farlevel: the largest: ' + '; '.join('%s %.1f KB (%d node%s)' % (name, b / 1024.0, g, '' if g == 1 else 's')
                                                 for b, name, g in sizes[:10]))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    try:
        main(sys.argv[1], sys.argv[2])
    except Refused as e:
        print('farlevel: refused: %s; the pack is left as it was' % e)
        sys.exit(1)
