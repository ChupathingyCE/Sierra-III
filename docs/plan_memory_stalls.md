# Plan: the memory stalls, the speed work's first goal (2026-10-16)

The current figures are in `cpu_cost_map.md`: this plan's "two thirds instruction" split is nearer 60/40 by count on tock159 [M].

Sheepdog01, 2026-10-16: this is the high-priority speed goal. Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.

## The root cause

**The PSP spends about half its cycles waiting on memory, and about 60% of those misses are instruction fetches** (by count, 68% on c20 [M]; `hardware_facts.md`'s miss-ratio bullet; corrected 2026-11-16 from "two thirds"). Most of the speed work so far cut instructions or moved them to the ME, so it barely moved the clock.

| What | Figure | Source |
|---|---|---|
| Tick cycles waiting on memory | 54–56% | [M] b107 b30 beach (`overnight_b108.md`), b95 (`library_hardware_b95.md`) |
| Drawing cycles waiting on memory | 47–59% | [M] same runs |
| Instruction share of the misses | ~65% (tick: 5,200 of 8,060 per M cycles; superseded 2026-11-16: ~60% by count today, `hardware_facts.md`) | [M] b107; the cache model agrees (64%) [E] |
| The tick's waits, in ms (b30 beach, tick 61.7 ms) | ≈ 21 ms on instruction misses, ≈ 12 ms on data misses (b107; superseded 2026-11-16, as above) | [U] from the shares (`optimization_ranking.md` §1) |
| The tick's hot code | ~59 KB, against a 16 KB instruction cache | [E] |
| Kind of miss | capacity, not conflict: the top function holds 3.2% of the misses, the top 30 about a third | [E] `overnight_b108.md` |
| One miss | 216 ns (72 cycles) with the bus quiet; 424 ns while the GE reads main RAM | [M] `psp_hardware_reference.md` |

**Capacity is the key word.** Each object's update streams ~35 KB of code [U, b112 model], so the next object's update finds its code evicted and fetches it again. Reordering the code (layout) only shuffles which lines collide. Two things help:
- make each stretch of work fit in 16 KB;
- run that stretch over many objects before moving on.

## Why the recent work didn't show

- **The ME took instructions, not misses** [M]. The pose job: −2.0% on b30's flight (PSP-1000), −6.4% on c20 (PSP-3000). Its main-CPU hand-over costs ~0.86 ms a tick on b30, because the copies are cold. The ceilings ([E], node matrices −5.4 ms on b30) counted cache refetch as work the ME would remove.
- **Fetching code once beat computing faster** [M]. b30's 36 scenery computes cost ~3 ms in the loop and 0.6–0.9 ms run together. The same batch on c10's ~300 light ones cost 9.7 ms against ~3 ms: out of the loop, each object's data came cold (lab 38871696661, 38884427350).
- **Builds swing more than levers gain** [M]:
  - b131 lost 10–17% to code placement.
  - b133 as first rebuilt (d9e7cf3) ran 3–20% slower than b132 on the PSP-3000 with nothing new switched on (lab 38895069416). It used b132's profile. GCC drops the counts of any function whose branches changed, and five hot functions lost theirs, the object loop's two biggest among them (`objects_update` 5.4 → 8.2 KB) [D].
  - A real 2–5% gain is invisible under swings like that.
- **The layout lever is smaller than it looked** [E]. score131c's −17 to −21% is the tick's *instruction misses* against b131's own layout. b132's order already took −17 of it. Over b132, placement by cache set saves ~5% of the tick's instruction misses. At the measured exchange rate (1% of tick I-misses ≈ 0.3% of the tick, b116) that is **~1.5% of the tick** [U]. Drawing shows −11%, but the model has misjudged drawing before.

## The goal

**The tick's memory waits on the PSP-1000, cut by a third** (b30's beach: ≈ 33 ms → ≈ 22 ms of waits) [U target]. Every build is measured on the goal's own numbers, and no build loses speed to its profile or layout again.

Where the tick has to go (b30's beach, from `optimization_ranking.md` §1): 61.7 → under 33 ms for 30 ticks a second. The waits are ~33 ms of that 61.7. Drawing (129 ms a frame on the beach) has the same disease. It belongs to the renderer session (`psp_ge.c`, `psp_geo.c`, `psp_tex.c`); this plan covers the tick and the shared measuring.

## The work, in order

**P0. A steady measuring stick** (first: the rest can't be judged without it)
1. **Profile gate.** The release build lists the hot functions compiled without their profile and fails if any is in the hot set. b133's would have shown five.
2. **Each build's profile is counted at its own code**, with its default keep words, before its release check (b133's rebuild reused b132's to ship sooner, and lost 3–20%).
3. **The ladder reports misses.**
   - `hwprofile` on the ladder's preset: I-misses and D-misses per M cycles, and the memory-wait share, per map, per build.
   - The goal's dashboard row comes from these numbers.
   - Check once that it costs nothing measurable.
4. **Decisions by A/B inside one build** (`keepsets`, on the PSP-1000), never by comparing two builds.

**P1. Code residency** (the tick's ≈ 21 ms of instruction waits)
1. **Measure the loop by pass.** The cache model, per `objects_update` step and object type: code fetched, I-misses, and data touched per object. That table picks P1.2's candidates.
2. **Split leaf passes out of the per-object update** (research T7).
   - Leaf passes are work nobody reads before the tick ends: light connection, validity checks.
   - Each pass's code stays resident across all objects.
   - Exact by construction, checked with the play hash.
3. **Batch where code is heavy and data is small** (nwbatch's rule, made explicit). The batch pays when the code fetched once outweighs the data fetched cold. Pick by the P1.1 table, per type and per pass, not by guessing.
4. **Shrink the hot code:**
   - **Dormant features out of the hot loop's code.** b133's batch, off, still grew `objects_update`'s hot part from 5.4 to 7.5 KB with its own profile [D], and c10 ran +6.9% [M]. Opt-in work that hooks the per-object path goes behind a build switch, not only a keep word.
   - **Untrained code split cold.** The profile build uses partial training (`PSP_PGO_PARTIAL=1`, chosen when the profile was one map's opening). It treats code the training never reached as unknown rather than cold, so that code stays inside the hot functions: `objects_update`'s cold part is 96 bytes [D]. With 13 runs over ten maps, `PSP_PGO_PARTIAL=0` may now pay. Score it in the model. Its risk is paths the training missed (later stretches of a map), so measure those too.
   - the assert audit (X10: up to 447 KB less code [D]);
   - an inlining budget for the hot units (T4);
   - more cold splits (T6).
   - Each is scored by tick I-misses in the model, then on the PSP.
5. **Layout from the build's own graph** (TG), last, because 1–4 change the code it places. ~1.5% of the tick [U].

**P2. Data misses** (≈ 12 ms of the tick's waits)
- Compact arrays for the per-object walks, plus prefetch ahead of them (X5: ceiling 3.1–5.4 ms a tick [U]).
- Hot tables in the scratchpad, ranked by misses per KB (`overnight_b108.md`).

**P3. Work not done** (cuts instructions and misses together)
- Lean paths for idle and asleep objects:
  - T9;
  - `machinesleep` (E4);
  - the standing-biped shortcut (E8);
  - bodies placed dead sleep (E26).
- Far actors' perception at half rate (B1) is Sheepdog01's call, deferred until the exact levers run out.

**P4. The ME: whole subsystems only**
- A whole subsystem's code then lives in the ME's own cache, and stops evicting the main CPU's [U].
- Before building, measure the hand-over budget: prize = compute left − (copy in + copy out + wait). Build only when that is clearly over 1 ms.
- Candidates: the next tick's AI (L3), after the tick/draw split (X16).

**Parked**
- ME slices of per-object work (the pose job showed their cap).
- Levers under ~3% until P0 lands (they can't be seen through the swings).
- Batching without both its code and data cost measured.

## Today (2026-10-16)

1. b133 rebuilt with its own profile (`405ca12`): release check and the PSP-3000's ladder running; then post it.
2. P0.1, the profile gate.
3. P0.3: the ladder with `hwprofile` on the PSP-1000, the goal's baseline (b30, c10, c20).
4. P1.1: the object loop's per-pass table in the cache model.
