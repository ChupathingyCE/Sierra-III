# Halo CE → PSP-1000: feasibility, budget and plan

Status as of 2026-10-01 (overnight update). Every figure says whether it was **measured** (on a desktop's Linux build or on the PSP-1000 itself), **documented** (firmware source, SDK, emulator source, or developer reports), or **estimated**.

## Goal and targets

- **Platform:** Sheepdog01's PSP-1000 (CFW, ~24 MB user RAM). A Slim runs the same build with more cache headroom, but the CPU is the same.
- **Frame rate:** **30 Hz simulation and 30 fps is the target.** 20 Hz with 20 fps is the divisible fallback (3 vblanks per frame, so no interpolation needed). Every performance analysis shows both columns.
- **Clock:** 400 MHz, using Sheepdog01's per-game overclock (stable on his unit), with 333 MHz as a fallback mode.
- **Source:** the halo-ce-universal port of the Halo CE Xbox decomp (cybersecurity/halo-ce-universal), plus Sheepdog01's NTSC disc maps (build 01.10.12.2276).

## How it was measured

| Bench | What it is | Caveats |
|---|---|---|
| Linux port on a desktop | `tickrate` branch build, headless (null renderer, hidden window, no audio), `b30` auto-loaded, no player input | Debug build (asserts and profiler compiled in); 128-player object pools; one scene |
| perf | Call-graph attribution to `game_tick` and `render_frame`; 40 s vs 100 s differential runs; L1-miss sampling; uprobes for exact counts | x86 to MIPS translation factors are estimates |
| `halo_bench` v1–v3 on the PSP-1000 | Clock check (differential add chain), VFPU vs FPU, trig variants, memory latency | Microbenchmarks, not Halo code |
| `halo_bench` v4–v6 | Media Engine bring-up (libme-core), ME memory latency, handoff cost, DMA | v4/v5 logs cut short by a missing file close; fixed in v6 |
| `halo_bench` v7 (run 2026-10-01, stock 333; stopped at the first long kernel-mode loop) | Cache ops `0x18`/`0x1E`, miss overlap, I-cache misses, volatile RAM, VRAM, Memory Stick, bus contention, hardware profiler | See "v7: what each result decides" |
| `halo_bench` v8 (built, not yet run) | The GE: fill rate, texture source and format, vertex formats, BBOX and CALL costs, CPU cost of building lists, bus sharing between GE and CPU | See "v8: what each result decides" |
| PPSSPP headless (built in Claude's workspace) | Runs v7's user-mode sections end to end; all data checks pass | Emulator timing is roughly one cycle per instruction: good for instruction counts, not for memory behaviour. No Media Engine, so the ME and kernel sections need hardware |
| psp-gcc compile-only trial | Every game source compiled for MIPS; double-precision call sites counted per function and per source line | Static counts, not runtime counts |

The cost per tick comes from the 40–100 s differential window, so it's **pure post-landing beach combat** (the intro cinematic is about 30 s). Breakdowns and ratios come from single runs and are **mixed load** (cinematic plus combat).

## Memory budget (measured from map files and structs; projections estimated)

The Xbox reserved a **22 MiB window for tags plus the active BSP** at `0x803A6000`. Bungie filled it to within 0.2 MiB (`d40` uses 21.86).

| Item | Campaign (`b40`-class) | Multiplayer (Blood Gulch) |
|---|---|---|
| Static code + data (PSP build measured: 7.4 MB as compiled; ~3.5 MB with Xbox limits and the debug trims, see "PSP build variants") | ~3.5 MB | ~3.5 MB |
| Game state at Xbox limits | 3.3 MB | ~3.3 MB |
| Tags after bake (raw 13.5–16.2 / 7.0) | ~7.8 MB | ~4 MB |
| BSP floor (collision compacted + vis + render) | ~2.7 MB | ~0.6 MB |
| **Left for texture/sound caches, display lists, heap (need 4–5.5)** | **~6.5 MB: tight but positive** | Comfortable |

Memory outside the 24 MB user partition (documented; v7 tests the first):
- **Volatile partition, 4 MB at `0x08400000`** (`sceKernelVolatileMemLock`, partition 5). Production use: DaedalusX64, Xash3D (as a scratch allocator). **Catch on the PSP-1000: it is the firmware's suspend save area** for the ME's and GE's eDRAM (2 MB each; uOFW `sceKernelGetMEeDramSaveAddr`), and sleep waits until it is unlocked. So it holds **reloadable data only** (BSP or sound streaming caches), with a power callback that unlocks before sleep and reloads on resume. With it, campaign headroom goes from ~6.5 to ~10.5 MB.
- **Spare VRAM, ~1.2 MB** (estimated). 16-bit color: two 512×272 buffers plus a 16-bit depth buffer = ~816 KB of the 2 MB. Textures kept there come out of main RAM *and* draw ~4× faster (below). At 32-bit color only ~370 KB is left, which is a strong reason for 16-bit color.
- **ME eDRAM, 2 MB**: only the ME can use it (it isn't mapped for the main CPU), so it saves main RAM only for data that **only** the ME reads.

What drives the bake (all measured):
- **Model geometry** (vertex buffers at the front of the tag data): keeping the 2 lowest of 5 LOD slots keeps ~45% (`b40` 4.95 → 2.02 MB). Repacking 32-byte Xbox vertices to 16–20-byte GE vertices gives ~1.3 MB.
- **Animations** are the largest tag group; **rotations are 88%** of frame data. Half-rate keyframes plus 6-byte quaternions take `b40` from 5.02 to ~2.5 MB. Halo's own codec gets 2.2–3.1× on cinematics. Decision: packed half-rate for gameplay animations, the engine codec for cinematics. Gameplay timing comes from first-person frame counts, so the port's PAL pacing pattern (`pal_tags.c`) is the decoder model.
- **Collision BSP** (38–51% of each BSP; it's gameplay, so it isn't decimated): `edges` are 30–43%. Field-by-field 16-bit indices save **35–38% losslessly**; `a30`'s node children need 32-bit.
- **BSP render vertices** are 18–43% of a BSP. There's no duplicate vertex copy; `detail_objects` (0.64 MB on `a30`) can be dropped.

Prior art agrees that **memory decides the PSP-1000 before the CPU does**: both PSP Quake 3 ports need 64 MB and skip the PSP-1000; Insomnia ProQuake gets a 14 MB heap on a 1000 versus 38 MB on a Slim.

## PSP hardware: measured on the PSP-1000

### Main CPU (`halo_bench` v3)

| | Stock 333/166 | Turbo 400 |
|---|---|---|
| Cache hit / scratchpad | 8 cycles (24 ns) | 20 ns |
| **Cache miss to main RAM** | **72 cycles (219 ns)** | **182 ns** |
| Uncached read | 62 cycles (189 ns) | 155 ns |
| double `sin` / `sinf` / VFPU `vsin` | 4,907 / 113 / 44 cycles | same cycles |
| 4×4 matrix multiply: FPU / VFPU | 627 / 78 cycles (**8×**) | same |
| Normalize: `sqrtf` / VFPU | 88 / 42 cycles (2.1×) | same |

- **The turbo raises the bus too**: memory latency drops by exactly the clock ratio, so 400 MHz is a clean 20% across the board.
- **There's no L2**: misses cost the same from 32 KB to 4 MB. (The firmware has L2 code, but it's gated behind a COP0 bit for devkit hardware.)
- `fabs` on a double that started as a float is free (GCC narrows it). **Real double math becomes soft-float library calls** (`__muldf3`, `__adddf3`).
- Clock requests work with the plugin off (222 → 218.7, 333 → 329.1 measured). With the plugin on, it holds the clock and the power service still reports 333.

### Media Engine, DMA and prefetch (`halo_bench` v6, stock 333)

| | Result |
|---|---|
| libme-core on a PSP-1000 | Works ("PSP-1000 image"); the ME ran our code |
| ME clock | 331.5 MHz (follows the main CPU's setting) |
| ME cache hit (8 KB) | 25 ns |
| **ME eDRAM, beyond its cache** | **164 ns cached, 157 ns uncached**: only ~25% faster than a main-CPU miss (222 ns), not a fast local store |
| ME → main RAM, uncached | 187 ns |
| ME → main RAM, cached, 4 MB | 27.7 ns: **invalid, stale ME cache** (a live demonstration that the ME's cache isn't coherent with the main CPU's). v7 re-measures with the cache invalidated first |
| Round trip, main CPU → ME → main CPU | **0.42 µs** (~140 cycles): handing work over is cheap |
| DMA main RAM → main RAM | **181 MB/s, correct**, vs 76 MB/s for a CPU memcpy (2.4×, CPU free meanwhile) |
| DMA into the scratchpad | **Doesn't work**: reports success but the data doesn't arrive |
| `pref` | **Not on the Allegrex**: the assembler rejects it and the hardware halts on it. `cache 0x1E` is the candidate substitute (v7) |

### Misses, code, memory and neighbours (`halo_bench` v7, stock 333)

| | Result | What it means |
|---|---|---|
| **A load that misses** | **Stalls at the load**: 32/64/96 independent adds after it cost almost their full time on top (100/131/164 cycles vs 72 + adds) | No hit-under-miss: work can't hide under a miss |
| **Two misses at once** | 144.5 cycles = 2 × 72 | **No miss overlap**: interleaving two traversals doesn't help. The only remaining way to hide a miss is `cache 0x1E`, if it doesn't block (v7b) |
| Sequential lines | 57.5 cycles vs 72 scattered | ~20% cheaper (DRAM page hits): sorted, streamed arrays pay off a little |
| **Instruction-cache miss** | 17 cycles a 16-instruction line in cache; **72–87 out of cache** | ~55–70 cycles a missed code line: a cost the x86 projection doesn't include |
| Writes | fill 172 MB/s; memcpy 101; VFPU copy 114 | |
| **Double precision** | **`sqrt` 2,390 cycles** vs `sqrtf` 31 vs VFPU 24; `x * 0.1` promoted 920 vs 1; `atan2` 8,947 vs `atan2f` 236 | Real cycles, ~30% above the emulator's instruction counts |
| **Volatile partition** | Locks, 4 MB at `0x08400000`, all data correct, normal latency (219 ns) | **+4 MB confirmed** (reloadable data only) |
| **Spare VRAM as CPU memory** | Miss **132 ns** cached, 126 ns uncached vs 220 ns main RAM | VRAM is **40% faster** for the CPU than main RAM: a candidate home for hot CPU data, competing with textures |
| DMA into VRAM | 323 MB/s, correct | |
| **DMA alongside the CPU** | CPU misses go from 218 to **499 ns** while DMA copies; DMA drops from 181 to 64 MB/s | **DMA is not free**: keep bulk DMA out of the tick |
| **Media Engine as a neighbour** | CPU misses 219–222 ns whatever the ME does (idle polling, eDRAM misses, main-RAM misses, streaming stores) | **The ME costs the main CPU nothing measurable**: a free second core |
| ME memory | eDRAM 158 ns; main RAM 214 ns cached (v6's 27.7 was indeed stale); slows to 211 ns from 185 when the CPU is also missing | |
| **Same float code on both CPUs** | 39.0 vs 38.8 cycles, **bit-identical** results | Raycast-style code runs on the ME at full speed |
| **`cache 0x1E` prefetch, user mode (v7b)** | **Works in user mode and doesn't block**: fill + 64 adds = 76 cycles vs 72 for the adds alone. A load 64–96 cycles after the fill costs 85/111 instead of 131/164: **~80–90% of the miss hidden** | The Allegrex has a usable prefetch. But fills serialize (back to back 72.5 each), and streaming with a fill ahead got *slower* (70 vs 58), which suggests **any memory access waits while a fill is outstanding**. v7c tests that directly; if so, a prefetch only helps ahead of register-only work (maths, VFPU) |
| **`cache 0x18`, user mode (v7b)** | **Fill 4 MB: 172 → 355 MB/s (2.05×); copy: 100 → 146 MB/s (1.46×)** | No kernel mode needed. Use for every full-line write (memcpy, memset, display lists, vertex and bone buffers) |
| **What waits for an outstanding fill (v7c)** | A cached load or store right after a fill: 148 cycles (vs 85 without it); halfway through: 117 | **Any memory access waits for the fill.** A prefetch only overlaps register-only work (integer, FPU or VFPU maths); in typical game code it buys little |
| **Hardware profiler (v7c)** | **Works on a retail PSP-1000**, started and stopped with two short kernel calls around user-mode work. Counts match exactly: 32,768 loads → 32,794 D-misses; 8,192 code lines → 8,219 I-misses | On-device profiling for milestone 4: cycles, memory stalls, I- and D-misses per section of the tick |
| Kernel mode through kcall (v7b) | Short runs work (1,024 loads); a ~14 ms run (65,536 loads) freezes the PSP | Likely an interrupt arriving while in kcall's kernel context. **Keep kernel-mode trips short**; everything the port needs runs in user mode |
| Memory Stick (Sheepdog01's 32 GB card) | Read 16 MB/s (256 KB pieces), 7.9 MB/s (4 KB); write 4.9 MB/s; open + close 6.6 ms; random 4 KB read 1.75 ms | A 15 MB level reads in ~1 s; one packed file per level, large reads |

### The GE (`halo_bench` v8, stock 333)

| | Result | What it means |
|---|---|---|
| Fill, flat colour | 1.66 ns a pixel (~600 Mpixels/s); depth test and blending nearly free | A full 480×272 layer is 0.22 ms |
| **Textures, ns a pixel** | 8-bit CLUT swizzled: **3.4 VRAM / 4.1 RAM**; 16-bit swizzled: 4.9 VRAM / 6.0 RAM; **linear: 7.5 VRAM, 9.9–19.5 RAM** | **Swizzling matters most** (2–3× in RAM); VRAM is only ~20% faster once swizzled. 8-bit CLUT swizzled is the fastest. A full textured layer is 0.45–0.8 ms, so fill isn't a 30 fps limit |
| Vertices, small triangles | 16-bit 30.7 ns, float 32.6, from VRAM 36.6 (slower than RAM), 1 light 54.9 | ~30 M vertices/s: vertex count isn't a 30 fps limit either |
| **Hardware skinning (v8b)** | **~42 ns per weight per vertex**: 1 weight 61 ns, 2 weights 103, 4 weights 188, 8 weights 357, vs 30.7 plain. **Rigid pieces with a matrix every 30 vertices: 49 ns** | Split models offline into rigid pieces (one node, drawn with its matrix); skin only the vertices that blend two nodes, at 2 weights |
| Per object | Draw 0.41 µs; **BBOX test ~1 µs** (1.18 skipped, 1.44 drawn); a CALL adds 0.27 µs | GE-side culling pays only for things bigger than a few dozen vertices: BSP clusters and large objects, not every small model part |
| CPU cost per draw | 62 cycles into a CALL list; 216 with a model matrix; 165 inside BBOX; **266 with GU_DIRECT** | 1,000 draws with matrices ≈ 0.2 M cycles a frame (1.5% of the 30 Hz budget). Never GU_DIRECT |
| **CPU misses while the GE draws (v8b)** | Textures from **VRAM: +0–2%**. Textures from **RAM: +65–96%** (swizzled is worse than linear, because the GE pulls data faster). Flat fill: +2%. **Vertices from RAM: +272% (804 ns a miss)**. **Vertices from VRAM: +0%** (the GE itself is 20% slower reading them) | **The GE's cost to the game is the shared bus, not its own speed.** Whatever the GE reads from main RAM while the tick runs slows every one of the tick's misses, and vertex reads are the worst. **Design rule: the tick owns main RAM; the GE works out of VRAM** |

## PSP hardware: documented (deep research pass, 2026-10-01)

### Allegrex CPU and caches
- **D-cache: 16 KB, 2-way, 8 KB per way, 64-byte lines, write-back** (uOFW cache loops). **Data spaced at multiples of 8 KB competes for the same two lines**, so avoid 8 KB-aliased strides in hot arrays (per-object or per-actor tables with 8 KB element sizes, parallel arrays allocated 8 KB apart).
- **Cache ops**, from uOFW plus retail-game usage recorded in PPSSPP:

| Op | What it does | Use for us |
|---|---|---|
| `0x18` | Create dirty exclusive: allocates the line **without reading RAM** | Destinations that are fully overwritten: memcpy/memset, display lists, vertex and bone buffers. Saves a ~72-cycle read per line. Firmware uses it in its decompressors and interrupt frames |
| `0x1E` | Fill: brings a line in (GTA LCS uses it "a lot") | **The only prefetch the Allegrex has.** Worth a lot if it doesn't block (v7) |
| `0x1C` / `0x1F` | 0x18 / 0x1E with a lock | Pin a hot line (e.g. the top of the collision BSP) |
| `0x1A` / `0x1B` / `0x19` | Hit writeback / writeback-invalidate / invalidate | Ranged flushes before the GE or DMA reads data |

  Retail games issue `0x18`, `0x1B` and `0x1E` from game code, so they're probably allowed in user mode (v7 tests it last, since a privilege fault ends the program). If not, kernel mode is available through libme-core's `kcall` bridge.
- **Pipeline costs** (community tables): integer multiply 5 cycles, divide 36; `add.s` latency 4, `mul.s` 7, `div.s`/`sqrt.s` 28; taken branches ~2–3 cycles. Branch-likely instructions and `min`/`max`/`movz`/`movn` exist for branch-free plane-side tests.
- **Scratchpad**: 16 KB at `0x00010000`, user-accessible, **not saved across thread switches** (the attribute that would save it was never implemented), so one thread owns it.
- **Kernel costs** (hardware-measured by PPSSPP developers): a thread handoff is 7–30 µs (2,300–10,000 cycles), and timed waits round up to ≥205 µs. **No fine-grained job threads**: parallelism comes from the ME, not from threads.
- **Hardware profiler** at `0xBC400000` (kernel mode): cycles, memory stalls, I- and D-misses, writebacks, cached/uncached loads and stores, FPU and VFPU instruction counts. If it works on retail units (v7), it's the on-device profiler for milestone 4.

### VFPU
- Throughput/latency in cycles: `vadd`/`vmul`/`vscl` 1/5; `vdot` 1/7; `vtfm4`/`vqmul` 4/10; **`vmmul.q` 16/22** (vs ~112 FPU ops); transcendentals N/N+6; `vdiv.q` 56/59; `mfv` 6 (keep it out of inner loops).
- Accuracy: `vsin` absolute error < 4.8e-7 (input in quarter-turns), `vrsq` relative < 7.3e-7; the lowest ~3 mantissa bits are unreliable. Fine for gameplay under distributed netcode.
- **PSP-1000 errata**: unaligned loads (`ulv.q` / `lvl.q` / `lvr.q`) corrupt an FPU register, and `vhtfm` writes the wrong register. **Use only 16-byte-aligned `lv.q`/`sv.q`.**
- The VFPU is the main CPU's only; **the ME has an FPU but no VFPU**.

### GE (graphics)
- **Clipping: only against near Z. A triangle with any vertex outside the 4096×4096 guard band is dropped whole.** Quake and Xash ports clip on the CPU with the VFPU every frame; we can **tessellate the BSP offline** so no triangle can cross the band, and skip runtime clipping.
- **Bounding-box culling in the display list** (`sceGuBeginObject`: BBOX + BJUMP): the GE tests a box and skips the object's commands if it's off-screen. ~22 GE cycles per box vertex (~1.6 µs for 8 corners), nestable 32 deep. Shipped games used it (OutRun 2006, Tekken 6).
- **Display lists**: CALL/JUMP/RET; the stall address lets the CPU extend a list while the GE runs it. `GU_DIRECT` makes a syscall after every draw; **use CALL lists or batch stall updates** instead. List writes go through the uncached alias, but **vertex and texture data written through the cache need a writeback** before the GE reads them (the firmware's automatic flush is a one-game patch).
- **Hardware skinning**: up to 8 bone matrices per draw (52 bytes each), 1–8 weights per vertex, 8/16-bit positions, normals and UVs. Halo characters have more than 8 nodes, so models are **split offline into ≤8-bone batches**.
- **Textures**: 8 KB texture cache. Documented as ~4× faster from VRAM than from RAM; **v8 shows that holds only for linear textures: swizzled, RAM is just ~20% slower**. Swizzled CLUT4/CLUT8 is the fast path; DXT expands to 32-bit in the cache and is slow.
- **Lightmaps**: Xash3D's recipe is a second pass with depth test EQUAL, depth writes off, and a multiply blend; lightmaps as RGB332. 16-bit depth only; 4 hardware lights; linear fog.
- **Shared bus**: the GE reads main RAM over the same bus as the CPU. Ready at Dawn: "everything, even graphics, can slow down the CPU". Textures and lists in VRAM keep the GE off the bus.

### Media Engine
- 333 MHz MIPS core with FPU, **no VFPU**, 16 KB I- and D-caches, 2 MB eDRAM on the PSP-1000. Not cache-coherent with the main CPU, and **can't see VRAM**. ME code can't call libc or make syscalls.
- **libme-core on a PSP-1000 is raw-only**: its firmware function table for the PSP-1000 image is largely unmapped (zeros), so calling any `meCore*` function there jumps into nothing. Use our own code plus the `meLib*` helpers that touch hardware directly, as v6 did. The VME (a fixed-point stream-math DSP) samples are Slim-only.
- **Sony's ME firmware is gone once libme-core takes over** (it resets the ME). That takes out the hardware-accelerated codecs and, very likely, **SAS** (`sceSasCore`, Sony's 32-voice synth). So audio means **our own mixer**, on the ME (as DaedalusX64 and SM64 PSP do), writing the audio FIFO directly (`0xBE000000`, libme-core `audio-local` sample) or handing buffers to `sceAudioOutput`.
- **Writing GE display lists from the ME works** (libme-core `gu-shared-list`): the ME fills an uncached double-buffered list, and the main CPU `sceGuCallList`s it.
- Sync: hardware mutex at `0xBC100048`; soft interrupt main → ME at `0xBC100044`; DMACplus channels for bulk copies between ME and main memory.
- **Idle polling costs bus time**: libme-core's shared variables live in uncached main RAM, so an ME spinning on a command word is a stream of bus reads. v7 measures how much that slows the main CPU; the port's ME loop should halt or poll rarely.
- `0xBCC00000`: a background unit that "processes over the eDRAM"; stopping it reportedly speeds ME code (mcidclan). v7 measures before and after.
- Independent benchmark (kan8223, April 2026): both CPUs at 333 averaged 3.5× one CPU at 222, with bit-identical float results.

### Audio and Memory Stick
- `sceAudio`: 8 channels, 44.1 kHz (48 allowed), buffers in multiples of 64 samples (1.45 ms). No published CPU cost for software mixing (to measure).
- Memory Stick Pro Duo: bus ~20 MB/s; real throughput is unknown (a low-confidence quote says 1.2 MB/s read). PPSSPP models a file open at 10–13 ms. Practice: one packed file per level, large aligned reads, `sceIoReadAsync` with double buffering. v7 measures Sheepdog01's card.

## Halo CPU cost (x86 measured; PSP projected)

**Tick:** **5.38 M x86 instructions** per tick.

| Bucket | Instructions | L1 misses |
|---|---|---|
| Object updates | 71% | 57% |
| AI | 27% | **41%** |
| Collision (line-of-sight and projectile raycasts, pill moves) | ~35–40% | (overlaps the above) |
| Skeleton posing (`object_compute_node_matrices`) | 16% (11% self) | <13% (compute-bound) |
| AI perception (line of sight 13%, `prop_status_refresh` 12%) | 17% | **25%** |

- **Miss rate** is 1.4% of instructions, about 75 K misses per tick on x86; on the PSP's 16 KB cache, ~110–150 K (estimated). **At ~64 extra cycles each, that's 7–9.6 M cycles of stalls per tick, roughly half the tick.** This is why prefetching (`cache 0x1E`) and miss overlap are the biggest open questions.
- **Trig in the tick:** ~600–900 double calls (`atan`, `atan2`, `acos`, `cos`, `sin`).

**Frame:** **~3.5 M x86 instructions** per rendered frame (an upper bound, PC GL path).

| Bucket | Share | On the PSP |
|---|---|---|
| Model submission | 45–48% | Mostly replaced by baked display lists and hardware skinning |
| Clock queries | 16–30% | Debug profiler; compiled out |
| Visibility | 15% | Stays (PVS walk); frustum tests move to the GE (BBOX) |
| Shadows | 7.5% | Optional |
| Dynamic triangles | 7% | Mostly stays |

Trig is about 96 calls per frame.

## Compiling the game for the PSP: first look (static analysis, measured 2026-10-01)

A compile-only trial: every game source the Linux build compiles, run through **psp-gcc 15.2** with the Linux build's include paths and defines (in its release configuration), the Android port's portable flags, and two small shim headers.

- **412 of 466 sources compile for MIPS as they stand.** Of the 54 failures, 28 are libtiff (bitmap import, which the game doesn't need at runtime); the rest are GCC-vs-clang dialect points (`ui64` literal suffixes, `#pragma weak` on static functions, a few K&R declarations). The Android port already compiles the game for another 32-bit-pointer ISA, which is the template for milestone 2.
- **Code size: ~2.1 MB of MIPS instructions** for those 412 files, against the 4–5 MB static estimate (which included data). Likely some room back in the memory budget; the linked size will tell.
- **Double precision in the compiled code** (static call sites; each is a software routine on the Allegrex):

| What | Sites | Where it comes from | Fix |
|---|---|---|---|
| `sqrt` (double) | **3,800** | `square_root()` in `real_math.h` returns `sqrt(x)`, inlined into every magnitude and normalize | **`sqrtf`: bit-identical on every IEEE platform** (a correctly rounded double square root, rounded to float, equals the float square root), so netcode determinism is untouched |
| float ↔ double conversions, double arithmetic | 3,171 | `fabs`/`fmod` on floats (e.g. `bsp3d_test_pill_recursive` in collision), double literals, varargs to debug printing | Float versions where hot (`fabsf`, `fmodf`, `f` literals); changes rounding slightly, so applied to every build of our fork |
| double maths library (`halo_sin`, `halo_atan2`, …) | 352 (≈670 after inlining in hot files) | the trig wrappers in `real_math.h` | Lever 1 (float or VFPU trig) |
| `fast_ftol` | 42 in hot files | `(long)rint((double)value)` | `rintf` / `cvt.w.s`: also bit-identical |

  In the hot files (objects, units, vehicles, bipeds, AI moving/looking/combat/perception, projectiles, physics, collision), `square_root` alone is **over half** of all double call sites.

  **What a slip into double costs** (v7's double tests run in the PPSSPP emulator, which counts roughly one cycle per instruction, so these are instruction counts and a floor on real cycles; v7 on hardware gives the real ones):

| Operation | Double | Float | VFPU |
|---|---|---|---|
| `sqrt` | ~1,830 | 5 (`sqrtf`) | 9 (incl. register moves) |
| `sum += x * 0.1` (double literal) | ~670 | 1 | |
| `atan2` | ~6,730 | 133 (`atan2f`) | |
| `x / y` with both promoted | 29 | 29 | |

  GCC already narrows a plain float-by-float operation written in double (the division row), so the costly cases are the ones that mix in a double literal, a double function (`sqrt`, `fabs`, `fmod`, trig) or a double variable. **Measured on hardware (v7): a double `sqrt` costs 2,390 cycles. Each 1,000 square roots a tick left in double would cost ~2.4 M cycles: 18% of the 30 Hz budget at 400 MHz, 12% of the 20 Hz budget.**

  **Why the x86 projection doesn't show this:** clang on x86 already turns `square_root` into a single float `sqrtss` (it knows the narrowing is exact), and SSE does the other double operations in a cycle or two. GCC for the Allegrex does neither: each becomes a software routine. So these are **hidden PSP-only costs on top of the budget model**, and the cleanup is mandatory, like the trig fix. The `sqrtf` change compiles to what the PC build already runs, so it's free and exact. The remaining promotions (`fabs`, `fmod`, literals) can only be weighed on the PSP itself (milestone 4, profiling the soft-float routines).
- **The port's pools are 4–8× the Xbox's** (8,192 objects vs 2,048, 8,192 particles vs 1,024, …) and its object garbage collection thresholds scale with them, so the desktop tick measurement may include work the Xbox never did. The PSP build uses Xbox sizes anyway; the `xbox-capacity` experiment measures the difference.
- **Clean-baseline check (measured, 2026-10-01):** a release build measures **5.53 M instructions a tick**, and the same build with Xbox-sized pools **5.53 M** (±0.04 M from perf's two-decimal percentages). The earlier dev-build figure was 5.38 M. So **neither release mode nor the oversized pools inflated the tick**; the 5.4–5.5 M figure stands. (The dev/release gap is within noise plus upstream changes since the first run.)
- **Assertions are always evaluated in the port**, release builds included (a failed one is logged, not fatal), because a few have side effects. A PSP build could compile out the pure checks (`valid_real_*` and similar); their share of the tick is a quick perf query on the existing data.

## PSP build: compiles and links (milestone 2 in progress, 2026-10-01)

A bring-up harness (`psp_port/build.py` and `link.py` in Claude's workspace) builds the game for the PSP without editing the game's source:
- **All 436 game sources compile with psp-gcc 15.2** (libtiff, a bitmap-import library, left out). The few GCC-vs-clang differences are handled by a build mirror with two textual fixes (`static __inline`, MSVC `ui64` literals) and two small headers (MSVC keywords as GCC attributes, newlib macro clashes).
- **The game links into a PSP module** with 436 stubbed platform symbols: Direct3D (105), DirectSound (35), Win32 files/threads/events, the Xbox kernel, XNet, XInput and Bink. That list is the PSP platform layer's to-do list; the Linux port's `port/linux/src` implements the same surface on SDL and OpenGL.
- **Size of the linked game** (with unused code removed by the linker):

| Section | Size | Notes |
|---|---|---|
| Code (`.text`) | 2.44 MB | Includes the Xbox rasterizer's game-side code, which the GE renderer replaces |
| Read-only data | 0.40 MB | |
| Data | 0.17 MB | |
| Zeroed data (`.bss`) | 4.34 MB | Trimmable: the debug profiler's buffers 1.13 MB, AI debug 0.55 MB, 128-player netcode state ~1.2 MB (client/server update globals, late joiners) |
| **Total** | **7.4 MB**, ~4.4 MB after the debug and 128-player trims | The budget assumed 4–5 MB |

## PSP build variants: memory and code measured (static, 2026-10-01)

Each variant is a full PSP compile and link of the game with one change, measured from the linked binary. These are counts and sizes, not timings.

**Soft-float call sites** (calls into software double routines):

| Build | Call sites | Notes |
|---|---|---|
| As compiled before | 3,850 | 533 double `sqrt`, 204 double `rint`, 1,540 float→double conversions |
| + `single-precision-1.patch` + `-fno-math-errno` + `-fsingle-precision-constant` | **2,136** | Double `sqrt` gone (1 left). Code −25 KB |

- `-fno-math-errno` lets `sqrtf` compile to the single FPU instruction with no library fallback. `-fsingle-precision-constant` makes `x * 0.1` float maths instead of a promote, a software double multiply and a demote. Neither changes anything the game relies on.
- What's left is mostly cold: float arguments promoted for error-message formatting (e.g. 41 in `object_compute_node_matrices`, all in its error paths), colour packing in HUD and Xbox-rasterizer code (`rint((double)c * 255)`), which the GE renderer replaces.
- **The trig wrappers are a header swap on the PSP.** `halo_sin`/`halo_cos`/`halo_atan2` etc. (`port/include/halo_math.h`) are double so every PC port gets bit-identical results for system link. The PSP won't play system link with PCs, so its version can define them as inline `sinf`/`cosf`/`atan2f` (or VFPU); GCC removes the float→double→float round trip around an inline call. That is lever 1.

**Assertions are 16% of the code.** The port's release build still evaluates every assertion (a few have side effects the game needs: `heap_insert` in path_obstacle_avoidance.c, `hs_parse_variable` in hs_compile.c).

| Build | Code (text + rodata) | Hottest 100 / 200 / 400 functions* |
|---|---|---|
| Assertions as now | 2.83 MB | 91 / 196 / 367 KB |
| **Assertions marked cold + `-freorder-blocks-and-partition`** (failure paths moved out of each function) | 2.84 MB | **85 / 179 / 334 KB (−7–9%)** |
| Assertions compiled out (size bound only; unsafe without an audit) | 2.38 MB (−447 KB) | 80 / 162 / 305 KB (−13–17%) |

\*Ranked by the Linux port's own PGO profile (`pgo/halo_linux.profdata`: menu plus the first minute of every campaign level), functions present in all three builds.
- The safe variant keeps every check and only moves failure paths out of line, so hot code packs tighter into the 16 KB I-cache. It needs `-mno-explicit-relocs` (MIPS high/low address halves must stay in one section). The cold parts must stay next to their functions: the conditional branches into them only reach ±128 KB.
- **Even the hottest code far exceeds the I-cache:** the 200 hottest functions are ~180 KB against 16 KB. Code layout (lever 7b) and PGO matter more than the x86 projection suggests.
- **GCC PGO is possible on the PSP:** `-fprofile-generate` writes `.gcda` counts through newlib's file I/O to the Memory Stick, or from a run in PPSSPP, since counts depend only on the code path, not timing. Then `-fprofile-use` splits hot from cold by real counts and orders blocks. The Linux port's numbers may already include clang PGO (it's used when clang is 22 or newer), so the PSP build without PGO starts behind the x86 measurements.

**Zeroed data (`.bss`), 4.34 MB:**

| Change | `.bss` |
|---|---|
| As compiled (128-player limits, native-port pool sizes) | 4.34 MB |
| **Xbox session limits and pool sizes** (`psp_limits.h`: 16 players, 4 machines, Xbox capacities; the netcode records come out at the Xbox's 0x226/0x434 layout) | **3.06 MB** |
| Then: profiler buffers (1.10 MB), AI debug recording (0.53 MB), `render.environment_surface_flags` (0.48 MB, below), lens-flare occlusion per split-screen window (0.19 MB) | **~0.75 MB** (estimated from symbol sizes) |

- **`render.environment_surface_flags` is declared 32× too large:** it's used only as a bit vector (`BIT_VECTOR_TEST_FLAG`, cleared with `BIT_VECTOR_SIZE_IN_BYTES`), but declared as one `unsigned long` per surface: 512 KB where 16 KB would do.
- `local_lens_flare_occlusion_test_results2` holds 64 K markers × 4 split-screen windows; one window, or sizing it from the BSP at load, takes 256 KB down to 64 KB or less.

**Static memory, revised:** code and read-only data ~2.6 MB once the GE renderer replaces the Xbox rasterizer's game-side code (219 KB), data 0.17 MB, `.bss` ~0.75 MB: **~3.5 MB** (the budget assumed 4–5 MB). Campaign headroom goes from ~5.7 to ~6.5 MB in main RAM, plus the 4 MB volatile partition.

## PSP platform layer, first slice (2026-10-01)

The first full PSP build of the game links with **nothing stubbed**: 470 units, a 5.36 MB program (code and read-only data 3.19 MB, data 0.17 MB, zeroed data 2.25 MB) and a 5 MB heap. Approach: **reuse the Linux platform layer** (`port/linux/src`) and replace only what the PSP can't do.
- **Reused as is:** the Win32/Xbox kernel shim (files, threads, events, timers over pthreads, which the PSP toolchain has), the C runtime shims, config, input, audio, and the Direct3D layer, which runs as its built-in null renderer (no OpenGL context, so it draws nothing).
- **Replaced (`psp/src`):**
  - `psp_memory.c`: `XPhysicalAlloc`. Fixed-address requests (game state at `0x092B0000`, tag window at `0x09600000`) are taken from the kernel at exactly that address, as `halo_loadtest` did; everything else comes from the heap.
  - `psp_sdl.c`: the 30-odd SDL3 calls the layer makes, written directly on PSP system calls. The PSP toolchain ships SDL3, but linking it cost **2.4 MB** (1.35 MB of code plus a 1 MB display list for its renderer).
  - `psp_libc.c`: `pread`/`pwrite` and other gaps in newlib; the shared maths functions in single precision (lever 1); stubs for internet play.
  - `psp_platform.c`: the entry point (fixed heap, exit callback, unbuffered log at `ms0:/halo/log.txt`, data root `ms0:/halo`), a 5-second heartbeat that logs game time and ticks per second, and no network yet.
- **Game-source changes (`psp_fixups.py`, each `#ifdef HALO_PSP`, exact-match so upstream changes stop the build):**
  - PSP tag window, texture cache (2.25 MB) and sound cache (1 MB) sizes.
  - **No Z: cache.** Maps are stored uncompressed and a cache slot opens the map file itself. The Xbox scheme would have written ~730 MB of cache files to the Memory Stick.
  - Physical and virtual addresses identical. `0x88000000` and up is the kernel's view of RAM on the PSP.
  - Debug profiler down to 2 frames; `render.environment_surface_flags` sized as the bit vector it is; the null renderer's GL page tables removed.
- **First run (PPSSPP, PSP-1000 memory, 2026-10-01): Blood Gulch loads and the game ticks at a steady 30 ticks/s** for 2+ minutes, with 600 KB of user memory left. Getting there took:
  - a CRT name clash: the port's `_close`/`_read`... replaced newlib's system-call hooks, so `close` recursed forever; renamed at build time;
  - thread stacks: 64 KB minimum instead of the Linux port's 1 MB;
  - the null renderer's back, depth and render-target buffers pointed at VRAM (~4 MB of RAM saved);
  - the AI debug records (6.6 MB per-actor, 3.7 MB path storage; gameplay only writes them) shared as one record on the PSP;
  - debug buffers and the texture cache's map-copy guard shrunk.
  - Texture cache 1.25 MB is too small for the null renderer's blocking requests ("YOU GOT STABBED", Bungie's texture-cache-exhausted message, every 10 s). It doesn't stop the game; the GE renderer will have its own VRAM cache.
- **First run on the PSP-1000 (build 5): stopped about 3 s in, during start-up, on an unaligned store.** `debug_malloc` writes a guard word right after each allocation; for a size that isn't a multiple of 4 that word is unaligned. **The PSP's CPU faults on unaligned word loads and stores; x86 doesn't care, and PPSSPP's JIT forgives them**, which is why the emulator ran fine. **PPSSPP's interpreter (`-i`) reports them** with the exact PC, so it's the tool for finding them. Fix: `PSP_UNALIGNED_ULONG` (a packed-struct access, which GCC turns into the unaligned-safe `lwl`/`lwr`/`swl`/`swr`); the whole start-up now runs clean in the interpreter. Map loading and the tick still need an interpreter pass with game data.
- **Build 5 times every tick and frame** and sets the CPU to 333 MHz (homebrew starts at 222). On the PSP-1000 itself this gives the first measured tick cost. Note that Blood Gulch with one player and no AI is a light tick.
- **Game data on the Memory Stick:** `ms0:/halo/maps/` holds `ui.map` and `bloodgulch.map`, both built with `-X 0x09600000 -T 0x900000` and uncompressed. `ms0:/halo/init.txt` holds the startup command.

## The tag-cache address: Invader as the baker (2026-10-01)

Xbox map files hold absolute pointers into a tag window at `0x803A6000`, which is kernel space on the PSP. Instead of relocating pointers at load time, **the maps are rebuilt for a PSP address**:
- **Invader** (open-source Halo CE map toolkit, GPL-3) extracts tags from Xbox maps (`invader-extract`, which needs `-n` to include a campaign map's globals) and builds `xbox-ntsc` maps (`invader-build`). In its source, each engine's tag base address is one table field (`base_memory_address`).
- **Our Invader fork** adds two build options:
  - `-U` / `--unchecked` keeps stock values the checker rejects. Bungie's Warthog gunner has `grenade_type = 28786`: leftover ASCII bytes the game never reads.
  - `-X` / `--tag-address` builds the tag data for any address, for example a reserved block in PSP user RAM. With `-T` (tag space size), it also sets where the BSP lands (Invader places it at the top of tag space, as the Xbox did).
- **Status: proven on desktop.** `b30` extracts completely (3,325 tags), rebuilds as `xbox-ntsc` and plays in the Linux port. Built with `-U -X 0x83000000` and run with the tag cache moved there (`HALO_TAG_CACHE_BASE=83000000`, tag-cache-address.patch), it loads and renders: the BSP is read from `0x845Cxxxx`, the top of the moved tag space. Every pointer shifts by the same amount (`0x2C5A000`) and the layout is otherwise identical.
- **Cache trap (fixed in the fork):** the game keeps a decompressed copy of each map in its Z: cache (`<save root>/z/cacheNNN.map`) and reuses it while the header checksum matches. Stock Invader stamps every Xbox map `0xFFFFFFFF`, so a rebuilt map with the same name silently runs the previous build. The fork now writes a CRC of the map body. Until a rebuild carries it, clear the cache by hand. `mapcheck.py` prints the address and checksum of any map or cache slot.
- **Loads on the PSP-1000 (`halo_loadtest`, 2026-10-01): passed.** `b30` built with `-U -X 0x08900000` (default 22 MB tag space), uncompressed with `mapunpack.py`, read from the Memory Stick into a block reserved at exactly `0x08900000` (`sceKernelAllocPartitionMemory`, `PSP_SMEM_Addr`). Every line matched the PC's prediction: crc32 of the 15.5 MB tag data `0x3A38B052`, all 3,325 tag names and 3,323 tag data pointers inside the window (the other 2 are the BSPs), both BSPs loaded at their addresses ending exactly at `0x09F00000` with valid `sbsp` headers.
  - **Timings (measured):** tag data 934 ms (15.8 MB/s, the Memory Stick's measured read speed); BSPs 408 ms (6.3 MB) and 245 ms (3.8 MB). A BSP switch costs ~0.25–0.4 s of reading.
  - **User memory free at start: 23,885 KB, largest block 23,633 KB.** The ceiling everything (code, game state, tags, caches) has to fit in.
  - The test's "words in the Xbox window" count is noise: 16,463 against ~20,800 expected by chance for non-pointer data such as packed normals. It was wrongly part of the pass/fail line (fixed in the source).
- **Game side:** the PSP platform layer returns the reserved block from `physical_memory_get_tag_cache_base_address()`, so no pointer in the game or the data needs touching.

## First runs on the PSP-1000: what real hardware does differently (2026-10-01)

Builds 5-12 on Sheepdog01's PSP-1000 (6.61 CFW). Blood Gulch's tags load on real hardware: 7.3 MB read into the window at 0x09600000, signature good, 1,806 tags indexed, and the first tick starts. Four ways the hardware differs from PPSSPP, each found once:

| Found in | What the PSP does | PPSSPP | Fix |
|---|---|---|---|
| debug_malloc (build 5) | faults on an unaligned word access | JIT forgives it; the interpreter (`-i`) reports it | `PSP_UNALIGNED_ULONG` (lwl/lwr) |
| script stack, `hs_evaluate` (build 9) | same: every stack frame's data starts at byte 14 of a 16-byte header, so the first value is 2 bytes off a word | as above (shows only once init.txt runs a command) | PSP: allocations word-aligned, frame sizes multiples of 4 |
| `unit_update`, first tick (build 11) | threads start with FPU traps for overflow, divide-by-zero and invalid operation **enabled** (fcr31 = 0x00000e00); a unit with `aiming_velocity_maximum` 0 does 0/0 | keeps the register, takes no FPU traps | mask the traps in every thread (`psp_fpu_mask_traps`); the Xbox ran with all masked |
| log file (builds 6-8) | log.txt appeared to stop at the same line every run | n/a | log moved to its own folder, and mirrored on screen |

**Build 12: Blood Gulch runs on the PSP-1000 (measured 2026-10-01, 333 MHz, null renderer, no audio).** It held 29.9-30.1 ticks/s steadily for more than a minute. Free kernel memory stayed flat at 579 KB.

| | average | worst in a 5 s window | share of a 30 Hz window (33.3 ms) | 20 Hz (50 ms) |
|---|---|---|---|---|
| Tick | 0.67-0.71 ms | 0.73-4.0 ms | 2% | 1.4% |
| Frame (render path, nothing drawn) | 3.17-3.23 ms | 12 ms, or 52-64 ms every other window | 10% | 6.4% |

What this does and doesn't show:
- **The world was idle.** No player was spawned; stabbed.txt says "player dead", and there is no AI on Blood Gulch. So this is a floor, not the combat tick the budget model above projects (5.38 M x86 instructions). b30 with AI and a live player is the real test.
- **The frame is the CPU half only.** It covers visibility, object preparation and the render calls into a renderer that draws nothing, so GE command building is still to come.
- **The 52-64 ms spikes** arrive every 10 s, which matches the texture cache's once-every-10-s "STABBED" report written to the Memory Stick. That is a pattern match, not confirmed.

**Build 13: a player spawns and walks.**
- **Why no player appeared before.** Loaded with `map_name`, Blood Gulch had no player. Its spawn points are flagged for multiplayer game types, and with no game engine running none of them match. On the PSP, any spawn point now counts when no engine runs (a `game_engine.c` fixup). Campaign maps are unaffected.
- **Telemetry.** Each heartbeat now also logs the local player's position and the raw pad state the game read.
- **Hands-free test.** With `ms0:/halo/walktest.txt` present, forward is held and the view turns slowly.
- **In the emulator:** the player spawned at (42.5, -90.5, 0.2) and walked about 2 world units per second, so input reaches the unit end to end.

**Build 13 on hardware: walking blind (measured 2026-10-01, 333 MHz, null renderer).**
- **Controls work end to end.** Position follows the pad: holding forward (pad `1000`) moves the player about 9 world units in 5 s, and height changes when jumping.
- **Startup.** The first ~10 s run slowly while breadcrumbs are being logged, then the game settles.
- **Steady state:** about 3 minutes of play with one live player, no AI, nothing drawn:

| | average | worst in a 5 s window | 30 Hz window (33.3 ms) | 20 Hz window (50 ms) |
|---|---|---|---|---|
| Tick, one player | 7.8-8.1 ms | 8.5-17 ms | 24% | 16% |
| Frame (PC-style d3d8 path, nothing drawn) | 25.7-28 ms | 35-92 ms | 78% | 52% |
| Tick + frame | ~34 ms | | **1.02x** | **0.68x** |

- **Rates.** Ticks held 29.2-30.2/s. Frames ran at 25-27 fps: one tick plus one frame (~34 ms) is just over a 30 Hz window, so frames are dropped to keep the tick on time.
- **The player costs most of the tick.** The live player took the tick from 0.67 ms (empty map) to ~8 ms. On a 20 Hz tick (50 ms window), tick plus frame fits with ~16 ms to spare. b30's AI is still to come; on x86, AI was 27% of the combat tick.
- **The frame is the path being replaced.** It runs through the PC-style d3d8 layer (shader and texture translation, state tracking) into a renderer that discards everything. The GE-native renderer replaces that layer, so 26 ms is an upper bound for this path, not the expected cost of the GE path. The projection for this path was ~3.5-5.5 M cycles, 10-16 ms at 333 MHz; measured is ~8.7 M cycles.
- **Stick drift.** The analog stick rests at about (121-125, 126-143), so it needs a dead zone.

**Build 14: where the time goes (measured on hardware, flat profile from the breadcrumbs).** Each call is charged the time until the next breadcrumb. Two views of Blood Gulch:

| per rendered frame | standing at spawn, looking at the base | walking in the open (steady) |
|---|---|---|
| frames per second | 11.6 | 24-26 |
| `render_objects` | **28.2-28.8 ms** | 6.0-6.8 ms |
| `objects_update` (the whole tick, about 7.8 ms per tick) | 15.3 ms (2 ticks a frame) | 9.2-9.6 ms |
| `structure_visibility_compute` | 6.6 ms | 0.6-0.7 ms |
| BSP passes (`structure_render_*`) | ~15 ms | ~6 ms |
| `first_person_weapon_render_update` | 3.7 ms | 3.8 ms |
| `sound_render` (no audio output) | 0.2 ms | **3.2-4.0 ms** once sounds play |
| `interface_draw_screen` (HUD) | 1.8 ms | 2.0-2.7 ms |
| `render_sky` | 1.1 ms | 1.1-1.4 ms |
| gap after each tick (`profile_tick_end`) | 1.3 ms per tick | 1.3 ms per tick |
| `input_update` | 0.75 ms | 0.75 ms |

Findings, by what they mean for the plan:
- **The view decides the frame.** From the spawn, `render_objects` alone is 28 ms. The base's objects are all in view, and object rendering (posing, then submission through the d3d8 layer) costs about 4.5x what it does in the open. This is the worst case to design for, and the next thing to profile in finer detail.
- **The tick is `objects_update`:** ~7.8 ms per tick with one player and Blood Gulch's vehicles and scenery. It needs a breakdown by object type.
- **`sound_render` costs 3-4 ms a frame with no audio output.** The game still runs its sound engine. That is a candidate quick win (skip or reduce it while there is no audio), but the audio design should decide it.
- **`first_person_weapon_render_update` is a steady 3.8 ms.** It poses the first-person arms and weapon; a VFPU target.
- **The 1.3 ms gap after each tick** sits between `game_tick` and the next breadcrumb (game_time.c's loop and `psp_tick_account`). It is unexplained.

**The sound cache overran its memory (fixed in build 15).** `xbox_sound_cache.c` sets up its cache as 1024 pages of 4 KB, the Xbox's 4 MB. The PSP gives it 512 KB, so the cache handed out addresses past its end. Sounds were read over the map-read request slots that follow it on the heap, and the next map read went to a garbage address.
- **How it showed:** the PSP died about a minute into play, after firing, jumping and meleeing.
- **How it was found:** a scripted-input file (`ms0:/halo/input.txt`: seconds, buttons in hex, stick) replayed fire, melee, grenade, Select and Start in the strict emulator. That reproduced the crash: an assert on a request with no buffer, then a write to 0x57902423.
- **Why the store watch missed it:** a watch on the request slots saw no foreign MIPS stores, because sceIoRead writes memory without executing a store instruction. The sound "outside the valid range" errors pointed at the cache instead.
- **The fix** sizes the page count from PSP_SOUND_CACHE_SIZE. With it, the same replay runs clean, including Start and Select.

**Build 15 on hardware: the sound cache, correctly sized, now thrashes.** No memory corruption this time. Once the player fired and meleed, the game reported "SOUND CACHE BLOWN" three times in 20 s: 512 KB cannot hold the sounds in play together. It kept evicting and re-reading them from the Memory Stick.
- `sound_render` rose to 16-22 ms a frame.
- The gap after each tick grew to 2.4-3.2 ms.
- One frame stalled for 2.4 s.

None of this produces sound, since there is no audio output yet. Build 16 disables the game's sound engine when `audio.enabled` is false (`sound_manager.c` fixup; `sound_new_impulse`, `looping_sound_new` and `sound_render` then do nothing). In the emulator, `sound_render` drops to 0.02 ms a frame. Real audio will need its own memory and CPU budget: Xbox ADPCM has to be decoded on the PSP (CPU or Media Engine), and the sound cache must be sized for the sounds that play together.

**Build 16 on hardware: stable, sound engine off (measured 2026-10-01).** Two sessions of several minutes each, with firing, meleeing, jumping and Start presses, and both ended with a clean exit to the XMB.
- **Steady state while moving:** 30 ticks/s held. Frames 23-26 ms (26-28 fps). Tick ~8 ms (`objects_update` 8.2-9.8 ms a frame). `render_objects` ~6.2 ms, first-person weapon 3.8 ms, HUD 2.0-2.6 ms, BSP passes ~6 ms, sky 1.1-2.0 ms.
- **The gap after each tick** varies from 1.5 to 3.5 ms a frame. Still unexplained.
- **Pressing Start** (the pause screen, which this map does not have) cost one window: kernel free memory dipped from 559 to 287 KB, the tick peaked at 66 ms, and `objects_update` averaged 16.8 ms a frame. Everything recovered. Lowest free kernel memory seen so far: 287 KB.

**Decision (Sheepdog01, 2026-10-01): audio is baked like every other asset.** The plan:
- The map baker transcodes the sounds to a PSP-native form and sizes them to an audio memory budget.
- The likely route is the PSP's own ADPCM format (VAG), mixed by the hardware sound library (sceSas, on the Media Engine). That takes the mixing off the main CPU. This needs checking against what the toolchain exposes (an expectation, not verified).

Known, not fixed: x87/SSE turn NaN into the integer 0x80000000, the PSP FPU into 0x7FFFFFFF, so `unit->aiming_change` comes out 255 instead of 0 for such units: a small divergence from the other ports.

**Tooling that came out of it:**
- **On-screen log**: every log line is drawn into a 16-bit frame buffer at the top of VRAM (0x041B0000) before it is written to the Memory Stick. When the PSP freezes, the screen keeps the last lines for a photo.
- **Numbered breadcrumbs**: `psp_step()` runs before every call in start-up, the main loop, `game_tick` and the render path, up to a limit. Bisecting with these found the FPU trap in two runs.
- **A stricter PPSSPP** (local patch to the interpreter):
  - It reports user-code accesses a PSP-1000 faults on: kernel RAM (0x08000000-0x087fffff), the kernel segments, low memory, and 0x0a000000 and above (the PSP-1000 has 32 MB).
  - It reports FPU operations that would trap with the thread's fcr31.
  - With Sheepdog01's maps, it reproduced the hardware crash exactly.
- **Crash handler: does not work on this CFW.** `halo_crash.prx` installs (the call returns 0), but a deliberate fault never reaches it. Three versions failed: two drew via display-driver calls, one with no kernel calls at all. ARK-4 registers its own default handler at boot (core/systemctrl/debug/exception.c), and that never showed either. Parked; psplink is the heavier fallback.

## GE renderer, slice 1: the level, flat-shaded (build 17, 2026-10-01)

The PSP draws Blood Gulch's level itself, from the player's camera: one flat color per material, distance fog, 480x272 at 16-bit. Objects, textures, sky and HUD are not drawn yet. Verified in PPSSPP (software renderer screenshots, strict interpreter clean); not yet on hardware.

**How the geometry gets to the GE without copying.**
- **The vertices.** The level's vertices (`environment_vertex_compressed`) are 32 bytes each: position (3 floats), packed normal, binormal and tangent (4 bytes each), then texcoord (2 floats). The GE wants the position last in a vertex. Asked for texcoord-float + normal-float + position-float, it reads 32-byte vertices that end in a position. So the GE is pointed 20 bytes before the game's buffer, and each GE vertex ends on the game vertex's position. The texcoord and normal it reads are the previous vertex's packed vectors, unused with texturing and lighting off.
- **The triangles.** These are the game's own per-frame list of visible surfaces (16-bit indices, `dynamic_triangles`), passed per material from the lightmap pass (`rasterizer_environment_lightmap_draw`).
- **The camera** comes from `global_window_parameters.camera` (`sceGumPerspective` + `sceGumLookAt`; Halo is right-handed and z-up, so right = forward x up).

**Near the camera the CPU helps.**
- **The problem:** the GE drops a whole triangle when a corner is behind the camera or lands beyond its 2048-pixel range. Blood Gulch's ground is large triangles, so without help the ground under the player vanished; the first emulator screenshot showed the lower half of the screen as sky.
- **The fix:** every triangle is now classified on the CPU against the near plane (0.25 units) and four guard-band planes (6 screen widths/heights):
  - wholly outside one plane: skipped (177 of 2,925 at the spawn);
  - safely inside: drawn by the GE as it is (filtered index lists go into display-list memory only when something was dropped);
  - otherwise: cut down (Sutherland-Hodgman) and drawn at the end of the frame in camera space (25 triangles, 123 vertices at the spawn).

**Passes made redundant.** With the GE drawing the level, the null-Direct3D level passes return at once on the PSP: diffuse, specular, reflections, reflection masks, transparent geometry, fog, screen fog, detail objects, sky, and the object lights on the environment. The lightmap pass keeps only its GE draw. On build 14's profile, these were about 6-8 ms a frame while walking. Hardware will show what is saved and what the GE path costs.

**Memory.**
- **VRAM:** two frame buffers plus a depth buffer, 816 KB at its start; the on-screen log stays at 0x041B0000; the null renderer's targets never draw.
- **RAM:** a 64 KB display list and a 48 KB buffer for the cut-up triangles. The heap shrank from 5,312 to 5,184 KB as a result, with no allocation failures in emulator runs.

**Slice 2: objects, rigidly (build 18).**
- **How.** Model vertices (`model_vertex_compressed`) are 32 bytes with the position first, so the same 20-byte offset trick reaches them. Each part is drawn with the node matrix of its first vertex; node indices are stored times three, for the Xbox's vertex-shader constants. The part's own index buffer is used directly: precompiled strips of count+2 indices, or triangle lists.
- **What it covers.** That is correct for vehicles, scenery and weapons, and approximate for parts that blend two nodes (bipeds, arms). The first-person rifle draws correctly.
- **Hooks in `rasterizer.c`:**
  - `rasterizer_window_begin` starts the GE frame, from the first window's camera; the GE frame now opens before `render_objects`.
  - `rasterizer_model_begin` records the node matrices.
  - `rasterizer_model_draw` sends static parts to the GE and skips their Direct3D draw.
  - Dynamic geometry, shadows and transparent parts still go to the null path.
- **Spawn view in the emulator:** 12 level draws (2,925 triangles), 25 triangles cut near the camera, 43 model parts. Strict interpreter clean with turning, walking and firing.

**Known limits, next:**
- Thin cracks can show where cut-up triangles meet uncut ones.
- Materials are told apart by color only (no lighting).
- Objects that bend (bipeds) are drawn rigidly. True skinning would need either a GE vertex layout with per-vertex bone weights (a baking job) or CPU skinning.
- The real fix for both the cracks and the CPU's classification cost is baking: GE-ready level geometry from the map baker, pre-split near ground level.

**Build 18 on hardware (measured 2026-10-02, 333 MHz, GE drawing the level and solid objects; 14 profile windows).**

| per rendered frame | on foot in the open (2 windows) | driving near the base (5 windows) |
|---|---|---|
| frames / ticks per second | 27.9 / 28.9 | 9.1 / 18.1 (the game slows down) |
| tick (`objects_update`, per tick) + gap after it | 8.2 + 1.3 ms | 13.4 + 6.0 ms |
| `render_objects` | 7.5 ms | 25.0 ms |
| see-through parts (null path, draws nothing) | 0.5 ms | 13.0 ms |
| `render_object_shadows` (null path) | 1.9 ms | 7.9 ms |
| particles + decals (null path) | 0.4 ms | 7.2 ms |
| level: lightmap step = CPU classification + GE submission | 1.5 ms (~1,150 triangles) | 6.5 ms (up to 3,722) |
| `interface_draw_screen` (null path) | 6.9 ms (was 2.0-2.6 in build 16; unexplained) | 3.2 ms |
| first-person weapon | 3.6 ms | 0 (in a vehicle) |
| waiting for the GE at frame end (`psp_frame_account`) | 0.2 ms | 0.2 ms |

- **One window (tick + frame):** ~35 ms on foot (1.07x of 30 Hz, 0.71x of 20 Hz); ~90 ms driving near the base (2.7x / 1.8x).
- **The GE keeps up:** a 0.2 ms wait, so overlapping CPU and GE frames gains little.
- **Classification costs ~1.3-1.8 us per level triangle**, more than the 300 cycles assumed.
- **Biggest quick lever:** the null path still runs shadows, see-through parts, particles, decals and the HUD, drawing nothing: ~10 ms a frame on foot, ~31 ms driving.
- **"STABBED" cause:** the texture cache fails on a 131,328-byte request for "rasterizer\vector normalization" (9 pages), a lookup texture only the Xbox path uses.
- Free memory held at 551 KB all session. Frame spikes 60-256 ms remain.

## Builds 19-21 (2026-10-02, emulator-verified; hardware runs pending)

- **Build 19 (speed):** null-path shadows, see-through parts, particles, decals and texture loads skipped while the GE draws (keep.txt brings each back to measure it); render interpolation off (its double sqrt explained the post-tick gap and most of the first-person weapon's 3.6 ms, per the source audit); float versions of sqrt/floor/ceil/fmod/trig for float arguments (bit-identical; double sqrt calls 28 -> 0, float->double conversions 1,096 -> 727); idle scenery sleeps when its update leaves it byte-identical (127 of 259 on Blood Gulch; the rest sway or blink); each level vertex classified once; one pad read per poll; cold asserts with block partitioning; log batched (one write per 5 s); LOD measured for 272 lines; the game's frustum set to 848x480 to match the GE's 480:272. New measurements: objects_update by object type, heap high-water (4,216 KB before textures), display-list peak.
- **Build 20 (textures):** `tools/texpack.py` bakes each map's 2D bitmaps into a `.ptex` pack (swizzled CLUT8/CLUT4 + mips, <= 256 px, objects 128 except first-person; lightmaps full size; level shaders' detail maps folded into the base map by their average colour). Runtime cache: VRAM after the depth buffer + 256 KB RAM, 96 KB of reads per frame. Level: base texture x lightmap (x2) in two passes from per-material GE vertex copies (184 KB on Blood Gulch); models: vertices rewritten in place for the GE (only the renderer reads them), textured, unlit.
- **Build 21:** HUD quads in GE 2D (meters by alpha test), the sky (transparent parts queued, drawn far to near, sky at the far plane), see-through model parts with their blend functions, the on-screen log's VRAM given to textures once the GE runs. Blood Gulch pack 4.6 MB; largest frame 60 textures / 1.1 MB.
- **Memory now:** heap peak 4,476 of 4,928 KB with textures (emulator). Reclaim candidates: netcode tables sized for 2,048 tracked objects (~600 KB, allocate only for network games), null-renderer leftovers (~160 KB), volatile partition (4 MB).
- Research reports: claude/engine_performance.md, claude/asset_baking.md, claude/audio.md. Overnight report doc: [private link]

## Budget model: one tick plus one frame (estimated from the measurements above)

PSP tick = compute (x86 instructions × 1.2–1.5 × ~1.2 cycles) + stalls (misses × 64 extra cycles) + double trig (calls × ~5,000 cycles). Ratios are total cycles over the window's budget at 400 MHz; **below 1.0 fits**.

| | **30 Hz (target)** | 20 Hz |
|---|---|---|
| Window | 33.3 ms | 50 ms |
| Budget at 400 / 333 MHz | 13.3 M / 11.1 M cycles | 20.0 M / 16.7 M cycles |
| Tick as the code stands | 17.8–23.7 M | 17.8–23.7 M |
| Tick after the trig fix | 14.9–19.3 M | 14.9–19.3 M |
| Frame after free fixes (PC-style path) | ~3.5–5.5 M | ~3.5–5.5 M |
| **A. Total after free fixes** | 18.4–24.8 M: **1.4–1.9×** | **0.9–1.2×** |
| **B. + GE-native renderer** (frame ~1.5–3 M) | 16.4–22.3 M: **1.2–1.7×** | **0.8–1.1×** |
| **C. + tick levers** (stagger perception, collision layout and scratchpad, VFPU skeleton: −3.5–6 M after overlap) | 10.4–18.8 M: **0.8–1.4×** | **0.5–0.95×** |
| **D. + ME offload** (raycasts beyond the stagger −1–2 M; visibility and list building −0.5–1.5 M) | 6.9–17.3 M: **0.5–1.3×** | **0.35–0.85×** |
| E. + prefetch where the code is register-only (v7c: other memory accesses wait for a fill) (−0.2–0.8 M) | 6.1–17.1 M: **0.45–1.3×** | **0.3–0.85×** |

Reading it: **20 Hz fits once the GE-native renderer and the tick levers land** (row C). **30 Hz needs row D** and lands around 0.9× at the middle of its range; prefetch turned out to be a small lever (row E), so margin has to come from the tick levers, the ME and the single-precision cleanup. Two costs aren't in these rows yet: instruction-cache misses (v7: ~55–70 cycles a missed code line) and the GE's bus traffic (v8: CPU misses +63% while it textures from RAM); both are reasons to keep textures in VRAM and lay out hot code. The ranges are wide because they stack estimates; the first real tick count on the PSP (milestone 4) replaces them. At 333 MHz, multiply every ratio by 1.2.

## Levers, ranked by expected gain (estimates unless marked)

| # | Lever | Gain per window | Basis |
|---|---|---|---|
| 1 | Trig wrappers in `real_math.h` → float/VFPU | −3–4.5 M tick, −0.5 M frame | Measured costs and call counts |
| 1b | **Single-precision cleanup** (mandatory): `square_root` → `sqrtf` and `fast_ftol` → `rintf` (both exact), then hot `fabs`/`fmod`/literal promotions | Avoids a hidden PSP-only cost the model doesn't include; size set by v7's double `sqrt` cost | Static analysis: 3,800 double `sqrt` sites; clang on x86 already emits float `sqrtss` |
| 2 | Release build: profiler out | Frame −16–30% | Measured share |
| 3 | **GE-native renderer**: baked BSP display lists, BBOX culling, hardware skinning, VRAM textures | Frame from ~3.5–5.5 to ~1.5–3 M | GE documented; PC frame breakdown measured |
| 4 | **Collision and object data laid out by cache line** (64-byte BSP node clusters, hot/cold splits, no 8 KB-aliased strides) + scratchpad for the top of the collision BSP | −1.5–3 M stalls | Measured miss cost and raycast share; 2-way cache documented |
| 5 | Stagger AI perception over 2–3 ticks, cache line-of-sight results | −1.5–2.5 M | 17% of instructions, 25% of misses |
| 6 | **Media Engine as a second core**: line-of-sight raycasts (collision in its eDRAM) and render-side visibility and list building | −1.5–3.5 M main CPU (overlaps 5) | ME measured: works on PSP-1000, 0.42 µs handoff |
| 7 | **`cache 0x1E` prefetch**, only ahead of register-only maths (e.g. the next node's data before the VFPU matrix work, the next plane before a plane test's arithmetic) | −0.2–0.8 M | **Measured (v7b, v7c)**: non-blocking and hides ~80–90% of a miss, but every load or store waits for it, so it can't run under ordinary code |
| 7b | **Code layout for the 16 KB I-cache** (hot/cold splitting, function ordering by profile) | Unknown until a PSP profile; each missed code line ~55–70 cycles | Measured on hardware (v7) |
| 8 | VFPU skeleton posing (aligned `lv.q`/`sv.q` only) | −0.8–1.2 M | 8× measured on matrix multiply |
| 9 | `cache 0x18` on full-line writes (bone palettes, display lists, copies) | 2× fill, 1.46× copy bandwidth; small in the tick, bigger in the frame | **Measured (v7b), user mode** |
| 10 | Encounter thinning | Scales AI + biped (~45% of tick) | Content choice; prior art (the GTA III-in-LCS port had to cap spawns on the 1000) |
| 11 | Shadows off | −~0.3 M frame | Measured share |
| 12 | **Draw distance, per level and switchable** (far clip + fog, LOD distances scaled), raised for cutscenes | Frame cost and VRAM-cache footprint; size comes from the per-cluster footprint analysis | Approved lever; must cover flying sightlines and not let AI out-range what the player sees |

## The bus rule (measured, v8b)

The tick spends about half its time waiting on main-RAM misses, and the GE runs in parallel with it. Measured with the GE busy:

| What the GE reads | CPU miss cost | Effect on a tick that is ~50% miss stalls, if the GE ran for the whole tick |
|---|---|---|
| VRAM textures, VRAM vertices, flat fill | 216–220 ns (unchanged) | none |
| RAM textures | 356–424 ns (+65–96%) | tick +30–50% |
| RAM vertices | 804 ns (+272%) | tick ~2.4× |

The real hit scales with how much of the tick overlaps the GE reading RAM: a GE that finishes its frame in 8 ms of a 33 ms window overlaps about a quarter of it. Even so, **vertices in RAM are the single most expensive thing in the frame**. So:

1. **Keep the visible set's vertices and textures in VRAM.** About 1.2 MB is free at 16-bit colour, which isn't enough for a whole level. The VRAM becomes a cache of the clusters and models in view, refilled by DMA (323 MB/s into VRAM; DMA also costs the CPU while it runs, so in short bursts at cluster changes, not continuously).
2. **Compact formats make the cache go further:** 8-bit CLUT or 4-bit CLUT textures, 16-bit positions, 8-bit normals, indexed vertices.
3. **Draw RAM-resident leftovers early in the frame window and let the GE finish fast**, so less of the tick overlaps them.
4. **Display lists** stay in RAM (small, sequential reads); not yet measured.

### What the visible set needs (measured from the map data, 2026-10-01)

A script walks each BSP's sub-clusters and visibility data and totals what the GE would need for the view from each sub-cluster (up to 400 viewpoints per BSP). Layout checks all passed. Textures count only what a PSP renderer would draw: the environment shader's base, primary detail and self-illumination maps (offsets asserted in the decomp), as 8-bit CLUT at half resolution with mipmaps. Geometry is 16-bit vertices for two passes (base and lightmap) plus 16-bit indices. Lightmaps at half resolution, 1 byte a pixel. Only the level's own geometry is counted; placed objects come on top.

| Level (BSPs) | Worst view, MB | Median view, MB | Median at 50 / 25 wu draw distance |
|---|---|---|---|
| `b30` Silent Cartographer (2) | 2.03 / 1.97 | 1.35 / 1.50 | 1.30–1.50 / 1.03–1.44 |
| `b40` Assault on the Control Room, also `c40` (13) | 1.20–2.55 | 1.20–2.24 | down to 1.83 / 1.42 in the open BSPs |
| `a30` Halo (2) | 1.66 / 2.47 | 1.46 / 1.51 | 1.24 / 1.14 → 0.97 / 0.87 |
| `d40` The Maw (9 with geometry) | 1.21–2.20 | 0.78–1.70 | little change (interiors) |

Of a typical worst view: geometry 0.3–1.1 MB (densest in `d40`), textures 0.45–1.45 MB, lightmaps 0–0.4 MB. Free VRAM is ~1.2 MB.

- **Medians straddle the 1.2 MB of free VRAM; worst views are up to 2× over.** Close, not comfortable.
- **The GE-unusable maps (bump, cube, micro detail) were ~40% of the first count.**
- **Draw distance is a modest VRAM lever**: in open BSPs a 25–50 wu cut (76–152 m) lowers the median 20–40%, but barely moves the worst views or interiors. It's mainly a GE-time and LOD lever.
- **What closes the gap**, in order:
  1. vertices always in VRAM (they're the most expensive thing to leave in RAM, v8b) and textures in an LRU cache with overflow to RAM, swizzled;
  2. 4-bit CLUT for detail maps (often greyscale) and small textures;
  3. baked vertex lighting, which removes lightmaps (up to 0.4 MB) and the second vertex stream (~40% of geometry). **Now backed by data, but still a quality trade-off for Sheepdog01 to decide.**

## Renderer plan (GE-native, from the GE research)

1. **Bake the BSP into display lists at level build time**: one sublist per cluster, sorted by material, with the lightmap pass included. Per frame, the CPU writes a few matrices and one CALL per visible cluster.
2. **BBOX + BJUMP in front of BSP clusters and large objects** (~1 µs of GE time a test, v8), so the GE does their frustum culling; small objects are culled on the CPU or not at all.
3. **Tessellate the BSP offline** so no triangle can cross the 4096 guard band; no runtime clipping.
4. **Models: rigid pieces where a vertex follows one node** (drawn with that node's matrix), hardware skinning only where vertices blend two nodes; 8-weight skinning measured 12× slower than plain vertices (v8). The CPU poses bones on the VFPU.
5. **Textures and vertices of the visible set in a VRAM cache** (the bus rule above); textures swizzled CLUT4/CLUT8 (fastest, v8), no DXT.
6. **Lightmaps: Xash3D-style second pass** (depth EQUAL, multiply blend).
7. **Double-buffered lists**: the GE draws frame N while the CPU builds N+1; the ME can build part of it.

## Media Engine plan and rules

- **Jobs**: line-of-sight raycasts against a read-only collision copy in eDRAM; render-side visibility and list building; the audio mixer.
- **Rules**: raw code only on the PSP-1000 (no `meCore*` calls); no libc, syscalls or VRAM access from the ME; shared, changing data goes through uncached queues, or the ME invalidates before reading; read-only data in eDRAM avoids coherency problems; the idle loop halts or polls rarely.
- **Audio**: our own mixer, since Sony's SAS goes away with the takeover. Its CPU cost is unmeasured.

## v7: what each result decides

| v7 measures | If it shows... | ...then |
|---|---|---|
| `cache 0x1E` + 64 adds vs adds alone; fill + adds + load | Close to adds alone, and the later load hits | **Prefetch works**: lever 7 is real, budget row E opens. Next: prefetch the next object, actor and BSP child |
| Load with adds before use | Adds hide under the miss | Loads only stall at use, so the compiler's scheduling (and ours) can overlap work with misses |
| Two loads, two lines | ~1 miss per pair | The CPU overlaps misses: interleaving two traversals (e.g. two raycasts) roughly halves stall time |
| Sequential vs scattered | Sequential much cheaper | Streaming layouts (sorted object arrays) pay off beyond the cache line |
| `cache 0x18` fill and copy | Faster than plain | Use it for every full-line write |
| Volatile lock | Locks, data correct, normal latency | +4 MB for reloadable data |
| 128 KB code vs 8 KB code | Cost per I-line miss | How much Halo's large tick code loses to the 16 KB I-cache; whether hot/cold code layout matters |
| CPU misses with the ME busy | Little slowdown from eDRAM work, more from main-RAM work | Confirms the ME is a near-free second core when it works out of eDRAM |
| ME float vs main CPU | Same cycles, identical results | The ME runs raycast-style float code at full speed |
| Profiler readings | Counters match the known workload | On-device profiling for milestone 4 |
| eDRAM unit stopped | Faster ME | Stop it in the port's ME start-up |
| Memory Stick | MB/s and open time | Streaming design and level-load time |
| The price of doubles | Cycles for a double `sqrt`, a promoted multiply, a double divide, double `atan2`, against their float and VFPU forms | How much every leftover double operation costs, so milestone 4's profile can be read |

## v8 (GE): what each result decides

| v8 measures | Decides |
|---|---|
| Fill: flat, with depth, with blending | How much overdraw a 30 fps frame affords (a 480×272 frame is 130,560 pixels) |
| Textures: VRAM vs RAM, 16-bit vs 8-bit CLUT, swizzled vs linear | Texture format and placement in the bake; how much VRAM is worth for textures |
| Vertices: 16-bit vs float, lit, 8-weight skinned, from RAM vs VRAM | Vertex budget per frame at 30 and 20 fps; the bake's vertex formats; whether hardware skinning is affordable at 8 weights |
| BBOX on and off screen; CALL vs inline | Whether GE-side culling is cheaper than CPU culling per object; granularity of baked sublists |
| CPU cycles per `sceGuDrawArray` (CALL list, with matrix, in BBOX, GU_DIRECT) | The CPU side of the frame budget per draw; whether to write raw list words instead of the GU library |
| CPU misses while the GE textures from RAM vs VRAM | How much RAM texturing slows the tick, which sets how hard to push textures into VRAM |

## Decisions so far

- **The goal is campaign (2026-10-01): The Pillar of Autumn (`a10`) or The Silent Cartographer (`b30`) loading and reasonably interactive.** 30 Hz stays the performance target, but a playable level comes first.
- **Multiplayer is a proof of concept**, between PSPs and/or PPSSPP (ad hoc), built at Xbox limits (16 players, 4 machines; `psp_limits.h`). **One executable**: the map file decides the mode, as on the Xbox. The session limits stay a build setting, not hard-coded Xbox values.
- **Stretch goal: 128 players in a controlled setup** (e.g. PSPs as clients of a PC host over infrastructure Wi-Fi). Costs to plan for: ~1.2 MB of netcode buffers at 128, the host's per-tick update (3,857 bytes at 128 players), and bit-identical maths with the PC builds, which means a build flavour on the ports' shared double-precision maths library instead of float or VFPU trig.
- **Documentation is a deliverable** (2026-10-01). Public docs written for other people to learn from and reuse: measured results with the code and commands that produced them, decisions with their reasons, and a plain-language log. Others decide what's useful to them; we make it easy to find and verify.
- **Offer what's general.** Fixes that aren't PSP-specific are offered to the decomp and to Invader as small, explained patches; it's their call what to take: Invader's `-X`/`-U` options and Xbox checksum, the oversized `render.environment_surface_flags`, the single-precision patch.
- **Distributed netcode** (host-authoritative). It tolerates float differences, so the VFPU and float trig are allowed. PSP-to-PSP lockstep remains possible; lockstep cross-play with PC does not.
- **Offline bake: our Invader fork** rebuilds maps for the PSP's tag address (`-X`), so no relocation at load. The downsizing steps above (model LODs, animation packing, 16-bit collision indices) and the GE conversions (tessellation to the guard band, rigid pieces and ≤8-bone batches, swizzled CLUT textures) become build steps in the fork.
- **First level: `b30`.** The whole map toolchain is proven on it (extract, rebuild, run at a moved address) and its visible-set footprint is measured; `a10` follows. (Earlier note: `c20` has the lightest window but the heaviest BSPs.)
- **The ME is ours** (libme-core, raw code) rather than Sony's (SAS and codecs). That means writing our own audio mixer.
- **16-bit color**, to keep ~1.2 MB of VRAM for textures.
- **Content cuts stay limited.** Encounter thinning is a last resort, not a plan.
- **Draw distance is an approved lever**, set per level and switchable at runtime (e.g. raised for cutscenes that need long views), paired with aggressive model LODs (Medal of Honor Heroes 2's approach: long view, cheap far geometry) rather than a hard fog wall where it can be avoided. Constraints: the cut must cover the long sightlines of flying sequences (Banshee in `c40` and `b40`, Pelican flights) and must not let enemies engage from beyond what the player can see (AI perception doesn't depend on rendering).
- **Baked vertex lighting (replacing lightmaps) is deferred** until the per-level footprint and GE frame cost show whether the lightmap pass is a problem.

## Tick-rate patch (applied on branch `tickrate`)

- `tick-rate-constants.patch`: 37 float + 111 integer tick literals routed through `TICKS_PER_SECOND` / `TICKS_FROM_30HZ`, plus 3 commented look-alikes. **Assembly is identical at 30 Hz** across all 40 touched files; compiles and rescales at 25 Hz.
- **Still needed for a 20 Hz build:**
  - per-tick tag values (generalize the PAL table in `pal_tags.c`: velocities ×k, accelerations ×k²);
  - HaloScript tick literals in scenarios;
  - single-digit literals.
- **Behavioral checks:** cutscene audio sync (the `a10` intro drifts if timing is wrong) and reload duration (assault rifle full reload = 87 ticks = 2.9 s).

## Single-precision patch, step 1 (ready, not applied)

`single-precision-1.patch`: `square_root()` → `sqrtf`, `fast_ftol()` → `rintf`. **Compiled for x86 across all game sources, the assembly is identical** except the spelling of one library-call relocation (clang was already emitting exactly this), so the PC build's behaviour can't change. On the PSP it removes a ~1,800-instruction software square root from every magnitude and normalize, and a software double round trip from every float-to-integer rounding.

## Next milestones

1. Run `halo_bench` v7 and v8; fold the results into the budget (v7: rows D and E; v8: the frame's GE side). On the desktop: the `xbox-capacity` experiment and the assertion share.
2. Compile the game for PSP with null renderer and audio. **Compiles and links (stubbed platform); linked size measured.** Next: a real PSP platform layer (files, threads, timing, memory, null D3D/DSound) and the tag-cache address (Invader, below).
3. Baker v0: `b30` rebuilt for the PSP tag address. **Proven on desktop at `0x83000000`.** Next: the PSP address and the downsizing steps until it fits.
4. Headless tick on the PSP. **First real per-tick cycle count**, replacing the x86 projection; report it at 30 Hz and 20 Hz. Use the hardware profiler if v7 shows it works.
5. Minimal GE renderer (BSP and objects, simple texturing) and input: **`b30`, then `a10`, loading and reasonably interactive.** Then the full renderer plan.
6. Apply levers in order and re-measure after each.

## Open questions

- Heavier scenes (`a50`, `d20`): a worst-case tick, with the deathless cheat for long runs.
- Share of the tick spent in assertion checks.
- Real GE cost per model part and per cluster, versus the PC submission path.
- CPU cost of a software mixer for Halo's sound load.
- A Halo frame's triangle, vertex and texture footprint per scene: decides whether the visible set fits the ~1.2 MB VRAM cache.
- The CPU cost of the GE reading display lists from RAM.

## Resources and tooling

### Toolchain: verified by building `halo_bench`
- **pspdev release v20260901** (GCC 15.2; asset `pspdev-ubuntu-latest-x86_64.tar.gz`). Bundles pspsdk, **psplink** (`pspsh`, `usbhostfs_pc`), **psp-gdb** and ebootsigner.
- **psp-packages include:** SDL3 for PSP (the port's platform layer is SDL3); **`libme-core`** (mcidclan, MIT; includes the `kcall` kernel bridge); `libpspgum_vfpu`, `libpspdmac`, `libpspprof` (gprof), intraFont.
- **CFW API:** `sctrlHENSetSpeed(cpu, bus)` / `sctrlHENGetSpeed()`: the game can set 400/200 itself and verify it (ARK/PRO-style CFW).
- Compiler flags from SM64 PSP: `-O3 -falign-functions=64 -ffp-contract=off`; plus `-fsingle-precision-constant`. A 2025 upstream GCC patch series adds Allegrex `clz`/`ext`/`ins`/`min`/`max`/`madd` emission; VFPU code still needs inline asm.

### Hardware documentation
- Architecture overview: https://www.copetti.org/writings/consoles/playstation-portable/
- uOFW (firmware reimplementation; cache ops, memory partitions, GE driver): https://github.com/uofw/uofw
- yapspd / PSPTEK: http://uofw.github.io/upspd/docs/hardware/PSPTEK.htm
- Allegrex: https://www.psdevwiki.com/psp/Allegrex ; PPSSPP hardware docs: https://www.ppsspp.org/docs/psp-hardware/cpu/allegrex-overview/
- **VFPU reference with accuracy and timing**: https://pspdev.github.io/vfpu-docs/
- Media Engine: https://github.com/mcidclan/psp-media-engine-reload and https://github.com/mcidclan/psp-media-engine-custom-core/ ; dual-CPU benchmark https://github.com/kan8223-dotcom/psp-me-bench
- Volatile memory behaviour: https://github.com/hrydgard/ppsspp/pull/16052 ; kernel timing measurements: PPSSPP PRs #22383 and #22395
- GE: PSPSDK GU docs https://pspdev.github.io/pspsdk/group__GU.html ; PPSSPP's GPU code (`GPUCommon.cpp`, `Clipper.cpp`) is the de-facto spec, including measured texture timings.

### Debugging and emulation
- **PPSSPP**: CPU and **GE debugger**. Fine for correctness, useless for timing. Its ME emulation is an open, unmerged PR (#21554), so ME code is tested on hardware.
- psplink + psp-gdb on hardware; `libpspprof` and (if v7 confirms) the hardware profiler for on-device profiling.

### Prior art
- **Half-Life (Xash3D, Crow_bar)**: https://github.com/Crow-bar/xash3d-fwgs. GU renderer, swizzled textures, lightmap second pass, VFPU clipping, volatile-memory allocator; studio-model skinning on the CPU (the thing to avoid).
- **SM64 PSP (z2442)**: https://github.com/z2442/sm64-port. VFPU assembly for transform and clipping, audio on the ME, gprof build.
- **DaedalusX64**: all audio on the ME through job queues.
- **ioQuake3-PSP (2026)**: 64 MB only; 33 fps average at 333 MHz on a demo; "don't expect big maps with many bots".
- Commercial: God of War: Chains of Olympus spent the 333 MHz unlock on lighting, particles and "improved enemy intelligence"; Peace Walker is capped at 20 fps. No published per-frame budgets.
- Halo on PSP so far: fan maps for CS Portable and Kurok only; no engine port.
