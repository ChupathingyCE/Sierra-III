# The memory ledger: what each block of RAM and VRAM holds, and what a KB of it buys

Sheepdog01, 2026-10-21 15:51-15:53: memory is now a lever. VRAM trades the texture cache against model copies; main RAM buys headroom, cache sizes and fidelity. Three digs for the master (helper `memdig`), on w140 `bc12443`, bake `b140x` (the exact CLUT4 set): (1) geometry bytes that could go without degrading the look; (2) the ledger of both pools; (3) the CLUT4 look A/B for Sheepdog01. Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference or arithmetic. Counts reused from recon's helpers (cull-lod, CTNG, survey) are cited, not redone.

## 0. Ledger 4: every pool's peak and room by map, on tock162 (2026-11-04)

The redesign's ledger 4 (`redesign_recon.md`). **Build:** tock162 c3f3dba5 with B58's emulator fix merged on top (the simulated ME's sound tier and AI stack in the emulator's mapped eDRAM: without it the emulator's heap was ~600 KB short and d20 failed 422 requests), tock162's recipe without STRICT (campaign-w162b, layouts w161a_tgc90 / hotg161a), bake w157m. **Runs** (lab sierra-batch 41956515467, `ledger4a`, all 10 ended at their stop, 0 strict sites): the ladder (each map's 90 s opening) and the a10, a30, b30, d20 routes (Sheepdog01's b139 hand-test inputs, deathless, `fixedticks fixedseed`; d20 with the flashlight from tick 1400), each once on the shipped build (`memcensus heapmap`, the metrics heap lines) and once with `PSP_RECON=1 PSP_POOLS_XBOX=1` (`poolpeaks`: the game-state arrays at the Xbox's sizes, so no peak is capped). Every figure here is **[E], that build**, unless marked. Read with `tools/memcensus.py`.

**The emulator against the PSP on the same tock162** (timeline3's sierra-hw 41941407089 b30 and 41941380156 d20, PSP-3000 [M], strict 64647bb5): the heap is 5,768 KB there and 5,764 here (the strict link's 4 KB); the sound tier splits the same (1,392 KB of main RAM + 656 KB of eDRAM); b30's heap top 5,292 KB over 150 s [M] against 5,416 over its 90 s opening and 5,456 over the route [E]; d20 5,228 over 90 s [M] against 5,372 / 5,288 [E]; 0 failed requests on both. Close enough to read the emulator's peaks as the PSP's, stretch for stretch.

### 0.1 What changed since §2 (w140, 10-21 to 10-27)

The sound cache is **2,048 KB**, its last 656 KB in the ME's eDRAM (SNDEDRAM2's tier, on by default), so the heap holds 1,392 KB of it plus 128 KB staging; the AI job's stack (544 KB) and data (48) are in eDRAM; the geometry cache is **1,280 KB** in the volatile partition, plus the tag window's free span as its second region on maps that have one (b30 1,592, c10 1,402, c40 1,346, d40 1,608, c20 2,496 KB); the level carve takes up to 1,024 KB; the heap is **5,764 KB**. **a10's 4 KB of headroom is gone: it has 344 KB at its route's peak.** No map failed a heap request.

### 0.2 Main RAM: the heap and its blocks, by map

Heap 5,764 KB. "Top" is the heap's highest point; room = 5,764 - top; "free low" and "largest low" are the metrics heap lines' least free and least largest block over the map (fragmentation shows as the gap between them).

| Map (run) | Top | Room at the top | In use, most | Free low / largest low | Display list, fullest frame (768) | Level copies' arena (800) | Level carve (1,024) |
|---|---:|---:|---:|---:|---:|---:|---:|
| a10 route | 5,420 | **344** | 5,415 | 348 / **255** | 525 | 800 (17 let go, 144 to the heap) | **848** |
| a30 route | 5,448 | 316 | 5,439 | 324 / 322 | 451 | 761 | 0 |
| b30 route | 5,456 | 308 | 5,397 | 366 / 328 | 603 | 800 (196 let go) | 256 |
| d20 route | 5,288 | 476 | 5,283 | 480 / 476 | 586 | 800 (90 let go) | 255 |
| a10 opening | 5,220 | 544 | 5,163 | 600 / 596 | 282 | 800 | 246 |
| a30 opening | 5,452 | 312 | 5,428 | 335 / 320 | 351 | 726 | 0 |
| a50 opening | 5,168 | 596 | 5,116 | 647 / 596 | 399 | 612 | 0 |
| b30 opening | 5,416 | 348 | 5,412 | 351 / 347 | 597 | 800 | 246 |
| b40 opening | 5,416 | 348 | 5,288 | 475 / 442 | 505 | 413 | 0 |
| c10 opening | 5,364 | 400 | 5,343 | 420 / 417 | 528 | 796 | 8 |
| **c20 opening** | **5,604** | **160** | 5,422 | 341 / **182** | 330 | 799 | 383 |
| c40 opening | 5,444 | 320 | 5,287 | 476 / 320 | 217 | 486 | 0 |
| d20 opening | 5,372 | 392 | 5,285 | 478 / 465 | 432 | 800 | 255 |
| d40 opening | 5,232 | 532 | 5,133 | 630 / 531 | 422 | 797 | 0 |

- **The tightest:** c20's opening (160 KB at the top; its largest free block fell to 182 KB) and b30's and a30's routes (~310 KB). The sound and copies' blocks inside the heap are full on every map (the sound cache 1,984-2,048 of 2,048 occupied, c20 1,236; the copies' arena 800 of 800 on a10, b30, c10, c20, d20, d40); the room is the heap's own margin, not idle blocks.
- **What churns at the limits** (routes): d20's sound cache failed 111 loads for room (the only map that did); b30 let level copies go 196 times, d20 90, a10 17; c20's opening sent 155 copies to the heap at w140 and its arena is again 799 of 800.
- **Outside the heap** (top of user RAM): **70 KB free at the least on every map, largest block 28 KB** (the metrics heap lines' "user memory free low"): the ~130 KB of §2.2 is down to 70.
- **The game state:** 3,003 of 3,012 KB allocated, every map.
- **The in-flight record buffer** (224 KB): b30's route 19 KB a frame on average, 62 at most [E: the emulator's ME is simulated, so its fills run lower than the PSP's 184-197 of §2.2].

### 0.3 The game state's arrays: the most in any run against the shipped size

`poolpeaks` on the Xbox's sizes; shipped = the campaign's sizes (`psp_capacity.h`). Route maps are a10, a30, b30, d20; the ladder adds the other six maps' openings (its peaks carry over from map to map for a few arrays: a value repeated on later maps is an earlier map's, not theirs).

| Array | Shipped size | Most (where) | Room at the most | Next |
|---|---:|---|---:|---|
| objects pool | 1,024 KB | **1,000 KB** (b30 route) | 24 KB: full by design (the Xbox's garbage limit) | d20 852-934 (furthest end) |
| cached object render states | 512 | **512** (a30, b30 routes) | 0: full (misses rework the lighting) | d20 500 |
| effect | 256 | **255** (a10 route) | 1: full, the Xbox's size | d20 248 |
| contrail point | 768 (trimmed) | **665** (a30 route) | **103**: the tightest trim (was 561 at the census) | b30 519 |
| particle | 1,024 | 512 (b30 route) | 512 | d20 395 |
| object | 2,048 | 1,122 (d20 route) | 926 | b30 1,100 |
| effect location | 512 | 358 (d20 route) | 154 | a10 257 |
| object looping sounds | 640 (trimmed) | 339 (c20) | 301 | d20 191 |
| lights | 896 | 363 (b30 route) | 533 (every encounter placed at once filled 896 on b40: §2.4) | d20 313 |
| prop | 512 | 260 (b30 route) | 252 (the hand test's 283 [M]) | d20 172 |
| system particles | 448 (trimmed) | 225 (a10 route) | 223 | b30 105 |
| hs thread | 256 | 179 (b40) | 77 | c20 151 |
| glow particles | 256 | 128 (b30 route) | 128 | 0 elsewhere |
| actor | 160 | 86 (b30 route) | 74 (every encounter on b40: 139) | d20 30 |
| collideable cluster references | 2,048 | 766-769 (b30 route) | ~1,280 | a30 434 |
| light cluster references | 2,048 | 691 (d20 route) | 1,357 | b30 442 |
| noncollideable cluster references | 2,048 | 705 (c10) | 1,343 | d20 492 |
| device groups | 1,024 | 697 (c20) | 327 | d20 518 |
| hs globals | 1,024 | 571 (a50) | 453 | a30 551 |
| decals | 2,048 | 391 (d20) | 1,657 | b30 383 |
| encounter | 128 | 81 (c20) | 47 | b40 75 |
| contrail / particle systems / swarm component | 96 / 32 / 160 (trimmed) | 37 (b30) / 12 (a10) / 23 (c20) | 59 / 20 / 137 | |

### 0.4 The other pools

| Pool | Size | Peak by map | Room | Source |
|---|---:|---|---|---|
| **VRAM**: frame buffers + depth | 544 + 272 KB | fixed | 0 | [D] |
| VRAM: the textures' share | 616 KB | 600-614 on every map | 2-16 KB | [E] |
| VRAM: the models' share | 616 KB | 584-605 on the levels (b40 504, a50 536); full 745 (d20) to 4,713 (b30) times over the routes | 11-32 KB on the busy maps | [E] |
| **Volatile 4 MB**: geometry cache | 1,280 KB (+ the tag window's span where there is one: b30 312, c10 122, c40 66, d40 328, c20 1,216 KB) | 1,221-1,266 of 1,280 (every map); b30 1,551 of 1,592; c20 1,428 of 2,496; c40 922 of 1,346 | c20 ~1 MB, c40 ~420 KB idle in their openings; full elsewhere | [E] |
| Volatile: level carve | up to 1,024 KB | a10 848 (route), c20 383, b30 / d20 256; 0 on a30, a50, b40, c40, d40 | grows on demand, out of the texture RAM cache | [E] |
| Volatile: texture RAM cache | 2,784 KB less the carve (2,528 at 256) | b30 15 MB read over the route, 948 evicted | the carve's loser | [E] |
| Volatile: glyph sheet | 32 KB | | | [D] |
| **Tag window** | 9,216 KB | tags: b40 5,944, a50 5,362, c10 5,278 ... c20 2,998 KB (the map file; the BSP takes the rest) | its free span above | [E] |
| **ME eDRAM** (2,048 KB; tock162 defaults [D], `psp_me.h`) | 0-192 left as found; 192-208 recon scratch; 208-256 the ME's private state (opt-in); **256-800 KB free (544)**; 800-1,456 the sound tier (656, full: 2,036-2,048 of the cache occupied); 1,456-1,504 the AI job's data (48); 1,504-2,048 its stack (544). **[2026-11-21, the draw list box's L1 (`docs/box_draw_list.md` §12.1a): MEXEC's copy is at 256-512 on the tock (`rbtsw`, room 256) and codediet's room of 264 KB takes it to 520; `PSP_MEXEC=rbtswlk` (room 268) adds the level's kept path's ME code at 520-524, so 524-800 (276 KB) stays free. **[2026-11-22, L2 (§12.1d): rbtswlk's room is 276, the kept geometry's code and the slant sort and `uv_shift` (pushed out of the level body's inlining, §12.1d (g2)) at 520-532, so 532-800 (268 KB) stays free; D]** The kept path's entry table is not in eDRAM: the replay also runs on the main CPU (the emulator, gemesim), which can't read eDRAM, so the table sits in main RAM at the front of its store (below); D]** | the stack's depth: ~192 KB+ on b30 and d20 [M, aime2a2 at w14x, edram.md]; not remeasured here | **544 KB at 256-800**, minus MIST's reservation (unpinned) | [D] + [M] |
| Display list's kept geometry's store (`PSP_DRAWLIST=1` with `levelgeo` or `levelkept2`, 2026-11-22, §12.1d slice L2) | 112 KB by default, the display list's last KB before Tier 1's store: the entries (128 of 120 bytes, 15 KB, the replay's), the buckets' pool (`levelgeomekbN`, 32 KB in pages of 128 bytes: a bucket a triangle and 16 bytes a chunk of 8, the replay's only) and the sorted copies' pool (`levelgeokbN`, 64 KB in pages of 512 bytes, the only part the GE reads). Main RAM, not the ME's eDRAM: the replay also runs on the main CPU (the emulator, gemesim). FIXES_DRAWLIST's list versions (the main CPU's, `structure_render.c`) are 32 KB of `.bss` (the surface bits as last seen and each word's last change) | the list's fullest frame must still leave 64 KB beside both stores: levelkeptroom3 (§12.1d) | 0 by design: a copy is given back only `DL_FENCE` frames after its last use | [D] |
| Display list's store (`PSP_DRAWLIST=1` with `levelkept`, 2026-11-21) | **40 KB by default since 2026-11-22 (slots of 768 bytes, 41 of them, L2's Tier 1 fix; §12.1d (c))**; was 48 KB (`levelkeptkbN`; 64 until 2026-11-22, when b40's fullest frame, 643 KB, left 61 KB beside it), the display list's last KB: the list is 720 KB with it on. The level's kept prologues (slots of 1 KB, the only part the GE reads) and their entry table (the replay's) | its fullest frame on every map of the bench set must leave 64 KB beside the store (the lead, 2026-11-21): the list's fullest frame at most 656 KB with the default store; levelkeptroom1's fullest frames b30 389, b40 643, c20 620, c40 558, a30 562, a50 387 KB [E], §12.1a (h) | 0 by design: its slots are fenced, never shared with the frame's words | [D] |
| **Scratchpad** | 16,384 B | 16,384 used (BSP top nodes, the profiler's slots and table) | 0 | [E], the start-up lines |

### 0.5 What a KB buys now (the order of §2.5, revised)

1. **c20's heap** (160 KB at the top, largest block 182) is now the tightest, not a10 (344). Anything the full set adds to c20 sizes against that.
2. **The eDRAM's 544 KB at 256-800 KB** is the largest free block in the machine: PDC (~240), L1 (~190) and CSTPOOL (64-80) fit beside each other (edram.md's claims), once MIST's table address is pinned. MUSICSTREAM's region (1,184-1,456) now overlaps the sound tier: it needs a new place if `PSP_MUSIC` returns.
3. **The geometry cache's second region** works where the tag window has room (c20's 1.2 MB is mostly idle in its opening); a10, a30, a50, b40 have none.
4. **User RAM outside the heap is down to 70 KB** (largest 28): a new thread stack or outside-heap block has to come from the heap.
5. Arrays: the only trims left with margin are small (contrail point has 103; the full arrays, objects, render states and effect, are full by the Xbox's design).

## 1. Geometry: what could go without degrading the look

### 1.0 First, a correction: the 14-byte vertex was never built

`asset_baking.md` §3.3's 14-byte vertex (s16 uv, s8 normal, s16 position) was the prototype's design (`model_geom.py`), not what the bake writes [D]. The `.pgeo` pack holds the Xbox's own model vertices, **32 bytes** each (`model_vertex_compressed`: float position, three packed vectors, s16 uv, nodes and weight; 68 bytes for the sky and parts too big for 16 bits), and 16-bit strip indices (`geometry_streaming.md`: `-G` moves the structs out unchanged). The runtime converts each block in place the first time it is drawn (`model_convert`, `psp_ge.c` ~10241): **24 bytes a rigid vertex** for the GE (float uv, s8 normal with the node in the pad byte, float position), skinned parts the weights plus the same 24. So the geometry cache holds 32 B a vertex, the VRAM copies and every GE read 24 B.

That makes the vertex format itself the largest geometry lever, ahead of (a)-(e):

| | Pack and geometry cache | GE reads (main RAM and VRAM copies) | Look | Exact? |
|---|---|---|---|---|
| **(0) 14-byte baked vertex** (s16 uv: the source is already s16, so exact; s8 normal: already the GE's; s16 position scaled per part through the model matrix) | 32 -> 14 B a vertex: **-56%** of the models' share of the pack (census below) and of the cache's working set (a10's 900-frame set 2,725 KB, b30 2,429 [E]) | 24 -> 14 B: **-42%** of model vertex bytes. Main RAM, the default set (m6bytes2 [E]): b30 138 KB a frame of model vertices -> -58 KB (**~0.55 ms** beside the tick at 9.6 us/KB), a30 229 -> -96 (~0.9), c20 56 -> -23 (~0.2). VRAM: the 616 KB room holds 1.7x the vertices; on b30's flight, where the room is the binding cost (CTNG: 15.9-17 us/KB [M]), that is worth up to the copies of ~440 KB more 24-byte data: **~3-7 ms a frame on the flight** [U: the room also holds level streams, and the slope is measured to +177 KB only] | sub-pixel: the position step is the part's span / 65,536 (0.19 mm for a 4.15 wu part [M, Blood Gulch]); first-person parts at the near plane (0.06 wu) would reach ~0.6 px: keep them float | no (positions rounded); "plays and looks the same" class, shown by side-by-sides |

Its cost is code, not bytes: the bake writes the new layout, `model_convert` becomes a copy, and every CPU reader of a converted vertex (the near-plane cut ~11846, the see-through colours ~13142, `model_to_camera` ~13220, ~13501, ~13614) reads s16 positions through the part's scale. A keep word can keep the float path for an A/B.

### 1.1 The census (what the packs hold)

Sierra-III `geocensus` 725f37a `tools/geocensus.py` (not for merge), run on bake b140x through the lab's sierra-tags (**39681278283**; the first run, 39676064840, read the five LOD cutoffs in reverse: Invader's definitions name them top down, the engine reads the first as the lowest level's). Counts and bytes from the maps' tags and the `.pgeo` [D]; "non-top" = a geometry never drawn at the top level, so it has a nearest drawn size (the next level's cutoff).

| Map | `.pgeo` | Models: vertices (KB at 32 B) + indices | As 14-byte vertices | Non-top geometry | Level: rendered (32 B) + lightmap (8 B) vertices | Level bytes never read |
|---|---:|---|---:|---:|---|---:|
| b30 | 11,988 KB | 153,341 (4,792) + 511 | **-2,695 KB** | 1,813 of 5,303 KB | 95,041 + 89,745 (2,970 + 701 KB) | 743 KB |
| c20 | 16,482 | 75,667 (2,365) + 249 | -1,330 | 778 of 2,614 | 314,692 + 299,190 (9,834 + 2,337) | 2,459 |
| a10 | 30,953 | 127,772 (3,993) + 420 | -2,246 | 1,478 of 4,413 | 603,012 + 601,674 (18,844 + 4,701) | **4,711** |
| a30 | 11,213 | 159,118 (4,972) + 538 | **-2,797** | 2,392 of 5,511 | 81,321 + 77,483 (2,541 + 605) | 635 |
| d20 | 18,546 | 125,493 (3,922) + 436 | -2,206 | 1,612 of 4,357 | 279,510 + 276,942 (8,735 + 2,164) | 2,184 |

The rest of each pack (~1.7-3.3 MB) is animation frame data (`-M`).

### 1.2 The candidates, ranked by bytes per unit of look risk

| # | Candidate | Pack (b30 / c20 / a10 / a30 / d20, KB) | Geometry cache and GE reads a frame | Look | Exact? |
|---|---|---|---|---|---|
| 1 | **(e) The level's binormal and tangent** (8 of each rendered vertex's 32 bytes): the level copies read only position, normal and uv, and the lightmap vertex's 8 bytes (`psp_ge.c` ~7616-7645 [D]); the engine's own CPU reads the 12-byte position copy in the BSP | **-743 / -2,459 / -4,711 / -635 / -2,184** (20% of the level's render data) | 0 GE (the copies don't change); 20% fewer bytes through the cache each time a material's copy is built or rebuilt (a10 let copies go 2,614 times in the hand test [M]); stick reads at load | none | **yes** (bytes never read) |
| 2 | **(b) 8-bit indices for parts of <= 256 vertices** | 513 of 674 parts on b30: **-57 / -31 / -55 / -84 / -51** | the heap's skinning lists: b30 172 of 282 KB a frame fit (gl3count1 39642564211 [E]) -> ~-86 KB, a30 -24, c20 -7, d20 -15; plus the rigid parts' pack indices (b30 37 KB a frame, a30 15, c20 10, m6bytes2 [E]). **b30 ~0.9 ms, a30 ~0.3** beside the tick (9.6 us/KB) [E x U] | none | **yes** |
| 3 | **(0) the 14-byte baked vertex** (§1.0) | **-2,695 / -1,330 / -2,246 / -2,797 / -2,206** | cache: -56% of the models' share; GE: -42% of model vertex bytes: main RAM b30 ~0.55, a30 ~0.9, c20 ~0.2 ms; VRAM room 1.7x the vertices: up to ~3-7 ms on b30's flight [U] | sub-pixel (first-person parts stay float) | no: "looks the same" class |
| 4 | **(c) 8-bit positions for non-top geometry**, where the rounding is <= 0.5 PSP px at its nearest drawn size | vertices that pass (and with no radius or extent, unknown): b30 26,410 (20,686), c20 6,521 (14,785), a10 19,317 (19,373), a30 42,538 (26,456), d20 24,561 (21,794): 4 B each beside the 14-byte vertex: **-103 / -25 / -75 / -166 / -96** (9 B each beside today's 24: -232 / -57 / -170 / -374 / -216) | b30's play draws ~33% of its model triangles at the lowest level (~57 tree, shrub and boulder draws a frame, cull-lod [E]): ~0.1-0.2 ms [U] | <= 0.5 px at the nearest size (bound: the part's extent / 510 over the object's diameter) | no |
| 5 | **(d) screen-bounded simplification**, non-top levels to k x D triangles at their nearest D PSP px (top level, first person, sky and cinematic stars untouched) | k12: **-251 / -207 / -244 / -353 / -171** (13-25% of non-top triangles); k4: -901 / -453 / -719 / -1,120 / -813 (42-59%) | play's share of model triangles over k x D: b30 21% (k12) / 30% (k4), a30 1.8-4.7%, c20 2.6-6.8% (S9, cull-lod [E]): ceilings b30 0.9-1.2 ms, a30 0.1-0.3 | <= ~0.5 px by construction, but a decimation is a new look path (silhouettes, seams, normals): Sheepdog01's call after an A/B | no; needs a bake tool |
| 6 | **(a) LOD pixels at the PSP's 272 lines** | **0 from the pack**: the geometries it would leave unchosen (chosen only when the object's sphere is taller than the screen: b30 32 geometries, 713 KB; c20 307; a10 713; a30 1,239; d20 540, the characters' and weapons' super-high levels, cutoffs 280-500 px) are the cinematics' movie stars, which always draw the top level | play: -0.5% of b30's model vertices, -3.1% a30, 0 c20/d20; cinematics -2 to -4.5% (S8, cull-lod [E]): under the gate everywhere; 3.3 draws a frame on b30 drop below the lowest level and vanish 1.76x nearer | objects drop detail and vanish nearer: **detracts** | no |
| - | Geometries no level ever selects (a higher level's cutoff at or below theirs) | b30 122, d20 122, others 0 | never read in play | none | yes, pack bytes only |

**What it means.** The look-free bytes are in the format, not in the levels of detail: the level's unread binormal and tangent (0.6-4.7 MB of pack a map, exact) and 8-bit indices (exact, ~0.9 ms on b30) can go with no picture question; the 14-byte vertex is the large one (-1.3 to -2.8 MB of pack, -42% of every model vertex the GE reads, and the VRAM room's reach on b30) at the price of sub-pixel position rounding and the CPU readers' rewrite. Every LOD-side cut (a, d, partly c) changes the picture and is worth less: the PSP's real pixel scale would leave nothing removable from the pack, because the cinematics draw the top levels anyway.

**Sheepdog01's calls in dig 1:** none for (e) and (b) (exact). The 14-byte vertex is a "looks the same" change: side-by-sides decide. (c), (d) and (a) detract and are not recommended at their prices.

## 2. The ledger, both pools (w140, 10-21; §0 supersedes it wherever it gives a figure)

### 2.1 VRAM (2,048 KB; the same layout on every map)

w140 `psp_tex.c` [D]; use from CTNG's VRSP-784 read (sierra-hw 39661123709, PSP-1000 [M]) and the memory census (memcensus1 routes [E], hand test [M]).

| Block | KB | Peak use | What a KB of it is worth | Idle or oversized at peak? |
|---|---:|---|---|---|
| Frame buffers, 2 x 512 x 272 x 16 bit | 544 | 100% | fixed: one buffer tears (rejected, `memory_savings_b137.md` §3); the 32-pixel stride padding is 51 KB in strips: too narrow for the HUD's textures (VRB1), and as vertex room by rewritten indices (VRTAIL: 1,600 24-byte slots, ~37 KB, depth rows 0-15 left to `blipdepth`) sized out, below | no |
| Depth buffer (motion sensor target inside it: `blipdepth`) | 272 | 100% | fixed; the sensor's 16 KB went to the copies' room | no |
| **Texture cache** | 616 | 99-100% on every map ([M] hand test 610-614; [E] routes 607-614) | **b30: more than a KB of copies.** Shrinking it 184 KB (616 -> 432, `vrammodels784`) took untextured frames 33/41 -> 194/581 and drawing +2.8-3.9% on the PSP [M], eating 40-55% of the copies' tick gain: rejected. c20 waits for no texture room (VSPLIT: 140 KB a frame more could move there, ~1.3 ms [E x M slope]) | b30, a30: no (binding). c20: yes, ~184 KB its textures never wait for [E] |
| **Model copies' room** (vertex buffers and level streams; indices never) | 616 (600 + `blipdepth`'s 16) | b30 483 of 616 used on average, 132 KB idle while the best candidate is refused 3 frames in 4 [M]; c20 ~37 idle, a30 ~89 [E]; 87.7% of draws from a copy (b30) | **15.9-17 us of tick a frame per KB on b30's flight and landing [M]** (+177 KB = -2.8 / -3.0 ms); ~3.4 beach [M]; c20 7.3, a30 7.5 [E slope x 9.6 us/KB] | the 132 KB of gaps (VMFILL: fill them: b30 1.0-2.1 ms flight [E x M], CTNG plan step 1) |
| **Total** | 2,048 | | | |

**What VRAM can still give:** only (a) the copies' own idle gaps (VMFILL, 66-132 KB, no texture cost), (b) texture bytes made smaller (CLUT4: the exact set freed 42 KB of b30's pack; the look set, dig 3), (c) per-map splits where textures don't wait (VSPLIT on c20, a30 if its textures allow). Every KB the copies take from the texture cache on b30 costs more in untextured frames than it saves in tick.

**The slope above has not held at the margin.** VMFILL later moved the room's refused buffers (95-152 KB a frame of GE reads on b30's flight) into VRAM and the flight did not move on the PSP (frame in the noise, bus and memory wait unchanged [M]); only the cinematic did. So the stride tails (VRTAIL) were sized, not built: the most they could take is the refused rigid model vertices, ~62 KB a frame of GE reads on b30's flight (~1,540 distinct 24-byte vertices, which would fit) [E], the same kind of bytes VMFILL moved for nothing, and fewer of them. Building them would also mean refilling the tails after every whole-buffer clear (the colour buffers' resets, the debug overlay's first opening on the depth buffer) [D]. Worth reopening only if a PSP pair shows refused model vertices costing flight time.

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

**The variant.** Sierra-III `clut4look` 63dd5e9 (off w140 bc12443; bake only, off by default): `halo-psp-bake --clut-look` (texpack `--clut4-look`) puts a lightmap or HUD sheet the bake makes CLUT8 at 16 colours when one palette over its whole chain passes the CLUT4W census's strict bar on every level (screen PSNR within 1 dB of CLUT8's or over 45 dB; no texel more than one 5-bit step past CLUT8's worst; `design_clut4w.md`). CLUT8's chain is kept; ui.map is untouched; meters and multipurpose maps stay out. Runtime unchanged (the GE and the texel readers take CLUT4 already [D, `psp_tex.c`]). Not merge-ready: the bake rule asks an all-maps bake first.

**Bake c4look1** (lab sierra-bake 39675794133, bundle local:63dd5e9, c20, b30, ui) [D], beyond the exact set:

| Map, pack | Lightmaps | HUD sheets | Total | Lowest level dB at 16 colours (median) |
|---|---|---|---|---|
| c20 main | 35 textures, **-493 KB** (227 missed the bar) | 13, -30 KB | **-523 KB** | lightmaps 48.3 (61.8), HUD 45.1 (52.9) |
| c20 track (the shipped `assettrack`: half-size lightmaps) | 26, **-107 KB** | 13, -30 | **-137 KB** | 45.1 (46.0) |
| b30 main | 1, -1 KB | 13, -30 | -31 KB | |
| b30 track | 0 | 13, -30 | -30 KB | |

The census's c20 figure (790 KB of strict lightmaps, one palette) is this 493 plus the 297 KB of lightmaps the exact set already took. On the shipped track pack the look set is worth 137 KB on c20 and 30 KB on b30.

**The A/B** (lab sierra-batch **39680280258**, emupool, desktop; one EBOOT w140 bc12443 both sides, bake b140x vs c4look1; hands off, `fixedticks fixedseed`, a shot every 5 s of game time to 100 s; plain run twice as the control; no `nomouth`). All 8 runs played to game time 2,971-2,977. Pixel differences per frame, the debug line (its clock) left out, 480 x 256 [E]:

| Pair | Shots | Pixels differing, mean (worst shot) | Largest channel step | PSNR, worst shot |
|---|---:|---|---:|---:|
| c20 plain vs plain (control) | 19 | 0.00% (0.00%) | 0 | identical |
| **c20 track: plain vs look** | 18 | **0.07% (0.33%, game time ~13-19 s)** | 17 | 61.6 dB |
| **c20 main pack: plain vs look** | 19 | **0.03% (0.16%)** | 9 | 65.1 dB |
| b30 plain vs plain (control) | 19 | 0.00% (0.02%) | 16 | 72.2 dB |
| **b30 track: plain vs look** | 19 | **0.02% (0.05%)** | 16 | 68.5 dB |

No pixel moved by 24 or more in any pair. c20's differences are faint dither-pattern changes on the lit walls (the opening's view up the Library's dome); b30's are inside the control's own run-to-run spread. (One c20 pair didn't match by name, shot 11: t79 vs t78.)

**Texture bytes in these stretches** (`textures:` lines, whole run): c20 track loaded 2,398 -> 2,383 KB, largest frame 1,673 -> 1,656 KB, untextured frames 19 -> 17; c20 main 3,178 -> 3,060 KB, largest frame 2,225 -> 2,181, untextured 27 -> 22; b30 largest frame 2,413 -> 2,402. The hands-off openings bind few of the changed lightmaps: what the VRAM texture cache gains in play is up to the pack's saving (137 KB on c20's track pack, 30 KB on b30), and on c20 it is room the textures don't wait for (section 2.1), so it buys VSPLIT room, not fewer untextured frames.

**The frames for Sheepdog01** (lab results branch, `sierra-batch/39680280258/<run>/shotNNN_tT.png`, the same names on both sides of a pair): `c20-plain-1` vs `c20-look` (track pack; most changed: `shot001_t13.png`, `shot002_t19.png`), `c20-plain-main` vs `c20-look-main` (full-size lightmaps; `shot001_t13.png`), `b30-plain-1` vs `b30-look` (`shot009_t104.png`); `c20-plain-2` and `b30-plain-2` are the controls.

**Sheepdog01's call:** whether 16-colour lightmaps and HUD sheets (strict bar; at most a 17-level channel step on 0.3% of a frame here) look the same to him. The bytes: c20 -137 KB on the shipped track pack (-523 KB on the main pack), b30 -30 KB. If yes, it ships as its own pack beside the usual one with a keep word to pick it (`design_clut4w.md` recommendation 2), after the all-maps bake the bake rule asks.

**Decided (Sheepdog01, 2026-10-21 17:17):** it ships, on by default in the bake (not as a second pack), `--no-clut4-look` to bake without it; Sierra-III `clut4ship`, `docs/review_clut4look.md`.
