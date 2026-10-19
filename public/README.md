# Sierra-III

Native PlayStation Portable port of Halo: Combat Evolved, built for the original PSP-1000 (32 MB RAM, 333 MHz). Based on the community decompilation. Bring your own game files: no game data is included.

*Sierra-III ("Sierra Three") is the port's codename.*

## Where it stands

The whole campaign loads and plays on a PSP-1000, all ten maps in one session. As of build 119 (2026-10-13, on a PSP-1000), *The Maw* (d40) runs at 19 fps; *The Pillar of Autumn*, *Assault on the Control Room* and *Two Betrayals* are within a few milliseconds of 15 fps at 30 ticks a second; the heaviest map, *The Silent Cartographer* (b30), runs at about 4 fps in its opening flight and 9 on the beach. The engine base is ChupathingyCE. Each release's notes are `docs/release_notes_bNNN.md`.

The target is a steady 30 ticks a second at 30 fps everywhere in the campaign. 15/15 (15 fps at 15 ticks a second) is a milestone to bench against, not the floor or the ceiling.

## How it's made

Sierra-III is written with an AI model, Claude, and every change says so. The division of work is fixed:

- **The maintainer decides what to build** and in what order, and every change to how the game looks or plays is the maintainer's call, made after seeing both sides.
- **Claude writes the code**, and each change carries its evidence: why it's there, what it was measured against, and whether the figure came from the PSP, the emulator, the code or inference (`[M]`, `[E]`, `[D]`, `[U]` in the docs).
- **Every build is tested on a real PSP-1000** before the next one is made, and speed-ups that claim to leave the game unchanged are checked tick by tick against the game's state in an emulator first.
- **The maintainer reviews every build's changes** through a walk-through of what changed and why.

The decompilation this port is built on, and ChupathingyCE, were also made with heavy use of AI tools; ChupathingyCE's README says how it uses them, and Sierra-III follows the same principle: a person decides, reviews, tests and answers for every change.

## What's here

| Path | What |
|---|---|
| `psp/src/` | The PSP platform layer: the GE renderer, memory, streaming caches, sound (mixed on the Media Engine), input, the launch menu, the crash handler's game side |
| `psp_fixups.py` | Every change made to the engine, applied to a mirror of it at build time |
| `psp_build.py` | The build: mirror, fixups, compile, link, `EBOOT.PBP` |
| `psp/crash`, `psp/profile` | Two helper PRX modules: the crash handler, the hardware-counter profiler |
| `pgo/`, `psp/layout/` | The release's profile (counts of this code's branches from our own runs, no game data) and code order |
| `bake/` | `halo-psp-bake`: turns your own Xbox disc image into the PSP's maps |
| `tools/` | The release build, the bake's helpers, the emulator harness |
| `bench/` | The hardware micro-benchmarks behind `docs/psp_hardware_reference.md` |
| `upstream/` | The upstream pins, and our patches to Invader (`UPSTREAM.md`) |
| `docs/` | The player's guide (`play.md`), building from a clean machine (`toolchain.md`), notes for other ports (`for_other_ports/`), the hardware reference, how the bake works |

Development happens in a separate repository. This one holds the releases, one commit each. Code comments and notes sometimes cite development documents (build notes, reviews, hardware reports) and test run numbers that stay there; they're left in as the record of why a line is what it is.

## Building

On Linux with the pspdev toolchain, Python 3, ninja, cmake and git:

```
PSPDEV=/opt/pspdev tools/build_release.sh <output folder>
```

It fetches the engine and the Media Engine library at the commits `upstream/pins.env` pins (once, into a cache), builds the port and its two helper modules, and writes `halo_psp_<commit>.zip`. The same commit and switches build the same bytes on any machine, so the EBOOT's hash can be checked against the release notes, which name the switches the release was built with.

**`docs/toolchain.md`** is the whole setup from a clean machine: the packages, the toolchain, the build, baking maps from your own disc, installing, and the emulator.

## Maps

The port reads maps baked for the PSP: the map itself, plus its texture, geometry and stencil packs. Make them from your own disc image:

```
<bundle>/halo-psp-bake <disc image> <maps out>       # halo-psp-bake.cmd on Windows
```

The bundle for your system (Windows, macOS or Linux) comes with each release; `bake/make_bundle.sh <out>` builds one from source.

Then copy the output's `halo/` folder to the root of the Memory Stick. `bake/README.md` has the details.

## Installing

**`docs/play.md` is the player's guide**, step by step from a stock PSP and your own Xbox disc to a playable game: custom firmware, imaging the disc, the bake, the stick, the first launch, and what to send when it goes wrong. In short: unzip the build zip at the Memory Stick's root: `PSP/GAME/halo/` (EBOOT.PBP, the two PRX modules, `LICENSE-mecore.md`, `licenses/`), and `halo/presets.txt` (the launch menu's test presets) when the build has one. Put the baked `halo/` folder at the stick's root. It needs custom firmware (built and tested on 6.61 with ARK-4).

## Multiplayer

**Multiplayer isn't part of this release.** The multiplayer build is in development: it joins Halo PC's public ChupathingyCE games over the internet, on the thirteen multiplayer maps from the disc. It is built from the same source with `PSP_MULTIPLAYER=1`, but isn't supported yet.

## Credits

What Sierra-III is built on (the decomp, ChupathingyCE, Invader, the Media Engine library, PPSSPP, pspdev) is in `UPSTREAM.md`. Beyond that, Sierra-III learns from other ports of the same decomp, and credits each one whose ideas or code it takes:

- **[halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita)** by BirchWoodGod, Halo CE on the PS Vita (GPL-3.0): exact speed-ups to the campaign's AI and object walks (b126's RV1, RV2 and RV4), and test methods (`docs/research/research_vita.md`). The code taken is his own, used with his permission.
- **[Halo 64](https://github.com/JackTulli/halo-64)** by JackTulli, Halo CE's multiplayer on the Nintendo 64 (CC0): fixes and speed-ups for a MIPS console (`docs/research/research_halo64.md`): a still unit's aiming change as x86 computes it (b126's RH1), and the path search's clear by its header (H2, taken in halo-ce-vita's form).
- **[DamnationCE](https://github.com/xshxdex98/DamnationCE)** by xshxdex98 (CC0): the HUD's weapon state zeroed before it's filled (`488bd83e`; b126's RB1), found through halo-ce-vita's notes (`docs/research/research_vita.md` §9).
- **ChupathingyCE's PC audio** (Jeff Clark, `5f3a0f98` "Fix Xbox ADPCM decoding"): the Xbox ADPCM block layout (the header's sample first, 63 nibbles, one padding), which cured the port's 344 Hz buzz (`psp_dsound.c`, the bake's `soundpack.py` and `snd0.py`).
- **Krona One** (Yvonne Schüttler) and **Audiowide** (Astigmatic), both SIL OFL 1.1: the game title on the XMB wallpaper and the tile's label (`psp/xmb/fonts/` holds Audiowide with its licence).
- **Overpass** (the Overpass Project Authors, Red Hat; SIL OFL 1.1), from ChupathingyCE's assets: the letters on the PSP button glyphs (`psp/glyphs/psp_glyphs.png`, drawn by `tools/psp_glyphs.py` in the shapes of the PSP-1000's buttons).

## Licence

Sierra-III's own code, scripts and documents are under the MIT licence (`LICENSE`). `LICENSE` also names what it doesn't cover: the XMB wallpaper (below), the project's own look (the Sierra mark and its XMB art, all rights reserved: a fork picks its own), our patches to Invader and PPSSPP (under those programs' GPL), and the third-party files carried as they are. The engine and the tools this builds on carry their own licences (`UPSTREAM.md`).

Sierra-III was created under Microsoft's "[Game Content Usage Rules](https://www.xbox.com/en-US/developers/rules)" using assets from Halo: Combat Evolved, and it is not endorsed by or affiliated with Microsoft. The only such assets in this repository are the campaign's XMB wallpaper (`psp/xmb/campaign/PIC1.PNG`), made from stills of our own gameplay footage. Halo: Combat Evolved © Microsoft Corporation.
