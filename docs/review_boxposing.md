# Review: the posing box cut in (`PSP_BOXPOSING=1`)

Off by default: a default build is the old path, byte for byte the rebase's. With the switch, every object's node matrices and bounding sphere come from the posing box (`psp/src/box_posing.c`, v0: the walk) in place of the engine's walk. The design is `docs/box_posing.md` §6c.

**The cut-over** (`psp_fixups.py`, `FIXES_BOXPOSING`, registered after the capture's group):
- In `object_compute_node_matrices_with`, at the point where the capture records the box's inputs (after the decode, the overlays, the object's scale, the type's preprocess and the interpolation), `psp_box_pose(...)` runs and `goto psp_box_posed` skips the engine's validity checks, its node walk and its bounding sphere. The label sits after the sphere, so NMSKIP's note, the spheres' copy and the capture's exit hook run as before, and a capture build with the box records the box's own outputs.
- Objects placed with no model take the same route (`model` NULL, flag 32), so no object falls back to the engine's walk.
- `psp_box_pose` gathers the object's flags and place exactly as the capture does (`psp_cap1.h`'s posing records): world-relative, mirrored, attached and the parent's mirror, the physics' centre of mass negated, origin and bounding offsets, radius and scale. It writes the sphere back to the object.
- `objects_initialize_for_new_map` calls `psp_box_posing_forget()`: a plan is keyed by the model's address, which only means something within one map.

**The glue** (`psp/src/psp_boxposing.c`, no engine header): 96 plans a map, found through 256 slots by the model's address (POSEK found 32-63 posed models a map). A model past the room still poses through the box, from a plan made for that call, and the count goes to the log at the map's end (`boxposing:`). The place is copied field by field into `struct box_posing_object`, not as one 20-float block, so no copy runs past a member.

**What the switch leaves out:** the old path's levers on posing's ground (POSEK's plan walk, the pose memo, PSC's locals, NRCHK's root test and its error report, the node prefetch, VFPUPOSE's products) all sit inside the skipped walk.

**Proof so far:**
- Emulator [E]: play and both seeds identical every tick on b30_play2, b40_play1 (to ~6,000 ticks) and c20_play1; pictures inside one build's run spread; strict 0; 0 failed requests to the pads' ends (the lab's boxpose1 and boxpose2).
- PSP-3000 [M]: **not exact**. Play differs from tick 0 against the old path, and the seeds part at tick 1,249. The emulator's VFPU rounds as its FPU and the PSP's doesn't, and the box multiplies on the FPU where the old path's products run on the VFPU. With the game's VFPU forms off, the PSP and the emulator replay the same pad bit for bit (findings `pads_part_on_the_emulators_vfpu`).
- **Price:** +1.56 ms a tick against the old path on b30_play2, read under statecrc (findings `boxposing_inframe_b30_psp3000`). The re-run without statecrc, with `timerson` for the bipeds' and vehicles' node-matrix times, is queued.

**Known limits:** a strict (`PSP_PGO_STRICT=1`) new-only build refuses to link until the profile is counted with the box in (`psp_box_pose` and the glue have no counts). The box's maths is v0's scalar FPU form; §8's VFPU kernel is the next step the lead weighs.
