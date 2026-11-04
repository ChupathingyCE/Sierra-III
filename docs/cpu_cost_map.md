# The main CPU's cost map

Where the main CPU's time goes, the rules the hardware sets, and how far each way of cutting the cost has got. **Read this before proposing a speed lever**, alongside the lab's `tools/levercheck.py` history for the lever's ground. A proposal names the line of section 4 it attacks, that line's ceiling, and what's different from the tries already listed there.

Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or docs, [U] inference. Each figure gives its source, so it can be checked and replaced. Section 6 says how this page is kept current.

Started 2026-11-04 (Sheepdog01: "we need to use it to guide our steps so we aren't firing into the dark every time we try a new lever").

## 1. Where the time goes

**The main CPU binds the heavy maps.** Its cycles fall into three phases, each either working or asleep [M, tock159 ladder, PSP-1000, lab sierra-hw 41821725723, the `hardware by phase` lines]:

| Map | Tick | Drawing | ↳ drawing asleep | Other | ↳ other working |
|---|---|---|---|---|---|
| b30 | 58% | 36% | 3% of the clock | 5% | 5% |
| c20 | 53% | 35% | 7% | 12% | 11% |
| d20 | 46% | 44% | 10% | 10% | 10% |
| a50 | 48% | 42% | 2% | 10% | 9% |
| c10 | 37% | 55% | 2% | 9% | 8% |
| a30 | 23% | 67% | 16% | 10% | 9% |

- **Tick:** pure CPU work; asleep ~0%.
- **Drawing asleep:** waiting on the ME's replay or the GE. Its cause by map is being measured (the lab's recon session, 2026-11-04).
- **Other, working:** outside both phases (threads, firmware calls, glue). No instrument names it yet; `recon/pcother` is building one.
- On the light maps (b40, c40, d40, a10), "other" is 33–50% of the clock and mostly asleep: waiting for the display refresh, which is headroom.

**Inside the work, memory dominates.** The tick waits on memory for 54–68% of its own cycles (b30 63%, c20 68%), drawing for 47–60% [M, same lines]. The tick's misses are all cached ones; its uncached accesses are ~50 a million cycles. On b30 that's roughly 11 ms of an 18 ms tick spent waiting [U, the share times the tick].

**The tick's misses, per million of its cycles** [M, same lines]:

| Map | I-misses | D-misses | Dirty writebacks |
|---|---|---|---|
| b30 | 3,749 (58%) | 2,702 | 895 |
| c20 | 5,211 (68%) | 2,475 | 962 |
| d20 | 3,541 (56%) | 2,804 | 871 |
| a50 | 3,296 (54%) | 2,829 | 903 |

The split is roughly 60/40 by count. A writeback adds to the cost of the data miss that caused it, so by cost the two sides are close to even [U]. **Both deserve equal attention.**

## 2. The machine's rules

| Rule | Figure | Source |
|---|---|---|
| Caches | 16 KB I and 16 KB D, 64-byte lines (256 each), 2-way (8 KB a way), write-back with a write buffer, no L2, nothing snoops | `psp_hardware_reference.md` §1; `research/research_icache.md` (2-way) |
| A miss | ~73 cycles for a random line, ~57 for a sequential one | [M, halo_bench v7, v9] |
| No overlap | The core is in-order: it stalls at the missing load (only ~10 cycles of work slip under a miss), never has two misses in flight, and has no hardware prefetcher | [M, v7, v9] |
| Prefetch, data | `cache 30` (Fill, 0x1E) is a real, non-blocking prefetch. It pays only with ≥ ~70 cycles of independent work before the use. While a fill is in flight, every other load and store waits for it, and fills don't overlap each other. There's no MIPS `pref` (`__builtin_prefetch` compiles to nothing) | [M, v7b, v7c, v9]; `prefetch_map.md` |
| Prefetch, code | None from user code: `cache 0x1E` fills no I-cache line, and the I-side op raises "coprocessor unusable" in user mode | [M, IPREFETCH, 2026-11-02, lab sierra-hw 41571045909] |
| Whole-line writes | `cache 24` (create dirty exclusive, 0x18) makes a line without reading it: half the cost of writing whole lines. Lines not wholly written hold garbage | [M, v9: 1 MB 6,067 → 3,116 µs] |
| Scratchpad | 16 KB, always hits, costs the D-cache nothing, reachable by the CPU and the GE. Uncached, code runs from it at 14.3 cycles an instruction | [M, v9; CODESPAD] |
| The GE shares the bus | CPU misses take +65–96% longer while the GE reads textures from RAM, and +272% while it reads vertices from RAM. Reads from VRAM leave them unchanged. DMA running alongside: 218 → 499 ns | [M, v8b, v7]; `memory_traffic.md` |
| The ME is a second CPU | Its own 16/16 KB caches. Its misses cost the same, and the two CPUs' misses don't slow each other (latency-bound work), so work on the ME roughly doubles the misses the machine serves at once | [M, v10] |
| The compiler | GCC has no Allegrex pipeline model: it won't move loads early to fill a window | [D, `psp_hardware_reference.md` §8] |

## 3. Why the tick misses

- **Capacity, not conflicts.** A fully associative cache would remove only 16–18% of the tick's I-misses [E, `cache_work.md`], so ~82–84% are capacity. The tick's hot code is ~70 KB, fetched again 47–75 times a tick [E, same].
- **The big updates are bigger than the cache on their own** [E, `cache_work.md` §1, c20]: one `object_update` call runs 365 code lines (78 calls a tick), `biped_update` 299, `unit_update` 68 (384 over its 29 calls). The cache holds 256 lines.
- **Data misses by region** [E, `cache_work.md`, b30]: game state 37% (the object pool alone 16%), the tag window 32%, program globals 19%, thread stacks 10%, heap 2%.
- **By function:** `collision_bsp_test_vector_recursive` 6.6%, `object_get_and_verify_type` 4.9% (TYP took it), `psp_bsp3d_test_sphere` 4.6%, `cluster_partition_get_next_datum` 3.1%, `object_compute_node_matrices` 2.5% [E, same].
- **The exchange rate:** 1% fewer tick I-misses ≈ 0.3% of the tick [M against E, PGO in b116; `research/research_icache.md` §0].

## 4. The ways to cut it, and how far each has got

| Way | What it attacks | Ceiling | Where it stands | What would open more |
|---|---|---|---|---|
| **Code packing** (layout, ordering) | I conflicts | ~16–18% of I-misses [E] | Mostly taken: TGC and HOTG are on by default. Squeezing further lost on the PSP: HG tick +1.7–3.9%, FH +2.2–4.4% [M] | Little: what's left is capacity |
| **Smaller code** | I capacity | — | Asserts out (X10) −6.6% of b30's tick [M]. `-Os` loses: all hot code tick +30% (`research/research_icache.md` §0), the big tick functions +1.0–1.4% (CI2), the ME replay (OSREPLAY, REPLAYOS) [M] | Removing code that runs, not compiling it smaller |
| **Doing less work** (reuse, skipping) | Both | Lever by lever | The tick's exact passes are taken: FVP, VISLAZY, LD1, PSC, TP1; drawing's reuse: RSO, LIGHTREUSE. Open: P1 (armed bipeds' node matrices), P2 (non-collideable reconnects) [`tick_passes.md`, branch `research/tickpasses`] | Each new pass is small; their sum matters |
| **Phasing / stage-major** (one system over all objects, then the next) | I capacity, paid for in data | Drawing: slice 2's census ceiling −23% drawing I-misses, slice 3's −19 to −26% [E] | Drawing slice 2 is in: −8 to −12% drawing [M]. Slice 3 is rejected as built: drawing +0.9–2.7% [M], because the code that still runs between parts stayed (DRAWPART3). Tick: big functions phased whole can't stay resident (§3). Node-matrix batching lost 34% on c10 (nwbatch) [M]. FISSB (split inside biped/unit update) parked 10-31 | A shape whose code between items truly fits; the data price of staging counted from the start |
| **Data density and layout** | D | Region by region | HOTG (globals) is on; TYP (one object-lookup load) [M neutral]; STACKHOT on | The object pool (16% of D-misses), the tag window (32%) and the cluster walk: unscored |
| **Scratchpad** | D (hot tables) | 16 KB | SCRATCHBSP in (small win [M]); PROFHEAP no gain; CSTPOOL null; CODESPAD rejected; SPX (time-shared) sized at ~0.15 ms [E], not free | A table hot across the whole tick that fits |
| **Data prefetch** (`cache 30`) | D latency | As the code is: 0.2–0.4 ms a tick at every site; in reshaped loops or next-item pipelining: 0.8–2.6 ms a tick [E, `prefetch_map.md`] | Placed by hand: microseconds (pf123b/c). NPF (node walk) exact but a loss in the model, +0.05 to +0.26 ms. PX open [E] | Loops reshaped so a fill has ~70+ cycles of miss-free work to hide behind: a redesign's tool, not a patch's |
| **Whole-line writes** (`cache 24`) | D writebacks (~900 a million cycles) | Half the cost of each line written whole [M bench] | In drawing's list writes. Node blocks considered and not built (lines shared by two nodes; ≤ ~0.2 ms) [E, `cache_work.md` §2] | Buffers written whole each tick, laid out on line boundaries |
| **Work moved to the ME** | Main CPU time (a second miss in flight) | — | Audio, the drawing replay, AI (AIME 2a), point physics, visibility, sound copies | Measured fit, both directions (CLAUDE.md, placement) |
| **Keeping the GE off RAM during the tick** | Miss cost (+65–272% under RAM reads) | Up to doubling of every miss | Counting the main CPU's lists only, GE busy 2–5% of the clock on b30, c20, d20 [M, 41821725723]. With the ME's lists timed (`gebusy2`), d20's heavy view: **GE busy 41 ms a frame, 55–57% of the clock**, 23–24 ms of it stalled waiting for the ME's next chunk; at least ~11 ms a frame of its RAM reading falls inside the tick [U on M, recon, sierra-hw 41893041680]. **Split by the main CPU's phase** (`recon/gephase`, PSP-3000): with frames in flight the GE reads through nearly the whole tick: d20's heavy view 27–28 ms of a ~30 ms tick (41896481721), b30's flight 84–89 of its 85–89 ms of GE time (41896511812) [M; the instrument's interrupts slow b30, so read the split, not the ms] | The tick's misses run against the GE's traffic almost always, so a miss costs up to twice the bench's 73 cycles. Each GE read moved to VRAM (vertices, index and display lists), or each RAM-heavy list timed away from the tick, cheapens every tick miss |

## 5. What the record teaches

- **The cache model undersizes cache wins on the PSP**, 2–3× for code size and layout: X10 scored −1.8% in the model and measured −6.6% [M]. Cache work is priced on the PSP (the lab's CLAUDE.md).
- **But a split's ceiling overstates what a restructuring gets.** Giving code its own cache shows the most grouping could save, not what a given shape reaches. Node-matrix phasing scored −11.5% and lost 34% on the PSP. Slice 3's ceiling was −26%, and it got −1.4% I-misses and a loss [M]. A ceiling says the prize exists; a built shape in the cache model is the next check, and the PSP the last.
- **Staging costs data.** Each stage writes its results out and the next reads them back. With I and D near even in cost, a restructuring has to price both from the start (slice 3).
- **Small fixes to scattered misses don't add up.** A miss at a site with no window can't be hidden. What pays is changing the shape of the work: fewer distinct lines per pass, loops regular enough to prefetch, buffers written whole.
- **Where things point:** packing is near its ceiling, prefetch is a hand tool that needs reshaped loops, and reuse comes in small steps. The large remaining ground is restructuring the tick around the cache, with its data price counted. That is why the rebuilds (the drawing rebuild, the ME scheduler, the tick) keep coming up.

## 6. Keeping it true

- **Who updates it:** the session that reads a result changing a figure here updates this page in the same turn as the findings (the figure, its source, the date). A lever that lands, is rejected or is shelved updates its row in section 4.
- **When it's checked:** at each tick build, the master re-reads section 1 against that build's ladder and section 4 against `levers.tsv`, and dates the check here.
- **What it isn't:** the detail lives in the pages it cites (`prefetch_map.md`, `cache_work.md`, `research/research_icache.md`, `memory_traffic.md`, `draw_rebuild.md`, the lab's findings). This page keeps the summary and the ceilings; when they disagree, fix this page from them.

Last checked: 2026-11-04 (tock159's ladder; DRAWPART3's pair; d20's gebusy2 run; gephase's split on d20 and b30).
