# How the project tests: the shape of the pipeline

The project's test setup is private (it runs on a machine holding game data), but its shape is simple enough to rebuild with your own pieces. This page describes that shape and the tools in this repository it uses. Labels as elsewhere: **[M]** measured on a PSP, **[E]** emulator, **[D]** read in code, **[U]** inference.

## Two loops

| Loop | Answers | Who runs it |
|---|---|---|
| **The emulator, in bulk** | Whether the game behaves, whether a change is exact, exact counts | Automated: dozens of runs a night |
| **The PSP** | *Where* time goes and *how long* things take (the emulator has no bus contention, no uncached-store cost, no real Memory Stick) | A person with the console, a few runs a day |

A change goes through the emulator first (does it work, is it exact, does play or the picture part), then to the PSP for every question of time, including where it goes: the emulator's split of a tick by step can differ from the hardware's several times over (2026-11-09: object updates 32% of the tick in the emulator, 49% on the PSP-3000, on the same build and replayed fight).

## The automated loop

```
 a private repo (scaffolding)          a desktop that holds the maps
 ├─ workflows ───── dispatch ───────►  self-hosted CI runners (containers)
 ├─ run scripts                        ├─ build runner: compiles
 ├─ emulator patches                   └─ emu runner: emulator runs, bakes
 ├─ batch files, routes                        │
 └─ results branch ◄──── push logs, profiles, screenshots (never game data)
```

**The pieces:**
1. **A private repository for the test scaffolding**, apart from the port's: run specs, workflows, run scripts, emulator patches, and a results branch. The port's repository holds only what it needs to build, bake and be tested by hand.
2. **Self-hosted CI runners** (GitHub Actions) on a desktop that holds the maps and disc images. They run in rootless containers, pull jobs over outbound HTTPS (nothing connects in), and are registered only to the private repository. Two runners, so a compile never waits behind an emulator run: a build runner (6 threads) and an emu runner (10 threads, 16 GB).
3. **Mounts:** the maps and disc images read-only; a scratch folder per job; a folder for maps the bake makes, which stays on your own machines and out of every repository.
4. **Workflows**, each a thin wrapper around a script:
   - *build*: a commit to a release zip and its link map (`tools/build_release.sh`);
   - *test*: build, then one emulator run: map, keep words, init lines, an input script, a route, wall seconds, the strict interpreter, the cache models;
   - *batch*: many test runs in one job, up to 10 at once (below);
   - *bake*: a disc image to maps (`bake/`), kept on the machine;
   - *tags*: what a baked map's tags refer to (`tools/tagdeps.py`; names only).
5. **Every job reports back, even when it fails:** the results folder is made first, each step's output is logged into it, a job report says how each step ended, and the folder is pushed to the results branch. A remote session that can't read CI logs reads the results branch instead.
6. **The emulator**, built once per pinned PPSSPP commit and patch (named by the patch's hash, so a changed patch gets a build of its own) and cached on the runner.

**Rules that keep it safe:**
- Runs read only their checkout and the read-only mounts, and write only to scratch. No `curl | sh`, nothing outside the container.
- Results hold logs, profiles and screenshots, never game files (maps, packs, textures, sounds).
- The runner trusts its repository: anything committed there can run on the desktop, so it stays private.
- Secrets are never printed in a run step: step output goes to the results.

## Many runs at once

One job can play several emulators side by side: each headless PPSSPP needs about one thread, and each run gets its own memory-stick folder and results folder (`tools/emu.sh` takes both as settings).

- **What made it work:** PPSSPP keeps the PSP's RAM in a sparse file in `/dev/shm`, and a container's `/dev/shm` is 64 MB, about one PSP-1000's worth. A second instance runs it out and every instance dies with SIGBUS within a second. The project's emulator patch takes the RAM from `memfd_create` instead, which counts only against the container's memory limit. (In your own setup, `--shm-size=1g` on the container does the same.)
- **Runs side by side measure exactly what a run alone does** [E]: eight copies of one run, eight at once, played identically to each other and to the run alone, every tick's game state and every timing line the same. The emulator counts the PSP's time itself, so the host's load changes only how far a run gets in its wall time. At eight at once each got about 60% as far, so eight at once is about 4.8 times the work.
- **So compare runs by game time, never by wall time.**

## Making runs comparable

| Tool | What it does |
|---|---|
| keep word `fixedticks` | Every frame runs exactly two ticks, whatever the clock says, so play doesn't depend on how fast a build runs |
| keep word `fixedseed` | The random seeds start at constants at map start; sounds draw from a seed of their own |
| keep word `statecrc` | A hash of the game state after every tick, in the log |
| keep words `stateobjectsN`, `statewordsN`, `statefullN` | For 48 ticks from tick N: each object's hash, its play words, or one object's whole datum |
| `tools/statecmp.py` | Two logs compared tick by tick: the first tick the state, play, or each seed differs; `--objects` names the objects |
| `tools/route_from_log.py --timed` | A route from a PSP log: the emulator puts the player where the tester was at each game time, so a run replays the tester's path |

Two runs of one build with the same keep words play identically for the whole level [E]. Two different builds, or keep words that change drawing, part ways after about 45 s of b30 (`b113_candidates.md` #25 tracks why).

## The deeper emulator modes

- **The strict interpreter** (`STRICT=1`): reports the unaligned and out-of-range accesses a PSP faults on and the JIT forgives. Slow: a strict run covers the first minute or two of a level.
- **The cache models** (`PSP_DCACHE`, `PSP_ICACHE`): the Allegrex's 16 KB data and instruction caches on every access, split by what the game is doing (tick, drawing, frame end), with misses attributed to functions and memory regions (`tools/cachemap.py`). This is where "drawing's instruction misses are spread thin over hundreds of functions" comes from.
- **The ME-strict check** (keep word `gemesim`): every system call made in code meant for the Media Engine, which can't make them.

## The hardware loop

1. A build is installed on the Memory Stick (`docs/toolchain.md`, step 6), with the launch menu's presets (`halo/presets.txt`) holding the tests' keep words.
2. The tester plays a fixed stretch (b30's opening bench: from the start to the first bunker) and pulls `halo/log/` afterwards.
3. `tools/hwreport.py` reads the hardware counters' lines; the build notes compare the stretches.

Each test has its keep words, a stop condition, and an honest time. A test that waits for game time runs slower than the clock at low frame rates.

## Setting up your own

The minimum that reproduces the automated loop:
1. A private repository with your workflows and run scripts.
2. A Linux machine with Podman (or Docker) and your baked maps.
3. A container image with pspdev, Python 3, ninja and cmake (to build the port) and PPSSPP's headless build dependencies (SDL2, GL headers, zlib, libzip).
4. A self-hosted runner registered to the private repository, running in that container, with the maps mounted read-only.
5. A workflow that checks out this repository at a commit, runs `tools/build_release.sh`, then `tools/emu.sh` with your settings, and pushes the log to a results branch.

Everything in this repository the loop runs is ordinary scripts: `tools/build_release.sh`, `tools/emu.sh`, `tools/strictfilter.py`, `tools/statecmp.py`, `tools/route_from_log.py`, `tools/cachemap.py`, `tools/hwreport.py`.
