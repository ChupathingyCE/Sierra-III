# Review section for build 121: the level sphere walk (BP)

For `docs/review_b121.md`. Same shape as `review_b120.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `bipeds121` (from `w121`, `5015ea3`).

| Change | Kind | Default |
|---|---|---|
| The level sphere walk tests each surface once per walk, scans the plane stack only where a bucket says it could match, and walks a surface's ring once | exact speed (lever BP) | on (`surfacetestold` off) |

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

## The level sphere walk (`3c48620`)

**What.** `collision_bsp_test_sphere` now walks a copy of the engine's walk (`psp_sphere_walk`) with three changes. Each gives the old walk's answer, list for list and in order.

1. **A surface is tested once per walk and plane.** A small table remembers each (surface, signed plane designator) the walk has tested, and a second visit is skipped. A walk number empties the table for every walk.
2. **The plane stack is scanned only where it could match.** The walk keeps a count of its stack's designators in 64 buckets. A reference whose bucket is at zero isn't on the stack, and is skipped without the scan.
3. **A surface's ring is walked once.** The vertex test and the edge test went round the same ring one after the other; they now share one pass.

**Key lines.** All in `psp_fixups.py`, `FIXES_121["source/physics/collision_bsp.c#surfacetest"]`:
- `psp_sphere_walk`: picks the walk (`surfacetestold`). Under `surfacetestcheck` it walks the old way too and compares the four lists;
- `psp_surface_seen_before`: the table (256 slots, open addressing). Past 192 entries in one walk it stops remembering, so a probe always ends;
- `psp_bsp3d_test_sphere`: the bucket counts, kept as the stack is pushed and popped;
- `psp_surface_test_sphere`: the one-pass ring;
- `psp_surface_test_report`: the `surfacetest:` line every 300 ticks.

**Why it is exact** [D].
- *A repeat test adds nothing.* A surface test reads the surface, the walk's centre and radius, and the projection the reference set. For one (surface, signed designator) all of these are the same on the second visit. It writes only through `add_feature`, which keeps an index once and drops it when a list is full. So a second visit finds every index it would add already there, or its list still full (lists only grow), and returns the same way.
- *The buckets.* A designator on the stack was counted in its bucket, so a bucket at zero holds none. The scan only ever asked whether the designator was on the stack, not where.
- *One pass.* The two loops fill different lists (vertices, edges), each in the ring's order as before, from the same reads. The third loop (whether the centre lies inside the surface) is unchanged.
- The X9 memo stays in front: a hit still answers without a walk.

**Proven.**
- **Play exact.** Hands off, `nearauto assettrack geo2 nomeaudio fixedticks fixedseed statecrc`, on against `surfacetestold`, compared with `tools/statecmp.py --ignore="object looping sounds,first person weapons"` [E] (lab 38525644466):
  - a50: the play hash and both random seeds are the same at all 40,036 common ticks;
  - c20: the same at all 30,280.

  The whole-state hash parts only in allocations known to follow the emulator's speed. `objects` (the render mark) differs on both. `lights` differs at one checkpoint of c20 (tick 13,260); `aiperc121`'s exact pair shows it at the same place (tick 13,290, lab 38512442380).
- **Every walk checked.** `surfacetestcheck` makes each walk both ways and compares surfaces, edges, vertices and leaves, count and order [E]. On b30's beach by the b113 route: 0 walks differed of 488,735 (lab 38525644466).
- **Still running at hand-off:** d20's pair, and `surfacetestcheck` on a50 and c20 (lab 38525896825, batch `bp121c`; and 38527888836, `bp121e`).
- **Multiplayer.** The multiplayer build compiles and links (lab 38525644466, build 2). Its run stops at start, as `w121`'s does: the display list doesn't fit the heap (`review_b121_aiperc.md`).

**The gain.** Measured in the cache models by lab 38525896825 (`bp121c`: a50 and c20 to game time 90 s, b30's beach to 150 s, on against `surfacetestold`), still running at hand-off. Read it with the estimate above, per tick of the tick phase.

The estimate from the counts above [U]:
- On a50 a surface test is ~350 fetches (the 2D walk's leaf and the inlined test, plus `fast_vector_intersects_sphere`'s ~4 edges), from `bsp2d_test_sphere_recursive`'s 407,000 fetches a tick over 16.5 walks and 93 surfaces.
- Skipping 51% of them saves ~270,000 fetches a tick, about 8.6% of the tick's 3.2 million. The one-pass ring and the skipped scans add ~2% more.
- With the walk's own data misses, that is about −1.2 ms of a50's estimated 23 ms (−5%).
- b30 and c20 gain little (0.3% and less): their walks reach few surfaces.

**Default.** On; an exact speed-up is pre-approved. Keep words:
- `surfacetestold`: the walk as before;
- `surfacetestcheck`: both walks, compared. The `surfacetest:` line counts the walks that differed.

**Campaign and multiplayer.** Bipeds run in both, and a networked client predicts its own biped through this walk. The change gives the walk's own answer, bit for bit, so a client stays in step with ChupathingyCE's servers (network version 28) [D, and the checks above].

**Memory.** 7.8 KB more static data: the table (3 KB) and the check's copy of a walk and its answer (4.7 KB, used only by `surfacetestcheck`). The heap is 12 KB smaller (the multiplayer build: 4,716 → 4,704 KB). The check's copy could move to the stack if the heap needs it back.

**Next** (branch `bipeds121b`, `f5b4ca5`, exactness and worth in lab 38528154096, `bp121f`, queued at hand-off). Each bucket also keeps an xor of its designators, so a bucket holding one answers with one compare. On a50 nearly every scanned reference matches (93.1 of 94.3), so the scan is still done for almost all of them; this takes it away.
