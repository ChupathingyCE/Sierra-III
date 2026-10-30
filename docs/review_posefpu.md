# Review: posefpu, a pose made ahead takes one form whoever makes it

`posefpu` is on by default. `posefpuoff` gives the old path. `posefpulate` is a test: it treats every fourth pose the ME made as late, so this CPU stands in and compares its pose with the ME's. It all lives in `FIXES_POSEFPU` in psp_fixups.py and touches only the pose-ahead code in objects.c. Labels: [M] measured on the PSP, [E] emulator, [D] read in code, [U] inference.

## 1. The fault

**The fault [M].** The Media Engine's pose job (poseahead) and the main CPU made the same pose with different last bits:
- c10: 367,576 of 404,520 poses on the PSP-1000;
- with `novfpuquatd`, none.

**The cause [D].** VFPUQD, on by default, normalizes on the main CPU with the VFPU's `vdot`/`vrsq` forms. The ME has no VFPU, so its entries answer 0 there and the FPU's code runs.

**Why it mattered.** Which unit makes a pose listed for the ME can turn on timing:
- the job failing or a page not resident (the pose is then made at the tick that wants it);
- the ring not ready when the list is made;
- the frame data not resident then (`psp_pose_ahead_me_can`).

So a pose's bits could follow which unit finished in time. Posing is local-only, so it plays the same (the multiplayer-boundary rule), but no pose should depend on timing.

## 2. What it does

Every pose listed ahead is decoded in the ME's form wherever it is made. `psp_posefpu_decode` sets `psp_vfpuquat_mode` to 0 for that one decode: the FPU's normalize and interpolation, which are the ME's code. That covers:
- this CPU's stand-ins while the list is made (`psp_pose_ahead_make_work`: the decode when the ME can't take a pose, and the fallback when the ring isn't ready);
- a pose the job left unmade. It now stays listed (state 4) instead of being forgotten, so the tick that wants it makes it in the ME's form (`psp_pose_ahead_take_work`).

Poses never listed keep the VFPU's form: an object's first, and a guess that missed. Whether a pose is listed is game state, not timing. `poseaheadcheck` now compares the ME's pose with what a stand-in would make.

**Key lines.** The stand-in:

```c
if ((slot->state == 4 || slot->state == 5) && slot->object_index == object_index && slot->model == model &&
	slot->animation == animation && slot->frame == frame)
{
	psp_posefpu_decode(model, animation, frame, out);
```

The form:

```c
int mode = psp_vfpuquat_mode;

psp_vfpuquat_mode = 0;
animation_get_node_orientations(model, animation, frame, out);
psp_vfpuquat_mode = mode;
```

**Log.** A `posefpu:` line every 900 ticks, beside poseahead's: whether it's on, the poses made on this CPU in the ME's form, and, with `posefpulate`, how many were compared with the ME's and how many differed.

## 3. Evidence [E]

Emulator, bake w148a, no PGO, Sierra-III posefpu a57eabbc and 61c455d6 (the second fixes the log line only).

- **Whichever unit decodes, the same bits.**
  - `poseaheadcheck` on c10: 0 of 204,726 differ (186,323 before).
  - `posefpulate` with `poseaheadcheck`: c10 51,135 stand-ins compared with the ME's pose, 0 differed; d20 6,196, 0 differed.
- **Forced-late against the default** (statecrc, game time 3,600):
  - c10: the whole state identical at every tick.
  - a30 and d20: play and both seeds identical at every tick. The whole state differs from tick 2,204 and 544, in the objects and their looping sounds, the allocations that follow the clock (the stand-ins cost time).
  - Without the fix (`posefpuoff` against `posefpuoff posefpulate`), d20 differs from tick 70.
- **Default against `posefpuoff`:**
  - play and both seeds identical at every tick on c10, a30, b30 and c20;
  - c10's whole state identical (no stand-ins there);
  - d20's play parts at tick 61. Its 134 stand-ins change form, from the VFPU's to the ME's: the change this fix makes on purpose. **So posefpu isn't bit-exact on d20**, and its proof there is a side-by-side, not statecrc.
- **d20 side by side** (the tock efc8642f, bake w148s, swamp route, fixedticks and fixedseed; a run of 4 frames every 10 s, 44 frames a side, so a late texture's frames would show): the default against `posefpuoff`, pixel for pixel. The scene is identical in all 44 frames. Only the timing text differs (the top two rows and the bottom row's figures), which follows the clock. Both sides are strict 0. So d20 plays and looks the same [E].
- **Strict:** c10 and a30, 0 distinct sites, no crash.

## 4. The price

The stand-ins lose VFPUQD's speed: on the PSP, b30's flight tick is 0.8 ms faster with VFPUQD than without [M]. They are few:
- c10 0 a tick;
- b30 6 in 3,600 ticks;
- d20 134;
- c20 ~1 a tick;
- a30 ~3 a tick (the frame data not resident when the list is made).

So the price is a small share of that 0.8 ms, and none on c10 [U].
