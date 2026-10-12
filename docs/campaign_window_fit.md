# Campaign maps against the PSP-1000's tag window (2026-10-06, all ten maps)

Follows campaign_census.md, whose last table (build-68 bake) this supersedes for c20 and b30. Labels: [M] measured, [E] estimate.

The build-70 collision packing (edges, surfaces and bsp2d references at 16 bits) had never been applied to a campaign map: the first try (2026-10-04, 11:51) failed in the fork's spawn check ("invalid plane range"), and the fork was fixed afterwards for the multiplayer maps. Rebuilt today with the current fork, both maps build cleanly. Exact fit = tag data + largest BSP against the window's 9,437,184 bytes [M]:

| Map | Tag data | Largest BSP | Total | Against the 9 MB window |
|---|---:|---:|---:|---|
| **c20 The Library** | 4,829,652 | 4,595,712 (c20_3) | 9,425,364 | **fits, 11,820 bytes spare** |
| b30 Silent Cartographer | 8,115,092 | 3,776,512 (b30a) | 11,891,604 | over by 2,454,420 (2.34 MiB) |

- **The Library fits the PSP-1000 today.** The margin is 12 KB, so anything that later adds to its tags or BSPs needs checking against it.
- **b30 needs 2.45 MB more.** Its tag data by class (census of the -G build): animations 3.13 MB (39%, already keyframed), sound tags 1.47 MB (1,435 tags), object collision 0.77, scenario 0.74, effects 0.49, models without geometry 0.24, bitmap headers 0.16, fonts 0.16, UI widgets 0.10.
- **No single lever closes b30's gap.** Candidates [E unless noted]:
  - the BSP's cheaper levers (16-bit quantised position copy, leaf surface references and subcluster indices at 16 bits): ~0.6 MB on b30a;
  - the scenario's script node table trimmed to the nodes in use (4,557 of 19,001; ~0.29 MB), if the engine doesn't need the full table in the tag;
  - cinematic-only animations out of the window (unmeasured);
  - sound tag metadata (1.47 MB: needs a census of what in it the game reads, and when);
  - a bigger window: maps built for a lower `-X` address, paid for by shrinking the heap or game-state pools sized for the Xbox (b30's object pool peaked at 650 of 1,024 KB in the emulator).
- Both maps' texture packs must be re-baked against these builds (bitmap addresses move), and their .pstn too.

## All ten maps (same bake, same day) [M]

MB = 1,000,000 bytes. Gap = tags + largest BSP − 9,437,184. Columns after the bar split the tag data by class (census of each -G build).

| Map | Tags | Largest BSP | Gap | Animations | Sound tags | Object collision | Scenario | Effects | Rest | BSPs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| a10 Pillar of Autumn | 7.95 | 3.97 | +2.49 | 3.11 | 1.40 | 0.69 | 0.81 | 0.43 | 1.51 | 9 |
| a30 Halo | 7.83 | **4.66** | +3.05 | 2.85 | 1.44 | 0.79 | 0.79 | 0.53 | 1.42 | 2 |
| a50 Truth and Reconciliation | 8.73 | 3.64 | +2.93 | 3.28 | 1.52 | 1.03 | 1.01 | 0.48 | 1.41 | 4 |
| b30 Silent Cartographer | 8.12 | 3.78 | +2.45 | 3.13 | 1.47 | 0.77 | 0.74 | 0.49 | 1.52 | 2 |
| b40 Assault on the Control Room | **9.62** | 3.28 | **+3.47** | 3.36 | 1.51 | 1.06 | **1.48** | 0.59 | 1.62 | 13 |
| c10 343 Guilty Spark | 8.90 | 3.50 | +2.97 | 3.47 | 1.38 | 0.95 | 1.05 | 0.49 | 1.56 | 6 |
| **c20 The Library** | **4.83** | 4.60 | **−0.01 (fits)** | 1.76 | 0.31 | 0.53 | 0.66 | 0.44 | 1.13 | 4 |
| c40 Two Betrayals | 8.87 | 3.52 | +2.95 | 3.62 | 0.95 | 1.25 | 0.75 | 0.69 | 1.60 | 13 |
| d20 Keyes | 7.69 | 4.34 | +2.59 | 3.25 | 0.76 | 0.94 | 0.71 | 0.58 | 1.45 | 5 |
| d40 The Maw | 8.15 | 3.78 | +2.49 | 3.58 | 0.77 | 0.86 | 0.76 | 0.59 | 1.60 | 10 |

**Reading it:**
- **Nine maps sit in one band, 2.45–3.47 MB over.** The gap is structural, so the lever has to be general, not per map.
- **The Library is the outlier because of its cast:** no marines and no Covenant. Its animations are half the others' (1.76 MB) and its sound tags a quarter (0.31 MB).
- **Animations are the largest class everywhere (2.85–3.62 MB), and they are the characters' graphs, not cutscenes.** b30, per tag: marine 0.65 MB, elite 0.57, Master Chief 0.55, jackal 0.33, grunt 0.29, hunter 0.23, engineer 0.10; its level-specific cinematic animation is 23 KB. So campaign_census.md's "streaming cinematic-only animations" lever is small; withdrawn.
- **Sound tag data tracks marine dialogue:** 1.38–1.52 MB on the marine levels, 0.76–0.95 on the Flood levels (c40, d20, d40), 0.31 on The Library. It is spread thin (b30: 1,435 tags, the largest 22 KB).
- **b40 is the worst:** its tag data alone (9.62 MB) is larger than the window, with the largest scenario (1.48 MB) and much object collision.
- **a30 and d20 are pushed over by their BSP** (a30_b 4.66 MB, d20_hangar 4.34) more than by their tags.

**Animation frame data after keyframing** (the fork's build line; Bungie's own compressed animations not included): a10 1.99 MiB, a30 2.23, a50 2.48, b30 2.52, b40 2.47, c10 2.82, c20 1.43, c40 2.64, d20 2.61, d40 2.53. Frame data is read only while an animation plays, the same shape as the model geometry that already streams. **Streaming it through a cache is the one lever the size of the gap on every map** [E]. With the BSP's cheaper levers (~0.6 MB) it covers all ten on paper. The catch: the cache needs RAM of its own outside the window, and a first play of an animation becomes a Memory Stick read (the same hitch problem as geometry, needing prefetch by character type).

**Fork fix (2026-10-06):** c10, c40 and d20 failed to build ("invalid utf-8") because their script sources are Windows-1252, with an ellipsis or curly apostrophe in debug `print` strings. The fork now turns those bytes into ASCII when a source isn't valid UTF-8 (4, 17 and 8 bytes); prints do nothing in a release build.

## Finding 4 MB (plan, 2026-10-06)

Target: b40's 3.47 MB plus a margin, so 4 MB. **It is one 24 MB budget.** User RAM on the PSP-1000, from psp_build.py's layout and the hardware logs [M]:

| From | What | Size |
|---|---|---:|
| 0x08804000 | the program: code 2.92 MB, read-only data 0.47, data 0.18, zeroed data 2.37 | 5.94 MB |
| after it | early allocations (0.26 MB), then the heap, sized to end at the game state | 5.25 MB |
| 0x092B0000 | game state (fixed pools, saved whole at checkpoints) | 3.43 MB |
| 0x09600000 | **the tag window** | 9.44 MB |
| 0x09F00000 | thread stacks | 1.05 MB |

(MB = 1,000,000 bytes, as in the tables above; 25.15 MB in all.)

Three kinds of saving, all drawing on that budget:
1. **Shrink what's in the window** (formats). Straight gain.
2. **Move data out of the window** (streaming). Gains only the difference between the data and the cache that replaces it.
3. **Grow the window into its neighbours.** The window starts lower; every map is rebuilt for the new `-X` address.

| Lever | Kind | Saves [E] | Confidence |
|---|---|---:|---|
| Debug-only data out of the release build (AI debug tables 238 KB static + 114 KB heap, the ME measure buffers 65 KB, profiler graphs 33 KB, render debug 30 KB) | 3 | ~0.45 | Sizes measured; references to check |
| BSP formats: 16-bit quantised position copy, leaf surface references and subcluster indices at 16 bits | 1 | 0.4–0.7 | Known approach, few readers |
| Game-state pools sized to measured peaks (actors 457 KB, props 234 KB, script threads 134 KB, lights 108 KB, …) | 3 | ~0.5 | Needs peaks on all ten maps |
| Dead code removed at link time | 3 | 0.2–0.6 | Unknown: try it and measure |
| **Animation frame data streamed through a cache** | 2 | **1.5–2.3 net** | Depends on the working set, unmeasured |
| Dialogue sound tags streamed | 2 | 0.3–0.8 | Unknown |

**Animation streaming is the load-bearing lever:** without it the rest come to ~1.5–2.6 MB (with sound), short of every map's gap. With it the total is ~3.5–5.5 MB.

Order:
1. **Measure (emulator, no hardware build):** the animation working set (which animations play, per character, per minute) on the openings of b30, a10 and b40 in the 64 MB recon build; game-state and heap peaks on all ten maps; a dead-code link test.
2. **Cheap wins:** debug data out, BSP formats, dead code, pools to their peaks (~1.5 MB). They also widen The Library's 12 KB margin.
3. **Animation streaming:** frame data to a side file at bake time (like -G), a cache with prefetch by character type when a unit spawns, decode reading through the cache.
4. **If still short:** dialogue sound tags.

Caveat: scripted emulator runs cover level openings, not the late fights; pool sizes keep a margin, and the hardware logs report every pool's peak.

## The heap, not the window, stopped The Library first (b93, emulator) [M]

The Library loads on the PSP-1000 layout (tags 4.83 MB + c20_3 in the window), then runs out of **heap** at its first frame: 4,728 of 4,736 KB in use, and every level material's copy and model conversion after that fails (0.4–0.9 fps of retries). Blood Gulch peaks at 4,500 KB of the same heap. What the heap holds at that point (bytes asked since start, by caller):

| Caller | KB | What |
|---|---:|---|
| platform_contiguous_alloc | 1,920 | the game's own blocks: 1 MB (the sound cache, by its size), 384, 128, 96, 96, 64 (the texture cache), 64 KB |
| psp_geo_map_opened | 1,024 | the geometry cache (in the heap since b73) |
| GlobalAlloc | 1,023 | data arrays: script nodes 371 KB, ai_debug 114 KB, sounds 86 KB, … |
| IDirectSound_CreateSoundStream | 237 | sound streams |
| D3DDevice_CreateIndexBuffer | 192 | the D3D shim's index buffer |
| level_stream_get | ~400 | the level's vertex copies (c20 needs far more: its first frame failed) |
| psp_ge_stencils_open | 194 | c20's stencil bake (80 walls) |
| skin_plan | 105 | the Flood's skinning plans |

So the 24 MB budget's tightest part for a campaign map is the heap (≈4.7 MB), before the window. The memory plan's "rebalance" lever now has a first target: the heap's own large users (the 1 MB sound cache, the 1 MB geometry cache, the 192 KB index buffer, ai_debug's 114 KB + 238 KB static, the 384 KB block) against what a campaign map needs for level copies. b93 adds `psp_heap_report` (who asked for the heap, at the first failure) for this.

## b94: The Library runs (emulator) [M]

Two changes freed the heap:
- **The debug-drawing vertex buffer, 384 KB → 16 KB** (`RASTERIZER_MAXIMUM_DYNAMIC_DEBUG_VERTICES` 24,576 → 1,024; a request past it is refused and warned once). Nothing in a PSP build draws debug geometry.
- **The geometry cache's 1 MB comes from the texture RAM cache when the map's texture pack is larger than the texture caches** (every campaign map: c20's is 14 MB against 5.3 MB, so its textures stream as drawn anyway). Blood Gulch's pack fits, and keeps the heap's arena (b73's lesson). Keep word `geoheap` forces the heap.

Result, emulator, PSP-1000 layout: The Library loads, plays its opening cinematic and runs 3+ minutes with **0 heap failures**; heap peak 4,200 of 4,736 KB; level copies at their 1 MB budget (trimming as designed); geometry cache 991 of 1,024 KB (89 evictions in 1,200 frames); display list peak 340 of 384 KB (the tightest margin now); tick 8.4–9.6 ms in the emulator (hardware ≈4× → the tick, not the frame, is the next wall). Blood Gulch's heap peak fell to 4,072 KB.

Download: the map's texture pixels zeroed (`tools/zerobitmaps.py`, 43 MB of 121), the four files zipped to 67 MB, sent in three 29 MiB parts.

## b95: high-water marks (emulator first look, The Library's opening) [M emulator]

New log lines every 5 s (stacks every 30 s), from the game thread:
- `sound cache:` occupied, the working set (pages used in the last second, peak), playing, loads, failures. The Library's cinematic and corridor: **working set peak 532 KB of 1,024**, playing up to 496 KB, 168 loads (10 MB, largest 66 KB), 0 failures. So the 1 MB cache is about 2× the opening's need; fights and dialogue may raise it.
- `dynamic geometry, frame peaks:` the D3D layer's groups. **Unlit (particles) 15 of 96 KB, model 0 of 64 KB, debug 0 of 16 KB, triangles 54 of 192 KB**, buffer slots 24 and 13 of 1,024. The model group never used; the rest far under their sizes.
- `stacks (KB used deepest/size):` **the main thread 166 of 512 KB**; every other thread ≤ 4 KB used of 16–64; 792 KB reserved, 180 KB ever used. (The scan skips the kernel's 16-byte guard at the stack's bottom.)
- Display list, game-state pools and the heap were already logged.

Hardware numbers (b95 on The Library) decide the sizes; fights will push every one of these.

## b106: The Silent Cartographer fits (emulator) [E]

The two levers that close b30's gap, both in the bake (invader-build `-M` and `-Y 512`, `bake.py`'s flags since b106):
- **Animation frame data streamed.** 2,494 KB of b30's frame data moved from the tags to the front of its `.pgeo` pack, on 8 KB pages (3,202 KB with the padding). The game reads it through a 512 KB cache (`psp_anim.c`): 368 KB of it in the window's own unused end (above the tags, below where the largest BSP goes), 144 KB from the texture RAM cache. A 9-minute trace of b30's reads, replayed by `tools/anim_cache_sim.py`, gave about 214 misses and 1.8 MB read at that size.
- **The script node table at the size the map uses**: 4,534 nodes in use plus 512 spare, 5,046 instead of 19,001 (0.28 MB saved; the estimate above held).

Result: b30's tags are **5.04 MiB** (5,282,112 bytes), and the map loads and plays on the PSP-1000 layout (13 descriptors): the Pelican drop, 35 actors, the beach fight. The geometry cache (2 MB) and the animation cache both come from the texture RAM cache, whose textures stream anyway. Heap in play: 4,421–4,467 KB in use of 5,376 [E].

**All the campaign maps, built with b106's bake flags [M, the bake's build step alone, 2026-10-08]** (`invader-build` with `-M -Y 512` and the rest of `bake.py`'s flags; "Tag space" is tags plus the largest BSP against the 9.00 MiB window):

| Map | Tags | Tag space | Script nodes in use | Frame data moved out | Fits? |
|---|---:|---:|---:|---:|---|
| a10 Pillar of Autumn | 5.14 MiB | 8.93 (99.2%) | 17,391 | 2.42 MiB | **yes**, 72 KB spare |
| a30 Halo | | 9.57 | 11,933 | | no, 0.57 MiB over |
| a50 Truth and Reconciliation | | 9.20 | 14,875 | | no, 0.20 MiB over |
| b30 Silent Cartographer | 5.04 MiB | 8.64 (96.0%) | 4,534 | 2.44 MiB | **yes** |
| b40 Assault on the Control Room | | 9.53 | 10,360 | | no, 0.53 MiB over |
| c10 343 Guilty Spark | 5.58 MiB | 8.92 (99.1%) | 9,144 | 2.73 MiB | **yes**, 80 KB spare |
| c20 The Library | | (fit before; not rebuilt) | | | **yes** |
| c40 Two Betrayals | 5.40 MiB | 8.75 (97.3%) | 7,817 | 2.85 MiB | **yes** |
| d20 Keyes | 4.57 MiB | 8.71 (96.8%) | 7,612 | 2.55 MiB | **yes** |
| d40 The Maw | 4.84 MiB | 8.44 (93.8%) | 13,222 | 2.84 MiB | **yes** |

**Seven of the ten fit the PSP-1000's window now.** The script tables vary far more than b30's suggested (4,534 to 17,391 nodes in use), which is why the on-paper estimate missed in both directions. a50 (0.20 MiB over), b40 (0.53) and a30 (0.57; its BSP is the largest, 4.66 MB) are within the BSP formats' estimated 0.4–0.7 MB, a30 and b40 only at its top end. a10 and c10 fit with under 100 KB spare, so anything added to their tags needs checking against it. c20 rebaked this way would gain about 1.4 MB of margin. Only the build step was run: the maps still need their full bakes (textures) and a play in the emulator.

## 2026-10-11: all ten fit (the campaign bake, b115) [D, E]

The lab's full campaign bake (`campaign`, 2026-10-09: textures and all) came out smaller than the b106 build-step table above, and all ten maps now load and play their openings on b115 in the emulator's PSP-1000 layout (batch `campaign-b115`: no input, 420 s each, no fault).
- **The window's spare** at the largest BSP is what the animation cache finds unused between the tags and the largest BSP (the pack's header says where it starts) [D]. The cache takes up to 512 KB there, so 512 means "512 or more":

  | Map | Tags (bytes) | Spare in the window | Heap peak (of 5,888 KB) |
  |---|---:|---:|---:|
  | a10 Pillar of Autumn | 4,957,524 | 488 KB | **5,764** |
  | a30 Halo | 4,917,552 | 504 KB | 5,352 |
  | a50 Truth and Reconciliation | 5,490,736 | 296 KB | 5,124 |
  | b30 Silent Cartographer | 4,811,552 | ≥512 KB | 5,216 |
  | b40 Assault on the Control Room | 6,086,192 | **64 KB** | 5,124 |
  | c10 343 Guilty Spark | 5,404,928 | ≥512 KB | 5,192 |
  | c20 The Library | 3,070,164 | ≥512 KB | 5,300 |
  | c40 Two Betrayals | 5,324,164 | ≥512 KB | 5,192 |
  | d20 Keyes | 4,527,960 | ≥512 KB | 5,188 |
  | d40 The Maw | 4,795,104 | ≥512 KB | 5,124 |

- **The tight ones:** b40's window (64 KB spare) and a10's heap (124 KB left at its peak). b115's program sits 0.7 KB under a 64 KB heap step (`build_b115.md`), so code added without a trim would leave a10 60 KB.
- **Openings only:** later BSPs fit the window by construction (the spare is measured against the largest), but the heap and pools in the late fights are unmeasured.

