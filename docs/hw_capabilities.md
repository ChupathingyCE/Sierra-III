# The PSP-1000's hardware: what we use, what we don't, and what to chase

Ledger 1 of the redesign's recon (`redesign_recon.md`). Sheepdog01, 2026-11-04 13:14: "we catalog gaps, chase them based on what they might contribute to the goal, and THEN we build."

**This is a first draft.** It comes from:
- the lab's OpenRouter sweep of every doc and the port's code (or-job 41895328253, checked by `check_quotes.py`: 201 of 240 citations confirmed; the rest were mostly "no hits" claims cited against a context line);
- a check of every `sceGu*` function and `GU_*` switch in the SDK's `pspgu.h` against the port's code (master, 2026-11-04);
- the master's spot checks of the claims below.

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or docs, [U] inference.

## 1. The units, feature by feature

### The main CPU (Allegrex, 333 MHz)

| Feature | In use | Measured | Note |
|---|---|---|---|
| 16 KB I and D caches, 64-byte lines, 2-way | yes | `cpu_cost_map.md` §2 [M] | The tick's main cost: §1 there |
| `cache 30` (Fill), the data prefetch | in places (the node walk, off by default) | v9: non-blocking, one in flight, needs ~70 cycles of miss-free work [M] | `prefetch_map.md` |
| `cache 24` (create dirty exclusive) | drawing's list writes (`psp_ge.c`) | halves whole-line writes [M] | Unused in the tick |
| Instruction prefetch | none possible from user code | IPREFETCH [M] | Closed |
| Allegrex's extra instructions (min/max, ext/ins, clz, wsbh, madd) | as GCC emits them | — | Unchecked: whether `-march=allegrex` emits them in hot code [U] |

### The VFPU

| Feature | In use | Measured | Note |
|---|---|---|---|
| Matrix and quad ops | at least these sites in `psp/src` [D]: the node product (`psp_vfpu.c`, on), the quaternion forms (`psp_vfpu_quat.c`, on), two drawing paths in `psp_ge.c` (built, off) | 1.9–2.6× the FPU on the node product [M] | `hardware_utilization.md` §3 holds the float inventory. Its summary line ("one job in the whole port") is stale |
| Approximations (`vsin`, `vcos`, `vrsq`, `vexp2`) | not checked | — | Where the engine's hot maths could take them, exactly or "plays the same" [U] |
| Saving its registers across threads | — | — | Whether more threads could use it [U] |

### The scratchpad (16 KB)

| In use | Measured | Note |
|---|---|---|
| The level BSP's top nodes (SCRATCHBSP, on; `psp_scratch.c`) | −0.3 ms cinematic and flight [M] | Code runs from it at 14.3 cycles an instruction, so code is out (CODESPAD). Table candidates: SPX, ~0.15 ms [E]. `hardware_utilization.md` §4 still says "never built": stale |

### The Media Engine and its 2 MB eDRAM

| Feature | In use | Measured | Note |
|---|---|---|---|
| The second Allegrex, with its FPU (no VFPU) | audio, the drawing replay, AI (AIME 2a), point physics, visibility, sound copies | its misses don't slow the main CPU's [M]; branchy scalar work at 0.88–0.96× the main CPU's time [M] | **On d20's heavy view it's the binding unit:** the render job takes 66 ms a frame, and the GE stalls 23–24 ms a frame waiting for its chunks [M, recon 41888761035, 41893041680] |
| The scheduler | slots and deadlines per job; no arbiter | — | The rebuild Sheepdog01 deferred to after the tick work (12:13) |
| eDRAM, 2 MB | the ME's code and stacks, the sound cache's tier (SNDEDRAM2), the music decoder's room | — | No one page maps all 2 MB: a gap |

### The VME (the ME's reconfigurable engine)

| In use | Measured | Note |
|---|---|---|
| A test path only (`psp_vme.c`) | 40 of 40 products exact, job 148 µs full and ~19 µs refresh, **on a PSP-3000** [M, `review_vmegame.md`] | Untried in a game build on the **PSP-1000**: one run away. `layout_plan.md` still says it "computes nothing": stale. VMEMIX (sound mixing on it) is parked |

### The AVC decoder and the other media blocks

| In use | Note |
|---|---|
| no | No doc tries them as general hardware. Taking the ME over gives up the firmware's own use of them [D, `psp_hardware_reference.md` §2]. Unknown whether any is usable for game work [U] |

### DMA

| In use | Measured | Note |
|---|---|---|
| texture uploads (`sceDmacMemcpy`), the DMACplus for the eDRAM | 2.6–3× memcpy [M]; the CPU's misses 218 → 499 ns while it runs [M] | Other bulk copies still go through the CPU [U, to count] |

### The GE

| Feature | In use | Note |
|---|---|---|
| Hardware skinning (8 bone matrices, vertex weights) | yes, on by default (`sceGuBoneMatrix`, `GU_WEIGHTS`) | Closed |
| Hardware lights | yes on models: 2 directional + 2 point (`sceGuLight`) | **`sceGuLightSpot` is never called.** The flashlight and the level's dynamic lights are drawn as extra passes or on the ME. `big_levers.md` measured those passes at up to 14 ms of a b30 frame [M, b107; to re-measure on tock161]. Vehicles' lighting cost (Sheepdog01, B53) sits here |
| Call lists | yes, hand-built (BASE/CALL/RET) | A CALL costs 0.27 µs [M]; the bindblocks showed no saving [M] |
| Texture formats | CLUT8 and CLUT4, swizzled, mipmapped | **DXT1/3/5 never used.** The docs disagree on whether it's worth trying (`streaming.md` S6 against `halo-psp-feasibility.md`). It's lossy, so it would be a look call for Sheepdog01 |
| Mipmaps | yes, every CLUT texture | Closed |
| Fog, dither, alpha test, blend, depth, scissor, culling, texture matrix, environment map | yes | — |
| The GE's clip planes switch | enabled; no plane set | The GE clips at the near plane only; the CPU cuts far triangles [D] |
| Bounding-box culling in a list (`sceGuBeginObject`) | no | ~1.6 µs a box [feasibility note]. Saves GE work only, small [D]. The keep word `noboxtest` is a CPU test, not this |
| Morphing (`sceGuMorphWeight`) | no | Halo's models have no per-vertex blend shapes [U] |
| Patches (`sceGuDrawBezier`, `sceGuDrawSpline`) | no | Halo has no curved surfaces [U] |
| Stencil (`sceGuStencilFunc`, `sceGuStencilOp`) | no | Replaced by the stencil bake (the GE's stencil is too limited) [D] |
| Logic ops, colour test | no | No Halo material needs one [U] |
| Material colours (`sceGuColorMaterial`, `sceGuModelColor`, `sceGuAmbientColor`) | no | Colours come with the vertices [U]. Whether material colours could shrink vertices is open |

### Memory

| Pool | In use | Note |
|---|---|---|
| Main RAM, 24 MB user | yes | `memory_ledger.md` |
| VRAM, 2 MB | frame and depth buffers 816 KB, the texture cache, the lists | Nearly full [D] |
| The volatile 4 MB | the texture RAM cache | Settled in its current use (`memory_ledger.md`: short by 0.4–0.9 MB on a10 and b30 without it) [D] |
| The bus | shared by the CPU, the ME, the GE and DMA | The GE's RAM reads cost the CPU 8–18 ms of a 29–69 ms frame (b102 [M]). A priority setting: none found among the registers read (b105 [M]). Two steps remain in `psp_hardware_reference.md` §9.1: disassemble `sceDdrWriteMaxAllocate` (Sony's code, kept on our machines), then a write test in halo_bench |

### Input and output

| Feature | In use | Note |
|---|---|---|
| Memory Stick async reads | **off by default** (`psp_geo.c`) | With them on, the texture cache's synchronous reads waited behind them (2.5–5 s frames). The bench showed async reads work if the I/O priority sits above the game thread's [M, v10]. The geometry reads are synchronous now: up to 98 ms in one frame on the ladder [M, 41821725723]. A hitch source |
| sceSas, the firmware's codecs | no | Superseded by the port's own ME mixer [D] |

## 2. The gaps to chase, by what they could add

| # | Gap | Could add | First step | Where it runs |
|---|---|---|---|---|
| C1 | **The ME's capacity and scheduling.** On d20's heavy view the ME binds the frame and starves the GE | Frame time wherever the ME binds; capacity for more AI and effects | The frame timeline (ledger 3), then the scheduler's design | PSP runs (recon) |
| C2 | **The GE's hardware lights, spotlights included,** for the flashlight, the dynamic lights and vehicles | Up to the extra passes' cost (14 ms of a b30 frame in b107; to re-measure) | A census of today's light passes and their cost per light; a bench of the GE's lighting cost per vertex | Emulator census, then a PSP bench |
| C3 | **Memory Stick reads async, done right:** one reader at a priority above the game thread, serving the geometry and the textures | The streaming hitches (up to 98 ms a frame) | Design from v10's rule, then one PSP pair | Code, then the PSP |
| C4 | **The bus's priority** | If it exists, up to the GE's 8–18 ms cost to the CPU | §9.1's steps 2 and 3 | Local disassembly, then halo_bench |
| C5 | **The VME on the PSP-1000, in a game build** | A third processor for fixed work, if it computes there | One run | PSP |
| C6 | **The VFPU's coverage:** the float inventory against tock161, and the approximation ops | Tick and drawing maths | Refresh `hardware_utilization.md` §3 | Code reading, emulator |
| C7 | **DXT textures** | VRAM and RAM room (8 → 4 bits a texel on CLUT8 textures) | A bench of its sampling cost and look; Sheepdog01's call on the look | PSP bench |
| C8 | **DMA for more copies** | CPU time on bulk copies, weighed against its bus cost | Count the CPU's bulk copies | Emulator census |
| C9 | **The eDRAM's map:** all 2 MB, who holds what | Room for ME code and data | One page | Code reading |
| C10 | The rest (BBOX, clip planes, morphing, patches, stencil, logic ops, material colours, AVC, sceSas) | Small or nothing, each with its reason above | — | — |

## 3. Docs found stale (to fix)

- `hardware_utilization.md`: "the VFPU does one job in the whole port"; the scratchpad lever "was never built".
- `big_levers.md`: "two hardware lights for models" (the code sets four).
- `layout_plan.md`: the VME "computes nothing".
- `psp_hardware_reference.md` §2.3: the audio path on the main CPU (it's on the ME since b86/b87).

## 4. Still to read

- The GE's command set itself (`pspge.h`, the GE registers) for anything `pspgu` doesn't wrap.
- The Allegrex's instruction set as GCC uses it in the hot code.
- The VFPU's full instruction set against the engine's hot maths.

Started 2026-11-04.
