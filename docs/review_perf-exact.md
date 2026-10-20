# Review: perf-exact (exact speed-ups, after w140)

Three speed-ups that change no game state and no frame, each on by default and each with a keep word that restores the old path. A fourth target, the blob shadows' surface query, is sized here but not built yet.

Status: written against w140; the campaign and multiplayer builds compile. Play is proven exact in the emulator [E]; the pictures are exact for WS; SR's memo drew some blob shadows differently (see Evidence), its cause is found and fixed [D], and its pictures re-check is pending. The PSP timing A/B is read (Evidence, "PSP-1000"): the three together are 3.2% faster a frame on a10, nearly all of it in drawing.

## Evidence

- **Play [E]:** `statecrc` on against off (all three words together, `fixedticks fixedseed readswait`, hand-test routes): the play hash and both random seeds are identical at every tick on a30 (4,500 ticks), a10 (9,600) and c10 (3,600, the opening). Strict runs: 0 sites, no crash. The whole-state hash differs, only in `objects`, `object looping sounds`, `particle`, `contrail point` and `hud unit interface` (and late in a10 `actor` and `prop`), the parts drawing and the sound mixer write into; `tools/statecmp.py` does not tell which lever writes which (a per-object dump with each lever alone would).
- **b30 [E]:** its play hash parts at tick 1,326 and the seeds at 3,703, as every b30 pair does for any change that moves the emulator's speed (`big_levers.md`, `review_lk22b.md`: 1,326). So b30 gives no verdict; a30, a10 and c10 are the proof.
- **Pictures (`framecrc`) [E]: WS exact in pictures, SR is not.** On against all three off: pictures differ in 123 of 2,244 frames (a30), 361 of 4,790 (a10) and 45 of 1,799 (c10). A second batch, on one machine (a hardware renderer's output differs between machines, so it is only ever compared within one batch), ran the baseline (`on2`), `shadowmemooff` alone (SR off) and `sleeperwalk` alone (WS off), same words otherwise. Each frame's three hashes were sorted into "all equal", "one run odd" and "all differ":

  | map (frames) | all equal | baseline odd | WS-off odd | SR-off odd | all differ |
  |---|---|---|---|---|---|
  | a30 (2,244) | 2,094 | 35 | 33 | **68** | 14 |
  | a10 (4,790) | 4,425 | 92 | 57 | **191** | 25 |
  | c10 (1,799) | 1,754 | 0 | 0 | **43** | 2 |

  Same-build noise (`readswait` still leaves some) shows as every run being odd about equally often; the baseline and WS-off runs are (35 against 33, 92 against 57, 0 against 0), so WS changes no pixel beyond noise. The SR-off run is odd 2 to 3 times as often on a30 and a10, and on c10, where the other two never part, in 43 frames (2.4%) that only the SR-off run draws differently. So SR's memo changes pixels. First frames where only the SR-off run differs: a30 frame 7 (game time 16), 16 (34), 28 (58); a10 frame 750 (1,506), 771 (1,548), 779 (1,564); c10 frame 37 (76), 44 (90), 46 (94). The differences are small: in 1 to 3 of the 16 tiles a frame (a10 up to 9), c10 in the middle-left tiles and mostly in the opening's first 10% and one stretch near 40%. Hash-only, so how many pixels is not known.
  - Cause [D] (read in the code): the memo is indexed by `index & 63`, and a triangle looks up its three corners one after another before reading any. Two corners whose indices differ by a multiple of 64 share a slot: the later lookup refills it, and the earlier corner's pointer then reads the later vertex's camera position, outside bits and blob corner. Fixed (see SR below); the re-check, the same three-way sort with `on`, SR off and `on` again in one batch, is pending.
- **Gain, cache model [E]:** b30 route, 5,400 ticks, the three together: modelled cost (fetches + 71 x misses) 108.6G against 113.5G, **-4.4%**: tick phase -5.5% (fetches -7.8%), drawing -2.6%, other -10.1%. The runs part at tick 1,326 (b30), so play differs slightly. The model cannot give each lever alone; the PSP A/B measures time.
- **PSP-1000, a10 route, A/B [M]:** one build, the three words off against on (`shadowmemooff sleeperwalk stepsall`), two runs a side, the route to game time 18,000 (600 s), `hwprofile` and `frametrace` on; all four runs ended in the game, no crash. Levers on against off, per run and stretch (`tools/abreport.py`), the whole route: **frame 70.2 ms against 72.5 (-2.2 ms, -3.2%)**, drawing 47.4 against 49.4 (-2.0 ms, -4.0%), a tick 7.7 against 7.8 (-0.1 ms, -0.9%, inside the repeats' spread). With the same game time, the levers-on runs drew 9,051 frames against 8,870 (+2.0%).
  - By stretch (60 s each, game ticks in brackets): frame gap -0.1 ms (0-1,800, the opening, inside the spread), then -1.8, -3.5, -2.4, -2.5, -3.2, -4.1, -4.3, -3.2, -3.3 ms (-2.5% to -5.2%); every stretch after the opening is beyond the spread. Drawing carries it: -1.4 to -3.7 ms in every stretch after the opening. The tick gap is -0.1 ms in eight of ten stretches (six flagged beyond the spread, only because the repeats agree to 0.1 ms) and 0 in the others.
  - Where the drawing gain sits (`profile.txt`, every window of the route, weighted by frames; the steps are timed one frame in four with the levers on, scaled): `render_object_shadow_end` (the blob shadows) **6.89 ms a frame against 8.07 (-1.18 ms, -15%)**, both repeats of each side agreeing to 0.01 ms; the rest of the gap (about 1 ms) is spread thinly over the other steps (`structure_render_lightmaps` -0.4, `objects_update` -0.16, `rasterizer_model_draw` -0.15, small shares elsewhere) which is what the clock reads removed by PS look like (each step's time includes the clock read that ends it) [U]. SR is the only lever that touches the shadow step, so its share is about 1.2 ms a frame, ~60% of the whole gap [M for the step, U for the split]; PS the remaining ~1 ms [U]. WS's own part is the tick's 0.1 ms or less on this map [M].
  - Against the cache model: the total agrees (-3.2% frame on a10 against -4.4% modelled on b30), the split does not. The model put the gain in the tick (-5.5%) and drawing at -2.6%; on a10 the tick gain is ~1% and drawing's is 4%. Two reasons, both [U]: the model counts fetches and misses, not the clock syscalls PS removes (each step's clock read is a system call), and WS's scenery sleepers are fewer on a10 than on b30 (recon sized 0.3-1.8 ms a tick on b30). SR also gains more than recon's sizing (about 1% of drawing, from the fetch share): measured 1.2 ms a frame, 2.5% of drawing, so the memo's saving is more than its fetch share (the per-triangle zeroing and the float work are cheaper to keep once than the model reads).
  - What it means: SR is worth keeping on for the blob-shadow maps; PS's gain exists only while the step timers run (`notimers` and `lean` have none to save); WS is a small gain on a10, the b30 figure still wants its own PSP A/B (the next b30 route on the PSP). Not yet separated per lever: that needs each off alone (`keepsets '- | shadowmemooff | sleeperwalk | stepsall'`, 4 sets, a10 route about 20 minutes a run).

## SR: a shadow receiver's corners worked out once per vertex (`shadowmemooff`)

`psp_ge.c`, `psp_ge_shadow_draw_body`. A blob shadow lands on the level triangles under an object. The old loop worked out each triangle's three corners from scratch (camera position, the outside-the-shadow bits, the shadow texture's u/v and colour), so a vertex shared by six triangles was done six times, and every triangle began with a `corners = {{0}}` clear (about 481k clears in one b30 run [E]).

- A 64-entry memo keyed by vertex index holds the corner's values; a triangle copies them instead of recomputing.
- The per-triangle clear is gone. The clip path sets the one field the clear used to give it (`c[10] = 0`), so the clipped pieces are the same.
- `shadowmemooff` runs the old loop, which is kept beside the new one.
- A triangle's corners can share a memo slot (indices equal mod 64): the later lookup takes the slot. After the three lookups, any corner whose slot no longer holds its own index is worked out again into a local entry (`own[2]`; only corners 0 and 1 can be overtaken), so every corner gets its own values:

  ```c
  for (corner = 0; corner < 2; corner++)
  	if (m[corner]->key != triangle[corner] + 1)
  	{
  		shadow_memo_fill(&own[corner], stream->base, triangle[corner]);
  		m[corner] = &own[corner];
  	}
  ```

  The cost is two compares a triangle, and a refill only on a collision.

Meant to be exact: the memo stores the same float results the old code computed from the same inputs, in the same order of operations [D]. Play identical [E]. The first version's pictures were not identical (Evidence, "Pictures"): two corners of one triangle sharing a slot read each other's values. With the refill above every corner is the unmemoised one [D]; the pictures re-check is pending [E]. In the cache model `psp_ge_shadow_draw_body` plus `shadow_corner` are 2.4% (a30) to 3.2% (b30) of the drawing phase's fetches, so the memo's gain is about 1% of drawing [E].

## WS: settled scenery passed over in the object walk (`sleeperwalk`)

`psp_fixups.py`, `FIXES_WS` (applied after `FIXES_BONUS`). With wake-on-write, settled scenery is asleep, but `objects_update` still walked into each sleeper's full update path to find that out. `psp_ws_asleep` makes the same decision inline and skips the call.

- It takes the inline path only when the full path would take its asleep branch: a nonzero checksum, not restless, not marked, quiet for 2 or more ticks, and after the first full-path sleeper has set `psp_func_check`. Anything else goes the full way.
- It moves the same counters the asleep branch moves, so the log and the wake logic see no difference.
- A log line, "sleeping scenery passed over in the walk per tick", shows it working.

Why exact: the same objects are updated in the same order; the skipped call had no side effects beyond those counters [D]. Play hash and seeds identical on and off on a30, a10 and c10, and its pictures differ from the baseline no more than the baseline differs from itself (Evidence, "Pictures") [E].

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
