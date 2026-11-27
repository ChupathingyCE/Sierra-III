# Box: the tick schedule (slice 1, the replay's pick-up points)

The lab's `research/opportunities/REBUILD.md` is the spine (its row "Tick schedule"); the box's shape and its requirements are the lab's `research/opportunities/two_core_tick.md` (the parallel phase, the serial phase, the hand-over; **requirement 1**: a job handed to the ME during the tick waits at most one slice of the ME's other work, never one of its whole jobs, and the ring takes tick work first). This page is the box's design page; this first slice is the replay's half of requirement 1, asked by the lead (the lab's coord/inbox/builder.md, 2026-11-19 16:0xZ) so that the pill sweep's ME placement (box_collision.md §11, `sweepme`) can be priced in the frame.

Labels: [M] PSP, [E] emulator, [D] code or the link, [U] inference.

**Joined (2026-11-26, builder2; the lab's briefs/tc_tick.md task 0):** this page had two versions that had parted from master/tock162 cd1c036b: `builder2/mesched` d3bb4e82 (2026-11-24, adds §7a) and `builder/ticksched2-step1` c77092d4 (2026-11-20, adds §11 and the 11-20 state below). This branch, `builder2/ticktwo`, is cut from d3bb4e82 with c77092d4's page changes brought in whole: the 11-20 state and §11 with its three subsections. No line of either was dropped or reworded (the lab's `toolbox/pagejoin.py` checks it). The only line both changed is the state line: the newer version's stands first, and the older version's follows, marked superseded. Where they read differently elsewhere (§7a item 2 against §11), the newer stands and a joining note sits on its own line. No code came from c77092d4's branch: slice 2 step 1's code (`ticksched2`, psp_me.c) stays on `builder/ticksched2-step1` only. The tick's two-core design (tc-tick) goes in new sections after §11, as §12.1-§12.4.

**State (2026-11-19):** slice 1 designed and built (Sierra-III `builder/ticksched`, keep word `ticksched`, off by default); emulator proof passed (§10); its whole-frame PSP pair in the lab's Ready (`ticksched-held`).

*The other version's state line (`builder/ticksched2-step1` c77092d4). Superseded on 2026-11-20 at 15:0x, when the lead kept slice 2 step 1 off after its pair (§11's last subsection):*

**State (2026-11-20):** slice 2's step 1 built behind `ticksched2` (off), its emulator proof passed and its PSP pair asked (§11, "Slice 2, step 1: built"). **(2026-11-19):** slice 1 designed and built (Sierra-III `builder/ticksched`, keep word `ticksched`, off by default); emulator proof passed (§10); its whole-frame PSP pair in the lab's Ready (`ticksched-held`).

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

## 7a. The ME lane's step 1: the pick-up wait cut (builder2, 2026-11-24, design before code)

The lab's brief `coord/briefs/me_relief.md` (Sheepdog01 2026-11-24 14:57: the ME's load is cut first) puts the scheduler first, because **every later ME placement waits behind the pick-up**. The figures are quoted from the lab's `findings/CURRENT.md` (the ME rows: busy and pick-up wait on b30's live fight, posepair2a [M]); the by-kind split on today's tock is the lab's PSP ask `mekinds1` (step 0).

**What holds a job up, as far as is known [D, U]:** a ring job waits for the occupant the ME is running to reach a point that looks at the ring. The replay looks every few records (slice 1). The light pass looks at its own ring points. **The AI job's yields run no ring jobs** (0 in a window, `sweepme_cause_b30` [M]) for 11-20 ms a job, worst 31.7. Audio blocks are served everywhere. The ring is FIFO, so frame work with a later reader (mevisland's visibility) sits ahead of tick work. An 8.5 ms average with a 32-33 ms worst is the shape of the AI job's length, not the replay's spacing [U].

**The order, each its own switch, exact by construction (the ring's jobs run their own code on their own inputs; only when they run moves):**
1. **A census first, no behaviour change** (keep word `mewaitwho`): for every ring job, its wait from push to pick-up, charged to the occupant running when it was pushed (render, AI, lights, audio, visibility, idle), and the kind of job that waited (batch, ray, pose, visibility). One PSP run beside `mekinds1`'s, or folded into it if the build carries both. It turns the [U] above into the ranking slices 2 and 3 are ordered by.
2. **Slice 2: the AI job's yields serve the ring** (§7 item 1): the replay's pick-up (the slice, the urgent word, batches first) at the AI job's looks, under its abort word. The AI job's own state is untouched by a ring job (the ring's jobs read last tick's world: requirement 1). Proof: play and seeds identical in the emulator (where the ME runs inside the push, so this proves the main CPU's side); on the PSP, the pick-up wait and the AI job's time before and after, and the ME alive.
   *(Joined 2026-11-26: this slice was already designed, censused and built as step 1 on 2026-11-20 on the other version, §11; it was kept off after its pair, see §11's last subsection. This item stands as the 11-24 plan; §11 is what had been done.)*
3. **Slice 3: tick work ahead of frame work** (§7 item 2): per-entry done words so a batch, ray or pose is taken before a waiting visibility job. The completion order stays push order for readers that need it (popped stays one counter, per its readers).
4. **MESCHED's classes and admission** (the lab's levers.tsv MESCHED, steps 3+): audio a hard deadline, the ring soft, the replay chunked, background in bounded slices, admission against the ~75% ceiling [U, `layout_plan.md` §1]. It comes after 2-3 because it is a policy over the pick-up points they add. Its prerequisite hazard (the ME's sp inside the main thread's stack at a fault) is checked first.

**Measured by:** the pick-up wait (average and worst) and the ME's busy share on b30's live fight, the PSP-1000, before and after each slice, both sides on the lab's `recipe.env`. **Proof standard:** play exact; on the PSP no missed audio block, the ME alive to the end.

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

**Step 0's census (built, 2026-11-20; branch `builder/ticksched2-step0` 62dcd956; keep word `aimelookcensus`, `PSP_RECON=1` builds only):** every call into `psp_aime_ring_yield` (`psp_me_look_census`, out of line and out of the ME's copy) and every yield between actors is a look, and the AI job's start and end (in `me_serve`) close a stretch too. Counted on the ME's own cached lines (as `tick_look`), written back at the job's end and read uncached in `psp_me_report`, which moves an epoch so the ME starts the counts again at its next AI job (the emulator's simulated AI job opens and closes the census in `psp_me_submit`). Three lines a window:
- `ME: the AI job's looks (aimelookcensus)`: AI jobs; calls, and how each ended: stopped at the call (looks off, the job's mark down, the word late, the abort), or in `psp_me_yield_inner` under the gap, low stack, audio only, the ring not ready, nothing waiting, served (and its ring jobs); yields between actors.
- `ME: the AI job's stretches without a look`: the stretches between looks, the longest with the step and tag it ended in (between actors, the actor's last step, not step 0), a job's longest on average, a histogram (under 0.5/1/2/5/10 ms and over, at 333 cycles a µs), and the three steps holding the most time in stretches over 1 ms (the census's steps, `PSP_AIME_STEPS`; -1 for none).
- `ME: the ring while the AI job ran`: ring jobs pushed and run inside it; looks that saw jobs waiting and the most at one; waits from a look that first saw jobs to one after which none were (the ring doesn't record push times, so a wait counts from the first look that saw it: a floor).

The step comes from `psp_me_stash_place`, which the recon build's step marks already call (now out of line). The recon build's ME copy needs its own list, `rbtswr` (rbtsw less `actor_update`, which then runs from main RAM): the tock's recon build on rbtsw came to 270,416 bytes, over the 262,144 room, with or without the census (the lab's sierra-batch 44772153599 lists the copy's functions). [E] proof, the emulator (which runs the ME's jobs inside the push, so the census reads no jobs there): the lab's sierra-batch 44779879306 (aimelook0f, 62dcd956, the tock's recipe with `PSP_RECON=1` and `PSP_MEXEC=rbtswr`), b30_leg2 held to 60, seeds 1 and 2, `aimelookcensus` off and on: strict 0, no crash, **play and both seeds identical over all 1,800 ticks** (statecmp), and the lines print each window. The emulator runs the AI job as the PSP does, inside its push, so its counts are true counts but its times aren't [E]: in the AI windows of seed 1, ~1,400-2,000 calls a window, all but 0-3 under the gap (`aimeringlook`'s yields are those 0-3), and 150-420 yields between actors; no ring job waits there (the emulator runs ring jobs inside their push). Only the PSP says where the stretches are long.

**The slice (after step 0):**
1. **The same pick-up as the replay's** (§3.1-3.2) at the AI job's looks: the tick slice (`tickschedusN`, 500 µs) instead of `INNER_GAP_CYCLES`'s 1 ms; at once when the main CPU waits (`RING_URGENT`); the waiting batches' records made first (`ring_batch_first`), then the drain, which stops at the collection's word as it does today. **The gap is measured from the last pick-up, not the last yield between actors**, so a run of short actors no longer resets it.
2. **Look sites where step 0 finds the long stretches**, each placed where the thinking holds nothing a ring job reads or writes, as AIMERINGLOOK's are (the reason written at each site). The ring jobs that can run there (rays, points, poses, the sweep's batches) read last tick's copies or their own inputs (wsold, the rays' inputs), not an actor's state in the making; each new site is checked against that list.
3. **The stack:** a batch job needs `RING_JOB_STACK_BYTES`, less than the rays' allowance the gate asks today. The batch-first pass runs when there's room for a batch even when there isn't for the whole drain.
4. **Unchanged:** audio's look inside actors (AIMEAUDIOLOOK), the abort and the late word (no look once either is up), the ring's completion order.

**Switch:** the keep word `ticksched2` (off: `ticksched2off`), off by default until its pair; under `ticksched`'s feature bit, so `tickschedoff` turns both off.

**Proof:** exact by construction as slice 1 (§9: the same jobs on the same inputs, picked up earlier); the emulator for play and seeds identical with it on and off (the main CPU's side and `batch_job`), strict 0; on the PSP, `sweepmecheck ticksched ticksched2` held b30 to 130 s, 0 differ, no ring failure; then the pair, both sides in one build (`ticksched | ticksched ticksched2`), held b30_leg2 and c20_leg2, repeat 2.

**Budget:** the AI job's own time (the job less what its looks served) within the off side's spread; ring pick-up latency while the AI job runs down from today's 4-16 ms average toward the slice's 0.5 ms; the frame decides.

**Open for the lead:** step 0 is one PSP run (~20 min, either PSP, held b30_leg2 to exitat130, `sweepme`) with a recon build; the ask goes to the master once it's built and its emulator run is clean.

### Step 0's result (2026-11-20, builder; the lab's sierra-hw 44785052822, the PSP-1000, row `aimelook_census_b30`) [M]

One recon run, held b30_leg2 to 130 s with `sweepme`, 2,109 AI jobs on the ME:
- **The looks are already there:** 156 calls a job, one every ~0.07-0.13 ms of an 11-20 ms job. Stretches without a look are almost all under 0.5 ms (356,211 of 358,283); a job's longest averages 0.82 ms. Only 66 stretches over 1 ms end outside any step and 111 in step 1, with the longest at 10.1 ms.
- **The gap holds the ring back:** 98.3% of looks end under the 1 ms gap, and only 0.08% serve. The 13 yields a job between actors each reset the gap.
- **Ring jobs wait 3.24 ms on average while the AI job holds the ME** (longest 17.9 ms, a floor). 2,176 were pushed in that time and 2,859 run inside it.

**What it decides [U on M]:** slice 2 needs no new look sites. Step 1 alone, the replay's pick-up timed from the last pick-up rather than the last yield between actors (500 µs slice, urgent at once, batches first), would serve a waiting job within ~0.5 ms of its first look, against 3.24 ms now. The long stretches outside a step (the 10.1 ms one included) are the only place a site might still pay. They're too few to build for before step 1 is priced.

### Slice 2, step 1: built (2026-11-20, the builder's agent; the lead 12:1x: step 1 only, no new look sites)

Sierra-III `builder/ticksched2-step1` b480d399 (on master/tock162 cd1c036b). Keep word **`ticksched2`**, off by default, off word `ticksched2off`; set only with `ticksched`'s feature bit, so `tickschedoff` turns both off. No build switch.

**What changed [D, psp_me.c]:**
- **Every look inside an actor serves the ring on slice 1's policy** (`ai_ring_look`, out of line, called from `psp_me_yield_inner` when the look wants the ring): when jobs wait, it picks them up once the tick slice (`tickschedusN`, 500 µs) has passed **since the last pick-up**, or at once when the main CPU waits on a ring job (`RING_URGENT`); otherwise it leaves them (held). A pick-up makes the waiting batches' records first (`ring_batch_first`), then drains the ring in order. The drain stops at the collection's word, as it does today, and so does the batch-first pass now (it takes the stop word; the replay passes none, so slice 1 is unchanged).
- **The last pick-up** (`ai_pick_mark`) moves at a look's pick-up and at a yield between actors whose drain ran ring jobs (a whole-ring drain is a pick-up). A yield between actors that found the ring empty no longer restarts the ring's clock; it still restarts the 1 ms gap, which now gates only audio's inner look (unchanged) and, with `ticksched2` off, the old ring drain (unchanged).
- **The stack:** the batch-first pass needs `RING_JOB_STACK_BYTES` + 4 KB of frames above the guard (`AI_BATCH_STACK_ROOM`, 8 KB); the drain still needs `INNER_STACK_ROOM` (40 KB). With room for the batches but not the drain, the batches are made and the rest of the ring is left for the next look (counted).
- **No look once the late word or the abort is up:** the caller (`psp_aime_ring_yield`) tests both before it calls, unchanged.
- **Counts** (ME-written, a 64-byte line of their own, read uncached in `psp_me_report`): `ME: the AI job's ring looks (ticksched2)` each window: looks, looks that saw jobs waiting, held to the slice, pick-ups (the µs they served, how many early for a waiting main CPU), ring jobs run, batch jobs made first, pick-ups with room for the batches only, looks with no room.
- **The look's cost:** at each of the ~156 looks a job [M, step 0], one cached read of the ring's state and one uncached read of `RING_PUSHED` (the clock and the urgent word only when jobs wait). The old path read the clock only.
- **The ME's copy:** none of these functions is in rbtsw's list; `ai_ring_look` is `noinline` so that LTO can't copy it into the think's copied functions through `psp_me_yield_inner`.

**Emulator proof [E]** (2026-11-20; the lab's sierra-batch ticksched2ida 44790267996 and ticksched2idb 44790300441, build b480d399 on the tock's recipe with `PSP_MEXEC=rbtsw`; findings `ticksched2_identity`): play and both seeds identical with `ticksched2` off against on, 600 of 600 ticks each, on b30_leg2, c20_leg2 and d20_play1, seeds 1 and 2 (6 pairs); `sweepmecheck ticksched2` on b30 identical to off, 3,615 answers, **0 differ**; **strict 0** on all 13 runs, no crash. The ME's copy is 260,984 of its 262,144 bytes: rbtsw still fits, with ~1.1 KB to spare. As §9 says, the ME's side of the looks doesn't run in the emulator, so this proves the build and that nothing on the main CPU moved; the ME's path is the PSP pair's to prove (its runs read the new line and `ME: ring`).

**The PSP pair:** the lab's `projects/sierra/coord/handoff/ticksched2_2026-11-20.md` has the ask (held b30_leg2 to 130 s and a30_leg1 to 460 s, `ticksched | ticksched ticksched2`, repeat 2).

**Its outcome (joined 2026-11-26 from the lab's `findings/levers/redesign.tsv` BOX-TICKSCHED and `findings/mesched_case_2026-11-26.md`:12; the pages' two versions held neither):** KEPT OFF BY DEFAULT 2026-11-20 (the lead 15:0x): the PSP pairs read b30 flat (`ticksched2_psp_b30`) and a30 inside the spread (`ticksched2_psp_a30`); "0 RING_URGENT pick-ups on either map, so the main CPU never waited on a job the AI job held… the step-0 waits (3.24 ms) were real but off the frame's path". revive_if: "a stretch with RING_URGENT pick-ups > 0 during the AI job". So the 11-20 state line at the top ("its PSP pair asked") is superseded by this, 2026-11-20 15:0x. §7a item 2's slice is the ground of this stopped step, not a new one.

## 12. The two-core tick's schedule (tc-tick, builder2, from 2026-11-26)

The lab's `briefs/tc_tick.md` (the lead 19:5x, re-briefed 20:5x). Its "§11.1-§11.4" are this page's §12.1-§12.4, numbered after the joined page's last section (task 0). The goal's frame is one tick a frame at 30 fps on both cores (Sheepdog01 2026-11-26 19:3x). Every figure here is labelled by how it was measured. A figure from the archive or a two-tick frame is a lead, never the goal's.

- **§12.1, the ME's column at 30 fps [M]:** waits on the counters' b30 PSP run being read (the brief's due date: a day after it).
- **§12.2, the two uses side by side [D]:** on §12.1's figures.
- **§12.3, the phase plan [D]:** below.
- **§12.4, the split and its price [D cost sheet]:** last, on §12.1-§12.3.

### 12.3 The phase plan [D] (written 2026-11-26 22:28)

**What it's drawn against.** The order is today's: `objects_update` and the game tick, as tc-copy read it from a fresh tree at engine 2e5eac00 (ship form + `PSP_TICK2_MOTIONNEW=1`, Sierra-III `builder/tick2-coll` 4c299ada). These are box_world_store.md's tc-copy section, 1c(i) "Today's order" and §3's table, on `builder2/storecopy` 34147323. I didn't re-read the source this run. `builder2/ticktwo`'s own head (b4d41c74, over 34825b3f) is older than that and holds none of the copy. So **task 5's build takes `builder2/tccopy` in by a merge**, not a rebase (a history rewrite waits for Sheepdog01), and the table below is checked again on the merged tree before the first code commit.

**The parallel jobs, by the order the brief sets** (20:2x (1)(b), (3)):
1. **The pill sweep on the ME: G whole, both halves.** Today G (store_coll.c:404-412) gathers each biped's reach list in the biped pass, after the walk. Its level half is already made on the ME when its answer is pending (SWEEPME, collisions.c:2149). In the plan, all of G goes to the ME at the copy point: the level half as today, and the objects' half from the copy (sweepme2's form, box_world_store.md §12). It packs the same lists (`store_coll_entries`, `psp_bs2_arena`, `psp_bs2_slot_of`) and the same reach-guard entries. The main CPU's M takes them as it takes G's today. M itself (the sweep through the list, `collision_move_point`, and the relink) **stays on the main CPU, in its order**. Biped motion on the ME is s1-me B's, and it has no case yet (20:2x (3)).
2. **s1-me Part A, the AI's think a tick old:** a slot in this plan, not a choice. Where it's posted (tc-copy §3's (a)-(c), or the record's window) is Part A's sheet's to settle (21:4x (4)). The plan carries each option and the requirements below.

**The order against the tick's phases** (tick N; "copy" = `psp_wsold_refresh`'s copy; MC = the main CPU):

| # | Phase (where, today's order) | Core | Writes what a parallel job reads? | The job's inputs and outputs here |
|---|---|---|---|---|
| H0 | **the fence**, new: tick N-1's jobs that hold the copy, joined (tc-copy §3 phase 0) | MC | — | the sweep was joined in N-1's biped pass, so this is a check (`wsold: refresh while held` 0). The think, in a form that runs past the tick, joins here and its record is installed in H1 |
| H1 | `ai_update` (game.c:435): Part A's record installed, then every actor's input refresh (`actor_input_update` actors.c:1898-1981), then the control half (:5019-5038) | MC | the actor's input block (1d), the AI's tables | Part A's post point if it keeps AITICKOLD's rule: **after** the input refresh (aime_me.md:135 [archive]). Option (b) puts the copy before H1 |
| H2 | players' stage, effects, scripts (game.c:440-466) | MC | team, a script's moves, creates and deletes | written before the copy point and marked: in the copy |
| H3 | activation (objects.c:10640-10687) | MC | the partition, deletes | the same |
| **H4** | **the copy point**: the refresh, the D-cache write-back, then the made word = N, written back last (tc-copy §3) | MC | the copy (its only writer) | **post the sweep** (G's job for every biped staged so far, in index order), with N. Option (c) posts the think here as well |
| P1 | **the walk** (objects.c:10695-10741): every non-biped's whole update, in index order | MC, **beside the ME's G** | everything the objects' half reads (1c(i)'s non-biped rows); **into bipeds**: a vehicle's push (physics.c:1389, :1405), an elevator's riders (device_machines.c:499) | not read by the job: it reads the copy. Each write is marked. The two kinds of change are below |
| P2 | the biped pass's units part (`unit_update` objects.c:9441) and I | MC, beside | a biped's own record | marked, refused at the take (below) |
| **J** | **the join**, at G's place in `store_biped_pass` (store_coll.c:404) | MC | — | the lists and guard entries read only from here on |
| S1 | M (store_coll.c:414-422), each biped in index order: the take, or a live gather if refused, then the sweep and the relink | MC | the bipeds' pill, position, velocity | the job's outputs, read through the take's tests |
| S2 | after M (:10749-10789; game.c:471-482): new objects' first update, deletes, the pose-ahead, GC | MC | everything, marked | next tick's copy |

**The check (the brief's task 3):** every input the sweep's job reads is written in H2-H3 of tick N, or in P1-S2 of tick N-1, and refreshed at H4. Nothing the job reads is written between H4 and the post. Every output (the lists, the guard entries) is read in S1 only, after J. This holds as long as every writer in the first group is marked, which **wsoldcheck proves, not this table** (tccopy-check, in flight). Two order conditions are carried from tc-copy 1d. G's lists are made before they're taken: now they're the job's own output, read after J. The AI's input refresh is done before the think is posted: H1, before any of Part A's post points.

**The walk's writes into bipeds, in the serial order.** In this plan they stay where today's order has them: in the walk (P1), on the main CPU, in index order, before M (S1). M runs after them on the main CPU, so the biped it moves is the pushed or carried one. That's today's play. **No queue is needed in this plan.** The sweep's job built that biped's list from where the biped stood at H4. The push or carry marks it (FIXES_WSOLD's three marks, tc-copy commit 1), so the take refuses the list and M gathers live from where the biped is now. The cost is a live gather for each pushed or carried biped, counted on the `wsold:` line as a refusal. **The queue is s1-me B's requirement, not this plan's:** if M moves to the ME, those writes land while M runs. Then a vehicle's and an elevator's update is held to a serial step before M is posted, or their writes into bipeds are queued as requests applied at the join, in the walk's index order (tc-copy §3, "the walk's writes into bipeds").

**The walk's other writes beside G: the play class, for the lead.** The objects' half reads the world as it stood at H4. The walk then changes it in two ways:
- **A relink** (a non-biped moving across clusters) bumps the cluster stamp (`tick2_cs_seq`). The reach guard reads it as changed: the take is refused and M gathers live, so play is unchanged and the extra gathers are counted (the lead 20:5x, tc-copy 1e (i)).
- **A change in place** (a sphere or pose moving inside its cluster, a flag, a death, a breakable surface broken by a projectile) bumps nothing. So the job's list shows that object as it was at H4, and M sweeps against it. **That is the parallel phase reading last tick's world, as the box's contract has it** (REBUILD.md:125; box_world_store.md §1, line 14), and it is a play change.

So the plan's first build had a choice for the lead, now ruled:
- **(α) exact: ruled, the lead 2026-11-26 22:4x (3)** ("The take compares each reach object against its copy record and refuses on a difference; its cost sits on 12.4's added side, priced in full"). At the take, M compares by value each object in the list's reach clusters against its copy record (sphere, flags, links, dead; the models' node count) and refuses on any difference, then gathers live. The compare's cost and its refusals' live gathers go on §12.4's added side, in full (§12.4's line A5).
- **(β) plays the same: not ruled.** The take would trust the list but for relinks and marked bipeds. It rests on an 11-09 17:30 collision ruling quoted from two_core_tick.md, and that page is "kept as reference under the base" (Sheepdog01 2026-11-26 20:4x), never imported as a ruling; his 20:0x made the old documents "not a source for a brief or a ruling". So (β) can come back only if that ruling passes the import gate and §12.4 shows (α)'s compare costs more than its job saves (22:4x (3)).

(α) keeps task 5's "play exact" check meaningful, and its refusal count is what would price (β) from the same run. **The breakable flags:** the level half on the ME reads the live BSP's 32 B row while P1's projectiles can break it. So the job reads a snapshot of that row taken at H4 (tc-copy 1d), and a break in P1 marks the BSP so its takes are refused under (α).

The rest of §12.3 is noted by the lead, not ruled (22:4x (3)): G whole to the ME at wsold's point, M on the main CPU after the join, vehicle pushes and elevator carries kept in the walk and marked. §12.1-§12.4 go to the lead together (task 4), due 2026-12-02.

**The hand-over** (the post and the join are the tick loop's phase calls, which sit beside slice 1's step files: they go to the lead and the builder as a request, never an edit; the brief's "The lane"):
- **The post** (H4): one ring job (§3.4's ring; requirement 1: a job handed to the ME waits a slice at most, §3.1). It carries the tick number N and the bipeds staged, in index order. No `kcall` on the tick's path (two_core_tick.md:62, [archive]).
- **The ME's side:** at the job's start, `psp_me_cache_refresh()` (psp_me.c:2482-2486). Then the made word, read uncached. If it isn't N, the whole job is **not held** and answers so at once (tc-copy §3, "If the copy isn't ready"). Then the bipeds one at a time: each biped's list, its guard entry, then a done word for it, written back and set last. Between bipeds it reads a **stop word** uncached.
- **The join** (J): the MC reads the done words uncached. **While it waits**, nothing in today's order is left to run before G's place: P1 and P2 are what it ran beside the job. The wait is counted (µs a tick, its peak) on a new `tick2 sched:` line. Measuring it is §12.4's.
- **If the ME is late: the fallback, exact.** At J the MC sets the stop word and waits for the ME's acknowledgement, which comes within one biped's gather. Then it runs **today's G on the main CPU** (`store_coll_begin` and the gathers, unchanged code) for every biped with no done word. Those bipeds are counted as `late`. A not-held job (the made word wrong, wsold off, a map's first tick) takes the same path for every biped. A biped the copy can't answer (past the slots, the pool's nodes (1024 since the lead 22:4x (2), builder2/tccopy 01208196), the unit cap, `bipeds_full`, tc-copy §3) is answered "not held" for that biped alone and gathered at its take. **None of these changes play:** each is G's own code on the main CPU at G's own place. **Forcing every biped down this path is task 5's "fallbacks forced" check:** play exact on the six maps.

**AITICKOLD's two faults, as requirements.** REBUILD.md:125 names two: the conversation fault and the firing-position fault (`aime_me.md` rounds 9-12, archive). Their census symptoms were b30's beach early (2 of 4) and b40 stuck (2 of 4) (s3o5, findings/levers/grounds.tsv AITICKOLD [archive]). The archive's fixes found three causes (tick_core.md:149-152, via findings/twocore_read_2026-11-26.md item 8, [archive, a lead]): the actor's own stale position (aitickoldself), the clock read one tick on (aitickoldclock), and the think's writes landing before the first tick acted (aitickoldbuf). Which cause is which fault isn't written on any page the lab holds. **s1-me Part A §2 names it** (its brief's task 2, "each fault names the read that causes it"), so the plan meets all three, whichever form Part A takes:
- **R1, the actor's own state is this tick's** (aitickoldself; a firing position picked from where the actor stood a tick ago fits it [U]). The think reads its own unit from the input block that H1's refresh made, never from the copy. The refresh is done before every post point the plan has (H1, H4). The input block is the tick's alone to write (aime_me.md:135, round 3 [archive]).
- **R2, one clock** (aitickoldclock). The post carries the tick number and game time the think is for. The think reads time from the post, never from the global clock that the serial tick advances beside it.
- **R3, the think's writes land after the tick that should see them** (aitickoldbuf; a conversation ended before the tick played its line fits it [U]). Everything the think writes to the AI's tables (props made and deleted, conversations ended, orders) goes into its record. The record is installed at the next tick's H1, before the input refresh, never live. The install doesn't overwrite a field the serial tick wrote since the post: one writer a field a phase (box_world_store.md §1, line 17). The damage and noise handlers that write the AI's tables beside the think (`ai_handle_damage` ai.c:1416, `ai_handle_unit_effect` :1562, from P1 and S1) keep writing the live tables on the main CPU, and the install merges around them.
- **The proof:** the six-map outcome census with b30's beach and b40's stretch held (s1-me task 4 [E]). Neither symptom may come back in 4 of 4.

**What §12.4 takes from this:**
- The sweep's saving: G's objects' half and its packing off the main CPU, beside P1-P2.
- Less the post, the join's wait at the peaks, the stop-and-fallback share, the refusals' live gathers (pushes, carries, relinks, and (α)'s compares), and (α)'s compare itself.
- Part A's slot priced on its own sheet.

### 12.4 The split and its price [D cost sheet] (opened 2026-11-26 22:47; filled last, on §12.1-§12.3)

Per tick on the main CPU, at the goal's frame (one tick a frame, 30 fps, both cores). The net must clear ~0.5 ms a tick on b30's fight (the lever rules, decisions.md 2026-11-26 18:3x). No figure is filled yet: each line names what prices it.

| # | Side | Line | Figure | Priced by |
|---|---|---|---|---|
| S1 | saved | G's objects' half and its packing, off the main CPU, beside P1-P2 | — | §12.1's column [M] and the counters' b30 PSP run |
| A1 | added | the post (H4): one ring job, no `kcall` | — | the pair, the `tick2 sched:` line |
| A2 | added | the join's wait (J), its mean and its peaks | — | the `tick2 sched:` line [M] |
| A3 | added | the stop-and-fallback share: late and not-held bipeds, G on the main CPU | — | the `tick2 sched:` line's `late`, the `wsold:` line's not-held |
| A4 | added | the refusals' live gathers: pushes, carries, relinks (marked bipeds, `tick2_cs_seq`) | — | the `wsold:` line's refusals × a live gather's cost |
| **A5** | **added, in full** | **(α)'s compare, ruled (the lead 22:4x (3)):** at every take, each object in the list's reach clusters compared by value against its copy record (sphere, flags, links, dead, the models' node count); plus the live gather of every take it refuses | — | the compare's own µs a tick on the PSP pair (never an emulator count, Sheepdog01 2026-11-26 15:3x), and its refusals on the `wsold:` line; carried whole, not netted against (β) |
| A6 | added | the copy itself (tc-copy commits 1-3, commit 4's ≤0.10) | — | carried on s1-me Part A's added side (21:4x (2)); which sheet bears it when both jobs share the copy is for the lead with task 4, never counted twice |
| P | apart | s1-me Part A's slot | — | its own sheet |
