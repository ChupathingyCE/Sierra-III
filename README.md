# Sierra-III

**warthog-psp**: ChupathingyCE's port of Halo: Combat Evolved to the PlayStation Portable, built for the original PSP-1000 (32 MB RAM, 333 MHz). It runs the community decompilation through ChupathingyCE, and its own spin is a ground-up rebuild of the engine's frame for a 333 MHz handheld. Bring your own game files: no game data is included.

*Sierra-III ("Sierra Three") is the port's name, kept when it joined ChupathingyCE (2026-11-19).*

## Where it stands

The whole campaign loads and plays on a PSP-1000, all ten maps in one session. Build 158 is the newest tick (the release candidate), and new work lands in the tock builds after it. `docs/builds.md` lists them all.

The target is a steady 30 ticks a second at 30 fps everywhere in the campaign, and it isn't there yet. On full playthroughs at Legendary, the heaviest case measured (PSP-3000, November 2026), the slowest stretches run at 7-10 fps on *Assault on the Control Room* (b40), *Halo* (a30), *Two Betrayals* (c40) and *The Truth and Reconciliation* (a50), with the game below real time there (ticks of 35-45 ms against the 33 ms a tick needs). Lighter stretches run in the teens and twenties.

The work under way is a ground-up rebuild: the frame budgeted from zero, with each subsystem rewritten as the port's own code (a "box") against data laid out for the PSP. `docs/redesign_plan.md` is the plan, and `docs/cpu_cost_map.md` says where the main CPU's time goes and how each cut is priced. The engine base is ChupathingyCE. Each build's notes are `docs/build_bNNN.md`, and what it did on the PSP `docs/hardware_bNNN.md`.

## Reading the code

- `psp_fixups.py` holds every change made to the engine, as anchored text replacements applied to a mirror of ChupathingyCE at build time. The engine checkout itself is never edited.
- `psp/src/` is the port's own code: the PSP platform layer (the GE renderer, the Media Engine's jobs, memory, streaming, sound, input) and the rebuild's boxes (`box_*.c`, each with its design page `docs/box_<name>.md`).
- `psp_build.py` builds it all; `CLAUDE.md` has the build switches.
- `docs/README.md` is the index to every write-up. Comments in the code say why, not what; a change's history is in its commit message and its review page (`docs/review_*.md`).

## How it's made

Sierra-III is written with an AI model, Claude. Commits are authored by Sheepdog-01, as ChupathingyCE's contributor rules ask, but the code is Claude's, and the division of work is fixed:

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

The port reads maps baked for the PSP: the map itself, plus its texture, geometry and stencil packs. Make them from your own disc image with the bake bundle for your system (Windows, macOS or Linux), which comes with each release: put the disc image and the build zip in the bundle's two folders, double-click `Bake Halo` (`Bake Halo.cmd`, `Bake Halo.command` or `bake-halo.sh`), and copy what it puts in `COPY-TO-MEMORY-STICK` (the game, with its menu sound, and the maps) to the root of the Memory Stick. `bake/README.md` has the details and the command line; `bake/make_bundle.sh <out>` builds a bundle from source.

## Installing

**`docs/play.md` is the player's guide**, step by step from a stock PSP and your own Xbox disc to a playable game: custom firmware, imaging the disc, the bake, the stick, the first launch, and what to send when it goes wrong. In short: unzip the build zip at the Memory Stick's root: `PSP/GAME/halo/` (EBOOT.PBP, the two PRX modules, `LICENSE-mecore.md`), and `halo/presets.txt` (the launch menu's test presets) when the build has one. Put the baked `halo/` folder at the stick's root. It needs custom firmware (built and tested on 6.61 with ARK-4).

## Multiplayer

**Multiplayer isn't part of the first public release (b139).** The multiplayer build is in development: it joins Halo PC's public ChupathingyCE games over the internet, from its ONLINE GAMES screen, on the thirteen multiplayer maps from the disc. When a release carries it, it has its own zip (`halo_psp_bNNNmp.zip`): it installs to `PSP/GAME/halomp` beside the campaign build, reads the same baked maps, and keeps its settings in `ms0:/halomp`. It is built from `main` with `PSP_MULTIPLAYER=1`. For now it only joins: no hosting, no PSP-to-PSP play (`docs/mp_client_only.md`).

`docs/multiplayer_setup.md` walks through it: the Wi-Fi a PSP can join, ARK-4's WPA2 setting, the online list, what to do when it goes wrong, and playing over your own PC's hotspot and dedicated server.

## History

The project began on 2026-10-09 in a repository of its own, with the history before then rebuilt from the source snapshots kept for each build (builds 87 to 110; builds 1 to 86 survive only as binaries). It moved into ChupathingyCE on 2026-11-19 with its history carried over and cleaned of private details, so the commits here have new hashes. Build records and zips are kept privately by the project; a build gets a Release here only when it's shared.

## Credits

What Sierra-III is built on (the decomp, ChupathingyCE, Invader, the Media Engine library, PPSSPP, pspdev) is in `UPSTREAM.md`. Beyond that, Sierra-III learns from other ports of the same decomp, and credits each one whose ideas or code it takes:

- **[halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita)** by BirchWoodGod, Halo CE on the PS Vita (GPL-3.0): exact speed-ups to the campaign's AI and object walks (b126's RV1, RV2 and RV4; perception passing over the objects it only stamps, PRS, from his count; the lruv caches' integrity walk left out and a hole's blocks found along the list, LVW), and test methods (`docs/research/research_vita.md`). The code taken is his own, used with his permission.
- **[Halo 64](https://github.com/JackTulli/halo-64)** by JackTulli, Halo CE's multiplayer on the Nintendo 64 (CC0): fixes and speed-ups for a MIPS console (`docs/research/research_halo64.md`): a still unit's aiming change as x86 computes it (b126's RH1), and the path search's clear by its header (H2, taken in halo-ce-vita's form).
- **[DamnationCE](https://github.com/xshxdex98/DamnationCE)** by xshxdex98 (CC0): the HUD's weapon state zeroed before it's filled (`488bd83e`; b126's RB1), found through halo-ce-vita's notes (`docs/research/research_vita.md` §9).
- **[psp-media-engine-custom-core](https://github.com/mcidclan/psp-media-engine-custom-core)** and **[psp-virtual-mobile-engine-ext](https://github.com/mcidclan/psp-virtual-mobile-engine-ext)** by mcidclan (m-c/d), MIT: the VME's library and its register knowledge, and his mat4x4-mul-distributed sample, which `vmetest` runs inside the game (`psp/src/vme/` carries his headers and licence; `psp/src/psp_vme.c` his library functions and the sample, with our own register writes in place of the firmware's; `docs/review_vmegame.md`).
- **ChupathingyCE's PC audio** (Jeff Clark, `5f3a0f98` "Fix Xbox ADPCM decoding"): the Xbox ADPCM block layout (the header's sample first, 63 nibbles, one padding), which cured the port's 344 Hz buzz (`psp_dsound.c`, the bake's `soundpack.py` and `snd0.py`).
- **[meshoptimizer](https://github.com/zeux/meshoptimizer)** by Arseny Kapoulkine, MIT: the bake's mesh simplification for the rigid models' detail levels (SSPR; `tools/third_party/` carries it with its licence; `docs/sspr_bake.md`).
- **Krona One** (Yvonne Schüttler) and **Audiowide** (Astigmatic), both SIL OFL 1.1: the game title on the XMB wallpaper and the tile's label (`psp/xmb/fonts/` holds Audiowide with its licence).
- **Overpass** (the Overpass Project Authors, Red Hat; SIL OFL 1.1), from ChupathingyCE's assets: the letters on the PSP button glyphs (`psp/glyphs/psp_glyphs.png`, drawn by `tools/psp_glyphs.py` in the shapes of the PSP-1000's buttons).

Each borrowed change also names its source in its commit message and its build's review.

## Licence

**GPL-3.0-or-later** (`LICENSE`, the licence's text; `NOTICE.md`, the notice and what it covers). The engine, ChupathingyCE, is GPL-3.0-or-later from 0.8.0 and this port's pin is past that change, so Sierra-III follows it. Sierra-III's own work before the switch was MIT (`LICENSES/MIT.txt`); copies taken from that history keep it.

`NOTICE.md` names what the GPL doesn't cover here:
- the XMB wallpaper (below);
- the project's own mark and XMB art;
- our patches to Invader and PPSSPP (under their own GPL terms);
- third-party files carried as they are.

The engine and the tools this builds on carry their own licences (`UPSTREAM.md`).

Sierra-III was created under Microsoft's "[Game Content Usage Rules](https://www.xbox.com/en-US/developers/rules)" using assets from Halo: Combat Evolved, and it is not endorsed by or affiliated with Microsoft. The only such assets in this repository are the campaign's XMB wallpaper (`psp/xmb/campaign/PIC1.PNG`), made from stills of our own gameplay footage. Halo: Combat Evolved © Microsoft Corporation.
