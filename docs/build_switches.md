# Building Sierra-III and its build switches

Moved from CLAUDE.md on 2026-11-18 (the rules reconcile, Sheepdog01 2026-11-18 12:30). These are reference, not rules: CLAUDE.md points here. From a clean machine: `toolchain.md`.

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
| `PSP_DATA_LAYOUT=none` | The SDK's data order. By default (`hotg`) the tick's hottest small variables are packed into one run after the ME's `.data` block, by `psp/layout/data_<name>.txt` (lever HOTG, `docs/hotg.md`); only data addresses move |
| `PSP_DATA_SPREAD=1` | With `PSP_DATA_LAYOUT`: each placement on lines of its own, so the cache map names each one's misses; recon only |
| `PSP_TICK_STEPS=1` | Per-step tick timing; recon only |
| `PSP_RECON=1` | The recon censuses (`modelcensus`, `sleepcensus`, `drawcensus`, `mebusN`; `docs/recon_134.md`); recon only |
| `PSP_NOINLINE=psp_ge,…` | Units whose path holds a listed word compiled without inlining, so the cache map names each helper; recon only |
| `PSP_POOLS_XBOX=1` | The Xbox's actor, prop, light and glow particle arrays and the game state as long as they need; for runs that measure the arrays' peaks |
| `PSP_METRICS=0` | Leaves out the per-map metrics (`metrics budget`, `hitches`, `hitch`, `io`, `heap` lines; `psp/src/psp_metrics.c`), on in every other build: set it for the final release only |
| `PSP_NO_COLD=1` | Everything at the hot optimisation level |
| `PSP_ASSERTS_ON=1` | Keeps the fatal asserts (cold, logged to `debug.txt` when they fail); by default they're compiled out (X10, Sheepdog01 2026-10-27) |
| `PSP_SHELF=1` | Builds the shelved experiments back in (`docs/shelf.md`) |
| `PSP_WALKAPART=1` | Builds in option C's groundwork, the drawing walk kept from the tick's state (keep word `walkapart`; the tests `mevis`, `walkwrites`): its hooks sit in the tick's object marks and the particle loop, so a default build leaves it out |
| `PSP_MEVISLAND=0` | Leaves out the frame's visibility made on the ME (mevis's copies of the visibility code and the landing's hooks; keep words `mevisland`, on by default, and `mevislandcheck`; `docs/mevis_landing.md`) |
| `PSP_DITHERSHIFT=1` | Builds in the GE's dither moved each pass (keep words `dithershift`, `dithershiftall`; `docs/review_dithershift.md`): its tests sit in hot draw paths, so a default build leaves them out |
| `PSP_DRAWLIST=1` | Builds in the draw list box (`docs/box_draw_list.md`), off by default until it's cut over; keep word `drawlist` turns it on. Its first slice holds the GE's origin on an 8-unit grid (`drawlistcell2`, `4`, `16` for others) so an unchanged instance's words stay the same between frames |
| `PSP_PADPATH=1` | Builds in the pad path (`docs/pad_path.md`; keep words `pathrec`, `pathlog`, `padwatch`): a pad's route written each tick, and a replay measured against it. A bench tool whose hook sits in the tick, so a default build leaves it out |
| `PSP_SNDSTREAM=1` | Builds in the long sounds' stream (SNDSTREAMLONG, `docs/plan_sndstream.md`; keep words `sndstream`, `sndstreamcheck`): its test sits in the mixer's block decode on the ME and its fields in every voice's packets, so a default build leaves it out |
| `PSP_MUSIC=1` | Builds in the music path (keep word `musicstream`, bake `--music-stream`; `docs/plan_music_stream.md`): Tremor and its buffers cost ~110 KB of heap, so a default build leaves it out until it's on by default |
| `PSP_CLAIMFILL=0` | Leaves out `memcpy`'s and `memset`'s wrappers that claim whole destination lines with `cache 0x18` instead of fetching them (lever CLAIMFILL, `docs/review_claimfill.md`). By default (since the rebase's recount, 2026-11-11) they're built in and on: keep word `noclaimfill` turns them off at launch, `claimcheck` is the PSP's proof; the old `claimfill` word does nothing |
| `PSP_AITICKOLD=1` | Builds in AITICKOLD (keep word `aitickold`, with `aime2a`; `docs/aime_me.md`, "AITICKOLD"): a compromise, the thinking for a frame's second tick made on the ME in the same job as the first's, from the world one tick older. Its clock hook sits in `game_time_get`, so a default build leaves it out |
| `PSP_PGO_STRICT=1` | Refuses to link when the profile doesn't match the code (any function compiled without its counts): set it for every release build and its release check. Every PGO build lists them in `pgo_mismatch.txt` |
| `PSP_FIXUPS_CHECK=1` | Lists every fixup anchor that no longer matches, and builds nothing |
| `PSP_DEADDEBUG=0` | Keeps the game's developer tools (the AI's, collision's and caches' debug drawing, the render-debug cache, the profiler and its frame marks, the collision log's counting). By default (lever DEADDEBUG, `docs/deadcode.md`) every unit gets ChupathingyCE's `HALO_CONSOLE_NO_DEBUG_TOOLS` and they leave the link: heap +216 KB on the shipped recipe (+248 with DEADXBOX), play and both seeds identical; their hs commands answer "not in this build". A `PSP_DEBUG_TOOLS=1` build always keeps them |
| `PSP_DEADXBOX=0` | Keeps the Xbox rasterizer's level passes behind skipped_passes' early returns and the transparent draw's gate (lever DEADXBOX, `docs/deadcode.md`, class 2). By default the GE-active test there is a constant (they're only reached inside a window, after the GE starts) and that Xbox code leaves the link; the keep word `transparent` then does nothing |
| `PSP_DEADCODE_INLINE=1` | The sleep's debug reports inline in psp_sleepy_update and psp_object_checksum, and the collision usage log's empty calls kept: the old layout, the other side of SLEEPDBG's A/B (by default they're cold functions and no calls) |
| `PSP_STACKHOT=0` | The tick's five collision feature lists on the stack, as the engine has them: the other side of STACKHOT's A/B. By default (`2`, `FIXES_STACKHOT2`) two shared lists come from the game's partition and the main thread's stack is 88 KB smaller, so the heap is as before; `1` is v1's five statics (~220 KB off the heap, which slowed the streaming stretches' drawing) |
| `PSP_ACCLEAF_OLD=1` | The hottest lookups (object_get, object_header_block_get, object_get_and_verify_type, datum_get) keep their keep tests, so `objallold`, `hdrblkold`, `notypehdr` and `dgfastold` work: the other side of ACCLEAF's A/B (by default the tests are gone) |
| `PSP_GU_PLACE=sdk` | pspgu's and pspgum's small variables left where the SDK's link script puts them: the other side of their placement's A/B (by default they sit on the line below the ME's `.bss` block) |
| `PSP_FRAMEPTR=1` | The engine's units with the frame pointer they kept from the Xbox build: the other side of FRAMEPTR's A/B. By default (since the RH5 bundle) they go without it, as psp/src always has: one more register and no save and restore of it |
| `PSP_SMALLDATA=0` | Every unit at `-G0`, as before the RH5 bundle: the other side of its A/B. By default (`8`) small variables (up to 8 bytes) defined in the unit or, under LTO, the link are read with one load from `$gp` (`-G8 -mno-extern-sdata -mgpopt`; the ME sets its own `$gp`). With its own profile and layouts (campaign-w162rh5, w162rh5_tgc90, hotg162rh5) the bundle measured c20 frame -3 to -9% and b30 -5 to -13% against the shipped tock, exact (rh5c) |
| `PSP_LTO=1` | Link-time optimisation for the engine's hot units (`source/`, not the `-Os` ones), with the profile: GCC inlines and prunes across units at the link. psp/src, the port's units and the cold units stay as they are. The layouts' placements of an LTO unit's sections name the link's own object; the build checks that LTO ran and that no LTO call skips a `--wrap`. `PSP_LTO_UNITS=ai,objects` limits it to those directories. Research (lever LTO) |
| `PSP_INTERPOLATION=1` | Builds render interpolation back in (`render_interpolation.c`, keep word `interpolation`). By default it stays out: the PSP never draws between ticks, and `psp/src/psp_nointerp.c` answers its calls as interpolation off does, the direct camera kept (23 KB of code and statics) |
| `PSP_CONFIGHELP=1` | Keeps the settings' help text (port_config.c's comment for each setting and section, written into a new `config.toml`). By default the campaign build leaves it out (~55 KB, CONFIGHELP); the settings, their defaults and environment names and the reading are as before. The multiplayer build always keeps it |
| `PSP_CONFIGFILE=1` | Keeps `config.toml` in the campaign build (its reader, writer and tomlc17). By default the campaign build reads and writes none: every setting is its default or the boot's environment, and the keep words stay (FIXES_V091A `port_config.c#nofile`). The multiplayer build always keeps the file |
| `PSP_BOXPOSING=0` | Leaves out the posing box (`docs/box_posing.md` §6c, §1b; `FIXES_BOXPOSING`): the engine's walk, the old-only side of the box's two-build pair. By default the campaign build has it: every object's node matrices and bounding sphere made by `psp/src/box_posing.c` in place of the engine's walk, a living biped making only the nodes its standing readers take (means B); keep word `boxposeboff` makes every node (means A), `boxposebcheck` is its check; the old `boxposeb` word does nothing. The multiplayer build leaves it out unless `PSP_BOXPOSING=1` |
| `PSP_FRAMEPIC=1` | Builds in the frame picture (`docs/frame_picture.md`, `psp/src/psp_framepic.c`; keep word `framepic`, `framepicnosw`): the engine's perf phases compiled in as box marks, each charged by halo_profile.prx's `halo_profile_box`. Its block (~30 KB, partition 2) is taken whenever the profiler runs in this build, so both sides of its A/B have it. Its tests sit in the tick and the frame, so a default build leaves it out |
| `PSP_VRAMCODE=<name>` | Builds in VRAMCODE (`docs/vramcode.md`, `psp/src/psp_vramcode.c`, `tools/vramcode.py`; list `psp/layout/vramcode_<name>.txt`, the ME's set `vramcode_me.txt`): keep word `vramroom` takes a room at VRAM 0xcc000 from the texture cache, `vramcode` the room with the main CPU's hot functions copied into it and the calls in main RAM that the ME can't make re-aimed. Its table is 16 KB of `.data`, so a default build leaves it out. A lever's three-way |
| `PSP_XMB_SND0=<file.AT3>` | Packs a local menu sound into the EBOOT (never commit it; see `psp/xmb/README.md`) |

**Menu art:** the XMB icon, overlay and background come from `psp/xmb/campaign/` or `psp/xmb/mp/` (by `PSP_MULTIPLAYER`); `psp/xmb/README.md` has the sizes.

**Keep words:** `ms0:/halo/keep.txt` (or the launch menu) turns features on and off at launch. `psp_keep()` in `psp/src/psp_platform.c` reads them. `docs/optimization_ranking.md` (appendix B) lists the speed-related ones.
