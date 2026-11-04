# Review: aimepathlook, the AI job looks while a path is built (B56, the ME's side)

**Branch** fix/b56, on mepointsclaim (`docs/review_mepointsclaim.md`). **Keep word** `aimepathlook`, on by default (`keep_levers` in psp_platform.c); `aimepathlookoff` keeps the marks and drops the look. **Touches** psp_fixups.py (FIXES_AIME2A: path_smoothing.c, path_obstacle_avoidance.c, path_obstacles.c, actors.c), psp/src/psp_platform.c.

## The stretch [M]

On the PSP-1000 (d20 to exitat120, the run with B56's three failed points jobs), the aime2a census's longest stretch without a place to answer the word was 9.0 ms. It ran from step 10's mark (a path search's node) to step 11's (the obstacle-avoidance search's first step), with only step 11's bit set inside it. The census's "a path search's node 9046 us" is the same stretch: a step's worst runs from its mark to the next mark of any step, so it holds everything after the search's last node.

## What runs there [D]

`actor_path_refresh` → `path_state_find` (its last node), then `path_state_build_path`:
- the walk back up the nodes into `raw_steps` (at most 64; cheap);
- `path_smooth`: up to 4 rounds. Each round scans the raw path with `structure_test_pill2d`, a 2D surface walk from the current point to a raw step, and then finds two turning points by walking the structure's edges;
- `path_avoid_obstacles`, per smoothed step: `obstacles_get_discs_in_sphere` (each object within 4 units, and each of its pathfinding spheres), `obstacles_recompute` (each disc's neighbourhood: quadratic in the discs), then `path_find`, whose steps AIMEOBSLOOK already looks between.

Which of the two parts makes the 9 ms isn't known yet. The new census steps split it on the PSP (step 13, a smoothing test; step 14, an obstacle gathered).

## The fix

- `psp_aime_build_look(step)` (actors.c), as `psp_aime_search_look`: a census mark, then the look at the collection's word (`psp_aime_late_check`). With `aimeringlook` on, that look also serves the ring.
- Step 13 is before each pill test of the smoothing's scan (the default from the back, and the game's scan under `aipathold` or `aipathcheck`) and before each turning point.
- Step 14 is before each smoothed step's obstacles are gathered, before each object in `obstacles_get_discs_in_sphere`, and before each disc in `obstacles_recompute`.
- **Why a stop there is safe:** between these calls the build holds only its own locals (`raw_steps`, `smoothed_steps`, the local `obstacles`; `objects_in_sphere` has already filled its list), and no collision walk is open. The actor's own writes before them (its path marked not valid) are the same as at the path search's nodes and the obstacle steps either side. So a stop is put back by 1b's copies, as there. Each function has one caller, the path build.
- Off the ME's pass (`psp_aime_arm` 0), each place costs one load and a branch.

## What it doesn't do alone

A look answers the collection's word. The ring is served at the looks only with `aimeringlook` (off by default; its v2 was safe on the PSP-3000 but ran ~0 ring jobs, and its looks missed this stretch). So the points job pushed while the ME is in this stretch is begun sooner only with both on. The PSP pair below prices both.

## Proofs

- Exact [E]: (pending) play and both seeds against the base on b30's route, c20 and d20, and d20 with `aimelate3` on both sides, which stops actors at the new looks.
- PSP-1000 [M]: (pending) d20 to exitat120: the longest stretch, steps 13 and 14, the points jobs "not begun", ring lates.
