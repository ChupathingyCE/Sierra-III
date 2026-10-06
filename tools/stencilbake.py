"""stencilbake.py - bakes the level's wall stencils into the walls' geometry
(build 87).

The problem: a "stencil" (a light strip on a base wall, the cave's fungus) is
its own see-through surface, a few millimetres to centimetres in front of the
wall. The Xbox's 24-bit depth sorts that out; the PSP's 16 bits can't
(at 20 units a depth step is centimetres), so the stencil sank into the
wall or showed through things in front of it, by the view, whatever nudge
we tried (builds 52-85).

The fix here: cut the wall's triangles along each stencil's outline, so the
part of the wall under a stencil is its own triangles. The wall draws as
before, from those pieces; the stencil is then drawn on the very same
pieces (same positions, same matrices), so its depth equals the wall's
exactly, as the lightmap and detail passes already do (depth EQUAL).

Output, <map>.pstn next to the map, read by psp_ge.c (stencil_bake_*):
	header (64 bytes): 'PSTN', version 1, the pack's map checksum, counts
	walls: per wall material (its pack key): the vertices added (position,
	  base and lightmap texture coordinates, packed normal), the triangles
	  replaced (the game's index triple) and the pieces each becomes
	layers: per (wall, stencil material): the stencil's vertices on the
	  pieces (its texture coordinates, the same positions) and triangles,
	  each tagged with the wall triangle it lies on (drawn only when that one
	  is visible this frame)
	stencils: per stencil material (its key), the triangles now drawn from
	  the layers (the game's index triples); the rest keep the old path

usage: python3 stencilbake.py map.map map.pgeo out.pstn [--report]"""
import sys, os, struct, math, collections
import numpy as np
import triangle
from shapely.geometry import LineString
from shapely.ops import unary_union
from bspread import load

PLANE_GAP = 0.06        # a stencil's corners within this of the wall's plane (units; 18 cm)
BEHIND = 0.004          # ...and not more than this behind it
ALIGN = 0.98            # normals within ~11 degrees
COVER = 0.98            # a stencil triangle is baked only if walls cover this share of it
EPS2D = 1e-6            # 2D tolerances (units)
ON_EDGE = 1e-4   # 0.3 mm: a point this close to a wall edge is on it


def f32(v):
    return float(np.float32(v))


def tri_normal(P):
    n = np.cross(P[1] - P[0], P[2] - P[0])
    L = np.linalg.norm(n)
    return (n / L, L / 2) if L > 1e-12 else (None, 0.0)


def poly_area(poly):
    a = 0.0
    for i in range(len(poly)):
        x0, y0 = poly[i]; x1, y1 = poly[(i + 1) % len(poly)]
        a += x0 * y1 - x1 * y0
    return a / 2


def clip_half(poly, a, b, keep_left):
    """the part of convex poly on the left (keep_left) or right of line a->b"""
    out = []
    n = len(poly)
    ax, ay = a; bx, by = b
    def side(p):
        return (bx - ax) * (p[1] - ay) - (by - ay) * (p[0] - ax)
    for i in range(n):
        p, q = poly[i], poly[(i + 1) % n]
        sp, sq = side(p), side(q)
        if not keep_left:
            sp, sq = -sp, -sq
        pin, qin = sp >= -EPS2D, sq >= -EPS2D
        if pin:
            out.append(p)
        if (sp > EPS2D and sq < -EPS2D) or (sp < -EPS2D and sq > EPS2D):
            t = sp / (sp - sq)
            out.append((p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])))
    # drop repeats
    clean = []
    for p in out:
        if not clean or abs(p[0] - clean[-1][0]) > EPS2D or abs(p[1] - clean[-1][1]) > EPS2D:
            clean.append(p)
    if len(clean) > 1 and abs(clean[0][0] - clean[-1][0]) <= EPS2D and abs(clean[0][1] - clean[-1][1]) <= EPS2D:
        clean.pop()
    return clean if len(clean) >= 3 and abs(poly_area(clean)) > 1e-9 else []


def split_by_triangle(poly, tri):
    """convex poly against a CCW triangle: (outside pieces, inside piece or [])"""
    outside, rest = [], poly
    for k in range(3):
        a, b = tri[k], tri[(k + 1) % 3]
        o = clip_half(rest, a, b, False)
        if o:
            outside.append(o)
        rest = clip_half(rest, a, b, True)
        if not rest:
            break
    return outside, rest


def bary2(p, a, b, c):
    d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    l0 = ((b[1] - c[1]) * (p[0] - c[0]) + (c[0] - b[0]) * (p[1] - c[1])) / d
    l1 = ((c[1] - a[1]) * (p[0] - c[0]) + (a[0] - c[0]) * (p[1] - c[1])) / d
    return l0, l1, 1.0 - l0 - l1


class Wall:
    """one opaque material's triangles and what the bake adds to them"""

    def __init__(self, mat):
        self.mat = mat
        self.n_original = mat['rcount']
        self.added = []          # [x, y, z, u, v, lu, lv, normal]
        self.affected = {}       # triangle index -> list of polygons [(vertex ids...)] + layer ids per polygon


def lightmap_uv(mat, i):
    if mat['lmraw'] is None:
        return (0.0, 0.0)
    t = np.frombuffer(mat['lmraw'][i, 4:8].tobytes(), '<i2')
    return ((t[0] * 2.0 + 1.0) / 65535.0, (t[1] * 2.0 + 1.0) / 65535.0)


def main(map_path, pgeo_path, out_path, report=False):
    mats, info = load(map_path, pgeo_path)
    pack_crc = struct.unpack_from('<I', open(pgeo_path, 'rb').read(64), 8)[0]
    walls_m = [m for m in mats if m['group'] == 'senv']
    stencils_m = [m for m in mats if m['group'] in ('sotr', 'schi', 'scex')]
    # every opaque triangle, with its plane
    W_tris = []   # (wall index, triangle index, P (3x3), normal, d)
    for wi, m in enumerate(walls_m):
        for ti, t in enumerate(m['tris']):
            P = m['pos'][t]
            n, area = tri_normal(P)
            if n is None:
                continue
            W_tris.append((wi, ti, P, n, float(n @ P[0])))
    WP = np.array([w[2] for w in W_tris]); WN = np.array([w[3] for w in W_tris]); WD = np.array([w[4] for w in W_tris])
    Wlo = WP.min(1); Whi = WP.max(1)
    # 1. which wall triangles each stencil triangle lies on, and how much of it they cover
    overlaps = collections.defaultdict(list)   # (wall index, tri) -> [(stencil mat index, stencil tri, 2D tri in wall plane, uvs)]
    baked_stencil_tris = collections.defaultdict(set)   # stencil mat index -> {tri index}
    report_lines = []
    signs = []
    for si, s in enumerate(stencils_m):
        for sti, st in enumerate(s['tris']):
            P = s['pos'][st]
            n, area = tri_normal(P)
            if n is None or area < 1e-8:
                continue
            lo = P.min(0) - PLANE_GAP; hi = P.max(0) + PLANE_GAP
            box = np.all(Wlo <= hi, 1) & np.all(Whi >= lo, 1)
            cand = np.nonzero(box & (np.abs(WN @ n) > ALIGN))[0]
            found = []
            covered = 0.0
            for k in cand:
                wi, ti, WPk, wn, wd = W_tris[k]
                dist = P @ wn - wd           # signed, along the wall triangle's normal
                if dist.max() > PLANE_GAP or dist.min() < -PLANE_GAP:
                    continue
                # 2D frame of the wall triangle
                e1 = WPk[1] - WPk[0]; e1 /= np.linalg.norm(e1); e2 = np.cross(wn, e1)
                to2 = lambda X: ((X - WPk[0]) @ e1, (X - WPk[0]) @ e2)
                wpoly = [to2(WPk[0]), to2(WPk[1]), to2(WPk[2])]
                if poly_area(wpoly) < 0:
                    wpoly = wpoly[::-1]
                spoly = [to2(P[0]), to2(P[1]), to2(P[2])]
                suv = [tuple(s['uv'][st[0]]), tuple(s['uv'][st[1]]), tuple(s['uv'][st[2]])]
                if poly_area(spoly) < 0:
                    spoly = spoly[::-1]; suv = suv[::-1]
                inter = wpoly
                for j in range(3):
                    inter = clip_half(inter, spoly[j], spoly[(j + 1) % 3], True)
                    if not inter:
                        break
                if not inter:
                    continue
                a = abs(poly_area(inter))
                if a < 1e-7:
                    continue
                found.append((wi, ti, k, spoly, suv, dist, a))
            # the side: a stencil lies in front of its wall. The level's front
            # faces wind clockwise (the Xbox culls D3DCULL_CCW), so a wall
            # triangle's front is against its cross-product normal: a stencil
            # in front has a negative signed gap. Only walls it lies in front
            # of count (the back face of a thin panel isn't painted too)
            use = [f for f in found if f[5].mean() <= BEHIND and f[5].max() <= 0.03 and f[5].min() >= -PLANE_GAP]
            if found:
                signs.append(len(use))
                covered = sum(f[6] for f in use)
                s_area2d = abs(poly_area(found[0][3]))
                if s_area2d > 0 and covered >= COVER * s_area2d and covered <= (2.0 - COVER) * s_area2d:
                    baked_stencil_tris[si].add(sti)
                    for (wi, ti, k, spoly, suv, dist, a) in use:
                        overlaps[(wi, ti)].append((si, sti, spoly, suv))
                report_lines.append('%-50s tri %3d: %d walls under it, covered %.3f of %.3f -> %s; gap %.4f..%.4f' % (
                    s['shader'][-50:], sti, len(use), covered, s_area2d,
                    'baked' if sti in baked_stencil_tris[si] else 'kept as before',
                    min(f[5].min() for f in use) if use else 0, max(f[5].max() for f in use) if use else 0))
    # 2. per wall triangle under stencils: the outline of each stencil's part
    #    on it. Where an outline meets the triangle's edge, a point every
    #    triangle along that edge gets too (one 3D position, other materials'
    #    triangles included), so no edge carries a vertex its neighbour lacks
    #    (a T-junction cracks on the GE)
    edge_points = collections.defaultdict(dict)    # canonical edge -> {t (rounded): position (f32 tuple)}
    outlines = {}   # (wi, ti) -> list of (inside polygon 2D, (si, sti, sp, su))
    for (wi, ti), lst in overlaps.items():
        fr = frame_of(walls_m[wi], ti)
        outs = []
        for (si, sti, spoly, suv) in lst:
            P = stencils_m[si]['pos'][stencils_m[si]['tris'][sti]]
            sp = [fr['to2'](P[0]), fr['to2'](P[1]), fr['to2'](P[2])]
            su = [tuple(float(c) for c in stencils_m[si]['uv'][stencils_m[si]['tris'][sti][j]]) for j in range(3)]
            if poly_area(sp) < 0:
                sp = sp[::-1]; su = su[::-1]
            inside = fr['poly']
            for j in range(3):
                inside = clip_half(inside, sp[j], sp[(j + 1) % 3], True)
                if not inside:
                    break
            if not inside:
                continue
            outs.append((inside, (si, sti, sp, su)))
            for q in inside:
                e = fr['on_edge'](q)
                if e is not None:
                    register_edge_point(edge_points, e[0], e[1], e[2])
        outlines[(wi, ti)] = outs
    # every opaque triangle with points on its edges is affected too
    affected = dict(outlines)
    neighbours = 0
    for wi, m in enumerate(walls_m):
        for ti, t in enumerate(m['tris']):
            if (wi, ti) in affected:
                continue
            P = m['pos32'][t]
            if any(edge_key(P[j], P[(j + 1) % 3]) in edge_points for j in range(3)):
                if frame_of(m, ti) is not None:
                    affected[(wi, ti)] = []
                    neighbours += 1
    # 3. each affected triangle: a constrained triangulation of its corners,
    #    its edge points and the stencils' outlines (noded where they cross),
    #    each piece knowing which stencils lie over it
    out_walls = {}
    out_layers = collections.defaultdict(lambda: dict(vertices=[], index={}, tris=[]))
    stats = collections.Counter()
    for (wi, ti), outs in sorted(affected.items()):
        m = walls_m[wi]
        fr = frame_of(m, ti)
        t = m['tris'][ti]
        W = out_walls.setdefault(wi, dict(added=[], index={}, affected=[]))
        verts = []      # (x, y, ident)
        vindex = {}
        rank = {'o': 0, 'e': 1, 'i': 2}
        def add_vertex(x, y, ident):
            k = ident if ident[0] != 'i' else ('i', round(x, 6), round(y, 6))
            if k in vindex:
                return vindex[k]
            # a point within 0.03 mm of one already there is that one (Triangle
            # crashes on coincident points; near-coincident ones make slivers).
            # A corner beats an edge point beats an interior point
            for n_, (vx, vy, vk) in enumerate(verts):
                if abs(vx - x) < 1e-5 and abs(vy - y) < 1e-5:
                    if rank[k[0]] < rank[vk[0]]:
                        verts[n_] = (x, y, k)
                    elif k[0] != 'i' and vk[0] != 'i' and k != vk:
                        continue   # two different shared points: both stay
                    vindex[k] = n_
                    return n_
            vindex[k] = len(verts)
            verts.append((x, y, k))
            return vindex[k]
        def classify(q):
            c = fr['corner_of'](q)
            if c is not None:
                return fr['poly_src'][c][0], fr['poly_src'][c][1], ('o', int(t[c]))
            e = fr['on_edge'](q)
            if e is not None:
                key, tt, pos = e
                tt = register_edge_point(edge_points, key, tt, pos)
                pos = edge_points[key][tt]
                x, y = fr['to2'](np.array(pos, np.float64))
                return x, y, ('e', key, tt)
            return q[0], q[1], ('i',)
        segs = set()
        # the triangle's boundary: corners and every point on each edge, in order
        for j in range(3):
            a_id = add_vertex(fr['poly_src'][j][0], fr['poly_src'][j][1], ('o', int(t[j])))
            key = edge_key(m['pos32'][t[j]], m['pos32'][t[(j + 1) % 3]])
            fwd = tuple(float(v) for v in m['pos32'][t[j]]) == key[0]
            pts = sorted(edge_points.get(key, {}).items(), key=lambda kv: kv[0] if fwd else -kv[0])
            prev = a_id
            for tt, pos in pts:
                x, y = fr['to2'](np.array(pos, np.float64))
                cur = add_vertex(x, y, ('e', key, tt))
                if cur != prev:
                    segs.add((min(prev, cur), max(prev, cur)))
                prev = cur
            b_id = add_vertex(fr['poly_src'][(j + 1) % 3][0], fr['poly_src'][(j + 1) % 3][1], ('o', int(t[(j + 1) % 3])))
            if b_id != prev:
                segs.add((min(prev, b_id), max(prev, b_id)))
        # the stencils' outlines, noded against each other
        lines = []
        for inside, layer in outs:
            for k in range(len(inside)):
                a_, b_ = inside[k], inside[(k + 1) % len(inside)]
                ca, cb = fr['corner_of'](a_), fr['corner_of'](b_)
                ea, eb = fr['on_edge'](a_), fr['on_edge'](b_)
                # outline edges lying along the triangle's own edges are already there
                if (ca is not None or ea is not None) and (cb is not None or eb is not None):
                    ja = ca if ca is not None else None
                    mid = ((a_[0] + b_[0]) / 2, (a_[1] + b_[1]) / 2)
                    if fr['on_edge'](mid) is not None:
                        continue
                lines.append(LineString([a_, b_]))
        if lines:
            noded = unary_union(lines)
            geoms = getattr(noded, 'geoms', [noded])
            for g in geoms:
                cs = list(g.coords)
                for k in range(len(cs) - 1):
                    xa, ya, ia = classify(cs[k]); xb, yb, ib = classify(cs[k + 1])
                    va = add_vertex(xa, ya, ia); vb = add_vertex(xb, yb, ib)
                    if va != vb:
                        segs.add((min(va, vb), max(va, vb)))
        V = np.array([(x, y) for x, y, _ in verts], np.float64)
        S = np.array(sorted(segs), np.int32).reshape(-1, 2)
        if os.environ.get('STENCIL_DEBUG'):
            np.savez('/tmp/claude-0/tri_in.npz', V=V, S=S)
        try:
            T = triangle.triangulate({'vertices': V, 'segments': S}, 'pQ')
        except Exception as ex:
            stats['triangulation failed'] += 1
            continue
        if len(T['vertices']) != len(V):
            # Triangle added points (crossing segments the noding missed): the
            # new ones are interior points like any other
            for k in range(len(V), len(T['vertices'])):
                x, y = T['vertices'][k]
                verts.append((x, y, ('i', round(x, 6), round(y, 6))))
            stats['triangle added points'] += len(T['vertices']) - len(V)
        tri_out = []
        layer_out = collections.defaultdict(list)
        for tri in T['triangles']:
            ids = []
            recs = []
            for vi in tri:
                x, y, ident = verts[vi]
                if ident[0] == 'o':
                    ids.append(ident[1])
                    pos = tuple(float(c) for c in m['pos32'][ident[1]])
                    recs.append(pos)
                    continue
                if ident[0] == 'e':
                    pos = edge_points[ident[1]][ident[2]]
                else:
                    X = fr['to3']((x, y))
                    pos = (f32(X[0]), f32(X[1]), f32(X[2]))
                x2, y2 = fr['to2'](np.array(pos, np.float64))
                l0, l1, l2 = bary2((x2, y2), *fr['poly_src'])
                uv = l0 * m['uv'][t[0]].astype(np.float64) + l1 * m['uv'][t[1]] + l2 * m['uv'][t[2]]
                lm = [lightmap_uv(m, int(t[j])) for j in range(3)]
                lu = l0 * lm[0][0] + l1 * lm[1][0] + l2 * lm[2][0]
                lv = l0 * lm[0][1] + l1 * lm[1][1] + l2 * lm[2][1]
                nearest = int(t[int(np.argmax([l0, l1, l2]))])
                rec = (pos[0], pos[1], pos[2], f32(uv[0]), f32(uv[1]), f32(lu), f32(lv), int(m['normal'][nearest]))
                dkey = (pos, rec[3], rec[4], rec[5], rec[6])
                if dkey not in W['index']:
                    W['index'][dkey] = m['rcount'] + len(W['added'])
                    W['added'].append(rec)
                ids.append(W['index'][dkey])
                recs.append(pos)
            # the triangle's area in 2D, and its winding against the wall's own
            pa = V_or(verts, tri)
            if abs(pa) < 1e-10:
                stats['slivers dropped'] += 1
                continue
            # 2D is counter-clockwise when not flipped: the wall's winding is
            # the source corners' order (counter-clockwise in 2D unless flipped)
            order = (0, 1, 2) if (pa > 0) == (not fr['flipped']) else (0, 2, 1)
            tri_ids = tuple(ids[k] for k in order)
            tri_out.append(tri_ids)
            cx = sum(verts[v][0] for v in tri) / 3; cy = sum(verts[v][1] for v in tri) / 3
            for inside, (si, sti, sp, su) in outs:
                l0, l1, l2 = bary2((cx, cy), *sp)
                if min(l0, l1, l2) < -1e-9:
                    continue
                L = out_layers[(wi, si)]
                lids = []
                for k in order:
                    pos = recs[k]
                    x2, y2 = fr['to2'](np.array(pos, np.float64))
                    b0, b1, b2 = bary2((x2, y2), *sp)
                    su_ = b0 * su[0][0] + b1 * su[1][0] + b2 * su[2][0]
                    sv_ = b0 * su[0][1] + b1 * su[1][1] + b2 * su[2][1]
                    rec = (f32(su_), f32(sv_), pos[0], pos[1], pos[2])
                    if rec not in L['index']:
                        L['index'][rec] = len(L['vertices'])
                        L['vertices'].append(rec)
                    lids.append(L['index'][rec])
                layer_out[(wi, si)].append(tuple(lids))
        affected_index = len(W['affected'])
        W['affected'].append((tuple(int(v) for v in t), tri_out))
        for key2, lt in layer_out.items():
            for tri in lt:
                out_layers[key2]['tris'].append((tri, affected_index))
        stats['triangles replaced'] += 1
        stats['pieces'] += len(tri_out)
    stats['neighbours (edge points only)'] = neighbours
    return dict(mats=mats, walls=walls_m, stencils=stencils_m, overlaps=overlaps,
                baked=baked_stencil_tris, report=report_lines, signs=signs, pack_crc=pack_crc,
                out_walls=out_walls, out_layers=out_layers, stats=stats)


def V_or(verts, tri):
    (x0, y0, _), (x1, y1, _), (x2, y2, _) = verts[tri[0]], verts[tri[1]], verts[tri[2]]
    return ((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)) / 2


def register_edge_point(edge_points, key, t, pos):
    """the point at t along a canonical edge; one within 0.06 mm of a point
    already there is that point (adjacent stencils meet an edge at nearly the
    same place)"""
    L = math.dist(key[0], key[1])
    pts = edge_points[key]
    for tt in pts:
        if abs(tt - t) * L < 2e-5:
            return tt
    tt = round(t, 9)
    pts[tt] = pos
    return tt


def edge_key(p, q):
    a = tuple(float(v) for v in p); b = tuple(float(v) for v in q)
    return (a, b) if a <= b else (b, a)


_frames = {}


def frame_of(m, ti):
    """a wall triangle's 2D frame: to2 / to3, its CCW polygon, which corner or
    edge a 2D point is, and the canonical 3D position of an edge point"""
    k = (id(m), ti)
    if k in _frames:
        return _frames[k]
    t = m['tris'][ti]
    P = m['pos32'][t].astype(np.float64)
    n, area = tri_normal(P)
    if n is None:
        _frames[k] = None
        return None
    e1 = P[1] - P[0]; e1 /= np.linalg.norm(e1); e2 = np.cross(n, e1)
    origin = P[0].copy()
    to2 = lambda X: (float((X - origin) @ e1), float((X - origin) @ e2))
    to3 = lambda q: origin + q[0] * e1 + q[1] * e2
    src = [to2(P[0]), to2(P[1]), to2(P[2])]
    flipped = poly_area(src) < 0
    poly = src[::-1] if flipped else src
    def corner_of(q):
        for j in range(3):
            if abs(q[0] - src[j][0]) < ON_EDGE and abs(q[1] - src[j][1]) < ON_EDGE:
                return j
        return None
    def on_edge(q):
        if corner_of(q) is not None:
            return None
        for j in range(3):
            a, b = src[j], src[(j + 1) % 3]
            L2 = (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2
            tt = ((q[0] - a[0]) * (b[0] - a[0]) + (q[1] - a[1]) * (b[1] - a[1])) / L2
            if tt <= 0 or tt >= 1:
                continue
            d = abs((b[0] - a[0]) * (q[1] - a[1]) - (b[1] - a[1]) * (q[0] - a[0])) / math.sqrt(L2)
            if d < ON_EDGE:
                p32, q32 = m['pos32'][t[j]], m['pos32'][t[(j + 1) % 3]]
                key = edge_key(p32, q32)
                # t along the canonical direction (from the key's first end)
                if tuple(float(v) for v in p32) != key[0]:
                    tt = 1.0 - tt
                A = np.array(key[0], np.float64); B = np.array(key[1], np.float64)
                X = A + tt * (B - A)
                return key, tt, (f32(X[0]), f32(X[1]), f32(X[2]))
        return None
    fr = dict(to2=to2, to3=to3, poly=poly, poly_src=src, flipped=flipped, corner_of=corner_of, on_edge=on_edge)
    _frames[k] = fr
    return fr


def write(r, out_path):
    walls_m, stencils_m = r['walls'], r['stencils']
    blob = bytearray()
    walls = sorted(r['out_walls'].items())
    layers = sorted(r['out_layers'].items())
    stencils = [(si, sorted(tris)) for si, tris in sorted(r['baked'].items()) if tris]
    body = bytearray()
    for wi, W in walls:
        m = walls_m[wi]
        pieces = []
        aff = bytearray()
        for triple, tris in W['affected']:
            aff += struct.pack('<HHHHHH', triple[0], triple[1], triple[2], len(pieces), len(tris), 0)
            pieces += tris
        rec = struct.pack('<IIIII', m['key'], m['rcount'], len(W['added']), len(W['affected']), len(pieces))
        for v in W['added']:
            rec += struct.pack('<7fI', *v)
        rec += aff
        for p in pieces:
            rec += struct.pack('<HHH', *p)
        while len(rec) % 4:
            rec += b'\0'
        body += rec
    for (wi, si), L in layers:
        rec = struct.pack('<IIII', walls_m[wi]['key'], stencils_m[si]['key'], len(L['vertices']), len(L['tris']))
        for v in L['vertices']:
            rec += struct.pack('<5f', *v)
        for tri, a in L['tris']:
            rec += struct.pack('<HHHH', tri[0], tri[1], tri[2], a)
        body += rec
    for si, tris in stencils:
        s = stencils_m[si]
        rec = struct.pack('<II', s['key'], len(tris))
        for ti in tris:
            t = s['tris'][ti]
            rec += struct.pack('<HHH', int(t[0]), int(t[1]), int(t[2]))
        while len(rec) % 4:
            rec += b'\0'
        body += rec
    header = struct.pack('<4sIIIIII', b'PSTN', 1, r['pack_crc'], len(walls), len(layers), len(stencils), len(body))
    header += b'\0' * (64 - len(header))
    open(out_path, 'wb').write(header + body)
    return len(header) + len(body)


if __name__ == '__main__':
    r = main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    if '--report' in sys.argv:
        for l in r['report']:
            print(l)
    print('stencil triangles baked: %d of %d' % (sum(len(v) for v in r['baked'].values()),
          sum(len(s['tris']) for s in r['stencils'])))
    for k, v in sorted(r['stats'].items()):
        print('  %s: %d' % (k, v))
    print('  walls %d, vertices added %d, layers %d (%d triangles, %d vertices)' % (
        len(r['out_walls']), sum(len(W['added']) for W in r['out_walls'].values()), len(r['out_layers']),
        sum(len(L['tris']) for L in r['out_layers'].values()), sum(len(L['vertices']) for L in r['out_layers'].values())))
    if len(sys.argv) > 3 and not sys.argv[3].startswith('--'):
        n = write(r, sys.argv[3])
        print('wrote %s (%d bytes)' % (sys.argv[3], n))
