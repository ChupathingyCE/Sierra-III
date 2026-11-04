# Review: DRAWPART slice 2 and LIGHTREUSE, the model walk in two stages

The drawing rebuild's stage 3, second slice (`docs/draw_rebuild.md`; slice 1 is `docs/review_lightfirst.md`). Both are off by default: keep word `drawpart` (which turns `lightreuse` on too) and `lightreuse` alone.

## What changed

One fixup group, `FIXES_DRAWPART` in `psp_fixups.py`, on `source/render/render_objects.c`, applied after `FIXES_LIGHTFIRST`:

- **lightreuse.** LIGHTFIRST's pass (`psp_light_first`) now remembers, for each of the view's first 256 objects it lit, the object and the level of detail it lit it at. In the walk that follows, `render_object` takes that object's lighting straight from its render state (`psp_walk_lighting`), and `render_object_list` takes the level of detail (`psp_walk_level_of_detail_pixels`), instead of working them out again.
- **drawpart.** While the walk records (`psp_drawpart_recording`), `render_object_list` doesn't call `render_model` or `widgets_render`. It writes a record of the call and its arguments (`psp_drawpart_model`, `psp_drawpart_widgets`). `psp_drawpart_draw` makes the recorded calls in the walk's order at the walk's end, or when the 64 records are full.
- `psp_render_states_spare` is LIGHTFIRST's test, moved into a function of its own so `psp_drawpart_wanted` uses the same one.
- The engine's own `render_model` and `widgets_render` calls are now braced, because the step labels put a `PSP_STEP` statement before each of them.

## Why

Per object, the walk ran about 20 KB of hot code lines, past the 16 KB I-cache: the walk's own code (`render_object`, `render_object_list`, the level of detail, the lighting lookup), then `render_model` and the rasterizer's per-part chain. So each object fetched all of it again. LIGHTFIRST took the lighting's refresh out of the walk. With LIGHTFIRST on, two things were still left in the walk:

- **Its second lighting call.** The walk still called the lighting lookup for each object, and its refresh path (`object_render_state_refresh`) still missed as before. A second refresh in one scene changes nothing. So lightreuse takes the state pass A refreshed.
- **The walk's own code** between each object's models. drawpart moves the models' and widgets' calls out of the walk. The walk then runs only its own code over a run of objects, and the models' chain runs over the same run after it.

## Why it's exact

- **The calls are the same.** They are made with the same arguments, in the same order: models and widgets as the walk met them, children after their parent's widgets. Nothing else draws between them. The debug drawing (`debug_objects`, `debug_inactive_objects`) turns the recording off.
- **What a record points to doesn't change before its call.** A record holds pointers to:
  - the object's node matrices (its own, or its interpolated pose, kept per object for the frame);
  - its permutations, change colours, function values and bounding sphere;
  - its render state's lighting.

  The walk writes none of these. It records only while the state cache has a free slot for every object of the view (plus 16), so no state it takes is one a record points to.
- **The RSO's choice moves with the record.** The walk sets `psp_rso_object` for each model's call. The record keeps that value, and the call sets it again just before `render_model`.
- **The lighting it reuses is the walk's own lighting.** A second lookup in the same scene, at the same level of detail, refreshes nothing and returns the same state (`review_lightfirst.md`). `psp_walk_lighting` takes the state only if it still belongs to the object and was refreshed in this scene.
- **The level of detail is the same number.** It's worked out from the same camera and the same sphere.

## Proofs

- **Exact [E].**
  - Play and both seeds are the same every tick on b30 (4,500 ticks), c10 and c20 (3,600 each) and d20 (2,700), drawpart against off.
  - The GE's work is the same list by list on all four maps: 2,280, 1,832, 1,829 and 1,378 lists.
  - Pictures: b30 differs at 161 of 2,247 frames, against 157 for off against off; the other maps differ at 2-12 frames.
  - The whole state's bytes differ from off, as they did with LIGHTFIRST alone.
  - The front door is clean: the menu, then a10, with drawpart on.
- **In the cache model [E]** (the drawing's misses over the run; ms at 80 cycles a miss; the tick unchanged):

  | | drawpart | lightreuse alone |
  |---|---|---|
  | b30 route | I-misses -9.1%, D -2.4%: -0.96 ms a frame (-3.3%) | -0.27 ms |
  | c10 opening | I-misses -22.0%, D -5.6%: -1.95 ms a frame (-8.1%) | -0.70 ms |

  With `render_model`'s calls in a cache pair of their own (this split's ceiling), the drawing's I-misses fall 22.9% on b30 and 21.7% on c10. So c10 reaches the whole ceiling and b30 about 40% of it. Why b30 takes less isn't measured yet [U]. Two candidates: b30's ~89 objects and their widgets fill the 64 records more than once a frame, so its runs are shorter; and the ceiling's pair also keeps `render_model`'s lines across the rest of the frame (the level, the effects), which counts for more on b30's heavier frame.
- **On the PSP** [M] (`- | drawpart`, 2 runs a side):
  - **c10 opening, PSP-3000:**
    - 300-1500: frame 68.5 → 64.3 ms (-6.1%), drawing -2.7 ms (-8.5%).
    - 1500-2600: frame 44.4 → 41.1 ms (-7.4%), drawing -3.4 ms (-12.4%). The tick there rises 0.9 ms; the cause isn't known [U].
  - **b30 route, PSP-1000:**
    - Cinematic: frame -1.7%.
    - Flight: frame 134.1 → 128.3 ms (-4.3%), drawing -3.6 ms (-7.9%), tick -1.0 ms.
    - Landing (2000-2600): frame +1.5 ms (+1.2%). Read 2100-2650 it's +1.0 ms (+0.8%), drawing +0.4 ms. The PSP's own drawing counters there are no higher. Read to 2700, the landing seems +3.1%, but that's the windows: the drawpart side runs ~30 ticks ahead from the flight on, so the 2700 cut drops its first fast window.
    - The route from 570 to 2600: frame -2.8%, drawing -5.7%.
  - Every gap above is larger than its side's own spread and than the stretch's floor.
  - The PSP's gain is 2-4 times the cache model's, as the lab's cache rule expects.

## Memory

`psp_drawpart_records`: 64 records of about 100 bytes (6.4 KB). `psp_walk_lit` and `psp_walk_pixels`: 2 KB. All static.
