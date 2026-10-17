#!/usr/bin/env python3
"""Draws ICON0.PNG, the game-list tile, for both builds from the Sierra-III mark.

The tile: the steel plate and border of the first placeholder, the border in the mark's amber;
the mark as it sits on the wallpaper (PSP under the III); the build named beneath it in
Audiowide, the font of the title's COMBAT EVOLVED. Written to psp/xmb/<campaign|mp>/ICON0.PNG.

Drawn at 4x and shrunk to 144 x 80: the mark's outline is about one pixel wide at tile size,
and drawn there directly it would break up (the wallpaper is drawn at 2x for the same reason).

Fonts: Audiowide is under the SIL Open Font License (fonts/OFL.txt). Art made with it ships
freely; the font file itself is unmodified.

usage: python3 psp/xmb/icon0.py      (needs Pillow and cairosvg)"""
import io
import os

import cairosvg
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
TILE = (144, 80)                     # the firmware's fixed size
SS = 4                               # supersampling

BUILDS = {"campaign": "CAMPAIGN", "mp": "MULTIPLAYER"}
# The colours are the mark's and the title's (sierra_mark.svg), so the tile reads as one set.
PLATE_TOP, PLATE_BOTTOM = (34, 40, 46), (10, 12, 14)
AMBER_TOP, AMBER_BOTTOM = (0xc2, 0x9b, 0x63), (0xa0, 0x6e, 0x26)
STEEL_TOP, STEEL_BOTTOM = (0xe9, 0xee, 0xf2), (0x9a, 0xa7, 0xb2)

BORDER = 2                           # px at tile size, as on the placeholder
MARK_W = 120                         # the lettering's width at tile size (600 units in the SVG)
MARK_X, MARK_Y = 12, 11              # top-left of the lettering
LABEL_PX = 10                        # label font size at tile size
LABEL_X, LABEL_BOTTOM = 13, 67       # left edge and baseline of the label
TRACK = 0.14                         # label tracking, in ems: COMBAT EVOLVED is set wide


def vgradient(size, top, bottom):
    w, h = size
    t = np.linspace(0, 1, h)[:, None, None]
    rgb = np.array(top) * (1 - t) + np.array(bottom) * t
    return Image.fromarray(rgb.repeat(w, 1).astype(np.uint8))


def with_shadow(layer, dy, blur, opacity=0.7):
    """The title's drop shadow (feDropShadow on the wallpaper): the layer's alpha, darkened,
    moved down and softened, under the layer."""
    pad = dy + int(blur * 3)         # room below, so the shadow isn't cut off at the edge
    a = Image.new("L", (layer.width, layer.height + pad))
    a.paste(layer.getchannel("A"))
    sh = Image.new("RGBA", a.size, (0, 0, 0, 0))
    sh.putalpha(a.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: int(v * opacity)))
    out = Image.new("RGBA", a.size, (0, 0, 0, 0))
    out.alpha_composite(sh, (0, dy))
    out.alpha_composite(layer)
    return out


def mark(width):
    """The mark rendered from its master. The SVG's viewBox has a 4-unit margin for the stroke."""
    with open(os.path.join(HERE, "sierra_mark.svg")) as f:
        svg = f.read()
    scale = width / 600

    def render(src):
        png = cairosvg.svg2png(bytestring=src.encode(), output_width=round(608 * scale))
        return Image.open(io.BytesIO(png)).convert("RGBA")

    # The master draws its outline under the fill (paint-order="stroke"); cairosvg ignores
    # that and strokes over the fill, which buries PSP's thin strokes. So: outline pass, then
    # the fill on top of it.
    out = render(svg.replace('fill="url(#slate)"', 'fill="none"').replace('fill="url(#amber)"', 'fill="none"'))
    out.alpha_composite(render(svg.replace('stroke="#06080a"', 'stroke="none"')))
    return out, 4 * scale


def label(text, px):
    """The build's name, tracked out and filled with the title's steel."""
    font = ImageFont.truetype(os.path.join(HERE, "fonts", "Audiowide-Regular.ttf"), px)
    track = TRACK * px
    width = sum(font.getlength(c) for c in text) + track * (len(text) - 1)
    ascent, descent = font.getmetrics()
    mask = Image.new("L", (int(width) + 2, ascent + descent))
    d, x = ImageDraw.Draw(mask), 0
    for c in text:
        d.text((x, 0), c, font=font, fill=255)
        x += font.getlength(c) + track
    fill = vgradient(mask.size, STEEL_TOP, STEEL_BOTTOM).convert("RGBA")
    fill.putalpha(mask)
    return fill, ascent


def tile(name):
    w, h = TILE[0] * SS, TILE[1] * SS
    img = vgradient((w, h), AMBER_TOP, AMBER_BOTTOM).convert("RGBA")
    b = BORDER * SS
    img.paste(vgradient((w - 2 * b, h - 2 * b), PLATE_TOP, PLATE_BOTTOM), (b, b))

    m, margin = mark(MARK_W * SS)
    img.alpha_composite(with_shadow(m, dy=SS, blur=SS * 0.8), (round(MARK_X * SS - margin), round(MARK_Y * SS - margin)))

    lab, ascent = label(name, LABEL_PX * SS)
    img.alpha_composite(with_shadow(lab, dy=SS, blur=SS * 0.6), (LABEL_X * SS, LABEL_BOTTOM * SS - ascent))
    return img.convert("RGB").resize(TILE, Image.LANCZOS)


def main():
    for build, name in BUILDS.items():
        path = os.path.join(HERE, build, "ICON0.PNG")
        # plain, non-interlaced PNG with no metadata: the XMB can show a blank tile otherwise
        tile(name).save(path, format="PNG", optimize=True)
        print("wrote", os.path.relpath(path, HERE), TILE)


if __name__ == "__main__":
    main()
