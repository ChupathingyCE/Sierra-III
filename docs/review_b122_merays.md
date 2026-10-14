# Review: perception's rays on the Media Engine (`merays122`)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). Same shape as `review_b121_me.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code or in the build, [U] inference.

Branch `merays122`, from `w121` (`446e3eb`, build 121's docs on top of `49d0938`).

| Change | Kind | Default |
|---|---|---|
| The queued rays past the first four, cast by the ME as ring jobs | speed (exact) | off (`meraysperc`) |

With the word off, nothing runs that didn't before, except one load and branch at the top of the five functions that count `psp_object_epoch`.

---

## The question: can the ME cast perception's queued rays, exactly?

**Verdict: feasible, with three changes, all made here.** The ray's code needs an ME-side copy of its top four functions (the deep walks are shared), the main CPU must write back its whole cache before each job, and nothing may move while a job is out. The emulator has shown it exact: 0 of 270,940 ME answers differ, and play is the same at every tick on d20 and a50. It has also shown the rules kept: no system call, and no line both CPUs wrote except the emulator's own stack. Only the rig can say what it is worth. **Expected gain [U]: about 0.5–0.9 ms a tick on b30's beach (1–2% of 49 ms), 0.4–0.7 ms on a50, little on c20.** Worth a rig A/B. It isn't a large lever.

### What a ray reads and writes [D]

`ai_test_line_of_sight` → `collision_test_vector` (for expanded targets, up to three more through `collision_test_line`) → the level's `collision_bsp_test_vector` → for each cluster the ray crosses, each collideable object → `object_test_vector` → `collision_model_test_vector` → the model's BSP walk.

**Reads:**

| What | Changes during `actors_update`? |
|---|---|
| The level's collision BSP, structure BSP (leaves, clusters, PVS), tags (collision models, object definitions) | No: fixed for the level |
| Breakable surfaces' flags | Only when glass breaks (damage, `objects_update`) [U] |
| The clusters' collideable-object lists (`cluster_partition_*`) | Only through disconnect/reconnect: counted by the epoch |
| Object datums: type, flags, sphere, children, next, region permutations, node matrices; `psp_object_spheres` | Moves, attachments and node matrices: counted by the epoch. The other fields only as FIXES_121 already assumes, which `perccheck` showed holds: 0 of 158,108 rays differed |
| Globals: `debug_collision_skip_*`, `ai_debug.fast_los`, `game_connection()`, the BSP pointers, `interpolation_rendering` | No |

**Writes:** these are the problem. Each CPU's cache writes back whole 64-byte lines and doesn't see the other's, so the ME may write only lines the main CPU doesn't touch while the job is out.

| Write | Where | What the ME's copy does |
|---|---|---|
| `object_mark_function` | Each object's `magic_number`, in the datum. The main CPU writes other fields of those lines meanwhile (unit control) and marks objects itself in its own tests | A bit set on the ME's stack |
| `structure_cluster_mark` | `structure_globals` | A bit set on the stack |
| `global_current_collision_users[]`, its depth | Globals | Left out (only the usage log reads it) |
| `ai_profile` meters | Globals | Counted into the answer, added when the ray is taken, as FIXES_121 already does |
| `collision_log_*` | The PSP build stores 0 into a global `LARGE_INTEGER` each call (build 106) | Left out |
| `psp_sphere_tests`, `psp_sphere_rejects`, the first `psp_keep` | Globals | Left out: the sphere copy is read directly |
| `error()`, `release_assert_failed` | Global tables | Reachable only on a malformed map or a failed assert (below) |

**System calls, malloc, double, VFPU** [D, `mewalk.py` on this build, root `psp_perception_me_rays`]:
- reached: 60 functions, 19 KB of code;
- 0 firmware calls, 0 `syscall`, 0 VFPU, 0 indirect calls;
- no malloc.

**Stores to the program's own data in that code** [D, the disassembly: a store through a register set by `lui`]: there are 15, in 9 functions, and every one is on a failure path.
- Each `warned_about_*` flag stored right after an `error()` call: the BSP walk's, the 2D test's, the leaf-to-cluster lookup's, the collision model's.
- `release_assert_failed`'s table.
- `cache_empty_tag_instance`, behind `tag_get`'s checked path.

On its normal path the job writes its stack, the `collision` and `result` structures on that stack, and its answers. That is what the emulator's ME write trace is queued to confirm.

Software double (`__adddf3`, `__extendsfdf2`, `__gedf2`) is reachable only through `render_interpolation_object_node_matrices`. That function returns at once outside a frame's drawing (`interpolation_rendering` is FALSE during a tick), so at tick time the path is dead, on the ME as on the main CPU.

With that function and the failure paths stopped (`--stop=render_interpolation_object_node_matrices,error,release_assert_failed,psp_tag_get_checked`), the walk reaches 40 functions and 11 KB of code, with no double in it.

**The stack** [D, frame sizes from the disassembly]:

| Function | Frame |
|---|---:|
| `psp_collision_test_vector_quiet` (its 1 KB leaf list) | 1,440 B |
| `psp_object_test_vector_quiet` (per level of attached children) | 1,128 B |
| `collision_bsp_test_vector_recursive` (each node; retail BSPs are at most 66 deep) | 120 B |

That comes to ~10–12 KB on a retail map, and at most ~17 KB at the port's 128-deep bound. The ring's 4 KB allowance is too small, so this job type gets 24 KB. The ME's stack has 512 KB.

**How much code needs an ME-side copy:** four functions, ~250 lines in all.
- `ai_test_line_of_sight`;
- `collision_test_vector`, for line of sight's flags only;
- `object_test_vector`;
- `collision_model_test_vector`.

`collision_bsp_test_vector` gets a 20-line entry without the log. The rest is shared as it is: the BSP walk, the leaf and 2D tests, `collision_model_instance_new`, `object_get`, `tag_get`, the PVS, the matrix maths and the cluster lists.

### What has to be snapshotted, written back, dropped

- **Nothing snapshotted.** Each ray's inputs are already in its queue record (FIXES_121). Everything else stays where it is, because the main CPU changes none of it while a job is out (the next point).
- **Nothing moves while a job is out.** The five functions that count `psp_object_epoch` are where anything that could change a ray's answer happens. Each now waits for the job first, so the ME never reads an object halfway through a change. The answers are then dropped, as the queue's already are.
- **Written back:** the main CPU's whole data cache, before each push (`sceKernelDcacheWritebackAll`; `ME_RING_ALL_OF_RAM`). The ray reads object data `objects_update` wrote last tick, which may still sit dirty in that cache.
- **Dropped:** the ME's whole cache before each job (the ring's existing path past 16 KB of inputs). The main CPU drops only the answer lines, as it does for any ring job.

## What was built

**Key lines**, `psp_fixups.py` `FIXES_122`:

- `collision_bsp.c#merays`: `psp_collision_bsp_test_vector_quiet`, the walk's entry without the usage log.
- `collision_models.c#merays`: `psp_collision_model_test_vector_quiet`, the same without the log.
- `collisions.c#merays`: `psp_collision_test_vector_quiet` and `psp_object_test_vector_quiet`.
  - The same walk in the same order: the sphere copy, the mark, then the object.
  - Marks go in bit sets on the stack.
  - It fills only what line of sight reads: whether it hit, `t`, the locations and the point.
  - It returns −1 for flags it doesn't serve (media, vehicle physics, keeping the location valid).
- `ai.c#merays`: `psp_ai_test_line_of_sight_quiet`, `ai_test_line_of_sight` line for line, with the collision tests counted into `*collisions`.
- `actor_perception.c#merays`:
  - **The queue.** With the word on, `psp_perception_queue` only records each ray. `psp_merays_hand_over` then pushes the rays past `meraysfrontN` (4 unless set) in jobs of up to 16, and casts the first ones on the main CPU meanwhile. Rays the ring refuses are cast on the main CPU too.
  - **Collecting.** `psp_merays_collect(index)` waits for jobs up to the one holding the ray an actor asks for, then copies the answers in. A ray the ME didn't serve, or every ray of a failed job, is cast on the main CPU there.
  - **Settling.** `psp_merays_settle` waits for any job still out and drops its rays. It runs from the epoch functions and at `actors_update`'s end.
  - **Checking.** `perccheck`, as before, casts every taken ray again on the main CPU. It now also counts the ME's answers that differ, on their own line: `perception, Media Engine: J jobs (R refused), N rays: A answered, U not served, F after a failed job, D dropped before a change; T taken, C checked, M mismatched`.
- `objects.c#merays`: `if (psp_merays_out) psp_merays_settle();` before each `psp_object_epoch++`.

`psp_me.c` / `psp_me.h`:
- `ME_JOB_PERC_RAYS`;
- `ME_RING_ALL_OF_RAM`: an input of all of RAM, which makes the main CPU write back its whole cache and the ME drop its own;
- `psp_me_push_perc_rays`: the engine's units don't see `psp_me.h`, so the job is put together here;
- the 24 KB stack allowance for this job type;
- `meraysperc` starts the ring under `nomeaudio`, as `meslant` does.

**Why jobs of 16:**
- A job's answers are then two whole 64-byte lines of its own.
- A job of 16 expanded rays (four vectors each, ~5 ms [U]) stays inside the ring's 10 ms watchdog. A late job stops the ring for the session, `meslant` included.
- Beyond 16, the ME starts each extra job with a cold cache. On b30, with 14 rays a tick, one job is the usual case [E, lab 38510147369].

## Proven so far

- **Builds** [D]: the campaign build compiles and links (`4bc4f00`); `mewalk.py` gives the figures above.
- **Memory** [D]: +6.8 KB code and +0.4 KB data against `446e3eb`, both built the same way. The heap is 8 KB smaller: 5,696 KB against 5,704.
- **Emulator** [E]: batch `merays122a`, lab run 38560105583, on `b8b7726`. That commit is this branch before the jobs were cut to 16; the walk is the same. Every run ended normally.
  - **Every ME answer checked** (`perccheck`, the main CPU casting each taken ray again): **0 mismatched of 270,940**, 0 not served, 0 after a failed job, 0 refused pushes, 0 failed ring jobs.
    - b30's route: 101,977 checked (103,003 cast by the ME);
    - a50: 130,363;
    - b40 (vehicles): 12,621;
    - c10 (the Flood): 1,219;
    - the strict a50 run: 24,760.
  - **Play exact.** Word on against off, hands off: the play hash and both random seeds are the same at every tick, on d20 (25,217 ticks) and a50 (40,835).
    - The perception lines of each pair are equal to the last count: the same rays cast, taken and refused.
    - The ME played 5,699 and 377,137 of its answers.
    - The whole-state hash differs in the allocations already known to follow the emulator's speed, and in `actor`. Most likely [U] that is the stack bytes copied in with a `direction_specification` (`review_b121_aiperc.md`), since the job's frames leave different bytes on the stack; not yet narrowed with `stateblocksN`.
  - **No system call in the ME's work** (a50, strict interpreter, `gemesim`): `MESTRICT: no system calls in work meant for the Media Engine`. 0 bad accesses.
  - **The ME write trace** (`mewrites`, same run): 46 lines written by the ME's work, 3,513 by the main CPU while a job was out, and 23 by both.
    - All 23 are in one 24 KB stretch at 0x09fb98c0–0x09fbf900: the main thread's stack, where the emulator runs the job.
    - The ME's first stores there are frames and locals: `ring_run`'s argument copies, the mark-clearing loop in `psp_collision_test_vector_quiet`, the matrix temporaries, and the slant job's own frame. The main CPU's are the deeper calls of its actor updates.
    - None is in the program image or the heap. On the PSP the ME runs on its own stack in its eDRAM, so these lines aren't shared there.
    - Read with `mewrites.py` against a local build of `b8b7726`.
  - **Multiplayer:** builds, and stops where `w121` does on Blood Gulch (the GE's 768 KB display list doesn't fit the heap).
- **Batch `merays122b`** (lab run 38561782273, queued) runs the final plumbing (`4bc4f00`, jobs of 16): `perccheck` on b30's route and a50, and the d20 state CRC pair.
- **What the emulator can't show.** It runs no ME code: each job runs inside the push, on the main CPU. So it proves the copy exact and the writes confined, but not:
  - the real ME's cache;
  - the speed;
  - whether the main CPU ever waits.

## The expected gain [U]

- On b30's beach (rig, `w121`: tick 49.2 ms [M]), perception's line of sight is 0.89 ms a tick in the emulator's JIT steps, 14 rays [E].
- On the PSP, the rays are about 0.9–1.3 ms a tick [U]: the cache-model estimate runs ~10% under the rig's tick on b30.
- The ME takes ~10 of the 14. That is 0.65–0.95 ms off the main CPU, less what the hand-over costs it:
  - the whole-cache write-back, ≤256 dirty lines, ~30–60 µs;
  - one push (~7 µs on the rig's slant jobs [M]);
  - the collect.
- The ME's job (~1–1.5 ms, starting with a cold cache) is done long before actor 5 asks: the main CPU first casts four rays and updates four actors, ~3–5 ms at ~17 ms of AI a tick.
- Net: **−0.5 to −0.9 ms a tick on b30's beach, −0.4 to −0.7 on a50, ~−0.1 on c20.** The ME's misses also load the bus the main CPU and the GE share, so the rig's A/B is the number.

## Risks

- **The fence is the exactness argument.** The ME never sees a half-made change only if everything that changes what a ray reads goes through the five epoch functions.
  - FIXES_121's evidence covers the start and the end of `actors_update` (0 mismatches), not every moment in between.
  - Not covered: an object's flags or bounding sphere changed in place mid-update [U].
  - The cost if it happens: a wrong answer `perccheck` would count. On the PSP, a fault on the ME could also follow, and then the ring's watchdog, the ring off for the session and the main CPU doing the work.
- **Assert and error paths.** These are reachable but take a malformed map or a failed assert. The main CPU's own ray would hit the same assert on the same data.

## Default

Off (`meraysperc`), until the rig measures it. Tuning word: `meraysfrontN`, how many rays the main CPU casts itself before the ME's part (default 4). Raise it if the ring line shows the wait large next to the ME's own time; lower it if the wait is ~0.

## Rig runs wanted

Two `sierra-hw` runs, after `merays122a` comes back clean. Both use `ref=merays122`, `maps=/bake/campaign121/halo/maps` and `keep="nearauto assettrack geo2 gebusy"`. Their shapes are the AI-perception rig runs' (lab 38524100365 and 38524222985).

1. **b30, the speed on the beach** (4 runs, ~45 min of the PSP):
   - `map=levels\b30\b30`, `route=b30_b113test1_timed`;
   - `keepsets="- | meraysperc"`, `repeat=2`, `wall=660`.

   Read `abreport.py` for the beach's tick (game time 2,700 to the end) and AI, `ME: ring:` (wait, pick-up, 0 failed), and the `perception, Media Engine:` counts.
2. **a50, the real ME's answers checked, and the front** (8 runs, ~40 min):
   - `map=levels\a50\a50`;
   - `keepsets="- | meraysperc | meraysperc meraysfront2 | meraysperc perccheck"`, `repeat=2`, `wall=300`.

   On the `perccheck` side, `perception, Media Engine:` must show 0 mismatched and 0 after a failed job: this is the check of the real ME's cache, which the emulator can't make. On the others, read the tick and the ring's wait against the ME's own time.
