"""The loading screen's picture: the disc's maps\\loading.tga (the Xbox copies
it to its utility drive at start-up and draws it under a sweeping band while
a map loads: progress_bar.c) into halo/maps/loading.pic, for psp_ge.c's
loading frames. Made from the player's own disc, so the EBOOT carries none
of it (go-live 3.5).

loading.pic: 'LDP1', width, height (little-endian words), 256 palette
entries (the GE's ABGR words), then width x height palette indices, top row
first. 320 x 240 x 1 byte: 76 KB, which the PSP reads into the depth
buffer's VRAM at each load's start (nothing else uses it while a load runs).
"""
import struct

import numpy as np

from bitmap_bake import best_kmeans, nearest_centre

MAGIC = b'LDP1'


def read_tga(data):
    """an uncompressed true-colour TGA as rows of RGB, top row first; None
    for anything else (the engine reads only an uncompressed 24-bit picture)"""
    if len(data) < 18:
        return None
    id_length, colour_map, image_type = data[0], data[1], data[2]
    width, height, depth, descriptor = struct.unpack_from('<HHBB', data, 12)
    if colour_map or image_type != 2 or depth not in (24, 32) or not width or not height:
        return None
    start = 18 + id_length
    size = width * height * (depth // 8)
    if len(data) < start + size:
        return None
    pixels = np.frombuffer(data, np.uint8, size, start).reshape(height, width, depth // 8)
    rgb = pixels[:, :, 2::-1]  # BGR(A) -> RGB
    # bit 5 of the descriptor: rows stored top first; otherwise bottom first
    if not descriptor & 0x20:
        rgb = rgb[::-1]
    return np.ascontiguousarray(rgb)


def convert(data):
    """loading.tga's bytes -> loading.pic's bytes (None if it isn't a picture
    this reads)"""
    rgb = read_tga(data)
    if rgb is None:
        return None
    h, w = rgb.shape[:2]
    points = rgb.reshape(-1, 3).astype(np.float32)
    rng = np.random.default_rng(1)
    sample = points if len(points) <= 65536 else points[rng.choice(len(points), 65536, replace=False)]
    palette = best_kmeans(sample, 256, 60)
    index = nearest_centre(points, palette).astype(np.uint8)
    entries = np.zeros(256, np.uint32)
    p = np.clip(np.rint(palette), 0, 255).astype(np.uint32)
    entries[:len(p)] = 0xFF000000 | (p[:, 2] << 16) | (p[:, 1] << 8) | p[:, 0]
    return MAGIC + struct.pack('<II', w, h) + entries.astype('<u4').tobytes() + index.tobytes()


def write(tga_path, out_path):
    with open(tga_path, 'rb') as f:
        out = convert(f.read())
    if out is None:
        return False
    with open(out_path, 'wb') as f:
        f.write(out)
    return True
