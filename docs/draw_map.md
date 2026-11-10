# The drawing map: the main CPU's drawing by part (2026-11-09, master)

The drawing side's counterpart to `tick_map.md` (Sheepdog01 18:16: two maps, one budget, measure before ranking, rank by all three units). Why it's needed: `direction_check_2026-11-09.md`. The main CPU's drawing is on the critical path of both goals, and the plan's figure for it (~10 ms a frame) rests on R's design, not on a measurement.

## §0 The split on the PSP [M]

**One run:** lab sierra-hw 42728051032, on the PSP-1000.
- **Build:** Sierra-III rslice1 e3d2113 in the PSP setup (LTO, profile campaign-w162rh5, layouts w162rh5_tgc90 / hotg162rh5).
- **What was on:** R's lean path, retainedmemo, retainedringkb128 and retainedbypass.
- **What it played:** b30's pad bench (`b30_padrec2`).
- **The sampler:** `pcsample` with snapshots at each stretch's ends.
- **Clean run:** 0 heap requests failed, and every kept part was written in one take.

The samples are named by `psp/sierra/pcsample.py`, using the symbols of sierra-build 42717338740 (the same EBOOT, and its bins match). Functions are grouped by name: share of the drawing phase, then ms a frame at this run's drawing time.

**What the samples count:**
- Every thread's samples during the drawing phase (sound included). Samples in the kernel are left out.
- The bins are 512 bytes, so a little of the tick's code shows here too.
- These are shares within one run, read as the lab's rules say.

| Part of the main CPU's drawing | cinematic (19.9 ms) | flight (42.2 ms) | landing (53.0 ms) | beach (23.6 ms) |
|---|---:|---:|---:|---:|
| R's per-part chain (kept parts) | 13% (2.6) | 19% (8.0) | 19% (10.2) | 9% (2.2) |
| the engine's model walk | 14% (2.8) | 19% (7.8) | 19% (10.3) | 8% (1.9) |
| object lighting | 2% (0.4) | 3% (1.3) | 3% (1.8) | 2% (0.6) |
| the level (BSP surfaces, lightmaps, fog) | 7% (1.3) | 6% (2.7) | 7% (3.7) | 7% (1.6) |
| visibility and culling | 3% (0.5) | 3% (1.5) | 3% (1.6) | 3% (0.6) |
| sprites, particles, decals, effects | 2% (0.4) | 4% (1.5) | 3% (1.7) | 7% (1.6) |
| HUD and screen | 3% (0.5) | 2% (1.0) | 3% (1.4) | 4% (0.9) |
| collision (camera, flares, decals) | 9% (1.8) | 3% (1.1) | 2% (1.1) | 9% (2.2) |
| memcpy and memset | 2% (0.5) | 3% (1.2) | 4% (1.9) | 4% (1.1) |
| vector maths | 3% (0.6) | 2% (0.9) | 2% (0.9) | 2% (0.4) |
| tags | 1% (0.2) | 1% (0.5) | 1% (0.5) | 1% (0.3) |
| other threads (sound, ME ring) | 2% (0.4) | 2% (0.9) | 3% (1.4) | 4% (0.9) |
| the frame's own (step marks, frame end) | 4% (0.8) | 3% (1.1) | 2% (1.1) | 2% (0.5) |
| tick code reached from drawing | 2% (0.4) | 3% (1.1) | 3% (1.3) | 2% (0.5) |
| the rest | 31% (6.1) | 25% (10.4) | 24% (12.5) | 32% (7.6) |

**"The rest" is a long tail:** 1,585 functions, none over 0.6% on the flight (`find`, `psp_transparent_map`, `render_player_frame`, `shader_texture_animation_evaluate`, `powf` ...).

## §1 What it says

1. **The models are the one large piece.** R's per-part chain, the engine's model walk and object lighting together take:
   - ~41% of the drawing on the flight and the landing: 17.1 and 22.3 ms a frame;
   - 29% and 19% on the cinematic and the beach.
2. **That is more than slice 5's ceiling.** Slice 5's ~7.5 ms came from the 11-07 sampler's per-part chain alone (`redesign_r_slice5.md`). With the bypass on, the kept parts still pay the geometry and vertex locks, `resident_entry`, the texture pretouches, the colours and the kept record (~8 ms on the flight). The walk around them (`render_model`, `rasterizer_model_draw` for the model-level work and the parts not kept, LOD by pixels, the object list) takes ~7.8 ms more.
   - **A retained model path that removes both has a ceiling of ~15-17 ms on the flight and ~20 ms on the landing** [U on M]. The model path would be held as GE command lists checked by stamp, and the walk's choices (LOD, permutations, visible parts) carried from frame to frame. That is R's original design (`redesign_r.md`, the instance as GE words), extended to the walk.
3. **Everything else is spread thin.** No other part is over ~3.7 ms on any stretch:
   - the level 2.7-3.7;
   - visibility ~1.5;
   - sprites and effects 1.5 (the beach 1.6);
   - collision 1.1-2.2 (the most on the cinematic and the beach);
   - memcpy 1.2-1.9;
   - the HUD ~1;
   - "the rest" 10-12 ms over 1,585 functions.

   **So even a perfect model path leaves the flight's drawing at ~25-27 ms, not ~10.**
4. **The tail is a code working-set question, not a lever list** [U]. On the tick the instruction-miss stalls are ~10 ms of 40 (`redesign_tick_target.md` §1). A drawing that runs through ~1,600 functions a frame is likely bound the same way. Two things would test it, both on the PSP:
   - the hardware's instruction-miss counter over the drawing phase alone;
   - a layout pass sized for the drawing phase (the tick's layout work, §3.9, was for the tick).

## §2 Open, in order

1. **R whole against the old path: priced** (lab sierra-hw 42729345783, PSP-1000, one build, both sides with R's code) [M]. R takes from the main CPU's drawing:
   - −1.8 ms on the cinematic (−8.4%);
   - −2.9 on the flight (−6.5%);
   - −7.3 on the landing (−12.7%);
   - −1.6 on the beach (−6.4%).

   The tick rises 0.6-1.3 ms (+1.6% to +3.8%): R's code sits in the cache beside the tick's. The frame falls 0.6-4.8 ms. **The ME's replay is unchanged** (50.1 → 51.3 ms a job, ×1.02). `redesign_plan.md` §1a carried R at the replay ×0.57 and the main CPU's drawing at ~10 ms, so its after-R columns are design figures and stand corrected by these.
2. **The drawing phase's instruction and data misses on the PSP** (the hardware counters across the drawing phase, one run). This says whether the tail is misses or work.
3. **Size the retained model path's design**: the instance as GE words, plus the walk carried by stamp. Weigh its ceiling (§1.2) on all three units: the main CPU above, the ME's replay, and the GE's lists.
4. Each part's section, as `tick_map.md` does for the tick: what it computes and who reads it, every lever tried on it with where it was priced, where else it might gain, and its algorithm.
