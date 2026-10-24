# The memory census (M7): each pool, cache and arena against its size, per map

The measurement round's M7 (`plan_measurement_round.md`, on branch `perfplan`): per map, at its worst moments, each pool's, cache's and arena's peak against its size, the tag window's free span, and the bus's traffic and contention. Labels: [M] measured on a PSP, [E] emulator, [D] read in code or data, [U] inference.

**Status (2026-10-20, second pass):** sections 1-4 are from the b139 hand test on the PSP-1000 (a10, a30, b30, the start of b40), the b139 ladder on the PSP-3000 (each map's opening, `hwprofile`), every earlier `poolpeaks` run (long play; every encounter placed at once) and the b139a bake's log. Sections 2, 3 and 5 now also carry the emulator census at `2dc42f2` (the lab's `memcensus1` batch: the ten maps' openings with `poolpeaks` and with `heapmap gereads`, the a10, a30 and b30 routes from the hand test, and b30 twice, `geowindow` off and on) [E]. Emulator figures are counts and bytes (what each pool, cache and arena holds, what the GE reads); the milliseconds are the emulator's and carry over only as shares.

Read any log with `tools/memcensus.py log.txt [log2 ...]`.

## 1. The answer in short

- **Almost everything is full in play.** On a10, a30 and b30 [M, PSP-1000 hand test]: the heap 99-100% (7-12 failed requests a map), the geometry cache 99-100%, the sound cache 100%, the level copies' arena 100% (a10, b30), VRAM's model share 97-99% and its texture share 99-100%, the display list 91-93% with draws skipped, the render-state cache 512 of 512, dynamic geometry 99% (a10, a30).
- **The display list can't shrink: it is full.** Its fullest frame reached 716 of 768 KB (a10), 702 (a30), 703 (b30) [M]; the world's share ends 80 KB below the end (the HUD's and the see-through draws' reserves), so those frames were at the limit, and draws were skipped: 47,686 on a10 (flares, shadows, decals, lights), 1,430 on a30, 1,390 on b30 [M]. The "296 KB" in the plan was a fullest-frame figure from a quieter window; the ladder's openings peak at 301-565 KB [M, PSP-3000], play goes higher.
- **The objects pool can't shrink either:** 1,023 of 1,024 KB on seven of ten maps [E, poolpeaks], 956 KB in a10's play [M]. 1 MB is the Xbox's own size; its garbage collection keeps the pool near full by design [D, `objects.h` GARBAGE_LIMIT_*], so running full is the original's behaviour.
- **Arrays with room:** six campaign arrays peak well under their sizes in every run (section 2), and the game state never allocates 68 KB of itself. Sized from the peaks, they give the heap **128 KB** (perf-memory `2dc42f2`); the emulator census over all ten maps and three routes found every trimmed array under its new size (tightest: contrail points 561 of 768) and the heap's failed requests down to 1 on a10 and 0 on a30 and b30. The heap is where it's needed: it failed the checkpoint writer's 32,883-byte buffer (H4, H40) and the level copies' memaligns.
- **The geometry cache's room has to come from elsewhere:** the tag window's span no BSP uses, past the animation cache's 512 KB, is 300 KB or more on b30, d40 and c20 and ~0 on a10, a30, a50, b40 (section 4). `geowindow` (opt-in) makes it the cache's second region: on b30 it cut geometry misses 16.6% and the synchronous read time 17% in the emulator (section 3). The large source is the sound cache's 1.8 MB if `me-foundations` moves it to the ME's eDRAM.
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

**The trims hold [E, memcensus1 at `2dc42f2`].** Recon `poolpeaks` over the ten maps' 90 s openings (ladder) and the a10, a30 and b30 routes (deathless, `fixedticks fixedseed`); no run logged a full pool, a failed allocation of the game state or a strict-mode site (all nine runs `ok`, `strict: 0 distinct sites`):

| Array | New size | Most in any run | Where | Use |
|---|---:|---:|---|---:|
| system particles | 448 | 244 | a10 route | 54% |
| contrail point | 768 | 561 | a30 route (ladder 215, d20) | 73% |
| object looping sounds | 640 | 339 | c20 opening | 53% |
| particle systems | 32 | 12 | a10 route | 38% |
| contrail | 96 | 34 | b30 route | 35% |
| swarm component | 160 | 23 | c20 opening | 14% |

The tightest is the contrail points: the a30 route reached 561 of 768 (the earlier peak, 431, was b30's), so 207 are left; the hand test's longest play is the check on it. The routes here are shorter than the hand test's (a10 8,261 KB of geometry read against the hand test's 63 MB), so the other arrays' table above stays on the hand test's and earlier every-encounter figures; these runs add no higher figure for any array except contrail point, the objects pool (974 KB, b30 route) and the effect array (256 of 256, d20 opening: full, as before).

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

**The same census in the emulator [E, memcensus1; the routes are the hand test's inputs, shorter in play; ladder = each map's 90 s opening].** Most of each against its size, after the trims (heap 5,992 KB; the hand-test build's was 5,872):

| | Size | a10 route | a30 route | b30 route | b30 `geowindow` |
|---|---:|---:|---:|---:|---:|
| heap (top) | 5,992 KB | 5,968 | 5,680 | 5,716 | 5,684 |
| heap requests failed | | 1 | 0 | 0 | 0 |
| display list (fullest frame) | 768 KB | 697 | 675 | 563 | 563 |
| geometry cache (resident) | 2,048 (2,360 on) | 2,001 | 2,042 | 2,012 | 2,307 |
| sound cache (occupied) | 1,792 KB | 1,792 | 1,784 | 1,784 | 1,792 |
| level copies' arena | 1,056 KB | 1,056 | 761 | 1,049 | 1,054 |
| level carve | 768 KB | 128 | 0 | 0 | 0 |
| VRAM, models | 600 KB | 590 | 570 | 580 | 587 |
| VRAM, textures | 616 KB | 614 | 607 | 614 | 614 |
| dynamic geometry (triangles / type 6) | 192 / 96 KB | 135 / 35 | 90 / 62 | 97 / 60 | 97 / 54 |
| draws skipped by a full list | | 8,005 | 0 | 0 | 0 |
| geometry loaded over the route | | 8.1 MB | 3.3 MB | 12.0 MB | 10.5 MB |
| animation cache reads | | 52 MB | 0.9 MB | 5.2 MB | 5.1 MB |

The ladder's openings (90 s, the PSP's own start of each map) peak lower on everything but the geometry cache and the sound cache (those fill in the first minute): heap top 5,140-5,628 KB (c20 highest), display list 299-603 KB (b30), geometry cache 931-2,034 KB (c40 lowest, d20 highest), sound cache 1,744-1,792 KB, level copies' arena 417-1,055 KB (d20, c20 and b30 above 1,000), VRAM models 475-595 KB (a50 lowest: 475), VRAM textures 592-615 KB. The ladder's geometry reads per map: 0.9-4.3 MB (d20 highest, 323 evictions; the others evict 1-27). Two flags from the openings: **d20's sound cache failed for room 340 times** in 90 s, and **c20 sends 155 level copies to the heap** (its arena is full with 1,026 KB).

**What the numbers say about each:**
- **Heap:** the +128 KB took a10's failed requests to 1 and a30's and b30's to 0 in the emulator (the hand test on the PSP had 7, 10 and 12) [E vs M: the routes differ, so the drop is indicative, not a measurement of the trim alone]. a10 still ends at 100%: its one failure asked 109,600 bytes with the largest gap 105 KB (2 KB short) and 350 KB free in 124 gaps (`heapmap`): fragmentation, not the sum. The room is there; the policy is `fix-memory`'s.
- **Display list:** full on a10's route (697 KB, 8,005 draws skipped), no draws skipped on the others in these routes; the hand test's PSP play went to 703-716 KB on all three. It stays a full pool in the places that matter.
- **Level copies' arena:** 100% on a10 and b30 again; let go for room 2,802 times on a10, 198 on b30, 0 on a30.
- **VRAM:** both shares 95-100% on every route; nothing to hand back.

**The geometry cache's working set** (KB of blocks wanted in the last N frames; the most of any window) against its 2,048 KB:

| Route | 30 frames | 150 | 300 | 900 |
|---|---:|---:|---:|---:|
| a10 | 1,502 | 1,784 | 2,153 | **2,725** |
| a30 | 1,097 | 1,252 | 1,787 | 2,017 |
| b30 | 842 | 981 | 2,109 | **2,429** |

The 150-frame set (5 s) fits on all three; the 300-frame set already reaches the cache on a10 and b30, the 900-frame set (30 s) exceeds it by 0.7 MB (a10) and 0.4 MB (b30). So the cache holds what is drawn now and is missing what comes back within half a minute.

**Re-reads by distance** (blocks loaded again; distance = bytes loaded since that block's last load, against the cache's size; a bucket "+1 MB" means a cache up to 1 MB larger would have kept it, to a first approximation). `first reads` includes blocks whose history was dropped when the entry table recycled the slot (`forgotten`), so those are re-reads at an unknown, long distance:

| Route (loads) | Under the size (policy) | +256 KB | +512 KB | +1 MB | +2 MB | +4 MB | more | forgotten |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| a10 (826) | 89 | 9 | 29 | 29 | 4 | 31 | 2 | 121 |
| a30 (577) | 45 | 3 | 4 | 31 | 0 | 0 | 0 | 0 |
| b30, `geowindow` off (2,120) | 480 | 9 | 18 | 54 | 113 | 56 | 69 | 809 |
| b30, `geowindow` on (1,772) | 210 | 31 | 50 | 34 | 57 | 92 | 2 | 784 |

Reading it: on a10, 46% of the known re-reads are the policy's (evicted with less than a cache's worth loaded since), and +1 MB would have kept 81%; the working set says +0.7 MB. On b30 (off), 480 of 799 known re-reads (60%) are under the size, and only 9 more lie within +256 KB: the cache's 2.0 MB is not what is missing there; the ring's gaps and protected blocks are. With the 312 KB window (+15% room) the under-the-size count falls from 480 to 210 (-56%): the extra region lets the ring place blocks it could not before. Of b30's 2,120 loads, 809 are re-reads of forgotten entries, so the true re-read share is about three quarters; the ones that matter are the near ones.

**H32's churn on b30, `geowindow` off against on [E]** (the same route and inputs, `fixedticks fixedseed`; the 690-704 s stretch is game ticks ~20,700-21,100). Both sides, over the whole route:

| | off | on | change |
|---|---:|---:|---:|
| geometry cache | 2,048 KB | 2,360 KB | +312 KB |
| misses (cache missed a draw) | 2,186 | 1,824 | -16.6% |
| loads / KB loaded | 2,120 / 12,241 | 1,772 / 10,747 | -16% / -12% |
| evictions | 1,795 | 1,445 | -19% |
| frames that missed something | 207 | 177 | -14.5% |
| synchronous read time (300-frame lines) | 410 ms | 339 ms | -17% |
| read time, heartbeat sampled (`io:`) | 427 ms, slowest read 13.8 ms | 349 ms, slowest read 6.2 ms | -18% |
| draws left out | 0 | 0 | |

The curve is the same shape from the start (10 fewer misses by 596 ticks, 280 fewer by 14,800, 362 by the end): the gap is steady, not one event. In the stretch itself the heartbeat sampled 70 geometry reads (15.6 ms) in two seconds with it off and 4 reads (0.5 ms) with it on; one sample each, on diverged play: read it as direction, not size. **Play is not identical** between the two sides: the player's position is bit-identical for the first 38 samples (to tick ~3,440), then the two routes drift apart (largest gap 34 units, about 3 m); two builds of the same code diverge earlier still (b30 route against the `poolpeaks` build: tick 2,784, gap 326 units), so the drift is the harness's known limit across builds and keep sets (`big_levers.md`, "Its limit, across builds": two sets of keep words that change the emulator's speed part between ticks 1,326 and 1,357; the same build twice stays identical) and not evidence `geowindow` changes the game. What the comparison can't prove is that it doesn't; the proof is a state-hash run on the same build, which this batch didn't have.

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

**Measured at load [E, memcensus1's ladder].** `memcensus`'s span line on each map's opening (the span no BSP of the map uses, minus what the animation cache took): a10 0, a30 4, a50 2, b40 2, d20 42, c40 66, c10 122, b30 312, d40 328, c20 1,216 KB. They match the table (within 10 KB), so the bake's count is what the game finds. The ladder with `geowindow` on (the `ladder-peaks` run) took the window on b30 (+312 KB), c10 (+122), c20 (+1,216), c40 (+66) and d40 (+328) and left the cache alone on the other five (d20's 42 KB is under the 64 KB floor); no map failed, logged a left-out draw it didn't have off, or reported a strict site.

## 5. Bandwidth and contention

**Traffic by source** (what crosses the main-RAM bus):
- **The CPU's misses and write-backs:** 120-210 MB/s while each phase runs (64-byte lines; `hwprofile`'s I- and D-misses and write-backs per M cycles, b139 ladder, PSP-3000 [M]). The tick moves the most lines per cycle (7,400-9,800 per M cycles), drawing 5,600-7,400.
- **The GE's reads from main RAM [E bytes; they don't depend on the CPU; memcensus1's `gereads`, KB a frame by where the data lives]:**

| Route (reports) | VRAM (not the bus) mean / most | Main RAM mean / most | Of the most: level copies | geometry cache | display list | heap or program | At 30 fps, most |
|---|---:|---:|---:|---:|---:|---:|---:|
| a10 (56) | 1,307 / 2,559 | 750 / 2,211 | 656 | 695 | 514 | 347 | 65 MB/s |
| a30 (19) | 2,991 / 5,761 | 602 / 1,673 | 489 | 866 | 209 | 328 | 49 MB/s |
| b30 (49) | 1,893 / 5,788 | 589 / 2,128 | 569 | 1,334 | 161 | 305 | 62 MB/s |
| the ladder's ten openings (91) | 1,681 / 5,947 | 475 / 1,751 | | | | | 51 MB/s |

(Main RAM = level copies + geometry cache + display list + heap or program; its columns are each place's own most, so they don't sum to the total's. The heap or program's index reads, 150-480 KB a frame, are inside it.) The GE reads **0.5-0.75 MB a frame from main RAM on average, 14-22 MB/s at 30 fps, and 2 MB a frame at its worst (~65 MB/s)**, against 120-210 MB/s of CPU misses: the GE is a tenth to a sixth of the CPU's own traffic on average and a third to a half at its worst frame. The VRAM reads (1.3-3 MB a frame on average, 6 MB at most) are off the bus. The geometry cache is the GE's biggest main-RAM reader at the worst moments (b30 1.3 MB a frame, a30 0.9) because models still draw from it where they don't fit VRAM (`gereads`' fit curve, two b30 reports: VRAM holding the best 600 KB of those buffers would serve 76% and 93% of the reads that go to main RAM now, 1 MB 93% and 100% [E]); the level copies come next. `geowindow` doesn't change the picture (b30: most 1.8 MB a frame on against 2.1 off; different play).
- **The display list:** written by the CPU through the uncached view and read by the GE: its fullest frames 300-716 KB, so up to ~2 × 0.7 MB a frame.
- **DMA from the Memory Stick:** the caches' loads; a10's play read 63 MB of geometry, 35 MB of textures, 58 MB of animations, 137 MB of sound over ~19 min [M]: small in bytes (~0.25 MB/s), large in stalls (the geometry reads are synchronous: 21 s on a10).
- **The ME:** the mixer and ring jobs; `me-foundations`' stage 0a (mebus by kind, both PSPs) measures what its traffic costs the CPU; recon's mebus (b30, PSP-1000): the main CPU's tick +11.9% with the ME on scattered misses, +5% with arithmetic between [M, `recon_134.md`].

**How busy, how much each slows the others:**
- `hwprofile`'s "bus" counter tracks the CPU's memory-wait cycles (correlation 0.99 with the wait share over 301 windows) [M]: it measures stall, not traffic, and "local bus" reads 0 on the PSP-3000.
- **Wait cycles per line moved, by phase** (b139 ladder, PSP-3000 [M]): the tick 59-64 on every map (a quiet-bus miss is ~72 cycles, halo_bench v7, write-backs cheaper); drawing 64-81 (a10 81, d40 81, a50 78, b30 78, a30 75): the GE's and the list's traffic add 10-30% to each of the drawing phase's misses. b93 measured a doubling before vrammodels and levelvram moved the GE's hottest reads into VRAM; most of that tax is gone [U].
- The memory-wait share is 41-62% of every phase's cycles on every map [M]: the waits are the CPU's own misses, mostly instruction misses (the tick's I-misses outnumber its D-misses about 3:1), not contention.

**What the emulator adds [E].** Bytes: the GE's main-RAM reads are small beside the CPU's, and the largest single source is the geometry cache, not the display list (up to 0.5 MB a frame on a10, where the list is full). The phase table (`memcensus.py`, per busiest windows) is from a PSP's `hwprofile` log only; the emulator's `ge_ram_MBs` (GE bytes times the emulated fps) tops out at 52 MB/s on b30 and 38 MB/s on a30. Hardware stalls per phase stay the b139 ladder's (above): that is a PSP-3000 figure and the emulator can't add to it.

**What follows [U]:** bandwidth isn't the wall; latency of the CPU's own misses is. Moving bytes off the bus helps the drawing phase by at most its 10-30% contention share; the tick's waits need fewer misses (`plan_memory_stalls.md`).

## 6. Levers (the lab's `levers.tsv`)

| Id | Lever | State |
|---|---|---|
| MC1 | Six campaign arrays and the game state's unallocated ends sized from peaks: heap +128 KB | built `2dc42f2`, checked (memcensus1): all ten maps and three routes under the new sizes (tightest: contrail points 561 of 768); a10's failed requests 1, a30's and b30's 0 in the emulator |
| MC2 | `geowindow`: the tag window's spare span as the geometry cache's second region (b30 +312 KB, d40 +328, c20 +1,216) | built `03c141a`, opt-in, checked: b30 misses -16.6%, loads -16%, read time -17%, nothing worse on the ten openings; default-on recommended after a same-state proof and merge with `fix-memory` (`review_perf-memory.md`) |
| MC3 | The geometry cache from the sound cache's 1.8 MB if it moves to eDRAM | idea (`me-foundations`); a10's 900-frame working set is 2.7 MB, b30's 2.4, so +0.7 MB would hold a10's |
| MC4 | The display list is full in play: draws skipped (flares, shadows, decals, lights) | finding: needs room, not less (a10 route 8,005 skipped in the emulator, 47,686 on the PSP) |
| MC5 | Render states full (512 of 512, b30): each miss reworks the object's lighting | idea: measure the misses' cost |
| MC6 | Bus census: the GE reads 0.5-0.75 MB a frame from main RAM (14-22 MB/s; 65 MB/s at its worst frame); the waits are the CPU's own misses | measured [E bytes, M waits] |
