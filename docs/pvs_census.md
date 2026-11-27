# The PVS census: does a potentially visible set cut the visibility walk?

_The lab's brief draw-pvs (2026-11-27 02:3x). Tasks 0-2 on one page: the code read, the bake census, the cost sheet. Nothing is built until the cost sheet clears ~0.5 ms a frame._

Code read at the engine pin (ChupathingyCE 2e5eac00, `upstream/pins.env`) and this repository's main at 293bc4c5. Line numbers below are at those commits.

## Task 0: the walk against the map's PVS [D]

### (a) The walk already skips every cluster outside the camera cluster's PVS

`structure_visibility_traverse_cluster` (`source/structures/structure_visibility.c:1666-1841`) loads the camera cluster's PVS once a call (`:1676-1678`, `structure_bsp_get_cluster_pvs(structure, render.cluster_index)`). For each portal out of the cluster, it enters the neighbour only if four tests pass (`:1782-1787`): the index is in range, the neighbour isn't on the current path, and `BIT_VECTOR_TEST_FLAG(cluster_pvs, neighbor_cluster_index)` is set. The PVS bit is the last of the four, and all four come **before** the portal's hull (`portal_hull_from_portal`, `:1790`), its distance test (`:1803`) and the clip (`convex_hull2d_intersect`, `:1810`).

So a portal into a cluster outside the camera's PVS costs its loads and four compares, and nothing more. The step counter the port adds (`psp_fixups.py:6629`, one step for each call of the function) never counts such a cluster, because the walk never enters it.

The ME runs the same test. Its walk (`psp_mv_traverse_cluster_body`) is a text copy of this function made at build time (`psp_fixups.py:54156-54211`, `mevis_copies`): it renames functions and swaps state for `psp_mv->` fields, and keeps the body as it is. The engine's own fallback (`structures_use_pvs_for_vs`, `:1947-1954`) and the port's 160-step fallback (`psp_fixups.py:6654-6655`) both use the same PVS as the cluster list.

**So the brief's branch "if (a) is yes" holds: the map's PVS prunes nothing the walk doesn't already prune.** A PVS lever can only be a finer bake, which drops portals or clusters the map's PVS keeps but the walk never reaches visibly from a given camera cluster. Task 1 sizes that: the reach set per camera cluster against its PVS. The census count "steps that enter a cluster outside the camera cluster's PVS" is 0 by construction and needs no counter.

What the walk spends its steps on instead: it walks every simple path through the portal graph. A cluster is unmarked when the walk leaves it (`:1835-1838`), so it is entered again by every other path whose clipped hull stays non-empty. That's why the port caps it at 160 steps (`psp_fixups.py:6611-6655`, build 31's note: "thousands of steps a frame" outdoors). A bake that cuts the walk's own work would have to cut those repeat paths. One example is a per-portal-pair reach bit: "from this portal, can that one ever be seen?" [U, an idea to size in task 1, not a design].

### (b) What the tag's PVS holds

- **Layout:** `structure_bsp_get_cluster_pvs` (`source/structures/structure_bsp_definitions.c:45-68`). The BSP's `cluster_data` holds one bit vector per cluster, `BIT_VECTOR_SIZE_IN_LONGS(clusters.count)` 32-bit words each, stored cluster after cluster (`:64-67`). Bit *j* of cluster *i*'s row says whether cluster *j* is potentially visible from cluster *i*. A map passes the port's check only if the data holds all rows (`:41`, `clusters × ceil(clusters/32) × 4` bytes) and has at most 512 clusters (`:38`). A row that is out of range reads as empty, so no cluster is visible from it (`:20-22`, `:54-61`).
- **Conservative?** The engine trusts it that way. The walk never enters a cluster outside it (`:1787`). The AI (`ai.c:2329`, `ai_communication.c:2906`, through `scenario_test_pvs`), sound (`game_sound.c:1007`) and objects (`objects.c:873-886`) all read it as "cannot be seen from here". Whether the compiled data ever leaves out a visible cluster can't be read from the engine [U]. The census can test it: a cluster the walk renders from a camera cluster always has its bit set, because `:1787` admits no other, so a breach would show only as a hole in the picture, not in a count.
- **How it was compiled:** not in the engine. It's map data: the retail BSP's `cluster_data` block as the map ships it. The compiler that wrote it isn't in this repository's pins (Invader is pinned at d43ce550; whether anything in the build rebuilds `cluster_data` or copies it through isn't read yet) [U]. To be read before task 2, because a finer bake would be written by the same tool.

### (c) What one step costs in the code

A step is one call of the walk (`psp_fixups.py:6629`), so one cluster entered. Its work splits into a per-portal test that every portal out of the cluster pays, and a hull-and-clip that only the portals passing the test pay. What follows is read from the source [D, source], then from the disassembly of the ME's copy in the ship build (below).

**Every portal out of the cluster** (`structure_visibility.c:1771-1787`): the portal's index and its range check, the portal's two cluster indices (the direction), the neighbour's range check, a word of the walk's visited bits, and a word of the camera cluster's PVS row. That is about six loads and five compares and branches, with no floating point. This is also the shape, and roughly the price, of any per-portal bit test a finer bake would add: one more word load, a shift, an and and a branch, plus whatever its row costs in the D-cache (the disassembly's figures are below).

**A portal that passes the test** (`:1790-1831`), in order:
1. **Its hull**, `portal_hull_from_portal` through build 31's cache (`psp_fixups.py:6773-6821`): 128 slots keyed by portal and direction, valid for one walk (`psp_portal_walk_id`). On a hit it costs two loads and a compare, then a copy of up to 8 points (16 words, `csmemcpy`, `:6804`). On a miss it builds the hull (`structure_visibility.c:924-1039`): the plane's range check and its distance to the camera (`:957`), then for each vertex a 4x3 transform into view space (`:977-980`, 9 multiplies and 9 adds), a clip against the screen plane (`convex_polygon3d_clip_to_plane`, `:983`), and a divide by z for each output point (`:1019`). The cache stores it (`:6810-6818`). So the repeat paths of (a) mostly hit the cache, and a first visit pays the build.
2. **Its distance test**, `points_within_distance` (`:866-894`), skipped when the sky is visible (`render.visible_sky_model`, `:1802`). For each vertex until one is within the far plane: a subtract and a dot product against the camera's forward axis.
3. **VIS's rect test**, `psp_visrect_apart` (`psp_fixups.py:27472-27507`, in every build: FIXES_SMALLWINS2 is in the unconditional list at `:54099`, and its mode is "skip" unless a keep word says otherwise, `:27477-27479`). It takes the min and max of both hulls' points (two compares for each coordinate of each point), and four compares with a margin. When the boxes are apart, the clip is skipped and the step ends there.
4. **The clip**, `convex_hull2d_intersect` (`geometry.c:1069-1115`). For each edge of the visible region (its `p_count`, 4 at the screen and more after clips), a line through two points (`plane2d_from_points`, a normalise), then `convex_polygon2d_clip_to_plane` (`:1117`) over the portal hull's points: a plane distance a point (2 multiplies, 2 adds), and an interpolation with a divide at each crossing. So the clip costs about `p_count × q_count` plane distances plus one normalise an edge. That makes it the step's largest floating-point block, and it grows as the visible region gains vertices.
5. If the clipped hull isn't empty, the next step (`:1820`).

**What this says for the cut order (brief task 2) [D, source; cycles U]:** the test a finer bake would add is integer work with one load. The work it would skip is a hull (a cache hit at least) plus the rect test, and on most portals a clip of about `p_count × q_count` FPU distances with a normalise an edge. The ≥10x margin the brief asks for is plausible from the source. It is shown only when the disassembly gives both sides' cycles.

**How the disassembly was got.** The disassembly stays on the build runner (`.github/workflows/sierra-build.yml` keeps only reports). So `tools/mewalk.py` gained `--dis=` (this branch, 70bedf3e), which prints named functions whole into the build's `reach.txt`. The lab's sierra-build 45970548101 built `builder/pvsdis` 00de9380 (slice 1's head dbc76ba0 plus that tools commit) with `reach=psp_mv_traverse_cluster_body --dis=psp_mv_,portal_hull,points_within,convex_hull2d_intersect,convex_polygon2d,psp_visrect`. Its EBOOT is **b739ef687f633bf9**, the ship build's (summary.txt), so what follows is the code the ME runs today.

### (c) From the disassembly [D, build pvsdis, EBOOT b739ef687f633bf9]

`portal_hull_from_portal`, `points_within_distance` and `psp_visrect_apart` have no symbols of their own: LTO inlined all three into `psp_mv_traverse_cluster_body` (0x16340, 3492 bytes, 873 instructions; frame 5240 bytes). The clip stays out of line: `convex_hull2d_intersect.constprop.0` (0x10c200, 420 bytes, frame 8264 bytes, two 4 KB point buffers it swaps between) calls `convex_polygon2d_clip_to_plane` (0x12e80, 1484 bytes, frame 4160 bytes) once an edge. Addresses below are in the body unless named. Counts are for the path described. "Points" means a 4-point visible region and a 4-point portal hull, the screen's region and a quad portal.

| Block | Where | Instructions | Loads | FPU |
|---|---|---|---|---|
| A step's own work, before its portals | 0x16340-0x164f0, 0x16980-0x169e8 | ~150, plus ~14 a region point (0x164f4-0x16594, the cluster's screen bound) | ~35, 20 of them the saved registers | none outside the bound's compares |
| The per-portal test, a portal turned away at the PVS bit (the last test) | 0x165e8-0x165fc, 0x16600-0x166b0 | **51** | 13 | none; 11 conditional branches |
| A finer bake's bit, if added beside it (the PVS test's own shape, 0x16678-0x166b0) | [U, modelled] | ~7 | 1-2 | none |
| The hull, cache hit (key `(2 × portal + side) & 127`, 76-byte slots) | 0x166b4-0x16728, 0x16de8-0x16e10 | ~40, plus `csmemcpy` of 8 bytes a 2D point | 6 | none |
| The hull, cache miss (the build) | 0x169f4-0x16d8c | ~370 for a quad: ~106 head (two `global_structure_bsp_get` calls, the tag-block checks, the plane's distance), 34 a vertex for the 4x3 transform, 19 an output point for the divide by z, ~25 for the slot store | the portal record (64 bytes), its plane (16), its vertices (12 each), the 13 view-matrix floats | 9-12 `mul.s` and 9 `add.s` a vertex, 1 `div.s` an output point; plus `convex_polygon3d_clip_to_plane.constprop.0` (0x4b640, not in this disassembly: its name missed the `--dis` prefixes) [U] |
| The distance test (only when the sky isn't visible, byte 12649) | 0x16ee8-0x16f5c | 19 a vertex, until one is within | 9 a vertex: its 3 floats, and 6 camera floats reloaded every vertex | 3 `sub.s`, 3 `mul.s`, 2 `add.s`, 1 compare a vertex, one dependent chain |
| VIS's rect test | 0x16740-0x168f4 | ~200: 26 a point after each hull's first, then 20 for the margin compares | 2 a point | 4 `c.lt.s` a point, each with a `nop` before its branch; 4 adds with the margin; no multiply |
| The clip, `convex_hull2d_intersect` | 0x10c25c-0x10c2e8 an edge | ~34 an edge, plus the call | 4 an edge | 2 `sub.s`, 6 `mul.s`, 2 `add.s`, 1 `sqrt.s`, 1 `div.s` an edge (the normalise) |
| ...its `convex_polygon2d_clip_to_plane`, per edge | 0x12e80-0x13490 | ~26 prologue and ~15 epilogue; ~25 a hull point (its distance and side); ~45 more a crossing (a `div.s`, a clamp to [0, 1], the new point, two near-duplicate checks) | 2-4 a point | 2 `mul.s`, 1 `add.s`, 1 `sub.s` a point; 1 `div.s`, 2 `mul.s`, 2 `add.s` a crossing |

So for 4 points each and 2 crossings an edge, the clip is about 4 × (34 + 26 + 4 × 25 + 2 × 45 + 15) ≈ **1,060 instructions, 4 `sqrt.s` and 12 `div.s`**. It grows with the region: `p_count` edges times `q_count` points. The cache keeps hulls of at most 8 points (0x16d2c, `slti 9`). A larger hull is built again on every visit.

What the code shows beyond the source read:
- **The walk's depth is capped at 14** (0x16920, `slti v0,v0,14` on the depth halfword at 10780; a deeper portal is skipped and counted at 10764). The 160-step cap is at 0x163ac (`slti v1,v0,161`). At 5240 bytes a frame, 14 levels hold ~73 KB of the ME's stack, plus the clip's 12 KB at the deepest [D on code].
- **The hull cache's slot state:** 1 = empty (wrong side of the plane, under 3 points, or clipped away; the portal is skipped), 2 = the camera is within epsilon of the portal's plane (0x16f60: the next cluster is entered with the region unchanged, by `psp_mv_structure_visibility_traverse_cluster`, 0x16ed8, with no clip), 0 = a hull is there.
- **The distance test reads the portal's world-space vertices even on a hull-cache hit** (0x16eec, `lw a0,56(s8)`, the portal's vertex pointer). So when the sky isn't visible, the cache spares the transform and the divides but not those vertex lines [D on code; whether they miss, U].
- **8 of the per-portal test's 13 loads are reloads of values that don't change in the loop:** the cluster's portal count and list (92 and 96 of the cluster record), the BSP's portal block (340, 344), its cluster count (308), the walk's state pointer through gp, its visited-bits pointer, and the PVS row pointer from the stack. They're reloaded because the loop holds a call (the recursion). Only 5 loads are the portal's own: its index in the cluster's list, its two cluster halfwords in a 64-byte portal record, the visited word and the PVS word.

**Cycles [U on the latencies, D on the counts].** The Allegrex issues one instruction a cycle, in order, and doesn't overlap misses (`docs/cpu_cost_map.md`, halo_bench v7 and v9 [M]). A D-cache miss costs ~73 cycles for a random line [M, same]. The lab holds no measured latency for `div.s` or `sqrt.s`, or for an FPU result used by the next instruction [U]. So the cycles below assume every load hits and count one cycle an instruction. They're a floor: the divides, square roots and FPU chains only add to them.

| A portal out of the cluster that... | Instructions (floor cycles) | Plus |
|---|---|---|
| is turned away at the PVS bit | 51 | the misses on its list entry and record |
| passes, hits the hull cache, and the rect test rejects it (sky visible) | ~51 + 40 + ~20 copy + 200 ≈ **310** | (sky not visible) the distance test, 19+ and its vertex lines |
| passes, hits the cache, and is clipped | ≈ 310 + 1,060 ≈ **1,370** | 4 `sqrt.s`, 12 `div.s`; and, if the clip isn't empty, the next step |
| misses the cache | + ~370 and the 3D clip | 4+ `div.s`, the record, plane and vertex lines |

**The cut-order check (brief task 2), answered from the code:** a finer bake's bit costs ~7 instructions and 1-2 loads, ~80 cycles even if its word misses every time. A step it skips costs at least the rect test and the clip (~1,370 instructions with 16 divides and square roots) plus the step's own ~150 and its portals. That's ~1,520 against ~80, ≥~19x, if the bit's word misses every time, and ~200x if it hits. So **≥10x holds against a skipped step** [D on counts; U on latencies, which only widen it]. Against a portal the rect test would have rejected anyway, the bit saves only ~310 - 51 ≈ 260 instructions, ~37x its own 7. A bit paid by every portal therefore pays only if it drops enough of them. With every tested portal paying ~7, it breaks even at ~3% of them dropped, if each drop saves only a rect test, or ~0.5% if each saves a clip; ≥10x needs ~10x those shares [U, a model]. If the finer bit implies the PVS bit, it replaces the PVS test instead of adding to it, and costs nothing extra a portal [U]. That's for task 2's design, not this read.

**For task 1's census:** the sheet needs to know where each step's portals end (at the PVS bit, the cache, the distance test, the rect test or the clip). The counts above price each ending. The cycles a step stay open until task 2's PSP figure (`mevisring` over the steps a frame). These counts are its fallback only, at half, never as the margin (brief task 2).

## Task 1: the bake census [E]

### The counter (builder/pvscensus 5106e6a3, 2026-11-27 05:2x)

It's cut from the slice 1 tock dbc76ba0 (off EBOOT b739ef687f633bf9), not from this branch: the census counts the tock's walk. `PSP_PVSCENSUS=1` builds it in. It's log only, emulator only, and its commit message is its review paragraph.

- **The walk it counts is the main CPU's.** Every census run carries the keep word `mevislandoff`, on and off alike, so the main CPU makes every frame's visibility. The ME's copy is the same text with a depth cap of 14 (above); when it stops there, mevisland makes that frame on the main CPU, so the drawn answer is always the uncapped walk's.
- **Hooks:** `PSP_PVSC(call)` lines in `structure_visibility_traverse_cluster` (6) and `structure_visibility_compute` (1). `mevis_copies` turns them into nothing in the ME's copies, and the counts live in `structure_bsp_definitions.c`. Without the switch, the fixed tree is the tock's byte for byte (a host form check, slice 1's on form).
- **Where each tested portal ends, six ways** (the lead 04:5x):
  1. the four-way test (split into range, on the path, not in the PVS);
  2. the hull discarded;
  3. beyond the far plane;
  4. VIS's rect test;
  5. the clip empty;
  6. a step entered (the walk's calls less one root a walk).

  The exits are counted in the loop and the steps at the callee, so the six summing to the portals tested checks one set of hooks against the other.
- **Also counted:**
  - steps (the same count as `psp_portal_steps`) and steps past the 160 budget;
  - re-entries of a cluster already rendered on the walk, split by whether its rect grew. "Rect inside" means the cluster's clip rect is unchanged. That doesn't prove the re-entry's sub-walk adds nothing, since its region's polygon differs [U as a cut];
  - per walk in budget, the clusters rendered and those outside the camera cluster's PVS (expected 0, from (a));
  - per camera cluster, its reach set against its PVS. The reach set is the union over the route's own walks: a ceiling for a finer bake, never a conservative set.
- **Placed growth** (`w_tgme2m1mp_tgc90`): `structure_visibility_traverse_cluster` (layout line 854) and `structure_visibility_compute` (388). The ME's copy doesn't grow. Only emulator times move, and the census reads counts.
- **The ask:** pvscensus-emu (the lab's briefs/asks/pvscensus-emu.toml): b30_leg2 and c20_leg2 to 120 s, d20_play1 to 100 s, off against on, plus an off-against-off control on b30.
