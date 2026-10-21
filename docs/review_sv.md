# Review: heaproom (384 KB of heap from the volatile block)

One change, off by default behind `heaproom`. It moves 384 KB of the level copies' arena out of the heap and into the level carve, which lies in the volatile block. The geometry cache and the texture RAM cache pay for it. The aim is heap room: Media Engine split #2's words (`gedefer gemeon geinflight mevram`) take about 200 KB more of the heap at its top [M], and that has to be found before they can go on by default. The CPA store wants heap too.

## Why these bytes

The heap holds the copies' arena (1,056 KB: the level copies' 1 MB budget plus 32 KB of headers and slack), taken once at the first copy. Since the level carve came in, a map whose geometry cache is carved from the volatile block can already give the copies a second room there, grown only when the arena and the heap have no room. So the copies already work across the two rooms (they go to the arena first, then the carve, and the general heap last). `heaproom` makes part of that second room permanent and shrinks the arena by the same amount.

What the volatile block gives up comes from the RAM budget census. It replayed the geometry and texture rings offline at the port's own sizes, plus and minus a delta, on b30, a30 and d20's opening [E]:

| Cut | b30 KB read (~3,300 frames) | a30 | d20 opening |
|---|---|---|---|
| geometry -256 KB | 2,231 vs 2,308 (no more) | 910 vs 975 | 486 vs 428 |
| textures -128 KB | between 4,260 and 5,605 (-256) | between 2,947 and 4,700 | between 480 and 600 |

Geometry -256 is free on all three. For textures the census had only a -256 point: about +1.3 MB on b30 over 3,300 frames, and 2 loads finding no room. -128 lies between 0 and that. The sound cache stays 1,792 KB: the census found d20's opening already failing 284 sound loads for room at that size.

## The code

- **`psp_ge.c`, `arena_size()`.** The arena's size is settled once: `ARENA_BYTES`, or `ARENA_BYTES - HEAPROOM_BYTES` (672 KB) with the word. `arena_start`, `copy_alloc`'s size check, `streamsqueeze` and the arena's report line all read the room's own size now, not the constant.
- **`psp_ge.c`, `psp_ge_level_carve`.** When the arena is short, the carve grows at once by the missing 384 KB (`carve_grow`, three 128 KB steps) and records it as `carve_floor`. `carve_most` is raised by the same amount, so `levelcarveN` keeps its meaning: the most the carve takes *past* the floor. If the carve can't grow, the log says so and the map runs with the smaller arena and the heap.
- **`carve_frame_grow`.** A carve with a floor never counts as idle, so it isn't given back after 300 empty frames. `carve_release` (the map's end, a failed comeback after a sleep) clears the floor.
- **`level_streams_trim`.** The copies' budget is the arena's real room less its headers, plus the carve's room as before. The total budget is unchanged on a map that carves (672 - 32 + 384 + whatever the carve has grown).
- **`psp_geo.c`.** With the word, a carved geometry cache over 1 MB gives 256 KB (`geo2`'s 2,048 becomes 1,792). The carve sits below it, so the other 128 KB come from the textures' end of the block.
- **Campaign only.** `PSP_MULTIPLAYER` compiles the word out. Multiplayer maps keep their geometry cache in the heap and carve nothing, so there the word could only shrink the arena.

## Sleep

The floor is part of the level carve, so it follows the carve's existing path. The power callback marks it lost. At the next frame's end its copies are let go without reading its headers. When the block comes back at the same address, the carve starts again empty, with the floor's size, and its copies are built again as they are drawn. If the block comes back somewhere else, the carve is closed for the rest of the map (as before), and the arena and the heap carry the copies until the next map.

## Risks

- **A map that doesn't carve** (the main menu, any map whose texture pack fits the caches) runs with a 672 KB arena. The main menu draws little level. A map whose pack fits keeps its geometry cache in the heap, so its copies get less room: more go to the general heap.
- **The textures lose 128 KB from the map's open,** not only when the copies need it. The level carve was made to grow on demand because a carve taken whole at b30's open (512 KB) left textures undrawn for thousands of frames [E]. This one costs the textures 128 KB, not 512. The census's replay puts that cost between nothing and +1.3 MB read on b30. The cache-model pair in the batch measures it.
- **A10's cryo chamber** wanted about 1.67 MB of copies a frame [D]. With the word, the arena and the floor give the same 1,024 KB, and the carve can still grow past it to `levelcarveN`.
- **Play is unchanged.** The word changes where copies and cached blocks live and how often they are read again. It changes nothing the simulation reads. statecrc pairs in the emulator check that.

## Default

Off. The emulator batch decides whether it pays (heap low point, failed requests, misses, frame cost), and a PSP point decides how long the extra reads take. If the cost is nil, the heap's room is what's needed, and the word should go on by default (with `heaproomoff` as its off switch).
