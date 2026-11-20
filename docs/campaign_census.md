# Campaign map census (all ten maps) — measured 2026-10-02 and 2026-10-04

Source: Sheepdog01's Xbox retail maps (xiso dump), zlib-decompressed, measured with `census.py`, `deep.py`, `anim_keyframe.py` (projects/sierra/research/baker). M = measured, E = estimate.

## Decision (Sheepdog01, 2026-10-04): the PSP-1000 is non-negotiable

The campaign targets the PSP-1000's 32 MB, so a 64 MB requirement is off the table. Deep restructuring and streaming are the path:
- the tag window holds gameplay data only;
- render data (model and BSP geometry, textures, sounds) streams through LRU caches;
- plus the levers below.

## Raw sizes (M), all ten maps

| Map | Level | Tag data | BSPs | Largest BSP | Animation frame data → keyframed at 0.25° |
|---|---|---:|---:|---:|---:|
| a10 | Pillar of Autumn | 14.16 MB | 9 | 8.57 MB | 3.81 → 2.36 |
| a30 | Halo | 15.35 | 2 | 6.84 | 3.74 → 2.11 |
| a50 | Truth and Reconciliation | 15.67 | 4 | 6.83 | 4.56 → 2.43 |
| b30 | Silent Cartographer | 15.72 | 2 | 6.61 | 4.24 → 2.27 |
| b40 | Assault on the Control Room | 17.00 | 13 | 5.84 | 4.37 → 2.51 |
| c10 | 343 Guilty Spark | 16.61 | 6 | 5.71 | 4.97 → 2.59 |
| **c20** | **The Library** | **8.95** | 4 | 8.62 | 2.55 → 1.28 |
| c40 | Two Betrayals | 16.54 | 13 | 6.03 | 4.70 → 2.70 |
| d20 | Keyes | 14.52 | 5 | 8.12 | 4.49 → 2.42 |
| d40 | The Maw | 15.02 | 10 | 7.54 | 4.63 → 2.58 |
| bloodgulch (ref) | | 7.31 | 1 | 1.29 | |

Tag classes: models (`mode`) 32–39%, animations (`antr`) 30–35%, sound tag data 3–10%, scenario 0.66–1.48 MB, collision 5–6%.

## The Library (c20) is the first campaign candidate [M for the sizes, E for the levers]

Its tag data is 8.95 MB, half of any other map's: no vehicles, no marines, one enemy family (the Flood) and the Monitor. What must stay resident on the PSP-1000:

| | c20 | Note |
|---|---:|---|
| Tag data as is | 8.95 MB | |
| − model geometry out of the window | −2.83 | streamed from the geometry cache |
| − animations keyframed at 0.25° | −1.28 | 2.55 → 1.28 MB (M) |
| Resident tags (E) | **≈ 4.8 MB** | |
| Largest BSP's collision, repacked 16-bit | 2.0 MB | 3.27 → 2.02 MB (M). **Every c20 BSP's counts fit 16 bits** (bsp3d nodes 27,500 and 32,454; references 39,716 / 45,689 < 65,535), the first campaign BSPs that do |
| BSP tree, clusters, pathfinding (E) | ≈ 1 MB | |
| **Resident total (E)** | **≈ 7.8 MB of 9.0** | fits, with about 1 MB over |

Compare a30 (Halo): 15.35 − 5.87 − 1.63 ≈ 7.9 MB of tags alone before any BSP, so over.

**Emulator recon (build 66, 64 MB variant, 2026-10-04) [M]:** The Library loads and plays: the opening cinematic (343 Guilty Spark), then the dark corridors. Tick 9.0–9.2 ms in the emulator with 30 bipeds (the Flood) updating a tick and 53 machines (doors); frame 30 ms, 18 fps, of which the renderer's CPU 15.6 ms (51 level draws, see-through level 2 ms, lights 1 ms). Scaled by the 4× hardware/emulator tick ratio seen on b30, **the tick would be ~36 ms on the PSP-1000**: the same wall as b30's beach, the biped physics and AI perception walking the collision BSP. The level itself is dark, so its visuals forgive more than Blood Gulch's.

**What it decides:** memory for one campaign map is reachable with the planned bake (geometry out, keyframes, 16-bit collision), without inventing new levers. The tick cost is the gate, and it is the same gate for every campaign map: the collision walk.

## Bake levers (M unless noted)

| | a10 | b30 | b40 |
|---|---:|---:|---:|
| Model geometry (all LODs) | 4.52 MB | 5.43 MB | 5.19 MB |
| …only reachable by high/super-high LODs | 1.76 | 2.12 | 2.49 |
| Model geometry baked for the GE (E, Blood Gulch ratio 0.49) | ≈2.2 | ≈2.7 | ≈2.5 |
| Animation frame data | 3.81 MB | 4.24 MB | 4.37 MB |
| …keyframed at 0.02° / 0.3 mm | 3.03 | 3.16 | 3.35 |
| …keyframed at 0.25° / 3 mm | 2.36 | 2.27 | 2.51 |
| Object collision 16-bit repack | 0.60 → 0.38 | 0.68 → 0.42 | 0.95 → 0.59 |
| Script nodes in use (of 19,001) | 17,516 | 4,557 | 10,436 |

Animations: a10 and b40 contain some animations Bungie already keyframe-compressed (`anim_keyframe.py` counts those as they are).

## BSPs (M)

Largest BSP after moving render vertices out and repacking collision where counts allow:

| Map | Largest BSP | Render vertices | Collision | Resident (collision, tree, clusters, pathfinding) |
|---|---:|---:|---:|---:|
| a10 (bsp 5/6) | 8.57 MB | 3.88 | 3.09 | 4.74 MB |
| b30 (bsp 0) | 6.61 MB | 2.02 | 2.86 | 4.59 MB |
| b40 (bsp 1) | 5.84 MB | 1.86 | 2.51 | 3.97 MB |

**The large campaign BSPs of a10, b30 and b40 have collision counts above 32,767** (b30 bsp 0: 41,948 bsp3d nodes), so the 16-bit repack can't apply to them as one format; c20's can.

## Budget against the PSP-1000 memory map (a10, b30, b40)

| Map | Resident | vs 9 MB window |
|---|---:|---:|
| a10 | 12.7 MB (13.4 at 0.02°) | over by 3.7 |
| b30 | 12.4 MB (13.3) | over by 3.4 |
| b40 | 13.4 MB (14.2) | over by 4.4 |

Render data then needs RAM of its own on top. Further levers: streaming cinematic-only animations, sound tag metadata (≈1.4 MB), effect struct padding, mixed-width collision, a smaller code/heap footprint.

## The multiplayer maps (M, 2026-10-04)

All twelve others fit the 9 MB window as they are: tag data 6.87–7.19 MB, BSP 0.37–1.94 MB (Sidewinder the largest at 8.92 MB together). All twelve were rebuilt for the PSP tag address, baked, and load in the emulator (build 66): Beaver Creek, Boarding Action, Chiron TL-34, Chill Out, Damnation, Hang 'Em High, Longest, Prisoner, Rat Race, Sidewinder, Wizard, plus Blood Gulch.

## Measured with the build-68 bake (2026-10-04): -G geometry out, -K keyframed animations

`invader-build … -G <map>.pgeo -K 0.25,0.003`. Tag space as Invader reports it (tags + largest BSP), the figure that must fit the 9 MB window. [M]

| Map | Before | Geometry out (-G) | + animations keyframed (-K) | Gap to 9 MB |
|---|---:|---:|---:|---:|
| Beaver Creek (MP) | 7.49 | 4.68 | 3.92 | fits |
| c20 The Library | 17.1 | 12.18 (tags 5.96, BSP c20_3 6.22) | **10.94** (tags 4.72) | 1.94 |
| b30 Silent Cartographer | 22.3 | — | **12.85** (tags 7.90, BSP b30a 4.95) | 3.85 |

Animations: c20 350 keyframed, 2.68 → 1.43 MB; b30 656, 4.40 → 2.52 MB; worst error 0.266° / 3 mm against the engine's own decode [M, kfcheck.py].

**What remains is the BSP.** c20_3 after -G (6.22 MB): collision 3.35 MB (edges 1.2, bsp3d nodes 0.4, bsp2d nodes 0.39, bsp2d references 0.38, vertices 0.37, surfaces 0.32, planes 0.18, leaves 0.14), the 12-byte position copy 1.06, leaf surface references 0.70, render surfaces 0.29, leaves 0.27, subcluster surface indices 0.21, nodes 0.20, materials 0.12.

Collision 16-bit is not a plain struct switch: c20_3 has 34,133 bsp3d nodes and 51k edges, past the 32,767 a signed short holds, and the code compares indices with NONE (−1). Children would need a 24-bit packing or unsigned indices with the NONE tests rewritten — accessor changes in the hottest tick code. The estimate of ≈1 MB for "tree, clusters, pathfinding" in the Library section above was low: measured, the non-collision, non-vertex remainder of c20_3 is ≈1.9 MB.

Cheaper BSP levers (few readers each): a 16-bit quantised position copy (−0.5 MB), leaf surface references at 16 bits (−0.35), subcluster surface indices at 16 bits (−0.1).

A larger idea for later: draw the level by cluster from the baked streams and let the GE cull, which makes the per-surface visibility structures (leaf surfaces, subcluster surface indices, the per-frame surface flags and index assembly) unnecessary: ≈1 MB of window and the per-surface CPU work, at the price of more triangles to the GE. See geometry_streaming.
