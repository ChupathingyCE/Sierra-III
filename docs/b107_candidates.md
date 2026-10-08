# b107 candidates (worked out 2026-10-08, after b106 shipped)

Items 1-13 are in b107 (see build_b107.md for the hardware test). Each item has passed the emulator on b30's beach route. The Library and Blood Gulch also ran clean with `spherecheck` and `unlinkcheck` on (0 mismatches). Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the decomp, **[U]** inference.

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
- Each object's bounding sphere is also kept in a dense array, 16 bytes an object (32 KB more game state: more than its CPU part had spare, item 10). Checkpoints save and restore it with the objects.
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

## 7. Sleeping scenery checked every other tick (keep word `sceneryhalf`, off by default)

**Cost.** Each tick, every sleeping scenery object's whole datum is checksummed to see whether anything changed it. [E] On b30's beach that's about 240 sleeping objects a tick. The checksum (`psp_checksum_span` and its callers) was 2.8% of the main thread's time.

**Change** (psp_fixups.py's `PSP_SLEEP_CODE`, in objects.c: `psp_scenery_skips_check`):
- With `sceneryhalf`, a scenery object that has been asleep two ticks running is checked on alternate ticks: half of them each tick, by index.
- On the other tick it stays asleep unchecked.
- Vehicles, machines and corpses are untouched.
- The double speed powerup updates objects on even ticks only, so the half is picked by half the game time while it's on (b107t; before, the same half would have been skipped every update).

**What it trades:** anything done to a sleeping scenery object (a script, a blast's push) is noticed up to a tick later than before. Every wake of a sleeping scenery object is counted, checked tick or not ("woken per tick" on the heartbeat's "sleeping scenery" line).

[E] b30's beach route, two 500 s runs:
- 113 objects a tick left unchecked, 0 woken.
- The checksum's share fell from 2.80% to 2.14% of the main thread, 0.23 ms a tick.
- scenery's update fell from 1.82 to 1.57 ms a tick.
- That's about 56 KB less memory read a tick (113 datums of about 500 bytes) [U]. On the PSP each datum read is mostly cache misses, so the saving there is likely larger than 4× the emulator's [U].

Not exact by design, so it has no check mode. Sheepdog01 decides whether it goes on by default after the PSP's A/B.

## 8. The campaign-only engine (Sheepdog01, 2026-10-08)

Co-op and multiplayer become a branch of their own, with their own budgets, so the main engine is campaign-only:
- `psp_build.py` leaves out the networking, the netcode and the multiplayer game engines (`MULTIPLAYER_UNITS`). It keeps bungie_net's threads and mutexes, which the saved games and menus use.
- `psp/src/psp_campaign.c` answers what the campaign still calls in those units: 74 functions, the engine table and two script globals. Each gives the real function's answer when there is no network game, read from the real code (two didn't until b107t, both reached only from the multiplayer menus: item 12). Two matter for play:
  - `network_damage_deals` is TRUE: this machine deals its own damage. The link's automatic `return 0` stub would have made the campaign deal none.
  - `network_objects_new_object_index` is NONE: the game picks where a new object goes. The stub's 0 would have put every object at index 0.
- It includes the real headers, so a stub whose signature drifts from its declaration fails to compile. The link now stubs **0** symbols.
- `PSP_MULTIPLAYER=1` builds the units in as before. It's the starting point for that branch, not maintained in the campaign line.

**The program: 5.16 → 4.57 MB (−600 KB); the heap grows to match (5,376 → 5,952 KB).** [E]:
- b30's beach: the same fight (14–24 corpses, the garbage collector running) and the same heap.
- The Library with `savetest`: checkpoint and revert.
- Blood Gulch loads (no game engine, as we test MP maps).
- No crash or new error.
- Tick and frame match b107g within the run-to-run noise. The networking was never more than 0.02% of the time.

## 9. Data sized for the Xbox, cut to the PSP's

- **Lens flare results per BSP marker.** The table held one result for every possible marker index: 65,536, 64 KB. The most markers in any BSP of the ten campaign maps is 4,616 (d40), read from the maps [D]. Now 8,192 + 8 (8 KB). A marker past that would share one spare result (its flare fades with the others') and logs once. [E] The Library (700+ markers a BSP): no marker past it.
- **The D3D shim's occlusion query slots** (`d3d8_gl.c`): 4,096 GL queries in four arrays (64 KB). Never used on the PSP: its flares test with `psp_ge_widget_occlusion`, and no run reached them. Now two slots: every use takes its index modulo the count and then turns 0 into 1, since slot 0 is the scratch query (b107h–s had one, which only the PSP's never-ready GL kept from being read past; item 12).
- **The vertex shader copies** (`D3DDevice_CreateVertexShader`): every Xbox vertex shader's code copied to the heap for the GL translator, which never runs on the PSP. [E] Heap in use on b30's beach fell from 4,501 to 4,463 KB.
- **Lens flares submitted a frame:** measured, not cut. The new "lens flares" heartbeat line shows **318 a frame at peak on The Library** [E], of the 1,024 kept (40 KB). a10 and d40 have more markers, so this waits for their bring-up.

The program is now **4.45 MB, heap 6,080 KB** (b106: 5.16 MB, 5,376 KB).

## 10. A game-state overflow from item 4, fixed

Item 4's sphere copies (32 KB) took the game state's CPU part past its end. game_state.c's assert 159 logged at every start from b107e on [E]. b106 is clean.
- It was harmless so far: the GPU part (the decal vertices, filled from the top) uses 160 KB of its 256, so the two never met, and checkpoints save the whole buffer.
- One more allocation would have put the CPU part's end inside the decal vertices.
- Fix: 64 KB moved from the GPU part to the CPU part (`psp_capacity.h`). The total and the layout are unchanged.

## 11. Three bugs a10 found (bring-up)

a10, baked with b106's bundle, crashed 40 s into its opening on b106 and b107 alike [E]. All three are fixed in the dev tree.

- **A BSP switch wiped the animation cache's pages in the tag window** (the crash). `scenario_structure_bsp_load` filled the window's free part with 0xCD, the Xbox debug build's fill, from the tags' end to the window's end, before reading the BSP.
  - Since b106 the animation cache keeps up to 368 KB of pages in the part of that range no BSP reaches (the pack's header gives it).
  - a10's first BSP switch filled them under the cache's feet, and the next keyframe read went through 0xCDCDCDCD offsets.
  - Fix (FIXES_107 `cache_files.c#bspfill`): no fill on the PSP. Nothing reads it, and it was also up to 3.8 MB of memory writes per switch.
  - [E] a10 now plays through its opening. On b106 the keep word `animnowindow` (no cache pages in the window) avoids it: Sheepdog01's b106 runs use it. **b30 would hit it at its BSP switch [U].**
- **A cinematic's recorded movement read a short from an odd address.** The event stream is packed byte by byte, and `vector_short_difference_data`'s two shorts can start at an odd address.
  - The Xbox reads them anyway; the PSP's CPU raises an address error. The emulator's JIT doesn't, so only the strict interpreter saw it, 35 s into a10's opening.
  - Fix (FIXES_107 `recorded_animation_playback.c`): the struct is packed on the PSP, so GCC reads it a byte at a time. The other events already used memcpy or single bytes.
- **The animation cache could take a range across its two blocks as a hit** (`psp_anim.c`). Pages read one at a time can land in the first block's last slot and the second block's first: numbered one after the other, but not side by side in memory. Not seen to fire (a10 ran into the fill first), but ranges of several pages are common: a10's cinematic animations are up to 32 KB, b30's animations up to 15 KB (8 KB pages). A run across the blocks now counts as a miss.

`tools/animcheck.py` checks a baked map's compressed animations the way the engine reads them. a10's 628 pass, so the data was never the problem.

## 12. A fresh-eyes review (b107t)

A reviewer that hadn't seen the work read everything b107 changed, and the parts of b106 it touches. Each finding was checked before it was fixed. Two are in b106 as shipped.

- **A read request could be taken half-written (b106 too).** `cache_file_read` publishes a request by setting its slot's `pending`; the reader thread takes any slot that is pending and not running.
  - [D] b107s's machine code stores `pending` first, before the request's event, size, offset and buffer. The C order says otherwise, but nothing told the compiler another thread looks.
  - Since b106 the reader runs above the game thread (0x1C over 0x20) and wakes whenever a read finishes, so it can preempt in those few instructions. It would read the slot's old size from offset 0 into the old buffer, and mark the new request running, so it is never read and its caller waits on it, or gets wrong data [U].
  - Rare (a window of about ten instructions), and no run has shown it [U].
  - Fix (FIXES_107 `cache_files_windows.c#publish`): a compiler barrier, so `pending` is stored last. [D] b107t's code stores it last.
- **A sound past the 12th could keep a stale muffling (b106 too).** b106's obstruction cache reused a result of any age once a frame's 6 tests were spent. The sounds come in the same order every frame, so at 2 ticks a frame the first 12 take the budget every time. The rest kept the muffling of wherever the camera was when they started (after a checkpoint revert, from before it).
  - Fix (FIXES_107 `game_sound.c#stale`): past the budget a result is reused only while under 15 ticks (half a second) old. An older one is tested anyway.
  - The "sound obstruction" log line now counts the tests past the budget (a new sound's first test is one too, as in b106). [E] b30's beach and The Library: 0.0 a frame. d40's opening (the most sounds in reach so far): 0.3 a frame on average, 2.7 at most, against 3.5 tests a frame in all.
- **`sceneryhalf` under the double speed powerup** would have skipped the same half every update: item 7.
- **The occlusion query slots needed two, not one**: item 9.
- **The animation cache's failure block could move under a caller** (`psp_anim.c`). A bigger failure reallocated it while an earlier failure's pointer could still be in use. Now a bigger block is added, and all of them are freed when the pack closes. No run has logged a failure.
- **Two campaign stubs didn't give the real answer** (`psp_campaign.c`): `network_game_player_is_local` (the real one is TRUE with no client) and `network_player_is_valid` (it checks the player's fields). Both are reached only from the multiplayer menus. Both now copy the real code.
- **Five comments corrected:** where the lens flare limit lives (psp_fixups.py's PSP sizes, not psp_limits.h); the animation sizes (measured from the baked maps: a10's largest 32 KB, b30's 15 KB); item 4's "spare 97 KB"; what "woken" counts; and collisions' "objects walked", which since item 4 counts only the objects past the sphere test.

[E] b107t, all clean (no crash, nothing logged as wrong):
- b30's beach route with `sceneryhalf`: 9,495 ticks.
- The Library with `savetest`, `spherecheck` and `unlinkcheck`: checkpoint and revert; 0 of 7.3 million sphere copies and 0 of 775,000 unlinks differed.
- d40's opening: 14,956 ticks.

The program and heap are unchanged: 4.45 MB, 6,080 KB.

## 13. Option A: room where every map shows it (Sheepdog01, 2026-10-08)

b106's two hardware runs (hardware_b106.md) showed two problems on every map: objects flickering in and out (the display list full) and sounds that didn't play (the sound cache full). Sheepdog01 chose to spend the freed memory there first; the three maps that don't fit the window wait for bake-side shrinking.

**Memory outside the heap** (`psp_memory.c`, `outside_alloc`).
- About 550 KB of user RAM outside the heap sat idle in two pieces: the display list's 384 KB fitted neither [M: 543 KB free, largest piece 276 KB].
- The rasterizer's contiguous start-up buffers (16-96 KB each) now go there, as long as 160 KB stays free for what is made later.
- [E] 384 KB placed there on every map; 165 KB left free.

**The sound cache: 1,152 → 1,536 KB**, and keep.txt's `soundN` (KB) picks another size (512 to 4,096).
- [E] d40's opening, to game time 6,650: failed loads for room 5,993 (1,152 KB), 144 (1,536 KB), 0 (1,920 KB). Loads fell from 1,720 to 882: fewer reads from the stick too.

**The display list: 384 → 512 KB**, and what the models put in it [E, b30's beach replayed from Sheepdog01's screenshot spots and views]:
- **Bone matrices once a part.** Every pass of a bending part (the two-sided pass, the change and glow colour passes) loaded every batch's bone matrices again. The first pass now writes them into the list as a sub-list each pass calls (2 words). Keep word `bonesdirect`: as before.
- **Change-colour palettes: 6 → 40 cached** (40 KB). With 6, about 60 palettes a frame were worked out again into the list, about 1 KB each. Over a run: 166,000 worked out before, 41 after. Less CPU too.
- **Near the eye, a copy only when needed.** A bending batch's kept triangles are copied at the first one that is dropped or cut; a rigid part checks first, and draws its own indices when every triangle is inside.
- **The smallest models go first when it's still full.** A model whose bounding sphere is under the size floor on the screen isn't drawn. The floor rises a quarter each frame that skipped model draws (up to 8 px) and eases back once there's room, so it's the same small, far models that go, not a random set each frame. Keep word `nosizefloor`: as before.
- Result: models' data copied into the list ~90 → 7-14 KB a frame late in the run; model draws skipped there 0 (was 100-160); decal draws skipped ~16 a frame (was ~300). The first windows beside the Pelicans still skip 14-34 model draws a frame (the floor at work).

**Memory, all of it:** sound +384 KB, list +128 KB, palettes +34 KB (static, so the heap is 64 KB smaller: 6,016 KB), against 384-512 KB placed outside the heap. [E] heap tops, of 6,016 KB: a10 4,820, The Library 4,564, b30 4,424, d40 3,900 (build_b107.md). b106 on the PSP ran about 500 KB above the emulator on The Library, from fragmentation in long play [M], so a10 should still have about 700 KB [U].

## 14. Seen on the PSP, not fixed yet (b108)

- **The Pelicans' thrusters and c40's light shafts draw white.** Both are see-through shaders of several maps multiplied together (`shader_transparent_chicago`: the Pelican decals atlas twice, at two offsets; `shader_transparent_generic`: noise × noise × a shaft mask, added). The GE path draws one map, at full strength, added: white [D, E]. The fix belongs in the bake: flatten such shaders into one texture.
- **The ocean** (`water simple`, a reflection cube map first) had no map the GE could draw and wasn't drawn: the white water. b107 passes over the cube for the next map, so the water draws [E]. White streaks over the water in the Pelican's flight remain, not yet identified.
- **Commands per model part** are now the models' largest share (50-135 KB a frame): state set again by every part and pass. Leaving out repeated state is the next display list saving.

## 15. Looked at, not done

- **The object render state cache is full** (b30's beach: peak 256 of 256 cached object render states). Objects past it probably rebuild their lighting every frame [U]; worth measuring what the cap costs.
- **VFPU skinning** (b106's `vfpuskin`): waiting for the PSP's A/B.
