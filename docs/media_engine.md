# The Media Engine in the game (builds 86–87)

2026-10-06. The PSP's second CPU (the ME) runs our code through mcidclan's me-core-mapper (MIT; `LICENSE-mecore.md` ships beside the EBOOT). Research and the first hardware tests are in `audio.md` §6. This doc covers the game's use of it. Labels: [M] measured on hardware, [S] emulator (which runs no ME code: every job is *simulated* on the main CPU), [E] estimated.

## How a job runs (psp_me.c)

1. **Start** (at launch, unless `nomeaudio`):
   - `meLibDefaultInit` writes `kcall.prx` to the current directory, so the game first `chdir`s to `ms0:/halo`.
   - The kernel part (`kinit`) replaces the firmware's ME sysevent handler.
   - The ME must report alive within 2 ms, then 20 ms.
   - Its cycle counter (CP0 $9) is calibrated against the main CPU's µs clock.
2. **Slots:** one per client thread (audio; render), each a few **uncached** words: request number, done number, job type, 12 argument words, 8 result words, cycle count. Uncached means both CPUs see the same word at once, so no cache flush is needed for the handshake itself.
3. **Hand-over:**
   - The main CPU writes back its data cache, so RAM holds what the ME will read, then bumps the request number.
   - The ME, polling, drops its own cache, runs the job, writes its cache back, and sets the done number.
   - The main CPU then invalidates the output lines.
   - **Outputs are whole 64-byte lines** (`memalign(64, …)`). A written-back line carries all 64 bytes, so a shared line would overwrite a neighbour's newer data.
4. **Idle:** the ME reads the uncached words once every ~25 µs (2,000 `nop`s in between), so an idle ME puts almost nothing on the bus.
5. **Stop** before HOME, quit, or sleep: set the stop flag, wait ≤100 ms for the acknowledgement, else hold the ME in reset through the kernel. The library's sleep handler holds it in reset too. After a stop every job runs on the main CPU. HOME flushes the log before and after the stop.
6. **What job code may do:** integer and FPU work only. The ME has no VFPU, and job code makes no system calls, no allocation and no I/O. `mewalk.py` checks the reachable code from the job entry points (0 flagged in b87). The compiler is told not to turn loops into `memset`/`memcpy` calls (`no-tree-loop-distribute-patterns`).
   - **Pure library code is allowed** (settled 2026-10-30, hardware; sierra-build 41034892327, `mewalk.py --list` from `meLibOnProcess`). That means libm's float functions and newlib's `mem*`/`str*` functions: code like ours, with no system call and no state of its own. The ME's jobs run it today [D]:
     - the GE replay: `sinf`/`cosf` (widget sprites), `tanf` (model shapes), `acosf` (TAA), `logf` (level transparents), `floorf`, `truncf` (`sceGuTexLevelMode`), `memcpy`/`memset`/`memcmp`;
     - the pose job: `memmove`;
     - the perception rays' node-matrix settles: `fmod` and `floor`, both `double`, so slow there.

     The same code on the same FPU gives the main CPU's bits.
   - **Library code that isn't pure stays out:** `malloc`/`free`, the `printf` family and the log, `qsort`'s comparator calls when they reach any of these, and anything that takes a lock. That walk still reaches some through rare paths, each a hazard to close at its site:
     - `malloc` from `psp_tex_bind` in `psp_ge_screen_quad`;
     - `free` from `overlay_on_a_wall`;
     - `vsnprintf` and the log from `ge_log`;
     - `qsort` from `ge_reads_report`;
     - `psp_keep`'s string search from `psp_ge_widget_sprite` (its first call).

     A job that reaches one at run time is broken, not merely slow.

Once taken, the ME stays the game's until the PSP reboots, so XMB music and video are off until then. **Sleep/wake with the ME running is untested** (a separate pass, by Sheepdog01's decision).

## Job 1: the sound mixer (b86; default since b87)

The main CPU still prepares each voice per 512-frame block: step, gains, and the list of live voices (`prepare_voice`). The ME runs `psp_dsound_mix_list`, integer-only, over that list into the output buffer. The audio thread sleeps while it waits, so the game thread gets the CPU.

**Hardware, b86 run 2** [M]:
- Firmware table 3 (the PSP-1000's) was recognised, and the ME idled at 551 polls per 20 ms.
- **~2,500 blocks, 0 failed, clean HOME exit.**
- The main CPU's cost per block: **~110 µs with the ME vs ~290–500 µs mixing itself** at ~1 voice.
- **The ME took ~615 µs a block** (the audio thread's wait), about twice the main CPU's own mix time. b87 logs the ME's *own* cycles per job to split mixing from polling and cache operations.
- Frames were unchanged (61–63 ms either way). The mixer was never late, and the game's waits for the mixer's lock were ≤0.9 ms.
- **Sheepdog01 heard regular breaks with both mixers**, so the breaks aren't the mixer. b87 counts *dry gaps*: a voice that had nothing queued for some frames and was fed later. That points at the game's streaming of sound data, not the output.

Debug menu: "Audio mixer (needs meaudio)": main CPU / Media Engine. Launch menu: `nomeaudio` opts out.

## Profiling (b87)

**b87 on hardware: the ME's cycle counter (CP0 $9) stood still** ("runs at 0.0 a microsecond"), so every ME-side time read 0. The main CPU's side (hand-over, wait) still works: ~100 µs hand-over and ~350–600 µs wait per audio block. b88 clears Cause bit 27 (DC, "disable count") on the ME at start and logs Cause before and after. me-core's own custom-core timer uses $9/$11 on the ME, so the counter exists [D].

Every heartbeat logs a line like:

`ME: running, 3.1% busy over 1.0 s; audio: 43 jobs, the ME's own time X us a job (the job's code J, cache hand-overs the rest; worst Y), Z% of the ME; the main CPU's hand-over A us a job, waiting B us a job (worst C); N failed; render: …`

- **The ME's own time** comes from its cycle counter. It is split into the job's own code and the two whole-cache write-back/invalidates around it, which answers why b86's mix took ~615 µs on the ME. **Hand-over** and **waiting** are the main CPU's side.
- The overlay shows **ME %** next to FPS.
- benchrun has a "ME audio off" switch, so each view gets a frame-time comparison.
- In the emulator the line says SIMULATED, and its numbers are the main CPU's.

## Jobs 2 and 3: render maths, measure only (b87, `meclassify`, off)

Two jobs shaped like the renderer's costliest CPU steps:
- **Classify** (job 2): each vertex of a level draw against the near plane and the guard band. Single-precision maths, reading the vertices in order.
- **Slant buckets** (job 3): `angle_sort`'s per-triangle bucket, which fetches three vertices per triangle from wherever they lie. It is 3.8 ms a frame on hardware (b86). The main CPU's loop and the ME job call the **same function** (`angle_bucket_one`), compiled without fused multiply-adds, so they must agree bit for bit.

With `meclassify`, each level draw runs both jobs on the ME after the main CPU's own work. Then the **same function runs again on the main CPU**, with the data first flushed out of its cache, as the ME's is at the start of a job.
- The log line `GE: ME measure (meclassify): …` gives, per job, the ME's code time per vertex or triangle against the main CPU's, both starting cold, and the answers that differ.
- **That is the like-for-like number** that decides any offload. If the ME is about as fast as the main CPU, splitting a draw between them pays; if it is twice as slow (as b86's audio hinted), the ME can take about a third of the work. (Answered below, b90 run 3: as fast as the main CPU [M].)
- **It costs frame time while on**, because the main CPU waits and repeats the work. It is an instrument, not a speed-up.
- [S: both jobs 0 differing answers over ~900,000 vertices and ~950,000 triangles. ]

**Hardware, b87 run 4** (Blood Gulch, blue base, ~60 s) [M]:
- **0 differing answers** over ~2.7 million vertices and ~2.6 million triangles, 0 failed jobs, about 7 classify and 14 bucket jobs a frame alongside the audio jobs. The hand-over (write-backs, invalidates, whole 64-byte output lines) is sound with render data, and the ME's float maths matches the main CPU's bit for bit.
- The main CPU, cache cold: **0.31–0.32 µs a vertex** (classify) and **1.07–1.15 µs a triangle** (bucket).
- **The ME's own time is unknown:** b87's counter stood still. The round trip was ~520 µs a job, 2.4–2.7× the main CPU's time for the same work. That is an upper bound, not the ME's speed: the waiting thread sleeps 50 µs, then looks every 100 µs (each sleep can overrun, and other threads run meanwhile), and the ME also serves the audio slot. b89's counter fix gives the real number (b90 run with `meclassify`).
- The instrument cost ~16 ms a frame while on (63 ms frames).
- **The bus:** the ME reads its inputs from main RAM, the same bus the CPU and the GE share (memory_traffic.md). Its speed alone doesn't settle an offload; the frame time with it on does.

## Where the ME could pay next (ranked from b86 hardware numbers)

The renderer's own CPU work at the blue-base spot is 17.5–19 ms of a 62 ms frame [M, b86 run 2]. Its level part, per frame:

| Step | µs a frame | ME fit |
|---|---:|---|
| Angle-bucket sort (1/cos per triangle → 7 buckets) | **3,787** | FPU per triangle, reads indices and positions, writes a bucket per triangle. Good fit |
| Classify corners | **2,053** | The `meclassify` job. Good fit |
| Passes (binds, list writing; includes the sort above) | 6,308 | List writing must stay on the main CPU |
| Stream + index range, cut up | 546 + 264 | Small |

Together the sort and the classification are **~5.8 ms a frame (~9% of the frame)**. Two ways to use the ME on them:

1. **Split each draw.** The ME takes half the vertices or triangles while the main CPU does the other half at the same time. This can save up to about half of 5.8 ms. The cost is two hand-overs per draw, at 19 level draws a frame; the hand-over must use range write-backs (the view block only), not a whole-cache write-back, or it eats the gain [E].
2. **Run ahead.** At the start of the level's drawing, the ME classifies and buckets the whole visible set while the main CPU does something else. This saves more [E] but needs the visible set before the draws arrive (the game's visibility pass knows it). It is a bigger restructure.

Lower on the list:
- Skinning near the eye (rare, and skincut handles it).
- Collision rays for sound obstruction and AI (the larger prize named in `audio.md` §6.4, but the collision code would have to run self-contained on the ME: a big port).
- Particles.

**Decision point (settled, next section):** `meclassify` on hardware gives the ME's speed on exactly these two jobs. If the ME's own time per triangle is near the main CPU's, build option 1 first, behind a switch, and measure it with benchrun.

## The ME's speed, measured (b90 run 3) [M]

b89's counter fix works on hardware: the cycle counter runs at 331.6 a microsecond (the ME clocks at 333 MHz, like the main CPU). Same jobs, same code, both starting with a cold cache, over ~5 million vertices and ~4.7 million triangles:

| Job | ME | Main CPU | Answers differing |
|---|---:|---:|---:|
| Classify (per vertex) | 288–311 ns | 292–352 ns | 0 |
| Slant bucket (per triangle) | 1,004–1,021 ns | 1,061–1,121 ns | 0 |

- **The ME is as fast as the main CPU on this work, about 5% faster.** b86's "twice as slow" (audio) was the hand-over, not the core.
- **The hand-over is the cost:** a job's round trip was ~520 µs against ~200 µs of code, so ~320 µs a job goes to the waiting thread's sleeps (50 µs, then every 100 µs, each able to overrun) and the whole-cache write-backs. 6,300 small jobs a 300-frame window can't pay that.
- **So an offload must be few, large, asynchronous jobs:** hand the ME a whole frame's worth (or a tick's) early, do other work, collect late; spin briefly rather than sleep when the answer is due; write back ranges, not the whole cache.
- With equal speed, the ME is a full second core. Where it goes first (renderer or tick) is the open decision; see campaign_window_fit.md and frame_rate_targets.md.

## The job ring (ME phase 1, `meslant`; `docs/review_b121_me.md`)

The answer to "few, large, asynchronous jobs" for work that comes in pieces: a ring of 8 jobs beside the slots, with a hand-over cheap enough for jobs of ~50–500 µs.

- **One writer per line.** `pushed` (main CPU) and `popped` (ME) are each on an uncached line of their own. Requests are written by the main CPU, then written back before the push. Results and outputs are written by the ME, then written back before `popped` moves. Each side drops its stale copies of the other's lines before reading them (`cache 0x1B` on the lines; the whole cache only past 16 KB).
- **Rules checked at the push:** the output is whole 64-byte lines, and everything is in main RAM (not VRAM).
- **Checked after each job:** a stack sentinel.
- **A 10 ms watchdog:** past it, the main CPU does the job itself and the ring stops for the session.
- **Pick-up:** the ring is served right after the audio slot. After a ring job the ME looks every ~1.5 µs for ~2 ms (the renderer pushes in bursts), then idles as before.
- **First client:** the slant sort. The ME takes the back part of a draw's bucket loop while the main CPU does the front (`slant_hand_over` / `slant_collect`, `meslantcheck`).
- **Its own check:** `meringtest`, 8 jobs a frame over freshly rewritten buffers.
- **In the emulator** every ring job runs inside the push, so the emulator proves the rules (strict interpreter, the ME write trace, the ME-strict check) and the exactness, but not the speed.
