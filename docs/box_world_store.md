# Box: the world store (bottleneck, 2026-11-11; v0, spec in progress)

The lab's `research/opportunities/REBUILD.md` is the spine (§3.3, §4, §7 step 1). This page follows its §4 layout. Labels: **[M]** measured on a PSP, **[E]** emulator or a count, **[D]** read in code or docs, **[U]** inference.

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

**Today's layout** [D, the datum layouts as the port compiles them, the lab's `psp/sierra/datum_layouts`]: one datum per object, its type's whole struct in one block: biped 1,152 bytes, vehicle 1,148, weapon 636, projectile 552, scenery 424, machine 472, light fixture 476, equipment and garbage 476, control 460, device 452; the node orientations and matrices follow each datum as header blocks. The pool is ~1 MB on b30 (the lab's findings `tickwrites_kb_a_frame`).

**What the tick touches of it** [E]:
- a biped's update touches **497 words (1,988 bytes) in 40 lines that half of all updates touch**, which packed would fit 32 lines; weapons 11 of 23 lines (13 hot, 8 packed); vehicles 24 of 51 (33 hot, 23 packed); scenery and machines one line for one word each (the lab's findings `objhot_biped_lines_touched`, cachecensus1 41308914906);
- the tick writes **163-236 KB a frame of object data** on b30 (objects, lights, particles; the AI's props and actors 46-98 KB) (the lab's `tickwrites_kb_a_frame`, snap1 40903127317);
- the costly steps' loads are **~97% their own family or last tick's state**: a parallel phase reading the tick's start is consistent with them (the lab's `tickcore_census`, 42927567774). The stores that need care are named there: collision's shared scratch, the walk's bookkeeping, the clusters' lists, the AI's shared scratch, the global seed, function values written into other objects, and fresh reads of others' nodes.

**The store's layout:** per type, the fields each box reads and writes every tick, grouped by owner; its first cut follows the map below. **Open (2026-11-16) [D]:** the first cut isn't one owner per field yet. In `psp/src/box_world_store.h`, a weapon's `triggers_2[72]` sits in `box_ws_weapon_read`, `box_ws_weapon_weapon` and `box_ws_weapon_walk` (lines 548, 570, 586), and `magazines_2[24]` in two of them. Used as the working store, that duplication is copies and syncs where the store means to be dense. It was found by GPT-6 Astra Pro's read in the lab's outside round two and checked against the header.

**Open (2026-11-18, biped motion's 1b) [D]:** the v0 biped section lacks 14 fields biped motion's intent reads or writes (the lab's findings `intent_fields_vs_store`, a DeepSeek read checked against this header). They include the biped's whole tail from +1060: `biped.flags`, `state`, `support_surface_index`, `airborne_ticks`, `slipping_ticks`, `landing`, `crouch`, `bank`, `ground_plane`. Also `unit.body_stun` +980, `object.recent_body_damage` +168, `angular_velocity` +60 and `looking_screen_index` +588. The section stops at +1118. Several of these are touched on every living biped's update, so the census's half-the-updates rule, or its counts, missed them [U]. Biped motion adds them as a hand-written addition beside the generated header (`box_biped_motion.md` §1c). The census rule wants a look before the other types' sections are trusted.

**The read and write map, per box** [E counts: the lab's worldmap1 (sierra-batch 43064891428: b30, b40, d20, a30, ~17,000 ticks, emulator item 29's per-step field counts), read by the lab's `psp/sierra/worldstore.py --boxes`; the whole map is the lab's `research/opportunities/worldstore_map_2026-11-11.txt`]. Each tick step is put in the box it belongs to (posing, biped motion, AI, effects, vehicles and other objects, the walk, the engine's scripts, players and HUD); a field counts for a box when the box touches it at least 0.1 times a tick, which leaves out an object's creation and deletion (they write a datum whole, once, in whatever step runs).

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

**The store's layout, first cut** [E counts: the same worldmap1 traces, read by the lab's `psp/sierra/worldlayout.py`; its output and word list are the lab's `research/opportunities/worldstore_layout_2026-11-11.txt` and `.tsv`]. The principle stands: per type, the fields each box reads and writes every tick, grouped by owner, in arrays by type (structure of arrays where a walk reads one field of many objects, the datum's own order where a box reads many fields of one object); the engine's cold fields stay in the datum. A word is hot when it's touched in at least half the updates of its type (the walk's reads of `object.type` count the updates; where under a quarter of the live objects are updated, the live objects are the base); its section is the boxes that write it, a writer with under a tenth of the main writer's stores counted as minor (a rare path, or a leaf's step label).

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

**The field list, v0** (`psp/src/box_world_store.h`, generated by the lab's `psp/sierra/worldheader.py` from the cut's word list; a spec that compiles, with each field's store offset checked against the compiler): per type, one struct per owner, each field its whole leaf (a hot word of two shorts takes both) with its datum offset, and the copy runs between the datum and the store. The owners: **unit** (biped motion with the unit's own update), **control** (the controller's record: whatever the AI or the players' input writes), **posing**, **walk** (its export of function values and its sleep bookkeeping), the type's **own update**, **effects**, and **read** (no box writes it every tick). The object's magic number stays in the datum. A biped's 400 bytes: read 107, unit 167, control 66, posing 36, walk 24; a weapon's 506: read 208, its update 174, walk 108, posing 16.

**The fill and write-back's count** [E, at the census's updates]: **18.7 KB filled and 12.3 KB written back a tick, in 1,874 copy runs** (a biped's 50 runs, a weapon's 27). That's the cost of §7's proof stage, where the store is filled from the datums and written back to them every tick with no box using it; it shrinks as boxes are cut over: read-only fields fill on change, not each tick, and a field whose every reader reads the store is no longer written back. Its time is the PSP's to price, in the serial phase (§4).

## 3. Placement

Main RAM, in the layout above; the parallel phase's two copies (last tick's and this tick's) are the double buffer only for the fields the parallel phase reads and the serial phase writes (`two_core_tick.md`, the one-tick-old view). Not eDRAM (2 MB, the ME's code and data and the drawing's); not the scratchpad (16 KB, and the ME can't see it). Code: none of its own beyond the fill and the write-back.

## 4. Budget

- **Memory:** the store's fields, per object, times the live objects of each type, plus the double buffer's share. To set from the map. The heap's room is the limit: c20 ends Sheepdog01's play with 89 KB free [M], and a whole-object snapshot (448-672 KB) didn't fit (the lab's `snapdry_buffer_kb`), so the store must replace datum fields, not copy them, wherever the engine's own readers allow.
- **The first slice (§9):** 40 KB, ~5.3 KB refreshed a tick [U on E], <=0.2 ms a tick of the main CPU, nothing on the ME but reads.
- **Time:** the store has no time of its own; it's paid back in the boxes' misses (means A). Its fill and write-back (§5) are budgeted in the tick schedule's serial phase.

## 5. Means

- **A (data owned and packed):** a biped's hot words fit 32 lines packed against 40 today, and a walk over one field of many objects reads one line per 16 objects in place of one per object [E counts; the price is the PSP's].
- **C (both cores):** it's what lets the parallel phase run: each core reads last tick's copy and writes its own family's fields.
- **B:** none directly; it carries the boxes' "changed" marks (work on change).

## 6. What it replaces

The engine's object datum as the tick's working storage (`object_get` and the datum walks in `objects_update`); the port's earlier tries on this ground: OBJHOT (the hot words packed), DATUMSPLIT, WORLDVIEW (~~declined on its standalone ceiling, Sheepdog01 2026-11-07~~ superseded for this use: Sheepdog01 2026-11-10 16:43, the lab's decisions.md: the one-tick-old world is pursued as the two-core tick's enabler, proven by side-by-sides and the outcome census), Plan C's snapshot (didn't fit, `snapdry_buffer_kb`). Each by `tools/levercheck.py` before the store's design is settled.

**Upstream manifest:** the engine's datum structs and the functions that read them in the tick, at the pin after the rebase (REBUILD.md §5b, §5c): taken on the rebased engine.

## 7. Proof

The store filled and written back with no box using it yet: play and both seeds identical every tick on b30, b40, d20 and a30 (the check build compares the store's fields against the datum's after each step).

## 8. States

- 2026-11-11: spec started; the map's census (the lab's worldmap1) running.
- 2026-11-11: the map taken (§2): per box, its fields read and written, the hot bytes per object, and the 379 fields two boxes write.
- 2026-11-11: the layout's first cut (§2): per type, the hot words by owner; the store ~27 KB of hot words and ~54 KB of hot node blocks at the live counts.
- 2026-11-11: the field list v0 (`psp/src/box_world_store.h`, generated) and the fill and write-back's count (18.7 + 12.3 KB a tick at the proof stage).
- 2026-11-19: the first slice designed (§9): the sweep's object inputs and the bipeds' pill and motion, one tick old, refreshed on change as the walk begins; under `PSP_WSOLD=1`, keep word `wsold`.

## 9. The first slice: the sweep's and biped motion's inputs, one tick old (2026-11-19)

The lab's direction of 2026-11-18 (`research/opportunities/direction_2026-11-18.md` §11, REBUILD §7 line (a); the lead, 2026-11-19 06:1xZ item 4): the store with its one-tick-old copy comes first, and its first slice is the inputs of the first placement, the pill sweep on the ME, and of biped motion. Not the whole hot set. Built under `PSP_WSOLD=1`, keep word `wsold` (off by default); `wsoldcheck` is its check. Code: `psp/include/box_ws_old.h` (the records and the readers), `psp/src/box_world_store_old.c` (the copy), `psp_fixups.py` FIXES_WSOLD (the marks, the refresh's place, the engine's side that makes a record from a datum).

**Decided, before the code:**

- **What the slice holds** [D, the engine's `object_get_features_in_sphere` and `biped_get_physics_pill` read at the v0.9.1a pin; the sweep box's object record, `box_sweep.h` `struct box_sweep_object`]. Two record kinds, indexed by the datum's absolute index (the disc's slots stay):
  - **every object, 32 B (two a line):** the bounding sphere (+80, +92), the flags the sweep and the pill test (+4: invisible, no collisions, movie star; the other bits left out, so a write to them isn't a change here), the child walk's links (next +196, first child +200), the type (+100), the dead bit (+182), and the datum's identifier (a record of a slot's earlier object never answers);
  - **every biped, 64 B:** the pill as the sweep tests it (base raised by its height, height, width; held only for a biped with no parent, whose pill stands on its own position: a child's stands on its parent's node, made on demand), the biped's position and velocity (+12, +24: what biped motion's parallel form reads of other bipeds, the bump), the parent (+204), player (unit +456), seat (unit +672) and the passes-through-bipeds bit (+1060).
  - **Not in it:** the clusters' object lists the sweep's cluster walk follows (the map links, `object_reconnect_to_map`'s), the level (read-only), and anything of a biped's own record (its unit section, `box_world_store_biped.h`, is biped motion's current copy). The cluster lists are this slice's second part: per cluster, its collideable objects as the walk began, refreshed for the clusters a reconnect touched; until they're held, a sweep on the ME reads the live lists, so the slice doesn't yet give the ME a whole world (the lead's ask said the sweep's inputs, and this is the part left).
- **Who writes the current copy, and when:** there is no second current copy in this slice: the engine's datums are the current world, written by the main CPU as today. The store holds only the **old** copy.
- **When the old copy is taken: once a tick, as the object walk begins** (`objects_update`, after the clusters' activation and before the first update), not double-buffered. Between two refreshes nothing writes it, so a reader on the other core reads the world as the walk began while the main CPU writes the datums, with no lock. A second buffer is needed only once a box writes its fields into the store instead of the datum (the current copy moves into the store); then the two flip at the same point. The refresh is **on change**: the objects marked as written since the last refresh, plus every biped held (their records are 2 KB in all; marking each of a biped's writers costs more than refreshing them). The marks: the walk's own updates (`object_update`, which the children go through too), the sleep's wake marks (`psp_wake`: every outside writer the sleep had to know of, and the parents above it), creation (`object_new`), node matrices made on demand (`object_compute_node_matrices_with`, `psp_nms_make`: the sphere moves there), and the child lists (`object_add_to_list`, `object_remove_from_list` and their callers' parents). `wsoldcheck` proves the marks complete: after each refresh every live object's fresh record is compared with the copy, and a difference is a writer no mark caught (counted by field, the first 24 named in the log).
- **Size and bytes a tick:** 1,024 object slots x 32 B + 96 bipeds x 64 B + a 2 KB biped map = **40 KB** of .bss (off the heap, only in a `PSP_WSOLD=1` build). A slot past 1,024 has no record (counted; its reader takes the serial path). Bytes a tick: the refreshed records x 32 B plus the bipeds x 64 B; estimated ~105 objects and ~31 bipeds on b30 (the store's map, §2: the updated objects a tick) = **~5.3 KB a tick** [U on E]; measured below.
- **How the readers switch:** in this slice **no reader reads the copy for play**, so the build is exact with the keep word on (its proof below). A reader switches at its own cut-over, not here, because reading the copy changes what it reads (one tick older): that's "plays the same", proven by side-by-sides and the outcome census then, not statecrc.
  - **The pill sweep on the ME:** its world's object table (`struct box_sweep_world`'s `objects`) is filled from `box_ws_old_object_of()` and `box_ws_old_biped_of_index()` instead of the capture's records (the fields map one to one: flags, damage's dead bit, type, sphere, links, parent; a biped's flags, player, seat, pill). A record missing (an object made this tick, a slot past 1,024, a child biped's pill) is the box's own `-1` answer: the sweep goes back to the main CPU's serial path. Before the ME reads, the main CPU writes back the refreshed records' lines (~85 a tick by range, under the ~480-line by-range limit, `hardware_facts.md`), and the ME's job invalidates the same lines in its own cache before its first sweep (the refresh list goes with the job).
  - **Biped motion's parallel form:** another biped's position, velocity and pill from its biped record; its own biped from its unit section (biped motion's current copy).
  - **wsoldcheck counts the reach** now: at each object the engine's sweep visits, the copy against what the engine reads; the share that parts is how often a sweep on the copy would see a different world (the hazard `box_biped_motion.md` Open 1 named).
- **Budget, each unit:** **main CPU** <=0.2 ms a tick on b30's beach and c20 for the refresh and the marks (~105 records of ~4 datum lines read and ~85 lines written, plus ~300 one-bit marks) [U]; the PSP's pair prices it. **ME:** reads only, no write; its reads replace the sweep's datum reads and are the sweep box's to budget. **GE:** none. **Memory:** 40 KB.
- **Proof:** with `wsold` on, play and both seeds identical to the same build with it off (statecrc, `psp/sierra/statecmp.py`), on held b30_leg2, c20_leg2 and d20_play1, seeds 1 and 2; `wsoldcheck` reads no unmarked change. The cache model's three figures (I-misses, D-misses, cycles a tick) against the same build with it off, as a lead only [E]: the PSP prices it, once the sweep's falsifier (`sweepme-falsifier`) and biped motion's held pair (bm1c-held) are read.

**Results:** see §8 (States) as they land.
