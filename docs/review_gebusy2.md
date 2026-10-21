# Review: `gebusy2`, the GE's busy time under M6 #2 (branch `gebusy2`, base w140 bc12443)

An instrument, off by default, plus one small opt-in lever it uncovered (`metail`). Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## What `gebusy` measures under M6 #2 [D]

`gebusy` puts a SIGNAL at the end of each chunk of up to 16 draws that the stall wrapper (`__wrap_sceGeListUpdateStallAddr`) passes to the GE. Each SIGNAL's interrupt closes its chunk, and the list's FINISH closes the last one. Under M6 #2 (`gedefer gemeon geinflight`), the Media Engine replays the frame. Its stall updates take the wrapper's `PSP_ME_WORK()` branch, which publishes the address for the forwarding thread and returns before gebusy's SIGNAL code. The forwarding thread, `defer_wait_me` and `defer_drain` then pass those addresses to the GE through `__real_sceGeListUpdateStallAddr` directly. So no chunk the ME builds is timed.

The only part still timed is the frame's FINISH. `frame_end_list` on the ME leaves the finish to the main CPU (`inflight_finish_left`), and `frame_end_show` calls `sceGuFinish` after the next frame's ticks. The FINISH chunk runs from that call (`gebusy_finish_handed`) to the FINISH interrupt. That covers the GE's tail at that moment: the draws it still had queued, plus the up to 15 draws the batching had not yet published (see `metail`). That is the 0.1-0.3 ms a frame on a10's busy stretch, and the 0 / 0 / 0 / 0 / 0 on b30's full-set side. The same holds for `gedefer` alone, where the main CPU replays the frame marked as ME work: `PSP_ME_WORK()` is true there too.

The suffix that should have flagged this, `(not while the ME builds)`, tested `PSP_ME_WORK()` in `gebusy_window`, which runs on the main CPU outside the replay. It never printed.

## The new measure

The keep word `gebusy2` turns on `gebusy` as well. Everything below is in `psp/src/psp_ge.c`.

- **The ME ends its chunks with a SIGNAL too.** In the wrapper's ME branch, `gebusy_me_signal()` inserts the SIGNAL before the address is published. It reuses gebusy's re-entry trick (`gebusy_inserting`). The SIGNAL's 16-bit id is `0x8000 | kind << 8 | slot`, so the interrupt gets the chunk's kind from the id and the ME writes no array that the main CPU reads.
- **No clock on the ME.** Every time comes from the main CPU's `sceKernelGetSystemTimeLow`. The ME writes each chunk's end address, then the chunk count, to `GEBUSY_ME_BOARD`, which both CPUs use only through the uncached mirror (0x40000000), on lines of its own. The writes are ordered with `sync`.
- **The hand-over time.** Every place that passes an ME stall address to the GE first calls `gebusy_me_handed(stall)`: the forwarding thread, `defer_wait_me`, the fallback in `defer_hand_over`, and `defer_drain`. A finish (`stall_force`) marks every chunk. Each chunk on the board whose end is at or below the address gets the current time, set before the address reaches the GE, so the interrupt never reads an older slot.
  - Within a frame the list only grows, and a frame's last address covers all of its chunks, so comparing addresses gives the chunks' order.
  - The marking runs with interrupts off, because both the forwarder and the game thread call it.
- **The busy time.** `gebusy_add` is unchanged: a chunk runs from the later of its hand-over and the previous chunk's end, to its SIGNAL. So the sum is the GE's own busy time, whoever kicked the list. "gebusy by kind" now covers the ME's chunks. `gebusy_kind_cut` also runs on the ME under `gebusy2`.
- **The list's tail.** On the ME, `frame_end_list` publishes the draws left since the last stall update as a chunk of their own (`gebusy_me_tail`), so the transparent flush and the TAA resolve are timed. Otherwise they would reach the GE only with the main CPU's finish.
- **A second log line after each frameend line:**
  `gebusy2: the ME's chunks N a frame, of M; the GE idle in a list I ms a frame; busy while the main CPU waited for the ME A, for the GE B ms a frame`
  - *idle in a list*: the GE had reached the stall and waited for more draws mid-frame. That is the forwarding and the ME's pace showing on the GE. The wait for the FINISH isn't counted.
  - *busy while the main CPU waited*: the GE's busy time inside the main CPU's wait for the ME's job (`defer_wait_me`) and inside its wait for the GE (`sceGuSync` in `frame_end_show`). `gebusy_wait()` brackets both.

The frameend line's `GE busy X ms a frame, Y% of the clock` keeps its form, so `benchreport.py` and `memcensus.py` read it as before. Without `gebusy2` its suffix now reads `(the ME's lists not timed: gebusy2)`.

**Cost:** under M6 #2, one SIGNAL+END (8 bytes) and one uncached board write per chunk on the ME. On the main CPU, one interrupt per chunk (a few us), as `gebusy` already costs when the main CPU builds, plus two interrupt-off sections per forward and per wait. The main CPU's interrupts take time from the tick, so `gebusy2` frames run a little slower than plain ones. Read the GE's time from it, and take the frame rate from runs without it.

**Changed only under `gebusy2` or `metail`:** the list's last few draws go to the GE at the ME's frame end, not at the main CPU's finish. The picture and the play are the same (proof below). Only when the GE starts that tail moves.

## `metail` (opt-in lever, off) [D]

Under M6 #2 the wrapper batches stall updates (`STALL_BATCH` 16). At the frame's end on the ME, the last 0-15 updates (the end of the transparent flush, the TAA resolve) are never published. `defer_wait_me` passes on only the last published address, so the GE gets those draws only with `sceGuFinish` in `frame_end_show`, after the next frame's ticks. The main CPU then waits for the GE there ("waiting for the GE": 14-18 ms a frame on d40, 5-9 on a10 [M]). `metail` publishes the tail when the ME's frame ends: the same `gebusy_me_tail()` call, without the SIGNAL.
- Exact: the same list, a 4-byte `sceGuGetMemory` jump more.
- How much of the tail wait it removes isn't known until the PSP shows how long that tail takes.
- Read it from an A/B in one job: `keepsets="gebusy | gebusy metail"`, comparing the frameend line's "waiting for the GE" and the frame.

## Proof

- Compiles: a host syntax check of `psp_ge.c` against pspsdk's headers, with the campaign, `PSP_MULTIPLAYER=1`, `PSP_RECON=1` and `PSP_METRICS=1` switches, gives no new warnings. The full builds are listed in the merge request.
- Emulator [E]: see "Results" (filled in once read).

## Results

**Emulator [E]** (one EBOOT; b30's route, a10, d40 hands off; `nomouth fixedticks fixedseed`, game time to ~5,950 ticks):

| Map | `gebusy2`, M6 #2 (the default) | `gebusy`, `m6off` | `gebusy`, M6 #2 |
|---|---:|---:|---:|
| b30 | 18.7 ms a frame (max 43.4); 83.7 of 83.7 chunks a frame the ME's | 18.0 (max 41.6) | 18.4, all of it one FINISH chunk ("other") |
| a10 | 8.0 (max 25.2); 33.3 of 33.4 the ME's | 6.7 (max 17.4) | |
| d40 | 4.7 (max 7.8); 30.5 of 30.5 | 4.7 (max 8.0) | |

- Under M6 #2 the new measure reads what `gebusy` reads where it works (`m6off`, every list kicked by the main CPU), in the emulator's own GE model. That is the plausibility check. The cache model has no GE figure to compare with.
- The by-kind split agrees too: b30 models 22.6 / 23.0, level 2.0.
- Old `gebusy` under M6 #2 reads the whole frame in the emulator only because there the simulated job passes the frame on all at once, just before the finish. On the PSP the forwarder passes it on during the ticks, so the finish sees only the tail (0.1-0.3 ms on a10).
- **Play:** the play hash and both random seeds are the same at every one of 6,000 ticks on all three maps (default vs `gebusy2`).
- **Pictures:** `framecrc` differs from the default under `gebusy2`, and also under the old `gebusy`, which adds no SIGNAL under M6 #2 and only registers the GE callbacks (637 of 2,998 b30 frames). See the follow-up batch below.
- **Compiles:** campaign (PGO), `PSP_MULTIPLAYER=1` and `PSP_METRICS=0` all build.

**On the PSP:** pending. The spec is in the merge request.
