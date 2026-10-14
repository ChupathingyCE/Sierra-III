# Review: vehcam122 (the Pelican's wall at the eye)

Sheepdog01 (2026-10-14): "Camera clipping while the player is in vehicles. Any time the player is in a Pelican, the wall behind the player clips or stops rendering."

Branch `vehcam122` (from `w122`), one commit in `psp/src/psp_ge.c`. Nothing changes by default except one log line.

## What was ruled out, and what is left

- **Not `nearauto` on the bench [D].** On a Pelican bench (`psp_player_seated` 2, a seat without the third-person camera) the Pelican's sphere holds the eye, counts as touching it, and caps the near plane at x1, the game's own 0.0625 (b116, `b113_candidates.md` #17). The x16 frames in b120's flight log are the opening cutscene's wide shots, not the bench.
- **The lead [D]: the model cut buffer runs out.** Seated in a cabin, the eye is a few tenths of a unit from the walls, so most of the wall it faces crosses the near plane or the guard band. Those triangles go through the CPU cut (`clip_triangle`). Its buffer (`model_cut`, in the scratchpad) holds 192 corners per rigid part, or per skinned batch: about 20 to 60 cut triangles. Past that, `clip_triangle` returned fewer pieces than the polygon had (rigid parts and see-through parts), and the skinned path skipped the triangle (`cut_count < MAXIMUM_MODEL_CUT_CORNERS - 9`). Neither was counted. A wall that loses its cut pieces shows holes, or vanishes where all of it is cut.

## The change

1. **A count**, `cut_pieces_lost` (always on): pieces `clip_triangle` had no room for, plus skinned triangles skipped for room. The log gets `GE: cut pieces lost to a full cut buffer N (since the start)` every 300 frames.
2. **`cutroom` (off for now)**: the cut buffer is 768 corners (33.8 KB), taken from the heap instead of the scratchpad. Every room check (`model_cut_room`) uses the size actually taken, so without the word nothing changes.

## Cost

- The count: one compare in `clip_triangle`, one in the skinned cut path.
- `cutroom`: 33.8 KB of heap, and the cut corners in cached RAM instead of the scratchpad (b108 moved them for 0.8 M cache misses over b30's beach fight [E]). Only while a model is at the eye.

## What the emulator showed (lab run 38592478877, `vehcam122b`)

b30 from the start, hands off but for short look pulses, shots every 5 s; `count` (with `nearclustercheck rigidrunscheck`) against `cutroom`:

- **The near plane sat at x1 on every frame of the bench and after it** (300 of 300 a window, "with a model near enough to cap it") [E]. `nearauto` isn't cutting the cabin on b122.
- **Pieces are lost:** 0 in the opening cutscene's 300 frames, 4,333 in frames 300-600 (the bench and the landing), then about 2,700 per 300 frames (9 a frame) for the rest of the run, with the Pelican beside the player [E]. With `cutroom`: 0 in every window [E].
- **The near runs' and rigid runs' verdicts all agreed** with their triangles' own (b119's shortcuts aren't dropping anything) [E].
- **The shots don't show the hole yet [E].** The look pulses hardly turned the seated view, so no shot faces the wall behind the bench; `count` and `cutroom` look the same in the views taken. That the lost pieces are Sheepdog01's missing wall is [U] until a shot faces it.

## Still open

- `vehcam122` (run 38591047447, b122 as shipped: `nearauto`, the game's own plane, `nearseatedfar`), queued on the build runner. Its `gamenear` side says whether the wall goes with the game's own plane too.
- A shot facing the wall: a longer look input while seated (a held stick, not pulses), or Sheepdog01 on hardware with `cutroom` on and off at the Pelican bench.
- If `cutroom` closes the hole, it is a restoration: on by default with an off word. A better shape keeps the scratchpad for the first 192 corners and spills only the rest to the heap, so the common case stays as fast as now.
