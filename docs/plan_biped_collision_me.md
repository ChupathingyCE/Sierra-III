# Plan: biped collision and the Media Engine

A plan, not a build: what each biped's physics reads and writes, which part can leave the main CPU without changing play, and in what order to try it. Labels: [M] measured on a PSP, [E] emulator or cache models, [D] read in code or data, [U] inference.

Branch `lititems123` (from `w122`). The recon instrument `pillreach` (`767feb7`, `PSP_DEBUG_TOOLS=1` builds only) and its runs (batch `lititems123c`) are this plan's.

## The verdict

**The batch recipe can't take a biped's whole physics exactly, but it can take the level walk's data off the main CPU.**

- **The whole physics can't be batched across bipeds.** Its inputs are final only inside the biped's own update. Its object part reads other objects that earlier objects' updates have just moved. Batching it reorders reads and changes play [D].
- **The level part is a pure function** of the sphere, the level's collision BSP and the breakable surfaces' flags [D]. The walk's order is fixed by the tree, not by the sphere (children[0] before children[1], always) [D]. So a walk over any *pruned copy* of the tree gives the same answer as long as it never needs a node the copy left out.
- **The plan:** the ME builds, for each biped, a compact copy of the level around it (Ericson's linearization cache), ahead of time, while the main CPU runs the AI. The CPU's sweep then walks that copy with the same code. If the walk ever needs a node the copy left out, it stops and walks the level as before. Exact by construction; a check word compares every answer.
- **Bipeds barely move in a tick, so a copy lasts** (`pillreach`, lab 38616332979) [E]. 94% of a50's sweeps and 81% of b30's move 0.025 wu a tick or less; all but 0.1% move 0.1 or less. A copy 0.25 wu wider than the pill at rest needs rebuilding on 1.4–4.9% of sweeps: 0.4–0.6 builds a tick, against 12–30 sweeps.
- **But a50's copies are large.** A walk of the wider sphere reaches 215+ leaves on a50 (and fills the walk's 256-leaf list on 73% of builds), against the ~47 a sweep reaches. b30's copies have ~21 leaves, c20's ~3.
- **What it buys is data misses, not instructions.** a50's level walk is ~2.2 ms a tick in the cache models' estimate, of which ~0.78 ms is data misses [E]. The copy might take a third to two thirds of those: **~0.2–0.5 ms a tick on a50, ~0.2–0.4 on b30's beach, little on c20** [U]. That's in the estimate's units; the rig's are larger (below).
- **The ME's part is the builds.** At 0.4–0.6 a tick they are cheap on b30 and c20, which could build on the main CPU. On a50 each build walks ~5 times a sweep's leaves, ~0.5 builds a tick: the ME takes that cost off [U].
- **First steps need no ME:** count whether the baked BSP's node order already serves the walk (a bake-side count), then build the copy on the main CPU to price the replay in the cache models, and try a narrower margin (0.1 wu) for a50's size.

## 1. Where the time goes

**The rig** (PSP-3000, `PSP_TICK_STEPS=1` ladder, lab 38472506973; `review_b121_bipeds.md`) [M]:

| Map | Tick | Bipeds | Biped physics alone | A physics call |
|---|---:|---:|---:|---:|
| b30 | ~57 ms | 26.3 ms | | 290 µs |
| a50 | ~31 ms | 20.2 ms | 8.7 ms | 668 µs |
| c20 | | 18.2 ms | | 143 µs |

**The cache models, a50, 60 s hands off** (build 121's walk, `c17bc51`, lab 38543568066, `a50-cache-on`), tick phase, per tick, at one cycle an instruction plus 72 a miss [E]:

| Function | Fetches | Data misses | Instr. misses | Est. ms a tick |
|---|---:|---:|---:|---:|
| `psp_bsp3d_test_sphere` (3D walk, leaves, surface tests inlined) | 431 M | 3.03 M | 0.59 M | 1.16 |
| `psp_bsp2d_test_sphere` (2D walks) | 270 M | 2.64 M | 0.32 M | 0.81 |
| `collision_bsp_test_sphere` and `.cold` | 50 M | 0.82 M | 0.64 M | 0.26 |
| **The level walk** | **751 M** (1.25 ms) | **6.5 M** (0.78 ms) | **1.55 M** (0.19 ms) | **2.2** |
| `collision_get_features_in_sphere` (object part's loop) | 45 M | 0.59 M | 0.31 M | 0.18 |
| `collision_bsp_test_vector_recursive` (rays: projectiles, AI) | 156 M | 2.18 M | 0.31 M | 0.56 |

- The walk's data misses come from the tag window (the BSP's nodes, planes, leaves, references, surfaces, edges and vertices: a node is 12 bytes, its plane 16 bytes in another array, a surface's edges 24 bytes each, all read by index) and from `psp_surfaces_seen`, the dedup table (0.49 M) [E].
- About 35% of the walk's estimated time is data misses; ~55% is instructions [E].
- a50's sweeps by speed (the `bipeds: pill sweeps` line, same runs): after the opening, ~70% "slow" (under 0.01 wu a tick horizontally), ~9% still, ~21% moving. Still and slow sweeps cost ~40% of a moving one in the emulator's time; the walk dominates either way [E].
- **`pillreach`** (lab 38616332979, batch `lititems123c`): how far a sweep reaches, and a per-biped cache of three margins played alongside. Results at the end of this document.

## 2. What a biped's physics reads and writes, per tick [D]

`biped_update` → `biped_update_moving` → `biped_update_physics(&physics)`. The engine already packs the call into a record: `struct biped_physics` (bipeds.c) holds every input and every output.

**Inputs, made just before the call** (`biped_update_moving`):
- the biped's own datum: position (the pill's base, `biped_get_physics_pill`), forward (turned this tick by the animation's yaw, `biped_snap_facing`), aiming, velocity, crouch (moved this tick), ground plane, support surface, flags (airborne, slipping, dead, no collision);
- the animation's frame info (dx, dy, dz, dyaw) at this tick's frame, the throttle and control flags the AI or the player set this tick, the player information tags;
- the biped definition.

**Reads inside the call:**

| What | Where | Changes during `objects_update`? |
|---|---|---|
| The level's collision BSP | `collision_move_pill` → `collision_get_features_in_sphere` → `collision_bsp_test_sphere`, `collision_bsp_get_features_in_sphere`; the support edge walk; the stand test's second sphere | No (fixed per BSP; d20 switches BSP) |
| Breakable surfaces' flags | the walk skips broken ones | When glass breaks (damage) |
| Clusters of the walk's leaves → collideable-object lists | `collision_get_features_in_sphere`'s object loop | Yes: every `object_translate` reconnects |
| Other objects: position, sphere, flags, node matrices, collision model, type, velocity | `object_get_features_in_sphere`; the bumped-object and elevator loops | Yes: every object updated earlier this tick |
| Globals: `debug_*`, `global_gravity`, the BSP pointers | | No |

**Writes inside the call:** the record's outputs (new position and velocity, ground plane, support and stick surfaces, out flags, landing and collision velocities, bumped and elevator objects). Also:
- marks: `object_mark_function` (each object's `magic_number`, in the datum) and `structure_cluster_mark`;
- `global_current_collision_users[]` and its depth, `collision_log_*`;
- the port's own: the X9 sphere memo's table, `psp_walk_leaves16` (which walk the next sweep takes), the dedup table and the counters.

**After the call** (`biped_update_moving`): `object_translate` (reconnects the biped to its clusters: the next bipeds' object part reads this), the biped's flags and ground plane, landing, jumping, a melee impact (a vector test against the target), `biped_bumped_object`, falling damage.

**Random draws:** none on this path. `collisions.c`, `collision_bsp.c` and `collision_features.c` draw none; the draws in `bipeds.c` are in the dead body's impulses (`real_random`, `real_seed_random_range`), not in the sweep [D].

## 3. Can the batch recipe apply exactly?

The recipe (`research_b1_insomniac.md`): group the same work across objects, define its inputs and outputs, queue the calls, run them as a batch. It is exact only for work whose inputs are final when it's queued, whose reads nothing changes between the queue and the use, and whose outputs nothing reads before they're applied.

| Piece | Inputs final ahead? | Reads stable? | Exact as a batch? |
|---|---|---|---|
| The whole physics call | No: made in the biped's own update (animation frame, turning, crouch, the throttle's stun) | No: objects updated earlier this tick | **No**: batching reorders. Another order is a behaviour change (as RH's actor order) |
| The object part | Needs the walk's leaves | No | **No** |
| `collision_move_point` (clipping against the features) | Needs both parts | Pure on its inputs | Only after both: nothing to overlap |
| **The level walk, exact sphere** | No: the sphere is `position + velocity / 2` with this tick's velocity | Yes (static, plus glass) | Only if the sphere were known ahead (it isn't) |
| **The level walk, a sphere that holds the exact one** | **Yes**: the biped's place at the tick's start plus a margin | **Yes** | **Yes, as a copy to walk** (§4) |

Why "a sphere that holds it" works. The 3D walk visits `children[0]` then `children[1]` at every node it straddles, whatever the centre (`bsp3d_test_sphere_recursive`) [D]. A smaller sphere's walk therefore visits a subset of a larger sphere's nodes, in the same order. Its plane stack, 2D walks and surface tests are its own, made with its own centre and radius, on the same data. So walking the exact sphere through a copy that holds every node, leaf, reference, 2D node, surface, edge and vertex the larger walk reached gives the exact walk's answer, list for list. Where rounding or a teleport takes the exact walk outside the copy, it meets a node the copy marks "not here" and stops. Then the level is walked as before, so the answer is the exact one either way. Exactness doesn't depend on the margin, only the hit rate does.

## 4. The jobs, in order

### Step 0: recon (this branch)

- `pillreach` (`767feb7`): how far each sweep reaches (speed against 0.025, 0.05, 0.1, 0.2 wu a tick), and a cache of each margin (0.25, 0.5, 1.0 wu) per biped played alongside: how often it's rebuilt, and the leaves and surfaces such a build's walk finds. That's the copy's hit rate and its size.
- **Still to count, bake side** [not built]: does the baked BSP already store nodes depth-first? Per node, the distance in index from parent to each child, and how many 64-byte lines a typical a50 walk touches in each array. A short script on the runner (the bake is game data; only the counts come back).

### Step 1: depth-first node order at bake time (CPU only)

If step 0 finds the nodes scattered, the baker renumbers `bsp3d.nodes` (and each 2D tree's nodes) in depth-first order and rewrites the children. Leaves, surfaces, planes and designators keep their indices.
- **Exact** [D]: the walk's decisions read the tree's shape and the planes, never the node indices' values; the root stays node 0. To check: that no game state stores a node index (a location stores a leaf and a cluster).
- **Helps every walk**, rays included (`collision_bsp_test_vector_recursive`: 2.18 M data misses on a50) [E].
- **Costs** a bake change and a new bake. If the nodes are already depth-first (Bungie's tool may emit them so), skip it.

### Step 2: the copy, built by the main CPU (to price the replay)

`psp_collision_cache` (a sketch):
- **Build:** walk the cache sphere (centre = the pill's centre at rest; radius = the pill's reach at rest + margin + 0.0625) once, recording in walk order every 3D node (with its plane copied beside it), leaf, 2D reference, 2D node, surface, edge and vertex reached. Renumber them into one contiguous block, keeping each item's original index beside it. Children the walk didn't take point to a "not here" leaf.
- **Use:** the sweep runs `collision_bsp_test_sphere`'s own code on a `struct collision_bsp` whose blocks point into the copy. On "not here", drop the partial answer and walk the level. Translate the four answer lists back to the original indices.
- **Keep:** while the biped's sweep stays inside, by its place (one test per sweep); rebuilt when it doesn't; dropped at a BSP switch or a new map (the relink memo's generation, FIXES_121).
- **Words:** `collcache` (opt-in first), `collcachecheck` (every answer walked both ways and compared, as `surfacetestcheck`).
- **Measure** in the cache models on a50, b30's route and c20: the walk's data misses with and without, and the build's cost. If the replay doesn't save more than the builds cost the CPU, step 3 is what makes it pay.

### Step 3: the builds on the ME (the batch recipe, applied where it's exact)

- **Group:** at `actors_update`'s start, every biped whose copy won't hold this tick (its speed bound passes the margin left), plus new bipeds.
- **Inputs:** the biped's centre and radius at the tick's start (final: AI doesn't move bipeds; scripts that teleport are caught by "not here"), the BSP pointer and index. Passed in the job's arguments, so the main CPU writes back only those few lines, not its whole cache as perception's rays must (b122, on by default; `meraysoff`). Both kinds of job share the ring during `actors_update`.
- **Reads:** the level's collision BSP only: written once at load, never dirty in the main CPU's cache.
- **Outputs:** each copy in a slot of its own, in lines the main CPU doesn't touch until it collects the job (the ring's rule). Two slots per biped (the copy in use and the one being built).
- **Queue:** ring jobs of up to N builds, N sized to stay under the ring's 10 ms watchdog (a build at +0.25 reaches ~5 times a sweep's leaves on a50, `pillreach`: perhaps 0.5–1 ms each on the ME [U]; ~0.5 builds a tick, so one job a tick is the usual case).
- **Collect:** at the biped's sweep: if its new copy is done, take it; if not, use the old one if it holds, else walk the level. Never wait.
- **Code:** the build is the walk plus the recording. Like `merays122`, it must reach no system call, no VFPU, no double, no allocator, and no global store on its normal path (`mewalk.py`, `gemesim`, `mewrites`).
- The ME is idle 85–92% of the tick [M], rays included; AI is ~17 ms of b30's tick [E], so the builds have room to finish before `objects_update`.

### Not planned

- **The object part and `collision_move_point` on the ME**: they read objects other objects' updates move this tick (§2). Exact only with a reorder, which changes play.
- **A speculative exact-sphere walk** (predict this tick's sphere, walk it on the ME, take it when the bits match): prediction means a pure copy of ~400 lines of `biped_update_moving` (the animation's frame info, turning, crouch). And the spheres don't repeat bit for bit even when bipeds stand: the X9 memo, which answers exact repeats, answered 0 of 6,166 of a50's queries in one 300-tick window (lab 38543568066, `a50-on`) [E]. A copy that holds a range of spheres is what the measured reach supports.
- **Filtering a larger sphere's answer** down to the exact one: not exact. `add_feature` keeps the first occurrence, so a larger walk can list a surface earlier than the exact walk would, and the lists' 256 caps cut differently. Only a walk of the copy gives the walk's order.

## 5. Exactness traps

- **Order.** Only the level walk moves. The object part, the clipping and everything after stay where they are in the biped's update, on the main CPU.
- **Random draws:** none on the sweep's path (§2), so no draw changes place.
- **Float paths.** The copy's walk must be the same compiled FPU code as the level's walk (one function, a different `bsp` pointer), with the planes, vertices and centre copied bit for bit.
  - The VFPU is not bit-identical to the FPU: nothing on this path may move to the VFPU.
  - No separate copy of the walk compiled with other flags (contraction, `-ffast-math`).
  - The ME runs the same Allegrex FPU code. Perception's rays showed it: 0 of 270,940 ME answers differed in the emulator [E] and 0 of 31,940 on the rig's real ME [M] (`review_b122.md` §1). The copies need the same check on the rig (`collcachecheck` with the ME building).
- **The walk's guards** (`collision_bsp3d_enter_node`'s node budget, `bsp2d_nodes_left` from the BSP's node count, the edge ring's bound) must be set from the level's counts, not the copy's, or a malformed map could stop the copy's walk where the level's goes on, silently. Retail maps don't reach them (`review_b121_bipeds.md`); `collcachecheck` would show a difference.
- **The 256 caps** on leaves, surfaces, edges and vertices: the copy's walk meets the same events in the same order, so the caps cut the same.
- **Breakable surfaces:** the copy keeps every surface, broken or not, with its breakable index; the walk tests the live flags as it does now. Nothing to invalidate when glass breaks.
- **The port's own state** read by the walk: the X9 memo (stays in front; its answers are exact either way), `psp_walk_leaves16` (which code walks; speed only), `psp_surfaces_seen` (per walk). None changes play.
- **Coherence.** The ME writes only its copies' slots and its own stack; the main CPU reads a slot only after collecting the job and dropping those lines from its cache.

## 6. Collision data layout

- **Depth-first node order at bake time** (step 1): one tree's walk touches a few contiguous runs instead of nodes spread over the array.
- **Planes beside nodes:** a node (12 bytes) and its plane (16 bytes) are read together at every step, from two arrays. A baked array of 32-byte node records with the plane inline halves those misses. Designators stay valid: the 2D references and surfaces keep reading the plane array.
- **The linearization cache** (Ericson, *Real-Time Collision Detection* §13.5, `research_b13_ericson.md`): steps 2 and 3. Everything one biped's sweeps touch, in one block, in walk order.
- **Per place rather than per biped:** a squad standing together could share one copy, built for a cell; fewer builds and less memory, at the cost of larger copies [U].
- **Memory.** A copy holds per surface ~12 bytes, a ring of ~3 edges (24 bytes each) and ~3 vertices (16 each): ~130 bytes; per leaf ~8 bytes and its 2D references (8 each); per node 28 bytes with its plane. On b30 and c20 (21 and 3 leaves at +0.25) a copy is a few KB [U]. On a50 (215+ leaves at +0.25, and every surface the walk tests, not only the ones it touches) it is likely 30–60 KB, larger than the data cache, and ~0.5 MB for 14 bipeds [U]: too much for the heap (5.6 MB campaign, 4.6 MB multiplayer). A narrower margin (0.1 wu still holds ~100% of sweeps' reach, by `pillreach`), copies only for bipeds near the player, or one copy per cell, if it stays large. Step 2 should log each copy's bytes.

## 7. Expected gain

| | a50 | b30's beach | c20 |
|---|---|---|---|
| The level walk, est. ms a tick [E] | 2.2 (0.78 of it data misses) | 2.72 (b121's walk functions) | 1.02 |
| Step 1, depth-first nodes [U] | 0–0.2 | 0–0.2 | ~0 |
| Steps 2+3, the copy, built on the ME [U] | −0.2 to −0.5 | −0.2 to −0.4 | ~−0.1 |
| On the rig [U] | the estimate's ratio to the rig is ~2–4 (`review_b121_machines.md`: 1.76 vs 6.5 ms for c20's machines, step ladder included), so perhaps −0.5 to −1.5 ms of a50's 31 ms | | |

The instructions (~55% of the walk) stay on the main CPU. A larger lever on them would be a cheaper walk (fewer surface tests), not the ME.

## 8. Rig runs wanted

None for this plan yet. After step 2: one `sierra-hw` A/B on a50 and one on b30's route, `keepsets="- | collcache"`, `repeat=2`.

## `pillreach` results

Lab 38616332979 (batch `lititems123c`, `767feb7` built with `PSP_DEBUG_TOOLS=1`), hands off to the wall (600 s), b30 by the b113 route [E]. Speeds are wu a tick, the sweep's velocity with the crouch's. A cache is a sphere of the pill's reach at rest plus the margin, centred where the biped stood when it was built, rebuilt when a sweep's sphere leaves it. Leaves and surfaces are those the build's walk lists (surfaces it *touches*: the walk tests more, so a copy holds more than this).

| | a50 | b30 (route) | c20 |
|---|---:|---:|---:|
| Sweeps (a tick) | 267,833 (12.4) | 246,479 (13.0) | 493,524 (29.9) |
| Speed ≤ 0.025 / 0.05 / 0.1 / 0.2 | 93.7 / 93.7 / 100 / 100% | 80.6 / 88.4 / 99.9 / 100% | 93.1 / 99.8 / 100 / 100% |
| Cache +0.25: rebuilt on (a tick) | 4.0% (0.49) | 4.9% (0.64) | 1.4% (0.41) |
| leaves, surfaces a build; builds with a full leaf list | 215, 5.6; 73% | 21, 3.4; 0% | 3, 1.9; 0% |
| Cache +0.5: rebuilt on (a tick) | 1.6% (0.20) | 2.0% (0.26) | 0.6% (0.18) |
| leaves, surfaces a build; full | 237, 9.6; 84% | 35, 5.6; 0% | 4, 2.8; 0% |
| Cache +1.0: rebuilt on (a tick) | 0.7% (0.09) | 0.8% (0.11) | 0.2% (0.07) |
| leaves, surfaces a build; full | 243, 19.9; 84% | 61, 12.3; 0% | 7, 5.4; 0% |

- A cache counts the biped's first sweep as a build, so the rates include each biped's start.
- The instrument's extra walks also pass through the `surfacetest:` counts and the sphere memo, so those lines in these runs aren't the play build's.
- What it says: bipeds' sweeps reach little beyond the pill at rest, so a copy built once serves 20–70 sweeps. On a50 the level's leaves are so small that even 0.25 wu more reaches ~5 times a sweep's leaves; that is the copy's size problem (§6), not its hit rate.
