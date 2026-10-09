# Geometry streaming (build 68): render data out of the tag window

2026-10-04, read-ahead analysis added 2026-10-06 (b87). Stage 2 of the campaign memory plan (see campaign_census). Labels: [M] measured, [E] estimated, [U] untested on hardware.

## Why

A campaign map's tags are 14–17 MB against the PSP-1000's 9 MB window. 4.5–5.4 MB of that is model vertex and index data, and 2–4 MB more is the level's render vertices in the BSP. The game's logic never reads that data; the renderer draws it. So it leaves the map and streams.

## The bake: invader-build -G

`invader-build … -G out/<map>.pgeo` (the fork) marks three kinds of struct *external* at layout time: the model vertex data struct, the model index data struct, and each BSP material's compressed vertex data (32 B a rendered vertex, then 8 B a lightmap vertex). External structs go to the .pgeo file (64-byte aligned) instead of the tag data, and every pointer to them resolves to `0x0C000000 + file offset`. Nothing else in the map changes.

The game's own code reads level vertices in three places, all for *positions* (found by audit — the first build crashed on the one the first grep missed):
- `structure_visibility.c`: the per-surface frustum test, every visible surface, every frame;
- `structures.c`: `structure_render_surface_from_point_and_leaf` (the surface under a point, for object lighting);
- `object_lights.c`: three vertices' texture coordinates (base and lightmap) for lighting an object from the lightmap.

So the bake leaves a **12-byte-a-vertex copy of the positions** in the BSP and turns the material's `compressed_vertex_data` pointer to it. The first two readers use a runtime stride (`psp_structure_vertex_stride`: 12 for such a map, 32 for an old one); the third reads the renderer's copies of the material (below). The map's layout is detected at load from the first model part's descriptor (`Data >= 0x0C000000`), independent of whether the .pgeo is present.

Sizes [M]: Beaver Creek tag space 7.49 → 4.68 MB (pack 2.86 MB). The Library 17.1 → 12.2 MB (tags 8.46 → 5.96, largest BSP 8.62 → 6.22; pack 14.4 MB for four BSPs and all models). Every MP map and campaign map builds the same way; the .ptex must be re-baked after -G (bitmap addresses move), and since b87 the .pstn too (stencil_bake.md).

Note: **the Xbox original of Blood Gulch is not on this machine** (only the PSP build Sheepdog01 sent back), and invader-extract refuses PSP-built maps, so Blood Gulch cannot be rebuilt here until the original is uploaded.

## The runtime: psp_geo.c

- `psp_geo_lock(address, size)` → resident bytes or NULL. NULL queues an asynchronous read (`sceIoReadAsync`, async priority 0x10, above the game thread's 0x20 — halo_bench v10: at equal priority the completion never runs while the game thread is busy). One read in flight at a time, polled at frame end and on a miss. A draw whose bytes aren't there is skipped for that frame (`psp_geometry_draws_missed`). **Since b72 the reads are synchronous by default** (`geoasync` restores the asynchronous path).
- Arena: 1 MB carved from the end of the texture RAM cache (`psp_tex_carve`; the volatile partition, so reloadable data only; given back and forgotten at sleep like the textures). Allocation is the texture cache's: ring position, else the start, else after a protected block; anything unprotected in the way is evicted. Protected = locked this frame or last, or loading.
- Cache coherence: before a read, `sceKernelDcacheWritebackInvalidateRange` on the destination (a previous tenant's dirty lines would otherwise land on the new bytes later); after completion, the same again (whichever way the kernel filled it).
- Every load bumps the block's **generation**. The renderer converts model vertices in place inside the block and records the generation; a block reused for other vertices is reconverted and its skinning plan freed.
- The six rasterizer hooks (level draw, light, shadow, level see-through, model see-through, model draw) go through `psp_vertex_lock`/`psp_index_lock` (size from `rasterizer_geometry_get_vertex_size` × count, or 2 × index count) when the descriptor's `Data` is external, and use `Data` directly otherwise (physical = virtual on the PSP; the D3D shim's lock did no more).
- **Lesson (b81):** anything cached *about* streamed data must be keyed by the data's pack address, never by the cache block's address. A reused block made red base's wall symbols use blue base's cached answers (renderer_debugging.md).

## The level's copies

The renderer has always copied each material's vertices into two GE streams (base uv + position, lightmap uv + position; 20 B each) the first time it draws them. Build 68 makes those copies the only thing the level paths use:
- the level hooks pass the *pack address* for an external material; the copy is keyed by it (stable identity; a cache block's address is not);
- the pack's bytes are locked only while building the copy, and a material's rendered and lightmap vertices are one block (40 B a vertex);
- the copies keep a packed normal per vertex (4 B) for the dynamic lights' reach, which used to read the map's vertex;
- with a pack open the copies have a 1 MB budget; the least recently drawn are freed, never one drawn this frame (`level streams … let go` in the log);
- b87: the stencil bake's added vertices are appended to the copies, and `levelcompact` can keep 10-byte copies beside them (memory_traffic.md).

The first Library run had the level's raw blocks re-locked every frame alongside the models and starved the cache (975 KB resident, 14 waits for room a frame). Passing addresses instead of locking fixed it: 160 blocks resident, 0 waits once settled [M emulator].

## What a run says

`geometry: N hits, N misses (N frames missed something); N loaded (N KB), N evicted, N waited for room, N for the queue; N blocks resident (N KB of N); reading N ms in all, worst frame N ms (synchronous)` every 300 frames, and `… buffers converted, N again after the cache moved them` in the GE frame line.

Emulator [M]: Beaver Creek level pixel-identical to the in-tag build at a fixed viewpoint (differences only in the weapon bob and radar sweep); 32 blocks, 178 KB resident. The Library: 252 loads (1.8 MB) in the first 1,200 frames, then none.

Hardware [M]: the pack, the arena and the coherence handling have run on Blood Gulch since b68 with no corruption reported.

## Read-ahead (analysed in b87, not built)

What the hardware runs show [M, b86 runs 1 and 2, Blood Gulch, reads synchronous since b72]:

| Run | Misses | Frames that missed something | Loaded | Evicted | Resident | Worst frame |
|---|---:|---:|---:|---:|---:|---:|
| b86 run 1 (~10 min) | 279 | 28 | 258 blocks, 1,117 KB | 29 | 900 of 1,024 KB | 208 ms |
| b86 run 2 | 179 | 13 | 158 blocks, 551 KB | 1 | 551 KB | 242 ms |

- **Most loads happen in the first second:** 115 reads, 260–295 ms of reading, while the map's first frames draw. That is a load-time cost, not a play-time hitch.
- **The rest cluster in a handful of frames** (13–28 per run). Each is a burst of several blocks read synchronously, one 100–240 ms freeze. That happens when a new area or object comes into view.
- **The cache rarely evicts** (1–29 blocks a run), so Blood Gulch's working set (~0.55–0.9 MB) nearly fits the 1 MB arena. The hitches are *first* reads, not thrashing.

So the fix is about **when** blocks are read, not cache size. Options, cheapest first:

1. **Warm the cache at map load.** Before the first frame, read every block the spawn view and the level's materials need (the level copies are built once anyway). It moves the first second's 115 reads into the loading screen. [E: ~0.3 s longer load]
2. **Asynchronous misses** (`geoasync`, built in b68 and off since b72): a missed draw is skipped for a frame or two instead of stalling. A pop-in replaces a freeze. Worth re-testing now that the cache rarely evicts.
3. **Prefetch at the natural points:**
   - When an object is created, or enters the potentially visible set of the player's cluster, queue its model's blocks.
   - When the BSP switches, queue the new BSP's materials.

   This needs a low-priority queue that the frame end services with a time budget (a few ms a frame), so prefetching never itself makes a hitch.
4. For campaign maps (multi-MB packs, more objects), the arena size decision comes back. Measure c20's working set with the same log line.

Not built in b87: it changes load and play timing, and Sheepdog01's rule is that nothing ships half-on. Order: 1, then a hardware run of 2 (`geoasync`), then 3.

## Open

- Read-ahead, in the order above.
- The arena size and the texture cache's loss of 1 MB, for campaign maps.
- Index data: the model index blob is read per part range; a baked GE-format pack (16–20 B vertices, no in-place conversion) would halve the reads and the conversion CPU.
