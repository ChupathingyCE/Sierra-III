# Working in Sierra-III

Sierra-III is a native PSP-1000 port of Halo: Combat Evolved, built on a decompiled engine. Start with README.md for the layout and UPSTREAM.md for what it's built on.

## Rules

- **No game data, ever.** That means no maps, extracted tags, baked packs (`.map .pgeo .ptex .pstn .vram`), sounds or textures from the disc, in any commit, release, Actions artifact or log. Game data stays in Sheepdog01's local environment (Sheepdog01, 2026-10-15): it may move between his own machines on his own network, and never touches GitHub. `.gitignore` enforces the common cases. Before a push, check that every binary or large file is ours.
  - **One exception** (Sheepdog01, 2026-10-12): the XMB art in `psp/xmb/` may use stills of Sheepdog01's own gameplay footage (recorded from PC ChupathingyCE), under Microsoft's Game Content Usage Rules; the README carries their notice. Only the exported XMB images: never the raw captures or cutouts, nothing extracted from the game's files, no Microsoft logos.
- **No Sony firmware.** `flash0:` modules (e.g. `lowio.prx`) stay off the repo; describing them in docs is fine.
- **Test scaffolding goes in the lab repository, not here.** That covers run specs, workflows, run scripts and results. This repo holds only what the port needs to build, bake and be tested by hand. Sessions that work on the lab get its name and rules from its own CLAUDE.md; `docs/testing_pipeline.md` describes its shape.
- **Commit identity** (Sheepdog01, 2026-10-10): Claude writes the code and Sheepdog01 tests it on hardware, so commits are authored as `Claude <noreply@anthropic.com>`, with Sheepdog01 as co-author: `Co-authored-by: Sheepdog-01 <15330660+Sheepdog-01@users.noreply.github.com>` (that line keeps the commits on his GitHub profile). Commits before 2026-10-10 are `Sheepdog-01`'s; leave them as they are.
- **The newest build is the one to test** (Sheepdog01, 2026-10-14). A new build carries every fix and change of the builds before it, so it may replace one Sheepdog01 hasn't tested yet, and the open tests move to it. Never hold work back to wait on a test. Each shipped build is tagged `bNNN`, and its zip is added to the `build-archive` Release (a record, not for sharing). A build gets a Release of its own only when Sheepdog01 chooses to share it.
- **Speed work is pre-approved unless it takes something away** (Sheepdog01, 2026-10-13). Claude may pursue, ship and iterate on any speed-up without asking, provided it either restores the original look and feel or doesn't detract from it. That covers:
  - exact speed-ups;
  - restorations;
  - changes that aren't bit-exact but that play and look the same (shown by emulator side-by-sides and called out in the build's review). **This covers the simulation too** (Sheepdog01, 2026-10-30 13:01; it replaces 2026-10-14's "the simulation is rebuilt only exactly"): where exactness isn't needed to meet the goals, a simulation change may be "plays the same", proven by side-by-sides and an outcome census (encounters end the same way, as aicad3's was). Collision, the disc's tag indices and object slots stay as the host has them (multiplayer, below).

  A speed-up that **detracts** (draw distance, fewer effects, lower quality, anything a player would notice as worse) needs Sheepdog01's approval after an A/B. **The aim is to approximate the original's look and feel, not replicate it** (Sheepdog01, 2026-10-22): reaching 30/30 will take compromises, so consider every option. Clean wins (exact, or the same in play and look) go in freely; a compromise is priced like any lever, built off by default behind its keep word and shown to Sheepdog01 as an A/B, and one that moves a heavy map toward 30/30 outranks a clean win of a fraction of a millisecond. AI thinking less often is open to experiment (Sheepdog01, 2026-10-13), but it ships only after an A/B shows it breaks nothing. Look or behaviour changes made for other reasons than speed (the HUD's size, say) also stay Sheepdog01's call after an A/B, unless they are restorations. Every change ships behind a keep word, so one build can test both sides and any change can be switched off.

  **Approval never holds up the work** (Sheepdog01, 2026-10-13). Anything that needs Sheepdog01's call goes into the next build anyway, off by default behind its keep word, with its A/B in the build's tests. Speed work carries on around it.
- **Multiplayer work is open** (Sheepdog01, 2026-10-16; it had waited on the campaign's 30 ticks a second since 2026-10-14). The multiplayer session builds basic, fundamental multiplayer features on the newest build, in multiplayer-only files and its own `FIXES_` groups, while the campaign's speed work goes on beside it. Both share the lab and the two PSPs.
- **Multiplayer is left out for now** (Sheepdog01, 2026-10-21 22:27; kept 2026-10-30 12:54): until the campaign's ticks are in a good state, no `PSP_MULTIPLAYER=1` compile checks or mpcheck runs, and keeping the multiplayer build compiling isn't maintained. Spending time, tokens or memory on a mode that isn't being worked on is the cost this avoids. Compatibility is kept by design instead (below).
- **Compatible with multiplayer by design** (Sheepdog01, 2026-10-13; reworded 2026-10-30 12:54: the broad strokes must work with multiplayer, and co-op is the end goal). Halo's netcode isn't lockstep: the host decides, and clients are corrected toward it (`docs/multiplayer_recon.md`), so results that differ in the last bits don't matter to it, with one exception: clients report their own hits and the host checks them against its own collision, so **collision must give the host's answers** in a network game, or edge hits get refused. What must match the host: the game rules, the 30 Hz tick, the network version (29 since b135's engine pin; `psp_limits.h` moves with the pin), collision's answers, the disc's tag indices, and object slot numbers (nothing removed so that slots shift). So:
  - **Structural work is designed for the engine's three roles:** single player, co-op host, and client (a co-op client runs no AI and no level scripts: `docs/multiplayer_plan.md` §8). Memory is planned per mode, allocated when a mode starts and freed when it ends. A unified EBOOT is the end goal.
  - **Campaign-only vs single-player only.** A change that hasn't been built for network play yet ships campaign-only, and its row in the lab's `levers.tsv` says in one line what co-op would need (a `co-op:` note in its evidence), so the debt stays visible. A change that can never work in a network game (a different tick rate, as L15) is single-player only, with Sheepdog01's OK.
  - **While multiplayer is left out, this is kept on paper, not by building:** the code is reconciled in one pass when multiplayer work resumes. Once Sheepdog01 lifts the rule above, a change to shared code compiles into the multiplayer build again (`PSP_MULTIPLAYER=1`; the lab's mpcheck batch), and a change that would cost multiplayer to help the campaign goes to Sheepdog01 first, settled case by case.
- **Restoring the original game isn't a change** (Sheepdog01, 2026-10-11). Where the port draws or behaves differently from Halo on the Xbox (the ocean showing through the seabed or drawn white, missing or wrong textures), the fix goes on by default without waiting for an A/B. It still has a keep word that turns it off, and the build notes say what it restores. Departures from the original for speed are the "detracts" case above.
- **The main menu is a showcase** (Sheepdog01, 2026-10-16). ui.map is the hardware's high-fidelity target: it may take whatever quality the PSP can give it (textures at their full size and colour, model detail, extra passes), exempt from the frame-time limits on restorations, as long as nothing it does costs the other maps anything. Its quality comes from its own pack and from switches that take effect on ui.map only; each still has a keep word.
- **What "the original" means** (Sheepdog01, 2026-10-12): Halo on the Xbox, or the PC edition, which is close enough although some of its features and visuals are broken. Where the tags and the code can't settle how something should look, ChupathingyCE is the faithful source. Screenshots from a web search are not: the remasters and later versions draw very differently.
- **Proper fixes, not band-aids; the main CPU takes work back only as a last resort** (Sheepdog01, 2026-10-27 12:33: "band aid fixes are less ideal than proper solutions"). When work moved off the main CPU (the ME's jobs, the GE's lists) fails or runs long, fix it where it runs: find why it ran long, make the job fit its deadline, keep its waits bounded. Routing the hard case back to the main CPU, or switching a lever off, is not the fix. The existing fallbacks stay as the safety net for a failure. A stopgap ships only with Sheepdog01's OK, named as one, with its proper fix tracked (the lab's bugs.md or levers.tsv) and done next.
- **The hardware rules:**
  - The target is the PSP-1000: 333 MHz, 24 MB user RAM, 16 KB caches. Never rely on overclocking.
  - Write the CPU's cache back before the GE, the Media Engine or DMA reads what it wrote.
  - Unaligned word loads fault on hardware (the emulator forgives them).
  - `double` is software: keep it out of hot paths.
- **How AI is used here** (Sheepdog01, 2026-10-13, measured against ChupathingyCE's generative-AI policy): Sheepdog01 decides what to build, tests every build on hardware, and is responsible for it; Claude writes the code, and every commit says so (the identity above). So that a person reviews every change:
  - **Each build ships with a review walk-through** (`docs/review_bNNN.md`): every change merged since the last build, its key lines and why they're there, short enough for Sheepdog01 to read and question before testing.
  - **Comments say why, not what, and not when.** No comment restates the code. Build history ("build 112: …") belongs in commit messages and build notes, not in the code: when a comment is touched, move the history out and keep the reason.
- **Upstreaming to ChupathingyCE** (Sheepdog01, 2026-10-13: a project rule; their `CONTRIBUTING.md`, "Using AI tools"): only small, self-contained changes; Sheepdog01 understands every line and can explain it in review; tested on ChupathingyCE's own desktop build, not only through the port; and the pull request says plainly that it was written with Claude. Nothing goes upstream that fails one of these.
- **Credit what we borrow** (Sheepdog01, 2026-10-14). A change whose code or idea comes from another project names its source, with the author, in three places: the commit message ("after halo-ce-vita's path state clear, BirchWoodGod"), the build's review section, and the README's Credits. The research notes in `docs/research/` say where each borrowed idea came from and under what terms (`research_vita.md` records BirchWoodGod's grant for halo-ce-vita; code from a GPL project is taken only with its author's grant, and only what that author wrote).
- **Evidence labels in docs:** [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. The emulator says *where* time goes; only the PSP says *how long*.

## Building

```
# engine: ChupathingyCE at the pin in UPSTREAM.md, configured once
python3 configure.py --no-game-browser
ninja build/linux/halo_msvc_semantics.h build/linux/platform_msvc_semantics.h

# the port (pspdev's bin on PATH)
HALO_REPO=<engine> PSP_OUT=<out> python3 psp_build.py --jobs 8

# or all of it, from a clean machine, into a release zip (docs/toolchain.md)
PSPDEV=<pspdev> tools/build_release.sh <out>
```

**How the build works:** `psp_build.py` mirrors the engine into `<out>/tree`. `psp_fixups.py` then applies anchored (old, new) text pairs to that mirror: `FIXES_*` are lists or dicts per build, written against ChupathingyCE (the only engine base since halo-ce-universal retired: `UPSTREAM.md`). Then it compiles (hot engine `-O2`, cold units `-Os`), links, sizes the heap from the program's end, and packs `EBOOT.PBP`. A link map (`halo.map`) is written every link. The engine checkout itself is never edited.

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
| `PSP_RECON=1` | The recon censuses (`modelcensus`, `sleepcensus`, `drawcensus`, `mebusN`; `docs/recon_134.md`); recon only |
| `PSP_NOINLINE=psp_ge,…` | Units whose path holds a listed word compiled without inlining, so the cache map names each helper; recon only |
| `PSP_POOLS_XBOX=1` | The Xbox's actor, prop, light and glow particle arrays and the game state as long as they need; for runs that measure the arrays' peaks |
| `PSP_METRICS=0` | Leaves out the per-map metrics (`metrics budget`, `hitches`, `hitch`, `io`, `heap` lines; `psp/src/psp_metrics.c`), on in every other build: set it for the final release only |
| `PSP_NO_COLD=1` | Everything at the hot optimisation level |
| `PSP_SHELF=1` | Builds the shelved experiments back in (`docs/shelf.md`) |
| `PSP_DITHERSHIFT=1` | Builds in the GE's dither moved each pass (keep words `dithershift`, `dithershiftall`; `docs/review_dithershift.md`): its tests sit in hot draw paths, so a default build leaves them out |
| `PSP_PGO_STRICT=1` | Refuses to link when the profile doesn't match the code (any function compiled without its counts): set it for every release build and its release check. Every PGO build lists them in `pgo_mismatch.txt` |
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
- **The lab's runners:** builds, bakes and emulator runs (many at once, as batches) can go to Sheepdog01's desktop through the lab repository's workflows. Its README describes them; maps stay on Sheepdog01's machines and never touch GitHub.
- **Hardware:** Sheepdog01 runs the build and sends back `halo/log/` (`log.txt`, `profile.txt`, `crash.txt`, shots). `tools/hwreport.py` reads the `hwprofile` lines.

## Installing (Sheepdog01's machine, fish)

`pspmount` mounts the Memory Stick as `$psp`. Unzip the build zip into `$psp`, then `umount $psp`. Rules for anything that touches the stick:
- Delete only within `$psp/halo`, `$psp/PSP/GAME/halo`, and the multiplayer build's `$psp/halomp` and `$psp/PSP/GAME/halomp` (Sheepdog01, 2026-10-10 and 2026-10-11).
- Leave ARK-4's files and `overconfig.txt` alone.
- Never format or wipe the stick.

To quit the game: HOME → ARK-4 Kommander → Exit.

**Logs come off the stick into `$HOME/psp/logs/`** (Sheepdog01's rule, 2026-10-10), one `.tgz` per run, named for the build and the test: `mkdir -p $HOME/psp/logs; tar czf $HOME/psp/logs/b114_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots`. Pull after each run: the last 20 launches' logs stay on the stick as `log/NNN_log.txt` (the `--exclude` leaves them out of the archive), so a missed pull can still be recovered. A network test's tarball also carries the server's `debug.txt` (Sheepdog01's rule, 2026-10-11), and the multiplayer build's log is in its own folder from mp4: `tar czf $HOME/psp/logs/mp4_test1.tgz -C $psp/halomp log -C ~/games/halo-server debug.txt`. Give each test its stop condition and an honest time: a bench that waits for game time runs slower than the clock at low frame rates.

## Docs

- `docs/` holds every write-up about the port: build notes, reviews, hardware reports, plans. Start with `rebuild_plan.md`, `optimization_ranking.md`, `chupa_rebase.md` and `psp_hardware_reference.md`.
- `docs/research/` holds the research behind the levers (the B-series source notes, the engine read-throughs). Cite it from a lever's doc rather than restating it.
- **GitHub is the home; the claude.ai project ("PSP Game Development") is the summary.** Its `living/` docs (`STATE`, `RULES`, `ARCHITECTURE`, `HARDWARE`, `TOOLCHAIN`, `BENCHMARKS`) say where things stand and point here for detail. A new write-up goes in `docs/` first; the project gets a copy only when it should be read in chat.
- Multiplayer: `multiplayer_setup.md` is the step-by-step (hotspot, ARK-4, server, stick, troubleshooting); `psp_wifi_hotspot.md` and `multiplayer_plan.md` are the reasons behind it.

## Slated to move to Sheepdog-01/Sheep-PSP-Tools

These are shared with the other PSP ports. For now they live here:
- the emulator harness (`tools/emu.sh`, `strictfilter.py`, `routestats.py`, `tools/ppsspp_local_patches.diff`);
- `tools/hwreport.py`, `tools/pcprofile.py`, `tools/cachemap.py`;
- `bench/`.
