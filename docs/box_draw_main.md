# The main CPU's drawing: the box page (draw-main, stage 1)

Owner: builder2 (the census, this page, its PSP ask's reader); rulings and spec: the lead. The lab's brief is `briefs/draw_main.md` (2026-11-26 20:1x): measure today's drawing on v2's build by part, then the budget summing to 11 ms a frame on b30's fight, each cut as a lever case, the placement at one tick a frame, 30 fps, and the first slice. §1-§4 go to the lead before any code beyond §1's stamps.

Labels: [M] measured on the PSP, [E] the emulator, [D] derived from the code, [U] unmeasured. Every figure from before v2's build is a lead, marked with its date, until the census (§2) measures it again.

## 1. The instrument

Written by builder2, 2026-11-26 20:14, from Sierra-III builder2/vissplit 857c3ca4 (builder/tick2-coll 4c299ada plus the two log-only splits) and the engine tree at 2e5eac00.

### 1.1 What the build already gives [D]

| Instrument | Where | What it gives the drawing | Why it isn't the census |
|---|---|---|---|
| frametrace, metrics budget | `psp_platform.c:4016` (`PSP_METRICS_FRAMETRACE_FRAME`); `psp_frametrace.c:9-24`; `psp_metrics.c:12-16` | the drawing a frame (`d`: `main_game_render`, the engine's render pass and the GE lists' building), the frame's end (`e`: `psp_ge_frame_end`, the GE's wait and the swap), the ticks (`t`) and the rest (`r`), every frame, in slice 1's own build; the whole frame's p99 | one figure for the drawing: no parts |
| framepic | `PSP_FRAMEPIC=1` + `halo_profile.prx`; `psp_framepic.c:39-71`, `map_phases` :136-176; `docs/frame_picture.md` | the engine's perf phases as boxes, in the profiler's cycles and misses, p50 and p99: the render's world, objects, transparent, effects, HUD, its other work, and the waits (occlusion, texture). b30_leg2, PSP-1000, 2026-11-17 [M then, a lead now] (`frame_picture.md:117-123`): objects 10.7, world 9.9, effects 2.1, other 1.3, HUD 1.2, transparent 0.9 ms a frame | (a) an instrument build that compiles the perf hooks into every engine file but objects.c and physics.c (`psp_build.py:1005-1010`), the tick's included, so its tick isn't slice 1's on form; (b) each mark is a ~2 µs system call into the module (`frame_picture.md:125`); (c) its parts are phases, not this brief's: world holds the visibility and its wait on the ME, the level's main-CPU side and their GE building; objects holds the model walk, the hook and its GE building |
| pcsample | `psp_platform.c:4131-4155` (keep words `hwprofile`, `pcsample`, `pcsampleother`) | the PC sampled ~1,144 times a second, binned by phase | the sampler knows a sample's PC, never its caller: a leaf shared by several parts (matrix and vector work, memcpy, the tag getters, collision) belongs to none of them. That is REBUILD.md §3.4's 42-47% unnamed tail [D]; no mapping of functions to parts can name it |
| steps (`timerson`) | the breadcrumbs before each call in `render_frame`, `render_window`, `render_objects`, `render_object`, `hud_draw_screen`, `structure_visibility_compute` (`psp_fixups.py:2156-2175`); timed one frame in four (`psp_platform.c:665-671`) | each call site's time, step to step | off in every build (`notimers` by default, `psp_platform.c:1480-1488`); a busy frame passes 2,000 steps or more, each a clock read (a system call, ~0.5 µs, `frame_picture.md:84`): ~1 ms on a timed frame, ~4% of the drawing [D]; and a step's time runs flat to the next step, so a call's callees' work sits under whichever step came last |
| the hook's timer | `psp_fixups.py:360-364`, `:587-597` (`psp_model_draw_us[3]`, build 106) | the model hook's time before the GE (keys, colours) and inside `psp_model_draw`, and its count | `psp_microseconds` reads 0 under `notimers` (`psp_platform.c:2800-2803`), so it's dark in every census-shaped run |

### 1.2 What it lacks

One split, every frame, in slice 1's on form with the tick's files untouched, of the drawing (`d`) into the brief's parts: the model walk; the hook and its GE lists' building and CALLs; the visibility and its wait on the ME; the level's main-CPU side; effects; transparent; HUD and its screen effect; the lens flares; the render's share of sound (`sound_render`, ~2.7 ms in REBUILD.md §3.1's "the rest", which sits inside `d`); collision reached from drawing; the texture and occlusion waits. The drawing's own work between those is the unnamed share, which the census must hold at 15% or under.

### 1.3 The stamp set: `drawsplit` [D, a design until built]

- **Its switch and word.** Build switch `PSP_DRAWSPLIT=1`, keep word `drawsplit`. With the switch off psp_build.py and psp_fixups.py add nothing, so the EBOOT is byte-identical to its head's (the check: the runner build's EBOOT hash against the head's). It refuses to build with `PSP_FRAMEPIC=1` (both define the engine's `halo_perf_*`).
- **Level 1, the engine's own phases, in the drawing's files only.** `-DHALO_PERF_LOG=1` for `source/render/render.c` (the 26 render-part switches, ~25 a frame: once per window, `render.c:438-909`), `source/main/main.c` (render, sound_render, present, vsync: `main.c:3516-3520`, `:4031-4079`), `source/interface/interface.c` (HUD and the screen effect, `:741-743`), `source/cache/xbox_texture_cache.c` (the texture wait, `:1266-1316`) and `source/rasterizer/xbox/rasterizer_xbox_widgets.c` (the occlusion wait, `:219-231`). Never game.c, game_time.c, objects.c, physics.c or collisions.c: they're the tick's (collisions.c is LTO with store_coll.c at 4c299ada). The hooks take framepic's expression form (`psp_fixups.py:2185-2204`, its `#ifdef PSP_FRAMEPIC` widened to either switch). `halo_perf_begin`, `_end`, `_render_part` and `_frame` come from a new `psp/src/psp_drawsplit.c`: each reads the clock (`sceKernelGetSystemTimeLow`) and charges the time since the last read to the part then open.
- **Level 2, inside the parts, port code only.** The hook: the build-106 site (`psp_fixups.py:587-597`) stamped on drawsplit's clock, not `psp_microseconds`, so it isn't dark under `notimers`; the GE lists' building and their CALLs inside `psp_model_draw` and the level's draw in `psp_ge.c` (the functions named in the code step); the visibility's wait on the ME at `psp_mevisland_collect` (structure_visibility.c, builder2's own file since 3cf1f1df). The model walk is then render objects less the hook.
- **Collision reached from drawing.** Its engine hook (`collisions.c:500`, `:751`) is a tick file's, so it isn't compiled in. Its drawing-side callers are stamped at their call sites if they sit in the drawing's files; the code step lists them (file:line, calls a frame). A caller in a tick file, or more call sites than level 2's budget holds, is a line to the lead, and that time stays in its part, said so.
- **The line.** One every 300 frames, beside frametrace's: for each part, ms a frame (mean, p50, p99, from a per-part histogram in 0.1 ms bins), the drawing's total (`d`, the same frames), the parts' sum, the unnamed (the render's own time between parts), and the stamps a frame. Under the log's 511 characters (lab `toolbox/fmtlen.py`), or two lines.
- **Its cost.** Stamps a frame times a read's price (~0.5 µs, a lead from `frame_picture.md:84`); the bar is 1% of the drawing, which on b30 runs 16.4 (the opening) to 30.9 ms (landing) a frame [archive, as_of 2026-11-15, REBUILD.md:51-60], so ~0.16 ms on its lightest stretch, ~320 reads a frame. Level 1 is ~60. Level 2 scales with the items drawn: if its reads a frame (counted in the emulator first [E]) would pass the bar, level 2 is timed one frame in N, as the steps are, and level 1 every frame. **Proved on the PSP**: the census pair's side A is the same head with the switch off, side B on; the drawing's `d` a frame, B against A, within 1%.
- **Exact.** The stamps write nothing the game reads: b30 and c20 in the emulator with `drawsplit` on and off, the play hash equal.
- **Placement.** Level 1 changes the text of render.c, main.c and the rest. If slice 1's text layout (`PSP_TEXT_LAYOUT`) places any of their functions, the instrument moves them (the lab's pending lesson `placed-instrument`): the code step checks the layout's names against those files' functions first, and says what moved.

### 1.4 The base

Sierra-III `builder2/drawmain`, cut from builder2/vissplit 857c3ca4 so the census also carries the visibility and points splits. Slice 1's on form is shipck-b30's switches (lab `toolbox/fixform.py` ship form + `PSP_TICK2_MOTIONNEW=1`). At the ask (by 2026-11-28 12:00) the stamps go onto the builder's head then, said in the ask.

## 2. The census [M]

Not yet: one ask (b30_leg2 and c20_leg2, PSP-1000, `drawsplit` on, slice 1's on form, two runs a leg), the lab's row `drawmain_census`.

## 3. The budget and the cuts [D]

Not yet.

## 4. Placement and the build cut [D]

Not yet.
