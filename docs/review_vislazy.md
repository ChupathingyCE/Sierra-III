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

An owed compute stays owed however long the object goes unread (nmskip's 8-tick limit is dropped under vislazy; a game time later than now is a revert, and is dropped). An object deleted while owed leaves a stale entry that its slot's next object releases.

## Proof

(filled from the runs)

## Cost and gain

(filled from the runs)

## Logs

- `vislazy: N ticks ...: M computes of scenery and its kin on the others, K left out (S replaced by a later compute, R made for a reader, ...); kept: attached, collision model (0 under vislazy), interpolating, root animated, root moved or unknown, no room, drawn last frame` every 900 ticks; `vislazy: frames listed; computes owed now / peak; catch-ups by reader (site:n)` (sites name the reader's return address: read them with the build's link map).
- `vislazycheck: the skips' test wrong X of Y; catch-ups differing D of C; the computes it would leave out T ms a tick`, and a line per differing catch-up naming the object, its tick and the reader.
