# The memory census (M7): each pool, cache and arena against its size, per map

The measurement round's M7 (`plan_measurement_round.md`, on branch `perfplan`): per map, at its worst moments, each pool's, cache's and arena's peak against its size, the tag window's free span, and the bus's traffic and contention. Labels: [M] measured on a PSP, [E] emulator, [D] read in code or data, [U] inference.

**Status (2026-10-20, first pass):** sections 1-4 are from the b139 hand test on the PSP-1000 (a10, a30, b30, the start of b40), the b139 ladder on the PSP-3000 (each map's opening, `hwprofile`), every earlier `poolpeaks` run (long play; every encounter placed at once) and the b139a bake's log. The emulator census over the same play (the `memcensus` keep word: the geometry cache's working set and re-reads by distance; `heapmap`; `gereads`; recon `poolpeaks`) is in flight; its numbers go into sections 2, 3 and 5.

Read any log with `tools/memcensus.py log.txt [log2 ...]`.

## 1. The answer in short

- **Almost everything is full in play.** On a10, a30 and b30 [M, PSP-1000 hand test]: the heap 99-100% (7-12 failed requests a map), the geometry cache 99-100%, the sound cache 100%, the level copies' arena 100% (a10, b30), VRAM's model share 97-99% and its texture share 99-100%, the display list 91-93% with draws skipped, the render-state cache 512 of 512, dynamic geometry 99% (a10, a30).
- **The display list can't shrink: it is full.** Its fullest frame reached 716 of 768 KB (a10), 702 (a30), 703 (b30) [M]; the world's share ends 80 KB below the end (the HUD's and the see-through draws' reserves), so those frames were at the limit, and draws were skipped: 47,686 on a10 (flares, shadows, decals, lights), 1,430 on a30, 1,390 on b30 [M]. The "296 KB" in the plan was a fullest-frame figure from a quieter window; the ladder's openings peak at 301-565 KB [M, PSP-3000], play goes higher.
- **The objects pool can't shrink either:** 1,023 of 1,024 KB on seven of ten maps [E, poolpeaks], 956 KB in a10's play [M]. 1 MB is the Xbox's own size; its garbage collection keeps the pool near full by design [D, `objects.h` GARBAGE_LIMIT_*], so running full is the original's behaviour.
- **Arrays with room:** six campaign arrays peak well under their sizes in every run (section 2), and the game state never allocates 68 KB of itself. Sized from the peaks, they give the heap **128 KB** (perf-memory `2dc42f2`). The heap is where it's needed: it failed the checkpoint writer's 32,883-byte buffer (H4, H40) and the level copies' memaligns.
- **The geometry cache's room has to come from elsewhere:** the tag window's span no BSP uses, past the animation cache's 512 KB, is 300 KB or more on b30, d40 and c20 and ~0 on a10, a30, a50, b40 (section 4). `geowindow` (opt-in) makes it the cache's second region. The large source is the sound cache's 1.8 MB if `me-foundations` moves it to the ME's eDRAM.
- **VRAM's model share is not unused on levels:** 536-597 of 600 KB on every map [M]; only the menu leaves it empty (35 KB). Nothing to move back to textures.

## 2. The game state

Each array's most in any run, against its size (`poolpeaks` [E]: poolpeaks2, poolroute1, poolroute2 with every encounter placed at once, heapladder1/2; the hand test's heartbeat lines [M] where higher):

| Array | Most | Map | Size (KB) | Verdict |
|---|---:|---|---:|---|
| objects pool | 1,023 KB | seven maps | 1,024 | full by design (the Xbox's) |
| actor | 139 | b40, every encounter | 160 (285) | R4's |
| prop | 283 | b30 (hand test) | 512 (156) | keep: play passed the every-encounter bound (258) |
| hs thread | 179 | b40 | 256 (134) | keep |
| cached object render states | 512 | b30 | 512 (128) | full: misses rework the lighting (a speed lever, not a cut) |
| decals | 1,266 | b30 | 2,048 (112) | keep |
| particle | 547 | b30 | 1,024 (112) | keep (H28 wants more particles, not fewer) |
| lights | 896 | b40, every encounter | 896 (108) | full at the bound |
| effect | 256 | d20 | 256 (63) | full: the Xbox's size |
| system particles | 244 | a10 | 512 → **448** | trimmed |
| contrail point | 431 | b30 | 1,024 → **768** | trimmed |
| object looping sounds | 347 | c20 | 1,024 → **640** | trimmed |
| particle systems | 12 | a10 | 64 → **32** | trimmed |
| contrail | 31 | b30 | 256 → **96** | trimmed |
| swarm component | 81 | c20 | 256 → **160** | trimmed |
| glow particles | 128 | b30 (hand test) | 256 | keep |
| cluster references (six) | 714-777 | d20, b30 | 2,048 each (24) | keep: a full one unlinks objects from clusters |

The six trims give 70,528 bytes; the CPU part's end never allocated 36 KB and the GPU part (decal vertices only, exactly 0x28000) 32 KB [D, the hand test's `game state layout` lines]. The game state starts 0x20000 higher and ends where it did: heap +128 KB (`psp_capacity.h`, campaign only). [E] proof: memcensus1's recon runs (all ten maps' openings and the three routes) log every trimmed array's peak against its new size.

**Instrument fix:** the release build's `game state:` line compared a memory pool's bytes with its KB peak, so it printed the pool's use at that moment, not its most [D]; fixed in `03c141a`. Its array peaks are kept since boot, not per map: a later map's figure is the most since launch.

## 3. The caches and arenas

The most of each in play [M, PSP-1000 hand test; the PSP-3000 ladder's openings in brackets]:

| | Size | a10 | a30 | b30 | Churn over the map (a10 / a30 / b30) |
|---|---:|---:|---:|---:|---|
| heap (top) | 5,872 KB | 5,868 | 5,840 | 5,848 | failed requests 7 / 10 / 12 |
| display list (fullest frame) | 768 KB | 716 [327] | 702 [434] | 703 [565] | draws skipped 47,686 / 1,430 / 1,390 |
| geometry cache | 2,048 KB | 2,036 | 2,038 | 2,022 | read 62.9 / 6.3 / 33.7 MB; 21.2 / 2.6 / 12.3 s of reads |
| texture RAM cache | 1,992 KB | (no occupancy line) | | | read 35 / 20 / 57 MB; 229 / 89 / 300 frames untextured |
| sound cache | 1,792 KB | 1,792 | 1,792 | 1,792 | failed for room 20 / 0 / 4 |
| level copies' arena | 1,056 KB | 1,056 | 776 | 1,056 | let go for room 2,614 / - / 1,147 |
| level carve (texture RAM) | 768 KB | 639 | 0 | 128 | |
| VRAM, models | 600 KB | 592 | 588 | 581 | full 2,591 / 3,254 / 1,789 times |
| VRAM, textures | 616 KB | 614 | 613 | 610 | |
| animation cache | 512 KB | 488 in the window | 504 | 512 | a10: 58 MB read |
| dynamic geometry (triangles) | 192 KB | 191 | 191 | 102 | |

**H32's churn (a10 600-720 s, b30 690-704 s):** the cache reads 0.3-3.7 MB every ~20 s window through a10's play [M], and its resident bytes sit at 1,400-1,900 KB while it evicts, so part of the churn is the ring's gaps and protected blocks (the eviction policy: `fix-memory`), part the size. `memcensus`'s re-read distances split the two: a re-read at a distance under the cache's size is the policy's; over it, the size's, and the bucket says how much more would have kept it. [E] numbers: memcensus1.

## 4. The tag window

9 MB at 0x09600000. Its use per map (the bake's count: the tags and the largest BSP [D, b139a bake log]), and the span past the animation cache's 512 KB:

| Map | Used (MiB) | Free (KB) | Past the animation cache (KB) |
|---|---:|---:|---:|
| b40 | 8.94 | 61 | 0 (the animation cache takes 448 KB from the texture RAM cache) |
| a50 | 8.70 | 307 | 0 |
| a10 | 8.52 | 492 | 0 |
| a30 | 8.50 | 512 | 0 |
| d20 | 8.46 | 553 | ~40 |
| c40 | 8.43 | 584 | ~70 |
| c10 | 8.38 | 635 | ~120 |
| b30 | 8.19 | 829 | ~318 |
| d40 | 8.17 | 850 | ~338 |
| c20 | 7.31 | 1,731 | ~1,219 |

The free span is per map and per bake and not guaranteed. `geowindow` uses only the bake's span no BSP of that map uses (pack header words 8-9, checked against the tags' end and the window's end at load), and with none the cache is its 2 MB arena as before. `memcensus` logs the span at each load.

## 5. Bandwidth and contention

**Traffic by source** (what crosses the main-RAM bus):
- **The CPU's misses and write-backs:** 120-210 MB/s while each phase runs (64-byte lines; `hwprofile`'s I- and D-misses and write-backs per M cycles, b139 ladder, PSP-3000 [M]). The tick moves the most lines per cycle (7,400-9,800 per M cycles), drawing 5,600-7,400.
- **The GE's reads from main RAM:** `gereads` KB a frame by place (level copies, geometry cache, display list, the rest) times fps [E bytes; they don't depend on the CPU]: memcensus1.
- **The display list:** written by the CPU through the uncached view and read by the GE: its fullest frames 300-716 KB, so up to ~2 × 0.7 MB a frame.
- **DMA from the Memory Stick:** the caches' loads; a10's play read 63 MB of geometry, 35 MB of textures, 58 MB of animations, 137 MB of sound over ~19 min [M]: small in bytes (~0.25 MB/s), large in stalls (the geometry reads are synchronous: 21 s on a10).
- **The ME:** the mixer and ring jobs; `me-foundations`' stage 0a (mebus by kind, both PSPs) measures what its traffic costs the CPU; recon's mebus (b30, PSP-1000): the main CPU's tick +11.9% with the ME on scattered misses, +5% with arithmetic between [M, `recon_134.md`].

**How busy, how much each slows the others:**
- `hwprofile`'s "bus" counter tracks the CPU's memory-wait cycles (correlation 0.99 with the wait share over 301 windows) [M]: it measures stall, not traffic, and "local bus" reads 0 on the PSP-3000.
- **Wait cycles per line moved, by phase** (b139 ladder, PSP-3000 [M]): the tick 59-64 on every map (a quiet-bus miss is ~72 cycles, halo_bench v7, write-backs cheaper); drawing 64-81 (a10 81, d40 81, a50 78, b30 78, a30 75): the GE's and the list's traffic add 10-30% to each of the drawing phase's misses. b93 measured a doubling before vrammodels and levelvram moved the GE's hottest reads into VRAM; most of that tax is gone [U].
- The memory-wait share is 41-62% of every phase's cycles on every map [M]: the waits are the CPU's own misses, mostly instruction misses (the tick's I-misses outnumber its D-misses about 3:1), not contention.

**What follows [U]:** bandwidth isn't the wall; latency of the CPU's own misses is. Moving bytes off the bus helps the drawing phase by at most its 10-30% contention share; the tick's waits need fewer misses (`plan_memory_stalls.md`).

## 6. Levers (the lab's `levers.tsv`)

| Id | Lever | State |
|---|---|---|
| MC1 | Six campaign arrays and the game state's unallocated ends sized from peaks: heap +128 KB | built `2dc42f2`; memcensus1 checks |
| MC2 | `geowindow`: the tag window's spare span as the geometry cache's second region (b30 +318 KB, d40 +338, c20 +1.2 MB) | built `03c141a`, opt-in; memcensus1's b30 A/B |
| MC3 | The geometry cache from the sound cache's 1.8 MB if it moves to eDRAM | idea (`me-foundations`) |
| MC4 | The display list is full in play: draws skipped (flares, shadows, decals, lights) | finding: needs room, not less |
| MC5 | Render states full (512 of 512, b30): each miss reworks the object's lighting | idea: measure the misses' cost |
