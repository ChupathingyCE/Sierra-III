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
- **How it was compiled:** not in the engine. It's map data: the retail BSP's `cluster_data` block as the map ships it. The compiler that wrote it isn't in this repository's pins.
- **The build copies it through, byte for byte [D, read 2026-11-27 05:4x from Invader d43ce550, `upstream/pins.env:8`].** The bake (`bake/bake.py:540-575`) runs `invader-extract` on the retail map, then `invader-build` on the extracted tags. In Invader's BSP definition, `cluster data` is a `TagDataOffset` (`src/tag/hek/definition/scenario_structure_bsp.json:1508-1511`). The generated code treats every `TagDataOffset` as opaque bytes. Extraction copies them out of the cache file (`src/tag/code_generator/read_cache_file_data.py:70`). The build copies them into a struct of their own, with a pointer and size and nothing else (`compile.py:140-150`). No range check runs on them (`check_invalid_ranges.py:13-14`, `pass`). No hand-written code names the field: `cluster_data` isn't in Invader's `src/`, the BSP's `pre_compile`/`post_compile` (`src/tag/parser/compile/scenario_structure_bsp.cpp:14`, `:191`) only touch clusters to fill their predicted resources (`:320-404`), and "PVS" appears nowhere in the repository. The port's own steps don't touch it either. `upstream/invader/psp-fork.patch` only touches clusters to pack their surface indices (`:1563-1580`). `tools/levelslim.py` never mentions clusters. `cluster_data` appears outside the docs only as `port_cluster_data_references` in `psp_fixups.py`, the objects' cluster references, which is a different table.
- **So a finer bake needs a writer of its own.** No tool in the pins computes a PVS. A finer one would be a new bake step, either one that rewrites the extracted BSP tag's `cluster data` between `invader-extract` and `invader-build` (`bake.py:540`, `:574`), or one that patches the built map. Either way the port's check (`structure_bsp_definitions.c:38`, `:41`) still holds it to ≤512 clusters and full rows. The tag definition's `"maximum": 65536` isn't enforced by Invader, and full rows at 512 clusters come to 512 × 16 × 4 = 32 KiB in any case. A bake that only changes the bits, with the same clusters, keeps the block's size as it is. A bake that adds clusters changes `clusters` too, so it's a geometry change and not only a PVS one.

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

### The census (the lab's sierra-batch 45990550618, read 2026-11-27 05:4x; rows pvscensus_b30, pvscensus_c20, pvscensus_d20) [E]

Every check passes:

- The off EBOOT is b739ef687f633bf9, and b30-off equals b30-off2.
- On plays exactly as off: play and both seeds are equal at every tick on all three maps.
- The six exits sum to the portals tested. Hulls worked out plus hulls reused equal the portals past the four-way test.
- The census's steps a walk, floored, equal the `portal walks` line's.
- Nothing is rendered outside the PVS.

Figures are over 1,800 walks on each map, seed 1:

| | b30_leg2 | c20_leg2 | d20_play1 |
|---|---|---|---|
| steps a walk | 54.0 | 5.36 | 9.57 |
| walks over the 160-step budget | 68 (3.8%) | 0 | 0 |
| rendered / in PVS / map clusters, a walk | 8.9 / 22.3 / 56 | 4.9 / 24.3 / 51 | 4.8 / 17.2 / 55+35 |
| portals tested a walk | 1,237 | 18.3 | 160 |
| exits: path / PVS / hull / rect / clip empty / step | 19.2 / 16.4 / 53.9 / 5.5 / 0.8 / 4.3% | 29.1 / 1.3 / 16.2 / 26.3 / 3.5 / 23.8% | 12.8 / 59.7 / 16.6 / 4.2 / 1.3 / 5.4% |
| re-entries of entered steps (rect inside) | 83.6% (38.6%) | 11.3% (0) | 55.8% (22.0%) |

Range and distance end 0 portals on every map.

- **The reach sets sit at or near the PVS where the steps are.** This is the reach set against the PVS, by camera cluster:
  - b30: 0.1 is 14/14 and 0.2 is 14/16; together they carry 53% of its steps.
  - d20: BSP 1's 1.32 is 10/10. BSP 1 holds all but 28 of d20's re-entries.

  The cameras that reach well under their PVS are c20's (12-15 of 22-27) and d20's BSP 0. Their walks are already the cheap ones.
- **So a finer bake has little room, and the walk's excess is its re-entries.** The walk searches every simple path, so on b30, 84% of entered steps re-enter a cluster already rendered.
- **Task 2 prices this [U until then].**

**Counter defect:** the summary line is cut at the log's line length after `map clusters N;`, so camera clusters and walks past the table never print. No figure is lost: the per-cluster lines' walks sum to 1,800 on every map, so 0 walks are past the table.

## Task 2: the cost sheet (written 2026-11-27 06:0x; the PSP price a step still open)

_Two cases in one sheet (the lead 2026-11-27 05:5x): (A) the bake as briefed, a stop; (B) the walk's re-entry cut, priced as a case and not built. Engine lines are at the pin 2e5eac00 as in task 0; census figures are task 1's [E], rows pvscensus_b30/_c20/_d20. "Floor" means task 0(c)'s instruction counts at one cycle each, every load a hit: a floor, never a price (brief: counted at half if no PSP figure, never the margin). The ME runs at 333 MHz (`docs/me_programme.md:11`), so 0.5 ms is 166,500 cycles._

### (A) A finer per-cluster PVS bake: a stop

- **It can never prune a step [D].** Every step renders its cluster (`structure_visibility.c:1734`), so that cluster is in its camera cluster's reach set, and any bake that keeps the picture keeps its bit. A finer bake can only drop portals that end before a step (hull discarded, rect test, clip empty) into clusters the camera never reaches.
- **Where the steps are, there is nothing to drop [E].** b30's camera clusters 0.1 (reach 14 of a PVS of 14) and 0.2 (14 of 16) carry 53% of its steps. d20's 1.32 is 10 of 10. The cameras well under their PVS are c20's (12-15 of 22-27, 18.3 portals a walk) and d20's BSP 0, the cheap walks.
- **Its ceiling is under the gate even if every non-step exit were droppable [D on counts, E on the mix].** A dropped portal saves its cost less the 51-instruction test it still pays. On b30, a walk's 667 hull-discarded exits (~40 saved each), 68 rect exits (~259) and 9.9 empty clips (~1,319) come to ≤57k instructions, ≤0.17 ms on the ME's floor. d20's come to ≤5.5k (≤0.02 ms), and c20's are smaller still. The true share is a fraction of that: on 0.1 it's 0, since its reach is its whole PVS.
- **To clear 0.5 ms** a stretch would need ≥~126 clipped portals (or ≥~643 rect-tested ones) a frame into clusters outside its camera's reach. b30 has 9.9 and 68 a walk of each kind in all.
- **Old layers: no.** The stop rests on reach = PVS where the steps are, which is a property of the map's own `cluster_data` (copied through byte for byte, task 0(b)) and of the engine's walk, which tests the PVS bit before any hull (`:1782-1787`). No port layer bounds it. VIS's rect test and the hull cache only make the exits a bake could drop cheaper.
- **PVS's `revive_if`** is filled in the lab's findings/levers/grounds.tsv from these figures.

### (B) The re-entry cut: skip a sub-walk that can add nothing

**Its ground** (the lab's `python3 -I toolbox/levercheck.py VIS PVS`, 2026-11-27 06:0x). The visibility ground holds no lever that cuts the walk's repeat paths. Its nearest try is VIS:

> VIS [open] (visibility) Portal screen-rectangle reject before convex_polygon2d_clip_to_plane / portal_hull_from_points
> status: built (small wins batch 2, Sierra-III research/smallwins2 d04eaf87; off words visrectold, smallwins2old; check visrectcheck): exact (play and pictures as the controls, check 0 of 78,872), merge asked
> worth: b30 route: 88 clips a frame skipped, draw lane -0.28 ms a frame (-0.75%) [E, cache model; controls agree to 0.01 ms]

VIS cuts a portal's clip. This cut would skip a whole sub-walk, its clip included. PVS's own line ("how much a PVS prunes on Halo's open maps is unknown") is answered by (A).

**Exactness [D, from the code], for the unchanged-bounds share only:**
1. **What a walk makes.** It makes `render.visible_cluster_flags` (`:1734`), the rendered clusters' order (`:1719`, set at a cluster's first entry) and each one's `clip_bounds`, grown by every entry's region (`:1735-1737`; `grow_clipping_rectangle_by_portal_hull`, `:784`, a min/max union). The visited bits are path state, cleared on leaving (`:1835-1838`). Debug hulls are drawn only under `debug_portals` (`:1742`).
2. **The census's "rect inside" share is not exactly cuttable on its own.** A re-entry whose region leaves its cluster's `clip_bounds` unchanged adds nothing to that cluster. Its sub-walk still runs every portal (`:1758-1833`) with a different polygon and a different path, so it can reach clusters or grow bounds the first entry didn't. So the rect test alone isn't a cut. Its share is the ceiling of the exact cut's: b30 38.6% of entered steps (~20.8 a walk), d20 22.0% (~2.1), **c20 0**.
3. **An exact test: the new region inside one of the cluster's earlier regions on this walk** (R' ⊆ R_old). A child's region is its parent's clipped by a portal (`:1810-1817`), or the parent's unchanged (`:1795-1799`). So regions only shrink along a path. The earlier entry has finished before the new one starts, since a cluster on the path is never re-entered (`:1786`). Take a path out of the new entry. If it avoids the earlier entry's path, the earlier sub-walk took it with larger regions. If it meets that path first at a cluster Y, Y was entered there with a region ⊇ R_old ⊇ the new one, and the same argument repeats from Y's entry on a shorter path. So every (cluster, region) the new sub-walk would make is covered by one already made, and the flags, the order and the bounds stay as they are. That holds in exact arithmetic. In floats, `convex_hull2d_intersect` (epsilon 0.0001) is monotone only up to rounding [U]. The build's check would be the `visdump` line equal every frame.
4. **Two caps change what exact means.** The 160-step budget counts steps at entry and makes an over-budget walk fall back to the PVS (`psp_fixups.py:6629`, `:6654-6655` at 293bc4c5). A cut that removes steps lets some of b30's 68 over-budget walks (of 1,800) finish, and they then draw the walk's clusters instead of the PVS. So the cut is **not exact against today on those walks**, only against the uncapped walk. That's a plays-the-same class, for the lead first. The ME's copy also caps depth at 14 (task 0(c)). An earlier entry cut short there covers nothing, so on the ME the test must refuse to use it [U, a design rule].

**Its pre-clip test against task 0(c)'s ~310 / ~1,370 [D on counts, U modelled]:**
- **Rect only:** the portal hull's box (VIS's test already has it) inside an already-rendered neighbour's `clip_bounds`. It costs ~15 instructions and 2 loads, ≥20x under the 310 rect exit and ~90x under a clip. It **isn't exact** (point 2), so it can't be the cut.
- **Exact:** the portal hull H, or the parent's region R (each ⊇ R'), inside the neighbour's stored region, tested before the clip. That's q points × p stored edge planes, ~8 instructions each, so ~130 + ~50 loop ≈ **~180 for quads**. It's paid only by portals that pass the rect test into an already-rendered neighbour: on b30 at most 63 a walk (the clip-bound exits, 5.1% of 1,237), ≤11k instructions (≤0.03 ms floor). A cut saves the clip (~1,060) plus the step and everything under it. The step alone, priced from b30's exit mix (task 1) at task 0(c)'s counts, is ~150 + 22.9 portals × ~154 ≈ **~3,680 floor**, so the test is ~26x under clip plus step, and ≥10x holds on b30 [D on counts, E on the mix]. On c20 the share is 0, so the test would be pure cost there. It belongs only behind a switch, or on maps whose census shows the share.

**Saved, a walk (one a frame) [U until the PSP price]:**

| | b30 fight | c20 opening |
|---|---|---|
| steps a walk (task 1) [E] | 54.0 | 5.36 |
| exact cut's share, ceiling | ≤20.8 direct (rect inside); with their sub-walks ≤44 (every step but the 8.9 first entries and the root) [E] | **0** (no rect-inside re-entry) [E] |
| its true share | [U]: needs R' ⊆ R_old counted, a census extension | 0 |
| a step, floor (task 0(c) x task 1's mix) | ~3,680 instr, so 20.8 steps ≈ 0.23 ms and 44 ≈ 0.49 ms; at half (the brief's fallback rule) 0.12-0.24 ms [D on E] | ~1,800 instr (3.4 portals a step); saves 0 |
| a step, measured | **a ceiling only: ≤118 µs** (~39,000 ME cycles at 333 MHz). That's the ME's 6.37 ms visibility job in the fight over the census's 54.0 steps [M over E, row pvswalk_b30]. The job also holds the objects' visibility, and the run printed no `portal walks` line (below), so the steps' own share is [U]. That's ~10x the floor | **open: pvswalk-c20** (its control) |
| saved, ME ms a job | ≤20.8 × 118 µs ≈ **≤2.45 ms** direct, ≤44 × 118 µs ≈ ≤5.2 ms with sub-walks (ceilings) [U]; floor at half 0.12-0.24 ms | 0 |
| saved, main CPU ms a frame | ≤ the collect wait: **1.13 ms a view × 0.95 views a frame ≈ ≤1.07 ms** [M, row pvswalk_b30] | 0 |
| added | ≤0.03 ms floor (the test), plus the stored regions (fit line) | the test on every clip-bound portal into a rendered cluster, ~180 instr each |

- **The two shares, at 30 fps on both cores:**
  - **The ME's share** is priced by the main-CPU work it frees. The main CPU waits at the collect 2.8-3.8 ms a view (`docs/layout_plan.md:95`, run 40845560977 [M], an older build). ME time saved frees main-CPU time only up to that wait.
  - **The collect wait, measured at the ship build** (pvswalk-b30, 2026-11-27, PSP-3000, 2 runs that agree): in b30's fight the main CPU waits at the first reader **1.13 ms a view** (mevisland's 300-view line). The ME's visibility job there takes 6.37 ms, push to seen done 7.81 ms, 0.95 jobs a frame (6.04 ms a frame of the ME) [M, row pvswalk_b30]. The older 2.8-3.8 ms and posepair2a's 1.32-1.40 ms are superseded for this build. So ME time saved gives the frame **at most ~1.07 ms**.
- **The gate (~0.5 ms a frame on a measured stretch) is not decided by pvswalk-b30.** The ceiling clears it: ≤1.07 ms a frame (the wait caps it before the step ceiling's 2.45 ms does). The floor doesn't: 0.12-0.24 ms at half. The gate lies between two unknowns:
  1. **The steps' share of the 6.37 ms job.** The job is clusters and objects, and it can't be split from this run. The run printed no `portal walks` line: that line prints every 900 main-CPU walks (`psp_fixups.py:6774-6776`), mevisland gives the ME ~95% of views (the main CPU walked ~48 in the run), and the ME copy's counters go to a sink (`:57836-57837`). The ask's own clause sent this back to the builder; it isn't a failed run. The floor is far under the ME's job (54 × ~3,680 ≈ 0.6 ms against 6.37 ms), so either misses dominate or the objects do.
  2. **The exact share** (R' ⊆ R_old, point 3), which only bounds it so far (≤20.8 direct of 54.0).
- **c20 can't gain:** its census shows no rect-inside re-entry.

**The fit line** (Sheepdog01 2026-11-27 02:1x):
- **The code.** The ME's walk is `psp_mv_traverse_cluster_body` (3,492 bytes), `convex_hull2d_intersect` (420) and `convex_polygon2d_clip_to_plane` (1,484), plus `convex_polygon3d_clip_to_plane`, whose size is [U] (task 0(c)). That's ~5.4 KB + [U] against the ME's 16 KB I-cache, and the test adds ~0.2-0.4 KB [U, modelled]. No MEXEC copy is needed for its size.
- **The data.** The stored regions take one a rendered cluster, capped at 8 points as the hull cache is (task 0(c), 0x16d2c): ~68 bytes of points + ~96 of edge planes ≈ 164 bytes. b30 renders 8.9 clusters a walk, so ~1.5 KB is live. Room for `MAXIMUM_RENDERED_CLUSTERS` (128, `render.h:26`) is ~21 KB of mevis state in main RAM. That adds to the ME's 16 KB D-cache, which mevis already overruns at 45-93 KB a call (`layout_plan.md:95`) [U]. An eDRAM slot is the alternative.
- **The hand-over.** The regions are the walk's own and the main CPU never reads them, so they add no write-back. The flags and rendered clusters handed over are the ones mevisland already hands over [D on the design]. Bake (A) would add 0 bytes: the same words, rewritten in place (task 0(b)).

**What's left for this sheet** (as of 2026-11-27 06:1x, after pvswalk-b30):
- **(B) is unpriced, not stopped:** its ceiling clears the gate and its floor doesn't.
- **Two counts would settle it,** both cheaper than a build:
  - **(i) The exact share R' ⊆ R_old, a census extension** on the emulator. It's a count of the walk's own work under fixed play, not a stand-in for a hardware cost.
  - **(ii) A split of the ME's job into its steps and its objects on the PSP:** the ME copy's step count wired to its line instead of the sink, or an ME stamp around the cluster walk. Either is an instrument commit, placed-block check first.
- **The lead rules** whether to spend them, and on the 160-step walks (point 4). pvswalk-c20 is still to read, as the control.
