import sys, numpy as np
from bspread import load
mats, info = load(sys.argv[1], sys.argv[2])
opaque = [x for x in mats if x['group'] == 'senv']
trans = [x for x in mats if x['group'] != 'senv']
# all opaque triangles
O = []
for mi, x in enumerate(opaque):
    p = x['pos'][x['tris']]
    O.append(p)
Oall = np.concatenate(O)
Oid = np.concatenate([np.full(len(x['tris']), i) for i, x in enumerate(opaque)])
Otri = np.concatenate([np.arange(len(x['tris'])) for x in opaque])
n = np.cross(Oall[:, 1] - Oall[:, 0], Oall[:, 2] - Oall[:, 0]); area = np.linalg.norm(n, axis=1); n = n / np.maximum(area, 1e-12)[:, None]
dpl = (n * Oall[:, 0]).sum(1)
for x in trans:
    hits = {}
    for ti, t in enumerate(x['tris']):
        P = x['pos'][t]
        tn = np.cross(P[1] - P[0], P[2] - P[0]); tn /= np.linalg.norm(tn)
        align = np.abs(n @ tn)
        dist = np.abs(P @ n.T - dpl[None, :]).max(0)     # max over the stencil's corners
        # bounding box overlap
        lo = P.min(0) - 0.01; hi = P.max(0) + 0.01
        olo = Oall.min(1); ohi = Oall.max(1)
        box = np.all(olo <= hi, 1) & np.all(ohi >= lo, 1)
        c = np.nonzero((align > 0.995) & (dist < 0.03) & box)[0]
        for k in c:
            hits.setdefault(ti, []).append((int(Oid[k]), int(Otri[k]), float(dist[k]), float(align[k])))
    on = len(hits); tot = len(x['tris'])
    dmax = max([h[2] for v in hits.values() for h in v], default=-1)
    walls = sorted(set((opaque[h[0]]['lightmap_index'], opaque[h[0]]['material_index']) for v in hits.values() for h in v))
    print('%-55s %2d/%2d triangles on a wall (max gap %.4f wu); wall materials %s; wall tris %d' % (x['shader'][-50:], on, tot, dmax, walls, sum(len(v) for v in hits.values())))
