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

## Before any code

- The inputs a compute reads (the object's position and orientation, its animation state, its parent's node, the model and animation tags, the pose made ahead) must not change while the ME computes. Which tick steps after `objects_update` write objects (scripts, effects, damage) decides how long a hand-over may run.
- The matrices' block and the object's other fields must not share a cache line that both CPUs write (the emulator's write trace, `mewrites`).
- `tools/mewalk.py` on the compute: 0 firmware calls, 0 VFPU, 0 indirect calls it can't follow.
- The order of work puts C (coarse ME jobs, chunked with audio between chunks) before D: one hand-over for the whole update's matrices, not one per object.
