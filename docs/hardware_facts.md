# Measured hardware facts

Every hardware figure the port has measured, in one place, each with the line it came from. `lab:` citations point into the lab repository, most often its findings table `psp/sierra/findings/measurements.tsv`, quoted with the row's fields joined by ` | `. The design docs hold the reasoning; this page holds the numbers. Read it before sizing a lever, writing a part's budget or briefing a helper.

**How to read it**
- Labels follow the repo's rule: [M] measured on the PSP, [E] emulator or estimate, [D] read in code, data or a vendor manual, [U] inference. "[M on E]" is a PSP measurement scaled by an emulator count.
- A citation is `file:line` in `docs/` (or `lab:` for the lab repository) and a fragment of that line, so it can be found again after the line moves. Lines are as of `docs/cost-map` at 5f1eb7d (2026-11-09).
- Where two measurements disagree, both stay, marked **CONFLICT**, with the conditions that differ.
- PSP-1000 unless a bullet names the PSP-3000. The two agree on almost everything measured on both (last section).

**How it was made (2026-11-09):** 942 candidate lines marked [M] or about hardware were pulled mechanically from the docs; a read-only DeepSeek pass grouped them; every citation was then checked against its file (all 188 found; a few differ from the page only by a dash or emphasis). Port facts (buffer sizes, per-map timings, levers) were left out. A second pass the same day compared the 450 hardware rows of the lab's findings tables against the page: it settled ten open items and added ~45 facts that had lived only in those tables. A new hardware measurement adds or updates its bullet here in the same commit as its write-up.

| # | Unit | Headline (cited in its section) |
|---|---|---|
| 1 | Main CPU | 333 MHz MIPS32 R2-like, 7-stage, no MMU; 16 KB/16 KB caches, 64 B lines, 2-way, no L2; miss ≈73 cycles (216 ns) quiet, 424 ns while the GE reads RAM; in-order, one miss in flight, no HW prefetcher |
| 2 | FPU/VFPU | doubles in software (sqrt 2,390 vs 31 cycles); VFPU 128 regs, 8×(4×4), vmmul ~8× FPU; node product 487 ns vs 1,291 ns; PSP-1000 unaligned `ulv.q`/`usv.q` corrupts data; ME has no VFPU |
| 3 | Memory | 32 MB DDR, 64-bit bus at 166 MHz; 24 MB user RAM; 4 MB volatile partition; no snooping; eDRAM 158 ns vs 214 ns main RAM |
| 4 | Scratchpad | 16 KB, always hits, CPU+GE reachable, 14.3 cycles/instruction for code; DMA cannot write it |
| 5 | Bus | Shared by CPU/ME/GE/DMA; no arbiter lever (closed); GE reads cost the CPU 8–18 ms of a 29–69 ms frame; 9.6 µs tick/KB geometry, 1.7 µs/KB textures |
| 6 | DMA | System DMAC 182–204 MB/s but doubles miss cost; DMACplus 250–273 MB/s, +44% misses; VME DMAC 110 / 28–36 MB/s, no miss cost |
| 7 | GE | Texture from VRAM ~500 MB/s vs RAM ~50 MB/s; CLUT8 6.9/7.4 ns/px vs DXT1 17.1/24.2; skinning ~60 ns/weight/vertex; flight work ~89% vertex-side |
| 8 | VRAM | 2 MB at 0x04000000 (uncached alias 0x44000000); display 832 KB, texture cache 616, model copies 600–616 (the docs differ); the ME cannot see it |
| 9 | Media Engine | Second 333 MHz Allegrex, own 16/16 KB caches, same miss cost; 0.45–0.92× the main CPU on GE records; busy 69% of b30's frame on average (82% at most), ~65% on c20, in today's builds (the drawing replay, audio, the AI pass, visibility) |
| 10 | ME eDRAM | 2 MB, ME+DMA only, latency 158 ns, lost on sleep; 256–800 KB free by default |
| 11 | VME | Computes on the PSP-1000; 4 PEs, 2 ops/cycle, pass 72+2n, 16,384×24-bit words, no gather/recurrence; 13 of 64 opcodes differ on the PSP-1000 |
| 12 | Memory Stick | Random 4 KB ≈1.75 ms; ~2 MB/s in play vs 16 MB/s large reads; async reads need I/O priority above the game thread |
| 13 | Audio HW | sceSas mixes VAG in hardware, 32 voices; 22,050 Hz, 512-frame (23.2 ms) blocks |
| 14 | Display/timing | 2×16-bit 512×272 + 16-bit depth = 816 KB; 480×272 output; CPU 333 / bus 166 requested, CPU 329.6 MHz measured |
| 15 | 1000 vs 3000 | Vertex rates equal to 0.1%; VME main-RAM paths slower on Slim; 13 opcodes differ on 1000; fast VME DMAC miss ×1.33 measured on 3000 only |

---

## Main CPU

- **Allegrex: MIPS32 R2-like, 7-stage pipeline, 32-bit, no MMU/TLB; 222 MHz firmware default, 333 MHz allowed (the port runs 333).** [M] `psp_hardware_reference.md:15` — `222 MHz by firmware default, 333 MHz allowed`.
- **16 KB instruction + 16 KB data caches, 64-byte lines, 2-way, no L2.** [M] `cache_work.md:5` — `16 KB instruction and 16 KB data caches and no L2`; [M] `hw_capabilities.md:18` — `16 KB I and D caches, 64-byte lines, 2-way`; [M] `psp_hardware_reference.md:15` — `16 KB instruction cache, 16 KB data cache`.
- **Random cache-line miss ≈73 cycles at 333 MHz; sequential line ≈57.** [M] `psp_hardware_reference.md:17` — `Random cache-line miss ≈ 73 cycles at 333 MHz`; [M] `cpu_cost_map.md:47` — `~73 cycles for a random line, ~57 for a sequential one`.
- **A quiet-bus miss is 216 ns (72 cycles); beside the GE reading main RAM it is 424 ns.** [M] `plan_memory_stalls.md:19` — `216 ns (72 cycles) with the bus quiet`; [M] `levers_next_b138.md:21` — `A miss costs 424 ns instead of 216`.
- **Miss cost also depends on distance: 55 cycles within 128 bytes of the last miss, 71 cycles at 1 KB or more.** [M] `order_file.md:11` — `one 1 KB or more away 71`.
- **The core is in-order: it stalls at the missing load, only ~10 cycles of work slip under a miss, there is never more than one miss in flight, and there is no hardware prefetcher.** [M] `cpu_cost_map.md:48` — `The core is in-order`; `never has two misses in flight`.
- **Data prefetch `cache 30` (Fill, 0x1E) is real and non-blocking, but pays only with ≥~70 cycles of independent work; fills do not overlap each other; there is no MIPS `pref`.** [M] `cpu_cost_map.md:49` — `It pays only with ≥ ~70 cycles of independent work`; [M] `discussion_2026-10-14.md:60` — `hides 80–90% of a miss`. **Refined by halo_bench17** (PSP-1000, bench-hw 42869850188, 2026-11-10) [M]: "independent" means no load, store or instruction miss between the prefetch and the use (they all wait behind the fill); register work of any unit counts alike: VFPU (`vmul.q`), FPU (`mul.s`) and the integer loop around them hide the same share. One line's miss is ~70 cycles; ~56 cycles of register work after the prefetch hid 88-89% of it, ~110 cycles all of it, on both units. So the VFPU isn't what makes the shape pay, and an FPU-only core can use it (the ME's own cache: unmeasured).
- **No instruction prefetch is possible from user code: `cache 0x1E` fills no I-cache line, and the I-side op raises "coprocessor unusable" in user mode.** [M] `cpu_cost_map.md:50` — `fills no I-cache line`; [M] `hw_capabilities.md:21` — `none possible from user code`.
- **`cache 24` (create dirty exclusive, 0x18) makes a line without reading it, at half the cost of writing whole lines: 1 MB 6,067 → 3,116 µs; 172 → 355 MB/s.** [M] `cpu_cost_map.md:51` — `half the cost of writing whole lines`; [M] `research/research_sh4zam_crosscheck.md:42` — `172 → 355 MB/s`.
- **Cache-op codes, from the firmware's own use: D index load tag 0x10 returns both ways' tags; 0x11/0x13 store tag; hit ops 0x18–0x1E; I hit invalidate 0x08; I index store tag 0x01/0x03; I fill 0x0E faults in user mode; no source names a cache-lock op (0x0F/0x1F likeliest).** [D]/[U] `redesign_tick_target.md:270` — `No source names a lock op`.
- **Uncached accesses are ~50 per million cycles; an uncached load/store costs 62 cycles/189 ns.** [M] `cpu_cost_map.md:29` — `its uncached accesses are ~50 a million cycles`; [M] `lab:coord/handoff/hw_audit_2026-10-30.md:88` — `v3 62 cycles/189 ns`.
- **The tick waits on memory for 54–68% of its cycles; drawing 47–60%; every phase 41–62%; ~30% of cycles issue.** [M] `cpu_cost_map.md:29` — `The tick waits on memory for 54–68%`; [M] `hardware_big_moves.md:25` — `53-63% memory wait and ~30% issuing`; [M] `memory_census.md:181` — `41-62% of every phase's cycles`.
- **Instruction misses outnumber data misses 1.2–1.8× on the campaign intros and about 3:1 in the tick; two thirds of misses are instruction fetches; the tick's hot code is ~59 KB against the 16 KB I-cache; ~70 KB of hot code is fetched 47–75 times a tick.** [M] `hardware_utilization.md:77` — `Instruction misses outnumber data misses`; [M] `big_levers.md:135` — `two thirds of those misses are instruction fetches`; [M/E] `big_levers.md:135` — `~59 KB against a 16 KB cache`; [E] `redesign_tick.md:22` — `~70 KB of hot code fetched again 47-75 times a tick`. **By cost the split is near even: data misses are 39–55% of the main CPU's memory stall by map and phase** (counts × the bench's miss prices; the cache model's 64/36, instruction misses first, was wrong) [U on M] `lab:research/opportunities/memory_split.md:7` — `By cost, data misses are 39-55%`.
- **Whole-cache write-back costs 12 µs even clean and 24 µs all dirty; write-back+invalidate 5–32 µs plus up to ~46 µs of refills; a hit op is 8 cycles clean and 19 dirty.** [M] `plan_node_matrices_me.md:64` — `12 µs even with nothing dirty, 24 µs all dirty`.
- **Two threads above the game's priority waking to poll the ME cost the game thread 25–60 µs per wake.** [M] `design_mepoll.md:7` — `each wake costs the game thread 25-60 µs`.
- **The scheduler runs the highest-priority ready thread and never shares the CPU between equals; pspdev pthreads hands out low priorities; `pthread_cond_wait` polls.** [M]/[E]/[D] `psp_hardware_reference.md:113` — `never shares the CPU between equals`.
- **Kernel calls come in two classes:** a firmware system call (`sceKernelGetSystemTimeWide`, which the port's GetTickCount calls ~70 times a tick) is priced at ~2.3 µs [U on M] `lab:docs/research/syscall_census_tock162.md:21` — `~70/tick ≈ ~160 µs/tick`; the port's own kcall into kernel mode (the VME lock's route) measured 17.1 µs empty [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11570` — `17.1 | us a kcall`. GetTickCount is the first class [D] `../psp_fixups.py:9421` — `return (unsigned long long)sceKernelGetSystemTimeWide() * 1000ULL;`.
- **The pipeline itself is not measured on hardware (only Copetti/YAPSPD), and no load-use bench exists.** [U] `lab:coord/handoff/hw_audit_2026-10-30.md:84` — `Pipeline itself`.
- **I-cache: 16 KB with a cliff past it, 2 ways, LRU.** Straight-line code runs 1.06 cycles an instruction at 16 KB, 1.68 at 17 KB, 3.13 at 20 KB; three lines in one set all miss, and A B A C gives 1.98 misses a round, which FIFO could not. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:2269` — `16 KB of straight-line code runs at 1.06 cycles`; `lab:psp/sierra/findings/measurements.tsv:2272` — `A B A C in one set: 1.98 misses a round`.
- **D-cache: 2 ways, LRU; a hit load 8.1 cycles, a miss 72.8.** [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:2273` — `A B A C 2.04 misses a round`; `lab:psp/sierra/findings/measurements.tsv:2274` — `72.8 cycles a load against 8.1 that hit`.
- **One RAM page is open at a time:** the cheap (~55-cycle) miss needs the previous miss in the same 1 KB; two alternating streams each pay the far price (72.5–72.7). [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:2318` — `one page open at a time, as far as these distances show`.
- **Stores miss dearer than loads (100.6 against 85.8 cycles scattered) and allocate the line.** [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:2276` — `a store miss costs ~15 cycles more than a load miss`.
- **A dirty victim adds ~34 cycles to a miss, and its write-back closes the open RAM page (44 clean against 92 dirty in address order).** [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:2485` — `a dirty victim adds 34.3 cycles to a load miss`; `lab:psp/sierra/findings/measurements.tsv:2486` — `the write-back closes the RAM's open page`.
- **A by-range cache hand-over beats a whole-cache write-back below ~480 clean lines (30 KB).** [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:2490` — `under ~480 clean lines (30 KB) of range`.
- **An I-miss waits out a `cache 0x1E` fill already in flight** (141.1 cycles against 88.1 if they overlapped); an I-miss is 68.9 cycles at 333/166 MHz. [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:12192` — `The I-miss WAITS for a cache 0x1E fill in flight`; `lab:psp/sierra/findings/measurements.tsv:12194` — `An I-cache miss (64-byte line, 333 MHz, bus 166) costs 68.9 cycles`.
- **Uncached code runs ~39 cycles an instruction from main RAM** (and ~14 from the scratchpad's uncached view), against ~1.0 cached. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:10981` — `Main RAM's uncached view: 39.45 straight-line, 65.82 in the loop`.

## Pipeline and scheduling

The Allegrex is a MIPS 4K-based core: in-order, single-issue, and every interlock is a slip (the pipeline waits; nothing is reordered). The FPU's latencies are measured (fpubench, 2026-10-27); the integer ones are the 4K's, a best guess until a latency bench runs.

| First instruction | Then | Wait | Source |
|---|---|---|---|
| Load | its consumer | 1 cycle | [D] MIPS MD00016 (4K manual) Table 2-1 |
| `mult`/`madd` 32×32 | `mfhi`/`mflo` | latency 2 | [D] Table 2-5 |
| `mul` 32×32 | its consumer | 3 cycles | [D] Table 2-5 |
| `mfhi`/`mflo` | its consumer | 1 cycle | [D] Table 2-5 |
| `div` | `mfhi`/`mflo` | 10–34 by operand size | [D] Table 2-5 |
| FPU add / multiply | its consumer | 5.7 / 5.2 cycles chained; independent ops issue every 1.7 | [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:9337` — `5.2/1.7` |
| FPU divide / square root | its consumer | ~31 / ~33 cycles, not pipelined | [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:9338` — `not pipelined; 5.2 when the dividend is zero` |
| `trunc.w.s` + `mfc1` | its consumer | 11.1 cycles | [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:9340` — `trunc.w.s and mfc1 (float to integer)` |
| VFPU ops | — | measured: arithmetic 5, reductions 7, compares 8, matrix product N²+6, division 14N+3 | [D] pspdev vfpu-docs `inst-vfpu-desc.yaml`, measured by its authors on PSPs |

- **The compiler doesn't know any of this.** pspdev's GCC 15.2.0 treats `-march=allegrex` as MIPS II with no pipeline description and no `-mtune`, so it schedules with the generic fallback [D, GCC source]. Inline assembly (all the VFPU code) is never scheduled by GCC.
- **What it costs, measured:** the profiler's stall counters put internal stalls at 6.1–8.1% and coprocessor (FPU) stalls at 3.0–5.6% of the main CPU's cycles on seven intros and three routes [M] `hardware_utilization.md` §2. That is the ceiling a pipeline model attacks; some of it sits on true dependency chains, so the recoverable share is a fraction [U]. The lead and its path (`-mtune=4kc` as a proxy, a latency bench, then an Allegrex description) are in the lab's research notes on the compiler.

## FPU and VFPU

- **Single-precision FPU only; doubles are software (`__extendsfdf2`/`__muldf3`), with 1,097 extend sites counted.** [M]/[D] `psp_hardware_reference.md:15` — `doubles are software`.
- **A double `sqrt` costs 2,390 cycles against 31 for `sqrtf`.** [M] `for_other_ports/portable_speedups.md:16` — `2,390 cycles against 31`; [M] `optimization_ranking.md:399` — `A double sqrt costs 2,390 cycles vs 31 for sqrtf`.
- **VFPU: 128 single-precision registers as 8×(4×4) matrices, quad ops; `vmmul` is 8× the FPU for 4×3 products.** [M] `psp_hardware_reference.md:16` — `128 single-precision registers as 8×(4×4)`.
- **A node-matrix product takes 487 ns on the VFPU against 1,291 ns on the FPU in cache (2.6×); from RAM 851 vs 1,630 ns (1.9×).** [M] `lab:coord/handoff/hardware_2026-10-28.md:39` — `487 ns on the VFPU against 1,291 ns`.
- **Rounding: `vadd.s`, `vmul.s`, `vdiv.s` match the FPU bit for bit; `vsqrt.s` is 1 ulp off in 93% of cases.** [M] `lab:coord/handoff/hardware_2026-10-28.md:40` — `is 1 ulp off in 93% of cases`.
- **PSP-1000 silicon bug: unaligned `ulv.q`/`usv.q` corrupt data; use `lv.q` on 16-byte-aligned memory or scalar `lv.s`.** [M] `psp_hardware_reference.md:16` — `PSP-1000 silicon bug: unaligned`.
- **The Media Engine has no VFPU; its FPU is exactly as fast as the main CPU's and bit-exact with it.** [M] `hardware_utilization.md:245` — `an FPU exactly as fast as the main CPU's`; [M] `big_levers.md:460` — `The Media Engine has no VFPU`.
- **The matrix product costs ~1.5 µs on the FPU against ~0.45 µs on the VFPU; `vfpu_multiply` differs from the FPU by up to 5.9e-6 relative.** [M] `plan_node_matrices_me.md:80` — `worst relative difference 5.9e-6`.
- **Branchy scalar work on the ME runs at 0.88–0.96× the main CPU's time.** [M] `hardware_utilization.md:245` — `0.88-0.96x the main CPU's time`.
- **VFPU usage in this port is small: 0.2–0.4% of the main CPU's cycles on a10/a30/b30/d20.** [M] `lab:coord/handoff/hardware_2026-10-28.md:13` — `VFPU **0.2-0.4%**` — quote fragment: `0.2-0.4%` (1 word).
- **Denormals:** a denormal operand doubles only a chained add (11.4 against 5.7 cycles), not independent streams; denormal results cost nothing; flush-to-zero (FCR31 bit 24, off in the game) removes the extra. No FPU slow path worth fixing. [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:9336` — `a denormal operand doubles the chain (11.4), not the streams`.
- **Quaternion product: FPU 718 ns, element-wise VFPU 220 ns (exact, 0 of 20,000 differ), `vqmul.q` 175 ns but not exact** (18,689 of 20,000 differ, worst 16,384 ulp). [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:9992` — `vqmul.q 4.1x but differs in 18,689`.
- **`vdot.q` and `vrsq.s` are not exact against the FPU:** 9,668 and 17,018 of 20,000 differ (worst 8,192 and 7 ulp); the emulator's VFPU rounding differs from the PSP's, so non-element-wise exactness is a PSP question. [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:9993` — `vdot.q differs from the FPU's sequential sum in 9,668`; [E] `lab:psp/sierra/findings/measurements.tsv:9994` — `the emulator's VFPU rounding is not the PSP's`.
- **VFPU rounding is the same on both PSPs:** add, multiply, divide exact against the FPU; square root 1 ulp off in 18,604 of 20,000. [M] `lab:psp/sierra/findings/measurements.tsv:8903` — `square root differs in 18,604 of 20,000`.

## Memory (DDR, partitions, uncached access, alignment)

- **Main RAM: 32 MB DDR on a 64-bit bus at 166 MHz (PSP-1000).** [M] `psp_hardware_reference.md:17` — `32 MB DDR, 64-bit bus at 166 MHz`.
- **The 166 MHz bus setting is requested but never verified.** [D]/[U] `lab:coord/handoff/hw_audit_2026-10-30.md:56` — `Bus frequency never verified`.
- **The CPU reads VRAM at 132 ns vs 220 ns for main RAM.** [M] `psp_hardware_reference.md:17` — `VRAM 132 ns vs 220 ns for RAM`; [M] `rebuild_plan.md:45` — `CPU misses there cost 132 ns, not 220`.
- **User RAM is 24 MB (the game's budget); the PSP-1000's 32 MB leaves ~24 MB for the game.** [M] `for_other_ports/psp_architecture.md:3` — `24 MB of user RAM`; [M] `lab:docs/research/rtcw_psp_feasibility.md:16` — `32 MB, ~24 MB for the game`.
- **The volatile partition is 4 MB at 0x08400000, all correct at normal latency (219 ns), and is lost on sleep.** [M]/[D] `lab:coord/handoff/hw_audit_2026-10-30.md:53` — `Locks, 4 MB at`; `normal latency (219 ns)`.
- **The PSP-3000's extra RAM isn't granted by `MEMSIZE=1` alone through the lab rig's launcher:** the 64 MB layout (`PSP_BIG_RAM=1`) started with 9,956 KB of user memory free (the standard build: 13,012 on the same setup), so the user partition stayed 24 MB, and the build crashed in `game_initialize` with its game state above it. Getting the extra RAM needs ARK-4's extra-memory option or a launcher that asks for the larger partition. [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:12613` — `KB of high memory granted: the 64 MB build crashed at load`.
- **Partition query trap: `sceKernelPartitionTotalFreeMemSize`/`MaxFreeMemSize` do not check that the partition exists; empty partitions on a fat (7 = ME user, 8 = slim-only extended kernel) dereference a null control block and bus-error in sysmem.** [M] `psp_hardware_reference.md:109` — `do not check that the partition exists`.
- **Nothing snoops: a write sits dirty until eviction, and the GE, ME and DMA read main memory.** [D] `lab:coord/handoff/hw_audit_2026-10-30.md:50` — `A write sits in a dirty line until eviction`; [D] `psp_hardware_reference.md:15` — `no snooping`.
- **eDRAM reads cost 158 ns against 214 ns from main RAM.** [M] `big_levers.md:58` — `158 ns against 214 ns from main RAM`; [M] `big_levers.md:76` — same figure.
- **A cache hit costs 24.2 ns.** [M] `big_levers.md:79` — `Cache hits stay at 24.2 ns`.
- **Job data in main RAM competes with the SC and the GE for the one bus; the ME's private eDRAM is the place for a working set.** [M] `psp_hardware_reference.md:76` — `competes with the SC and the GE for the one bus`.

## Scratchpad

- **16 KB scratchpad: always hits, costs the D-cache nothing, and is reachable by both the CPU and the GE.** [M] `cpu_cost_map.md:52` — `16 KB, always hits, costs the D-cache nothing`.
- **Code run from the scratchpad executes at 14.3 cycles an instruction (so code is not a good use of it).** [M] `cpu_cost_map.md:52` — `14.3 cycles an instruction`; [D] `hw_capabilities.md:36` — `code runs from it at 14.3 cycles an instruction`.
- **DMA cannot write the scratchpad: it reports success but no data arrives; the scratchpad is off the system bus.** [M]/[D] `discussion_2026-10-14.md:63` — `reports success but no data arrives`.
- **The w142 log shows 15,104 of 16,384 bytes in use.** [M] `hardware_utilization.md:92` — `15,104 of 16,384 bytes used`.
- **SCRATCHBSP (the BSP's top nodes) gains −0.3 ms in cinematic and flight.** [M] `hw_capabilities.md:36` — `−0.3 ms cinematic and flight`.
- **DMA running beside the CPU slows its misses by duty: ×2.1 at 100% (large blocks), ×1.2 at 50%, none at 25%;** the older single reading (218 → 499 ns, ×2.3) matches 64 KB blocks at full duty. [M, both PSPs] `lab:psp/sierra/findings/measurements.tsv:10326` — `the 218 -> 499 ns (2.3x) figure re-measured: it holds as ~2.1x for large blocks at 100% duty`.
- **The scratchpad's cached view goes through the I-cache like main RAM; its uncached view (0x40010000) runs at 14.3 cycles an instruction and leaves the I-cache alone.** [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:10981` — `It costs 14.29 cycles an instruction straight-line`.

## Bus and contention

- **The bus is shared by the CPU, the ME, the GE and DMA.** [M] `hw_capabilities.md:108` — `shared by the CPU, the ME, the GE and DMA`.
- **No bus-priority register has been found: the register snapshot found none, and the disassembly route is closed.** [M]/[D] `hw_capabilities.md:125` — `found no register that moves with load`; [M] `psp_hardware_reference.md:253` — `Nothing moved with the GE's load`.
- **The GE's main-RAM reads cost the CPU 8–18 ms of a 29–69 ms frame; with the GE on VRAM only, memory wait falls 56% → 40% and cycles per D-miss 358 → 192.** [M] `psp_hardware_reference.md:234` — `memory wait 56% → 40%`. A later, wider A/B (model buffers and level streams in RAM against VRAM, b30 on the PSP-3000) puts it at 10–25 ms of tick (+30–65% by stretch) while drawing moves only +0.3 to +1.8 ms: contention, not drawing. [M] `lab:psp/sierra/findings/measurements.tsv:12469` — `tick 32.8 vs 42.6 ms cinematic (+29.7%)`.
- **CPU misses take +65–96% longer while the GE reads textures from RAM and +272% while it reads vertices; VRAM reads leave them unchanged.** [M] `cpu_cost_map.md:53` — `+65–96% longer while the GE reads textures`.
- **The GE's reads from main RAM cost the CPU about one for one.** [M] `levers_next_b138.md:21` — `cost the CPU about one for one`.
- **CONFLICT, the price of a KB the GE reads from RAM:** 9.6 µs of tick per KB/frame of geometry and 1.7 µs per KB of textures read once (a fit to geometry-read curves) [M on E] `lab:coord/handoff/gebus.md:48` — `9.6 us of tick a KB a frame`; ~40 µs of tick a KB for ~550 KB of model buffers and level streams moved between VRAM and RAM (one A/B) [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:12469` — `~40 µs of tick a KB`. The units differ more than the hardware: 9.6 is per KB *read* a frame, 15.9–17 and ~40 per KB *resident* in VRAM (`memory_ledger.md:149` — `15.9-17 us of tick a frame per KB`); at the flight's ~1,645 KB a frame of geometry reads, ~40 per resident KB is ~13 per KB read [U]. A per-read count on the ~40 µs A/B's two sides would put all three in one unit. The marginal KB is worth less than the average: moving index lists to VRAM (VMINDEX, −368 KB read a frame) measured tick +2.1% on the PSP-1000 [M] `lab:psp/sierra/findings/measurements.tsv:10763` — `tick 52.5 -> 53.6 (+1.1, +2.1%, real)`.
- **VRAM copies of model/level bytes are worth 15.9–17 µs of tick per KB resident on b30's flight and landing.** [M] `memory_ledger.md:149` — `15.9-17 us of tick a frame per KB`.
- **A KB moved into VRAM changes the GE's own time by 1.63 µs.** [M on E] `redesign_plan.md:308` — `1.63 µs of GE time a KB`.
- **c20's geometry read beside the ticks costs ~12.4 µs per KB/frame.** [M/U] `lab:coord/handoff/gebus.md:15` — `~12.4 us per KB a frame`.
- **The GE's reads inside the ticks cost b30 11.7–15.1 ms of tick a frame (16–17%), c20 5.5 (15%), d20 3.4–4.4 (7–8%).** [U on E and M] `cpu_cost_map.md:79` — `cost b30 11.7–15.1 ms a frame of tick`.
- **The ME's own traffic costs the tick 1.4–6.7%.** [M] `hardware_utilization.md:245` — `Its traffic costs the tick 1.4-6.7%`.
- **ME scattered misses slow the main CPU's tick +11.9% (+6.4 to +13.3) and slow the ME itself 15%; with arithmetic between loads, +5% and −10%.** [M] `me_programme.md:33` — `the main CPU's tick +11.9%`.
- **ME work beside the main CPU: 4.5% tick slowdown for work like the CPU's own, 10–11% for scattered misses, 1–3% when on its own eDRAM; the ME itself runs 11–15% slower while the main CPU works.** [M] `plan_m6_limits.md:11` — `slows the main CPU's tick 4.5%`; `runs 11-15% slower while the main CPU works`.
- **CONFLICT (conditions differ):** a latency-bound bench found the two CPUs did *not* slow each other (189.7 vs 189.8 ms), while halo_bench v12 puts the ME at 1.04–1.14× as a bus contender. The first is a walk with no GE and no shared working set; the second is a contention bench. [M] `lab:coord/handoff/hardware_2026-10-28.md:41` — `neither slowed the other`; [M] `lab:coord/handoff/hw_audit_2026-10-30.md:38` — `1.04–1.14×`.
- **There is no quiet phase to schedule the GE into: every phase waits 34–59% on memory.** [M] `optimization_ranking.md:314` — `No quiet phase`.
- **In the beach bus budget the CPU moves ~90% of the bytes; price, not bytes, ranks the masters.** [M] `lab:coord/handoff/contention.md:45` — `The CPU moves ~90% of the bytes`.
- **The profiler has no live GE-bus counter:** 0xBC400050 (the offset PSPTEK's notes name) reads 0 with the GE idle and busy, as does 0x4C; only 0x48 counts. So the GE's main-RAM toll on the tick stays [U on M], measured by A/B (VRAM against RAM), never by a counter. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:12468` — `bc400050 0 -> 0`.
- **What each master costs a CPU miss (quiet 221 ns):** DMA ×2.1 at full duty; RAM → VRAM copies ×1.93; the GE texturing a 1 MB RAM texture ×1.64 (362 ns); the ME streaming ×1.14, copying ×1.04. Both PSPs alike. [M] `lab:psp/sierra/findings/measurements.tsv:10326` — `GE texturing a 1 MB RAM texture: 362.1 ns (x1.64)`.
- **Small DMA blocks at high duty slow even cache hits (24.2 → 35.0 ns):** the copying thread's switches, not the bus; large blocks leave hits alone. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11113` — `hits 24.2 -> 35.0 ns`.
- **The VME's DMAC yields to the CPU:** the CPU's misses stay at 226 → 225 ns while the DMAC's own transfers slow ×1.74 in and ×2.0 out. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11457` — `THE VME's DMAC COSTS THE MAIN CPU's MISSES NOTHING`.
- **The ME's cost to the tick depends on its work:** scattered main-RAM misses +10–11%, with arithmetic between +4.5%, on its own eDRAM +0.8–3.2%; the ME itself runs 11–15% slower beside the tick. Both PSPs agree within 0.7 points. [M] `lab:psp/sierra/findings/levers.tsv:175` — `ME scattered main-RAM misses +11.0% (PSP-3000) / +10.3% (PSP-1000)`.
- **No bus arbiter lever exists:** a read-only snapshot of 241 documented registers found none that moves with load, and the DDR driver's 14 exports touch no per-master register, so there is nothing to write. The bus is routed around (VRAM for the GE, RAM-heavy passes out of the tick). [M]+[D] `lab:psp/sierra/findings/measurements.tsv:11699` — `0 | documented config registers that move with load`; `lab:docs/research/bus_arbiter_closure.md:10` — `NO, closed both ways.`
- **The clock registers read 0x01ff01ff idle and busy, which regsnap reads as CPU 333 MHz, bus 166 MHz;** the CPU's own clock measures 329.6 MHz. The bus clock is confirmed as set, not timed. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:12468` — `bc200000 and bc200004 01ff01ff both ways`.

## DMA

- **System DMAC: 182–204 MB/s, but the CPU's misses ×2.1 at full duty, ×1.2 at half, none at a quarter.** [M] `hw_capabilities.md:62` — `182–204 MB/s`.
- **DMACplus rate by length (main RAM → eDRAM, 16-byte width, burst 1):** 4 KB 97 MB/s, 16 KB 198, 32 KB 222, 65,520 B 263 (~296 MB/s slope, ~28 µs fixed); eDRAM → main 273 MB/s for 16 KB; the CPU's misses +44% beside it. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11458` — `4 KB 42-45 us (97 MB/s), 16 KB 82-83 (198)`; `hw_capabilities.md:62` — `the misses +44%`.
- **DMACplus measured on the PSP-3000: 4 KB in 42–43 µs (234–250 MB/s); 16 KB 85–89 µs; eDRAM→main 62 µs (264 MB/s).** [M] `dma_engines.md:125` — `4 KB in 42–43 µs`.
- **DMACplus into the ME's eDRAM: 254–256 MB/s at 16-byte transfers; one descriptor up to 4,095 counts (65,520 bytes); a chained descriptor list hung the PSP.** [M] `plan_sound_edram.md:7` — `254-256 MB/s at 16-byte transfers`; `A chained descriptor list hung the PSP`.
- **The VME's DMAC: 110 MB/s from eDRAM and 28–36 MB/s from main RAM, at no cost to the CPU's misses.** [M] `hw_capabilities.md:62` — `28–36 from main RAM`; [M] `dma_engines.md:13` — `Its cache misses stay at 225 ns`.
- **Round 3 fast mode (VME DMAC config 0x310, bits 8 and 9): main RAM straight into VME buffers at 262 MB/s (5.05 cycles/word), eDRAM at 448 MB/s (2.96); it raises a main-CPU miss 227 → 303 ns (×1.33).** [M, PSP-3000] `dma_engines.md:11` — `262 MB/s (5.05 cycles a word)`; `a main-CPU miss goes 227 → 303 ns`.
- **Best pipelined main RAM → eDRAM → VME staged copy: 88 MB/s with the ME's CPU free.** [M] `dma_engines.md:10` — `88 MB/s from main RAM into the VME's buffers`.
- **Priority against the main CPU: 227 ns alone, 226 beside 0x40, 220 beside 0x48 (PSP-1000).** [M] `dma_engines.md:85` — `227 ns alone, 226 beside 0x40`.
- **DMA's cost to the CPU's misses scales with its duty (×2.1 / ×1.2 / ×1.0 at 100 / 50 / 25%), so eDRAM staging belongs at load or at low duty, not as a constant per-job stream.** [M] `lab:psp/sierra/findings/measurements.tsv:10326` — `the 218 -> 499 ns (2.3x) figure re-measured`; the older figure: [M] `research_application.md:51` — `218 to 499 ns while it runs`.
- **Copying large blocks flat out: a CPU miss takes ~470 ns instead of ~221 (×2.1; 64 KB blocks ~500, ×2.26; into VRAM ~428, ×1.9), the CPU's streaming reads fall to 0.37×, and the DMA itself drops from 182–204 MB/s to 62–65 MB/s while the CPU misses.** [M] `big_levers.md:79` — `makes a CPU cache miss take ~470 ns instead of ~221`.
- **4 KB pieces at 99% duty cost +44% (221.5 → 318.2 ns).** [M] `dma_engines.md:130` — `221.5 → 318.2 ns`.
- **USB DMA doubles miss latency (the rig runs the game with no USB).** [M] `lab:docs/infra/hardware_rig.md:47` — `bus DMA doubles miss latency`.
- **DMACplus burst code is worth 4.5×** (code 0 58 MB/s, code 1 197); code 2 stalls the channel without a hang. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11463` — `the burst is worth 4.5x`.
- **The VME's DMAC prices inbound per memory read, not per byte** (16-bit halves at half a word's cost); outbound 2.70 cycles a word (491 MB/s); its group modes (0x50 in, 0x58 out) gather and scatter in 2-D for a copy's price. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11426` — `the inbound DMAC costs per memory read, not per byte`; `lab:psp/sierra/findings/measurements.tsv:11455` — `12.09 | ME cycles a word, eDRAM -> VME (0x40)`; `lab:psp/sierra/findings/measurements.tsv:11456` — `0x50 is 0x40 with a second address pattern`.
- **The ME feeding the VME by itself:** straight from main RAM 37 MB/s; copy to eDRAM then DMAC 55; overlapped 72.6 (the ME's CPU 100% busy). The DMACplus pipeline's 88 MB/s leaves the ME free. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11459` — `the overlap reaches 66% of the ideal max(copy, DMAC)`; `lab:psp/sierra/findings/measurements.tsv:11462` — `THE STAGING PIPELINE OVERLAPPED`.

## GE

- **The GE reads textures from VRAM at ~500 MB/s but from main RAM at ~50 MB/s; RAM→VRAM copies run ~150 MB/s.** [M] `psp_hardware_reference.md:20` — `from main RAM at ~50 MB/s`.
- **Texture sampling at 1:1 (ns/pixel, RAM/VRAM, 32-px strips): CLUT8 6.9/7.4, CLUT4 5.9/7.1, 8888 15.2/12.3, DXT1 17.1/24.2, DXT3/5 19.6/14.9, flat fill 4.95; minified 2:1 DXT1 49.5 vs CLUT8 11.3.** [M] `hw_capabilities.md:71` — `against CLUT8 6.9 / 7.4`. Full-screen sprites fill faster (2.05 ns/px, CLUT8 2.79–2.86), so 4.95 isn't the fill floor. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:6600` — `full-screen sprites (16 vs 32, the difference)`.
- **Mipmaps save CLUT8 11.3 → 7.2 ns/pixel (−36%) and DXT1 49.5 → 17.7 (−64%).** [M] `hw_capabilities.md:72` — `CLUT8 11.3 → 7.2 ns a pixel`.
- **DXT1/3/5 are never used and are closed by the bench: DXT1 holds the same 4 bits/texel as CLUT4 at ~3× the sampling cost.** [M] `hw_capabilities.md:71` — `DXT1/3/5 never used`.
- **The GE's 8 KB on-chip texture cache is LRU and holds texels widened (the reason DXT and 5650 lose).** [D]+[U] `lab:coord/handoff/hw_audit_2026-10-30.md:59` — `8 KB on-chip texture cache (LRU)`.
- **Hardware skinning: 8 bone matrices with weights, on by default; the GE blends every weight a draw declares (zeros included) at ~60 ns a weight/vertex (1:82, 2:139, 3:199, 4:259, 8:501 ns unlit).** [M] `hw_capabilities.md:68` — `~60 ns a weight a vertex`; [M] `hw_capabilities.md:68` — `sceGuBoneMatrix`.
- **The GE cannot take posing: no hierarchy and no output but pixels.** [D] `hw_capabilities.md:68` — `no hierarchy`.
- **Call lists: BASE+CALL+RET costs 0.33 µs (~32 plain commands at 10.4 ns); a bone-matrix load is 132 ns in the list, 164–191 ns called from RAM in eights, 105 ns from VRAM.** [M] `hw_capabilities.md:70` — `132 ns in the list`.
- **Bounding-box culling in a list is unsupported (`sceGuBeginObject`); a passing box costs 46 ns, a failing one 1.18 µs.** [M] `hw_capabilities.md:75` — `one that fails costs 1.18 µs`.
- **Morphing (`sceGuMorphWeight`) is unsupported and is irrelevant here: Halo's models have no per-vertex blend shapes; the rate would be ~65 ns a morph target above the lighting floor.** [M]/[U] `hw_capabilities.md:76` — `~65 ns a morph target`.
- **GE vertex rates are equal on the PSP-1000 and PSP-3000 to 0.1% on every case.** [M] `hw_capabilities.md:84` — `equal to 0.1% on every vertex case`.
- **CONFLICT, GE lighting:** at d20's load the models' two point lights cost nothing measurable (38.8 vs 38.8 ms GE busy) [M] `capacity_model.md:129` — `costs it nothing measurable per vertex`; a synthetic list (half-pixel triangles) prices it steeply: unlit 65.5, 2 distant 96.5, 2 distant + 2 point 211.1, + specular 319.8 ns a vertex [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11834` — `unlit 65.5; 2 distant 96.5; 2 distant + 2 point 211.1`. In play the GE's vertex cost is hidden behind other limits; the bench is the unit's own rate.
- **On b30's flight the GE's work is ~89% vertex-side and ~11% fill: clipping every draw to one pixel took 55.6 → 49.6 ms; the opening is ~30% fill (26.6 → 18.5 ms).** [M] `redesign_plan.md:201` — `~89% vertex-side`.
- **Moving the GE's reads off main RAM barely changes the GE's own time (1.63 µs per KB); it is bound by vertex and pixel work.** [M on E] `redesign_plan.md:308` — `barely changes its own time`.
- **The CPU waits for the GE only 0.3–0.4 ms a frame on a30/b30/d20.** [M] `research/research_vita.md:237` — `the CPU waits 0.3–0.4 ms a frame for it`; [M] `for_other_ports/psp_architecture.md:26` — `0.2–1.5 ms a frame`.
- **The GE's own work on b30, with no other instrument on (G6, `gebusy2` alone): busy 48.8 ms a frame on the flight and 53.7 on the landing, plus 14.5 / 16.9 ms idle inside a list waiting for the ME's next chunk.** b151's ~84 ms (83.5 unscaled) came with the attribution run's other instruments on, so it overstates the GE. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:12612` — `the GE busy 48.8 / 53.7 ms a frame`.
- **Under the ME replay, 96–100% of the GE's busy time falls inside the tick — there is no outside-the-tick window.** [M] `redesign_tick.md:35` — `96-100% of its busy time falls inside the tick`.
- **GE busy time, mean/p90 of logged windows: b30 23.6/36.6 ms, a30 39.0/69.3, a10 24.5/33.2.** [M] `plan_m6_limits.md:12` — `b30 23.6 / 36.6 ms`.
- **The GE has spare fill on the main-CPU-bound maps.** [M] `hardware_utilization.md:246` — `has spare fill on the main-CPU-bound maps`.
- **A GE list from a cached alias hangs the GE: `listcachedall` hung within 2 s because a line was still in the CPU's cache when the GE read it.** [M] `optimization_ranking.md:181` — `the GE hung within 2 s on the PSP`.
- **The frame end's wait for the GE is 0–2.0 ms; `geshow3` waits 200 × 2 ms and then shows without it.** [M]/[D] `job_graph.md:45` — `200 × 2 ms, then shown without it`.
- **Lighting and skinning overlap; the slower sets the rate** (skin 1–3 lit = 211.1 ns, the lighting floor; skin 4 259.4; 8 500.9). [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11836` — `lit = max(lighting, skinning)`.
- **Vertex source and format don't change the GE's rate:** VRAM against RAM and 16-bit against float within 1.4%. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11837` — `no effect: VRAM vs RAM and 16-bit vs float within 1.4%`.
- **No post-transform vertex reuse:** every index of an indexed mesh is a full transform (255.4 ns an index); a strip costs the same per vertex as a list, so a strip triangle is ~1 transform against a list's 3. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11838` — `no post-transform reuse, every index is a full transform`; `lab:psp/sierra/findings/measurements.tsv:11839` — `same per vertex as a list`.
- **Sub-lists: a 13-word bone load costs 105 ns from a VRAM sub-list, 164 from RAM, 132 inline; GE lists do execute from VRAM.** [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11842` — `called sub-list in VRAM 105`.
- **GE calls nest two deep, no deeper** (a third level never runs; the list still ends); a call costs 310 ns one deep, 554 two deep. [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:9856` — `The GE keeps two return addresses`; `lab:psp/sierra/findings/measurements.tsv:9857` — `310 ns a CALL and its RET over two NOPs`.
- **The GE truncates when converting colour to integer codes** (red's first step at input 8), and readback copies top bits into low ones. [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:3132` — `red's first code step at input 8`; `lab:psp/sierra/findings/measurements.tsv:3133` — `24 red inputs dropped`.

## VRAM

- **2 MB at 0x04000000 (uncached alias 0x44000000) with a 512-bit internal bus to the GE.** [M] `psp_hardware_reference.md:20` — `512-bit internal bus to the GE`.
- **The fixed display takes 832 KB: two 16-bit 512×272 buffers + 16-bit depth = 816 KB, plus the motion sensor's 16 KB target.** [M] `psp_hardware_reference.md:20` — `two 16-bit 512×272 buffers + 16-bit depth = 816 KB`; [D] `plan_ge_vram.md:34` — `two frame buffers and the depth buffer`.
- **The rest of VRAM is allocated: texture cache 616 KB and model/level copies' room 600 KB.** [M] `plan_ge_vram.md:34` — `the texture cache 616 KB`; [M] `memory_ledger.md:148` — `616` (texture cache size row).
- **Only 13–48 KB of VRAM is free behind the 816 KB of fixed buffers.** [U on M] `redesign_plan.md:258` — `13-48 KB free behind 816 KB`.
- **The ME cannot see VRAM: at VRAM's addresses it reads memory of its own.** [M]+[D] `design_b25_bake.md:5` — `the ME doesn't see VRAM but memory of its own`; [D] `layout_plan.md:173` — `the ME, which writes them, can't see VRAM`.
- **Swizzled data read from RAM is ~20% slower than from VRAM.** [M] `design_b25_bake.md:40` — `~20% slower than VRAM`.

## Media Engine

- **The ME is a second Allegrex at 333 MHz with its own 16 KB/16 KB caches, the main CPU's shape: 2-way, 64-byte lines, LRU-like, for code and data.** [M] `mehotg.md:7` — `The ME's caches are the main CPU's shape`.
- **The ME's cache miss costs the same as the CPU's: 218.9 ns against 215.7 ns.** [M] `lab:coord/handoff/hw_audit_2026-10-30.md:63` — `218.9 ns against 215.7 ns`.
- **ME misses do not slow the main CPU's (latency-bound work), so work on the ME roughly doubles the misses the machine serves at once.** [M] `cpu_cost_map.md:54` — `the two CPUs' misses don't slow each other`.
- **On its own the ME replays the GE records at 0.45–0.92× the main CPU's time and builds lists ~25% slower (1.25×).** [M] `me_programme.md:35` — `0.45–0.92× the main CPU's time`; [M] `frames_in_flight.md:32` — `~25% slower than the main CPU`.
- **The ME is busy most of the frame in today's builds: 69.2% of b30's frame on average over 45 windows, 81.8% at most (counted once); its job on c20's late windows is 38.3 ms a frame (~65% of a ~59 ms frame), 38% of it waiting on memory.** [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:11180` — `busy mean 69.2%, max 81.8% (counted once)`; [M, PSP-1000] lab findings `me_c20_frame` — `12.77 M ME cycles (38.3 ms at 333 MHz)`. On b30's flight its replay alone is 70-79 ms a frame [M] (lab findings `me_hw_counters`). R top-down removes the replay's model share, the room any further ME work needs.
- **Earlier (superseded): the ME was idle ~94% of the time before render jobs, and 9–14% busy with sound, slant sort and rays.** [M] `big_levers.md:135` — `idle ~94% of the time`; [M] `plan_m6_limits.md:11` — `It is 9-14% busy today`. Both predate the ME's drawing replay; Sheepdog01 (2026-11-10 00:52) caught the stale figure.
- **The ME is a frequent binding unit: on d20's heavy view the render job takes 66 ms a frame and the GE stalls 23–24 ms waiting for its chunks.** [M] `hw_capabilities.md:42` — `the render job takes 66 ms a frame`.
- **A whole-cache hand-over costs 12–24 µs write-back and 5–32 µs drop plus up to ~46 µs of refills; forty a tick would cost a millisecond.** [M] `plan_node_matrices_me.md:64` — `forty whole-cache hand-overs a tick would`.
- **`mehalt` works: the ME wakes from idle in 22 µs via a kernel call, with no measurable gain.** [M] `me_programme.md:24` — `wakes 22 µs, a kernel call`.
- **The ME restarts after the game's own sleep stop on the PSP-1000** (B41's fix: 5 of 5 dry stops, restarted in 1 ms with its jobs back, the 4 MB partition unchanged) [M] `lab:psp/sierra/findings/measurements.tsv:10443` — `5 of 5 dry stops: the ME halted itself in 1 ms`; **real power-switch sleeps pass** with the ME left off after the wake (b149, PSP-1000: five sleeps) [M] `lab:psp/sierra/findings/measurements.tsv:10436` — `each stopped itself in 14-46 ms`, and with the ME restarted on the PSP-3000 (3 of 4; the 4th a second switch event while parked, a port bug) [M] `lab:psp/sierra/findings/measurements.tsv:10115` — `B41's fix through real sleeps`. The earlier `mewake` failure (b124–125) was before that fix: `me_programme.md:30` — `Fails on the PSP-1000`.
- **The ME has a system-control block of its own: 0xBC100050 holds 0x0F (its start's value), where the main CPU's holds 0xD41D.** [M] `hw_capabilities.md:215` — `Its 0xBC100050 holds 0x0F`.
- **The ME runs in kernel mode with CU0 on (Status 30000002), unlike the user-mode main CPU (Status 60008613).** [M] `layout_plan.md:217` — `The ME is in kernel mode with CU0 on`.
- **A system-call stub reached from ME-run code faulted the ME at start (EPC 08af9cc0, from `psp_step`).** [D]/[M] `capacity_model.md:147` — `EPC 08af9cc0`.
- **`mevis` reads 45–93 KB a call against the ME's 16 KB cache.** [M] `layout_plan.md:95` — `mevis reads 45–93 KB a call`.
- **The ME's I-cache, measured on the ME: 16 KB, 2-way, 64-byte lines, LRU (or FIFO, not random).** Config 0x00000480, PRId 0x31040125. [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:10990` — `16 KB, 2-way, 64-byte lines, 8 KB a way, LRU`.
- **The ME's cycle counter runs at the full 333 MHz (331.36 a µs) but reads 0 until Count is written at the ME's start; a start with Cause.DC set leaves it dead.** [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11406` — `the ME's CP0 count over 1,015,795 us of the main CPU's clock`; `lab:psp/sierra/findings/measurements.tsv:10667` — `Round 3's dead counter was Cause.DC left set after the power cycle`.
- **On b30's flight the ME's job is 29-32% code misses:** its profiler (0xBC400000) read at every model step on R's milestone build gives 47-65 ms a frame, 45-50% waiting on memory, I-misses 2.5-6× the D-misses, and 72-76 cycles of memory wait a miss inside a step. The part chain's steps wait 46-56% (bind + state + lighting 70-72%, convert 84-86%); the level's 28-37%. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:12720` — `Model steps 16.3 / 27.3 / 15.2 ms`; `lab:psp/sierra/findings/measurements.tsv:12721` — `Code misses are 29-32% of the whole job`.
- **The ME's FPU matches the main CPU's (1,281 against 1,291 ns a node product), bit for bit.** [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:9878` — `main FPU 1291.5 ns, main VFPU 487.3 ns, ME FPU 1281.4 ns`.

## ME eDRAM

- **2 MB, reachable only by the ME and DMA; contents do not survive sleep and are rebuilt.** [M]/[D] `layout_plan.md:148` — `Only the ME and DMA reach it`; `Its contents don't survive a sleep`.
- **The ME's stack began at 2,039 KB and nothing below 1,792 KB changed, so the full 2 MB is reachable.** [M] `layout_plan.md:148` — `the ME's stack began at 2,039 KB`.
- **eDRAM latency is 158 ns against 214 ns from main RAM.** [M] `big_levers.md:58` — `158 ns against 214 ns from main RAM`.
- **Default eDRAM map: 0–192 KB left as found; 192–208 recon scratch; 208–256 ME private state; 256–800 free (544 KB); 800–1,456 sound tier (656 KB); 1,456–1,504 AI job data (48 KB); 1,504–2,048 stack (544 KB).** [D]/[M] `memory_ledger.md:83` — `256-800 KB free (544)`.
- **On the PSP-1000, 256–1,984 KB (~1.7 MB) was untouched by the ME's stack; the stack reaches at most the top 64 KB of the 2 MB.** [M] `me_programme.md:117` — `256–1,984 KB, about 1.7 MB`.
- **The eDRAM runs to the full 2 MB on the PSP-1000:** the ME's stack starts at 2,039 KB and nothing below 1,792 KB changed. [M] `lab:psp/sierra/findings/measurements.tsv:10615` — `the eDRAM runs to the full 2 MB on the PSP-1000`.
- **On the PSP-1000, ~1,664 KB of eDRAM is usable for claims apart from the stack.** [D] `lab:coord/handoff/hw_audit_2026-10-30.md:65` — `Usable for claims: ~1,664 KB`.
- **The ME wrote eDRAM at 23 MB/s (172 ns a word) in a word-at-a-time loop.** [M] `hardware_b115.md:139` — `the ME wrote eDRAM at 23 MB/s`.
- **The eDRAM's address is 0 on the ME's side.** [M] `dma_engines.md:109` — `0 on the ME's side`.
- **DMACplus can write the ME's eDRAM exactly from the main CPU's side at 254–256 MB/s (16-byte transfers).** [M] `plan_sound_edram.md:7` — `254-256 MB/s at 16-byte transfers`.

## VME

- **4 processing elements at 2 operations a cycle; a pass costs 72 + 2n cycles trigger to done, a refresh with its flow word in place 56 + 2n; 17–32× the ME's CPU on long streams (~1.29 G ops/s sustained).** [M] `hw_capabilities.md:50` — `4 PEs at 2 operations a cycle`; `lab:psp/sierra/findings/measurements.tsv:11470` — `56+2n`.
- **16,384 words total (8 buffers × 2,048), each 24-bit sign-extended from bit 23: 48 KB of data.** [M] `hw_capabilities.md:50` — `16,384 words of 24 bits`; [M] `vme_capability.md:66` — `Each word is 24 bits`.
- **The VME has no gather/lookup by data value and no recurrence opcode.** [M] `lab:docs/research/vme_nn_scoping.md:9` — `no gather / no lookup by data value`.
- **A context costs ~709 cycles to write; the VME DMAC moves 12–45 cycles a word inbound and 2.7–7.5 outbound beside the CPU.** [M] `hw_capabilities.md:50` — `a context ~709 cycles to write`.
- **13 of 64 opcodes differ on the PSP-1000 (no-ops or zeros there).** [M] `hw_capabilities.md:50` — `13 of 64 opcodes differ on the PSP-1000`.
- **The PSP-1000's odd opcodes, confirmed opcode by opcode with an independent harness:** Dink's vme2-next-level kernels (4 PEs × 256 inputs each), checked against his emulator, pass exactly with `add_ofa` (0x030) as the control, and fail for 0x010, 0x014, 0x018, 0x038, 0x03c, 0x058, 0x05c, 0x08c (8 constant pairs) and 0x098 (5 constants), each passing one operand through. The toolkit's `--compat fat` marks only 0x010 not portable. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:12719` — `add_ofa (0x030) x7 controls: exact`.
- **Round 2: the saturator and the secondary ALU behind each PE are free — 4 ops a cycle.** [M] `hw_capabilities.md:50` — `4 ops a cycle`.
- **All 32 unit tests give the same words on both PSP models.** [M] `vme_capability.md:140` — `all 32 unit tests`.
- **The element-wise stream at 0x310 runs 6.9 cycles a result (2.4× the ME's CPU), 0.53 resident; a context load from eDRAM is 866 cycles; pipelined switches cost ~150 over a 400+ pass.** [M] `lab:coord/handoff/vme4r_2026-11-04.md:13` — `6.9 cycles a result (2.4x the ME's CPU)`.
- **The lock is owner-only and costs 118 cycles a pair; the main CPU needs a kcall (17.1 µs).** [M] `lab:coord/handoff/vme4r_2026-11-04.md:13` — `The lock is owner-only, 118 cycles`.
- **The texture OR runs 5.56 cycles a pixel main RAM → main RAM at 0x310.** [M] `lab:coord/handoff/vme4r_2026-11-04.md:13` — `5.56 cycles a pixel`.
- **The accumulator is 64-bit, shifted, not rotated.** [M] `lab:coord/handoff/vme4r_2026-11-04.md:13` — `The accumulator is 64-bit`.
- **The DMAC sits at 0x440ff000 with 0x010 address, 0x014 count−1, 0x018 buffer word, 0x008 control; it never slows the main CPU's misses.** [M] `vme_capability.md:87` — `Its registers, as his samples use them`; [M] `vme_capability.md:105` — `The DMAC never slows the main CPU's misses`.
- **The firmware's colour-space converter (VME) turns 4:2:0 planes in ME eDRAM into a 480×272 8888/565 frame in 0.56 ms (930 MB/s); it costs the CPU's misses ×1.09 and a GE texture list ×1.26.** [M] `hw_capabilities.md:56` — `It costs the CPU's misses ×1.09`.
- **The VME computes on the PSP-1000, with no firmware call:** mcidclan's 4×4 sample unchanged gives 4 of 4 vectors exact against the VFPU, and stays exact with every firmware table word pointed at the port's own code (round 5). The datapath window at 0x440f8000 is write-only by design: reads return other state. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:10895` — `4 | of 4 input vectors exact against the VFPU`; `lab:psp/sierra/findings/measurements.tsv:10900` — `NO ME FIRMWARE CALL`; `lab:psp/sierra/findings/measurements.tsv:11551` — `the window is write-only: its reads return other state`. Rounds 1–4's zeros were the start sequence (`layout_plan.md:279` is stale).
- **The VME's DMAC is a mover that never slows the CPU (110 MB/s from eDRAM, 28–36 from main RAM); a staged main→eDRAM→VME pipeline gives 88 MB/s with the ME free.** [M] `hw_capabilities.md:62` — `no cost to the CPU's misses`.
- **Moving and computing are serial:** every write into the buffers during a pass is lost, DMAC reads during a pass fail, and a context loaded during a pass waits for the flow word. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11517` — `ME stores in a pass landed 0 of 11-12 in each of 8 buffers`; `lab:psp/sierra/findings/measurements.tsv:11518` — `reads in a pass fail too`.
- **Units in series are free:** a pass of 1,024 costs 2,107 cycles at every depth 1–4; each stage adds 3 cycles of write delay; each PE can run its own address units and count. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11558` — `2107 | cycles a pass of 1,024 at every depth`; `lab:psp/sierra/findings/measurements.tsv:11550` — `each PE's own address units run their own stream and count in one pass`.
- **Contexts pipeline:** a context load (866 cycles from eDRAM) hides behind any pass of ~400 elements or more. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:11583` — `the load hides behind any pass of ~400 elements or more`.
- **The ramp opcode 0x00260000 is (A·(i+1) + B) >> k, exact except for negative B with k > 0.** [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:12189` — `gives (A*(i+1) + B) >> k`.
- **A secondary unit's buffer input arrives 3 words ahead on the PSP-1000:** read a secondary's words with that skew. [M, PSP-1000] `lab:psp/sierra/findings/measurements.tsv:12191` — `a secondary's front input (top or base) arrives 3 words ahead`.

## Memory Stick

- **A random 4 KB read costs ≈1.75 ms.** [M] `psp_hardware_reference.md:23` — `Random 4 KB read ≈ 1.75 ms`; [M] `audio.md:76` — `1.75 ms per random 4 KB`.
- **In play the stick gives ~2 MB/s against 16 MB/s for large reads; reads are small (5–13 KB on average) and each pays ~2 ms of fixed cost.** [M] `hw_capabilities.md:152` — `~2 MB/s, against 16 MB/s for large reads`.
- **Async reads are off by default; with them on, the texture cache's synchronous reads waited behind them (2.5–5 s frames); the bench shows async reads work if the I/O priority sits above the game thread's.** [M] `hw_capabilities.md:114` — `off by default`; `2.5–5 s frames`; [M] `lab:coord/handoff/hw_audit_2026-10-30.md:72` — `any streaming runs at an I/O priority above the game thread's`.
- **Every thread's calls together kept the stick busy 52 s of about 12 minutes (~7%), loading included.** [M] `streaming.md:9` — `kept it busy 52 s of about 12 minutes`.
- **The stick serves one call at a time, and the port's lock makes threads take turns.** [M] `streaming.md:28` — `The stick serves one call at a time`.
- **A 64 MB/s sequential figure seen in the emulator is meaningless there.** [M]/[U] `psp_hardware_reference.md:23` — `meaningless there`.
- **USB host file copy on the rig ran ~6 MB/s (125 files, 2.7 GB in 450 s) — the rig's I/O, not the stick in play.** [M] `lab:docs/infra/hardware_rig.md:96` — `about 6 MB/s over usbhostfs`.

## Audio hardware

- **sceSas mixes VAG in hardware, 32 voices, which removes the CPU ADPCM decode.** [M] `asset_baking.md:208` — `sceSas mixes VAG in hardware (32 voices)`.
- **VAG is 16 bytes per 28 samples plus a 16-byte end frame.** [M] `asset_baking.md:208` — `16 B per 28 samples`.
- **The mixer outputs 22,050 Hz in 512-frame blocks, a 23.2 ms block.** [D] `job_graph.md:47` — `512 frames at 22,050 Hz`.
- **The audio job has a hard 23.2 ms deadline; a 40 ms limit stops the ME for the session and moves mixing to the main CPU.** [D] `job_graph.md:47` — `a 23.2 ms block`; `40 ms`.
- **The audio job is ~0.7 ms of ME time, but the audio thread waits 0.9–2.4 ms a block and looks ~10 times a block.** [M] `design_mepoll.md:14` — `The audio job is ~0.7 ms of ME time`.
- **Stereo sounds become two panned mono voices on the PSP.** [M] `asset_baking.md:208` — `Stereo sounds become two panned mono voices`.

## Display and timing

- **The frame is two 16-bit 512×272 colour buffers + 16-bit depth = 816 KB.** [M] `psp_hardware_reference.md:20` — `two 16-bit 512×272 buffers + 16-bit depth = 816 KB`.
- **The GE outputs a 480×272 8888 or 565 frame.** [M] `hw_capabilities.md:56` — `a 480×272 8888 or 565 frame`.
- **The port requests CPU 333 / bus 166 MHz via `scePowerSetClockFrequency`; the CPU measures 329.6 MHz.** [D] `lab:coord/handoff/hw_audit_2026-10-30.md:56` — `scePowerSetClockFrequency(333, 333, 166)`; `CPU clock yes: 329.6 MHz`.
- **The GE's own finish costs ~1.6 ms a frame on d20; the frame end's wait for the GE is 0–2.0 ms.** [M] `cpu_cost_map.md:23` — `The GE's own finish is ~1.6 ms a frame`.
- **The game's frame throttle busy-waits on the vertical blank, and a core burns while it waits.** [D]/[M] `for_other_ports/game_bugs.md:18` — `loops on the blank count`.
- **The frame end's whole-cache write-backs run 97 a second (1.9 ms) and the drops 21 a second (0.33 ms): ~0.28 ms a frame.** [M] `lab:coord/handoff/render_ownership_2026-10-27.md:11` — `97 a second (1.9 ms)`.

## PSP-1000 vs PSP-3000

- **The two PSPs agree within 1% on the DMA bench cells, and GE vertex rates are equal to 0.1% on every case.** [M] `big_levers.md:79` — `the two PSPs agree within 1%`; [M] `hw_capabilities.md:84` — `equal to 0.1% on every vertex case`.
- **VME: 13 of 64 primary opcodes differ on the PSP-1000 (no-ops or zeros) while all 32 unit tests agree on both models.** [M] `hw_capabilities.md:50` — `13 of 64 opcodes differ on the PSP-1000`; [M] `vme_capability.md:140` — `all 32 unit tests`.
- **VME DMAC eDRAM paths match to within 0.3% on the Slim, but its main-RAM paths are slower: in 91,888 cycles (44.9 a word) vs 24,817 on the 1000.** [M] `vme_capability.md:103` — `The main-RAM paths are slower on the Slim`.
- **The eDRAM paths match to the cycle (in 255 … 24,814, out 109 … 5,557); main RAM inbound on the Slim is ~165 + 46.6 a word (28 MB/s).** [M] `dma_engines.md:66` — `the eDRAM paths match to the cycle`; `Main RAM inbound is slower on the Slim`.
- **DMACplus on the PSP-3000: 4 KB in 42–43 µs (234–250 MB/s), burst 0 289–291 µs, 8 B wide 128 µs, eDRAM→main 62 µs (264 MB/s).** [M] `dma_engines.md:125` — `4 KB in 42–43 µs`.
- **The fast VME DMAC's cost to main-CPU misses (×1.33) was measured on the PSP-3000 only.** [M, PSP-3000] `dma_engines.md:11` — `a main-CPU miss goes 227 → 303 ns`.
- **The PSP-1000's VFPU corrupts data on unaligned `ulv.q`/`usv.q`; the PSP-3000 is not named in that bug.** [M] `psp_hardware_reference.md:16` — `PSP-1000 silicon bug: unaligned`.
- **The two PSPs draw b119 and b120 identically.** [M] `hardware_b120.md:82` — `draws b119 and b120 identically`.
- **The PSP-3000's second 32 MB reaches the game only with ARK-4's extra-memory option set:** a MEMSIZE=1 build (PSP_BIG_RAM=1) saw the standard user partition and crashed at load without it; with the option at its maximum it started with 37,604 KB free (the standard build: 13,012). [M, PSP-3000] `lab:psp/sierra/findings/measurements.tsv:12613` — `got the standard user partition`; `lab:psp/sierra/findings/measurements.tsv:12614` — `started with 37,604 KB free`.

---
- **The PSP-3000's misses are cheaper in cycles** (44–59 against the 1000's 55–71), so the 3000's cycle counts stand in for the 1000's only as shares. [M] `lab:psp/sierra/findings/measurements.tsv:2487` — `The PSP-3000's misses are cheaper than the PSP-1000's`.
- **The VME's datapath is identical to the cycle on both** (passes, contexts, counter); only its main-RAM DMAC paths are slower on the Slim (46.6 against 37.2 cycles a word). [M] `lab:psp/sierra/findings/measurements.tsv:11407` — `THE SAME AS THE PSP-1000 (41903916684) to the cycle`; `lab:psp/sierra/findings/measurements.tsv:11466` — `~165 + 46.6 a word (28 MB/s; the PSP-1000 37.2)`.

## Settled (were open in the records; the answer is in the section named)

- **Bus arbiter:** no knob exists; closed by a register snapshot and the DDR driver's disassembly (Bus and contention, 2026-11-04/07).
- **Bus clock:** the clock registers read 333/166 as set (Bus and contention, 2026-11-07).
- **A direct GE-bus counter:** none; 0x50 reads 0 under load (Bus and contention, 2026-11-07).
- **The VME on the PSP-1000:** computes exactly, with no firmware call (VME, round 5, 2026-11-01).
- **The ME after a sleep:** restarts after the game's own stops on the PSP-1000; real sleeps pass (Media Engine, 2026-10-29).
- **The eDRAM's extent:** the full 2 MB on the PSP-1000 (ME eDRAM).
- **The ME's I-cache geometry:** 16 KB, 2-way, 64-byte lines, LRU (Media Engine).
- **GE lists from VRAM:** sub-lists execute from VRAM, the cheapest placement (GE).
- **Whether the two CPUs slow each other:** by load: latency-bound ME work doesn't, scattered ME misses cost the tick 10–11% (Bus and contention).
- **The FPU's latencies:** measured (Pipeline and scheduling, 2026-10-27).

## Still open

- **The integer pipeline's latencies (load-use, multiply, divide) are not measured; the FPU's are (Pipeline section).** [U] `lab:coord/handoff/hw_audit_2026-10-30.md:84` — `Pipeline itself`.
- **No cache-lock opcode is named in any source; 0x0F/0x1F are guesses, and the I-side fill (0x0E) faults in user mode, so a lock probe needs kernel mode.** [U] `redesign_tick_target.md:270` — `No source names a lock op`.
- **The GE's 8 KB texture cache is assumed LRU and widening; only its consequences are measured.** [D]+[U] `lab:coord/handoff/hw_audit_2026-10-30.md:59` — `no` directly.
- **The 4 MB volatile partition is verified on lock and latency, unchanged across the game's own stops; across a real power-down it is untested directly (real sleeps pass, b149).** [M]/[D] `lab:coord/handoff/hw_audit_2026-10-30.md:53` — `Loss on real sleep: no`.
- **The write buffer's / no-snooping model has never been benched directly.** [D] `lab:coord/handoff/hw_audit_2026-10-30.md:50` — `Not a bench of the *write buffer* itself`.
- **A DMACplus fill into eDRAM while the ME is busy hung the PSP-3000 at b30's opening; the cause is unproven.** [M]/[U] `plan_sound_edram.md:17` — `hung the PSP-3000 hard`; `the likeliest cause`.
- **13 of 64 VME opcodes differ on the PSP-1000; every one is a no-op or zero there (none gives a different answer), but why is unknown.** [M] `hw_capabilities.md:50` — `no-ops or zeros there`.
- **`pthread_cond_wait` polls in pspdev's pthreads; the scheduler's priority behaviour on waits is measured only in passing.** [D]/[E] `psp_hardware_reference.md:113` — `polls condition variables`.
- **Async stick reads: the priority rule holds for the bench, but was not re-measured for each port thread.** [M]/[U] `lab:coord/handoff/hw_audit_2026-10-30.md:72` — `not re-measured per port thread`.
- **The ME's D-cache geometry** (its I-cache is measured: Media Engine section).
- **The bus clock, timed:** the clock registers confirm 333/166 as set (Bus and contention); no bench times the bus itself.

## Left out on purpose

Game-data sizes and level budgets (`asset_baking.md` MB figures, `campaign_census.md`, `geometry_streaming.md`), per-map/per-stretch frame and tick milliseconds (`hardware_bNNN.md`, `frame_timeline.md`, `capacity_model.md`, `bake_census.md`), port buffer and heap sizing (`psp_capacity.h`, `memory_ledger.md` heap rows, `layout_plan.md` §7), build/PGO/layout results (`order_file.md`, `handoff_2026-10-16_master.md` #5), gameplay bugs and the port's own levers (`for_other_ports/game_bugs.md`, `optimization_ranking.md` lever rows), WiFi channel/ping measurements (`psp_wifi_hotspot.md`), and the port's job-graph timeouts (`job_graph.md` rows other than the 23.2 ms/40 ms audio figures). These are port or game facts, not PSP hardware behaviour; they live in the docs named.

