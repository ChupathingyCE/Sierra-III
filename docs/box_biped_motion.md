# Box: biped motion, with the collision service (bottleneck, 2026-11-11; v0, spec started)

The lab's `research/opportunities/REBUILD.md` is the spine (§3.3, §4, §7 step 4: "after the pilot"). This page is design work started while the pilot (`box_posing.md`) waits on its capture; nothing here is built until the pilot's price says the method holds. Labels: **[M]** measured on a PSP, **[E]** emulator or a count, **[D]** read in code or docs, **[U]** inference.

**What it is:** a biped's motion each tick, as one box: the unit's own update (the control record in, aiming, looking, facing and throttle out), the biped's physics (ground, slipping, jumping, the move), its link to the map after the move, and the queries it makes against the level and nearby objects through **the collision service**: the level's collision BSP and the objects' bounds as one query path over data packed for it. The collision service is a box of its own (REBUILD §3.3) built with this one, because the bipeds are its main askers; vehicles and projectiles ask it too.

**State:** spec started (2026-11-11).

## 1. Contract

- **Reads:**
  - from the world store (`box_world_store.md` §2, the v0 field list `psp/src/box_world_store.h`): a biped's **unit** section (167 B: position, velocity, forward and up, aiming and looking with their velocities, the damage and speech timers, the animation's frame indices), its **control** record (66 B, written by the AI box or the players' input: desired facing, aiming and looking, throttle, control flags, zoom, the desired weapon), its **read** section (107 B: definition, type, scale, family links, seat, weapons, actor and player) and posing's sphere (36 B);
  - the bipeds' tags (physics, the unit's speeds and limits), read only;
  - through the collision service: the level's collision BSP and the bounds of the objects near the biped.
- **Writes:** the unit section only, and a relink request when the biped crosses a cluster (the walk owns the map links). The control record is the controller's: the unit update normalises its own copy instead of writing the record back (`unit_verify_vectors` writes the desired vectors today [D, the store's map]).
- **Who reads its output, and when:** posing, for the biped's own node matrices (its declared reader: posing today runs inside the biped's steps, 57 node-block words written under biped motion's steps, `box_world_store.md` §2); held weapons and riders, which update after their parent; the AI's perception next tick; the drawing.
- **The order it keeps** [D, `cpu_cost_map.md` §4a's collision row]: today every query reads the live world in the walk's order, so a biped sees the objects moved earlier this tick at their new places and the later ones at their old. Two forms for the box, settled by the census, not by choice:
  - **serial form:** the same order, against this tick's store: exact by construction, one core;
  - **parallel form:** each biped queries the other movers at last tick's positions (the one-tick-old view, `two_core_tick.md`), so bipeds can update in any order on either core. The one-tick-old world is pursued as the two-core tick's enabler (Sheepdog01 2026-11-10 16:43, the lab's decisions.md). Not exact, so it's "plays the same" efficiency (CLAUDE.md, collision included since 2026-11-09): the multiplayer boundary asks only that the client's own biped stay well inside the host's 3.5 units (`hardware_utilization.md` §6), and play holds by side-by-sides and the outcome census (the AITICKOLD lesson, `aime_me.md` round 14: a census that holds can still lean).

## 2. Data

- **Per biped, the store's share** [E counts, worldmap1 through the lab's `worldlayout.py`]: 400 B in five sections (read 107, unit 167, control 66, posing 36, walk 24) against the datum's 1,152, on ~7 lines packed against the 11.3 datum lines an update touches today. Of the box's fields, biped motion's steps read 406 and write 330 over all types (the store's map, §2 of its page).
- **The collision service's data:** the level's BSP as the queries walk it, and per cluster the objects' bounds in arrays (positions and spheres: structure of arrays, since a query reads one field of many objects). The draft design is the lab's `research/opportunities/collision.md` (DeepSeek's draft; its [M] rows checked by research 2026-11-11 12:5x, b30's ms corrected; its [U] lines and ceilings stay leads): its §2 target shape and §5 slices are this box's starting point.
- **What it shares:** the map links with the walk (relink requests, the walk's to make); nearby objects' bounds with every asker; nothing written by two boxes once the control record is its own (the store's rule).

## 3. Placement

- **The main CPU, with the VFPU where a test is 4-wide.** Collision's plane tests are a dot product and two compares a node chasing tag data, not VFPU material (`hardware_utilization.md`); the vector maths of the move and the turning is.
- **Code:** today's biped update runs 299-405 code lines a call (19-26 KB) and collision's eight sampled functions 23,976 B, each more than the 16 KB I-cache [D, E: `cpu_cost_map.md` §4a, `collision.md` finding 4]; half of c20's tick I-misses are in the bipeds' update [E]. So the box's code is written as stages that each fit (the unit's control, the physics, the move, the link), run over all bipeds stage by stage where the order allows; collision's walk is the one subsystem whose code fits as one stage.
- **The ME:** not this box's first form. The whole biped update on the ME was counted NO-GO on b30 and 27-53% reach in c20's fights (BW, below); the ME's room at 15 fps is the tick schedule's to give (REBUILD §7 step 5), and a far biped's whole update there is S4's form (`redesign_s.md`).

### 3b. Code budget (the lab's REBUILD §3.6, in force 2026-11-17)

- **Lines today, the old path** [E, exact counts in the cache model; the lab's findings `fpass2_own_pair_by_subsystem` (each subsystem on its own cache pair) and `cpu_cost_map.md` §4a's bipeds row]:
  - `biped_update`: **776 lines a tick on b30, 952 on c20**; 338 lines a call on b30, 405 on c20 (22.8 and 29.6 calls a tick).
  - `unit_update`: **418 lines a tick on b30, 394 on c20**.
  - Together **1,194 a tick on b30 and 1,346 on c20**, against the I-cache's 256 lines.
  - Fetched again and again: on its own cache pair `biped_update` takes **4,847 I-misses a tick on b30 and 13,743 on c20**, ~6x and ~14x its lines a tick; `unit_update` 1,229 and 597.
- **Fit: no. The box is cut into stages, each ≤ ~200-256 lines and resident over every biped** before the next stage runs. One call's 338-405 lines can't stay resident across bipeds, and it evicts its neighbours (the frame picture's objects' update waits more on code than on data, REBUILD §3.5).
- **Where the lines fall** [E, exact counts: the lab's sierra-batch 44125649004 (stagecut1, footprint per function, tock 7a7ebab3, EBOOT 60ee7972; b30_leg2 to exitat130, c20_leg2 to exitat150), findings `stagecut1_lines_by_stage`]. Every code line a footprint's calls touched, named by the function it falls in, then grouped by step. Lines a tick, **b30 / c20**, scaled to the cache model's own count; a call's lines in brackets:

  | Step | `biped_update` | `unit_update` |
  |---|---|---|
  | the unit's control (unit_update's own body, melee, seat labels) | (in move) | 125 / 69 (64 / 30) |
  | aiming (the euler aim, its trig) | 7 / 9 | 117 / 66 (72 / 8) |
  | the move (biped_update_moving's own, snap facing, falling, the pill) | 164 / 219 (116 / 160) | |
  | its collision queries (sphere features, the BSP walks, move_point) | 208 / 169 (125 / 129) | 70 / 59 (7 / 3) |
  | the link (cluster reconnect, map reconnect and disconnect, portals) | 93 / 101 (60 / 75) | |
  | animation | 20 / 19 (13 / 13) | |
  | **events: functions under 1 line a call** (sounds, footsteps, deactivation, the limp body, marker lookups, the object's lighting) | **246 / 400** (26 / 23) | **83 / 178** (12 / 14) |
  | the tick, all of it | 776 / 952 | 418 / 394 |

- **What it says:**
  - **The event paths are the largest group: 32-42% of `biped_update`'s lines a tick and 20-45% of `unit_update`'s**, run by a few calls a tick each. They can't be phased (no two calls share them in a tick), but they evict the steady stages. **The box queues them** (means: work on need) and runs them as one pass after the stages, so their lines come in once a tick and don't land between bipeds.
  - **Every steady step but one fits 256 alone.** The unit's control (69-125), aiming (66-117), the link with animation (113-120) and the move (164-219) each fit. **The move and its queries together are 372-388**, and they interleave inside one call (collision_move_point's slide loop asks, moves, asks again), so they can't be two stages as written.
  - **The move's cut is rounds:** the move's intent for every biped; then query round k for every biped (each biped's k-th sphere/vector test, a dependent slide waits for the next round); then the move applied. Each round's code is the query's 169-208 lines; the per-biped record carries the pill, origin, delta and the planes found. Most bipeds settle in one or two rounds (`collision_bsp_test_sphere` 2.2 calls a biped a tick on b30, 1.2 on c20) [E].
  - **Aiming splits from the unit's control on b30** (125 + 117 + the unit's 70 query lines would be 312); on c20 aiming runs for few units (5.4 calls a tick against 37 updates) and the two fit as one.
  - **Two copies of the sphere walk are live:** the port's dedup walk (`psp_bsp2d/3d_test_sphere`, 54-64 lines) on b30's big walks, the engine's (`bsp2d/3d_test_sphere_recursive`, 45 lines) on c20's small ones, picked by a running average of leaves (`psp_sphere_walk`, `PSP_SURFACE_DEDUP_LEAVES16`) [D]. The box carries one walk.
  - **The object's lighting runs inside `unit_update`** (find_point_lights_for_object_in_cluster, sample_lightmap, psp_tex_light_texel: hot on b30's calls, an event on c20's) [E]. It isn't this box's ground; it goes to the box that owns object lighting, and leaves this box's budget.
- **What crosses a stage, per biped:** the unit's control writes the desired move and facing; aiming writes the aim; the move rounds carry the pill, origin, delta and the plane list; the link reads the new position. A small record (~64-128 bytes [U]) carried from stage to stage, not re-read from the datum.
- **Still open: the order a stage keeps** (§1's serial form). The link phased after every move means a biped's queries in round k see the others' new positions but their old cluster links; the serial form sees links updated biped by biped. The hazard count in Open 1 settles whether any query reads another biped's this-tick link, and whether a stale one plays the same (the outcome census).
- **Next:** the stages as a code budget in §4 (each stage's line count as its ceiling), then the events queue's list from the same counts.
- **The ceiling it chases** [E]: phasing the bipeds' update was counted at ~1.1 ms a tick on b30 and ~2.6 on c20 (§5), the largest of the code-side figures on the tick. No PSP price until a stage form is built.

## 4. Budget

- **Today** [M, PSP-1000, the shipping sampler, tock162 b3461c1: the lab's findings `pcsample_c20_play1_shipping`, `pcsample_b40_route_shipping`]: collision **13.3-18.4% of the tick** (c20 14.0, a30 14.2, b30 18.4, b40 13.3), the bipeds' and units' updates **~10%** (c20 10.0, b40 9.8), the map links 4.6-8.1%. At b40's snowfield tick of 37.6 ms [M, b151 ledger] that's ~5.0 ms of collision, ~3.7 of biped and unit updates and ~1.7 of links a tick [U: the sampler's shares on another stretch's tick].
- **v0's share of REBUILD's 7.5 ms** (the objects' update row, with vehicles and the rest): **4.5 ms a tick for this box with the collision service and the links** on the beach and the snowfield, proportional to today's shares [U]; vehicles and the rest keep the other 3.0. Per biped at the census's ~26 updates a tick [E]: **~170 µs an update**, collision's share for vehicles and projectiles counted in it until the service is priced alone.
- **Against today, per stretch:** on b40's snowfield today is ~5.0 + 3.7 + 1.7 = **~10.4 ms** a tick [U on M]; on b30's beach collision alone is ~4.8-5.6 ms [U on M: the run's own sample split, research's correction of collision.md], and on b30's flight ~10 ms (25.6% of a ~39 ms tick, the lab's s_gap §2). So 4.5 ms is a cut of more than half on the snowfield and of more than half for collision alone on the flight. **The sized pieces reach ~3 ms with their overlap** (CSPK 1.2-1.4 on b30, the bipeds' stages ~1.1, collision's one stage 0.75 [E]); **the rest is unsized, and it's this box's question**, as posing's per-node gap is the pilot's.
- **Memory:** the store's 400 B a biped is the store's; the collision service's packed copies are this box's own, to size from the slices (`collision.md` §3's ledger row).

## 5. Means

- **A (data owned and packed):** the biped's sections on 7 lines against 11.3; the object half of collision packed per cluster (CSPK's sizing: 1.2-1.4 ms a frame on b30, 1.0-1.1 a30, 0.4 c20 [E], overlapping the rest).
- **B (work on change):** a biped standing still with no input and nothing near it moving needs no physics walk; BW's count found few such updates on b30 (2.8-3.3%) [E], so B is small here except through the far tier.
- **C (both cores):** the parallel form (§1), if the census holds; collision's queries only read (they write visit marks, which become per-query state).
- **The stages:** phasing the bipeds' update is worth ~1.1 ms a tick on b30 and ~2.6 on c20 on tock162's LTO build [E, `cpu_cost_map.md` §4a], collision's one stage 0.75 and 0.54 [E]. Ceilings overlap; none is a PSP price.

## 6. What it replaces

The engine's `biped_update` and `unit_update` (the motion half), `biped_update_physics` / `biped_update_moving` and their sphere walks, `collision_test_vector`, `collision_bsp_test_sphere` and the features gathers, `object_translate` and the reconnect after the move. **Earlier tries on this ground** (the lab's `tools/levercheck.py --ground collision`, 2026-11-11): BP (the port's level sphere walk, chosen per walk: on), P2NC (the non-collideable reconnects after the walk: on), CW (the objects' walk: exact, built), CT (type-first reject: rejected small), BW (the whole biped update on the ME: stopped, reach), VH1 (vehicles' update split), STK (the tick's stack frames named), and collision's exact reuses (SMEMO, BSPR, COLLMEMO, S2REUSE) and BSPW, which failed on answer reuse or recursion cost (`collision.md` finding 6, §4). The box revives S2REUSE's and CLUSTERPACK's `revive_if` ("under a collision redesign") [D]. The full list goes here before the design is settled.

**Upstream manifest:** the biped, unit and collision functions and their tags at the pin after the rebase (REBUILD §5b).

## 7. Proof

Alone, against a capture of a pad bench (one of Sheepdog01's recordings): per biped update, its inputs (the store's sections, the control record, its near objects' bounds and the level) and the engine's outputs (the unit section, the relink); the box's outputs compared with the engine's (the serial form to the last bits, which keeps statecrc as its cheap proof; the parallel form within a tolerance set far under the host's 3.5 units, its proof the side-by-sides and census). Then in the frame: play and both seeds in the serial form; play and the outcome census in the parallel form.

## 8. States

- 2026-11-17: REBUILD §3.6's code budget added (§3b): 1,194-1,346 lines a tick against 256, so the box is staged; the stage cut is its next design question.
- 2026-11-11: spec started, from the world store's layout and the collision draft; waits on the pilot's price before any code.
- 2026-11-11: the hazard's reach, first read (Open 1): half of a biped's reads of other bipeds land on ones updated earlier this tick; bipeds write other bipeds and scenery 91 times a tick.
- 2026-11-13: slice 1 built (Sierra-III `builder/motion1`, `PSP_BOXMOTION=1`, keep words `boxmotion`, `boxmotioncheck`: the walking bipeds' physics as box code, exact against the old path, reaching 97% of b30's physics calls; the lab's findings `boxmotion_exact_play_seeds`, `boxmotion_reach`). **Priced alone [M]** (the lab's bench-hw 43508316286, PSP-3000, halo_box 7138a85, b30's beach capture from tick 2,700, 256 ticks, 2,762 biped entities, 10.8 a tick; findings `boxmotion_standalone_beach_psp3000`): **warm 3.08 µs an entity, emptied 16.3, crowded 18.1; with the GE at 50% warm 3.18, emptied 22.1, crowded 25.4** (0.03-0.27 ms a tick); 2,762 outputs compared, 0 outside the tolerance (exact). The physics step is one part of the box's ~170 µs an update (§4): warm it is under 2% of that, crowded ~15%; as with posing, the gap between warm and crowded (6x) is the cache's, so the box's working set and the two-core split decide its in-frame price.

## Open

1. **Serial or parallel form: the hazard's reach, first read** [E counts, worldmap1's object trace, the emulator's `cross` lines over b30, b40, d20 and a30, 16,954 ticks]. During a biped's update, **its loads of other bipeds' data run 370 a tick into bipeds updated earlier this tick and 351 into later ones**, on their position, velocity, forward and up (+12 to +56); into vehicles 92 earlier and 9 later; into scenery 920 and 448 (most of it asleep and unchanged, so the order doesn't matter there). So about half of what a biped reads of other bipeds already holds this tick's values today, and the parallel form would show it last tick's instead: that half is the play the census has to hold. **A biped also writes others' data**: 27 stores a tick into other bipeds (position, velocity, forward and up, and +144 to +168) and 64 into scenery (moved or pushed), so the parallel form needs those as requests the serial phase applies, not stores. Still to count: how many of the earlier reads find words that changed this tick (a mover that moved), which is the hazard's true size. `[enables:biped motion]`
2. The collision service's own page (`box_collision.md`), from `collision.md` once it's verified line by line.
3. Posing called from inside the motion's steps: the posing box takes those calls as declared readers (`box_posing.md`); which ones the motion needs before its own move ends is to read in code.
