# Visibility on the ME, slice 2: the answer ready when it's read

**Status:** built, both off by default: `mevispush` (FIXES_MEVISLAND in `psp_fixups.py`: `main.c`'s `main_game_render`, `render.c`'s `render_window`, the C at mevis's tail in `structure_visibility.c`) and `ringslotdone` (`psp/src/psp_me.c`, `psp_me_push`'s busy test). Exact in the emulator and priced on the PSP-3000 (section 8): b30's beach frame −2.6 ms. MEVISEARLY slice 2, lever MEVISPUSH, with RINGSLOTDONE beside it. It follows `mevis_landing.md` (the landing, the placement, "The rest's stale walk time") and `frame_timeline.md`'s B1 table, row 1.

Evidence labels: [M] measured on the PSP, [E] emulator, [D] read in code, [U] inference.

## What slice 1 taught

MEVISPROBE (the placement's rest decided on a fresh walk time) did what it was built for: on b30's beach the ME took 179 of 300 views against 37-97 [M, PSP-3000 pair, 2 runs a side, beach 2,700-3,700]. The frame didn't follow: 99.4 against 96.1 ms, inside the runs' spread. The drawing fell 0.6 ms; the main CPU waited 1.65 ms a view at the first reader (`lights_preprocess_scene`) against 0.23-0.67 off.

So a view on the ME pays only the walk it saves less the wait it adds. Today the span from the push to the first reader is about 3.0-3.5 ms of the main CPU's work: the ME's job takes 4.5-5.3 ms from push to done (`mevisring`, 3.4-4.4 ms of its own time plus ~0.9 ms before it starts), less the 1.7-2.25 ms waited [M, PSP-3000, the same pair]. The wait is what slice 2 removes.

**The tick's +1.7 ms isn't the ME's doing, as far as its counters show** [M, the same pair, 5-second lines with game time 2,900-3,700]:

| | on (run 1, run 3) | off (run 2, run 4) |
|---|---|---|
| Tick, ms a tick | 33.2, **35.1** | 32.7, 32.3 |
| The AI job's wait at the tick's start | 4 µs, 4 µs | 4 µs, 4 µs |
| Ring pushes refused, ME busy (a 5 s line) | 59, 56 | 56, 58 |
| The GE's busy time inside the ticks, ms a frame | 31.1, 29.1 | 31.2, 30.7 |
| The ME busy | 61%, 58% | 62%, 59% |

One run (run 3) carries the rise, and none of the paths by which the ME could slow the tick moved: the AI job is never waited for, the tick's ring jobs aren't refused more, and the GE reads no more inside the ticks. Read it as that run's play (its ticks took 71.0 ms a frame against 64.7-66.7) [U on M]. Slice 2's pair carries the same counters, so a tick rise past the floor with them flat would point to the bus instead.

## levercheck, and what's different

`projects/sierra/tools/levercheck.py --ground visibility` and `--ground me-scheduling` (the lab, 2026-11-05):
- **AIMEVISLEFT** (rejected): "stretch 17.9 -> 7.2-7.7 ms but frame +5.5% cinematic, +5.8% flight on the PSP-3000 (the visibility answer later)"; revive if "the visibility answer not needed early". The answer moved later cost the frame. Slice 2 moves the work the main CPU does before the answer is needed, never the answer.
- **MEVISPROBE** (shelved): "the main CPU waits on the ME's answer 1.65 ms a view against 0.23-0.67 off"; revive if "the ME's answer ready at the first reader without a wait (slice 2 ...)". This is that slice.
- **MEVGATHER** (idea): visibility's inputs gathered compactly for the ME, "-1 to -3 ms of the collect's 2.8-3.8 ms wait a view". Not this slice (below: it is the next one).
- **R2Y** (shelved): the sound copy job yielding to the ring every 4 KB took refusals to 0 but made the bunker +8.1% (drawing +5.8 ms). RINGSLOTDONE doesn't touch the copy's job: it only stops a copy that has already finished from refusing.
- **L1, MEB, MESCHED, HANDOFF** (stopped): none moved a job's main-CPU work into the span before its reader.

`cpu_cost_map.md` §4, **"Work moved to the ME"**: b30's beach main-CPU-bound, up to −17 ms a frame (~32-36 ms of ME room); B1's exact moves ~5-8 ms on the beach, ~2-3 on c20. This slice attacks B1 rows 1 (visibility's views) and 4 (the ring's refusals).

## 1. The push point: how much earlier, in time

Between the camera's last write and today's push, in the engine's order (engine `main.c` 3537-3546, `main_game_render` 3220-3300, `render.c` `render_frame` 590-640, `render_player_frame` 439-590, `render_window` 302-328) [D]:

1. `observer_update`: the camera final.
2. `game_engine_update_non_deterministic`, `render_interpolation_frame_begin` (interpolation is off by default on the PSP): ~0 on the beach.
3. `main_game_render`: `lock_global_random_seed`, **`sound_render`**, then the window loop (`observer_get_camera`, `set_window_camera_values`).
4. `render_frame`: `rasterizer_frame_begin`, `rasterizer_windows_begin`.
5. `render_player_frame`'s prelude: `structure_visibility_find_camera`, the fog (and the port's `drawdistN` cap), the far clip, two frustum builds, the mirror search.
6. `render_window`: the head set (`scene_index`, the camera, the frustum), then today's push.

Their cost on the beach, from the PC sampler's drawing phase (PSP-1000 lab sierra-hw 42000782600, game time 2,650-3,650; 11,741 drawing samples for 25.5 ms a frame, so a sample is ~2.2 µs a frame) [M]:

| Step | Samples | ms a frame |
|---|---|---|
| `sound_render`'s own functions (`sound_*`, `dsound_*`) | 178 | 0.39 |
| `sound_render` with its obstruction rays (B1 row 5; the profile step "sound_render: refresh and obstruction" 1.0 on b30's route, `review_audiofx.md`) | | **~0.9** |
| `rasterizer_frame_begin` | 57 | 0.12 |
| `render_player_frame` and `render_camera_build_frustum` (step 5) | 196 | 0.43 |
| step 2, `main_game_render`'s own, the fog lookup | ~10 | ~0.02 |

**The push needs step 5's answers** (the head the ME copies: the camera with its far clip, the frustum, the camera's leaf and cluster, the sky). So a push "at `observer_update`" must make step 5 first, and the time it gains is steps 3-4 only: **~1.0 ms, of which `sound_render` is ~0.9**. Against a wait of 1.65-2.4 ms a view (PSP-3000 1.70-2.25; PSP-1000 1.64-2.48 where the ME takes most views, 42000782600) [M], that hides about half of it: the wait becomes ~0.7-1.5 ms a view [U on M]. It isn't enough to hide the whole wait. Section 6 sizes what that's worth, and section 7 names what would hide the rest.

### Two ways to gain it

**A. Move `sound_render` into the span (recommended: MEVISPUSH).** `main_game_render` stops calling `sound_render` at its start and leaves it owed; `render_window` calls it right after `psp_mevisland_push` (whichever way the view is made), once a frame; `main_game_render` calls it after `render_frame` if no window took it (a film's or a screenshot's path). The push stays where it is, with no prediction, and the main CPU does sound_render's ~0.9 ms while the ME walks.
- It is exact if `sound_render` and steps 3b-6 (the window loop, the frame and windows begin, the prelude, the head) share no state [D]:
  - `sound_render` reads the listener from `observer_get_camera` (`sound_manager.c` 3837) and no `render` field (no `render.` in `sound_manager.c`);
  - steps 3b-6 start no sound and write only the windows, `render`'s head and the GE layer's state (no `render.` head read in `psp/src`; the port's prelude fixups, `drawdistN` among them, write only the window cameras and `render.fog`);
  - `sound_render` writes the sound pools and the mixer's command ring and casts obstruction rays, which only read the world. `lock_global_random_seed` and the collision log's period still hold over it.
- What moves is when the mixer's commands are queued: ~1 ms later in a 23 ms audio block. A sound may start a block later now and then [U]. Play doesn't hear it (statecrc covers no sound) and the frame's length already varies far more.
- Steps 4's `rasterizer_frame_begin` (0.12 ms) could follow, but it is a GE call in the record's order: left out.

**B. The early push with a predicted head (designed; not recommended now).** Push at `main_game_render`'s start (after step 2, so interpolation's camera is in), from a head made then:
- Save `render`'s head (`PSP_MV_HEAD_BYTES`, up to `visible_cluster_flags`), run window 0's camera (`set_window_camera_values` on a scratch window) and step 5's prelude on it (the same code, made a function by the fixup so the two can't drift), set the fields `render_frame` and `render_window` will (`frame_index + 1`, `time_delta_since_tick_sec`, `scene_index + 1`, `local_player_index`, `window_index`, the camera, the frustum), copy that into the record, then put the saved head back. `render` itself is untouched until the engine writes it.
- **The check at `render_window`:** after its own head set, `memcmp` of the record's head against `render`'s. Equal: the push is skipped and the ME's answer is collected as today. Different: that view goes today's way (the ME's early answer stays pending, so a mirror's view, which comes first and differs, doesn't throw away the main view's; `psp_mevisland_settle` takes any answer no view matched at the window's end).
- **What is final at the push, field by field** [D]: the camera (observer, final at step 1; with `interpolation` from `render_interpolation_camera`, which step 2 makes ready), the far clip (fog and `drawdistN`, from tags and the camera), the frustum (from those), the leaf, cluster and sky (`structure_visibility_find_camera`: the BSP and the camera; the last leaf is only its search hint), `frame_index`, `scene_index`, `time_delta_since_tick_sec` (the loop's elapsed time), `fog` (tags and the camera's cluster). Nothing is set only inside `render_window` that can't be made at the push: what `render_window` adds is its own copies of these.
- **Expected miss rate:** ~0 in campaign play [U]. A miss needs a second view before the main one (a mirror: `structure_visibility_find_mirror`, none seen on b30's route), more than one local player (not on the PSP), or a write to the head between the push and `render_window` that the [D] reading missed. `mevisearlycheck` would count misses and compare the ME's answer with one made at the window (as `mevislandcheck` does). Struct padding: the record's head is written by the same struct copies as `render`'s, so padding can't make a false miss.
- **Why not now:** it gains steps 3-4 (~1.0 ms) but makes step 5 twice (~0.25-0.4 ms more of the main CPU, on a main-CPU-bound stretch), so it nets ~0.1 ms more than A for more code and a check. It becomes worth it only if more work comes to sit between the camera and the window, or if hoisting step 5 (made once, earlier) is shown exact. That needs `rasterizer_frame_begin` and `rasterizer_windows_begin` to read nothing step 5 writes, and a write trace on the landing (`mewrites`).

**Gathering the inputs earlier (MEVGATHER) is exact** [D]: the objects' cluster lists, centres and scales are final at the frame's last tick. `game_frame` (particles, contrails, the sound manager), `director_update`, `observer_update` and the drawing up to the first reader move no object (job graph §5: visibility reads tick state, which holds still while no tick runs). The AI job running on the ME then writes actors, not objects. So a gather job may run from the last tick on, before the camera exists. The camera-dependent walk still waits for the push.

## 2. The ring: the refusals are the sound copies, not the replay

The handoff put the ring's refusals (~0.15-0.2 of the beach's views) on "a render slot job out at the push". The code says otherwise [D, `psp_me.c` 3233-3241, 2461-2465, 2107-2131; `sound_manager.c` 3810, 4000]:
- `psp_me_push` refuses while a non-audio slot job is submitted and doesn't yield (`slot_yields`). The replay (`ME_JOB_GE`) and the AI job (`ME_JOB_AIME`) both yield, so neither refuses. The one non-yielding slot job in a default build is the sound tier's copy (`ME_JOB_SOUNDCOPY` on `ME_SLOT_PROBE`).
- A copy counts as submitted until the main CPU collects it, and that happens only in `psp_sound_upload_poll` from `sound_cache_idle`, once a frame at `sound_render`'s end. The copy takes 0.38-0.39 ms of the ME (worst 0.64-0.68) [M], but the ring stays shut to every push for about a frame after it.
- **Measured:** across whole runs, ring refusals per 5-second line follow the copies per line at r = 0.97-0.98, **~4.6 refusals per copy** (PSP-3000 42006913219 runs 1-2: 1,500 and 1,397 refused against 321 and 312 copies; PSP-1000 42000782600: 1,765 against 389) [M]. That's every push of about one frame (~4.2 pushes a frame). On the beach that is ~1.1 refused ring jobs a frame: the perception rays and the poses inside the ticks, and the ~0.15-0.2 visibility views a frame.

**The fix where it runs (RINGSLOTDONE, keep word `ringslotdone`):** in `psp_me_push`'s busy test, a non-yielding slot job the ME has finished (`psp_me_submitted_done(k)`: its `done` word matches its request) no longer refuses. The ME runs nothing more for that slot, so a ring job behind it waits only on what is running (audio, a yielding job's look). A copy still running refuses as today, and that window is bounded by the copy itself (~0.4 ms, its own 250 ms bound behind it). Nothing in the copy's job changes, unlike R2Y.
- Exact: every ring job's code answers bit for bit on either unit (the rays, poses and points proofs; mevisland's). Only who runs it changes.
- Cost on the main CPU: one uncached load a non-yielding submitted slot a push.
- An earlier push doesn't meet these copies more often. A copy is submitted at `sound_render`'s end and collected at the next one, so the chance one is out is the same anywhere in the frame. Under MEVISPUSH, the push comes just before that frame's `sound_render` collects the last copy, so it meets the same ~0.2 a frame.

**The visibility job behind the AI job** needs no change. It is served at the AI job's drains (between actors; 1,218 visibility jobs had run inside its drains by game time ~3,500 in run 1, PSP-3000) and at its ~1 ms looks, and the AI job was never waited for at the tick's start (4 µs a job, worst 5-109 µs) [M]. The ~0.9 ms from push to start is mostly audio: an audio block holds the ME 5.5-5.8 ms and doesn't yield to the ring, at ~25% of the ME, so a push meets one about a quarter of the time with ~2.9 ms left [U on M]. All waits stay bounded: the collect at 50 ms (`PSP_ML_LATE_US`, then the main CPU makes the view), the copy at 250 ms, audio at 40 ms.

**The ME's room:** moving the beach's remaining ~0.6 views a frame to the ME adds ~2.5 ms of ME a frame (4.1 ms a view, `mevisring` PSP-1000) against ~36 ms of room with visibility counted [M, `frame_timeline.md`]. MEVISPROBE's own move left the ME's busy share where it was (61% and 58% on, 62% and 59% off).

## 3. The lights chain: not yet

`lights_preprocess_scene` costs the main CPU 0.7 ms a frame on the beach and 1.0 on c20 [M, B1 row 1]. Chained after visibility on the ME (~0.8 ms there [U]), it moves the first reader to `render_objects` and saves the main CPU its 0.7 ms. But while the main CPU still waits at the collect (~0.7-1.5 ms a view after MEVISPUSH), every ms of ME work chained in front of the collect is waited for: net ~−0.1 to +0.1 ms on the beach [U]. It pays only once the wait is gone with ~0.8 ms to spare: after the gather (section 7). It also allocates light datums from its pool (`object_lights.c` `datum_new(light_data)`), so on the ME it is a pool writer and needs the AI job's kind of hand-over (lines written out, the main CPU's lines dropped). Not in slice 2. On c20 the ME's room lies behind the replay, which the record already waits for: no.

## 4. Keep words

| Keep word | Default | What it does |
|---|---|---|
| `mevispush` | off until its pair | `sound_render` made after `psp_mevisland_push` in the frame's first `render_window`, not at `main_game_render`'s start (owed and made after `render_frame` if no window took it). A log line every 300 frames: frames whose sound ran after the push, and owed to the end |
| `ringslotdone` | off until its pair | `psp_me_push`'s busy test passes a non-yielding slot job the ME has finished. A line of its own beside the ring's (every 5 s): `ME: ringslotdone: N pushes passed a finished slot job not yet collected` |
| `mevisprobe` | on (slice 1's branch) | the rest weighed on a fresh walk time. Still needed: at a wait of ~1 ms, a stale landing walk time (1.4-2.6 ms) would still call for rests |

The code is in `psp_fixups.py` (FIXES_MEVISLAND: `main.c`'s `main_game_render` and `render.c`'s `render_window`) and `psp/src/psp_me.c` (the busy test), with the defaults in `psp_platform.c`. The build bases on recon/mevisearly 93e6ae3e (tock162 a22aed6f + MEVISPROBE). It touches hot code only in `psp_me_push` (one test a push). It doesn't touch the boot, but `main_game_render` is the main menu's path too, so the proof has a front-door run.

## 5. Proof

**Exactness, in the emulator** (one `sierra-batch`; the emulator runs a ring job inside its push, so it proves exactness, not time):
- b30's route to 4,500, c20 hands off to 3,600, d20 flashlight to 2,700 (mve2's set), sides `- | - | mevispush ringslotdone`: the off/off2 control and the slice. `nomouth` on every side.
- Play and both seeds identical every tick between off and the slice.
- Pictures: framecrc differences off-vs-slice within off-vs-off2 on the same host (the default's own pictures don't repeat: b30 ~424 frames in mve2).
- `mevislandcheck` beside it on b30 (the ME's answers equal the main CPU's, 0 differing).
- The counters: `ringslotdone`'s refusals near 0 while copies run; `mevispush`'s line says the sound ran after the push in every frame that had a player window.
- A front-door run: `map = "none"` into the menu, then a level and back (`m6menu2.toml`'s hop).

**Price: one PSP pair** (the only hardware ask). b30's timed route to the beach (`exitat125`, wall as 42006913219), keep `nearauto gebusy gebusy2 assettrack geo15 nomouth hwprofile hwthreads frametrace fixedseed mevisring`, **`keepsets="mevisprobeoff | mevispush ringslotdone"`, `repeat=2`**: 4 runs. That's today's default against the slice on top of slice 1; 42006913219 already has slice 1 alone. Either PSP; the PSP-1000 has the tighter floor. Read with `tools/abreport.py <folder> --nofloor`, beach 2,700-3,700 first, against the floor (0.6 ms frame on the PSP-1000, 0.5 on the PSP-3000). Per side: `mevisland: 300 views` (used, refused, the wait), `ME: ring` refusals, the AI job's wait, the GE inside the ticks, the ME busy. It pays when the frame is faster past the floor with the ME still under the main CPU. No c20 pair: section 6 puts c20 under its floor.

## 6. Sized ceiling [U on M]

**b30's beach** (main-CPU-bound; per frame, ~1 view):
- Today (default): visibility costs the main CPU 2.2 ms a frame in the drawing (B1 row 1: the ~0.63 views made here plus the collect), plus 0.37 views × ~0.6 ms waited: **~2.4 ms**.
- After MEVISPUSH + RINGSLOTDONE (with MEVISPROBE): ~0.95 views on the ME waiting ~0.7-1.5 ms each (1.65-2.4 less ~0.9), ~0.05 made here (~0.2), the copy-back (~0.1): **~1.0-1.8 ms**.
- **Gain ~0.6-1.4 ms a frame, central ~1.0**, at the floor's edge on the low side (PSP-1000 0.6, PSP-3000 0.5). Cross-check: MEVISPROBE's drawing −0.6, plus ~0.9 ms off the wait for ~0.6 views (~0.5), plus the refused views moved (~0.17 × ~2.5 ≈ 0.4), gives ~1.5 at the top.
- **RINGSLOTDONE's tick share is its own:** the ~0.9 refused rays and poses a frame inside the ticks go back to the ME, **~0.6-0.9 ms a frame** on the beach (B1 row 4). Together, ~1.2-2.3 ms.
- The ME takes ~+2.5 ms a frame, inside its room.

**c20:** ~0-0.3 ms [U]. Its views already wait ~0 on the ME. What it makes on the main CPU is the placement's rest at the floor (the walk ~2.0 ms, visibility 0.7 ms a frame), and its refusals (~0.2 a frame) meet a ME that the record already waits for (B1 row 4: ~0). That's under the floor, so it's proven only in the emulator.

**The §4 line** ("Work moved to the ME"): of B1's ~5-8 ms of exact moves on the beach, this slice is rows 1 and 4, ~1.2-2.3 ms of B1's ceiling for them (row 1 ≤ 2.0-2.9, row 4 0.6-0.9).

## 7. What would hide the rest of the wait

- **The gather (MEVGATHER, slice 3):** visibility's inputs copied compactly after the last tick, exact (section 1), as a ring job before the camera exists. mevis reads 45-93 KB a call, scattered against the ME's 16 KB cache (`mevis_landing.md`). A walk shorter by 1-3 ms closes most of the ~0.7-1.5 ms left and opens the lights chain. Needs an eDRAM or RAM row (`edram.md`) and a removal count (misses of the walk, the cache model) before it is built.
- **Audio's hold on the ring** (~0.7 of the ~0.9 ms before the job starts [U]): a ring look inside the mix is audio's deadline to spend, not this slice's.
- Recording passes before the collect that don't need visibility (the HUD, decals: ~0.6 ms) changes the draw order, so it is not exact (`mevis_landing.md`).

## 8. Proofs and price

**Exactness [E]** (the emulator, `mevispush ringslotdone` against off with an off2 control, non-strict, bake w157m):
- Play and both seeds the same at every tick: b30's route to 4,500, c20 to 3,600, d20's swamp with the flashlight to 2,700; and on b30 with `mevislandcheck` too (0 differing clusters or objects in every 300-view line).
- Pictures: b30 differs at 508 frames against off/off2's 503, c20 at 5 against 4.
- The whole-state hash's objects differ from tick ~89 with the slice (off/off2 from ~3,180): that is what the drawing writes into objects, which the play hash leaves out. d20's actor and looping-sound allocations differ once each, read as the sound's timing [U].
- `mevispush`: the sound ran after the push in 300 of 300 frames in every line, the main menu included. The front door went from the menu into a10 clean.
- `ringslotdone`: refusals for a busy ME 1,909 -> 0 on b30, 612 -> 0 on c20, 1,185 -> 0 on d20, with ring jobs 6,454 -> 7,963, 4,792 -> 5,326, 3,563 -> 4,489. The emulator runs a copy inside its submit, so every copy has finished when the next push comes; on the PSP a copy still running refuses, for its ~0.4 ms.

**Price [M]** (the PSP pair of section 5, on the PSP-3000: b30's timed route, `mevisprobeoff | mevispush ringslotdone`, 2 runs a side, beach 2,700-3,700):

| | today's default (mevisprobeoff) | the slice (with MEVISPROBE) |
|---|---|---|
| Frame by the clock, ms | 95.7 (96.4, 95.1) | **93.1** (91.6, 94.6) |
| Drawing, ms | 24.8 (25.2, 24.4) | **21.6** (22.1, 21.1) |
| Tick, ms | 32.4 (32.5, 32.3) | 33.0 (32.0, 34.0) |
| Views the ME's answer used, of 600 | 324, 327 | 584, 584 |
| Made here: ring refusals / placement | 79, 67 / 197, 206 | 0, 0 / 16, 16 |
| The wait at the first reader, ms a view | 1.39, 1.50 | 0.81, 0.78 |
| Ring jobs / refused for a busy ME | 1,432, 1,466 / 493, 489 | 1,944, 2,101 / 2, 0 (614-650 passed a finished copy) |
| The ME busy | 61%, 60% | 66%, 63% |
| The GE inside the ticks, ms a frame | 30.4, 30.3 | 30.2, 30.6 |

- The frame is 2.6 ms faster (−2.7%), past the PSP-3000's 0.5 ms floor, and the sides' runs don't overlap. abreport's own spread test calls it noise because the slice's two runs differ by 3.0 ms. The drawing's −3.2 ms is real by any test. The flight (300-1,800) is the same: frame −0.4 ms, inside the floor.
- The wait fell to ~0.8 ms a view, inside section 6's 0.7-1.5, and the ME took 97% of views against 54%. The ring's refusals went to ~0. The ME stays under the main CPU (63-66% busy).
- The tick's +0.6 ms is one run (run 4: 34.0 against 32.0-32.5), as slice 1's +1.7 was (its run 3). The GE inside the ticks didn't move. It is worth a look on the PSP-1000 before the default flips: two slices each with one slow-tick run.
