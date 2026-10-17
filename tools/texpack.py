"""texpack.py - bakes a map's bitmaps into a PSP texture pack (.ptex).

The first slice of the asset baker (build 20): every 2D bitmap with power-of-
two sides becomes a swizzled CLUT8 texture with a mip chain, at the
resolution policy of research/baker/bitmap_bake.py (top mip dropped for
textures of 64 px and more, 512 px at most; lightmaps at full size). The
game finds a texture by the address of its bitmap_data in the tag window,
which is fixed for a map built for the PSP (invader-build -X 0x09600000).

usage: python3 texpack.py map.map out.ptex [--limit N] [--tints-from-means] [--no-glow-maps]
       [--premultiplied-masks] [--showcase [--showcase-32 | --showcase-palettes]]
       [--quality 1|2] [--quality-report]

File layout (little endian):
  header: 'PTX1', u32 count, u32 directory size
  directory, sorted by key: count x { u32 key (bitmap_data address),
    u16 width, u16 height, u8 levels, u8 psm (5 = T8, 4 = T4; in its own colours,
    --showcase: 3 = 8888, 0 = 5650), u8 clut (0 = 5650, 3 = 8888; 0xFF none),
    u8 flags (1 = has alpha), u32 offset, u32 size }
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
  glow maps: directory entries keyed by a self-illuminated model shader's
    tag address | 2 (glow_looks), beside the bitmaps' keys; entries with the
    same texels share their data
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


def glow_looks(m, B):
    """soso shaders that light themselves: shader tag address -> the base
    bitmap and multipurpose map to bake a glow map from. On the Xbox a model
    shader's self-illumination (the multipurpose map's green times the
    shader's colour) is added to the lighting, and the lighting multiplies the
    base map (rasterizer_xbox_models.c, set_environment_shader_pixel_shader:
    stage 1 adds R0 x C0 to V0, the later stages multiply by T0), so a lit-up
    area shows its own base texture at full brightness. The GE adds the
    colour in a pass of its own (psp_ge.c, multipurpose_pass), so it takes a
    map of what that pass must add: the base x the green. Only shaders the
    runtime gives a colour (a period, and a colour at either end); the key is
    the shader's address | 2, which no bitmap's key can be"""
    by_tag = {}
    for b in B:
        by_tag.setdefault(b['tag'], []).append(b)
    glows = {}
    for t in m.tags:
        if t.group != 'soso' or not t.address:
            continue
        o = m.off(t.address)
        period, = struct.unpack_from('<f', m.data, o + 116)
        colours = struct.unpack_from('<6f', m.data, o + 120)
        base_id, = struct.unpack_from('<i', m.data, o + 176)
        multipurpose_id, = struct.unpack_from('<i', m.data, o + 200)
        base, mp = m.by_id.get(base_id), m.by_id.get(multipurpose_id)
        if period == 0.0 or not any(colours) or not base or not mp or base.name not in by_tag or mp.name not in by_tag:
            continue
        glows[t.address | 2] = dict(shader=t.name, base=by_tag[base.name][0], multipurpose=by_tag[mp.name][0])
    return glows


PREMULTIPLIED_MASKS = False    # --premultiplied-masks: the detail masks read as before
MASKS_MENDED = []              # (shader, share of texels) whose detail mask the fix changed


def channel_at(image, channel, w, h):
    """one channel of an image at w x h, 0-1. Resized on its own: PIL resizes
    RGBA premultiplied, so a multipurpose map's red, green or blue under its
    alpha (the detail mask, not a transparency) would come out darkened, and
    black where that alpha is 0"""
    one = np.ascontiguousarray(image[:, :, channel])
    if one.shape != (h, w):
        one = np.array(__import__('PIL.Image', fromlist=['Image']).fromarray(one).resize((w, h)))
    return one[:, :, None].astype(np.float32) / 255.0


def apply_glow(img, multipurpose):
    """what the self-illumination pass adds, at full colour: the base x the
    multipurpose map's green, opaque (the pass draws its colour only)"""
    h, w = img.shape[:2]
    green = channel_at(multipurpose, 1, w, h)
    rgb = np.clip(img[:, :, :3].astype(np.float32) * green + 0.5, 0, 255).astype(np.uint8)
    return np.concatenate([rgb, np.full((h, w, 1), 255, np.uint8)], 2)


def apply_model_look(img, look):
    rgb = img[:, :, :3].astype(np.float32) / 255.0
    h, w = rgb.shape[:2]
    mask = np.ones((h, w, 1), np.float32)
    k = look['mask']
    mp = look['multipurpose']
    if k and mp is not None:
        channel = [None, 0, 0, 1, 1, 2, 2, 3, 3][k] if k < 9 else None
        if channel is not None:
            mask = channel_at(mp, channel, w, h)
            if mp.shape[:2] != (h, w) and channel < 3:
                old = np.array(__import__('PIL.Image', fromlist=['Image']).fromarray(mp).resize((w, h)))
                old = old[:, :, channel:channel + 1].astype(np.float32) / 255.0
                changed = float((np.abs(old - mask) > 2.0 / 255.0).mean())
                if changed:
                    MASKS_MENDED.append((look['shader'], changed))
                if PREMULTIPLIED_MASKS:
                    mask = old
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


SHOWCASE = False    # --showcase: the main menu's pack at the most the GE can take
# the classes a showcase pack keeps in their own colours; the rest keep
# their palettes, which the runtime reads (meters, the change colour's mask,
# the lightmaps' samples, the detail pass's grey)
DIRECT_CLASSES = ('objects (soso)', 'sky', 'level (senv)')


SHOWCASE_32 = False  # --showcase-32: opaque maps in 32 bits too (for a 32-bit screen)
SHOWCASE_PALETTES = False  # --showcase-palettes: full size, but every map on its palette
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16.0 - 0.5


def to_5650(img):
    """16-bit texels (GU_PSM_5650, red in the low bits), with a 4 x 4 ordered
    dither: the GE's filter averages it back into the steps 5 and 6 bits
    lose, where rounding alone would band a slow gradient (the sky's)"""
    h, w = img.shape[:2]
    noise = np.tile(BAYER, ((h + 3) // 4, (w + 3) // 4))[:h, :w]
    def channel(c, bits):
        steps = (1 << bits) - 1
        return np.clip(np.floor(img[:, :, c].astype(np.float32) * steps / 255.0 + 0.5 + noise), 0, steps).astype(np.uint16)
    return channel(0, 5) | (channel(1, 6) << 5) | (channel(2, 5) << 11)


def direct_levels(small, tw, th, levels_wanted, wide):
    """a texture in its own colours: 32 bits a texel (GU_PSM_8888: bytes
    r g b a) when wide, else 16 (GU_PSM_5650); its levels swizzled as the
    T8 ones are, 16-byte rows"""
    levels, w, h, img = [], tw, th, small
    size = 4 if wide else 2
    while len(levels) < levels_wanted:
        row, rows = level_bytes(w * size, h)
        padded = np.zeros((rows, row), np.uint8)
        texels = img if wide else to_5650(img).astype('<u2').view(np.uint8)
        padded[:h, :w * size] = texels.reshape(h, w * size)
        levels.append(swizzle_psp(padded.tobytes(), row, rows))
        if w <= 8 or h <= 8:
            break
        w, h = max(1, w // 2), max(1, h // 2)
        img = downscale(img, w, h)
    return levels


# ---- how texels become palette indices (Sheepdog01, 2026-10-16: the textures'
# blockiness and "oily" bands, everywhere). Three steps every 8- and 4-bit
# texture goes through, with --quality 2. Measured closer to the source by
# 2-9 dB a class, but on the PSP-1000 the two bakes were indistinguishable
# (Sheepdog01, 2026-10-17), so the default stays 1: it bakes in half the time.
#  - the palette is fitted in Oklab, a space where equal distances look
#    equally different, so dark tones get entries of their own rather than
#    sharing a few (RGB distance spends them on bright differences);
#  - texels are mapped onto it with error diffusion (Floyd-Steinberg,
#    serpentine, at 3/4 strength): each texel's rounding is carried to its
#    neighbours, so a gradient between two entries comes out as fine grain
#    the GE's filter blends, not as a flat step with a hard contour. Against
#    the palette as drawn (a 16-bit palette's 5650 rounding included). Not
#    the alpha: cut-outs and masks keep their edges;
#  - sizes are made with a Lanczos filter, which keeps edges sharp where a
#    box blurs them and steps them; alpha and lightmaps keep the box (a
#    cut-out's coverage, a chart's dark gutter, would ring).
QUALITY = 1
QUALITY_REPORT = False
QUALITY_STATS = {}
DIFFUSE_STRENGTH = 0.75


def srgb_to_oklab(rgb):
    c = rgb / 255.0
    c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    l = 0.4122214708 * c[:, 0] + 0.5363325363 * c[:, 1] + 0.0514459929 * c[:, 2]
    m_ = 0.2119034982 * c[:, 0] + 0.6806995451 * c[:, 1] + 0.1073969566 * c[:, 2]
    s_ = 0.0883024619 * c[:, 0] + 0.2817188376 * c[:, 1] + 0.6299787005 * c[:, 2]
    l, m_, s_ = np.cbrt(l), np.cbrt(m_), np.cbrt(s_)
    return np.stack([0.2104542553 * l + 0.7936177850 * m_ - 0.0040720468 * s_,
                     1.9779984951 * l - 2.4285922050 * m_ + 0.4505937099 * s_,
                     0.0259040371 * l + 0.7827717662 * m_ - 0.8086757660 * s_], 1)


def oklab_to_srgb(lab):
    l = lab[:, 0] + 0.3963377774 * lab[:, 1] + 0.2158037573 * lab[:, 2]
    m_ = lab[:, 0] - 0.1055613458 * lab[:, 1] - 0.0638541728 * lab[:, 2]
    s_ = lab[:, 0] - 0.0894841775 * lab[:, 1] - 1.2914855480 * lab[:, 2]
    l, m_, s_ = l ** 3, m_ ** 3, s_ ** 3
    c = np.stack([4.0767416621 * l - 3.3077115913 * m_ + 0.2309699292 * s_,
                  -1.2684380046 * l + 2.6097574011 * m_ - 0.3413193965 * s_,
                  -0.0041960863 * l - 0.7034186147 * m_ + 1.7076147010 * s_], 1)
    c = np.clip(c, 0, 1)
    c = np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)
    return c * 255.0


def fit_palette(img, colors):
    """(palette Nx4 uint8, has_alpha): k-means in Oklab (and alpha), as
    quantize does in RGB; a texture with no more colours than entries keeps
    its own"""
    from sklearn.cluster import KMeans
    has_alpha = bool(img[:, :, 3].min() < 255)
    pts = img.reshape(-1, 4).astype(np.float32)
    if not has_alpha:
        pts = pts[:, :3]
    uniq = np.unique(pts, axis=0)
    if len(uniq) <= colors:
        pal = uniq
    else:
        sample = pts if len(pts) <= 100000 else pts[np.random.RandomState(0).choice(len(pts), 100000, replace=False)]
        feats = srgb_to_oklab(sample[:, :3])
        if has_alpha:
            feats = np.concatenate([feats, sample[:, 3:4] / 255.0], 1)
        km = KMeans(n_clusters=colors, n_init=1, max_iter=60, random_state=0).fit(feats)
        centres = km.cluster_centers_
        rgb = oklab_to_srgb(centres[:, :3])
        pal = np.concatenate([rgb, centres[:, 3:4] * 255.0], 1) if has_alpha else rgb
    pal = np.clip(np.rint(pal), 0, 255).astype(np.uint8)
    if not has_alpha:
        pal = np.concatenate([pal, np.full((len(pal), 1), 255, np.uint8)], 1)
    if len(pal) < colors:
        pal = np.concatenate([pal, np.zeros((colors - len(pal), 4), np.uint8)])
    return pal, has_alpha


def as_drawn(pal, alpha):
    """the palette's colours as the GE draws them: a 16-bit palette's 5650
    rounding, widened back to 8 bits"""
    shown = pal.astype(np.float32).copy()
    if not alpha:
        for c, bits in ((0, 5), (1, 6), (2, 5)):
            v = pal[:, c].astype(np.int32) >> (8 - bits)
            shown[:, c] = (v * 255 + ((1 << bits) - 1) // 2) // ((1 << bits) - 1)
    return shown


def diffuse(img, pal, alpha, count):
    """indices into the palette's first count entries, by error diffusion of
    the colour (the alpha matched, not diffused)"""
    h, w = img.shape[:2]
    ch = 4 if alpha else 3
    P = as_drawn(pal[:count], alpha)[:, :ch]
    work = img[:, :, :ch].astype(np.float32).copy()
    out = np.zeros((h, w), np.int32)
    k = DIFFUSE_STRENGTH / 16.0
    for y in range(h):
        forward = y % 2 == 0
        d = 1 if forward else -1
        row = work[y]
        below = work[y + 1] if y + 1 < h else None
        for x in (range(w) if forward else range(w - 1, -1, -1)):
            v = row[x]
            i = int(((P - v) ** 2).sum(1).argmin())
            out[y, x] = i
            e = v - P[i]
            if ch == 4:
                e[3] = 0.0
            e *= k
            if 0 <= x + d < w:
                row[x + d] += 7 * e
            if below is not None:
                if 0 <= x - d < w:
                    below[x - d] += 3 * e
                below[x] += 5 * e
                if 0 <= x + d < w:
                    below[x + d] += e
    return out


def resample(img, tw, th, cls):
    """a smaller size: Lanczos on the colour, the box on the alpha and on
    lightmaps (see above); downscale as before with --quality 1"""
    if QUALITY < 2 or cls == 'lightmap' or (img.shape[1] == tw and img.shape[0] == th):
        return downscale(img, tw, th)
    from PIL import Image
    rgb = np.array(Image.fromarray(np.ascontiguousarray(img[:, :, :3]), 'RGB').resize((tw, th), Image.Resampling.LANCZOS))
    a = np.array(Image.fromarray(np.ascontiguousarray(img[:, :, 3]), 'L').resize((tw, th), Image.Resampling.BOX))
    return np.concatenate([rgb, a[:, :, None]], 2)


def blurred_psnr(a, b):
    """PSNR of the colour after a 3 x 3 box blur of both, as the screen's
    filtering would see them (dither grain counts as the average it makes)"""
    def blur(x):
        x = x[:, :, :3].astype(np.float32)
        p = np.pad(x, ((1, 1), (1, 1), (0, 0)), mode='edge')
        return sum(p[dy:dy + x.shape[0], dx:dx + x.shape[1]] for dy in range(3) for dx in range(3)) / 9.0
    return psnr(blur(a), blur(b))


def quality_note(cls, old, new):
    st = QUALITY_STATS.setdefault(cls, [0, 0.0, 0.0])
    st[0] += 1
    st[1] += old
    st[2] += new


def bake_one(m, b, cls, look=None, model_look=None, full_hud=True, meter=False, glow=None, own_colours=True):
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
    # the showcase: every map at its own size, up to the GE's 512 a side
    if SHOWCASE and act == 'clut' and cls != 'lightmap':
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
    if glow is not None:
        img = apply_glow(img, glow)
    own_levels = None
    if cls == 'detail':
        own_levels = None if os.environ.get('TEXPACK_DETAIL_DOWNSCALE') else detail_levels(m, b, tw, th)
        if not own_levels:
            img = neutral_detail(img)
    small = own_levels[0] if own_levels else resample(img, tw, th, cls)
    # build 116: a meter shader's map (the plasma weapons' gauges, the
    # Warthog's meters) takes the grid too, always: the GE works its colours
    # out per palette entry from the blue (the position along the meter) and
    # the alpha (the shape), as the HUD's (psp_ge.c, meter_palette), and an
    # ordinary palette's entries are averages of both (b113_candidates.md #16)
    grid = (cls == 'ui/hud' and grey_meter(small)) or meter
    if SHOWCASE and not SHOWCASE_PALETTES and own_colours and cls in DIRECT_CLASSES and not grid and not own_levels:
        dropped = 0
        while (b['w'] >> dropped) > tw and dropped < 12:
            dropped += 1
        alpha = bool(small[:, :, 3].min() < 255)
        wide = alpha or SHOWCASE_32
        levels = direct_levels(small, tw, th, min(8, max(1, b['mips'] + 1 - dropped)), wide)
        # clut 0xFF: no palette, so the runtime's palette reads pass it by
        return dict(w=tw, h=th, levels=len(levels), clut=0xFF, alpha=alpha, data=b''.join(levels),
                    psm=3 if wide else 0)
    if grid:
        grid_off = grid_offset(small)
        idx, pal, alpha = grid_index(small, grid_off), grid_palette(grid_off), True
    elif own_levels:
        # one palette for every level: the faded levels' near-greys need
        # entries of their own, which the top level alone wouldn't earn
        every = np.concatenate([l.reshape(-1, 1, 4) for l in own_levels], 0)
        _, pal, _, alpha = quantize(every, 256)
        idx = nearest(small, pal, alpha)
    # the quality steps (above): not for the HUD and menus (drawn at their
    # own size, where grain would show), the grid's exact entries, or a
    # detail map's own levels
    diffusing = QUALITY >= 2 and not grid and not own_levels and cls not in ('ui/hud', 'detail')
    if diffusing:
        pal, alpha = fit_palette(small, 256)
        idx = diffuse(small, pal, alpha, 256)
        rec = as_drawn(pal, alpha)[idx]
        if QUALITY_REPORT:
            old_idx, old_pal, _, _ = quantize(small, 256)
            quality_note(cls, blurred_psnr(small, as_drawn(old_pal, alpha)[old_idx]), blurred_psnr(small, rec))
    elif not grid and not own_levels:
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
                if diffusing:
                    pal16, _ = fit_palette(small, 16)
                    pal = pal16[:16]
                    idx = diffuse(small, pal, alpha, 16)
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
        ix = idx if not levels else (grid_index(level_img, grid_off) if grid else
                                     diffuse(level_img, pal, alpha, len(pal)) if diffusing else nearest(level_img, pal, alpha))
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
            chain_img = resample(chain_img, w, h, cls)
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
    ap.add_argument('--no-glow-maps', action='store_true',
                    help='no glow maps: the self-illumination pass adds its colour by the mask alone')
    ap.add_argument('--premultiplied-masks', action='store_true',
                    help='detail masks read from the RGBA-resized multipurpose map, darkened under its alpha (as before)')
    ap.add_argument('--showcase', action='store_true',
                    help='the main menu\'s pack: every map at its own size up to 512, objects, sky and level maps '
                         'in their own colours (ui.map; the levels never)')
    ap.add_argument('--showcase-32', action='store_true',
                    help='with --showcase: opaque maps in 32-bit colour too (by default 16-bit, dithered; '
                         'the screen is 16-bit)')
    ap.add_argument('--quality', type=int, default=1, choices=(1, 2),
                    help='1: palettes fitted in RGB, texels mapped to the nearest entry, sizes by a box filter '
                         '(the default); 2: Oklab fit, error diffusion, Lanczos (closer to the source by 2-9 dB, '
                         'but indistinguishable on the PSP-1000, and twice the palette step\'s bake time)')
    ap.add_argument('--quality-report', action='store_true',
                    help='also fit each texture the old way and log both against the source, per class')
    ap.add_argument('--showcase-palettes', action='store_true',
                    help='with --showcase: every map on a palette (256 colours), at its own size')
    a = ap.parse_args()
    global FULL_DETAIL, HUD_CLUT4, PREMULTIPLIED_MASKS, SHOWCASE, SHOWCASE_32, SHOWCASE_PALETTES
    SHOWCASE_PALETTES = a.showcase_palettes
    global QUALITY, QUALITY_REPORT
    QUALITY, QUALITY_REPORT = a.quality, a.quality_report
    PREMULTIPLIED_MASKS = a.premultiplied_masks
    SHOWCASE = a.showcase
    SHOWCASE_32 = a.showcase_32
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
        # a multipurpose map's palette is read for the change colour's mask
        own_colours = not any('multipurpose' in role for role in tag_roles)
        r = bake_one(m, b, cls, looks.get(b['address']), full_hud=full_hud, meter=meter, own_colours=own_colours)
        if r is None:
            skipped[cls] = skipped.get(cls, 0) + 1
            continue
        r['key'] = b['address']
        r['name'] = b['tag']
        entries.append(r)
        if b['address'] in mlooks:
            v = bake_one(m, b, cls, looks.get(b['address']), mlooks[b['address']], full_hud=full_hud, meter=meter,
                         own_colours=own_colours)
            if v is not None:
                v['key'] = b['address'] | 1
                v['name'] = b['tag'] + ' (with its detail map)'
                entries.append(v)
                print('  %s: detail map folded in for %s' % (b['tag'], mlooks[b['address']]['shader']), file=sys.stderr)
        if i % 50 == 0:
            print('  %d/%d' % (i, len(B)), file=sys.stderr)
    for shader, changed in MASKS_MENDED:
        print('detail mask read on its own for %s: %.0f%% of its texels changed%s' % (
            shader, 100.0 * changed, ' (--premultiplied-masks: kept as before)' if PREMULTIPLIED_MASKS else ''),
            file=sys.stderr)
    print('detail masks: %d shaders whose mask the resize had darkened' % len(set(s for s, _ in MASKS_MENDED)),
          file=sys.stderr)
    # the self-illuminated shaders' glow maps, at their base map's size and
    # from the base the GE draws (with its detail map folded in, if it has
    # one); shaders with the same two maps share one copy of the texels
    glows = glow_looks(m, B) if not a.no_glow_maps else {}
    made = {}
    glow_bytes = 0
    for key in sorted(glows):
        g = glows[key]
        base, mp = g['base'], g['multipurpose']
        pair = (base['address'], mp['address'])
        if pair not in made:
            mp_img = decode(m, mp)
            cls = refs.classify(roles.get(base['tag'], ()), base)
            made[pair] = None if mp_img is None else bake_one(
                m, base, cls, looks.get(base['address']), mlooks.get(base['address']), glow=mp_img)
            if made[pair] is not None:
                glow_bytes += len(made[pair]['data'])
        r = made[pair]
        if r is None:
            print('glow map for %s: none (%s or %s not baked)' % (g['shader'], base['tag'], mp['tag']), file=sys.stderr)
            continue
        entries.append(dict(r, key=key, name=g['shader'] + ' (glow map)'))
        print('glow map for %s: %s x the green of %s, %dx%d, %d bytes%s' % (
            g['shader'], base['tag'], mp['tag'], r['w'], r['h'], len(r['data']),
            '' if sum(1 for k in glows if (glows[k]['base']['address'], glows[k]['multipurpose']['address']) == pair) == 1
            else ' (shared)'), file=sys.stderr)
    print('glow maps: %d self-illuminated model shaders, %d maps, %d bytes%s' % (
        len(glows), sum(1 for v in made.values() if v is not None), glow_bytes,
        ' (--no-glow-maps)' if a.no_glow_maps else ''), file=sys.stderr)
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
    placed = {}
    for e in entries:
        if id(e['data']) in placed:
            start = placed[id(e['data'])]
        else:
            start = offset + len(blob)
            placed[id(e['data'])] = start
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
    print('quality %d: %s' % (QUALITY, 'palettes fitted in Oklab, texels by error diffusion, sizes by Lanczos'
                              if QUALITY >= 2 else 'as before (RGB fit, nearest entry, box)'), file=sys.stderr)
    for cls in sorted(QUALITY_STATS):
        n, old, new = QUALITY_STATS[cls]
        print('quality %s: %d textures, blurred PSNR %.2f dB as before, %.2f now' % (cls, n, old / n, new / n),
              file=sys.stderr)
    if SHOWCASE:
        own = [e for e in entries if e['psm'] in (0, 3)]
        for e in sorted(own, key=lambda e: -len(e['data'])):
            print('showcase: %s, %dx%d in %d-bit colour, %d levels, %d bytes' % (
                e['name'], e['w'], e['h'], 32 if e['psm'] == 3 else 16, e['levels'], len(e['data'])), file=sys.stderr)
        print('showcase: %d textures in their own colours (%d bytes), every map at its own size up to 512' % (
            len(own), sum(len(d) for d in {id(e['data']): e['data'] for e in own}.values())), file=sys.stderr)
    print('%s: %d textures, %d bytes; skipped %s' % (a.out, len(entries), len(out), skipped))


if __name__ == '__main__':
    main()
