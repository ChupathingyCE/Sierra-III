# Research: cutting instruction-cache misses on the Allegrex (Sierra-III)

Question: which techniques reduce I-cache misses for a ~59 KB hot tick against a 16 KB I-cache, and which are practical for this port? Written 2026-10-13. Labels: **[M]** measured on the PSP, **[E]** emulator/cache model, **[D]** read in code, docs or data, **[U]** inference.

**Follow-up (2026-10-14, `../research_application.md` W6/W7):** b120's `hotgroup` test grouped GCC's whole "hot" set (562 KB, 769 functions), not the hot core, so its rejection rests on the wrong test. A tighter threshold and the loop-duplicating passes off are in b123.

## 0. What the project already knows (the baseline for everything below)

- **Exchange rate, from the PGO build** (b116 test 7): the model predicted tick I-misses −21% [E], and the PSP tick fell 59.7–60.6 → 55.6–56.1 ms, −7% [M] (`hardware_b116.md`, `b117_candidates.md`). So **1% fewer tick I-misses ≈ 0.3% of the tick ≈ 0.15–0.2 ms on b30** [U]. That's an upper bound, because PGO also changed instruction counts. Use it to size the rows below.
- **Layout alone was measured small, before PGO** (b102): cold parts apart −3.2% I-misses, 16 KB hottest first −0.8% [E]. The misses are mostly capacity misses: the model's top function holds 3.2%, the top 30 about a third (`overnight_b108.md`).
- **The model is right about the tick and wrong about drawing.** For PGO it predicted −6.1% tick (the PSP gave −7%) and −9.2% drawing (the PSP gave ~0). For `gedefer` it predicted −20% drawing I-misses, and the PSP showed no change. Trust model I-miss gains for the **tick**, and confirm drawing on the PSP.
- **`-Os` on all the hot code lost on the PSP:** tick +30%, I-misses per M cycles +9% [M] (E3).
- **The shipped builds don't group hot functions** [D]. pspsdk's `linkfile.prx` places `*(.text .stub .text.* …)` in input order. GNU ld's default script, by contrast, groups `.text.unlikely*`, `.text.exit*`, `.text.startup*`, `.text.hot*` and `SORT(.text.sorted.*)` [D, `ld --verbose`, binutils 2.42]. `PSP_TEXT_LAYOUT` (coldfirst / order file) exists, but no release uses it (`build_b114.md`).

## 1. Summary table

Payoff is the change in tick I-misses unless stated otherwise. ×0.3 converts it to tick time (section 0).

| # | Technique | Expected payoff | Cost | Exact? | How to measure |
|---|---|---|---|---|---|
| T1 | **Group PGO's `.text.hot.*` together, cold first** (linker script only) | 2–6% [U]; b102's cold-apart alone gave 3.2% before PGO made the cold split larger | S | Yes | `halo.map` census; model via `PSP_ICACHE_MAP` with no rebuild; then a build variant on the PSP |
| T2 | **Call-graph order (Pettis–Hansen / C3) inside the hot group** | +1–3% on top of T1 [U]; b102's density-only order gave 0.8% | S–M: call-edge counts from the emulator + a script | Yes | Model via `PSP_ICACHE_MAP`; then a build variant |
| T3 | Conflict-aware placement (cache-line colouring, 2-way aware) | Small [U]: misses are capacity, not conflict | M | Yes | Model: a per-set miss histogram. Only pursue if a few sets dominate |
| T4 | **Inlining budget for the hot units under PGO** | ±5% [U]; direction unknown | S (flags) | Yes (play hash) | `.text.hot` size plus model I-misses, 3–4 variants in one lab batch |
| T5 | **`-Os` per function for "streaming" warm code** (low fetches per miss), `-O2` for the dense loops | Possibly 3–8% [U]; E3 lost because it also shrank the dense 5 KB | M | Yes | Model: per-function fetch/miss density; PSP A/B |
| T6 | More rare paths split cold (validity checks X10, debug/timer code, error paths) | 1–3% per big site [U] | S–M per site | Yes if the work is unchanged | Model; `.text.unlikely` count in `halo.map` |
| T7 | **Phase-split the per-object update** (e.g. lights 8% and validity checks 6% as their own passes over all objects) | 2–6% [U]: each pass's code stays resident across 142 objects | M | **Not guaranteed** (order of side effects): play hash; Sheepdog01's call if it diverges | Model per `PSP_TICK_STEPS` step; PSP |
| T8 | **Batch the update by object type** (sort the iteration, respecting parent/child) | Unknown, possibly large on framework and posing [U] | M | Probably not (update order) | Same as T7 |
| T9 | Lean paths for idle/unseen objects (sleep, #25, E8) | Already ranked; they cut I- and D-misses together | M | Mixed | Existing |
| T10 | LTO + `-fipa-reorder-for-locality` (GCC 15) | Unknown | L: toolchain support unverified; decomp UB risk | Should be | Recon variant only |
| T11 | `-falign-functions=64` / `-falign-loops` | Likely negative for a capacity-bound 16 KB cache [U] | S | Yes | Model; skip unless T1/T2 leave hot loops straddling lines |
| — | Machine outliner | **Not available**: GCC 15 has no outlining option (none in `common.opt` or `mips.opt`) [D] | — | — | — |
| — | Code in the scratchpad / the ME's own I-cache | Scratchpad execution unknown (section 4); the ME is already lever L1 | — | — | — |

## 2. Details by technique

### Cache geometry (everything else depends on it)
- **Size: 16 KB I-cache and 16 KB D-cache** per core ([pspdevwiki CPU](https://playstationdev.wiki/pspdevwiki/index.php?title=CPU); [Copetti](https://www.copetti.org/writings/consoles/playstation-portable/)). 64-byte lines: PPSSPP's interpreter notes that games' cache loops step 0x40 ([PPSSPP `Interpreter.cpp`, Int_Cache](https://github.com/hrydgard/ppsspp/blob/master/Core/MIPS/Interpreter.cpp)) [D].
- **2-way, 8 KB per way: supported by two independent code bases, not by documentation.**
  - uofw's `sceKernelIcacheClearAll` (`src/kd/loadcore/cache.S`) reads the I-cache size from Config bits 9–11 (4096 << IC). It loops over **half** that size in 64-byte steps and issues the index op `cache 0x4` **twice per index**: one op per way [D, [uofw](https://github.com/uofw/uofw)].
  - Daedalus's D-cache loop has the same shape (`research_b4_me_cache.md`).
- **Replacement policy: unknown.** The emulator's model assumes LRU (`tools/ppsspp_local_patches.diff`). The hardware test in section 3 settles both points.
- **Other consoles with the same budget:** the PS2 EE core has a 16 KB 2-way I-cache ([Wikipedia, Emotion Engine](https://en.wikipedia.org/wiki/Emotion_Engine)). The N64's VR4300 has a 16 KB I-cache that is direct-mapped with 32-byte lines (recalled, not re-verified here) [U].

### T1: group the hot code (PGO already labels it)
- **How GCC labels functions** [D, GCC 15 `varasm.cc` `default_function_section`, `predict.cc` `compute_function_frequency`]. With `-freorder-functions` (on at -O2/-Os), GCC puts each function in `.text.hot.<fn>`, `.text.unlikely.<fn>`, `.text.startup.<fn>` or plain `.text.<fn>`, by node frequency.
  - **With a profile, a function is HOT** if any block's count is at least the program's largest block count ÷ `--param hot-bb-count-fraction` (default 10000; non-LTO mode) [D, `predict.cc` `get_hot_bb_threshold`].
  - Per-object code (~500 k calls over a 3,600-tick profile) should land in `.text.hot`. Once-a-tick code should land in plain `.text` [U].
- **Never-executed functions:** with `-fprofile-partial-training` (our `PSP_PGO_PARTIAL=1` default), GCC does **not** treat them as cold: "Do not assume that functions never executed during the train run are cold" [D, `common.opt`]. They stay in plain `.text`, interleaved with the hot code.
- **Why it helps although the cache is far smaller than the hot set** [U]:
  - Hot fragments spread between cold and normal code each waste part of a line at both ends. With hundreds of small decompiled functions, that waste can reach several KB of the 16 KB cache.
  - A contiguous hot block also fixes the hot code's addresses, so unrelated cold-code edits stop reshuffling the hot layout (see "A/B hygiene" below).
- **Change:** a `PSP_TEXT_LAYOUT=hotgroup` mode, i.e. `*(.text.unlikely .text.unlikely.*)` then `*(.text.hot .text.hot.*)` then the rest. It's a few lines next to the existing `coldfirst` code in `psp_build.py`. Exact.

### T2: order functions by call graph
- **Pettis & Hansen (PLDI 1990), "Profile guided code positioning":** procedure ordering by heaviest call edges, block positioning, and hot/cold procedure splitting. The last two are what `-freorder-blocks-and-partition` + PGO already do; the summary is in [Cornell CS6120](https://www.cs.cornell.edu/courses/cs6120/2019fa/blog/profile-guided-code-layout).
- **C3 (Ottoni & Maher, CGO 2017; "hfsort")** is implemented in lld ([`CallGraphSort.cpp`](https://codebrowser.dev/llvm/lld/ELF/CallGraphSort.cpp.html)) [D].
  - It merges each function into its most likely caller's cluster, unless the caller's edge is under 1/10 of the function's weight or the merged density drops more than 8×.
  - It then sorts clusters by weight ÷ size.
  - lld caps clusters at 1 MiB, which suits servers and iTLB/pages. **For a 16 KB cache, cap a cluster near 4–8 KB** [U].
- **Practical here:**
  - `tools/icache_order.py map` sorts by fetch density only; extend it with a C3 mode.
  - Call-edge weights need a small emulator addition: count `jal`/`jalr` per (caller function, callee) on the interpreter. PGO's `.gcda` files have intra-function arcs and indirect-call value profiles, not a usable call graph.
  - `PSP_ICACHE_MAP` measures the new order on the same run **without building it**, so the payoff is known before any EBOOT.
- **GNU ld 2.43+ has `--section-ordering-file`** ("extra mapping of input sections to output sections") [D, [ld/NEWS](https://gnu.googlesource.com/binutils-gdb/+/refs/heads/master/ld/NEWS)].
  - pspdev's binutils is `allegrex-v2.44` [D, [psptoolchain-allegrex config](https://github.com/pspdev/psptoolchain-allegrex/blob/master/config/psptoolchain-allegrex-config.sh)], so it has the option.
  - Our script edit (one `*(.text.<fn>)` line per function) already does the same job, so this is optional.

### T3: conflict-aware placement
- **Hashemi, Kaeli & Calder, "Efficient Procedure Mapping using Cache Line Coloring"** ([DEC WRL 96/3](https://ftp.mirrorservice.org/sites/www.bitsavers.org/pdf/dec/tech_reports/WRL-96-3.pdf)):
  - It places popular procedures so that they avoid the cache sets of their direct callers and callees, and fills the gaps with unpopular code.
  - On an 8 KB direct-mapped cache it cut misses 14% vs Pettis–Hansen and 45% vs link order. Set-associative caches are only proposed (count colours up to the associativity), not evaluated.
- **Here:** 2-way with 59 KB of hot code means mostly capacity misses. Colouring only matters if the model shows a few sets with far more misses than average. Check the histogram before writing anything.

### T4: inlining budget
- Inlining copies a callee into each hot caller, and each copy takes its own lines in the cache. Profile-driven inlining (`-fprofile-use` turns on `-finline-functions`, `-fipa-cp`, `-fipa-cp-clone`, unrolling, peeling, `-ftracer`, `-funswitch-loops`, `-fsplit-loops`, loop versioning, vectorisers [D, GCC 15 `opts.cc` `enable_fdo_optimizations`]) can grow the hot set. The release already turns off unrolling, peeling, tracer and cp-clone (`PSP_PGO_EXTRA`). **Not yet turned off:** `-funswitch-loops`, `-fsplit-loops`, `-fversion-loops-for-strides`, `-fpredictive-commoning`. Each duplicates loop bodies [U].
- **The N64 precedent:** HackerSM64 (Super Mario 64 on the N64's 16 KB I-cache) builds with `-Os`, `-finline-limit=1`, and `-fno-inline -finline-limit=0` for the collision and graph-node units. It also caps `max-unrolled-insns`, uses `-freorder-blocks-algorithm=simple` and `-falign-functions=32` (the N64's line size) ([HackerSM64 Makefile, master `8953c07`](https://github.com/HackerN64/HackerSM64/blob/master/Makefile)) [D].
  - Its note: any `-finline-limit` worsened `math_util.c`. Dense math wants inlining; sprawling logic doesn't.
  - Kaze Emanuar, whose optimisation work led to HackerSM64's settings: unrolled loops are "actually bad" on the N64 because of instruction fetches ([summary](https://setsideb.com/?p=8300); [video](https://www.youtube.com/watch?v=Ca1hHC2EctY)).
  - E3 shows `-Os` wholesale doesn't transfer to the PSP. The per-unit tuning does.
- **Variants** (`PSP_PGO_EXTRA`, hot units):
  - (a) add `-fno-unswitch-loops -fno-split-loops -fno-version-loops-for-strides`;
  - (b) (a) plus `--param max-inline-insns-auto=` at half its default;
  - (c) (a) plus `-fno-inline-functions` (keeps `inline`-declared and small-function inlining).

### T5: optimise for size where code streams, for speed where it loops
- Ericson (GDC 2003, code-cache section): reorder functions, **compile for size where it helps**, watch hidden calls ([slides](https://www.slideshare.net/slideshow/gdc03-ericson-memoryoptimization/5908376); our notes in `research_b13_ericson.md`).
- **The arithmetic** [U]: a line fetched once and run through ~16 instructions costs ~70 cycles of miss against 16 of work, so 20% less code on such paths beats a few extra instructions. In loops fetched thousands of times per miss (the dense 5 KB: BSP walks, matrix code), speed optimisation wins. E3's −30% is consistent with `-Os` hurting the second kind.
- **How:**
  1. From `icache.bin`, rank functions by fetches ÷ (misses × 16). Streaming functions are near 1–4; loops are ≫ 10.
  2. Give the streaming ones `__attribute__((optimize("Os")))` via fixups, or `#pragma GCC optimize` blocks. GCC documents the attribute as meant for debugging, so check that each function's code actually shrinks [U].
  3. PGO has no "warm → size" setting. Its only size lever is for never-run code (`PSP_PGO_PARTIAL=0`), which also slows maps outside the profile.

### T6: more cold splitting
- Same mechanism as E1. The per-object validity checks (`unit_verify_vectors` etc., 6% of the tick) are the largest remaining always-on block; X10 already ranks them.
- Albrecht's rule: "Don't test for exceptions – sort by them" ([Pitfalls of OOP, SCEE 2009](https://www.jmeiners.com/shamans/papers/engine/pitfalls_of_oop.pdf)).

### T7/T8: reorganise the loop, not the code
- **Today's loop shape** [U, from the b114 step table]: each object runs posing → framework → lights → checks before the next object starts. So each of the 142 objects re-streams most of the hot code. 77 k I-misses a tick (b112 model) ≈ 35 KB of code fetched per object update.
- **"For each function, loop over objects" instead of "for each object, call N functions":** each pass's code stays resident, and the cost moves to data (each pass re-touches each object's ~1–2 KB).
- **Sources:**
  - Albrecht (homogeneous arrays, passes per hierarchy level: 19.6 → 4.8 ms on PS3-class hardware, data and code together) [D, slides above].
  - Insomniac R&D (`research_b1_insomniac.md`, proposal H: sort actors by AI state, objects by type).
  - [Jackson Dunstan, "The Other CPU Cache"](https://jacksondunstan.com/articles/3879): group by type to avoid indirect calls and I-misses "if update order allows it".
- **Risk:** Halo's update order is observable (objects read each other's state within a tick).
  - Hoisting **leaf** work whose results nobody reads before the tick ends (light connection, validity checks) is the safe first step.
  - Sorting by type changes the order of interactions and is likely gameplay-visible: Sheepdog01's call.

### T10: LTO with locality reordering
- GCC 15.1 already documents `-fipa-reorder-for-locality`: "group call chains close together in the binary layout" ([GCC 15.1 Optimize Options](https://gcc.gnu.org/onlinedocs/gcc-15.1.0/gcc/Optimize-Options.html)).
  - Its pass is gated on `flag_wpa`, so it **requires LTO** [D, GCC 15 `ipa-locality-cloning.cc`]. It clones callees into callers' clusters, which grows code.
  - Partition size is `--param lto-max-locality-partition` (default 1,000,000 estimated instructions); for us it would need something like 4 k [U].
  - Origin: [Tkachov's RFC](https://gcc.gnu.org/pipermail/gcc/2024-November/245076.html), aimed at large server apps.
- `-fprofile-reorder-functions` (part of `-fprofile-use`) orders by **first execution time** under LTO, aimed at start-up [D, `varasm.cc` comment]. No use for a steady tick.
- **Toolchain:** pspdev's binutils is built `--enable-plugins` [D, `scripts/001-binutils.sh`]; whether `psp-gcc` ships `liblto_plugin` is unverified. Decompiled code often declares one function differently in different units, which LTO turns into real bugs (`-Wlto-type-mismatch`) [U]. Recon only.

### T11: alignment
- **Upstream MIPS sets default `align_*` values only for 64-bit targets** [D, GCC 15 `mips.cc`]. On 32-bit Allegrex, `-O2`'s `-falign-functions` probably adds nothing [U: pspdev's fork may differ; check function starts in `halo.map`]. If so, E3's "`-Os` turns off alignment" explained none of its loss.
- On x86, Ingo Molnar measured 64-byte function alignment at −8% I-misses vs 16, but **`-Os` at +31% misses** ([LKML 2015](https://lkml.iu.edu/hypermail/linux/kernel/1505.2/03248.html)). That's large caches and decoders. With 16 KB and many small functions, padding (~30 B a function on average) costs capacity [U].
- Prefer packed hot code (T1/T2). `-falign-functions=64:17` (pad only when ≤16 bytes are lost) is the safe form if anything.

### A/B hygiene: the "performance lottery"
- Adding or removing unrelated code shifts addresses and moves performance. Mytkowicz et al., "Producing wrong data without doing anything obviously wrong!" (ASPLOS 2009) ([bib](https://evaluate.inf.usi.ch/biblio/keyword/7)), shows link order and environment size changing results. Playdate developers report ±50% swings from layout on a 4 KB I-cache ([devforum](https://devforum.play.date/t/dirty-optimization-secrets-c-for-playdate/23011), anecdotal).
- **For us:**
  - Compare per-function model miss counts, not just totals.
  - T1's fixed hot block makes later A/Bs fairer, because cold-code edits no longer move the hot code.

## 3. Recommended order of experiments

Each step is one build variant or one keep word.

1. **Measure (no build):**
   - (a) `halo.map` of b119 (PGO): count and size of `.text.hot.*`, `.text.unlikely.*` and plain `.text.*`, and function start alignment.
   - (b) An `icache_order.py report` extension: per-function fetches ÷ misses and per-set miss histogram, on the b30 Pelican flight.
   - (c) **Hardware microbench** (one bench EBOOT, `hwprofile` I-miss counter):
     - 2 vs 3 one-line code blocks 8 KB apart, executed in a loop. 2 hit / 3 miss → 2-way; 3 miss every pass → LRU, partial hits → random replacement.
     - The same at 4 KB → rules out 4-way.
     - A 32 KB straight-line run → cycles per line; well under ~70+16 means sequential prefetch.
     - It also checks the model's LRU assumption.
2. **T1 `hotgroup`** on the PGO build. First the model via `PSP_ICACHE_MAP` (remap only), then a build variant, A/B on the Pelican flight. Exact.
3. **T2 C3 order** inside the hot group. Emulator call-edge counts → `order.txt` → `PSP_ICACHE_MAP` → build variant. Exact.
4. **T4 inline/loop-duplication sweep:** variants (a)/(b)/(c) as a model batch, the best one on the PSP. Exact (play hash).
5. **T5 `-Os` for streaming functions,** list from 1(b). Model, then the PSP. Exact.
6. **T6** validity checks cold or off the hot path (X10), per site.
7. **T7 phase split** of lights + validity checks, behind a keep word. Play hash against off; if it diverges, Sheepdog01's A/B.
8. **T8 type-batched update order:** keep word, recon first, Sheepdog01's call.
9. **T10** only after `psp-gcc -flto` builds a toy PRX: a recon variant.

## 4. Open questions

1. **Associativity and replacement** of the Allegrex I-cache. 2-way is inferred from index loops [D→U]; replacement is unknown. Settled by 1(c).
2. **Does the Allegrex prefetch the next instruction line?** If so, straight-line code misses are cheaper than the model's flat cost, which would favour fall-through layout (T1/T2) over size cuts.
3. **Why do drawing I-miss gains vanish on the PSP?** (PGO's drawing −9% [E] vs ~0 [M]; `gedefer` −20% [E] vs 0 [M].) Most likely the frame waits on the GE, so CPU savings there don't show [U]. If so, I-cache work should target the tick.
4. **Can the CPU execute from the scratchpad**, and does that bypass the I-cache? It's single-cycle and off the system bus [D, psdevwiki via `psp_hardware_reference.md`], but nothing found says it is executable. The Playdate's ITCM trick is the analogue. It's also already wanted for data (N1, E13, X9).
5. **Is there an I-cache fill or lock `cache` op on the Allegrex?** PPSSPP handles only op 8 (I invalidate) for the I-cache [D]. MIPS32's Fill/Fetch-and-Lock ops are unverified here.
6. **Does pspdev's GCC fork set any function alignment for Allegrex**, and does `psp-gcc` ship the LTO plugin? Check with `psp-gcc -Q --help=optimizers -O2 -march=allegrex | grep align` and `psp-gcc -v`.

**Not found:** an Acton piece titled "Code is data" (cellperformance articles are already in `research_b2_cellperformance.md`). The Gavin article ([Crash Bandicoot, part 3](https://all-things-andy-gavin.com/2011/03/28/crash-bandicoot-teaching-an-old-dog-new-bits-part-3/)) mentions profiling cache latencies and rewriting into assembly, with no I-cache layout specifics. No PSP-specific developer source on I-cache layout turned up.
