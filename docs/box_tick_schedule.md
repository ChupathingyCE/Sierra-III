# Box: the tick schedule (slice 1, the replay's pick-up points)

The lab's `research/opportunities/REBUILD.md` is the spine (its row "Tick schedule"); the box's shape and its requirements are the lab's `research/opportunities/two_core_tick.md` (the parallel phase, the serial phase, the hand-over; **requirement 1**: a job handed to the ME during the tick waits at most one slice of the ME's other work, never one of its whole jobs, and the ring takes tick work first). This page is the box's design page; this first slice is the replay's half of requirement 1, asked by the lead (the lab's coord/inbox/builder.md, 2026-11-19 16:0xZ) so that the pill sweep's ME placement (box_collision.md §11, `sweepme`) can be priced in the frame.

Labels: [M] PSP, [E] emulator, [D] code or the link, [U] inference.

**State (2026-11-19):** slice 1 designed and built (Sierra-III `builder/ticksched`, keep word `ticksched`, off by default); emulator proof passed (§10); its whole-frame PSP pair in the lab's Ready (`ticksched-held`).

## 1. Why this slice, measured

The pill sweep's level half on the ME (slice 1 with per-record claims) gains the tick on both maps: b30's flight -0.8 ms, c20's first stretch -0.5 ms, real [M] (findings `sweepme_claims_psp_b30`, `sweepme_claims_psp_c20`). c20's frame is flat because the main CPU's drawing gave +0.6 ms back, and the ME's render (replay) job ran 32.85 / 32.88 ms a job against 31.24 / 31.04 off (+1.7 ms) [M] (`sweepme_claims_c20_replay`).

**Read again from the same four logs (sierra-hw 44552450507, PSP-1000, c20_leg2 held), the +1.7 ms is mostly the batches' own work, run inside the replay** [M, derived from the `ME: running` and `ME: batch timing` lines]:

| run | side | render job, ms | nested in a yield, ms a render job | job less nested | batch jobs' own time, ms a render job | batch jobs picked up within |
|---|---|---:|---:|---:|---:|---:|
| 1 | off | 31.24 | 2.26 | 28.98 | - | - |
| 3 | off | 31.04 | 2.26 | 28.78 | - | - |
| 2 | sweepme | 32.85 | 3.61 | 29.24 | 1.25 (31,071 jobs, 97.8 us each) | 2,100 us |
| 4 | sweepme | 32.88 | 3.64 | 29.24 | 1.27 (32,136 jobs, 96.8 us each) | 2,005 us |

- The slot's job time counts the work run in its yields (`me_job_cycles` spans the job), so the batches' ~1.26 ms is inside the +1.7 by definition. **The replay's own slowdown is +0.26 to +0.46 ms a job (+1.2%)** [M, derived]: what the I-cache sharing and the claims' uncached words can cost it. (The nested figure is every slot's yields over the render jobs' count; the AI job's share is the same on both sides, so the difference stands.)
- The batch job's own time on the ME, ~97 µs, is 2.4x the first window's 40 µs (the first log window, few jobs): the sweep's code arrives cold at each job [U on M].
- **So the slice's worth on c20 is bounded:** ~0.3-0.4 ms a frame of the replay's refills, plus whatever grouping saves of the batches' own refills (up to ~half of their 1.26 ms if the second job of a group runs warm [U]). The rest of the drawing's +0.6 ms is the replay ending later because it carries the batches' work, which no schedule removes while the ME does the sweep; it is the price of the placement, weighed against the tick's -0.5 ms by the in-frame pair.
- **The pickup on c20 is 2.0-2.1 ms on average** [M] although the replay already serves the ring every record: the latency is set by the other occupants (the AI job between actors, mevisland's visibility jobs ahead in the FIFO ring; `sweepme_cause_b30`: 3.6-3.7 ms on b30), not by the replay's spacing. Slice 1's batch-first pick-up helps when a visibility job sits ahead of the batches during the replay; the AI job's looks are slice 2 (§7).

## 2. Contract

- **Reads:** the ring's pushed counter and its entries (as the drain does); the main CPU's urgent word (`RING_URGENT`, uncached, one writer: the main CPU sets it to the ticket + 1 while it spins in `psp_me_wait`, 0 after); the slice (`me_control.tick_slice_cycles`, set once after start: cycles in the low 24 bits, the record cap in the top 8).
- **Writes:** what the drain writes (popped, the jobs' results); a batch's records and claim words through `batch_job`, as its own ring job would; its counters (`tick_look`, a line of its own in the ME's section, read uncached by the main CPU).
- **Who calls it:** the frame's replay on the ME, before each record (`psp_ge.c` `inflight_serve` → `psp_me_replay_look`), only when the replay runs in flight (geinflight). The replay outside geinflight keeps its every-32-records audio look.
- **Unchanged:** the ring's order of completion (popped is one counter; jobs end in push order), audio's every-record look, the AI job's yields, the light pass's ring look, the ME's loop.

## 3. The design

### 3.1 Pick-up points a slice apart (every N records, sized by the clock)

Today every record is a pick-up: a batch job pushed during the tick runs alone between two records. With `ticksched`, a look before a record:

1. mixes a waiting audio block (unchanged: audio's deadline is its own);
2. if ring jobs wait, runs them only when **the slice has passed since the last pick-up** (`tickschedusN`, default 500 µs, requirement 1's ~0.5 ms), or **the records since reach the cap** (`tickschedrecN`, default none), or **the main CPU is waiting on a ring job** (`RING_URGENT`); otherwise it leaves them (counted as held).

**N is the clock, not a count:** the replay's records run 7-50 µs mostly, with long ones of 200-570 µs [M, box_draw_list.md §12's table], so a fixed N would be 10-70 records for one slice. The ME's cycle counter is one instruction (`mfc0 $9`), so the slice is measured directly; the record cap exists for a test of the count form.

**The first job of a burst is picked up at once** (the last pick-up is more than a slice old); the ones behind it gather for a slice and run back to back, so a burst of k jobs costs one refill a slice instead of one a job. A hand-over waits a slice (and the record in progress) at most, never a whole job: requirement 1's bound, inside the replay.

**A job the main CPU is already waiting on** (perception's rays, the poses, a batch's finish) is picked up at the next record: its wait grows by one record at most. The batch's own needs don't wait for the ME (a record not begun is taken back by the main CPU), so a batch held for the slice costs the ME a share of records, never a main-CPU wait; that share is the pair's `ME: batches` line.

### 3.2 The waiting batches first (the ring's order: tick work first)

The ring is FIFO with in-order completion (`RING_POPPED`), so a frame job ahead of the batches (mevisland's visibility, up to 18.5 ms [M]) holds them, and reordering the ring would need a done word per entry. Batches don't need the ring's order to be answered: each record has its own claim and made word, and the main CPU reads `made`. So **a pick-up first makes the records of every batch job waiting in the ring** (`ring_batch_first`: the entry's inputs dropped, its own `batch_job` run with its own arguments under its FPU control word, its stack sentinel checked), then drains the ring in order. When the drain reaches those entries, `batch_job` passes every record already made (a new test: `made[r]` set means the ME finished it; only the ME sets it, never for an unfinished record) and the entry costs a few uncached reads. Exact by construction: the same function on the same inputs, the claims' protocol unchanged. Batches under the one limit word (`mebatchlimit`) aren't made first: they have no per-record made word.

### 3.3 Both jobs' code against the ME's 16 KB I-cache [D, the link of sierra-build 44563640160, b74f3b0a, the sweepme recipe; `psp-nm -S`]

The ME's I-cache is 16 KB, 2 ways, 64-byte lines, LRU, with a cliff past 16 KB [M, hardware_facts.md].

**The sweep's batch, per job:**

| code | where it runs from | bytes |
|---|---|---:|
| `psp_sweepme_make` | eDRAM (MEXEC's copy) | 744 |
| `box_sweep_level_features` | eDRAM | 168 |
| `bsp_features` | eDRAM | 3,872 |
| `box_collbsp_sphere`, `sphere_from` | eDRAM | 28 + 168 |
| `sphere3d`, `sphere2d`, `edge_touches` | eDRAM | 1,276 + 1,676 + 264 |
| `axis_left_out`, `from_point`, `matrix_point` | eDRAM | 128 + 320 + 180 |
| **the copy's 11 named functions** | | **8,824** |
| `__wrap_memset` + `memset` (sphere_from's clear; the wrappers stay out of the copy, rbsw's list) | main RAM | 248 + 284 |
| `ring_run` (`batch_job` inlined), `me_ring_drain`, `psp_me_yield` | main RAM | 2,940 + 948 + 336 |
| **a batch job's whole reach** | | **~13.6 KB** |

MEXEC's tool counts 13 functions reached from `psp_sweepme_make` on b74f3b0a (12 on 5561c5ec); the table names the 11 the copy holds, the other two are the C library's plain routines it reaches [U on D: the tool prints a count, not the names]. Not all ~13.6 KB is touched each job, but its hot loops (`bsp_features`, `sphere2d`, `sphere3d`: 6.8 KB) are.

**The replay:** its copy is 107 functions, 154,440 bytes [D, the same build's mexec line]; on b30's flight the top 10 functions hold 45% of its I-misses in 48 KB and the top 40 hold 75% in 104 KB [E, docs/mexec.md]. **No hot/cold split brings it under 16 KB**, so the replay misses already; what the batch adds is the lines the replay would have reused across neighbouring records, evicted once a batch job.

**Together:** a batch job's ~13.6 KB is 85% of the I-cache, so each job taken between two records costs a near-full refill on both sides. At eDRAM's 25 cycles a line [M, docs/mexec.md]: ~210 lines back for the sweep (~15 µs) and up to ~256 for the replay (~19 µs), ~35 µs a job; at c20's 12.8 batch jobs a render job that is ~0.44 ms a frame [U on M], the size of the measured +0.26-0.46 ms (§1). Grouping k jobs a pick-up divides it by k.

**What this slice doesn't do:** shrink either footprint. The ring's own ~4 KB in main RAM (54-70 cycles a line) is a candidate for MEXEC's copy (rbsw's list), and the sweep's hot loops for a layout that keeps them clear of the replay's hottest sets (TGME's `codeorder --me-copy`); both are measured by the cache model (`meside`, `mexec`) before a PSP pair.

### 3.4 The ring's order

Slice 1 gives tick work first inside the replay (§3.2) without changing completion order. The ring's general order (per-entry done words so any occupant can take tick work first, the FIFO's long frame jobs behind it) is slice 3 (§7); it touches every ring job's collection and is priced on its own.

## 4. Placement and budget

- **Unit:** the ME, inside its render job. The main CPU's only change is two uncached stores around a spinning ring wait.
- **Cost of the look:** one uncached read (pushed) and two `mfc0` a record more than today when jobs wait; with the ring empty the same reads as `psp_me_yield`. The counters are ME-written stores on a line of their own.
- **Budget:** the replay's own time per job with the batch on, back to within the off side's spread (c20: 28.8-29.0 ms against 29.24 [M]), and the batch's pickup no worse than today's (2.0-2.1 ms c20, 3.6-3.7 b30). The frame decides (§6).

## 5. Means

- **B (work on change):** none.
- **A (data owned and packed):** none; the code's residency is §3.3's next step.
- **C (both cores on separate data):** the slice is the scheduling that lets C's first placement (the sweep's batch) share the ME with the replay. It moves no work.
- **What leaves:** refills: one a slice instead of one a job, on both the replay and the batch.
- **What it adds:** a branch and one uncached read a record when jobs wait; the batch-first scan (one request line dropped and read a waiting entry).

## 6. What's measured, and the pair

New line, both sides (`ME: replay looks`, every report window): the render jobs, **the job's time and its own time (the job less what its looks served: audio and the ring)**, the looks, pick-ups, ring jobs they ran, looks held to the slice, pick-ups made early for a waiting main CPU, batch jobs made first. With `ticksched` off the served time is the same `psp_me_yield` timed, so the replay's own time is read the same way on both sides.

Existing lines read with it: `ME: batch timing (claims)` (**the batch's pickup latency**, "picked up within", and the records' hand-over to the gather's ask), `ME: batches` (the ME's share of records), `ME: ring` (the waits of the other ring jobs: the cost of holding them), `ME: running` (the render job, busy share).

**The pair** (the lab's Ready, `ticksched-held`; the lead: the in-frame pair of slice 1 + this slice is what switches the placement on): one build, `sweepme` against `sweepme ticksched`, held b30_leg2 and c20_leg2, repeat 2, no statecrc; reader the builder. Read: the frame and the tick by stretch (b30_leg2's own windows, the lead's re-cut), then the replay's own time a job, the batch's pickup and the ME's share. Before it, one proof run on the PSP (a path newly on the ME): `sweepmecheck ticksched`, held b30 to 130 s, 0 differ and no ring failure.

## 7. Next slices (requirement 1's other occupants)

1. **Slice 2: the AI job's looks run ring jobs.** Today its inner looks ran 0 ring jobs (26 yields in a window) [M, `sweepme_cause_b30`], and the job runs 11-20 ms (worst 31.7). The same pick-up (slice, urgent, batches first) at its looks, under its abort word.
2. **Slice 3: the ring's order.** Per-entry done words, so tick work (batches, rays, poses) is taken ahead of frame work with a later reader (mevisland's visibility); or the visibility walk looking at the ring between clusters (AIMEVISLEFT's lesson: leaving it to the loop cost the frame +5.5%, `levers.tsv`).
3. **The code's residency** (§3.3): the ring's code into the ME's copy, the sweep's hot loops placed against the replay's sets.

## 8. What it replaces, and earlier tries (levercheck)

- **Replaces:** `inflight_serve`'s `psp_me_yield` before every record (geinflight, `review_geinflight.md`), under the keep word.
- **MESCHED** [stopped as a whole; steps 1-2 merged, w142]: "the ME's scheduler rewritten: job classes with deadlines (audio hard, ring soft, replay chunked, background in bounded slices)". Its steps that landed were the counters and a slot job's yields not running a render job whole (menestrender). This slice is a narrow piece of its "bounded slices" with a measured reason (the batches' refills); the classes and preemption aren't revived.
- **R2Y** [stopped]: the sound copy job yielding to the ring every 4 KB: refusals 0 but the bunker +8.1% frame; a yield's own cost is real, so the pick-up runs only when jobs wait and the slice has passed.
- **AIMERINGLOOK** [on]: the AI job's looks serve the ring every ~1 ms of its clock; they ran ~0 ring jobs on b30. Slice 2's ground.
- **AIMEVISLEFT** [stopped]: leaving a visibility job to the ME's loop from the AI job's drain cost the frame +5.5%; why slice 3 is priced alone.
- **MECHUNK**, **DRAINFLIGHT** [on]: the replay's hand-over in chunks and the drain kept in flight; unchanged here.
- `levercheck pickup` names no lever on this ground; `levercheck --ground me-scheduling` lists the above.

### Outside leads on this ground (the lab's levers.tsv, outside eyes round two, `research/outside_eyes/read_round2_2026-11-16.md`)

- **MEMARSHAL** (DeepSeek V4.1 Flash: the ME chases headers, tags and cluster lists and writes a packed per-type tick view the main CPU streams; PROPOSED, unbuilt, "lives or dies on the ME's p99"). **Not taken in this slice:** it's a new ME occupant with a deadline inside the tick, so it needs exactly what this box provides (a bounded pickup, tick work first) before its p99 can be met; it's a candidate occupant for the parallel phase once slices 1-3 bound the ME's waits, and the world store's one-tick-old copy (wsold) is the nearest thing built.
- **GLOBALSPLIT** (Kimi K3.1 Pro: hot globals given one writer each, a main-CPU and an ME copy in place of shared lines; PROPOSED, needs the top 64 lines' reader/writer census). **Not taken here, adopted as a rule of the box:** the slice's new words follow it already (the urgent word has one writer, the main CPU; the look counters one, the ME; the slice is written once before the ME reads it), and two_core_tick.md's per-core counters (hazard kind 2) are its form for the parallel phase. The census it asks for belongs to that phase, not to the replay's pick-ups.

## 9. Proof

- **Exact by construction:** the batch-first pass runs each batch job's own code on its own inputs; the drain is the same drain, later; the made-word test is never true without the pass (made is cleared at each record's add).
- **Emulator** [E] (the ME's jobs run simulated inside the push, so the look's ME side doesn't run there; what runs is the main CPU's urgent word and `batch_job`'s new test): play and seeds identical with `sweepme` on, `ticksched` off and on, on b30_leg2, c20_leg2, d20_play1; `sweepmecheck` 0 differ; strict 0. Passed: §10.
- **PSP:** the proof run (§6), then the pair.

## 10. Results

- **Emulator proof, 2026-11-19** [E] (the lab's sierra-batch ticksched1ida 44574542407 and ticksched1idb 44574604888, build fbd536d5, the sweepme recipe; findings `ticksched1id_identity`): play and both seeds identical with `sweepme` on, `ticksched` off against on, over 600 ticks each on b30_leg2, c20_leg2 and d20_play1, seeds 1 and 2 (6 pairs); `sweepmecheck ticksched` identical to off on all three maps, **0 differ** (b30 3,615, c20 3,891, d20 12,810 answers compared); `mebatchsimhalf ticksched` on b30 identical (records made by both CPUs interleaved: ME 2,106, here 1,568); **strict 0** on all 16 runs. The batch first sent as one job of 16 (ticksched1id 44573588252) was refused by the batch's 300-minute budget (341 estimated at the pool's 4 at once) and ran as these two halves. As §9 says, the ME side of the looks doesn't run in the emulator; the PSP proof run comes first in the pair's ask.

**The held pairs [M]** (2026-11-19, `- | ticksched` on the build of 421af56b, sweepme slice 1 on in both sides, two runs a side, read with `tools/abreport.py`; the lab's measurements.tsv `ticksched_held_psp_b30`, `ticksched_held_psp_c20`):

| Pair | Stretch | Frame | Drawing | Tick | Verdict |
|---|---|---:|---:|---:|---|
| b30_leg2, PSP-3000 (the lab's sierra-hw 44678156082) | flight | 86.8 → 85.8 (−1.1%) | 27.6 → 27.3 (−1.2%) | 28.6 → 28.4 (−0.9%) | real |
| | opening, fight, beach | ±0–2% | | | noise |
| c20_leg2, PSP-1000 (44678181784) | opening | 52.4 → 52.5 (+0.2%, noise) | 13.7 → 13.5 (−1.3%) | 21.1 → 21.4 (+1.0%) | drawing and tick real, frame flat |
| | first, play | | | | noise |

All eight runs exited cleanly. The slice wins about 1 ms of frame on b30's flight. On c20 it's a wash: ~0.2 ms moves from the drawing into the tick on the opening, and the frame is unchanged. Whether that switches on the sweep's placement (sweepme slice 1) is the lead's call.

**Switched on (the lead, 2026-11-20 00:1x):** slice 1 is on by default in the tock with the sweep's slice 1 (keep word `ticksched`, off: `tickschedoff`; Sierra-III builder/tock-sweep). **A known cost, kept:** each pick-up point costs the replay +0.3–0.5 ms a job on the ME. That's the ME's floor for now, not a reason to hold the placement. Slice 2 (the AI job's looks) is next.

## 11. Slice 2's design: the AI job's looks (DRAFT for the lead, 2026-11-20 08:0x, builder; the lead 07:4x; nothing built)

**What's measured** [M, sierra-hw 44533583170, the PSP-3000, b30_leg2 held, `sweepme`; the `ME: inside the AI job's actors` line]: **1 yield a 300-frame window, 0 ring jobs, 0 looks passed by for the stack**, while the AI job runs 11-20 ms a frame (worst 31.7) and ring jobs wait 4-16 ms on average for a pick-up (worst 20-44 ms). The stack never stops a look; the looks barely happen.

**Why [D, psp_me.c and psp_fixups.py on the tock]:** `psp_me_yield_inner` counts a yield only once the ME's clock is `INNER_GAP_CYCLES` (~1 ms) past `inner_mark`, and every yield *between* actors (`psp_me_yield_slots`) moves `inner_mark` to now. So an actor shorter than 1 ms never yields inside, and a long one yields only where its thinking reaches a look site (`psp_aime_ring_yield`: the collection's word's looks, perception's props, `aimepathlook`'s nodes) more than 1 ms after it started. That one yield a window says the long actors spend their time where no look site is, or the window's long actors are few. **Which one isn't known, and only the PSP can tell** (the emulator runs the ME's jobs inside the push, §9).

**Step 0, a census of the looks' reach (recon, PSP, one run):** for each AI job, the calls to `psp_aime_ring_yield` and how each ended (gated off at the call, under the gap, low stack, nothing waiting, served); the longest stretch between two looks and the step tag that held it (`psp_aime_step_tag_now`); and how long ring jobs waited while the AI job held the ME (push to pick-up, by occupant). Counters on the ME's own line, read uncached, as `tick_look`. This says where looks are missing before any is added.

**The slice (after step 0):**
1. **The same pick-up as the replay's** (§3.1-3.2) at the AI job's looks: the tick slice (`tickschedusN`, 500 µs) instead of `INNER_GAP_CYCLES`'s 1 ms; at once when the main CPU waits (`RING_URGENT`); the waiting batches' records made first (`ring_batch_first`), then the drain, which stops at the collection's word as it does today. **The gap is measured from the last pick-up, not the last yield between actors**, so a run of short actors no longer resets it.
2. **Look sites where step 0 finds the long stretches**, each placed where the thinking holds nothing a ring job reads or writes, as AIMERINGLOOK's are (the reason written at each site). The ring jobs that can run there (rays, points, poses, the sweep's batches) read last tick's copies or their own inputs (wsold, the rays' inputs), not an actor's state in the making; each new site is checked against that list.
3. **The stack:** a batch job needs `RING_JOB_STACK_BYTES`, less than the rays' allowance the gate asks today. The batch-first pass runs when there's room for a batch even when there isn't for the whole drain.
4. **Unchanged:** audio's look inside actors (AIMEAUDIOLOOK), the abort and the late word (no look once either is up), the ring's completion order.

**Switch:** the keep word `ticksched2` (off: `ticksched2off`), off by default until its pair; under `ticksched`'s feature bit, so `tickschedoff` turns both off.

**Proof:** exact by construction as slice 1 (§9: the same jobs on the same inputs, picked up earlier); the emulator for play and seeds identical with it on and off (the main CPU's side and `batch_job`), strict 0; on the PSP, `sweepmecheck ticksched ticksched2` held b30 to 130 s, 0 differ, no ring failure; then the pair, both sides in one build (`ticksched | ticksched ticksched2`), held b30_leg2 and c20_leg2, repeat 2.

**Budget:** the AI job's own time (the job less what its looks served) within the off side's spread; ring pick-up latency while the AI job runs down from today's 4-16 ms average toward the slice's 0.5 ms; the frame decides.

**Open for the lead:** step 0 is one PSP run (~20 min, either PSP, held b30_leg2 to exitat130, `sweepme`) with a recon build; the ask goes to the master once it's built and its emulator run is clean.
