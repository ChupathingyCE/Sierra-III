# Review section for build 121: the level sphere walk (BP)

For `docs/review_b121.md`. Same shape as `review_b120.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `bipeds121b` (from `w121`, `5015ea3`, by `bipeds121`).

| Change | Kind | Default |
|---|---|---|
| The level sphere walk, chosen per walk: the engine's own for walks of a leaf or two, else a copy that tests each surface once per walk, looks the plane stack up through buckets and walks a surface's ring once | exact speed (lever BP) | on (`surfacetestold` off) |

---

## Where a biped's time goes

The rig's step ladder (PSP-3000, lab 38472506973, `PSP_TICK_STEPS=1`) put bipeds at 26.3 ms of b30's ~57 ms tick, 20.2 of a50's ~31 and 18.2 of c20's [M]. Biped physics alone was 8.7 ms of a50's tick, 668 µs a call, against 290 µs on b30 and 143 µs on c20 [M].

The cache models say what the time is made of (w121's `aiperc121` runs, `761f2d4`: hands off to game time 120 s, b30 on the b113 route to 150 s; lab 38512475945) [E]. Shares of the tick's instruction fetches, by the functions in each list's top 40:

| Map | Level sphere walk | Vector and pill walks, cluster lookups | Posing (node matrices, animation, aiming) | Sphere walk, est. ms a tick |
|---|---:|---:|---:|---:|
| a50 | **27.5%** | 8.2% | 7.5% | 3.75 of 23.0 |
| b30 (flight and beach) | 10.3% | 11.2% | 8.9% | 2.82 of 45.1 |
| c20 | 5.4% | 7.8% | 17.6% | 1.02 of 23.4 |
| d20 | 13.3% | 5.5% | 4.5% | 1.83 of 21.7 |

- "Est." is the PSP-1000 estimate the earlier reviews use: a cycle an instruction plus 72 a miss of either cache, at 333 MHz.
- The level sphere walk is `collision_bsp_test_sphere`. Every biped asks it each tick for the features around its pill (`collision_get_features_in_sphere`, from the pill sweep and `biped_find_nearby_support_surface`). Corpses settling (`biped_limp_noodle.c`) ask it through `collision_test_sphere` [D].
- Posing is c20's largest part: the Library's bipeds are the Flood, and their animation and node matrices dominate.
- Unit and weapon updates are spread thin: no function of theirs reaches 1.5% of any map's fetches (`unit_update` 0.3%, `weapon_update` 0.2% on a50).

## Why a50's physics is dear

The walk now counts its own work (the `surfacetest:` log line, lab 38525644466) [E]:

| Map | Walks a tick | Leaves a walk | References a walk (on the stack) | Surfaces reached a walk | Of those, reached again |
|---|---:|---:|---:|---:|---:|
| a50, hands off | 16.5 | **47.5** | 114.7 (93.1) | **93.1** | **51%** |
| b30, beach (route) | 20.6 | 4.4 | 10.9 (6.0) | 6.4 | 33% |
| c20, hands off | 27.6 | 1.7 | 7.7 (2.1) | 2.2 | 12% |

- A walk on a50 reaches 11 times b30's leaves and 15 times its surfaces. The cost of a call follows. A walk descends to every leaf the sphere touches. Then, for every surface reference on a plane the sphere straddles, it walks that plane's 2D tree and tests the surface: each edge of its ring, twice.
- The pill's sphere is the same size on every map: half the biped's height, plus its width, plus the step. So the difference is the ground's collision geometry. a50's opening is a rocky canyon, and its collision surfaces are small and many [U: the walk counts are measured; that it is the terrain's triangulation is read from them, not checked in the tags]. The lead's guess, the ship's interior, isn't where these runs play.
- Half of a50's surface tests were repeats. One ground triangle runs through several leaves, and each leaf's reference reaches it again. A repeat can't change the answer (below), so it was pure waste.
- The X9 memo can't take much of it. An a50 answer of ~93 surfaces is too big for a memo entry, so only 2,839 of 6,961 queries' answers were kept (`spherememo:` line).

## The level sphere walk (`3c48620`, `f5b4ca5`, `c17bc51`)

**What.** `collision_bsp_test_sphere` now chooses its walk each time (`psp_sphere_walk`). While the last walks averaged three leaves or fewer, it walks with the engine's own code. Otherwise it walks with a copy carrying three changes. Each change gives the old walk's answer, list for list and in order.

1. **A surface is tested once per walk and plane.** A small table remembers each (surface, signed plane designator) the walk has tested, and a second visit is skipped. A walk number empties the table for every walk.
2. **The plane stack is looked up through buckets.** The stack's designators are counted in 64 buckets, each with a count and an xor:
   - a bucket at zero holds none, so the reference is skipped without the scan;
   - a bucket holding one holds its own xor, so the match is one compare;
   - only a bucket holding more is scanned as before.

   On a50 nearly every scanned reference matches (93 of 94), so the xor is what takes the scan away.
3. **A surface's ring is walked once.** The vertex test and the edge test went round the same ring one after the other; they now share one pass.

**Why it chooses.** The first version took the copy for every walk, and it made c20 worse: its estimated tick went from 23.23 to 23.47 ms (+1.05%). Its fetches hardly moved (+0.1%), but its data misses rose 4.6% (lab 38525896825) [E].
- c20's walks reach 1.7 leaves and 2.2 surfaces, with 12% repeats. Repeats come from triangles that run through several leaves, so a walk of a leaf or two has almost none to save.
- What it paid was the copy itself. Each walk copied its 560-byte context and cleared 320 bytes of buckets on the stack, and the copy's code competed with the rest of the tick for the caches.
- A second try (`4696c68`) started every walk in the copy without the changes and switched at the third leaf. It was exact (below), but c20 was still +0.80% (lab 38539791064): the copy's own walk had 0.5 million more data misses and 0.5 million more instruction misses than the engine's over 60 s.

So the choice is made before the walk:
- After every walk the leaf count of its answer updates an average over about eight walks (in sixteenths: `psp_walk_leaves16`).
- Past three leaves the next walk takes the copy. c20's walks (1.7) stay on the engine's code; b30's and d20's (4.4, where the copy paid: b30's walk functions 2.78 → 2.52 ms a tick) and a50's (47) take the copy.
- The copy no longer copies the context, and its buckets are static and need no clearing. Every designator counted is popped before the walk ends, so they are back at zero for the next walk.
- Either walk gives the same answer, so the choice can't change play; it only decides which code runs.

**Key lines.** All in `psp_fixups.py`, `FIXES_121["source/physics/collision_bsp.c#surfacetest"]`:
- `psp_sphere_walk`: picks the walk (`surfacetestold`). Under `surfacetestcheck` it walks the old way too and compares the four lists;
- `PSP_SURFACE_DEDUP_LEAVES16` and `psp_walk_leaves16`: the choice. `surfacetestcheck` takes the copy every time, so that every walk of a check run tests it;
- `psp_surface_seen_before`: the table (256 slots, open addressing). Past 192 entries in one walk it stops remembering, so a probe always ends;
- `psp_bsp3d_test_sphere`: the buckets (`psp_stack_counts`, `psp_stack_xors`), kept at each push and pop;
- `psp_surface_test_sphere`: the one-pass ring;
- `psp_surface_test_report`: the `surfacetest:` line every 300 ticks, with how many walks were the engine's own.

**Why it is exact** [D].
- *A repeat test adds nothing.* A surface test reads the surface, the walk's centre and radius, and the projection the reference set. For one (surface, signed designator) all of these are the same on the second visit. It writes only through `add_feature`, which keeps an index once and drops it when a list is full. So a second visit finds every index it would add already there, or its list still full (lists only grow), and returns the same way.
- *The buckets.* Each push counts a designator and each pop uncounts it, so a bucket always counts exactly the stack's designators in it, and every bucket is at zero between walks. A bucket at zero holds none; a bucket holding one holds its xor. The scan only ever asked whether the designator was on the stack, not where.
- *One pass.* The two loops fill different lists (vertices, edges), each in the ring's order as before, from the same reads. The third loop (whether the centre lies inside the surface) is unchanged.
- The X9 memo stays in front: a hit still answers without a walk.

**Proven** [E]. Hands off, `nearauto assettrack geo2 nomeaudio fixedticks fixedseed statecrc`, on against `surfacetestold`, compared with `tools/statecmp.py --ignore="object looping sounds,first person weapons"`. `surfacetestcheck` makes each walk both ways and compares surfaces, edges, vertices and leaves, count and order.

**`c17bc51` (the choice per walk, the one to ship)**: its pairs on a50, c20 and d20 and its `surfacetestcheck` on a50, c20 and b30's route are in lab 38543568066 (`bp121j`), still running at hand-off.

**`4696c68` (the copy from a walk's third leaf)**, lab 38539760969 (`bp121g`, walls of 600 s):
- **Play exact.** The play hash and both random seeds are the same at every common tick: a50 26,371, c20 20,341, d20 16,616.
- **Every walk checked.** `surfacetestcheck`: 0 walks differed of 586,454 on a50, 731,329 on c20 and 427,377 on b30's beach by the b113 route.
- **How often a walk switched** (`surfacetest:` lines):
  - a50: 86% of its walks (47 leaves a walk); half their surface tests are repeats;
  - b30's route: 39% (4.9 leaves);
  - d20: 29% (4.4 leaves);
  - c20: 14% (1.7 leaves); 9.5% of the switched walks' surface tests are repeats.

Earlier versions, same tests:
- **`3c48620` (all three, every walk):**
  - play hash and both seeds the same at every common tick: a50 40,036, c20 30,280 (lab 38525644466), d20 17,444 (38525896825) and 38,971 (38527888836);
  - `surfacetestcheck`: 0 walks differed of 488,735 on b30's route (38525644466), 548,008 on a50 and 699,290 on c20 (38525896825), and 1,164,314 on a50 (38527888836).
- **`f5b4ca5` (with the xor, the copy every walk):** d20 the same at all 27,841 ticks; `surfacetestcheck` 0 of 788,821 on a50 and 0 of 495,033 on b30's route (lab 38528154096).

The whole-state hash parts only in allocations known to follow the emulator's speed. `objects` (the render mark) and `contrail point` differ. So does `lights`, at one checkpoint of c20 (tick 13,260); `aiperc121`'s exact pair shows it at the same place (tick 13,290, lab 38512442380). Each pair's last checkpoint is cut off by the wall, and its list of differing allocations means nothing.

**Multiplayer.** The multiplayer build compiles and links (lab 38525644466, build 2). Its run stops at start, as `w121`'s does: the display list doesn't fit the heap (`review_b121_aiperc.md`).

**Measured: the cache models** [E]. The tick phase, per tick, with the estimate above. a50 and c20 play the same on both sides, so their counts compare exactly; b30 plays alike, not the same.

`c17bc51`'s cache-model runs (a50 and c20 to 60 s, b30's route to 100 s, on against `surfacetestold`) are in lab 38543568066 (`bp121j`), still running at hand-off. `4696c68`'s b30 is in lab 38539821915 (`bp121i`).

Earlier versions:

| Version, run | Map, stretch | Instructions | Data misses | Est. tick, old → on |
|---|---|---:|---:|---|
| `3c48620`, 38525896825 | a50, 90 s | −7.4% | +1.0% | 22.63 → 22.00 ms (−0.63, −2.8%) |
| `3c48620`, 38525896825 | c20, 90 s | +0.1% | **+4.6%** | 23.23 → 23.47 ms (**+0.24, +1.05%**) |
| `3c48620`, 38525896825 | b30 route, 150 s | −6.5% | −2.8% | 45.90 → 43.86 ms (−2.04, −4.4%; alike, not the same) |
| `3c48620`, 38528154096 | a50, 60 s | | | 20.37 → 20.15 ms (−0.23, −1.1%) |
| `f5b4ca5`, 38528154096 | a50, 60 s | | | 20.37 → 19.85 ms (−0.53, −2.6%) |
| `4696c68`, 38539791064 | a50, 60 s | −6.0% | +1.6% | 20.46 → 20.10 ms (−0.36, −1.8%) |
| `4696c68`, 38539791064 | c20, 60 s | +0.4% | +1.4% | 22.83 → 23.02 ms (**+0.18, +0.80%**) |

**Default.** On; an exact speed-up is pre-approved. Keep words:
- `surfacetestold`: the walk as before;
- `surfacetestcheck`: both walks, compared. The `surfacetest:` line counts the walks that differed.

**Campaign and multiplayer.** Bipeds run in both, and a networked client predicts its own biped through this walk. The change gives the walk's own answer, bit for bit, so a client stays in step with ChupathingyCE's servers (network version 28) [D, and the checks above].

**Memory.** About 8 KB more static data:
- the table (3 KB) and the buckets (320 bytes);
- the check's copy of a walk and its answer (4.7 KB), used only by `surfacetestcheck`.

The first version's heap was 12 KB smaller (the multiplayer build: 4,716 → 4,704 KB). The check's copy could move to the stack if the heap needs it back.

`bipeds121` keeps the first version (`3c48620`); `bipeds121b` is the one to merge.
