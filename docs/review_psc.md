# Review: PSC, a looping animation's poses kept and made once a map

PSC is an exact speed-up and is **on by default**. `pscexact` keeps the decoded orientations alone, without the local matrices. `pscoff` gives the old path. `psccheck` makes every pose and local matrix the table hands over again, live, compares the two bit for bit and uses the live one. `psckbN` sets the table's cap in KB (the default is 128). It all lives in `FIXES_PSC` in psp_fixups.py and touches only objects.c. Labels: [M] measured on the PSP, [E] emulator, [D] read in code, [U] inference.

## 1. What it does

**The work it removes [D].** An object whose animation runs by itself (`_object_animates_automatically_bit`) takes its frame from game time: `(object index + game time) % frame count`. Scenery's looping animations are these, such as c10's swamp plants. So every tick, every instance calls `animation_get_node_orientations(model, animation, frame)` again, and the instances run the same frames, each at its own offset. That decode reads only the model, the animation (tag data, fixed within a map) and the frame. So its answer can be kept, keyed by (model, animation, frame), and copied after that: the same function on the same inputs gives the same bits. Only an animation whose nodes are the model's is kept. When the decode's checks refuse a pair, it writes the model's own pose instead, which has the model's node count. The first proof build kept those too, and the whole state parted against `pscoff` (counted now as "not the model's nodes": 0 on the five maps).

**The local matrices [D].** In the node chain, each child node is `matrix4x3_from_orientation(child)` and then `matrix4x3_multiply(parent, child)`. The first step depends only on the decoded orientation. For scenery, the table also keeps that local matrix, made by the same function from the same orientation, and the chain multiplies it onto its parent as before. So the node matrices are the same bits. The root is made as before, because its orientation takes the object's scale (and it gets the placement, the mirror and the pose memo). A local matrix is used only when nothing changed the children's orientations after the decode:
- the graph has no object overlays;
- the object isn't interpolating;
- the object is scenery, whose type preprocesses nothing (`object_types.c`: units, bipeds, vehicles, weapons and devices do).

**What it leaves alone.** The Media Engine's pose job (poseahead) still makes its poses ahead, unchanged. The table serves only the poses this CPU would decode, and only this CPU's decodes fill it. A pose the ME hands over takes the table's local matrices only when it is the table's pose bit for bit (`csmemcmp`). The reason is that the ME's decodes can differ from this CPU's in the last bit. In the emulator on c10, poseaheadcheck finds 186,323 of 204,726 ME poses differing from the live decode: a quaternion's w is 0x3f800000 on one side and 0x3f7fffff on the other. An earlier version kept the ME's poses in the table, and that changed this CPU's ticks: a30's and b30's play parted against `pscoff`. That difference already lives in the default build: which side makes a pose decides its last bit (section 5).

**Memory, and when the table holds any.** Main RAM only, never eDRAM. The heap comes first:
- **A pose is kept only at its second decode within 16 ticks.** Its first decode is only noted in the directory. A pose decoded again soon is one the table will serve: c10's swamp plants come back every few ticks. One that isn't would hold memory for little, like a single long animation or poses the ME's job makes nearly all of.
- **Local matrices only where the chain can use them:** scenery whose definition's graph has no overlays.
- **Headroom floors.** No new 16 KB piece (or the 20 KB directory) unless the heap's largest free block stays above 256 KB (`pscfloorN`) and its free total above 320 KB (`psp_heap_survey`, now built in every build). The cap is 112 KB (`psckbN`), c10's use.
- **Giving the memory back.** Every tick (from `psp_pose_ahead_make_work`), every 150 ticks of game time, the heap is surveyed while the table holds memory. The table is released for the rest of the map when the heap falls under those floors, or when it served under 2 poses a tick over the last 900 ticks.
- Past the cap, the floors or 3/4 of the directory, a pose is refused and made as before.
- Everything is dropped at each map's load (`objects_initialize_for_new_map`), or if `hs_model_animation_compression_enabled` turns. It's freed with newlib's own `free` (`(free)(p)`), because the engine's `free` macro is its debug allocator's. The first build called that one at map load and crashed the emulator.

**Log.** A `psc:` line every 900 ticks and at each drop, giving:
- poses kept, KB used, pieces and the cap;
- poses handed over, kept, refused and unwritten;
- poseahead's poses against the table (the same or different);
- local matrices used;
- what the check found (with `psccheck`, the first 12 differing poses named).

## 2. Key lines

At the base pose, in `object_compute_node_matrices_with`:

```c
psp_psc_listing = psp_psc_eligible(object, animation);
if (psp_pose_ahead_take(object_index, model, animation, frame_index, node_orientations))
{
	if (psp_psc_listing)
		psp_psc_match(object_index, object, model, animation, frame_index, node_orientations);
}
else if (psp_psc_listing && psp_psc_take(object_index, object, model, animation, frame_index, node_orientations))
	;
else if (psp_psc_listing && psp_psc_make(object_index, object, model, animation, frame_index, node_orientations))
	;
```

`psp_psc_make` decodes into the table entry, which carries the `PSP_POSE_UNWRITTEN` sentinel (as poseahead's arena does). If the decode wrote nothing, it returns FALSE and the old path runs. In the chain's child branch:

```c
if (psp_psc_locals && psp_psc_locals_object == object_index && node_stack_index < psp_psc_locals_count &&
	object->object.animation.interpolation_frame_count == 0)
	psp_psc_local(&object_nodes[node_stack_index], orientation, node_stack_index);
else
matrix4x3_from_orientation(&object_nodes[node_stack_index], orientation);
```

`psp_psc_locals` is cleared at the start of each compute's pose, and again before the overlay loop when the graph has overlays.

## 3. What reads the matrices in the tick

Both forms give the same bits as `pscoff`, so no reader can see a difference. Collision against scenery, AI rays, markers and attachments, lights, the sleep checks and the bounding sphere (from node 0) all read what they read before. The other matrix form, with each node's whole product down from the root baked (`world = root × L`), is the one that would differ in the last bits at depth 2 and below. It isn't built. On these maps the scenery has at most 4 nodes (c10's largest animation is 4 nodes × 250 frames), so beyond what the exact form already takes, it would save only the multiplies below depth 1. That isn't worth giving up exactness for.

## 4. Evidence [E]

Emulator, bake w148a, no PGO, Sierra-III psc 1d05237a.

- **Exactness** (fixedticks fixedseed nomouth statecrc, game time 3,600). Default against `pscoff`: play and both random seeds identical at every tick on c10 hands off and on the a30, b30, c20 and d20 routes. `pscexact` against `pscoff` gave the same on the build before the heap fix (82fa8bec). The whole-state hash differs in the allocations that follow the clock (object looping sounds, objects; the HUD's unit interface). Two runs of `pscoff` are identical, so this is the timing difference between builds seen on other exact levers, not this change's maths.
- **psccheck:** every pose handed over compared with a live decode, every local matrix with a live `matrix4x3_from_orientation`. c10: 123,306 poses and 223,926 matrices, 0 differed. b30 (before its release): 3,913 and 9,695, 0. The other maps served too little to matter.
- **The heap at each run's end** (metrics heap: the low over the run, on against `pscoff`; requests failed 0 everywhere):

| Map | Free low on / off (KB) | Largest block low on / off (KB) | The table |
|---|---|---|---|
| c10 | 333 / 454 | 259 / 389 | kept: 556 poses, 96 KB in pieces, ~34 a tick served |
| a30 | 193 / 193 | 153 / 146 | released: the heap under its floors |
| b30 | 283 / 283 | 219 / 225 | released: the heap under its floors |
| c20 | 325 / 325 | 279 / 290 | released: under 2 a tick served |
| d20 | 355 / 297 | 297 / 275 | released: under 2 a tick served |
| a10 | 294 / 294 | 248 / 250 | released: under 2 a tick served |
| d40 | 572 / 572 | 559 / 559 | never used |

  The few KB left on c20, b30 and a10 are the holes the released pieces leave. On c10 the table holds its ~130 KB above the 256 KB floor; if c10's heap falls under the floor later in a full play, the table gives it back.
- **Strict:** the default on the five maps (82fa8bec) and on c10 and a30 (cf8d9b4f): 0 distinct sites, no crash.

## 5. The price

Cache model (c10 hands off and a30's route to game time ~3,200, no PGO; heartbeat ticks from game time 1,200; cf8d9b4f, which differs from the head only in when the checks run):

| Map | Side | Tick (ms) | Change | Scenery (per-type line, ms a tick) | Tick fetches a tick |
|---|---|---|---|---|---|
| c10 | `pscoff` | 4.77 | | 2.29 | 1.712 M |
| c10 | `pscexact` | 4.54 | -4.9% | 2.07 | 1.642 M |
| c10 | default | 4.54 | -4.8% | 2.04 (-11%) | 1.643 M (-4.0%) |
| a30 | `pscoff` | 6.32 | | | 1.790 M |
| a30 | default | 6.33 | flat (the table released) | | 1.796 M |

On the PSP [U on M x E]: c10's scenery is 7.0-7.5 ms a tick [M], and -11% of it is ~0.8 ms a tick, ~1.1-1.3 ms a frame at 1.65 ticks a frame. Elsewhere the table gives its memory back and costs nothing.

## 6. What else it found

**The ME's poses aren't this CPU's bits in the emulator.** With `poseaheadcheck` on c10, the Media Engine's poses differ from the main CPU's decodes in the last bit: 186,323 of 204,726. MP's exactness was proved on b30 and a10, and on the PSP on b30 (0 of 164,981). c10's compressed scenery animations weren't in that proof. Whether the PSP's ME differs there too needs a `poseaheadcheck` run on c10 on the PSP. If the two agree on the PSP, the table could also take the ME's poses and let the pose job leave them out (the ~115 a tick on c10). That is the ME side this build leaves out.

The fix for that (B44) is on its own branch, `posefpu`: every pose listed for the ME is made in the ME's FPU form wherever it is made, so a pose no longer depends on which unit finished in time.

## 7. The other matrix form

The whole product from the root baked was not built (section 3).
