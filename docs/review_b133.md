# Review: build 133

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b133 is b132 (`review_b132.md`) plus the branches below, and its own profile (`campaign-b133`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch | What | Kind | Default | Words | Review |
|---|---|---|---|---|---|
| `nwbatch133`, `nwheld133` | Scenery's node matrices made together after the objects' loop (lever NW's first step) | exact speed-up (play) | on | `nwbatchoff`; `nwbatchwide`, `nwbatchheld`, `nwbatchsort` (opt-in); `nwbatchcheck` | §1 |
| `menu133` | The launch menu's presets one row each, up to 22; preset 15 shows again | fix | on | none | §2 |
| `nwheld133` | `statefullall`: every object's whole datum at one tick | recon | off | `statefullall` | §3 |

## Release check

Lab 38882219486, against b132 as built: play and both seeds identical at every tick on d20, a50 and c20 (12,000 each) [E], the pose job made by the simulated ME on both sides; the whole state differs from the first tick, as it does whenever a build's allocations change. The logs read whole. b30 and c40 shots right; the multiplayer build plays Blood Gulch. Built from `1fff864` (lab sierra-build 38884092696).

## §1 Scenery's node matrices made together after the objects' loop (`psp_fixups.py` FIXES_136; lever NB, NW step 1; `docs/plan_node_matrices_me.md`)

**What.** `objects_update` makes each object's node matrices (its skeleton's pose) in the middle of its update. Now scenery and its kin (sound scenery, light fixtures, placeholders) with no parent and no attachments have their compute and postprocess listed where they would have run, and the list runs after the loop:

```c
/* object_update */
psp_nwb_later = psp_nwb_put(object_index, object);
if (!psp_nwb_later)
	object_compute_node_matrices(object_index);
...
if (!psp_pose_skipped && !(psp_nwb_later && psp_nwb_put_post(object_index)))
	object_postprocess_node_matrices(object_index);
```

**Why.** In the loop each compute fetches its code and data again after the object's update evicted them. Made together they're fetched once: on the PSP-3000, b30's 36 scenery computes a tick cost 0.61 ms after the loop, and their place in the loop had cost ~3 ms (the scenery account 7.6 → 4.5 ms) [M, lab 38871696661].

**Why only scenery.** With machines and unarmed bipeds in the list too, c20 ran 5% *slower* [M, lab 38876150724, 38878940196]: its machines' computes cost the same made together as in their updates, and its unarmed Flood were nearly all read by another object's update first, which made them in the middle of that update instead of their own. `nwbatchwide` puts them back in for study.

**Exact: each compute reads what it would have read, and every reader sees what it would have seen.** Four rules, each found by a difference:
- **Readers settle first.** A read through `object_get_node_matrices` or `object_get_node_matrix`, and every test of another object's bounding sphere (the compute sets it), runs the waiting entries of the object read (`psp_nwb_settle`, `psp_nwb_sphere_read`: objects_in_sphere, the collision tests, area damage, biped–vehicle collisions, item positions, projectile homing and flyby, melee, doors, a weapon's damage from its owner).
- **Only that object's entries.** Each top-level object's entries read only its own nodes, so the order between objects is free: a reader runs its object's entries out of the list's order, and the rest stay batched. (Running the list up to the object read was exact too, but on c20 it undid the batch.)
- **The inputs put back.** The object's own update recomputes its function values right after the compute's place, and its overlays read them (c20's Monitor parted at tick 991 until this); another object's update may move it or start another animation on it. The entry keeps what the compute reads from the object and puts it back for the compute's duration, then restores the object, keeping any byte the compute itself changed:
  ```c
  psp_nwb_inputs_put(object, &psp_nwb_inputs[psp_nwb_record[entry]]);
  psp_nwb_self = object_index;
  object_compute_node_matrices(object_index);
  psp_nwb_self = self;
  psp_nwb_inputs_get(object, &after);
  psp_nwb_inputs_merge(&now, &psp_nwb_inputs[psp_nwb_record[entry]], &after);
  psp_nwb_inputs_put(object, &now);
  ```
- **Writers settle first.** A compute from anywhere else (`object_set_position`, an attach or detach) and an interpolation started or shifted on the object run its waiting entries first, as they ran first.

**Evidence** [E]: play and both seeds identical to the build without it on c20 (12,000 ticks), d20, a50, and b30 to tick 1,501; b30 parts at 1,326 or later whatever changes the emulator's speed (three runs of one nwbatch configuration agree with each other at every tick, lab nwdet133). `nwbatchcheck` made each put-off compute both in place and from the list: none of 1.06 million on c20 and 280,000 on b30 differed. The objects' whole datums differ only in the mark a collision or render pass stamps (a pass counter) and two bytes of a location's padding (lab nwfull133). And the release check above: b133 plays as b132 did.

**Worth** [M, PSP-3000, on against `nwbatchoff` in one build, two runs a side]: b30's cinematic tick −4.4% (52.3 → 50.0 ms), flight −3.3%, landing and beach in the noise (lab 38881473541). c20: the A/B of this default is running.

**Memory.** On, ~55 KB from the heap (1,024 entries, 512 inputs records; a compute without a free record is made in place). The static lists are gone: the program's heap is 5,676 KB (b132: 5,656).

**Left opt-in:** `nwbatchwide` (above); `nwbatchheld` (an armed biped with what it holds: not exact, its weapon's update changes its animation state before reading the hand, and no faster on b30); `nwbatchsort` (the list by animation graph: no gain on c20, lab 38861946069).

**What it means for step 2 (the ME).** What's left of a batched scenery compute is ~17 µs, so the ME could take under 1 ms a tick on b30. The plan's update says so; the ME's bigger prize is elsewhere.

## §2 The launch menu's presets (`psp/src/psp_menu.c`)

**What.** One row a preset, up to 22 (`MAXIMUM_PRESETS`), with the cursor's keep words shown below the list (`PRESET_WORD_ROWS` 5). A presets file with more than the menu holds fails the release build (`tools/build_release.sh`), and the page warns if any were left out.

**Why.** b132's preset 15 (test 8) wasn't on the page: the old layout gave each preset two rows and held 14.

## §3 `statefullall` (`psp_fixups.py`, `objects.c` `psp_objects_play_dump`)

**What.** With `statecrc` and `stateobjectsN`: every object's whole datum, word by word, at tick N. Two runs whose objects allocation parts while play stays the same say which fields (it found §1's pass mark and padding). Test word only.
