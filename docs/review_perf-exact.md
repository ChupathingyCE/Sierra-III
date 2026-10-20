# Review: perf-exact (exact speed-ups, after w140)

Three speed-ups that change no game state and no frame, each on by default and each with a keep word that restores the old path. A fourth target, the blob shadows' surface query, is sized here but not built yet.

Status: written against w140; the campaign and multiplayer builds compile. Play is proven exact in the emulator [E]; the pictures are not yet (see Evidence). The PSP timing A/B (a10 route, `- | shadowmemooff sleeperwalk stepsall`, two repeats) is queued.

## Evidence

- **Play [E]:** `statecrc` on against off (all three words together, `fixedticks fixedseed readswait`, hand-test routes): the play hash and both random seeds are identical at every tick on a30 (4,500 ticks), a10 (9,600) and c10 (3,600, the opening). Strict runs: 0 sites, no crash. The whole-state hash differs, only in `objects`, `object looping sounds`, `particle`, `contrail point` and `hud unit interface` (and late in a10 `actor` and `prop`), the parts drawing and the sound mixer write into; `tools/statecmp.py` does not tell which lever writes which (a per-object dump with each lever alone would).
- **b30 [E]:** its play hash parts at tick 1,326 and the seeds at 3,703, as every b30 pair does for any change that moves the emulator's speed (`big_levers.md`, `review_lk22b.md`: 1,326). So b30 gives no verdict; a30, a10 and c10 are the proof.
- **Pictures (`framecrc`) [E], open:** on against off, pictures differ in 123 of 2,244 frames (a30), 361 of 4,790 (a10) and 45 of 1,799 (c10). The same build twice already differs in about 2% of frames with `readswait` (a10: 181 of 8,706; the cause is outside the GE's list), so these rates are not yet a verdict. A second batch plays each map's baseline twice and each of SR and WS switched off alone, to say whether the gap is noise and which lever, if any, shows.
- **Gain, cache model [E]:** b30 route, 5,400 ticks, the three together: modelled cost (fetches + 71 x misses) 108.6G against 113.5G, **-4.4%**: tick phase -5.5% (fetches -7.8%), drawing -2.6%, other -10.1%. The runs part at tick 1,326 (b30), so play differs slightly. The model cannot give each lever alone; the PSP A/B measures time.

## SR: a shadow receiver's corners worked out once per vertex (`shadowmemooff`)

`psp_ge.c`, `psp_ge_shadow_draw_body`. A blob shadow lands on the level triangles under an object. The old loop worked out each triangle's three corners from scratch (camera position, the outside-the-shadow bits, the shadow texture's u/v and colour), so a vertex shared by six triangles was done six times, and every triangle began with a `corners = {{0}}` clear (about 481k clears in one b30 run [E]).

- A 64-entry memo keyed by vertex index holds the corner's values; a triangle copies them instead of recomputing.
- The per-triangle clear is gone. The clip path sets the one field the clear used to give it (`c[10] = 0`), so the clipped pieces are the same.
- `shadowmemooff` runs the old loop, which is kept beside the new one.

Why exact: the memo stores the same float results the old code computed from the same inputs, in the same order of operations [D]. Play identical [E]; the pictures' proof is open (Evidence). In the cache model `psp_ge_shadow_draw_body` plus `shadow_corner` are 2.4% (a30) to 3.2% (b30) of the drawing phase's fetches, so the memo's gain is about 1% of drawing [E].

## WS: settled scenery passed over in the object walk (`sleeperwalk`)

`psp_fixups.py`, `FIXES_WS` (applied after `FIXES_BONUS`). With wake-on-write, settled scenery is asleep, but `objects_update` still walked into each sleeper's full update path to find that out. `psp_ws_asleep` makes the same decision inline and skips the call.

- It takes the inline path only when the full path would take its asleep branch: a nonzero checksum, not restless, not marked, quiet for 2 or more ticks, and after the first full-path sleeper has set `psp_func_check`. Anything else goes the full way.
- It moves the same counters the asleep branch moves, so the log and the wake logic see no difference.
- A log line, "sleeping scenery passed over in the walk per tick", shows it working.

Why exact: the same objects are updated in the same order; the skipped call had no side effects beyond those counters [D]. Play hash and seeds identical on and off on a30, a10 and c10 [E].

## PS: the profile's steps timed one frame in four (`stepsall`)

`psp_platform.c`. Every profile step read the clock and updated its totals on every call (hundreds of steps a frame). Now steps are timed in one frame of four (`steps_frame_edge`, set from `psp_frame_account`), and `profile_report` scales the totals by frames over timed frames.

- Only `profile.txt` reads the step clock, so the game cannot see the change [D].
- `stepsall` times every frame, as before.
- **Reading profiles:** with this on, `profile.txt`'s step figures are a 1-in-4 sample, scaled. Short windows are noisier; use `stepsall` for a run that needs every frame.

## Sized, not built

- **Blob shadows (`render_object_shadow_end`):** 5.5 ms a frame on a10 and a30, 1.8 ms on b30 [M]; up to 3.7 ms a call. The step runs from `render_object_shadow_end` to the next mark, so it covers `structure_render_shadow`: the BSP query for surfaces inside the shadow volume (`structure_visibility_build_surfaces`, a node walk for small radii), the dynamic-triangle copy, and the GE's shadow draw (deferred when frames are in flight, so SR's gain may show in another step) [D]. The cache model with call edges says which of the three it is [E]: per `structure_render_shadow` call, `structure_render_build_dynamic_triangles` is 79k fetches on a30 (2.2% of the drawing phase) and 60k on b30 (1.7%); the GE side (`psp_ge_shadow_draw_body` + `shadow_corner`, what SR trims) is 84k (a30) and 53k (b30); the surface query is a node walk of about 30-40 `traverse_node` calls a shadow and falls under 0.7% of the phase. So a memo of the surface list would gain little; the target is the dynamic-triangle build (memoised per object while the volume is unchanged, or made cheaper), which stays unbuilt.
- **HUD and menu widgets:** `render_ui_widgets` and `process_ui_widgets` are 0 in play; their cost is the main menu (frame-capped) and the pause menu. The HUD's own work is about 0.9 ms a frame [E]. Low value.
- **Level of detail:** the step named `object_get_level_of_detail_pixels` actually times the lighting lookup; the LOD itself is about 0.2 ms a frame and a memo would save under 0.05 ms [E]. Not built.
- **Throttle:** the idle vblank wait is headroom, not work [M]. `render_model`'s stack array: no gain expected [D]. Scratchpad RAM: about 0.15 ms a tick at best [E].
