#!/usr/bin/env python3
"""psp_glyphs.py - the PSP's button glyphs, for the game's button prompts.

Halo's prompts name Xbox buttons; the port shows the PSP's instead
(docs/game_feature_survey.md, section 4.7). This draws the glyph sheet from
Kenney's Input Prompts (CC0, the PlayStation font ChupathingyCE's own button
glyphs use: port/linux/ui/fonts/input_playstation.ttf in the engine checkout)
and, for what that font has no PSP glyph for (the PSP's L and R, its nub,
the Select chords), shapes drawn here in the same style.

Every glyph is white on transparent, so the game tints it as it tints the
Xbox's. Cells are CELL pixels high, one to three cells wide; the sheet's
layout is written beside it as a C table (psp_glyphs.h), in the order of
GLYPHS below, which psp_sdl.c's maps from Xbox buttons refer to.

usage: python3 tools/psp_glyphs.py ENGINE_CHECKOUT OUT_DIR [--cell=32] [--header=PATH]
(the repo's copies: OUT_DIR psp/glyphs, --header=psp/src/psp_glyphs.h)"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

CELL = 32

# (name, kind, what, cells): kind "font" is a codepoint of the PlayStation
# font; "dpad" the D-pad with one arm (or none: all) lit; "pill" a small
# button with its name on it (Start, Select); "shoulder" a PSP shoulder
# button with its letter; "nub" the PSP's nub; "cluster" the four face
# buttons in their diamond (the face buttons as a stick: moving); "chord"
# two glyphs with a plus between them. Cells: the glyph's width in cells
GLYPHS = [
    ("cross", "font", 0xE04B, 1),
    ("circle", "font", 0xE041, 1),
    ("square", "font", 0xE051, 1),
    ("triangle", "font", 0xE053, 1),
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


def font_glyph(font, codepoint, size):
    """a font glyph scaled to fill nine tenths of a size x size cell"""
    room = int(font.size) * 2
    probe = Image.new("LA", (room, room), (255, 0))
    draw = ImageDraw.Draw(probe)
    draw.text((room // 4, room // 4), chr(codepoint), font=font, fill=(255, 255))
    box = probe.getbbox()
    glyph = probe.crop(box)
    scale = size * 0.9 / max(glyph.width, glyph.height)
    glyph = glyph.resize((max(1, round(glyph.width * scale)), max(1, round(glyph.height * scale))), Image.LANCZOS)
    cell = Image.new("LA", (size, size), (255, 0))
    cell.alpha_composite(glyph, ((size - glyph.width) // 2, (size - glyph.height) // 2))
    return cell


def dpad(lit, size):
    """the D-pad as a cross; the pressed arm solid, the rest faint (all solid
    with none named), which still reads at the HUD's 14 pixels"""
    cell = Image.new("LA", (size, size), (255, 0))
    draw = ImageDraw.Draw(cell)
    arm, margin = size * 3 // 10, size // 20
    near, far = (size - arm) // 2, (size + arm) // 2
    faint = 255 if lit is None else 90
    draw.rounded_rectangle((near, margin, far, size - margin), radius=size // 16, fill=(255, faint))
    draw.rounded_rectangle((margin, near, size - margin, far), radius=size // 16, fill=(255, faint))
    arms = {
        "up": (near, margin, far, near),
        "down": (near, far, far, size - margin),
        "left": (margin, near, near, far),
        "right": (far, near, size - margin, far),
    }
    if lit in arms:
        draw.rounded_rectangle(arms[lit], radius=size // 16, fill=(255, 255))
    return cell


def pill(text, width, height, text_font):
    """a small button with its name in it, as the PSP's Start and Select are
    named under them"""
    cell = Image.new("LA", (width, height), (255, 0))
    draw = ImageDraw.Draw(cell)
    draw.rounded_rectangle((height // 10, height // 5, width - height // 10, height - height // 5),
                           radius=height * 3 // 10, fill=(255, 255))
    box = draw.textbbox((0, 0), text, font=text_font)
    tw, th = box[2] - box[0], box[3] - box[1]
    draw.text(((width - tw) / 2 - box[0], (height - th) / 2 - box[1]), text, font=text_font, fill=(255, 0))
    return cell


def shoulder(letter, size, letter_font):
    """the PSP's shoulder button: a wide rounded tab with its letter cut out"""
    cell = Image.new("LA", (size, size), (255, 0))
    draw = ImageDraw.Draw(cell)
    inset = size // 8
    draw.rounded_rectangle((inset, size // 4, size - inset, size - size // 4), radius=size // 5, fill=(255, 255))
    box = draw.textbbox((0, 0), letter, font=letter_font)
    width, height = box[2] - box[0], box[3] - box[1]
    draw.text(((size - width) / 2 - box[0], (size - height) / 2 - box[1]), letter, font=letter_font, fill=(255, 0))
    return cell


def nub(size):
    """the PSP's nub, seen from above: a disc with a ring cut in it"""
    cell = Image.new("LA", (size, size), (255, 0))
    draw = ImageDraw.Draw(cell)
    margin = size // 8
    draw.ellipse((margin, margin, size - margin, size - margin), fill=(255, 255))
    ring = size // 4
    draw.ellipse((ring, ring, size - ring, size - ring), outline=(255, 0), width=max(1, size // 16))
    return cell


def cluster(font, size):
    """the four face buttons in their diamond"""
    cell = Image.new("LA", (size, size), (255, 0))
    small = size * 2 // 5
    for codepoint, (x, y) in ((0xE053, (1, 0)), (0xE051, (0, 1)), (0xE041, (2, 1)), (0xE04B, (1, 2))):
        glyph = font_glyph(font, codepoint, small)
        cell.alpha_composite(glyph, (x * (size - small) // 2, y * (size - small) // 2))
    return cell


def chord(first, second, size, plus_font):
    """a two-cell glyph at four fifths, a plus, and a one-cell glyph: three
    cells wide"""
    cell = Image.new("LA", (size * 3, size), (255, 0))
    smaller = first.resize((first.width * 4 // 5, first.height * 4 // 5), Image.LANCZOS)
    cell.alpha_composite(smaller, (0, (size - smaller.height) // 2))
    cell.alpha_composite(second, (size * 2, 0))
    draw = ImageDraw.Draw(cell)
    box = draw.textbbox((0, 0), "+", font=plus_font)
    width, height = box[2] - box[0], box[3] - box[1]
    middle = (smaller.width + size * 2) / 2
    draw.text((middle - width / 2 - box[0], (size - height) / 2 - box[1]), "+", font=plus_font, fill=(255, 255))
    return cell


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    header_path = None
    for a in sys.argv[1:]:
        if a.startswith("--cell="):
            global CELL
            CELL = int(a.split("=", 1)[1])
        elif a.startswith("--header="):
            header_path = a.split("=", 1)[1]
    if len(args) != 2:
        sys.exit(__doc__)
    engine, out = args
    font_path = os.path.join(engine, "port/linux/ui/fonts/input_playstation.ttf")
    letter_path = os.path.join(engine, "port/assets/fonts/Overpass-900.ttf")
    # drawn at 4x and reduced, for clean edges at the PSP's sizes
    big = CELL * 4
    font = ImageFont.truetype(font_path, big)
    letter_font = ImageFont.truetype(letter_path, big * 3 // 5)
    pill_font = ImageFont.truetype(letter_path, big * 2 // 5)
    plus_font = ImageFont.truetype(letter_path, big * 3 // 5)

    drawn = {}
    for name, kind, what, cells in GLYPHS:
        if kind == "font":
            image = font_glyph(font, what, big)
        elif kind == "dpad":
            image = dpad(what, big)
        elif kind == "pill":
            image = pill(what, big * cells, big, pill_font)
        elif kind == "shoulder":
            image = shoulder(what, big, letter_font)
        elif kind == "nub":
            image = nub(big)
        elif kind == "cluster":
            image = cluster(font, big)
        else:
            image = chord(drawn[what[0]], drawn[what[1]], big, plus_font)
        drawn[name] = image

    # one row per glyph would waste room: cells packed left to right in rows
    # of 8 cells (256 pixels at 32), a two-cell glyph never split
    columns = 8
    places, x, y = [], 0, 0
    for name, kind, what, cells in GLYPHS:
        if x + cells > columns:
            x, y = 0, y + 1
        places.append((name, x, y, cells))
        x += cells
    rows = y + 1
    sheet = Image.new("LA", (columns * CELL, rows * CELL), (255, 0))
    for name, x, y, cells in places:
        small = drawn[name].resize((cells * CELL, CELL), Image.LANCZOS)
        sheet.alpha_composite(small, (x * CELL, y * CELL))

    os.makedirs(out, exist_ok=True)
    sheet.save(os.path.join(out, "psp_glyphs.png"))
    with open(header_path or os.path.join(out, "psp_glyphs.h"), "w") as header:
        header.write("/* psp_glyphs.h - the button glyph sheet's layout (tools/psp_glyphs.py writes it, beside\n"
                     "psp_glyphs.png): each glyph's cell in pixels, x y width height */\n")
        header.write("#define PSP_GLYPH_SHEET_WIDTH %d\n#define PSP_GLYPH_SHEET_HEIGHT %d\n" % (columns * CELL, rows * CELL))
        header.write("enum psp_glyph\n{\n")
        for name, *_ in places:
            header.write("\tPSP_GLYPH_%s,\n" % name.upper())
        header.write("\tPSP_GLYPH_COUNT\n};\n")
        header.write("static const unsigned short psp_glyph_cells[PSP_GLYPH_COUNT][4] =\n{\n")
        for name, x, y, cells in places:
            header.write("\t{ %d, %d, %d, %d }, /* %s */\n" % (x * CELL, y * CELL, cells * CELL, CELL, name))
        header.write("};\n")
    print("psp_glyphs: %d glyphs, sheet %dx%d" % (len(places), columns * CELL, rows * CELL))


if __name__ == "__main__":
    main()
