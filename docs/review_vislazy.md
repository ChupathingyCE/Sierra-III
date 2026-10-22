# VISLAZY: undrawn objects' node matrices made only when read

Keep words: **on by default**; `vislazyoff` turns it off; `vislazycheck` makes every compute as before and checks the lever's claims. `nmskip` / `nmskipcheck` (the shelved frame's-end form) win over it when named. Code: `psp_fixups.py` FIXES_NMSKIP (objects.c, collisions.c, render_objects.c, game_state.c).

## Why

Every awake object's animation and node matrices are made on every tick, whether or not anything looks at them. A census of where that work goes (an instrumented recon build, each compute judged at the next frame's draw) found that the computes of objects the next frame doesn't draw and nothing reads before it are 17-22% of animation and node-matrix time on b30 (97% of it scenery), 32-45% on a30, 50-54% on c20, 15-43% on d20 and 7-33% on a10 [E]. Animation and node matrices are 19-40% of a busy tick on the PSP [M].

## What it does

The frame's-end skip (nmskip) already had the machinery: a compute left out keeps the inputs it would have read (position, facing, scale, flags, animation state, parent, function values) and its game time; a read through either accessor makes it first from those inputs, then puts the object's present inputs back; a compute anywhere replaces it. VISLAZY runs that skip on every tick instead of a frame's first, for an object the last frame didn't draw, so its compute stays owed until something reads it.

**Which objects** (as nmskip): scenery, sound scenery and placeholders that hold and are held by nothing, not interpolating, whose animation leaves the root alone (no node-0 channel, no overlays) and whose root inputs are those of the last compute. Such a compute would write the root node and bounding sphere it already holds, so the sphere every other system reads directly stays exact without a compute; only the other nodes are owed. These types can't interpolate (no node orientations kept) and have no postprocess. Unlike nmskip, a collision model doesn't keep an object out (its readers are caught below). Not drawn: the renderer's lists (`find_rendered_objects`, every view of the frame) mark the frame's objects; the first list after a tick starts the set afresh. Which objects are left lazy decides only the gain, never the result: any object's owed compute is made exactly when read.

**Bipeds, weapons, vehicles and machines are not in it** (c20's Flood is half of c20's share). They have children, postprocess (aim and look), node orientations kept for interpolation, and their markers are read the next tick by their AI; each needs its own input record. Left as the next step.

## The readers, and how each makes the compute

| Reader | How |
|---|---|
| Everything through `object_get_node_matrices` / `object_get_node_matrix`: markers (`object_get_marker_by_name`, the AI's next tick), `collision_model_instance_new` (every collision test on the main CPU, bullets, hits, line of sight, physics), attachments and children's computes, effects and particles at markers, object lights, the renderer (`render_objects.c`), shadows | the accessors' owed bit, tested in line; the catch-up runs from the recorded inputs and game time |
| The Media Engine's perception rays (collisions.c, `psp_collision_test_vector_quiet`): `collision_model_instance_new` there can't make a compute on the ME | a ray reaching an owed object (its bit, written back before the hand-over; no skip happens while a job is out, since a skip and a compute both settle the ME first) returns -1: the ray is the main CPU's, through the accessor |
| A deactivated object (leaves the loop) | made at deactivation (nmskip's hook) |
| A game state saved (a checkpoint) | every owed compute made first (`before_save_procs`), so a revert loads the matrices it would have |
| A game state loaded, a map disposed | nothing owed (nmskip's hooks) |
| Render interpolation (off on the PSP) | reads through the accessor every tick: exact, and the lever then does nothing |
| Direct reads of the block | none for these types: the other direct readers are the compute itself, the postprocess (not these types), biped limp noodle (bipeds), the allocation |
| Writers (set position, attach, a new animation) | need nothing: the catch-up reads the inputs recorded at the update, not the present ones, as the eager compute did |
| The sleep check (`psp_sleepy_update`): an object whose update leaves its datum's checksum unchanged two ticks running falls asleep and is no longer updated. The node matrices are in the datum, so it reads them without an accessor | the skip of an object whose animation plays marks the update as a change (`psp_vl_moving`): the eager compute would have rewritten its nodes, so it stays awake as it would have. A skip of an object with no animation playing changes nothing the eager compute wouldn't (its default pose from the same root inputs), so it sleeps as before |

The sleep check was the gap: with its compute left out, a swaying tree's datum stopped changing, so the tree fell asleep, stopped updating and kept a stale pose, where the eager build keeps it awake and moving. b30's Pelican, made at a tick when two such trees (with collision models) were asleep, was placed against their stale nodes. `vislazycheck` couldn't see it, as it makes every compute (the trees stay awake there). Found by keeping one kind of object out at a time (the run is exact with no collision models, no scenery or no animations in the lazy set, and still differs with every owed compute made at the objects loop's end) and tracing that tick's reads, computes and skips.

An owed compute stays owed however long the object goes unread (nmskip's 8-tick limit is dropped under vislazy; a game time later than now is a revert, and is dropped). An object deleted while owed leaves a stale entry that its slot's next object releases.

## Proof

All in the emulator, campaign PGO build, fixedticks fixedseed nomouth, VISLAZY on against `vislazyoff` [E]:

| Map (stretch) | Ticks | Play hash | Global seed | Local seed |
|---|---|---|---|---|
| b30 (the timed route: cinematic, flight, landing, beach) | 8,400 | same at every tick | same | same |
| a30 (timed route) | 9,000 | same | same | same |
| c20 (route t5) | 8,100 | same | same | same |
| d20 (swamp route) | 3,000 | same | same | same |
| a10 (hands off) | 4,800 | same | same | same |

The whole-state hash differs by design (owed node matrices sit in the objects allocation). Before the sleep check's fix, b30's play parted at tick 4351 (the Pelican made at the landing), with the local seed following at 4374; the other four maps were already the same.

`vislazycheck` on b30 and c20: the skips' test wrong 0 and catch-ups differing 0 in every 900-tick window (b30: 46,437 skips and 4,710 catch-ups in the first). It can't see a reader that bypasses the accessors (it makes every compute), which is how the sleep check got past it; the play proof above is the one that covers those.

## Cost and gain

Removal in the emulator [E]: the check word's "computes it would leave out" times the share of left-out computes no reader asks for (the `vislazy:` lines). b30: 0.22-0.42 ms a tick of computes left out, 88% never made (275,251 left out, 31,655 made for a reader): about 0.2-0.37 ms a tick. c20: 0.01-0.10 ms a tick, 51% never made. a30 50%, d20 36%, a10 52% never made. Animated scenery now stays awake as it does without the lever, so these are what the lever removes from the eager build; an object's update still runs, only its node walk is left out.

## Logs

- `vislazy: N ticks ...: M computes of scenery and its kin on the others, K left out (S replaced by a later compute, R made for a reader, ...); kept: attached, collision model (0 under vislazy), interpolating, root animated, root moved or unknown, no room, drawn last frame` every 900 ticks; `vislazy: frames listed; computes owed now / peak; catch-ups by reader (site:n)` (sites name the reader's return address: read them with the build's link map).
- `vislazycheck: the skips' test wrong X of Y; catch-ups differing D of C; the computes it would leave out T ms a tick`, and a line per differing catch-up naming the object, its tick and the reader.
