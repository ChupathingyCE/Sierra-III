"""checks a stencil bake: pieces cover each replaced triangle exactly, keep
its winding, and add no T-junctions to the level (all materials together)"""
import sys, numpy as np, collections
import stencilbake as sb

def tri_area_n(P):
    n = np.cross(P[1] - P[0], P[2] - P[0]); return n

def main(mp, pg):
    r = sb.main(mp, pg, None)
    walls = r['walls']
    problems = collections.Counter()
    # final triangle list per material, positions
    final = []
    for wi, m in enumerate(walls):
        W = r['out_walls'].get(wi)
        pos = m['pos32'].astype(np.float64)
        if W and W['added']:
            pos = np.concatenate([pos, np.array([a[:3] for a in W['added']], np.float64)])
        replaced = {}
        if W:
            for triple, tris in W['affected']:
                replaced[triple] = tris
        for t in m['tris']:
            key = tuple(int(v) for v in t)
            if key in replaced:
                P = pos[list(key)]
                n0 = tri_area_n(P); a0 = np.linalg.norm(n0) / 2
                a1 = 0.0
                for pt in replaced[key]:
                    Q = pos[list(pt)]
                    n1 = tri_area_n(Q)
                    if np.linalg.norm(n1) > 1e-12 and n1 @ n0 < 0:
                        problems['piece wound the other way'] += 1
                    a1 += np.linalg.norm(n1) / 2
                    final.append(Q)
                if abs(a1 - a0) > 1e-4 * max(a0, 1e-6) + 1e-7:
                    problems['area mismatch'] += 1
            else:
                final.append(pos[list(key)])
    F = np.array(final)
    # T-junctions: a vertex strictly inside another triangle's edge
    verts = np.unique(F.reshape(-1, 3), axis=0)
    from scipy.spatial import cKDTree
    tree = cKDTree(verts)
    tj = 0
    for T in F:
        for j in range(3):
            a, b = T[j], T[(j + 1) % 3]
            L = np.linalg.norm(b - a)
            if L < 1e-9:
                continue
            mid = (a + b) / 2
            for k in tree.query_ball_point(mid, L / 2 + 1e-6):
                p = verts[k]
                if any(np.array_equal(p, T[i]) for i in range(3)):
                    continue   # the triangle's own corner (a sliver), not a T-junction
                t = (p - a) @ (b - a) / L ** 2
                if t <= 1e-6 or t >= 1 - 1e-6:
                    continue
                d = np.linalg.norm(a + t * (b - a) - p)
                if d < 1e-5:
                    tj += 1
    return problems, tj, len(F)

if __name__ == '__main__':
    p, tj, n = main(sys.argv[1], sys.argv[2])
    print('triangles %d; T-junctions (vertex inside an edge) %d; problems %s' % (n, tj, dict(p)))
