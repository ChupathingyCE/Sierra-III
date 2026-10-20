# Review: the memory census (branch `perf-memory`, M7)

Per map, at its worst moments: each pool, cache and arena against its size, the tag window's free span, the GE's reads by where they live; then act on what has room. Four changes: the instruments, six campaign arrays trimmed, `geowindow`, and the fixes to the census's own reading. The numbers are in `memory_census.md` (sections 2, 3, 5); this page is the walk-through.

Labels: [M] measured on a PSP, [E] emulator (the `memcensus1` batch at `2dc42f2`: ten maps' 90 s openings, the a10, a30 and b30 routes, b30 twice; `fixedticks fixedseed`, all nine runs `ok`, no strict-mode site), [D] read in code, [U] inference. Emulator milliseconds are not the PSP's; counts, bytes and shares carry over.

## 1. The instruments (`03c141a`)

| Where | What | Why |
|---|---|---|
| `psp_libc.c` `psp_heap_map`, `psp_loading.c` | `heapmap`: the heap's gaps and blocks of 16 KB or more at each level change (called before the old level is let go and after the new one loads) and at a failed request, with what asked for each | The heap fails requests with hundreds of KB free; the gaps say whether it is the sum or the shape |
| `psp_fixups.py` `FIXES_RECON` (`poolpeaks`), `psp_loading.c` | `poolpeaks` (recon builds, linked weakly): each game-state array's exact count high-water against its maximum, reported per map | The heartbeat samples once a second and misses bursts |
| `psp_geo.c` `census_read`, `census_report` | `memcensus`: the geometry cache's working set over 30/150/300/900 frames; re-reads bucketed by bytes loaded since the block's last load against the cache's size; the tag window's free span at each load | Tells a cache that is too small (re-reads beyond its size) from a policy that evicts what it should keep (re-reads under it) |
| `psp_ge.c` (one line), `psp_geo.c` `psp_geo_holds` | `gereads` already printed where the GE's reads live; its "geometry cache" test now also counts the second region | The bandwidth section stays right when `geowindow` is on |
| `psp_fixups.py` | The release `game state:` line compared a pool's bytes with its KB peak and so printed its use at that moment, not its most | Fix: a comparison in the wrong unit |
| `tools/memcensus.py` | Reads any log into per-map tables | The census |

Cost: all behind keep words (recon-only for `poolpeaks`), nothing in a default build's hot path beyond one extra range compare in `in_cache` [D].

## 2. Six arrays trimmed (`2dc42f2`, campaign only)

| Array | Was | Now | Most seen [E] |
|---|---:|---:|---:|
| object looping sounds | 1,024 | 640 | 339 (c20) |
| contrail points | 1,024 | 768 | 561 (a30 route) |
| contrails | 256 | 96 | 34 (b30 route) |
| particle systems | 64 | 32 | 12 (a10 route) |
| system particles | 512 | 448 | 244 (a10 route) |
| swarm components | 256 | 160 | 23 (c20) |

Key lines: `psp_capacity.h` (the six maxima; `PSP_POOLS_GAME_STATE_SAVED` 0x5C000 and the new `PSP_POOLS_GPU_SAVED` 0x8000, the two parts' ends the game state never allocates), `psp_fixups.py` (the game state starts 0x20000 higher, ends where it did), `psp_build.py` (`GAME_STATE_BASE` +0x64000), `tools/cachemap.py` (base 0x09304000).

**Why.** The arrays were sized from the Xbox's maxima or from early guesses; every run, hand test and census, peaked well below. The room goes to the heap, which fails requests with the data resident (the checkpoint buffer, the level copies).

**Evidence.** [E] every trimmed array under its new size on all ten maps and three routes; the tightest, contrail points, uses 73%. The heap is 5,992 KB (the hand-test build's 5,872): a10's failed requests 1 (a 109,600-byte request against a 105 KB largest gap, 350 KB free in 124 gaps), a30's and b30's 0 in the emulator, against 7, 10 and 12 on the PSP in the hand test [M]; the routes differ, so the drop is indicative. Cost: none in time. `PSP_POOLS_XBOX=1` restores the originals (campaign only; multiplayer unchanged, its build compiles: `perfmem-mp` at `2dc42f2`).

**Risk.** An encounter-heavy map past the longest play seen could pass 768 contrail points or 640 looping sounds. An overflow in these arrays fails an allocation, not memory; the contrail points are the one to watch (a30's route went 561 [E] where the earlier maximum was 431).

## 3. `geowindow` (`03c141a`, opt-in)

The tag window is 9 MB; the largest BSP of a map and its tags use less on most maps, and the animation cache takes 512 KB from the span past them. `geowindow` makes what is left the geometry cache's second region (64 KB or more, from the bake's per-map count, checked against the tags' end and the window's end at load); with none the cache is its 2 MB arena as before.

Key lines (`psp_geo.c`): `in_cache` and `psp_geo_range` / `psp_geo_holds` (the second region counts as the cache's own for `gereads` and bounds checks); `fits` (the end limit is the region's own); `allocate` (one ring through both regions: at the ring's end it goes on at the other region's start, then the first free fit, then after a protected block); the load-time setup (`extra_start`, `extra_end`) from `psp_anim_window_end()` (`psp_anim.c`).

**A/B [E], b30 route, same inputs, `geowindow` off against on** (the cache 2,048 KB against 2,360 KB):

| | off | on |
|---|---:|---:|
| misses | 2,186 | 1,824 (-16.6%) |
| loads / KB loaded | 2,120 / 12,241 | 1,772 / 10,747 (-16% / -12%) |
| evictions | 1,795 | 1,445 (-19%) |
| frames that missed something | 207 | 177 |
| synchronous read time (emulated ms) | 410 | 339 (-17%) |
| heartbeat-sampled read time; slowest read | 427 ms; 13.8 ms | 349 ms; 6.2 ms |
| draws left out | 0 | 0 |
| re-reads under the cache's size (known) | 480 | 210 |

The gap grows steadily through the route (10 fewer misses at tick 596, 280 at 14,800, 362 at the end), so the churn is not one event. The mechanism is the ring, not the raw room: 312 KB is 15% more cache, and it removes 56% of the re-reads that happened under a cache's worth of loaded bytes, the signature of the ring's gaps and protected blocks. b30's 900-frame working set is 2.4 MB; the larger cache is 2.36.

**The window on the ten openings [E].** `ladder-peaks` ran with it on: b30 +312 KB, c10 +122, c20 +1,216, c40 +66, d40 +328; a10, a30, a50, b40 have none (0-4 KB) and d20's 42 KB is under the 64 KB floor. No map failed, left out a draw it hadn't left out before, or reported a strict-mode site.

**Is play identical? Not proven.** The player's position is bit-identical for the first 38 samples (to game tick ~3,440), then the runs drift (largest gap 34 units, ~3 m). That is the harness's known limit across keep sets (`big_levers.md`, "Its limit, across builds"): two builds with different recon switches parted at tick 2,784 with a 326-unit gap. A cache that holds more or evicts differently should not change the simulation, and nothing in `geowindow` writes game state [D]; but the proof is a state-hash run (`statecrc`) of the same build with and without it, which this batch did not have. Sound-cache failures were 1 off and 9 on in the same route (the sound cache is a separate arena [D]); play drift is the likely reason and the state-hash run settles it.

**Recommendation.** Default on is warranted for the maps that have a window (b30, d40, c20, c10, c40: five of ten, +66 to +1,216 KB), on three conditions: (1) a `statecrc` run of b30 and c20 with and without it comes back identical in the game state; (2) merge with `fix-memory`'s `geofifo`, which changes `allocate()` and `fits()` in the same file: the two-region ring has to sit under whatever eviction order it brings (the hunk is about 60 lines; the region's end limit in `fits` and the ring's wrap in `allocate` are the lines that overlap), and the A/B repeats on the merged branch, since the policy loss (480 re-reads under the size) is `geofifo`'s to remove and the window's gain may shrink by the same amount; (3) a PSP run (`keepsets`) on b30 to see the read time on the stick (the emulator's milliseconds are not the stick's). It costs nothing on the five maps without a window. It is no lever for a10 (no window; the 900-frame set is 2.7 MB against 2.0): that needs the sound cache's room (MC3, `me-foundations`).

Keep word to flip it: `geowindow` (on, once default). Cost in tick time: none [D]: the hit path adds one range compare in `in_cache` (two regions instead of one).

## 4. Findings that are not changes

- **The display list can't shrink.** Fullest frame 697 KB on a10's route (8,005 draws skipped) and 716 on the PSP [M]; the world's share ends 80 KB below the end. A bigger list or fewer bytes a draw is the only way to the skipped flares, shadows and decals.
- **The objects pool can't shrink** (974-1,023 of 1,024 KB; the Xbox's size and its collector keeps it near full).
- **VRAM models 475-597 of 600 KB on every level:** nothing to give back to textures.
- **The GE's main-RAM reads are small beside the CPU's:** 0.5-0.75 MB a frame on average (14-22 MB/s at 30 fps), 1.7-2.2 MB at the worst frame (49-65 MB/s) against the CPU's 120-210 MB/s of misses [E bytes, M]; the geometry cache is the largest at the worst frames. Contention on the PSP stays the b139 ladder's +10-30% on the drawing phase's misses [M]. Bandwidth isn't the wall.
- **Two flags from the openings:** d20's sound cache failed for room 340 times in 90 s; c20 sent 155 level copies to the heap.
- **Memory-stick traffic** is small in bytes (a10's hand test: 63 MB of geometry, 35 MB textures, 58 MB animations over ~19 minutes [M]) and large in stalls (synchronous geometry reads).

## 5. What to test

Not by hand: the changes ship behind keep words or are sizes. The PSP needs one batch, after `fix-memory` merges: b30 with `keepsets="- | geowindow" repeat=2` for the read time on the stick, and a `statecrc` run on the emulator for the identity.
