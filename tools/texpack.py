"""texpack.py - bakes a map's bitmaps into a PSP texture pack (.ptex).

The first slice of the asset baker (build 20): every 2D bitmap with power-of-
two sides becomes a swizzled CLUT8 texture with a mip chain, at the
resolution policy of research/baker/bitmap_bake.py (top mip dropped for
textures of 64 px and more, 512 px at most; lightmaps at full size). The
game finds a texture by the address of its bitmap_data in the tag window,
which is fixed for a map built for the PSP (invader-build -X 0x09600000).

usage: python3 texpack.py map.map out.ptex [--limit N] [--tints-from-means]

File layout (little endian):
  header: 'PTX1', u32 count, u32 directory size
  directory, sorted by key: count x { u32 key (bitmap_data address),
    u16 width, u16 height, u8 levels, u8 psm (5 = T8), u8 clut (0 = 5650,
    3 = 8888), u8 flags (1 = has alpha), u32 offset, u32 size }
  tints, right after the directory: 'TNT1', u32 count, then count x
    { u32 shader tag address, u32 tint (ABGR) }, sorted by address: the
    see-through shaders' tints for the one map the GE draws of each
    (seethrough.py). Bit 0 of the address set: the tint is half the colour,
    drawn with the GE's colour doubling; bit 1 alone: the tint is added to
    the map, not multiplied; both bits: the tint alone is drawn, the map
    giving only its alpha (seethrough.py's models). The textures' data is
    read by offset, so a build that doesn't know a table never looks at it
  drawn maps, right after the tints: 'TND1', u32 count, then count x
    { u32 shader tag address (bits 0 and 1 as above, for the third word),
    u32 the map the GE draws (its index in the shader's maps), u32 the tint
    for the map the runtime's own rule would draw (keep word drawnmapscale) },
    sorted by address: only the shaders whose drawn map isn't the rule's.
    Only when there are any, after a 'TNT1' (of count 0 when no shader is
    tinted)
  data: per texture, 16-byte aligned: the palette (256 x 2 or 4 bytes), then
    each level swizzled, rows padded to 16 bytes and to 8 rows
"""
import sys, os, struct, argparse
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'bake', 'lib'))  # the bake's readers (halomap.py, ...)
from halomap import Defs, CacheMap
from assets import fo
import refs
from bitmap_bake import decode, downscale, quantize, swizzle_psp, target, psnr, decode_dxt, decode_plain, BPP
from assets import FORMATS, TYPES, BTYPES, USAGES
import seethrough


def bitmap_list(m, d):
    bf = fo(d, 'Bitmap')
    out = []
    for t in m.tags:
        if t.group != 'bitm' or not t.address:
            continue
        o = m.off(t.address)
        btype, bfmt, usage, bflags = struct.unpack_from('<hhhH', m.data, o)
        n, p = struct.unpack_from('<iI', m.data, o + bf['bitmap data'])
        for i in range(n):
            address = p + 48 * i
            bo = m.off(address)
            (cls, w, h, depth, typ, fmt, flags, rx, ry, mips, _, poff, psize) = \
                struct.unpack_from('<IhhhhhHhhhhIi', m.data, bo)
            out.append(dict(tag=t.name, index=i, address=address,
                            group_type=BTYPES[btype] if btype < 5 else btype,
                            usage=USAGES[usage] if usage < 6 else usage,
                            w=w, h=h, depth=depth, type=TYPES[typ] if typ < 4 else typ,
                            format=FORMATS[fmt] if fmt < len(FORMATS) else '?', fmt=fmt,
                            flags=flags, mips=mips, pixels_offset=poff, pixels_size=psize))
    return out


def pow2(v):
    return v > 0 and (v & (v - 1)) == 0


def level_bytes(w, h, bits=8):
    """a swizzled level: rows of at least 16 bytes, padded to 16; 8-row blocks"""
    row = max(16, (w * bits + 7) // 8)
    row = (row + 15) // 16 * 16
    return row, (h + 7) // 8 * 8


def nearest(img, pal, alpha):
    ch = 4 if alpha else 3
    pts = img.reshape(-1, 4)[:, :ch].astype(np.int32)
    p = pal[:, :ch].astype(np.int32)
    best = np.zeros(len(pts), np.int64)
    bestd = np.full(len(pts), 1 << 60, np.int64)
    for start in range(0, len(p), 64):
        d = ((pts[:, None, :] - p[None, start:start + 64, :]) ** 2).sum(2)
        j = d.argmin(1)
        dj = d[np.arange(len(pts)), j]
        better = dj < bestd
        best[better] = j[better] + start
        bestd[better] = dj[better]
    return best.reshape(img.shape[:2])


def detail_colour(m, d, B_by_tag, tag_id):
    """a detail map's average colour, 0-1 (a tiled detail map averages out at
    the distances the PSP's 272 lines resolve it)"""
    t = m.by_id.get(tag_id)
    if not t or t.group != 'bitm':
        return None
    for b in B_by_tag.get(t.name, ())[:1]:
        img = decode(m, b)
        if img is not None:
            return img[:, :, :3].reshape(-1, 3).mean(0) / 255.0
    return None


def level_looks(m, d, B):
    """senv shaders: base bitmap address -> how to bake its look. Halo's level
    shaders blend two tiled detail maps by the base map's alpha and combine
    them with the base map (double/biased multiply, multiply or double/biased
    add); the GE draws one texture at a time, so the base map is baked with
    the detail maps' average colours folded in (build 20)"""
    by_tag = {}
    for b in B:
        by_tag.setdefault(b['tag'], []).append(b)
    looks = {}
    for t in m.tags:
        if t.group != 'senv' or not t.address:
            continue
        o = m.off(t.address)
        flags, = struct.unpack_from('<H', m.data, o + 40)
        function, = struct.unpack_from('<h', m.data, o + 176)
        base_id, = struct.unpack_from('<i', m.data, o + 148)
        primary_id, = struct.unpack_from('<i', m.data, o + 196)
        secondary_id, = struct.unpack_from('<i', m.data, o + 216)
        micro_function, = struct.unpack_from('<h', m.data, o + 244)
        micro_id, = struct.unpack_from('<i', m.data, o + 264)
        base = m.by_id.get(base_id)
        if not base or base.name not in by_tag:
            continue
        primary = detail_colour(m, d, by_tag, primary_id)
        secondary = detail_colour(m, d, by_tag, secondary_id)
        micro = detail_colour(m, d, by_tag, micro_id)
        key = by_tag[base.name][0]['address']
        looks.setdefault(key, dict(function=function, primary=primary, secondary=secondary,
                                   micro_function=micro_function, micro=micro,
                                   alpha_tested=bool(flags & 1), shader=t.name))
    return looks


def model_looks(m, d, B):
    """soso shaders with a detail map (build 36): base bitmap address -> how
    to fold it in. The Xbox's model shader blends the detail map toward
    neutral by a mask from the multipurpose map (red: reflection; green:
    self-illumination; blue: change colour; alpha) and combines it with the
    base (double/biased multiply, multiply, double/biased add); the chief's
    armour and the vehicles' hulls take a dark corroded-metal detail map
    there (average 0.25, so x 0.5 under the mask), which the PSP never drew:
    they came out twice as bright. The GE takes the folded base under the
    key address | 1 (psp_fixups.py), the plain one stays for other shaders"""
    by_tag = {}
    for b in B:
        by_tag.setdefault(b['tag'], []).append(b)
    looks = {}
    for t in m.tags:
        if t.group != 'soso' or not t.address:
            continue
        o = m.off(t.address)
        base_id, = struct.unpack_from('<i', m.data, o + 176)
        multipurpose_id, = struct.unpack_from('<i', m.data, o + 200)
        function, mask = struct.unpack_from('<hh', m.data, o + 212)
        detail_id, = struct.unpack_from('<i', m.data, o + 232)
        base = m.by_id.get(base_id)
        if not base or base.name not in by_tag:
            continue
        detail = detail_colour(m, d, by_tag, detail_id)
        if detail is None:
            continue
        mp = m.by_id.get(multipurpose_id)
        multipurpose = None
        if mp and mp.name in by_tag:
            multipurpose = decode(m, by_tag[mp.name][0])
        key = by_tag[base.name][0]['address']
        looks.setdefault(key, dict(function=function, mask=mask, detail=detail, multipurpose=multipurpose,
                                   shader=t.name))
    return looks


def apply_model_look(img, look):
    rgb = img[:, :, :3].astype(np.float32) / 255.0
    h, w = rgb.shape[:2]
    mask = np.ones((h, w, 1), np.float32)
    k = look['mask']
    mp = look['multipurpose']
    if k and mp is not None:
        if mp.shape[:2] != (h, w):
            mp = np.array(__import__('PIL.Image', fromlist=['Image']).fromarray(mp).resize((w, h)))
        channel = [None, 0, 0, 1, 1, 2, 2, 3, 3][k] if k < 9 else None
        if channel is not None:
            mask = mp[:, :, channel:channel + 1].astype(np.float32) / 255.0
            if k % 2 == 1:
                mask = 1.0 - mask
    detail = look['detail'][None, None, :]
    if look['function'] == 1:
        out = rgb * ((1.0 - mask) + mask * detail)
    elif look['function'] == 2:
        out = rgb + mask * (2.0 * detail - 1.0)
    else:
        out = rgb * ((1.0 - mask) + mask * 2.0 * detail)
    out = np.clip(out * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return np.concatenate([out, img[:, :, 3:4]], 2)


def combine(rgb, detail, function):
    if function == 1:                   # multiply
        return rgb * detail
    if function == 2:                   # double/biased add
        return rgb + 2.0 * detail - 1.0
    return rgb * 2.0 * detail           # double/biased multiply


def neutral_detail(img):
    """a detail map the GE draws over the base map (build 31): scaled so its
    average is 0.5 in each channel, so that doubled (the GE's 2 x src x dst
    blend) it multiplies by detail / average. The base map already holds the
    average (apply_look), so the two together give the Xbox's base x 2 x
    detail, and where the map mips or fades out to its average it changes
    nothing"""
    rgb = img[:, :, :3].astype(np.float32)
    mean = np.maximum(rgb.reshape(-1, 3).mean(0), 1.0)
    out = np.clip(rgb * (127.5 / mean)[None, None, :] + 0.5, 0, 255).astype(np.uint8)
    return np.concatenate([out, np.full_like(img[:, :, 3:4], 255)], 2)


def apply_look(img, look):
    rgb = img[:, :, :3].astype(np.float32) / 255.0
    a = img[:, :, 3:4].astype(np.float32) / 255.0
    p, q = look['primary'], look['secondary']
    if p is None and q is None and look.get('micro') is None:
        return img
    if p is None:
        p = q
    if q is None:
        q = p
    out = rgb
    if p is not None:
        out = combine(out, q[None, None, :] * (1.0 - a) + p[None, None, :] * a, look['function'])
    # the micro detail map combines with the result the same way (build 31)
    if look.get('micro') is not None:
        out = combine(out, look['micro'][None, None, :], look['micro_function'])
    out = np.clip(out * 255.0 + 0.5, 0, 255).astype(np.uint8)
    alpha = img[:, :, 3:4] if look['alpha_tested'] else np.full_like(img[:, :, 3:4], 255)
    return np.concatenate([out, alpha], 2)


MAXIMUM_SIDE = 256     # a 480 x 272 screen; lightmaps keep their size
OBJECT_SIDE = 128      # objects rarely cover more of the screen; first-person ones do


# The HUD's meters (build 23): a grey bitmap whose grey is the position along
# the meter and whose alpha is its shape, with more grey-alpha pairs than a
# palette holds. The game works a meter out per palette entry (psp_ge.c), so
# the palette is a fixed grid instead: 64 greys x 4 alphas, every grey within
# 2 of the original (64 steps along a meter; the assault rifle's 60 rounds
# each keep their own).
GRID_PALETTE = np.array([[min(255, (i >> 2) * 4 + 2)] * 3 + [(i & 3) * 85] for i in range(256)], np.uint8)


# build 51: the grid's greys sit on the bitmap's own step. The assault rifle's
# 60 rounds are greys 4, 8 ... 240, and its meter (alpha multiplier 4, bias 2)
# fills a round while its grey is below 4 x rounds + 2. The grid's greys were
# 4k + 2, so round 60's 240 became 242, equal to the full magazine's 242: the
# last round never lit (Sheepdog01, every build). The offset is now the bitmap's
# commonest grey modulo 4 (0 here), so greys on the 4-step stay exact
def grid_offset(img):
    a = img[:, :, 3] > 0
    if not a.any():
        return 2
    return int(np.bincount((img[:, :, 2][a].astype(np.int32) & 3), minlength=4).argmax())


def grid_palette(offset):
    return np.array([[min(255, (i >> 2) * 4 + offset)] * 3 + [(i & 3) * 85] for i in range(256)], np.uint8)


def grey_meter(img):
    rgba = img.reshape(-1, 4).astype(np.int32)
    if rgba[:, 3].min() == 255 or (rgba[:, 0] != rgba[:, 1]).any() or (rgba[:, 1] != rgba[:, 2]).any():
        return False
    return len(np.unique(rgba[:, [2, 3]], axis=0)) > 256


def grid_index(img, offset=2):
    y = np.clip((img[:, :, 2].astype(np.int32) - offset + 2) >> 2, 0, 63)
    a = (img[:, :, 3].astype(np.int32) + 42) // 85
    return y * 4 + a


def decode_level(m, b, k):
    """the Xbox bitmap's own mip level k as RGBA (build 59), or None"""
    fmt, w, h = b['fmt'], b['w'], b['h']
    if b['type'] != '2d' or fmt in (17,) or BPP[fmt] == 0 or k > b['mips']:
        return None
    dxt = FORMATS[fmt].startswith('DXT')
    off = 0
    for _ in range(k):
        if dxt:
            off += max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if FORMATS[fmt] == 'DXT1' else 16)
        else:
            off += w * h * BPP[fmt] // 8
        w, h = max(1, w // 2), max(1, h // 2)
    buf = m.data[b['pixels_offset'] + off:b['pixels_offset'] + b['pixels_size']]
    if dxt:
        return decode_dxt(buf, w, h, FORMATS[fmt])
    return decode_plain(buf, w, h, fmt, bool(b['flags'] & 8))


def detail_levels(m, b, tw, th):
    """a detail map's levels from the Xbox's own mip chain (build 59). Halo's
    detail maps fade toward their average colour level by level (the bitmap's
    detail fade, applied when the tag was built): measured on Blood Gulch,
    "detail rock sandstone" keeps half the contrast of a plain downscale at
    its sixth level, "detail grass" a third. Downscaled from the top level as
    before, the small levels kept the full pattern, and at a middle distance
    the canyon walls showed it as bands (Sheepdog01, b56-b59). All levels are scaled
    by the top level's average, so a faded level fades to the neutral grey"""
    dropped = 0
    while (b['w'] >> dropped) > tw and dropped < 12:
        dropped += 1
    top = decode_level(m, b, dropped)
    if top is None:
        return None
    scale = 127.5 / np.maximum(top[:, :, :3].astype(np.float32).reshape(-1, 3).mean(0), 1.0)
    out, w, h, k = [], tw, th, dropped
    while k <= b['mips'] and len(out) < 8:
        img = decode_level(m, b, k)
        if img is None:
            break
        if img.shape[1] != w or img.shape[0] != h:
            img = downscale(img, w, h)
        rgb = np.clip(img[:, :, :3].astype(np.float32) * scale[None, None, :] + 0.5, 0, 255).astype(np.uint8)
        out.append(np.concatenate([rgb, np.full_like(img[:, :, 3:4], 255)], 2))
        if w <= 1 or h <= 1:
            break
        w, h, k = max(1, w // 2), max(1, h // 2), k + 1
    return out


# build 102 (the asset track): lightmaps shrunk by this factor (texpack.py
# --lightmap-scale 2), their charts' colours first grown into the gutters
# (lightmap_masks.py) so the shrinking mixes nothing dark into the seams
LIGHTMAP_SCALE = 1
LIGHTMAP_MASKS = {}
# build 102 (the asset track): alpha-tested textures' smaller levels keep the
# top level's coverage (texpack.py --alpha-coverage). A box filter averages a
# leaf's edge with the air around it, so at each smaller level fewer texels
# pass the GE's alpha test (alpha > 127) and a far plant thins out and
# shimmers as the level changes (Sheepdog01: the flickering plants, b62 shot 11).
# Each level's alpha is scaled so the same share of it passes as at the top
# (Castano's coverage-preserving mipmaps)
ALPHA_COVERAGE = False
ALPHA_TESTED = set()

# HUD sheets stay at 8 bits, so the HUD stops looking soft. The
# 16-colour test below compares a bitmap's error with the 16-bit frame
# buffer's own, which a glyph's anti-aliased edge or a meter's soft ramp can
# pass while its steps show (the same test banded effect gradients,
# hardware_b114.md). HUD sheets are few and small, and drawn near their own
# size, so every step is seen. --hud-clut4 puts them back under the test.
# HUD_KEPT_8: the sheets the old rule would have put at 4 bits (the log's
# 'HUD sheets' line, so a bake says whether this changed anything)
HUD_CLUT4 = False
HUD_KEPT_8 = []
# build 102: per-class detail (texpack.py --full-detail fp,vehicles,...):
# these classes keep their top level (the bake otherwise halves every texture
# of 64 px and more) and the 256 px cap rises to 512. A lever to try, not the
# default: each doubles its textures' bytes four times over
DETAIL_CLASSES = {
    'fp': lambda tag: '\\fp\\' in tag,
    'weapons': lambda tag: tag.startswith('weapons\\') and '\\fp\\' not in tag,
    'vehicles': lambda tag: tag.startswith('vehicles\\'),
    'characters': lambda tag: tag.startswith('characters\\') and '\\fp\\' not in tag,
}
FULL_DETAIL = set()


def full_detail(tag):
    return any(DETAIL_CLASSES[c](tag) for c in FULL_DETAIL)


def coverage(alpha):
    return float((alpha > 127).mean()) if alpha.size else 0.0


def keep_coverage(img, target):
    """img's alpha scaled so coverage(alpha) is as close to target as it gets"""
    a = img[:, :, 3].astype(np.float32)
    if a.size == 0 or target <= 0.0 or target >= 1.0:
        return img
    low, high = 0.25, 8.0
    for _ in range(24):
        mid = (low + high) / 2
        if coverage(np.clip(a * mid, 0, 255)) < target:
            low = mid
        else:
            high = mid
    out = img.copy()
    out[:, :, 3] = np.clip(a * high + 0.5, 0, 255).astype(np.uint8)
    return out


def alpha_tested_bitmaps(m, d):
    """bitmap tag names used as an alpha-tested shader's base map: model
    shaders (soso) unless "not alpha tested", environment shaders (senv) with
    "alpha tested" """
    names = set()
    for t in m.tags:
        if t.group not in ('soso', 'senv') or not t.address or not m.in_tags(t.address):
            continue
        o = m.off(t.address)
        flags, = struct.unpack_from('<H', m.data, o + 40)
        base_offset = 164 if t.group == 'soso' else 136
        tid, = struct.unpack_from('<i', m.data, o + base_offset + 12)
        tested = not (flags & 4) if t.group == 'soso' else bool(flags & 1)
        if tested and m.by_id.get(tid):
            names.add(m.by_id[tid].name)
    return names


def bake_one(m, b, cls, look=None, model_look=None, full_hud=True, meter=False):
    act, tw, th = target(b, cls)
    if cls == 'lightmap' and LIGHTMAP_SCALE > 1:
        tw, th = max(8, tw // LIGHTMAP_SCALE), max(8, th // LIGHTMAP_SCALE)
    # HUD sheets (meters, numbers, reticles) at their full size: halved, the
    # digits and meter cells fall below a texel each (build 39). The big
    # menu screens keep the halving, and so do the split-screen scope masks
    # (one player on a PSP: never drawn)
    if cls == 'ui/hud' and full_hud and act == 'clut' and max(b['w'], b['h']) <= 256:
        tw, th = b['w'], b['h']
    limit = MAXIMUM_SIDE
    if cls == 'objects (soso)' and '\\fp\\' not in b['tag']:
        limit = OBJECT_SIDE
    if FULL_DETAIL and act == 'clut' and full_detail(b['tag']):
        tw, th, limit = b['w'], b['h'], 512
    while cls != 'lightmap' and max(tw, th) > limit:
        tw, th = max(1, tw // 2), max(1, th // 2)
    if act != 'clut' or b['type'] != '2d' or not pow2(tw) or not pow2(th):
        return None
    img = decode(m, b)
    if img is None:
        return None
    if cls == 'lightmap' and LIGHTMAP_SCALE > 1 and (b['tag'], b['index']) in LIGHTMAP_MASKS:
        from lightmap_masks import dilate
        img = dilate(img, LIGHTMAP_MASKS[(b['tag'], b['index'])], 2 * LIGHTMAP_SCALE + 2)
    if look:
        img = apply_look(img, look)
    if model_look:
        img = apply_model_look(img, model_look)
    own_levels = None
    if cls == 'detail':
        own_levels = None if os.environ.get('TEXPACK_DETAIL_DOWNSCALE') else detail_levels(m, b, tw, th)
        if not own_levels:
            img = neutral_detail(img)
    small = own_levels[0] if own_levels else downscale(img, tw, th)
    # build 116: a meter shader's map (the plasma weapons' gauges, the
    # Warthog's meters) takes the grid too, always: the GE works its colours
    # out per palette entry from the blue (the position along the meter) and
    # the alpha (the shape), as the HUD's (psp_ge.c, meter_palette), and an
    # ordinary palette's entries are averages of both (b113_candidates.md #16)
    grid = (cls == 'ui/hud' and grey_meter(small)) or meter
    if grid:
        grid_off = grid_offset(small)
        idx, pal, alpha = grid_index(small, grid_off), grid_palette(grid_off), True
    elif own_levels:
        # one palette for every level: the faded levels' near-greys need
        # entries of their own, which the top level alone wouldn't earn
        every = np.concatenate([l.reshape(-1, 1, 4) for l in own_levels], 0)
        _, pal, _, alpha = quantize(every, 256)
        idx = nearest(small, pal, alpha)
    else:
        idx, pal, rec, alpha = quantize(small, 256)
    # 16 colours where that is no worse than the 16-bit frame buffer itself
    # (within 1 dB of its 5650 rounding): half the bytes (research/baker)
    bits = 8
    if not grid and tw >= 32 and th >= 8 and cls not in ('level (senv)', 'lightmap', 'sky', 'detail'):
        idx4, pal4, rec4, _ = quantize(small, 16)
        reference = small.copy()
        reference[:, :, 0] &= 0xF8
        reference[:, :, 1] &= 0xFC
        reference[:, :, 2] &= 0xF8
        if psnr(small, rec4) >= psnr(small[:, :, :3], reference[:, :, :3]) - 1.0:
            if cls == 'ui/hud' and not HUD_CLUT4:
                HUD_KEPT_8.append(b['tag'])
            else:
                idx, pal, rec, bits = idx4, pal4[:16], rec4, 4
    if alpha:
        palette = pal.astype(np.uint8).tobytes()                       # 8888, r g b a
        clut = 3
    else:
        r, g, bl = pal[:, 0].astype(np.uint16), pal[:, 1].astype(np.uint16), pal[:, 2].astype(np.uint16)
        v = (r >> 3) | ((g >> 2) << 5) | ((bl >> 3) << 11)              # GU_PSM_5650: r low bits
        palette = v.astype('<u2').tobytes()
        clut = 0
    levels = []
    w, h, level_img = tw, th, small
    chain_img = small
    fix_coverage = ALPHA_COVERAGE and alpha and b['tag'] in ALPHA_TESTED and not own_levels and not grid
    top_coverage = coverage(small[:, :, 3]) if fix_coverage else 0.0
    # build 49: no more levels than the Xbox bitmap has (its extra mip count
    # + 1, less the levels dropped above the baked size). Blood Gulch's 15
    # lightmaps have none on the Xbox; baked with a chain, their small levels
    # mixed each lightmap page's charts and dark gutters together, and
    # surfaces seen edge-on or far off took that dark mix (Sheepdog01's dark lines,
    # gone with "nomip", b47)
    dropped = 0
    while (b['w'] >> dropped) > tw and dropped < 12:
        dropped += 1
    source_levels = max(1, b['mips'] + 1 - dropped)
    if own_levels:
        source_levels = len(own_levels)
    while len(levels) < min(8, source_levels):
        ix = idx if not levels else (grid_index(level_img, grid_off) if grid else nearest(level_img, pal, alpha))
        row, rows = level_bytes(w, h, bits)
        if bits == 4:
            pixels = np.zeros((rows, row * 2), np.uint8)
            pixels[:h, :w] = ix
            packed = (pixels[:, 0::2] & 15) | ((pixels[:, 1::2] & 15) << 4)      # low nibble first
            levels.append(swizzle_psp(packed.astype(np.uint8).tobytes(), row, rows))
        else:
            padded = np.zeros((rows, row), np.uint8)
            padded[:h, :w] = ix
            levels.append(swizzle_psp(padded.tobytes(), row, rows))
        # a detail map tiles up to a hundred times across a surface, so it
        # mips down to a single texel: its average, the neutral grey (build 31)
        if cls == 'detail':
            if w <= 1 or h <= 1:
                break
        elif w <= 8 or h <= 8 or (bits == 4 and w < 32):
            break
        # the HUD is drawn flat on the screen at near its own size: no mip
        # levels (a quarter of its bytes, build 39)
        if cls == 'ui/hud':
            break
        w, h = max(1, w // 2), max(1, h // 2)
        if own_levels and len(levels) < len(own_levels):
            level_img = own_levels[len(levels)]
        else:
            chain_img = downscale(chain_img, w, h)
            level_img = keep_coverage(chain_img, top_coverage) if fix_coverage else chain_img
    data = palette + b''.join(levels)
    return dict(w=tw, h=th, levels=len(levels), clut=clut, alpha=alpha, data=data, psm=4 if bits == 4 else 5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('map')
    ap.add_argument('out')
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--lightmap-scale', type=int, default=1, help='asset track: lightmaps shrunk by this (2: half)')
    ap.add_argument('--pgeo', default=None, help='the map\'s geometry pack (default: beside the map) for the lightmap masks')
    ap.add_argument('--alpha-coverage', action='store_true', help='asset track: alpha-tested textures keep their coverage at every level')
    ap.add_argument('--full-detail', default='', help='classes kept at full size: fp, weapons, vehicles, characters')
    ap.add_argument('--hud-clut4', action='store_true', help='HUD sheets may take 16 colours again (as before build 120)')
    ap.add_argument('--tints-from-means', action='store_true',
                    help='see-through tints from every map\'s mean, drawn on the runtime\'s own map (as before build 121)')
    a = ap.parse_args()
    global FULL_DETAIL, HUD_CLUT4
    HUD_CLUT4 = a.hud_clut4
    FULL_DETAIL = set(c for c in a.full_detail.split(',') if c)
    if FULL_DETAIL - set(DETAIL_CLASSES):
        sys.exit('texpack: unknown --full-detail class(es): %s' % ', '.join(sorted(FULL_DETAIL - set(DETAIL_CLASSES))))
    d = Defs()
    m = CacheMap(a.map)
    global ALPHA_COVERAGE, ALPHA_TESTED
    if a.alpha_coverage:
        ALPHA_COVERAGE = True
        ALPHA_TESTED = alpha_tested_bitmaps(m, d)
        print('alpha coverage kept for %d alpha-tested bitmaps' % len(ALPHA_TESTED), file=sys.stderr)
    roles = refs.bitmap_roles(m, d)
    # build 116: the bitmaps meter shaders draw (bake_one gives them the grid)
    meter_maps = set(tag for tag, tag_roles in roles.items() if any(r.startswith('smet:') for r in tag_roles))
    print('%d meter shader maps: the grid palette' % len(meter_maps), file=sys.stderr)
    B = bitmap_list(m, d)
    if a.limit:
        B = B[:a.limit]
    global LIGHTMAP_SCALE, LIGHTMAP_MASKS
    LIGHTMAP_SCALE = max(1, a.lightmap_scale)
    if LIGHTMAP_SCALE > 1:
        from lightmap_masks import page_masks
        sizes = {(b['tag'], b['index']): (b['w'], b['h']) for b in B
                 if refs.classify(roles.get(b['tag'], ()), b) == 'lightmap'}
        pgeo = a.pgeo or os.path.splitext(a.map)[0] + '.pgeo'
        LIGHTMAP_MASKS = page_masks(a.map, pgeo, sizes)
        covered = sum(int(v.sum()) for v in LIGHTMAP_MASKS.values())
        total = sum(v.size for v in LIGHTMAP_MASKS.values())
        print('lightmaps 1/%d: %d of %d pages masked, %.0f%% of their texels in charts' % (
            LIGHTMAP_SCALE, len(LIGHTMAP_MASKS), len(sizes), 100.0 * covered / max(1, total)), file=sys.stderr)
    entries = []
    skipped = {}
    looks = level_looks(m, d, B)
    mlooks = model_looks(m, d, B)
    print('%d level shaders: base maps baked with their detail maps' % len(looks), file=sys.stderr)
    for i, b in enumerate(B):
        cls = refs.classify(roles.get(b['tag'], ()), b)
        if cls == 'detail' and any('detail map' not in role for role in roles.get(b['tag'], ())):
            cls = 'detail (also drawn plain)'
        tag_roles = roles.get(b['tag'], ())
        full_hud = not tag_roles or any('splitscreen' not in role for role in tag_roles)
        meter = b['tag'] in meter_maps
        r = bake_one(m, b, cls, looks.get(b['address']), full_hud=full_hud, meter=meter)
        if r is None:
            skipped[cls] = skipped.get(cls, 0) + 1
            continue
        r['key'] = b['address']
        r['name'] = b['tag']
        entries.append(r)
        if b['address'] in mlooks:
            v = bake_one(m, b, cls, looks.get(b['address']), mlooks[b['address']], full_hud=full_hud, meter=meter)
            if v is not None:
                v['key'] = b['address'] | 1
                v['name'] = b['tag'] + ' (with its detail map)'
                entries.append(v)
                print('  %s: detail map folded in for %s' % (b['tag'], mlooks[b['address']]['shader']), file=sys.stderr)
        if i % 50 == 0:
            print('  %d/%d' % (i, len(B)), file=sys.stderr)
    if HUD_CLUT4:
        print('HUD sheets: 16 colours allowed (--hud-clut4)', file=sys.stderr)
    else:
        kept = sorted(set(HUD_KEPT_8))
        print('HUD sheets: %d kept at 8 bits that the old rule put at 4%s' % (
            len(kept), (': ' + ', '.join(kept)) if kept else ''), file=sys.stderr)
    entries.sort(key=lambda e: e['key'])
    # the see-through shaders' tints and drawn maps (seethrough.py)
    tints, drawn = seethrough.tints(m, bitmap_list(m, d), from_means=a.tints_from_means)
    names = {t.address: t.name for t in m.tags}
    for address in sorted(set(k & ~3 for k in tints) | set(drawn)):
        found = next((tints[k][2] for k in tints if k & ~3 == address), None) or drawn[address]
        alt = '' if found.alt is None else ' (the scale rule\'s map %d: tint %s)' % (found.scale_drawn, found.alt.name())
        print('  tint %s for %s: map %d drawn%s; the shader averages %s over its map\'s %s' % (
            found.name(), names[address], found.drawn, alt, ' '.join('%.2f' % v for v in found.result),
            ' '.join('%.2f' % v for v in found.mean)), file=sys.stderr)
    print('%d see-through shaders tinted (%d doubled, %d added, %d flat); %d drawn from another map than the scale rule\'s%s' % (
        len(tints), sum(1 for k in tints if k & 3 == seethrough.DOUBLED), sum(1 for k in tints if k & 3 == seethrough.ADD),
        sum(1 for k in tints if k & 3 == seethrough.FLAT), len(drawn),
        ' (tints from the maps\' means: --tints-from-means)' if a.tints_from_means else ''), file=sys.stderr)
    tint_bytes = b''
    if tints or drawn:
        tint_bytes = struct.pack('<4sI', b'TNT1', len(tints)) + b''.join(
            struct.pack('<II', key, tints[key][0]) for key in sorted(tints))
        if drawn:
            tint_bytes += struct.pack('<4sI', b'TND1', len(drawn)) + b''.join(
                struct.pack('<III', address | drawn[address].alt.flags(), drawn[address].drawn,
                            drawn[address].alt.word()) for address in sorted(drawn))
    header = 12
    directory = 20 * len(entries)
    offset = (header + directory + len(tint_bytes) + 15) // 16 * 16
    blob = bytearray()
    dir_bytes = bytearray()
    for e in entries:
        start = offset + len(blob)
        blob += e['data']
        while len(blob) % 16:
            blob += b'\0'
        dir_bytes += struct.pack('<IHHBBBBII', e['key'], e['w'], e['h'], e['levels'], e['psm'], e['clut'],
                                 1 if e['alpha'] else 0, start, len(e['data']))
    out = bytearray(struct.pack('<4sII', b'PTX1', len(entries), directory)) + dir_bytes + tint_bytes
    while len(out) < offset:
        out += b'\0'
    out += blob
    open(a.out, 'wb').write(out)
    print('%s: %d textures, %d bytes; skipped %s' % (a.out, len(entries), len(out), skipped))


if __name__ == '__main__':
    main()
