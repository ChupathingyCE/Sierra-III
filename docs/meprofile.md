# meprofile: the Media Engine's own miss counters, by job and by part

A recon instrument (`PSP_RECON=1`, keep words `meprofile` and `meparts`). It answers one question on the PSP: **where the ME's remaining misses are, code or data, and in which job and which step.** Labels: **[M]** PSP, **[E]** emulator or estimate, **[D]** code or docs, **[U]** inference.

## Why

- The emulator's cache model can count the ME's misses, but only the PSP says what they cost, and the emulator runs the ME's jobs simulated on the main CPU, where the ME's profiler isn't reachable.
- The earlier meprofile (R's milestone branch, 2026-11-02) and its per-step counters (meunnamed, 2026-11-10) found the ME's render job 29-32% code misses on b30's flight [M, `hardware_facts.md`]. That led to MEXEC (`mexec.md`). With the code room closed (the lead, 2026-11-20), the next choice rests on what's left: code misses still in main RAM, the copy's own misses (cheaper from eDRAM), or data.
- The tock had no ME miss counter (`tgme.md`). This brings it back onto today's code, widened from the render job to every job the ME runs.

## What it reads [D]

The PSP's hardware profiler block at `0xBC400000` (`PspDebugProfilerRegs`: an enable word, then 19 counters; `frame_picture.md`). **The ME has a block of its own there**, off until the ME turns it on, and its CPU cycles track the ME's Count to 0.001% [M, the lab's `me_hw_counters`]. So its I-misses are the ME's code misses and its D-misses the ME's data misses. The main CPU can't read the ME's block: it sees its own at the same address, and only in kernel mode.

### `meprofile`: by job (`psp_me.c`)

- **Where:** `me_serve` (every slot job: the render job, audio, the AI's think, music, the probes) and `ring_run` (every ring job: batches, the sweep, visibility, poses, points, perception rays). Each reads the block at its start and end: the ME's Count, memory-wait cycles, I-misses, D-misses, write-backs, uncached and cached loads and stores, the local bus and bus access.
- **Each job's counts are its own.** A job served inside another (audio mixed at the render job's looks, the ring drained in the AI job's yields) is taken off its host's, up to four deep. So the bins add up to the ME's time in jobs, with nothing counted twice. A slot job's span includes its cache hand-overs (their write-backs are the job's).
- **Bins:** slot jobs by type, ring jobs by type, summed on the ME into a 64-byte line per type. Each line is written back as it's added to. The main CPU reads them uncached each 300 frames and reports the difference from its last copy, so nothing is ever reset under the ME.
- **Lines** (each 300 frames, from `psp_ge.c`'s report beside `mekinds`):
  - `ME: meprofile, N frames, the ME's jobs a frame: ...`: the totals, the memory wait a miss, and what the enable word read before the first job (it should say off: the ME's own block);
  - `ME: meprofile, slot|ring <job>: ...`: one line a job type, heaviest first. Jobs a frame, ms a frame, the memory-wait share, I-misses, D-misses and write-backs a frame, memory wait a miss, uncached and cached loads and stores, local bus, bus access cycles.

### `meparts` (+ `meprofile`): by step of the render job (`psp_ge.c`)

- **`meparts`** times the GE layer's parts on the ME too, on its cycle counter (`ge_clock` reads nothing there: no system call on the ME), with the list words each writes. It adds five steps inside `model_geometry`'s skinned batches (11 bone matrices, 12 near tests and lists, 13 draw calls, 14 since the last mark to the batch, 15 bone sub-list take), so steps 7 and 8 keep what's left of theirs. Line: `GE: parts on the ME (meparts), us a frame (calls a frame; list words a frame)`.
- **With `meprofile` too,** each mark also reads the ME's I-misses, D-misses, write-backs and memory-wait cycles. Line: `GE: parts on the ME, counters a frame (meprofile; part: I-misses/D-misses/write-backs/memory-wait cycles)`, by part number in the order of the names, and `GE: parts on the ME, bus and copy a frame (meprofile; part: bus-access cycles/marks from MEXEC's copy/marks)` beside it. **The render job's bin less the parts' sums is the time in no part,** with its own split.
- Only inside the ME's own render job (`part_counters_on`, set in `psp_ge_me_replay`), and only on the ME (`psp_on_me()`): the block is a kernel address the main CPU can't read.

## Code against data, and MEXEC [U on M]

- **The counts split directly:** I-misses are code, D-misses and write-backs data.
- **The cost needs one more step.** The block gives one memory-wait figure for both kinds of miss. A line costs the ME ~25 cycles from eDRAM and ~70-77 from main RAM under load [M, `mexec_code_fetch_loaded`].
  - Where a job's code runs from MEXEC's copy (`PSP_MEXEC=rbtsw`: the replay chain, `run_job`, the think, the sweep's record function), its I-misses fill from eDRAM.
  - So a bin's memory wait a miss falls toward 25 as more of its code is copied, and stays near 70-77 where neither its code nor its data is in eDRAM.
  - Taking the data line's cost as known (main RAM's ~72-77, except the AI job's data held in eDRAM), a bin's implied code-line cost is (memory wait − data cost × D-misses) ÷ I-misses.
- **No sampled program counters.** The ME takes no profiling interrupt, so the bins name code by job and by step, not by address. That's also why MEXEC needs no mapping here: a step's mark sits in its own code, copy or original.
- **The cache model's ME side** gives the shares by function (`drawhot2`, `tgme2g` in the lab) [E counts]. This page's lines are the PSP's costs those shares are read against.

## Where a code miss fills from: eDRAM's copy or main RAM

**The block can't say directly** [D]. Its 19 counters (`PspDebugProfilerRegs`; psdevwiki's and PSPTEK's register lists, on the lab's `external`) count I-misses as one number: no counter splits a fill by its source. The two bus counters are the only candidates:
- **local bus cycles (0x50)** reads 0 on the ME in every job (meprofile1, 9 windows) [M], as it does on the main CPU (`hardware_facts.md`, the profiler bullets): dead on both;
- **bus access cycles (0x24)** had never been read on either CPU. If the ME's eDRAM is off the system bus, an eDRAM fill leaves it alone, and it counts only main RAM's traffic [U]. meprofile now reads it (`bus access ... cycles` on each job's line; first value on the parts' `bus and copy` line).

**What meprofile1 already says** [U on M] (sierra-hw 44930018547, PSP-3000, b30_leg2 to exitat150, the 9 windows' parts lines): fitting each part's memory wait as a·I-misses + b·D-misses + c·write-backs over 126 part-windows gives **a = 55 cycles a code miss** (53-57 in every window, R² 0.996), b = 65 a data miss, c = 32 more for a dirty victim (55-68 and 21-65 by window). The data figures sit where the PSP-1000's counter prices do (51 clean, 97 dirty; `hardware_facts.md`). A code miss that costs 55 is main RAM's price on a lightly loaded bus (54.3 [M], `mexec_code_fetch_psp1000`), not eDRAM's 25: **taken at face value, at most ~0-15% of the replay's code misses fill from the copy.** Each part's own implied code price (its memory wait less the fitted data prices, over its I-misses) is 43-67 cycles, part 15 at 86; none is near 25. That agrees with MEXEC buying only 2-5% of the ME's job (`mexec_r2_d20_flashlight`) and with the meroom pair. The fit is an inference: the counts come from the PSP, but the split of the memory wait comes from the regression.

**Two reads settle it, both in one keepsets pair** (the same build, `PSP_MEXEC=rbtswprof`, `mexec` taken out of the keep list, `keepsets="mexec | -"`):
1. **Copy on against copy off.** With the copy off every code miss fills from main RAM. If the copy-on side's code price (the fit above) matches the copy-off side's, the replay's code misses aren't filling from the copy. If it's lower, the eDRAM share is f = (a_off − a_on) / (a_off − 25), and the replay's I-misses × f are the copy's own.
2. **`bus and copy` on each side** (meprofile + meparts, psp_ge.c): for each part, the bus-access cycles, and how many of its marks ran from the copy (a `bal` reads the mark's own PC: eDRAM is 0x80000000 up on the ME). Read it as:
   - **marks from the copy:** a part whose marks ran from the copy (on side A) has its own code there. If it still pays main RAM's price a miss, its misses are in what it calls outside the copy: calls through registers, import stubs and functions not on the list (`mexec: the ME check` in the build log names the counts);
   - **bus access:** live on the ME if it isn't 0. On the copy-off side, bus cycles ÷ (I + D + write-backs + uncached loads and stores) is main RAM's bus cost a transaction. On the copy-on side, the code misses from main RAM are (bus cycles − that cost × the data side's transactions) ÷ that cost, and the rest of the I-misses came from eDRAM. A part whose bus cycles fall on side A by its I-misses × that cost had its code in the copy.

The code: `meprof_read` takes the bus access counter as its eleventh word (the bin's line still 64 bytes); the parts' counters take it as their fifth, and `part_in_copy()` counts the marks that ran from eDRAM. The parts' `counters a frame` line reads as before; the new figures are on a line of their own.

## Building and running

- **Build:** `PSP_RECON=1` with the tock's recipe, but with **`PSP_MEXEC=rbtswprof`** in place of rbtsw. Its list is rbtsw's functions in rbtsw's order in a 288 KB room (eDRAM 256-544 KB, inside the 512-800 KB left free since the code room closed). The marks and counter reads are inlined into the copied replay chain, and rbtsw fills its 256 KB to within ~750 bytes, so `tools/mexec.py` would refuse it.
- **Keep words:** `meprofile` (the job bins; with `meparts`, the steps' counters too), `meparts` (the steps on the ME), with `mexec` as the recipe has it. `mekinds` beside them gives the render job's time by record kind.
- **A default build is byte-identical:** everything is under `#ifdef PSP_RECON` (in a default build, `part_clock()` is `ge_clock()` and the marks are empty statements).
- **The emulator:** jobs run simulated on the main CPU, so `meprofile` logs nothing there, and `meparts`' ME line has calls and list words but no times. It proves the build plays the same with the words on (play and both seeds identical); every figure on this page is the PSP's.
- **The instrument's own cost:** ~11 uncached loads of the block at each job's ends and ~5 at each mark (plus a `bal` and two counts), plus one line written back a job. Its reads land in the host's or the next step's counts. Expect the job times a few percent over a bare build's; read the shares, and price levers in builds without it.

## History

- **The earlier meprofile** (R's milestone branch, 2026-11-02): the block read around the render job only, its 19 counters summed. It found the ME's own block, 46% memory wait, 74.4 cycles a miss, I-misses twice the D-misses on b30 [M, the lab's `me_hw_counters`].
- **meunnamed** (2026-11-10): the same counters at each meparts mark. It found code misses spread over every step of the part chain, 29-32% of the job [M, `me_unnamed_test1_*`].
- **This port** (2026-11-21): onto the tock (MEXEC_JOBS, the ring, the sweep). The counters are now taken around every job, each job's counts are its own, and the ME's figures are added to rather than reset by the main CPU.
