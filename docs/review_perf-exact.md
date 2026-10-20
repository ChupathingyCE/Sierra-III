# Review: perf-exact (exact speed-ups, after w140)

Three speed-ups that change no game state and no frame, each on by default and each with a keep word that restores the old path. A fourth target, the blob shadows' surface query, is sized here but not built yet.

Status: written against w140; exactness and the gain are being measured in the emulator (statecrc and framecrc on and off on b30, a30, a10 and c10; the cache model on b30). Until those runs pass, treat every lever below as unproven.

## SR: a shadow receiver's corners worked out once per vertex (`shadowmemooff`)

`psp_ge.c`, `psp_ge_shadow_draw_body`. A blob shadow lands on the level triangles under an object. The old loop worked out each triangle's three corners from scratch (camera position, the outside-the-shadow bits, the shadow texture's u/v and colour), so a vertex shared by six triangles was done six times, and every triangle began with a `corners = {{0}}` clear (about 481k clears in one b30 run [E]).

- A 64-entry memo keyed by vertex index holds the corner's values; a triangle copies them instead of recomputing.
- The per-triangle clear is gone. The clip path sets the one field the clear used to give it (`c[10] = 0`), so the clipped pieces are the same.
- `shadowmemooff` runs the old loop, which is kept beside the new one.

Why exact: the memo stores the same float results the old code computed from the same inputs, in the same order of operations [D]. Proof pending: framecrc on and off.

## WS: settled scenery passed over in the object walk (`sleeperwalk`)

`psp_fixups.py`, `FIXES_WS` (applied after `FIXES_BONUS`). With wake-on-write, settled scenery is asleep, but `objects_update` still walked into each sleeper's full update path to find that out. `psp_ws_asleep` makes the same decision inline and skips the call.

- It takes the inline path only when the full path would take its asleep branch: a nonzero checksum, not restless, not marked, quiet for 2 or more ticks, and after the first full-path sleeper has set `psp_func_check`. Anything else goes the full way.
- It moves the same counters the asleep branch moves, so the log and the wake logic see no difference.
- A log line, "sleeping scenery passed over in the walk per tick", shows it working.

Why exact: the same objects are updated in the same order; the skipped call had no side effects beyond those counters [D]. Proof pending: statecrc on and off.

## PS: the profile's steps timed one frame in four (`stepsall`)

`psp_platform.c`. Every profile step read the clock and updated its totals on every call (hundreds of steps a frame). Now steps are timed in one frame of four (`steps_frame_edge`, set from `psp_frame_account`), and `profile_report` scales the totals by frames over timed frames.

- Only `profile.txt` reads the step clock, so the game cannot see the change [D].
- `stepsall` times every frame, as before.
- **Reading profiles:** with this on, `profile.txt`'s step figures are a 1-in-4 sample, scaled. Short windows are noisier; use `stepsall` for a run that needs every frame.

## Sized, not built

- **Blob shadows (`render_object_shadow_end`):** 5.5 ms a frame on a10 and a30, 1.8 ms on b30 [M]; up to 3.7 ms a call. The step runs from `render_object_shadow_end` to the next mark, so it covers `structure_render_shadow`: the BSP query for surfaces inside the shadow volume (`structure_visibility_build_surfaces`, a node walk for small radii), the dynamic-triangle copy, and the GE's shadow draw (deferred when frames are in flight, so SR's gain may show in another step) [D]. The cache model with call edges says which of the three it is; the candidate exact levers are a per-object memo of the surface list while the shadow volume is unchanged (exact: the BSP is static) and a cheaper walk.
- **HUD and menu widgets:** `render_ui_widgets` and `process_ui_widgets` are 0 in play; their cost is the main menu (frame-capped) and the pause menu. The HUD's own work is about 0.9 ms a frame [E]. Low value.
- **Level of detail:** the step named `object_get_level_of_detail_pixels` actually times the lighting lookup; the LOD itself is about 0.2 ms a frame and a memo would save under 0.05 ms [E]. Not built.
- **Throttle:** the idle vblank wait is headroom, not work [M]. `render_model`'s stack array: no gain expected [D]. Scratchpad RAM: about 0.15 ms a tick at best [E].
