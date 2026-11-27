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

### 1.3a Level 1 as built (builder2, 2026-11-26 20:26)

- **Placement first.** Slice 1's layout (`PSP_TEXT_LAYOUT=w_tgme2m1_tgc90`, the ship form) places 11 lines, 10 functions, from the five files (lab `toolbox/placedin.py`, the engine at 2e5eac00). Four of the functions hold hooks level 1 compiles in, so they grow and every placed line after them moves: `render_player_frame` (layout lines 294-295; render.c:607), `interface_draw_screen` (362; interface.c:741, :743), `_texture_cache_bitmap_get_hardware_format` (625; xbox_texture_cache.c:1266, :1316), `main_game_render` (781; main.c:3516-3520). The other six (texture_cache_new, set_window_camera_values, texture_cache_bitmap_delete, main_framerate_render, `_rasterizer_widget_begin`, halt_and_catch_fire) hold none, though a hooked static callee inlined into a placed function grows it too, which the text can't show. The census's side B is therefore a moved layout from the first of them (line 294 of 850) on: what moved, in bytes and I-cache sets, is lab `toolbox/placediff.py` over the off and on builds' `halo.nm.txt`, read with the census; the 1% bar on `d` (B against A) prices it with the stamps.
- **The switch.** `psp_build.py`: `PSP_DRAWSPLIT=1` adds `psp_drawsplit` to the port's files, `-DPSP_DRAWSPLIT=1` to the defines, and `-DHALO_PERF_LOG=1` to `DRAWSPLIT_UNITS` only (the five files and psp_drawsplit.c); it refuses `PSP_FRAMEPIC=1` beside it. `psp_fixups.py`'s perf-log header fix is taken for either switch, its expression form under `#if defined(PSP_FRAMEPIC) || defined(PSP_DRAWSPLIT)`.
- **The clock.** `psp/src/psp_drawsplit.c`: the render phase's begin opens the frame, `psp_frame_account` closes it with frametrace's `d` (`psp_drawsplit_frame`, first thing in it, under `#ifdef PSP_DRAWSPLIT`). A hook that changes the part charged reads `sceKernelGetSystemTimeLow` and charges the time since the last read to the part open; render parts replace each other, the waits and `sound_render` nest (a stack 8 deep); a hook that leaves the same part charged reads nothing; sound, network, present and the vertical blank's wait are outside `d` and unmapped. 14 parts (unnamed, begin, setup, world, objects, transparent, effects, flares, hud, screen, end, sound, occlusion, texture), each with a run-wide histogram in 0.1 ms bins to 60 ms, as are `d` and the parts' sum (41 KB, calloc'd when the word is first seen).
- **The line.** `drawsplit: N frames (run F): d M/P50/P99; parts M; unnamed M/P50/P99; world ...; stamps M a frame (most K)`, every 300 frames; built in a 500-byte buffer, so under the log's 511; 374 characters in the host test.
- **Checks [E/host].** The host test (lab `toolbox/drawsplit_test.c`, a fake clock driving main.c's order of hooks): every part charged as driven, `d` equal to the parts' sum, 12 stamps a frame, no unbalance. Fixups on fresh trees at 2e5eac00, ship form + `PSP_TICK2_MOTIONNEW=1` (lab `toolbox/fixform.py`): exit 0 at 9f39a4ef, here off, here on; the off tree and log identical to 9f39a4ef's; on, `halo_perf_log.h` the only file differing (the log's files-changed count 191 to 192). With the switch off psp_build.py adds nothing and psp_platform.c's block is preprocessed away (it has no `__LINE__` user), so the EBOOT should be the head's: proved by the runner build's EBOOT hash. The compile on psp-gcc is the runner build's (no PSP toolchain here: the host's `cc -fsyntax-only` only).
- **Exact, and the off build the head's [E] (drawexact1, sierra-batch 45930727785, read 2026-11-26 21:0x; the lab's row `drawexact1_exact`).** Slice 1's on form, b30_leg2 and c20_leg2 to 120 s: on against off, and off against its control, play and both seeds equal on all 3600 ticks on both maps; the off EBOOT's hash (68d91d3e9bb2a379) is 9f39a4ef's; both on runs log `drawsplit: on` and a line each 300 frames, the parts summing to `d`. In the emulator 29-31 stamps a frame (the host test's 12 drove main.c's hooks only). Setup, screen, sound, occlusion and texture read 0.0 ms on both maps, world and objects carrying the rest: a lead for the census to answer (hooks that don't fire on this path, or their time charged to the part open), never a price, as none of the line's times are.

### 1.4 The base

Written by builder2, 2026-11-26 23:46, in place of the plan this section held (the stamps on builder2/vissplit 857c3ca4).

- **The census's head.** Sierra-III `builder2/drawcensus` c5ff3114: this branch's two commits (9f39a4ef, 077ba4f3) taken onto the builder's slice 1 head dbc76ba0 (task 5 step 3 fix (a), the colour anchors; on builder/motionplace). `git range-diff 857c3ca4..077ba4f3 dbc76ba0..c5ff3114` shows both commits equal, so the stamps measured are the ones §1.3a proved exact. The census therefore does **not** carry vissplit's visibility and points splits: the visibility's wait on the ME sits inside `world`, as §1.2 says it would without level 2.
- **Its form.** Slice 1's new form: ship form + `PSP_TICK2_MOTIONNEW=1`, `campaign-mrc6531`, `PSP_TEXT_LAYOUT=w_tgme2m1mp_tgc90` (w_tgme2m1_tgc90 plus six `COLOUR` anchors and MOTIONNEW's six motion names, 924 lines). Side A drawsplit off, side B `PSP_DRAWSPLIT=1`, two runs each, PSP-1000. A's EBOOT is b739ef687f633bf9, flipck's B's, so A is the build flipck priced [D] (the lab's row `drawmain_census`).
- **Placement on this layout [D, from the layout's text].** The four hooked placed functions sit at w_tgme2m1mp_tgc90's lines 298-299 (`render_player_frame`), 370 (`interface_draw_screen`), 637 (`_texture_cache_bitmap_get_hardware_format`) and 794 (`main_game_render`), not §1.3a's 294-781 (that was w_tgme2m1_tgc90). Each `COLOUR` line pads the next placement back to the 8 KB way offset it had on shipck A, so B's growth moves the placed text only until the next anchor: lines 298-364 (anchor at 365), 370-483 (anchor at 484), 637-760 (anchor at 761) and 794 to the end. What moved, in bytes and I-cache sets, waits on `toolbox/placediff.py` over A's and B's `halo.nm.txt` [U: the pair keeps no nm table; a build of each side is an ask to the master].

## 2. The census [M]

Written by builder2, 2026-11-26 23:46, from the lab's rows `drawcensus_b30` (sierra-hw 45943027708) and `drawmain_census` (sierra-hw 45945230751), both [M], PSP-1000, at c5ff3114 (§1.4); the lab's page `findings/drawcensus_2026-11-26.md`. Every figure is ms a frame, B's mean (two runs, run 1 / run 2 where they differ), from B's `drawsplit:` lines; `d` is frametrace's drawing.

### 2.1 The stamps' cost: inside the 1% bar [M]

| Leg, window | A's `d` (2 runs) | B's `d` (2 runs) | B against A |
|---|---|---|---|
| b30_leg2, the fight | 34.4 / 34.5 | 34.3 / 34.2 | 0.6% under |
| c20_leg2, peak 1200-1800 | 22.2 / 22.1 | 22.3 / 22.4 | 0.9% over |
| c20_leg2, whole 0-3560 | 23.2 / 22.2 | 22.4 / 22.4 | 1.3% under (A's runs 1.0 apart) |

benchreport's frametrace `d` on both sides. No cost shows past the runs' own spread, so B's lines stand as the census. The stamps: 29.0-30.7 a frame on c20, as the emulator counted (§1.3a). B's heap top is 40 KB over A's (4528 against 4488 KB), the histograms' 41 KB calloc [D].

### 2.2 The census by part [M]

| Part | b30 fight (ticks ~1108-1732) | b30 opening (~500-1105) | c20 whole (frames 1-1800, ticks ~0-3330) | c20 line ~1033-1619 | c20 line ~1619-2196 |
|---|---|---|---|---|---|
| `d` | 34.4 / 34.4 | 25.9 / 26.0 | 22.17 / 22.08 | 21.3 | 28.2 / 28.4 |
| objects | 15.7 / 15.9 | 13.3 / 13.4 | 5.53 / 5.57 | 5.3 / 5.4 | 8.0 |
| world | 11.5 / 11.2 | 8.6 | 9.35 / 9.37 | 9.4 | 10.2 / 10.4 |
| effects | 3.2 | 0.8 / 0.9 | 2.15 / 2.10 | 1.7 | 3.7 |
| hud | 2.4 | 0.3 | 1.85 / 1.82 | 1.8 | 3.1 |
| transparent | 0.8 | 1.7 | 1.82 / 1.80 | 1.9 | 1.9 |
| flares | 0.3 | 0.8 | 0.93 / 0.92 | 0.8 | 0.8 |
| end, begin, setup | 0.2 / 0.1, 0.1, 0.1 | | 0.22, 0.18 / 0.17, 0.10 | | |
| screen, sound, occlusion, texture | 0.0 each | | 0.00 each | | |
| unnamed | 0.1 (0.3% of `d`) | | 0.10 (0.5%) | 0.1 | 0.1 |

- **The windows.** A line covers 300 frames: ~620 ticks on b30's fight, ~580 on c20. None lies wholly inside b30's fight (1098-1712) or c20's peak (1200-1800). b30's fight is the line over ~1108-1732: the fight plus 20 ticks past its end. c20's peak is the two lines that straddle it, each [M] for its own span. A figure for 1200-1800 alone would be an interpolation [U] (lab `toolbox/drawsplitwin.py` gives each line's tick span).
- **The naming bar is met on both legs** (the brief's done-when 1: parts within 3% of `d`, unnamed at 15% or under). The parts sum to `d` (100.0-100.1% on c20) by construction: drawsplit charges every clock read's interval to the part then open. The lines' `d` on c20's whole (22.1) and benchreport's (22.4) agree within 1.5%, so the stamps time the same span.
- **The four 0.0 parts are really small here.** The four 0.0 parts read 0.0 in the emulator too (§1.3a), and with unnamed at 0.1, no time is going missing. The texture and occlusion waits don't cost the main CPU on these paths, and the render's share of sound (`sound_render`, ~2.7 ms in REBUILD.md §3.1, [archive, as_of 2026-11-15]) reads 0.0 inside `d`. So on v2's build it runs outside the drawing, where §1.3a leaves sound unmapped [D]. Level 2 (§1.3) isn't needed for naming. It would only split `world` (the visibility, its wait on the ME, the level's main-CPU side) and `objects` (the model walk, the hook, its GE lists) for the cuts.
- **Where the drawing goes.** On b30's fight, objects and world are 27.15 of 34.4, ~79%. Effects and hud add 5.6 more. On c20's whole they're 67%, and effects, hud and transparent take 26%. As c20's fight comes in, line to line, objects goes 5.3 to 8.0, effects 1.7 to 3.7, hud 1.8 to 3.1, while world moves only 9.4 to 10.3. So world is the steady floor on both maps, and objects and effects scale with the fight. Task 3 sizes the 11 ms budget on b30's fight (the heavier, `d` 34.4) and checks it against c20's two peak lines.
- **Beside the drawing [M].** The tick on the fight/peak: b30 A 35.8 / 35.8, B 35.4 / 35.3; c20 A 31.0 / 30.9, B 30.5 / 30.5. c20's frame by the clock on the peak: A 86.3 / 85.9, B 85.5 / 85.3. B's tick runs ~0.45 under A's on both legs, though the hooks are in the drawing's files only. A's tick on b30's fight matches flipck-b30's 35.7 to within 0.1 (bar 0.2), so the rig held.
- **The tick's 0.45 is placement [D]** (builder2, 2026-11-27 00:12; lab `toolbox/placediff.py` on this head's `psp/layout/w_tgme2m1mp_tgc90.txt`, A = sierra-build 45926591725's `halo.nm.txt`, B = drawsplitB, sierra-build 45948893789, EBOOT 79ab5c9c142c08ad, the same as the pair's B in sierra-hw 45943027708's `build-B.log`). c5ff3114 is dbc76ba0 plus the drawsplit hooks only, yet B's placed block leaves A's colour by more than 2 sets in six runs: line 164 `store_motion_moved` (-21 sets), lines 284-298 `render_player_frame`'s run (-41), 300-324 (-19), 325-344 `lights_preprocess_scene`'s (-20), 345-364 `biped_update` and `store_motion_update` (-21), 447-482 `encounters_test_activation`'s (-19). Lines 4-27, 166-178, 366-445, 485-585 and 762-766 hold A's colour exactly; the rest are within 3 sets. The moves start at line 163, not at 294. `store_motion_call` comes out 1,208 B smaller, `unit_update_animation` 192 B, `psp_bsp2d_test_sphere` 60 B, and none of them is in the drawing's five files, so the hooks moved whole-program inlining under LTO and PGO (the mechanism is [U]). So three tick runs moved (the motion store, `biped_update`, the encounters), and B's tick difference from A is charged to placement, not to the stamps. Which move gains and which loses is [U]: placediff gives where the code moved, not what each move costs. The census in §2.2 doesn't depend on this: the drawing's `d` is inside the 1% bar (§2.1).
- **Unread.** Every p99 of 60.0 is the 0.1 ms histogram's top bin, not a measured worst [U]. The p50s are in the lines (the lab's page) and not repeated here.

## 3. The budget and the cuts [D]

Written by builder2, 2026-11-27 00:16, from §2.2's census (run 1's figures, where run 1's parts sum to its `d` of 34.4 exactly). Every figure is ms a frame. A share is a design [D] until a cut's sheet and its PSP pair back it.

### 3.1 The budget: 11 ms on b30's fight, checked on c20's peak [D]

`rest` is end, begin, setup and unnamed (b30 0.5, c20 whole 0.6). "Worst" is the part's highest figure in §2.2's table.

| Part | b30 fight | Share in proportion to b30 | Its worst, where | Cut it needs there | Share at an equal cut on each part's worst |
|---|---|---|---|---|---|
| objects | 15.7 | 5.02 | 15.7, b30 fight | 68% | **4.61** |
| world | 11.5 | 3.68 | 11.5, b30 fight (c20 10.2) | 68% | **3.38** |
| effects | 3.2 | 1.02 | 3.7, c20 ~1619-2196 | 72% | **1.09** |
| hud | 2.4 | 0.77 | 3.1, c20 ~1619-2196 | 75% | **0.91** |
| transparent | 0.8 | 0.26 | 1.9, c20 both lines | 86% | **0.56** |
| flares | 0.3 | 0.10 | 0.93, c20 whole | 89% | **0.27** |
| rest | 0.5 | 0.15 | 0.6, c20 whole | 75% | **0.18** |
| sum | 34.4 | 11.00 | 37.4 | | **11.00** |

- **The proportional form doesn't hold on c20.** Sized on b30's fight alone (the brief's sizing case), four of its seven shares need a deeper cut on c20's later peak line than on b30. Transparent needs 86% and flares 89%, against 68% on b30, because they're larger on c20 (1.9 and 0.8) than on b30's fight.
- **The last column is the one I propose:** every part cut by the same 70.6% on its own worst window (11 / 37.4). It still sums to 11 on b30's fight. On c20's later line each part stays at or under its share, so the line's 28.2 also comes in under 11.
- **What the budget says [D]:** no part can be left as it is. Objects and world must give 11.1 and 8.1 ms of b30's fight, 19.2 of the 23.4 to cut. The four small parts together give ~4.2. The 0.5 ms bar a cut (the lever rules, decisions.md 2026-11-26 18:3x) is ~2% of what has to go. So this box is a rebuild of the drawing's two big parts, not a set of levers. That's the brief's own frame (REBUILD.md §3.1's "drawing's build 11"), and §4's first slice is taken from objects or world.

### 3.2 The cuts, part by part [D, in progress]

Each cut is written as a lever case: its ground (the stopped tries first, quoted from the lab's `toolbox/levercheck.py`), the line of `docs/cpu_cost_map.md` it attacks with that line's ceiling, and a cost sheet with three figures (cycles saved an item, cycles its bookkeeping adds an item, the items a frame). The cut is taken in this order: the work done at bake time, then every item's work made shorter, then a coarse group whose change its writer already knows, and only then a per-item skip or lookup at about 10x the check's cost.

| Part | Its grounds (levercheck) | Its cost-map line, with that line's ceiling | Case |
|---|---|---|---|
| objects | model-walk (40 levers, 16 stopped), lighting, code-layout | `cpu_cost_map.md:88`: drawing's reuse (RSO, LIGHTREUSE, `retainedloop`) is in, and "the rest of the per-part chain (`psp_model_draw`'s locks, pretouch and records, ~15% of drawing)" is next (ceiling: lever by lever). `:89`: slice 2's census ceiling is −23% of the drawing's I-misses [E], and DRAWPART3 was rejected at +0.9-2.7% [M]. `:32`: the ME's traffic costs render objects ~0.9 ms a frame | §3.2.1 |
| world | visibility, me-replay, lighting | `:94`: work moved to the ME. Visibility is 4.85 ms a frame on b30's flight [M], and B1's exact moves are ~5-8 ms a frame on b30's beach. Placement (§4) rules out a move without ME room shown at 30 fps (REBUILD.md:87) | after objects |
| effects | effects (49 levers, 14 stopped) | `:33`: effects' drawing is 4.1 ms a frame on b40, three-quarters of it work | after world |
| hud | hud-ui (17 levers, 3 stopped) | no line names it | after effects |
| transparent, flares, rest | to be looked up | no line names them | last |

#### 3.2.1 Objects: the per-part chain's locks [D]

The case is for the per-part chain's lookups. It is a slice of `objects`' 15.7 ms, not the whole of its 11.1 ms cut. Written by builder2 at 2026-11-27 00:25 from the code at this branch's 68cfdf36; the hook and its counter are the same at the census head c5ff3114.

- **The ground: stopped tries first** (the lab's `python3 -I toolbox/levercheck.py psp_model_draw RSO DRAWPART retainedloop`, then `DRAWPART3 MB`, 00:2x):
  - **DRAWPART3** (stopped, rejected as built 2026-11-04): recorded models drawn in two stages over runs of up to 32. On the PSP-3000 b30 pair it cost drawing +0.2 ms in the cinematic, +0.5 in flight and +1.2 on landing [M]. The cost map gives the reason (`:89`): "the code that still runs between parts stayed". Its `revive_if` asks for "the per-part chain itself under ~8 KB hot", among other things. This cut removes code from that chain and adds no stage.
  - **MB** (stopped): model parts drawn in stages, with each part's decisions gathered first. Its worth is ~45 k instruction misses a frame [E]. It was class B in the 2026-11-18 cache-model audit and never had a PSP pair.
  - **PDCCALL** (stopped): a part whose record matches last frame's is CALLed from a retained block. On c10's PSP-1000 pair the frame cost +1.2-2.3% [M]. This cut keeps every record as it is, so the ME's replay doesn't change.
  - **In and on by default:** RSO, which re-emits a still object's records (b30 flight drawing −0.6 ms [M]); DRAWPART2 with LIGHTFIRST (the walk in two stages; b30 PSP-1000 flight drawing −3.6 ms [M]); LIGHTREUSE; `retainedloop`; and PASSSMALL. This cut sits beneath all of them. It changes how a part that still walks the chain finds its geometry, not which parts walk it.
- **The cost-map line it attacks:** `cpu_cost_map.md:88`, "the rest of the per-part chain (`psp_model_draw`'s locks, pretouch and records, ~15% of drawing)". The line's ceiling is lever by lever. On §3.1's 34.4 ms of b30 fight drawing, 15% is ~5.2 ms [U: the line's 15% carries no build]. This case takes only the locks. Pretouch runs only while `PRETOUCHING()`, and the record is what the ME replays, so both stay.
- **What a part does today, from the code:**
  - The `rasterizer_model_draw` hook (`psp_fixups.py:351-600`) calls `psp_model_draw` (`psp_fixups.py:2695`).
  - `psp_vertex_lock` (`psp/src/psp_geo.c:1918`) does one `psp_geo_lock` for the baked block. That is a hash `find` (`psp_geo.c:369`), then the state and size test, then three stores. With SSPR at a level below the top it does two more: the 32-byte directory entry and the level's block. If the level isn't resident it does a third, locking the top again (`:1943-1967`).
  - `psp_index_lock` (`:2020`) does one more `find` when the indices are external.
  - `psp_ge_draw_model` (`psp/src/psp_ge.c:17370`) asks `psp_geo_baked`, which calls `resident_entry` (`psp_geo.c:1095`) for the same block. That usually comes back on the `last_locked` fast path.
  - So a part does 2-5 lookups and one `resident_entry` to resolve the same address to the same block it resolved last frame, for as long as the block stays resident.
- **The cut, in §3.2's order:**
  - The bake already gives each buffer its block's index (`buffer->pad`, `pcv_table`).
  - The coarse group whose change its writer knows: the geometry cache knows when it evicts or re-reads an entry, so it bumps a generation word each time it does.
  - A part's resolved pointers (block, level header, indices) are then kept per model key beside `retainedloop`'s kept calls. A part checks them with one compare against the generation word and goes straight to `psp_ge_draw_model_body`'s inputs.
  - It's exact by construction: the same pointers are drawn whenever the generation hasn't moved, and otherwise the chain runs as today. The emulator's GE lists prove it.
- **The cost sheet:**
  - **Saved an item [D, counted from the code, not timed]:** 2-5 hash finds, each ~25-40 instructions when hot, plus `resident_entry`'s fast path and the five functions' entry checks. That's ~60-200 cycles of the data path a part. On the instruction side, `psp_vertex_lock`, `psp_geo_lock`, `psp_index_lock`, `psp_geo_baked` and `resident_entry` leave the per-part chain. In `draw_rebuild.md:15`, `psp_geo_lock` and `resident_entry` were ~1.3 M instruction misses each on b30's route [E, b113, lab 41308914906, 2026-11-01, before DRAWPART2, RSO and `retainedloop`: a lead, to be read again on today's head].
  - **Added an item [D]:** one load of the kept entry (12-16 bytes, one data line, a miss when cold, against the 2-4 lines the finds read in `hash_head`, `hash_next` and `entries`), the generation compare and a branch: ~6-10 instructions. The cache's evict and re-read paths each gain one store.
  - **Items a frame: 39 on b30's fight, 59 on its opening [M, an exact count from the census's own PSP runs; a count prices nothing].** The build already counts them twice, so this case needs no new counter:
    - **The GE's count** (`model_part_calls++`, `psp_ge.c:17434`, every part that reaches `psp_ge_draw_model_body`) prints as `N part draws a frame` over the same 300 frames as each `drawsplit:` line (`GE: frame 600` / `frame 900`). In `drawcensus_b30`'s two B runs (sierra-hw 45943027708, run2 and run4) it reads 59 / 59 on the opening window (frames 301-600) and 39 / 39 on the fight window (frames 601-900, ticks ~1108-1732), the window where objects is 15.7 / 15.9 ms.
    - **The hook's count** (`psp_model_draw_us[2]++`, `psp_fixups.py:597`, every call into the game's part draw) prints as `model part draws: N` per profile window (`psp_platform.c:1124`; `notimers` zeroes its two times, not the count). Over the fight's three windows it reads 4328 + 2949 + 2859 and 4386 + 2949 + 2901 parts, from game time 1103 to 1640. Those windows hold 96 + 88 + 85 frames (the `frameend:` lines): 37.7 / 38.1 a frame, beside the GE's 39.
    - **`retainedloop`'s kept parts are in both counts [D]:** its kept calls are the same draw calls, made by `models.c` itself (`psp/src/psp_rloop.c:10-17`), so they still enter the hook. The two counts agree to ~3%, so nearly every hooked part reaches the GE.
- **What the sheet says at its count [D at the count]:** 39 parts × ~60-200 cycles is ~2,300-7,800 cycles a frame, 0.007-0.023 ms at 333 MHz, net of the ~6-10 added instructions a part. That is 21-72× short of the 0.5 ms bar, which needs ~830-2,800 parts a frame. To clear it on the instruction side, the chain would have to cost ~4,300 cycles a part (0.5 ms is 166,500 cycles over 39 parts). The only lead for that size is `draw_rebuild.md:15`'s ~1.3 M misses each [E, b113, before DRAWPART2, RSO and `retainedloop`], and it is too old to stand on. **So the cut is no lever on its own, and is not built.** It waits as one slice of §4's objects rebuild. Revive it if a map's window draws ≥ ~830 parts a frame, or if a read on today's head puts the chain at ≥ ~4,300 cycles a part.
- **What the count says about the objects part [U]:** 15.8 ms over 39 parts is ~400 µs a part, ~135,000 cycles. The geometry locks are a small share of that. Most of the objects part is spent outside the per-part chain: the object walk (the render list's peak is 245 of 256 objects a view, in the profile's `objects drawn:` line), posing and bone matrices (the GE's 17.6 KB of bone matrices a frame on the fight), or `psp_ge_draw_model_body`'s own work. Splitting objects (§1.3's level 2) is what names which, and an objects cut is sized on that split, not on parts.

### 3.3 What the sheets lack: the items a frame [D]

A sheet's third figure is the item count a frame "from task 2's census" (the brief, task 3). The census times parts and counts no items: no models, parts, effects, hud elements or transparent records a frame. Level 2 of the stamp set (§1.3) would split `objects` and `world` but still not count items. The counts are exact in the emulator, since a count isn't a price, so one emulator batch on b30 and c20 with a per-part item counter on `drawsplit`'s lines (log-only, as §1.3) fills every sheet's third figure [E]. The lab's lead rules whether it rides the stamp set before any code.

## 4. Placement and the build cut [D]

Not yet.
