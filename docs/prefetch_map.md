# Where a prefetch pays: the `cache 0x1E` map

The Allegrex has one prefetch, `cache 0x1E` (also "cache 30"). This page maps where in the tick it would pay, measured in the emulator rather than guessed, and says which placements to try first. Labels: **[M]** measured on a PSP, **[E]** emulator or estimate, **[D]** read in the code or data, **[U]** inference.

**In short:**
- **Dropped into today's code, a prefetch is a small lever: about 0.2–0.4 ms a tick at most [E]**, and that's if every one of ~8,500 missing loads and stores got one at its best spot. Placed by hand at the top sites, it saves microseconds. The earlier estimate for X5's prefetch, 0.6–2.4 ms a tick [E], only holds if the code around the loads is reshaped (below).
- **Why it's small:**
  - A fill in flight holds up every load and store [M v7c], and the tick's code has almost no long stretches without one.
  - The modelled I-cache misses one fetch in 34–81 in the tick (39,000–99,000 misses a tick). If an I-miss also waits for a fill [U], those misses break nearly every window that's left.
- **What would make it pay:**
  - **Reshaping code:** moving up to three loads or spills per site takes the total to 0.9–2.2 ms a tick [E].
  - **Software pipelining:** fetching the next item a loop run ahead gets 0.8–2.6 ms [E]. Both figures assume every site is reworked, so they are ceilings.
  - **A hardware answer:** if I-misses don't wait for a fill [U], the total is 0.8–1.9 ms.
- **Scored placements:**
  - pf123b: nine prefetches at the map's top sites, placed with `PSP_PF_INJECT` (the code itself unchanged). Every target's misses disappeared, but the tick gained only 1,900–4,100 model cycles (6–12 µs). The next load after each short stretch waited out the fill instead.
  - pf123c: three placed where the stretch is long in cycles saved 2,600–10,800 cycles a tick (8–33 µs).
  - **The model predicts each placement's gain to within ~5%.** `matrix4x3_rotation_from_quaternion`'s output line, prefetched before its `div.s`, saved 12,666 cycles a tick on b30 against 13,283 predicted, and the same on c20 and d20.
- **The model's own by-product:** the I-cache stall is larger than the data stall in every tick (b30: 19 ms against 10.6 ms) [E]. Code layout (E7, PGO's ordering) is worth more than prefetch, and it would also open prefetch windows.

## How it was measured

### The model (the lab's PPSSPP patch, its `VARIANTS.md` item 12)

The emulator's D-cache model (16 KB, 2-way, 64-byte lines) gained a clock and the timing a PSP-1000 measured (`halo-psp-feasibility.md`, halo_bench v7, v7b, v7c):

| Rule | Basis |
|---|---|
| A miss stalls 72 cycles at its load or store; two misses never overlap | [M v7] |
| `cache 0x1E` doesn't stall; its line lands 72 cycles later and a load after that hits | [M v7b]: 80–90% of a miss hidden 64–96 cycles ahead |
| While a fill is in flight, every load and store waits until it lands; a second prefetch waits too (fills serialize) | [M v7c] for cached loads and stores (148 cycles right after a fill, against 85); assumed for uncached and scratchpad ones [U] |
| An I-miss costs 64 cycles and waits for a fill in flight | 55–70 cycles [M v7]; **the wait is unmeasured [U]** (`PSP_DCACHE_IWAIT=0` turns it off) |
| Every other instruction costs PPSSPP's estimate (1 cycle; VFPU 2; `div.s` 29) | [E]. Real FPU and VFPU latencies are longer, so real register-only stretches are, if anything, longer than modelled |

The model was checked against a test program (the lab's `psp/emulators/ppsspp/tests/pftest/`). The program runs 2,048 loads per test, and every load misses:
- A load 80 register-only instructions after its base is written stalls 72 cycles.
- With `cache 0x1E` right after the base's write, the stall goes away.
- The same prefetch placed by `PSP_PF_INJECT` scores the same to 0.01%.
- A load of another line right after a prefetch waits 71 cycles, then misses: 143 cycles in all, against v7c's 148 [M].

**Two things the model doesn't see:**
- Code inside HLE calls and function replacements.
- `cache 0x18` and `0x1B`. The renderer uses them, and the tick barely does.

### The window

At every miss, the model looks back from the access to the cycle its base register was written, which is when the address became known. It then finds the longest stretch between the two that no memory event interrupts. A prefetch placed at the start of that stretch hides min(stretch, 72) cycles. The sites file (`PSP_DCACHE_SITES`) records this window for each load or store that misses often, in several versions:
- **I-misses as blockers or not:** the window with I-misses counted as interruptions (the default figure, `win`), and with only data operations counted (`win*`).
- **Near misses:** the window with up to three data operations taken out and the I-misses left in place. These are loads that could move up, and spills.
- **Loops:** for an instruction that ran again within 4,096 cycles, the best window anywhere in the previous iteration. This is what fetching the next item while working on this one can use.
- **Stride:** whether the miss address keeps stepping by the same amount.

`tools/prefetchmap.py` ranks the sites from these numbers, names their functions, and reads the code back from each access to its base's write. That gives the blockers and a placement. With `--inject` it writes the placements as `PSP_PF_INJECT` lines, so the model can score them on the same instruction stream.

### The runs

| Batch | Lab run | What |
|---|---|---|
| pf123a | 38621088779 | b30's beach on the b113 route to 100 s, c20's and d20's openings to 90 s, hands off, `fixedticks fixedseed`; the cache models with timing and sites (format 1) |
| pf123b | 38634162461 | The same three (format 3: near misses with I-misses left, loops, strides), each also with nine placements injected. The plain runs reproduced pf123a's tick cycle for cycle |
| pf123c | 38640112956 | The same three, with three placements where the stretch is long in cycles |

The tick in the model [E]:

| Map | Tick | Data-miss stall | I-miss stall | Data misses | I-misses |
|---|---|---|---|---|---|
| b30 | 46.96 ms | 10.55 ms | 18.95 ms | 48,781 | 98,606 |
| c20 | 22.17 ms | 4.10 ms | 11.57 ms | 18,972 | 60,213 |
| d20 | 22.88 ms | 5.39 ms | 7.56 ms | 24,932 | 39,315 |

These are model cycles at 333 MHz: shares and differences carry over to the PSP, absolute values don't.

## What it would hide

Sum over every site, each prefetched at its own best window, in ms a tick [E]:

| Map | As the code is | I-misses don't wait [U] | Up to 3 data operations moved | Next item a run ahead (loops) |
|---|---|---|---|---|
| b30 | 0.40 | 1.92 | 2.15 | 2.64 |
| c20 | 0.18 | 0.98 | 0.87 | 0.83 |
| d20 | 0.19 | 0.81 | 0.99 | 1.49 |
| mean | **0.26** | 1.24 | 1.34 | 1.65 |

Reading it:
- **"As the code is" is 3.8% of these sites' data stall.** The mean window is a few cycles: most missing loads have their address only a handful of cycles ahead (a pointer just loaded, an index just computed).
- **Every column is a ceiling.** It assumes a prefetch at every one of ~8,500 sites (each with at least 32 misses a run) at its best spot, and each prefetch costs a cycle every time it runs. Counting that cost, the first column drops to 0.12 ms.
- **What hand placement got:** pf123b's nine placements saved 6–12 µs a tick. pf123c's three saved 8–33 µs.

### By lever (mean of the three maps, cycles a tick [E])

| Lever | Sites | Misses a tick | Hideable as is | With 3 events moved | Loops, next item |
|---|---|---|---|---|---|
| X5 compact loops (object and datum walks) | 3,579 | 10,514 | 28,260 (0.085 ms) | 239,082 | 155,908 |
| X13 VFPU node matrices (posing) | 669 | 3,164 | 25,407 (0.076 ms) | 105,428 | 40,249 |
| Perception's sphere tests | 952 | 6,175 | 11,232 (0.034 ms) | 116,639 | 127,218 |
| X9 collision walks | 847 | 4,068 | 5,000 (0.015 ms) | 54,580 | 112,482 |
| Other (`tag_get`, `memset`, sound, audio decode) | 2,465 | 6,963 | 15,006 (0.045 ms) | 108,633 | 112,609 |

The levers are matched to sites by function name (`prefetchmap.py`'s `LEVERS`). The "3 events moved" column counts I-misses among the events; the near-miss table below counts data operations only.

## The ranked sites

These are the top of the combined ranking (mean of b30, c20 and d20 [E]):
- **win:** the mean window per miss, in cycles.
- **known:** how many cycles before the access its base was written.
- **hide/t:** the cycles a tick a prefetch at the best spot would hide.

| # | Site | Kind | Misses a tick | Loop | Known | win | hide/t | Lever |
|---|---|---|---|---|---|---|---|---|
| 1 | `matrix4x3_rotation_from_quaternion+0x68` (`sw zero,40(a0)`) | store | 262 | no | 128 | 33.0 | 8,649 | X13 |
| 2 | `matrix4x3_rotation_from_quaternion+0x70` (`swc1 $f5,0(a0)`) | store | 103 | no | 178 | 33.0 | 3,411 | X13 |
| 3 | `object_compute_node_matrices+0xbe0` (`lh v0,36(s2)`) | load | 347 | yes | 14 | 5.9 | 2,042 | X13 |
| 4 | `tag_get+0x5c` | load | 405 | yes | 19 | 2.8 | 1,115 | other |
| 5 | `psp_bsp2d_test_sphere+0x3c8` | load | 120 | yes | 40 | 8.8 | 1,050 | perception |
| 6 | `object_mark_function+0x34` | load | 252 | no | 43 | 4.0 | 1,007 | X5 |
| 7 | `object_get_and_verify_type+0x44` | load | 885 | yes | 1 | 1.0 | 885 | X5 |
| 8 | `objects_update+0x378` | load | 213 | yes | 82 | 4.0 | 851 | X5 |
| 9 | `decode_block+0x1e0` | store | 52 | yes | 52 | 16.0 | 831 | other (audio) |
| 10 | `collision_bsp_test_vector_recursive+0x54` (a stack store) | store | 42 | yes | 621 | 19.6 | 820 | X9 |

What the table says:
- **The only site with a real window is the posing maths.** `matrix4x3_rotation_from_quaternion` writes a node matrix whose line isn't in the cache, and its output pointer is known from the function's first instruction. Between the four quaternion loads and the first store there is register-only FPU work, including a 29-cycle `div.s`. It is the top site on every map.
- **Most other sites are pointer chases.** `object_get_and_verify_type` (885 misses a tick) loads through a pointer it fetched one cycle earlier, so a prefetch there has no time at all.

## Near misses: a few memory operations in the way

These are the sites where taking out up to three data operations, with the I-misses left where they are, opens the window (format 3, mean of the maps [E]). The patterns repeat:

| Site | Misses a tick | win → with 1 / 3 moved | What's in the way | Lever |
|---|---|---|---|---|
| `objects_update+0x378` | 213 | 4 → 72 / 72 | one store (`sw v0,32(s8)`) between a call's returned pointer and its use | X5 |
| `actor_perception_refresh_test_object+0x98` | 128 | 5 → 72 / 72 | the prologue's register spills; the base is an argument | perception |
| `matrix4x3_rotation_from_quaternion+0x68` | 262 | 33 → 63 / 64 | the constant loads (`lwc1 …(at)`) in the middle of the maths | X13 |
| `object_mark_function+0x34` | 252 | 4 → 26 / 29 | three global loads before the access | X5 |
| `actor_perception_update+0xe8` | 65 | 1 → 69 / 69 | prologue spills | perception |
| `object_compute_function_values+0x110` | 80 | 1 → 53 / 53 | prologue spills | X5 |
| `collision_bsp_test_vector_recursive+0x23c` | 72 | 2 → 47 / 48 | the node's plane loads come first | X9 |
| `collision_get_features_in_sphere+0x22c` | 51 | 2 → 65 / 65 | the sphere's own loads | perception |
| `psp_bsp3d_test_sphere+0xa8` | 52 | 2 → 58 / 58 | the plane loads | perception, X9 |

Three shapes cover most of the list:
1. **Prologue spills.** A function gets the object as an argument, then spends its first instructions storing callee-saved registers, and every store would wait for the fill. The prefetch belongs in the caller, before the call, or the spills belong after the first use.
2. **Loads interleaved with maths.** The constants and the second operand load in the middle of the arithmetic. Loading everything into registers first, then prefetching, then computing gives the whole arithmetic as the window. This is the "load everything first" pattern, and it suits X13's posing code and the plane tests of X9 and perception.
3. **A pointer stored to the frame before use.** Moving one store does it.

## Loops: the next item, a run ahead

When an instruction runs again within 4,096 cycles, the model also keeps the best window anywhere in the previous iteration. The largest gains from that kind of software pipelining [E]:

| Site | Misses a tick | win → previous iteration's | Steady step | Lever |
|---|---|---|---|---|
| `collision_bsp_test_vector_recursive+0x21c` | 621 | 0 → 53 | no (a tree walk) | X9 |
| `memset+0x7c` | 313 | 2 → 70 | 96% (64 bytes) | other |
| `object_get_and_verify_type+0x44` | 885 | 1 → 23 | 40% | X5 |
| `cluster_partition_get_next_datum+0x84` | 591 | 0 → 33 | no (a list) | X5 |
| `psp_bsp3d_test_sphere+0x8c` | 383 | 0 → 43 | no (a tree walk) | perception |
| `objects_fix_for_deleted_object+0x64` | 199 | 3 → 68 | 82% (552 bytes, an object datum) | X5 |
| `data_next_index+0x54`, `data_iterator_next+0x6c` | 294, 190 | 0 → 37, 53 | 77%, 73% | X5 |

What the table says:
- **Steady steps are datum arrays.** The iterators (`data_iterator_next`, `data_next_index`, `objects_fix_for_deleted_object`) walk fixed-size datums, so the next address is this one plus the datum size.
- **That window is the caller's work on the item.** The iterator returns and the caller starts loading from what it returned, which would wait. A next-item prefetch pays only where the caller's per-item work starts with register-only maths, or where X5's compact arrays put several items in one line.
- **`memset` is the wrong tool for prefetch.** It writes whole lines, and `cache 0x18` (create the line without a fill, lever 9, [M v7b]: 2× fill bandwidth) removes its misses outright.
- **Tree walks have no steady step.** The BSP and cluster walks (X9, perception) can only prefetch the child once the node is loaded. That is X9's cache-line layout question, not a prefetch question.

## Scored placements

**pf123b:** nine placements from pf123a's top sites, each where `prefetchmap.py` (then counting instructions, not cycles) found the longest stretch. Results [E], model cycles a tick:

| Map | Tick change | Data-miss stall removed | Waits on fills added |
|---|---|---|---|
| b30 | −4,088 | 94,546 | 86,202 |
| c20 | −1,908 | 31,247 | 28,047 |
| d20 | −1,936 | 35,735 | 32,008 |

What happened:
- **The targets' misses went.** The prefetched lines were used: 99.3% of fills.
- **The cost moved instead of vanishing.** Each prefetch sat 3–10 cycles before the next load, which then waited out most of the fill. The waits landed on `object_mark_function`'s own global loads, the constant load inside `matrix4x3_rotation_from_quaternion`, and the targets themselves.
- **Three placements never ran or missed their line** (`object_compute_node_matrices+0xbe0`, `object_header_block_get`, `actor_perception_refresh_test_object`). The straight-line walk's spot isn't on the path the misses come from.

**The lesson:** a prefetch saves only the stretch it starts, so placement has to follow cycles, and a short stretch gains nothing. `prefetchmap.py` now measures stretches in the model's cycles.

**pf123c:** three placements where the walk, now counting cycles, found 16–33 cycles free. Results [E], model cycles a tick against pf123b's plain runs:

| Map | Tick change | Data-miss stall removed | Waits on fills added | Prefetches run |
|---|---|---|---|---|
| b30 | −10,814 (−32 µs) | 37,212 | 21,483 | 4,915 |
| c20 | −7,277 (−22 µs) | 20,382 | 11,266 | 1,839 |
| d20 | −2,572 (−8 µs) | 16,305 | 10,072 | 3,661 |

Each placement's net, set against `prefetchmap.py`'s prediction (cycles a tick):

| Placement | Map | Stall saved | Waits | Prefetch cycles | Net | Predicted |
|---|---|---|---|---|---|---|
| `matrix4x3_rotation_from_quaternion`: `cache 0x1E, 40(a0)` before the `div.s` (0x08939f80) | b30 | 28,982 | 15,296 | 1,021 | **12,666** | 13,283 |
| | c20 | 17,696 | 9,339 | 613 | **7,744** | 8,110 |
| | d20 | 9,934 | 5,243 | 380 | **4,311** | 4,553 |
| `quaternion_decompress_6byte`: its output, 28 cycles ahead (0x08948c9c) | b30 | 3,083 | 907 | 894 | **1,282** | 938 |
| | d20 | 895 | 283 | 324 | **288** | 279 |
| `decode_block` (audio): its output, 16 cycles ahead (0x08a38d44) | b30 | 5,018 | 3,911 | 3,001 | **−1,894** | −1,346 net |
| | d20 | 5,381 | 4,172 | 2,958 | **−1,749** | |

What it shows:
- **The prediction holds.** For a placement at the measured window, the stall saved minus the waits comes within ~5% of the window × misses that `prefetchmap.py` reports.
- **A prefetch that runs far more often than its line misses loses.** `decode_block`'s ran 3,000 times a tick for 70 misses, and the ranking's `net/t` column had already marked it negative.
- **The matrix placement alone is worth 13–38 µs a tick across the three maps [E].** That's the best single site in the tick.

## What to try first

Each of these is an exact speed-up: it changes timing only.
1. **`matrix4x3_rotation_from_quaternion` (X13)**:
   - Load the quaternion and both constants first.
   - Then issue `cache 0x1E` on the output's line (the store to `40(a0)` misses most).
   - Then do the maths, including the `div.s`.
   - Measured window: 33 cycles as the code is, 63 with the constant loads moved [E]. Over the three maps that's about 8,600 cycles a tick (26 µs) as is and 16,400 (49 µs) reshaped; on b30, 13,300 and 25,200 (40 and 76 µs).
   - Its caller, `object_compute_node_matrices`, writes the node matrices in order, so prefetching node *n + 1*'s output before node *n*'s maths is the same idea a run ahead.
2. **`objects_update+0x378` (X5)**: one store sits between a call's returned pointer and its first use. Move the store after the load, or prefetch inside the getter it calls. win 4 → 72 [E], 213 misses a tick: up to about 15,000 cycles a tick.
3. **The prologue-spill sites in perception** (`actor_perception_refresh_test_object`, `actor_perception_update`) **and X5** (`object_compute_function_values`): prefetch the argument's line in the caller before the call, where the caller has register-only work. That is up to 4,000–8,600 cycles a tick each [E].
4. **`memset` in the tick: use `cache 0x18`, not a prefetch.** 313 misses a tick at one store alone. That's lever 9, already measured on hardware [M v7b].
5. **One hardware question before any of it: does an I-miss wait for a fill in flight?** It decides between 0.26 and 1.24 ms a tick for the whole lever. A halo_bench test: `cache 0x1E`, then a jump to code that isn't cached, then time the jump. It fits the rig's idle gaps (`research_application.md` item 9).

After any of these is built, score it in the model with the build's own cache runs (`pfmap = 1`, the DCTIME lines against the base). Then measure it on the PSP. Placement 1 as the code stands (no reshaping) is already scored: 4,300–12,700 cycles a tick (pf123c).

## Honest totals

| Case | ms a tick | Label |
|---|---|---|
| Hand placements in today's code (pf123b, nine sites) | 0.006–0.012 | [E] |
| Three placed by cycles (pf123c); the matrix one alone 0.013–0.038 | 0.008–0.032 | [E] |
| The best five placements once their code is reshaped | ~0.1–0.15 | [E→U] |
| Every site at its best spot, code as is | 0.18–0.40 (mean 0.26) | [E] |
| The same if I-misses don't wait for fills | 0.8–1.9 (mean 1.24) | [E] on an [U] |
| Every site with up to three data operations moved | 0.9–2.2 (mean 1.34) | [E], a ceiling |
| Every loop prefetching its next item | 0.8–2.6 (mean 1.65) | [E], a ceiling |

**The verdict on X5's prefetch half:** the earlier 0.6–2.4 ms a tick [E] matches the reshaped-code ceilings, not plain insertion. The realistic share comes with X5's and X13's restructuring, not before it. And it is smaller than what code layout could take off the I-miss stall (7.6–19 ms a tick modelled [E]).

## Tools and where they live

- **`tools/prefetchmap.py`** (here, beside `cachemap.py`; the emulator harness is slated to move to Sheep-PSP-Tools with it):

  ```
  python3 tools/prefetchmap.py pfsites.txt [more pfsites.txt ...] halo.elf [--phase 1] [--top 30] [--near 20] [--inject N]
  ```

  It reads several runs as their mean a tick. `--inject` leaves out stretches shorter than `--inject-min` cycles (16), sites whose prefetch would cost more than it hides, and a second placement at the same spot: fills serialize, so it would wait for the first. It needs the ELF of the same build to read the code: the lab's build and a local one match byte for byte (EBOOT `d4de9c76198990c8` for pf123 both ways).
- **The emulator's switches:** `PSP_DCACHE_TIMING`, `PSP_DCACHE_SITES`, `PSP_PF_INJECT` and `PSP_DCACHE_IWAIT`, on top of `PSP_DCACHE`. They are documented in the lab's `psp/emulators/ppsspp/VARIANTS.md` (item 12).
- **The batch fields:** `pfmap` and `pfinject` (the lab's `batches/README.md`).
- **Where the emulator patch lives:** the patch went into the lab's `halo-local.patch`, which the runners build. This repo's `tools/ppsspp_local_patches.diff` is the public copy and has trailed the lab's since 2026-10-10 (it lacks items 6–12). The two repos' rules disagree here: this repo's `CLAUDE.md` names `tools/ppsspp_local_patches.diff` as the emulator's patch, and the lab's says emulator patches go in the lab. The lab's is the one in use.
