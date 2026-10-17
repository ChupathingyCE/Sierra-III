# RAM and VRAM: where it goes, what to cut, and how to use both (2026-10-17, b137)

Sheepdog01, 09:02: places to save RAM and VRAM, and to gain speed by using both better; safe choices and riskier higher-value ones. Read by a delegate of the master session from b137's first PSP-1000 ladder (lab 39034486402), the link maps of b137's campaign and multiplayer builds, and the code. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

**Settled savings first** (`optimization_ranking.md` ~281–284 already lists S3, S4, S6 and R4's kind). **Found on the way [M]:** b137's ladders failed to load **b30's stencils** (`memalign of 159916 bytes FAILED`: 451 KB free inside the heap, no block that large); b136's ladder loaded them, and a b30 boot does. The campaign heap is nearly full too (5,441 KB peak in use, 5 KB free inside). Being reproduced (lab batch `stencilheap`).

## 1. The memory map

| Pool | Size | Consumers |
|---|---:|---|
| Program (campaign) | 4.89 MB | code 2,922 KB; rodata 516 (~350 strings); data 172; bss 1,402 (psp_ge.o 407, `render` 133, player_queues 82, lens flares 78, objects.o 69) |
| Program (multiplayer) | 6.77 MB | bss 2,794 KB, ~1.1 MB of it the network units |
| Heap (campaign) | 5,600 KB | sound cache 1,792; level copies' arena 1,056 (peak 1,047); display list 768 (peak 571 on this ladder, 601 in b114); shim index buffer 192 (peak 97); rasterizer pool 96; sounds 86; see-through table 50 |
| Game state | 3,412 KB | objects 1,024 (peak 661); actors 457 (peak 29 of 256); props 234 (235 of 768); script threads 134; render states 128; lights 108 (244 of 896); glow particles 50 (0 of 512); decals 112; particles 112 |
| Tag window | 9,216 KB | tags 3.0–6.1 MB plus the largest BSP; the animation cache in its unused end |
| Top of user RAM | ~1 MB | thread stacks 784 KB reserved, 375 used (three pthreads use 1 KB of 64 each); outside-heap blocks 384; 141 KB free in play |
| Volatile partition | 4,096 KB | geometry cache 2,048; texture RAM cache 2,024; animations 24 |
| ME eDRAM | 2 MB | the ME's stack at the top; the rest unused |
| Scratchpad | 16 KB | 15,104 B: cut corners 8,448, profiler slots 6,144, front table 512 |
| VRAM | 2,048 KB | framebuffers 2 × 272 (5650, stride 512); depth 272; motion sensor 16; glyph sheet 32; textures 592; vrammodels' room 592 (models and level streams) |

## 2. Safe savings

| # | What | Saves | Owner |
|---|---|---:|---|
| S1 | **Multiplayer client-only cut:** the co-op units (178 + 86 + 12 KB), `update_server_globals` 128, `late_joiner_players` 64, `objects_host_*` 94, `distributed_host_*` 18, sized to 0/1 under `PSP_MULTIPLAYER` (the build never hosts: `mp_v075a_and_host_memory.md` §2) | ~580 KB of multiplayer heap; ~300 more with code changes | multiplayer |
| S2 | **Multiplayer game-state pools:** actors 457 and props 234 peak at 0 in multiplayer (`mp_map_survey_prelim.md` §3) | ~690 KB | multiplayer |
| S3 | Thread stacks 64 → 16 KB (the Xbox's own ask; 1 KB used), vblank 32 → 8, the freed room given to a heap user | ~170 KB, both builds | master |
| S4 | The shim's dynamic geometry sized to its peaks (triangles 192 KB, peak 97; type 5 64 KB, peak 0; type 9 16, peak 0) | ~150 KB | master (check overflow behaviour, menus, multiplayer) |
| S5 | Lens-flare arrays for 8,192 markers against 4,616 at most on the ten maps | ~30 KB | master |
| S6 | Assert text, desktop-only help, debug leftovers (`ai_profile`, `debug_path`, `simulated_sums`, `ringtest`…) | ~120–150 KB | master |
| ~~S7~~ | ~~Display list 768 → 640 KB~~: **dropped** (the reviewer): `optimization_ranking.md` ~323 lists it under "Settled: don't chase again" (b30 filled the list at 512 KB; grown in b107) | — | — |

## 3. Higher value, riskier

| # | Lever | Expected | Risk | First experiment |
|---|---|---|---|---|
| R1 | **VRAM toward vertex data:** vrammodels past half (784–800 KB) with textures ~400; better, textures and vertex buffers ranked together in one VRAM arena per map | 1–3.5 ms a frame on b30, a30, d20 [U] (texels cost the CPU ~0, the GE ~1.5 ms a frame for the whole set [M, b115]) | more textures from RAM (~0.5 ms of GE), pop-in | relax `psp_tex.c` ~213; `vrammodels784` against the default with `gereads gebusy` on the PSP-1000 |
| R2 | **The sound cache in the ME's eDRAM** (squares with `big_levers.md` §0's ME-only eDRAM plan only if the ME's own work leaves the 1.5–1.8 MB free: §0 ~55, ~58 keeps it for ME jobs) | 1.5–1.8 MB of heap, both builds | the main CPU can't see eDRAM (the CPU mixer fallback dies: a build switch); stick reads through an ME copy; eDRAM after sleep | `edramcheck` over a ladder; an ME job mixing from eDRAM against `audiodump` |
| R3 | DXT1 textures | ~2× texels in the same caches [U] | **against `halo-psp-feasibility.md` ~151, ~576** (swizzled CLUT4/CLUT8 are the GE's fastest; "no DXT"): only with a PSP measurement that overturns it; looks (Sheepdog01's A/B for non-DXT sources) | one map, `gebusy` on the PSP-1000 first |
| R4 | Campaign pools to their peaks (actors 256 → 128, props 768 → 384, lights 896 → 448, glow particles → 128) | ~430 KB | below the Xbox's limits: a fight that overflows fails to spawn | long play through every fight for peaks |
| R5 | Kernel partition 4's free 1 MB | 1 MB | speculative (kernel allocation, protection, sleep) | query and pattern-check over a ladder and a sleep |

Rejected: one framebuffer (tears: detracts); scan-out from main RAM (costs more than it frees under the bus tax); the stride padding (51 KB, too fiddly).

## 4. Value per KB moved into VRAM

Each 100 KB a frame of GE reads taken off main RAM saves ~0.8–1 ms of frame [U, from c20's levelvram −4.6 ms for 558 KB a frame and b137's accident]. A KB of VRAM is worth about (reads a byte a frame) × 0.01 ms: the hottest model buffers 0.3–0.5 ms a KB (already in); models ranked 400–600 KB 0.02–0.03; the level streams' best 256 KB 0.015–0.03; models 600–800 KB and b30's contested streams 0.01–0.017 (R1); the display list 0.002–0.003; **the worst third of the VRAM texture set ~0: give it up first.**

## 5. Short list

- **Safe:** S1 + S2 (multiplayer, ~1.27 MB against Damnation's 16 KB margin: in progress as `mpcut`, heap ceiling 3,668 → 4,632 KB [D]); S3 + S5 (master: done in `mem138`; S4 dropped, its buffers reach their sizes at peak [M]); b30's stencil failure (fixed in `mem138`).
- **Higher value:** R1 (renderer: the best ms per KB left); R2 (recon's `edramcheck` first, then the master); R4's peaks (recon). R3 after R1.
