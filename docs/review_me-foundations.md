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
- **Take-back, first pass: failed, for the switch's sake (below); proven in the second pass ("Proof" at the end of this section).** No take-back happens in the emulator without the half switch (taken back 0 in every pass: the emulator runs a pushed job inside the push, so the ME is always done). With `mebatchsimhalf` (the simulated ME makes half of each job) the first three passes show 98 records made by the simulated ME, 46 here, 5 taken back, and then "a batch record (10) held past 10 ms; the ME is stopped"; **46 of those first 144 records are wrong**. The same three figures come out in three runs (b30 JIT, b30 strict, d20 strict). After the stop every pass is made on the main CPU (0 wrong), so the path never runs again. The strict half runs also list 238 (b30) and 394 (d20) lines both CPUs wrote beyond the arena and the stack, which no other run has, from code addresses that need the build's symbols to name: not clean against the stage's rule.
- **Cause: the half switch, not the protocol** [D]. Both failures come from how the switch stopped the simulated ME, and neither can happen on a real ME:
  - *Wrong records.* `done` is one word for the batch, and `psp_me_batch_need` trusts every handed-over record below it. The old switch ended each job halfway and then let the next chunk's job run whole, so `done` jumped past the first job's unmade half (records 3-5 of chunk 0-5 read as the ME's while still holding their inputs). A real ME job ends early only at the main CPU's `limit`, which only falls, so every later job withdraws at its first record and `done` never passes a record the ME didn't make.
  - *"Record 10 held".* A later job that withdraws writes `started` = its own first record, past the gap. When the main CPU then needs a gap record below its own `limit`, it sees `done <= index < started`, reads that as "the ME is on it this moment", and waits for a record no ME will ever make: 10 ms, then the ME is stopped. On a real ME, `started > index >= done` means the ME is on `index` itself (the records before it are made in order), and the wait is the protocol's own.
  - *The shared lines* (with the build's link map): the test's arena aside, every one is in the pose-ahead arena on the heap, written by the simulated ME's pose job (`quaternions_interpolate_and_normalize`, `quaternion_decompress_6byte`, `animation_get_node_orientations`) and by the main CPU's (`psp_pose_ahead_make_work`, the same decoders). Once the late record stopped the ME, every pose went back to the main CPU, into lines the ME had written in earlier ticks: in turn, after the ME's last job was collected, not side by side. No such line appears in a run where the ME isn't stopped [E: confirmed by the second pass, where the ME is never stopped and the pose-ahead arena has no shared line].
- **The fix.** `mebatchsimhalf` now simulates an ME that is behind rather than one that skips: in each pass one job stops halfway (after 0-3 whole jobs, by turns, so the ME's records, the ones taken back and the ones never handed over fall in different places), and every later job of that batch makes nothing, as jobs queued behind a slow one would. `done` then stops at the gap, and needs past it take the real take-back path (`limit` lowered, `started` read, made here). The ME's side of a withdrawal (`r >= limit`) still runs only on a real ME: in the emulator a job runs at its push, before any record is taken back. The protocol's code is unchanged; a comment over `batch_job` states the invariant it rests on.
- **Proof** (second emulator pass, `mebatchtest` + `mebatchsimhalf` with the fix) [E]: **0 wrong** in every pass of all three runs, and no "held past" line, so the ME is never stopped:

  | Run | Passes | Records checked | Made by the ME | Made here | Taken back | Late | Waits for a record the ME was on |
  |---|---|---|---|---|---|---|---|
  | b30 route, JIT | 152 | 7,296 | 1,584 | 5,712 | 379 | 0 | 0 |
  | b30 route, strict | 61 | 2,928 | 608 | 2,320 | 151 | 0 | 0 |
  | d20, strict | 99 | 4,752 | 1,031 | 3,721 | 255 | 0 | 0 |

  (Made here counts the records the ME never made, the taken-back ones among them.) The take-back path now runs (151-379 records a run) and every answer is right. Lines both CPUs wrote (strict runs): 62 on b30 and 61 on d20, down from 286 and 442. All of them are the test's own arena (one 8 KB stretch: 27 lines, `08ca8f40`-`08caae80`) and the emulator's stack (35 lines on b30, 34 on d20, `09fb9840`-`09fbf900`); the pose-ahead arena's lines are gone [E; stretches read from the addresses against the first pass's link map, the same build text]. The play of the strict runs ended with no strict site and no crash.
- **On the PSP** (PSP-3000, b30 route to game time ~2,650, `mebatchtest mestash meringtest`, 4 runs: 2 with the keep alone, 2 with `meedramstate`) [M]: **0 wrong in 63,840-64,704 records a run (every run, both sides), `meringtest` 0 wrong in 10,664-10,792 jobs (the three runs with the ME running).** Real races were met and the protocol held through them:

  | Run | Side | Passes | Made by the ME | Made here | Taken back | Waits for a record the ME was on | Hand-overs refused | Late |
  |---|---|---|---|---|---|---|---|---|
  | 1 | keep alone | 1,330 | 531 | 63,309 | 102 | 12 | 0 | 1 |
  | 3 | keep alone | 1,333 | 7,568 | 56,416 | 1,537 | 201 | 21 | 0 |
  | 2 | `meedramstate` | 1,349 | 7,815 | 56,937 | 1,555 | 201 | 9 | 0 |
  | 4 | `meedramstate` | 1,347 | 7,771 | 56,885 | 1,551 | 192 | 8 | 0 |

  - **Waits above 0 on every run (12-201):** the main CPU did meet a record the ME was on, and waited for that one record; every answer was right.
  - **The ME's own withdrawal (`r >= limit`)** has no counter of its own; the 1,537-1,555 records taken back a run are the records it must have withdrawn from (the main CPU lowered `limit` over records the ME's job had yet to reach), so it ran, inferred from the counts rather than counted [U]. A counter would make it a [M] line.
  - **Run 1 (keep alone): the deadline fired once**, at game time ~180 (the 6th batch pass of that stretch): "a batch record (9) held past 10 ms; the ME is stopped". The path worked as designed: the record was made on the main CPU, `late` 1, every later pass made here, 0 wrong, the game ran on to the exit. The ME stayed stopped for the rest of the run, so run 1 lost the ME's audio and ring work: **tick 57.1 ms against 47.7 ms in run 3 over game time 150-2,640** (one pair, so indicative; the frame by the clock 240 against 210 ms). Why the ME held one record for 10 ms in one run of four is **not known**: the ring's worst pick-up was 4.2 ms in the interval before and 5.4 ms in the one that stopped the ME (0.7-2 ms in the earlier ones), and the frames were slow (170 ms), but nothing in the log names the cause. `mestash` printed nothing here because the batch deadline path calls `psp_me_stop` without `me_stash_dump` (the stash is dumped only on the slot-job and ring-job late paths) [D].
  - **`meedramstate`: no fault, no change.** Both runs with it ran to the exit with 0 wrong, 0 late and the same waits, take-back and ring-test figures as run 3. Timing over game time 150-2,640 (`abreport.py`): tick 47.0 ms (46.9, 47.2) against run 3's 47.7 (run 1 is the stopped-ME run, so the "keep alone" mean of 52.4 is not a fair side); the gap to run 3 is -1.5%, inside the runs' own spread, so the verdict is that it changes nothing measurable, as expected for 4 KB of state. 208-256 KB of the eDRAM took the ME's private stores through two runs (about 1,350 ring bursts each) without a fault.

### The 10 ms deadline read again (`megraceoff` keeps the old behaviour)

- **What the ME can be doing on a claimed record** [D]. Between `started = r + 1` and `done = r + 1`, `batch_job` runs only the limit check, `make` and the record's write-back. No yield, no audio mix (`psp_me_ring_step` mixes after `done`), no other ring job. `batchtest_make` is about 200 multiply-adds, a microsecond or two. Short of a bus stall, the ME cannot hold a test record for 10 ms.
- **The ME wasn't hung** [M, run 1's log]. The stop line says "halted itself after 1 ms": the ME was back in its idle loop at the stop word's first check, so the job had ended.
- **The likely cause is the main CPU, not the ME** [U]. The wait looks at `done`/`started` and then reads the clock. The heartbeat thread (priority 0x18, above the game's) formats and writes its whole report each second, and the stop line comes straight after one in the log. If the game thread is taken off the CPU between its look and its clock read, it comes back to a clock past 10 ms with the record already made, and stops a working ME. The ring's wait (`psp_me_wait`, 10 ms from the push) reads the clock the same way.
- **Making the record here and keeping the ME running is not safe** [D]. The ME still finishes the record, and its write-back covers whole lines. Those can land after the main CPU's copy and overwrite what the client wrote once `psp_me_batch_need` returned. So the recoverable path waits for the ME instead of racing it.
- **The change** (`psp_me_batch_need`, `psp_me_wait`):
  - A wait past the deadline by the clock looks once more. If the record (or ring job) is done by then, it is used, the ME keeps running, and the event is counted ("done at a second look") and logged with how long the main CPU was off its wait just before.
  - A batch record that really is still on the ME at 10 ms is counted as slow. The stash is dumped (where the ME is, its clock), and the main CPU waits up to 40 ms (`BATCH_RECORD_GIVEUP_US`, the limit an audio slot job has).
  - Only a record still held at 40 ms stops the ME, as before, now with the stash dumped.
  - `megraceoff` restores the old rule: the first wait past 10 ms stops the ME, with no second look.
- **New counters on the `ME: batches:` line:**
  - jobs the ME ended at the limit (its own withdrawals, counted by the ME in a fourth uncached word, `withdrawn`);
  - the longest wait for a record, and the longest stretch between two looks in one wait (anything in milliseconds is a thread above the game's);
  - past 10 ms: done at a second look / slow / late.
- **Cost.** None on the usual path: a subtraction and a compare more per look while waiting, and one uncached store when the ME withdraws. Compiled clean, campaign and `PSP_MULTIPLAYER=1`.
- **A lever found on the way** [U]: the heartbeat's report each second runs above the game's priority, so whatever it costs comes out of a frame. Its own time is not measured yet; if it is milliseconds, it could be formatted at a lower priority or spread over frames.
- **Proof wanted** (PSP, the same route and words): no "held past" line, the ME running to the exit in every run, `batchtest` 0 wrong. A "done at a second look" line, with a gap of milliseconds, confirms the cause. A "slow" line with a stash dump would mean the ME itself was held instead.

**Cost.** None by default: no client uses batches yet; `mebatchtest` is a check word.

## 4. The ME's private state (`meedramstate`)

**What.** The state only the ME touches (the ring's cursor handed to a yielding job; the sums of audio blocks mixed inside other jobs, 4 KB) lives in main RAM on lines of its own, or with `meedramstate` in the eDRAM at 208–256 KB (`psp_me.h`'s map).

**Key lines.** `psp_me.c`: `ME_PRIVATE` and `ME_AUDIO_SUMS` pick the copy by `ME_FEATURE_EDRAM_STATE`; the ME's start resets the main-RAM copy only (the features aren't set yet).

**Why.** What the ME alone writes belongs off the shared bus (§1: eDRAM work costs the main CPU a third of main-RAM work). It's off by default because 192–256 KB is the one stretch `edramcheck` doesn't cover; 192–208 KB took `mebusedram`'s stores through eight PSP runs without a fault [M], 208–256 KB took two b30 runs (about 1,350 ring bursts each) with `meedramstate` without a fault or a timing change [M, section 3's PSP run].

**Default.** Off. Turn on after a PSP run with `meedramstate meringtest` on the ladder (one b30 pair passed; the ladder is still to do).

## 5. R2: the sound cache in the eDRAM (`soundedram`, `soundgeo`)

Built for a trial (the decision to try it is taken; whether it stays is the PSP A/B's). Plan and gate: `me_programme.md` §5; the gate is met on both PSPs (§1).

**What.**
- `soundedram`: the sound cache lives in the ME's eDRAM, 256 KB–1,920 KB (1,664 KB), instead of 1,792 KB of main RAM. The engine is given the eDRAM address as the cache's base; only the ME, where the mixer runs, ever reads it.
- The stick reader can't write eDRAM, so each sound is read in pieces of 60 KB into two 64 KB staging slots in main RAM, and an ME slot job (`ME_JOB_SOUNDCOPY`, up to four pieces a job) copies them into place. The engine's `loaded` flag is set when the last piece is in.
- **Fallback:** the first time the ME stops (a failure, a sleep, HOME), the eDRAM is taken as lost for the session, even if `mewake` restarts the ME. From then on every voice playing from the cache is mixed silent (the mixer's own silent path: the position moves on, nothing is read), and new loads are marked failed. The log says so once (`sound eDRAM: … taken as lost`). The main CPU never touches an eDRAM address: `cache_lines` skips them, and when it mixes (the ME stopped, or the debug menu's mixer switch) it mutes those voices first.
- `soundgeo`: the start-up sound block keeps the size it always had (1,792 KB); the part the sound cache doesn't take becomes a second stretch of the geometry cache's ring. With `soundedram` on the PSP that is 1,664 KB (the block less the staging); with `sound512` and no eDRAM, 1,280 KB. Without `soundgeo`, `soundedram` takes only the 128 KB staging at start-up and the rest stays the heap's. With both `soundgeo` and `geowindow` on, the ring still has one second stretch: `soundgeo` takes it (set first, and larger than any window part measured), and `geowindow` logs that its part isn't used (`psp_geo_window`).
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

**Proven (emulator, ten runs at the commit above, `soundedram` simulated) [E].**
- No crash; every run reached its exit. The mode line reads "SIMULATED ... cache 1664 KB ... 1792 KB taken from the heap (128 KB staging)". a10 strict with `gemesim mewrites`: no system calls in the ME's work; 8 lines written by both CPUs, all in the emulator's stack range (09fbe040-09fbf900), none in the cache or the staging.
- Sounds in: a10 676, a30 287, b30 1,944 (a10 strict 66); 0 reads failed, 0 deleted while loading, 0 refused for a full queue. Asked to loaded, mean (worst): 222 ms (1.1 s), 114 ms (0.4 s), 429 ms (2.7 s). That includes the simulated stick reads and the once-a-frame poll, and the off side logs no figure to set it against, so "a frame or two" is not separated here; no sound was cut as "not cached in time" on either side. The ME mixed every block (433 of 433), none late, no gap heard, the game never waited on the mixer's lock.
- **Play unchanged:** a10's play hash, global and local seeds are identical to the off side's at all 9,000 ticks. The whole-state hash differs from tick 72: the first allocation is "object looping sounds" (tick 90), then objects; actor, prop and particle only from tick 6,960. That is sound state following load timing, outside what the game plays with. b30 parts at tick 1,357, inside the band where b30 parts on any change or an identical rerun (~1,326); its first 45 s are identical. No off-against-off control was in the batch.
- **Refused for room, at 1,664 KB against 1,792 KB:** a10 0 against 27 (one burst of 27 around 110 s of game time in the off run, none in the on run, which loaded 6 more sounds); a30 0 against 0; b30 0 against 0. The 1,664 KB cache fits what these routes ask, with room to spare: the most used in any one second was 912 / 680 / 1,156 KB (a10 / a30 / b30), the most playing at once (locked, every class) 1,048 / 1,080 / 908 KB. One burst in one run each is a thin sample of a10's 27: the same burst could land at 1,664 KB on another timing.
- **Fallback:** with the ME stopped at 15 s (`mewaketest mewake`), the loss is logged once, "voices from the cache mixed silent" once, the 48 sounds already in stay silent, 576 loads asked after it are marked failed, the main CPU mixes what is left; no crash. The wake brings the ME back in 22 ms and the cache stays lost for the session, as designed.
- **`sound512 soundgeo`** (1,280 KB of extra geometry cache; the PSP's would be 1,664 KB): a10 geometry misses 846 against 1,001 (-15%), frames that missed something 66 against 81, 338 blocks evicted against 581; b30 729 against 808 (-10%), 64 against 79 frames, 188 against 355 (b30's play differs after tick ~1,350: indicative). The sound cache in those two runs is 512 KB, so their sound refusals are the stand-in's, not R2's.
- **Cost in the emulator:** the sound steps grow, `sound_render: prioritise` and `update channels` by 0.2 (a30) to 1.7 ms a frame (b30, where statecrc's hashing also fills the frame); a30, without statecrc, totals 45.6 against 46.1 ms a frame, inside the spread. The simulated ME's copy job runs on the main thread, so this overstates what the PSP pays [E]; the PSP A/B measures it.
- **Music and a split mixer.** The cache report has no split by sound class, so how much of the cache music holds is not known: class 32 (music) appears in these logs once (a looping-sound fade-out in b30 at 241 s), and music starts are not logged. What the data does show is that the cache is not short of room: no sound refused at 1,664 KB, a working-set peak of 1,156 KB, 508 KB free at the busiest second the 5 s reports saw. The rule for acting on this data: no refusals, change nothing; a few, trim the ME stack's 128 KB reserve (64 KB was the most the ladder measured); real drops or a large music share, split the mixer (music on the main CPU from main RAM, effects on the ME) behind its own keep word. Here there are no refusals, so nothing moves. A split mixer stays a candidate only if the PSP A/B shows refusals. To size it then, add a per-class line (bytes loaded, bytes held) to the cache report first.
The eDRAM path itself only the PSP can prove.

**PSP A/B.** [M] One hardware job on a PSP-1000, a10 on the route of the b139 hand test, keep `nearauto assettrack geo2 exitat300`, sides `-` and `soundedram soundgeo`, two runs each, the b139 release bake and PGO profile. All four runs ended in the game (exitat, the ME halted itself 1 ms after its stop), no crash; the `sound eDRAM:` line says "in the ME's eDRAM" on both on-runs, with 0 reads failed, 0 refused (queue full), 0 blocks mixed silent, no fault of any kind.

| Stretch (game time) | Frame off / on, ms | Tick off / on, ms | Gap against the runs' own spread |
|---|---:|---:|---|
| cinematic 150-570 | 34.2 / 34.4 | 3.4 / 3.6 | in the noise |
| flight 300-2000 | 43.9 / 43.7 | 5.3 / 5.2 | in the noise |
| landing 2000-2700 | 64.1 / 63.1 | 6.2 / 6.2 | in the noise |
| beach 2700-6000 | 114.0 / 117.3 | 15.5 / 15.8 | **real, on is slower** (+2.9% frame, +3.5% drawing, +2.2% tick) |
| bunker 6000-8100 | 74.0 / 74.7 | 9.4 / 9.2 | frame in the noise; tick -0.2 ms, real by the tool's rule but tiny |

- **The geometry cache did what the emulator said** [M]: misses 847 against 978 (-13.5%; the emulator said -15%), evicted 262 against 596, cache 3,712 KB against 2,048 KB, synchronous reading 1,442 ms against 1,612 ms. That is 170 ms in a 9,000-tick run, about 0.03 ms a frame: too little to show in any stretch. The worst frame's read (134 ms) is the same either way: first loads, not churn.
- **The ME paid for it** [M]: the ring refused about 6,700 jobs a run as "ME busy" (off: 0), 12% of the 58-59k jobs the off side pushed, and the main CPU ran them. The sound copy job is a long slot job that never calls `psp_me_yield` (§2), so while it copies, ring pushes are refused. Sounds were ready 253-264 ms after asked (the emulator's simulated job: 222). This is the likely cause of the beach gap, which is where the most sounds load [U].
- **Sound room**: loads failed for room 15 and 11 on the on-runs against 27 and 40 off (a10 fails some either way; 1,664 KB is not worse than 1,792 KB here).

**Verdict.** On hardware `soundedram soundgeo` does not pay as built: the geometry misses fall but the time saved is lost in the noise, and the beach stretch gets slower. It stays off by default. It is exact, so the cost is time only, not correctness. Before another A/B: make the copy job yield to the ring every few KB (`psp_me_yield` inside the loop, or pieces of 16 KB in place of 60 KB), then re-read the `refused (ME busy)` count (want 0) and the beach stretch. Without the refusals the ring's jobs are back and the expected result is the sound path costing nothing and the geometry gain staying small on a10; the larger win would need a map whose geometry churn is higher [U].

**Default.** Both off. `soundedram` is refused with `edramcheck`. Turn either off by leaving its word out.
