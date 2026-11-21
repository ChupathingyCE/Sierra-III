# The sweep's records staged into the scratchpad: the count, and why it stops there (2026-11-21)

The question (the lead, 2026-11-21): the ME makes the bipeds' level-half collision gathers ahead (`psp/src/psp_sweepme.c`, `box_collision.md` §11) and the main CPU takes them in the tick. Should the ME stage each record into the main CPU's scratchpad by DMACplus (Me2Sc, burst 2, double-buffered beside SCRATCHBSP's nodes), so the take reads SRAM instead of missing on main RAM? The count comes first: what the main CPU pays reading the taken records today is the most such a box could take away.

**Verdict: the ceiling is too small for a box.** At most 0.06 ms a tick on c20 and 0.02 ms on b30 (both under 0.3% of the tick), and half of it is gone at less cost by an exact change that needs no DMA (§3). Staging would add more ME and bus time than it saves (§4). Nothing is listed in REBUILD. §5 says when to ask again.

## 1. The count [E, the cache model]

The scratchpad census (`spad_staging.md` on builder/spadstage; the lab's sierra-batch 44924540134, Sierra-III f5aaba64, on tock162's parent) already names the records: `sweepme_records` is a program variable (64 records of 1,280 B, 80 KB), and the cache map attributes every line in it by name. The census's collision part (`spadcoll`: only the collision steps count as the tick) is the take: `psp_sweepme_find` and `psp_sweepme_answer`, then the gather reading the leaves and features. The census's ME work goes to a cache of its own (`meside`), so every line the ME wrote is a cold miss for the main CPU. The PSP's take works the same way: `batch_need_claim` drops the record's lines (`lines_drop`) before the main CPU reads a record the ME made.

| map (pad leg 2, the batch's stretch) | takes a tick | misses a tick, collision part | misses a take | misses a tick, whole tick | source |
|---|---|---|---|---|---|
| b30 (3,900 ticks) | 11.3 | **79.8** | 6.95 | 135.7 | the lab's findings `spad_coll_set` (b30); the run's tick-phase dset line |
| c20 (2,700 ticks) | 45.1 | **284.7** | 6.22 | 510.5 | `spad_coll_set` (c20), `spad_tick_set` (c20) |

Takes a tick is the misses on each record's first line (one per take, since the line is dropped before every take), and agrees with the records the maps stage (b30 ~12, c20 ~50: `psp_sweepme.c`'s claim-table comment). The whole tick also counts `psp_sweepme_add` writing each record's inputs (write-allocate on the main CPU's lines) and the finds. Staging wouldn't touch either of those.

**Which lines miss** (c20; b30 has the same shape) [E, per line, from the runs' cache maps; the layout read in code, D]:

| line of the record | what's on it | misses (c20, the run) |
|---|---|---|
| 0 (0-63) | `level` (the answer reads `level.planes`) | 121,746 |
| 1 (64-127) | `level`'s tail, `breakable` (the answer's `memcmp`), `center` | 118,667 |
| 2 (128-191) | the inputs' tail, `status`, `leaf_count`, `count`, `leaves[0..18]` | 119,046 |
| 3 (192-255) | `leaves[19..50]` | 0 (b30: 8,564) |
| 4-6 (256-447) | `leaves`' tail, the first ~160 B of the features | 115,005 / 115,003 / 115,003 |
| 7-17 | the rest of the features | 28,817 falling to 103 |

So a take reads about 6.5 lines (~400 B), not 1.25 KB: features are mostly a few spheres and prisms.

**In time** [U on M]: a data miss costs 157-216 ns with the bus quiet and 424 ns beside the GE reading main RAM (`hardware_facts.md`). The records' lines are consecutive, so 196 ns (one miss straight behind another) is the closer figure.

| map | the ceiling, quiet bus | beside the GE | the tick (PSP, the sweep's pairs) | share |
|---|---|---|---|---|
| b30 | 0.013-0.017 ms | 0.034 ms | ~31 ms (flight; the lab's findings `sweepme_claims_psp_b30`) | ≤0.1% |
| c20 | 0.045-0.061 ms | 0.121 ms | ~26 ms (the first stretch; `sweepme_claims_psp_c20`) | ≤0.5% |

A PSP pair resolves about 0.3 ms (~1%) on these stretches. The whole ceiling is under that on both maps.

## 2. What staging would cost

The Me2Sc prices [M]: a start costs 1.76 µs plus 1.93 µs a KB (529 MB/s), and burst 2 on the scratchpad side is 27% faster. A null kernel call costs 17.7 µs, and the main CPU's own start costs 231 cycles once it's in kernel mode (the lab's findings `dmacplus_rates_1000`, `dmacplus_burst_chain_1000`, `spad_dmacplus_me_v21_3000`).

- **The main CPU starts each transfer by a kernel call:** c20's 45 takes in pairs are ~23 starts a tick, or ~0.40 ms of kernel calls to save ≤0.06 ms, 6.5 times the ceiling. One call a tick for the whole set doesn't fit: c20's records cover 18-22 KB a tick against the scratchpad's 16 KB. Refused.
- **The ME starts the transfers itself** (its DMACplus channels, user-segment forms only): the main CPU pays nothing, but the ME pays the DMA instead. That's ~23 transfers a tick of ~0.8 KB each at burst 2, or ~50 µs of channel time on c20, which the ME waits out or schedules around. It saves the main CPU ≤61 µs. The ME is the unit with no room in heavy stretches: its 31-37 ms of work a tick there leaves it nothing at one tick a 33 ms frame (`hardware_facts.md`, row 9). The DMA also takes the bus while it moves the same bytes the misses would have (×1.20 at 25% duty [M]).
- **Room:** the scratchpad is 15.1 of 16 KB in use (SCRATCHBSP's nodes ~9.7 KB, the profiler's 6.6 KB, the probes). A ring of four 512 B slots (2 KB; a bigger record is read from main RAM) would come from the nodes (their misses would rise by an amount the census doesn't settle [U]) or from the profiler. PROFHEAP moved the profiler out and gave the nodes its room, which measured noise [M]. So that room is free, but it's worth no more than this ceiling.
- **Exactness and fallbacks:** the record would still have to reach main RAM for the cases a slot can't hold (a record made on the main CPU, a take out of add order, a record past the slot's size). That means the ME writes it twice, or a second path with its own proof.

Against ≤0.06 ms, that's a wash at best on c20 and a loss on b30. The rule is to attack 30/30 with efficiency and untapped hardware, not with marginal levers that scrape by (CLAUDE.md, the goals). This one is marginal before its costs.

## 3. The exact form that takes a third of it, with no DMA [D; a lever, not a box]

Lines 0 and 1 are the record's inputs. The main CPU wrote them at the add, and the ME only reads them. The answer reads them again only to check `level.planes` and `breakable` against the gather's. But every record of a pass is given `sweepme_level` and `sweepme_breakable` at its add, and both are the main CPU's own and hot. So:

- the answer compares `sweepme_level.planes` and `sweepme_breakable` (the pass's) instead of the record's copies;
- the take drops only the ME's lines (line 2 on, from the record's line that holds `status`) instead of the whole record.

The ME writes nothing before `status`, so the main CPU's cached inputs stay valid. Moving the inputs' tail off line 2 (the outputs starting on their own line) would leave line 2 cached as well. That's 2 of ~6.5 misses a take, about 23 a tick on b30 and 90 on c20 [E], or ≤0.02 ms. It plays identically (the same answers, compared the same way), and its proof is statecrc plus `sweepmecheck` at 0 differ. It's tagged `[lever]` on the sweep's ground. It's small enough to ride along with the next change to `psp_sweepme.c` rather than earn a PSP pair of its own, since no pair could see it.

## 4. Why the ceiling is small

A take reads ~6.5 lines, ~45 times a tick at most (c20's held bench): ~290 misses. The collision part's sets that cost the most are elsewhere: on b30, object spheres 625 a tick, the coll tags 477, the cluster collideable object references 407, the BSP planes 381 (`spad_staging.md`, b30's collision table). Those are where a Me2Sc stage or the box's own data layout can find hundreds of misses a tick. The sweep's records were already made compact: the ME made them small precisely so the main CPU's take would be cheap.

## 5. When to ask again (revive_if)

- the takes grow by about 5× or more, to ~1,500 misses a tick (~0.3 ms, a pair's resolution): for example, the gather's whole answer (sweepme2's level and objects halves) staged in records of its own, or many more bipeds a tick on a heavier map;
- or the ME has spare time in the heavy stretches (the tick schedule's slices), and a Me2Sc stage is wanted for a set that already pays, so the records could ride on its starts at no extra start cost.

Until then, no box: the count above is the record.
