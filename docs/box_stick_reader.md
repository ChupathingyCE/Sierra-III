# Box: the stick reader (streaming)

One owner of the Memory Stick: every read and write goes through it, in priority order, ahead of need where need can be known. First draft 13:03 2026-11-14 (the audit, a read; the lead's 12:2x order under memory relief step 7b; REBUILD's Streaming row). Nothing here is built or run. Labels: [M] PSP, [E] emulator, [D] read, [U] inference. The lab's rows are named in backticks.

## 0. In short

- **The stick is not short of capacity; the game waits on it at the wrong moments.** On the long benches the game thread spends 1-5% of the clock inside synchronous reads, in bursts: single reads of 0.3-0.6 s on the PSP-1000 are the hitches Sheepdog01 sees.
- **Every earlier try changed how much is read or held; none changed who waits.** A bigger geometry cache cut misses and not time (MC3, D20S). Aligned reads saved 0.14 ms a command (STKALIGN). Asynchronous geometry alone (b68) made it worse, because the texture cache's synchronous reads then queued behind it (b72: 2.5-5 s frames).
- **The box is `docs/streaming.md`'s S1 and S2, unbuilt since 10-11:** S1, one input stage owning the stick (the builder's estimate: 2-3 days); S2, prefetch by place from a per-cluster table in the bake (~1 day after).
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

(the lab's `tools/levercheck.py --ground stick-io`, and the docs)

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

1. **S1a: the queue under the readers that already wait asynchronously** (textures' next-frame path, sound stream pieces, writes): no behaviour change, the lock's waits by class logged. It shows the real contention on the 1000.
2. **S1b: geometry onto it** (asynchronous with the skipped-draw form, warm at load), textures' synchronous budget removed. This is the slice b72 failed at; it goes in only with slice 1's wait figures.
3. **S2: the per-cluster table in the bake and prefetch by place and by object.**
4. **Animation pages by encounter.**
5. **The cache sizes from the table** (§6), handed to the memory map.
(S3, the pack order, rides the next bake.)

## 10. Open

- The table's size and the bake's cost.
- Whether a pose may hold a tick instead of waiting for its page ("plays the same", an outcome census): it would remove class 0's exception; not proposed until prefetch shows how often it would matter.
- The 1000's 0.3-0.6 s reads: which reads they are (first touch of a file region, a FAT chain, the card's own housekeeping). The lab's firmware notes have the driver's side; the count is which request each long read was.
- Upstream manifest: none (the engine's readers are the port's own files already); the engine's sound reader thread is the one engine-side reader to route.
