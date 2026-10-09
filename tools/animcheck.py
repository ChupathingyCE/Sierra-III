"""animcheck.py - every compressed animation of a PSP-built map, checked the
way the engine reads it (model_animations.c): the block's 11 offsets within
it, each flagged node's keyframe range within the index and keyframe arrays,
its last keyframe the animation's last frame, the default rotations and
translations within the block. Both kinds are checked: Bungie's own
compressed animations (the cinematics') and the bake's keyframed ones
(invader-build -K), whether in the tags or in the .pgeo pack (-M).

Build 107: written while finding a10's crash, which turned out to be the
animation cache's (psp_anim.c), not the data: a10's 628 pass.

usage: python3 tools/animcheck.py MAP PGEO"""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'bake', 'lib'))  # the bake's readers (halomap.py, ...)
from halomap import Defs, CacheMap

PACK_BASE = 0x0C000000
HEADER = 64
path, pgeo = sys.argv[1], sys.argv[2]
d = Defs()
m = CacheMap(path)
pack = open(pgeo, 'rb').read()
af = {f.get('name'): x for x, f, sz in d.fields('ModelAnimationsAnimation')}
asz = d.structs['ModelAnimationsAnimation']['size']


def flagged(words, nodes):
    return [n for n in range(nodes) if (words[n >> 5] >> (n & 31)) & 1]


def check(h, avail, fc, nodes, rot, trn, scl):
    if avail < 44:
        return 'block of %d bytes' % avail
    o = struct.unpack_from('<11i', h, 0)
    for k, v in enumerate(o):
        if v < 0 or v > avail:
            return 'offset %d = %d past the block (%d)' % (k, v, avail)
    kinds = (('rotation', 44, o[0], o[2], 6, o[1], 6, rot),
             ('translation', o[3], o[4], o[6], 12, o[5], 12, trn),
             ('scale', o[7], o[8], o[10], 4, o[9], 4, scl))
    for name, headers, indices, keys, key_size, defaults, default_size, nodes_flagged in kinds:
        # (a compressed animation's unflagged scales are 1: the engine reads
        # no default scales, and Bungie's own blocks have none)
        if name != 'scale' and defaults + default_size * nodes > avail:
            return '%s defaults past the block' % name
        if headers + 4 * len(nodes_flagged) > avail:
            return '%s node headers past the block' % name
        for i, n in enumerate(nodes_flagged):
            nh, = struct.unpack_from('<I', h, headers + 4 * i)
            first, count = nh >> 12, nh & 0xfff
            if not count:
                continue
            if indices + 2 * (first + count) > avail or keys + key_size * (first + count) > avail:
                return '%s node %d: keyframes %d..%d past the block' % (name, n, first, first + count)
            last, = struct.unpack_from('<H', h, indices + 2 * (first + count - 1))
            if last != fc - 1:
                return '%s node %d: last keyframe %d, frames %d' % (name, n, last, fc)
    return None


bad = total = compressed = 0
for t in m.tags:
    if t.group != 'antr' or not t.address:
        continue
    o = m.off(t.address)
    an, ap = struct.unpack_from('<iI', m.data, o + 0x74)
    for ai in range(an):
        a = m.off(ap) + asz * ai
        total += 1
        name = m.data[a:a + 32].split(b'\0')[0].decode('latin1')
        fc, = struct.unpack_from('<H', m.data, a + af['frame count'])
        nodes, = struct.unpack_from('<H', m.data, a + af['node count'])
        flags, = struct.unpack_from('<H', m.data, a + af['flags'])
        if not flags & 1:
            continue
        compressed += 1
        coff, = struct.unpack_from('<I', m.data, a + af['offset to compressed data'])
        size, ext, foff, addr = struct.unpack_from('<IIII', m.data, a + af['frame data'])
        rot = flagged(struct.unpack_from('<II', m.data, a + af['node rotation flag data']), nodes)
        trn = flagged(struct.unpack_from('<II', m.data, a + af['node transform flag data']), nodes)
        scl = flagged(struct.unpack_from('<II', m.data, a + af['node scale flag data']), nodes)
        if addr >= PACK_BASE:
            blob = pack[HEADER + addr - PACK_BASE:HEADER + addr - PACK_BASE + size]
        else:
            blob = m.data[m.off(addr):m.off(addr) + size]
        problem = check(blob[coff:], size - coff, fc, nodes, rot, trn, scl)
        if problem:
            bad += 1
            print('%s / %s (%d frames, %d nodes, %d bytes at %08x): %s' % (t.name, name, fc, nodes, size, addr, problem))
print('%d animations, %d compressed, %d with problems' % (total, compressed, bad))
