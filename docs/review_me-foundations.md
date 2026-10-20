# Review: me-foundations (the Media Engine programme, stages 0 and 1)

For reading and questioning before testing (CLAUDE.md, "How AI is used here"). One section per change: **what**, **key lines**, **why**, **proven**, **cost**, **default**. The programme itself (history, causes of loss, stages, R2's plan) is `me_programme.md`. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Nothing here changes play by default. The one default path it touches (the job ring's loop, on because the slant split is) keeps its state where it was before, in main RAM, unless `meedramstate` asks otherwise.

## 1. Stage 0's instruments (PSP_RECON builds and `edramcheck`)

**What.** `mebusedram` (with `mebusN`): the ME's test load runs on its own eDRAM instead of main RAM; every 300-tick window also times the load alone in both memories. `edramcheck` reworked into three stretches: 0–192 KB hashed and watched (never written), 256 KB–1.5 MB patterned (any change is an alarm), 1.5 MB–1,984 KB patterned below the ME's stack (the lowest change is how deep the stack reaches). The load now reads its stop word every 16th load, not every load.

**Key lines.** `psp_me.c`: `bus_load_job` (`edram ? BUS_EDRAM_BASE : BUS_BASE`; `while (((n & 15u) || !BUS_STOP) && …)`); `psp_me_bus_phase` (the alone calibration `for (m = 0; m < 2; m++)`, in this run's memory then the other).

**Why.** Every later ME job is sized by two numbers: what ME work costs the main CPU beside it, and whether data the ME keeps in its eDRAM is safe there and cheaper. `edramcheck` had never finished a run (b115–116).

**Proven [M].** Both PSPs, b30's route (`me_programme.md` §4): the ME's main-RAM misses cost the main CPU's tick +11.0% (PSP-3000) and +10.3% (PSP-1000); mixed work +4.5% on both; the same work in eDRAM at most +3.2% and +0.9%. The ME alone: eDRAM +19% loads a µs over main RAM. The eDRAM figures are bounds because the load polled its stop word, an uncached main-RAM read, every load; hence the every-16th change, so a rerun reads the eDRAM itself. `edramcheck` over the ladder: running. `edramcheck` over the ten-map ladder on the PSP-3000 (default build, two runs): 0 of 120 patterned pieces changed and 0 hashes moved, no alarm; the stack's reach not measured (that PSP's ME stack is above 2 MB), so R2's gate needed the same ladder on the PSP-1000. That run: clean again (0 of 120 pieces, 0 hashes moved), and the ME's stack never went below 1,984 KB (no word changed in 1,536–1,984 KB, where the reach branch ran), so about 1.7 MB (256–1,984 KB) is free of it: the gate is met on both PSPs [M]. The slant split against `meslantoff` on the same ladder: the sort 0.69–0.91 of the single-CPU cost per draw on nine maps (0.07–2.2 ms a frame), 1.19 on d20; fps and ticks a second equal within noise (`me_programme.md` §4 (b), (c)).

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

**Proven, and not proven.** Compiles [D]. The emulator pass (`mebatch1`), `mebatchtest` (48 records a frame, chunks of 6, needs in a scrambled order, every answer checked) [E]:
- **Answers right: 0 wrong** in 808,224 records across b30's route (JIT with the real ME emulation: 338,784 records, the ME making 97% of them, 10,797 made here; and strict with the simulated ME: 40,272), d20 (215,712) and c20 (213,456); `meringtest` beside it, 150 bursts and 1,200 jobs, 0 wrong. Hand-overs refused 0, late 0.
- **Strict checks:** no system call in work meant for the ME (all three strict runs). Lines both CPUs wrote: 85 on b30, the test's own arena (50, written in turn) and the emulator's stack (35, where the simulated ME runs), nothing else.
- **Play the same:** with `mebatchtest` on and off (`statecrc`, d20 and c20, 9,000 ticks each), play and both seeds are identical at every tick; the whole state differs only in what drawing writes (contrail point, object looping sounds, objects, lights).
- **Take-back is NOT proven.** No take-back happens in the emulator without the half switch (taken back 0 in every pass: the emulator runs a pushed job inside the push, so the ME is always done). With `mebatchsimhalf` (the simulated ME makes half of each job) the first three passes show 98 records made by the simulated ME, 46 here, 5 taken back, and then "a batch record (10) held past 10 ms; the ME is stopped"; **46 of those first 144 records are wrong**. The same three figures come out in three runs (b30 JIT, b30 strict, d20 strict). After the stop every pass is made on the main CPU (0 wrong), so the path never runs again. The strict half runs also list 238 (b30) and 394 (d20) lines both CPUs wrote beyond the arena and the stack, which no other run has, from code addresses that need the build's symbols to name: not clean against the stage's rule.
- **Cause: the half switch, not the protocol** [D]. Both failures come from how the switch stopped the simulated ME, and neither can happen on a real ME:
  - *Wrong records.* `done` is one word for the batch, and `psp_me_batch_need` trusts every handed-over record below it. The old switch ended each job halfway and then let the next chunk's job run whole, so `done` jumped past the first job's unmade half (records 3-5 of chunk 0-5 read as the ME's while still holding their inputs). A real ME job ends early only at the main CPU's `limit`, which only falls, so every later job withdraws at its first record and `done` never passes a record the ME didn't make.
  - *"Record 10 held".* A later job that withdraws writes `started` = its own first record, past the gap. When the main CPU then needs a gap record below its own `limit`, it sees `done <= index < started`, reads that as "the ME is on it this moment", and waits for a record no ME will ever make: 10 ms, then the ME is stopped. On a real ME, `started > index >= done` means the ME is on `index` itself (the records before it are made in order), and the wait is the protocol's own.
  - *The shared lines* (with the build's link map): the test's arena aside, every one is in the pose-ahead arena on the heap, written by the simulated ME's pose job (`quaternions_interpolate_and_normalize`, `quaternion_decompress_6byte`, `animation_get_node_orientations`) and by the main CPU's (`psp_pose_ahead_make_work`, the same decoders). Once the late record stopped the ME, every pose went back to the main CPU, into lines the ME had written in earlier ticks: in turn, after the ME's last job was collected, not side by side. No such line appears in a run where the ME isn't stopped [U until the proof run].
- **The fix.** `mebatchsimhalf` now simulates an ME that is behind rather than one that skips: in each pass one job stops halfway (after 0-3 whole jobs, by turns, so the ME's records, the ones taken back and the ones never handed over fall in different places), and every later job of that batch makes nothing, as jobs queued behind a slow one would. `done` then stops at the gap, and needs past it take the real take-back path (`limit` lowered, `started` read, made here). The ME's side of a withdrawal (`r >= limit`) still runs only on a real ME: in the emulator a job runs at its push, before any record is taken back. The protocol's code is unchanged; a comment over `batch_job` states the invariant it rests on.
- **Proof wanted** (emulator): `mebatchtest` with and without `mebatchsimhalf` on b30's route (JIT and strict) and d20 (strict): 0 wrong, taken back above 0 in the half runs, no late record, and no shared line beyond the arena and the stack. Then the PSP run (`mebatchtest mestash`, `- | meedramstate`) for the real races: the ME on a record when it's needed, and the ME's withdrawal.

**Cost.** None by default: no client uses batches yet; `mebatchtest` is a check word.

## 4. The ME's private state (`meedramstate`)

**What.** The state only the ME touches (the ring's cursor handed to a yielding job; the sums of audio blocks mixed inside other jobs, 4 KB) lives in main RAM on lines of its own, or with `meedramstate` in the eDRAM at 208–256 KB (`psp_me.h`'s map).

**Key lines.** `psp_me.c`: `ME_PRIVATE` and `ME_AUDIO_SUMS` pick the copy by `ME_FEATURE_EDRAM_STATE`; the ME's start resets the main-RAM copy only (the features aren't set yet).

**Why.** What the ME alone writes belongs off the shared bus (§1: eDRAM work costs the main CPU a third of main-RAM work). It's off by default because 192–256 KB is the one stretch `edramcheck` doesn't cover; 192–208 KB took `mebusedram`'s stores through eight PSP runs without a fault [M], 208–256 KB is unproven.

**Default.** Off. Turn on after a PSP run with `meedramstate meringtest` on the ladder.

## 5. R2: the sound cache in the eDRAM (`soundedram`, `soundgeo`)

Built for a trial (the decision to try it is taken; whether it stays is the PSP A/B's). Plan and gate: `me_programme.md` §5; the gate is met on both PSPs (§1).

**What.**
- `soundedram`: the sound cache lives in the ME's eDRAM, 256 KB–1,920 KB (1,664 KB), instead of 1,792 KB of main RAM. The engine is given the eDRAM address as the cache's base; only the ME, where the mixer runs, ever reads it.
- The stick reader can't write eDRAM, so each sound is read in pieces of 60 KB into two 64 KB staging slots in main RAM, and an ME slot job (`ME_JOB_SOUNDCOPY`, up to four pieces a job) copies them into place. The engine's `loaded` flag is set when the last piece is in.
- **Fallback:** the first time the ME stops (a failure, a sleep, HOME), the eDRAM is taken as lost for the session, even if `mewake` restarts the ME. From then on every voice playing from the cache is mixed silent (the mixer's own silent path: the position moves on, nothing is read), and new loads are marked failed. The log says so once (`sound eDRAM: … taken as lost`). The main CPU never touches an eDRAM address: `cache_lines` skips them, and when it mixes (the ME stopped, or the debug menu's mixer switch) it mutes those voices first.
- `soundgeo`: the start-up sound block keeps the size it always had (1,792 KB); the part the sound cache doesn't take becomes a second stretch of the geometry cache's ring. With `soundedram` on the PSP that is 1,664 KB (the block less the staging); with `sound512` and no eDRAM, 1,280 KB. Without `soundgeo`, `soundedram` takes only the 128 KB staging at start-up and the rest stays the heap's.
- The emulator has no eDRAM: there `soundedram` runs **simulated**, the cache in main RAM at the eDRAM's 1,664 KB, with the same staging and copy jobs (run by the main CPU as the simulated ME's), so everything but the eDRAM itself is the PSP's path. The heap is as it is today there, so the emulator can't show the freed RAM; `sound512 soundgeo` stands in for the geometry side.

**Key lines.**
- `psp_dsound.c`, section "R2": `psp_sound_edram_mode` (refused with `edramcheck`, which patterns the same stretch, or with the ME off); `psp_sound_block_bytes` / `psp_sound_cache_place` (what start-up takes and where the cache goes); `psp_sound_upload_start` / `psp_sound_upload_poll` (the game thread, once a frame from `sound_cache_idle`: collect the copy job only once done, `psp_me_submitted_done`; drop pieces of a load that is gone or failed; set `loaded`; start reads; hand what's read to the ME in the order it was asked for); `mute_cache_voices` and `edram_unreachable`, called in `mix()` before a main-CPU mix and before an ME mix after a loss.
- `psp_sound_cache_alive` (`psp_fixups.py` FIXES_SOUNDEDRAM, in `xbox_sound_cache.c`): a sound deleted while it loads (a flush) may have its pages given to another sound; each piece is copied, and `loaded` set, only while the datum's identifier, sound and address are still the load's. The original game read straight into the block and had no such check.
- `psp_me.c`: `ME_JOB_SOUNDCOPY` in `run_job` (an eDRAM destination is refused off the ME); `psp_me_stops`, counted in `me_stop`; `psp_me_submitted_done`. `psp_me.h`: `ME_EDRAM_SOUND` / `ME_EDRAM_SOUND_END`.
- `psp_geo.c`: `extra_start`/`extra_end`, `stretch_end`, `largest_stretch`; `allocate` runs the ring from the end of one stretch to the start of the other. `psp_geo_range` (for `gereads`) still names only the first stretch.
- `psp_platform.c` `psp_sound_cache_bytes`: the eDRAM's room caps the cache.

**Why.** The freed main RAM is the point (`me_programme.md` §5; MC3 in `memory_census.md`): a10's 900-frame geometry working set is about 2.7 MB against `geo2`'s 2 MB and a10 has no tag-window room to borrow. A side effect worth measuring: the mixer's sample reads leave the shared bus (they are eDRAM reads on the ME now), and §4 (a) puts ME main-RAM streaming at +10–11% on the main CPU's tick while it runs [M]; the copy jobs put some of that traffic back, but only once per sound loaded [U].

**What it costs.**
- **The cache is 128 KB smaller** (1,664 KB against 1,792, 7%). The edge measured on d40's opening (b107, emulator): 1,536 KB failed 144 loads for room, 1,920 KB none [E]; 1,664 KB should drop a few on the heaviest maps [U]. The 256 KB–1,920 KB stretch is all the eDRAM there is on the PSP-1000 below the ME's stack's 128 KB (twice the 64 KB the ladder measured) [M for the reach]. It also takes the room `me_programme.md` held for resident job data; R2 and a resident job table can't both have it.
- **A sound is ready a frame or two later**: two staging slots, read and copied in a pipeline paced by the game's frame. The report line gives the mean and worst milliseconds from asked to loaded.
- **The trade accepted for the trial:** no sound after the ME stops (after a sleep, by default, the ME stays stopped). No main-RAM fallback cache is kept; that would give back 512 KB of what R2 frees.
- Frame and tick time: nothing per frame on the main CPU beyond the poll; per sound loaded, a `sceKernelDcacheWritebackInvalidateRange` over the staged piece and the ME's copy (about 1 ms of ME time per 120 KB [U]). To be measured: the emulator batch (game time, the same stretch, on against off) and the PSP A/B.

**Proven.** Compiled locally (campaign and `PSP_MULTIPLAYER=1`); the emulator batch (a10, a30, b30, off against on; a10 strict with `gemesim mewrites`; `statecrc` pairs; a stop at 15 s with `mewaketest mewake`) is in flight. The eDRAM path itself only the PSP can prove.

**PSP A/B (next).** One hardware job: the commit the emulator batch ran, a10 on the route of the b139 hand test, keep `nearauto assettrack geo2 exitat300`, sides `-` and `soundedram soundgeo`, each twice, about 20 minutes a run, the b139 release bake and PGO profile; either PSP. Read: `sound eDRAM:` (the mode line says "in the ME's eDRAM"; sounds in, 0 failed, ms to loaded), `sound cache:` failed for room, `geometry:` misses and miss frames, the tick time and fps per stretch (`tools/abreport.py`), no crash. Then a listen on the next hand test: does sound start late anywhere?

**Default.** Both off. `soundedram` is refused with `edramcheck`. Turn either off by leaving its word out.
