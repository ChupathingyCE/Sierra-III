# The frame picture: each box's milliseconds by cause (design v1)

Brief: the lab's `coord/briefs/frame_picture.md` (the lead, 2026-11-17). One table per stretch, on the PSP-1000: each box of the tick and the frame in ms a tick and a frame, at p50 and p99, split into **work**, **code-miss wait**, **data-miss wait**, the memory wait the prices don't explain, **asleep**, and **waits for the GE or ME**, with each box's miss counts by kind. Every box and lever is then ranked on the same table (REBUILD §3). The build goes behind a switch, off by default.

v1 answers the lead's acceptance of v0 (three conditions, 01:1x) and the eight additions from DeepSeek's cold read (or-job 44032878476, its two code claims checked by the lead, 01:0x). What changed from v0 is listed at the end.

## 1. What the hardware counts (the firmware read, 2026-11-17)

The main CPU's profiler is the block at `0xBC400000` (`PspDebugProfilerRegs`): an enable word, then **19 counters**. They are 32-bit counts of system cycles, CPU cycles, cycles stalled internally, **on memory**, on the FPU and other coprocessors, on the VFPU, and **asleep**. Then bus accesses, uncached loads and stores, cached loads and stores, **instruction misses, data misses, data write-backs**, COP0, FPU and VFPU instructions, and the local bus. [D: halo_profile.prx reads the 19 after the enable word, `COUNTERS` in `psp/profile/main.c`; [M] the counters move as named, `hardware_facts.md` profiler bullets.]

- **Code wait and data wait are not counted apart.** There is one memory-stall count. Misses are counted by kind, but the cycles they cost are not. So the split is by count times a price (§4), as the brief's fallback says.
- **No GE-bus counter.** 0x48 counts, while 0x4C and 0x50 read 0 with the GE busy [M], `hardware_facts.md`. The GE's toll stays an inference [U on M].
- **The counters are the whole CPU's, not a thread's.** Sony's thread manager can keep them per thread, but only in a mode chosen at its start from the kernel's debug flags, which a retail PSP doesn't set [D, the lab's `docs/research/fw661/frame_picture_read.md`]. Other threads' work inside a box is handled in §5.
- **Reading it** takes kernel mode; there is no user-mode clock either (`hardware_facts.md`). `halo_profile.prx` is the port's kernel module.

## 2. Marks

- **Their own entry point.** v0 reused `halo_profile_phase`, but that call masks its argument into the four phases (`next & (PHASES - 1)`), so a box number would land in a phase. It also walks the whole thread list at every change when `hwthreads` is on (`threads_account`, up to 96 system calls). The box marks get a system call of their own, `halo_profile_box(box)`. The four phases stay as they are, read as the port reads them now, and the box stack sits beside them.
- **What a mark reads:** the counters the split needs, and the thread-switch count as one value. The switch count is the kernel's own word, read once, not `threads_account`'s walk, and not a refer call when a word read does it; the build names which word. Whether the profiler's words can be read singly (6 of the 19: CPU cycles, memory stall, asleep, I-misses, D-misses, write-backs, plus the two uncached counts) is tested first (§7). If they can't, the mark reads all 19.
- **The keep word is read once at launch.** `framepic` sets a flag in the game, and a mark is one load and a branch when it's off.
- **At most ~60 marks a tick (the lead's condition 1).** A box is a **once-a-tick region**, not a function. Its marks sit where the tick enters and leaves that work as a whole, never at a per-call entry. The collision service is asked ~144 times a tick (`box_collision.md`) and biped motion 22.8, so neither is marked per call. Each is marked at its batch point where it has one. Where it has none (collision is asked from inside other boxes), it has no row of its own. Its cost stays inside its callers' rows, and beside them the page gives halo_box's standalone price per query times its counted queries, labelled as such.
  - **Before any PSP run:** an emulator count of marks a tick per box on every stretch (an exact count, which the emulator answers truly), held to ≤ ~60 a tick.
- **The marks' own cost taken out (condition 2).** Each run also takes a **null mark pair** (two marks back to back, nothing between) every tick. Its counts, times the number of marks a box made, are taken off that box and reported as their own row, "the marks".
- **Nesting.** Boxes call each other. The module keeps a stack of open boxes, 8 deep. The counts since the last mark go to the box on top (exclusive), and each box's inclusive total is kept beside it. A box's row is exclusive.

- **Where the marks are (the build, 2026-11-17):** the engine's own perf phases (`halo_perf_log.h`), compiled in under `PSP_FRAMEPIC` (`-DHALO_PERF_LOG`) and answered by `psp/src/psp_framepic.c`. Upstream already brackets each once-a-tick and once-a-frame region with them, so the box marks need no anchors of their own. The phases entered once a call or an object (collision, physics, node matrices, each object type's update, damage, functions, lights, activation, garbage) map to no box and cost one test each; the box that calls them keeps their cost.

## 3. Where the tables live, and what's reported

- **The histograms are in game memory.** The module refuses tables outside user RAM, and its own memory can't hold them. They sit in one block the game takes at launch from partition 2, outside the heap (`halo framepic`, ~30 KB). **It is taken on both sides of the A/B** (the lead's acceptance, 01:4x): whenever a `PSP_FRAMEPIC` build runs the profiler, with `framepic` or not, so the two sides' memory is the same. There are 21 boxes × 9 measures (8 counters, and cycles again for switch-free samples) × 32 buckets (two a doubling, from 256 cycles to ~50 ms) × 4 bytes, plus each box's sums. The block's address is handed to the module once, as the table is today.
- **Per tick and per frame.** At each tick's and frame's end the module folds each box's counts into its histograms (cycles, memory stall, asleep, I-misses, D-misses, write-backs). p50 and p99 come from them, and the means from the sums.
- **Reported** at each heartbeat as `framepic:` lines (one a box), and once at the stretch's end as the table.

The boxes, as built (the engine's perf phases; each row is exclusive of the boxes inside it):
- **tick** (folded once a tick): the tick's other work, units (`units_update`; the players' updates before and after the game are the tick's other work, 4 marks a tick less: framepic1c), AI, effects, the game engine, objects (`objects_update`, with posing, biped motion and the other objects' updates inside it);
- **frame** (folded once a frame): the netcode, the render's other work (with its small parts: begin, setup, lens flares, the screen effect, end), its parts (world, objects, transparent, effects, HUD), sound (`game_sound_update` and `sound_render`), present, the vsync wait, the render's occlusion and texture waits. The small parts and the network (which the campaign never runs) are boxes only under `framepicall`: framepic1 counted 68-74 marks a frame with them, over the ~60 bound [E, exact count];
- "outside any box", and "the marks", the null pair once a tick.

`game.c`'s perf statements are stripped before its fixups are matched (`PERF_STRIP`), so under `PSP_FRAMEPIC` the tick's boxes are put back last, around the calls as the other fixups leave them (framepic1b's marks by box had none of them). v0's finer tick boxes (biped motion, posing, vehicles) are entered once an object, inside `objects_update`, so they would break the ~60 bound. Like collision, each is a priced column inside objects' row (halo_box's price per entity × its counted entities), not a row. Visibility, the draw list, lighting and the GE's forms run inside the render's parts and are named by them.

## 4. The split

From one box's counts over a tick, with the prices from §6:

| Column | Is |
|---|---|
| **asleep** | the asleep count: its own column (3-16% of the clock on the stretches read so far), never in work or memory |
| **GE/ME wait** | cycles inside the marked waits for the GE or the ME (the frame end's finish, drawing's waits on a list, a box's wait for an ME answer). These waits are marked as sub-regions, so they aren't memory stalls and can't land in the residual |
| **code wait** | I-misses × P_I |
| **data wait** | D-misses × P_D + uncached loads × P_UL |
| **unexplained memory wait** ("the price residual") | the memory-stall count − code wait − data wait: the GE's toll, contention with the ME, DMA, and pricing error [U on M] |
| **work** | cycles − memory stall − asleep − GE/ME wait (internal, FPU and VFPU stalls stay in work, reported beside it) |

- **Write-backs are priced once.** A write-back's cost is inside the miss that evicts the dirty line. So P_D is the **dirty-mixed** price (measured with the box's own clean/dirty mix of victims, read from its write-back count against its D-misses), and there is no separate P_WB term. The write-back count is reported beside the split, not priced.
- **Uncached stores** (the GE's command stream) are buffered and mostly don't stall. They aren't priced; any stall they cause stays in the residual. They are reported as a count.
- **A range, not one number.** Each price has a low and a high bound (§6). The table gives code wait, data wait and the residual as a range over those bounds, never as a point.
- **"Outside any box"** is its own row: the time no box had open. It is not the price residual, which is a column of every row.

## 5. Other threads

The counters see the whole CPU, so the sound thread, or any thread that runs while a box is open, lands in that box. Each mark reads the switch count (§2). A box's tick sample with a switch inside it goes to a second histogram, so p50 and p99 are given for switch-free samples and for all. The ME isn't in these rows (its per-step read exists, `hardware_facts.md` line 249). Each window's line names what the ME ran, so the main CPU's table isn't read as the whole frame.

## 6. The prices

halo_box's counted pass (its `counters` mode) is **re-run on the PSP-1000 the same day as the frame-picture runs**. The old prices are the PSP-3000's. It runs:
- a kernel with only code misses (P_I);
- a data-miss kernel at clean, half-dirty and all-dirty victims (P_D's bounds; the box's own mix picks its point);
- an uncached-load kernel (P_UL);
- each with the GE idle and with the GE reading main RAM (bench18's list).

The GE-idle prices are the ones applied, and their spread (low to high) is the range in §4. The GE-reading prices bound how much of the residual the GE can explain.

**This does not settle `hardware_facts.md` line 49's CONFLICT** (the miss ratio by count against by cost). The price uncertainty there, 19-33 cycles over ~87k misses (5-8.7 ms), is larger than the 2.9 ms gap it would have to settle. The CONFLICT stays, and this run's range is added beside it.

## 7. The runs, and the done-check

- **First, an emulator run:** marks a tick per box on every stretch (§2's bound); the build doesn't go to a PSP until each is ≤ ~60.
- **Then the single-word read test**, on the PSP: read 6 profiler words singly against all 19 in one block, 10,000 times, on a bench. Are the six's deltas the same, and what does each read cost? This decides the mark's size.
- **Stretches** (legendary): b30_leg2's flight and landing (window 3); b40's drive (16,600-19,400); c20's opening; and **the firing stretch, c40_leg2, the window ending at tick 8,768** (~292 s; fallback a30_leg1 at tick 19,456; the lead 00:5x, chosen from the pads' own PSP logs).
- **Each run:** `keepsets="hwprofile | hwprofile framepic"`, one PSP-1000 job a stretch, under 90 minutes. **Both sides load the profiler module (the control, addition 6),** so the A/B isolates the marks only.
- **The firing stretch twice more:** with the ME on, and with it off. The ME-off side has to stop **all** of the ME's jobs (AI, visibility, the ring), not only audio (condition 3). `nomeaudio` alone does not stop it: the ME still starts under it for the ring's clients and the point jobs. It stays off only with `nomeaudio nomepoints` and none of `meslant`, `meringtest`, `meraysperc` (`psp_me.c`, the start gate) [D]. So the ME-off side is `nomeaudio nomepoints`, and its log is read for "ME: starting" (it must be absent) and for each job's off line. If one doesn't, the page names the keep word that holds it off, as the 055 run had it.
- **The done-check:**
  - (a) the instrumented side's frame against the uninstrumented side's, on the same stretch: that is the instruments' cost, and it must be ≤ 1%. Over 1%, boxes are merged before any figure is read;
  - (b) the identity: every row's columns, summed over the rows and "outside any box", equal the module's own whole-frame counters over the same window, for every column. That catches bookkeeping errors, not prices;
  - (c) p50 and p99 are never summed; only means are.
- **Rows:** `frame_picture_<stretch>_1000` in the lab's measurements. The lead puts the table into REBUILD §3 and the cost map §1.

## 8. Build

- **Switch:** `PSP_FRAMEPIC=1` builds the marks in (their calls sit in hot entry points, so a default build leaves them out). Keep word `framepic` turns them on at launch. The module's side is in `halo_profile.prx`.
- **The prices' bench:** halo_box's counted pass, with §6's kernels added as boxes.
- **Order** (the lead): after sndpart over the whole partition, ahead of VRAMCODE's three-way build.

## What changed from v0

- Marks: their own system call, not `halo_profile_phase` (its four-phase mask and thread walk). The switch count is read as one value. The keep word is read once. Boxes are once-a-tick regions, ≤ ~60 marks a tick, checked in the emulator first. Collision is priced from halo_box, not marked per call. A null mark pair is subtracted.
- Split: P_WB is dropped (the dirty-mixed P_D carries it). Uncached stores are not priced. Every split is a range over the price bounds. Asleep and GE/ME waits are their own columns. "Outside any box" and "the price residual" are named apart.
- The CONFLICT on `hardware_facts.md` line 49 is no longer claimed as settled.
- Done-check: v0's "within 5%" is replaced by the ≤ 1% instrument cost, the column identity, and no summed percentiles. The control loads `hwprofile` on both sides.
- Prices are re-run on the PSP-1000 the same day.
- Small fixes: 19 counters, not 21; the histograms live in a partition-2 block taken only with `framepic`; the stall columns sit beside work.

## Open

- The switch count's word (§2): which kernel word holds `threadSwitchCount`, so a mark reads it without a refer call. Until then the mark makes the refer call (`sceKernelReferSystemStatus`), and `framepicnosw` leaves it out; the A/B and the null pair price it.
- The GE/ME wait column (§4): the render's occlusion and texture waits and the vsync wait are rows of their own in this build, not a column of the box they sit in. The CPU's waits for the GE's finish and for an ME answer inside the port's own code (`psp_ge.c`, `psp_me.c`) aren't marked yet.
