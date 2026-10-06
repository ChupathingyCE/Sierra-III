import sys, numpy as np
from bspread import load
mats, info = load(sys.argv[1], sys.argv[2])
opaque = [x for x in mats if x['group'] == 'senv']
trans = [x for x in mats if x['group'] != 'senv']
Oall = np.concatenate([x['pos'][x['tris']] for x in opaque])
Oid = np.concatenate([np.full(len(x['tris']), i) for i, x in enumerate(opaque)])
n = np.cross(Oall[:, 1] - Oall[:, 0], Oall[:, 2] - Oall[:, 0]); area = np.linalg.norm(n, axis=1); n = n / np.maximum(area, 1e-12)[:, None]
dpl = (n * Oall[:, 0]).sum(1)
cent = Oall.mean(1)
for x in trans[:12] + trans[-3:]:
    for ti, t in enumerate(x['tris'][:2]):
        P = x['pos'][t]
        tn = np.cross(P[1] - P[0], P[2] - P[0]); L = np.linalg.norm(tn)
        if L < 1e-9: print('  degenerate'); continue
        tn /= L
        c = P.mean(0)
        near = np.argsort(np.linalg.norm(cent - c, axis=1))[:400]
        best = None
        for k in near:
            dist = np.abs(P @ n[k] - dpl[k]).max()
            al = abs(n[k] @ tn)
            # in-plane: is the stencil centroid inside the opaque tri (projected)?
            A, B, C = Oall[k]
            v0, v1, v2 = B - A, C - A, c - A
            d00, d01, d11, d20, d21 = v0 @ v0, v0 @ v1, v1 @ v1, v2 @ v0, v2 @ v1
            den = d00 * d11 - d01 * d01
            if abs(den) < 1e-12: continue
            bv = (d11 * d20 - d01 * d21) / den; bw = (d00 * d21 - d01 * d20) / den
            inside = bv >= -0.05 and bw >= -0.05 and bv + bw <= 1.05
            if inside and (best is None or dist < best[0]):
                best = (dist, al, opaque[Oid[k]]['shader'][-30:])
        print('%-40s tri %d at %s: nearest underlying %s' % (x['shader'][-40:], ti, c.round(2), best))
