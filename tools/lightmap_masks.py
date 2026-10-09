"""lightmap_masks.py - which texels of each lightmap page the level uses
(build 102, the asset track's smaller lightmaps).

A lightmap page holds many charts (patches of the level unwrapped flat) with
dark gutters between them. Shrunk as it is, each chart's edge texels average
in the gutter and the level's seams go dark (Sheepdog01's b47 dark lines, when the
pages had mip levels). So, before shrinking, each page's charts are found by
drawing the level's own lightmap texture coordinates into a mask, and the
colours are grown outward from the charts into the gutters; then the gutters
hold the charts' own edge colours and a box filter mixes nothing dark in.

usage (library):
    masks = page_masks(map_path, pgeo_path)       # {(bitmap tag name, page): bool HxW}
    img = dilate(img, mask, steps)                  # RGBA uint8, grown outward"""
import struct, sys, os
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'bake', 'lib'))  # the bake's readers (halomap.py, ...)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from halomap import Defs, CacheMap

PACK_BASE = 0x0C000000
HEADER = 64


def fo(d, s):
    return {f.get('name'): x for x, f, sz in d.fields(s)}


def _raster(mask, uv, tris):
    """marks every texel whose centre a triangle covers (and the texels its
    corners fall in: a sliver thinner than a texel still owns its texels)"""
    h, w = mask.shape
    p = uv[tris]                                   # (n, 3, 2) in [0, 1]
    px = p[:, :, 0] * w - 0.5
    py = p[:, :, 1] * h - 0.5
    for k in range(3):
        xi = np.clip(np.round(px[:, k]).astype(int), 0, w - 1)
        yi = np.clip(np.round(py[:, k]).astype(int), 0, h - 1)
        mask[yi, xi] = True
    x0 = np.clip(np.floor(px.min(1)).astype(int), 0, w - 1)
    x1 = np.clip(np.ceil(px.max(1)).astype(int), 0, w - 1)
    y0 = np.clip(np.floor(py.min(1)).astype(int), 0, h - 1)
    y1 = np.clip(np.ceil(py.max(1)).astype(int), 0, h - 1)
    for t in range(len(tris)):
        if x1[t] < x0[t] or y1[t] < y0[t]:
            continue
        xs = np.arange(x0[t], x1[t] + 1)
        ys = np.arange(y0[t], y1[t] + 1)
        gx, gy = np.meshgrid(xs, ys)
        (ax, ay), (bx, by), (cx, cy) = zip(px[t], py[t])
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(d) < 1e-12:
            continue
        l1 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / d
        l2 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / d
        l3 = 1 - l1 - l2
        e = -1e-4
        inside = (l1 >= e) & (l2 >= e) & (l3 >= e)
        mask[gy[inside], gx[inside]] = True


def page_masks(map_path, pgeo_path, sizes):
    """sizes: {(tag name, page): (w, h)} of the lightmap pages to mask.
    Returns {(tag name, page): bool array (h, w)}"""
    d = Defs()
    m = CacheMap(map_path)
    pack = open(pgeo_path, 'rb').read() if pgeo_path and os.path.exists(pgeo_path) else b''
    scn = m.tags[m.scenario_id & 0xffff]
    sf = fo(d, 'Scenario')
    bf = fo(d, 'ScenarioStructureBSP')
    lmf = fo(d, 'ScenarioStructureBSPLightmap')
    mf = fo(d, 'ScenarioStructureBSPMaterial')
    masks = {}
    n, p = struct.unpack_from('<iI', m.data, m.off(scn.address) + sf['structure bsps'])
    for which in range(n):
        start, size, addr = struct.unpack_from('<III', m.data, m.off(p) + 32 * which)
        tr = lambda a: start + (a - addr)
        base, = struct.unpack_from('<I', m.data, start)
        so = tr(base)
        lm_tag_id, = struct.unpack_from('<i', m.data, so + bf['lightmaps bitmap'] + 12)
        lm_tag = m.by_id.get(lm_tag_id)
        if not lm_tag:
            continue
        sn, sp = struct.unpack_from('<iI', m.data, so + bf['surfaces'])
        surfaces = np.frombuffer(m.data, '<u2', sn * 3, tr(sp)).reshape(sn, 3)
        ln, lp = struct.unpack_from('<iI', m.data, so + bf['lightmaps'])
        for li in range(ln):
            lo = tr(lp) + 32 * li
            page, = struct.unpack_from('<h', m.data, lo)
            if page < 0 or (lm_tag.name, page) not in sizes:
                continue
            w, h = sizes[(lm_tag.name, page)]
            mask = masks.setdefault((lm_tag.name, page), np.zeros((h, w), bool))
            mn, mp = struct.unpack_from('<iI', m.data, lo + lmf['materials'])
            for mi in range(mn):
                mo = tr(mp) + 256 * mi
                first, count = struct.unpack_from('<ii', m.data, mo + mf['surfaces'])
                rcount, = struct.unpack_from('<I', m.data, mo + mf['rendered vertices count'])
                lcount, = struct.unpack_from('<I', m.data, mo + mf['lightmap vertices count'])
                if not lcount:
                    continue
                ip, = struct.unpack_from('<I', m.data, mo + mf['rendered vertices index pointer'])
                key, = struct.unpack_from('<I', m.data, tr(ip) + 4)
                if key >= PACK_BASE:
                    if not pack:
                        continue
                    raw = pack[HEADER + key - PACK_BASE + rcount * 32: HEADER + key - PACK_BASE + rcount * 32 + lcount * 8]
                else:
                    raw = m.data[tr(key) + rcount * 32: tr(key) + rcount * 32 + lcount * 8]
                lm = np.frombuffer(raw, np.uint8).reshape(lcount, 8)
                st = lm[:, 4:8].copy().view('<i2').reshape(lcount, 2).astype(np.float64) / 32767.0
                tris = surfaces[first:first + count].astype(np.int64)
                tris = tris[(tris < lcount).all(1)]
                _raster(mask, st, tris)
    return masks


def dilate(img, mask, steps=8):
    """the colours of the masked texels grown outward, steps texels"""
    out = img.astype(np.float32).copy()
    have = mask.copy()
    h, w = mask.shape
    for _ in range(steps):
        if have.all():
            break
        acc = np.zeros_like(out)
        cnt = np.zeros((h, w), np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
            src = np.roll(np.roll(out, dy, 0), dx, 1)
            ok = np.roll(np.roll(have, dy, 0), dx, 1)
            # no wrapping across the page's edges
            if dy == -1: ok[-1, :] = False
            if dy == 1: ok[0, :] = False
            if dx == -1: ok[:, -1] = False
            if dx == 1: ok[:, 0] = False
            acc += src * ok[:, :, None]
            cnt += ok
        grow = (~have) & (cnt > 0)
        out[grow] = acc[grow] / cnt[grow][:, None]
        have |= grow
    return np.clip(out + 0.5, 0, 255).astype(np.uint8)
