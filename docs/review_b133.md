# Review: build 133

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b133 is b132 (`review_b132.md`) plus the branches below, built with b132's profile and b132's own code order. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch | What | Kind | Default | Words | Review |
|---|---|---|---|---|---|
| `nwbatch133`, `nwheld133`, `nwhook134` | Scenery's node matrices made together after the objects' loop (lever NW's first step) | exact speed-up on some maps, a loss on others | off | `nwbatch` (preset 16); `nwbatchwide`, `nwbatchheld`, `nwbatchsort`; `nwbatchcheck` | §1 |
| `menu133` | The launch menu's presets one row each, up to 22; preset 15 shows again | fix | on | none | §2 |
| `nwheld133` | `statefullall`: every object's whole datum at one tick; `tools/datumdiff.py` | recon | off | `statefullall` | §3 |
| `w133` | b132's profile and kept code order; the order's file covers hot and cold parts | build | on | the build switch `PSP_TEXT_LAYOUT` | §4 |

## Release check

Lab 38895023525, against b132 as built: play and both seeds identical at every tick on d20, a50 and c20 (12,000 each) [E], the pose job made by the simulated ME on both sides; the whole state differs from the first tick, as it does whenever a build's allocations change. The logs read whole. b30 and c40 shots right; the multiplayer build plays Blood Gulch. Built from `d9e7cf3` (lab sierra-build 38895660524).

## §1 Scenery's node matrices made together after the objects' loop (`psp_fixups.py` FIXES_136; lever NB, NW step 1; `docs/plan_node_matrices_me.md`)

**What.** `objects_update` makes each object's node matrices (its skeleton's pose) in the middle of its update. With `nwbatch`, scenery and its kin (sound scenery, light fixtures, placeholders) with no parent and no attachments have their compute and postprocess listed where they would have run, and the list runs after the loop:

```c
/* object_update */
psp_nwb_later = psp_nwb_put(object_index, object);
if (!psp_nwb_later)
	object_compute_node_matrices(object_index);
...
if (!psp_pose_skipped && !(psp_nwb_later && psp_nwb_put_post(object_index)))
	object_postprocess_node_matrices(object_index);
```

**Exact: each compute reads what it would have read, and every reader sees what it would have seen.** Four rules, each found by a difference:
- **Readers settle first.** A read through `object_get_node_matrices` or `object_get_node_matrix`, and every test of another object's bounding sphere (the compute sets it), runs the waiting entries of the object read (`PSP_NWB_READ`; the collision tests' `psp_nwb_sphere_read`: objects_in_sphere, the collision tests, area damage, biped–vehicle collisions, item positions, projectile homing and flyby, melee, doors, a weapon's damage from its owner). The waiting bit is tested in line: readers are many, waiting objects few.
- **Only that object's entries.** Each top-level object's entries read only its own nodes, so the order between objects is free: a reader runs its object's entries out of the list's order, and the rest stay batched.
- **The inputs put back.** The object's own update recomputes its function values right after the compute's place, and its overlays read them (c20's Monitor parted at tick 991 until this); another object's update may move it or start another animation on it. Each entry keeps what the compute reads from the object and puts it back for the compute's duration, then restores the object, keeping any byte the compute itself changed:
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

**Evidence** [E]: play and both seeds identical to the build without it on c20 (12,000 ticks), d20, a50, and b30 to tick 1,501; b30 parts at 1,326 or later whatever changes the emulator's speed (three runs of one nwbatch configuration agree at every tick, lab nwdet133). `nwbatchcheck` made each put-off compute both in place and from the list: none of 1.06 million on c20 and 280,000 on b30 differed. The objects' whole datums differ only in the mark a collision or render pass stamps (a pass counter) and two bytes of a location's padding (lab nwfull133, `statefullall`).

**Why it's off** [M, PSP-3000]. The idea was that each compute fetches its code once after the loop instead of once an object. That holds where the computes are few and heavy: b30's 36 scenery computes a tick cost 0.61–0.92 ms after the loop against ~3 ms in it (the cinematic tick −4.4%, flight −3.3%, lab 38881473541). Where they're many and light it reverses: c10's ~300 a tick cost **9.7 ms after the loop against ~3 ms in it** (b133's first build on the ladder: c10's tick 25.3 against b132's 18.9 ms, lab 38884427350), because out of the loop each compute takes its object's datum and blocks cold. c20 +2.2% (lab 38887527534), with the readers' cost already in line. A rule that picks only the computes that gain is the open question; until then preset 16 is there for a look.

**Memory.** On, ~55 KB from the heap (1,024 entries, 512 inputs records). Off, nothing: the lists come from the heap only when asked.

**Left opt-in too:** `nwbatchwide` (machines, controls and unarmed bipeds as well: c20 5% slower, lab 38876150724); `nwbatchheld` (an armed biped with what it holds: not exact, its weapon's update changes its animation state before reading the hand); `nwbatchsort` (no gain).

**For step 2 (the ME).** What's left of a batched compute is ~17–30 µs, so the ME could take under 1 ms a tick on b30. Not the ME's next job (`plan_node_matrices_me.md`).

## §2 The launch menu's presets (`psp/src/psp_menu.c`)

**What.** One row a preset, up to 22 (`MAXIMUM_PRESETS`), with the cursor's keep words shown below the list (`PRESET_WORD_ROWS` 5). A presets file with more than the menu holds fails the release build (`tools/build_release.sh`), and the page warns if any were left out.

**Why.** b132's preset 15 (test 8) wasn't on the page: the old layout gave each preset two rows and held 14.

## §3 `statefullall` and `tools/datumdiff.py`

**What.** With `statecrc` and `stateobjectsN`: every object's whole datum, word by word, at tick N; `tools/datumdiff.py` compares two runs' dumps object by object. Two runs whose objects allocation parts while play stays the same say which fields (it found §1's pass mark and padding). Test word only.

## §4 b132's profile and code order (`psp/layout/b132.txt`, `PSP_TEXT_LAYOUT=b132`)

**What.** b133 is built with b132's profile (`campaign-b132`) and linked in b132's own code order. The order's file was rewritten so each function's line names both its hot and its plain section, and a cold part b132 didn't have goes right after it:

```
*source/objects/objects.o(.text.hot.object_get_node_matrix .text.object_get_node_matrix)
*source/objects/objects.o(.text.unlikely.object_get_node_matrix)
```

**Why.** The code layout is still a lottery (the profile places the hot code; b131 lost 10–17% to it). b133's own profile was counted with the batch on, and that first build was slower than b132 on most maps on the PSP-3000's ladder; with the batch off, its code is b132's but for the menu and §1's dormant checks, so b132's measured-good layout is the safe choice until phase B's own layout replaces it. As written, the file named sections as b132 had them; b133's code split `object_get_node_matrix` into hot and cold parts and changed `object_compute_node_matrices` (its counts dropped, so it isn't "hot" now), and the parts the file didn't name went to the end of the code, past the 16-bit branches' reach: the link failed (relocation truncated, lab 38893068103). `codeorder.py --keep` and `--color` now write placements the same way (lab).

**Next for the layout (phase B).** The temporal graph's placement by cache set beats b132's order on every map in the cache model (lab score131c), but only up to ~93% of the fetches can be placed without gaps a link can't make; the next build's layout comes from its own graph and its own profile.
