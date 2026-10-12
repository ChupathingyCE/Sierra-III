# Working in Sierra-III

Sierra-III is a native PSP-1000 port of Halo: Combat Evolved, built on a decompiled engine. Start with README.md for the layout and UPSTREAM.md for what it's built on.

## Rules

- **No game data, ever.** That means no maps, extracted tags, baked packs (`.map .pgeo .ptex .pstn .vram`), sounds or textures from the disc, in any commit, release or log. `.gitignore` enforces the common cases. Before a push, check that every binary or large file is ours.
- **No Sony firmware.** `flash0:` modules (e.g. `lowio.prx`) stay off the repo; describing them in docs is fine.
- **Test scaffolding goes in Sheepdog-01/lab-repo, not here.** That covers run specs, workflows, run scripts and results. This repo holds only what the port needs to build, bake and be tested by hand.
- **Commit identity** (Sheepdog01, 2026-10-10): Claude writes the code and Sheepdog01 tests it on hardware, so commits are authored as `Claude <noreply@anthropic.com>`, with Sheepdog01 as co-author: `Co-authored-by: Sheepdog-01 <15330660+Sheepdog-01@users.noreply.github.com>` (that line keeps the commits on his GitHub profile). Commits before 2026-10-10 are `Sheepdog-01`'s; leave them as they are.
- **Finish testing a build before making the next one.** Each shipped build is tagged `bNNN`, and its zip is added to the `build-archive` Release (a record, not for sharing). A build gets a Release of its own only when Sheepdog01 chooses to share it.
- **Changes that alter what the player sees or how the game behaves are Sheepdog01's call, one at a time, after an A/B.** Exact speed-ups don't need that. Every change ships behind a keep word so one build can test both sides.
- **The hardware rules:**
  - The target is the PSP-1000: 333 MHz, 24 MB user RAM, 16 KB caches. Never rely on overclocking.
  - Write the CPU's cache back before the GE, the Media Engine or DMA reads what it wrote.
  - Unaligned word loads fault on hardware (the emulator forgives them).
  - `double` is software: keep it out of hot paths.
- **Evidence labels in docs:** [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. The emulator says *where* time goes; only the PSP says *how long*.

## Building

```
# engine: ChupathingyCE at the pin in UPSTREAM.md, configured once
python3 configure.py --no-game-browser
ninja build/linux/halo_msvc_semantics.h build/linux/platform_msvc_semantics.h

# the port (pspdev's bin on PATH)
HALO_REPO=<engine> PSP_OUT=<out> python3 psp_build.py --jobs 8
```

**How the build works:** `psp_build.py` mirrors the engine into `<out>/tree`. `psp_fixups.py` then applies anchored (old, new) text pairs to that mirror: `FIXES_*` are lists or dicts per build, and `Either(universal=…, chupa=…)` covers the two engine bases. Then it compiles (hot engine `-O2`, cold units `-Os`), links, sizes the heap from the program's end, and packs `EBOOT.PBP`. A link map (`halo.map`) is written every link. The engine checkout itself is never edited.

**Build switches (environment):**

| Switch | What it does |
|---|---|
| `PSP_GAME_OPT=-Os` | The `-Os` variant of the hot code |
| `PSP_MAP_CHECKS=1` | Keeps ChupathingyCE's per-access map checks |
| `PSP_DEBUG_TOOLS=1` | Debug-only instruments (e.g. `meclassify`) |
| `PSP_MULTIPLAYER=1` | Builds the multiplayer units back in |
| `PSP_BIG_RAM=1` | 64 MB recon variant; never for play |
| `PSP_TEXT_LAYOUT=coldfirst` | Hot/cold code layout |
| `PSP_TICK_STEPS=1` | Per-step tick timing; recon only |
| `PSP_NO_COLD=1` | Everything at the hot optimisation level |
| `PSP_FIXUPS_CHECK=1` | Lists every fixup anchor that no longer matches, and builds nothing |
| `PSP_XMB_SND0=<file.AT3>` | Packs a local menu sound into the EBOOT (never commit it; see `psp/xmb/README.md`) |

**Menu art:** the XMB icon, overlay and background come from `psp/xmb/campaign/` or `psp/xmb/mp/` (by `PSP_MULTIPLAYER`); `psp/xmb/README.md` has the sizes.

**Keep words:** `ms0:/halo/keep.txt` (or the launch menu) turns features on and off at launch. `psp_keep()` in `psp/src/psp_platform.c` reads them. `docs/optimization_ranking.md` (appendix B) lists the speed-related ones.

## Testing

- **Emulator:** PPSSPP at `tools/ppsspp_BASE_COMMIT.txt` with `tools/ppsspp_local_patches.diff`, built as `PPSSPPHeadless`. It gives you:
  - `PPSSPP_FAT=1`: PSP-1000 memory;
  - `-i`: the strict interpreter (range, unaligned and FPU checks);
  - `PSP_DCACHE` / `PSP_ICACHE`: the cache model, with exact tick counts.

  `tools/emu.sh LABEL` does one run. It is driven by environment variables (map, keep words, input script, route, strict mode, wall time) and summarised by `tools/strictfilter.py`. The Pelican flight into b30 is the fair A/B stretch; beach battles vary ~10% between runs.
- **The X3D runner:** builds, bakes and emulator runs can go to Sheepdog01's desktop through lab-repo's workflows. That repo's README describes it; maps never leave that machine.
- **Hardware:** Sheepdog01 runs the build and sends back `halo/log/` (`log.txt`, `profile.txt`, `crash.txt`, shots). `tools/hwreport.py` reads the `hwprofile` lines.

## Installing (Sheepdog01's machine, fish)

`pspmount` mounts the Memory Stick as `$psp`. Unzip the build zip into `$psp`, then `umount $psp`. Rules for anything that touches the stick:
- Delete only within `$psp/halo`, `$psp/PSP/GAME/halo`, and the multiplayer build's `$psp/halomp` and `$psp/PSP/GAME/halomp` (Sheepdog01, 2026-10-10 and 2026-10-11).
- Leave ARK-4's files and `overconfig.txt` alone.
- Never format or wipe the stick.

To quit the game: HOME → ARK-4 Kommander → Exit.

**Logs come off the stick into `$HOME/psp/logs/`** (Sheepdog01's rule, 2026-10-10), one `.tgz` per run, named for the build and the test: `mkdir -p $HOME/psp/logs; tar czf $HOME/psp/logs/b112_ceiling.tgz -C $psp/halo log`. `log.txt` is wiped at every launch, so pull it after each run. A network test's tarball also carries the server's `debug.txt` (Sheepdog01's rule, 2026-10-11), and the multiplayer build's log is in its own folder from mp4: `tar czf $HOME/psp/logs/mp4_test1.tgz -C $psp/halomp log -C ~/games/halo-server debug.txt`. Give each test its stop condition and an honest time: a bench that waits for game time runs slower than the clock at low frame rates.

## Docs

- `docs/` holds every write-up. Start with `rebuild_plan.md`, `optimization_ranking.md`, `chupa_rebase.md` and `psp_hardware_reference.md`.
- Copies also live in Sheepdog01's claude.ai project ("Game Dev and Programming", `claude/*.md`).

## Slated to move to Sheepdog-01/Sheep-PSP-Tools

These are shared with the other PSP ports. For now they live here:
- the emulator harness (`tools/emu.sh`, `strictfilter.py`, `routestats.py`, `tools/ppsspp_local_patches.diff`);
- `tools/hwreport.py`, `tools/pcprofile.py`, `tools/cachemap.py`;
- `bench/`.
