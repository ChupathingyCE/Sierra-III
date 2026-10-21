#!/usr/bin/env python3
"""tagdeps.py - what a map's tags refer to, as a tree of tag names (build 113).

For each tag whose name contains the pattern, lists the tags it refers to
(group, name and the field that refers to it), and follows those of the
groups that lead to what is drawn or fired: a weapon to its first-person
model and its projectile, a model to its shaders, a projectile to its
attachments (contrails, lights, particle systems, effects).

    python3 tagdeps.py MAP PATTERN [--depth N] [--all] [--stages]

  PATTERN  part of a tag name, e.g. "plasma rifle" (case does not matter)
  --depth  how many steps to follow (default 3)
  --all    follow every group, not only those below
  --stages every see-through shader's combiner stages (always when the
           pattern matches four or fewer)

Prints names and groups only: no tag data leaves the machine. Uses the bake's
map reader (bake/lib/halomap.py) and Invader's tag definitions
(HALO_PSP_INVADER_DEFINITIONS, as the bake bundle ships them).

Shader groups, for reading the output: soso model, sotr transparent generic,
schi chicago, scex chicago extended, swat water, sgla glass, smet meter,
spla plasma (the PSP draws sotr, schi, swat and sgla see-through parts;
b113_candidates.md #16).

For each generic, chicago or water shader whose name contains the pattern,
the map the PSP draws of it and the tint the bake gives that map
(bake/lib/seethrough.py), beside the tint from the maps' means (the packs'
before the texel fit), how well each drawing model fits, and the combiner
stages of a generic shader whose tint changed: derived colours and register
wiring, no tag data."""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'bake', 'lib'))
# texpack.py sits beside this file; python -I leaves the script's own
# folder off the path, so it is put there by name
sys.path.insert(1, HERE)
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
    # model shaders (soso) whose name holds the pattern: how they are drawn
    # on the PC (the base map's scale, the detail map's scale, function and
    # mask, which maps are set), for checking what the PSP's one pass keeps.
    # Numbers and enum names only
    print_model_shaders(m, d, [t for t in m.tags if t.group == 'soso' and pattern in t.name.lower()])
    print_level_shaders(m, d, [t for t in m.tags if t.group == 'senv' and pattern in t.name.lower()])
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
    changed = 0
    for t in sorted((t for t in m.tags if t.name in shaders and t.group in ('sotr', 'schi', 'swat')), key=lambda t: t.name):
        found = seethrough.shader_tint(m, t, means)
        if found is None:
            print('tint for %s %s: none (numeric, no maps, or a map that cannot be read)' % (t.group, t.name))
            continue
        print('tint for %s %s: %s (map %d drawn%s; the shader averages %s; that map %s)' % (
            t.group, t.name, found.name(), found.drawn,
            '; the scale rule\'s map %d: %s' % (found.scale_drawn, found.alt.name()) if found.alt else '',
            ' '.join('%.2f' % v for v in found.result),
            ' '.join('%.2f' % v for v in found.mean)))
        # the tint as the packs had it before: every map at its mean, the
        # runtime's map (texpack.py --tints-from-means)
        before = seethrough.shader_tint(m, t, means, from_means=True)
        same = before is not None and before.word() == found.word() and before.flags() == found.flags() and \
            before.drawn == found.drawn
        changed += not same
        print('    from means: %s (map %d drawn)%s' % (before.name(), before.drawn, '' if same else '   CHANGED')
              if before is not None else '    from means: none')
        if found.errors is not None:
            print('    fit over the texels: error modulate %.5f, add %.5f, flat %.5f; each map\'s share of the output: %s' % (
                found.errors['modulate'], found.errors['add'], found.errors['flat'],
                ', '.join('map %d %.5f' % (j, v) for j, v in sorted(found.shares.items())) or 'none'))
        print_tint_inputs(m, t, means, stages=not same or len(shaders) <= 4 or '--stages' in sys.argv)
    print('tints that change from the means\' (texels, the drawn map, the model): %d' % changed)


def print_model_shaders(m, d, tags):
    struct = d.groups.get('shader_model')
    if not struct:
        print('model shaders: no definition for soso here')
        return
    fields = d.fields(struct)
    for t in sorted(tags, key=lambda t: t.name):
        o = m.off(t.address)
        if o is None:
            continue
        parts = []
        for off, f, sz in fields:
            name = f.get('name', '')
            ftype = f['type']
            if ftype == 'TagDependency':
                target = struct_unpack('<i', m.data, o + off + 12)
                parts.append('%s %s' % (name, 'set' if target != -1 else 'none'))
            elif ftype in ('float', 'Fraction') and any(w in name for w in ('map u scale', 'map v scale', 'detail map', 'translucency', 'brightness')):
                parts.append('%s %.4g' % (name, struct_unpack('<f', m.data, o + off)))
            elif ftype in d.enums and name.startswith('detail'):
                v = struct_unpack('<H', m.data, o + off)
                options = d.enums[ftype].get('options', [])
                parts.append('%s %s' % (name, options[v] if v < len(options) else v))
            elif ftype in d.bitfields and 'flags' in name:
                parts.append('%s 0x%x' % (name, struct_unpack('<H' if sz == 2 else '<I', m.data, o + off)))
        print('model shader %s: %s' % (t.name, '; '.join(parts)))


def print_level_shaders(m, d, tags):
    """level (senv) shaders whose name holds the pattern: their flags, base
    and bump maps' sizes, the bump map's scale, and for an alpha-tested one
    how the cut-out the bake makes from the bump map's alpha samples it: the
    bump map's texels per baked base texel (over 1: the shape is thinned to
    a sample of it) and the share drawn, at the bump's own size and as baked.
    Numbers only"""
    try:
        import numpy as np
        from texpack import bitmap_list, target, MAXIMUM_SIDE
        from bitmap_bake import decode_bump
        import refs
    except ImportError as e:
        print('level shaders: not worked out here (%s)' % e)
        return
    B = bitmap_list(m, d)
    first = {}
    for b in B:
        first.setdefault(b['tag'], b)
    roles = refs.bitmap_roles(m, d)
    for t in sorted(tags, key=lambda t: t.name):
        o = m.off(t.address)
        if o is None:
            continue
        flags, = struct.unpack_from('<H', m.data, o + 40)
        base = m.by_id.get(struct.unpack_from('<i', m.data, o + 148)[0])
        bump = m.by_id.get(struct.unpack_from('<i', m.data, o + 308)[0])
        bump_scale, = struct.unpack_from('<f', m.data, o + 292)
        su, sv = struct.unpack_from('<2f', m.data, o + 312)
        bb = first.get(base.name) if base else None
        pb = first.get(bump.name) if bump else None
        text = 'level shader %s: flags 0x%x' % (t.name, flags)
        if bb:
            act, tw, th = target(bb, refs.classify(roles.get(bb['tag'], ()), bb))
            while max(tw, th) > MAXIMUM_SIDE:
                tw, th = max(1, tw // 2), max(1, th // 2)
            text += '; base %s %dx%d %s mips %d, baked %dx%d' % (bb['tag'], bb['w'], bb['h'], bb['format'], bb['mips'], tw, th)
        if pb:
            text += '; bump %s %dx%d %s mips %d, scale %g, u %g v %g' % (pb['tag'], pb['w'], pb['h'], pb['format'],
                                                                     pb['mips'], bump_scale, su, sv)
        print(text)
        if not (bb and pb and flags & 1):
            continue
        img = decode_bump(m, pb)
        if img is None:
            print('    cut-out: the bump map cannot be read')
            continue
        u = su if su else (bump_scale if bump_scale else 1.0)
        v = sv if sv else (bump_scale if bump_scale else 1.0)
        a = img[:, :, 3] > 127
        bh, bw = a.shape
        xs = np.floor((np.arange(tw) + 0.5) / tw * u * bw).astype(np.int64) % bw
        ys = np.floor((np.arange(th) + 0.5) / th * v * bh).astype(np.int64) % bh
        cut = a[ys[:, None], xs[None, :]]
        # runs of drawn texels along rows: a wire mesh has long runs, beads short ones
        def runs(x):
            r = np.diff(np.concatenate([np.zeros((x.shape[0], 1), bool), x, np.zeros((x.shape[0], 1), bool)], 1).astype(np.int8), axis=1)
            starts, ends = np.where(r == 1), np.where(r == -1)
            n = len(starts[0])
            return (ends[1] - starts[1]).mean() if n else 0.0
        print('    cut-out: bump texels per baked base texel %.2f x %.2f; drawn %.3f at the bump\'s size, %.3f as baked; '
              'mean drawn run along a row %.1f bump texels, %.1f baked texels (%.1f bump texels long)' % (
                  u * bw / tw, v * bh / th, a.mean(), cut.mean(), runs(a), runs(cut), runs(cut) * u * bw / tw))
        # what the GE draws: the bake's cut-out at the base map's own size,
        # box-filtered to a baked size, magnified back bilinearly and tested
        # (alpha > 0x7F); its mismatch against the cut-out at full size, and
        # the lattice's period (the strongest repeat along a row and a column)
        from PIL import Image
        W, H = bb['w'], bb['h']
        xs = np.floor((np.arange(W) + 0.5) / W * u * bw).astype(np.int64) % bw
        ys = np.floor((np.arange(H) + 0.5) / H * v * bh).astype(np.int64) % bh
        full = img[ys[:, None], xs[None, :], 3]
        ref = full > 127

        def period(x, axis):
            f = np.abs(np.fft.rfft(x - x.mean(), axis=axis)).mean(1 - axis)
            k = int(np.argmax(f[1:])) + 1
            return x.shape[axis] / k

        sizes = []
        sw, sh = tw, th
        while sw <= min(512, W) and sh <= min(512, H):
            sizes.append((sw, sh))
            sw, sh = sw * 2, sh * 2
        parts = []
        for sw, sh in sizes:
            small = Image.fromarray(full).resize((sw, sh), Image.Resampling.BOX)
            back = np.array(small.resize((W, H), Image.Resampling.BILINEAR)) > 127
            parts.append('%dx%d: %.3f wrong, drawn %.3f' % (sw, sh, (back != ref).mean(), back.mean()))
        print('    as the GE draws it: lattice period %.1f x %.1f base texels; %s' % (
            period(ref.astype(np.float32), 1), period(ref.astype(np.float32), 0), '; '.join(parts)))


def struct_unpack(fmt, data, at):
    return struct.unpack_from(fmt, data, at)[0]


# the shader fields a tint depends on, by their tag enums' order
BLENDS = ('alpha blend', 'multiply', 'double multiply', 'add', 'subtract', 'min', 'max', 'alpha multiply add')
FADE_MODES = ('none', 'fade when perpendicular', 'fade when parallel')
FADE_SOURCES = ('none', 'A out', 'B out', 'C out', 'D out')


def print_tint_inputs(m, t, means, stages=False):
    """what a tint was worked out from: the framebuffer blend and fade, and
    each map's bitmap, scale and mean. A wrong tint shows up here as a map
    whose mean doesn't stand for what the shader draws (a cube map, a
    mostly-black mask) or a blend the mean can't model. stages: a generic
    shader's combiner stages too, as register wiring"""
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
        # the bitmap's kind: a bump or vector map read as colours averages
        # its normals, not what the combiners make of them
        b = means.first.get(target.name) if target is not None else None
        kind_text = ', %s %s %s %dx%d' % (b['usage'], b['type'], b['format'], b['w'], b['h']) if b else ''
        print('    map %d: %s, scale %g x %g, mean %s%s' % (
            i, target.name if target is not None else 'none', u, v,
            ' '.join('%.2f' % x for x in mean) if mean is not None else 'unreadable', kind_text))
        if b and b['type'] == 'cube':
            for line in cube_face_lines(m, b):
                print('      ' + line)
    if stages and kind == seethrough.GENERIC:
        for line in stage_lines(m, o):
            print('    ' + line)


def cube_face_lines(m, b):
    """a cube map's faces in stored order: each face's mean, and its top and
    bottom halves' (what a reflection sees depends on which faces and halves
    it reaches, not on the six faces' mean)"""
    import seethrough
    from bitmap_bake import BPP
    bpp = BPP[b['fmt']] if b['fmt'] < len(BPP) else 0
    face = seethrough._face_bytes(b, bpp) if bpp else 0
    for stride in (face, (face + 127) // 128 * 128):
        if stride and 6 * stride == b['pixels_size']:
            lines = []
            for k in range(6):
                img = seethrough.decode(m, dict(b, pixels_offset=b['pixels_offset'] + k * stride, pixels_size=stride))
                if img is None:
                    return ['face %d unreadable' % k]
                h = img.shape[0] // 2
                rgb = lambda x: ' '.join('%.2f' % v for v in x.reshape(-1, 4).mean(0)[:3] / 255.0)
                lines.append('face %d: mean %s; top half %s; bottom half %s' % (k, rgb(img), rgb(img[:h]), rgb(img[h:])))
            return lines
    return ['faces not placed (%d bytes)' % b['pixels_size']]


# the generic shader's combiner registers and mappings, for stage_lines
INPUTS = ('zero', 'one', 'half', '-1', '-half', 'T0', 'T1', 'T2', 'T3', 'V0', 'V1', 'R0', 'R1', 'C0', 'C1')
MAPPINGS = ('', '1-', '2x-1:', '1-2x:', 'x-.5:', '.5-x:', 's:', '-')
OUTPUTS = ('-', 'R0', 'R1', 'V0', 'V1', 'T0', 'T1', 'T2', 'T3')
OUTPUT_MAPPINGS = ('', ' x0.5', ' x2', ' x4', ' -0.5', ' (x-0.5)x2')


def stage_lines(m, o):
    """each combiner stage of a generic shader as one line: the colour half's
    AB, CD and AB+CD (or the mux) and where each goes, then the alpha half's"""
    import struct
    stages_n, stages_p = struct.unpack_from('<iI', m.data, o + 96)
    lines = []
    so = m.off(stages_p) if stages_n > 0 else None
    for s in range(min(stages_n, 7)):
        at = so + 112 * s
        flags, = struct.unpack_from('<H', m.data, at)
        c = struct.unpack_from('<14h', m.data, at + 60)
        a = struct.unpack_from('<12h', m.data, at + 88)

        def name(index, mapping, alpha):
            if index < 5:
                return INPUTS[index]
            index -= 5
            half = ('.a' if not alpha else '.b') if index >= 10 else ('.rgb' if not alpha else '.a')
            reg = INPUTS[5 + index % 10] if index < 20 else '?'
            return '%s%s%s' % (MAPPINGS[mapping & 7], reg, half)
        cin = [name(c[2 * k], c[2 * k + 1], False) for k in range(4)]
        ain = [name(a[2 * k], a[2 * k + 1], True) for k in range(4)]
        out = lambda i: OUTPUTS[i] if 0 <= i < len(OUTPUTS) else '?'
        omap = lambda i: OUTPUT_MAPPINGS[i] if 0 <= i < len(OUTPUT_MAPPINGS) else '?'
        lines.append('stage %d: colour %s%s%s -> %s, %s%s%s -> %s, %s -> %s%s; alpha %s*%s -> %s, %s*%s -> %s, %s -> %s%s' % (
            s, cin[0], '.' if c[9] == 1 else '*', cin[1], out(c[8]), cin[2], '.' if c[11] == 1 else '*', cin[3],
            out(c[10]), 'mux' if flags & 1 else 'sum', out(c[12]), omap(c[13]),
            ain[0], ain[1], out(a[8]), ain[2], ain[3], out(a[9]), 'mux' if flags & 2 else 'sum', out(a[10]),
            omap(a[11])))
    return lines


if __name__ == '__main__':
    main()
