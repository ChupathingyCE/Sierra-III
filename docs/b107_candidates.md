# b107 candidates (worked out 2026-10-08, after b106 shipped)

In the dev tree, not shipped: b106 goes to the PSP first. Each item has passed the emulator on b30's beach route. The Library and Blood Gulch also ran clean with `spherecheck` and `unlinkcheck` on (0 mismatches). Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the decomp, **[U]** inference.

## 1. No debug record for an actor's path search

**Cost [D].** Each path search (`path_state_new` from actor movement and firing positions) took a debug record from `ai_debug_get_path_storage`, 82 KB: most of it is a copy of the search's own 82 KB state. That function cleared all of it, and the search copied its state into it at the end. Two 82 KB memory passes per search, for a debug display the PSP never shows. [E] The clearing alone was 3% of b30's "ai update" step.

**Change.** With `PSP_DEBUG_TOOLS` off, the function returns no record. The path code tests `state->debug` before every use (path.c), so nothing else changes.

**Left alone:** `path_state_new` still clears its own 82 KB state per search (1.2% of the AI step). Trimming that needs an audit of which fields the search reads before writing them.

## 2. Cluster partition unlinks without walking the list

**Cost.** Every moving object is disconnected from its clusters and reconnected each tick. The removal walked the cluster's whole list to find the element before it. b30's beach cluster holds about 300 objects, so a removal read about 150 elements. [E] `reference_list_remove` was 1.35% of all the time, 4% of "objects update"; on the PSP each element read is likely a cache miss [U].

**Change** (`cluster_partitions.c`, FIXES_107):
- Each element keeps the element before it in its spare word (`pad`: an absolute index, 0xFFFF for the list's head).
- The object's own reference to the cluster keeps where its element is.
- The unlink checks both links before trusting them and falls back to the walk otherwise.
- It removes the same element the walk would (an object is in a cluster's list once), so the lists come out identical.

[E] b30's beach: 108 direct unlinks a tick, 0 walks. **`unlinkcheck`** compared 1,648,960 direct unlinks with the walk over a whole run: **0 removed a different element**.

## 3. `_stricmp` without a `tolower` call per character

Marker lookups (`model_find_marker`'s binary search: weapons, attachments, the AI's aim) and the scripts' name lookups go through `_stricmp`. [E] `_stricmp` and `tolower` came to about 1% of b30's beach, and 4% of The Library's "widgets render" step (lightning's markers).

The game never changes the C locale, where `tolower` maps A–Z alone, so folding A–Z inline gives the same answers.

## 4. Objects rejected on a copy of their sphere, before their datum is read

**Cost.** A ray's objects (`collision_test_vector`: the AI's lines of sight, projectiles, sound obstruction) and a sphere query's (`collision_get_features_in_sphere`: biped and vehicle physics) are every object in each cluster they touch. That's about 300 on b30's beach, mostly sleeping scenery. Each visit read the object's header and datum just to test its bounding sphere. [E] In b30's "ai update" step, `object_get_and_verify_type` was 16.8% and `datum_get` 10.7%, mostly from these visits. On the PSP each visit is likely two or three cache misses [U]; the emulator doesn't charge for them.

**Change** (FIXES_107, `objects.h#spheres`):
- Each object's bounding sphere is also kept in a dense array, 16 bytes an object (32 KB of the game state's spare 97 KB). Checkpoints save and restore it with the objects.
- The sphere is written in one place (`object_compute_node_matrices`) and the copy is written there too. A new object's datum and copy both start zeroed.
- The two loops test the copy first, with the same test on the same numbers the original makes first. Why the same objects get tested:
  - the original's other conditions are and-ed with that test;
  - a cluster's objects are roots (a child is never in a cluster list, and a root's sibling link is always NONE);
  - children are only tested inside the sphere test.

[E] b30's beach: **3,573 sphere tests a tick, 3,395 rejected without reading the object** (95%). **`spherecheck`** compared the copy with the datum at every use: **20,936,711 compared over a whole run, 0 differed**.

## 5. The field of view's tangent, once

The decals asked `tanf` twice per call (800 calls a frame in the fight): 0.4% of the beach's time [E]. It's now worked out when the field of view changes.

## 6. A rigid part's corners near the eye: one transform, not two

A rigid part near the eye has each corner tested against the near plane and the screen's guard band. That took two three-row products per corner: the part's matrix, then the camera's axes (`model_to_camera`). The two are now composed once per part (`rigid_rows`), the way the bending parts already did. The rounding differs, so a corner exactly on a plane could fall the other way. [E] **`nearcheck`** compared 9,522,140 corners with the two-step transform: **0 verdicts differed**. `psp_ge_draw_model_body` went from 3.84% to 3.25% of b30's beach. The cut path still uses the two-step transform for the pieces it makes.

## 7. Looked at, not done

- **The object render state cache is full** (b30's beach: peak 256 of 256 cached object render states). Objects past it probably rebuild their lighting every frame [U]; worth measuring what the cap costs.
- **Scenery sleep checks every other tick**: the checksum is 2.3% of the beach's time. Checking idle scenery on alternate ticks halves it, but anything done to a scenery object is then noticed up to a tick late.
- **VFPU skinning** (b106's `vfpuskin`): waiting for the PSP's A/B.
