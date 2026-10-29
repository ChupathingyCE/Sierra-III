# Review: PSC, a looping animation's poses kept and made once a map

PSC is an exact speed-up and is **on by default**. `pscexact` keeps the decoded orientations alone, without the local matrices. `pscoff` gives the old path. `psccheck` makes every pose and local matrix the table hands over again, live, compares the two bit for bit and uses the live one. `psckbN` sets the table's cap in KB (the default is 128). It all lives in `FIXES_PSC` in psp_fixups.py and touches only objects.c. Labels: [M] measured on the PSP, [E] emulator, [D] read in code, [U] inference.

## 1. What it does

**The work it removes [D].** An object whose animation runs by itself (`_object_animates_automatically_bit`) takes its frame from game time: `(object index + game time) % frame count`. Scenery's looping animations are these, such as c10's swamp plants. So every tick, every instance calls `animation_get_node_orientations(model, animation, frame)` again, and the instances run the same frames, each at its own offset. That decode reads only the model, the animation (tag data, fixed within a map) and the frame. So its answer can be kept, keyed by (model, animation, frame), and copied after that: the same function on the same inputs gives the same bits.

**The local matrices [D].** In the node chain, each child node is `matrix4x3_from_orientation(child)` and then `matrix4x3_multiply(parent, child)`. The first step depends only on the decoded orientation. For scenery, the table also keeps that local matrix, made by the same function from the same orientation, and the chain multiplies it onto its parent as before. So the node matrices are the same bits. The root is made as before, because its orientation takes the object's scale (and it gets the placement, the mirror and the pose memo). A local matrix is used only when nothing changed the children's orientations after the decode:
- the graph has no object overlays;
- the object isn't interpolating;
- the object is scenery, whose type preprocesses nothing (`object_types.c`: units, bipeds, vehicles, weapons and devices do).

**What it leaves alone.** The Media Engine's pose job (poseahead) still makes its poses ahead, unchanged. The table serves only the poses this CPU would decode, and only this CPU's decodes fill it. A pose the ME hands over takes the table's local matrices only when it is the table's pose bit for bit (`csmemcmp`). The reason is that the ME's decodes can differ from this CPU's in the last bit. In the emulator on c10, poseaheadcheck finds 186,323 of 204,726 ME poses differing from the live decode: a quaternion's w is 0x3f800000 on one side and 0x3f7fffff on the other (lab psc5). An earlier version kept the ME's poses in the table, and that changed this CPU's ticks: a30's and b30's play parted against `pscoff`.

**Memory.** Main RAM only, never eDRAM. The table takes:
- a 1,024-entry directory (`calloc`, 20 KB);
- 16 KB pieces (`memalign`) as the map uses them, up to the cap.

Past the cap, or past 3/4 of the directory, a pose is refused and made as before (counted). Everything is dropped at each map's load (`objects_initialize_for_new_map`), or if `hs_model_animation_compression_enabled` turns. It's freed with newlib's own `free` (`(free)(p)`), because the engine's `free` macro is its debug allocator's. The first build called that one at map load and crashed the emulator (lab psc0).

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

Both forms give the same bits as `pscoff`, so no reader can see a difference. Collision against scenery, AI rays, markers and attachments, lights, the sleep checks and the bounding sphere (from node 0) all read what they read before. The form Sheepdog01's note describes (each node's whole product down from the root baked, `world = root × L`) is the one that would differ in the last bits at depth 2 and below. It isn't built. On these maps the scenery has at most 4 nodes (c10's largest animation is 4 nodes × 250 frames), so beyond what the exact form already takes, it would save only the multiplies below depth 1. That isn't worth giving up exactness for.

## 4. Evidence [E]

Emulator, bake w148a, no PGO, Sierra-III psc ebd896d1.

- **Exactness** (lab psc1 40955301313, psc2 40955408810; fixedticks fixedseed nomouth statecrc, game time 3,600). Default against `pscoff`, and `pscexact` against `pscoff`: play and both random seeds identical at every tick on c10 hands off and on the a30, b30, c20 and d20 routes. The whole-state hash differs (see section 5 for the control).
- **psccheck** (psc2): every pose handed over compared with a live decode, and every local matrix with a live `matrix4x3_from_orientation`. Results:

| Map | Poses checked | Local matrices checked | Differed |
|---|---|---|---|
| c10 | 128,929 | 224,688 | 0 |
| b30 | 23,302 | 56,271 | 0 |
| a30 | 5,336 | 0 (its scenery has overlays) | 0 |
| c20 | 1 | 143 | 0 |
| d20 | 4 | 407 | 0 |

  The largest difference was 0 everywhere.
- **Use (c10, 3,600 ticks):**
  - 650 poses kept, 107 KB;
  - ~36 poses a tick from the table (the main CPU's decodes);
  - ~62 local matrices a tick;
  - the ME still makes ~115 a tick; of those, 27,577 matched the table bit for bit and 268,138 didn't (the emulator's ME difference).
- **Use on the other maps:**
  - a30: the table fills to the cap (634 poses, 127 KB) and serves 1.5 a tick;
  - b30: 320 poses, 79 KB, 6.5 a tick;
  - c20 and d20: almost nothing.
- **Strict:** psc3 40955524675 (section 5).
- **The cache model's tick:** psc4 40955190287 (section 5).

## 5. Still to fill in

The strict runs, the off-vs-off2 control (psc6), the cache-model tick and the PSP pair. See the lab's findings for the figures; this section is completed with them.
