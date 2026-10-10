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
b113_candidates.md #16)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bake', 'lib'))
from halomap import Defs, CacheMap, Walker, FOURCC_GROUP  # noqa: E402

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


if __name__ == '__main__':
    main()
