# Going public: the checklist

Sheepdog01, 2026-10-17 12:44: **the repo goes live when three things hold**, and that is the only deadline:
1. **A release we're happy with.** b138 is the dress rehearsal (Sheepdog01 plays it by hand on a PSP); b139 tests the new bake; whatever is in flight now and done by then folds in.
2. **Documentation complete enough that someone can follow it to a playable game on their own PSP**, from a stock PSP and their own Xbox disc.
3. **Every legal point and attribution settled.**

How it goes out (Sheepdog01, 12:11 and 12:38): Sierra-III stays private as the dev repo; `Sheepdog-01/Sierra-III-Release` mirrors `main` and the release tags (same hashes) and carries the bake bundle, so players bake their own maps. Before the mirror goes live, `public-prep` (docs and config: the LICENSE, README fixes, home paths as variables, a docs index, a gitleaks guardrail) merges into `main`.

Owners: **M** master, **R** renderer, **S** sound, **MP** multiplayer, **Sheepdog01**. Each line is done when its proof is named beside it.

## 1. The release

| | Item | Owner | Done when |
|---|---|---|---|
| 1.1 | b138 played by hand on a PSP: boot to the menu, a campaign level start to finish, a level change, a multiplayer join | Sheepdog01 | his notes in the build's tests |
| 1.2 | b139 on the new bake (below, 3.3): the release check against b138, the ladder, Sheepdog01's hand test | M, Sheepdog01 | `build_b139.md` |
| 1.3 | The release is named: the tag, its zips' hashes, its bake bundle's hash | M | `releases.tsv` and the release notes |

## 2. The path to a playable game (one guide, in order)

The guide is `docs/play.md` (new); README's "Installing" points to it. Each step says what the player needs, the commands, and how to tell it worked.

| | Step | Owner | Today |
|---|---|---|---|
| 2.1 | What you need: a PSP (1000 tested; 2000/3000 work), custom firmware (6.61 + ARK-4), a Memory Stick with ~3.5 GB free, the Xbox disc (NTSC 01.10.12.2276) as an image, a PC with Windows, macOS or Linux (the bake bundle, 3.1) | M | scattered across README and `multiplayer_setup.md` |
| 2.2 | Custom firmware: point to ARK-4's own instructions; say which settings matter (WPA2 for multiplayer). We don't write a CFW guide | M | `multiplayer_setup.md` §ARK-4 |
| 2.3 | Imaging your disc: what the bake accepts (XISO, Redump image, a folder of maps) and how to check the revision | M | `bake/README.md` |
| 2.4 | Baking: download the bundle, run it, how long it takes, how much disk, what "hash mismatch" means | M | `bake/README.md` (sizes stale) |
| 2.5 | Installing on the stick: the build zip and the baked `halo/` folder, the stick layout, the stick rules (what never to delete) | M | README "Installing" |
| 2.6 | First launch: what to expect (the boot, the menu, the load and chant), the launch menu, how to quit | M | `build_bNNN.md` per build |
| 2.7 | Multiplayer: joining a ChupathingyCE server (hotspot or router), the online list | MP | `multiplayer_setup.md` (stale: `mp-recon` bake) |
| 2.8 | When it goes wrong: pulling the log, what to send in an issue | M | Sheepdog01's own log rules |
| 2.9 | **A dry run of the guide by someone who hasn't seen it** (a fresh session, then Sheepdog01), on the release as named | M, Sheepdog01 | — |

## 3. The bake for players

| | Item | Owner | Done when |
|---|---|---|---|
| 3.1 | One bundle per OS and tagged commit, built from source on GitHub's own runners: `.github/workflows/bake-bundle.yml` (on tags `b*`, and by hand). Linux in a manylinux_2_28 container (glibc 2.28, so any distribution from 2018 on), Windows with MSYS2's UCRT64 gcc, macOS universal (arm64 and x86-64) on macos-14. `bake/bundle/build_tools.sh` builds Invader and atracdenc with every library static (only the system's C/C++ runtime left); `assemble.py` packs it; `smoke.py` checks each bundle on made-up data (every script, Invader's three tools, a tone through atracdenc into an EBOOT) under the oldest and newest Python. Players install Python 3.9–3.14; `launcher.py` sets up numpy, Pillow and shapely (`requirements.txt`) on the first run. **Left:** a real bake from each OS's bundle (Sheepdog01: Windows and Linux; a friend: macOS), and the bundles' hashes in the release notes | M | the workflow's run green on all three, its artifacts' `.sha256` in the release notes, and the three test bakes |
| 3.2 | **Done in the bundle:** `LICENSES/README.txt` maps every component to its licence, its texts and its source: Invader (GPL-3.0, with its ADPCM pieces' BSD and MIT), Rat In a Tube (GPL-3.0), libFLAC, libvorbis, libogg (BSD-3), libsamplerate (BSD-2), libsquish (MIT), zlib, the Rust standard library (MIT/Apache-2.0), atracdenc (LGPL-2.1) with libgha and KISS FFT, libsndfile (LGPL-2.1+, built WAV only: no external codecs), the GCC runtime (its runtime exception) and on Windows MinGW-w64's runtime and winpthreads, and ChupathingyCE (CC0). The GPL and LGPL pieces' full source as built is in `source/`, with the script that built them. libgomp is gone (libsquish is built without OpenMP) | M | `LICENSES/` in each built bundle, checked once by eye |
| 3.3 | **The new bake** (documented, Sheepdog01 12:38): `triangle` and scikit-learn out (numpy, Pillow, shapely only; Python 3.9–3.14). Palettes by a seeded numpy k-means, proved per texture against the old (Oklab error, none worse); decals by faces (`polygonize`) then per-face triangulation, proved by identical stencil coverage per layer, then emulator shots of b30's and c20's decals | R | `bake_changes.md` (what changed, why, re-bake needed) and the proofs |
| 3.4 | A bake version the EBOOT checks: a mismatched bake says "re-bake with bundle N" instead of drawing wrong | M | the check, and the message on screen |
| 3.5 | The disc's content out of the EBOOT: the loading chant (upstream's 18 KB) and `loading.tga` baked from the player's disc | S (chant), R (`loading.tga`) | the EBOOT carries no disc bytes (`compressed_monk_tone` compiled out) |
| 3.6 | Stale lines fixed: `multiplayer_setup.md`'s `mp-recon` bake, `bake/README.md`'s sizes ("build 108 or later" and `make_bundle.sh`'s default paths: done) | M, MP | — |

## 4. Legal and attributions

| | Item | Owner | Done when |
|---|---|---|---|
| 4.1 | Sierra-III's licence: MIT, `psp/xmb/` carved out (public-prep) | Sheepdog01 | `LICENSE` on `main` |
| 4.2 | Microsoft's Game Content Usage Rules: the notice in README and in the release notes; the XMB stills the only Halo assets in the repo; nothing from the disc in any EBOOT (3.5) | M | the notice, and 3.5 |
| 4.3 | Upstream and borrowed work credited: `UPSTREAM.md` (the decomp, ChupathingyCE CC0, Invader GPLv3, the Media Engine library and its `LICENSE-mecore.md`, PPSSPP, pspdev), README's Credits (halo-ce-vita with BirchWoodGod's grant, Halo 64 CC0, Overpass OFL), each borrowed change's commit and review | M | a read of every `docs/research/` source against the Credits |
| 4.4 | Invader's GPLv3: the bundle carries the full source of its binaries and our patch | M | `source/` in every bundle (`build_tools.sh`: the patched tree as built, both patches); checked in the built bundle |
| 4.5 | The repo's history clean: no secrets, personal emails or game data (Sheepdog01's delta audit: none); the lab's name out (`docs/review_b121_me.md:135` at the tip); gitleaks in CI | M (public-prep) | the guardrail passes |
| 4.6 | Upstreaming stays by the project rule (small, understood, tested on ChupathingyCE, says it was written with Claude) | Sheepdog01 | — |
