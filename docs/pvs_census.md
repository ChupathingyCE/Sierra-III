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

Open: the per-step path (`portal_hull_from_portal` with build 31's hull cache, `points_within_distance`, `convex_hull2d_intersect` with VIS's rect test, `psp_fixups.py:27509`), its loads, and the cycles from the disassembly of the ME's copy. This is the next step.
