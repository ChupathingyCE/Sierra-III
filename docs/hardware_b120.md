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
- **Test 4 ran without `hud177`:** its log's keep words are `nearauto assettrack geo2 hudsnap`, preset 4's, and the HUD is the same size as test 3's (with `hud177` it would be ~18% bigger). The launch menu keeps unknown words as typed (`apply_preset`, `psp_menu.c`), so the likeliest cause is preset 4 applied again. The menu doesn't log which preset it applied: b121's preset-on-the-stamp ask covers that.
- **Test 4b (`hudsnap hud177`, re-run with preset 5)**: Sheepdog01: **looks much better**; the HUD needs pushing closer to the top edge, given the extra space it takes (`b120_hud_test3_vs_test4b.png`). The push that moves corner elements toward their edges while the HUD is zoomed (`PSP_HUD_PUSH`, 32 of the game's 848×480 units, `psp_fixups.py`) was sized for the 1.5 zoom.
- **Verdict:** `hudsnap hud177`, with a push sized for the 1:1 zoom (the top edge first).

## Test 5: the ladder (a10, preset 2)

`b120_test5.tgz`: **all ten maps in one session, no crash** [M]. `ladderreport.py`, b119's PSP-1000 ladder beside it:

| Level | fps (b119 → b120) | Tick, ms (b119 → b120) |
|---|---|---|
| The Pillar of Autumn (a10) | 14.9 → 15.1 | 6.6 → 6.6 |
| Halo (a30) | 7.0 → 7.0 | 11.5 → 10.6 |
| Truth and Reconciliation (a50) | 9.2 → 9.3 | 32.0 → 31.3 |
| The Silent Cartographer (b30) | 3.9 → 4.0 | 59.3 → 56.8 |
| Assault on the Control Room (b40) | 14.6 → 15.0 | 15.7 → 15.3 |
| 343 Guilty Spark (c10) | 9.1 → 9.2 | 22.7 → 21.3 |
| The Library (c20) | 8.0 → 8.0 | 26.3 → 25.9 |
| Two Betrayals (c40) | 13.5 → 13.7 | 16.2 → 15.3 |
| Keyes (d20) | 7.6 → 7.7 | 16.9 → 16.2 |
| The Maw (d40) | 19.0 → 19.6 | 5.5 → 5.4 |

The tick is faster on 9 of 10 maps (a30 −8%, c10 and c40 −6%) and fps is up or level on all ten, as the rig predicted.

## LK16: a10's window glass, yellow splotches in a repeating pattern

Sheepdog01 (20:23, during the ladder): a10's window glass looks worse than before, yellow splotches all over it in a repeating pattern.
- **Not a b120 regression in the emulator** [E] (lab run 38495638507, a shot every 10 s through a10's opening): b119 on its maps, b120 on its maps, b120 on b119's maps and b120 with `glassflat` draw the bridge windows identically, a green-yellow mottled pattern repeating where the view outside should be (shots 004 and 022). The two bakes' logs also match for a10 except the new HUD line.
- **So it's longstanding** [E]; the lead [U]: a texture meant for something else (the window shader's reflection map, or a bump or detail map) drawn as the glass's colour. For the look batch.
- **On the PSP it is worse** (Sheepdog01, 20:39): much more pronounced, less abstract and see-through.
- **The rig's PSP-3000 draws b119 and b120 identically** [M] (lab runs 38496994706, 38497057378; `b120_a10_glass_rig_b119_vs_b120.png`), and the same as the emulator; `vramlargest` (VRAM filled by size) changes nothing. The PSP-1000's test 5 log and the rig's show the same texture setup for a10 (RAM cache 4,096 KB, the same pack, learned binds read).
- **Sheepdog01's PSP-1000 on b120** (`b120_test1a_a10glass1.tgz`, shots at game time 45-73; `b120_a10_glass_psp1000_vs_rig.png`): the same green-yellow mottled pattern at about the same strength as the rig's frames, from inside the bridge and from outside the ship. The frames come from nearby moments of the cutscene, so not pixel for pixel.
- **So the pattern is b119's too** (emulator, rig, and now the PSP-1000 agree on b120's look; the emulator and the rig show b119 identical). Sheepdog01: the glass was faint and see-through in the first builds a10 ran in.
- **Dated to b116** [E] (lab run 38501924836, `b120_a10_glass_tour.png`): b113-b115 draw the bridge windows pale bluish-white; b116 onward green-yellow, darker and opaque. b116 brought the see-through shaders' tints (`seethrough.py` in the bake, multiplied in by the game).
- **Found: the tint** [E] (lab run 38502938098, `b120_a10_glass_tint.png`): b120 with `notint` draws the windows as b113-b115 did. The tint the bake records for the bridge windows' shader is wrong for this glass.
- **Sheepdog01 (21:52): the glass wobbles as the camera pans in from outside.** Lead [U]: the mottled texture is the window's reflection map, its coordinates following the view direction; the tint makes it dominant, so its slide shows.
- **Fix (b121, a restoration with an off word):** the tint for this shader in the bake (why `seethrough.py` gives it green-yellow), then whether the reflection should be as strong as it is even untinted (pre-b116 still shows the pattern, paler).

## LK17: the Pillar of Autumn's thruster, its inner part shimmers (a10)

Sheepdog01 (21:52), in the external flyby. Leads [U]: the inner glow nearly coplanar with the nozzle and fighting it for depth (as LK13), or a fine texture without the smaller mip levels at that distance. An emulator capture of the flyby tells them apart.

## For b121 (Sheepdog01, 2026-10-13)

- **The build number and the active preset on screen.** The stamp line at the bottom of every screenshot already shows the build, game time and frame (`b120 t 341.0 f 1930`); add the preset there, and in the launch menu's header.
- **Reopen files on wake** (the sleep bug above).
- **The HUD: `hudsnap hud177` on by default** (Sheepdog01's A/B, test 4b), with the corner push re-sized for the 1:1 zoom so the elements sit closer to the edges, the top first. Off words for each.
- **LK16, a10's glass: the see-through tint for its shader** (found; the fix above).
- LK13 (far water), LK14 (Pelican window), LK15 (tracker cross), LK17 (thruster shimmer): leads above.
- **The log pull's `rm -f $psp/halo/shots/*`** fails in fish when the folder is empty (fish refuses a wildcard that matches nothing). Use `find $psp/halo/shots -type f -delete`.
