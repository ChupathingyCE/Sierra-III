# Node matrices on the Media Engine (lever NW, phase D): what the recon says

Phase D's headline. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference. The lab's `levers.tsv` row NW tracks it; the numbers are in `measurements.tsv`.

## The ceiling

With `object_compute_node_matrices`'s whole calls taken off the main CPU's caches (the emulator's cache split, b131, lab `ceil131`) [E]:

| Map | The main CPU's tick | Change | The moved code on caches of its own |
|---|---:|---:|---|
| b30, the b113 route to 170 s | 39.26 → 33.87 ms | −5.4 ms (−13.7%) | 678K fetches a tick, I-misses 0.76% of them, D-misses 2.0% of accesses: ~4.2 ms of ME time |
| c20, the opening to 120 s | 19.75 → 15.46 ms | −4.3 ms (−21.7%) | fits likewise |

The relief is partly cache: the main CPU's own misses fall with the work (b30 I-misses −16%, c20 −21%). A ceiling leaves out the hand-over and any wait.

## Who reads the matrices, and when (`nmreaders`, lab `nmr132`, `nmr132b`) [E]

Each compute inside `objects_update` is classed by its first read after it, weighted by nodes:

| Map | Needed inside `objects_update` | Later in the tick | While drawing | Not before the next compute |
|---|---:|---:|---:|---:|
| b30 | 72% | 0% | 2% | 24% |
| a50 | 78% | 8% | 2% | 11% |
| c20 | 12% | 20% | 20% | 45% |
| d20 | 24% | 11% | 21% | 42% |

("Not before the next compute": the renderer's interpolation copies them directly, without the accessors.)

**By type:** scenery, machines (d20's aside), light fixtures and sound scenery are almost never needed inside the update. Bipeds are needed when they hold something: b30's bipeds 98%, a50's 88%, c20's 13% (the Flood mostly hold nothing), d20's 19%. Weapons and vehicles ~100%.

**The immediate readers:** 56–75% of them are `object_compute_node_matrices` itself: an attached object (the weapon in a biped's hand) computing its own matrices reads its parent's node. The rest is `object_get_marker_by_name`. Other objects' updates read under 1% (`collision_model_instance_new`).

**Not counted:** `object_postprocess_node_matrices` reads and changes an object's own matrices directly, after its attachments' updates. For units that's the inverse kinematics putting hands on the held weapon's or the vehicle's markers (`unit_postprocess_node_matrices`) [D].

## The shape of the job [U]

`object_update` runs, for each top-level object: its type update, damage, function values; **its compute**; function values, colours, lights; **its attachments' updates** (each the same, recursively, with their computes reading its node); **its postprocess**. So:

1. **Objects with no attachments and no immediate reader** (scenery, machines, light fixtures, sound scenery; most of c20's and d20's bipeds): the compute is handed to the ME and the main CPU goes on. A read of a pending object's matrices waits for the ME, or computes them itself if the ME hasn't started them. That's most of c20's and d20's matrix work as the code stands.
2. **A biped and what it holds:** the whole subtree's computes and postprocesses as one ME job, in the order the main CPU would run them, so the held weapon reads its parent's node on the ME. That needs the attachments' type updates moved ahead of the parent's compute, which changes the order of reads and writes. It's exact only if no attachment's type update reads its parent's new matrices, and the statecrc pairs must show it.
3. **Every compute the same arithmetic on both CPUs:** the ME has no VFPU, and `psp_vfpu_matrix4x3_multiply` differs from the FPU in the last bit. Either the FPU path everywhere (the engine's own arithmetic) or the main CPU never computes a deferred object's matrices.

## What the ME would have to run (`tools/mewalk.py`) [D]

The walk from `object_compute_node_matrices` and the five types' `*_preprocess_node_orientations` (the type table's one indirect call), stopping at the log and assert paths: 242 functions, 98 KB of code. What the ME can't run, and what each needs:

| Found | Where | What it needs |
|---|---|---|
| **Animation reads from the stick** | `animation_get_node_orientations` → `animation_get_frame_data` → `psp_anim_lock` → `psp_geo_pack_read` (the animation cache pages frames in on a miss) | The pose job's way (`psp_anim.c`): on the ME, `psp_anim_on_me` reads resident pages only, and a range not resident marks the object unmade, for the main CPU to compute when it reads it; nothing moves a page while a job is out (`settle_me`). The base pose is mostly made ahead already; the overlays (`overlay_animation_apply_*`) and the types' preprocess (aiming, looking) read other animations through the same lock. Misses are rare (b30's beach: 214 in 9 minutes) |
| **The VFPU** | `matrix4x3_multiply` → `psp_vfpu_matrix4x3_multiply` (41 instructions) | `psp_vfpu.c`'s `fpu_multiply` on the ME, and on the main CPU too for a deferred object's compute, so both CPUs give the same bits. Objects computed only on the main CPU keep the VFPU |
| Lazy allocations | `psp_pose_ahead_take_work` (`memalign`), the `nmreaders` table (`calloc`) | Made before the first hand-over |
| Settling the ME's rays | `psp_merays_settle` at the compute's start | Settled once before the hand-over, not per object |
| Counters and notes the main CPU owns | `psp_object_epoch++` (perception's facts go stale), `psp_nmr_computed`, `psp_pose_predict_note`, `psp_pose_ahead_list` (next tick's pose), the timing hooks | Done by the main CPU when it hands the object over; the pose listing reads only the animation state, which the compute doesn't change |
| Error reports | `object_definition_data_error`, `model_data_report_once`, `csprintf` (a map's bad data) | The job notes the object and the main CPU reports it |

The type preprocess functions add no firmware calls of their own. `psp_keep` inside the compute is read once and cached (`psp_pose_predict < 0`).

**The check that keeps it exact:** with the job on, `nwcheck` computes each deferred object again on the main CPU (the FPU's way) and compares bit for bit, as `poseaheadcheck` does for the pose job. Against b132 the bits differ wherever the VFPU's last bit did, so b132's statecrc isn't the reference; `nwcheck` is.

## Before any code

- The inputs a compute reads (the object's position and orientation, its animation state, its parent's node, the model and animation tags, the pose made ahead) must not change while the ME computes. **`objects_update` is the tick's last large step** (`game.c`) [D]: after it come only `players_update_after_game`, `hud_update`, `player_effect_update` and the data checks, then the frame. Scripts, effects, AI and units all run before it. So a job handed over inside `objects_update` runs beside the main CPU's remaining object updates and those few steps, and must be done before the matrices' next reader: the frame's drawing, or, in a frame of two ticks, the next tick's early steps (units, AI, effects) and its interpolation copy. Which of those steps write objects is the next thing to read.
- The matrices' block and the object's other fields must not share a cache line that both CPUs write (the emulator's write trace, `mewrites`).
- The order of work puts C (coarse ME jobs, chunked with audio between chunks) before D: one hand-over for the whole update's matrices, not one per object. **What a hand-over costs** (`halo_bench11` test 10, PSP-3000, lab 38839406352) [M]: `sceKernelDcacheWritebackAll` 12 µs even with nothing dirty, 24 µs all dirty; `WritebackInvalidateAll` 5–32 µs and then every line the CPU uses again misses (up to ~46 µs of refills); the hit ops 8 cycles a clean line and 19 a dirty one. So a hand-over writes back and drops its own ranges, never the whole cache, and forty whole-cache hand-overs a tick would cost a millisecond.

## The steps

1. **The batch on the main CPU (`nwbatch`, FIXES_136, branches `nwbatch133`, `nwheld133`; on by default from b133).** Scenery and its kin (sound scenery, light fixtures, placeholders) with no parent and no attachments have their compute and postprocess put off to the end of `objects_update`'s main loop and made there one after another. Machines, controls and unarmed bipeds were in the first list too (`nwbatchwide` keeps them): on c20 they made the tick 5% slower [M, lab 38876150724, 38878940196], because the machines' computes cost the same made together as in their updates, and the unarmed Flood were nearly all read by another object's update first, which made each in the middle of that update instead of its own. What it took to make it play exact, each rule found by a difference [E]:
   - **Readers settle first:** a read through either accessor, and every test of another object's bounding sphere (which the compute sets), runs the list up to that object's last entry (lab `nwb133a`: without the sphere readers, c20 and b30 parted).
   - **The inputs put back:** the object's own update recomputes its function values right after the compute's place, and the overlays read them (c20's Monitor parted at tick 991 until they were kept: lab `nwdiag133`, `nwchk133`); another object's update may move it or start another animation on it. Each entry keeps what the compute reads from the object and puts it back for the compute.
   - **Writers settle first:** a compute from elsewhere (`object_set_position`, attach, detach) and an interpolation started or shifted on it.
   - `nwbatchcheck` makes each put-off compute both in place and from the list and compares: none of 1.06 million (c20) and 280,000 (b30) differed. Play and both seeds identical to the build without it on c20 (12,000 ticks), d20, a50, and b30 to tick 1,326, where any change in speed parts it (three runs of one nwbatch configuration agree: lab `nwdet133`).
   - **Why it pays** [M, PSP-3000]: b30's scenery account fell 6.1 → 3.7 ms a tick with only 10–40 computes put off (lab 38858945509), and the tick fell by more than that moved out: in the loop each compute fetches its ~16 KB of code (the compute and the decode) again after the type update's code evicted it, ~40 µs of the ~70 µs a scenery compute costs there [U, from the accounts]. Made together they fetch it once.
   - Left opt-in: `nwbatchheld` (an armed biped with its weapon: not exact, the weapon's update changes the biped's animation state before reading the hand, and each attachment's read of the hand runs the list early, so no faster), `nwbatchsort` (no gain).
2. **The batch to the ME.** What step 1 leaves is the compute's own work: b30's batch at the loop's end costs 0.61 ms a tick in the cinematic (36 computes) and 0.01–0.18 ms after (`nwbatch`'s heartbeat, the PSP-3000, lab 38871696661) [M]. So the ME could take under 1 ms a tick: a small prize for the work below, which waits until a larger one needs the same machinery. The plan, for then:
   - **Hand over as the list grows,** not at the loop's end: after the loop the main CPU reads matrices again within a millisecond or two (the frame's particles, contrails and sounds, then the visibility pass's spheres), so a job handed over at the end could only overlap that. The loop itself is the window. Each entry's inputs are already a record of their own (step 1's put-back), so the ME reads the record, never the live object, while the main CPU goes on changing objects.
   - **A reader of a waiting object** (the accessors, the sphere readers, the writers' settles) waits for its entry if the ME has it, or takes it back and makes it itself if the ME hasn't reached it: a cursor the ME advances and the main CPU can stop, in uncached memory.
   - **Types first: scenery, sound scenery, placeholders, then the devices** (their preprocess reads `device.position` and flags: into the record). Units later: their preprocess reads a dozen unit fields and writes two flags.
   - **The outputs:** the matrices blocks (the ME writes back its lines; the main CPU drops its own before reading), and the sphere into the record, copied into the object by the main CPU when it settles or closes the list (the sphere shares its line with the position and facing, which the main CPU writes meanwhile).
   - **The arithmetic:** the ME has no VFPU. `vfpu_multiply` differs from the FPU's product in the last bits on the PSP (the launch's self-test: worst relative difference 5.9e-6 on the PSP-3000) and not at all in the emulator [M, E]. So an object made on the ME isn't bit-exact with b132 on the PSP, and the emulator can't show the difference: a "plays the same" change. The FPU's product costs ~1.5 µs to the VFPU's ~0.45 [M], so the main CPU keeps the VFPU for everything it makes.
   - Animation reads go through `psp_anim_on_me`; an object whose range isn't resident is marked unmade and made on the main CPU at its reader or the close.
3. **Bipeds holding weapons (b30, a50).** The subtree job (shape 2 above), once 1 and 2 hold.
