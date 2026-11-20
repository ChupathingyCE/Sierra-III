# Box: the stick reader (streaming)

One owner of the Memory Stick: every read and write goes through it, in priority order, ahead of need where need can be known. First draft 13:03 2026-11-14 (the audit, a read; the lead's 12:2x order under memory relief step 7b; REBUILD's Streaming row). Nothing here is built or run. Labels: [M] PSP, [E] emulator, [D] read, [U] inference. The lab's rows are named in backticks.

## 0. In short

- **The stick is not short of capacity; the game waits on it at the wrong moments.** On the long benches the game thread spends 1-5% of the clock inside synchronous reads, in bursts: single reads of 0.3-0.6 s on the PSP-1000 are the hitches Sheepdog01 sees.
- **Every earlier try changed how much is read or held; none changed who waits.** A bigger geometry cache cut misses and not time (MC3, D20S). Aligned reads saved 0.14 ms a command (STKALIGN). Asynchronous geometry alone (b68) made it worse, because the texture cache's synchronous reads then queued behind it (b72: 2.5-5 s frames).
- **The box is `docs/streaming.md`'s S1 and S2, unbuilt since 10-11:** S1, one input stage owning the stick; S2, prefetch by place from a per-cluster table in the bake. ~~(the builder's estimate: 2-3 days, and ~1 day after)~~ **Withdrawn 2026-11-14 13:1x (Sheepdog01: a gross overestimate): that figure was relayed, and neither `docs/streaming.md` nor `docs/hw_capabilities.md` carries it.** By the code [D]: the asynchronous geometry path is built (`geoasync`, `psp_geo.c`), urgent piece reads and game-thread-first reads exist for the sound stream, and every reader already takes one lock; slices 1 and 2 are a queue put under readers that exist. The code is hours of a session. What sets the elapsed time is proof on the PSP-1000 (b72's failure only shows there): one emulator batch and one pair a slice, ~20-30 minutes of PSP each.
- **Compressed blocks don't pay (§11, measured 2026-11-15):** decoding a 16 KB block costs 0.39-0.49 ms on either core, about the 0.33-0.47 ms of stick time its smaller read saves; once the game no longer waits on reads, fewer bytes buy only throughput the stick has to spare.
- **It is also the sizing rule the caches lack:** with look-ahead, a cache holds what is in view plus what the table says is next, a number the bake can compute per map, instead of "as much as fits".

## 1. Today: who reads, and what it costs

[D, `psp_geo.c`, `psp_tex.c`, `psp_anim.c`, `psp_dsound.c`; `docs/streaming.md` §2; `docs/geometry_streaming.md`]

| Reader | How it reads today | If its bytes are late |
|---|---|---|
| Geometry blocks | synchronously on the game thread at the draw that misses (`geoasync` off since b72) | nothing is late: the frame waits |
| Textures | synchronously, within a per-frame budget; past it the request waits a frame | a stand-in colour (`texavg`) |
| Animation pages | synchronously at the pose that needs the page | nothing is late: the tick waits |
| Sounds (whole) | the engine's reader thread, into the sound cache; the eDRAM tier's pieces by ME copy jobs | held back (`sndwait`) or refused (the lab's B84) |
| Sound stream pieces | read as a line or loop plays (off by default) | silence (the lab's B86) |
| The log, checkpoints | written from the game's threads | - |

All of them share one lock on one device that serves one call at a time.

**What the long benches say** [M; the lab's `cache_pressure_by_build`, `stick_time_by_bench`; whole runs]:

| bench (PSP) | clock, s | geometry: misses / MB read / evicted / s | textures: MB / waited a frame / s | animations: reads / evicted / s | stick s (share of the clock) | slowest read |
|---|---|---|---|---|---|---|
| b30_play2 to 6,144 (1000) | 257 | 1,038 / 5.6 / 777 / 3.0 | 6.1 / 851 / 1.6 | 236 / 175 / 0.7 | 5.3 (2.1%) | 42 ms |
| d20_play1 to 7,122 (1000) | 284 | 3,611 / 17.4 / 3,043 / 9.9 | 11.4 / 1,326 / 3.4 | 572 / 510 / 1.9 | 15.2 (5.4%) | 285 ms |
| d20_play1 to 8,934 (3000) | 397 | 4,111 / 19.6 / 3,565 / 10.5 | 13.4 / 1,331 / 3.3 | 715 / 652 / 1.8 | 15.6 (3.9%) | 22 ms |
| c20_play1 to 13,080 (3000) | 503 | 1,095 / 5.3 / 578 / 2.0 | 16.4 / 2,095 / 3.5 | 366 / 349 / 1.0 | 6.5 (1.3%) | 23 ms |
| c20 legendary to 62,945 (1000) | 2,618 | 3,943 / 18.8 / 3,454 / 9.6 | 59.7 / 3,013 / 20.2 | 1,884 / 2,111 / 7.7 | 37.6 (1.4%) | 570 ms |
| c20 legendary to 48,527 (3000) | 1,971 | 3,114 / 14.7 / 2,557 / 6.2 | 51.9 / 2,967 / 12.0 | 1,483 / 1,630 / 4.3 | 22.5 (1.1%) | 113 ms |
| b40_play1 to 17,831 (1000) | 741 | 4,442 / 23.1 / 3,925 / 12.3 | 28.5 / 975 / 7.5 | 639 / 578 / 1.9 | 21.7 (2.9%) | 598 ms |
| b40_play1 to 15,852 (3000) | 620 | 3,598 / 20.3 / 3,369 / 8.0 | 23.0 / 1,069 / 5.1 | 494 / 434 / 1.3 | 14.4 (2.3%) | 103 ms |
| a30_play1 to 8,903 (3000) | 367 | 1,976 / 6.1 / 789 / 3.3 | 8.0 / 1,801 / 1.9 | 162 / 77 / 0.4 | 5.5 (1.5%) | 84 ms |

- **The demand is small against the stick:** d20's worst is ~31 MB in 284 s, ~110 KB/s, where the stick reads ~16 MB/s in large pieces and a random read costs ~2 ms (halo_bench v10). The stick is idle well over 90% of the time on every bench.
- **The cost is where the reads fall:** all of the geometry and animation seconds, and most of the textures', are the game thread stopped. As an average that is 1-5% of the clock; as the player meets it, it's the frame that reads a burst of blocks when a new area or a new enemy type comes into view, and on the 1000's stick the occasional single read of 0.3-0.6 s.
- **Geometry turns over its cache 7-10 times on d20 and b40** (17-23 MB through ~2 MB): what's evicted is read again. More room alone didn't help when tried (below), so this is first a question of timing, then of what the cache is asked to hold.
- The two PSPs' sticks differ: the 1000's worst reads are 4-10x the 3000's for the same bytes. Any pass line is set on the 1000.

## 2. What was tried on this ground

(the lab's `projects/sierra/tools/levercheck.py --ground stick-io`, and the docs)

| Try | Result | What it teaches the box |
|---|---|---|
| `geoasync` (b68): geometry misses read asynchronously, a missed draw skipped | off since b72: the texture cache's synchronous reads waited behind the asynchronous geometry read on the same lock, frames of 2.5-5 s | no reader may stay outside the queue; an in-flight read must be short enough for a needed-now request to wait behind |
| D20S, MC3: a larger geometry cache (from the texture cache, from the sound cache's room) | D20S rejected; MC3 on the PSP-1000: "the misses fall, the time does not" | room without timing doesn't pay; freed memory is not automatically the geometry cache's |
| D20P: prefetch ahead of d20's scripted cuts | idea, parked: its ceiling was put at 2.4% of the clock | the average is the wrong measure of a hitch; prefetch is priced by frames over a threshold, not by seconds |
| STKALIGN: sector-aligned reads | parked: a command is ~0.14 ms; "stick time's real lever is fewer reads" | fewer, larger reads (S3's pack order) come with prefetch, not before it |
| LW2: the log's writes made rare | shelved: on c10 no reader was waiting behind the log | writes go last in the queue, in short pieces; not a lever alone |
| SNDSTREAMLONG slices 3b-3e: urgent piece reads, the game thread's reads first, a read-ahead in a full pool | 3b kept; 3e's read-ahead re-read pieces and was rejected; lates on the PSP-1000 | a read-ahead needs room of its own, and one scheduler, not a second one beside the game's |
| RQF (halo-ce-vita's read queue: the game's waits first, urgent sounds, then by offset; BirchWoodGod) | conditional: the port's pack readers bypass the engine's queue, so a reorder there can't see them | the queue must be the port's, under every reader |
| `docs/geometry_streaming.md` (b87): warm at load, asynchronous misses, prefetch at natural points | analysed, not built ("nothing ships half-on") | its order stands: warm, then asynchronous, then prefetch, as one switch |

## 3. Contract

- **Owns:** the stick's lock, one reader thread at an I/O priority above the game's (halo_bench v10: at equal priority a completion never runs while the game thread is busy), the request queue, the open file handles, the prefetch table's use.
- **Takes** a request from any client: file, offset, size, destination, a class, a deadline, and what the client does if it's late.
- **Promises:** a client never blocks on the stick. It gets "resident", "coming by its deadline" or "not this frame", and its own late form covers the last (a stand-in texture, a skipped or coarser draw, a held sound). The one exception is named in §4.
- **Writes** (the log, checkpoints) are requests too, served last, in pieces short enough to wait behind.

## 4. The queue and its classes

| Class | Who | Deadline | If late |
|---|---|---|---|
| 0. needed now | an animation page for a pose in this tick; a texture or geometry block for a draw in view that wasn't predicted | this tick / this frame | **animation: the tick waits (the exception: a pose is simulation state; the wait is what today's code does, and prefetch at spawn is what makes it rare).** Texture: stand-in. Geometry: the draw skipped or its coarser level drawn |
| 1. sound's deadline | a streamed line's or loop's next piece (the engine queues ~0.74 s ahead); a whole sound asked for | the block it's needed for | silence or a held sound: both audible, so this class goes ahead of all prefetch |
| 2. prefetch | the table's sets for the clusters visible from a new cluster; an object's model blocks and textures when it's created or enters the visible set; an encounter's animation pages at its spawn; a BSP switch's materials; a scripted cut's next stretch (D20P); line heads by the script's position | none | nothing: it becomes class 0 or 1 if need arrives first |
| 3. writes | the log, checkpoints, saves | none | the log holds its bytes |

- **One read in flight.** Its size is bounded so class 0 and 1 never wait long behind it: at ~16 MB/s plus ~2 ms a command, 32 KB is ~4 ms and 64 KB ~6 ms. Prefetch reads are cut to that bound; a large block (a 178 KB line, a 300 KB texture) is several requests.
- **No pre-emption is needed** with bounded reads. What the bound can't cover is the card's own stall (the 0.3-0.6 s reads on the 1000): only reading ahead by more than a stall does, which sets the look-ahead distance (§5).
- **Cancelling:** a prefetch whose cluster the player left is dropped before it starts, never mid-read.

## 5. Read-ahead: where it's triggered and how far

- **By place (S2).** The bake lists, per BSP cluster, the texture and geometry blocks its surfaces use: an offline calculation from the map, so exact. In play, entering a cluster queues the sets of the clusters visible from it that aren't resident. The table's size is the count to take in the bake (a few KB a map [U]).
- **By object.** A model's blocks and textures are queued when the object is created or enters the potentially visible set, not at its first draw. An encounter's spawn queues its actor types' animation pages (`docs/streaming.md`'s H8): this is what takes class 0's exception out of fights (c20 legendary: 1,884 animation reads, 7.7 s, the slowest 391 ms).
- **By script.** A BSP switch's materials; a scripted cut's next stretch.
- **How far:** enough that a stall on the stick doesn't reach the frame: at the player's speeds a cluster ahead is seconds, more than the 1000's worst read. The measure is "needed-now requests a frame", which should fall to the unpredicted remainder.
- **The budget:** the reader thread does the reading; the game thread's cost is taking completions (the cache invalidate around each destination, the in-place vertex conversion, the bookkeeping). To hold under ~0.5 ms a frame [U], completions are taken a bounded number a frame at the frame's end, as the geometry cache's poll already does. The stick's own budget is its idle time: prefetch stops when a class 0 or 1 request is waiting.

## 6. What each cache's size becomes

Today each cache is "as much as could be found" (geometry 2,048-2,240 KB, textures ~3-4 MB, animations 512 KB, sound 1,792 KB) and is judged by misses. With the table:

- **Geometry and textures:** the size a map needs is the largest, over its clusters, of (the blocks in view + the blocks of the clusters visible from it that prefetch holds). The bake can print that number per map and per cluster. A map whose number is under today's size gives room back; one over it says which clusters need a coarser set. This is the memory plan's sizing step for both caches, computed, not swept.
- **Animations:** by encounter: the pages of the actor types alive at once (the lab's count on c20 legendary: 1,408 KB of frame data against a 512 KB cache). The bake knows each type's pages; the peak over encounters is the size.
- **Sound** is sized by its own rule (the lab's sound.md: held at most + margin, after pieces), but its reads enter this queue in class 1.
- So the box is where "every claimant ends with a fixed reservation sized from whole levels" (the lab's memory_relief brief, step 8) gets its numbers for three of the four caches.

## 7. What it removes, per map

Upper bound: the game thread's synchronous stick time in §1's table (5-38 s a bench; 1.1-5.4% of the clock), all of which moves to the reader thread. What stays on the game thread is completions (§5) and class 0's unpredicted remainder, which shows as stand-in frames and skipped draws, not stalls. The part a player feels is the tail: frames over 100 ms that contain a read, counted before and after. Neither figure is a promise until the pair below is run: the average is [M], the remainder after prefetch is [U].

## 8. Proof and price

- **Emulator:** play and both seeds identical with the box on (only arrival times change; animation pages are exact by class 0's wait); pictures identical once loaded, and the loading look (stand-ins, skipped draws) shown in shots for Sheepdog01's eye since it changes while a block is coming.
- **PSP, the in-frame pair** (an I/O box has no standalone price over a capture): d20_play1 and b40_play1 on the PSP-1000, old against new, repeat 2: the frame by window, frames over 100 ms, needed-now requests a frame, stand-in frames, sound's held and late counts (B84's and B86's lines).
- **The failure to rule out is b72's:** a run with every reader on the queue and the queue's worst wait for a class 0 request logged; the pass line is that wait under one bounded read.

## 9. Slices

1. **Built 2026-11-14 (builder; keep word `stickqueue`, off by default):** the stick's lock handed on by class (needed now: textures, geometry, animations; then the map file's reader, sound included; then other files; the log's writes last), FIFO within a class, one read in the stick as before; a `stickqueue:` line each report gives every class's waits and how often it went ahead of an older waiter. Today's waits by stream and 'behind whom' are `iostats`'s. **The queue under the readers that already wait asynchronously** (textures' next-frame path, sound stream pieces, writes): no behaviour change, the lock's waits by class logged. It shows the real contention on the 1000.
2. **Built 2026-11-14 (builder; keep word `stickreader`, off by default):** geoasync's path with a reader thread of the port's own (priority 0x18, above the game's) in place of the kernel's sceIoReadAsync, each read under the stick's lock as a read needed now, so the queue's order holds and no read is in the stick unseen (b72's failure). A draw whose block is still coming is left out for the frame, as geoasync does. **Geometry onto it** (asynchronous with the skipped-draw form, warm at load), textures' synchronous budget removed. This is the slice b72 failed at; it goes in only with slice 1's wait figures. **First PSP pair, d20 (2026-11-14, PSP-1000, sierra-hw 43651044551, 7a10bbd) [M]: the frame is the same in every window** (flight 55.4 vs 55.3 ms, beach 66.8 vs 67.0, bunker 68.4 vs 68.1; drawing +1.1-1.4% in flight and beach). Every geometry read left the main thread (9.7 s and 3,324 reads -> 0; the reader's 3,287 reads, 9.5 s in the stick), geometry's hitches 49 -> 0 and draws left out 112 -> 79, but the frames over 100 ms stayed ~470 and 'other' rose by ~45: the frame now waits on the reader for a draw's own block (the miss path, up to 2x the share) [U: that wait isn't timed yet]. One stick: moving the read moves the wait, not its time. The gain needs the wait gone: a draw left out rather than waited on once its block is pushed (`stickreaderskip`), or the block asked for ahead of need (slice 3, S2). **b40's pair (43651099096) [M] reads the same:** frame unchanged in every window, geometry's 7.2 s of reads and 30 hitches off the main thread, 'other' up ~40, draws left out 0-2. Slice 2 is the machinery; its gain waits on slice 3's reads ahead of need.
3. **S2: the per-cluster table in the bake and prefetch by place and by object.** **First count, geolead1 (2026-11-14 19:57, the lab's sierra-batch 43664528608; builder/stick-reader efba8a4, recon `geolead`; emulator counts [E], fixedticks) — the place table's reach is small:** of each geometry miss, its frames since the camera entered its cluster, level blocks apart from object blocks.

   | run | misses: level / object | KB: level / object | level misses a frame or more after entry | level KB on the entering frame | cluster stays |
   |---|---|---|---|---|---|
   | d20_play1 to 7,151 | 568 / 3,061 | 5,399 / 14,060 | 419 (74%); 2+ frames 266 (47%) | 1,928 KB (36%) | 63 |
   | b40_play1 to 17,785 | 549 / 3,645 | 4,949 / 19,091 | 421 (77%); 2+ frames 407 (74%) | 1,628 KB (33%) | 46 |

   - **Object blocks are most of the misses** (84-87% by count, 72-79% by KB): a place table can't hold them; they come by object.
   - **Most misses come long after entry** (object misses 64+ frames in: 787 of 3,061 on d20, 2,898 of 3,645 on b40; the camera changes cluster only 46-63 times a run): these are blocks evicted and needed again, or objects coming into view, not a new place. With the cache's 1 MB (264 blocks resident, 961 KB; 3,069 and 3,952 evicted), the churn (§1: 7-10x) is the larger part, which reading ahead by place doesn't touch [U until split].
   - **geolead2 (2026-11-14 20:1x, sierra-batch 43665632489, d20 [E]; an exact set of missed addresses): most misses are re-reads.** Of d20's 19,441 missed KB, first reads are 4,335 KB (22%: level 2,003, object 2,332) and re-reads of a block evicted before 15,106 KB (78%: level 3,395, object 11,711). **With a 1,792 KB cache (geo2: 2,048 KB less heaproom256's 256; '2 MB' before the audit's read, 2026-11-14 22:3x) object re-reads fall 61%** (2,494 → 1,018 misses, 11,711 → 4,579 KB) and **level re-reads don't move** (340 → 336, 3,395 → 3,316 KB): the level's are re-read by something that doesn't depend on the cache's size [U: level_streams_trim's budget, or a block asked again longer, psp_geo_lock's grow path]. So prefetch can reach at most the 22% that are first reads; the cache's working set (slice 5) and the level's re-read path carry the rest. b40's run stopped at 9,067 ~~because the set's 32 KB cost its heap~~ (superseded 2026-11-14 22:4x: geolead3, with no added memory, stops at 9,013 the same way; it's B79's full-heap stop on b40's recon build, its first refusal `overlay_on_a_wall`'s handled calloc, bugs.md B79); geolead3 re-reads it with the entry form.
   - **geolead3 (20:2x, sierra-batch 43667127729, 5625519; re-reads told by the block's entry, a lower bound, no memory) [E]:** d20 re-reads 69% of 19,438 missed KB (level 2,652, object 10,707), first reads 31%: the exact set's 78% sits above it, as a lower bound should. **b40 to game time 8,812** (the recon build runs out of heap there on b40, a calloc from 0x8a4f320 failing in geolead2 and 3, not the census's; geolead1 got past it): re-reads 52% of 11,871 KB (level 1,072, object 5,089), first reads 48% (level 2,805, object 2,905): the drive meets new ground, so prefetch's share is larger there.
   - **The level's re-reads, read in the code (builder 2026-11-14 21:0x) [D]:** a level material is drawn from a converted copy in the copies' own room (`level_streams`, psp_ge.c), and its geometry block is locked only when that copy is built; when the room is full, `level_stream_let_go_oldest` drops the least recently drawn copy, and rebuilding it locks the block again, by which time the block has usually aged out of the geometry cache (nothing touched it meanwhile). So part of the level's re-reads follow the copies' room, not the geometry cache's size. **But that part is small on d20 [E]:** 94 copies let go in the whole run (119 at geo2, whose `heaproom256` takes 256 KB of the level carve) against 340 level re-reads (336 at geo2). The other ~220-250 come by another path [U: psp_geo_lock's grow path, a block asked again longer (a material's lightmap vertices after its own), or the far level's / sspr's lookups of the same blocks]; next: a census splitting level re-reads by path, before slice 3's design is fixed.
   - **Reading for slice 3:** prefetch (by place for the level, by object for the rest) can reach the first reads: ~22-31% of d20's missed KB, ~48% of b40's opening half. The re-reads are the cache's working set (slice 5, §6: 1,792 KB took 61% of d20's object re-reads) and, for the level, a path that doesn't depend on the cache's size (d20's level re-reads didn't move at 2 MB): **that path is the next thing to read in the code** before slice 3's design is fixed [U].
   - **So the place table alone reaches at most ~10% of d20's missed KB** (superseded 2026-11-14 22:4x, the audit's read: '~27% / ~21%, the level's share' counted the level's re-reads too; of its 5,398 missed KB on d20 only 2,003 are first reads, geolead2, and prefetch can't help a re-read), less the third of that on the entering frame. ~~Next count (geolead2)~~ done: its answer is the bullets above (re-reads are most misses), and the lead chose slice 5 over slice 3 (22:27).
4. **Animation pages by encounter.**
5. **The cache sizes from the table** (§6), handed to the memory map.
   **What slice 5 asks for (builder 2026-11-14 20:5x, the lead's 20:32 ask; geolead2 43665632489, d20 [E]): about 768 KB of new memory for geometry, not a carve from textures.** The geometry cache is 1,024 KB by default, carved from the texture RAM cache (`ARENA_BYTES`; `geo2` takes 2,048 KB less `heaproom256`'s 256 KB, so 1,792). On d20 at 1,792 KB geometry read 10,314 KB from the stick against 17,528 (-7.2 MB; misses 3,629 → 2,149, frames that missed 579 → 340), but the texture cache it came from read ~19.8 MB against ~12.1 (+7.7 MB, from 'textures: reading took 330 → 519 ms' at ~37 MB/s; replays left to the frame's end 837 → 1,399, the ME's wants 360 → 783). **The stick's bytes come out about even: moving room from textures to geometry trades one reader's misses for the other's.** So the gain needs the 768 KB from elsewhere, and it competes with memory relief's list for the heap; the price to put beside that list is geometry -7.2 MB read on d20 with textures unchanged [U until a run holds textures at 1 MB beside geo2's room]. Next count: the same run with the geometry's extra 768 KB from the heap and the texture cache left whole (a keep word that doesn't carve), d20 and b40, to give the list its number. **The count (geoheapx1, 2026-11-14 21:0x, sierra-batch 43669720230; builder/stick-reader ec74039, the 64 MB recon build) [E]: 768 KB of heap for geometry, the textures as today, saves 5.7-6.0 MB of stick reads.** d20 to 7,149: geometry read 17,314 → 11,340 KB (-35%), misses 3,581 → 2,306, frames that missed a block 567 → 372. b40's whole drive to 17,853: 26,213 → 20,534 KB (-22%), misses 4,628 → 3,313, frames 843 → 607. Textures unchanged within their run-to-run spread (d20 316/337/323 ms reading, b40 713/610/621). The control (geoheapx at 1 MB) reads exactly as the default on both maps, so the instrument moves nothing by itself. **For memory relief's list: ~768 KB buys about a third fewer geometry misses and frames that wait on one on d20, a quarter on b40** (a frame's time is the PSP's to say: the in-frame pair, §8).
   **The victim tier (geovictim1, 2026-11-14 22:1x, sierra-batch 43674744658; builder/stick-reader a5b3c56, the 64 MB recon build) [E]: the eDRAM's free band can hold slice 5's room as a tier behind the cache, and catches about what the heap's 768 KB saves.** The cache at its default 1 MB and the textures as today; each block the cache let go (state 3, read whole) put in a FIFO tier of the size given (`geovictimkbN`), each later miss checked against it. Summed over the report windows: **d20 to ~7,150, a 544 KB tier catches 1,304 of 3,339 misses (5,767 of 15,552 KB, 37%), 768 KB 1,618 (7,105 KB, 46%); b40 whole, 544 KB 1,274 of 4,080 (7,136 of 23,598 KB, 30%), 768 KB 1,610 (9,205 KB, 39%)** (b40's one window that loads the next part, 36.5 MB of first reads, left out of both: no tier holds those). Beside geoheapx1 (768 KB more cache: d20 -35% of KB read, b40 -22%) the tier at the same size takes more of the misses, as a FIFO of the blocks just let go is the working set's edge. **What it would cost:** a hit is an eDRAM-to-RAM copy by the ME's DMA in place of the stick's read [U: the copy's time and its contention are the PSP's to price], no heap, and the band's room is shared with what else uses it (sndedram, mevram: what's free there is the 256-800 KB band's own figure, `hardware_facts.md`). **For the audit's point above:** what it moves is misses, so frames that wait on a block and draws left out are the PSP's question, not the KB; a 544 KB tier is the size that leaves the band's other users their room [U].
**The audit's cold read of slices 3 and 5 (2026-11-14 22:0x, before the lead accepts the next slice).** The counts are exact [E] and read right; four things to carry before slice 3 or 5 is chosen:
- **What a slice buys on the frame isn't stated.** Slice 2's PSP pairs (d20, b40) took every geometry read off the main thread and left the frame the same in every window, the waits moving to 'other' (findings stickr_d20_psp1000, stickr_b40_psp1000, re-read with the tock's abreport). So geometry KB read is no longer a stand-in for frame time: slices 3 and 5 cut KB and misses, and their worth is in what slice 2 left, the frames that wait on a block and the draws left out (d20 112 -> 79), not a frame figure [U]. Each slice's ask should say which of those it moves, by how much, before its PSP pair.
- **The place table's ceiling is ~10% of d20's missed KB, not ~27%.** The '~27% / ~21% (the level's share)' bullet predates geolead2: of the level's 5,398 missed KB on d20 only 2,003 are first reads (geolead2), and prefetch can't help a re-read, so by place it reaches ~10% (less the third on the entering frame); the ~22-31% is prefetch by place and by object together.
- **'2 MB' in geolead2 is 1,792 KB:** geo2 is 2,048 KB less heaproom256's 256 (slice 5's own line), so 'with a 2 MB cache object re-reads fall 61%' is at 1,792.
- **Two lines to bring into step:** the 'Next count (geolead2)' bullet is done (its answer is the bullets above it); and geolead2's 'b40's run is invalid (the set's 32 KB cost its heap)' against geolead3's 'not the census's' (B79's b40 heap stop near 9,000, bugs.md): one of them is superseded. **Brought into step (builder 2026-11-14 22:4x, the lead's 22:27 accepting all four):** the ceiling reads ~10%, '2 MB' reads 1,792 KB, the geolead2 line is marked done, and b40's stop is B79's (geolead2's line superseded).

(S3, the pack order, rides the next bake.)

**Size, by slice (the builder, 2026-11-14 16:3x, on the audit's code read; Sheepdog01 13:08).** Session time is writing and the emulator's proof; PSP time is the 1000's runs, which set the elapsed time.

| Slice | Session | Emulator | PSP-1000 |
|---|---|---|---|
| 1. The queue under today's asynchronous readers | 2-3 hours | one batch (play + seeds, the waits by class) | one pair (d20 + b40, repeat 2): ~45 min |
| 2. Geometry onto it, textures' synchronous budget gone | 2-4 hours | one batch, shots of the stand-ins | one pair, the b72 check (class 0's worst wait): ~45 min |
| 3. S2, the per-cluster table and prefetch by place | 4-6 hours (the bake's half is new code) | a bake and one batch | one pair: ~45 min |
| 4. Animation pages by encounter | 2-3 hours | one batch (exact by class 0's wait) | rides slice 3's pair |
| 5. The cache sizes from the table | 1-2 hours | the per-map numbers printed by the bake | none (a sizing step, priced in the next pair) |

So 11-18 session hours in all, and three PSP pairs (~2.5 hours of the 1000); slices 1 and 2, the part that relieves memory and the game thread, are 4-7 hours and two pairs [U: sizes from the code read, not from a build].

## 10. Open

- The table's size and the bake's cost.
- Whether a pose may hold a tick instead of waiting for its page ("plays the same", an outcome census): it would remove class 0's exception; not proposed until prefetch shows how often it would matter.
- The 1000's 0.3-0.6 s reads: which reads they are (first touch of a file region, a FAT chain, the card's own housekeeping). The lab's firmware notes have the driver's side; the count is which request each long read was.
- Upstream manifest: none (the engine's readers are the port's own files already); the engine's sound reader thread is the one engine-side reader to route.

## 11. Compressed blocks on the stick (the master's 01:00 (b), [enables:streaming])

**The question:** whether blocks are cheaper to fetch compressed and decode on a core than to read whole. The stick reads 16 KB at 13.7 MB/s and 64 KB at 15.7 MB/s (`streaming.md`), so each block's decode must cost less than the read time it saves.

The two candidates' sources are on the lab's branch `external`: `research/external/compression/`, holding lz4 1.9.4 (BSD-2) and miniLZO 2.09 (GPL-2+). **The worker's read of both decoders** (lab or-job 43688472875; checked by the builder 2026-11-15) [D]:
- **lz4's `LZ4_decompress_safe`** is the first choice.
  - On MIPS, GCC takes its packed-union reads (`LZ4_FORCE_MEMORY_ACCESS 1`, lz4.c:81-82). Those compile to `lwl`/`lwr`, which are safe on the Allegrex.
  - It also takes the non-x86 loop (`LZ4_FAST_DEC_LOOP 0`, lz4.c:455-468; both lines checked).
  - It copies 8 bytes a step, and 16 for literals.
  - Its only libc call besides `memcpy` is `memmove`, for the last literals. With `LZ4_FREESTANDING` and its own `memmove`, it is clean for the ME.
  - It can dirty up to 7 bytes past the decoded length, within the buffer's capacity, so a reader must not read past the return value.
- **miniLZO's `lzo1x_decompress_safe`** on MIPS compiles only its byte loop: no unaligned option is set for MIPS, and `LZO_ALIGNED_OK_4` is undefined in 2.09. That makes it roughly 8x the loop steps per byte for copies [U]. It calls nothing and writes exactly what it decodes.
- **Knobs** if lz4 is built:
  - `LZ4_FORCE_MEMORY_ACCESS` left at 1 (never 2: plain `lw` on unaligned pointers would fault);
  - `LZ4_STATIC_LINKING_ONLY_DISABLE_MEMORY_ALLOCATION 1`;
  - `LZ4_memmove` defined to a local loop.

**The ratios (lab sierra-packratio 43694548935, bake w162far19; every block decoded again and checked, none bad)** [E: an exact host count]. Each figure is the compressed size as a share of the original:

| Kind | Block | lz4 | lz4 HC 9 | LZO1X-1 | Range over the maps |
|---|---|---|---|---|---|
| Geometry (`.pgeo`) | 16 KB | **63-66%** | 59-61% | 64-67% | a30, b30, b40, c20, d20 |
| Geometry (`.pgeo`) | 64 KB | 57-61% | 50-54% | 61-65% | |
| Textures (`.ptex`) | 16 KB | **49-64%** | 41-60% | 49-64% | c20 best, b30 worst |
| The map file (`.map`) | 16 KB | 65-84% | 59-82% | 63-84% | b30 and a30 worst: 1,600-1,700 of their 9,400 blocks don't shrink at all |
| Sound pieces (`.pstn`) | 16 KB | 64-72% | 59-69% | 62-71% | small files |

- **At the stick reader's block size (16 KB), lz4 takes about a third off geometry and 35-50% off textures.**
  - HC buys another 4-8 points. Its cost is paid at the bake, and it uses the same decoder.
  - At 4 KB blocks the ratios are 5-7 points worse for geometry, and much the same for textures.
- **What that's worth:** at 13.7 MB/s a 16 KB read takes ~1.2 ms. At 64% it reads 10.5 KB, saving ~0.4 ms of stick time a block [U: the stick's rate taken as flat in size].
  - It pays on a core only where decoding 16 KB costs less than that.
  - On the ME, it pays wherever the ME has room.
  - What it doesn't save is the cache's room: blocks are held decoded.

**The decode's price (2026-11-15, the lab's bench-hw 43703346808, PSP-1000; `bench/halo_lz4dec` over 240 real 16 KB blocks of bake w162far19, 24 geometry and 24 texture blocks from each of a30, b30, b40, c20 and d20, spread through each file; the decoder is the port's own, `psp/src/psp_lz4.c`)** [M]. Every block decoded right on both cores (960 decodes each). µs a 16 KB block, mean (median, 95th percentile):

| Kind | Form | Compressed | Main CPU warm | Main CPU emptied | ME warm | ME emptied |
|---|---|---|---|---|---|---|
| Geometry | lz4 | 10.4 KB (65%) | 487 (484, 651) | **491** (488, 658) | 487 | **494** |
| Geometry | HC 9 | 9.7 KB (60%) | 440 (448, 532) | **444** (448, 557) | 438 | **448** |
| Textures | lz4 | 8.8 KB (56%) | 443 (433, 652) | **455** (441, 658) | 440 | **452** |
| Textures | HC 9 | 8.0 KB (51%) | 382 (378, 546) | **392** (389, 555) | 377 | **393** |
| 16 KB memcpy | - | - | 155 | 151 | - | - |

- **The decode runs at 32-41 MB/s of output, 3x a 16 KB copy's time, the same on both cores (within 1%), and warm and emptied within 1-3%:** ~~it's bound by its own work, not memory~~ **(superseded 2026-11-15 04:5x, the audit's second read):** warm and emptied agree because neither is warm for data: a block's input and output (8-10 KB and 16 KB) don't fit the 16 KB data cache, so both modes miss alike. By lines, a geometry block is ~166 source and 256 destination line misses, ~71-91 us of its 491 at 56-72 cycles a miss (`hardware_facts.md`) [U on M]: memory is about a sixth of the decode, its own work the rest (~110 cycles a sequence). The 256 destination lines are fetched only to be overwritten; claimed instead (`cache 0x18`, as CLAIMFILL does for `memcpy`) they'd take ~43-55 us off, about a tenth, exact [U]. (The ME's warm rows are one untimed decode then one timed, not the mean of two: `main.cpp`'s ME loop, and the pass's 92.6M cycles is two decodes a block plus the check [D].) 3 texture blocks of 120 don't shrink under lz4 (2 under HC): they'd be stored raw. Geometry always shrinks.
- **Against the stick time it saves, it is a wash.** The stick's figures (16 KB at 13.7 MB/s, 64 KB at 15.7) fit ~0.19 ms a read plus ~0.059 ms a KB [U on M], so a block read smaller saves 0.059 ms a KB it doesn't read:

| Kind, form | Stick time saved a block | Decode (emptied) | Net, if the reader decodes as it reads |
|---|---|---|---|
| Geometry lz4 | 0.33 ms | 0.49 ms | **-0.16 ms** (worse) |
| Geometry HC 9 | 0.38 ms | 0.44 ms | -0.07 ms |
| Textures lz4 | 0.43 ms | 0.45 ms | -0.03 ms |
| Textures HC 9 | 0.47 ms | 0.39 ms | +0.08 ms |

  Taking the stick's rate as flat at 13.7 MB/s instead (the line above's [U]), the nets are -0.09, +0.01, +0.06 and +0.18 ms. Either way, decoding costs about what it saves.
- **Pipelined on a core with time to spare** (block k decoded while block k+1 is read), a burst of reads would finish sooner: each geometry block takes its compressed read, ~0.81 ms against 1.14, ~29% sooner (HC textures ~42%), at 0.39-0.49 ms of that core a block [U on M]. No core has that time in the heavy views: the main CPU sets the frame, and the ME is busy 69% of b30's frame and binds d20's (`hardware_facts.md`).
- **What decides it:** this box's point is that the game stops waiting on the stick (§0). Once reads are asynchronous and ahead of need, stick time is off the frame, and the stick is idle over 90% of the time on every bench (§1): fewer bytes buy only a burst's throughput, which the demand doesn't need, at a core's cost. Nor does it save the caches' room: blocks are held decoded. **On this price the box takes raw blocks** (the lead's call). Compressed blocks come back if the decoder gets to ~0.2 ms a block (~80 MB/s, under half the stick time it saves), or if a stretch shows prefetch falling behind on the stick's throughput, with a core's time free for it. **The audit's second read (2026-11-15 04:5x):** the table's figures match the bench's raw rows, and parking it holds. Two notes on the way back in. (1) The 0.2 ms bar is out of reach on the bench's own floor [U on M]: a 16 KB copy alone is 0.151 ms, which leaves 49 us, ~13 cycles for each of a block's ~1,220 sequences; a claimed destination and a short-sequence path might reach ~0.35 ms [U]. So only the throughput clause is live. (2) That clause's plainest case is the game already stopped on the stick (a level's load, a hitch prefetch missed): no frame runs then, so the ME isn't being handed the frame's jobs and has the time the heavy views lack, and the pipelined figure above (a burst ~29-42% sooner) applies there. Unpriced: what share of a load or a hitch is stick time [U].
- **Where the decoder's time goes:** a geometry block under lz4 is ~1,220 sequences (85% with 8 literals or fewer, 74% with matches of 8 bytes or fewer, 2% with offsets under 8; HC 989), so 491 µs is ~130 cycles a sequence, of which the block's misses are at most memcpy's ~150 µs [D: counted from the same blocks on a desktop; U: the split]. The per-sequence work is the lever: short literals and matches copied in one fixed 16-byte step without a loop, wherever the block and buffer leave room (the format allows it; our own form) [U: not built, not priced].
