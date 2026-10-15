# Review: the research levers, first batch (`rv2`, `rv1`, `rv4`, `stabbed`)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). Same shape as the other reviews: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Four branches stacked on `w125` (`dce92ba`, b125's code): `rv2` → `rv1` → `rv4` → `stabbed`. All of it lives in one new fixup group, `FIXES_126` in `psp_fixups.py`, plus one log line in `psp_platform.c`. The first three are exact speed-ups taken from BirchWoodGod's [halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita) (used with his permission; README, Credits); the fourth is a fix.

| Change | Kind | Default | Words |
|---|---|---|---|
| 1. RV2: a path search's state cleared by its header alone | exact speed-up | **on** | `pathclearfull` (off), `pathclearpoison` (check) |
| 2. RV1: a deleted object's fix-up walks only the types with a handler | exact speed-up | **on** | `deletedwalkall` (off), `deletedcheck` (check) |
| 3. RV4, first part: the nearby test searches when needed; smoothing scans from the last step | exact speed-up | **on** | `aipathold` (off), `aipathcheck` (check) |
| 4. The sound cache's "stabbed" dump file left out | fix | **on** | `stabbedfile` (the dump as before) |

**How exactness is judged** (as since b117): the game played hands off with `fixedticks fixedseed statecrc` on d20, a50 and c20, the lever on against its off word, one build. The **play hash** (every object's kind, place, motion, facing, health, parent, animation) and **both random seeds** must match at every tick. The whole-state hash may differ only in allocations already known to follow the emulator's speed (the render and collision marks in `objects`, looping sounds, particles, contrails, first-person weapons); each run's last checkpoint, cut by the wall, is left out.

---

## 1. RV2: the path search's clear (`ai/path.c`)

**What.** `path_state_new` cleared the whole search state, 82 KB (1,024 nodes, the heap, an 8 KB hash table), before every AI path search. Now it clears the header (132 bytes) and `heap_count`, and each node is cleared as the search makes it.

**Why it's exact.** Every caller runs one `path_state_find` on a fresh state, and that search sets up what it reads: `path_state_reset` fills the hash table and starts the counts; a node is only read once made (`node_index < node_count`), and a heap entry only below `heap_count`. So every byte a search reads holds what the whole clear left. With debug storage (never on the PSP since b107) the whole clear stays, as the search copies all of it there.

**Why it pays.** On b30's beach the clear was a quarter of each firing-position selection's cycles (the Vita port's measurement on our map), and an 82 KB sweep evicts the 16 KB data cache five times over.

**Proven** [E] (lab 38695367894, `rv2exact`): the play hash and both seeds identical at every tick on d20 (14,971 ticks), a50 (24,688) and c20 (18,781). `pathclearpoison` fills the uncleared part with `0xA5` instead of leaving it, so a search that read anything it hadn't written would play differently: it plays the same, at every tick, on all three.

## 2. RV1: deleted objects (`objects/objects.c`)

**What.** Each deleted object (every projectile that hits) made `objects_fix_for_deleted_object` walk every object in the game, ~1,000 on b30's beach, to clear references to it. Now the walk takes only the types whose definition handles a deleted object, worked out once from the constant definitions: mask `0x23`, bipeds, vehicles and projectiles.

**Why it's exact.** The walk does two things per object: the type's handler (only units and projectiles have one), and clearing an umbrella shield that names the deleted object. In ChupathingyCE that field is set to NONE by `object_new` and only ever read (`damage.c`), so it never names anything [D]. The handlers run on the same objects in the same order.

**Proven** [E] (lab 38695711199, `rv1exact`): play hash and seeds identical at every tick on d20 (15,736), a50 (26,164), c20 (19,968). `deletedcheck` walks everything as before and logs any skipped object whose umbrella shield named the deleted one: none on b30's beach (the b113 route) or a50.

## 3. RV4, first part (`ai/actor_firing_position.c`, `ai/path_smoothing.c`)

**V5, the nearby firing positions test.** A ground actor's 4-unit path search ran before the walk over its encounter's positions, though only a position within 4 units ever asks it. It's now made when the walk first finds one; with none, never. Same search, same questions, same answer. It changes only the AI profile's path meters, which only the debug display reads (and which live outside the game state).

**V6, path smoothing.** Each smoothing pass tested every step from the first, but keeps only the run of colliding steps that reaches the last one (its first step and that step's edge). Scanning from the last step back finds that run as it ends and never tests the steps before it. The pill test reads the level alone and nothing after the scan reads its full result, so each pass smooths as before.

**Proven** [E] (lab 38696990799, `rv4exact`): play hash and seeds identical at every tick on d20 (14,761), a50 (24,891), c20 (18,628) against `aipathold`. `aipathcheck` scans both ways and compares: 0 of 4,000 smoothing passes differed on b30's beach, 0 of 4,000 on a50.

**Not yet:** V4 (a firing position's line of sight stops once its answer can't change the choice) and V7 (perception's per-refresh facts). V4 changes `ai_test_line_of_sight`, which the Media Engine's ray code also wraps; its worth is to be sized from the cache-model run before it's built.

## 4. The stabbed dump (`cache/xbox_sound_cache.c`)

**What.** When a sound finds no room, the game wrote the sound cache's whole layout to `d:\stabbed.txt` mid-game, a file for Bungie's developers. A PSP build has no reader for it; the port's own log line (b113's) already names the sound, the longest run the cache could clear and what playing sounds held. It's now left out; `stabbedfile` writes it as before, with a log line on each side.

**Why now.** d20 played hands off in the emulator (w125, no `statecrc`) stops right after "SOUND CACHE BLOWN" at game time 482, log and heartbeat together (lab 38688506641). Batch `stabbed126` tells whether the stop is inside the dump. *Results to be added.*

## Worth

Batch `levers126cache`: b30's beach (b113 route) and a50's opening in the cache models, all four on against all off, per function. *Results to be added.* A PSP bench follows once that says the gain is worth the PSP's time.
