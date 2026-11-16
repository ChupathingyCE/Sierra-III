# Build 106 on the PSP-1000: b30 and The Library (2026-10-08)

Two runs on Sheepdog01's PSP-1000 at 333 MHz. `tools/hwreport.py` reads both (it leaves out stretches with no ticks: the pause menu, the main menu).

## Run 1: The Silent Cartographer (b30)

The map's first hardware run. Sheepdog01's PSP-1000 at 333 MHz. Keep words: `assettrack geo2 nearauto animnowindow`, deathless. The map was baked on the laptop with the b106 bundle (`--maps b30,ui`). Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

### What happened

- The map loads and plays: the Pelican drop, the landing and the beach fight [M].
- **It crashed at game time 2648 (88 s of play)**: an address error in `update_controller_short` (recorded_animation_playback.c). It is the halfword read from an odd address that the strict interpreter found on a10 (b107_candidates.md item 11). The Pelicans fly by recorded animations, so b30 on b106 should die near the same point every run [U]. b107 reads that struct a byte at a time.
  - The emulator's b30 runs never hit it: its JIT doesn't fault on unaligned reads, and the strict interpreter's b30 run stopped at 1,309 ticks.

### Speed [M]

- **3.6 fps on average** in play (10th percentile 3.1); tick 68 ms; 1.93 ticks a frame.
- A frame at the beach: about 138 ms of ticks, 53 ms of the GE renderer's CPU work, 25 ms the rest.
- Top profile steps, ms a frame: `objects_update` 90 (bipeds 32 ms a tick for about 30 bipeds), `ai_update` 42, `rasterizer_model_draw` 42.
- **The cache file thread's fix worked:** 3.0 ms a stick call (2,436 calls), against b105's 87 ms.
- The game thread itself spent 36.5 s of the run in stick calls (worst 872 ms). Most of that is textures read as drawn: b30's track textures (7,060 KB) don't fit the caches' 2,752 KB.
- Sound obstruction ran at its budget every frame (6.0 tests a frame), which is the case b107's stale-result cap is for.
- The sound cache failed 204 loads for room.

### What Sheepdog01 saw, and what the log says

- **Objects flickering in and out** (rocks, crates, shields, trees), better further up the beach. [M] The display list (384 KB, from the heap) fills, and the draws after that are skipped: up to 235 model draws and 135 decal draws a frame skipped in the fight. Which ones get skipped depends on draw order, so different objects drop out from frame to frame. Further up the beach fewer models are in view. The emulator's beach route stands still in one spot and never filled the list (55 KB of models a frame).
- **The Pelican's thrusters draw as white cones.** Their shaders (`hover thrusters`, `jet thrusters`) are `shader_transparent_chicago` [D]. c40's light shafts, also see-through (`shader_transparent_generic`), draw white too. Not looked into yet.
- **The water was white, then textured for a moment, a few times.** The ocean is `shader_transparent_generic` ("water simple") [D]. Either its textures are thrashing in the texture cache or the shader falls back to white [U]. `geo2` takes 2 MB of the texture RAM cache for geometry, which leaves textures less room.

## Run 2: The Library (c20), against b105 run 1

Same keep words; the map is the older bake (b105's). No crash. 168 s of game time were played; the rest of the run's 30 minutes was the pause menu (281 heartbeats with no ticks) and, at the end, the main menu (quit from the pause menu).

**The same stretch of game time (150 to 5200) [M]:**

| | b105 | b106 |
|---|---|---|
| fps, mean | 5.98 | **7.45** (+25%) |
| frame by the clock, ms | 172 | 144 |
| ticks, ms a frame | 64.8 | 58.4 |
| the rest (outside the game's code), ms a frame | 18.1 | 9.6 |
| GE renderer's CPU, ms a frame | 38.8 | 43.2 |
| tick, ms mean | 32.8 | 32.8 |

**What b106 aimed at, measured (profile steps, ms a frame, windows that ran ticks):**
- The cache file thread: **87 → 2.2 ms a stick call** (5,726 calls).
- Looping sounds (the obstruction tests): 11.8 → 1.8.
- Lightning (`widgets_render`): 6.4 → 0.5.
- Decals: 13.4 → 3.0.
- `objects_update` 63.9 → 43.5 and `ai_update` 13.3 → 7.3, but those follow the route (a hand-played fight).

**Not better:**
- `lights_render_diffuse` 2.6 → 7.1 ms a frame. This run drew the flashlight's cone far more (2,623 cone light draws against 562), which is likely most of it [U].
- The display list overflows here too, less than on b30: up to 12 model draws a frame skipped in the Flood fight (b105: up to 20).
- The sound cache: 97 loads failed for room, two "SOUND CACHE BLOWN" messages, and one 0.7 s gap.
- The main menu's ring draws with a white band (shot t1820), likely the same white see-through drawing as b30's thrusters [U].

## Next

- b106 is done: b30 stops at the crash, and The Library's A/B against b105 is in. The next hardware build is b107 (the crash fix).
- The freed memory now has four claimants with hardware evidence behind them: the display list (the flicker), the sound cache (sounds that don't play), the tag window (a50, b40, a30) and the BSP formats.
- The white see-through shaders (chicago and generic): reproduce in the emulator and fix.
