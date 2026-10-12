# halo-psp-bake

Turns **your own copy** of Halo: Combat Evolved for the original Xbox into the
map files the PSP build reads. Nothing from the game is included here.

## What you need

- A Linux PC (x86-64) with Python 3.9 to 3.13 (`python3 -m venv` must work;
  on Debian/Ubuntu: `sudo apt install python3-venv`). Python 3.14 alone won't
  do yet: the decal bake's `triangle` doesn't build there. On a system whose
  only Python is 3.14 (a current Arch or CachyOS), install
  [uv](https://docs.astral.sh/uv/) (`curl -LsSf https://astral.sh/uv/install.sh | sh`):
  the script then has uv fetch a Python 3.12 for the bake alone.
- The bundled Invader needs glibc 2.39 or later (Ubuntu 24.04, Fedora 40, a
  current Arch or CachyOS). On older systems, build it from source: see
  INVADER.md.
- An image of your Halo disc: NTSC, version 01.10.12.2276 (the one the build
  was made with; the tool checks each map's SHA-256 against hashes.json).
  A bare game-partition image (what most Xbox dumping tools and extract-xiso
  make) or a full Redump image both work. A folder with the disc's maps
  (`maps/*.map`) works too.
- About 3 GB free while it runs; the result is about 1.6 GB.

## Run it

```
./halo-psp-bake ~/Halo.iso ~/halo-psp-out
```

The first run sets up a Python environment beside the script (needs the
internet once). Then, for each map, it reads it from the image, checks it,
rebuilds it for the PSP's memory, and bakes the textures, the asset track's
textures (`_track.ptex`: half-size lightmaps, plants that keep their leaves at
a distance) and the wall decals. Expect 3 to 10 minutes a map (all 24: an
hour or two).

`--detail fp,vehicles` also bakes `_detail.ptex`, with those classes' textures
at full size (fp, weapons, vehicles, characters). The launch menu chooses:
"asset track" uses the `_track` packs, "asset detail" the `_detail` ones.

Only some maps: `--maps c20,bloodgulch,ui` (always include `ui`).

## The menu's sound (optional)

Give the bake the release's EBOOT and it adds a clip of the music, cut from
your disc, as the sound the PSP's menu plays on the game's entry:

```
./halo-psp-bake ~/Halo.iso ~/halo-psp-out --eboot ~/Downloads/halo-bNNN/PSP/GAME/halo/EBOOT.PBP
```

The copy goes to `~/halo-psp-out/PSP/GAME/halo/EBOOT.PBP` (`halomp/` for the
multiplayer build's). `--music` picks another clip:
`[MAP:]TAG[@START][+LENGTH]`, a music tag by path, with seconds, e.g.
`--music 'b30:levels\b30\music\b30_01@10+30'`. `--maps ui --list-music` lists a
map's music and its lengths, without baking anything.

The sound is encoded with atracdenc (bundled when the bundle was made with
it; otherwise one on PATH). If it can't be made, the bake says why and copies
the EBOOT as it is: the sound is never why a bake fails.

## Then

Copy `~/halo-psp-out/halo` to the root of the Memory Stick, so it holds
`halo/maps/`. The game itself (`PSP/GAME/halo/`) comes from the build's zip,
or, with `--eboot`, from `~/halo-psp-out/PSP/`: then copy both folders.

Maps from this version of the tool need **build 108 or later**: the BSPs'
grass and the bytes of four tag blocks the game never reads are left out,
which is what lets a30, a50 and b40 fit the PSP's memory. Older builds can't
load them, and build 108 still loads maps baked before it.

## Licences

- The bake tools in `lib/` are part of the Halo PSP port.
- Invader (`invader/`) is GPLv3 (LICENSES/). This is a modified version: the
  full source is in `invader-src.tar.gz`, and `invader-psp-fork.patch` is the
  change against upstream Invader (commit d43ce55).
- The game code the build runs is halo-ce-universal (CC0).
- atracdenc (`atracdenc/`, when bundled) is LGPL-2.1 (LICENSES/); its source
  is in `atracdenc-src.tar.gz`. The libraries beside it (libsndfile and its
  codecs) are the build machine's, under their own licences.
