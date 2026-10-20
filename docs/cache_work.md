# The cache work: loop phasing, prefetch, data layout (draft, 2026-10-20)

The PSP-1000 has 16 KB instruction and 16 KB data caches and no L2; about half of play's cycles wait on memory [M]. This page collects the cache work after `plan_memory_stalls.md`, `prefetch_map.md`, `order_file.md` and `research/research_icache.md`. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code, [U] inference. **Status: the base measured (b30, c20, d20); loop phasing's first footprints in (c20, 30 s); the phased tick and `nodeprefetch`'s score in flight.**

## Where it starts (b30's beach, the cache model, game time 90–170 s) [E]

- Tick 29.7 ms in the model: instruction stall 9.7 (33%), data stall 7.6 (26%), work 12.5.
- The tick refetches its ~70 KB hot code 47–75 times a tick; the misses are capacity, not conflicts (a fully associative cache removes only 16–18%).
- Data misses by region: game state 37% (the object pool alone 16%), tag window 32%, program globals 19%, thread stacks 10%, heap 2%.
- The main thread's stack reaches 280 KB deep [E, the `stacks` line], so it can't move to the 16 KB scratchpad; its misses (~4,200 a tick) stay a layout question for the frames that cause them.

**The base on three maps** (w140's build, b139's profile; the cache model with timing; game time to 170 s on b30's route, 90 s on c20's and d20's openings; a tick's mean) [E]:

| Map | Tick | D-miss stall | D-misses | I-miss stall | I-misses | Prefetch at every site, best window | after its own cycles |
|---|---|---|---|---|---|---|---|
| b30 | 34.75 ms | 8.93 ms | 41,282 | 11.86 ms | 61,734 | 0.38 ms | 0.18 ms |
| c20 | 18.52 ms | 4.17 ms | 19,285 | 8.41 ms | 43,753 | 0.21 ms | 0.12 ms |
| d20 | 18.88 ms | 4.86 ms | 22,477 | 5.21 ms | 27,093 | 0.19 ms | 0.09 ms |

Placing prefetches in the code as it is hides 4–5% of the data stall at best, as `prefetch_map.md` found on b30 alone. Moving up to three memory operations out of each window (reordering, which is a code change per site, not a prefetch) would hide 2.5 ms on b30 and 1.2 ms on c20 and d20: the ceiling for reshaped loops.

On all three maps the node walk holds the top sites: `matrix4x3_rotation_from_quaternion`'s first stores into the node's matrix (both ends of its 52 bytes) and `object_compute_node_matrices`' read of the node's links in the model tag. Together 917 misses a tick on b30, 683 on c20, 430 on d20 (0.20, 0.15, 0.09 ms a tick at full hide). Next on c20 is the animation's keyframe read (`animation_get_keyframe_rotation`, 158 a tick).

Data misses in b30's tick by allocation: the object pool 15.6%, the main thread's stack 7.2%, object spheres 4.1%, cluster collideable object references 3.4%, props 3.1%, actors 2.6%; by function: `collision_bsp_test_vector_recursive` 6.6%, `object_get_and_verify_type` 4.9% (one load, its type check through the object header, 1,196 misses a tick with no window), `psp_bsp3d_test_sphere` 4.6%, `cluster_partition_get_next_datum` 3.1%, `object_compute_node_matrices` 2.5%.

## 1. Loop phasing's data side (P1.1)

Question (the review of icache1): for `unit_update`, `weapon_update`, `actor_look_update`, `biped_update` and `object_update`, how much data does each call touch, and does running one function's calls over every object, then the next (phasing), fit both caches?

Method: two new cache-model switches (the lab's emulator patch, its item 18):
- **Footprints:** for each call in the tick, the distinct code and data lines it touches, the data lines an earlier call in the same tick touched too (what a pass keeps warm), and a tick's calls together.
- **Each function a pass of its own:** each listed function's calls run on a pair of caches of their own, the rest on the main pair, with a clock each, so main + passes is the phased tick, scored against the same run as built.
- **The method's check:** the node-matrix pass on c10, where the real batch (nwbatch) lost 34% on the PSP-3000 and its lean rebuild lost in the model: the split must show a loss there too before its gains are trusted.

First results (c20's opening, 30 s; the switches' smoke run) [E]. A pass of one function's calls keeps its code warm only if the code all its calls run fits the cache's 256 lines:

| Function | Calls a tick | Code lines a call | Code lines a tick, all calls | Data lines a call (own) | Fits? |
|---|---|---|---|---|---|
| `weapon_update` | 1.4 | 51 | 52 | 22 (20) | yes |
| `actor_look_update` | 3.9 | 75 | 105 | 35 (22) | yes |
| `unit_update` | 29 | 68 | 384 | 41 (20) | no |
| `biped_update` | 29 | 299 | 543 | 120 (28) | no |
| `object_update` | 78 | 365 | 1,463 | 142 (38) | no |

(Callees included; "own": lines no earlier call in the tick had touched, which a pass of its own would fetch again.) `biped_update`'s single call already runs more code than the cache holds, and `unit_update`'s calls together run half again as much, because each kind of unit takes its own path. So phasing those three can't keep their code resident; only the small ones fit, and they are few calls a tick. The split with its own clock agrees with the old one-pair split to the cycle on the same function (the switch's own check).

**The method's check failed** (c10, to 90 s, 2,700 ticks) [E]. The node-matrix pass (`object_compute_node_matrices`, 305 calls a tick) run as a pass of its own scores 14.44 ms a tick against 16.31 as built: a gain of 1.87 ms (11.5%), tick I-misses down 11,689 and D-misses up 1,739. The real batch of those calls (nwbatch) lost 34% of c10's tick on a PSP-3000, and the model scored its lean rebuild a loss, so the check expects a loss here. The reason is the score's own definition: it is the gain if the calls' order could change freely, with the pass's caches taking every call in order, and it counts nothing for what the restructuring costs (the loop that gathers the objects, state carried between the passes, the extra loads and stores). That cost is what the real batch paid. Phasing scores therefore give an upper bound whose sign can't be trusted, and the tables for b30, c20 and d20 (all five updates at once, and each alone) were not read. Phasing stays unscored until the method is given the restructuring's cost, or until a candidate is built for real and A/B'd in the model; the footprints (above) are the part of this that stands.

## 2. Prefetch

`prefetch_map.md` stands: as the code is, prefetch is worth at most ~0.2–0.4 ms a tick, and only reshaped code or next-item loops open real windows. The one clean next-item case is the node matrices: each node's first store (`matrix4x3_rotation_from_quaternion`'s, the tick's top data-miss site, ~450 misses a tick on b30) waits for its line, and the walk's queue names the next node before this one is done.

Built and compiled, scoring in flight: `nodeprefetch` (`FIXES_CACHE`). The walk is a queue (siblings and children appended as each node is done), so the node after next is often named already: at the end of each node, every queued node up to the one after next that hasn't been asked for gets three `cache 0x1E`s, for both ends of its matrix and its links in the model tag; the root's before the walk. Each queued node is asked for once, about one node's compute (more than the ~72 cycles a line takes) ahead. A tag block without data is skipped, so no prefetch names an address outside memory. Off word `nonodeprefetch`. Exact by construction. Expected: most of the node walk's 0.09–0.20 ms a tick [E].

Not built: making the node block's lines with `cache 0x18` instead (no read at all, since each matrix is written whole). A line may hold parts of two nodes, and the walk's order is not the nodes' order, so a created line could wipe a node already written this tick; it would need a per-model plan of which lines are whole, for at most the same 0.2 ms.

## 3. Data layout

Misses a tick in the base, b30 to 170 s [E]: the object pool ~6,400; the main thread's stack ~2,960 (and a second stack ~1,150); object spheres ~1,710; cluster collideable object references ~1,400; props ~1,260; actors ~1,080; `psp_sphere_memo` ~290; `psp_surfaces_seen` ~260; `ai_profile` ~260.

Built and compiled, scoring in flight: **`typehdr`** (`FIXES_CACHE`). `object_get_and_verify_type`, called from everywhere, checked the type in the object's own data, a line the caller often never touches otherwise: on b30 that load missed ~1,200 times a tick (18% of its runs), the tick's most-missed load, with no window for a prefetch (its address comes from the load before it). The object's header, whose line the lookup has just read for the data pointer, holds the same type, written with it when the object is made and never changed [D]: the check reads that instead. Exact: it passes and fails as before. Off word `notypehdr`. At most ~0.26 ms a tick on b30 [E] if the callers don't then miss on the same line; the batch says. Its keep check is a byte load and a branch on each call, paid on both sides.

Still to score: the cluster reference walk (`cluster_partition_get_next_datum`, 3.1% of the tick's misses) and the object spheres (`psp_sphere_memo`'s neighbours); the stack's misses follow the deep frames, a layout question for the frames that cause them.

## 4. Loop phasing itself

Not started. Section 1: the code of the big updates doesn't fit the cache, and the model's phasing score failed its check, so a candidate needs the restructuring's cost counted before it is built.

## The model's own fix

The cache model counted the game's `cache 0x18` (create dirty exclusive, which the renderer uses on display-list lines) as a drop, so the stores after it missed in the model. It should allocate the line without a read (measured on a PSP-1000, halo_bench v7b). Now fixed in the emulator's patch: `0x18` makes the line without a read, `0x19` is the drop. Cache-model runs before and after the fix differ a little in drawing; compare them only within one batch.
