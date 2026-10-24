# Review: heaproom256 (heap from the volatile block)

`heaproom256` moves 256 KB of the level copies' arena out of the heap and into the level carve, which lies in the volatile block; the geometry cache pays for it. It comes on by itself with frames in flight (`geinflight`'s buffer) and a geometry cache over 1 MB (`geo2`, `geo15`), the runs that need the room; `heaproomoff` turns it off, and the word `heaproom256` turns it on with any geometry cache. The aim is heap room: Media Engine split #2's words (`gedefer gemeon geinflight mevram`) take about 200 KB more of the heap at its top [M], and the 224 KB in-flight buffer (`inflightkb224`) more again, so a30 ran out of heap with them. The CPA store wants heap too. A 384 KB size (`heaproom`, 128 KB more from the texture RAM cache) was built and measured beside it; it cost b30 and a30 textures and is gone.

## Why these bytes

The heap holds the copies' arena (1,056 KB: the level copies' 1 MB budget plus 32 KB of headers and slack), taken once at the first copy. Since the level carve came in, a map whose geometry cache is carved from the volatile block can already give the copies a second room there, grown only when the arena and the heap have no room. So the copies already work across the two rooms (they go to the arena first, then the carve, and the general heap last). `heaproom256` makes part of that second room permanent and shrinks the arena by the same amount.

What the volatile block gives up comes from the RAM budget census. It replayed the geometry and texture rings offline at the port's own sizes, plus and minus a delta, on b30, a30 and d20's opening [E]:

| Cut | b30 KB read (~3,300 frames) | a30 | d20 opening |
|---|---|---|---|
| geometry -256 KB | 2,231 vs 2,308 (no more) | 910 vs 975 | 486 vs 428 |
| textures -128 KB | between 4,260 and 5,605 (-256) | between 2,947 and 4,700 | between 480 and 600 |

Geometry -256 is free on all three. For textures the census had only a -256 point: about +1.3 MB on b30 over 3,300 frames, and 2 loads finding no room, which is why the textures give nothing here. The sound cache stays 1,792 KB: the census found d20's opening already failing 284 sound loads for room at that size.

## The code

- **`psp_ge.c`, `psp_heaproom_bytes()` and `arena_size()`.** The share is 256 KB when `geinflight`'s buffer is on (`inflight_buffer_bytes()`, so `gedefer` too) with `geo2` or `geo15`, or with the word `heaproom256`; 0 with `heaproomoff`, without them, and always in the multiplayer build. The arena's size is settled once, at the first copy: `ARENA_BYTES` less the share (800 KB). `arena_start`, `copy_alloc`'s size check, `streamsqueeze` and the arena's report line all read the room's own size now, not the constant.
- **`psp_ge.c`, `psp_ge_level_carve`.** When the arena is short, the carve grows at once by the missing 256 KB (`carve_grow`, two 128 KB steps) and records it as `carve_floor`. `carve_most` is raised by the same amount, so `levelcarveN` keeps its meaning: the most the carve takes *past* the floor. If the carve can't grow, the log says so and the map runs with the smaller arena and the heap.
- **`carve_frame_grow`.** A carve with a floor never counts as idle, so it isn't given back after 300 empty frames. `carve_release` (the map's end, a failed comeback after a sleep) clears the floor.
- **`level_streams_trim`.** The copies' budget is the arena's real room less its headers, plus the carve's room as before. The total budget is unchanged on a map that carves (800 - 32 + 256 + whatever the carve has grown).
- **`psp_geo.c`.** With the share on, a carved geometry cache over 1 MB gives 256 KB (`geo2`'s 2,048 becomes 1,792, `geo15`'s 1,536 becomes 1,280), and the texture RAM cache keeps its size. Only the word `heaproom256` with a 1 MB cache takes the 256 KB from the textures' end instead; the default never does.
- **Campaign only.** `PSP_MULTIPLAYER` compiles the word out. Multiplayer maps keep their geometry cache in the heap and carve nothing, so there the word could only shrink the arena.
- **Hot code.** None per draw. `copy_alloc`'s size check reads the arena room's size instead of a constant (one load per copy taken); `level_streams_trim` and the arena's report line the same. `psp_heaproom_bytes()` runs once at the first copy (the size is kept) and once per map open (`psp_geo.c`).

## Sleep

The floor is part of the level carve, so it follows the carve's existing path. The power callback marks it lost. At the next frame's end its copies are let go without reading its headers. When the block comes back at the same address, the carve starts again empty, with the floor's size, and its copies are built again as they are drawn. If the block comes back somewhere else, the carve is closed for the rest of the map (as before), and the arena and the heap carry the copies until the next map.

## Risks

- **A map that doesn't carve** gets the 800 KB arena and nothing back. No campaign map is one: all ten carve a 2,048 KB geometry cache with `geo2` and take the floor at their open. The main menu (the showcase) never opens the level carve, but it makes no level copy before the first level loads, so it loses nothing. With `geoheap` (a test word: the geometry cache in the heap, no carve) the copies would have 256 KB less room, and more of them would go to the general heap; with the in-flight buffer that is the case that runs short, so pair `geoheap` with `heaproomoff`.
- **The geometry cache is 256 KB smaller** on every map from its open: a few percent more misses where it costs anything (proof, below), and no more frames drawn with a texture missing.
- **A10's cryo chamber** wanted about 1.67 MB of copies a frame [D]. With the floor, the arena and the floor give the same 1,056 KB, and the carve can still grow past it to `levelcarveN`.
- **Play is unchanged.** The change moves where copies and cached blocks live and how often they are read again. It changes nothing the simulation reads (proof, below).

## Proof [E]

Emulator, M6 #2's words on (`gedefer gemeon geinflight mevram`), statecrc on every run.

**Heap.** With the words and `inflightkb224` (the in-flight buffer at the size the hardware proved), a30 runs out with the word off: heap top 5,776 of 5,780 KB, 8 KB spare at the least, 410 failed requests from about tick 1,200, the largest gap at a failure 37 KB. `heaproom256`: top 5,704, 116 KB spare, none failed. The dropped 384 KB size (`heaproom`): 5,616, 251 KB, none failed. b30 the same way: spare 100 -> 310 (256) -> 446 KB (384). a10: failed requests 12 -> 4 with `heaproom256` (its arena stays full and copies go on to the heap). Without `inflightkb224`: c10's fight and revert, spare 118 -> 501 KB and the largest gap after the revert 99 -> 479 KB with `heaproom`, no failures either side; a10 4 failed -> 0.

**Every map carves.** All ten campaign maps carve a 2,048 KB geometry cache, so each takes the floor at its open (the log's `GE: heaproom:` line), the revert included. The main menu is the showcase map and never opens the level carve, but it makes no level copy before the first level loads, so the smaller arena costs it nothing. A map whose pack fits the caches would get the smaller arena with nothing back; no campaign map does.

**What the volatile block pays.** The geometry cache's 256 KB costs a few percent more misses where it costs anything: b30 509 -> 588, a30 770 -> 778, a10 1,000 -> 987, c10 +4-7% of KB read. The 384 KB size's 128 KB from the textures was not free on b30: 372 -> 554 loads, 0 -> 60 waits for room, frames drawn with a texture missing 176 -> 291 and the longest run 14 -> 22 frames. On a30 with `inflightkb224`: 643 -> 1,008 loads, 284 -> 416 frames. With `heaproom256` the texture RAM cache keeps its size: b30 424 loads, 200 frames (longest 14); a30 653 and 284. The cache model's tick and drawing phases (b30, a30) move by under 2% either way, inside what a play split moves them.

**Play.** a10, c10 (twice, across the revert) and a30 without `inflightkb224`: play and both seeds identical on every tick; only the whole state differs (objects, looping sounds). b30's play parts at about 1,350-1,460, where it parts on any change; a30 with `inflightkb224` parts at 5,942 for both sizes. The word changes no simulation code, so a split is the drawing's timing reaching play [U], not isolated.

## On the PSP [M]

PSP-1000, a30, M6 #2's words against them with `inflightkb224 heaproom256` (2 runs a side, every run to its end): no speed change (frame within 1% over the whole route), headroom 92-120 -> 324-336 KB, 0 failed requests either side, draws skipped for a full list 263 / 39 -> 0, record-buffer fills 62 / 43 -> 0, frames drawn with a texture missing the same (236 / 241 -> 211 / 245). Geometry misses rose in the middle third (+37-56), with that third's frame +3.1%; the whole route was within 1%. PSP-1000 c20: headroom 360 -> 520 KB, frame 78.4 -> 78.0 ms, misses equal. b30 with the whole set against plain: headroom 380-392 KB, 0 failed, the frame gains the in-flight buffer gave without heaproom256.

## Play under `nomouth` [E]

The a30 parting at 5,942 was lip sync, not this change: with `nomouth` (lip sync kept out of game state), M6 #2's words, `inflightkb224` and `heaproom256` play the same as plain on a30 (all 9,000 ticks) and b30 (all 6,000), both seeds equal every tick.

## Default

On with frames in flight and a geometry cache over 1 MB (`geinflight` with `geo2` or `geo15`), off otherwise; `heaproomoff` turns it off. It isn't on everywhere: without frames in flight the heap has room (b30 plain on the PSP-1000: 388 and 436 KB of headroom, 0 failed), and the move would only buy geometry misses (b30 509 -> 588 in the emulator). With them it is needed even at the 128 KB buffer (a10: 4 failed requests -> 0, emulator), and at 224 KB a30 runs the heap out without it; it costs no textures on any map measured and no speed on the PSP.
