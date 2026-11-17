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

**The prices, measured on the PSP-1000 (2026-11-17, bench-hw 44055874064, 44056762824, 44056839732, 44056914214; the lab's rows framepic_price_*_1000) [M]:**

| Price | Cycles of memory stall, GE idle | With the GE reading main RAM 35% (timed) |
|---|---|---|
| P_I, a code miss | 56 | ~77 |
| P_D, a data miss with a clean victim | 51 | ~75 |
| P_D, a data miss with a dirty victim (load or store) | 97-98 | ~137 |
| P_UL, an uncached load | 39 | ~59 |

Each held within ~2% across the warm, emptied and crowded modes. A box's P_D sits between 51 and 98 by its write-backs over its D-misses.

**The mark's size, from the read test (§7) [M]:** a counter read alone gives the same counts as the block of 19 (D-misses identical, cycles within 0.25%), and a read's cost is mostly the system call (all 19 in one call 1.49 µs; one word a call 0.51 µs). So the mark stays as built: one call that reads its 8 words inside it. Each counter read is itself an uncached load, which the null pair takes out.

**This does not settle `hardware_facts.md`'s CONFLICT on the miss ratio** (the miss ratio by count against by cost). The price uncertainty there, 19-33 cycles over ~87k misses (5-8.7 ms), is larger than the 2.9 ms gap it would have to settle. The CONFLICT stays, and this run's range is added beside it.

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

## The first table: b30 (PSP-1000, 2026-11-17)

sierra-hw 44058220314: a7031cf9 with the shipped recipe, b30_leg2 to 130 s legendary, the whole run (2,233 frames, 3,899 ticks), two runs a side; the lab's row `frame_picture_b30_1000` [M]. Each box's mean a sample, in ms; the split uses the prices of §6, its data and unexplained columns as ranges over P_D's bounds. The two framepic runs agree within a few percent.

| Box | Per | Mean | Work | Code wait | Data wait | Unexplained |
|---|---|---:|---:|---:|---:|---:|
| objects | tick | 20.5 | 7.5 | 5.0 | 3.1-5.9 | 2.0-4.8 |
| the tick's other work | tick | 3.9 | 1.5 | 0.7 | 0.7-1.4 | 0.3-0.9 |
| AI (main CPU) | tick | 1.6 | 0.6 | 0.4 | 0.3-0.5 | 0.1-0.4 |
| effects | tick | 0.29 | 0.12 | 0.07 | 0.05-0.09 | 0.01-0.05 |
| render: objects | frame | 10.7 | 4.0 | 2.8 | 2.0-3.9 | 0-1.7 |
| render: world | frame | 9.9 | 5.4 | 1.4 | 2.1-3.6 | 0-1.1 |
| outside any box | frame | 5.5 | 2.6 | 1.2 | 1.0-1.9 | 0-0.8 |
| render: effects | frame | 2.1 | 1.5 | 0.2 | 0.3-0.6 | 0-0.1 |
| render: other | frame | 1.3 | 0.6 | 0.3 | 0.3-0.5 | 0-0.2 |
| render: HUD | frame | 1.2 | 0.6 | 0.3 | 0.1-0.2 | 0-0.2 |
| render: transparent | frame | 0.9 | 0.6 | 0.06 | 0.1-0.3 | 0 |

Units, the game engine, sound, the netcode, present and the vsync wait are each under 0.1 ms. Asleep is ~0 in every box. A mark costs ~2 µs (the null pair 4.0 µs).

**The instruments' cost (§7 a), off against framepic, both on hwprofile:** cinematic −0.4%, flight +0.3% (both in the noise), landing's drawing +2.0 ms, +7.4% (real; the frame +4.2%, in the noise), the beach's frame +5.5% (in the noise). So ≤ 1% holds on two of the four stretches. The marks themselves are ~0.15 ms a frame, which can't explain landing's +2 ms. The suspect is each mark's switch-count refer call, a kernel call leaving its footprint in the render's caches; `framepicnosw` leaves it out, and a three-way run decides. *(Superseded 2026-11-17 by the b30 three-way below: landing's gap isn't reproduced, and the switch count is no cost.)*

**Not yet read:** the identity check (§7 b), each column summed over the boxes against the module's own whole-frame counters, waits on those counters taken over the same window.

## The second table: c20 (PSP-1000, 2026-11-17)

sierra-hw 44062045115: the same build and recipe as b30's, c20_leg2 to 150 s legendary, the whole run (2,451 frames, 4,499 ticks: 1.84 ticks a frame), two runs a side; the lab's row `frame_picture_c20_1000` [M]. Columns as b30's. The two framepic runs agree within 1% in every box over 0.1 ms.

| Box | Per | Mean | Work | Code wait | Data wait | Unexplained |
|---|---|---:|---:|---:|---:|---:|
| objects | tick | 23.9 | 7.1 | 6.7 | 3.0-5.7 | 4.3-7.0 |
| the tick's other work | tick | 2.9 | 1.1 | 0.6 | 0.5-1.0 | 0.3-0.8 |
| AI (main CPU) | tick | 1.0 | 0.4 | 0.2 | 0.2-0.4 | 0.04-0.2 |
| effects | tick | 0.48 | 0.18 | 0.11 | 0.08-0.15 | 0.04-0.11 |
| render: world | frame | 8.4 | 4.4 | 1.2 | 1.9-3.5 | 0-1.0 |
| outside any box | frame | 6.1 | 2.9 | 1.4 | 1.0-1.8 | 0.1-0.9 |
| render: objects | frame | 5.5 | 2.2 | 1.4 | 1.1-2.1 | 0-0.8 |
| render: effects | frame | 2.0 | 1.2 | 0.3 | 0.3-0.5 | 0-0.2 |
| render: transparent | frame | 1.9 | 1.0 | 0.17 | 0.4-0.8 | 0-0.2 |
| render: HUD | frame | 1.7 | 0.9 | 0.4 | 0.15-0.3 | 0.1-0.2 |
| render: other | frame | 1.6 | 0.8 | 0.3 | 0.3-0.6 | 0-0.2 |

Units, the game engine, sound, the netcode, present and the vsync wait are each under 0.1 ms; asleep is ~0 everywhere.

**What it says:** the objects' update is ~85% of c20's tick (23.9 of ~28 ms), and over half of it is waiting: code 6.7 ms (a larger share than b30's 5.0 of 20.5), data 3.0-5.7, and 4.3-7.0 ms the prices don't explain. The unexplained part grows with the objects' load from b30 to c20. A likely cause [U] is that §6's prices were measured with the GE idle, and they rise ~1.4x at GE 35% (P_I 56 to ~77, P_D 51-98 to ~75-137). At those prices the unexplained objects part would mostly become code and data waiting. The identity check (§7 b) and a price under load are what settle it.

**The instruments' cost, off against framepic:** the first stretch's frame +1.0 ms, +1.3% (real); play's frame +0.5 ms, +0.8% (real), its drawing +1.1 ms, +5.2% (real). As on b30's landing, drawing carries the cost, which points at the per-mark switch-count call again. The c40 and b30 three-ways (`framepic framepicnosw`) decide it.

## The third table: c40, the ME on (PSP-1000, 2026-11-17)

sierra-hw 44065994276: the same build, c40's pad to its stretch end, one run a side, three ways (off, `framepic`, `framepic framepicnosw`); 7,975 frames and 8,759 ticks; the ME running on every side; the lab's row `frame_picture_c40me_1000` [M]. The framepic run's numbers (nosw's agree within 6%).

| Box | Per | Mean | Work | Code wait | Data wait | Unexplained |
|---|---|---:|---:|---:|---:|---:|
| objects | tick | 10.3 | 3.4 | 2.9 | 1.6-3.0 | 1.0-2.4 |
| AI (main CPU) | tick | 1.1 | 0.6 | 0.2 | 0.2-0.3 | 0.02-0.2 |
| the tick's other work | tick | 1.0 | 0.4 | 0.2 | 0.2-0.4 | 0.02-0.2 |
| effects | tick | 0.15 | 0.06 | 0.04 | 0.03-0.05 | 0.01-0.03 |
| render: world | frame | 5.9 | 3.2 | 0.7 | 1.0-1.8 | 0.2-1.0 |
| render: objects | frame | 5.4 | 2.5 | 1.1 | 0.9-1.7 | 0.1-0.9 |
| outside any box | frame | 2.4 | 1.0 | 0.5 | 0.4-0.6 | 0.2-0.5 |
| render: other | frame | 2.2 | 1.1 | 0.3 | 0.5-0.9 | 0-0.3 |
| render: transparent | frame | 0.86 | 0.5 | 0.1 | 0.1-0.3 | 0-0.08 |
| render: effects | frame | 0.53 | 0.35 | 0.06 | 0.08-0.14 | 0-0.05 |
| vsync wait | frame | 0.50 | 0.14 | 0.18 | 0.07-0.11 | 0.05-0.09 |
| render: HUD | frame | 0.39 | 0.18 | 0.11 | 0.04-0.08 | 0.02-0.06 |

**The switch count isn't landing's cost:** the null mark pair is 2.5 µs with the switch-count call and 1.9 µs without it (`framepicnosw`), so the call is ~0.3 µs a mark, ~20 µs a frame at 62 marks. That can't make b30 landing's +2 ms. **The instruments' cost here, one run a side, no verdict:** the opening holds (frame +0.0%, tick +2.0% / +1.6%, drawing +1.5% / +0.9%); the later stretch's gaps (frame +12% framepic, +20% nosw) run the wrong way for the switch count and are within what one run a side can't separate from play varying. The b30 three-way at two runs a side is what can.

## The fourth table: c40, the ME off (PSP-1000, 2026-11-17)

sierra-hw 44070031867: as the third, with `nomeaudio nomepoints` (no 'ME: starting' in any log); 7,976 frames and 8,759 ticks; the lab's row `frame_picture_c40nome_1000` [M]. The framepic run, beside the third table's ME-on figures.

| Box | Per | ME off | (ME on) | Work | Code wait | Data wait | Unexplained |
|---|---|---:|---:|---:|---:|---:|---:|
| render: world | frame | 27.5 | (5.9) | 18.0 | 3.8 | 3.0-5.6 | 0.1-2.7 |
| objects | tick | 8.9 | (10.3) | 3.7 | 2.7 | 1.4-2.7 | -0.1-1.2 |
| render: objects | frame | 4.5 | (5.4) | 2.3 | 0.9 | 0.8-1.6 | 0-0.5 |
| outside any box | frame | 3.8 | (2.4) | 1.8 | 0.9 | 0.7-1.3 | 0-0.4 |
| render: other | frame | 1.9 | (2.2) | 1.1 | 0.3 | 0.4-0.8 | 0-0.1 |
| AI (main CPU) | tick | 1.0 | (1.1) | 0.6 | 0.2 | 0.2-0.3 | 0-0.1 |
| the tick's other work | tick | 0.8 | (1.0) | 0.4 | 0.2 | 0.2-0.3 | 0-0.07 |

**What it says:** the ME's work takes ~21.6 ms a frame off the main CPU's render of the world (27.5 to 5.9), the whole of it work and waits the main CPU no longer does. With the ME on, the main CPU's own boxes run slower: the objects' update +1.4 ms a tick (+15%), its unexplained part up from -0.1-1.2 to 1.0-2.4 ms; render: objects +0.9 ms. [U] that is the ME contending for main RAM, which §6's prices, taken with the ME idle, don't hold. A price with the ME busy is what would turn it into a figure. **The marks' cost with the ME off, one run a side:** frame +1.8% framepic / +1.5% nosw on the opening, +1.2% / +0.0% later; the switch count is worth ~0.3 points. The ME-on run's later +12% / +20% doesn't appear with the ME off, so it was the ME's variance, not the marks.

## The instruments' cost, settled: b30 three-way (PSP-1000, 2026-11-17)

sierra-hw 44075240128: b30's opening bench, two runs a side, three ways (off, `framepic`, `framepic framepicnosw`); the lab's row `framepic_cost_b30_3way_1000` [M]. **The ≤ 1% done-check (§7 a) holds wherever play is the same on both sides:** cinematic frame −0.1%, flight frame −0.0%, drawing and tick within ±0.2%, all in the noise. Play parted at game time 2448, inside landing (a side's player over 5 units from off's), so landing and the beach compare different games and get no verdict. The two off runs alone differ by 12% on landing's frame (105.3 and 93.7 ms). **So the first table's 'landing +7.4%' isn't reproduced:** it was one pair on a stretch that varies that much, not the marks. `framepicnosw` buys nothing either: flight −1.9%, in the noise; cinematic +1.5%, real but the wrong way for a saving. With the null pairs' 0.3 µs a mark, that settles the switch count as no cost. **Decision:** the marks stay as built, the switch count kept, and the render's marks not merged. `framepicnosw` stays as a keep word, not the default.

## The fifth table: b40 (PSP-1000, 2026-11-17)

sierra-hw 44085373539: the same build, b40_leg1 legendary to 650 s (13,512 frames, 19,499 ticks), one framepic run (the cost is settled, above); the lab's row `frame_picture_b40_1000` [M].

| Box | Per | Mean | Work | Code wait | Data wait | Unexplained |
|---|---|---:|---:|---:|---:|---:|
| objects | tick | 15.1 | 5.0 | 4.0 | 2.1-4.1 | 2.0-3.9 |
| the tick's other work | tick | 2.3 | 1.0 | 0.4 | 0.5-0.9 | 0.05-0.5 |
| AI (main CPU) | tick | 1.6 | 0.7 | 0.3 | 0.3-0.5 | 0.06-0.3 |
| effects | tick | 0.12 | 0.04 | 0.03 | 0.02-0.04 | 0.01-0.03 |
| render: world | frame | 6.3 | 3.2 | 0.9 | 1.3-2.5 | 0-0.9 |
| render: objects | frame | 4.3 | 1.9 | 0.9 | 0.8-1.6 | 0-0.7 |
| render: effects | frame | 4.1 | 3.1 | 0.3 | 0.4-0.8 | 0-0.3 |
| outside any box | frame | 3.7 | 1.7 | 0.8 | 0.7-1.2 | 0-0.6 |
| render: HUD | frame | 1.4 | 0.7 | 0.4 | 0.16-0.3 | 0.09-0.2 |
| render: other | frame | 0.9 | 0.4 | 0.2 | 0.18-0.3 | 0.01-0.15 |
| render: transparent | frame | 0.56 | 0.3 | 0.07 | 0.1-0.2 | 0-0.08 |

**What it says:** the objects' update leads again (15.1 ms a tick, about 3/4 of the tick), a third of it work and the rest waiting, as on c20. **b40 is the first stretch where drawing the effects is large:** 4.1 ms a frame, three-quarters of it work, not misses. That's the particles' own drawing, which the far-and-tiny thresholds (`drawing_sizing.md`) and the effects box are aimed at.

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
