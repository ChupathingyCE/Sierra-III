# Review: me-foundations (the Media Engine programme, stages 0 and 1)

For reading and questioning before testing (CLAUDE.md, "How AI is used here"). One section per change: **what**, **key lines**, **why**, **proven**, **cost**, **default**. The programme itself (history, causes of loss, stages, R2's plan) is `me_programme.md`. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Nothing here changes play by default. The one default path it touches (the job ring's loop, on because the slant split is) keeps its state where it was before, in main RAM, unless `meedramstate` asks otherwise.

## 1. Stage 0's instruments (PSP_RECON builds and `edramcheck`)

**What.** `mebusedram` (with `mebusN`): the ME's test load runs on its own eDRAM instead of main RAM; every 300-tick window also times the load alone in both memories. `edramcheck` reworked into three stretches: 0–192 KB hashed and watched (never written), 256 KB–1.5 MB patterned (any change is an alarm), 1.5 MB–1,984 KB patterned below the ME's stack (the lowest change is how deep the stack reaches). The load now reads its stop word every 16th load, not every load.

**Key lines.** `psp_me.c`: `bus_load_job` (`edram ? BUS_EDRAM_BASE : BUS_BASE`; `while (((n & 15u) || !BUS_STOP) && …)`); `psp_me_bus_phase` (the alone calibration `for (m = 0; m < 2; m++)`, in this run's memory then the other).

**Why.** Every later ME job is sized by two numbers: what ME work costs the main CPU beside it, and whether data the ME keeps in its eDRAM is safe there and cheaper. `edramcheck` had never finished a run (b115–116).

**Proven [M].** Both PSPs, b30's route (`me_programme.md` §4): the ME's main-RAM misses cost the main CPU's tick +11.0% (PSP-3000) and +10.3% (PSP-1000); mixed work +4.5% on both; the same work in eDRAM at most +3.2% and +0.9%. The ME alone: eDRAM +19% loads a µs over main RAM. The eDRAM figures are bounds because the load polled its stop word, an uncached main-RAM read, every load; hence the every-16th change, so a rerun reads the eDRAM itself. `edramcheck` over the ladder: running.

**Cost.** None by default: `mebusN` is in PSP_RECON builds only; `edramcheck` is opt-in.

## 2. A long ME job serves the ring (`psp_me_yield`)

**What.** Inside a long slot job (the GE layer's replay with `gemeon`), every 32 records the ME mixes a waiting audio block (as before) and now also runs any ring jobs waiting. A push no longer refuses while such a job is out.

**Key lines.** `psp_me.c`: `psp_me_yield` (`if (p->ring_ready && !p->in_ring && RING_PUSHED != p->ring_popped) … me_ring_drain(…)`); the ME's loop hands the ring's cursor to the slot job before it starts (`p->ring_ready = ring_ready && !(control->features & ME_FEATURE_NO_YIELD)`). Inside a ring job a yield mixes audio only (`in_ring`): the ring's jobs run in order, and the one running isn't popped yet.

**Why.** A refused push costs the offload for nothing: the main CPU does the work while the ME is busy with something that could pause. Cause 2 of `me_programme.md` §2.

**Proven.** Compiles in the default, PSP_RECON and multiplayer configurations [D]. It runs only on the PSP and only under the opt-in `gemeon`/`geinflight` paths: the emulator runs each job inside its push, so nothing there yields. A PSP run with `gemeon` is the proof, still to come.

**Cost.** One uncached read a yield (the features word), every 32 records of a replay [U].

**Default.** On wherever its paths are on (they are opt-in). `meyieldoff`: the old behaviour.

## 3. Batches: records handed over in chunks, taken back when not started

**What.** A client's fixed-size records go to the ME a chunk at a time (one ring entry a chunk) while the main CPU adds more. Before the main CPU reads a record it calls `psp_me_batch_need`: made by the ME, read it; never handed over or handed over but not started, made here (taken back); the ME on it that moment, wait for that one record (10 ms deadline, then the ME is stopped and the record made here).

**Key lines.** `psp_me.h`: the API (`psp_me_batch_init/begin/add/close/need/finish`). `psp_me.c`: the ME's `batch_job` claims each record by writing `started = r + 1`, `sync`, then reads `limit`; the main CPU's `psp_me_batch_need` writes `limit = index`, `sync`, then reads `started`. Both through uncached words of their own lines, so one of the two always sees the other's write: either the ME backs off (`started = r`) or the main CPU waits for that record [D].

**Why.** Eight ring entries carried eight jobs; a client with hundreds of small pieces (particles, node matrices, rays) filled the ring and the rest stayed on the main CPU. And "done before first use" is the one sync rule every job can share, with the main CPU never waiting on work the ME hasn't begun.

**Proven.** Compiles [D]. The emulator pass (`mebatch1`): `mebatchtest` (48 records a frame, chunks of 6, needs in a scrambled order, every answer checked) with and without `mebatchsimhalf` (the simulated ME makes half of each job, so take-back runs), under the strict interpreter with `gemesim mewrites`, and play-hash pairs with it on and off: running. Then a PSP run with `mebatchtest mestash` for real take-back races.

**Cost.** None by default: no client uses batches yet; `mebatchtest` is a check word.

## 4. The ME's private state (`meedramstate`)

**What.** The state only the ME touches (the ring's cursor handed to a yielding job; the sums of audio blocks mixed inside other jobs, 4 KB) lives in main RAM on lines of its own, or with `meedramstate` in the eDRAM at 208–256 KB (`psp_me.h`'s map).

**Key lines.** `psp_me.c`: `ME_PRIVATE` and `ME_AUDIO_SUMS` pick the copy by `ME_FEATURE_EDRAM_STATE`; the ME's start resets the main-RAM copy only (the features aren't set yet).

**Why.** What the ME alone writes belongs off the shared bus (§1: eDRAM work costs the main CPU a third of main-RAM work). It's off by default because 192–256 KB is the one stretch `edramcheck` doesn't cover; 192–208 KB took `mebusedram`'s stores through eight PSP runs without a fault [M], 208–256 KB is unproven.

**Default.** Off. Turn on after a PSP run with `meedramstate meringtest` on the ladder.

## 5. For decision: R2, the sound cache in the eDRAM

Not built. Its plan is `me_programme.md` §5: 1,792 KB of heap freed in both builds, but the main CPU can't reach the eDRAM, so **with the ME stopped (HOME, a failure, after a sleep) there would be no sound** unless a 512 KB main-RAM fallback cache is kept. That changes behaviour, so it's a call to make after an A/B on the PSP-1000, and it is gated on `edramcheck` coming back clean with the stack's reach leaving at least 1 MB.
