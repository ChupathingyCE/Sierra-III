# Streaming: the Memory Stick as a stage of the pipeline (2026-10-11)

Part of the priority in `big_levers.md` §0 (aligning the workload to the pipeline). This covers what the game reads from the Memory Stick as it plays (textures, geometry, animations, sound), how to make it arrive ahead of need without stopping the game, how to make the assets smaller, and how to measure the headroom and balance the caches.

Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code, **[U]** inference, **[C]** computed exactly from the data. Quality: **L** lossless, **≈L** invisible or inaudible at 480×272 and the PSP's output, **V** visible, a trade-off for Sheepdog01's A/B (the legend of `asset_baking.md`). Levers here are numbered **S1–S11** (streaming and assets).

## 1. In short

- **The stick has plenty of capacity.** It's busy about 3% of a run [U: the log's reads against halo_bench v10's speeds].
- **The trouble is timing.** Three of the four readers stop the game thread while they read, behind a fourth that reads on its own thread. And every read is made only once the thing is needed.
  - One beach frame took 1.1 s, 844 ms of it reading textures.
  - 30 frames drew a flat-coloured surface, up to 12 in a row: seconds at 3–5 fps.
- **The fix is the same shape as the rest of §0:** one input stage that owns the stick, works ahead of the frame, and reads in large pieces.
- **Smaller assets help twice:** fewer bytes to read, and more of the working set in each cache.

## 2. Today (b114 test 1: b30 from the start to the first bunker, about 12 minutes) [M]

| | Textures | Geometry | Animations | Sound |
|---|---:|---:|---:|---:|
| Read from the stick | 8.0 MB in 598 reads | 7.1 MB in 1,321 | 2.6 MB in 320 | **118 MB in 3,695** |
| Average read | 13 KB | 5 KB | 8 KB | 32 KB |
| Time spent reading | 3.8 s | 3.9 s | 1.6 s | not timed |
| Who waits [D] | the game thread (`psp_tex.c`) | the game thread (`psp_geo.c`) | the game thread (`psp_anim.c`) | the engine's reader thread (priority 0x1C, above the game's 0x20) |
| Worst frame | 844 ms | 393 ms | 41 ms | |
| What shows | 30 frames with a flat-coloured surface, longest run 12 | 196 frames skipped a draw | | 3 sounds failed for room |
| Cache | 3,120 KB (VRAM 1,216 + RAM 1,904) for a 7,060 KB pack | 2,048 KB for an 11,988 KB pack | 512 KB, all 64 pages in use | 1,792 KB, full |

**What the stick gives** (halo_bench v10 [M], `psp_hardware_reference.md`):
- **Speed by read size:** 4 KB reads run at 8.5 MB/s, 16 KB at 13.7, 64 KB at 15.7, 256 KB and up at 16.3.
- **A random read costs about 1.6–2 ms more than a sequential one.**
- **`sceIoReadAsync` leaves the CPU free** (97% kept during a 1 MB read), as long as the I/O priority sits above the game thread's.

**What that means:**
1. **Sound is the stick's biggest reader:** about 7× the other three together.
   - The cache is 1.75 MB against the Xbox's 4 MB, so part of the 118 MB is probably sounds read again soon after being evicted. The log doesn't separate those from streamed music and dialogue yet.
2. **The game thread's reads take about twice what the bench predicts** [U]:
   - textures 3.8 s against ~1.7;
   - geometry 3.9 against ~3.0;
   - animations 1.6 against ~0.8.

   The likely difference is waiting behind the sound reader on the same stick. That would also explain the 844 ms frame on the beach at game time 4,791.
3. **The reads are small and scattered,** so each one's fixed cost (~2 ms) dominates: ~2 MB/s in play against 16 MB/s for large reads.
4. **Everything is read on demand.** A texture is asked for the first time it's drawn, and the 96 KB-a-frame budget spreads a new view over several frames.
5. **Texture space is tight:** one frame bound 2.43 MB of textures, against 3.1 MB of caches.
   - The best third of the VRAM texture set (383 KB) took 92% of VRAM's binds.
   - That's the basis for keeping ~400 KB of textures in VRAM in §0.
6. **History, b71:** asynchronous geometry reads made every frame take 2.5–5 s, because the texture cache's synchronous reads waited behind them on the same stick. Everything went synchronous after that (`psp_geo.c`). The lesson is one reader, not none.

## 3. Arriving ahead of need

| # | Lever | What it does | Quality | Needs |
|---|---|---|---|---|
| S1 | **One input stage** | A single thread owns the stick. It takes requests from all four readers (textures, geometry, animations, sound) and serves them in priority order: needed this frame; sound chunks, whose deadline is ~2.6 s (`audio.md`); then prefetch. The game thread never sleeps on the stick. Reads go async at an I/O priority above the game's (v10's rule) | L (only arrival times change) | The game's reader thread folded in, or its requests routed through S1 |
| S2 | **Prefetch by place** | At bake time, list each BSP cluster's textures and geometry blocks; it's an offline calculation from the map, so exact. In play, a cluster change queues the sets of the clusters visible from the new one. Characters: an encounter's spawn queues its types' models, textures and animations | L | A per-cluster table in the bake |
| S3 | **Pack order** | Sort the packs by cluster and first use, so a prefetch is a few large reads near 16 MB/s instead of hundreds of small ones near 2 MB/s | L | `texpack.py`; the Invader fork's `-G` writer |
| S4 | **Smallest mips first** | A texture's top level is 3/4 of its bytes, and at 480×272 most surfaces sample lower levels. Load small to large: a blurry texture shows within a frame instead of flat colour, and distant textures never need their top level. The renderer already works out the level a surface needs (`texture_levels_by_distance`, `psp_ge.c`) | ≈L once loaded (with a one-level margin); **V while loading** (blurry instead of flat) | Pack levels stored small to large; residency by level |
| S5 | **Pin the effects** | Tracers, plasma, muzzle flashes and explosions are bound constantly: they stay resident for the whole map | L | A pinned set in the texture cache |

## 4. Smaller assets

### Textures

Today's bake [D, `texpack.py`]:
- Swizzled CLUT8; CLUT4 where it's within 1 dB of the 16-bit framebuffer's own rounding.
- The top mip dropped for textures of 64 px and up; 256 px at most.
- The asset track halves lightmaps and keeps alpha-tested coverage through the mips.

On Blood Gulch, 23.1 MB of Xbox textures became 6.34 MB [M, `asset_baking.md`].

| # | Lever | Saves | Quality | Notes |
|---|---|---|---|---|
| S6 | **Native DXT for DXT sources.** The GE samples DXT1/3/5 directly. Take the Xbox's own mip level at the baked size, where the Xbox bitmap has one (110 of Blood Gulch's 516 have no mips), and rewrite each block in the GE's DXT layout, with no re-encode | DXT1 sources that stayed CLUT8: 8 → 4 bits a texel, half their bytes | **L against the Xbox's own blocks.** A CLUT8 copy of a DXT source quantises a second time | Blood Gulch's bitmaps: DXT1 228 of 516 (9.2 MB), DXT3 112 (6.2 MB) [M]. Needs: b30's split by format and chosen depth, and a GE A/B (DXT sampling from RAM hasn't been timed). **For effects with alpha, native DXT3/5 is the Xbox's own look at 8 bits**, a candidate fix for test 7–8's streaky tracers and cloudy plasma |
| S7 | **Learned mip need.** Record, per texture, the finest level the renderer asked for across runs (as the learned binds already are, in `<map>.vram`). The bake then drops levels never used | unknown: textures always seen far away (cliffs, distant structures, the sky) lose their larger levels | ≈L with a one-level margin; a view never played could be a level blurrier | Runs on every map feed it; emulator routes can cover what Sheepdog01 doesn't play |
| S8 | **The CLUT4 bar by class** (1 dB today) | more textures at 4 bits | V, a step at a time | A per-class A/B (terrain, objects, detail maps) |

### Sound (the shelved bake, revived for streaming)

The bake was shelved because sound wasn't limiting speed (`optimization_ranking.md`). Streaming brings it back: sound is the stick's biggest reader, and its cache is the fullest. The ME mixer decodes Xbox ADPCM [D, `psp_dsound.c`], so the bake can keep that codec and change only channels and rates. That way nothing changes in the mixer.

| # | Lever | Saves | Quality |
|---|---|---|---|
| S9 | **Option B** (`audio.md` §3b): sound effects downmixed to mono, 22.05 kHz at most, music stereo | Blood Gulch: 15% of the bytes [C]; stereo effects halve (`bullets_projectile`: 42.8 → 21.8 KB [M]) | ≈L for 3D sounds, which play as mono anyway. Effects with real stereo width lose it. The re-encode is a second generation: median 29.2 dB SNR over 40 permutations [M]. Needs a listening test on the PSP's speakers |
| S10 | **Lower rates for noisy classes** (footsteps, impacts, casings at 11 kHz; or all effects at 16 kHz) | Blood Gulch: 32–38% [C] | V: needs a listening A/B |
| S11 | **A bigger sound cache**, from the RAM §0 frees | fewer re-reads | L |

b30's sound census is still to do (`snd_census.py` on b30). Campaign maps add music and scripted dialogue.

## 5. Measuring the headroom, and balancing it

Headroom has two parts:
- **Bandwidth:** the stick's time against the demand at the busiest moments, not the average.
- **Capacity:** each cache's size against its working set.

Balancing means splitting a fixed amount of RAM between the caches, so the next KB always goes where it saves the most stick time. That's subject to each stream's deadline:
- sound: hard (a gap is heard);
- animations: hard (the pose waits for its frame data);
- textures: soft (flat colour);
- geometry: soft (a skipped draw).

| Part | How | Where |
|---|---|---|
| Supply | halo_bench v10's read-size table [M] is the curve: about 2 ms a random read plus the bytes at 16 MB/s. No new hardware test needed | done |
| Demand on the PSP | A keep word (`iostats`) adding a line per heartbeat for each stream: reads, bytes, time queued, time reading, worst read, and whether each read was needed now or prefetched. Totals only, so it costs no RAM worth counting. It settles point 2 of §2 | b115, Sheepdog01's PSP |
| Demand by stretch, and the cache curves | The emulator writes every cache request (texture key and size, geometry block, animation page, sound permutation) to a trace on the host, along a fixed route. A replay tool then runs each stream's requests through its cache at 0.5× to 3× its size, giving stick bytes and reads per stretch for each size. `tools/anim_cache_sim.py` is the precedent | the lab's emulator runs |
| The balance | From the curves: the RAM split that minimises stick time for the RAM available, the deadline streams first. It's re-run after each asset bake, since S6–S10 move every curve | a table per stretch in this doc |

What comes out, per stretch (flight, landing, beach, bunker):
- the stick's busy share at the busiest second and the busiest 5 seconds;
- each stream's needed-now reads per frame, which are the hitches and the pop-in;
- the recommended size of each cache, with the stick time it saves.

## 6. Order

1. **Measure:**
   - `iostats` in b115;
   - the emulator traces and the replay tool;
   - b30's sound census;
   - b30's texture split by source format and chosen depth (a stats pass of `texpack.py` in the lab; results only, no data leaves the runner).
2. **S1**, the single input stage. It fixes the hitches whatever the bakes do.
3. **S2 + S3**, the per-cluster table and the pack order (bake).
4. **S5 + S6**, pinned effects and native DXT, with a GE A/B on the cinematic. The full-size effects bake comes in here.
5. **S9**, the sound bake, with a listening test. Then S11, sized by the curves.
6. **S4 and S7**, mips first and learned mip need (A/B for the loading look).
7. **S8 and S10** only if the curves say capacity is still short.
