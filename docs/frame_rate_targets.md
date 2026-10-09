# Frame-rate targets: 15, 20 or 30 fps (analysis b87, hardware results b87 run 3)

2026-10-06. What each display rate costs, and why the choice interacts with the game's fixed 30 Hz tick. Labels: [M] measured on hardware, [D] from the source, [C] computed, [E] estimated.

## Two clocks

- **The game's tick is fixed at 30 Hz** [D: `TICKS_PER_SECOND = 30`, cseries.h; 302 uses in the game code]. Physics, AI timers and animations advance in whole ticks. Changing the tick rate would mean finding every per-tick value in the code and in the tags. That isn't a tuning knob, so it is not considered here.
- **The display** shows a frame whenever the renderer finishes one. Before each frame the game runs as many ticks as real time demands, so a slow frame is followed by more ticks.

With interpolation off (the PSP default), a frame shows the world as of the last tick. The number of ticks between frames is therefore how far the world visibly jumps.

| Frame rate | Ticks a frame | Steps the eye sees |
|---|---|---|
| 30 fps, locked | 1, 1, 1 … | Even |
| 20 fps, locked | 1, 2, 1, 2 … (1.5 average) | **Uneven** (judder) unless interpolated [C] |
| 15 fps, locked | 2, 2, 2 … | Even |
| Unlocked ~16 fps (now) | Mostly 2, some 1 (1.86 average at blue base) | Uneven; b87's histogram counts it |

**So 20 fps needs interpolation to look smooth, and 15 fps doesn't** [C]. That is the likely mechanism behind the warthog's "donut bounce" (Sheepdog01, b80): uneven steps during fast turns. The b87 test is to compare the histogram line and the feel with and without the `interpolation` launch option.

## The budget at the blue-base spot (b86 run 2, standing) [M]

| Part | ms a frame |
|---|---:|
| Ticks: 1.86 × 4.5 ms | 8.5 |
| The frame (game render code + our GE renderer 17.5–19.1 + frame end) | 47.2 |
| The rest | 6.6 |
| **Total** | **62.3 (16 fps)** |

The tick cost per frame scales with the frame time: ticks per frame = 30 × frame time. So frame time = render side ÷ (1 − 0.03 × tick ms), with tick ms per tick and the frame time in ms [C]. With the render side at 53.8 ms (frame + rest) and 4.5 ms a tick:

| Target | Frame | Ticks cost | Render side must be | Cut needed |
|---|---:|---:|---:|---:|
| 15 fps locked | 66.7 ms | 9.0 | ≤ 57.7 | **none at this spot** (4 ms headroom) |
| 20 fps locked | 50.0 ms | 6.75 | ≤ 43.3 | **−10.5 ms (−20%)** |
| 30 fps locked | 33.3 ms | 4.5 | ≤ 28.8 | −25 ms (−46%) |

**Caveats:**
- This is a quiet multiplayer spot with no AI. Campaign ticks cost several times more (see `campaign_recon_b30.md`), which pushes every row toward a lower rate.
- Busier views (the cliff top) are CPU-heavier.

## Where 10 ms could come from (measured candidates)

- **Angle-bucket sort 3.8 ms, vertex classification 2.1 ms** [M, b86]: Media Engine candidates (`media_engine.md`), or a cheaper sort.
- **Level vertex traffic:** levelcompact halves the bytes the GE reads per level vertex. Its gain is the open benchrun measurement (`memory_traffic.md`).
- **Model draws:** 5.3–6.3 ms of renderer CPU [M]. Bind, state and lighting are 1.8 ms of that; two-sided and change-colour passes 1.3.
- **structure_visibility_find_clusters 5.5 ms** [M, profile]: the game's own visibility walk. A baked per-cluster visible set could replace part of it (`campaign_census.md`).
- **Frame end 4.3 ms** [M, profile section `psp_frame_account`]: the measured parts (GE wait 0.3, write-back, swap) don't add up to it. Breaking it down is cheap and should come first.

## The lock already exists: Halo's own throttle

`main_update_time` (main.c) has two paths:
- **Unthrottled (the PSP default since b66):** the game time per frame is the measured milliseconds, capped at 1/15 s. Uneven frames give uneven ticks, and frames over 66.7 ms slow the game down (b86: 28–30 ticks/s at times) [D, M].
- **Throttled (`vblankthrottle`, launch menu):**
  - The frame is padded to a whole number of vertical blanks, and the game time is counted *in vertical blanks*.
  - The interval starts at 2 (30 fps) and steps down after repeated misses. At ~62 ms frames that means 4 (15 fps), so **exactly 2 ticks every frame** [D].
  - It was turned off in b66 because padding cost 3.9 ms a frame at 21 fps (it held 20 fps when frames could have been ~47 ms).
  - Its wait is a spin loop, not a sleep, so the audio and file threads get no CPU while it waits [D]. With the mixer on the ME this matters less.

## Hardware: the donut test (b87 run 3, Blood Gulch, the warthog turning in circles)

| Run | Setting | Frame (render side) | Displayed fps | Ticks a frame (1 : 2) | Sheepdog01 |
|---|---|---:|---:|---|---|
| 3a | default (unthrottled) | 32–48 ms | 14–19 | ~1 : 2 | bounce during donuts |
| 3b | `interpolation` | 39–59 ms | 12–19 | ~1 : 3, mostly 2 | **worse: rubber banding all through driving** |
| 3c | `vblankthrottle` | 33–49 ms | 13–19 | ~1 : 2 | **almost gone**; slight at the hardest turns, as the load peaks and falls |

[M: windows after the first 20 s; Sheepdog01's account]
- **Interpolation costs CPU** (its first 15 s ran 52–54 ms a frame against 37–40 for the others) **and looks worse.** Dropped.
- **The throttle helps although the tick mix is the same.** What changes is the timing of the swap: with it, each frame waits for a vertical blank and the game time is counted in those same blanks, so the step a frame shows matches how long it stays on screen. Unthrottled, the swap isn't synced to the display and the step is the measured milliseconds, so steps and on-screen durations drift apart. [inference from main.c and Sheepdog01's account, not measured]
- The throttle is not holding 15 fps (it would show 2 ticks every frame). Its interval adapts between 2, 3 and 4 vertical blanks, and the remaining bounce coincides with the load rising and falling, when the interval changes. [D, M]
- **b90:** `vblankthrottle` is the default; `novblankthrottle` turns it off; benchrun turns it off (its timings are of the unpadded frame).
- The brief pauses Sheepdog01 felt in the first ~30 s (3c) are the geometry cache's first reads (~20 a second early on, 100–240 ms each burst), not the throttle (geometry_streaming.md).

## Recommendation [E]

1. ~~Test the existing lock against the donut bounce~~ Done (run 3): the throttle nearly cures it; it is the default since b90.
2. **Next: pacing choices to test by feel and histogram**: adaptive (Halo's own, b90's default), fixed 2 blanks (30 fps), fixed 3 (20 fps: ticks 1, 2, 1, 2), fixed 4 (15 fps: 2 every frame), each waiting by sleep instead of a spin. **30 fps stays the target** (Sheepdog01); a fixed rate is a floor for when a map can't hold it, and the test says which floor feels best. Fixed 3 is the open question: its steps are uneven by tick, yet run 3c (throttled, mixed 1 and 2) felt nearly smooth.
3. **20 fps needs interpolation**, which run 3b showed costs CPU and looked worse. Not a target until it is understood.
4. 30 fps is not realistic for the campaign on the PSP-1000 without a different renderer architecture.
