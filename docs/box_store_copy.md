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

_To come, in this order (next runs): (i) biped motion's parallel form, the reads of other objects in store_motion.c and store_coll.c (the collision service's gather, the bump, the elevator and support surface, the melee target, the seat's parent); (ii) AITICKOLD's reads. Each field: section, offset, size, its reader's file:line on the ME side and its serial writer's file:line._

What is already known of (ii), from `aime_me.md` [archive form, D]:
- In its archive form AITICKOLD needed no copy: its pass ran in the record's window, when "the world holds still through the record, so no snapshot or second copy of it is needed" (aime_me.md:127). Under the two-core tick the main CPU's serial phase runs beside it, so that holds no longer.
- Its reads reach well past the store: the actors' own datums and their props (`aitickoldbuf`'s record covers "the actor's words, its props' words, the props it made or deleted", aime_me.md:132), the conversations (rounds 9-13), and the objects it perceives. So (ii)'s field set is mostly datum fields, and its writes (props made and deleted, conversations ended) are a second hand-over, the record's install, not the copy's.
- The input block is the tick's own to refresh (`actor_input_update`): "the parallel phase's think writes its decisions, never the inputs the tick refreshes" (aime_me.md:135).

### 1d. Fields the ME reads that no serial phase writes during its read

_To come with 1c._
