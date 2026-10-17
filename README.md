# Sierra-III

Native PlayStation Portable port of Halo: Combat Evolved, built for the original PSP-1000 (32 MB RAM, 333 MHz). Based on the community decompilation. Bring your own game files: no game data is included.

*Sierra-III ("Sierra Three") is the port's codename.*

## Where it stands

The whole campaign loads and plays on a PSP-1000, all ten maps in one session. As of build 119 (2026-10-13, on Sheepdog01's PSP-1000), *The Maw* (d40) runs at 19 fps; *The Pillar of Autumn*, *Assault on the Control Room* and *Two Betrayals* are within a few milliseconds of the floor of 15 fps at 30 ticks a second; the heaviest map, *The Silent Cartographer* (b30), runs at about 4 fps in its opening flight and 9 on the beach. The engine base is ChupathingyCE. Each build's notes are `docs/build_bNNN.md`, and what it did on the PSP `docs/hardware_bNNN.md`.

The goal is a steady 30 ticks a second everywhere in the campaign, with 30 fps the target and 15 fps at 30 ticks the floor. The plan is `docs/rebuild_plan.md`, and every known speed lever, ranked, is in `docs/optimization_ranking.md`.

## How it's made

Sierra-III is written with an AI model, Claude, and every commit says so: Claude is the author and Sheepdog01 (Sheepdog-01) the co-author. The division of work is fixed:

- **Sheepdog01 decides what to build** and in what order, and every change to how the game looks or plays is his call, made after seeing both sides.
- **Claude writes the code**, and each change carries its evidence: why it's there, what it was measured against, and whether the figure came from the PSP, the emulator, the code or inference (`[M]`, `[E]`, `[D]`, `[U]` in the docs).
- **Every build is tested on a real PSP-1000** by Sheepdog01 before the next one is made, and speed-ups that claim to leave the game unchanged are checked tick by tick against the game's state in an emulator first.
- **Sheepdog01 reviews every build's changes** through a walk-through of what changed and why (`docs/review_bNNN.md`).

The decompilation this port is built on, and ChupathingyCE, were also made with heavy use of AI tools; ChupathingyCE's README says how it uses them, and Sierra-III follows the same principle: a person decides, reviews, tests and answers for every change.

## What's here

| Path | What |
|---|---|
| `psp/src/` | The PSP platform layer: the GE renderer, memory, streaming caches, sound (mixed on the Media Engine), input, the launch menu, the crash handler's game side |
| `psp_fixups.py` | Every change made to the engine, applied to a mirror of it at build time |
| `psp_build.py` | The build: mirror, fixups, compile, link, `EBOOT.PBP` |
| `psp/crash`, `psp/profile` | Two helper PRX modules: the crash handler, the hardware-counter profiler |
| `bake/` | `halo-psp-bake`: turns your own Xbox disc image into the PSP's maps |
| `tools/` | Bake helpers, the emulator harness, log and profile readers |
| `bench/` | The hardware micro-benchmarks behind `docs/psp_hardware_reference.md` |
| `upstream/` | Our patches to the engine and to Invader (`UPSTREAM.md`) |
| `docs/` | The design notes, build reports and hardware findings |

## Building

On Linux with the pspdev toolchain, Python 3, ninja, cmake and git:

```
PSPDEV=/opt/pspdev tools/build_release.sh <output folder>
```

It fetches the engine and the Media Engine library at the commits `upstream/pins.env` pins (once, into a cache), builds the port and its two helper modules, and writes `halo_psp_<commit>.zip`. The same commit builds the same bytes on any machine, so the EBOOT's hash can be checked against the build's notes.

**`docs/toolchain.md`** is the whole setup from a clean machine: the packages, the toolchain, the build, baking maps from your own disc, installing, and the emulator. **`docs/testing_pipeline.md`** describes how the project tests.

## Maps

The port reads maps baked for the PSP: the map itself, plus its texture, geometry and stencil packs. Make them from your own disc image:

```
bake/make_bundle.sh <out>                  # once: needs the Invader fork built (UPSTREAM.md)
<out>/halo-psp-bake <disc image> <maps out>
```

Then copy the output's `halo/` folder to the root of the Memory Stick. `bake/README.md` has the details.

## Installing

Unzip the build zip at the Memory Stick's root: `PSP/GAME/halo/` (EBOOT.PBP, the two PRX modules, `LICENSE-mecore.md`), and `halo/presets.txt` (the launch menu's test presets) when the build has one. Put the baked `halo/` folder at the stick's root. It needs custom firmware (built and tested on 6.61 with ARK-4).

## Multiplayer

The multiplayer build lives on the `mp-recon` branch. It is built with `PSP_MULTIPLAYER=1`, installs to `PSP/GAME/halomp` beside the campaign build, and keeps its settings in `ms0:/halomp`.

A PSP-1000 has played whole games against a PC on ChupathingyCE's dedicated server, over the PC's own Wi-Fi hotspot. `docs/multiplayer_setup.md` walks through it: the build, the bake, the hotspot, ARK-4's WPA2 setting, the server and the stick.

## History

This repository was created on 2026-10-09. The history before then was rebuilt from the source snapshots kept for each build, one commit per snapshot, tagged by build (`b87` to `b110`). Builds 1 to 86 survive only as binaries. Every build's zip is kept in one private Release, `build-archive`; a build gets a Release of its own only when it's shared.

## Credits

What Sierra-III is built on (the decomp, ChupathingyCE, Invader, the Media Engine library, PPSSPP, pspdev) is in `UPSTREAM.md`. Beyond that, Sierra-III learns from other ports of the same decomp, and credits each one whose ideas or code it takes:

- **[halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita)** by BirchWoodGod, Halo CE on the PS Vita: exact speed-ups to the campaign's AI and collision walks, and test methods (`docs/research/research_vita.md`). Used with his permission.
- **[Halo 64](https://github.com/JackTulli/halo-64)** by JackTulli, Halo CE's multiplayer on the Nintendo 64 (CC0): fixes and speed-ups for a MIPS console (`docs/research/research_halo64.md`).
- **Overpass** (Red Hat, SIL OFL 1.1), from ChupathingyCE's assets: the letters on the PSP button glyphs (`psp/glyphs/psp_glyphs.png`, drawn by `tools/psp_glyphs.py` in the shapes of the PSP-1000's buttons).

Each borrowed change also names its source in its commit message and its build's review.

## Licence

None chosen yet. The engine and the tools this builds on carry their own licences (`UPSTREAM.md`).

Sierra-III was created under Microsoft's "Game Content Usage Rules" using assets from Halo: Combat Evolved, and it is not endorsed by or affiliated with Microsoft. The only such assets in this repository are the XMB menu's wallpaper, made from stills of our own gameplay footage. Halo: Combat Evolved © Microsoft Corporation.
