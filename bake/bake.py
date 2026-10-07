#!/usr/bin/env python3
"""halo-psp-bake: your own copy of Halo (Xbox, NTSC, 01.10.12.2276) turned
into the map files the PSP build reads. One command:

    ./halo-psp-bake <your Halo disc image (.iso) or its maps folder> <output folder>

What it does, map by map (ten campaign levels, thirteen multiplayer maps and
the menus' ui.map):
  1. reads the map out of the disc image (xiso.py: no other tool needed)
  2. checks it is the retail map this build was made for (hashes.json)
  3. extracts its tags and rebuilds the map for the PSP's memory with the
     port's Invader (-X 0x09600000 -T 0x900000, model and level geometry to a
     .pgeo pack, animations keyframed)
  4. bakes the textures for the PSP's graphics chip (.ptex), and the asset
     track's version (_track.ptex: half-size lightmaps, and alpha-tested
     plants that keep their leaves at a distance; --no-track skips it)
  5. bakes the level's wall decals into its walls (.pstn)

The output folder then holds halo/maps/...: copy the "halo" folder to the
root of the Memory Stick, beside the PSP folder from the build's zip.

Options:
    --maps a10,c20,bloodgulch   only these maps (default: all found)
    --no-track                  skip the asset track's _track.ptex
    --keep-work                 keep the extracted tags (several GB)
    --force                     bake maps whose hashes don't match
    --detail fp,vehicles        also bake _detail.ptex with these classes at
                                full size (fp, weapons, vehicles, characters):
                                the launch menu's "assetdetail" uses them

Nothing from the game is in this tool; it only converts the copy you own."""
import argparse, hashlib, json, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, 'lib')
INVADER = os.path.join(HERE, 'invader')
sys.path.insert(0, HERE)
from xiso import XIso

CAMPAIGN = ['a10', 'a30', 'a50', 'b30', 'b40', 'c10', 'c20', 'c40', 'd20', 'd40']
MULTIPLAYER = ['beavercreek', 'bloodgulch', 'boardingaction', 'carousel', 'chillout', 'damnation', 'hangemhigh',
               'longest', 'prisoner', 'putput', 'ratrace', 'sidewinder', 'wizard']
ALL = CAMPAIGN + MULTIPLAYER + ['ui']
BUILD_FLAGS = ['-g', 'xbox-ntsc', '-X', '0x09600000', '-T', '0x900000', '-U', '-l', '0', '-H', '-K', '0.25,0.003']


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
    env['LD_LIBRARY_PATH'] = os.path.join(INVADER, 'lib') + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
    env['HALO_PSP_INVADER_DEFINITIONS'] = os.path.join(INVADER, 'definitions')
    env['PYTHONPATH'] = LIB + (':' + env['PYTHONPATH'] if env.get('PYTHONPATH') else '')
    return env


def bake_map(name, retail, out_maps, work, track, keep_work, detail=''):
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
    log('%s: building %s for the PSP' % (name, scenario))
    built = os.path.join(folder, name + '_z.map')
    pgeo = os.path.join(out_maps, name + '.pgeo')
    run([invader('invader-build')] + BUILD_FLAGS + ['-t', os.path.join(folder, 'tags'), '-G', pgeo, '-o', built, scenario],
        log_path, env=env)
    final = os.path.join(out_maps, name + '.map')
    run([sys.executable, os.path.join(LIB, 'dez.py'), built, final], log_path, env=env)
    os.remove(built)
    log('%s: textures' % name)
    run([sys.executable, os.path.join(LIB, 'texpack.py'), final, os.path.join(out_maps, name + '.ptex')], log_path,
        cwd=LIB, env=env)
    if track and name != 'ui':
        log('%s: textures, asset track (half-size lightmaps, plants kept whole at a distance)' % name)
        run([sys.executable, os.path.join(LIB, 'texpack.py'), final, os.path.join(out_maps, name + '_track.ptex'),
             '--lightmap-scale', '2', '--alpha-coverage', '--pgeo', pgeo], log_path, cwd=LIB, env=env)
    if detail and name != 'ui':
        log('%s: textures at full size for %s' % (name, detail))
        run([sys.executable, os.path.join(LIB, 'texpack.py'), final, os.path.join(out_maps, name + '_detail.ptex'),
             '--full-detail', detail], log_path, cwd=LIB, env=env)
    if name != 'ui':
        log('%s: wall decals' % name)
        run([sys.executable, os.path.join(LIB, 'stencilbake.py'), final, pgeo, os.path.join(out_maps, name + '.pstn')],
            log_path, cwd=LIB, env=env)
    if not keep_work:
        shutil.rmtree(folder, ignore_errors=True)
    log('%s: done in %.0f s' % (name, time.time() - started))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source', help='your Halo disc image, or a folder holding its maps')
    ap.add_argument('output', help='where the halo/maps folder is written')
    ap.add_argument('--maps', default='', help='comma-separated map names (default: all)')
    ap.add_argument('--no-track', action='store_true')
    ap.add_argument('--keep-work', action='store_true')
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--detail', default='')
    a = ap.parse_args()
    wanted = [m.strip() for m in a.maps.split(',') if m.strip()] or ALL
    unknown = [m for m in wanted if m not in ALL]
    if unknown:
        sys.exit('halo-psp-bake: unknown map(s) %s; the maps are %s' % (', '.join(unknown), ', '.join(ALL)))
    out_maps = os.path.join(a.output, 'halo', 'maps')
    work = os.path.join(a.output, 'work')
    os.makedirs(out_maps, exist_ok=True)
    found = gather(a.source, work, wanted)
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
            bake_map(name, found[name], out_maps, work, not a.no_track, a.keep_work, a.detail)
        except Exception as e:
            failures.append(name)
            log('%s: FAILED: %s' % (name, e))
    if not a.keep_work:
        shutil.rmtree(os.path.join(work, 'retail'), ignore_errors=True)
    log('baked %d of %d maps into %s%s' % (len(found) - len(failures), len(wanted), out_maps,
        '; failed: ' + ', '.join(failures) if failures else ''))
    log('copy %s to the root of the Memory Stick (it holds maps/)' % os.path.join(a.output, 'halo'))
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
