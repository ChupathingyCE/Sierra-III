# Build 120 on Sheepdog01's PSP-1000 (2026-10-13)

Labels: **[M]** measured on a PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference. The tests are `build_b120.md`'s.

## Test 1: the cinematic bench (b30, preset 3)

`b120_test1.tgz`. `benchreport.py` [M]:

| Stretch | b120 | b119 | Change |
|---|---:|---:|---:|
| cinematic (150-570), frame | **158.7 ms** (6.30 fps) | 162.2 ms (6.17 fps) | −2.2% |
| cinematic, drawing | 49.1 ms | 49.5 ms | −0.8% |
| cinematic, tick | 52.7 ms | | |
| flight (300-2000), frame | 238.1 ms | 242.2 ms | −1.7% |
| flight, drawing | 111.6 ms | 112.3 ms | −0.6% |

The rig saw the same direction from the fresh profile on b30's flight (231.7 against 235.9 ms with b119's profile on the same code) [M, rig].

### LK13: far water flickers on the horizon in b30's intro (Sheepdog01)

Sheepdog01: the water in the far distance of the intro cutscene flickers on the horizon; likely longstanding.

- **Not dropped draws:** "draws skipped with the display list full 0" in every window [M].
- **The lead [U]: depth precision as `nearauto` switches.** In this run the near plane was ×16 on 151 frames and ×1 on 149 [M]. Each halving of the near plane doubles a depth step at every distance (the design note above `near_plane_auto` in `psp_ge.c`), so ×1 frames have 16× coarser depth than ×16 frames, with a 16-bit depth buffer. Close-up shots in the Pelican pin it at ×1 (a model near the eye); wide shots go to ×16. On the coarse frames the far water and what lies just behind it would share depth values and fight, so the horizon flickers as the cuts alternate.
- **The check (Sheepdog01, ~3 min, any time):** preset 3 with `nearauto` replaced by `near4` in `keep.txt`. Steady water confirms it. The Pelican's interior will be cut open near the eye, which is expected with a pinned plane.
- **If confirmed, the fix (b121, a restoration with an off word):** draw the far water with a slight depth bias toward the eye so it wins ties, as the stencil nudges do. Smoothing `nearauto` would only slow the flicker.

## Test 2: play, b30 (preset 1)

`b120_test2.tgz` (shots 000-013). Sheepdog01's notes, and what the log says.

### Resting items: they work, with one ask
- Still fully interactive: thrown by explosions, dropped plasma grenades detonate in blasts, picked up as before.
- **"Sticky":** a dropped weapon sticks to an object it falls against. Sheepdog01 first asked for a 4 s delay before an item may sleep; then (20:06) said a found mechanism makes the timer unnecessary.
- **The mechanism is Bungie's, not the sleep** [D]. In a campaign game only one line sets an item's at-rest bit (`items.c` `item_update`, the landing branch; `game_engine.c`'s is multiplayer): a falling item that lands on the level (flat enough) is "on structure", and one that lands on top of a scenery or device object is "on object". An on-object item is re-aligned to that object's node every tick at the point and angle where it landed, so it stays glued there. The sleep rule never offers on-object items (`psp_item_may_sleep`), and a falling item isn't at rest, so sleeping can't cause the sticking.
- **So no timer.** The cheap proof, when convenient: the same drop with `itemsawake`; if it still sticks, it's the original's.

### The sleep breaks the game (found: dead file handles)
Sheepdog01 slept the PSP as the Warthog dropped; after the wake the level was garbage (a "tunnel", shots 011-013), sound effects broke, the ambient track changed and explosions played at random.
- **The log** [M]: from the wake on, every read fails: `textures: reading texture 600 failed`, `geometry: reading 6336 bytes at 0c35c1a0 failed (-1)`, over and over.
- **Why** [D + U]: the game keeps its map, geometry and texture files open and streams from them. A sleep invalidates open files on the Memory Stick; the power callback (`power_callback`, `psp_platform.c`) gives back and restores the caches' memory but never reopens the files, so every later read uses a dead handle. Geometry reads fill vertex buffers with garbage (the tunnel), and sounds streamed from the map file come out wrong.
- **Fix (b121, on by default):** on `PSP_POWER_CB_RESUME_COMPLETE`, reopen every file the game holds open (same path, same mode) before anything reads.

### LK14: b40's chasing Pelican, its cockpit window pops and turns lighter than the body
In b40's intro. Leads [U]: the glass shader on the Pelican's window, or a level-of-detail swap that changes which shader draws it. To look into it: the log's see-through lines on b40, and an emulator capture of the intro.

### LK15: the motion tracker's pulse shows a faint cross
Shots 005-007: a hairline straight down from the centre in 005. Lead [U]: the pulse is drawn as four mirrored quarter-quads, and where two meet, the bilinear filter samples the texture's edge or wraps to its far edge. Fixes to try: clamp the sheet's edges, or pull the UVs in by half a texel.

## Tests 3 and 4: the HUD A/B

- **Test 3 (`hudsnap`)** against test 2 (plain), at 4× (`b120_hud_plain_vs_hudsnap.png`): slightly better; Sheepdog01: both somewhat jagged, test 3 a little better on screen thanks to the smoothing.
- **Test 4 ran without `hud177`:** its log's keep words are `nearauto assettrack geo2 hudsnap`, preset 4's, and the HUD is the same size as test 3's (with `hud177` it would be ~18% bigger). The launch menu keeps unknown words as typed (`apply_preset`, `psp_menu.c`), so the likeliest cause is preset 4 applied again. To re-run with preset 5. The menu doesn't log which preset it applied: b121's preset-on-the-stamp ask covers that.

## For b121 (Sheepdog01, 2026-10-13)

- **The build number and the active preset on screen.** The stamp line at the bottom of every screenshot already shows the build, game time and frame (`b120 t 341.0 f 1930`); add the preset there, and in the launch menu's header.
- **Reopen files on wake** (the sleep bug above).
- LK13 (far water), LK14 (Pelican window), LK15 (tracker cross): leads above.
- **The log pull's `rm -f $psp/halo/shots/*`** fails in fish when the folder is empty (fish refuses a wildcard that matches nothing). Use `find $psp/halo/shots -type f -delete`.
