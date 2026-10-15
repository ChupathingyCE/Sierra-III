# Research: Halo CE for PS Vita, read for Sierra-III

A tour of BirchWoodGod's [halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita) (commit `61debf0`, 2026-10-14) for ideas Sierra-III could use, and for what we could offer back. It runs the full campaign natively on the Vita, with checkpoints, movies, system link and network co-op. It is built on halo-ce-universal (OpenCE's lineage), so its engine is a close cousin of ChupathingyCE's.

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. The Vita port's own numbers are from Vita hardware, its Linux harness or callgrind's Cortex-A9 model, and are marked as theirs.

**Licence: read before borrowing code.** The Vita port is **GPL-3.0-only**: its generated tag layouts come from Invader's GPL definitions, and the licence covers the whole port. The decomp and OpenCE underneath stay CC0. Sierra-III has no licence chosen yet (`README.md`).

**Permission (Sheepdog01, 2026-10-14):** BirchWoodGod, the port's author, told Sheepdog01 on Discord that Sierra-III may take from it as it pleases. That covers the code he wrote. It can't cover what isn't his to grant:
- `port/linux/src/tag_layouts.h` and anything else generated from Invader (GPL-3, SnowyMouse's);
- code he credits to others, such as iamhaller's networking or Bruno Santana's settings work (ask them, or take the idea only);
- upstream decomp and OpenCE code, which is CC0 anyway.

Anything taken still names the Vita port in its commit and review. A written line of the grant (a Discord message saved in the lab's notes, or a comment on his repo) is worth keeping, so the permission outlives the chat.

**Where to read it.** `port/vita/README.md` (settings, the profiles' costs, debug switches), `ROADMAP.md` and the main README's Performance section. Engine changes carry `/* port: */` or `/* (port) */` comments under `#ifdef HALO_LINUX` (the Vita builds the Linux platform layer); the speed ones name their map and their measurement.

## The short answer

**This is the most useful port we've read.** Its CPU bottleneck is the same as ours (The Silent Cartographer's beach, where the processor is the limit), and it attacked the campaign AI with **exact changes, each with a verify mode** that runs the original beside it and compares. That's our own method. None of them is in ChupathingyCE at our pin (`v0.7.4e`) [D]. Eight are worth taking (§1). Its behaviour-changing settings make a well-tested A/B shelf (§3), with one warning about sound (§4).

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

## 3. The A/B shelf (behaviour changes, Sheepdog01's call)

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
