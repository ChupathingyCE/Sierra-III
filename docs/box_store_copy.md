# Box: the store's one-tick-old copy (design, builder2; the lab's briefs/tc_copy.md, spec v1 §38)

_The design of the copy the ME's half of the tick reads: §1 the field set, §2 its size, §3 the hand-over, §4 its price. Paper only: no code before the lead rules §1-§4 into the lab's spec §38. Written by builder2 from 2026-11-26 20:00, on builder2/storecopy, cut from the builder's head 4c299ada._

**The tree every file:line here names** (unless it says otherwise): the engine at 2e5eac00 with Sierra-III's psp_fixups.py at 4c299ada over it, ship form (slice 1's recipe, `PSP_TICK2=biped PSP_TICK2_HOIST=1 PSP_TICK2_POSENEW=1 PSP_TICK2_COLLNEW=1`) plus `PSP_TICK2_MOTIONNEW=1`, the switches the builder's shipck-b30 pair builds (the lab's briefs/asks/shipck-b30.tsv). Made with the lab's `toolbox/fixform.py` (exit 0). Sierra-III's own files (psp/src, docs) are named at 4c299ada.

## 1. The field set [D]

### 1a. The rule

A field goes in the copy when **an ME job of slice 1's ME share reads it and the main CPU's serial phase can write it while that job runs**. A field the ME reads that no serial phase writes in that window needs no copy: it is listed apart (1d), with the reason. A field only the ME job itself writes (its own output) is not the copy's either: it is the job's result, handed back as its results are today.

The two ME jobs (the lab's plan.tsv `s1-me`):
- **biped motion's parallel form** (`box_biped_motion.md` §1): each biped's motion reads the other movers at last tick's state, so bipeds update in any order on either core;
- **the AI's think a tick old** (AITICKOLD, `aime_me.md` "AITICKOLD"; the lab's tick_store_biped_plan.md:231).

### 1b. The records the fields live in (the store at 4c299ada, ship form)

Every offset and size below is checked at compile time by the lab's `toolbox/storelayout.c` (`cc -fsyntax-only`, no output: every figure holds), its structs copied from the generated tree; the PSP's `long` is 4 B.

| Section | Struct (generated tree) | Indexed by | Record | Fields: offset, size |
|---|---|---|---:|---|
| ROOT | `struct tick2_root`, objects.h:262-278 | the slot (`tick2_slot_of`), every object | 92 B | position +0 12; translational_velocity +12 12; forward +24 12; up +36 12; angular_velocity +48 12; location +60 8 (leaf 4, cluster 2, bonus 2); bounding_sphere_center +68 12; bounding_sphere_radius +80 4; unit_slot +84 2; pose_first +86 2, pose_orient +88 2, pose_count +90 1, pose_blocks +91 1 |
| ANIM (unit) | `struct unit_animation`, units.h:391-415, at `tick2_unit_rec` +0 | `unit_slot` (`tick2_unit_rec_of`, units.h:492) | 72 B | flags +0 2; seat/weapon/state/action/desired_state/base_seat +8..+15 1 each; the three animation states +18 12; aiming/looking screen bounds +32 16, +48 16; external graph and state +64 8 |
| CONTROL | `struct tick2_unit_ctl`, units.h:422-431, at `tick2_unit_rec` +72 | the same | 56 B | desired_facing +0 12; desired_aiming +12 12; desired_looking +24 12; throttle +36 12; control_flags +48 4; aiming_speed +52 1; current_zoom_level +53 1 |
| BIPED 1 | `struct tick2_unit_bip1`, units.h:432-439, at +128 | the same | 52 B | aiming_vector +0, aiming_velocity +12, looking_vector +24, looking_velocity +36 (12 each); crouch +48 4 |
| BIPED 2 | `struct tick2_unit_bip2`, units.h:440-461, at +180 | the same | 52 B | the four landing/elevator chars +0..+3; elevator_object_index +4 4; support_surface_index +8 4; bump_object_index +12 4; bump/airborne/slipping/stop ticks, jump_recovery, the two melee chars +16..+22; landing +24 2; bank +28 4; ground_plane +32 16; the limp body's two bytes +48 |
| BIPED 3 | `struct tick2_unit_bip3`, units.h:462-470, at +232 | the same | 32 B | pathfinding_surface_index +0 4; pathfinding_point +4 12; last_pathfinding_attempt_time +16 4; last_pathfinding_surface_index +20 4; impact_target_object_index +24 4; last_falling_communication_time +28 4 |

The unit section's record (`struct tick2_unit_rec`, units.h:471-478) is the five parts in one 264 B record. Two facts for §2 and §3, from the layout, not yet weighed: ROOT's record is 92 B, not the 96 B line-sized record spec §2 lists, and the unit record's 264 B puts its parts across line boundaries (CONTROL at +72..+127 spans lines 1-2, BIPED 2 at +180..+231 spans lines 2-3), not one line a writer as spec §34 lays them out. A copy that takes a part whole takes the lines it touches.

**Everything else stays in the datum** (spec §2, "Stays in the datum"): object and unit flags, damage and vitality, function values, attachments and family links, speech, weapons and grenades. A field the ME reads there is a datum field the copy must name by its datum offset, not a store field.

### 1c. The fields each job reads, with their readers and serial writers

**The order, from the lab's decisions.md 2026-11-26 20:2x (Sheepdog01's pick of the two-core plan's version):** (1)(b) "the pill sweep on the ME is the first placement", and (3) biped motion on the ME "goes in only on its own case at the goal's frame", so s1-me's biped-motion part waits for that case and its AI part goes on after this copy. So (i) is **the pill sweep** (the biped's move against the world, `collision_move_pill` as slice 1 left it: `store_coll_move`), not the whole of biped motion; the rest of motion's reads (the bump, the elevator and support surface, the melee target, the seat's parent) come only with its case. (ii) is AITICKOLD's reads, as before. Each field: section, offset, size, its reader's file:line on the ME side and its serial writer's file:line.

#### 1c(i). The pill sweep (written 2026-11-26 20:3x)

**What the sweep is in slice 1's on form.** M's move (store_motion.c:1555) calls `store_coll_move` (store_coll.c:464-560). It makes the pill's sphere from the biped's own position and velocity (:485-487), takes the list G packed for it (`store_coll_take`, :489, :280) or, refused, gathers live (`collision_get_features_in_sphere`, :492-499; collisions.c:2115), then sweeps the point through the list (`collision_move_point`, :537; collisions.c:2676), which reads only the list and its own arguments. The live gather has two halves: the level's, already made on the ME when its answer is pending (`psp_sweepme_take`, collisions.c:2149, FIXES_SWEEPME), and the objects' (collisions.c:2190-2260), which reads other objects. The move's relink is M's own: `object_translate` (store_motion.c:2497; objects.c:7025, its `object_reconnect_to_map` at :7040).

**The order inside M that the copy has to keep** (the lead 2026-11-26 00:3x, box_world_store.md Part 6: M is one stage, a biped at a time in index order, each biped relinking at its own turn, its rest seeing the bipeds after it unmoved). G's packed list is the world before any biped moves; a take is trusted only while the reach guard (store_coll.c:135, `psp_sr_slot_watch_set`; the entry's `sr_seq`, :129) says nothing relinked into its reach since, else the move gathers live and sees the earlier bipeds where they moved to. So **a biped's state another biped's sweep reads is the ME job's own output when M runs whole on the ME in its order**, not the copy's: it is written by the same job, in the order it is read. The copy is for what **the main CPU writes beside the job**: on the plan's split (two_core_tick.md:207, [archive], a lead under 20:2x (4)), the non-biped objects' updates that don't read this tick's biped results (vehicles, projectiles, items, machines, scenery).

**The fields, by where they live** (readers on the sweep's side; writers in the serial phase that can run beside it):

| Field | Lives in | Read by the sweep | Written beside it by |
|---|---|---|---|
| G's packed lists: `store_coll_entries`, the arena `psp_bs2_arena`, the slot map `psp_bs2_slot_of` | store_coll.c:69, :119 | the take, store_coll.c:280-354 | G only (`store_coll_pack`, :110-173), before M; the next writer is next tick's `store_coll_begin` (:89). **Not the copy's** (1d): a by-value copy already, and the ME's own if G moves with it |
| the reach guard: `sr_clusters`, `sr_seq`, `sr_met`, the watch | the entry, store_coll.c:129-135 | the take's trust test | G (:135); the cluster stamp `tick2_cs_seq` bumped by every relink (:128-129, `tick2_cs_settle`). **Copy question, not a field:** a non-biped relink beside M bumps the stamp the ME's take reads; queued to the join, as two_core_tick.md:57 has it [archive], or read as "changed" (more live gathers, play unchanged) |
| the collideable partition: `collideable_object_cluster_partition` (cluster → object references) | objects.c:323 | `cluster_get_first_collideable_object` objects.c:864; `psp_next_collideable` collisions.c:705-719 | `object_reconnect_to_map` objects.c:3620 (its `cluster_partition_reconnect` :3738), `object_disconnect_from_map` :2546 (:2601), from `object_translate` :7025, `object_set_position` :6978, the attach paths :3997, :7235, :7407, :8060, units.c:7845, :7961, :12128. **The copy's**: the tick-start partition for the ME's walk (twocore_read item 3) |
| the sphere copy `psp_object_spheres[]`: center 12, radius 4 | objects.c:6519-6520 | `psp_sphere_may_touch` collisions.c:738-751 (via `psp_sphere_of` :679) | the pose, `store_pose_now_cs` objects.c:6517-6521. **The copy's** for a non-biped posed beside M |
| ROOT: bounding_sphere_center +68 12, bounding_sphere_radius +80 4 | the store (1b) | `object_get_features_in_sphere` collisions.c:3270-3276 | the pose (store_pose_now_cs, objects.c:6462-6527). **The copy's**, the same objects |
| ROOT: position +0 12, forward +24 12, up +36 12 | the store (1b) | `physics_instance_new` physics.c:603-604 (a vehicle, with the vehicle-physics test bit, collisions.c:3323); the biped pill's origin, bipeds.c:613-615 | a non-biped's update (`object_update` objects.c:7775; its writer lines by kind: next run). **The copy's** for the vehicle; a biped's is the job's own |
| BIPED 1: crouch +48 4 | the store (1b) | `biped_get_physics_pill_with` bipeds.c:625 | M only (the job's own) |
| pose matrices and node count (`store_node_matrices`, `store_node_count`) | the store's POSE | `collision_model_instance_new` collision_models.c:98, :103 | `store_pose_box_gather` objects.c:6510, in the pose. **The copy's** for a non-biped with a collision model posed beside M, ~0.5-2 KB an object (two_core_tick.md:59, [archive], a lead) |
| datum: `object.flags` (invisible, no collisions), `damage_flags` (dead), `type`, `parent_object_index`, `first_child_object_index`, `next_object_index`, `definition_index`, `region_permutations` | the datum (spec §2) | collisions.c:3262-3373; collision_models.c:88-97 | the serial phase: damage, attach and detach (objects.c:3918 `object_detach`), `object_set_visibility` :3086, `object_delete` :3601. **The copy's** for a flag or link that can change beside M; the type and definition can't (1d) |
| biped datum: `biped.flags` (passes through bipeds), `unit.player_index`, `unit.parent_seat_index`, `object.flags` movie star | the datum | collisions.c:3286-3293; bipeds.c:620-621 | a seat change (unit enter and exit) in the serial phase: next run, by file:line |
| the marks: `structure_cluster_mark` structures.c:438, `object_mark_function` objects.c:1790 | per-call scratch | collisions.c:2199-2240 | **Not the copy's**: per-core scratch (two_core_tick.md:55, [archive], a lead) |
| the level's half (BSP, breakable surfaces' flags) | the map | collisions.c:2149-2177; already on the ME (SWEEPME) | breakable surfaces' flags by damage: next run, by file:line |

**Read so far (a design read, [D]):** the sweep's copy is narrower than a field set of every ME read: it is the **non-biped collideable objects' collision state** (their partition links, sphere, ROOT pose fields and pose matrices, a few datum flags and links) as the tick's serial phase last left it before the ME starts M. Bipeds' own state needs no copy so long as M moves to the ME whole, in its order. Two things are open for §2-§3: the reach guard's stamp across the two CPUs (queue or treat as changed), and whether the non-biped updates run beside M at all on the schedule tc-tick sets: if they all run before M (today's order in `objects_update`, objects.c:10606, is next run's read), the pill sweep needs no copy of other objects, only its hand-over (§3).

_To come next run: today's order of the non-biped updates against M (objects_update, objects.c:10606), the writer lines left "next run" above (a non-biped's update by kind, a seat change, breakable surfaces), then (ii)._

What is already known of (ii), from `aime_me.md` [archive form, D]:
- In its archive form AITICKOLD needed no copy: its pass ran in the record's window, when "the world holds still through the record, so no snapshot or second copy of it is needed" (aime_me.md:127). Under the two-core tick the main CPU's serial phase runs beside it, so that holds no longer.
- Its reads reach well past the store: the actors' own datums and their props (`aitickoldbuf`'s record covers "the actor's words, its props' words, the props it made or deleted", aime_me.md:132), the conversations (rounds 9-13), and the objects it perceives. So (ii)'s field set is mostly datum fields, and its writes (props made and deleted, conversations ended) are a second hand-over, the record's install, not the copy's.
- The input block is the tick's own to refresh (`actor_input_update`): "the parallel phase's think writes its decisions, never the inputs the tick refreshes" (aime_me.md:135).

### 1d. Fields the ME reads that no serial phase writes during its read

_To come with 1c._
