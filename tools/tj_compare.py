import sys, numpy as np
import stencilbake as sb
from scipy.spatial import cKDTree
def tj_list(F):
    verts = np.unique(F.reshape(-1, 3), axis=0); tree=cKDTree(verts); out=[]
    for ti,T in enumerate(F):
        for j in range(3):
            a,b=T[j],T[(j+1)%3]; L=np.linalg.norm(b-a)
            if L<1e-9: continue
            for k in tree.query_ball_point((a+b)/2, L/2+1e-6):
                p=verts[k]
                if any(np.array_equal(p,T[i]) for i in range(3)): continue
                t=(p-a)@(b-a)/L**2
                if t<=1e-6 or t>=1-1e-6: continue
                if np.linalg.norm(a+t*(b-a)-p)<1e-5: out.append((ti,j,tuple(p)))
    return out
m_, g_ = sys.argv[1], sys.argv[2]
r = sb.main(m_, g_, None)
walls=r['walls']
orig=np.array([m['pos32'].astype(np.float64)[list(t)] for m in walls for t in m['tris']])
o=tj_list(orig)
added=set()
for wi,W in r['out_walls'].items():
    for a in W['added']: added.add(tuple(np.float64(a[:3])))
final=[]
for wi,m in enumerate(walls):
    W=r['out_walls'].get(wi); pos=m['pos32'].astype(np.float64)
    if W and W['added']: pos=np.concatenate([pos,np.array([a[:3] for a in W['added']],np.float64)])
    rep={t:tr for t,tr in W['affected']} if W else {}
    for t in m['tris']:
        k=tuple(int(v) for v in t)
        if k in rep: final += [pos[list(p)] for p in rep[k]]
        else: final.append(pos[list(k)])
f=tj_list(np.array(final))
new=[x for x in f if x[2] in added]
print('original %d, after %d, involving added vertices %d' % (len(o), len(f), len(new)))
for x in new[:6]: print('  ', x)
