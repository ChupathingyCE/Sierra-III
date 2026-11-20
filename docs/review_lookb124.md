# Review: Sheepdog01's b122 look reports, on w124 (branch `lookb124`)

Sheepdog01's PSP-1000 runs of b122 (2026-10-14), tests 8, 10a, 10b and 13: b30's far water and shore, the screen's dither, c10's ferns and d20's doors. Each was reproduced in the emulator on w124 (`9360c9c`), traced in the code, and fixed where the cause is a departure from the original. Every fix has an off word; the dither has an opt-in A/B word, since it's Sheepdog01's look call.

Labels: [M] Sheepdog01's PSP-1000 (his logs and shots), [E] emulator (lab batches named below, all `fixedticks fixedseed`, hands off unless a `tp` or route is named), [D] read in the code, [U] inference.

| # | Report | Cause | Change | Keep word | Default |
|---|---|---|---|---|---|
| 1a | b30: far water flickers on the horizon (LK13) | The port pushes see-through surfaces that aren't decals 1+ depth steps away from the eye; at the ×1 near plane the far ocean ties with the seabed and loses | No push for shaders that aren't decals, as the Xbox (LESSEQUAL, no bias) | `seethroughpush` | restoration, on |
| 1b | b30: steps and "layers" along the shore (LK21) | (i) each ocean draw took its own push, 1-9 steps; (ii) the shore's waves shader has a mask map the GE never draws; (iii) the water's fog by depth isn't drawn from above | (i) fixed by 1a; maps clamped as the Xbox clamps them; added water faded by the fog. (ii) and (iii): found, not fixed | `seethroughwrap`, `waterunfogged` | restorations, on |
| 1c | b30: white bar where sky meets water (with `near4`) | The frame's clear colour (the fog colour, as the Xbox clears) showing through the sky's see-through layers at the horizon | None: no departure found | | |
| 2 | Fixed pattern over everything (test 13) | The GE's 4×4 ordered dither, on since b116 | `ditherhalf` added for the A/B | `nodither` (exists), `ditherhalf` | unchanged |
| 3 | c10: bright "sawbands" on fern leaves (test 8) | Another leaf's front face crossing the leaf near the stem, lit by the flashlight; not `foliagetwopass`, not the back faces | None yet | `nobackpass` (recon) | |
| 4 | d20: doors bright, far door flickers, HUD blinked | Not the sleeping machines' functions; the pale face isn't the self-illumination pass (only the rim is); the far door's look changes with the viewpoint the same way at `near4` | None yet | `noglowpass` (recon) | |

## 1. b30's water

### 1a. The far water's flicker (LK13)

**Reproduced** [E, lab 38670735611 `lookb124a`]. b30's flight sits at the ×1 near plane on every frame inside the Pelican (the shots' "near plane 0.0625", shots 6-17); `near4` holds ×4. Against a run with no level see-through (`nooverlays`), the water at ×1 is missing in a dashed band a few pixels deep at the horizon, and the band changes from frame to frame (three consecutive frames, shots 9-11). At ×4 the water reaches the horizon. Sheepdog01's report [M, test 13]: steady with `near4`.

**Cause** [D].
- Every see-through level triangle not lying on a wall is pushed *away* from the eye by at least one depth step (`overlay_units`, `psp_ge.c` 10773-10842; `overlayfreefar`, on since b116, made the push small and measured at the far corner).
- At ×1 a depth step is z²/(0.0625 × 61439): 65 units at 500 units out. Beyond ~500 units the whole scene has about 8 depth values, so the ocean and the seabed under it round to the same value on many pixels, and a one-step push makes the water lose them. Which pixels tie changes with every small move of the eye: the flicker.
- So it isn't `nearauto` changing scale within a shot (the scale doesn't change during the Pelican shots [E]), and hysteresis or holding the scale through cutscenes wouldn't help: the ×1 frames themselves are the problem. A bias toward the eye was the right family of fix, and the Xbox already says how much: **none**.

**What the Xbox does** [D]. `rasterizer_xbox_transparent_geometry.c` (the generic and chicago passes) and `rasterizer_xbox_water.c` draw see-through surfaces with `D3DCMP_LESSEQUAL` and `D3DRS_ZBIAS` 0, except shaders flagged decal (`shaders.c`, `shader_is_decal`), which get the bias. So the water wins its ties.

**The change** (`psp_ge.c`):
- 9293-9313: the comment and the depth-mode bits a see-through draw carries (`SEETHROUGH_EXACT_DEPTH`, `SEETHROUGH_CLAMP_U/V`, `SEETHROUGH_FOG_FADE`).
- 10378-10411, `psp_ge_seethrough_shader`: reads the shader's type (+36) and decal bit (generic and chicago: the flags byte at +41, bit 1; glass: the word at +40, bit 1; meter: bit 0) on the engine's side and passes them in the tint call's flags, so they survive a deferred queue. A shader that isn't a decal gets `SEETHROUGH_EXACT_DEPTH`.
- 11397, 11462-11469, 11496: the level queue skips the nudge and the wall test (`overlay_answer`, a collision ray per triangle) for such a shader and puts its triangles in one draw; 11320 does the same for the surfaces faded by angle.
- 11796-11800: the flush gives that draw a depth offset of 0.
- Decals keep the port's nudges (they stand in for the Xbox's decal bias on 16-bit depth). The teleporter shield's case from b65 is a decal question too: if its shader isn't flagged decal, it now ties with its frame as on the Xbox [U].

**Evidence** [E].
- Horizon band, shots 6-14 of the flight (a script over the shots: the band's pixels that show seabed instead of water, against `near4`): `seethroughpush` 1,523 more pixels than `near4`; the change 36 more. Pixels changing between a moment's consecutive frames: `seethroughpush` 3,727, the change 2,718, `near4` 2,729 (lab 38672722971 `lookb124b`, 38674956470 `lookb124d`). The change is as steady as `near4`, without its cut cabin.
- Shots at the same frames, on against all three off words (`lookb124d`): 240-600 pixels differ per shot in the flight, 960-1,560 at the landing, along two lines only: the horizon and the waterline.
- **Speed**: the flight's frame 34.72 → 34.03 ms (−2.0%, same frames, `lookb124d`): the wall test and the bucketing are skipped for the water.

### 1b. The shore's layers (LK21)

`tdump` now lists every map of a generic or chicago shader (`psp_ge.c` 11676-11696). At the landing [E, lab 38673819723 `lookb124c`] the shore is two see-through surfaces:
- `levels\b30\shaders\water` (the water shader, type 7): base map `water mask`, added;
- `levels\b30\shaders\waves` (generic): maps `waves mask`, `waves`, `waves`, `waves water detail` (×18). **The GE draws one map: `waves`.**

Three causes, one fixed:
1. **The pushes** [D, E]: each ocean draw went out at the smallest push among its triangles (1-9 steps by the far corner's distance), so neighbouring draws met the sloping seabed at different depths: steps along draw edges that move as the eye moves (Sheepdog01's "water layers jump up and down through the seabed" with `near4`). Gone with 1a: every water draw is at its own depth.
2. **The waves' mask** [D, U]: the `waves mask` map (map 0, same scale as `waves`) is what fades the waves strip out along its edges on the Xbox; the GE draws the strip at full strength to its mesh's edge, the hard outer boundary of the pale band near the beach (`lookb124c` shots 15-17, `nooverlays` for the seabed alone). **Not fixed.** The GE has one texture a pass and the 5650 frame has no alpha to hold a mask between passes. Options for a follow-up: the mask sampled per vertex into the vertex alpha (needs the mask's texels on the CPU and a strip fine enough), or a baked map per shader where the mask and the drawn map share coordinates and the drawn map doesn't move (here `waves` scrolls, so not this one).
3. **The water's fog from above** [D]: the Xbox colours what lies under water by its depth below the fog plane in a pass of its own (`rasterizer_xbox_environment_fog.c`), so the shallows shade into the deep smoothly; the GE fogs by distance only (b116's note, `build_b116.md`). **Not fixed**: a second pass over the underwater level with a fog colour and an alpha per vertex (depth below the plane), depth EQUAL, as the Xbox does; its density curve is a texture in the map (`planar_fog_density`) the code doesn't show.

Two smaller restorations ride along, both from the same Xbox files:
- **Maps clamped as the Xbox clamps them.** The water shader's base map always (`rasterizer_xbox_water.c` sets CLAMP on it); a generic or chicago map when its flags say u or v clamped (bits 1-2 generic, 2-3 chicago). `psp_fixups.py` 113-125 (`psp_transparent_map`) reads the chosen map's flags and tells the GE (`psp_ge_seethrough_map_clamp`, `psp_ge.c` 10373); 140-144 keeps a clamped map's whole offset (only the fraction is the same for a repeating one). The flush sets the wrap per draw (11808-11811). Off: `seethroughwrap`. In b30 and c40 no generic map had a clamp flag set [E, tdump]; the water's mask is the one that changes.
- **Added water fades with the fog.** A water shader flagged for atmospheric fog (its flags word at +40, bit 2) fades its added reflection with the fog on the Xbox (the final combiner takes the fog factor). The GE's fog mixes toward its colour, so such a draw gets the fog colour black (11727-11732, restored at 11874-11875). Before, an added draw went unfogged. c10's swamp water carries the flag [E, tdump: depth mode 240], and so does b30's ocean [E: the fade's counter, from the landing on, `lookb124d`]; in b30's flight the fade changed no pixel at the horizon. Off: `waterunfogged`.

**What's left on the shore and isn't a departure**: the sawtooth where the water meets the sand. Where two surfaces cross at a shallow angle, the 16-bit depth can't tell them apart over a strip a few pixels wide at ×1 (b115's note). Equal depths now go to the water, as on the Xbox, so the line sits a little higher up the sand [E, the waterline in `lookb124d` shots 12-17]; the Xbox's 24 bits make that strip 256 times thinner.

### 1c. The white bar at the horizon (`near4`)

**Found** [E, `lookb124c`/`lookb124g` with `clearmagenta`]: the bar turns magenta, so it's the frame's clear colour. The sky is see-through layers (`sky clear blue`, `clouds`, `fog band`, the sky's own `water simple`) over the clear colour everywhere (the whole sky shows magenta through its thin parts), and at the horizon the layers are thinnest. The Xbox clears each window to its atmospheric fog's colour too (b116, `window_clear_color`), so this may be the original's look.
- With `nearauto`, the far water's flicker hid the bar; with the water now reaching the horizon at ×1 as at ×4, **the bar will show with `nearauto` in this build** [U].
- Tried and taken back: the sky drawn with the Xbox's sky near plane (1/256 of a unit, `rasterizer_set_frustum_z`): not one pixel changed in b30's flight [E, lab 38676317889 `lookb124g`, magenta pixels 79,067 both ways].
- **Sheepdog01's call**: a PC ChupathingyCE screenshot of the flight's horizon settles whether the bar is the original's.

## 2. The fixed pattern: the GE's dither

**What it is** [M]: in Sheepdog01's test 13 shots, a flat stretch of sky or water carries a 4×4 pattern fixed to the screen: per channel, the 4×4-periodic part of the image (each pixel minus its 9×9 mean, averaged by its place in the 4×4 cell) spans 7.4-10.3 levels (of 255; two shots, three channels each) and has the shape of the GE's matrix, `{-4,0,-3,1 / 2,-2,3,-1 / -3,1,-4,0 / 3,-1,2,-2}` (`ge_dither`, `psp_ge.c` 1357).

**What's on by default** [D]: since b116 (b113 #6) the world is drawn with the GE's ordered dither, so the 16-bit frame keeps the Xbox's 32-bit gradients; since b117 only on draws that don't blend and on the level's lightmap pass (a blended draw re-rounding the frame built fixed patterns under smoke). The HUD isn't dithered. The matrix spans 8 levels: one step of red's and blue's 5 bits, two of green's 6, so the pattern is strongest in green.

**What turning it off costs** [E, lab 38672722971 and 38673819723]: banding. In d20's dark hallway, brightened 4×, `nodither` shows the pillar and walls in flat contour bands; the default shows the pattern instead; `ditherhalf` softer contours and half the pattern. Pattern amplitude (luminance, 4×4-periodic part, mean over 24 shots):

| Map | default | `ditherhalf` | `nodither` |
|---|---:|---:|---:|
| b30 flight and beach | 7.49 | 3.44 | 1.00 |
| d20 hallway | 4.06 | 2.11 | 0.52 |

**The A/B word** (`psp_ge.c` 1357-1374): `ditherhalf` (opt-in) sets a half-span matrix (`{-2,0,-2,0 / 1,-1,1,-1 / ...}`, a 2×2 Bayer at half strength). `nodither` already turns it off. The default is unchanged: Sheepdog01's look call. Cost: none (one matrix or the other, once a frame).

**For Sheepdog01**: three runs of the same spot (preset with `nodither`, with `ditherhalf`, without): the overlay against the bands, on the PSP's screen, which the emulator's numbers can't judge.

## 3. c10's ferns

**Sheepdog01's test 8 had the flashlight on** [M: the leaves and the rock behind them lit, against the emulator's dark night at the same spot; `lookb124a`]. With the flashlight (select + up) [E, `lookb124b`, `tp` to test 8's spot]:
- **The bright sawband reproduces**: a bright, serrated band along a big leaf's midrib that moves as the plant sways (shots 54-56).
- **Not `foliagetwopass`**: the runs have it off.
- **Not the back faces**: `nobackpass` (new, recon: the two-sided pass left out, `psp_ge.c` 8789 and 9027-9029) leaves the band as it is [E, `lookb124c`]. `nocull` makes the whole leaf dark instead (its back faces win), so this leaf is drawn two-sided as designed.
- **So it's another leaf's front face** crossing this one near the stem, lit by the flashlight; the serration is that leaf's alpha-tested edge [U]. Whether the Xbox shows the same crossing depends on the plant's pose matching the original's (it's a skinned, swaying model); the emulator can't say what the Xbox draws. **Not fixed.**

**The far plants' flicker** (Sheepdog01: medium and far plant textures flicker as the camera moves) [D, U]: models are alpha-tested at 0x7F with trilinear filtering, as the Xbox (`rasterizer_xbox_models.c`: ALPHAREF 0x7F, MIPFILTER LINEAR); the differences are the PSP's automatic level choice (by s across and t down the screen, `psp_ge.c` 2349-2356), the dropped top mip and 272 lines instead of 480. The level's surfaces already take their level by distance ("slope" mode, b58); model parts don't. A slope-mode A/B for alpha-tested model parts is the next thing to try. **Not done here.**

**For Sheepdog01** (one screenshot): ChupathingyCE on the PC at test 8's spot (`-134.21 -39.35 39.21`, looking `0.64 -0.55 -0.53`) with the flashlight on: is the band there too?

## 4. d20's doors and the HUD

**The doors' brightness is not the sleeping machines' functions** [E, `lookb124a`, `tp` to test 10b's spot]: the doors' pixels are identical with `machinefuncsheld` and with `machinesawake` (0 pixels differ in the far door's box at each of the hallway's 8 moments). **Nor the self-illumination pass**: `noglowpass` (new, recon: the self-illumination pass left out, `psp_ge.c` 9059-9068) changes about 1,850 pixels a frame: a reddish glow along the near door's rim (680 of them), and nothing on its pale face (the face's mean colour is the same to a level) [E, `lookb124b`]. So the face's brightness is the base map under the model's lighting [U], and the rim's colour is the door's self-illumination, which `machinefuncsheld` doesn't change here. The port doesn't draw model detail maps or reflections; a missing detail map (biased multiply) would brighten or darken a model's texture. **Needs a reference**: ChupathingyCE on the PC at `16.07 -95.23 -73.18` looking `1.00 0.03 0.00`.

**The far door's flicker**: the door at the end of the hallway shows its glyph partly covered by a lighter panel whose edge moves with the viewpoint [E, route `d20_hall_creep`, lab `lookb124d`]. The same frames at `near4` look the same, so it isn't depth precision: it's the door's panels at different depths (parallax), or a level-of-detail change with distance [U]. At Sheepdog01's frame rate and pace that change reads as a flicker. **Not fixed**; the same reference shot from a couple of spots along the hallway tells.

**The HUD's blink** (test 10a): `test10a/log/log.txt` is a 5-line launch (`HOME: exiting`), not the run's; the run's log should still be on the stick as `log/NNN_log.txt` (the last 8 launches are kept). Nothing to read until that's pulled. In test 10b's log the display list never overflowed ("draws skipped with the display list full 0" in all 18 windows [M]), the usual cause of a missing HUD.

## Play and multiplayer

- **Play is unchanged** [E, lab 38674956470 `lookb124d`]: hands-off `statecrc`, the build against all three off words. The play hash and both random seeds are the same at every tick on **d20 (16,351 ticks), a50 (27,192) and c20 (20,521)**. Whole state differs in `objects` (the render caches drawing writes) from tick 30, as in every drawing change; a50's `first person weapons` too; each map's last checkpoint is cut by the wall.
- **b30 is not a fair statecrc map for a drawing change** [E, lab `lookb124f` and `lookb124h`]: two runs of the same keep words match at every tick (10,228 and 10,211 ticks), but *any* change to the drawing moves play, from tick 872-1,887: each off word alone does (`seethroughpush` from 1,326, `seethroughwrap waterunfogged` from 1,887), and so did the taken-back sky change, which only moved a projection. The first allocations to differ are `actor` and `prop` in the Pelican's flight [U: b30's scripted opening keeps time by its dialogue's sound, which the frame's emulated length shifts]. The shots match until then.
- **Multiplayer** [E, lab 38674966445 `lookb124e`]: the multiplayer build (`PSP_MULTIPLAYER=1`) compiles and links (heap 4,608 KB), and Blood Gulch's 24 shots are pixel for pixel the same with the off words. The changes are drawing only; nothing reaches the server.
- c40, on against off [E, `lookb124e`]: 22 of 24 shots within 80 pixels; two shots of the control room's see-through sphere differ by 4,106 and 13,145 pixels (its map's edges, now clamped or exact in depth).

## What only Sheepdog01's PSP can confirm

1. **b30's flight with `nearauto`** (no `near4`): the far water steady at the horizon; the shore without the moving steps. Expect the white bar (1c) to show now.
2. **The dither A/B**: `nodither`, `ditherhalf`, default, at the same spot (a dark corridor and b30's sky).
3. The cost: the emulator's flight frame is 2% faster; the PSP's figure is its own.

## Follow-ups (not in this branch)

- The waves' mask (1b-ii) and the water's fog from above (1b-iii).
- A generic map's "unfiltered" flag (bit 0): the Pelican's decals carry it [E, tdump]; the Xbox draws such a map with point sampling, the GE with bilinear.
- A slope-mode level choice for alpha-tested model parts (3).
- The reference shots in 1c, 3 and 4.

## Batches

All in the lab's `projects/sierra/scripts/batches/`: `lookb124a` (38670735611), `lookb124b` (38672722971), `lookb124c` (38673819723), `lookb124d` (38674956470), `lookb124e` (38674966445), `lookb124f` (38676185405), `lookb124g` (38676317889), `lookb124h` (38676961774). The route `d20_hall_creep` is in `projects/sierra/scripts/routes/`.
