# Review: lightringlook (B50, the light pass holds the ME past a ring job's deadline)

**Branch** master/ringlate2, on master/tock158 (round 1's `aimeringlook` carried, off). **Keep word** `lightringlook`, on by default (`keep_levers` in psp_platform.c); `lightringlookoff` turns it off. **Touches** `psp/src/psp_me.c`, `psp/src/psp_me.h`, `psp/src/psp_ge.c` (the light pass's triangle loop: a flag test while off).

## The bug

A points job (the mepoints ring job, type 12) not done 10 ms after its push makes the main CPU do it and rests the ring for 1-2 s; that second's tick runs ~50% slower [M]. `aimeringlook` covers the case inside the AI job. A PSP_RECON census of d20's flashlight stretch put every late of its run (12 of 12) somewhere else: the ME was inside the render job, at record kind 0x1a, `DEFER_LIGHT_DRAW` [M]. That record is the spotlight's light pass over the level's triangles near the player, with their cut pieces (B7's restoration; FLC's split). The replay serves the ring only between records, and one such record runs more than 10 ms on the ME. Its last look at the ring came up to 10.7 ms before the late.

## The fix

- `psp_ge_light_draw_body`'s triangle loop calls `psp_me_ring_look` every 8 triangles. It reads the ME's clock and drains the ring if ~1 ms has passed since the record began or since its last drain. Off the ME, or with the word off, the loop pays one test of a local flag.
- The loop is a safe point. Its state belongs to the GE layer: the light, the near plane, the memo, the cut pieces and the list. A ring job reads and writes only its own inputs and outputs: points, rays, poses, slant, batch records or the visibility copies. The replay already runs these between records with the same GE layer state live.
- The drain is `me_ring_drain`, as in `psp_me_yield`. A render job can run nested inside the AI job's yields; there the AI job's mark is down, so the drain runs to the ring's end, as `psp_me_yield` does. The look counts its time in `nested_cycles_total`, so MEBUSY's busy share takes it off once.
- The ring alone. Audio is still mixed between records, as before. The drain mixes a waiting block before each ring job only when the ring has jobs, as it does between records.
- Stack. A render job can sit on the AI job's stack, so the look runs only with a ring job's room (24 KB + 16 KB) above that stack's guard. Otherwise it counts a "passed by".
- The safety net stays: the 10 ms deadline, and the rest after a late job.

## Instruments

A line each second while the word is on: `ME: inside the light pass (lightringlook): N looks, M ring jobs run in them, K looks passed by for the stack`. In PSP_RECON builds each look also stamps mestash's last-look clock.

## Proofs

- Emulator [E]: play and both seeds, every tick, on the b30 route, c20 and d20 with the flashlight. The emulator's render job runs on the main CPU, where the look does nothing, so this proves the code around the look changes nothing. It doesn't prove the ME's interleaving.
- Pictures [E], d20: on vs off differ at 2 of 1,345 frames, off vs an off2 control at 3. So the look is picture-exact within a build. Across builds d20's pictures move with any rebuild, even one whose change is off.
- PSP-3000 pair [M]: d20 to exitat90, flashlight at 1400, `- | lightringlook` in one build, two runs a side, mouths on.
  - Late ring jobs: off 9 and 8 a run (points, poses and rays; each rests the ring 1-2 s); on 0 and 0.
  - The ME didn't stop except at the game's own exit. The looks never passed by for the stack.
  - In a busy second the light pass made ~960 looks and ran 50-100 ring jobs.
  - The AI word answered within 15.8 / 5.9 ms (off) and 14.1 / 5.2 ms (on) at worst; the bound is 20 ms.
  - Tick over game time 1400-2700: off 19.9 / 21.4 ms, on 20.0 / 20.7. fps 11.67 / 11.42 vs 11.75 / 11.50. Both are a tie inside the runs' own spread.

  It fixes the failure where it runs, at no measured cost.
