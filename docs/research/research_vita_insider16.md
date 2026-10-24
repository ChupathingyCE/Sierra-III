# Research: halo-ce-vita 1.1.1-insider.16, read for Sierra-III

A second look at BirchWoodGod's Vita port, at its developer package `1.1.1-insider.16` (commit `82a9f97e`, 2026-10-20), compared file by file with `b6a7a29`, the version `research_vita.md` §12 last read. The package is a pre-release, given to the maintainer with a request not to pass it on; this note keeps to ideas and pointers, and nothing of it is in this repository.

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. Their numbers are from Vita hardware or their Linux harness, and are marked as theirs.

**Terms:** as `research_vita.md`'s header. BirchWoodGod's own code is ours to take; anything he credits to others is idea-only until its author agrees, except OpenCE's, which is CC0.

## The short answer

**Little that is new for the campaign.** Of ~280 changed or added files, nearly all are multiplayer menus, friends and parties, Forge, Custom Edition and MCC maps, movies and ragdolls [D]. Within the engine there is one tick speed-up worth a count (**L1, dark lights**), one render cull we already mostly have (L2), a client clock fix for the multiplayer session (L3), and two cheap diagnostics (§4). **Nothing new for the open look list** (§5).

| # | Idea (their file) | Exact? | For us | Worth [U] |
|---|---|---|---|---|
| L1 | Attached lights that are dark stay off the map (`objects/object_lights.c`, `light_attached_reconnect`) | Render-exact; game state differs | Count first, then a keep word | **Medium**: a share of `object_connect_lights`' 2.57 ms a tick on b30 |
| L2 | Lens flares out of view get no occlusion test (`rasterizer/rasterizer_lights.c`; OpenCE's) | Yes | Mostly in already | ~0 |
| L3 | A client a few ticks behind the host for a second takes the host's time (`networking/network_client_manager.c`) | Changes the client's clock | Multiplayer session's call | Unknown |
| D1 | A memory pool that runs out says so in release builds (`memory/stack_memory_pool.c`) | Logging only | Cheap | Low, diagnostic |
| D2 | A progress heartbeat that tells a long load from a hang (`port/linux/src/frame_timing.c`) | Logging only | Maybe for the PSP agent | Low |

## 1. L1: dark attached lights left off the map

- **What [D].** Every tick an object's lighting is recomputed, `object_connect_lights` takes each attached light off the map and puts it back: its marker found by name, its sphere's clusters walked, its cluster references remade. The dynamic lighting flag is never cleared, so for a unit, weapon or vehicle that is every tick. Their change works out each attached light's colour first, from the object's function value and change colour, exactly as `lights_preprocess_scene` will. A light whose colour is black is left off the map (or taken off), and goes back as before on the first tick it isn't black. Switch: `HALO_DARK_LIGHTS`, on by default.
- **Why it is safe [D, their reading].** Only the render reads a light's cluster lists, and the render already passes over a black light: no rasterizer light, no lens flare, and nothing taken from it by an object it reaches. The one difference is that a dark light no longer takes one of a scene's 128 visible-light places, which only a scene of more than 128 could show. Unattached (effect) lights, and lights whose object can't be got, go back as before.
- **Their measurement.** In a 32-player crossplay game, lights were 15–20% of a client's tick, and 60% of them were put back. The rest were the plasma weapons' flares (theirs).
- **For us.** `object_connect_lights` measured 2.57 ms a tick on b30 [M] (`levers_next_b138.md`, lever 2). Campaign lights that are dark most of the time [U]: muzzle flashes between bursts, flashlights that are off, and perhaps lights driven by a weapon's charge or overheat. **The count comes first:** per tick on b30, a10 and c20, the attached reconnects and how many of them are dark. If half are dark, it saves ~1 ms a tick on b30 [U]. The test costs a function value and a colour blend per light. That is cheaper than the marker lookup and cluster walk it replaces [U].
- **Checks before it ships.**
  1. **Sleep and the relink memo.** Our sleeping objects, and `relinkmemo`, skip `object_connect_lights`. A light left off the map by an object that then sleeps stays off until the object wakes. This is exact only if a sleeping object's function values can't change [U].
  2. **The state hash.** The light datum's connected flag and its cluster references now differ from the original's, as T7's illumination fields do in `research_vita.md` §7. The play-identical check must mask them.
  3. **Fit with lever 2.** Lever 2 (re-linking lights once a frame) and L1 add up: L1 removes work, and lever 2 moves what is left.
- **Credit.** BirchWoodGod's code, under his grant. Suggested keep word: `darklights`.

## 2. L2: out-of-view lens flares (OpenCE's)

- **What [D].** Before a lens flare's occlusion test is submitted, a frustum test of a sphere that takes in the test's offset point and size (three occlusion radii). First-person weapon flares are exempt, because OpenCE draws them at the weapon's own projection. Their reason: on the Vita each test is a draw and a visibility slot, and a level with many lights submitted hundreds a frame.
- **For us [D].** Our stand-in for the Xbox's query (`psp_fixups.py`, FIXES_31) already casts no ray for a flare with no pixels on screen, and it caps rays at 24 a frame. All that is left to save is the projection of each off-view flare. That is worth ~0 [U] unless a count shows hundreds of flares a frame. Leave it.
- **Credit.** OpenCE `744040c8`, xshxdex98 (CC0).

## 3. L3: the client's clock (for the multiplayer session)

- **What [D].** `network_game_client_handle_game_update` now has a slack constant, `TICKS_PER_SECOND / 6`. A client whose clock stays more than that behind the host's for a whole second takes the host's time, once. The original corrects only a gap of a second or more, so a client that joins mid-match can play 0.4–1 s behind the host all game (their comment; the insider.15 changelog). Short stalls are still caught up tick by tick and never skipped. This is a client-side change, with nothing new on the wire at network version 29 [D].
- **For us [U].** A PSP that can't hold 30 ticks a second falls behind steadily. Under this rule it would jump forward every second, which may be better or worse than lagging. That depends on how our client keeps up today, and it is the multiplayer session's question. Our compile-only rule tonight leaves it untried.
- **Credit.** Probably BirchWoodGod's, though the port's networking also carries others' work. Check the file's history before taking code. The idea is free.

## 4. Diagnostics worth copying

- **D1. Pool exhaustion in release builds** (`stack_memory_pool.c`). When a pool runs out, the request is dropped and nothing is said: a screen opens without a widget, or a text box draws blank. They now log it, at most once a second. Their insider.16 fix (the menus' pool made 4× bigger, which mended blank browser rows and failed joins) is the kind of bug this catches. For us it is one log line where the pool gives up, useful on ui.map and the HUD [U].
- **D2. A load heartbeat** (`frame_timing.c`, `halo_progress_heartbeat`). Long-running loads bump a counter, so the watchdog can tell a stretch with no frames but a moving counter (a long load) from a hang. For us [U]: a stuck Memory Stick read and a hang look the same to the PSP agent today. Low priority.

## 5. The look list: nothing new

Cut-out (alpha-tested) shaders and bump map alpha, see-through and sky water, tracers and contrails, light gels, Cortana's hologram, glass: none of the code behind these changed since `b6a7a29` [D]. The renderer's changes are the interface drawn at display resolution, a scope mask, a compressed bitmap's swizzle, Custom Edition maps and movies. Their renderer runs the Xbox's combiners on the GPU (`research_vita.md` §5), so for these issues the references are still the decomp's own `rasterizer_xbox_*.c` and the PC reference runs. Their contrail changes (`render/render_contrails.c`) guard against the tick and the render overlapping, which only matters to us if the ME ever runs the two at once.

## 6. Not for us

- **Ragdolls** (`port/linux/game/ragdolls.c`, new; the idea is from jojoxdk's MCC physics mod). A killed character's body is drawn as a ragdoll; only the picture changes. It is a look change, and it adds tick work for every body.
- **The interface on its own layer.** The 3D picture is drawn smaller and scaled up, with the interface at the display's resolution. It belongs to the Vita's dynamic resolution.
- Movies from uncached memory, Custom Edition and MCC maps, Forge, parties and friends, the Halo 3-style menus.

**What we could offer back:** unchanged (`research_vita.md` §6).

---

# Second read (2026-10-20)

A full read of the insider.16 tree, not only what changed since `b6a7a29`: every `(port)` block in the engine's AI, physics, structures, objects, units, effects, memory, sound, render and rasterizer files, the port's own game files (`port/linux/game/`), and the Vita device's split of work across cores. Each find was checked against ChupathingyCE at our pin and against Sierra-III, so that nothing already in `research_vita.md`, this note's first half, `shelf.md` or the levers already tried is listed again.

**Held: needs both go-aheads.** Everything below comes from the pre-release package. Until its author and the maintainer both agree, any of it that is built stays on a branch of its own, behind a keep word that is off by default, and reaches no build branch or release. Grant: BirchWoodGod's own code only. Code he credits to others is idea-only, except OpenCE's, which is CC0.

## The short answer

The second read turned up one exact tick item worth counting first (**S1**, the cache allocator's assertion walk, which our release build still runs). It found one AI idea of ours that their counts make worth sizing (**S2**, perception passing over scenery), and two sound fixes for the audio package (**S8**, **S9**). The rest is rig filler, design references for frames in flight, or things we already have. **The look list still has nothing new** [D].

| # | Idea (their file, function) | Exact? | Worth on b30/c20 [U] | Grant |
|---|---|---|---|---|
| S1 | The cache allocator's integrity walk left out of release builds, and a hole's blocks found along the block list (`memory/lruv_cache.c`: `lruv_cache_verify`, `lruv_block_new`) | Yes (the walk only asserts; the delete order is kept) | **Unknown, possibly ~0.5–1 ms in decal-heavy fights**: count first | The walk's removal repeats our own `poolverify` pattern; the list walk is his code |
| S2 | Perception's refresh passes over childless scenery without reading its datum (built on T1's type table; their count, our idea) | For play (marker stamps, masked already) | Medium if scenery is most of the visits: a share of perception's 2.47 ms | Idea only (their count) |
| S3 | Static scenery skips its node matrices when its inputs haven't changed (`objects/objects.c`, `objects_update`, `HALO_STATIC_SCENERY`) | Yes | ~0 beyond our sleepers (WS, NMS, LEAF) | His |
| S4 | A sound obstruction ray asked again, bit for bit, answered from a memo of its structure test (`physics/collisions.c`, `obstruction_memo`) | Yes | Sub-ms; mostly fidelity: fewer stale muffles under our budget | His |
| S5 | A body not drawn lately isn't relaxed onto the ground (`units/bipeds.c`, `biped_update_dead`, `object_drawn_within`; multiplayer only in theirs) | No (an unseen body keeps its dying pose) | Shelf for the campaign; the multiplayer session's for clients | His |
| S6 | Render binds: the same bitmap on the same stage again in a frame skips the cache lookup; a (group, sequence) → bitmap table per map (`rasterizer/xbox/rasterizer_xbox.c`) | Yes | Filler, only if our draws still go through those binds | His |
| S7 | The structure's visible-surface bit vector walked by counting zeros (`structures/structure_render.c`, `structure_render_dynamic_triangles_from_bitvector`) | Yes | Filler (shadow and light lists only) | His |
| S8 | A sound the cache misses as it starts (impacts, explosions, weapons) waits 250 ms for its load before it is dropped (`sound/sound_manager.c`, `sound_discard_grace_waits`) | Restoration: what the Xbox's disk gave | Fidelity: explosions without a sound | His |
| S9 | A long sound's next three links asked for ahead (`sound/sound_manager.c`, `sound_prefetch_linked_permutations`) | Yes (cache contents only) | Fidelity: dry gaps in music and long dialogue | His |

## S1. The cache allocator's integrity walk (count first)

- **What [D].** `lruv_cache_verify(cache, TRUE)` walks every live block of an lruv cache, two `datum_get`s a block, and asserts the links and page order. Nothing else happens in that walk. ChupathingyCE's release builds evaluate their assertions (`cseries.h`, `HALO_RELEASE`), and our build defines `HALO_RELEASE`, so the walk runs on the PSP. At our pin, `lruv_block_new`, `lruv_block_delete` and `lruv_flush` call it with `TRUE`. Their change drops the walk in release builds, as `memory_pool.c`'s already was. It also finds a hole's blocks along the block list, instead of `lruv_block_new`'s pass over every slot of the block array, and still deletes them in the array's order.
- **Who pays [D].** The decal vertex cache (`rasterizer_xbox_decals.c`: up to 2,048 blocks, one block for each decal made and one freed for each decal that expires), the sound cache (each sound loaded), and the texture cache wherever our path still allocates through it.
- **Why it may matter.** It is the same pattern as `memory_pool_verify`, which the emulator's cache model put at 4.9% of the tick's data misses on b30's beach before b108 turned it off (`poolverify`). E2 found no payout from the allocator's search, but it measured only the sound cache. Decals were never measured.
- **Removal estimate (to make before building).** Per tick on b30's beach and a10: the walks (calls with `TRUE`) times the live blocks, for the decal cache and the sound cache. Also how often a decal's allocation takes the full array pass. As a rough guess [U], ~500 live decals and ~10 allocations and expiries a tick would be ~10 k block visits a tick, ~0.5–1 ms. If the count comes out under ~1% of the tick, stop.
- **Fit.** It costs no memory. It is tick-side, and decals come from the tick's impacts. Suggested keep word: `lruvfast`, opt-in while held.

## S2. Perception passing over scenery (their count, our idea)

- **Their count [D, theirs].** An actor's timeslice refresh tests every object in every cluster it can see, ~350–550 objects on b30's beach. Nearly all of them are scenery, which `actor_perception_refresh_test_object` only stamps with the marker (it walks their children, if any). Their change batches the list walk and prefetches, which pays little on our CPU (`prefetch_map.md`).
- **Our version [U].** With T1's cluster tables (type and child flag per entry, RT1), a childless object of a type perception ignores is passed over without reading its datum. The only difference is the unwritten marker stamp. The state hash already masks stamps (RM1), and an object met again from another cluster still does nothing.
- **Count first.** Per refresh on b30 and c20: the objects visited, by type and by whether they have children. Then this lever's share of perception's 2.47 ms a tick on b30. It depends on RT1, which the collision walks want too.

## S3–S7. Smaller tick and render items

- **S3, static scenery [D].** The node matrices of a scenery object with no animation graph, parent or children are a function of its position, orientation, scale and flags. While those don't change, the recompute is skipped. Our exact sleepers (WT, WS) already take still objects off the walk, and NMS and LEAF were shelved as small, so this is worth ~0 unless WS leaves such scenery awake. A check, not a build.
- **S4, obstruction memo [D].** Their numbers: with the player standing, two thirds of b30's fight rays are the same ray bit for bit. The structure test's answer (a surface met, or the leaves passed through) is kept while the BSP and the breakable surfaces' flags stay as they were. Objects are still tested. Our b106/b107 scheme allows 6 tests a frame and reuses results up to 15 ticks old, which isn't exact. A memo would answer the identical rays fresh and free the budget for the rays that changed. Theirs holds 256 entries of 32 leaves (~40 KB); for us, 32–64 entries. It pairs with T6 (stop at the first hit).
- **S5, unseen bodies [D].** A body relaxing onto the ground costs "5,000 collision tests a death" in their big games. They skip it for bodies not drawn in the last few frames, in multiplayer games only. For the campaign it detracts (a body that dies out of view lies in its animation's pose), so it goes on the shelf. T3 (an exact memo inside the relax) is the exact way. It is the multiplayer session's to consider for clients.
- **S6, render binds [D].** It is only worth anything if our GE path still calls the rasterizer's bitmap lookup per stage and per part. BB found no saving beyond repeats, so it is probably filler. A census line first.
- **S7, bit vector walk [D].** It walks set bits by counting zeros (the Allegrex has `clz`). Our b119 `surfacesort` covers the sort next to it, not this walk. Filler.

## S8–S9. For the audio package

- **S8, a grace period for missed sounds [D].** A sound whose class drops it when it isn't in the cache as it starts (impacts, explosions, weapons) was dropped at the next update, a tick later. The Xbox's hard disk had loaded it by then; a memory card often hasn't. Their fix waits up to 250 ms for the load (their issue #26: explosions with no sound; a throttled harness run dropped 37 in 2 minutes and none at disk speed). We read from the Memory Stick, so the same drops are likely [U]. First, SI's cut reasons should say whether we drop sounds that aren't loaded. Under §4 of `research_vita.md`, what plays feeds back into the game, so it needs the play-identical check. It restores the original, though.
- **S9, link prefetch [D].** Long sounds are chains of ~1.3 s links, and the next link is loaded only once the one before it starts. They ask for the next three ahead, without a reference. It complements SQ's deeper queue (b138) for the dry gaps. Our "sound reader first" (`psp_platform.c`) already covers their urgent-reads-first change.

## Work across cores: what maps to the Media Engine

- **The render worker's record split [D]** (`port/vita/platform/d3d8_gxm.c`, `HALO_RECORD_SPLIT`). The game's thread copies the device state into a per-frame block only when it changes. The worker turns it into shader keys, uniforms and states, because that translation was most of the cost of a record. It is the same shape as M6 #2 (the GE layer on the ME). If the main CPU's share of recording is still large after M6 #2, "copy state only when it changes" is the next step to size [U].
- **The tick on its own core [D]** (`port/linux/game/tick_thread.c`, `render_interpolation.c`, `render_epoch.c`). It is a correctness reference for frames in flight (L5, GEV), not a lever. After each tick, every object's node matrices are copied into one of three buffers and the render reads the published one. The cluster lists are published the same way. A per-slot mark lets the render skip datums the tick is still building, and `switch_bsp` waits for the frame. Their symptom without these: an object the camera follows (a driven vehicle, the Pelican in a cutscene) shook against a still world. **A check for GEV [U]:** does anything on the ME read live node matrices or cluster lists while the next tick runs? Their triple buffers cost memory that we would size before copying the design.
- **The fourth core's helpers [D]:** the mixer, the cache file reads, map decompression, the checkpoint writer and the log writer. They match ME ideas already listed (R2, FM3, LW2).

## Already ours, or not ours

- **Already in Sierra-III:**
  - the portal hull cache (`structure_visibility.c`; ours since build 31);
  - the objects' packed bounding spheres (OpenCE's, after Addison Bair and MrBruh; ours: the sphere copies and CW);
  - the cluster list's tail table (`cluster_partitions.c`): ChupathingyCE already adds at the head and remembers each reference's place;
  - urgent sound reads first.
- **Not for us:**
  - the object header tested before the render epoch (their threads);
  - this file's lookups inline (our RL1: more I-misses);
  - the threat scan's read one past the array (harmless: it reads a zero);
  - trimmed sprites and the dropped alpha-test discards (Vita GPU fill);
  - the light and shadow scissors (GE fill isn't our limit).
- **The look list [D]:** nothing new for cut-out shaders and bump alpha, water, tracers, light gels, Cortana or glass. One small check: Bungie's cutscene scripts leave `time_code_show` on in several cinematics, which draws the tick count in a corner. They hide it in release builds. It shows on ours only if `render_debug_string` draws.
- **For the multiplayer session:** the target-name search skipped when no living biped is within 20 units (`game_engine.c`, `find_closest_player_candidate_near`; ~5 ms a frame on the Vita in 32-player games, theirs), and S5.

**Credit, if any is taken:** BirchWoodGod (halo-ce-vita), under his grant and held as above. The bounding-sphere copy is OpenCE's (CC0).

# A relay (2026-10-21)

**Hearsay, relayed by Sheepdog01; not checked by us:** BirchWoodGod says his playtesters report heavy battery drain, and slowdowns when playing at 16:9 that 4:3 doesn't show. Sheepdog01 asked for anything that could help. What we found, read at his public `b6a7a295`, is in `research_vita.md` §6, "Widescreen and battery". The report itself stays here, not there: `research_vita.md` goes out with the public release (`tools/public_export.py`, RULES), and this note doesn't.
