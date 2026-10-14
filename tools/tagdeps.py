#!/usr/bin/env python3
"""tagdeps.py - what a map's tags refer to, as a tree of tag names (build 113).

For each tag whose name contains the pattern, lists the tags it refers to
(group, name and the field that refers to it), and follows those of the
groups that lead to what is drawn or fired: a weapon to its first-person
model and its projectile, a model to its shaders, a projectile to its
attachments (contrails, lights, particle systems, effects).

    python3 tagdeps.py MAP PATTERN [--depth N] [--all]

  PATTERN  part of a tag name, e.g. "plasma rifle" (case does not matter)
  --depth  how many steps to follow (default 3)
  --all    follow every group, not only those below

Prints names and groups only: no tag data leaves the machine. Uses the bake's
map reader (bake/lib/halomap.py) and Invader's tag definitions
(HALO_PSP_INVADER_DEFINITIONS, as the bake bundle ships them).

Shader groups, for reading the output: soso model, sotr transparent generic,
schi chicago, scex chicago extended, swat water, sgla glass, smet meter,
spla plasma (the PSP draws sotr, schi, swat and sgla see-through parts;
b113_candidates.md #16).

Build 116: for each generic, chicago or water shader whose name contains the
pattern, the tint the bake gives the one map the PSP draws of it
(bake/lib/seethrough.py): derived colours, no tag data."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bake', 'lib'))
from halomap import Defs, CacheMap, Walker, FOURCC_GROUP  # noqa: E402

# the shader tag groups, in the order the summary prints them (build 116)
SHADER_GROUPS = ('soso', 'senv', 'sotr', 'schi', 'scex', 'sgla', 'swat', 'smet', 'spla')
FOLLOW = {
    'weap', 'proj', 'mod2', 'mode', 'cont', 'ligh', 'lens', 'pctl', 'part', 'effe', 'eqip', 'vehi', 'bipd',
    'garb', 'soso', 'sotr', 'schi', 'scex', 'smet', 'spla', 'sgla', 'swat', 'item', 'obje', 'unit',
}
FALLBACK = {'Soul': 'TagCollection'}


def deps_of(m, d, walker, tag):
    """[(field path, group, tag)] for every dependency inside the tag"""
    sname = d.groups.get(FOURCC_GROUP.get(tag.group)) or FALLBACK.get(tag.group)
    if not sname or not tag.address or not m.in_tags(tag.address):
        return []
    found = []

    def cb(kind, path, tid, n, group, count):
        if kind == 'dep':
            target = m.by_id.get(tid)
            if target is not None:
                found.append((path, group, target))
    walker.walk(sname, m.off(tag.address), 1, '', cb)
    return found


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 2:
        sys.exit(__doc__)
    depth = 3
    for a in sys.argv[1:]:
        if a.startswith('--depth='):
            depth = int(a.split('=', 1)[1])
    follow_all = '--all' in sys.argv
    m = CacheMap(args[0])
    d = Defs()
    walker = Walker(d, m.data, lambda p: m.off(p) if m.in_tags(p) else None)
    pattern = args[1].lower()
    roots = [t for t in m.tags if pattern in t.name.lower()]
    print('map %s: %d tags match "%s"' % (m.name, len(roots), args[1]))
    shown = set()

    def show(tag, level, field):
        indent = '  ' * level
        again = tag.index in shown
        print('%s%s %s%s%s' % (indent, tag.group, tag.name, '   <- ' + field if field else '',
                               '   (above)' if again else ''))
        if again or level >= depth:
            return
        shown.add(tag.index)
        for path, group, target in deps_of(m, d, walker, tag):
            if follow_all or group in FOLLOW:
                show(target, level + 1, path)
            else:
                print('%s  %s %s   <- %s' % (indent, group, target.name, path))

    for tag in roots:
        show(tag, 0, '')
        print()
    # every meter and plasma shader in the map, whoever uses it
    for group in ('smet', 'spla'):
        names = sorted(t.name for t in m.tags if t.group == group)
        print('%s shaders in the map (%d): %s' % (group, len(names), '; '.join(names) or 'none'))
    # build 116: how many shaders of each kind the map has, by name for the
    # kinds the PSP draws least of (glass, water, chicago extended), and the
    # object models that use level (environment) shaders, which b115 drew
    # in a flat colour (hardware_b115.md)
    counts = {}
    for t in m.tags:
        if t.group in SHADER_GROUPS:
            counts[t.group] = counts.get(t.group, 0) + 1
    print('shader kinds in the map: %s' % ', '.join('%s %d' % (g, counts.get(g, 0)) for g in SHADER_GROUPS))
    for group in ('sgla', 'swat', 'scex'):
        names = sorted(t.name for t in m.tags if t.group == group)
        print('%s shaders in the map (%d): %s' % (group, len(names), '; '.join(names) or 'none'))
    users = []
    for t in m.tags:
        if t.group in ('mode', 'mod2'):
            senv = sorted({target.name for path, group, target in deps_of(m, d, walker, t) if group == 'senv'})
            if senv:
                users.append('%s (%s)' % (t.name, ', '.join(senv)))
    print('object models with environment shaders (%d): %s' % (len(users), '; '.join(sorted(users)) or 'none'))
    # build 116: the see-through tints (texpack.py writes them into the pack).
    # They decode bitmaps, which needs the bake's environment (numpy, Pillow):
    # without it, said and skipped
    try:
        import seethrough
        from texpack import bitmap_list
    except ImportError as e:
        print('tints: not worked out here (%s; the bake\'s environment has it)' % e)
        return
    shaders = {t.name for t in m.tags if t.group in ('sotr', 'schi', 'swat') and pattern in t.name.lower()}
    means = seethrough.Means(m, bitmap_list(m, d))
    for t in sorted((t for t in m.tags if t.name in shaders and t.group in ('sotr', 'schi', 'swat')), key=lambda t: t.name):
        found = seethrough.shader_tint(m, t, means)
        if found is None:
            print('tint for %s %s: none (numeric, no maps, or a map that cannot be read)' % (t.group, t.name))
            continue
        tint, drawn, result, mean, doubled = found
        print('tint for %s %s: %08x%s (map %d drawn; the shader averages %s; that map %s)' % (
            t.group, t.name, seethrough.abgr(tint), ' doubled' if doubled else '', drawn,
            ' '.join('%.2f' % v for v in result),
            ' '.join('%.2f' % v for v in mean)))
        print_tint_inputs(m, t, means)


# the shader fields a tint depends on, by their tag enums' order
BLENDS = ('alpha blend', 'multiply', 'double multiply', 'add', 'subtract', 'min', 'max', 'alpha multiply add')
FADE_MODES = ('none', 'fade when perpendicular', 'fade when parallel')
FADE_SOURCES = ('none', 'A out', 'B out', 'C out', 'D out')


def print_tint_inputs(m, t, means):
    """what a tint was worked out from: the framebuffer blend and fade, and
    each map's bitmap, scale and mean. A wrong tint shows up here as a map
    whose mean doesn't stand for what the shader draws (a cube map, a
    mostly-black mask) or a blend the mean can't model"""
    import struct
    import seethrough
    o = m.off(t.address)
    kind, = struct.unpack_from('<H', m.data, o + 36)
    if kind not in (seethrough.GENERIC, seethrough.CHICAGO):
        return
    first_map_type, blend, fade_mode, fade_source = struct.unpack_from('<hhhh', m.data, o + 42)
    count, maps_p = struct.unpack_from('<iI', m.data, o + 84)
    stages, = struct.unpack_from('<i', m.data, o + 96) if kind == seethrough.GENERIC else (0,)
    name = lambda table, i: table[i] if 0 <= i < len(table) else str(i)
    print('    blend %s; fade %s by %s; first map type %d; %d maps%s' % (
        name(BLENDS, blend), name(FADE_MODES, fade_mode), name(FADE_SOURCES, fade_source),
        first_map_type, count, '; %d stages' % stages if kind == seethrough.GENERIC else ''))
    stride, scale_at, map_at = (220, 84, 120) if kind == seethrough.CHICAGO else (100, 4, 40)
    mo = m.off(maps_p)
    for i in range(count):
        tid, = struct.unpack_from('<i', m.data, mo + stride * i + map_at)
        u, v = struct.unpack_from('<ff', m.data, mo + stride * i + scale_at)
        target = m.by_id.get(tid)
        mean = means(tid)
        print('    map %d: %s, scale %g x %g, mean %s' % (
            i, target.name if target is not None else 'none', u, v,
            ' '.join('%.2f' % x for x in mean) if mean is not None else 'unreadable'))


if __name__ == '__main__':
    main()
