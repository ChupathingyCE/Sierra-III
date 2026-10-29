# Review: PSC, a looping animation's poses kept and made once a map

PSC is an exact speed-up and is **on by default**. `pscexact` keeps the decoded orientations alone, without the local matrices. `pscoff` gives the old path. `psccheck` makes every pose and local matrix the table hands over again, live, compares the two bit for bit and uses the live one. `psckbN` sets the table's cap in KB (the default is 128). It all lives in `FIXES_PSC` in psp_fixups.py and touches only objects.c. Labels: [M] measured on the PSP, [E] emulator, [D] read in code, [U] inference.

## 1. What it does

**The work it removes [D].** An object whose animation runs by itself (`_object_animates_automatically_bit`) takes its frame from game time: `(object index + game time) % frame count`. Scenery's looping animations are these, such as c10's swamp plants. So every tick, every instance calls `animation_get_node_orientations(model, animation, frame)` again, and the instances run the same frames, each at its own offset. That decode reads only the model, the animation (tag data, fixed within a map) and the frame. So its answer can be kept, keyed by (model, animation, frame), and copied after that: the same function on the same inputs gives the same bits. Only an animation whose nodes are the model's is kept. When the decode's checks refuse a pair, it writes the model's own pose instead, which has the model's node count. The first proof build kept those too, and the whole state parted against `pscoff` (counted now as "not the model's nodes": 0 on the five maps).

**The local matrices [D].** In the node chain, each child node is `matrix4x3_from_orientation(child)` and then `matrix4x3_multiply(parent, child)`. The first step depends only on the decoded orientation. For scenery, the table also keeps that local matrix, made by the same function from the same orientation, and the chain multiplies it onto its parent as before. So the node matrices are the same bits. The root is made as before, because its orientation takes the object's scale (and it gets the placement, the mirror and the pose memo). A local matrix is used only when nothing changed the children's orientations after the decode:
- the graph has no object overlays;
- the object isn't interpolating;
- the object is scenery, whose type preprocesses nothing (`object_types.c`: units, bipeds, vehicles, weapons and devices do).

**What it leaves alone.** The Media Engine's pose job (poseahead) still makes its poses ahead, unchanged. The table serves only the poses this CPU would decode, and only this CPU's decodes fill it. A pose the ME hands over takes the table's local matrices only when it is the table's pose bit for bit (`csmemcmp`). The reason is that the ME's decodes can differ from this CPU's in the last bit. In the emulator on c10, poseaheadcheck finds 186,323 of 204,726 ME poses differing from the live decode: a quaternion's w is 0x3f800000 on one side and 0x3f7fffff on the other. An earlier version kept the ME's poses in the table, and that changed this CPU's ticks: a30's and b30's play parted against `pscoff`. That difference already lives in the default build: which side makes a pose decides its last bit (section 5).

**Memory.** Main RAM only, never eDRAM. The table takes:
- a 1,024-entry directory (`calloc`, 20 KB);
- 16 KB pieces (`memalign`) as the map uses them, up to the cap.

Past the cap, or past 3/4 of the directory, a pose is refused and made as before (counted). Everything is dropped at each map's load (`objects_initialize_for_new_map`), or if `hs_model_animation_compression_enabled` turns. It's freed with newlib's own `free` (`(free)(p)`), because the engine's `free` macro is its debug allocator's. The first build called that one at map load and crashed the emulator.

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

Emulator, bake w148a, no PGO, Sierra-III psc 82fa8bec.

- **Exactness** (fixedticks fixedseed nomouth statecrc, game time 3,600). Default against `pscoff`, and `pscexact` against `pscoff`: play and both random seeds identical at every tick on c10 hands off and on the a30, b30, c20 and d20 routes.
  - The whole-state hash differs, in the allocations that follow the clock: object looping sounds, the HUD's unit interface, objects. Two runs of `pscoff` are identical (c10, a30), so this is the timing difference between builds seen on other exact levers, not this change's maths: the check word below compares every pose and matrix the table hands over.
- **psccheck:** every pose handed over compared with a live decode, and every local matrix with a live `matrix4x3_from_orientation`.

| Map | Poses checked | Local matrices checked | Differed |
|---|---|---|---|
| c10 | 128,929 | 224,688 | 0 |
| b30 | 23,302 | 56,271 | 0 |
| a30 | 5,336 | 0 (its scenery has overlays) | 0 |
| c20 | 1 | 143 | 0 |
| d20 | 3 | 418 | 0 |

  The largest difference was 0 everywhere.
- **Use (c10, 3,600 ticks):**
  - 650 poses kept, 107 KB;
  - ~36 poses a tick from the table (the main CPU's decodes);
  - ~62 local matrices a tick;
  - the ME still makes ~115 a tick; of those, 27,577 matched the table bit for bit and 268,138 didn't (the emulator's ME difference).
- **Use on the other maps:**
  - a30: fills to the cap (634 poses, 127 KB, 115 refused) and serves 1.5 a tick. Its scenery has overlays, so the local matrices kept there are never used; keeping them only for graphs without overlays would roughly halve that, a follow-up.
  - b30: 320 poses, 79 KB, 6.5 a tick;
  - c20 and d20: almost nothing.

## 5. What else it found

**The ME's poses aren't this CPU's bits in the emulator.** With `poseaheadcheck` on c10, the Media Engine's poses differ from the main CPU's decodes in the last bit: 186,323 of 204,726. MP's exactness was proved on b30 and a10, and on the PSP on b30 (0 of 164,981). c10's compressed scenery animations weren't in that proof. Whether the PSP's ME differs there too needs a `poseaheadcheck` run on c10 on the PSP. If the two agree on the PSP, the table could also take the ME's poses and let the pose job leave them out (the ~115 a tick on c10). That is the ME side this build leaves out.

## 6. Strict, and the price

- **Strict** (the default form, c10 hands off and the a30, b30, c20, d20 routes to game time ~1,600): 0 distinct sites and no crash on all five.
- **The cache model's tick** (c10 hands off and a30's route to game time ~3,250, no PGO; heartbeat ticks from game time 1,200):

| Map | Side | Tick (ms) | Change | Scenery (per-type line, ms a tick) | Tick fetches a tick |
|---|---|---|---|---|---|
| c10 | `pscoff` | 4.77 | | 2.28 | 1.710 M |
| c10 | `pscexact` | 4.54 | -4.9% | 2.08 | 1.640 M (-4.1%) |
| c10 | default | 4.52 | -5.2% | 2.08 | 1.635 M (-4.4%) |
| a30 | `pscoff` | 6.33 | | | 1.735 M |
| a30 | `pscexact` | 6.33 | 0% | | |
| a30 | default | 6.24 | -1.3% | | 1.734 M (-0.05%) |

  c10's tick instruction misses also fall 6%. a30 is flat: its table serves 1.5 poses a tick.
- **On the PSP [U on M x E]:** c10's scenery is 7.0-7.5 ms a tick [M]. The emulator's -9% of scenery comes to ~0.65 ms a tick there, ~1.1 ms a frame at 1.65 ticks a frame; the tick's -5% on c10's 13-18 ms of tick a frame gives about the same, 0.7-0.9 ms. If the PSP's ME agrees with the main CPU on c10 (it does on b30), the local matrices also serve the ME's ~115 poses a tick, which the emulator can't show: the matrix form would then be worth more there than its 0.3% here.
- **The other matrix form,** the whole product from the root baked, was not built (section 3).
