# halo-psp-bake

Turns **your own copy** of Halo: Combat Evolved for the original Xbox into the
map files the PSP build reads. Nothing from the game is included here.

## What you need

- A Linux PC (x86-64) with Python 3.9 or later (`python3 -m venv` must work;
  on Debian/Ubuntu: `sudo apt install python3-venv`).
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

## Then

Copy `~/halo-psp-out/halo` to the root of the Memory Stick, so it holds
`halo/maps/`. The game itself (`PSP/GAME/halo/`) comes from the build's zip.

## Licences

- The bake tools in `lib/` are part of the Halo PSP port.
- Invader (`invader/`) is GPLv3 (LICENSES/). This is a modified version: the
  full source is in `invader-src.tar.gz`, and `invader-psp-fork.patch` is the
  change against upstream Invader (commit d43ce55).
- The game code the build runs is halo-ce-universal (CC0).
