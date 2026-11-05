# The redesign: the tick

Sheepdog01, 2026-11-05 07:42: "we're working on a redesign, not patches." This page is the design's tick section: what binds the main CPU's tick, what can change that, and the order to build it in. It rests on the recon's ledgers (`redesign_recon.md`) and the cost map (`cpu_cost_map.md`), and it cites them rather than restating them. The fact sheet behind it is lab or-job 42038302650 (128 of 130 quotes checked).

Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or docs, [U] inference.

## 1. What binds

- **The tick sets the pace.** On b30, c20 and both of b40's late zones the frame waits on the main CPU's ticks (`frame_timeline.md` §1) [M]. b30's beach: frame 97.2 ms, tick 65.4 a frame [M, PSP-1000 42000782600].
- **Most of the tick is waiting, not working.** It waits on memory for 54-68% of its own cycles (b30 63%, c20 68%, d20 56-60%, b40 62%) [M, `cpu_cost_map.md` §1]. The core is in order: one miss in flight, no hardware prefetcher, a random line ~73 cycles [M].
- **The misses are spread thin.** Instruction misses are capacity: ~70 KB of hot code fetched again 47-75 times a tick [E]. Data misses run across the object pool (16%), the tag window (32%, half of it the level BSP), globals, stacks and the cluster walk [E, `cache_work.md`]. The top 30 functions hold 44-46% of b40's tick [M].
- **The GE makes every miss dearer.** A main-CPU miss takes +65-96% longer while the GE reads textures from RAM, and +272% while it reads vertices [M, `cpu_cost_map.md` §2]. Its reads inside the ticks cost b30 11.7-15.1 ms a frame of tick [U on E x M, lead A].
- **The leaf work is done.** The exact passes, the layouts, the profile, the asserts out, the data placements, the scratchpad, BSPLAYOUT (neutral on the PSP) and every subsystem's stage-major ceiling (none with an exact cut worth building) are in `cpu_cost_map.md` §4-4a with their prices. What's left at the leaves is under the floors (collcensus, 2026-11-05).

So the tick doesn't get faster by shaving functions. It gets faster when **the main CPU does less of it, waits less for each miss, or does it less often**. Those are the three parts below.

## 2. The parts

| Part | What it changes | Ceiling | Exact? | Where it stands |
|---|---|---|---|---|
| **T1. Work off the main CPU** (B1) | Whole jobs placed on the ME where the main CPU binds | ~5-8 ms a frame on b30's beach, ~2-3 on c20 [U on M] | Yes, job by job | Sized (`frame_timeline.md`, "Which tick and frame jobs could move"). MEVISEARLY slice 1 gave nothing because the main CPU waited for the answer; slice 2 (the earlier push) is designed (`mevis_slice2.md`, recon/mevis-slice2). Then DRAWRAYSME (~2.5), RINGREFUSE (0.6-0.9), the sound manager (~0.9) |
| **T2. The shared pool** | Divisible work (points, AI thinks in batches, decoding, posing batches) taken by whichever unit is free within the frame | Grows with load: what T1's fixed homes can't place | Results identical either way where `exact_state.md` says so | mepointsclaim (B56) is its first form. The placement rule: fixed homes, overloads fixed where they happen, a pool for the rest; no balancer (Sheepdog01 2026-11-04 22:53) |
| **T3. The tick run less often** | The simulation's rate where a player wouldn't feel it | L15 (15 Hz with interpolation): b30's flight **frame -21.5%, game speed +28%**; landing -15.4%, +19% [M, PSP-3000 40961005993]. AIFAR: tick -1.2 to -2.7% [M]. FX15: d20 tick -0.6 to -1.4% [M]. AITICKOLD: ~5-8 ms on the beach [U on M] | No: compromises, each behind its word, each Sheepdog01's A/B | L15, AIFAR, FX15 built and off; AITICKOLD to build (Sheepdog01 07:30: "try it and see"). Campaign only: a co-op client ticks at the host's 30 Hz, and the AI doesn't run on a client |
| **T4. The GE kept off the tick's misses** | When the GE reads main RAM, not how much | The GE wholly out of the tick: tick -2.6 to -7.6 ms, but frame +14 to +52 ms [M]: serializing loses | Yes | **Unmeasured middle:** which record kinds the GE reads during the tick (vertices hurt 3x what textures do). The VRAM route didn't hold at the margin (VMFILL moved 95-152 KB a frame into VRAM with no change [M]), so the next question is order, not room |
| **T5. Walks that overlap their misses** | The collision, cluster and node walks reshaped so each miss has ~70+ cycles of work to hide behind (next-item prefetch) | 0.8-2.6 ms a tick [E]; as the code is, 0.2-0.4 [E]; hand placements gave microseconds | Yes | **Gated on one hardware answer:** does an instruction miss wait for a data fill in flight? It moves the ceiling between 0.26 and 1.24 ms a tick (`prefetch_map.md`). A bench, not a build |
| **T6. A tick-start world** (WORLDVIEW) | Collision and node matrices read a copy of the world as the tick began, so each runs as one pass | Collision ~0.75 / ~0.54 ms, node matrices ~0.7-1.2 a tick [U on E], less the copy's price (uncounted) | No: it changes campaign play, inside every client bound | Catalogued. Sheepdog01's call, and the copy's price is counted first (`stage_state_note.md`) |

**Why this shape.** T1-T3 attack the time the main CPU spends; T4-T5 attack the time each miss costs; T6 is the only way the two biggest subsystems (collision 25% of the beach's tick, node matrices 10%) can be restructured at all, and it costs exactness. The exact parts add to roughly 6-11 ms a frame on b30's beach; the compromises (L15 above all) are where the large numbers are.

## 3. The build order

Each step proves itself in the lab and rides the tick/tock cadence.

1. **MEVISEARLY slice 2** (T1, recon): the visibility views pushed to the ME once the camera is final, so the answer is ready at its first reader. Its PSP pair on b30's beach.
2. **AITICKOLD** (T3, recon): built off by default; side-by-sides over a long route, no AI misbehaviour, its price on the beach.
3. **The fill bench** (T5): one `halo_bench` case on the PSP-1000 timing an instruction miss with a data fill in flight. It decides whether T5 is a 0.3 ms or a 1.2 ms part.
4. **The replay's reads by tick phase** (T4): `gebusy2`'s phase split with the record kinds beside it, on b30's flight and beach: which kinds the GE reads while the tick runs. If vertex-heavy records dominate the tick's window, the replay's order (vertex-heavy work outside the tick, texture-heavy inside) is the design; if not, T4 closes.
5. **L15's A/B for Sheepdog01** (T3): built since 10-30, the largest single number on this page, and a feel question only hand play answers.
6. **DRAWRAYSME, RINGREFUSE, the sound manager** (T1), then **the pool's next jobs** (T2: AI think batches, posing batches).
7. **T5's reshaped walks**, if step 3 says the ceiling is the higher one: the collision cluster walk first (its code fits as one pass).
8. **T6**, only with Sheepdog01's OK and the copy counted.

## 4. The targets (to agree with Sheepdog01)

The redesign needs numbers to meet, not "faster" (`redesign_recon.md`, "Before the design commits"). A first proposal, per heavy stretch on the PSP-1000:

| Stretch | Today (tock162) | With T1-T2 (exact) | With T3 (Sheepdog01's calls) |
|---|---|---|---|
| b30's beach | frame 97.2 ms (tick 65.4) [M] | ~89-91 [U] | L15's share of the tick halves it again [U] |
| c20 | frame 64.7 (tick 38.0) [M] | ~62 [U] | |
| b40's snowfield | 7.43 fps [M, 41999004922] | | |

Started 2026-11-05 (master), after the overnight round. Revised as each step reports.
