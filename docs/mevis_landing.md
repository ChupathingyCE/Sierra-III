# Visibility on the Media Engine: landing mevis (option C's step 3, re-scoped)

**Status:** a design. Nothing is built yet beyond mevis, which already runs the frame's visibility a second time on the ME as a measurement: timed and checked, while the frame still draws from the main CPU's own answer.

Evidence labels: [M] measured on the PSP, [E] emulator, [D] read in code, [U] inference.

## Why step 3 changed

Option C had the ME own the whole drawing walk. Step 3 was its snapshot: the objects' render state and node matrices, lights and effects, frozen each frame so the walk could run beside the next tick.

The hardware survey (`hardware_utilization.md`, section 5) re-weighed C with the VFPU:
- The model walk holds every VFPU use in the drawing.
- On the ME, the model walk's node matrices would take 2.5–5 ms instead of 1.3–1.9 [M].
- So the model walk stays on the main CPU. The ME takes visibility, the level pass and the small passes (B plus mevis).

Without full C there is no walk to snapshot. What's left of step 3 is the narrower question: **what does visibility need to run on the ME without the main CPU waiting for it?** That's ranked #1 in the survey: 8.0–8.8 ms of b30's main-CPU frame [M]. On the ME it takes 0.88–0.93× that, it is exact, and it was never late (mevis [M]).

## What visibility reads [D]

From the engine at the pin (d98982b). `structure_visibility.c` finds the clusters; `render_objects.c`'s `find_rendered_objects` finds the objects:

| Input | Where it lives | Who writes it, and when |
|---|---|---|
| The camera and frustum (`render.camera`, `render.frustum`) | `render` globals (mevis copies its head into the job's record) | `render_window`, just before `structure_visibility_compute` |
| The BSP: clusters, portals, the PVS | tag data | nobody (static per map) |
| Each cluster's objects: two reference lists (`collideable_object_cluster_partition`, `noncollideable_...`) | data arrays | **the tick, every tick.** Every moving object leaves its clusters and joins them again each tick (the port's own comment in `cluster_partitions.c`) |
| Each object's sphere: `bounding_sphere_center`, `scale` | the object's datum | the tick |
| The sphere's radius: `render_bounding_radius` | the object's definition (tag) | nobody |
| `object_get` (index to datum) | the object header array | the tick (objects made and deleted) |
| The marks (`object_unmarked_function`, `object_mark_function`) | walkapart's own array, apart from the tick's | the drawing |

So visibility reads tick state (the cluster lists and the spheres). That state holds still only while no tick runs.

## Where the ME's visibility can overlap the main CPU

**The engine's own order** (`render_window`, render.c 302–338) [D]:
1. The camera and frustum set (`render.camera = *camera`).
2. `structure_visibility_compute()`: the clusters.
3. `rasterizer_window_begin()`: in the port, `psp_ge_window_begin`. This is the wait: the frame out with the ME, the GE's show, the vblank, the VRAM preload.
4. The sky, the first-person weapon's update, `lights_preprocess_scene` (its own `structure_visibility_find_objects` over the lights).
5. `render_objects()`, which starts with `find_rendered_objects`.

**The exact landing, with no snapshot:**
- Push visibility to the ME at step 2: the clusters and the objects in one job, as mevis does now.
- The main CPU skips its own copy, goes on through step 3's wait, and collects the ME's answer at the first reader:
  - `lights_preprocess_scene` needs the rendered clusters;
  - `find_rendered_objects` takes the ME's object list.
- No tick runs between the push and the collect. The main CPU's work in that span (the window's start, the sky, the first-person update) writes none of the inputs in the table. So **the inputs are frozen by the frame's own order, and the answer is the main CPU's, bit for bit.**
- The time it frees is the overlap: `min(the ME's visibility, the main CPU's work and waits from the push to the first reader)`.
  - The window-begin wait is 2.4–6.7 ms a frame on b30 (the per-pass table, syncclock 40608347188 [M]).
  - The "frame start" part is 3.9–14.7 ms on c20 and a10 (framesplit, 39798793709 and 39799091151 [M]).
  - Against visibility's 5.4–7.3 ms on the ME, that's **~2.4–6.7 ms a frame back to the main CPU on b30** [U from M].
- **The catch** is what that wait waits on.
  - The part that waits for the GE or the vblank is free time: the ME can run visibility then.
  - The part that waits for the ME's replay of the last frame (`inflight_sync`) isn't. Visibility would queue behind the replay, or slow it through `psp_me_yield`, and the main CPU would wait as long as before.
  - The b30 figure doesn't split the two. That's the one measurement to make before building (below).

**Stronger overlaps, and why they wait:**
- **With the next tick.** Visibility for frame N would run while tick N+1 does. Its inputs then need a frozen copy.
  - The copy would be the two partitions' reference arrays (contiguous, so a copy or a DMA) and each object's center and scaled radius, gathered into one array: **~20–44 KB double-buffered** [U: ~500–1,000 references at ~20 bytes].
  - That copy would also shrink the ME's working set. mevis reads 45–93 KB a call, scattered over object datums, against the ME's 16 KB cache (the hardware session's WM finding [M]); the gathered spheres sit in a few KB. So a gather may pay on its own once visibility is on the ME.
  - But the walk of frame N would then run after tick N+1 and draw N+1's state with N's visibility. That isn't exact: the camera has moved a tick, so clusters can be missed when turning. It is a compromise: catalogued (levers.tsv, kind `compromise`), not built.
- **The passes that don't need visibility, recorded first** (HUD, decals, the first-person weapon): about 0.6 ms [M]. They change the draw order, so this wouldn't be exact. Not worth it.

## Step 1's result: b30 [M]

`visspan` on the PSP-1000, b30's route, two runs (sierra-hw 40829107756, commit 528462d3, `PSP_RECON=1`). The six clean 150-frame windows past the load in each run (the other windows read a wrapped counter, fixed in 927aeef) give, in ms a frame:

| | Run 1 | Run 2 |
|---|---|---|
| The span, visibility to its first reader | 8.45 | 8.31 |
| Visibility | 5.56 | 5.38 |
| Waiting on the ME's frame out | 0.01 | 0.01 |
| Waiting on the show (the forwarding thread) | 1.46 | 1.51 |
| Waiting on the GE's end (this thread) | 0.00 | 0.00 |
| The rest: work | 1.41 | 1.41 |

- **The catch isn't there.** Under geshow3 the window-begin wait is no longer the ME's replay: 0.01 ms a frame.
- **The overlap** is the show wait plus the work, 1.5–5.0 ms a window; `min(visibility on the ME, that)` averages **2.9 ms a frame** in both runs. That clears step 2's 2 ms. The show wait alone (1.5) doesn't: the work in the span carries the rest.
- **The ME has room:** 56–57% busy (median of the 1-second lines, 68–69 of them a run).
- **Not yet measured:** c20, hands off. The pair on b30's route is the proof either way.

So step 2 goes ahead: build `mevisland`.

**The ME's side is slower than the doc assumed.** mevis measured the ME's visibility at 5.4–7.3 ms a frame on b30 [M], against the main CPU's 5.5 here. So the span becomes about the ME's time plus the copy-in, not the overlap alone: 8.4 → ~6.6 ms, **~1.8 ms a frame** [U from M], with 2.9 the ceiling if the ME were as fast as the main CPU. The pair decides.

## Step 2: built (`PSP_MEVISLAND=1`, keep word `mevisland`)

- **The push** (`psp_mevisland_push`, in place of `structure_visibility_compute` in `render_window`): mevis's record and job. The job's output is the copies' whole state block, so the ME writes back its cache when it ends.
- **The collect** (`psp_mevisland_collect`, before `lights_preprocess_scene`): it waits for the answer, at most 50 ms. It then copies in exactly what `structure_visibility_compute` writes: the visible clusters' bits, the rendered clusters, the surfaces' bits and count, and the PVS-fallback flag. Nothing else reads the walk's other state (the hull cache is per walk; `rendered_cluster_indices` is read only inside a walk).
- **The objects** (`psp_mevisland_objects`, in `find_rendered_objects`): the ME's list. The marks are made again on the main CPU, at the same point in the frame. An object is marked only as it is listed, so the objects' marks and the marker count are the main CPU's.
- **The safety net:** a refused push, an ME still on an earlier answer, a late answer, or a failed job all have the main CPU make its own visibility there. The window's end (`psp_mevisland_settle`) collects a view that never reached its first reader (a film), so no tick runs with an answer outstanding.
- **`mevislandcheck`:** the main CPU makes its own too, and a line every 300 views counts the differences.
- **Its memory:** mevis's copies took 144 KB of state and a 160 KB stack from the heap. In the emulator that pushed the level's material copies into the level carve and shrank the texture cache. Play stayed exact (mevl1 40834378117: five maps, every tick), but c20's pictures differed at 591 of 899 frames, and the main CPU's own walk run beside it (`mevislandcheck`) differed the same way (mevl3 40835407293), so the cause was the memory, not the answer. The state now ends before the render globals' unused surface index list (64 KB less). The walk is capped at 14 clusters deep (the deepest measured is 12) on a 72 KB stack (`PSP_MV_DEPTH`, `PSP_MV_STACK`). A deeper walk, or one that ran past its stack (checked with a canary), has the main CPU make that frame's visibility.
- **Proof:** exactness in the emulator (play and seeds every tick, `framecrc`, five maps, off vs on). Then `mevislandcheck` with the cache model (`mecohere`) on b30 and c20. Then the PSP-3000 pair, `- | mevisland`, in one `PSP_MEVISLAND=1` build.

## Step 2's result: b30 on the PSP-3000 [M]

sierra-hw 40845560977 (329e167, `PSP_MEVISLAND=1`, b30's route to game time 90 s, `- | mevisland`, 2 runs a side, no PGO):

| Stretch | Frame by the clock | Drawing | Tick |
|---|---|---|---|
| cinematic (150–570) | -1.5 ms (-1.2%), noise | **-0.6 ms (-2.3%)** | noise |
| flight (300–2000) | **-2.1 ms (-1.3%)** | **-2.2 ms (-4.7%)** | noise |
| landing (2000–2700) | **-4.6 ms (-2.8%)** | **-4.4 ms (-7.6%)** | noise |

- The ME's answer was used at every view (none refused, late, cut or failed; the deepest walk 12 clusters).
- **The main CPU still waits 2.8–3.8 ms a view at the first reader** (at most 7–20 ms). That's the ME's visibility outlasting the span. The next gain is the ME's own speed: its working set (mevis reads 45–93 KB a call against a 16 KB cache) and the gathered-spheres copy.
- So `mevisland` is on by default, and built in unless `PSP_MEVISLAND=0`.

## Placement by measurement (after c10)

The hardware session's PGO pricing on the PSP-1000 (c10, hands off) has the drawing down 2.1–3.4 ms, but the tick up 0.8–1.5 ms. Its psp_mevisland_collect costs as much as the walk did (4,618 against 4,656 ms over a run): c10's span before the first reader is too short to hide the ME's walk, so the main CPU waits it out. That read compares two tocks with two profiles; a clean pair decides.

So where the walk runs is decided by measurement, view by view:
- Each view made on the main CPU is timed, and each wait for the ME's answer too (moving averages).
- When the wait comes within 0.5 ms of the walk itself, the next 240 views are made on the main CPU (which refreshes the walk's time), then the ME is tried again.
- After 600 pushes in a row, 16 views are made on the main CPU to keep the walk's time current.
- A walk under 1.5 ms stays on the main CPU: the ME's time isn't free (its own frame's replay waits behind the walk, and the bus is shared with the tick).
- `mevislandalways` pushes every view, the A/B's other side.

Both paths are exact, so the choice changes no answer.

**The rest's stale walk time (`mevisprobe`, on by default; off: `mevisprobeoff`).** The walk's time is refreshed only while views are made on the main CPU, so a check can weigh this stretch's wait against a walk timed in another. On b30's route (PSP-1000, two runs [M]) a 16-view refresh in the landing timed the walk at 1.4-2.6 ms; the next check rested 240 views on that, and the beach then walked 4.5 ms a view on the main CPU while the ME's answer waited only 2.4-3.5. Of the views the ME missed on the beach (it took 0.37 a frame), about four in five were that rest and one in five the ring's refusals (a render job out). c20 rested the same way once (a walk of 2.0 ms, the ME's answer waiting 0.0-0.1). With `mevisprobe` a check that calls for a rest first makes 16 views on the main CPU and rests the full 240 only if that fresh time still calls for it; otherwise the ME takes the views again at once.

## The steps

1. **Measure the wait's split.** Per frame on b30 (route) and c20 (hands off), split the main CPU's time from `structure_visibility_compute`'s start to `lights_preprocess_scene` into:
   - waiting for the ME's frame out;
   - waiting for the GE's show and the vblank;
   - work.

   The GE layer already times its waits (`show2_wait_us`, the frame-out wait in `inflight_wait_me`, `ge_wait_us`); what's new is one line a window that marks the span's start and end and adds those waits up inside it. **Two runs on the PSP-3000** (one A/B side is enough: this prices, it doesn't compare). The ME's slack in the same span comes from the ME's busy share (`psp_me_busy_percent`) over that window.
2. **If the GE and vblank part is ≥ 2 ms a frame:** land it behind keep word `mevisland`.
   - The push moves to `structure_visibility_compute`'s start; the main CPU's copy is left out.
   - The ME's clusters (`render` state, rendered clusters, surfaces) and object list are copied in at the collect.
   - The fallback, if the ME is stopped, refused or late (mevis's 100 ms bound), is the main CPU's own visibility, run then: the existing safety net, not the route.
   - Proof:
     - exactness in the emulator: the frame's cluster and object hashes equal mevis's check, and play and seeds every tick on five maps;
     - then one PSP-3000 pair on b30's route, `- | mevisland`.
3. **If the ME's own replay fills the wait:** the landing waits on B's level pass and the bus work (the bus census, `bus1`), which free the ME. The gathered-spheres copy is then worth pricing on its own: the ME's working set, on the PSP.
4. **Lights' visibility** (`lights_preprocess_scene`'s `structure_visibility_find_objects` over the light partitions) can join the same job later, under the same order argument.

## Open questions

- Whether `psp_ge_window_begin` writes anything visibility reads. Its writes are the GE layer's own [D: no object or partition writes found], but this needs checking with the emulator's write trace on the landing (mewrites, mevis's lines).
- The second caller of `render_window` (the mirror and the second view, render.c 509–572) runs visibility again with another camera. Landing covers the first view; the others stay on the main CPU until measured.
