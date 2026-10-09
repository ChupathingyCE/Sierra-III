# Build 109: candidates (2026-10-09)

What comes after b108, ranked by what the emulator's cache model measured on b30's beach (b108l, `docs/overnight_b108.md`). Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code, **[U]** inference. Nothing here ships until b108's hardware runs are in.

## 1. Marker lookups remembered (in the tree: b109c)

- **Why:** units, weapons, the AI's aim and effects look their markers up by name every tick: a binary search by `_stricmp` through the model's markers. In the b108l model, `model_get_marker_by_name`, `model_find_marker` and `_stricmp` had 2.5% of the tick's misses, instructions and data alike [E].
- **What:** `model_find_marker` keeps its answer for a model and a name pointer in a 256-entry table (FIXES_109 in `psp_fixups.py`). Only names that can't change under the pointer are kept:
  - string constants (the program's read-only data);
  - tag strings in the tags' part of the tag window (not the BSP's, which a BSP switch replaces).
  - The table is forgotten as each map's tags load. Other names search as before.
- **Found so far** [E]: b30's beach makes ~600 lookups a frame, 97% answered from the table. b40 makes ~40. Same AI peak (29 actors) as b108, no assert.
- **Off switch:** keep word `markersearch`.
- **The model run** (b109c from the same moment as b108l) [E]: `_stricmp` is gone from the tick's top 40. The marker code runs about half the instructions a tick (113 K → 54 K), and the search's own data misses a tick halved (595 → under 300). `model_get_marker_by_name`'s own misses (the marker's instances, the node matrices) are unchanged, as expected. The tick ran 12% fewer instructions and had 10% fewer data misses than b108l. That total is soft: b109c's run covered a longer stretch of the fight (1,614 ticks against 1,011), because the runs stop on the wall clock.
- **Next:** the hardware A/B (`markersearch` against the default).

**Method note:** the cache-model runs should stop on emulated time (`--timeout-emulated`), not the wall clock. Then two builds cover the same stretch of the game, whatever else the machine is doing.

## 2. The geometry cache's entry table

- `entries` (12 KB) has 4.0% of the drawing's data misses: the largest named table [E]. Each lock finds its entry again by hash.
- **Option A:** `entries`, `hash_head` and the profiler's front table in the scratchpad (14.8 KB). That saves about 1.7 times what b108's pair saves, but `model_cut` and the profiler's slots go back to the heap.
- **Option B:** fewer lookups. Remember the last few locked entries, and don't write `last_frame` when it already holds this frame.
- **Measure first.** Option B costs no scratchpad.

## 3. The sleep checksums

- `psp_checksum_span` has 6.7% of the tick's data misses [E]. Each sleeping object's data is summed every tick to see that it didn't change.
- `sceneryhalf` (b107, off by default) checks idle scenery every other tick. That halves the scenery's share, at a tick's delay before something done to it is noticed.
- **The model run** (b109c with and without `sceneryhalf`, the same moment on the beach; 1,626 against 1,614 ticks over nearly the same stretch) [E]:

| Per tick | Default | `sceneryhalf` | Change |
|---|---:|---:|---:|
| Instructions | 4.13 M | 3.76 M | **−9%** |
| Instruction misses | 69.5 K | 63.3 K | −9% |
| Data misses | 35.2 K | 32.1 K | −9% |
| `psp_checksum_span`'s data misses (the run) | 2.42 M | 1.69 M | −30% |

  113 idle scenery objects a tick go unchecked.
- **Next:** the hardware A/B. If it holds, make it the default in b109 (Sheepdog01's call: a change to idle scenery is noticed up to a tick late).

## 4. The Media Engine's first job: AI line-of-sight rays

- Rays against the level's BSP: `collision_bsp_test_vector_recursive` plus `collision_test_vector` are 4.6% of the tick's instructions and 6.6% of its data misses [E].
- The BSP is read-only tag data, so the ME reads it with no coherence work. The ME's own caches hold the BSP's nodes, away from the main CPU's.
- The AI's rays go through `ai_test_line_of_sight` (`ai/ai.c`; four calls in `actor_perception.c`). One call tests the level *and* the sight-blocking objects. The level's half suits the ME; the objects' half (game state, changing every tick) stays on the CPU [D].
- **Design steps:**
  1. Count the calls per tick and the share the level's half takes.
  2. Batch them: the requests made one tick are answered by the ME for the next. Check which callers can live with an answer a tick late.
  3. Measure one batch on both sides (copy in, run, copy out) before going further.

## 5. The main menu pass

- See `overnight_b108.md` section 8. The camera follows the menu script's 11 points as on the Xbox. Next: Xbox reference shots of the same moments (the script's points: `uicam`, `point_2` ... `point_11`, 400 ticks between each).
- Then: the near-plane cuts and the black below them, the white band, and quality only the menu can afford (3 ms of 33 a frame used).

## 6. Game-state pools sized to measured peaks

- b30's beach: actors peak at 29 of 256 (457 KB); props at 189-226 of 768 (234 KB); hs threads at 68 of 256 (134 KB) [E].
- Every campaign map now loads, so the openings' peaks can be read in the emulator. The fights need hardware runs (the `game state:` lines).
- The memory would go to the heap, which is tight: b107's full run of b30 on the PSP topped out at 5,956 of 6,016 KB [M] (`hardware_b107.md`). The emulator's short runs stay lower (b40's opening 3.7 of 6.1 MB [E]), so they don't show the real peak.

## 7. ChupathingyCE

- Sheepdog01 decided to switch (2026-10-09). The rebase is build 110: `chupa_rebase.md`.
- `PSP_FIXUPS_CHECK=1 python3 psp_fixups.py TREE` lists every anchor a new upstream breaks.
