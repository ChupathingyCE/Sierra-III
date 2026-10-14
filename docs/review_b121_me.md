# Review: ME phase 1, the job ring and the slant sort (`meplumb121`)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). Same shape as `review_b120.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `meplumb121`, from `w121` (`274f08a`).

| # | Change | Kind | Default |
|---|---|---|---|
| 1 | The ME job ring (`psp_me.c`) | ME plumbing | off (`meslant` or `meringtest` turn it on) |
| 2 | The slant sort split with the ME (`psp_ge.c`) | speed (exact) | off (`meslant`) |
| 3 | `meringtest`: the ring's own check | instrument | off |
| 4 | `nomeaudio` with a ring word: the ME for the ring alone | test plumbing | n/a |

With the words off nothing runs that didn't before: the ME's idle loop reads one features word where it read mehalt's (the same bus traffic), each sorted level draw tests one word, each frame's end tests one word. Proven below: the word-off build plays the same game as `w121`, with the same frame and tick times.

---

## 1. The job ring

**What.** Beside the three slots (one job each; the caller sleeps or spins until it's done), a ring of 8 jobs. The main CPU pushes a job, goes on with its own work, and collects the answer with a wait when it needs it.

**Who writes what.** Each CPU's cache writes back whole 64-byte lines and doesn't see the other's, so every line has one writer:

| Lines | Written by | The other side, before reading |
|---|---|---|
| `ring_pushed` (a line of its own) | main CPU, uncached | reads it uncached |
| `ring_popped` (a line of its own) | ME, uncached | reads it uncached |
| `ring_requests[8]` (2 lines each) | main CPU, cached, written back (`cache 0x1A`) before `pushed` moves | ME drops its copy (`cache 0x1B`) |
| `ring_results[8]` (1 line each) | ME, cached, written back before `popped` moves | main CPU drops its copy |
| the job's output | ME, likewise | main CPU drops its copy |
| the job's inputs | main CPU (`psp_me_writeback`) before the push | ME drops its copies: a range, or its whole cache past 16 KB |

**Key lines** (`psp_me.c`):
- `psp_me_push()` refuses, so the caller does the work itself, when:
  - the ring is off or full;
  - the ME holds a render or probe slot job (milliseconds of work a ring job would wait behind);
  - a transfer breaks the rules. The output must be 64-byte aligned whole lines, and everything must be in main RAM (`ring_in_ram`: not VRAM, which the ME can't see).
  
  Then it writes the request, `lines_writeback`, `RING_PUSHED = t + 1`, `sync`, and `me_wake()` (mehalt only).
- `ring_run()` (on the ME):
  1. drops the request's lines and the inputs';
  2. runs the job with a **stack sentinel** 4 KB below the stack pointer, checked after;
  3. writes back the output and the result, then `sync`.
- `me_ring_drain()` reads `pushed` once, runs every waiting job in order, and stores `popped` after each.
- `meLibOnProcess()`:
  - The ring is served right after the audio slot (`k == ME_SLOT_AUDIO`), so a ring job never waits behind a render job.
  - After a ring job the ME **lingers** (`me_ring_linger`): for 64 passes (~2 ms) it looks at `pushed` every ~1.5 µs instead of once per ~36 µs idle pass. The renderer pushes in bursts (a level's draws), and every microsecond a job waits to be picked up may be a microsecond the main CPU waits at the end.
- `psp_me_wait()` spins with ~1 µs between uncached looks at `popped`, reading the clock and the stop word every 16th look.
  - **The watchdog:** it returns 0 when:
    - the job isn't done 10 ms after its push;
    - the ME stopped (HOME, sleep);
    - the answer carries the wrong ticket or a bad status (a broken sentinel, an unknown type).
  - The caller then does the work itself, and the ring stops for the session. A late job may still write its output, but nobody reads it again.
  - So a stuck ME never hangs the game.
- `psp_me_report()` prints an `ME: ring:` line each heartbeat (below).

**Why.** b90 measured the ME as fast as the main CPU. What lost was the hand-over: ~320 µs a job of sleeps and whole-cache operations (`media_engine.md`). This hand-over is designed for small jobs:
- no sleeps;
- no system call per job: the main CPU's cache work is the `cache` instruction on the lines involved;
- no whole-cache drop on the main CPU;
- pick-up within microseconds.

It follows the agreed design inputs (`handoff_2026-10-13c.md` §3, `research_brief_for_master.md` §1.4–1.5).

**The log line**, every heartbeat while the ring is on:

`ME: ring: N jobs (D done before the wait), the ME's own time X us a job (the job's code J; picked up within L us, worst W); the main CPU's push P us a job, wait Q us a job (worst R), push to answer T us; refused ...; F failed`

- X − J is the ME's cache work around a job.
- L bounds how long a job sat before the ME saw it (counted from the ME's last look that found nothing).
- P and Q are what the main CPU pays per job, besides the client's merge.
- In the emulator (no ME code runs) the job runs inside the push, and the line says SIMULATED.

**Default.** Off.

## 2. The slant sort split with the ME

**What.** `angle_sort`'s bucket loop calls `angle_bucket_one` once per triangle. It is the renderer's most expensive CPU step on the PSP: 3.8 ms a frame in b86, and 4–5.7 ms on a30, b30, c20 and d20 in b119's probes [M]. Now it is split:
- the ME takes the back part of a draw's triangles through the ring;
- the main CPU does the front part at the same time;
- the main CPU then copies the ME's buckets in and adds the ME's totals.

**Key lines** (`psp_ge.c`):
- `slant_hand_over()`:
  - reads its words once: `meslant`, `meslantcheck`, `meslantminN`, `meslantbiasN`;
  - splits only draws of at least 96 triangles;
  - gives the ME `(count − 32) / 2` triangles, since the ME starts later and drops its cache first;
  - writes back the part's indices (the draw's list copy may be this frame's). The vertices are already in main RAM: they are written back when a stream is built or moved, for the GE.
- `psp_ge_me_slant()` is the job. It calls the same `angle_bucket_one` as the main CPU's loop and returns the part's bucket totals, two to a word.
- `slant_collect()`:
  - waits, then copies `me_slant_out` into `angle_bucket_of`;
  - with `meslantcheck`: works the part out again on the main CPU, counts every differing bucket and total, and uses its own answer;
  - after a failed wait: does the part on the main CPU.
- `angle_sort()`: `me_slant ? slant_hand_over(...) : count`. With the word off, that is one test a draw.
- The split runs only where the main CPU draws the level. Under `gemeon` the whole level draw already runs on the ME (`PSP_ME_WORK()`), and no ring job is pushed while a render job is out.

**Why it is exact.**
- Both CPUs run the same function on the same floats.
- The ME's FPU gives the same bits: b90 found 0 differing buckets over 4.7 million triangles [M].
- The buckets decide only the order of the triangles in the display list and each bucket's texture slope. With the same buckets, the list is byte for byte the same.

**Default.** Off (`meslant`) until the rig measures it. Two tuning words for the rig:
- `meslantminN`: the smallest draw that is split (default 96);
- `meslantbiasN`: how many triangles fewer the ME gets (default 32).

## 3. `meringtest`

**What.** A burst of 8 ring jobs at each frame's end (`psp_ge_frame_end`, the ring's one producer thread):
- Each job reads 256 bytes the main CPU has just rewritten with a new pattern.
- It writes 256 bytes the main CPU read a frame before.
- It returns the input's sum.
- The answers are collected last first.

Log: `ME: ringtest: B bursts, J jobs checked, W wrong (words or sums), S bursts short of a full ring, F jobs failed`.

**Why.** The slant job's inputs barely change (level vertices are written once), and its vertex range usually takes the whole-cache path on the ME. This test exercises the paths the slant job doesn't:
- the range drops and write-backs;
- an index that wraps every frame;
- 8 jobs out at once;
- waits out of order.

A missing write-back or drop on either CPU shows as a wrong word. That makes it the rig's sharpest check of the ownership rules on a real ME.

**Default.** Off.

## 4. `nomeaudio` with a ring word

**What.** Before this branch, `nomeaudio` kept the ME off. With `meslant` or `meringtest` it now starts the ME for the ring alone, and the mixer stays on the main CPU (`psp_dsound.c`: `me_wanted = 0`). Without those words it is unchanged.

**Why.** An A/B of `meslant` should leave audio where it was. In the emulator, ME audio is simulated on the main CPU, and its timing moved the game state between the sides.

## Proven [E] (the emulator runs no ME code: each ring job runs inside the push, on the main CPU, marked as the ME's work)

Lab runs (lab-repo, `sierra-batch`): 38511103442 (121a), 38511131463 (121b), 38512381219 (121c), 38514031556 (121d).

- **Exact.** `meslantcheck` found **0 differing buckets or totals over 46.6 million triangles** handed to the ring: b30, a50, c20 and d20, with and without ME audio, plus the strict run. The game's play hash (`statecrc`) matched at every shared tick, word on and off, on d20 (10,720 ticks), a50 (17,155) and c20 (13,478) with `nomeaudio`. b30's play hash parts at tick ~1,357 only with ME audio on and the check's extra time, as it does across any timing change (b30's known audio-timing split). The whole-state hash differs in the `objects` record, which drawing writes and which the play hash leaves out. The same happens across `probes` on `w119` (lab 38443084885, from tick 102 on d20).
- **Failures 0.** 0 fell back, 0 refused, 0 failed in every run. Under the strict interpreter (121b, b30, 1,623,289 triangles to the ring): 0 bad accesses; the ME write trace found **0 lines written by both** the ME's work and the main CPU while a job was out (37 lines the ME's, 649 the main CPU's); the ME-strict check found **no system call** in the ME's work.
- **Off changes nothing.** `meslant121c`'s word-off runs against `w121a`'s (lab 38509454132, base `274f08a`), same keep words: the play hash is equal at every shared tick (d20 10,771, a50 17,431), and the heartbeats' frame and tick times are equal to 0.01 ms in every stretch.
- **The main CPU's cost of the plumbing** (simulated, so the ME's half still runs on the main CPU):
  - JIT runs, the bucket loop's time on vs off: d20 +117 µs a frame, c20 +100, a50 +33. That is 15, 15 and 5 jobs a frame, so **~7 µs a job**.
  - Cache models, b30 to game time 600 (121b): drawing +2.8 million instructions, +56,000 data misses and +192,000 instruction misses over ~1,800 jobs, so **~1,560 instructions, ~31 data and ~107 instruction misses a job**. That is an upper bound for the PSP [U]: there, `ring_run` and the job's code run on the ME, not here.
- **Memory.** +8 KB code and +4 KB data: the heap is 12 KB smaller (5,756 KB against `w121`'s 5,768).
- **Multiplayer.** It compiles (121a, 121c). The batch's MP run fails at start on `w121` as on this branch: no room for the display list, a different data folder and bake. The netplay run against ChupathingyCE's server is in the report.

## Rig runs wanted

In the session's report. In short: `meringtest meslant meslantcheck` once (exactness on the real ME, the protocol's sharpest check); `keepsets="- | meslant"` with `repeat=2` on b30's cinematic and c20 (the speed); then the same with `mehalt`, and the tuning words.
