#!/usr/bin/env python3
"""psp_glyphs.py - the PSP's button glyphs, for the game's button prompts.

Halo's prompts name Xbox buttons; the port shows the PSP-1000's instead
(docs/game_feature_survey.md, section 4.7), each in the shape of the button
itself: the face buttons as round buttons with their symbol, the D-pad as its
four pointed leaves, L and R as the slanted shoulder tabs with their curved
backs, the nub as its dimpled disc, START and SELECT as the small oval keys.

The glyphs are glass in the game's own colour, not pictures in fixed colours:
each texel holds a coverage (alpha, 4 bits) and a shade (4 bits), and the
draw (psp_ge.c, glyph_palette) turns the shade into colour from the colour
the game hands the icon, which is its text colour. So a prompt's glyph sits
in the HUD's or the menu's scheme. Shade 0 is the colour darkened to 15%,
0.7 the colour itself, and above it the colour lightens to white: the glass
body is the colour a little darkened, its gloss and rim lighter, the symbol
near white. glyph_palette in psp_ge.c and palette() here are the same rule.

Cells are CELL pixels high, one to three cells wide, packed into rows of 8
cells; psp_glyphs.h carries the layout (in the order of GLYPHS, which
psp_sdl.c's maps from Xbox buttons refer to) and, for psp_ge.c, the sheet as
one byte a texel (alpha << 4 | shade): the GE's 8-bit indexed format, 32 KB.
psp_glyphs.png beside it is a preview in the HUD's blue on the left and white
on the right.

usage: python3 tools/psp_glyphs.py ENGINE_CHECKOUT OUT_DIR [--cell=32] [--header=PATH]
(the repo's copies: OUT_DIR psp/glyphs, --header=psp/src/psp_glyphs.h;
ENGINE_CHECKOUT for Overpass, ChupathingyCE's port/assets/fonts)"""

import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

CELL = 32
# drawn this many times larger and reduced, for clean edges
SCALE = 4

# (name, kind, what, cells)
GLYPHS = [
    ("cross", "face", "cross", 1),
    ("circle", "face", "circle", 1),
    ("square", "face", "square", 1),
    ("triangle", "face", "triangle", 1),
    ("dpad_up", "dpad", "up", 1),
    ("dpad_down", "dpad", "down", 1),
    ("dpad_left", "dpad", "left", 1),
    ("dpad_right", "dpad", "right", 1),
    ("dpad", "dpad", None, 1),
    ("l", "shoulder", "L", 1),
    ("r", "shoulder", "R", 1),
    ("nub", "nub", None, 1),
    ("face", "cluster", None, 1),
    ("start", "pill", "START", 2),
    ("select", "pill", "SELECT", 2),
    ("select_up", "chord", ("select", "dpad_up"), 3),
    ("select_down", "chord", ("select", "dpad_down"), 3),
    ("select_l", "chord", ("select", "l"), 3),
]

# the shades (0-1, see the docstring)
DARK_EDGE = 0.0
BODY_TOP, BODY_BOTTOM = 0.47, 0.30
GLOSS = 0.84
RIM = 0.86
SYMBOL = 0.96


class Layers:
    """a glyph as it is built up: coverage A and premultiplied shade P"""

    def __init__(self, width, height):
        self.a = np.zeros((height, width), np.float32)
        self.p = np.zeros((height, width), np.float32)

    def over(self, coverage, alpha, shade):
        """a layer on top: coverage 0-1 (an array), its alpha, its shade (a
        number or an array)"""
        a = coverage * alpha
        self.p = shade * a + self.p * (1.0 - a)
        self.a = a + self.a * (1.0 - a)


def blank(width, height):
    return Image.new("L", (width, height), 0)


def as_array(mask):
    return np.asarray(mask, np.float32) / 255.0


def rounded(mask, radius):
    """the shape's corners rounded: blurred and cut back at half"""
    if radius < 1:
        return mask
    return mask.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: 255 if v >= 128 else 0)


def grown(mask, pixels):
    size = 2 * int(pixels) + 1
    return mask.filter(ImageFilter.MaxFilter(size)) if size > 1 else mask


def shrunk(mask, pixels):
    size = 2 * int(pixels) + 1
    return mask.filter(ImageFilter.MinFilter(size)) if size > 1 else mask


def bounds(mask):
    box = mask.getbbox()
    return box if box else (0, 0, mask.width, mask.height)


def glass(layers, mask, strength=1.0, body=None):
    """a glass button of this shape: a dark edge to stand out from what is
    behind, the body (the colour, darker toward the bottom; or the shades
    given), a gloss over its upper part and a bright rim"""
    size = mask.height
    x0, y0, x1, y1 = bounds(mask)
    top = (np.arange(mask.height, dtype=np.float32)[:, None] - y0) / max(1, y1 - y0)
    top = np.clip(top, 0.0, 1.0)
    m = as_array(mask)
    layers.over(as_array(grown(mask, size * 0.03)), 0.55 * strength, DARK_EDGE)
    shade = BODY_TOP + (BODY_BOTTOM - BODY_TOP) * top if body is None else body
    layers.over(m, 0.9 * strength, shade)
    # the gloss: an ellipse over the top of the shape, fading downward
    gloss = blank(mask.width, mask.height)
    ImageDraw.Draw(gloss).ellipse((x0 - (x1 - x0) * 0.25, y0 - (y1 - y0) * 0.55, x1 + (x1 - x0) * 0.25,
                                   y0 + (y1 - y0) * 0.5), fill=255)
    fade = np.clip(1.0 - top * 1.6, 0.0, 1.0)
    layers.over(as_array(gloss) * m * fade, 0.55 * strength, GLOSS)
    rim = as_array(mask) - as_array(shrunk(mask, max(1, size * 0.025)))
    layers.over(np.clip(rim, 0.0, 1.0), 0.75 * strength, RIM)


def symbol(layers, mask, strength=1.0):
    layers.over(as_array(mask), strength, SYMBOL)


def points_at(points, size, dx=0.0):
    return [((x + dx) * size, y * size) for x, y in points]


def outline(draw, points, width):
    """a closed outline with round joins"""
    draw.line(points + points[:2], fill=255, width=int(width), joint="curve")


def face_symbol(draw, kind, cx, cy, r, size):
    """the PlayStation symbols as the PSP-1000 marks them: thin outlines"""
    width = size * r * 0.17
    if kind == "triangle":
        k = r * 0.52
        pts = [(cx, cy - k * 1.05), (cx + k * 0.95, cy + k * 0.6), (cx - k * 0.95, cy + k * 0.6)]
        outline(draw, [(x * size, y * size) for x, y in pts], width)
    elif kind == "circle":
        k = r * 0.46 * size
        draw.ellipse((cx * size - k, cy * size - k, cx * size + k, cy * size + k), outline=255, width=int(width))
    elif kind == "cross":
        k = r * 0.42
        for a, b in (((cx - k, cy - k), (cx + k, cy + k)), ((cx + k, cy - k), (cx - k, cy + k))):
            draw.line([(a[0] * size, a[1] * size), (b[0] * size, b[1] * size)], fill=255, width=int(width))
    else:
        k = r * 0.4
        pts = [(cx - k, cy - k), (cx + k, cy - k), (cx + k, cy + k), (cx - k, cy + k)]
        outline(draw, [(x * size, y * size) for x, y in pts], width)


def face_button(layers, kind, size, cx=0.5, cy=0.5, r=0.44):
    mask = blank(layers.a.shape[1], size)
    ImageDraw.Draw(mask).ellipse(((cx - r) * size, (cy - r) * size, (cx + r) * size, (cy + r) * size), fill=255)
    glass(layers, mask)
    mark = blank(layers.a.shape[1], size)
    face_symbol(ImageDraw.Draw(mark), kind, cx, cy, r, size)
    symbol(layers, mark)


def rotate(points, quarter):
    """points about the cell's centre by quarter turns (clockwise)"""
    out = []
    for x, y in points:
        for _ in range(quarter):
            x, y = 1.0 - y, x
        out.append((x, y))
    return out


def bezier(a, control, b, steps=24):
    return [((1 - t) ** 2 * a[0] + 2 * (1 - t) * t * control[0] + t * t * b[0],
             (1 - t) ** 2 * a[1] + 2 * (1 - t) * t * control[1] + t * t * b[1])
            for t in (i / steps for i in range(steps + 1))]


# one leaf of the D-pad as the up button, as a PSP-3000's photo shows it
# (Sheepdog01): its outer edge gently bowed, straight sides for its outer half, then
# a taper to a point at the pad's middle; corners and point rounded (DPAD_ROUND,
# drawn as rounded()). Its arrow is solid, as the PSP's outline is lost at the
# HUD's size
DPAD_LEAF = ([(0.1, 0.5), (0.1, 0.17)] + bezier((0.1, 0.17), (0.5, 0.0), (0.9, 0.17))[1:] +
             [(0.9, 0.5), (0.5, 0.92)])
DPAD_ARROW = [(0.5, 0.2), (0.67, 0.44), (0.33, 0.44)]
DPAD_ROUND = 0.055
DPAD_TURNS = {"up": 0, "right": 1, "down": 2, "left": 3}


def dpad_leaf(layers, turns, size, scale=1.0, dx=0.0, dy=0.0):
    """a leaf turned to its direction, drawn at scale about the point dx, dy
    from the cell's middle"""
    def place(points):
        return [((0.5 + (x - 0.5) * scale + dx) * size, (0.5 + (y - 0.5) * scale + dy) * size)
                for x, y in rotate(points, turns)]

    mask = blank(size, size)
    ImageDraw.Draw(mask).polygon(place(DPAD_LEAF), fill=255)
    glass(layers, rounded(mask, size * DPAD_ROUND * scale))
    arrow = blank(size, size)
    ImageDraw.Draw(arrow).polygon(place(DPAD_ARROW), fill=255)
    symbol(layers, arrow)


def dpad(layers, lit, size):
    """a D-pad button: the one leaf of the PSP's pad, pointing in to the
    middle; with none named, the whole pad, its four leaves spaced round the
    middle as the PSP's are (a gap between the points)"""
    if lit is not None:
        dpad_leaf(layers, DPAD_TURNS[lit], size)
        return
    offsets = {"up": (0.0, -0.275), "right": (0.275, 0.0), "down": (0.0, 0.275), "left": (-0.275, 0.0)}
    for name, turns in DPAD_TURNS.items():
        dpad_leaf(layers, turns, size, 0.44, *offsets[name])


def shoulder(layers, letter, size, font):
    """the PSP's L or R, after Sheepdog01's sketch and a PSP-3000's photo: a tab
    flat along its top and bottom, its inner side a straight slant, its back
    a smooth convex curve round the PSP's rounded corner, from the flat top
    down to the bottom; the hard corners stay sharp (rounded only enough not
    to sparkle). R is L mirrored"""
    shape = [(0.97, 0.19), (0.74, 0.81), (0.03, 0.81)] + bezier((0.03, 0.81), (0.07, 0.21), (0.46, 0.19))
    mask = blank(size, size)
    ImageDraw.Draw(mask).polygon(points_at(shape, size), fill=255)
    mask = rounded(mask, size * 0.012)
    if letter == "R":
        mask = mask.transpose(Image.FLIP_LEFT_RIGHT)
    glass(layers, mask)
    mark = blank(size, size)
    draw = ImageDraw.Draw(mark)
    box = draw.textbbox((0, 0), letter, font=font)
    width, height = box[2] - box[0], box[3] - box[1]
    cx = 0.52 if letter == "L" else 0.48
    draw.text((cx * size - width / 2 - box[0], 0.5 * size - height / 2 - box[1]), letter, font=font, fill=255)
    symbol(layers, mark)


def nub(layers, size):
    """the PSP's nub: a dark disc, densely dimpled (a checker of two shades)"""
    r = 0.42
    mask = blank(size, size)
    ImageDraw.Draw(mask).ellipse(((0.5 - r) * size, (0.5 - r) * size, (0.5 + r) * size, (0.5 + r) * size), fill=255)
    cells = 7
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    step = 2 * r * size / cells
    checker = ((np.floor((x - (0.5 - r) * size) / step) + np.floor((y - (0.5 - r) * size) / step)) % 2).astype(np.float32)
    body = 0.2 + 0.2 * checker - 0.08 * (y / size)
    glass(layers, mask, body=body)


def cluster(layers, size):
    """the four face buttons in their diamond: the face buttons as a stick"""
    r = 0.2
    for kind, (cx, cy) in (("triangle", (0.5, 0.22)), ("square", (0.22, 0.5)), ("circle", (0.78, 0.5)),
                           ("cross", (0.5, 0.78))):
        face_button(layers, kind, size, cx, cy, r)


def pill(layers, text, cells, size, font):
    """START or SELECT, after Sheepdog01's sketch: the PSP-1000's key, flat along
    its top and curved underneath like a shallow bowl, its name on it (the
    two keys are one shape)"""
    width = cells * size
    left, right, top, bottom = 0.06 * size, width - 0.06 * size, 0.22 * size, 0.8 * size
    # the bowl: half an ellipse below the flat top, its ends a little
    # rounded where they meet the top
    steps = 48
    edge = [(left + (right - left) * (0.5 - 0.5 * math.cos(math.pi * i / steps)),
             top + (bottom - top) * math.sin(math.pi * i / steps) ** 0.8) for i in range(steps + 1)]
    mask = blank(width, size)
    ImageDraw.Draw(mask).polygon([(left, top)] + edge + [(right, top)], fill=255)
    glass(layers, rounded(mask, size * 0.015))
    mark = blank(width, size)
    draw = ImageDraw.Draw(mark)
    box = draw.textbbox((0, 0), text, font=font)
    tw, th = box[2] - box[0], box[3] - box[1]
    draw.text(((width - tw) / 2 - box[0], 0.44 * size - th / 2 - box[1]), text, font=font, fill=255)
    symbol(layers, mark)


def paste(target, source, x, y, scale=1.0):
    """one glyph's layers into another's, at x, y, smaller by scale"""
    a, p = source.a, source.p
    if scale != 1.0:
        new = (max(1, round(a.shape[1] * scale)), max(1, round(a.shape[0] * scale)))
        a = np.asarray(Image.fromarray(a, "F").resize(new, Image.BOX))
        p = np.asarray(Image.fromarray(p, "F").resize(new, Image.BOX))
    h, w = a.shape
    region = (slice(y, y + h), slice(x, x + w))
    target.p[region] = p + target.p[region] * (1.0 - a)
    target.a[region] = a + target.a[region] * (1.0 - a)


def chord(layers, first, second, size, font):
    """SELECT at four fifths, a plus, then the other button: three cells"""
    paste(layers, first, 0, round(size * 0.1), 0.8)
    paste(layers, second, size * 2, 0)
    mark = blank(size * 3, size)
    draw = ImageDraw.Draw(mark)
    box = draw.textbbox((0, 0), "+", font=font)
    width, height = box[2] - box[0], box[3] - box[1]
    middle = (first.a.shape[1] * 0.8 + size * 2) / 2
    draw.text((middle - width / 2 - box[0], (size - height) / 2 - box[1]), "+", font=font, fill=255)
    symbol(layers, mark)


def palette(tint, fade=1.0):
    """psp_ge.c's glyph_palette: index alpha << 4 | shade to RGBA"""
    out = np.zeros((256, 4), np.float32)
    base = np.array(tint, np.float32)
    for index in range(256):
        a, s = (index >> 4) / 15.0, (index & 15) / 15.0
        if s <= 0.7:
            colour = base * (0.15 + 0.85 * s / 0.7)
        else:
            colour = base + (255.0 - base) * ((s - 0.7) / 0.3)
        out[index, :3] = colour
        out[index, 3] = a * 255.0 * fade
    return np.clip(out, 0, 255).astype(np.uint8)


def main():
    global CELL
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    header_path = None
    for a in sys.argv[1:]:
        if a.startswith("--cell="):
            CELL = int(a.split("=", 1)[1])
        elif a.startswith("--header="):
            header_path = a.split("=", 1)[1]
    if len(args) != 2:
        sys.exit(__doc__)
    engine, out = args
    letter_path = os.path.join(engine, "port/assets/fonts/Overpass-900.ttf")
    big = CELL * SCALE
    letter_font = ImageFont.truetype(letter_path, big * 2 // 5)
    pill_font = ImageFont.truetype(letter_path, big * 3 // 10)
    plus_font = ImageFont.truetype(letter_path, big // 2)

    drawn = {}
    for name, kind, what, cells in GLYPHS:
        layers = Layers(big * cells, big)
        if kind == "face":
            face_button(layers, what, big)
        elif kind == "dpad":
            dpad(layers, what, big)
        elif kind == "shoulder":
            shoulder(layers, what, big, letter_font)
        elif kind == "nub":
            nub(layers, big)
        elif kind == "cluster":
            cluster(layers, big)
        elif kind == "pill":
            pill(layers, what, cells, big, pill_font)
        else:
            chord(layers, drawn[what[0]], drawn[what[1]], big, plus_font)
        drawn[name] = layers

    # packed left to right in rows of 8 cells, a wide glyph never split
    columns = 8
    places, x, y = [], 0, 0
    for name, kind, what, cells in GLYPHS:
        if x + cells > columns:
            x, y = 0, y + 1
        places.append((name, x, y, cells))
        x += cells
    rows = y + 1
    sheet_a = np.zeros((rows * CELL, columns * CELL), np.float32)
    sheet_p = np.zeros_like(sheet_a)
    for name, x, y, cells in places:
        layers = drawn[name]
        # reduced by averaging each SCALE x SCALE block (premultiplied, so
        # the shade at an edge stays the edge's own)
        h, w = layers.a.shape
        a = layers.a.reshape(h // SCALE, SCALE, w // SCALE, SCALE).mean(axis=(1, 3))
        p = layers.p.reshape(h // SCALE, SCALE, w // SCALE, SCALE).mean(axis=(1, 3))
        sheet_a[y * CELL:(y + 1) * CELL, x * CELL:(x + cells) * CELL] = a
        sheet_p[y * CELL:(y + 1) * CELL, x * CELL:(x + cells) * CELL] = p
    shade = np.where(sheet_a > 1e-4, sheet_p / np.maximum(sheet_a, 1e-4), 0.0)
    alpha4 = np.clip(np.rint(sheet_a * 15.0), 0, 15).astype(np.uint8)
    shade4 = np.clip(np.rint(shade * 15.0), 0, 15).astype(np.uint8)
    indices = (alpha4 << 4) | np.where(alpha4 > 0, shade4, 0).astype(np.uint8)

    os.makedirs(out, exist_ok=True)
    # the preview: the sheet as the GE draws it, in the HUD's blue and in white
    blue = palette((96, 160, 255))[indices]
    white = palette((235, 235, 235))[indices]
    preview = Image.new("RGBA", (columns * CELL * 2 + 8, rows * CELL), (0, 0, 0, 0))
    preview.alpha_composite(Image.fromarray(blue, "RGBA"), (0, 0))
    preview.alpha_composite(Image.fromarray(white, "RGBA"), (columns * CELL + 8, 0))
    preview.save(os.path.join(out, "psp_glyphs.png"))

    with open(header_path or os.path.join(out, "psp_glyphs.h"), "w") as header:
        header.write("/* psp_glyphs.h - the button glyph sheet (tools/psp_glyphs.py writes it, with a preview,\n"
                     "psp/glyphs/psp_glyphs.png): each glyph's cell in pixels, x y width height */\n")
        header.write("#define PSP_GLYPH_SHEET_WIDTH %d\n#define PSP_GLYPH_SHEET_HEIGHT %d\n" % (columns * CELL, rows * CELL))
        header.write("enum psp_glyph\n{\n")
        for name, *_ in places:
            header.write("\tPSP_GLYPH_%s,\n" % name.upper())
        header.write("\tPSP_GLYPH_COUNT\n};\n")
        header.write("static const unsigned short psp_glyph_cells[PSP_GLYPH_COUNT][4] =\n{\n")
        for name, x, y, cells in places:
            header.write("\t{ %d, %d, %d, %d }, /* %s */\n" % (x * CELL, y * CELL, cells * CELL, CELL, name))
        header.write("};\n")
        # only psp_ge.c, which draws them, asks for the texels: one byte
        # each, alpha << 4 | shade (glyph_palette there turns them to colour)
        header.write("#ifdef PSP_GLYPH_PIXELS\n")
        flat = indices.reshape(-1)
        header.write("static const unsigned char psp_glyph_pixels[%d] __attribute__((aligned(16))) =\n{\n" % len(flat))
        for at in range(0, len(flat), 32):
            header.write("\t" + ",".join("%d" % b for b in flat[at:at + 32]) + ",\n")
        header.write("};\n#endif\n")
    print("psp_glyphs: %d glyphs, sheet %dx%d" % (len(places), columns * CELL, rows * CELL))


if __name__ == "__main__":
    main()
