#!/usr/bin/env python3
"""zerobitmaps.py - zero a PSP map's bitmap pixel data, in place (build 93).

On the PSP every texture comes from the map's texture pack (<map>.ptex);
the game's texture cache hands back a stand-in and never reads the map's
own pixels (psp_fixups.py, xbox_texture_cache.c). Those pixels are 30-45% of
a map file. Zeroed, the map keeps its layout and size, and compresses to
almost nothing for the download. Bake the .ptex FIRST: texpack.py reads the
pixels.

usage: python3 zerobitmaps.py map.map   (a decompressed map, rewritten in place)
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), 'research', 'baker'))
sys.path.insert(0, '/home/claude/research/baker')

from census import Defs  # noqa: E402
from halomap import CacheMap  # noqa: E402
import texpack  # noqa: E402


def main():
    path = sys.argv[1]
    m = CacheMap(path)
    d = Defs()
    size = os.path.getsize(path)
    tag_start = m.tag_data_offset
    tag_end = tag_start + m.tag_data_size
    ranges = []
    for b in texpack.bitmap_list(m, d):
        start, length = b['pixels_offset'], b['pixels_size']
        if length <= 0:
            continue
        if start < 2048 or start + length > size:
            sys.exit('%s: pixels outside the file (%d + %d)' % (b['tag'], start, length))
        if start < tag_end and start + length > tag_start:
            sys.exit('%s: pixels inside the tag data' % b['tag'])
        ranges.append((start, length))
    ranges.sort()
    total = 0
    with open(path, 'r+b') as f:
        for start, length in ranges:
            f.seek(start)
            f.write(b'\0' * length)
            total += length
    print('%s: %d bitmaps, %.1f MB of pixels zeroed (of %.1f MB)' % (path, len(ranges), total / 1e6, size / 1e6))


if __name__ == '__main__':
    main()
