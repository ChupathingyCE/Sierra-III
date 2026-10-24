# Review: me-stall (why a batch record looked held for 58 ms)

Branch off me-foundations. One change in `psp/src/psp_me.c`: a batch job looks at the limit before it claims a record. A second guard ends a wait once the batch's last job has ended. Both are exact. `meclaimold` restores the old claim order. `megraceoff` turns the guard off too, along with the rest of the deadline handling.

## 1. The cause: a stale claim, not a stalled ME

- **What the PSP showed** (me-foundations §3) [M]: one wait in four runs found record 5 of a `mebatchtest` pass "still on the ME" at 10 ms and at 40 ms. In the stash, the ring was empty, the ME was at its idle note, and the last ring job (type 11) had stopped at step 0. 11 is `ME_JOB_BATCH` among ring jobs; the sound copy's 11 is a slot type. The ME's own clock puts that job's end at the start of the wait. After that the ME looped idle and served one audio slot job. **The ME was never held.** The "below 0" reading in §3 was the label, not the value: the idle pass came 10 and 17 ms *after* the note.
- **How the words get there** [D]: `started` is one word for every job of a batch. `mebatchtest` hands records over in chunks of 6, so job A has records 0-5 and job B has 6-11. In the old order:
  1. A claims record 5 (`started = 6`). The main CPU needs record 5: it lowers `limit` to 5 and reads `started = 6`, so it waits.
  2. A sees the limit and withdraws (`started = 5`).
  3. B runs next. It claims record 6 (`started = 7`), sees the limit and withdraws (`started = 6`).
  4. If the main CPU's next look comes after step 3, it reads `started = 6 > 5` and `done = 5`. That is "claimed, not done". Nothing will ever change it, so it waits to the deadline and stops a working ME.

  The window between steps 2 and 3 is the few microseconds B's start takes. An interrupt or one slow poll on the main CPU is enough to miss it. That fits one event in four runs, with tens of waits a run. The earlier ~10 ms stop under the old deadline rule ("halted itself after 1 ms", the ME idle) fits the same mechanism [U].
- **The 19.7 ms gap between two looks** [U]: it is the wait's own iteration that logged the "slow" line and dumped the stash. That gap was measured across those two log calls, so the logging (and any log flush it woke) is the likeliest owner. It is not a second stall, and it explains the 57.7 ms total: 40 ms plus the late line, the stash and the stop.
- **Why the emulator never showed it** [D]: there, a pushed job runs inside the push, before the main CPU lowers any limit, so no job ever withdraws at its first record.

## 2. The fix (default on; `meclaimold` is the old order)

- **`batch_job`**: `if (!claim_old && r >= *limit)` withdraws *before* `*started = r + 1`. The claim and its re-check after it stay as they were, for a limit lowered between the two.
  - Why that is enough [D]: `limit` only falls. A job that withdrew did so at a limit no higher than its record. Every later job's first record is past that, so the pre-check declines it and `started` is never written past a record an earlier job withdrew at.
  - Within one job, the final words are the same as before.
- **`psp_me_batch_need`'s wait**: once `RING_POPPED` has passed the batch's last ticket, no job of the batch is left on the ME. The wait ends, and the record is made here if it isn't done. A stale claim is counted on the `ME: batches:` line as "stale claims taken back". With the fix on, the count should be 0. With `meclaimold`, it counts the old bug's events instead of stopping the ME.
- **Exactness**: neither change alters how a record is made. They only decide which CPU makes it, and the main CPU makes only a record no job will touch again. `mebatchtest` checks every record against the same function.
- **Cost**: the ME reads one uncached word (`features`) a job and one compare a record. The main CPU reads one uncached word per look, only while it waits for a record (a few a second, microseconds each). Compiled clean, campaign and `PSP_MULTIPLAYER=1`.
- **The keep words**: the ME reads `ME_FEATURE_CLAIM_OLD` (128); the simulated ME reads `me_claim_old`. Both feature writes (launch, and the restart after a wake) carry the bit.

## 3. Proof wanted on the PSP

A `keepsets` A/B on one build: `-` against `meclaimold`, with `mebatchtest mestash meringtest`. Pass: 0 wrong both sides, "slow 0, late 0" and "stale claims taken back 0" on the fixed side. The `meclaimold` side should show stale claims above 0 and still no stop (the guard catches them). If it does, that confirms the mechanism [M].
