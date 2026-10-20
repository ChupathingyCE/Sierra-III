# Research: Halo CE for PS Vita, read for Sierra-III

A tour of BirchWoodGod's [halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita) (commit `61debf0`, 2026-10-14) for ideas Sierra-III could use, and for what we could offer back. It runs the full campaign natively on the Vita, with checkpoints, movies, system link and network co-op. It is built on halo-ce-universal (OpenCE's lineage), so its engine is a close cousin of ChupathingyCE's.

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. The Vita port's own numbers are from Vita hardware, its Linux harness or callgrind's Cortex-A9 model, and are marked as theirs.

**Licence: read before borrowing code.** The Vita port is **GPL-3.0-only**: its generated tag layouts come from Invader's GPL definitions, and the licence covers the whole port. The decomp and OpenCE underneath stay CC0. Sierra-III's own code is MIT (`LICENSE`), with the carve-outs it names.

**Permission (2026-10-14):** BirchWoodGod, the port's author, told the maintainer (Sheepdog-01) on Discord that Sierra-III may take from it as it pleases. That covers the code he wrote. It can't cover what isn't his to grant:
- `port/linux/src/tag_layouts.h` and anything else generated from Invader (GPL-3, SnowyMouse's);
- code he credits to others, such as iamhaller's networking or Bruno Santana's settings work (ask them, or take the idea only);
- upstream decomp and OpenCE code, which is CC0 anyway.

Anything taken still names the Vita port in its commit and review. The exchange is kept in the project's private research notes. His words: "Dude steal away I want to try your port sometime!"

**Where to read it.** `port/vita/README.md` (settings, the profiles' costs, debug switches), `ROADMAP.md` and the main README's Performance section. Engine changes carry `/* port: */` or `/* (port) */` comments under `#ifdef HALO_LINUX` (the Vita builds the Linux platform layer); the speed ones name their map and their measurement.

## The short answer

**This is the most useful port we've read.** Its CPU bottleneck is the same as ours (The Silent Cartographer's beach, where the processor is the limit), and it attacked the campaign AI with **exact changes, each with a verify mode** that runs the original beside it and compares. That's our own method. None of them is in ChupathingyCE at our pin (`v0.7.4e`) [D]. Eight are worth taking (§1). Its behaviour-changing settings make a well-tested A/B shelf (§3), with one warning about sound (§4). A second pass (§7–§11) found seven more tick speed-ups, three render-side ones, four engine bugs our engine shares, and test methods that may explain our cross-build divergence (#25).

## 1. Worth taking (exact, with their verify modes)

| # | Change (their file) | Their measurement | Verify switch |
|---|---|---|---|
| V1 | `objects_fix_for_deleted_object` visits only the types with a handler (`objects.c`, `object_types.c`) | ~1000 objects walked per projectile hit on b30's beach | asserts builds walk all and check |
| V2 | `path_state_new` clears the header only (`ai/path.c`) | the clear was **a quarter of each firing position selection's estimated cycles** on b30's beach; the state is 82 KB | `HALO_AI_PATH_STATE_VERIFY` |
| V3 | A cache of surfaces' path edges (`ai/path.c`, `path_surface_edges`) | ~140 surfaces expanded per firing position selection on b30 (680 in a50's pursuits); an LRU of 512 holds 99.7% of b30's | `HALO_AI_PATH_STATE_VERIFY` |
| V4 | A firing position's line of sight stops once its answer can't change the choice (`ai/ai.c`, `ai_test_line_of_sight_unless_moot`) | | `HALO_AI_PATH_STATE_VERIFY` |
| V5 | The nearby firing positions test searches only when a position is within 4 units (`actor_firing_position.c`) | each search cleared an 8 KB hash table and flooded the surfaces within 4 units, 33 a pursuit selection on c10 (by their count) | `HALO_AI_PATH_STATE_VERIFY` |
| V6 | Path smoothing tests its steps from the last back (`ai/path_smoothing.c`) | | `HALO_AI_PATH_STATE_VERIFY` |
| V7 | Perception computes "reachable and unopposable" once a refresh, and looks a header up once (`ai/actor_perception.c`) | | `HALO_AI_PERCEPTION_VERIFY` |
| V8 | `scenario_get_current`'s answers kept per cluster, rebuilt when the BSP changes (`scenario/wind.c`) | ~600 calls a tick on b30's fight (point physics, per particle); ~0.26 M A9 cycles a tick | `HALO_SCENARIO_CURRENT_VERIFY` |

### V1: deleted objects

- **What.** When an object is deleted (every projectile that hits), `objects_fix_for_deleted_object` walks *every* object in the game to clear references to it. Only units and projectiles have a handler, and the other reference it clears (the umbrella shield) is always NONE: `object_new` sets it so and nothing in the game changes it [their reading].
- **Their fix.** A type mask of the types with a handler, computed once from the constant definitions; the iterator visits only those types, in the same order.
- **Why it pays for us.** ~1000 objects is b30's beach. Each visit reads the object's header and datum: on our 16 KB cache, that walk evicts the tick's working set for every hit.
- **Sierra-III status.** Not done [D]. Check the umbrella-shield claim in ChupathingyCE's `damage.c` before taking it.

### V2: the path search's clear (our H2, now measured)

`research_halo64.md` H2 found this in Halo 64. The Vita port measured it **on our map**: on b30's beach the clear was a quarter of each firing position selection's estimated cycles, and it flushed as much of the cache. Its implementation keeps the whole clear when debug storage is passed (`path_state_find` copies the whole state there), sets `heap_count = 0`, and checks every search against one from a fully cleared state. That's the design to copy.

### V3: the path edges cache

- **What.** A search builds each surface's edges from the collision BSP (the surface, then an edge, two vertices and a neighbour's flags for each edge: reads that mostly miss). The answer is map data the game never changes, so they cache it: 128 sets of 4 ways, least recently used replaced, emptied when the BSP, map or game state is replaced. 68 KB of BSS. Surfaces with more edges than a way holds are built each time.
- **For us.** 68 KB is a real cost on a 5.6 MB heap. The first step is counting expansions and hit rates per map in the emulator at 32 and 64 sets. Exact by construction; their verify builds the edges the original way and compares.

### V4 to V7: the firing position and perception group

These are smaller changes inside the same hot code: `actor_firing_position.c`, path smoothing and perception, where b30's AI spends its time. Each is exact and each has a verify mode. Take them together, behind one keep word, with one check word.

### V8: the current at a point

- **What.** The point physics asks `scenario_get_current` for each particle's current every frame. Each call does a dozen tag reads and two or three tag lookups whose answers depend only on the cluster.
- **Their fix.** A table by cluster (weather palette entry, fog region or plane), rebuilt at a BSP change. The same comparisons pick the same answers.
- **Sierra-III status.** Not done [D]. Its size depends on how many particles our builds keep; count the calls on b30 first.

## 2. Already in Sierra-III

| Vita's change | Ours |
|---|---|
| Light cluster cache (`object_lights.c`, `HALO_LIGHT_CLUSTER_CACHE`) | The relink memo (`relinkmemo`, opt-in): the same idea, worth ~0 on c20's machines (`review_b121_machines.md`). They found ~250 light reconnects a tick in a 15-bot Blood Gulch; our campaign number is unmeasured |
| Vertex and edge tests in one walk round the surface (`collision_bsp.c`) | `psp_surface_test_sphere` does the same |
| Packed bounding spheres for the collision walks | The object sphere copies (`psp_capacity.h`) |
| Sound obstruction rays reused | Reused per tick since b107 (`audio_fidelity.md`) |
| Prefetch hints in perception (`__builtin_prefetch`; ~1,600 misses a tick in the props walk) | `prefetch_map.md`: on the PSP a fill blocks every load, so hints gain little without reshaping the code. The *targets* they name (the props walk, the cluster object walk) are worth checking against our map |
| `tag_loaded` answers memoized | `verify_tag_reference` answered from the reference (b66) |

## 3. The A/B shelf (behaviour changes, the maintainer's call)

Their settings panel sorts these by cost, and the profiles have been played by real players: useful evidence of what passes a feel test.

| Setting | What it does | Their default |
|---|---|---|
| AI think rate: Adaptive | Actors within 8 units of a player think every tick, within 25 every second tick, farther every third; movement, aim and firing stay every tick | On in every profile |
| Sound occlusion | A sound's obstruction ray re-cast every 3rd tick while neither it nor the camera moved a quarter unit (b30: ~40 voices' rays, 3.7–4.2 ms of the Vita's tick) | Every 3rd |
| Sound updates | The sound manager every 2nd frame | Every 2nd |
| Scenery updates, Object lighting | Static props a quarter as often; object lighting every third | Quarter, Third |
| Particles: Reduced | Fewer made (big bursts cut, short lives shorter, too-small ones not made) | Reduced |
| Decal cache window | A new decal takes the least recently drawn of the 64 slots after the last one made, instead of searching all 2048 | On (64) |
| Tiny decals | Decals under 2 pixels across not made | Being tried |
| Tick overlap | When a tick takes over 33 ms, one tick a frame while the game stays within a fifth of two ticks' speed | Off in the campaign |

Our `aihalf` already is the simple form of Adaptive. Their distance tiers are the version to try next, if AI rate stays on the table.

## 4. A warning for the audio package

The Vita port tried capping positional voices and found that **what plays feeds back into the game**: a b30 run with fewer voices saw its units and actors part from the original's after ~40 seconds (`sound/sound_preferences.c`). So it keeps the original 46 by default.

For us [U]: anything in the audio package that changes which sounds the game thinks are playing (a channel count, a priority, perhaps a sound the cache couldn't load in time) can change play. Mixing, decoding and muffling filters are safe; the sound manager's choices are not. Our seeded runs would catch it, so the audio package's builds need the same play-identical check as the speed work.

## 5. Not transferable

- The renderer: a Direct3D-to-GXM device that compiles the Xbox's register combiners into Cg shaders on the device. The PSP's GE has no programmable stages, so its combiner work can't carry over; Halo 64's offline stage evaluation (`research_halo64.md`) is the closer model for us.
- The tick on its own core, overlapping the render without locks ("an object may be drawn mid-update"). Our analogue is the ME roadmap; their experience says drawing a half-updated object looks acceptable in play, which is evidence for phase 3, not a design we can copy.
- Dynamic resolution, frame interpolation, the fourth core: Vita hardware.

## 6. What we could offer back

The Vita port is processor-bound on the beach too, and it lacks these [D]:

1. **Exact sleeping.** Our machine sleep (a machine whose update would leave it byte-identical sleeps) and ground-weapon sleep, with their functions kept. Their nearest is "Scenery updates: Quarter", which isn't exact. On our b30 beach, ground weapons cost 5.1 ms a tick before b124 [M].
2. **The `weapon_new` bug** behind it: `tracked_object_index` is never set, so a new weapon "tracks" object slot 0. Their engine has it too (`items/weapons.c`). It matters to anything that reasons about a ground weapon's state.
3. **Surface dedup in the sphere walk.** Our `psp_surface_seen_before` skips a surface already walked from another leaf, for walks past three leaves; their walk has no dedup [D].
4. Halo 64's finds apply to them as well: the prism facing test first (H3) and the `add_feature` hash (H4).

Not needed on their side: H1's `aiming_change` fix. ARM converts a NaN to 0, which gives the same byte as x86, so only MIPS ports diverge there.

Sierra-III is private, so an offer would be ideas and short snippets, not links, and it would say plainly that the work is Claude-assisted. Their GPL-3 doesn't stop us sending ideas or code of our own; it only governs code we take from them.

---

# Second pass (2026-10-14, night)

A sweep of the rest of the Vita engine in four parts: tick speed-ups, render-side CPU work, bug fixes our engine shares, and test tooling. Each find was checked against ChupathingyCE `v0.7.4e` and Sierra-III; none of the speed-ups below is in ChupathingyCE [D].

## 7. More tick speed-ups

| # | Change (their file) | Exact? | Their measurement | Verify switch | Sierra-III |
|---|---|---|---|---|---|
| T1 | A cluster's object list kept with each object's type and child flag, so a collision walk passes over the types it doesn't test before reading their datums (`port/linux/game/cluster_object_types.c`, `physics/collisions.c`) | Exact | Collision queries walk **~6,000 objects a tick** on b30's beach (`cluster_partitions.c`); a line of sight walks ~65 and reads the datum of 2–3 | `HALO_AI_LINE_OF_SIGHT_VERIFY` | **Partly:** the sphere-copy reject is ours (FIXES_107 `collisions.c#spheres`); the type table is not |
| T2 | A particle's leaf kept with its three nearest split planes, so a point that hasn't crossed them skips the BSP walk (`port/linux/game/point_leaf_cache.c`) | Exact (a 16× float margin) | ~440 walks a tick on b30's fight, ~0.29 M A9 cycles | `HALO_POINT_LEAF_CACHE_VERIFY` | No |
| T3 | A relaxing body's collision tests remembered within one relax call (`units/biped_limp_noodle.c`) | Exact (their claim) | ~60% of vector and ~35% of sphere tests repeat; ~70 vector tests a body a tick for a few dozen ticks after each death | None | No (our corpse sleep starts after the body settles) |
| T4 | The BSP vector walk as a loop, with both children prefetched (`physics/collision_bsp.c`) | Exact | ~40 nodes a line of sight on b30, each a miss | None | No; the prefetch half needs `cache 0x1E` and gains little on its own (`prefetch_map.md`) |
| T5 | A cluster's datum list reused while no partition list changed (`structures/cluster_partitions.c`) | Exact (their claim) | Banshee mass points walk the same clusters ~110 times a tick | None | No; the relink memo covers lookups, not list walks |
| T6 | Sound obstruction rays stop at the first hit (`collision_test_vector_obstructed`) | Exact (obstruction only asks whether something is in the way) | | `HALO_SOUND_OBSTRUCTION_VERIFY` | No |
| T7 | `unit_refresh_illumination` only under its debug flag (`units/units.c`) | Exact for play: nothing but a debug print reads the two fields (checked in ChupathingyCE [D]); our state hash would need them masked | 8% of a 24-player client's tick; ~2 a tick in the campaign | | No |

T1 is the biggest. Our b122 perception rays already walk these lists on the ME, so the type table would serve both paths.

## 8. Render-side CPU work

| # | Change (their file) | Look | Sierra-III | For us |
|---|---|---|---|---|
| R1 | A model's parts walked once instead of once a pass (solid, decal, transparent), keeping up to 64 for the later passes (`models/models.c`) | Exact | No | **High**: fewer lookups per model per frame, and a step towards baked part lists (X2) |
| R2 | A part with the same shader, permutation and vertex type as the last reuses its states; per-model values worked out once (`rasterizer_xbox_models.c`) | Exact | Partly: `statememo` (b114) showed no gain on test 3 | Medium |
| R3 | Decals batched: consecutive decals with the same blend and bitmap in one draw, colour and fade moved to the vertices (`rasterizer_xbox_decals.c`, `HALO_DECAL_COLOR_STREAM`) | Exact | No: `psp_ge_decal` is still one call a decal | **High**: they sent 790 decals as 25 draws, and our b30 display list has filled with decals |
| R4 | Objects too small to cast a shadow skip shadow lighting (`render_objects.c`) | Exact | No | Low, with blob shadows on |
| R5 | Model LOD lowered only until no transparent part (visor, canopy) is lost, and never for a model holding the camera (their issue #29, a10's cryotube) | Guard | **Check:** our 272/480 LOD scale has no such guard | A possible look bug of ours to test |
| — | Tiny objects dropped before lighting, lighting refreshed every third time, scene lights capped at 4, particles drawn by a divisor | Look changes | No | A/B shelf |

## 9. Bugs our engine shares

1. **The weapon interface state is uninitialised** (`items/weapons.c`, `weapon_build_weapon_interface_state`). Every caller keeps it on the stack unset, and the function fills only the weapon's own magazines. A one-magazine weapon's second magazine is stack garbage, which can set off the HUD's secondary warnings ("NO FUEL" beside "NO AMMO", the empty-secondary sound) and feed the swap prompt's depleted test. Every campaign level has one-magazine weapons.
   - The fix: zero the state, and fill no more magazines than the HUD keeps.
   - The fix is DamnationCE's (xshxdex98, CC0, `488bd83e`), so it's free to take whatever the licence.
   - Still in ChupathingyCE `v0.7.4e` and in Sierra-III [D]. What shows depends on our own stack, so it repeats within a build and may change between builds [U].
   - Kind: a fix most likely restoring the Xbox's HUD [U]; it touches nothing networked.
2. **Looping sounds keep the old BSP's cluster after a switch** (`sound/sound_manager.c`, `game_sound.c`). It only bites when the sound manager skips frames; their harness halted on the cluster assertion in half of a10's runs. It can't happen in ChupathingyCE today, which refreshes every frame. **A guard to take before any "sound updates less often" lever.**
3. **An uninitialised evasion result** (`ai/path_structure_bsp.c`), an original Xbox bug the decomp keeps on purpose. With no ground surface, the result's point comes back unset and `actor_move_try_evasion_vector` reads its `z`. On MIPS a garbage or NaN `z` fails both comparisons, so the evasion point is accepted where x86 may reject it [U]. Not seen in play yet; a question for the author.
4. **`game_sound_restore` reads a looping sound through NULL** after a bad game state load (`sound/game_sound.c`). A cheap guard that would also surface any bug in our compact checkpoint writer.

Not ours: the cloaked Flood slowdown in The Maw (#33) is the Vita GPU's screen copy per cloaked unit; we draw camouflage as a faded model. The dropped-weapon and a10 `switch_bsp` crashes come from their tick and render overlapping; they become relevant only if ME phase 3 ever runs render and tick at once.

## 10. Test methods worth copying

1. **Repeatable runs across builds.** This may answer our open #25: b30 runs from different builds part around tick 1,320, and the object mark counter already differs from tick 1,204 (`b113_candidates.md`). Their `port/linux/game/tick_hash.c` names the causes they had to remove:
   - **uninitialised stack bytes copied into game state:** they build with `-ftrivial-auto-var-init=zero`. A different build moves the stack, which fits runs that part only *across* builds [U]. Our psp-gcc 15.2 has the flag;
   - **object marker stamps** (`magic_number`): every marker session stamps objects, and the number of sessions depends on render-side work and the sound cache. Their hash leaves the stamps out (`HALO_TICK_HASH_MASK`);
   - **every run starts from the same saved game**, since the state keeps some fields from the last run.

   A first test: one b30 A/B pair built with `PSP_CFLAGS=-ftrivial-auto-var-init=zero`, compared with the stamps masked.
2. **A draw hash** (`port/vita/null/vgxm_null.c`, `HALO_DRAW_HASH`). Every draw folded into a per-frame hash of what the GPU is given: states, uniform bytes, texture words and the vertex bytes actually read, never pointers. One hash per render target, so reordering work between targets still compares. Our equivalent would hash the GE list with pointers replaced by the bytes they point to, one hash per pass. It turns render speed-ups into proofs instead of side-by-sides.
3. **A level tour** (`port/linux/game/shader_tour.c`, `HALO_SHADER_TOUR`). A deathless player teleported to each cutscene flag in turn, every few seconds, structure after structure; scripts and encounters fire as usual. As a keep word it would give our strict interpreter and cache model whole levels instead of a minute or two.
4. **Verify modes always print their counts** ("N checked, M different"), so a run that checked nothing can't pass for one that found nothing. Some of our check words do; all should.
5. **An always-on frame-hitch line**: any frame over 100 ms is logged with its parts (the game thread's steps, lock waits, the slowest file call by name). On the PSP it would name Memory Stick stalls as they happen.
6. **Stress switches** (`HALO_STRESS_PARTICLES`, `_DECALS`, `_LIGHTS`): n short-lived effects spawned a frame around the player. For us, a worst-case frame and a pool-limit test on demand.

## 11. Questions for the author

1. **Cross-build determinism:** was `-ftrivial-auto-var-init=zero` enough to make runs from different builds agree, or what else did it take?
2. **The path edges cache:** what hit rates did smaller caches give? We have a 5.6 MB heap, so 32 or 64 sets would suit us better than 128.
3. **The ~270 of ~360 draws drawn one part at a time:** where was that measured, and did merging model parts ever get tried?
4. **T3 and T5 have no verify modes:** how were they checked?
5. **The weapon interface fix:** did the HUD visibly change, and does anyone know what the Xbox showed?
6. **The evasion bug:** has it ever been seen in a campaign level?
7. **Would they like ours?** Exact machine and ground-weapon sleeping, the `tracked_object_index` fix, and surface dedup in the sphere walk (§6).

## 12. Since `61debf0` (read 2026-10-19, at `b6a7a29`)

**No new speed-ups.** [D] Main has six commits since `61debf0`, all README (the v1.1.0 video, a donation badge added and removed, PC players pointed to OpenCE); no branch has moved since 10-09. Every `perf/*` branch was already in `61debf0` except `perf/coop-host` (`05bcacf8`: a co-op host re-reads only the device groups `devices.c` marks as changed). That one is co-op only, so it isn't ours.

A second pass over the `61debf0` tree found three switches the first read left out:

| Switch (their file) | What it does | Exact? | For us |
|---|---|---|---|
| `HALO_SKY_LATE` (`render/render.c`, on by default) | The sky is drawn after the opaque scene, only on pixels still at the cleared depth. On the Vita's tile renderer, a blended layer drawn first is paid in full (one to two screens of fragments a frame). | Yes (every pixel ends the same, by their argument) | **Leave.** Our GE already draws the sky's see-through parts first in the end-of-frame transparent list, and its solid parts at the far plane without writing depth (`psp_ge.c`). The GE isn't a tile renderer, and on b136's ladder the CPU waits 0.3–0.4 ms a frame for it on a30, b30 and d20 [M]. Payout is ~0 on b30/a30/d20 until frames in flight (#4) make the GE the limit [U]. |
| `HALO_AI_PERCEPTION_LOD=n` (`ai/actor_perception.c`) | Actors more than n units from every player refresh their props' line of sight every other tick. | No (AI behaviour) | **Leave.** He retired it for Bruno Santana's think rate (§3). Thinking less often is open to experiment here, but it ships only after an A/B (CLAUDE.md), and §3 already holds the better version. |
| `HALO_DECAL_STACK_CAP=n` (`rasterizer_xbox_decals.c`, opt-in) | At most n decals drawn in one 0.15-unit cell; older ones under them are left out (a10's knot: ~800 quads, 2.4 screens of blended pixels). | No (an old mark that showed through goes) | **Shelf, the maintainer's call.** It detracts. GE fill only matters in decal piles, and the GE isn't the limit (above). |

**Credit, if any is taken:** all three are BirchWoodGod's code under his grant (header), except the think rate, which is Bruno Santana's (idea only).

**What we could offer back:** unchanged (§6, and `docs/for_other_ports/`). His crossplay is at network version 29, the same as ours (OpenCE build-206, ChupathingyCE 0.7.5b). That matters only once multiplayer is unshelved.

## 13. Addendum (2026-10-20): insider.16

`1.1.1-insider.16` (`82a9f97e`, a pre-release developer package) is read in `research_vita_insider16.md`. Most of it is multiplayer, menus and Forge. One tick idea is worth a count: dark attached lights left off the map (L1), against `object_connect_lights`' 2.57 ms a tick on b30. There is a client clock fix for the multiplayer session (L3), and nothing new for the look list.
