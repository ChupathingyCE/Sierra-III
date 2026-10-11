# Sierra-III

Native PlayStation Portable port of Halo: Combat Evolved, built for the original PSP-1000 (32 MB RAM, 333 MHz). Based on the community decompilation. Bring your own game files: no game data is included.

*Sierra-III ("Sierra Three") is the port's codename.*

## Where it stands

The whole campaign loads on a PSP-1000: all ten maps fit its memory since build 108. On hardware (build 107), *The Silent Cartographer* (b30) plays start to finish at about 9 fps on average, 15 ticks a second. Build 110 moved the engine to ChupathingyCE. Each build's notes are `docs/build_bNNN.md`, and what it did on the PSP `docs/hardware_bNNN.md`.

The goal is a steady 30 ticks a second everywhere in the campaign, with 30 fps the target. The plan to get there is `docs/rebuild_plan.md`, and every known speed lever, ranked, is in `docs/optimization_ranking.md`.

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

## History

This repository was created on 2026-10-09. The history before then was rebuilt from the source snapshots kept for each build, one commit per snapshot, tagged by build (`b87` to `b110`). Builds 1 to 86 survive only as binaries. Every build's zip is kept in one private Release, `build-archive`; a build gets a Release of its own only when it's shared.

## Licence

None chosen yet. The engine and the tools this builds on carry their own licences (`UPSTREAM.md`).
