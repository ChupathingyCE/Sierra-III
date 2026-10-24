# Review: fix-alpha3

Branch `fix-alpha3`, off w139. The b139 hand-test leftovers in the alpha family: a10's corridor sign (H7), the violet far ocean on b30, the Pelican canopy after the cut-out bake.

## H7: a10's floor sign drawn as a green square (fixed, engine side, on)

**Reproduced [E].** Earlier tries used the debug flying camera and never showed the square. A real player at the hand test's camera (the shot's eye less 0.57, feet on the floor) with BSP 4 forced draws it, the same green square as on the hardware; BSPs 0-3, 5 and 6 draw nothing there.

**Cause [E]/[D].** The square is not the alpha-tested level shader `levels\a10\shaders\alpha test` (the earlier lead): the debug draw listing names a permanent decal, `levels\a10\decals-environment\bitmaps\decal floor maint` (multiply blend, alpha tested), and `nodecals` removes it. The Xbox draws alpha-tested decals with the alpha test on at 0x7f, and its decal combiners put the texel's alpha in the register the test reads (`rasterizer_xbox_decals.c`). The port's multiply and double-multiply decal path (`psp_ge.c`, the decal body) uses the GE's blend texture function with `GU_TCC_RGB`, so the fragment's alpha stayed opaque, the test passed everywhere and the whole quad was drawn.

**Fix.** `GU_TCC_RGBA` for an alpha-tested multiply or double-multiply decal whose map carries alpha: the texel's alpha reaches the alpha test, and the multiply blend reads no alpha, so the colour sums are unchanged. Keep word `decalsquare`: as before.

**Evidence [E].** Same spot, release bake: with `decalsquare` the floor shows the solid green square (31% of the frame green); with the fix, the stencilled arrow and lettering only (10% green); 23% of pixels differ, nothing outside the marking. Two runs each side, pixel-identical within a side.

**Cost [E].** None measurable: a10 to game 16,000, frame 20.84 vs 20.85 ms, tick 3.061 vs 3.060 ms (51 heartbeats each, two runs each side, identical). One texture-function bit per alpha-tested decal draw.

Other alpha-tested multiply decals (a10's `decal symbol cryo` among them) are cut out the same way now.

**Not settled.** The grating (hand shot 027) and the BSP 5 corridor (shot 030) views are dark in every BSP with a real player too: the lit surfaces are a red glow on black. Not a see-through or cut-out fault on this evidence; whether it is the level's lighting at that point of the opening (the alarm-lit corridors) or a lightmap fault needs the hand shots side by side, which this container doesn't have.

## The violet far ocean (b30): cause narrowed, not fixed

`sky\clear afternoon\shaders\water simple` (generic, alpha blend): three stages, R0 = ripple0 x ripple1, + ripple1 x 1/2, + the reflection cube map. The ripple maps (`sky\bitmaps\crap water0/1`) are **Y8 grey maps, not normal maps** [D]: the working guess (normal maps averaged as colours) is refuted. The bake's composite adds about 0.27 grey to the cube map's six-face mean (0.34 0.38 0.64), which gives the violet-blue (0.61 0.65 0.90). The cube map's faces differ a lot (face 5 dark blue 0.11 0.12 0.44; face 2's lower half 0.62 0.68 0.94), and a reflection seen from above at a low angle samples only some of them, so the six-face mean is the likely error [U]; the desktop build's (94,123,142) is greyer and less blue than any single face, so its fog over the sky's water may count too [U].

## The Pelican canopy: not done

The two compared bakes' bundles differ by more than the cut-out [D]. The cut-out touches only alpha-tested level shaders; the canopy is a model. Also new in the later bundle: the cube-map patches (a cube map packed as its six faces' mean, which the model reflection folds into a model's lighting) and the see-through tints' spread-texel rule. The earlier bake had no cube patches, so the canopy's reflection had nothing to add; the lighter, bluish glass is most likely its reflection cube map now drawn as an average [U]. That is the original's intent (the glass reflects), but whether the average is too bright needs the desktop build at the same view: the reference frames on hand show the canopy only from outside on b30 (dark glass, a white highlight). Next: an emulator pair of the a30 opening, cube patches off vs on (bake `--no-cube-patches`), against a desktop-build frame of the same flight.

## Instruments

`tools/tagdeps.py`: a see-through shader's maps now say their bitmap's usage, type and format, cube maps are listed face by face, and the combiner stages print whenever the pattern names four shaders or fewer (`--stages`: always).
