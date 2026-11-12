# Box: the collision service (bottleneck, 2026-11-11; v0, spec started)

The lab's `research/opportunities/REBUILD.md` is the spine (§3.3: "with biped motion"; §7 step 4). The design draft behind this page is the lab's `research/opportunities/collision.md` (DeepSeek's draft; its [M] rows checked by research 2026-11-11 12:5x, b30's ms corrected; its [U] lines and ceilings stay leads) with its callers' anatomy, `collision_anatomy_ds.md`. This page doesn't restate them: it sets the box's contract, budget and shape, and carries their labels as they are. Labels: **[M]** measured on a PSP, **[E]** emulator or a count, **[D]** read in code or docs, **[U]** inference.

**What it is:** every collision query the tick makes, through one service with its own data: sphere queries (the features in a sphere: the level's surfaces, edges and vertices, and the objects' bounds), vector tests (rays: first hit), and point tests (which leaf and cluster a point is in). Its askers are other boxes: biped motion first (the pill sweep), vehicles and mass points, projectiles, point physics, the AI's lines of fire and sight, and the drawing's rays.

**State:** spec started (2026-11-11).

## 1. Contract

- **Reads:** the level's collision BSP and its surfaces (the tags, read only, laid out the service's way), the objects' bounds and their cluster membership (the world store: posing's sphere section, and the walk's map links), each query's arguments.
- **Writes:** the answers only (a features list, a hit, a leaf), into the asker's own memory. Today's visit marks on surfaces and objects become per-query state, so a query writes nothing shared and can run on either core.
- **Who asks, and when** [D, E: `collision_anatomy_ds.md` §1]: biped motion's pill sweep per moving biped per tick (~90% of the level sphere walks on b30, 36 a tick), the crouch and stand test (player bipeds only), mass points' ground plane (vehicles), vehicle suspension and effect markers, projectiles' lines, point physics (some on the ME already), the AI's line of fire and sight (the ME's twin serves part), and in the drawing sound obstruction, lens flares and object lighting.
- **What the world looks like to a query:** in the serial form, the live world in the walk's order (today's semantics, exact); in the parallel form, last tick's objects and this tick's level (`box_biped_motion.md` §1, the one-tick-old world, Sheepdog01 2026-11-10 16:43). The level never moves within a tick except breakable surfaces, whose flags are part of its state.

## 2. Data

- **The level:** node records with their plane inline (the node and its plane are read together every step, from two arrays today), surfaces with their edges and vertices packed, the top nodes in the scratchpad as today (`collision.md` §2's data layout) [D, U].
- **The objects:** the bounds the queries test, in arrays by cluster (structure of arrays: a query reads one sphere of many objects), patched on the reconnect that changes them, not rebuilt per query.
- **What counting says about it** [E, research's §1a in `collision.md`; the lab's dcanat1 42989839989, dcanat2 `footprint.md`]: collision's functions take **8,990 data misses a tick on b30** (28% of the tick's, the largest system) and 2,802 on c20 (21%). The sphere table and cluster references are already compact (137 lines, 2,119 fills a tick on b30): they miss because they're **evicted**, not because they're spread. So packing alone doesn't pay (CLUSTERPACK, rejected neutral on the PSP-3000; the C2 premise "51 → 16 lines" doesn't hold); what pays is a **smaller working set over the tick**, or the queries' misses moved where they don't stall the tick.

## 3. Placement

- **The main CPU** for the serial form. The VFPU only for 4-wide plane tests, and last: a plane test is one dot product and two compares a node, chasing tag data (`hardware_utilization.md`).
- **Code:** today the eight sampled functions are 23,976 B, 1.5x the 16 KB I-cache, and the port ships two walk implementations chosen per walk by its leaves (`collision_bsp.c`), so the resident set differs by section [D]. The service is one path, its target ~10-12 KB [U] (`collision.md` §2), written so the engine's walk's speed on c20's one- and two-leaf walks is kept: ONEWALK (the port's walk always) was exact and **slower on c20** on the PSP-3000 (tick +1.2%) [M].
- **The second core:** the parallel form's queries (askers at rest, far bipeds, level-only rays) can run on the ME in the parallel phase; the ME's room is the tick schedule's to give (at 15 fps the snowfield has ~13 ms a tick free, the beach ~9 [M, `me_room_fights_*`]).

## 4. Budget

- **Today** [M shares, PSP-1000, tock162 b3461c1's shipping sampler; ms U on M]: collision is 13.3-18.4% of the tick (c20 14.0, a30 14.2, b30 18.4, b40 13.3): **~2.9 ms on c20, ~3.4 on a30, ~4.8-5.6 on b30's beach play, ~10 on b30's flight** (25.6% of a ~39 ms tick, the lab's s_gap §2), **~5.0 on b40's snowfield** (13.3% of 37.6 [M, b151 ledger]); the drawing's rays another 0.7-1.2 ms a frame.
- **v0: 2.2 ms a tick on the beach and the snowfield** for every asker, inside biped motion's 4.5 (`box_biped_motion.md` §4), proportional to the snowfield's split [U]. Per query: b30 makes ~88 collision calls a tick (42 `collision_test_vector` + 46 `collision_bsp_test_sphere`, `cpu_cost_map.md` §4a [E]), so **~25 µs a query** against ~57 today [U].
- **So the cut is about 2.5x on the snowfield and the beach, and 4.5x on the flight.** The sized pieces reach far less: collision's one stage 0.54-0.75 ms [E], the object half packed (CSPK) whose premise the counts undid, the rest under any floor (`collision.md` §5's slice states). **The rest is unsized and is this box's question.**

## 5. Means

- **A (a smaller working set):** one walk instead of two, node and plane in one record, surfaces packed, and per-query state instead of marks written into shared data. Worth: unsized; the data side's 8,990 misses a tick on b30 are 1.9-3.8 ms at the PSP's miss price [U on E] (`collision.md` O1).
- **B (work on change):** what repeats is askers at rest, not rays [E]: biped level spheres repeat 12-20% on b30, resting bipeds' model spheres 84.4% on a30. Exact reuse of the level walk's answer was tried and bought nothing (S2REUSE: 26-33% reused, neutral on the PSP-3000, because the level walk is cheap there [M]). So B skips the whole asker's query, level and objects, when nothing in its reach changed since its last answer: the service's cluster arrays know which objects in reach moved, the guard STILLSWEEP lacked ("objects in reach had moved"). It's "plays the same" for an asker whose inputs and reach are unchanged and exact when the guard is complete; proven by play and seeds where exact, the outcome census where not.
- **C (both cores):** queries only read once marks are per-query, so the parallel form's queries run on either core against last tick's objects (the hazard's reach, `box_biped_motion.md` Open 1: half of a biped's reads of other bipeds land on ones updated earlier this tick).

## 6. What it replaces

`collision_get_features_in_sphere`, `collision_bsp_test_sphere` and both sphere walks (the engine's `bsp3d/bsp2d_test_sphere_recursive` and the port's `psp_bsp3d/2d_test_sphere`), `collision_test_vector` and `collision_bsp_test_vector_recursive`, `collision_features_test_vector`, `bsp3d_test_point` as the tick uses it, the objects' half (`objects_in_sphere`, the cluster walks), and the port's answer memos (BSPR's standing answers stay until B replaces them). **Earlier tries** (the lab's `tools/levercheck.py --ground collision`, 2026-11-11; `collision.md` §4 says why each does or doesn't sink this): BP and P2NC (on), ONEWALK (slower on c20), CLUSTERPACK (neutral), S2REUSE, SMEMO, BSPR, COLLMEMO and BSPW (answer reuse and faster walks: neutral or noise), CSPK, PACKFEAT, SC1 (sized, unbuilt), CT and RT1 (covered by CW), STILLSWEEP (stopped on its guard: reopens under B), VFPUMATH, BETWEENRAYS, DRAWRAYSME, BW and FARHALF (the far tier: shelved slower on b30's flight).

**Upstream manifest:** the collision and BSP functions and the level's collision tags at the pin after the rebase (REBUILD §5b).

## 7. Proof

Alone, against a capture of its queries over a pad bench (each query's arguments, the level, the objects' bounds that tick, the engine's answer): the service's answers equal the engine's (the serial form is exact: same features, same order, same hit). In the frame: play and both seeds identical for the serial form; for B's skips and the parallel form, play and the outcome census, and the multiplayer boundary (the client's own biped well inside the host's 3.5 units; hit reports true).

## 8. States

- 2026-11-11: spec started, from `collision.md` (its [M] rows checked) and the world store's map.

## Open

1. **What the working set is, per query and over the tick**: lines a query touches, distinct and evicted, on b30's beach and flight (firmware's census by structure is the start, `footprint.md`). It sizes A, and it's the figure §4's unsized rest stands on. `[enables:collision]`
2. **B's reach**: of the pill sweeps on b30 and b40, how many have unchanged inputs and nothing in reach moved since the last answer (STILLSWEEP's census, now with the guard). `[enables:collision]`
3. The capture's shape for queries (a query record per call) for the harness (`harness.md`): the adapter is this box's.
