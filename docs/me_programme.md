# The Media Engine programme

The ME, the PSP's second CPU, sits idle ~90% of play. It is the largest piece of hardware the port still leaves on the table, so it gets a programme of its own: what was tried and why it won or lost, the foundations every ME job stands on, and each result as it comes in.

Labels: **[M]** measured on a PSP · **[E]** emulator or cache model · **[D]** read in code or data · **[U]** inference. The emulator runs no ME code: it proves a job exact and its rules kept, never its speed.

Related: `media_engine.md` (how a job runs), `frames_in_flight.md` (L5), `me_poses.md` (MP), `plan_node_matrices_me.md` (NW), `plan_biped_collision_me.md` (BW and the collision copy), `big_levers.md` §0 (the eDRAM), `review_b121_me.md` (the ring), `review_b123_mehyg.md` (the hand-over words).

## 1. What the ME is

- A 333 MHz Allegrex core like the main CPU, with an FPU and **no VFPU**, 16 KB instruction and data caches, **not coherent** with the main CPU's.
- **Its own 2 MB of eDRAM** (4 MB on a slim) that the main CPU can't see. It can't see VRAM. It reads and writes main RAM over the bus the main CPU and the GE share.
- Our code runs on it through me-core-mapper (MIT): from main RAM, with its stack at the top of eDRAM. It makes no system call and no library call.

## 2. History: what shipped, what lost, and why

| When | Lever | Result | Why |
|---|---|---|---|
| b86–87 | The sound mixer (audio slot) | **Shipped, default.** The main CPU's cost per block ~110 µs against 290–500 µs mixing itself [M] | Integer-only, a clean leaf |
| b87–90 | `meclassify`: the near-plane classify and the slant buckets, run on both CPUs and compared | **The ME is as fast as the main CPU** on the same code, ~5% faster: classify 288–311 ns a vertex against 292–352; slant 1,004–1,021 ns a triangle against 1,061–1,121; 0 answers differing over ~5 M vertices and 4.7 M triangles [M] | b86's "twice as slow" was the hand-over (sleeps, whole-cache write-backs), not the core |
| b112–113 | `gemeon`: the GE layer's recorded calls replayed on the ME (render slot), audio mixed between records | Works; serial, so a loss on its own | The main CPU waits for the replay |
| b115–116 | `edramcheck` | **Never completed a run.** b115's fill (1.25 MB in one 56 ms job) starved the audio job, which timed out and stopped the ME for the session; b116 cut it into 64 KB pieces, never run over a ladder since | — |
| b118 | L1 `gemeoverlap` (the ME builds half a frame's lists while the main CPU builds the other half) | **Lost: 3–6% slower** on b30 (PSP-3000) [M]; stays opt-in | The GE started late (the frame end waited 35 ms against 0.8), and the ME built lists ~25% slower while both ran (the bus); the whole-cache drop after the ME's half threw away the main CPU's working set |
| b120 | MH `mehalt` (the ME halts when idle, woken by interrupt) | Works (wakes 22 µs, a kernel call); **no gain** [M]; opt-in | An idle polling ME puts almost nothing on the bus already |
| b120 | MF `mefinishslow` (fewer looks while waiting) | **No effect** [M] | — |
| b121 | RF the job ring: 8 entries, one writer per line, range write-backs, 10 ms watchdog | **Shipped** (the plumbing every ring job uses) | "Few, large, asynchronous jobs" (b90) |
| b121 | SL the slant sort split: the ME buckets the back part of each large level draw | **Shipped, default** (`meslantoff`): exact on the PSP (0 of 414,000 triangles differing), the sort 0.6–1.4 ms a frame less on b30 and c20 [M] | Small but real |
| b122 | Perception's line-of-sight rays on the ME | **Shipped, default** (`meraysoff`): 0 of 31,940 answers differing on the PSP, **a50's tick −1.0 to −1.8 ms (−3.8 to −5.7%)** [M] | Needs the main CPU's whole cache written back before the first job of a tick |
| b123 | Hand-over hygiene (`meaudiofirst`, `meraysbatch`, `meaudioranges`, `mepushdrop`, `mestash`, `meraysminN`) | **All opt-in**: c40's cinematic ran slower with them on (tick 20.4 against 17.7 ms) for an unnamed reason; `meraysbatch` slower [M] | — |
| b124–125 | MW `mewake` (the ME restarted after a sleep) | **Fails on the PSP-1000**: restarts, then answers no job [M]; opt-in. The default leaves the ME stopped after a sleep and the main CPU takes its jobs | Unknown; the firmware's own wake path is replaced by the library's |
| b131–132 | MP next tick's base poses decoded on the ME | **Shipped, default**: exact on the PSP (164,981 poses compared, 0 differed); b30's flight tick −2.0%, c20's tick −6.4% (PSP-3000) [M]; the beach gains nothing | The main CPU still pays the copies (cold after the collect's drop) and the hand-over |
| b132 | WM the ME's cache kept warm between jobs | **No gain** for the pose job (−1.3% warm, −1.7% cold) [M]; parked | Its reads overflow 16 KB anyway |
| recon 134 | `mebusN`: how the two CPUs slow each other (PSP-1000, b30) | The ME on scattered misses (`mebus0`): **the main CPU's tick +11.9%** (+6.4 to +13.3%), the ME −15%. With arithmetic between loads (`mebus50`): +5% and −10% [M] | The bus has room: lever L5 survives the sharing |
| inflight | L5 frames in flight: F0 (the frame recorded, replayed after the next frame's ticks) and F1 (replayed on the ME beside them) | **Lost as built: the frame +3 to +25%** (PSP-3000, b30) [M]. Parked behind the GE's bytes into VRAM | See below |
| inflight | `mekinds`: the ME's rate by record kind, with no overlap | **The ME replays at 0.45–0.92× the main CPU's time** (b30 PSP-3000: 57.6 against 72.4 ms a frame; c20 PSP-1000: 29.6 against 36.0) [M] | The core is not the limit |
| inflight | F1 beside the ticks, c20 (PSP-3000) | The ME slows only 15%, but **the tick slows 73%**; with the GE held to the sync (`geserial`) the ticks come back (87.6 → 55.6 ms a frame) and the frame is 114.2 against F0's 118.9 [M] | **The GE drawing beside the tick**, not the ME: its main-RAM fetches cost the miss-bound tick ~32 ms a frame; the ME's own traffic ~5 ms (+10%, as `mebus`) |
| plans | NW node matrices on the ME; BW the whole biped update; L3 AI | **Ceilings** (cache model): NW b30 −5.4 ms a tick (−13.7%), c20 −4.3 (−21.7%); BW b30 −2.4, c20 −2.8 [E]. `nwbatch` on the main CPU (the step before the ME) shelved: exact, but c20 +5% and c10 +34% on the PSP-3000 [M] | The batch takes each object's data cold out of its loop |

### What the losses have in common

1. **The bus, when both CPUs miss at once.** `mebus0` (the ME missing on every load) costs the main CPU's tick ~12%; light ME work ~5%. The GE fetching from main RAM beside the tick costs far more (F1's +73%). An ME job pays where its traffic is light or its data are the ME's own.
2. **Serial synchronisation.** gemeon, L1 and F1 all had one CPU wait for the other at a fixed point every frame: the ME's lane plus the main CPU's lane, not the longer of the two.
3. **Whole-cache hand-overs.** A whole-cache write-back costs 12–24 µs, a whole-cache drop 5–32 µs and then up to ~46 µs of refills [M]. Forty a tick would cost a millisecond; L1's drop threw away the main CPU's working set.
4. **The replay's skipped memos.** On the ME the GE layer's replay can't use the main CPU's memos (`psp_ge.c`'s per-frame caches that only the main CPU keeps), so it redoes work the main CPU skips.
5. **No VFPU.** Anything the main CPU does with the VFPU the ME does on the FPU (the matrix product: ~1.5 µs against ~0.45 [M]), and not bit-for-bit alike: such work can move to the ME only as a whole, done the FPU's way on both CPUs when either might do it.
6. **The ME's rate is not the problem.** On the same code it matches the main CPU (b90) and replays 0.45–0.92× the main CPU's time (`mekinds`).

So the programme's rule: **few, large, asynchronous jobs, handed over without waiting, collected at their first use, whose data stay off the shared bus as far as they can.**

## 3. The stages

### Stage 0: measure what the foundations depend on (PSP time)

| Question | Instrument | Status |
|---|---|---|
| (a) **How much each CPU slows when both run, by kind of ME work**: scattered misses in main RAM, light main-RAM traffic, eDRAM only; on both PSPs | `mebusN` (PSP_RECON builds; ticks loaded by a coin against unloaded), with `mebusedram` for the eDRAM kind | done: §4 |
| (b) **Does anything but our ME code write the eDRAM**, over a ladder of all ten maps; how deep the ME's stack reaches; the ME's speed from eDRAM against main RAM | `edramcheck` (now: the bottom 192 KB hashed and watched, 256 KB–1.5 MB patterned, the stretch below the stack patterned for its reach); every `mebus` window times the load alone in both memories | eDRAM clean on both PSPs and speed done: §4. **Stack reach measured on the PSP-1000: never below 1,984 KB** (the PSP-3000's ME stack is above 2 MB, so no reach there) |
| (c) **SL, the slant split, on today's build**: what it's worth now | `- \| meslantoff` on the same ladder | done (PSP-3000): §4 |

**Instruments added for stage 0** (this branch):
- `mebusedram` (with `mebusN`): the ME's load runs on its own eDRAM, reads and stores both. The line `mebus: work N eDRAM | … the main CPU +x% | the ME … alone: … | alone in main RAM …` gives the main CPU's cost of work that never leaves the ME, and the ME's eDRAM rate against main RAM's for the same work.
- `edramcheck`: three stretches, 64 KB a heartbeat each, a pass ~30 s: 0–192 KB hashed and never written; 256 KB–1.5 MB patterned (any change is an alarm); 1.5 MB–1,984 KB patterned below the stack (the lowest change is the stack's reach on a PSP-1000; on a slim it's patterned room). Lines: `ME: edramcheck: …`.

Results: §4.

### Stage 1: the foundations

Every later ME job (particles, node matrices, frames in flight, the collision copy) stands on these. Each is exact by construction or a pure instrument, and each ships behind a keep word.

1. **Hand-off never waits.** A push already never waits: when it can't hand a job over it refuses, and the caller does the work. Two refusals cost the offload for no reason:
   - *The ME busy with a long slot job.* A long ME job serves the ring between its steps (`psp_me_yield`: audio first, then any ring job waiting, from a slot job; audio only from inside a ring job), so pushes aren't refused while it runs.
   - *The ring full.* **Batches** (below): a client's records go over in chunks, one ring entry a chunk, so eight entries carry hundreds of records, and a chunk the ring can't take stays the main CPU's.
2. **Batches: double-buffered records, one sync rule.** A client keeps an arena of fixed-size records (whole 64-byte lines) and adds records as its loop finds them; every `chunk` records the filled part goes to the ME as one ring job while the main CPU fills the next. **The sync rule is "done before first use"**: `psp_me_batch_need(b, i)` before the main CPU reads record *i*:
   - done by the ME: its lines dropped, the answer read;
   - not yet handed over: the main CPU makes it (and the ones before it) itself;
   - handed over but not started: **taken back** (a limit word the ME reads before each record) and made by the main CPU;
   - the ME on it at that moment: wait for that one record.

   The main CPU never waits for work the ME hasn't started. Each record is a pure function of its own inputs, made by the same compiled function on either CPU under the main CPU's FPU control word (the batch sets the ME's), so who makes it doesn't change the answer.
3. **The ME's own data in its own memory.** An eDRAM map (`psp_me.h`): 0–192 KB left as found; 192–208 KB `mebusedram`'s scratch; 208–256 KB the ME's private state (the ring cursor its loop and a yielding job share; the mixing sums of audio blocks mixed inside ring jobs); 256 KB–1.5 MB room for resident data (R2, job tables); 1.5–2 MB the stack. What the ME alone writes stays off the bus and off lines the main CPU might share. The private state goes there only with `meedramstate` until a PSP run shows 208–256 KB free (`edramcheck` covers 0–192 KB and 256 KB up); without it, it stays in main RAM on lines of its own.
4. **One standard emulator pass for every ME job:** strict, `gemesim mewrites`, the job's check word, on the maps it touches. `mestrict.txt` must list no system call made in the job's work; `mewrites.txt`, named by `mewrites.py`, must list no line both CPUs wrote except the emulator's own stack (where the simulated ME runs). A job isn't ready until this pass is clean and its check word shows 0 differing.
5. **Not yet: the ME's hot code in eDRAM.** The ME runs our code from main RAM. A copy in eDRAM keeps calls into main-RAM code working (a `jal` from 0x80xxxxxx lands on the main RAM's 0x88xxxxxx view), but calls between copied functions would land back in main RAM, so only leaf loops could move without a second link of the code at eDRAM addresses. The jobs so far fit the ME's 16 KB instruction cache (the mixer, the slant loop, a ray) or miss little (the node matrices' 98 KB of code: I-misses 0.76% of fetches [E]); so code waits until a job's I-misses are measured to matter.
6. **R2, the sound cache in eDRAM** (1,792 KB of heap freed, both builds), only if (b) shows the eDRAM clean. Its plan is §5.

### Stage 2 and on: the jobs

Built by the jobs' own branch on these foundations, each through the standard pass, then a PSP A/B: particles, node matrices (NW), frames in flight (L5, after the GE's bytes leave main RAM), the collision copy (BW step 3), AI (L3).

## 4. Results

### (a) How much the two CPUs slow each other, by kind of ME work [M]

b30's opening route, nine 300-tick windows a run, on each PSP; ticks loaded by a coin against unloaded, so both sides sample the same play. The ME's rate alone is its 8 ms calibration with the main CPU asleep, the quiet ones (about a third read ~24% lower in both memories: the GE drawing meanwhile).

| ME work | Main CPU's tick, PSP-3000 | PSP-1000 | The ME during the tick against alone |
|---|---|---|---|
| Scattered misses in main RAM, a store each (`mebus0`) | **+11.0%** (+8.5 to +13.9) | **+10.3%** (+7.9 to +12.2) | 1.02 against 1.20 loads a µs: −15% |
| The same with 50 steps of arithmetic between (`mebus50`, about the main CPU's own mix) | **+4.5%** (+0.1 to +6.5) | **+4.5%** (+1.4 to +6.7) | 0.49 against 0.55: −11% |
| Scattered misses in the ME's eDRAM (`mebus0 mebusedram`) | **≤ +3.2%** | **≤ +3.2%** | 1.22 against 1.43: −14% |
| Mixed, in eDRAM (`mebus50 mebusedram`) | **≤ +0.9%** (−1.9 to +3.1) | **≤ +0.8%** (−1.9 to +2.7) | 0.52 against 0.59: −11% |

**The ME's eDRAM against main RAM, alone:** 1.425 loads a µs against 1.20 (+19%, about 130 ns less a load); with arithmetic between, +7%.

What it means:
- **The two PSPs are one.** Every kind agrees within 0.7 points, and the PSP-1000's earlier figures (+11.9%, +5.0%) sit inside these windows' spread.
- **The bus has room.** ME work as heavy in misses as anything we'd give it costs the tick a tenth; work shaped like the main CPU's own costs it a twentieth. Jobs beside the tick keep their ceiling, less these shares [U].
- **Work that stays in the eDRAM costs the main CPU a third or less of the same work in main RAM**, and the eDRAM figures are upper bounds: this run's load read its stop word from main RAM, uncached, every load, a bus trip of its own. That poll is also why the ME loses the same 11–15% in either memory, and why the eDRAM's lead over main RAM reads as only ~130 ns a load. The instrument now polls every 16th load; a rerun reads the eDRAM itself [U].
- **For the jobs:** inputs that live in the eDRAM (tables, resident data) and the ME's private state there are nearly free to the main CPU; an ME loop must not poll a main-RAM word every step. The batches' limit word is read once a record, and a record is microseconds of work, so its share is small [U]; the standard pass should still count it.


### (b) Does anything else write the eDRAM; how deep does the ME's stack reach [M, both PSPs]

`edramcheck` on the ten-map ladder (45 s a map), two runs on the PSP-3000 (one with `meslantoff`), the default build otherwise (PGO, ME ring on):

- **Clean.** 4 passes, 120 pieces of 64 KB checked in each run: **0 patterned pieces changed** (256 KB–1,984 KB), **0 hashes moved** (0–192 KB). No alarm; nothing but our own ME code wrote the checked stretches during play, ring jobs (slant sort, audio mixing) included. The coverage is 4 passes of the 30 pieces, so a write that came and went between two visits could hide; each stretch was visited about every 30 s across all ten maps [U].
- **The stack's reach is not measured.** `psp_me.c` reports a reach only when the ME's own stack lies below 2 MB (a PSP-1000); this PSP-3000 printed no reach line, which fits its stack sitting above 2 MB (its eDRAM is 4 MB). The final line's "stack's deepest 0 KB" means "not seen", not "unused". What the run does show: nothing wrote 1.5 MB–1,984 KB, so on this PSP the room R2 would use is free of the ME's stack. The R2 gate (reach leaving at least 1 MB on the PSP-1000) needs the same ladder on the PSP-1000: `edramcheck` alone, no keep sets, `wall` 1,800.
- **The same ladder on the PSP-1000** (`edramcheck` alone, ten-map ladder, the default build): **clean again**, 4 passes, 120 pieces of 64 KB, 0 patterned pieces changed and 0 hashes moved. This PSP's ME stack lies below 2 MB, so the reach branch ran (the log names it: "the ME's stack's reach from 1536 KB") and printed no reach line: no word changed anywhere in 1,536–1,984 KB, and the final line's "deepest 0 KB" is what `psp_me.c` prints when none changed [D]. So the ME's stack never went below 1,984 KB, at most the top 64 KB of its 2 MB [M]: **256–1,984 KB, about 1.7 MB, was untouched by it and by everything else.** R2's gate (the reach leaving at least 1 MB) is met on both PSPs; whether to build R2 stays a decision (§5). The main thread's own stack (a different stack) reached 361 of its 512 KB. Coverage as on the PSP-3000: 4 passes, so a write that came and went between two visits could hide [U].
- **The 192–256 KB stretch** is still unwatched (`mebusedram` scratch there); `meedramstate` stays off by default.

### (c) What the slant split (SL) is worth now [M, PSP-3000]

The same two runs: the split on (default) against `meslantoff`, build otherwise equal, one run a side, so fps and ticks carry the runs' few-percent spread. `edramcheck` was on in both, and its probe job (about 3 ms of the ME's time a heartbeat) can delay a slant job's pick-up, so the split reads slightly worse here than it would without.

The sort's cost is the GE layer's "angle buckets" line (µs a frame, per 300-frame window); the windows match in draws within 3% between the sides. Mean µs a frame sorting, split on / off, and the sort's cost per draw on against off:

| Map | Sort µs a frame, on / off | Per draw, on ÷ off | fps on / off | Tick ms on / off | Ticks a second on / off |
|---|---|---|---|---|---|
| a10 | 2,585 / 3,408 | 0.75 | 16.2 / 15.9 | 7.8 / 7.8 | 29.4 / 29.1 |
| a30 | 2,218 / 2,826 | 0.79 | 7.5 / 7.6 | 8.6 / 8.5 | 15.1 / 15.0 |
| a50 | 1,642 / 1,991 | 0.82 | 10.7 / 10.7 | 18.3 / 18.0 | 21.4 / 21.4 |
| b30 | 2,395 / 3,042 | 0.79 | 4.4 / 4.4 | 47.4 / 47.6 | 8.9 / 8.9 |
| b40 | 786 / 856 | 0.91 | 17.5 / 16.7 | 10.9 / 11.0 | 28.9 / 28.9 |
| c10 | 1,953 / 2,199 | 0.90 | 9.1 / 9.0 | 16.9 / 17.2 | 18.1 / 17.9 |
| c20 | 2,834 / 3,196 | 0.88 | 10.9 / 10.9 | 24.0 / 24.1 | 21.7 / 21.8 |
| c40 | 1,028 / 1,156 | 0.89 | 20.2 / 20.4 | 12.3 / 12.3 | 29.6 / 29.9 |
| d20 | 1,693 / 1,412 | 1.19 | 7.2 / 7.1 | 36.3 / 38.1 | 14.5 / 14.3 |
| d40 | 4,625 / 6,857 | 0.69 | 24.4 / 23.9 | 1.5 / 1.7 | 30.0 / 29.9 |

- **The split still shortens the sort** in nine maps of ten: 0.69–0.91 of the single-CPU cost per draw, 0.07–2.2 ms a frame (d40 most, a10, b30, a30 about 0.6–0.8 ms). That matches b121's 0.6–1.4 ms on b30 and c20 [M]; the ME takes 23–46% of the triangles.
- **It's worth 0.1–1.3% of a frame, 5% on d40**, so fps and ticks a second agree between the sides within the noise of one run each (largest gap 0.8 fps, b40, with 4–5 heartbeats a side). The ladder can't show a gain this small; `abreport.py` needs repeats and gives no verdict on one run a side.
- **d20 runs the other way** (the split costs 1.19 per draw, 0.3 ms a frame). Its ring jobs took the ME 0.5–1.0 ms each against 0.15–0.3 ms elsewhere, with picks-up of 0.3–1.3 ms, so the ME was slow or busy there. A cause isn't found [U]: the probe's 3 ms jobs, the stick or the bus are candidates; the rerun without `edramcheck` (and a `mebus`-style quiet window) would tell. For now SL stays on by default: it is exact, small and positive on nine maps.

## 5. R2: the sound cache in eDRAM (plan)

**What it frees:** the sound cache, 1,792 KB of heap (`psp_sound_cache_bytes`), in both builds. The PSP-1000's heap has ~147 KB free at worst.

**Who touches the cache's bytes today [D]:**
- *Written* by the asynchronous stick reader: `sound_cache_start_loading_sound` → `cache_file_read(…, cache_address, &loaded)` (`xbox_sound_cache.c`).
- *Read* by the mixer only: each queued packet points into the cache (`voice_packet.data`), and `mix_voice` decodes it 64 samples at a time (`psp_dsound.c`), on the ME by default.
- The main CPU computes ranges over packet bytes for the hand-over by ranges (`sample_lines`, opt-in `meaudioranges`) but never reads them.

**The design [U]:**
- The cache's pages live in eDRAM 256 KB–1.5 MB (1,280 KB: smaller than today's 1,792; the size b106–b114 measured as the edge is 1,536–1,920 KB, so the cache would drop sounds on the heaviest maps unless the stack's reach (stage 0b) leaves more room).
- The engine still sees a cache base address; it is the eDRAM's ME address, which the main CPU never dereferences.
- The stick reader reads into a small staging buffer in main RAM; an ME job copies it into place and then sets `loaded`. Sounds loaded meanwhile stay "loading" a block longer.
- **The main CPU can't mix any more.** With the ME stopped (HOME, a failure, after a sleep: MW), there is no sound, unless a small main-RAM cache is kept as the fallback (512 KB, `sound512`), allocated only when the ME stops. That changes behaviour (sound after a sleep), so R2 is opt-in, decided by an A/B on the PSP-1000.
- The emulator can't run it (no eDRAM): there the cache stays in main RAM, and the staging and the upload job are simulated, so the plumbing is proven but the eDRAM path only on the PSP.

**Gate:** `edramcheck` clean over the ladder on the PSP-1000, and the stack's reach leaving at least 1 MB. Status: clean on the PSP-3000 (§4 (b)); the PSP-1000's run and the stack's reach are still to do.

## 6. For the jobs' branch: what's ready

- **The costs to size a job by** (§4 (a)): ME work beside the tick costs the main CPU +10–11% when it streams main-RAM misses, +4.5% when mixed like the main CPU's own, a third of that or less when its data stays in the eDRAM. Don't poll a main-RAM word every step of an ME loop.
- **Batches** (`psp_me.h`): the API is in. Use it once its emulator pass and a PSP run with `mebatchtest` are clean (review_me-foundations.md §3). Records whole 64-byte lines; `make()` touches only its own record; `psp_me_batch_need` before every read of an answer.
- **`psp_me_yield`**: a long slot job no longer blocks the ring; ring jobs themselves should keep calling `psp_me_ring_step` between steps.
- **eDRAM room for resident data**: 256 KB–1.5 MB, once `edramcheck` is clean over the ladder.
