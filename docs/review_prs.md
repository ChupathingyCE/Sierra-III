# Review: PRS, perception passes over objects it only stamps

**What:** `actor_perception_refresh` tests every object in every cluster an actor can see (the collideable and the noncollideable lists of each cluster in its PVS), through `actor_perception_refresh_test_object`, which walks each object and its children. The walk only acts on three types: bipeds (props, suicide danger), vehicles (empty ones) and projectiles (danger zones). Every other object (scenery, weapons, equipment, machines, controls, lights, garbage, placeholders, sound scenery) is only stamped with the object marker, and its children walked. Now such an object is passed over from its header's type: its children and siblings are walked as before, with no `object_get`, no stamp and no reads of the datum's first lines (flags, magic number, type). On by default (an exact speed-up).

**Why it pays [E]:** on b30's beach past 90 s a refresh walk visits ~350 objects a tick, 309 of them (88%) of the types it ignores, 262 of them scenery; the cluster walk takes ~385 emulated us a tick, and the counted removal is ~0.27 ms a tick in the emulator, ~0.55 ms PSP-scaled (perception's census). b30's fights are tick-bound, so this is a tick-lane gain.

**Why it is exact [D]:**
- *The stamp's readers.* `object_mark_function` sets `magic_number` to `global_object_marker` and says whether it was already set. In the refresh's epoch (`object_marker_begin` ... `object_marker_end`), the stamps are read only by later `object_mark_function` calls on the same object: the prop loop (units only), the swarm loops (units only) and the cluster walk itself. In the cluster walk the answer gates only the biped, vehicle and projectile tests; the child walk runs whatever it says. A unit is a biped or a vehicle, never of a skipped type, so no unit's stamp is lost. A skipped object met again from another cluster did nothing the first time and does nothing again.
- *Later epochs.* Every `object_marker_begin` (the next refresh, the collision walks, the render walk) increments the marker first, so a stamp left out in an earlier epoch can't change what a later one sees; a new object starts at marker - 1.
- *State hashes.* The sleep checksum leaves out `magic_number` (word 2), and statecrc's play hash doesn't include it. The whole-state hash does include it, so on vs off differs there by design; check vs off doesn't.
- *Type.* The header's type is the datum's type, written together when the object is made and never changed (the reason `typehdr` is exact).
- *Children and siblings.* `first_child_object_index` and `next_object_index` are read from the same datum as before.
- *What changes:* the `match_vassert` in `object_get_and_verify_type` isn't evaluated for the skipped objects (it can't fail with `_object_mask_all`), and the datum's first cache line isn't fetched for them, which is the gain.

**Keep words:** `percall` turns the skip off (every object tested and stamped, as the engine does). `prscheck` lets the skip decide, then stamps each passed-over object anyway and checks the datum's type against the header's; every 150 ticks a `prscheck:` line gives the objects passed over and the differences (0 expected). With `prscheck` the game state matches `percall` word for word, whole-state hash included.

**Key lines** (psp_fixups.py, `FIXES_PRS["source/ai/actor_perception.c#prs"]`): at the top of the walk's loop, the header's type; for a type other than biped, vehicle or projectile, the child walk and the step to the next sibling from the datum, then `continue`. The check is a cold, out-of-line function.

**Cost and risk:** one header read and two compares per visited object (`object_get` read the header before too). The walk's PGO counts no longer match its code until the next profile is made, so a `PSP_PGO_STRICT=1` link lists this function until then. Multiplayer: the skip is exact, so it leaves the simulation as it was [D]; it compiles into `PSP_MULTIPLAYER=1`. The rest of the saving (not reading the header or the datum at all, from typed cluster lists) waits on RT1.

**Credit:** after halo-ce-vita's perception count, by BirchWoodGod (`docs/research/research_vita_insider16.md`, S2).
