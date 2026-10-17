#!/usr/bin/env python3
"""halo-psp-bake: your own copy of Halo (Xbox, NTSC, 01.10.12.2276) turned
into the map files the PSP build reads. One command:

    ./halo-psp-bake <your Halo disc image (.iso) or its maps folder> <output folder>

What it does, map by map (ten campaign levels, thirteen multiplayer maps and
the menus' ui.map):
  1. reads the map out of the disc image (xiso.py: no other tool needed)
  2. checks it is the retail map this build was made for (hashes.json)
  3. extracts its tags and rebuilds the map for the PSP's memory with the
     port's Invader (-X 0x09600000 -T 0x900000, or 0x600000 for the
     multiplayer maps and ui; model and level geometry and the animations'
     frame data to a .pgeo pack, animations keyframed, the script node table
     sized to the map, the BSPs' grass and the bytes the port never reads
     left out)
  4. bakes the textures for the PSP's graphics chip (.ptex), and the asset
     track's version (_track.ptex: half-size lightmaps, and alpha-tested
     plants that keep their leaves at a distance; --no-track skips it)
  5. bakes the level's wall decals into its walls (.pstn)
  6. with --eboot: cuts the XMB menu's sound from the music on your disc and
     writes a copy of the release EBOOT with it (lib/snd0.py)

The output folder then holds halo/maps/... (and, with --eboot,
PSP/GAME/halo/EBOOT.PBP): copy its contents to the root of the Memory Stick.

Options:
    --maps a10,c20,bloodgulch   only these maps (default: all found)
    --no-track                  skip the asset track's _track.ptex
    --keep-work                 keep the extracted tags (several GB)
    --force                     bake maps whose hashes don't match
    --detail fp,vehicles        also bake _detail.ptex with these classes at
                                full size (fp, weapons, vehicles, characters):
                                the launch menu's "assetdetail" uses them
    --sound s9                  the sound bake (build 116, streaming.md S9):
                                every sound at 22,050 Hz at most, effects mono,
                                music stereo, still Xbox ADPCM (lib/soundpack.py)
    --eboot PATH                the release's EBOOT.PBP: a copy with the menu
                                sound goes to <output>/PSP/GAME/<its folder>/
    --music [MAP:]TAG[@START][+LENGTH]
                                which clip (default MUSIC below; MAP: ui)
    --list-music                list the maps' music tags and lengths, and stop
    --tints-from-means          the see-through shaders' tints from every
                                map's mean, on the runtime's own map, as
                                the packs were before (lib/seethrough.py)

Nothing from the game is in this tool; it only converts the copy you own."""
import argparse, hashlib, json, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, 'lib')
INVADER = os.path.join(HERE, 'invader')
# options every texpack.py run takes (--tints-from-means)
TEXPACK_OPTIONS = []
ATRACDENC = os.path.join(HERE, 'atracdenc')    # the bundled encoder, if any (else atracdenc on PATH)
sys.path.insert(0, HERE)
from xiso import XIso

CAMPAIGN = ['a10', 'a30', 'a50', 'b30', 'b40', 'c10', 'c20', 'c40', 'd20', 'd40']
MULTIPLAYER = ['beavercreek', 'bloodgulch', 'boardingaction', 'carousel', 'chillout', 'damnation', 'hangemhigh',
               'longest', 'prisoner', 'putput', 'ratrace', 'sidewinder', 'wizard']
ALL = CAMPAIGN + MULTIPLAYER + ['ui']
# -M (build 106): animation frame data to the .pgeo pack, read through the
# game's animation cache; -Y 512: the script node table sized to the nodes in
# use plus 512 spare. Maps built with either need build 106 or later.
# -D (build 108): the BSPs' detail objects (grass) left out, which the port
# never draws (a30: 1.2 MiB); -Z (build 108): four blocks without the bytes
# the port never reads (sound permutations' and pitch ranges' names and
# subtitle data, AI squads' unused fields, firing positions' padding: ~0.5 MiB
# a campaign map, and what lets b40 fit). Maps built with -Z need build 108 or
# later; build 108 still loads older ones (psp_tag_layouts.c)
# The XMB menu's sound: [MAP:]TAG[@START][+LENGTH] (snd0.py). Not chosen yet;
# `--list-music` shows the candidates.
MUSIC = ''
BUILD_FLAGS = ['-g', 'xbox-ntsc', '-X', '0x09600000', '-U', '-l', '0', '-H', '-K', '0.25,0.003',
               '-M', '-Y', '512', '-D', '-Z']
# the tag window a map is built for (-T; psp_capacity.h's PSP_TAG_CACHE_SIZE):
# the campaign build's 9 MB, and the multiplayer build's 6 MB for the
# multiplayer maps and ui (they take 2.3-3.3 MiB of tag space as baked). A
# structure BSP loads at the end of the window it was built for, so a map
# built for 6 MB loads in both builds
TAG_SPACE = '0x900000'
MULTIPLAYER_TAG_SPACE = '0x600000'


def log(text):
    print(time.strftime('%H:%M:%S ') + text, flush=True)


def invader(tool):
    path = os.path.join(INVADER, 'bin', tool)
    if not os.path.exists(path):
        sys.exit('halo-psp-bake: %s is missing (the bundle\'s invader/bin)' % path)
    return path


def run(command, log_path, cwd=None, env=None):
    with open(log_path, 'a') as out:
        out.write('$ ' + ' '.join(command) + '\n')
        out.flush()
        r = subprocess.run(command, cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT)
    if r.returncode:
        tail = open(log_path, errors='replace').read()[-1500:]
        raise RuntimeError('%s failed (%d); the end of %s:\n%s' % (os.path.basename(command[0]), r.returncode, log_path, tail))


def echo_from(log_path, start, markers):
    """the lines a step added to a map's own log (from byte start) that
    hold one of markers, into the bake's log (build 116: the texture pack's
    see-through tints and meter maps; names and colours only)"""
    with open(log_path, errors='replace') as f:
        f.seek(start)
        for line in f.read().splitlines():
            if any(marker in line for marker in markers):
                log('  ' + line.strip())


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def gather(source, work, wanted):
    """the retail maps into work/retail/<name>.map, from a disc image or a folder"""
    os.makedirs(os.path.join(work, 'retail'), exist_ok=True)
    found = {}
    if os.path.isdir(source):
        for name in wanted:
            for candidate in (os.path.join(source, name + '.map'), os.path.join(source, 'maps', name + '.map')):
                if os.path.exists(candidate):
                    found[name] = candidate
                    break
    else:
        with XIso(source) as iso:
            listing = {p.lower(): p for p, n in iso.files()}
            for name in wanted:
                key = 'maps/%s.map' % name
                if key in listing:
                    destination = os.path.join(work, 'retail', name + '.map')
                    if not os.path.exists(destination):
                        log('reading %s from the disc image' % listing[key])
                        iso.extract(listing[key], destination)
                    found[name] = destination
    return found


def scenario_path(map_path, log_path):
    r = subprocess.run([invader('invader-info'), '-T', 'scenario_path', map_path], capture_output=True, text=True,
                       env=tool_env())
    path = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ''
    if r.returncode or not path:
        raise RuntimeError('invader-info could not read the scenario of %s: %s' % (map_path, r.stderr[-500:]))
    return path


def tool_env():
    env = dict(os.environ)
    env['LD_LIBRARY_PATH'] = os.path.join(INVADER, 'lib') + ':' + os.path.join(ATRACDENC, 'lib') + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
    env['HALO_PSP_INVADER_DEFINITIONS'] = os.path.join(INVADER, 'definitions')
    env['PYTHONPATH'] = LIB + (':' + env['PYTHONPATH'] if env.get('PYTHONPATH') else '')
    return env


def bake_map(name, retail, out_maps, work, track, keep_work, detail='', sound=''):
    started = time.time()
    folder = os.path.join(work, name)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(os.path.join(folder, 'maps_in'))
    os.makedirs(os.path.join(folder, 'tags'))
    log_path = os.path.join(work, name + '.log')
    open(log_path, 'w').close()
    env = tool_env()
    shutil.copyfile(retail, os.path.join(folder, 'maps_in', name + '.map'))
    log('%s: extracting its tags' % name)
    # -n: a campaign map's (and the menus') own globals tag too
    run([invader('invader-extract'), '-m', os.path.join(folder, 'maps_in'), '-t', os.path.join(folder, 'tags'), '-G']
        + ([] if name in MULTIPLAYER else ['-n']) + [os.path.join(folder, 'maps_in', name + '.map')], log_path, env=env)
    scenario = scenario_path(os.path.join(folder, 'maps_in', name + '.map'), log_path)
    if sound:
        log('%s: sounds re-encoded (%s)' % (name, sound))
        run([sys.executable, os.path.join(LIB, 'soundpack.py'), os.path.join(folder, 'tags'), sound], log_path, env=env)
        summary = [line for line in open(log_path, errors='replace') if line.startswith('soundpack ')]
        if summary:
            log('%s: %s' % (name, summary[-1].strip()))
    # a multiplayer map keeps the disc map's tag order (invader-build -w):
    # the netcode names objects' types by tag handle, so a PSP on a PC host
    # needs every tag where the host's map has it (docs/multiplayer_plan.md,
    # section 3.2; tools/tagorder.py checks a bake)
    order = []
    if name in MULTIPLAYER:
        index = os.path.join(folder, name + '_index.txt')
        run([sys.executable, os.path.join(LIB, 'tagindex.py'), os.path.join(folder, 'maps_in', name + '.map'), index],
            log_path, env=env)
        order = ['-w', index]
    log('%s: building %s for the PSP' % (name, scenario))
    built = os.path.join(folder, name + '_z.map')
    pgeo = os.path.join(out_maps, name + '.pgeo')
    space = MULTIPLAYER_TAG_SPACE if name in MULTIPLAYER or name == 'ui' else TAG_SPACE
    run([invader('invader-build')] + BUILD_FLAGS + ['-T', space] + order +
        ['-t', os.path.join(folder, 'tags'), '-G', pgeo, '-o', built, scenario], log_path, env=env)
    # the tag window's use (Invader's own count: the tags and the largest BSP),
    # into the bake's log: the window is what a map must fit
    for line in open(log_path, errors='replace'):
        if line.startswith(('Tag space:', 'BSPs:')):
            log('%s: %s' % (name, ' '.join(line.split())))
    final = os.path.join(out_maps, name + '.map')
    run([sys.executable, os.path.join(LIB, 'dez.py'), built, final], log_path, env=env)
    os.remove(built)
    log('%s: textures' % name)
    start = os.path.getsize(log_path)
    run([sys.executable, os.path.join(LIB, 'texpack.py'), final, os.path.join(out_maps, name + '.ptex')] + TEXPACK_OPTIONS,
        log_path, cwd=LIB, env=env)
    echo_from(log_path, start, ('tint ', 'see-through shaders tinted', 'meter shader maps', 'HUD sheets', 'glow ', 'detail mask', 'quality '))
    # the main menu is the hardware's showcase (CLAUDE.md): a pack of its own
    # at the most the GE can take, beside the usual one ("noshowcase")
    if name == 'ui':
        log('%s: textures, the showcase' % name)
        start = os.path.getsize(log_path)
        run([sys.executable, os.path.join(LIB, 'texpack.py'), final, os.path.join(out_maps, name + '_showcase.ptex'),
             '--showcase'] + TEXPACK_OPTIONS, log_path, cwd=LIB, env=env)
        echo_from(log_path, start, ('glow maps', 'showcase:'))
        # the same sizes on palettes, for the comparison ("showcasepal")
        log('%s: textures, the showcase on palettes' % name)
        start = os.path.getsize(log_path)
        run([sys.executable, os.path.join(LIB, 'texpack.py'), final, os.path.join(out_maps, name + '_showcase_pal.ptex'),
             '--showcase', '--showcase-palettes'] + TEXPACK_OPTIONS, log_path, cwd=LIB, env=env)
        echo_from(log_path, start, ('textures,',))
    if track and name != 'ui':
        log('%s: textures, asset track (half-size lightmaps, plants kept whole at a distance)' % name)
        run([sys.executable, os.path.join(LIB, 'texpack.py'), final, os.path.join(out_maps, name + '_track.ptex'),
             '--lightmap-scale', '2', '--alpha-coverage', '--pgeo', pgeo] + TEXPACK_OPTIONS, log_path, cwd=LIB, env=env)
    if detail and name != 'ui':
        log('%s: textures at full size for %s' % (name, detail))
        run([sys.executable, os.path.join(LIB, 'texpack.py'), final, os.path.join(out_maps, name + '_detail.ptex'),
             '--full-detail', detail] + TEXPACK_OPTIONS, log_path, cwd=LIB, env=env)
    if name != 'ui':
        log('%s: wall decals' % name)
        run([sys.executable, os.path.join(LIB, 'stencilbake.py'), final, pgeo, os.path.join(out_maps, name + '.pstn')],
            log_path, cwd=LIB, env=env)
    if not keep_work:
        shutil.rmtree(folder, ignore_errors=True)
    log('%s: done in %.0f s' % (name, time.time() - started))


def atracdenc():
    bundled = os.path.join(ATRACDENC, 'bin', 'atracdenc')
    return bundled if os.path.exists(bundled) else shutil.which('atracdenc')


def music_spec(spec):
    """[MAP:]TAG[@START][+LENGTH] -> (map, the rest)"""
    head, colon, rest = spec.partition(':')
    return (head, rest) if colon and head in ALL else ('ui', spec)


def menu_eboot(eboot, spec, found, output, work):
    """Best-effort: the release EBOOT, with the clip as its SND0 when it can be
    made, goes to output/PSP/GAME/<halo or halomp>/. Sound is never why the
    bake fails: without it, the EBOOT is copied as it is."""
    folder = os.path.basename(os.path.dirname(os.path.abspath(eboot)))
    folder = folder if folder in ('halo', 'halomp') else 'halo'
    out_dir = os.path.join(output, 'PSP', 'GAME', folder)
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, 'EBOOT.PBP')
    source_map, clip = music_spec(spec) if spec else (None, None)
    encoder = atracdenc()
    why = ('no clip chosen (--music)' if not spec else 'atracdenc not found' if not encoder else
           '%s.map not found' % source_map if source_map not in found else None)
    if why is None:
        log_path = os.path.join(work, 'snd0.log')
        try:
            log('menu sound: %s from %s.map' % (clip, source_map))
            run([sys.executable, os.path.join(LIB, 'snd0.py'), 'make', found[source_map], clip,
                 os.path.join(work, 'SND0.AT3'), '--atracdenc', encoder, '--eboot', eboot, '--eboot-out', out],
                log_path, env=tool_env())
            log('menu sound: %s' % out)
            return
        except Exception as e:
            why = str(e)
    shutil.copyfile(eboot, out)
    log('menu sound skipped (%s); %s is the release EBOOT as it is' % (why, out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source', help='your Halo disc image, or a folder holding its maps')
    ap.add_argument('output', help='where the halo/maps folder is written')
    ap.add_argument('--maps', default='', help='comma-separated map names (default: all)')
    ap.add_argument('--no-track', action='store_true')
    ap.add_argument('--keep-work', action='store_true')
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--detail', default='')
    ap.add_argument('--sound', default='', choices=['', 's9', 's10'])
    ap.add_argument('--eboot', default='')
    ap.add_argument('--music', default=MUSIC)
    ap.add_argument('--list-music', action='store_true')
    ap.add_argument('--tints-from-means', action='store_true')
    a = ap.parse_args()
    if a.tints_from_means:
        TEXPACK_OPTIONS.append('--tints-from-means')
    wanted = [m.strip() for m in a.maps.split(',') if m.strip()] or ALL
    unknown = [m for m in wanted if m not in ALL]
    if unknown:
        sys.exit('halo-psp-bake: unknown map(s) %s; the maps are %s' % (', '.join(unknown), ', '.join(ALL)))
    out_maps = os.path.join(a.output, 'halo', 'maps')
    work = os.path.join(a.output, 'work')
    os.makedirs(out_maps, exist_ok=True)
    if a.eboot and not os.path.isfile(a.eboot):
        sys.exit('halo-psp-bake: --eboot %s: no such file' % a.eboot)
    music_map = music_spec(a.music)[0] if a.eboot and a.music else None
    found = gather(a.source, work, wanted + ([music_map] if music_map and music_map not in wanted else []))
    if a.list_music:
        r = subprocess.run([sys.executable, os.path.join(LIB, 'snd0.py'), 'list'] + [found[m] for m in wanted if m in found],
                           env=tool_env())
        if not a.keep_work:
            shutil.rmtree(os.path.join(work, 'retail'), ignore_errors=True)
        sys.exit(r.returncode)
    music_source = {music_map: found[music_map]} if music_map in found else {}
    found = {m: found[m] for m in found if m in wanted}
    missing = [m for m in wanted if m not in found]
    if missing:
        log('not found in %s: %s' % (a.source, ', '.join(missing)))
    hashes = json.load(open(os.path.join(HERE, 'hashes.json')))
    for name in list(found):
        digest = sha256(found[name])
        if hashes.get(name) and hashes[name] != digest:
            log('%s: not the retail map this build expects (sha256 %s...)%s' % (name, digest[:16],
                '; baking anyway (--force)' if a.force else '; skipped (--force bakes it anyway)'))
            if not a.force:
                del found[name]
    failures = []
    for name in wanted:
        if name not in found:
            continue
        try:
            bake_map(name, found[name], out_maps, work, not a.no_track, a.keep_work, a.detail, a.sound)
        except Exception as e:
            failures.append(name)
            log('%s: FAILED: %s' % (name, e))
    if a.eboot:
        menu_eboot(a.eboot, a.music, music_source, a.output, work)
    if not a.keep_work:
        shutil.rmtree(os.path.join(work, 'retail'), ignore_errors=True)
    log('baked %d of %d maps into %s%s' % (len(found) - len(failures), len(wanted), out_maps,
        '; failed: ' + ', '.join(failures) if failures else ''))
    log('copy %s to the root of the Memory Stick (it holds maps/)' % os.path.join(a.output, 'halo')
        if not a.eboot else 'copy the contents of %s (halo/ and PSP/) to the root of the Memory Stick' % a.output)
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
