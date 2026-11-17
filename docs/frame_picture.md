# The frame picture: each box's milliseconds by cause (design v0)

Brief: the lab's `coord/briefs/frame_picture.md` (the lead, 2026-11-17). One table per stretch, on the PSP-1000: each box of the tick and the frame in ms a tick and a frame, at p50 and p99, split into **work**, **code-miss wait**, **data-miss wait** and **the rest of the memory wait** (the GE's toll and contention), with each box's miss counts by kind. Every box and lever is then ranked on the same table (REBUILD §3). This page is the design the lead accepts before anything is built; the build goes behind a switch, off by default.

## 1. What the hardware counts (the firmware read, 2026-11-17)

The main CPU's profiler is the block at `0xBC400000`: 21 words (`PspDebugProfilerRegs`): an enable word, then 32-bit counts of system cycles, CPU cycles, cycles stalled internally, **on memory**, on the FPU and other coprocessors, on the VFPU, asleep; bus accesses, uncached loads and stores, cached loads and stores, **instruction misses, data misses, data write-backs**, COP0, FPU and VFPU instructions, and the local bus [D: halo_profile.prx and the SDK's register layout; [M] the counters move as named, `hardware_facts.md` profiler bullets].

- **Code wait and data wait are not counted apart.** There is one memory-stall count; misses are counted by kind, the cycles they cost are not. So the split is by count times a price (§4), as the brief's fallback says.
- **No GE-bus counter** (0x48 counts; 0x4C and 0x50 read 0 with the GE busy [M], `hardware_facts.md`). The GE's toll stays an inference [U on M].
- **The counters are the whole CPU's, not a thread's.** Sony's thread manager can keep them per thread: in its "thread mode" it copies the 21 words into a profiled thread's own save area when the thread is switched out and writes them back when it's switched in (threadman, its switch path, and `sceKernelReferThreadProfiler` / `sceKernelReferGlobalProfiler`, which hand out the block's address in thread or global mode). But the mode is chosen once, when the thread manager starts, from the kernel's debug flags, which a retail PSP doesn't set: on ours the mode is off and nothing is saved per thread [D, the lab's `docs/research/fw661/frame_picture_read.md`]. Turning it on would mean writing the thread manager's own state, so the design doesn't. Other threads' work inside a box is handled in §5 instead.
- **Reading it** takes kernel mode (there's no user-mode clock either, `hardware_facts.md`). The port already has the path: `halo_profile.prx`'s system call `halo_profile_phase` snapshots the counters into the game's table at a phase change (4 phases today: other, tick, drawing, frame end).

## 2. What is read where

- **A mark at each box's entry and exit, never inside its loop** (the brief's rule 2). The game calls one function, `psp_box_mark(box)` on entry and `psp_box_mark(BOX_POP)` on exit; it makes the existing system call with the box's number.
- **Nesting.** Boxes call each other (biped motion calls the collision service). The module keeps a stack of open boxes (8 deep): a snapshot's counts since the last mark go to the box on top (exclusive), and each box's inclusive total is kept beside it. A box's row in the table is exclusive; "the rest" is everything outside any box.
- **Per tick and per frame, not per window.** At each tick's and frame's end the module folds each box's counts for that tick into **histograms**, one per box and measure (cycles, memory stall, I-misses, D-misses, write-backs, uncached): 64 log-spaced buckets each. p50 and p99 come from them; the window means from the sums. 14 boxes × 6 measures × 64 × 4 bytes is ~21 KB, in the module's own memory, not the game's heap (85 KB free on b30).
- **Reported** at each heartbeat as `framepic:` lines (one a box: ms a tick p50/p99 and mean, the split, misses a tick by kind) and once at the stretch's end as the table.

The boxes (REBUILD §3.3, as the code stands today; each one's entry points are settled in the build against `cpu_cost_map.md` §1 and named on this page then): **tick:** collision service, biped motion, posing, AI's main-CPU share, vehicles and other objects, effects (update and particles), the tick's other work; **frame:** visibility's main-CPU side, draw list, GE lists and forms (the main CPU's part), lighting, audio's main-CPU share (`sound_render`), the frame end; and "the rest".

## 3. What it costs, and how that's checked

- **A mark:** one system call and 21 uncached loads (~0.14 µs each in kernel mode [M], `hardware_facts.md`), plus the stack's bookkeeping: ~4-5 µs [U]. Two a box a tick, ~14 boxes: ~0.13 ms a tick, **~0.4% of a 33 ms frame** [U], under the brief's 1%.
- **What ledger1 taught** (its instruments cost b30's frame +5-7% [M]): the cost isn't only the instructions but the instrument's code and data in the caches. So the price isn't estimated, it's measured: every run is an A/B in one build, `keepsets="- | framepic"`, and the uninstrumented side is the control. Over 1% → fewer marks (boxes merged) before any figure is read (the brief's fallback).
- **The done-check:** each stretch's box rows plus "the rest" sum to within 5% of the uninstrumented side's frame on the same stretch.

## 4. Work, code wait, data wait and the rest

From one box's counts over a tick:

- **memory wait** = the memory-stall count;
- **code wait** = I-misses × **P_I**; **data wait** = D-misses × **P_D** + write-backs × **P_WB** + uncached accesses × **P_U**;
- **the rest** = memory wait − code wait − data wait (the GE's toll, contention with the ME, DMA, and pricing error) [U on M];
- **work** = cycles − memory wait (internal, FPU and VFPU stalls stay in work and are reported beside it).

**The prices are measured on the same PSP the same day,** by halo_box's counted pass (its `counters` mode reads the same counters around each entity): a kernel with only code misses, one with only data misses (clean and dirty victims), one with uncached accesses, each with the GE idle and with the GE reading main RAM (bench18's list). The GE-idle prices are the ones applied; the GE-reading prices bound how much of "the rest" the GE can explain. This is also what settles `hardware_facts.md` line 49's CONFLICT (the miss ratio by count against by cost).

## 5. Other threads

The counters see the whole CPU, so the sound thread's work, or any thread that runs while a box is open, lands in that box. The module already reads the thread-switch count; each mark reads it too, and a box's tick sample that had a switch inside it is counted apart (its own histogram), so p50 and p99 are given for switch-free samples and for all. The ME isn't in these rows (its per-step read exists, `hardware_facts.md` line 249); each window's line names what the ME ran, so the main CPU's table isn't read as the whole frame.

## 6. The runs

- **Stretches** (legendary, the design case): b30_leg2's flight and landing (window 3), b40's drive (16,600-19,400), c20's opening; and **the firing stretch**, chosen by the worker's emulator search for the densest sustained firing in a quiet area (or-job 44030538311, the master reads it).
- **Each run** `keepsets="- | framepic"`, one PSP-1000 job a stretch, under 90 minutes. The firing stretch twice more: with the ME on and with it off (keep word `nomeaudio`, which keeps the ME stopped), so the firing's own cost is told apart from the ME's work falling back to the main CPU (Sheepdog01's AR stretch, `ar_firing_me_off_b30_1000`).
- **Rows:** `frame_picture_<stretch>_1000` in the lab's measurements; the lead puts the table into REBUILD §3 and the cost map §1.

## 7. Build

- **Switch:** `PSP_FRAMEPIC=1` builds the marks in (their calls sit in hot entry points, so a default build leaves them out); keep word `framepic` turns them on at launch. The module's side is in `halo_profile.prx` and costs nothing unless called.
- **The prices' bench:** halo_box's counted pass, with the kernels above added as boxes.
- **Order** (the brief): after the partition work, ahead of VRAMCODE's three-way build.

## Open

- Each box's entry and exit points (§2), settled in the build.
- Whether 21 uncached loads a mark can be cut to the 6 the split needs (cycles, memory stall, I- and D-misses, write-backs, uncached): a smaller mark, if the profiler's words can be read singly without disturbing the rest.
