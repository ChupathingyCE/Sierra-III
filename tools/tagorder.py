#!/usr/bin/env python3
"""tagorder.py - does a baked map keep its source map's tag order?

The netcode names an object's type by its tag's datum handle (index and
salt: network_objects.c's definition_index), so a PSP playing on a PC host
needs every tag at the handle the host's map gives it
(docs/multiplayer_plan.md, section 3.2). The bake rebuilds each map with
Invader, which orders tags its own way unless it's given an index (-w).

usage: tagorder.py <source> <baked map>
  <source>  the disc's map file (retail: compressed or not), or
            <disc image>:<map name> to read it straight from the image

Prints counts only, no tag names (results may leave the machine; the names
are the game's). Exit status 0 when every tag of the source sits at the same
handle in the baked map, 1 when not."""
import os, struct, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'bake', 'lib'))
sys.path.insert(0, os.path.join(HERE, '..', 'bake'))
from halomap import CacheMap  # noqa: E402

# the groups of the game's objects: what the host creates and names by tag
# on every machine (bipeds, vehicles, weapons, equipment) and the rest of
# the object tree
OBJECT_GROUPS = {'bipd', 'vehi', 'weap', 'eqip', 'garb', 'proj', 'scen', 'mach', 'ctrl', 'lifi', 'plac', 'ssce',
                 'obje', 'unit', 'item', 'devi'}


def decompressed(path, folder):
    """the map as a file CacheMap reads: a retail map's zlib body inflated"""
    data = open(path, 'rb').read()
    length, = struct.unpack_from('<I', data, 8)
    if len(data) == length:
        return path
    out = os.path.join(folder, 'source.map')
    with open(out, 'wb') as f:
        f.write(data[:2048] + zlib.decompressobj().decompress(data[2048:]))
    return out


def source_file(spec, folder):
    """the source map as a file in folder, from a path or <image>:<name>"""
    if os.path.isfile(spec):
        return decompressed(spec, folder)
    image, _, name = spec.rpartition(':')
    from xiso import XIso
    with XIso(image) as iso:
        listing = {p.lower(): p for p, _ in iso.files()}
        key = 'maps/%s.map' % name.lower()
        if key not in listing:
            sys.exit('tagorder: %s has no %s' % (image, key))
        out = os.path.join(folder, 'retail.map')
        iso.extract(listing[key], out)
    return decompressed(out, folder)


def compare(source, baked):
    key = lambda t: (t.group, t.name.lower())
    baked_by_key = {key(t): t for t in baked.tags}
    same_handle = same_index = moved = missing = 0
    objects = objects_same = 0
    first_difference = None
    # the tags at the same index with another salt: their groups, and how far
    # each salt is from the source's (a group's four letters and a number say
    # nothing of the game's content)
    salt_groups, salt_steps = {}, {}
    for t in source.tags:
        b = baked_by_key.get(key(t))
        is_object = t.group in OBJECT_GROUPS
        objects += is_object
        if b is None:
            missing += 1
        elif b.tag_id == t.tag_id:
            same_handle += 1
            objects_same += is_object
        elif b.index == t.index:
            same_index += 1
            salt_groups[t.group] = salt_groups.get(t.group, 0) + 1
            step = ((t.tag_id >> 16) - (b.tag_id >> 16)) & 0xFFFF
            salt_steps[step] = salt_steps.get(step, 0) + 1
        else:
            moved += 1
        if first_difference is None and (b is None or b.tag_id != t.tag_id):
            first_difference = t.index
    source_keys = {key(t) for t in source.tags}
    extra = sum(1 for t in baked.tags if key(t) not in source_keys)
    print('source: %d tags (version %d, build %s); baked: %d tags' % (len(source.tags), source.version, source.build,
                                                                     len(baked.tags)))
    print('same handle (index and salt): %d; same index, other salt: %d; at another index: %d; '
          'missing from the bake: %d; only in the bake: %d' % (same_handle, same_index, moved, missing, extra))
    print('object tags at the same handle: %d of %d' % (objects_same, objects))
    if salt_groups:
        print('other salt, by group: %s' % ', '.join('%s %d' % kv for kv in sorted(salt_groups.items())))
        print('other salt, source salt less the baked one: %s' %
              ', '.join('%#06x (%d tags)' % kv for kv in sorted(salt_steps.items())))
    if first_difference is None:
        print('tag order: the same')
        return True
    print('tag order: differs, first at index %d' % first_difference)
    return False


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with tempfile.TemporaryDirectory() as folder:
        source = CacheMap(source_file(sys.argv[1], folder))
        baked = CacheMap(sys.argv[2])
        sys.exit(0 if compare(source, baked) else 1)


if __name__ == '__main__':
    main()
