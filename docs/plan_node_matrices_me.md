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
