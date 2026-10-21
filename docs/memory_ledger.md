# The memory ledger: what each block of RAM and VRAM holds, and what a KB of it buys

Sheepdog01, 2026-10-21 15:51-15:53: memory is now a lever. VRAM trades the texture cache against model copies; main RAM buys headroom, cache sizes and fidelity. Three digs for the master (helper `memdig`), on w140 `bc12443`, bake `b140x` (the exact CLUT4 set): (1) geometry bytes that could go without degrading the look; (2) the ledger of both pools; (3) the CLUT4 look A/B for Sheepdog01. Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference or arithmetic. Counts reused from recon's helpers (cull-lod, CTNG, survey) are cited, not redone.

## 1. Geometry: what could go without degrading the look

(pending: the geometry census, lab sierra-tags 39676064840)

## 2. The ledger, both pools

### 2.1 VRAM (2,048 KB; the same layout on every map)

w140 `psp_tex.c` [D]; use from CTNG's VRSP-784 read (sierra-hw 39661123709, PSP-1000 [M]) and the memory census (memcensus1 routes [E], hand test [M]).

| Block | KB | Peak use | What a KB of it is worth | Idle or oversized at peak? |
|---|---:|---|---|---|
| Frame buffers, 2 x 512 x 272 x 16 bit | 544 | 100% | fixed: one buffer tears (rejected, `memory_savings_b137.md` §3); the 32-pixel stride padding is 51 KB in strips, too fiddly | no |
| Depth buffer (motion sensor target inside it: `blipdepth`) | 272 | 100% | fixed; the sensor's 16 KB went to the copies' room | no |
| **Texture cache** | 616 | 99-100% on every map ([M] hand test 610-614; [E] routes 607-614) | **b30: more than a KB of copies.** Shrinking it 184 KB (616 -> 432, `vrammodels784`) took untextured frames 33/41 -> 194/581 and drawing +2.8-3.9% on the PSP [M], eating 40-55% of the copies' tick gain: rejected. c20 waits for no texture room (VSPLIT: 140 KB a frame more could move there, ~1.3 ms [E x M slope]) | b30, a30: no (binding). c20: yes, ~184 KB its textures never wait for [E] |
| **Model copies' room** (vertex buffers and level streams; indices never) | 616 (600 + `blipdepth`'s 16) | b30 483 of 616 used on average, 132 KB idle while the best candidate is refused 3 frames in 4 [M]; c20 ~37 idle, a30 ~89 [E]; 87.7% of draws from a copy (b30) | **15.9-17 us of tick a frame per KB on b30's flight and landing [M]** (+177 KB = -2.8 / -3.0 ms); ~3.4 beach [M]; c20 7.3, a30 7.5 [E slope x 9.6 us/KB] | the 132 KB of gaps (VMFILL: fill them: b30 1.0-2.1 ms flight [E x M], CTNG plan step 1) |
| **Total** | 2,048 | | | |

**What VRAM can still give:** only (a) the copies' own idle gaps (VMFILL, 66-132 KB, no texture cost), (b) texture bytes made smaller (CLUT4: the exact set freed 42 KB of b30's pack; the look set, dig 3), (c) per-map splits where textures don't wait (VSPLIT on c20, a30 if its textures allow). Every KB the copies take from the texture cache on b30 costs more in untextured frames than it saves in tick.

### 2.2 Main RAM (24 MB user RAM, plus the 4 MB volatile partition and the ME's 2 MB eDRAM)

Sizes: w140 build log (sierra-hw 39640880636: program 5.01 MB, heap 5,876 KB), `psp_capacity.h` [D], the ladder's start-up lines [M]. Peaks per map: the PSP runs of the full set (M6 #2 + `inflightkb224` + `heaproom256`) where they exist [M], else the emulator [E].

| Block | KB | Peak use | What a KB of it is worth | Idle or oversized at peak? |
|---|---:|---|---|---|
| **Program**: text (code + rodata) | 3,566 | all resident | nothing: code doesn't shrink for speed (cold units already `-Os`); assert text and desktop leftovers ~120-150 KB (`memory_savings_b137.md` S6) are the only known cut | S6, if still in: ~120-150 KB [U] |
| Program: data + bss | 155 + 1,408 | static | bss: `psp_ge.o` 407, `render` 133, player queues 82, lens flares 78 (S5 cut it), objects 69 (b137 link map [D]) | none measured |
| **Heap** (one block below the game state) | 5,876 | see below | | |
| - sound cache | 1,792 | 1,706-1,792 occupied [E]; music held at peak b30 942, a10 767, c10 415 KB, but **the rest's own peak is 1,544 / 1,582 / 1,688 KB** (music1 39663636397 [E]); d20 fails 284 loads for room at 1,792 [E] | each 512 KB less: +27% loads (b30, a30), a30 174 failed, d20 3,922 failed (rambudget1 39541738706 [E]) | **no**: the rest's peak leaves 100-250 KB, and only once music leaves |
| - level copies' arena | 800 (`heaproom256`: 256 KB of it moved to the level carve) | b30 799 of 800, a10 800 of 800, a30 671 of 672 [M/E] | let go for room = copies rebuilt; b30 3 times a run full set [M] | no |
| - display list | 768 | 716 a10, 702 a30, 703 b30 at their fullest frames, draws skipped [M, hand test]; the full set skipped 0 on a30 [M] | room for draws (flares, shadows, decals): "can't shrink" (MC4) | no |
| - in-flight record buffer (`inflightkb224`) | 224 | whole frame b30 184-197, a30 141-174, d20 104-110, c10 68-85, a10 75-77, c20 47-62, b40 23-30, d40 12-15 KB [M, S10 table] | the record of a frame: a30's fills went 62 -> 0 at 224 [M]; a ring in it gives S10b at 0 KB | **yes off b30/a30:** a10 ~147 idle, c20 ~160, d20 ~114, c10 ~140 KB at peak [U from M]; size it per map, or spend it on S10b's ring |
| - shim index buffer (calloc) | 192 | peak 97 (b137 [M]); S4 dropped: reaches its size at peak | | no |
| - stencils (`.pstn`, per map) | b30 156, c20 162, a30 230, d20 20, a10 0 | loaded whole | the walls' decals | no |
| - rasterizer pool, sounds array, small engine blocks | 96 + 86 + the rest | | | |
| - **headroom (taken at peak)** | | b30 380-392, a30 324-336, d20 336-352, c20 368 (3000, M6#2 + mevram), **a10 4 KB** [M] | a10: RSO's retained records, any table, a fight's checkpoint buffer (H4: 32,883-byte failures before MC1) | a10 is at its ceiling (1-2 failed requests a run, 107 KB and 67 KB) |
| **Game state** (below the tag window) | 3,012 | 3,003 allocated | pools at their sizes: a fight past one fails to spawn | see 2.4 |
| **Tag window** | 9,216 | tags + the largest BSP: a10 8.52 MiB ... c20 7.31 | its unused end: the animation cache (512) and, with `geowindow` (opt-in), a second geometry cache region: b30 312, c20 1,216, d20 42, a10 0, a30 4 KB [E memcensus1] | c20 1.2 MB and b30/d40 ~0.3 MB idle unless `geowindow` is on |
| **Top of user RAM** (thread stacks; outside-heap blocks: the see-through table 55, the blackbox, ~384 of contiguous blocks) | ~1,000 | free low 127-133 KB on every map [M] | | ~130 KB, kept for a thread and the blackbox (`TRANSPARENT_TABLE_LEAVES_FREE` 80) |
| **Volatile partition** | 4,096 | | | |
| - geometry (+ animation) cache, carved | 1,792 (2,048 less `heaproom256`'s 256) | 99-100% in play [M]; 900-frame working set a10 2,725, b30 2,429, a30 2,017 KB [E] | a10: 21 s of synchronous reads over the 19-minute hand test [M]; +1 MB would keep 81% of a10's known re-reads [E]: hitches, not steady ms | no: short by 0.4-0.9 MB on a10, b30 |
| - level carve | 256 floor, up to 1,024 | 256 taken at the open; a10 639 in the hand test [M] | the copies' second room | no |
| - texture RAM cache | 2,016 (2,272 before the carve) | b30 57 MB read in the hand test, 300 frames untextured [M] | untextured frames; texpretouch reads | no |
| - glyph sheet | 32 | | | |
| **ME eDRAM** | 2,048 | the ME's stack and jobs; `soundedram` (opt-in) moved 128 KB of sound there | the ME's own work | ~1.5-1.8 MB unused [D, b137] |

### 2.3 Per map at peak (heap in use and headroom, KB)

| Map | Heap in use at most | Headroom (heap top to its end) | Device, set, run |
|---|---:|---:|---|
| b30 | 5,487-5,492 (route) | 380-392 | PSP-1000, full set, 39582467478 [M] |
| a30 | 5,556-5,568 taken | 324-336 | PSP-1000, full set, 39577431764 [M] |
| a10 | 5,818 in use, 5,892 taken | **4** | PSP-1000, M6 #2 + mevram, 39572396332 [M] |
| c20 | 5,532 taken | 368 | PSP-3000, M6 #2 + mevram, 39562595232 [M] |
| d20 | 5,514-5,516 | 336-352 | PSP-3000, + inflightkb224, 39574873281 [M] |

### 2.4 The heap side: what could be freed, and its risk

| Source | KB freed | Risk | Status |
|---|---:|---|---|
| **Music leaving the sound cache** (its own ring, the audio plan's step 3) | **~0 net as heap.** The cache's non-music peak is 1,544-1,688 KB of 1,792 (b30, a10, c10 [E]); the music ring wants ~176 KB a second of PCM buffer plus the codec | low | buys sound-cache hit rate instead: music is 23% of b30's stick reads and 53% of a10's, all re-reads of evicted music [E]. d20 (284 fails at 1,792) wasn't in music1: its share is the open question |
| **Pools at measured peaks** | **~150-270 KB left**, not 430: the "~430 KB" (b137 R4) is mostly taken already: `PSP_POOLS_SMALL` gave 281 KB (actors 256 -> 160, props 768 -> 512, glow 512 -> 256) and MC1 128 KB (six arrays + the game state's unallocated ends) [D, `psp_capacity.h`]. What remains at ~1.25x the most seen: props 512 -> 354 (~48 KB), lights 896 -> 448 (~54), cluster references 6 x 2,048 -> ~970 (~76), particles 1,024 -> 684 (~37), decals 2,048 -> 1,584 (~25), hs threads 256 -> 224 (~17), glow 256 -> 160 (~9) [U from the census's peaks] | **a fight past a size fails to spawn** (the Xbox's own behaviour past its sizes). Lights fill all 896 with every encounter placed at once (b40); props reached 283 in play; long play is the check | not built |
| **In-flight buffer sized per map** | a10 ~147, c20 ~160, d20 ~114 at peak [U from M whole-frame KB] | a frame over the size draws at once (the existing fallback, no picture change [D]) | idea; conflicts with S10b's ring (that wants the 224 KB) |
| **Code** | S6 ~120-150 (asserts, desktop help, debug leftovers) [U] | none (text only) | open since b137 |
| **Sound cache to the ME's eDRAM** (R2/MC3) | 1,664-1,792 of heap | the CPU mixer fallback dies; eDRAM after sleep | `soundedram` moved 128 KB only (r2edram1 [E]); the whole cache is a build switch's work |

### 2.5 Where RAM buys most

1. **a10's heap (4 KB of headroom)** gates two levers: RSO's retained records and anything else the full set adds. The per-map in-flight buffer (~147 KB on a10) or the pool trims are its cheapest sources. Value: a lever unblocked, not ms by itself.
2. **The geometry cache, +0.4-0.9 MB** (a10, b30's 900-frame working sets): fewer synchronous reads (a10 21 s over 19 minutes [M]): hitches more than steady frame time. `geowindow` already gives b30 +312 KB and c20 +1.2 MB for free (opt-in, MC2: -16.6% misses on b30 [E]); default-on is the cheapest RAM in the ledger.
3. **VRAM copies on b30, only from idle VRAM** (VMFILL 66-132 KB: 1.0-2.1 ms a frame on the flight [E x M]), never from the texture cache there.
4. **Texture bytes** (CLUT4: exact set in; the look set is dig 3) feed both the texture cache and, where textures stop waiting, the copies' room (VSPLIT).
5. Not the sound cache: music leaving frees hit rate, not heap.

## 3. The CLUT4 look A/B (lightmaps and HUD sheets beyond the exact set)

(pending: bake c4look1, batch c4look1)
