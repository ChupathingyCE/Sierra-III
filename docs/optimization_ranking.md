# What to chase: every speed lever, ranked (2026-10-09)

This is every speed lever the project has measured, proposed, tried, shipped or set aside, from build 13 to build 110. It ranks the open ones by what they would buy on Sheepdog01's PSP-1000. It draws on all 40 Halo docs, the b110 code (keep words, build flags, fixups), b107's b30 hardware log, past conversations, and what the C&C port found. A separate check against the code, docs and log corrected 32 points before it was saved.

**Update 2026-10-10** (b108's A and D runs, b111, the b112 candidates): M1's `-Os` question is settled (E3: it loses); the capture rule in §2 is corrected; E1 is built as a b112 candidate, along with a second regression from the rebase (`fast_ftol` rounding through a double); b108 run 1's log gave two new levers (E26, placed-dead bodies, a b112 candidate; the converted-buffer sweep, in b111). The rest stands.

**Update 2026-10-10, later** (Sheepdog01: "significantly improve tickrate and frame rate… rather than chasing small gains"): the structural line is re-ranked and sized in **`big_levers.md`**. First: the Media Engine builds the GE's lists (the GE layer's CPU is 42–65 ms a frame on b30 [M]; a new lever, ahead of X3/X4), then VRAM residency (X1, M3 still decides), the next tick's AI on the ME, a profile-guided build (X17: its instrumented program doesn't fit the PSP-1000's layout), and the full tick/draw split (X16). b112 carries the instruments: the ceiling bench, `gedefer` (step 1 of the GE layer's move), `fixedticks` and `statecrc`.

**Update 2026-10-11** (b114's hardware runs and a day of recon with Sheepdog01): **aligning the workload to the pipeline is the priority** (`big_levers.md` §0): the memory pools, the VRAM swap (level vertices and the shared models in, most textures out), DMA's role, and three measurements to size it (`gereads`, `gebusy` by layer, `vramtex`/`vrammodels`). Streaming has its own doc and levers S1–S11 (`streaming.md`), which take in several of §5's H levers. The tick by call is `big_levers.md` §8, the VFPU's ranking §9. New levers N1–N5 are in §7. The next build's list is `b115_candidates.md`.

The levers come in three kinds:
- **Exact:** the same game and the same picture, only faster. These need no decision from Sheepdog01.
  - **≈ exact** marks levers whose float results differ only in the last bits, like the VFPU matrix product shipped in b37. They are listed with the exact ones, flagged so Sheepdog01 can veto.
- **Behaviour or look:** they change something you can see or feel, so Sheepdog01 decides each one after an A/B (his rule: "case by case… test and evaluate").
- **Hitches:** one-off stalls (loading, checkpoints, stick reads), not steady cost.

**Labels:**
- [M] measured on Sheepdog01's PSP-1000.
- [E] emulator (PPSSPP, often its cache model) or an estimate.
- [D] read in the code or the data.
- [U] inference.
- [E→U] an emulator share scaled to the PSP. These are rough, and many shares date from before b106, so they overstate what's left of costs since reduced.

Build numbers point to their docs (`build_bNNN.md`, `hardware_bNNN.md`); "v7", "v9" and so on are `halo_bench` runs (`psp_hardware_reference.md`). Scenes: BG = Blood Gulch, HEH = Hang 'Em High, Library = The Library (c20). "b107 log" is Sheepdog01's run `b107_b30_hw` (b30's beach, with `hwprofile`).

## The short version: what to chase, in order

1. **Measure** (Sheepdog01's runs, §2):
   - b108's optional A/Bs (M1);
   - b110 on hardware (M2);
   - benchrun switch 8 (M3).

   They size or reorder half the list.
2. **The next build: exact and quick:**
   - E1, cold asserts (a rebase regression);
   - E2, the lean play build, and E3, optimisation levels, if M1 agrees;
   - E6, geometry-cache lookups;
   - the cheap hitches H5–H7, H9 and H10.
3. **The structural line:**
   1. X3, the Media Engine hand-over fix;
   2. X4, AI rays on the ME;
   3. X1, VRAM residency, shaped by M3;
   4. X2, precompiled lists (which take in X6, per-part state);
   5. X16, the tick/draw split.

   X5 (data-oriented loops) and X9 (collision walks) run alongside.
4. **Behaviour A/Bs for Sheepdog01** (§6): B1 perception rate (the biggest), B2 `sceneryhalf`, B3 far machines.

---

## 1. The gap, in numbers

**The reference scene is b30's beach on the PSP:**
- **Tick:** 61.7 ms (b107 [M]).
- **Ticks a frame:** 1.93 in b106 [M]; the b107 log gives 1.91 over the run and 1.98 in the fight.
- **The frame's own work (drawing):** 129 ms (b107 [M]).
- **A displayed frame:** 3.86 fps ≈ 259 ms (b107 [M]). Computed as 1.93 × 61.7 + 129, it comes to ≈ 248 ms [U]; the conversions below use that.
- **Over the whole b30 run:** 9.0 fps and 15.0 ticks/s; the facility runs 6–12 fps.
- **The Library (b106 [M]):** tick 32.8 ms, 7.45 fps.

**What the targets need:**
- **30 ticks/s at the 15 fps floor:** two ticks plus one frame's drawing in 66.7 ms. The beach today needs 2 × 61.7 + 129 ≈ 252 ms, **about 3.8× too slow**.
- **30 fps:** one tick plus drawing in 33.3 ms. Today that is 61.7 + 129 ≈ 191 ms, **about 5.7× too slow**.
- **The tick is the gate.** Even if drawing cost nothing, a tick over 33 ms can't make 30 ticks/s. The beach needs about **29 ms off the tick**.

**Where the time goes on the beach (PSP):**
- **Tick, b107 log fight windows [M]:**
  - `objects_update` 34–60 ms a tick; bipeds 21–47 ms a tick for 24–34 of them.
  - `ai_update` 12–23 ms a tick.
  - Quieter stretches: objects ~19–23 ms, AI ~4 ms.
- **Drawing, b107 log [M]:**
  - `rasterizer_model_draw` 20–95 ms a frame.
  - The GE renderer's CPU is 24–85 ms a frame, median 55 ms.
  - Its parts:
    - model geometry 6.8–30.3 ms;
    - model bind/state/lighting 1.7–11.0 ms;
    - two-sided and change colour 1.3–11.5 ms;
    - level passes 1.7–8.6 ms;
    - level classification 0.5–3.6 ms;
    - lights up to 14 ms.
  - The GE itself is busy only about a third of the frame, so **the CPU is the limit**.
- **Memory waits (b107 [M]):**
  - 54% of the tick's cycles and 59% of drawing's wait on memory.
  - **Two thirds of the misses are instruction fetches.** The tick's hot code is ~59 KB against a 16 KB instruction cache [E].
- **The GE's own reads double the CPU's miss cost.** The ceiling on what VRAM residency could return is ≈ **36 ms of drawing a frame** [U, from the GE-serial wait shares].

**Converting shares to ms** (all [U]):
- 1% of tick instructions ≈ 0.62 ms.
- The tick waits ≈ 21 ms on instruction misses and ≈ 12 ms on data misses.
- Drawing waits ≈ 47 ms on instruction misses and ≈ 29 ms on data misses. 1% of drawing's data misses ≈ 0.3 ms.

**The honest sum.** The exact tick levers below add up to roughly **8–16 ms**, and most of those numbers are ceilings. That doesn't close the beach's 29 ms. Closing it takes four things together:
- the structural work: the ME as a second core, the tick/draw split, data-oriented loops;
- the instruction-cache side: code size, layout, cold asserts;
- some behaviour levers;
- drawing brought down so two ticks fit.

The Library and the facility are much closer.

---

## 2. Measure first: the runs that reorder this list

Each of these is cheap and decides a lot. Rules for every A/B:
- Run the same stretch twice with `hwprofile`.
- Use no screen capture. RemoteJoyLite's USB DMA doubled CPU miss cost on halo_bench; in the game it cost about 2% of b30's flight frame (b108's A against run 1 [M]). Runs still go without it.
- Remember that beach battles vary ~10% between runs of the same build. The Pelican flight is the fair comparison in the cache model.

| # | Run | What it decides |
|---|---|---|
| M1 | **b108's optional A/Bs:** `sceneryhalf`, the `-Os` zip, `noscratch`, `notimers`; `vfpuskin` (low priority). **The `-Os` zip is done** (E3); the rest carry over to b111, whose keep words are the same | B2, E3, the scratchpad's worth, E2, E24 |
| M2 | **b110 on hardware**: b30 start to finish, heap line watched | The base for everything after; whether the heap fits |
| M3 | **benchrun switch 8** (level vertices in VRAM, the 9th entry) against switch 3 (GE skips the level) | Whether the GE's cost is bytes or scattered fetches. Gates X1, the biggest drawing lever |
| M4 | **`machinesleep` A/B** on The Library | E4; machines cost 7.5 ms a tick there [M] but the only A/B was in the emulator |
| M5 | **The b30 drawing steps the b107 log lacks**: the slant-bucket sort; a `notimers` run for true totals | E10, E2 (the b107 log already holds the other parts) |
| M6 | **`nomeaudio` at b30's voice counts** | What the ME mixer really saves (only measured at ~1 voice) |
| M7 | **`stickparallel` A/B** | Whether the stick lock still earns its waits |
| M8 | **PSP sampler + worst-case campaign sweep** (rebuild pillar 5) | Label-free profiles; "steady 30 ticks everywhere" measured, not assumed |
| M9 | **Bus counters** CNTBUSACCESS / CNTLOCALBUS in `halo_profile.prx` | Attributes bus contention to the GE directly |

---

## 3. Exact levers, quick (mostly S cost), ranked

Mostly a day or less each. Savings are at the b30 beach on the PSP unless another scene is named.

| # | Lever | Saves | Status | Notes |
|---|---|---|---|---|
| E1 | **Restore cold asserts on ChupathingyCE** (`PSP_ASSERTS_COLD` is inert since the rebase) | ≤ 1.1 ms a tick + ≤ 1.4 ms a frame [U, ceiling] | **Built: b112 candidate** (`b112_candidates.md` §2) | b110's link map: 3 `.text.unlikely` sections against 2,981 on the old base [D]; b112's: 2,951. ChupathingyCE's `cseries.h` had neither the cold nor the `PSP_ASSERTS_OFF` branch; one fixup ports both, and with them the same patch's float `fast_ftol` (libm's double `rint` was linked for it; now gone). Likely part of b110's +5% instruction misses. Gives E7 its cold half |
| E2 | **Lean play build**: `notimers` + `nooverlay` as play defaults, per-object timers and the game's own profiler/QPC reads out; a separate profile build keeps them | 3–10% of frame rate [E]; hardware unknown; **b114: −8 to −12% of the frame in the opening bench's flight, landing and beach [E]; on the PSP −2.6% in the opening cinematic [M, hardware_b114.md test 4]** | **built (b114): keep word `lean`**, off; Sheepdog01's call (2–3% against the fps readout and step timers) | The biggest cheap item if the PSP agrees; M1's `notimers` run sizes it. Frees 6.6 KB of scratchpad (E13). Sheepdog01's call on the fps readout |
| E3 | **Optimisation level per unit**: `-Os` vs `-O2` on hot code; move hot functions out of `-Os` units (`tag_get` in `cache_files.c`, `psp_trace.c`'s step marks) | **`-Os` on all hot code loses** | **Settled for the whole hot engine** (b108 D, `hardware_b108.md`) | `-Os` against `-O2` on the PSP: tick +30%, frame +8% over b30's flight; instruction misses per million cycles +9%, stores +14% [M]. `-Os` turns off GCC's alignment and hot-path block layout and inlines less [U]. The hot code stays `-O2`. Still open: moving single hot functions out of `-Os` units, `-O3`, `-funroll-loops`. **flags123:** the PGO build's counted units now align functions to 64 and loops to 32 and leave out four loop-copying transforms: tick instruction misses −10%, estimated tick −4.4% (b30's flight) and −4.6% (c20's opening), exact [E] (`review_b123_flags.md`); a smaller PGO hot set lost |
| E4 | **`machinesleep` on by default** (after M4) | Library ceiling ≈ 3 ms a tick [U]; b30 unknown | keep word, off (b102) | Only type-1 machines and doors can sleep; 60% are always-moving platforms |
| E5 | **Format lock off the tick path** (printf/strtod under one lock) | ≈ 2.5 ms a tick if the 4% share still holds [E→U] | measured, not built | The share may predate b106's keep-word cache; `printfcount` finds the busy call sites first |
| E6 | **Geometry-cache lookups, option B** (a lock keeps its entry; don't rewrite `last_frame`) | ≈ 1.2 ms a frame [E→U] | proposed (b109) | `entries` is the largest named table in drawing's data misses (4.0%) |
| E7 | **Hot/cold text layout** (`PSP_TEXT_LAYOUT`: hot order, and `coldfirst` after E1) | ≈ 0.7 ms a tick + 1.5 ms a frame [E→U] | in tree, unshipped | Measured on the old base: cold apart −3.2% I-misses, 16 KB hottest first −0.8% [E]. `coldfirst` has nothing to move until E1. PGO (X17) would supply the order |
| E8 | **Standing-biped shortcut** (cache the ground result for still bipeds) | ≈ 1.2–3.3 ms a tick [E→U, pre-b106 share] | measured, not built | 7–20% of sweeps are "still" [E]. Check that gravity's push doesn't change behaviour |
| E9 | **Per-vertex level classification** in the scratchpad, then the VFPU | part of 0.5–3.6 ms a frame on b30 [M, b107 log]; ~1–1.5 ms on BG [E→U] | proposed | The box test (b66) already removed most of it |
| E10 | **Slant-bucket sort cheaper on the CPU** | part of 3.8 ms a frame (BG) [M]; b30 unmeasured (M5) | proposed | "The renderer's most expensive CPU step" on BG |
| E11 | **Stencil bake cheaper paths** (fast-reject bitmap; pieces as one block; fewer pieces) | up to ~4 ms a frame (HEH) [E] | proposed | Target: under 1 ms, so the bake can be default everywhere. Path 3 is M cost. b30 unmeasured |
| E12 | **Skip the null-path model begin and transparent submit** | unknown on b30; the steps cost 8.2 and 5.4 ms a frame there [M, breadcrumb]; 0.2–0.6 ms on BG [E] | proposed (2026-10-02) | Breadcrumb times run until the next mark, so split the steps finer before sizing |
| E13 | **Scratchpad: `rasterizer_xbox_d3d_globals`** (2.2 KB) | ≈ 0.4–0.5 ms a frame [E→U] | proposed | Needs room: E2 frees it |
| E14 | **Display list in VRAM** | 0.4–0.8 ms a frame (BG) [M] | measured, not built | b30's list is bigger and won't all fit. Overflow handling first |
| E15 | **`cache 24` for whole-line writes** (memset, vertex copies, cut buffers, mixer output) | unknown; memset is 3.4% of instructions [E] | measured, not built | Half the cost of whole-line writes [M v9]. Only for fully overwritten, aligned lines |
| E16 | **Remaining double maths**: `periodic_functions` fmod (exact); `aiming_screen_apply`'s two `fmod`s a call (exact as `x - truncf(x)`; 2026-10-11); collision `fabs`, lift terms, `vehicles.c:661` (≈ exact) | < 0.5 ms a tick [U] | proposed | The `fabsf` sites round slightly |
| E17 | **VFPU trig** (`vsin` etc.) for the remaining tick trig (≈ exact) | 0.1–0.2 ms a tick [U] | proposed | Error < 4.8e-7; the last ~3 mantissa bits differ |
| E18 | **Frame pointers omitted** (`-fno-omit-frame-pointer` is on the game's units) | unknown | proposed | The crash handler dumps from `sp` and doesn't walk `$fp` [D] |
| E19 | **Keep-word asks cached in statics** (per model LOD, shadow, decal cluster, HUD draw, texture, particle) | small | proposed | Each ask is still a hash + strcmp + seqlock read |
| E20 | **VFPU context only on threads that use the VFPU** | unknown: 0 to ~3% of the CPU [U] | proposed | The audio thread wakes up to 10,000 times a second during ME jobs [D], and `psp_vfpu.c`'s wrapper gives every thread the VFPU. A halo_bench test of thread switches with and without the flag settles whether the kernel saves the registers each time |
| E21 | **Maths flags for the plain-C `psp/src` units** (`-fno-math-errno` etc.) | small | proposed | Drops the errno branch after `sqrt.s` in the GE code's hot loops |
| E22 | **Object render-state cache cap** (256, full on b30, c10, d20) | unknown | measure first | Objects past the cap may rebuild their lighting every frame [U] |
| E23 | **HUD bitmaps resident in VRAM** | unknown | proposed | No blocking texture load inside a frame |
| E24 | **`vfpuskin`** (bending corners near the eye on the VFPU) | unknown on the PSP; the emulator came out 2–4% slower [E] | keep word, off (b106) | 0 differing verdicts over 2.19 M corners [E]. A free PSP A/B: the cinematic, with marines beside the camera, is its best case (2026-10-11) |
| E25 | **Ranged write-backs** instead of whole-cache write-backs | frame end ≈ 0–0.1 ms a frame [M, b107 log]; the audio block's is untimed, likely ≪ 1 ms [U] | proposed | Small, as measured. Risk: a missed range brings back b39–40's rubber-banding |
| E26 | **Bodies placed dead sleep** like garbage corpses (`b112_candidates.md` §1) | b30's security room: 7 of 20 bodies updated every tick [M, b108 run 1]; ms unmeasured; levels dressed with bodies likely more [U] | **Built: b112 candidate** (`placeddeadawake`: off) | `units.c` bars set-dressing bodies from the garbage list, so b105's rule (garbage only) never offered them sleep [D]. The byte-identical rule still decides |

---

## 4. Exact levers, structural (M–L cost), ranked

These are the bigger ceilings. Most need a design and their own build.

| # | Lever | Saves | Status | Notes |
|---|---|---|---|---|
| X1 | **VRAM for what the GE reads**: level vertices (switch 8), then model vertices, then a visible-set cache refilled by DMA | ceiling ≈ 36 ms a frame [U]; 8–18 ms of BG frames [M] | switch 8 in tree; rest proposed | **The biggest drawing lever.** Texels are none of the cost [M]. VRAM is tight: worst views need 1.2–2.55 MB against ~1.2 MB free [D]. M3 decides the shape. Conflicts with ME jobs on the same data (the ME can't see VRAM) |
| X2 | **Precompiled display lists, model parts first**, then level clusters | target: `rasterizer_model_draw` 20–95 ms a frame [M]; saving unknown | proposed (rebuild step 4) | The renderer's CPU is "per-draw bookkeeping, not maths". Takes in X6 |
| X3 | **ME hand-over fix**: few large jobs, spin when the answer is due | ~320 µs a job [M b90]; ≤ 1.2 ms a frame for audio today [U] | proposed | **The prerequisite for every ME job.** A bare round trip is 1.16 µs [M v10]; the rest is sleeps and polls |
| X4 | **ME job: AI line-of-sight rays** against the level BSP | ≈ 2.8 ms a tick [E→U] | proposed (rebuild step 3) | Exact if the CPU waits for the batch; a tick-late variant is B5. The ME runs the same code at the same speed, and its misses don't slow the CPU [M] |
| X5 | **Data-oriented hot loops** (compact arrays for per-object walks) + `cache 30` prefetch ahead of them | ceiling 3.1–5.4 ms a tick [U]; prefetch 0.6–2.4 ms [E] | in progress (b106–b109 pieces) | Game state is 43% of the tick's data misses [E]. Prefetch must be placed by hand: the wrong distance makes it slower [M v9] |
| X6 | **Per-part state left out of the list** (bind, state, lighting once) | target on b30: bind/state/lighting 1.7–11 ms a frame [M, b107 log]; 8–12 ms for 127–220 parts in b113 Test 3 [M] | **built (b114): `statememo`** (texture binds the GE already has: 8–17% of binds left out [E]; model lighting), off. **PSP test 3: no change** (cinematic frame 195.6 ms both ways; 8% of binds and 71% of lighting sets left out, about 1% of the list's words) [M] | Also fewer dropped draws: 35.6 part draws a frame went to the size floor in the Pelican flight [M] |
| X7 | **Dynamic lights**: per-triangle selection around each light | target on b30: lights up to 14 ms a frame [M, b107 log]; Library 6.5–7.1 ms [M] | no design | The Library's rise from 2.6 to 7.1 ms (b105 → b106) was likely more cone draws [U]. GE hardware lights unexplored |
| X8 | **`object_connect_lights`**: re-link only the lights that moved | ≈ 0 [E] | **Parked: nothing to win** | It isn't lighting: it re-links an object's attached point lights to the map's clusters, and returns at once for an object with none (`_object_has_attached_lights_bit`) [D]. By exact counts it is below the top 40 functions of the tick on all six levels (tickmap118, lab 38359927909) [E]; c20's 10 machines with lights cost 0.175 ms a tick between them (lab 38526124924) [E]. The 8% (and a "4.9 ms" target) was a step share, which counts the work after the call up to the next step mark [U] |
| X9 | **Collision walks**: cache-line BSP layout, per-leaf packed tables, compact broad-phase, shared feature gathers, BSP top in the scratchpad, VFPU plane tests | ≤ 1.6–2 ms a tick [U]; feasibility said 4.5–9 ms [E] | proposed | Biped physics is the tick's biggest bucket. Window space after the b108 bake: b40 has 61 KB spare, a50 0.30 MiB |
| X10 | **Assert audit**: compile out pure checks (`valid_real_*`, `unit_verify_vectors`, lookup asserts) | 0.6–1.9 ms a tick [E→U] | proposed | Up to 447 KB less code [D]. Needs E1's `PSP_ASSERTS_OFF` hook; some asserts have side effects |
| X11 | **ME classify + slant buckets, run ahead** for the visible set | ≤ 5.8 ms a frame (BG) [M bound] | proposed; the per-draw split was measured and rejected (§7) | Exact (0 differing answers over ~5 M vertices [M]). Needs X3; competes with X1 and the tick for the ME |
| X12 | **Baked per-cluster visible set**, or the **portal walk on the ME** | ≤ 5.5 / ≤ 2.5 ms a frame (BG) [M] | proposed / measured | Alternatives to each other and to cluster lists |
| X13 | **VFPU node matrices / posing** (quaternions with `vrsq`; render-side relative matrices; the aim blend, `aiming_screen_apply`) (≈ exact) | 0.3–1 ms a tick [E]; 0.5–2 ms in the flight counting the aim blend [U] (`big_levers.md` §9) | proposed | "The VFPU only helps arithmetic; the CPU's problem is memory." Watch alignment: unaligned `ulv.q` corrupts on the PSP-1000 |
| X14 | **Hot CPU tables in VRAM** (tag-instance table, BSP top) | ≤ 1.5 ms a tick [U] | proposed | A CPU miss to VRAM is 132 ns vs 220 [M v7]; competes with X1 for VRAM |
| X15 | **ME collision sphere walks** | ≈ 1 ms a tick [E→U] | proposed | After X4; hard to batch |
| X16 | **Tick/draw split with a snapshot** | enables ME tick jobs | proposed (rebuild step 5) | The structure that makes the ME a real second core |
| X17 | **PGO / LTO** | unknown | proposed | PGO also supplies E7's order. LTO's inlining grows code against the I-cache |
| X18 | **Raw GE list words written cached** instead of uncached `sceGu` stores | unknown; halo_bench v9's uncached store (~37 ns) bounds the words alone at 2–4 µs a part | **shelved (b114 test 2): the GE hung within 2 s on the PSP** [M], a line still in the CPU's cache when the GE read it [U]; the emulator can't see it (no caches). Worth at most ~1% of drawing (744 uncached stores per M cycles on the beach) [U]. `listcachedall` stays, off | 1,500–3,000 uncached stores per M cycles in drawing [M b47] |
| X19 | **GE bounding-box culling by cluster** + static scenery merged at bake | unknown | proposed | After X2 |
| X20 | **`path_state_new` clears only what it reads** | ≈ 0.26 ms a tick [U] | proposed | Needs an audit |
| X21 | **GE-native model vertex pack** (14–20 B vertices, 8-bit indices for small parts) | memory 2.63 → 1.15 MB [M-map]; speed likely small | proposed | levelcompact's null result argues against a bus gain |
| X22 | **Learned VRAM ranking for campaign-size packs** | unknown | deferred (b89) | Inert above 1.125× the caches or 1,024 entries |
| X23 | **Bus-arbiter priority** (CPU fills ahead of GE reads) | same ceiling as X1 | research (step 1 done) | Risky: a wrong value can hang the bus. halo_bench only |
| X24 | **Strict aliasing and other correctness flags** (today `-fno-strict-aliasing`, `-fwrapv`, …) | unknown | proposed | Risk: changing the decomp's semantics; needs validation across the code |

---

## 5. Hitches and streaming, ranked by stall size

| # | Lever | Stall it removes | Status | Notes |
|---|---|---|---|---|
| H1 | **Texture streaming on b30** (track textures 7,060 KB vs 2,752 KB of caches; 3,120 KB in b114) — now `streaming.md` S1–S7 | 681 waits, up to 12 untextured frames in a row (b107 [M]); 36.5 s of game-thread stick time, worst 872 ms (b106 [M]) | open problem | Needs memory or smaller textures; competes with B14's `geo2` for texture RAM. Predicted-resource prefetch from the tags is one idea |
| H2 | **Checkpoints write only changed pages** | ≈ 0.44–0.7 s per checkpoint [E→U]; freezes of 1.1–1.76 s [M] | measured, not built | First find why writes run at ~0.9 MB/s against the stick's 4.9 |
| H3 | **Geometry read-ahead**: warm at load → `geoasync` re-test → prefetch at natural points | 100–240 ms freezes, 13–28 a run (b86 [M]); worst 244–319 ms (Library) [M] | proposed / keep word off | Warm-up moves 260–295 ms into the load. `geoasync` trades a freeze for a pop-in (a look call) |
| H4 | **Read budgets per read, not per byte** | caps 200–319 ms read frames [M] | proposed | A read costs ~2 ms however small [M v10] |
| H5 | **Start-up and load log lines batched** (RAM ring + writer, flush on crash) | ~0.35–1 s a load [U] | proposed (C&C carry-back) | Every line goes open/append/close for ~20 s after launch [D]. Writer priority is an open question |
| H6 | **Skip the case-insensitive path scan on FAT** | ~0.1 s a load [U] | proposed (C&C: start-up 12.2 → 5.1 s [E]) | `platform_translate_path` opens directories per path part [D] |
| H7 | **Keep file handles; stop polling `teleport.txt`** | ≤ ~86 ms in the first minute; ~2 ms of stick time a second | proposed | Descriptors are scarce (~13 in all [M]) |
| H8 | **Animation prefetch by character type at spawn** | ≈ 0.47 s per 9 min, as ~2.2 ms hitches [E→U] | proposed | Pays only with async reads |
| H9 | **Diagnostics off the game thread** (`stack_report`, `psp_tick_account`) | ≤ ~2.8 ms per 30 s [U] | proposed | |
| H10 | **`lruv_debug_to_file` compiled out** (the sound cache's "blown" file) | per event unmeasured; the 52–64 ms is the b12 texture-cache write [U, analogy] | proposed; still compiled in | Fired 7 times in the b107 log on b30, more on d20/d40 |
| H11 | **Sound cache beyond 1,536 KB** (or in volatile RAM) | d40 failed loads 144 → 0 [E]; b30 96 failed [M] | proposed | A heap trade. Music out of the cache (own ring) and sound residency (hot set, contiguous permutations) are related |
| H12 | **Stick lock: keep or drop** | lock waits 59 s, worst 0.9 s (b101, before reader priority) [M] | A/B pending (M7) | Its cause (Library freezes) was found elsewhere |
| H13 | **Full caches' `allocate()` is O(n²)** | unknown | proposed | Hits the full sound cache every frame |
| H14 | **Music at 22 kHz** | sound is ~86% of stick reads [M] | bake shelved | Exact: the mixer already outputs 22,050 Hz (Sheepdog01) |
| H15 | **Pack layout for request size and locality** | — | measured rule | 64 KB+ reads run at ~16 MB/s; random costs +1.6–2 ms [M v10] |
| H16 | **Sleep/wake pass for the streaming caches** | steadiness after sleep | deferred (b89) | |
| H17 | **Object-lighting pack reads** | 43 reads, 161 ms over the whole b107 run [M] | — | Tiny in play; a texel cache isn't worth it yet |

---

## 6. Behaviour and look levers: Sheepdog01's call, after an A/B

| # | Lever | Saves | What changes | Status |
|---|---|---|---|---|
| B1 | **AI perception less often** (far actors at half rate; stagger + line-of-sight cache) | ≤ 6.8 ms a tick if every actor went half rate [U, pre-b106 share]; far actors only: smaller, unknown | reaction delay for far actors | **built (`aihalf122`): `aihalf`**, off; `aihalfdistN` (15 world units), `aihalfview`. Far actors (no target in range, no recent damage, not in a zoomed view) service no prop every other tick, staggered. Bungie already services one prop per actor per tick, non-enemies 8× less often [D]. `review_b122_aihalf.md` |
| B2 | **`sceneryhalf`** | 0.25–1 ms a tick [E→U]; the b109c model said −9% (disputed) | a change to sleeping scenery noticed up to a tick late | keep word, off; first on M1's list |
| B3 | **Far, unseen always-moving machines at a lower rate** | Library ≈ 2.2 ms a tick [U] | coarser off-screen platform motion | **built (b122): `machinesfar`**, off; campaign only (`review_b122_farrate.md`) |
| B4 | **Half-rate posing** for distant, at-rest, unseen objects | ceiling ≤ 8.6 ms, likely small [E→U] | "very low" | **built (b122): `posehalf`**, off; campaign only (`review_b122_farrate.md`) |
| B5 | **ME rays answered a tick late** (pipelined X4) | removes X4's wait: 17 µs a job on a50, 301 of 335 jobs done before the wait [M, lab 38567183309] | AI sees a tick late | **not worth building** (`review_b122_aihalf.md`); revisit only if a map's `ME: ring:` wait is large |
| B6 | **`noangle`** (slant buckets off) | 3.8 ms a frame (BG) [M] | the ground swims, cliff feet alias (b63/64) | keep word |
| B7 | **`noshadows`** | 1.33 ms a frame (BG) [M] | no blob shadows | keep word |
| B8 | **`nolightsplit`** | ~3 ms renderer CPU facing a floor [E]; the light pass ~3.1 ms a frame (b104 [M]) | undoes the flashlight fix Sheepdog01 asked for | keep word; no clean hardware A/B yet |
| B9 | **Draw distance per level** (far clip + fog, raised for cutscenes) | unknown; mainly GE time and VRAM fit; **b114 on b30: −5% of drawing at 60 world units, nothing at 100 [E]** | shorter view; enemies never engage from beyond it | **built (b114): `drawdistN`**, off. PSP: −3% at 60, about −6% in the approach at 40 [M]. **Sheepdog01 (2026-10-11): kept in the back pocket, off.** 40 looks fine but cuts b30's landmark structure; 45 would suit this section. It may suit the large maps (Two Betrayals, Assault on the Control Room) better. If it comes back, it'll be a value per level or per section |
| B10 | **Hover vehicles settle; scorpion idle frame** | small | parked bob/idle frame freezes | proposed |
| B11 | **Decal cap and lifetime; flares, weather, contrails off; particle collision LOD** | small since b106's cull and budgets | fewer effects | proposed |
| B12 | **`nodetail` / shorter `detailnear`** | unmeasured | less detail texture | keep words |
| B13 | **Fewer virtual sound channels** (73 → 32) | loops 2.3× smaller [E] | fewer sounds at once | proposed |
| B14 | **`geo2` default** (2 MB geometry cache) | frames missing a block 111 → 55 [E] | memory trade: less texture RAM | keep word; in every hardware run since b104 |
| B15 | **`assettrack` default** | Library pack 14.3 → 6.7 MB [M] | half-size lightmaps | keep word; in every hardware run since b104 |
| B16 | **`lightwhite`** | removes H17's reads (tiny) | objects lit as if in full light | keep word |
| B17 | **Fixed-interval pacing** (2/3/4 vblanks) as a per-map floor; real-vblank pacing | steadiness | even tick steps; tearing choice | proposed |
| B18 | **Baked vertex lighting instead of lightmaps** | frees VRAM; lightmap pass CPU | look | deferred |
| B19 | **Texture resolution per class** | memory → cache hits | blurrier where applied | proposed |
| B20 | **400 MHz** | "a clean 20%" [M v3]: tick 61.7 → ≈ 51 ms [U] | none; but Sheepdog01's line: **last resort only, never relied on** | not used |

---

## 7. Other open levers: small, unsized, or memory only

These are listed so nothing is lost. Each is in the merged notes with its sources.

**New, 2026-10-11** (found while mapping the frame, `frame_map.html`, and in b114's cache map and tick steps, `big_levers.md` §8). All exact:

| # | Lever | Size | Notes |
|---|---|---|---|
| N1 | **Tag lookups' globals in the scratchpad** (`global_tag_instances`, `cache_file_globals`) | ~10% of the tick's data misses [E; attribution U] | Every `tag_get` reads both, and they keep being pushed out of the cache |
| N2 | **Skip the Xbox shim's shader constants on the GE path** | 2.2% of drawing's data misses [E] | The engine's model path still calls the Direct3D layer's constant stores; check what the GE path needs from them |
| N3 | **A biped's per-tick shortcuts:** marker indices cached instead of looked up by name; checks and the skeleton skipped for a biped whose pose didn't change | 0.5–1.1 ms per biped a tick is the whole cost [M]; the name lookups alone run 278 M instructions over the bench [E] | Footsteps, attachments and weapons look markers up by name every tick |
| N4 | **Cluster re-links only when an object crosses a portal** | `sphere_intersects_cluster_portal`: 346 M instructions in the tick over the bench [E] | First check whether objects that barely moved redo the test |
| N5 | **The tick's `memset`s** | 238 M instructions in the tick over the bench [E] | Find the callers first. Large clears could skip work or use DMA in a burst; small ones stay on the CPU |

**Streaming and assets** are S1–S11 in `streaming.md`. **The VRAM swap and DMA** are in `big_levers.md` §0.

- **Tick:**
  - driven-vehicle physics (8–10 ms a tick on BG when driving [M]; a target, no design);
  - animation decoding on the ME;
  - `cache` line locks to pin hot lines;
  - ME eDRAM for ME-only data;
  - MIST safe tasks;
  - stopping the eDRAM background unit;
  - the per-call cost of the link-time wraps (the malloc caller-table scan, stick wrappers).
- **Drawing:**
  - ME skinning;
  - VFPU gum for GE matrix setup (~0.05 ms at BG's draw counts);
  - offline pre-split of large BSP triangles (≤ 8 wu);
  - the remaining null-D3D-path work ("all small");
  - ME particle raycasts.
- **Audio:**
  - skip `update_channels` for unchanged channels;
  - the ME mixer skipping silent blocks;
  - ME sound streaming or ADPCM decode.
- **Build:**
  - gp-relative small data (`-G8`; PRX relocation may forbid it);
  - upstream Allegrex GCC patches.
- **Memory, which feeds the caches above:**
  - **the converted-buffer table swept sooner** (in b111, `build_b111.md`; `sweeplate`: off): dead entries' skinning plans held up to about 1 MB of heap in b108 run 1 [M]; in the replay of that run the plans left on the heap fell from 994 KB to 176 KB [E];
  - game-state pools sized to measured peaks (~420–500 KB of heap);
  - debug-only data out (~0.45 MB);
  - the remaining Xbox-sized tables;
  - thread stacks and D3D dynamic geometry sized to peaks;
  - PC shims and zlib out;
  - unused Xbox rasterizer paths;
  - scanf and merged strings;
  - full-coverage dead-code work;
  - cheaper BSP formats;
  - streamed sound tags;
  - tag struct repacking;
  - hashed tag names;
  - 4-bit font glyphs.
- **Tools:**
  - exclusive-time zones (C&C's lesson: "a cost outside every zone looks like work in the zone around it");
  - sound profile sections;
  - a wider coverage set;
  - the deferred diagnostics fixes (b89);
  - a halo_bench ME-bandwidth test;
  - a hardware run with the clock unset;
  - the X3D self-hosted runner for parallel emulator A/Bs.

---

## 8. Settled: don't chase again

| Lever | Why |
|---|---|
| 20 Hz tick / changing the tick rate | "Not a tuning knob": the target is a steady 30 ticks/s (b87) |
| Render interpolation | Costs CPU and looks worse: 52–54 vs 37–40 ms a frame, more rubber banding (b87 [M]) |
| levelcompact (16-bit level vertices) | No gain three times (b87, b93, b102 [M]) and ground cracks |
| Texel levers (textures in VRAM by bytes, stand-ins, mip bias) | All within ±1 ms (b102 run 5 [M]); the learned VRAM set moved drawing stalls only 53.3% → 50.8% (b46 [M]) and left frames unchanged (b92 [M]) |
| GE overlapped with the next tick | Not until vertices live in VRAM: CPU misses +272% during GE RAM reads [M v8b] |
| Scheduling the GE around CPU phases | No quiet phase: every phase waits 34–59% on memory [M] |
| Soften / temporal blend | 7–8 ms a frame and the image rocked [M b78] |
| `PSP_ASSERTS_OFF` on its own | Some asserts have side effects; only after the audit (X10) |
| Inline `datum_get` everywhere | 48 KB of code, 64 KB of heap (b106) |
| ME classify split per draw | ~21 jobs × ~320 µs would cost more than the work [U, from b90's M]; run-ahead (X11) instead |
| Interleaving misses; hand-ordered loads | No miss overlap: two misses = 2 × 72 cycles [M v7]; ~10 cycles slip [M v9] |
| L2 cache control | Not on a PSP-1000 [M b46] |
| A thread stack in the scratchpad | Too big (b108) |
| 64 MB / `MEMSIZE=1` for play | The PSP-1000 is the target (Sheepdog01) |
| Shrinking the display list | b30 filled 384 KB; grown to 512 (b107) |
| sceSas + VAG bake; the sound bake | Superseded by the ME mixer; bake shelved (Sheepdog01: "sound isn't limiting performance") |
| Offline tessellation as a clipper remover | Can't remove the near-plane clipper (asset_baking §3.4) |
| Async geometry as the default | b71: frames of 2.5–5 s [M]; re-test only as `geoasync` (H3) |
| ARK-4 "Memory Stick Speedup" | A 4 KB read-ahead; nothing for 64 KB reads [D] |
| Encounter thinning | A content cut; last resort only |

---

## 9. Decisions and constraints that shape the ranking

- **Targets:**
  - 30 fps average, chased aggressively.
  - A steady 30 ticks/s at every point in the campaign; the lowest acceptable frame rate is whatever keeps that.
  - The original look and feel.
  - A structural rebuild around the PSP is accepted (`rebuild_plan.md`).
- **Behaviour or look changes are decided one at a time**, after an A/B, by Sheepdog01.
- **No overclocking past 333 MHz.** It is a last resort only; the port must work on any PSP-1000.
- **ChupathingyCE is the base** (b110, committed). Its map checks are compiled out ("trusted maps"; `PSP_MAP_CHECKS=1` restores them).
- **Measurement:**
  - The emulator answers *what and where*; the PSP answers *how long*.
  - Every change gets a keep word for its A/B.
  - Finish testing a build before the next one.
- **The rebuild order** (`rebuild_plan.md`, agreed 2026-10-09):
  1. ChupathingyCE;
  2. the b109 A/Bs;
  3. the ME's first job: the AI's rays, which fits "the tick is the gate"; renderer jobs come later;
  4. precompiled lists, models first;
  5. the tick/draw split.

  This ranking agrees, with E1 (a regression) and the quick exact batch slotted in before step 3.
- **VRAM is the contested resource.** X1, E14 (the display list), X14, E23, the texture cache and the learned set all want the same ~1.2 MB.

---

## 10. Open questions (numbers that disagree, statuses no doc settles)

1. **`sceneryhalf`'s size:** −9% of the tick in the b109c model vs −0.23 ms a tick (b107) and −30% of a 6.7% share. Only M1 settles it.
2. **Bytes or fetches:** levelcompact's null result vs "vertex reads are the bulk". M3 decides.
3. **ME hand-over:** 1.16 µs bare vs ~320 µs in game. Reconciled as mechanism (sleeps, polls, whole-cache write-backs); never measured with the fix in place.
4. **ME audio's real saving:** "no effect" at ~1 voice vs the CPU mixer's 1.6 ms per block at 6.4 voices [M]. M6 decides.
5. **Geometry cache default:** the code says 1 MB; every hardware run since b104 used `geo2` (2 MB).
6. **Checkpoint writes at ~0.9 MB/s** against the stick's 4.9 MB/s: small writes, the stick lock, or sound streaming alongside?
7. **The printf lock's 4% of the tick** may predate b106.
8. **Skipped decal draws on b30 (b106):** 135 a frame in `hardware_b106.md`, 850 in a code comment; both [M].
9. **Heartbeat/log writer priority:** above the game (prompt writes, steals game time) or below it (may make the game flush)?
10. **The emulator's heap against the PSP's:** replaying b108 run 1's own path (the lab's route `b30_b108run1_timed`) matches the copies' churn but carries 700–950 KB less heap in use, because the replay meets far fewer models (`build_b111.md`). Memory questions that depend on what the player looks at still need the PSP.
11. **The heap's 64 KB rounding** (`psp_build.py`) leaves up to 63 KB between the heap and the game state; about 140 KB stays free outside the heap during play [M]. Whether the heap can safely take more needs a look at what's placed outside it after start-up.

**Docs to correct (the code settles these):**
- `port_overview.md` lists double-buffered display lists as done. The code has one list, and the CPU waits at every frame end.
- The `converted[]` overflow, deferred in b89, was fixed in b93: `converted_sweep` forgets dead entries.
- The rebuild plan's beach line said "AI 8.5 ms a tick", which was The Library's figure. It is corrected there with the b107 log's figures.

---

## Appendix A. Done (shipped, by area)

One line each: the build it shipped in, then what it measured.

**Tick: objects, physics, AI**
- **Object sleep, byte-identical** (`awake` reverts): b19; rules extended b44–b106.
- **Fast checksum in spans:** b106.
- **Vehicle sleep:** b44/b89. Before it, 8 parked vehicles cost 4.7 ms a tick [M b39].
- **Corpse sleep:** b105, fixed b106. It targets the 116 biped updates a tick seen in b102 run 3 [M].
- **Cluster elements unlinked directly:** b107.
- **Dense sphere copy:** b107. 95% of tests reject without reading the object [E]. With b107's other items, the tick fell 72.3 → 61.7 ms [M].
- **`datum_new` without re-walks; pool verify off:** b108. With the scratchpad and slim layouts, tick D-misses −12% [E].
- **Frameless/cold lookups and the `tag_get` fast path:** b106.
- **Inline tag-block accessor:** b46/b66.
- **Trusted maps:** b110. ~45 K instructions a tick [E].
- **Debug tools off:** b105–b107.
- **Keep-word answer cache:** b106. Asks had been 6.6% of the game thread [E].
- **Sleeping condition variables:** b106. The reader thread's wakes had cost 5% of the CPU [E].
- **`_stricmp` fold:** b107.
- **Marker lookups remembered:** b109.
- **Single-precision maths:** b19. A double sqrt costs 2,390 cycles vs 31 for sqrtf [M].
- **VFPU matrix product:** b37.
- **Pad read once a poll:** b19/b105.
- **`verify_tag_reference` answered from the reference:** b66. HUD 1.75 → 0.93 ms [E].

**Drawing**
- **GE-native renderer:** b17–b21. The d3d8 path took 25.7–28 ms a frame drawing nothing [M].
- **Null-path passes skipped:** b14–b20. ~10 ms a frame on foot, ~31 driving [M b18].
- **Render interpolation off:** b19.
- **Classification once per vertex:** b19.
- **Frustum matched to 480:272; LOD at 272 lines:** b19.
- **GE skinning in ≤8-bone batches.**
- **In-place vertex conversion; level copies keyed by pack address:** b20/b68.
- **`converted[]` table forgets dead entries:** b93.
- **Swizzled CLUT8/CLUT4 textures, VRAM-first:** b20/b21. CPU misses +0–2% with VRAM textures vs +65–96% from RAM [M v8b].
- **16-bit buffers:** b17.
- **Lightmaps as a second pass:** b20.
- **HUD as GE quads:** b21.
- **Blob shadows:** b25.
- **Flare rays kept 3 frames:** b31/b44.
- **Portal walk budget:** b31.
- **Back-face culling:** b35.
- **Particle budgets:** b36–b41. Updates had cost 15–39 ms a frame under fire [M b35]. The budgets price the updates (a count); drawing particles costs by their screen coverage, not their count (a30's opening: 100–150 sprites a frame covering ~14 screens, 0.3 once away from the pod [E]).
- **Near-plane verdicts per batch:** b43.
- **Cached list writes with `cache 0x18`; stall updates batched:** b48.
- **Box pre-test, detail fade buckets, palette cache:** b66.
- **Stencil bake:** b87. A look fix that costs 5 ms on HEH [M].
- **Float colours:** b102. Frame CPU 11.0 → 9.5 ms [E].
- **Level list reserve:** b103.
- **Flashlight split:** b103. A look fix Sheepdog01 asked for.
- **Near-plane memo + six-plane verdicts:** b106.
- **Point-light search off:** b106.
- **Decal cull:** b106. Library decals 13.4 → 3.0 ms [M].
- **Lightning memset:** b106. `widgets_render` 6.4 → 0.5 ms [M].
- **Timer thinning:** b106.
- **Change-colour blend and palette cache:** b106/b107.
- **Geometry lookups by hash, then a 64-slot table:** b106/b108.
- **512 KB list, bones once a part, near-eye copies only when needed, size floor, rigid rows:** b107.
- **`model_cut` and profiler slots in the scratchpad:** b108.
- **No mirror window:** b108. 21.5 → 6.9 ms at the mirror spot [E].
- **Learned VRAM set saved in play:** b88/b89.

**Memory, build, hardware**
- **The level copies' arena, compaction, and the sooner sweep:** b111 (`build_b111.md`). b108 run 1 failed to place copies from game time 27,907 to the level's end, 5,290 materials undrawn [M]; the arena keeps the copies' churn off the general heap, and the sweep frees dead skinning plans. Awaiting its hardware run.
- **Hot engine `-O2`, cold engine `-Os`:** b38/b69/b110. `-Os` on the hot engine measured 30% slower on the tick (b108 D [M]).
- **Dead-code GC.**
- **Packed BSP/collision:** b68/b70.
- **`-G`, `-K`, `-M`, `-Y`, `-D`, `-Z` bakes:** all ten maps fit the window since b108.
- **Cache write-back rules:** b39–b44.
- **Bulk DMA kept out of the tick.**
- **Volatile partition caches.**
- **Campaign-only engine:** b107. Heap 5,376 → 6,016 KB.
- **Sound cache 1,536 KB:** b107.
- **Display list outside the heap:** b105/b107.
- **ChupathingyCE trims:** b110. Heap 6,016 KB.
- **ME mixer:** b87. Hands back the CPU mixer's 1.6 ms per 23 ms block [M].
- **ME profiling:** b88–b90.

**IO, audio, pacing**
- **Maps built for the PSP's tag address.**
- **One packed file per level.**
- **Reader thread above the game:** b106. 87 → 2.2 ms a stick call [M].
- **Stick lock:** b100.
- **Load trace only with `blackbox`:** b102. BG preload 9 → 1.2 s [M].
- **Log batched in play:** b19.
- **File-descriptor diet:** b105.
- **Animation page cache:** b106.
- **BSP switch without the debug fill:** b107.
- **Checkpoint page map:** b26.
- **Software mixer, then the ME mixer.**
- **Sound obstruction reuse:** b106/b107. Looping sounds 11.8 → 1.8 ms a frame [M].
- **Clock fixes:** b71/b75/b110. Took the game from under 1 fps to 25–29 fps [M].
- **Throttle sleeps instead of spinning:** b41.
- **vblank throttle on by default:** b90. It costs ~3.9 ms of padding at 21 fps [M b64] and fixes the warthog bounce.

**Tools:**
- halo_bench v1–v10;
- `halo_profile.prx`/`hwprofile`;
- benchrun;
- the emulator cache model (tick-exact since b110);
- step-tagged PC sampling;
- heartbeat lines;
- the crash handler;
- the coverage tool.

## Appendix B. Revert and check words

These words turn a shipped speed lever **off** again, for A/Bs and diagnosis:
- `awake`, `vehiclesawake`, `corpsesawake`, `slowchecksum`, `markersearch`, `poolverify`, `verifytags`;
- `fulllod`, `flaresexact`, `fullparticles`, `objectpointlights`, `bonesdirect`, `decalall`, `decalnocull`, `detailallfar`;
- `noboxtest`, `listuncached`, `stalleach`, `nolevelreserve`, `nosizefloor`, `vramlargest`;
- `transparent`, `textures`, `interpolation`, `novfpu`, `noscratch`, `nocull`;
- `readerslow`, `nomeaudio`.
- build 118: `verifyvectors` (X10, the vector checks), `tcutfull` (T1, the see-through cut's sphere test), `restlesscheck` (the restless back-off: every sleep candidate checked every tick).
- build 119: `nearclusterfull` (NC, the near corner loop's runs of 16), `skinaxisloop` (SU, the corner sums' axes written out), `rigidrunsfull` (NR, the rigid near path's runs), `surfaceqsort` (SS, the light pass's surface bits), `vblankslow` (the clock fix: the vertical blank thread back below the game thread).
- build 136: `wakeonwriteoff` (WT, wake-on-write: every sleeping object checksummed every tick again, not only those a writer marked; on by default in local games only, `wakeonwrite` puts it on in networked games too; `docs/wake_on_write.md`).
- `decalfadeoff` (DFADE: dynamic decals live their tags' own lifetimes again, up to 300 s, instead of 30 s and a 2 s fade; `decalfadeN` sets N seconds, `decalcensus` logs the live counts; campaign only; `docs/review_decalfade.md`).
- `lfsonceoff` (LFS: the level's lens-flare markers' direction and up worked out for every marker every frame again, not only for the flares kept and from the direction-byte table; `docs/review_lfs.md`). Check word `lfscheck` (every table answer worked out again and compared, no marker past a full frame skipped, the BSP's distinct direction triples counted).
- BSPR v2 (b141 candidate, on: b30 -2.1 to -2.6 ms a frame on the PSP-3000): `bspsame` (a biped's last collision answers not kept: every sphere and ray question walks the BSP), `bspsamecheck` (each kept answer checked against the walk, and a mismatch counted; `docs/review_bspr2.md`).
- NRCHK (on by default; `nonrchk` puts the test back): the default (the root node matrix's validity test in object_compute_node_matrices left out: its failing branch only writes debug.txt, so play can't change [D]), `nrchknan` (only its finite tests, no unit-length or square-axes products). Its price is a PSP pair on c10, ~215 tests a tick [E].
- SLEEPDBG + COLLUSAGE (a code layout, so a build switch, not a keep word; `PSP_DEADCODE_INLINE=1` builds the old layout): psp_sleepy_update's reports (`sleepdebug`, `itemsleepcheck`, `flipwatch`) and psp_object_checksum's `checksumcheck` moved into cold, never-inlined functions called under the same tests, and the collision usage log's empty calls compiled to nothing. Nothing that runs in play changes [D]; the hot function was 9.7 KB and 4.6% of c10's tick instruction misses [E]. Exact in the emulator (deadchk1 40807412345: play and both seeds identical, b30 3,600 ticks, c10 7,200, the reports on too). PSP-3000 c10, two builds without PGO (40812138009 old, 40812168360 new): tick -0.3 to -0.5 ms (-1.2 to -2.5%) in every stretch, frame -0.9 and -1.2 ms late, I-misses -2% [M], all real. With the stale campaign-w142g profile the new side lost 8 functions' counts and measured +5 to +8% (40809312949 vs 40809345943): a PGO build of it needs a profile made from this code.
- GSCHECK (on by default; `gscheck` puts the checks back): the tick's two game_state_check_data_arrays walks (every game-state allocation, twice a tick; NATIVE-7's corruption check) left out: on a failure they only log, and their state is read by nothing else, so play can't change (deadchk1 40807412345: play and both seeds identical, b30 3,600 ticks, c10 7,200). PSP-3000 c10 40809345943: tick -0.3 ms (-0.9%) and -0.2 ms (-0.7%) in game time 150-2000 (real), in the noise after. Sheepdog01 2026-10-28 21:14: off by default if it isn't needed; turn on `gscheck` when chasing NATIVE-7.

Opt-in look calls (build 122, off, for Sheepdog01's A/B; `docs/review_b122_look.md`): `seethroughvfpu` (multi-node see-through parts placed on the VFPU), `lightmerge` (the light pass's bands drawn as one, each corner lifted), `foliagetwopass` (alpha-tested model parts' soft second pass), `sceneryhalf` (B2, build 107).

Opt-in, off (`docs/review_gebusy2.md`): `metail` (under M6 #2 the ME publishes its frame's last draws to the GE at its frame's end, not with the main CPU's finish after the next ticks; exact). Instrument `gebusy2`: `gebusy` with the ME's lists timed too.

Opt-in speed calls (the cut stream, off, for Sheepdog01's A/B; `docs/review_cutstream.md`): `cutless` (triangles and cut pieces wholly off the screen not cut: −1.1 to −1.4 ms a frame of renderer CPU [E], but one or two colour steps move in scattered pixels in the emulator, cause not found), `guardwide` (the guard planes at 1920 pixels: not pixel for pixel, and no less cutting). The fix's off word: `cutstreamoff` (the fixed cut buffers, pieces past them lost). Recon: `cutcount`, `cutlessmaskN`.

Pacing word: `novblankthrottle` turns the throttle off. Frames get faster, but the warthog bounces again.

Check words: `checksumcheck`, `spherecheck`, `unlinkcheck`, `nearcheck`, `skinvfpucheck`, `tcutcheck` (build 118), `nearclustercheck`, `skinaxischeck`, `rigidrunscheck`, `surfacesortcheck` (build 119), `meslantcheck`, `meringtest` (ME phase 1, with the opt-in `meslant`), `wakecheck` (build 136: every sleeper summed anyway, and an unmarked change logged with the words that changed).

Instrument word: `probes` (build 119) splits the hot loops from their calls on the PSP: the near corner loop and bone slots, the level's passes by piece, the light draw before, in and after its triangle loop, the decal calls by piece. Off by default: each reading costs ~0.6 us.

Build switches that undo savings: `PSP_MAP_CHECKS=1`, `PSP_NO_COLD=1`, `PSP_DEBUG_TOOLS=1`, `PSP_MULTIPLAYER=1`.
