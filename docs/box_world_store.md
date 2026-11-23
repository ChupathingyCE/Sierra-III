# Box: the world store (bottleneck, 2026-11-11; v0, spec in progress)

The lab's `projects/sierra/research/opportunities/REBUILD.md` is the spine (§3.3, §4, §7 step 1). This page follows its §4 layout. Labels: **[M]** measured on a PSP, **[E]** emulator or a count, **[D]** read in code or docs, **[U]** inference.

**What it is:** the objects' hot state laid out for the tick's walks. It holds what each tick box reads and writes every tick, apart from what scripts and rules touch rarely. It's the root of the tick: every other tick box (posing, biped motion, the collision service, AI, vehicles, effects) reads and writes through it. Datum indices and object slot numbers stay the disc's (the multiplayer rule).

**State:** spec, with its read and write map taken (2026-11-11, §2); the first slice, the sweep's and biped motion's inputs one tick old, built under `PSP_WSOLD=1` (2026-11-19, §9).

## 1. Contract

- **Reads:** the engine's object datums at the tick's start (once, when a box is cut over: the store is filled from them and written back to them where the engine still reads), and each tick box's writes.
- **Writes:** nothing of its own; it's the storage the boxes write into.
- **Who reads it, and when:**
  - the tick boxes, in the tick schedule's phases: the parallel phase reads **last tick's** store (both cores), the serial phase reads and writes **this tick's** (`two_core_tick.md`, the lab's `tick_core.md`);
  - the engine's remaining readers (scripts, rules, damage, the netcode) through the engine's datums, which stay right at the tick's end (the write-back, §5);
  - the drawing (the draw list box), after the tick, for what it carries between frames (positions, node matrices, the far ladder's state).
- **Its rule:** one writer a field in each phase. A field two boxes both write is split, owned by one and handed to the other, or written in the serial phase only (the ME's rule for its lines: one writer a 64-byte line).

## 2. Data

**Today's layout** [D, the datum layouts as the port compiles them, the lab's `projects/sierra/scripts/datum_layouts`]: one datum per object, its type's whole struct in one block: biped 1,152 bytes, vehicle 1,148, weapon 636, projectile 552, scenery 424, machine 472, light fixture 476, equipment and garbage 476, control 460, device 452; the node orientations and matrices follow each datum as header blocks. The pool is ~1 MB on b30 (the lab's findings `tickwrites_kb_a_frame`).

**What the tick touches of it** [E]:
- a biped's update touches **497 words (1,988 bytes) in 40 lines that half of all updates touch**, which packed would fit 32 lines; weapons 11 of 23 lines (13 hot, 8 packed); vehicles 24 of 51 (33 hot, 23 packed); scenery and machines one line for one word each (the lab's findings `objhot_biped_lines_touched`, cachecensus1 41308914906);
- the tick writes **163-236 KB a frame of object data** on b30 (objects, lights, particles; the AI's props and actors 46-98 KB) (the lab's `tickwrites_kb_a_frame`, snap1 40903127317);
- the costly steps' loads are **~97% their own family or last tick's state**: a parallel phase reading the tick's start is consistent with them (the lab's `tickcore_census`, 42927567774). The stores that need care are named there: collision's shared scratch, the walk's bookkeeping, the clusters' lists, the AI's shared scratch, the global seed, function values written into other objects, and fresh reads of others' nodes.

**The store's layout:** per type, the fields each box reads and writes every tick, grouped by owner; its first cut follows the map below. **Open (2026-11-16) [D]:** the first cut isn't one owner per field yet. In `psp/src/box_world_store.h`, a weapon's `triggers_2[72]` sits in `box_ws_weapon_read`, `box_ws_weapon_weapon` and `box_ws_weapon_walk` (lines 548, 570, 586), and `magazines_2[24]` in two of them. Used as the working store, that duplication is copies and syncs where the store means to be dense. It was found by GPT-6 Astra Pro's read in the lab's outside round two and checked against the header.

**Open (2026-11-18, biped motion's 1b) [D]:** the v0 biped section lacks 14 fields biped motion's intent reads or writes (the lab's findings `intent_fields_vs_store`, a DeepSeek read checked against this header). They include the biped's whole tail from +1060: `biped.flags`, `state`, `support_surface_index`, `airborne_ticks`, `slipping_ticks`, `landing`, `crouch`, `bank`, `ground_plane`. Also `unit.body_stun` +980, `object.recent_body_damage` +168, `angular_velocity` +60 and `looking_screen_index` +588. The section stops at +1118. Several of these are touched on every living biped's update, so the census's half-the-updates rule, or its counts, missed them [U]. Biped motion adds them as a hand-written addition beside the generated header (`box_biped_motion.md` §1c). The census rule wants a look before the other types' sections are trusted.

**The read and write map, per box** [E counts: the lab's worldmap1 (sierra-batch 43064891428: b30, b40, d20, a30, ~17,000 ticks, emulator item 29's per-step field counts), read by the lab's `projects/sierra/scripts/worldstore.py --boxes`; the whole map is the lab's `projects/sierra/research/opportunities/worldstore_map_2026-11-11.txt`]. Each tick step is put in the box it belongs to (posing, biped motion, AI, effects, vehicles and other objects, the walk, the engine's scripts, players and HUD); a field counts for a box when the box touches it at least 0.1 times a tick, which leaves out an object's creation and deletion (they write a datum whole, once, in whatever step runs).

| Box | Loads / stores a tick | Fields only read | Fields written |
|---|---:|---:|---:|
| posing | 19,900 / 17,200 | 511 | 1,638 (mostly node blocks) |
| vehicles and other objects | 6,060 / 740 | 701 | 375 |
| biped motion | 3,420 / 1,160 | 406 | 330 |
| AI | 3,380 / 310 | 521 | **32** |
| the walk | 2,030 / 580 | 968 | 218 |
| effects | 740 / 30 | 277 | 60 |
| engine (scripts, players, HUD) | 250 / 60 | 98 | 30 |

**Hot fields per object** ~~(touched at least once every two updates of that type, against its datum; the node blocks counted apart): biped 119 words (476 B, 41% of its 1,152) of which 36 written, and 478 node-block words; vehicle 33 words (132 B, 11%), 35 node-block words; weapon 40 (160 B, 25%); projectile 49 (196 B, 35%); light fixture 16 (64 B); scenery 4 (16 B, 3%) and sound scenery 6, over 5.4 and 0.9 million updates.~~ (superseded 2026-11-11 by the layout's first cut below, which counts the updates that touch each word (the census's word touches) where these came from the map's per-step access counts: biped 99 words, 396 B). **So the store is small per object outside the bipeds' node blocks**, and the bipeds' 396 hot bytes against 1,152 (the cut below) is means A's room: today each biped's hot words sit spread over ~40 lines (the earlier `objhot_biped_lines_touched`, 497 words with node blocks, 32 lines packed).

**Fields written by more than one box: 379** (the world store's rule is one writer a field a phase). The groups:
- **posing called from inside other boxes' steps** (node-block words written under biped motion's steps: 57 biped, 39 scenery; under the vehicle update: 84 vehicle): node matrices are made over on demand inside those updates, so reader-driven posing (`box_posing.md`) has to take those calls as declared readers, not leave them to the old walk [U];
- **the controller's hand-over to the unit:** `unit.desired_facing/aiming/looking_vector`, `throttle`, `control_flags`, `primary_trigger` written by the AI, the players' input (engine) and the unit's own update; `aiming/looking_vector` and their velocities by biped motion and the unit update. The owner is the controller for the desired words and the unit's update for the actual ones, the hand-over a per-unit control record (AITICKOLD's shape: the AI box writes almost nothing else, 32 fields in all, mostly these plus `biped.pathfinding_point`);
- **function values and change colours** (`incoming/outgoing_function_values`, `outgoing_change_colors`) written by the per-type update and the walk's export: one owner, the export;
- **projectiles' position and velocity** written by effects, their update and the walk; scenery moved by biped motion (pushed) and effects (attached).

These are the hand-overs the tick schedule's serial phase has to own (`two_core_tick.md`); the counts are the census's [E] and say nothing of time.

**The store's layout, first cut** [E counts: the same worldmap1 traces, read by the lab's `projects/sierra/scripts/worldlayout.py`; its output and word list are the lab's `projects/sierra/research/opportunities/worldstore_layout_2026-11-11.txt` and `.tsv`]. The principle stands: per type, the fields each box reads and writes every tick, grouped by owner, in arrays by type (structure of arrays where a walk reads one field of many objects, the datum's own order where a box reads many fields of one object); the engine's cold fields stay in the datum. A word is hot when it's touched in at least half the updates of its type (the walk's reads of `object.type` count the updates; where under a quarter of the live objects are updated, the live objects are the base); its section is the boxes that write it, a writer with under a tenth of the main writer's stores counted as minor (a rare path, or a leaf's step label).

| Type | Live | Updated a tick | Hot bytes | Lines packed | Lines, a box a line | Datum lines touched today | Hot KB at the live count | Node blocks hot |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| biped | 30.6 | 25.7 | 396 of 1,152 | 7 | 16 | 11.3 | 11.8 | 1,428 B |
| weapon | 45.2 | 14.9 | 248 of 636 | 4 | 7 | 10.6 | 10.9 | 252 B |
| projectile | 1.8 | 3.0 | 128 of 552 | 2 | 6 | 4.2 | 0.2 | 52 B |
| light fixture | 11.8 | 4.3 | 116 of 476 | 2 | 4 | 7.7 | 1.3 | 52 B |
| vehicle | 18.9 | 5.5 | 24 of 1,148 | 1 | 1 | 10.3 | 0.4 | - |
| scenery, machine, sound scenery, equipment, control | 520 | 51 | one word each | 1 | 1 | 1.0-1.8 | 1.8 | (scenery 104 B under its updates) |

What it says:
- **The store is small: ~27 KB of hot words at the live counts, ~54 KB more of hot node-block words**, against a pool of ~1 MB, so it fits beside the datums with room: means A's packing and C's double buffer of the parallel phase's fields are both affordable in memory (the heap's room is still the limit, §4).
- **A biped's 396 hot bytes fit 7 lines against the 11.3 datum lines an update touches today** (plus its 1,428 hot bytes of node blocks, posing's; `box_posing.md`). Laid out one box a line (the ME's rule, one writer a line) they take 16 until the shared words are owned; once the unit's own update and biped motion are one box (below) and the controller's hand-over is a record of its own, about 10.
- **A weapon's 248 hot bytes fit 4 lines against 10.6**: a held weapon's update (heat, triggers, magazines, its position copied from the holder's hand) touches a tenth of its words but over most of its lines.
- **The vehicles' hot set is small because most are parked**: of 18.9, 5.5 are updated a tick and only 6 words are touched in half of those; a driven vehicle's set is the vehicle box's to take from a capture of its own.
- **Scenery and the other still kinds have one hot word each over their live counts** (most sleep); the reads of them are other boxes' (collision, the AI's perception) over many objects, the structure-of-arrays case: their positions and bounds in arrays the collision service walks.
- **The biped's shared words, by the step that writes them** [D, the map's steps]: the controller's desired words and control flags (`desired_aiming/looking/facing_vector`, `throttle`, `control_flags`, `current_zoom_level`) are written by `actor_unit_control` (the AI) or `players_update_before_game` and then normalised by `unit_verify_vectors` in the unit's own update; the actual aiming and looking vectors and velocities by biped motion's turning and the unit update; the velocity and the animation's frame indices by biped motion and posing. In the map's box rules the unit's own update falls under "vehicles and other objects"; in the layout it belongs with biped motion (it's the unit's update), which takes the motion + objects words to one owner. What's left is the controller's hand-over, already the per-unit control record of §2's groups, and the animation state written by both the motion and posing, which the posing box's v0 leaves to the engine (`box_posing.md` §6b) and reads as an input.

**The field list, v0** (`psp/src/box_world_store.h`, generated by the lab's `projects/sierra/scripts/worldheader.py` from the cut's word list; a spec that compiles, with each field's store offset checked against the compiler): per type, one struct per owner, each field its whole leaf (a hot word of two shorts takes both) with its datum offset, and the copy runs between the datum and the store. The owners: **unit** (biped motion with the unit's own update), **control** (the controller's record: whatever the AI or the players' input writes), **posing**, **walk** (its export of function values and its sleep bookkeeping), the type's **own update**, **effects**, and **read** (no box writes it every tick). The object's magic number stays in the datum. A biped's 400 bytes: read 107, unit 167, control 66, posing 36, walk 24; a weapon's 506: read 208, its update 174, walk 108, posing 16.

**The fill and write-back's count** [E, at the census's updates]: **18.7 KB filled and 12.3 KB written back a tick, in 1,874 copy runs** (a biped's 50 runs, a weapon's 27). That's the cost of §7's proof stage, where the store is filled from the datums and written back to them every tick with no box using it; it shrinks as boxes are cut over: read-only fields fill on change, not each tick, and a field whose every reader reads the store is no longer written back. Its time is the PSP's to price, in the serial phase (§4).

## 3. Placement

Main RAM, in the layout above; the parallel phase's two copies (last tick's and this tick's) are the double buffer only for the fields the parallel phase reads and the serial phase writes (`two_core_tick.md`, the one-tick-old view). Not eDRAM (2 MB, the ME's code and data and the drawing's); not the scratchpad (16 KB, and the ME can't see it). Code: none of its own beyond the fill and the write-back.

## 4. Budget

- **Memory:** the store's fields, per object, times the live objects of each type, plus the double buffer's share. To set from the map. The heap's room is the limit: c20 ends Sheepdog01's play with 89 KB free [M], and a whole-object snapshot (448-672 KB) didn't fit (the lab's `snapdry_buffer_kb`), so the store must replace datum fields, not copy them, wherever the engine's own readers allow.
- **The first slice (§9):** ~~40 KB~~ 74 KB (2026-11-19: 2,048 object slots, the object array's maximum, after d20's overflow), ~6-7 KB refreshed a tick [E], <=0.2 ms a tick of the main CPU, nothing on the ME but reads.
- **Time:** the store has no time of its own; it's paid back in the boxes' misses (means A). Its fill and write-back (§5) are budgeted in the tick schedule's serial phase.

## 5. Means

- **A (data owned and packed):** a biped's hot words fit 32 lines packed against 40 today, and a walk over one field of many objects reads one line per 16 objects in place of one per object [E counts; the price is the PSP's].
- **C (both cores):** it's what lets the parallel phase run: each core reads last tick's copy and writes its own family's fields.
- **B:** none directly; it carries the boxes' "changed" marks (work on change).

## 6. What it replaces

The engine's object datum as the tick's working storage (`object_get` and the datum walks in `objects_update`); the port's earlier tries on this ground: OBJHOT (the hot words packed), DATUMSPLIT, WORLDVIEW (~~declined on its standalone ceiling, Sheepdog01 2026-11-07~~ superseded for this use: Sheepdog01 2026-11-10 16:43, the lab's decisions.md: the one-tick-old world is pursued as the two-core tick's enabler, proven by side-by-sides and the outcome census), Plan C's snapshot (didn't fit, `snapdry_buffer_kb`). Each by `projects/sierra/tools/levercheck.py` before the store's design is settled.

**Upstream manifest:** the engine's datum structs and the functions that read them in the tick, at the pin after the rebase (REBUILD.md §5b, §5c): taken on the rebased engine.

## 7. Proof

The store filled and written back with no box using it yet: play and both seeds identical every tick on b30, b40, d20 and a30 (the check build compares the store's fields against the datum's after each step).

## 8. States

- 2026-11-11: spec started; the map's census (the lab's worldmap1) running.
- 2026-11-11: the map taken (§2): per box, its fields read and written, the hot bytes per object, and the 379 fields two boxes write.
- 2026-11-11: the layout's first cut (§2): per type, the hot words by owner; the store ~27 KB of hot words and ~54 KB of hot node blocks at the live counts.
- 2026-11-11: the field list v0 (`psp/src/box_world_store.h`, generated) and the fill and write-back's count (18.7 + 12.3 KB a tick at the proof stage).
- 2026-11-19: the first slice designed (§9): the sweep's object inputs and the bipeds' pill and motion, one tick old, refreshed on change as the walk begins; under `PSP_WSOLD=1`, keep word `wsold`.
- 2026-11-19: d20's overflow fixed (§9, the copy at 2,048 slots); the copy's reader named: slice 2 of the ME's pill sweep (§12, `box_collision.md` §11).

## 9. The first slice: the sweep's and biped motion's inputs, one tick old (2026-11-19)

The lab's direction of 2026-11-18 (`research/opportunities/direction_2026-11-18.md` §11, REBUILD §7 line (a); the lead, 2026-11-19 06:1xZ item 4): the store with its one-tick-old copy comes first, and its first slice is the inputs of the first placement, the pill sweep on the ME, and of biped motion. Not the whole hot set. Built under `PSP_WSOLD=1`, keep word `wsold` (off by default); `wsoldcheck` is its check. Code: `psp/include/box_ws_old.h` (the records and the readers), `psp/src/box_world_store_old.c` (the copy), `psp_fixups.py` FIXES_WSOLD (the marks, the refresh's place, the engine's side that makes a record from a datum).

**Decided, before the code:**

- **What the slice holds** [D, the engine's `object_get_features_in_sphere` and `biped_get_physics_pill` read at the v0.9.1a pin; the sweep box's object record, `box_sweep.h` `struct box_sweep_object`]. Two record kinds, indexed by the datum's absolute index (the disc's slots stay):
  - **every object, 32 B (two a line):** the bounding sphere (+80, +92), the flags the sweep and the pill test (+4: invisible, no collisions, movie star; the other bits left out, so a write to them isn't a change here), the child walk's links (next +196, first child +200), the type (+100), the dead bit (+182), and the datum's identifier (a record of a slot's earlier object never answers);
  - **every biped, 64 B:** the pill as the sweep tests it (base raised by its height, height, width; held only for a biped with no parent, whose pill stands on its own position: a child's stands on its parent's node, made on demand), the biped's position and velocity (+12, +24: what biped motion's parallel form reads of other bipeds, the bump), the parent (+204), player (unit +456), seat (unit +672) and the passes-through-bipeds bit (+1060).
  - **Not in it:** the clusters' object lists the sweep's cluster walk follows (the map links, `object_reconnect_to_map`'s), the level (read-only), and anything of a biped's own record (its unit section, `box_world_store_biped.h`, is biped motion's current copy). The cluster lists are this slice's second part: per cluster, its collideable objects as the walk began, refreshed for the clusters a reconnect touched; until they're held, a sweep on the ME reads the live lists, so the slice doesn't yet give the ME a whole world (the lead's ask said the sweep's inputs, and this is the part left).
- **Who writes the current copy, and when:** there is no second current copy in this slice: the engine's datums are the current world, written by the main CPU as today. The store holds only the **old** copy.
- **When the old copy is taken: once a tick, as the object walk begins** (`objects_update`, after the clusters' activation and before the first update), not double-buffered. Between two refreshes nothing writes it, so a reader on the other core reads the world as the walk began while the main CPU writes the datums, with no lock. A second buffer is needed only once a box writes its fields into the store instead of the datum (the current copy moves into the store); then the two flip at the same point. The refresh is **on change**: the objects marked as written since the last refresh, plus every biped held (their records are 2 KB in all; marking each of a biped's writers costs more than refreshing them). The marks: the walk's own updates (`object_update`, which the children go through too), the sleep's wake marks (`psp_wake`: every outside writer the sleep had to know of, and the parents above it), creation (`object_new`), node matrices made on demand (`object_compute_node_matrices_with`, `psp_nms_make`: the sphere moves there), and the child lists (`object_add_to_list`, `object_remove_from_list` and their callers' parents). `wsoldcheck` proves the marks complete: after each refresh every live object's fresh record is compared with the copy, and a difference is a writer no mark caught (counted by field, the first 24 named in the log).
- **Size and bytes a tick:** ~~1,024 object slots x 32 B + 96 bipeds x 64 B + a 2 KB biped map = **40 KB** of .bss (off the heap, only in a `PSP_WSOLD=1` build). A slot past 1,024 has no record (counted; its reader takes the serial path).~~ (superseded 2026-11-19: d20 uses 1,070 slots.) **2,048 object slots** (the object array's own maximum, `HALO_PORT_MAXIMUM_OBJECTS_PER_MAP`, checked against the engine's `MAXIMUM_OBJECTS_PER_MAP` at compile time, so no map can overflow it) x 32 B + 96 bipeds x 64 B + a 4 KB biped map = **74 KB**, plus the marks' 16-bit lists (~8 KB): **~82 KB of .bss in a `PSP_WSOLD=1` build, +36 KB on 1,024 slots**. The budget: the copy's memory is the heap's, so the store's later slices replace datum fields rather than add copies (§4). Bytes a tick: the refreshed records x 32 B plus the bipeds x 64 B; estimated ~105 objects and ~31 bipeds on b30 (the store's map, §2: the updated objects a tick) = **~5.3 KB a tick** [U on E]; measured below.
- **How the readers switch:** in this slice **no reader reads the copy for play**, so the build is exact with the keep word on (its proof below). A reader switches at its own cut-over, not here, because reading the copy changes what it reads (one tick older): that's "plays the same", proven by side-by-sides and the outcome census then, not statecrc.
  - **The pill sweep on the ME:** its world's object table (`struct box_sweep_world`'s `objects`) is filled from `box_ws_old_object_of()` and `box_ws_old_biped_of_index()` instead of the capture's records (the fields map one to one: flags, damage's dead bit, type, sphere, links, parent; a biped's flags, player, seat, pill). A record missing (an object made this tick, a slot past 1,024, a child biped's pill) is the box's own `-1` answer: the sweep goes back to the main CPU's serial path. Before the ME reads, the main CPU writes back the refreshed records' lines (~85 a tick by range, under the ~480-line by-range limit, `hardware_facts.md`), and the ME's job invalidates the same lines in its own cache before its first sweep (the refresh list goes with the job).
  - **Biped motion's parallel form:** another biped's position, velocity and pill from its biped record; its own biped from its unit section (biped motion's current copy).
  - **wsoldcheck counts the reach** now: at each object the engine's sweep visits, the copy against what the engine reads; the share that parts is how often a sweep on the copy would see a different world (the hazard `box_biped_motion.md` Open 1 named).
- **Budget, each unit:** **main CPU** <=0.2 ms a tick on b30's beach and c20 for the refresh and the marks (~105 records of ~4 datum lines read and ~85 lines written, plus ~300 one-bit marks) [U]; the PSP's pair prices it. **ME:** reads only, no write; its reads replace the sweep's datum reads and are the sweep box's to budget. **GE:** none. **Memory:** 40 KB.
- **Proof:** with `wsold` on, play and both seeds identical to the same build with it off (statecrc, `psp/sierra/statecmp.py`), on held b30_leg2, c20_leg2 and d20_play1, seeds 1 and 2; `wsoldcheck` reads no unmarked change. The cache model's three figures (I-misses, D-misses, cycles a tick) against the same build with it off, as a lead only [E]: the PSP prices it, once the sweep's falsifier (`sweepme-falsifier`) and biped motion's held pair (bm1c-held) are read.

**Results** (Sierra-III builder/wsold b3ad7640; the lab's sierra-batch 44477000214, wsoldid1, held, 600 ticks a run, strict, 0 sites) [E]:
- **Exact:** with `wsold` on, play and both seeds identical every tick to the same build with it off, on b30_leg2, c20_leg2 and d20_play1, seeds 1 and 2; with `wsoldcheck`, identical too (`psp/sierra/statecmp.py`).
- **The marks are complete** on all three: after every refresh every live record checked (b30 563,792, c20 353,720, d20 540,750 over 600 refreshes), **no change a mark missed** in any field.
- **Bytes a tick:** b30 144.7 objects + 28.9 bipeds a refresh, **6.5 KB**; c20 102.7 + 62.0, **7.3 KB**; d20 84.6 + 52.5, **6.1 KB** (the first refresh fills all 1,024 slots once). Above the ~5.3 KB estimate: the bipeds held are all refreshed, and c20 holds 62.
- **Slots:** b30 used to 965, c20 597, **d20 1,070: past the 1,024 slots** (350 marks past them over 600 ticks, objects with no record). ~~Open: the slots go to 1,152 (+4 KB) or the full 2,048 (+32 KB)~~ **Fixed 2026-11-19** (Sierra-III builder/sweepme 03a58130; the lab's sierra-batch 44520688818, wsoldid2, findings `wsoldid2_overflow`) [E]: the copy holds all 2,048 slots; d20 uses 1,070 of them, **0 marks past them, 0 bipeds without a record**; with `wsold` on, play and both seeds identical every tick to it off on d20_play1 seeds 1-2 and b30_leg2, c20_leg2 seed 1 (600 ticks, strict 0); `wsoldcheck` on d20: 568,304 records checked, no change a mark missed. A fixed 1,152 was rejected: it holds d20 and nothing says another map (b40's drive, d40) stays under it.
- **What it costs a tick and what it buys** (the builder, 2026-11-19, after bm1c-held: biped motion's 1b record and its window measured exact and slower on the PSP, b30 +0.5 to +1.6 ms a tick, c20 +3.7 [M, the lab's bm1c_1b_psp_b30, bm1c_window_psp_b30, bm1c_window_psp_c20]: the record's accessor cost more than the datum lines it saved). **The cost** [E counts, U lines]: the bytes above written (b30 6.5 KB, ~101 lines); the datum lines read for them, ~4 an object (+0-63: flags; +64-127: sphere, type; +128-191: damage; +192-255: links) and ~5 more a biped (unit +456, +672; biped +1060, +1124; the pill's definition), so **~725 lines read a tick on b30**, ~720 on c20, ~600 on d20; ~300 one-bit marks. **The indirection:** none on any existing read. Nothing reads the copy for play, and a reader written for it takes one record pointer an object (`box_ws_old_object_of()`, one compare of the identifier) and reads its fields from that pointer: the opposite of 1b's per-field accessor, which chose between two places at every field (277 sites inlined). **What it buys:** nothing on the main CPU alone. It's a cost the main CPU pays so that a reader on the ME can read a world the main CPU isn't writing. It pays only if the sweep and biped motion run on the ME (line (b); `sweepme-falsifier` decides it); as a main-CPU-only change it would most likely lose, as 1b did. So it stays exact and off by default, and the PSP prices it only beside the ME's sweep, as one pair.
- **The cache model's three figures a tick, the tick phase, `wsold` off -> on** [E, a lead only; the lab's sierra-batch 44477046311, wsoldcache1, b3ad7640, one build; I-misses / D-misses / cycles = fetches + I x 56 + D x 51]: **b30_leg2** (3,900 ticks) 31,322 / 24,985 / 5.408 M -> 31,367 / 25,827 / 5.499 M (**+1.7%**); **c20_leg2** (4,500) 42,858 / 25,118 / 6.160 M -> 42,907 / 26,096 / 6.252 M (**+1.5%**); **d20_play1** (3,000) 23,494 / 21,624 / 4.560 M -> 23,543 / 22,405 / 4.653 M (**+2.1%**). The drawing and the rest unchanged. The cost is data: +842, +978 and +781 D-misses a tick, about the ~600-725 datum lines the refresh reads plus the ~100 lines it writes. It's the main CPU's price for a copy the ME would read; the PSP prices it only beside the ME's sweep.
- **The copy's reach at the engine's sweep** (the objects its walk read, against the copy): **b30 1.97%** of 5,026 visits parted (sphere 99, pill 1), **c20 0.11%** of 5,390, **d20 6.72%** of 10,545 (sphere 692, pill 184, links 41, flags 10). Nearly all are bounding spheres of objects already updated in the tick: what a sweep on the ME would see a tick old, and what its outcome census will have to show plays the same.

## 10. Biped motion's record priced in pieces, and means B (2026-11-19)

The lead, 2026-11-19 07:5xZ (after bm1c-held: 1b and the window are exact on the PSP and slower, b30 +0.5 to +1.6 ms a tick, c20 +3.7 [M]): the store's first requirement is that **a record costs less than the datum lines it saves, measured**. Price the pieces before the slice is designed, then design means B first. §9's slice code stays on its branch and isn't priced on the PSP alone.

**The split** [E, cache model; the lab's sierra-batch 44486082980 (wssplit1), Sierra-III builder/wssplit b746d108 = the window's build 41adf7d6 plus two recon switches; b30_leg2 to exitat130 (3,900 ticks), c20_leg2 to exitat150 (4,500), fixedticks fixedseed; the three figures a tick, cycles = fetches + I x 56 + D x 51]. Five sides, each piece the difference of two:
- **plain** (`PSP_BMS_PLAIN=1`: every accessor a plain field access): the baseline;
- **off** (the accessor compiled in, never live: what bm1c-held priced as "1b");
- **ghost1** (`bipedmovestore bmsghost1`: every fill, keep, resume, sync and write-back of the window, the accessor never pointed at the record, every record in slot 0's lines);
- **ghost** (`bmsghost`: the same, a record a slot, so the 64-record kept set takes its cache room);
- **window** (`bipedmovestore`).

In ghost, each copy of a record to the datum is refilled from the datum first, so it writes back what's there: play and both seeds identical to off, and plain and ghost1 too (the lab's sierra-batch 44486120039, wssplit1id: b30_leg2 held, seed 1, 600 ticks, strict). That refill adds a datum read and a record write before each write-back, so the copies' piece is a little high.

| Piece (tick phase, a tick) | b30 cycles | b30 I / D | c20 cycles | c20 I / D |
|---|---:|---:|---:|---:|
| plain | 4.992 M | 27,213 / 23,235 | 5.170 M | 32,551 / 21,992 |
| the accessor compiled in (off - plain) | **+0.084 M (+1.7%)** | +1,974 / -802 | **+0.160 M (+3.1%)** | +3,820 / -1,255 |
| the copies: fills, keeps, resumes, syncs, write-backs (ghost1 - off) | **+0.182 M (+3.6%)** | +645 / +682 | **+0.494 M (+9.3%)** | +2,004 / +1,788 |
| the kept set's residency (ghost - ghost1) | -0.001 M (0.0%) | -33 / +120 | +0.024 M (+0.4%) | -76 / +491 |
| the accessor live: the record's reads against the datum lines they save (window - ghost) | +0.003 M (+0.1%) | +478 / -23 | +0.027 M (+0.5%) | +2,054 / -91 |
| **the window in all (window - plain)** | **+0.268 M (+5.4%)** | | **+0.704 M (+13.6%)** | |

The drawing (the frame's other phase, same runs): b30 plain 3.633 M, off 3.635, ghost1 3.638, ghost 3.631, **window 3.621 (-0.3% against plain, -0.4% against off)**; c20 2.675, 2.694, 2.698, 2.694, 2.696 (flat within 0.8%).

**What it says** [E counts; U where marked]:
1. **The copies are most of it**: two thirds of b30's cost and 70% of c20's. Not the fill alone: the window copies the whole 540 B record out and back in at each of its 21 synced helpers (`PSP_BMS_SYNC`), and again at keep, resume and end [D, `psp_bipedmove.h`, BIPEDMOVE_SYNC], so a biped's record crosses the datum several times a tick. Their I-misses (+645 / +2,004) are the copy loops and the sync calls themselves.
2. **The accessor's compiled cost is the second piece** (+1.7% / +3.1%), almost all instruction misses: 503 inlined compare-and-choose sites grow the window's code, whether or not a record is ever live.
3. **The record saves no data misses**: the accessor live costs instructions and saves ~0 D-misses (-23 / -91 a tick), because the datum lines it would spare are read anyway by the fill and the syncs. So today's record can't pass the requirement, however the kept set is sized.
4. **Residency is near zero in the tick** in the model (34.5 KB kept: 0.0% / +0.4%). **The drawing:** the model shows b30's drawing -0.4% with the window and nothing on c20; the PSP showed the drawing 0.5-0.8 ms faster wherever the window read (the lab's bm1c_window_price_b30, bm1c_window_price_c20) [M]. Why is open [U: the window's last write-back leaves the bipeds' hot datum lines in the cache in the order the drawing then walks them, or the kept records displace lines the drawing would otherwise miss on]. Any design is counted on both phases, and the PSP pair reads both.

**Means B, designed from the split** (the lead's 07:5xZ: records filled once a tick in one sequential pass and read by plain pointer; the kept set sized to the D-cache, or to eDRAM for the ME):
- **One fill, one write-back, no syncs.** After the unit pass, one sequential pass over the staged bipeds fills each one's record; after the solves, one pass writes back the fields the window wrote. Nothing in between copies a whole record. The record holds two kinds of field: **read-only in the window** (filled, never written back) and **written only by the window's own code**, written back once. A field any synced helper reads or writes inside the window stays on the datum, so no helper needs a sync. Where a helper needs one written field, a narrow span goes out and back (as `object_translate`'s 4 B today), counted. The census that sorts the fields: the 21 synced helpers' datum reads and writes inside the window, against the window's own writes (`census_phase4_unit`'s method, the lab's), so the record is what's left.
- **Read by plain pointer, settled when the code is built.** The pass that rewrites the window's accesses decides each one when it rewrites it, from the constant map: `rec->field` where the record holds the field and the pointer is the window's own biped, `datum->field` otherwise. No compare at run time. A function gets `rec` once at its entry, from the staged slot (`psp_bs3_cur`), not once per access. A site whose pointer may be another biped (the bump's reads of others) stays on the datum, or later reads the other's record by its slot, also a plain pointer. This removes piece 2 and the live accessor's instruction cost (piece 4 above).
- **Small enough to stay resident.** Target **<=256 B (4 lines) a biped**, so the set is 6.4 KB at b30's 25 staged bipeds and ~10 KB at c20's 39, under the 16 KB D-cache with the collision walk's lines beside it [U]. For the ME placement the set goes in eDRAM instead: 64 x 256 B = 16 KB of the approved 107-180 KB. Today's 540 B a record is sized by the census's fields, not by what the window reads hot.
- **Who reads the kept lines, and when:** the tick only. The intent fills and reads them, the solve and the tail read and write them in the same tick, and the write-back ends their life before posing and the drawing. The drawing reads the datum, after the write-back. So the drawing's side is the write-back's order and the datum lines it leaves in the cache: the write-back pass walks the bipeds in the order the drawing's object walk does, and both phases are counted in every figure.
- **The requirement, measured:** the cache model first, on the same five-side form against **plain** (not off), on b30_leg2 and c20_leg2, tick and drawing. It goes to the PSP only if its tick figure is under plain. Then the PSP pair is plain against means B on held b30_leg2 and c20_leg2, without statecrc, tick and drawing by stretch. The lead orders it.
- **§9's slice** is already this shape: one refresh a tick in one pass, read by one record pointer an object, no syncs. Its cost (+1.5 to +2.1% [E]) is the refresh's datum reads, and it buys only on the ME. It waits on the falsifier, and on means B proving a record can pay on the main CPU.


## 11. Means B built and proven (2026-11-19)

Built as §10 designed it on Sierra-III builder/meansb (663ae6cb, `PSP_BMSB=1`, keep word `bmsb`). The census, the form and the figures are in docs/box_biped_motion.md §1d; here, what it says for the store.
- **The census sets the record:** the fields only the window's own functions touch from the fill after the unit pass to the write-back after the solves.
  - It came to **60 B, ten runs**: five read-only, five written.
  - §10's ≤256 B target was never the limit. The helpers, posing and the other bipeds' windows touch everything else biped motion uses.
  - The kept set is 64 x 60 B, under 4 KB.
- **Exact** [E] on b30, c20 and d20, seeds 1-2, and with the keep word off (the lab's msbid1).
- **The cache model, a lead only** [E] (the lab's msbcache1; the lead 09:3xZ):
  - Tick against plain +0.8% (b30) / +1.2% (c20), where 1c's window was +5.4% / +13.6%.
  - D-misses -1.6% / -4.1%: **the first record form that saves datum lines**, so a record can pay against §10's requirement.
  - I-misses up more. Most of that is the text layout not placing the new code [U].
- **The PSP pair decides** (plain against means B, held b30_leg2 and c20_leg2, repeat 2): the lab's coord/psp_queue.md, `msb-held`.
- **For §9's slice:** means B shows a once-a-tick fill is cheap when the record holds only the window's private fields. The slice's sweep inputs are read by others (posing, the gather), so on the main CPU they'd stay on the datum. Only the ME's placement needs them copied.

## 12. The copy's first reader: the ME's pill sweep (2026-11-19)

The lead's order of 2026-11-19 11:5xZ: after bm1c-held and msb-held (the record forms on the main CPU measured slower on the PSP), **the store's first slice is the ME's input**: the one-tick-old copy (§9) for the pill sweep on the ME. The design is `box_collision.md` §11; what it asks of the store:

- **Slice 1 doesn't read the copy.** The level's half of the gather (65% of a sweep [M]) reads only the level, which nothing writes in a tick, so it's made on the ME exact without the copy (`PSP_SWEEPME=1`). The copy's cost (+1.5-2.1% a tick on the main CPU [E], §9) is paid only with slice 2. A `PSP_SWEEPME=1` build needn't set `PSP_WSOLD=1`.
- **Slice 2 reads it for the objects' half:** each object the cluster walk meets from `box_ws_old_object_of()` (sphere, flags, type, dead, the child links), a biped's pill from `box_ws_old_biped_of_index()`. Two parts the store doesn't hold yet, both refreshed on the marks it already has:
  - **the clusters' collideable lists as the walk began** (§9's "second part"): per cluster, its objects and their sphere copies, refreshed for the clusters a reconnect touched (`object_reconnect_to_map`'s mark); sized from the cluster references' maximum (2,048 a partition, `psp_capacity.h`), ~20 B an entry;
  - **each collision model's live nodes' matrices** (the nodes that reach a BSP: the lab's `collnodes1_collision_node_sets`), refreshed where node matrices are made (`object_compute_node_matrices_with`, `psp_nms_make`, both already marks). Its size is from that census, not guessed: open.
- **When the ME reads it:** the copy is taken as the walk begins and nothing writes it until the next tick's walk, so slice 2's records can be handed as early as the intents end (as slice 1's are) or earlier, with the main CPU writing datums meanwhile, no lock (§9's rule).
- **Its proof** (slice 2, not exact): the outcome census and play side-by-sides on b30_leg2, c20_leg2 and d20_play1 against the build with `sweepme` off; the copy's reach at the sweep (§9: 0.11-6.72% of visits part, almost all spheres of objects already updated in the tick) is where play can part.

## 13. The count before the rewrite: `PSP_TICK2=1` (2026-11-22)

The lab's tick rewrite plan (Sheepdog01 2026-11-22) makes the store the one owner of the bipeds' motion, collision-query and posing state: those fields leave the engine's datum structs, so old code that touches them can't compile. Slice 0 is the count of that old code, taken before any of it is rewritten.

- **The switch:** `PSP_TICK2=1` runs `psp_fixups.py`'s `tick2_poison` over the mirror. Each owned field keeps its place and size in its struct under the name `tick2_owned_<field>`, so the layout, every `sizeof` and every static assertion stay as they were and the only compile errors are touches of the old names. The build stops before the link; `compile_errors.txt` is its output.
- **The fields, in three tiers by how far a struct is shared** [D]:
  - **A, `_biped_datum`, whole** (27 fields; bipeds only).
  - **B, `_unit_datum`** (shared with vehicles): motion's own (`aiming_vector`, `aiming_velocity`, `looking_vector`, `looking_velocity`, `animation`, `mouth_aperture`, the four seat motion fields) and the controller's hand-over (`control_flags`, the three desired vectors, `throttle`, `aiming_speed`, `primary_trigger`, `current_zoom_level`).
  - **C, `_object_datum`** (shared with every object type): `position`, `translational_velocity`, `forward`, `up`, `angular_velocity`, `location`, the bounding sphere, `animation` and the three node block references.
- **Left out:** `object.flags` and `unit.flags` (their motion bits are named with the list), damage and vitality, the family links, function values and colours, speech, weapons and seats' indices.
- **What it can't see:** reads by raw offset from a datum pointer, whole-datum copies, and code the campaign build doesn't compile.
- **State:** the pass is built; the list is the lab's to read from the build's errors.


## 14. The store's ROOT section: `PSP_TICK2=root` (slice 1, step 1, 2026-11-22)

The first section the store owns for every object type: the object's root (`position`, `translational_velocity`, `forward`, `up`, `angular_velocity`, `location`, the bounding sphere), 84 bytes a record (the lab's `tick_store_spec_v1.md` §17-18, §23).

- **Where it lives** (spec §26): each map's records are carved from the tail of the empty object pool when the map starts (`memory_pool_initialize` at the smaller size), with their free stack, all in the game state so a checkpoint's revert keeps them together. The count is the larger of a map's peak of live objects on today's tock and in the memory sheet, plus 15%, rounded up to 16 (the lab's census27 over every ledger2 leg, d20_play1 and c10_play1, the latter unheld across the whole level; a10 and d40 from the memory sheet): a10 432, a30 944 (C5's 815 on its fullest pool tick, above the census' 673), a50 1,408, b30 1,296, b40 1,184, c10 1,440, c20 896, c40 1,088, d20 1,296, d40 528; 1,280 for any other map (`tick2_slots_for_map`). The count is a target: past it the store overflows (below), so a map whose pool peaks with few objects (a30: 960 KB at 815) gives the store only what it uses. A datum keeps one pointer to its record, taken in `object_header_new` after the datum's memset and given back when the object is deleted.
- **Overflow** (spec §27): past the map's own records, an object's record comes from an overflow chunk of 64 records, an object pool block held through the pool's block reference (so compaction moves it like any block), and given back to the pool when its last record is freed. After each compaction of the object pool, the records of a chunk that moved are reached at its new place (one walk of the live objects, only when a chunk moved). So the store refuses an object only where the old pool would: the map's count is a target, not a cap, and every run's collector line says how many records overflowed (overflow on a bench is a finding: the table is short there).
- **Why not a record per slot** (step 1b's first form, tick2root3 [E]): 2,048 records cost the pool 168 KB while the datums gave back 80 bytes a live object (~84 KB at d20's 1,070), so the pool ran out on d20 and c20 (`ARRAY FULL: objects (the pool)`; the collector had nothing to delete, the objects were live projectiles).
- **What the engine sees** (`psp_fixups.py` `tick2_root_cut`): every `x->object.<field>` and `x.object.<field>` touch becomes `x->object.root-><field>` (1,097 touches at the 0.9.1a pin, biped motion's window macros included). The eight fields leave `_object_datum`, so every datum is 80 bytes smaller: the 16 datum sizes in `object_types.c` and the engine's layout checks past the root run (28 at the pin) move by 0x50, checks written over several lines included. A layout check inside the run stops the build.
- **The collector** (spec §23): its pool tests in `objects_garbage_collection` read the pool's free size plus the store's free records (`TICK2_STORE_SPARE()`), and its free-objects test reads the store's free records where they bind before the datum array's slots (from ~1,178 live objects, above every map's peak seen). The pool itself is smaller than today by the records' block less what the live datums give back (80 bytes each): at a map's peak of live objects that's small, and the collector line reports it [D].
- **The sleep's checksum** reads the record's words after the datum's, so a root that changes still wakes its object.
- **The collector's line** (`gccount`, on with this switch, and on the old build with `PSP_GCCOUNT=1`): at each map's end and each minute, the collector's deletions, the pool's size, its peak in use and its free low (the pool's own figures), the peak of live objects, the store's records and peak bytes, and the overflow records taken and their peak. The two builds are held to the same deletions on the ledger legs (spec §18.3).
- **Proven** [E] (the lab's tick2root9, 2026-11-23): play and both seeds identical to the old build at every tick on a10, d40, b30_leg2, c20_leg2 and d20_play1, the collector's deletions the same; `docs/review_tick2_slice1.md` has the steps and the fixes they took.
- **Step 1a** (the same records with the datum's layout unchanged, a union in the fields' place) was proven exact against the tock on b30_leg2, c20_leg2, a10 and d40 [E]. It isn't kept: step 1b is the shrunken datum above.
- **Biped motion's window** (`bipedmovestore`, off by default) can't run in this build: its store and byte map are laid out on the old biped datum. Its accessors are plain datum accesses here (where every access lands today with the word off) and the word logs that it's off. Slice 1's store takes its ground.
- **Opt-in checks that read raw datum words** (funccheck, exportcheck, wakecheck): funccheck and exportcheck update an object on trial and put its datum back. Since 2026-11-23 they put its ROOT record back too, and its ANIM record under `PSP_TICK2=anim`, so their trials no longer leak into play. Their comparisons still cover the datum's words alone, which is where the functions they test live. wakecheck only reads: its sum includes the records (`psp_object_checksum`), but when it names the words that changed it lists only the datum's, so a miss inside a record shows as "no word of its first bytes changed".

## 15. Step 1c-2's design: the unit's animation (draft for the lead, 2026-11-23)

Step 1c-1 (`PSP_TICK2=anim`, §14 and the lab's spec §31) moves `object.animation` out of the datum and into ANIM's array, indexed by ROOT's slot. Step 1c-2 does the same for `unit.animation`. This section is the design, written before any code; the lead rules on the open questions. [D] means read in the code; [U] means inference.

**What moves.** `struct unit_animation` is 72 bytes, on bipeds and vehicles only [D]. Every write and read is in units.c (209 touches), bipeds.c (25), and six in the AI and the director's camera [D]. Its readers fall in two groups:
- **Posing** (`unit_preprocess_node_orientations`, 34 touches) reads the action, overlay and soft-ping animations, the aiming and looking screens (index, euler flag, bounds), the emotion, and the seat and weapon indices [D].
- **Motion and the unit update** (`unit_update_animation`, `unit_animation_set_state`, bipeds.c) read and write the state, the desired state, the base seat and the flags. The AI reads the state [D].

The struct is also passed by address in nine places [D]: `unit_animation_busy` (five calls; it reads only the state), `unit_animation_aiming_screen`, `unit_animation_vehicle_ik`, posing's local pointer, and one argument of the animation update.

**Open question 1: whole now, or split by writer now.** Spec v1.1 item 2 splits unit.animation by writer: BIPED takes motion's part, ANIM takes posing's. BIPED doesn't exist yet, so a split now would park motion's fields somewhere temporary, and the nine by-address helpers would need rewriting to take two parts. **Recommendation: move the struct whole into ANIM's unit record in 1c-2.** The helpers keep working unchanged on the record's address, so the proof stays the cheap exact one. Motion's fields (state, desired state, base seat, flags; about 8 bytes) then move from ANIM to BIPED in the step that builds BIPED, with each by-address helper rewritten then. That step has to touch them anyway.

**Open question 2: how a unit record is found.** A full slot-parallel array (like 1c-1's) costs every slot 72 bytes. On a30 that's 944 × 72 = 66 KB, against the roughly 9 KB that a30's 131 units (C5's census: 125 bipeds, 6 vehicles) give back. That's 57 KB worse on the tightest map, so it breaks §18's zero-net rule [U, from the table's counts]. Options:
- **(a) A unit index in ROOT's record (recommended).** ROOT's record gets a 2-byte unit index (it has room before its 16-byte rounding; check against `sizeof(struct tick2_root)` at build). ANIM's unit records are an array sized per map by peak live units + 15%, with a free stack and 16-record overflow chunks like ROOT's. One inline function turns a record into its unit index, the way `tick2_slot_of` does for slots, so the form can change in one place. Cost: one more load per access, from ROOT's line, which the accessor has usually just read.
- (b) The slot-parallel array, with the 57 KB found elsewhere. Rejected on bytes unless the lead wants the single key badly enough to pay it.
- (c) A pointer in the datum, like 1b's. It works, but it's the second pointer the lead turned down for 1c-1.

**What 1c-2 needs before code:**
1. **A per-map unit census:** each map's peak live units (bipeds + vehicles), for the table. The gccount line gains a peak-units count, so census27's runs can be read again cheaply. C5's counts are from one tick each, not peaks.
2. **§28's literal sweep for unit.animation's old offsets** (its byte range in the unit datum, word indices included), in the port's text and the engine's, before the proof is traced (spec §31).
3. **Layout:** unit datum sizes drop by 72 bytes. The engine's layout checks past the field move by 72 more; one inside it fails the build. Same two-run shift as 1c-1, plus a third run for unit-family datums only.
4. **Proof:** as 1c-1. The old build against `PSP_TICK2=unit` (which implies anim and root) on the six maps, a30 included, with the per-tick animation hash from the tracer (`animsum`), which now covers the unit records too.

1c-2 starts once 1c-1 is exact on a30.
