"""seethrough.py - a see-through shader's tint, for the one map the PSP draws of
it (build 116).

The Xbox draws a transparent generic shader (sotr) with up to four maps run
through up to seven register-combiner stages (shader_transparent_generic_
preprocessor.c), and a transparent chicago shader (schi) with its maps folded
one into the next by each map's colour and alpha functions (shader_transparent_
chicago_preprocessor.c). The GE draws one texture at a time: the port picks one
map of the shader (psp_fixups.py, psp_transparent_map) and drew it untinted
until build 116. b30's ocean ("water simple": a reflection cube, then three
ripple maps) came out white, the ripple map drawn as it is.

This works the shader out the way the Xbox's combiners would, with every map
at its mean colour, and divides the result by the mean of the map the PSP
draws. The GE multiplies that map by the quotient (the tint), so the draw
averages to the shader's own colour and keeps its map's pattern. What the
mean can't stand for (the angle a surface is seen at, a constant colour's
animation over time) takes its middle value. Prints nothing; texpack.py
writes the tints into the pack, by the shader's tag address (psp_tex.c).
"""
import struct
import numpy as np

from bitmap_bake import decode

GENERIC, CHICAGO, WATER = 5, 6, 8
# the maps the PSP looks through for the one it draws (psp_transparent_map)
PSP_MAPS_LOOKED_AT = 4

# ---------------------------------------------------------------- the means


def _face_bytes(b, bpp):
    """one cube face's bytes, its mip levels included (the Xbox's textures
    are in 4x4 blocks for DXT, and a face is padded to 128 bytes)"""
    w, h, total = b['w'], b['h'], 0
    for _ in range(b['mips'] + 1):
        if b['format'].startswith('DXT'):
            total += max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if b['format'] == 'DXT1' else 16)
        else:
            total += w * h * bpp // 8
        w, h = max(1, w // 2), max(1, h // 2)
    return total


def bitmap_mean(m, b):
    """a bitmap's mean colour, RGBA 0-1, or None: its top level, every face
    of a cube map (a reflection averages what it reflects)"""
    from bitmap_bake import BPP
    if b['type'] != 'cube':
        img = decode(m, b)
        return None if img is None else img.reshape(-1, 4).mean(0) / 255.0
    faces = []
    bpp = BPP[b['fmt']] if b['fmt'] < len(BPP) else 0
    face = _face_bytes(b, bpp) if bpp else 0
    for stride in (face, (face + 127) // 128 * 128):
        if stride and 6 * stride == b['pixels_size']:
            for k in range(6):
                img = decode(m, dict(b, pixels_offset=b['pixels_offset'] + k * stride, pixels_size=stride))
                if img is None:
                    break
                faces.append(img.reshape(-1, 4).mean(0))
            break
    if len(faces) == 6:
        return np.mean(faces, 0) / 255.0
    img = decode(m, b)  # face 0 alone when the faces can't be placed
    return None if img is None else img.reshape(-1, 4).mean(0) / 255.0


class Means:
    """bitmap tag id -> the mean of its first bitmap, worked out once"""

    def __init__(self, m, bitmaps):
        self.m = m
        self.first = {}
        for b in bitmaps:
            self.first.setdefault(b['tag'], b)
        self.cache = {}

    def __call__(self, tag_id):
        if tag_id in self.cache:
            return self.cache[tag_id]
        t = self.m.by_id.get(tag_id)
        b = self.first.get(t.name) if t is not None and t.group == 'bitm' else None
        mean = bitmap_mean(self.m, b) if b else None
        self.cache[tag_id] = mean
        return mean


# ---------------------------------------------------------------- generic

# NV2A input mappings, by the tag's mapping index (code = index * 0x20)
def _mapped(x, mapping):
    x = np.asarray(x, dtype=np.float64)
    pos = np.maximum(x, 0.0)
    return [pos,                                 # clamp x (unsigned identity)
            1.0 - np.clip(x, 0.0, 1.0),          # 1 - clamp x (unsigned invert)
            2.0 * pos - 1.0,                     # expand normal
            1.0 - 2.0 * pos,                     # expand negate
            pos - 0.5,                           # half bias normal
            0.5 - pos,                           # half bias negate
            x,                                   # signed identity
            -x][mapping & 7]                     # signed negate


# the constant inputs (zero, one, one half, -1, -1/2) read the zero register
# through these mapping codes (shader_stage_constant_input_registers)
_CONSTANT_CODES = [
    [0x00, 0x20, 0x40, 0x20, 0x80, 0xa0, 0x00, 0x00],
    [0x20, 0x00, 0x20, 0x40, 0xa0, 0x80, 0x20, 0x40],
    [0xa0, 0xa0, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x80],
    [0x00, 0x20, 0x40, 0x20, 0x80, 0xa0, 0x40, 0x20],
    [0x00, 0x20, 0x40, 0x20, 0x80, 0xa0, 0x80, 0xa0],
]
_CODE_AT_ZERO = {0x00: 0.0, 0x20: 1.0, 0x40: -1.0, 0x60: 1.0, 0x80: -0.5, 0xa0: 0.5, 0xc0: 0.0, 0xe0: 0.0}
# the registers by the tag's input index, after the five constants: map
# colours 0-3, vertex colours 0-1, scratch 0-1, constants 0-1, then the same
# again for the other half (alpha for a colour input, blue for an alpha one)
_REGISTERS = ['T0', 'T1', 'T2', 'T3', 'V0', 'V1', 'R0', 'R1', 'C0', 'C1']
# outputs: discard, scratch 0 (the result), scratch 1, vertex 0-1, maps 0-3
_OUTPUTS = [None, 'R0', 'R1', 'V0', 'V1', 'T0', 'T1', 'T2', 'T3']
_OUTPUT_MAPPINGS = [lambda x: x, lambda x: x * 0.5, lambda x: x * 2.0, lambda x: x * 4.0,
                    lambda x: x - 0.5, lambda x: (x - 0.5) * 2.0]


def _colour_input(reg, index, mapping):
    if index < 5:
        return np.full(3, _CODE_AT_ZERO[_CONSTANT_CODES[index][mapping & 7]])
    index -= 5
    if index < 10:
        return _mapped(reg[_REGISTERS[index]][:3], mapping)
    if index < 20:
        return _mapped(np.full(3, reg[_REGISTERS[index - 10]][3]), mapping)
    return np.zeros(3)


def _alpha_input(reg, index, mapping):
    if index < 5:
        return float(_CODE_AT_ZERO[_CONSTANT_CODES[index][mapping & 7]])
    index -= 5
    if index < 10:
        return float(_mapped(reg[_REGISTERS[index]][3], mapping))
    if index < 20:
        return float(_mapped(reg[_REGISTERS[index - 10]][2], mapping))
    return 0.0


def _function_mean(function):
    """a periodic function's value over time: 'one' 1, 'zero' 0, the waves
    their middle"""
    return 1.0 if function == 0 else 0.0 if function == 1 else 0.5


def _argb(data, at):
    a, r, g, b = struct.unpack_from('<4f', data, at)
    return np.array([r, g, b, a])


def generic_result(m, o, maps, means):
    """the shader's colour (RGBA 0-1) with every map at its mean"""
    stages_n, stages_p = struct.unpack_from('<iI', m.data, o + 96)
    reg = {k: np.zeros(4) for k in _REGISTERS}
    for i, mean in enumerate(maps[:4]):
        reg['T%d' % i] = np.array(mean if mean is not None else np.zeros(4), dtype=np.float64)
    reg['V0'] = np.ones(4)        # the diffuse light: unlit, white
    reg['V1'] = np.full(4, 0.5)   # the fades by angle: their middle
    reg['R0'][3] = reg['T0'][3]   # the combiners start with map 0's alpha in scratch 0
    if stages_n <= 0:
        reg['R0'] = reg['T0'].copy()
    so = m.off(stages_p) if stages_n > 0 else None
    for s in range(min(stages_n, 7)):
        at = so + 112 * s
        flags, = struct.unpack_from('<H', m.data, at)
        function, = struct.unpack_from('<h', m.data, at + 6)
        lower, upper, colour1 = _argb(m.data, at + 12), _argb(m.data, at + 28), _argb(m.data, at + 44)
        reg['C0'] = lower + _function_mean(function) * (upper - lower)
        reg['C1'] = colour1
        c = struct.unpack_from('<14h', m.data, at + 60)
        a = struct.unpack_from('<12h', m.data, at + 88)
        A, B, C, D = (_colour_input(reg, c[2 * k], c[2 * k + 1]) for k in range(4))
        ab = np.full(3, A @ B) if c[9] == 1 else A * B
        cd = np.full(3, C @ D) if c[11] == 1 else C * D
        mux = reg['R0'][3] >= 0.5
        total = (cd if mux else ab) if flags & 1 else ab + cd
        fmap = _OUTPUT_MAPPINGS[c[13]] if 0 <= c[13] < 6 else _OUTPUT_MAPPINGS[0]
        colour_out = [(c[8], ab), (c[10], cd), (c[12], total)]
        aA, aB, aC, aD = (_alpha_input(reg, a[2 * k], a[2 * k + 1]) for k in range(4))
        aab, acd = aA * aB, aC * aD
        atotal = (acd if mux else aab) if flags & 2 else aab + acd
        amap = _OUTPUT_MAPPINGS[a[11]] if 0 <= a[11] < 6 else _OUTPUT_MAPPINGS[0]
        alpha_out = [(a[8], aab), (a[9], acd), (a[10], atotal)]
        for out, value in colour_out:
            if 0 < out < len(_OUTPUTS):
                reg[_OUTPUTS[out]][:3] = np.clip(fmap(value), -1.0, 1.0)
        for out, value in alpha_out:
            if 0 < out < len(_OUTPUTS):
                reg[_OUTPUTS[out]][3] = float(np.clip(amap(value), -1.0, 1.0))
    return np.clip(reg['R0'], 0.0, 1.0)


# ---------------------------------------------------------------- chicago

def _fold(c, n, function):
    """chicago's colour and alpha functions, current (c) with the next map (n)"""
    if function == 0: return c
    if function == 1: return n
    if function == 2: return c * n
    if function == 3: return 2.0 * c * n
    if function == 4: return c + n
    if function == 5: return n + 2.0 * c - 1.0    # add signed current
    if function == 6: return c + 2.0 * n - 1.0    # add signed next map
    if function == 7: return n - c                # subtract current
    if function == 8: return c - n                # subtract next map
    return None


def chicago_result(m, o, maps_p, count, means):
    """the shader's colour with every map at its mean: map 0, then each next
    map folded in by the previous map's functions"""
    mo = m.off(maps_p)
    current = None
    for i in range(min(count, 4)):
        at = mo + 220 * i
        flags, = struct.unpack_from('<H', m.data, at)
        mean = means[i]
        n = np.array(mean if mean is not None else np.zeros(4), dtype=np.float64)
        if flags & 2:   # alpha replicate: the map's alpha in its colour too
            n = np.array([n[3], n[3], n[3], n[3]])
        if current is None:
            current = n
            continue
        colour_function, alpha_function = struct.unpack_from('<hh', m.data, mo + 220 * (i - 1) + 44)
        rgb, alpha = current[:3], current[3]
        if colour_function in (9, 10, 11, 12):
            w = alpha if colour_function in (9, 10) else n[3]
            rgb = n[:3] * w + rgb * (1 - w) if colour_function in (9, 11) else rgb * w + n[:3] * (1 - w)
        else:
            rgb = _fold(rgb, n[:3], colour_function)
        if alpha_function in (9, 10, 11, 12):
            w = alpha if alpha_function in (9, 10) else n[3]
            alpha = n[3] * w + alpha * (1 - w) if alpha_function in (9, 11) else alpha * w + n[3] * (1 - w)
        else:
            alpha = _fold(alpha, n[3], alpha_function)
        if rgb is None or alpha is None:
            return None
        current = np.clip(np.append(rgb, alpha), -1.0, 1.0)
    return None if current is None else np.clip(current, 0.0, 1.0)


# ---------------------------------------------------------------- water

def water_tint(m, o, means):
    """water (swat): the Xbox draws the background times the base map's
    colour (flag bit 1), the frame's alpha from the base map's alpha and the
    view (bit 0), then the reflection cube times a tint between the parallel
    and perpendicular colours, added through that alpha (rasterizer_xbox_
    water.c). The GE draws the base map once: alpha-blended when its colour
    modulates the background (psp_fixups.py), so the background keeps
    1 - alpha of itself, alpha being one minus the base map's mean colour,
    and the source is what the reflection adds over that alpha; added
    otherwise. (tint, result, the base map's mean) or None"""
    flags, = struct.unpack_from('<H', m.data, o + 40)
    base_id, = struct.unpack_from('<i', m.data, o + 88)
    reflection_id, = struct.unpack_from('<i', m.data, o + 168)
    perpendicular, parallel = _argb(m.data, o + 108), _argb(m.data, o + 124)
    base = means(base_id)
    if base is None:
        return None
    reflection = means(reflection_id)
    r = reflection[:3] if reflection is not None else np.zeros(3)
    view = (perpendicular + parallel) * 0.5   # the view between face on and edge on
    through = base[3] * view[3] if flags & 1 else 1.0
    added = r * view[:3] * through
    if flags & 2:
        keep = float(np.clip(base[:3] @ np.array([0.299, 0.587, 0.114]), 0.0, 1.0))
        alpha = max(1.0 - keep, 1.0 / 255.0)
        result = np.append(np.clip(added / alpha, 0.0, 1.0), alpha)
    else:
        result = np.append(np.clip(added, 0.0, 1.0), 1.0)
    floor = np.maximum(base, 1.0 / 255.0)
    tint = np.clip(result / floor, 0.0, 1.0)
    if not flags & 2:
        tint[3] = 1.0
    return tint, result, base


# ---------------------------------------------------------------- the tint

def shader_tint(m, tag, means):
    """(tint RGBA 0-1, the drawn map's index, the shader's result, the drawn
    map's mean) for a generic, chicago or water shader, or None when the
    PSP's draw needs none (a numeric shader, no maps, a map that can't be
    read). A water shader's drawn map is its base map (index 0 here)"""
    if not tag.address or not m.in_tags(tag.address):
        return None
    o = m.off(tag.address)
    kind, = struct.unpack_from('<H', m.data, o + 36)
    if kind == WATER:
        found = water_tint(m, o, means)
        return None if found is None else (found[0], 0, found[1], found[2])
    if kind not in (GENERIC, CHICAGO):
        return None
    flags = m.data[o + 41]
    first_map_type, = struct.unpack_from('<h', m.data, o + 42)
    count, maps_p = struct.unpack_from('<iI', m.data, o + 84)
    skip = 1 if first_map_type != 0 else 0
    if flags & 0x80 or count <= skip:
        return None
    stride, scale_at, map_at = (220, 84, 120) if kind == CHICAGO else (100, 4, 40)
    mo = m.off(maps_p)
    ids = [struct.unpack_from('<i', m.data, mo + stride * i + map_at)[0] for i in range(count)]
    map_means = [means(i) for i in ids]
    # the map the PSP draws: of those after a cube map, the one with the
    # smallest scale (the map that spans the surface), as psp_transparent_map
    drawn = skip
    best, = struct.unpack_from('<f', m.data, mo + stride * drawn + scale_at)
    for i in range(skip + 1, min(count, skip + PSP_MAPS_LOOKED_AT)):
        scale, = struct.unpack_from('<f', m.data, mo + stride * i + scale_at)
        if scale != 0.0 and (best == 0.0 or scale <= best):
            drawn, best = i, scale
    if map_means[drawn] is None:
        return None
    result = generic_result(m, o, map_means, means) if kind == GENERIC else \
        chicago_result(m, o, maps_p, count, map_means)
    if result is None:
        return None
    base = np.maximum(map_means[drawn], 1.0 / 255.0)
    tint = np.clip(result / base, 0.0, 1.0)
    return tint, drawn, result, map_means[drawn]


def abgr(rgba):
    """a tint as the GE's colour word"""
    r, g, b, a = (int(round(float(v) * 255.0)) for v in rgba)
    return (a << 24) | (b << 16) | (g << 8) | r


def tints(m, bitmaps, shaders=None):
    """{shader tag address: (ABGR, the details)} for every generic and
    chicago shader whose draw needs a tint (one that isn't white)"""
    means = Means(m, bitmaps)
    out = {}
    for t in m.tags:
        if t.group not in ('sotr', 'schi', 'swat') or (shaders is not None and t.name not in shaders):
            continue
        found = shader_tint(m, t, means)
        if found is None:
            continue
        word = abgr(found[0])
        if word != 0xFFFFFFFF:
            out[t.address] = (word, t.name, found)
    return out
