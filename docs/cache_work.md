# The cache work: loop phasing, prefetch, data layout (draft, 2026-10-20)

The PSP-1000 has 16 KB instruction and 16 KB data caches and no L2; about half of play's cycles wait on memory [M]. This page collects the cache work after `plan_memory_stalls.md`, `prefetch_map.md`, `order_file.md` and `research/research_icache.md`. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code, [U] inference. **Status: measurements in flight; nothing here is scored yet.**

## Where it starts (b30's beach, the cache model, game time 90–170 s) [E]

- Tick 29.7 ms in the model: instruction stall 9.7 (33%), data stall 7.6 (26%), work 12.5.
- The tick refetches its ~70 KB hot code 47–75 times a tick; the misses are capacity, not conflicts (a fully associative cache removes only 16–18%).
- Data misses by region: game state 37% (the object pool alone 16%), tag window 32%, program globals 19%, thread stacks 10%, heap 2%.
- The main thread's stack reaches 280 KB deep [E, the `stacks` line], so it can't move to the 16 KB scratchpad; its misses (~4,200 a tick) stay a layout question for the frames that cause them.

## 1. Loop phasing's data side (P1.1)

Question (the review of icache1): for `unit_update`, `weapon_update`, `actor_look_update`, `biped_update` and `object_update`, how much data does each call touch, and does running one function's calls over every object, then the next (phasing), fit both caches?

Method: two new cache-model switches (the lab's emulator patch, its item 18):
- **Footprints:** for each call in the tick, the distinct code and data lines it touches, the data lines an earlier call in the same tick touched too (what a pass keeps warm), and a tick's calls together.
- **Each function a pass of its own:** each listed function's calls run on a pair of caches of their own, the rest on the main pair, with a clock each, so main + passes is the phased tick, scored against the same run as built.
- **The method's check:** the node-matrix pass on c10, where the real batch (nwbatch) lost 34% on the PSP-3000 and its lean rebuild lost in the model: the split must show a loss there too before its gains are trusted.

Results: pending.

## 2. Prefetch

`prefetch_map.md` stands: as the code is, prefetch is worth at most ~0.2–0.4 ms a tick, and only reshaped code or next-item loops open real windows. The one clean next-item case is the node matrices: each node's first store (`matrix4x3_rotation_from_quaternion`'s, the tick's top data-miss site, ~450 misses a tick on b30) waits for its line, and the walk's queue names the next node before this one is done.

Built (not yet compiled or scored): `nodeprefetch` (`FIXES_CACHE`), a `cache 0x1E` of the next node's position line at the end of each node, and of the root's before the walk. Off word `nonodeprefetch`. Exact by construction. Expected ≤ ~0.1 ms a tick on b30 [U].

## 3. Data layout

Pending the base run's names. Candidates from icache1 [E, misses a tick on b30's beach]: the tag instance array through `tag_get` (~1,070), the object headers and spheres (~970, ~1,760), the cluster object references (~1,590), small hot globals that miss on every use (`game globals`, `global_goal`, `ai_profile`, `cache_file_globals`, `psp_surfaces_seen`: ~190–250 each).

## 4. Loop phasing itself

Only if section 1 shows it fits.

## The model's own fix

The cache model counted the game's `cache 0x18` (create dirty exclusive, which the renderer uses on display-list lines) as a drop, so the stores after it missed in the model. It should allocate the line without a read (measured on a PSP-1000, halo_bench v7b). Fixed in a patch not yet in the lab; until it is, the model overstates drawing's list-write misses a little.
