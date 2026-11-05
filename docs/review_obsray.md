# obsrayreuse: two repeated rays of the obstacle-avoidance pill test reused (OBSRAY)

On by default; off word `obsrayreuseoff`. Exact. Runs wherever the AI thinks (the ME's AI job or the main CPU).

## Why

An actor's path is built by the obstacle-avoidance search (`path_obstacle_avoidance.c`, `path_find`): a heap of steps, each tested with a pill test (`path_test_pill2d`) that casts six 2d rays across the pathfinding surfaces (`structure_test_ray2d`), plus a pill test per disc it floods. A goal it can't reach empties the heap: 128 steps. On c10 with an encounter placed beside the player, one actor's think made 132 steps, 654 pill tests and 4,160 rays [E]; on the PSP-3000 that kind of think held the ME's AI job ~42 ms (B60's think).

Two of the six rays repeat a call the pill test has already made, with the same inputs [D]:

- **The right side's ray across the pill** is the left side's call again. Both pass `&perpendicular` and `radius` from `point` on `surface_index`; the engine never negates it for the right side (only the right *point* is placed at `-radius`). Kept as the engine has it: it's what the game plays.
- **The last ray**, which finds the surface at the result's distance, is the first ray again whenever nothing shortened the distance (no hit, and the endpoint isn't the goal, so the length is the first ray's).

## The change (psp_fixups.py, FIXES_OBSRAY)

- `path_test_pill2d`: the first ray's length is kept; after the left side's ray across the pill, its answer is copied to `right_result` and the right side's call is skipped; the last call is skipped when the length is still the first's (and the structure was tested), since `ray_result` still holds the first call's answer.
- `ai.c` (`ai_initialize_for_new_map`): `psp_obsray_on` is read from the keep words on the main CPU when a map starts, before any actor thinks.
- `psp_platform.c`: `obsrayreuse` joins the levers on by default.

## Why it's exact

`structure_test_ray2d` reads only its arguments, the structure's collision BSP and pathfinding tags, and the breakable surfaces' flags; a pill test changes none of them, and the function writes all three fields of its result on every path. So a repeated call's answer is the earlier one, bit for bit [D]. Proof [E]: play and both seeds identical every tick, on against `obsrayreuseoff`, on the b30 route (4,500 ticks), c20 (3,600), c10 (2,700), d20 with the flashlight (2,700), and c10 with the encounter placed beside the player (1,350); strict 0 sites on every run. The whole-state hash differs where searches run (c20, with none, is identical throughout), as it did in earlier exact pairs; play and seeds are the proof.

## What it removes

Census (`obscensus`, recon builds) [E]: the repeats are 21-24% of the search's rays (b30 23.5%, c10 21.0%, c10 placed 21.4%, d20 20.8%). On c10 placed the rays fall 26,062 to 20,494 a run, but the surfaces they cross only 71,555 to 67,267 (-6%): the rays across the pill are a radius long and cross few surfaces. The disc tests (36,310 a run there) are untouched. So the saving on the search is between ~6% (if its time goes by surfaces crossed) and ~21% (by calls). The PSP-3000 pair prices it.

A memo of whole repeated finds was counted too and isn't built: finds whose whole input matched one of the last 64 carry 0.1-1.6% of the search's rays.
