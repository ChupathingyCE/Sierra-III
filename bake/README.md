# halo-psp-bake

Turns **your own copy** of Halo: Combat Evolved for the original Xbox into the
map files the PSP build reads. Nothing from the game is included here.

## What you need

- A PC with Windows 10 or 11 (x86-64), macOS 11 or later (Apple silicon or
  Intel), or Linux (x86-64, glibc 2.28 or later: any distribution from 2018 on).
- **Python 3.9 to 3.14**, installed once:
  - Windows: from [python.org](https://www.python.org/downloads/windows/);
    tick "Add python.exe to PATH" in the installer.
  - macOS: from [python.org](https://www.python.org/downloads/macos/), or
    Homebrew's `python@3.13`.
  - Linux: your distribution's `python3`, with `python3-venv` on Debian and
    Ubuntu (`sudo apt install python3-venv`). No suitable Python? Install
    [uv](https://docs.astral.sh/uv/) and the bake has it fetch one.
- An image of your Halo disc: NTSC, version 01.10.12.2276 (the one the build
  was made with; the tool checks each map's SHA-256 against hashes.json). A
  bare game-partition image (what most Xbox dumping tools and extract-xiso
  make) or a full Redump image both work. A folder with the disc's maps
  (`maps/*.map`) works too.
- About 3 GB free while it runs; the result is about 1.6 GB.

## Get the bundle

Download the bundle for your system from the release and unpack it:

| System | File |
|---|---|
| Windows | `halo-psp-bake-windows-<release>.zip` |
| macOS | `halo-psp-bake-macos-<release>.tar.gz` (one bundle for Apple silicon and Intel) |
| Linux | `halo-psp-bake-linux-<release>.tar.gz` |

Each has a `.sha256` beside it, to check the download.

The tools in the bundle aren't signed, so the system may ask once:

- **macOS** blocks unsigned programs downloaded from the internet. Allow the
  bundle's once, in Terminal, then run the bake:
  `xattr -dr com.apple.quarantine ~/Downloads/halo-psp-bake-macos-<release>`
  (the bake says so too, with the right folder, if it's blocked).
- **Windows**: if SmartScreen or your antivirus warns about the bundle's
  programs, choose "More info", then "Run anyway". Or, before unpacking,
  right-click the zip, Properties, tick "Unblock".

## Run it

Linux and macOS, in a terminal, in the bundle's folder:

```
./halo-psp-bake ~/Halo.iso ~/halo-psp-out
```

Windows, in a Command Prompt, in the bundle's folder:

```
halo-psp-bake.cmd C:\Users\you\Halo.iso C:\halo-psp-out
```

On Windows keep the output folder's path short (`C:\halo-psp-out`): the
tags extracted while it works have long paths of their own.

The first run sets up a Python environment beside the bake (`.venv`) with
numpy, Pillow and shapely (it needs the internet that once;
`halo-psp-bake --setup` does only this). Then, for each map, it reads it from
the image, checks it, rebuilds it for the PSP's memory, and bakes the
textures, the asset track's textures (`_track.ptex`: half-size lightmaps,
plants that keep their leaves at a distance) and the wall decals. Expect 3 to
10 minutes a map (all 24: an hour or two).

`--detail fp,vehicles` also bakes `_detail.ptex`, with those classes' textures
at full size (fp, weapons, vehicles, characters). The launch menu chooses:
"asset track" uses the `_track` packs, "asset detail" the `_detail` ones.

Only some maps: `--maps c20,bloodgulch,ui` (always include `ui`).
`--help` lists every option.

## The menu's sound

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

The sound is encoded with atracdenc, which every bundle carries
(`atracdenc/`). If it's missing or doesn't start, the bake warns, loudly, at
its start and end, and copies the EBOOT as it is: the maps bake all the same.

## Then

Copy `halo-psp-out/halo` to the root of the Memory Stick, so it holds
`halo/maps/`. The game itself (`PSP/GAME/halo/`) comes from the build's zip,
or, with `--eboot`, from `halo-psp-out/PSP/`: then copy both folders.

Maps from this version of the tool are for **build 139 or later** (its
palettes and wall decals are made differently: Sierra-III's
`docs/bake_changes.md`). Bake again when a release says its bundle changed.

## What's inside, and the licences

- `bake.py`, `launcher.py`, `xiso.py`, `lib/`: the bake itself (the Halo PSP
  port's own scripts).
- `invader/bin`: Invader (`invader-build`, `invader-extract`, `invader-info`),
  the port's fork, GPLv3. Its full source is `source/invader-src.tar.gz`, and
  `source/invader-psp-fork.patch` is the change against upstream Invader
  (commit d43ce55). INVADER.md says how to rebuild it.
- `atracdenc/bin`: atracdenc, LGPL-2.1, with libsndfile (LGPL-2.1) linked in;
  their source is in `source/`.
- Every program is linked statically, so nothing else needs installing.
- `LICENSES/README.txt` lists every component, its licence and where its
  source is; the licence texts are beside it.
