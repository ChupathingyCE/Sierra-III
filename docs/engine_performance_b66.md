# Engine performance, continued: build 66 (overnight 2026-10-04)

Continues `engine_performance.md` (sections 0–10). Labels: measured (hardware or emulator, named), verified (code), estimated.


## 1. Frame-time work, measured against Sheepdog01's b64 run

**Where the b64 frame went on the PSP** (profile.txt window 3, on foot at the base, 21.5 fps = 46.5 ms a frame) [measured]:

| Step | ms a frame | Note |
|---|---:|---|
| `structure_render_lightmaps` (level draws, game side + GE) | 6.95 | my renderer's part 3.2–4.8 ms |
| `objects_update` (tick) | 5.58 | |
| `rasterizer_model_draw` | 4.26 | ~22 parts |
| `main_rasterizer_throttle` | **3.91** | **a sleep, see below** |
| HUD (`hud_render_weapon_interface` + `unit_interface`) | 4.88 | nearly the level's cost |
| `structure_visibility_compute` | 2.52 | the portal walk |
| `rasterizer_decals_draw` | 2.04 | |
| shadows | 1.33 | |

The `budget:` line in the same run: ticks 4.7 ms + frame 19.5 ms + "the rest" 9.6 ms = 33.8 ms (29.6 fps) in the good stretches; 51 ms (19.5 fps) with the warthog. The GE waited for never more than 0.3 ms: the CPU is the whole budget.

**Build 66 changes (every one exact; keep words revert):**

1. **Halo's vertical-blank throttle is off** (`vblankthrottle` restores it). The Xbox game paces frames to whole vblank intervals and, after four missed targets, steps the interval down (2 → 3 → 4: 30 → 20 → 15 fps) and then *sleeps* to pad every frame out to it. At 21 fps that was 3.9 ms a frame of sleep. Without it the millisecond branch caps at 30 fps and never waits when slower. [cause: code; cost: measured]
2. **A level draw whose vertex box is wholly inside the view skips classification** (`noboxtest`). At the vantage point 15 of 17 draws skip it. [emulator: level draws 4.0 → 3.5 ms; the PSP figure will be larger, the loop is memory-bound]
3. **Detail passes stop at the fade** (`detailallfar`). Triangles whose every corner is past DETAIL_FAR (60 units) are sorted into "far" buckets drawn by the base and lightmap passes only; a draw with nothing nearer skips both detail passes. The detail maps fade to neutral grey at the fog's far end, so nothing changes on screen (pixel-identical at the vantage point outside the weapon bob). Vantage: 1,906 of 3,426 triangles a frame are far; detail passes 13,712 → 5,492 per 300 frames.
4. **Cut corners made once a draw** instead of once a pass (four times).
5. **Model palettes cached** (the change-colour and self-illumination passes rebuilt a 256-entry palette with three divisions an entry every part every frame): 8 worked out, 4,792 reused in a 30 s run.
6. **`verify_tag_reference` answers from the reference.** It looked every reference up by name (`tag_loaded`: a case-insensitive string compare against every tag of the group) to assert the index matched, then returned the lookup's result. The crosshairs called it per item per frame. The HUD's emulator time fell from 1.75 to 0.93 ms a frame; `tag_loaded` calls a frame went from many to 0 (`budget: tag lookups by name`). `verifytags` restores the lookup.
7. **The inline tag block accessor for the whole game** (`-DPSP_FAST_TAG_BLOCKS=1`, build 46's for the collision files). 1,352 uses; +56 KB of code.
8. The b64 trial-split counters are gone (the answer: 4–6% of level triangles cross a 4- or 12-unit split, so a near/far depth split stays affordable if nearauto fails).

**Not done, measured for next time:**
- **The portal walk** is 3.1 ms a frame in the emulator at the vantage point (58 steps a walk, 80% hull-cache hits), 2.5 ms on the PSP on foot. Per step: the cluster's portals, each clipped against the visible region (`convex_hull2d_intersect`). This is the renderer's largest remaining game-side item and the best Media Engine job: read-only on tags, a small answer (rendered clusters and clip bounds), and the CPU has other render preparation to do meanwhile.
- The HUD's remaining 0.9 ms (emulator) is spread over Bungie's generic element code (crosshairs 0.24, shield/health meters 0.16, grenades 0.14).
- Level classification itself (0.8–1.6 ms PSP) still waits for the VFPU or scratchpad version where draws cross the near plane.

**A proposal: a sampling profiler.** Every figure above comes from timers around steps, which cost 0.6 µs each and only see what was labelled. The `halo_profile.prx` kernel module could register a timer interrupt (say 1 kHz) whose handler records the interrupted thread's program counter (`EPC`) into a ring in RAM; a 20 s run gives 20,000 samples, mapped to functions with `psp-addr2line` afterwards. That would show, with no labels and no guesswork, where the PSP's 38–50 ms a frame goes, cache stalls included. Effort: a day; worth it before any further tuning.

**Emulator figures to read with care:** PPSSPP's JIT runs the CPU work 3–4× faster than the PSP and models no cache, so a change that saves memory traffic (the box test, the tag accessor) shows less there than on hardware, and one that saves instructions (the palette cache, verify) shows about the same.
