# The next Media Engine job: next tick's base poses (plan, 2026-10-15)

Sheepdog01's call (2026-10-15): choose the next ME job by its performance impact. This note says which one, why, and how it stays exact. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## Which job

The two candidates were biped movement's collision and node matrices. The PSP-1000's tick split, step by step (`PSP_TICK_STEPS` recon builds; each step's time runs until the next step) [M]:

| Map | Node matrices | Biped physics | Tick | Source |
|---|---:|---:|---:|---|
| b30, the beach | 6.9 ms | 3.8 ms | ~52 ms | lab 38713734855 (b126) |
| c20, the opening | 5.6 ms | 4.1 ms | ~24 ms | lab 38767101467 (b129) |
| a50, the opening | 4.9 ms | 7.2 ms | ~28 ms | lab 38769194212 (b129) |

Node matrices lead on b30 (the slowest map) and c20, and in total (17.4 against 15.1 ms a tick over the three). **So the job is node matrices**, starting with the part of them that can be made ahead of time.

## The part that can be made ahead

`object_compute_node_matrices` starts with the base pose: `animation_get_node_orientations(model, animation, frame_index, orientations)`, the keyframe decode, quaternion work and interpolation for every node. It reads only the model, the animation and an integer frame [D]. Everything after it (overlays, aiming, the matrices themselves) depends on where the object is this tick, so it stays on the main CPU.

**The guess holds.** `posepredict` (recon, b130) counts whether each tick's base pose is the frame after the object's last one [E, lab 38771424880]:

| Map | Base poses | The frame after the last |
|---|---:|---:|
| c20 | 620,000 | 99.5% |
| a50 | 420,000 | 99.5% |
| b30 (b113 route) | 1,180,000 | 98.8% |
| d20 | 160,000 | 97.4% |

**What it's worth.** On the PSP-3000, c20's base pose takes **63 µs**, about 30 a tick: ~1.9 ms of c20's 24 ms tick (lab 38771536232) [M]. b30's figure is being measured. Net of the copy and the hand-over, perhaps 1.3–1.6 ms a tick on c20 [U]. The Media Engine is otherwise ~5% busy (the audio).

## How it works

1. **Collect.** As each tick poses an object, note `(object, model, animation, frame + 1)` in a list (`posepredict`'s table, kept).
2. **Hand over.** After the frame's last tick, the list goes to the ME as a ring job (`psp_me_push`, as perception's rays do). The ME runs the same `animation_get_node_orientations` for each entry into a buffer of its own (64-byte lines, whole), while the main CPU draws the frame.
3. **Use.** At the next tick, before the first object is posed, the main CPU waits for the job (`psp_me_wait`; normally long done). Each object whose animation and frame are the ones guessed takes its orientations from the buffer (a copy) instead of decoding them. Any other object decodes as now.

## Why it stays exact

- **The same function on the same inputs.** The model and animation are tag data, unchanged within a map; the frame is an integer. The ME runs the same compiled code.
- **The same arithmetic.** The ME's FPU is the main CPU's (an Allegrex core); the build has no fused multiply-adds (`-ffp-contract=off`); the code path has no VFPU (the ME has none). The ME's FPU control register must match the main CPU's (rounding, flush to zero, traps); the job sets it and checks it.
- **The data under it doesn't move.** c20's Flood animations stream from the geometry pack (`psp_anim.c`). An entry goes on the list only if its animation's data is wholly resident, and those pages are held until the wait; any eviction meanwhile waits for the job first.
- **Proven, not assumed.** A check word compares every buffered pose with the main CPU's own decode, bit for bit (as `nearautocheck` and `fractioncheck` do), and `statecrc` pairs on c20, a50, d20 and b30 show play identical.

## Steps

1. `posepredict` on the PSP for b30's decode time (running).
2. The list and the buffer on the main CPU alone (the "ME" work run by the CPU at the frame's end: `PSP_ME_WORK` context, as `gemesim` does), with the check word: proves the plumbing exact before the ME runs it.
3. The ring job on the ME; its FPU state set and checked.
4. Cache models and a PSP A/B on c20 and b30.

Keep words to come: the job on by default once proven; an off word, and a check word.

## Where it stands (2026-10-15, `mepose131` dcf09cb)

Keep words: `poseahead` (the main CPU makes them ahead: plumbing only), `poseaheadme` (the ME makes them), `poseaheadcheck` (every pose made ahead and used is also decoded and compared, bit for bit).

- **Exact** [E]: 0 of ~1.3 million poses checked differ on c20, a50, d20 and b30 (lab 38782815918); play and both seeds identical to the default at every common tick on a50, d20 and c20.
- **The ME's rules** [E]: no system call in the job's work; no line written by both CPUs outside the game thread's stack, where the emulator runs the ME's job (lab 38786384396).
- **On the PSP-1000, b30** [M] (lab 38784132264, repeat 2): the ME made 99.997% of the poses, no job failed; tick −1.4 ms (−2.5%) in the flight and −1.8 ms (−3.6%) in the cinematic, both real; the frame −0.9% in the flight; landing and beach in the noise. Against ~2.2 ms of decodes a tick (the PSP-3000's b30, above), about two thirds; the rest would go to listing, the hand-over's cache work and the copies [U].
- **On the PSP-3000, c20, with the check word** [M] (lab 38784036485): the PSP stopped answering after the launch. Not seen in the emulator; the log on its stick should say how far it got. Until that's understood the job stays opt-in.
