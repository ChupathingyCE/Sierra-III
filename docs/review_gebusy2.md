# Review: `gebusy2`, the GE's busy time under M6 #2 (branch `gebusy2`, base w140 bc12443)

**`gebusy2` is hardware-unsafe until the fixed build (its own GE callbacks, below) has run on the PSP:** the first build hung the PSP-1000.

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

**Cost:** under M6 #2, one SIGNAL+END (8 bytes) and one uncached board write per chunk on the ME. On the main CPU, one interrupt per chunk (a few us), as `gebusy` already costs when the main CPU builds, plus two interrupt-off sections per forward and per wait. The main CPU's interrupts take time from the tick, so `gebusy2` frames run a little slower than plain ones. Read the GE's time from it, and take the frame rate from runs without it. (It hangs on the PSP as built: see "Results".)

**Changed only under `gebusy2` or `metail`:** the list's last few draws go to the GE at the ME's frame end, not at the main CPU's finish. The play is the same at every tick, and the pictures differ from the default no more than the default differs from itself (proof below). Only when the GE starts that tail moves.

## The hang on the PSP, and the fix [D]

**Where it stopped [M] [D].** Both hung runs end the same way: frames stop, game time stops, the main CPU is asleep in the drawing phase, and the ME's audio and render jobs carry on with none failed. Under `geinflight` the frame's end runs at the next frame's first call, in the drawing phase: `inflight_wait_me` collects the ME's frame, writes the main CPU's cache back and drops it, then `frame_end_show` adds the finish (`sceGuFinish`) and waits for the GE (`sceGuSync`). The wait there never returned: the GE never reached the list's FINISH.

**Why [D].** pspgu's signal callback (`callbackSig`, pspsdk `sceGuInit.c`) writes each signal's id into its settings, `gu_settings.signal_history[]`, and counts it in `gu_settings.signal_offset`. In the link map `gu_settings` sits at 0x415fb0 and `gu_contexts` at 0x415fec, so `signal_offset` and the history's last twelve entries share a 64-byte line (0x415fc0) with `gu_contexts[0].list.start` and `.current`: the list's write position, which the ME moves with every command it writes. Old `gebusy` never met this: its SIGNALs come only while the main CPU writes the list itself, and under M6 #2 it adds none. `gebusy2`'s SIGNALs come from the ME's chunks while the ME is still writing. Each one interrupts the main CPU, whose cache takes that line from RAM with the write position as RAM held it then (behind the ME's) and marks it dirty. When `inflight_wait_me` writes the main CPU's cache back (`sceKernelDcacheWritebackInvalidateAll`), that old position goes back over the ME's. The finish is then written at the old position, inside the part of the list the GE has already run, and the stall address handed over is behind the GE: the GE never reaches a FINISH, and the main CPU waits for ever [U: the exact way the old position breaks the list; the shared line and the write-back are read in code and the link map]. Whether the line is still dirty at that write-back depends on whether the tick evicted it first, which is why the hang came after a few frames on a10 and after about 70 on d40 [U]. The emulator has no caches that disagree, so it can't show this.

**The fix.** Under `gebusy2` the lists get GE callbacks of our own: `gebusy_start` registers them with `sceGeSetCallback`, and the `sceGeListEnQueue` wrapper (`psp_trace.c`) passes their id in place of pspgu's (`psp_ge_list_callback`). They call `gebusy_signal` and `gebusy_finish` with the same 16-bit ids, and they write only `PSP_MAIN_SIDE` words, on cache lines of their own. pspgu's callback, its history and the event flag it sets are left out (nothing in the port reads them; pspgu's `sceGuSync` uses `sceGeDrawSync`). Plain `gebusy` keeps pspgu's callbacks as before. If `sceGeSetCallback` fails, `gebusy2` turns off and `gebusy` runs alone.

- No other word the main CPU's interrupt writes is shared with the ME: `gebusy_signal`, `gebusy_finish` and `gebusy_add` touch only `PSP_MAIN_SIDE` sums, `gebusy_me_handed` and `gebusy_wait` likewise, and the board is read and written only through the uncached view [D].
- Proof: the emulator (gebusy2's readings and play the same, below) and a PSP run of the fixed build, `keepsets="gebusy | gebusy2"` on a10 with `exitat60`, which must end in the game.

## `metail` (opt-in lever, off) [D]

Under M6 #2 the wrapper batches stall updates (`STALL_BATCH` 16). At the frame's end on the ME, the last 0-15 updates (the end of the transparent flush, the TAA resolve) are never published. `defer_wait_me` passes on only the last published address, so the GE gets those draws only with `sceGuFinish` in `frame_end_show`, after the next frame's ticks. The main CPU then waits for the GE there ("waiting for the GE": 14-18 ms a frame on d40, 5-9 on a10 [M]). `metail` publishes the tail when the ME's frame ends: the same `gebusy_me_tail()` call, without the SIGNAL.
- Exact: the same list, a 4-byte `sceGuGetMemory` jump more.
- How much of the tail wait it removes isn't known until the PSP shows how long that tail takes.
- Read it from an A/B in one job: `keepsets="gebusy | gebusy metail"`, comparing the frameend line's "waiting for the GE" and the frame.

## Proof

- Compiles: a host syntax check of `psp_ge.c` against pspsdk's headers, with the campaign, `PSP_MULTIPLAYER=1`, `PSP_RECON=1` and `PSP_METRICS=1` switches, gives no new warnings. The full builds are listed in the merge request.
- Emulator [E]: see "Results".

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
- **Pictures:** `framecrc` differs from the default under `gebusy2`, and also under the old `gebusy`, which adds no SIGNAL under M6 #2 and only registers the GE callbacks (637 of 2,998 b30 frames).
- **Do the default's pictures repeat?** No, not under M6 #2 in the emulator, so `framecrc` is no exactness test there; play is. The same EBOOT, the default twice on one host and in one batch: 488 of 2,998 b30 frames differ. The same EBOOT on two hosts with the same emulator: 2,906 b30, 2,386 a10, 2,642 d40 frames of ~2,990. Play and seeds stay the same at every tick in every pair.
- **`metail` alone** against the default, one host, one batch: 505 b30 frames (inside the default's 488), 35 a10, 17 d40; play the same at every tick. Inside the default's own spread.
- **`gebusy2`'s** differences (2,947 / 2,439 / 2,684) are larger than one host's default spread but no larger than the default's across two hosts. The instrument's interrupts likely shift the timing that decides which frames differ [U]; play is exact. It is a recon instrument, off by default.
- **Compiles:** campaign (PGO), `PSP_MULTIPLAYER=1` and `PSP_METRICS=0` all build.

**On the PSP-1000 [M]** (one EBOOT, a10 and d40 hands off, `gebusy | gebusy2 | gebusy metail`, one run each):

- **`gebusy2` hung on the PSP** (the build before the fix below). On a10 it drew 2-3 frames and stopped at game time 9; on d40 it ran about 70 frames and stopped at game time 77, as the level's first draws began. The main CPU sat asleep in the drawing phase until the run's wall, the ME's render jobs all done and none failed. The emulator never hung. The cause and the fix are under "The hang on the PSP, and the fix" above. **Until the fixed build has run on the PSP, `gebusy2` is hardware-unsafe: don't use it there.** It is off by default, and its off path is the same code as before.
- **So the GE's busy time under M6 #2 is still unmeasured on the PSP.** What the `gebusy` side shows, from the main CPU's waits and the ME's job times:

| Map, stretch | Frame | Ticks / drawing / rest | Main CPU waits for the ME / the GE | ME replay a job | Main CPU asleep | Binds |
|---|---:|---|---|---:|---:|---|
| a10 opening | 40.0 | 8.4 / 20.2 / 11.5 | 3.3 / 4.7 | 14.1 | 41% | no lane full (the GE or the refresh [U]) |
| a10 middle | 46.3 | 12.4 / 28.8 / 5.0 | 10.2 / 3.3 | 27.8 | 29% | the ME's replay |
| a10 beach | 90.4 | 46.0 / 41.1 / 3.2 | 7.5 / 1.4 | 40.8 | 12% | the main CPU |
| d40 slow stretch | 43.9 / 38.6 | 1.8-3.6 / 38.7-25.6 / 3.5-9.4 | 7.2-4.8 / 15.5-9.3 | 13-14 | 48-49% | the GE |
| d40 later | 34.8 / 33.6 | 4.4-5.0 / 15-16 / 14 | 2.3-1.6 / 1.9-1.3 | 17-19 | 35-39% | the 30 fps cap |

  (ms a frame; drawing includes the waits.)
- **`metail`: no gain.** a10 whole 54.6 -> 54.8 ms. The beach is 90.4 -> 87.8, but its GE wait moved only 0.15 ms and its tick 0.9, neither of which `metail` can remove. d40's slow stretch is 43.9 -> 44.0 and 38.6 -> 38.6, with the GE wait unchanged (15.5, 9.3). That wait is the GE still drawing the frame, not the unpublished tail. `metail` stays off; no merge request.
