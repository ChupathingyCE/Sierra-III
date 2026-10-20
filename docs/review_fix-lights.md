# Review: fix-lights (b139's hand test: H6, H22, H9, H23, H33, H41, H42, H24)

Branch `fix-lights`, off `w139`. Every change has a keep word that turns it off. Evidence labels as in the
README: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.

Status: the code compiles file by file in both builds [D]; the full builds, the bake and the emulator
A/Bs are below as they come in. Costs are game-time comparisons on the same stretch in one batch [E].

## H6: relief streaks on a30's cliffs

**What:** relief (the bump map's shading baked into a second copy of the base texture) stays on by default,
now on a bake that filters the bump; keep word `norelief` turns it off. (`relief` no longer does anything.)

**Why [U]:** the cliffs' bump maps tile far finer than their base maps. The bake sampled one bump texel per
base texel, unfiltered, so the bumps aliased into stripes, and the cliffs' stretched UVs drew those as
streaks.

**Key lines:**
- `psp/src/psp_tex.c`, `psp_relief_key`: `on = !psp_keep("norelief")`.
- `tools/texpack.py`, `apply_relief`: the bump is box-filtered over each base texel's footprint before its
  shading is taken; relief is left out (logged "bump relief left out for ...") where a base texel covers
  more than `RELIEF_MAX_FOOTPRINT` (4) bump texels, since a filtered bump that fine is flat anyway.
  `--relief-unfiltered` bakes the old way, for A/Bs.
- `psp_fixups.py`, the level detail scale: a relief texture lives under its base's key `| 2`, so the
  base's size is read from `base_key & ~3u`, not from the relief key (it read the wrong header before).

**Cost:** none measurable either way [E]: a30's cliff stretch, game ticks 600-2,900, 37.5 / 14.2 ms a frame off
against 37.6 / 14.2 with relief on the new bake.

**A/B [E]** (a30's two cliff spots, emulator size): the filtered relief on the new bake differs from relief off
by 0.2-2.4 levels of 255 on average, the old bake's relief by 1.7-2.7 with single pixels up to 255. No streaks
show at the emulator's size in any of the three. **Owner's call (made):** relief on by default with the new
bake, to be confirmed by eye on the PSP; `norelief` is the A/B's other side.

## H22: shiny objects too dark in shade

**What:** a model part's reflection is folded into its ambient light. Keep word `noreflection` turns it off.

**Why [D]:** the Xbox's model pixel shader (`rasterizer_xbox_models.c`, its last stage) draws
base x lighting + cube map x specular mask x the reflection's tint and brightness. The port never drew the
reflection, so polished metal (the Pelicans, the Warthog, Covenant armour) went near black where the
lighting is dark, while the original keeps a grey sheen.

**Key lines:**
- `psp_fixups.py` (the model shader path): the reflection's strength and tint worked out as the Xbox's
  draw does: the brightness faded between the shader's falloff and cutoff distances, the perpendicular and
  parallel brightness and tint taken at their middle, times the object's lighting reflection tint.
- `psp/src/psp_ge.c`, `ambient_with_reflection`: ambient + reflection x cube average x mask average /
  base average. On average the base x (lighting + that) equals base x lighting + reflection, with no extra
  pass. The cube's average comes from an 8 x 8 patch the pack now stores (`texpack.py`, `cube_patch`),
  else 0.35.

**Cost [E]:** b30's beach spots, game ticks 5,400-7,000: 25.9 ms a frame with it, 25.6 without (+1%).
It restores the original's look, so it is on by default.

**Look [E]:** at the beach's still spots the fold lightens 6-8 thousand pixels by more than 8 levels (the
marines' armour, the Pelicans) and darkens almost none. It is a small part of the gap: against the PC
reference at the opening (same flight, by tick), the first-person rifle in shade averages 27/36/39 on the
PSP and 47/52/56 on the PC, while the lit beach matches (107 against 108). The shaded cliffs (lightmaps,
not models) are darker on the PSP too (31/30/16 against 40/36/37), so part of the gap is not the models'.

**Tuning (in test):** keep numbers `modelambientN` and `modellightN` scale the models' ambient light and
distant lights by N percent (`psp_ge_model_begin`; 100 when absent; read once, without a log line, since
the Media Engine may be first to ask). An emulator batch at the opening tries 150 and 200 against the
PC's frames; the default follows from it. The fold itself (`ambient_with_reflection`) is model-generic:
any lit, textured model part with a reflection cube gets it, so the menu ring's hull can take it as is.

**lights4 [E]:** `modelambient150/200`, `modellight150` and both move the world's models (up to 11k
pixels differ) but leave the first-person rifle at 27/36-37/38-39 against the PC's 46-47/51/54-55: the
rifle isn't lit through the scaled path, and its gap is hue as much as level (red 0.59 of the PC's, green
and blue 0.70), as on the shaded cliffs (a level surface: 31/29/15 against 39/36/36). The default stays
100 (no gain matches the PC); next is the first-person weapon's lighting, then the shared hue gap.

## H9: a10's floor glass a black pit

**What:** level glass with no diffuse map draws its reflection: the cube map's patch, added, tinted
between its perpendicular and parallel colours by the angle it's seen at. Keep word `glassnoreflection`.

**Why [D]:** that glass had no diffuse map, and a see-through shader with texture key 0 was never drawn,
so the dark pipes under it showed through untouched.

**Key lines:** `psp_fixups.py`, FIXES_118 `glasslit` (the type 8 branch); `psp/src/psp_ge.c`,
`psp_ge_seethrough_shader` (`SEETHROUGH_ANGLE_TINTED` for those shaders). Needs a bake with cube patches.
Not yet drawn: the glass's own tint pass (the frame times its tint colour).
**Evidence:** not yet [E]: two emulator A/Bs never reached the glass. The first moved the player while
seated in the opening's cryo tube. The second took the player out of the tube first (`unit_exit_vehicle`),
but its first teleport still landed in the tube's frame (the player stood at the tube's position plus the
spot, the camera left in the tube: a black frame), and by the second spot the opening's "look around" test
was still on screen with no HUD and a black corridor: the cryo look tests gate the scene, and a stick that
only moves up and down doesn't pass them. Base and `glassnoreflection` frames were byte-identical (both
black), so there is no verdict. **Cost [E]:** nothing measurable over a10's opening (3.9 / 3.8 ms a frame).

**Third attempt [E]:** the stick swept right, left, up and down (two cycles, from 225 s) passes the cryo
look tests: the prompt moves on to "exit the cryo-tube", the HUD comes up, and the maintenance tunnel
(H23's spot, the hand test's camera) draws with the flashlight. The glass spot itself drew black after
`switch_bsp 1`: the glass is in another structure. Next run holds the glass spot through BSPs 0, 2, 3
and 4 in turn, base and `glassnoreflection`, to find it and give the A/B.

## H23: dark hairlines along seams under the flashlight (restoration, on by default)

**What:** the spotlight's split of large triangles (build 103) is decided per edge, not per triangle.
`light_edge_splits(a, b)` asks whether an edge needs halving from the edge alone (its length against
half its nearer end's distance to the light, at least a fifth of the radius), a test symmetric in its two
ends bit for bit; `light_split` halves the longest edge that needs it, `(pick, m, other)` and
`(m, next, other)` in the parent's winding, and again in each half, six halvings at most (the old three
levels of four: 64 pieces).

**Why [D]:** the old split chose per triangle (its longest edge against its nearest corner) and cut all
three edges at once, so two neighbours could split their shared edge to different depths: one's long edge
met the other's halves, a T-junction, which the GE rasterises with hairline gaps where neither covers a
pixel. Under an additive light those gaps are dark lines along the seams, only in the flashlight's pool,
as the hand test saw. Per edge, both triangles cut the shared edge at the same float midpoint
(`0.5f * (a + b)` is the same from either end), so their pieces meet corner to corner.

**Keep word:** `lightsplitold` (the per-triangle split); `nolightsplit` (no split, as before build 103).
**Evidence:** [D] so far; the emulator A/B on a10's corridor (flashlight on: new against `lightsplitold`)
ran but gave no verdict [E]: both frames were the same black picture with the opening's look-test prompt
(the player was placed but the cryo look tests were not passed, so the scene stayed gated and the
flashlight showed nothing). Asked again with a stick sweep that tries to pass the tests; a b30 corridor
A/B of the unlifted change with the flashlight on is in the same batch. **Cost:** to be measured (piece
counts are in the stats line as split pieces).

## H33: a bright line along a corner seen edge-on (restoration, on by default; the cause is in round four)

**What:** in light mode 2 (the default), a whole triangle kept as the level drew it (no near-plane cut,
no split) is drawn without the depth lift, in a seventh band at depth offset 0; cut and split pieces keep
their lift. `light_exact_of[]` marks them in `light_emit`; the band loop sends them to band `LIGHT_BANDS`.

**Why [D]:** mode 2 lifts the light toward the eye (1% of the distance, up to 0.2 units) so that pieces
with new corners still land on the surface. A whole triangle has the base pass's own corners and lands on
its exact depths, as the lightmap pass does, so it needs no lift. Lifted, a face seen edge-on (90 degrees to
the view, facing the light) passed the depth test over the face in front of it along their shared corner:
a one-pixel bright line, solid or dashed, the full height of a pillar (b30's interior, shots of the hand
test at 2600 and 2674 s).

**Keep word:** `lightliftall` (every triangle lifted, as before). The light pass's stats line now counts
the unlifted triangles ("unlifted"). **Evidence:** [D]; the emulator A/B at b30's spots (new, both old,
`nolightpass` to show whether the light pass is the source at all, `stencilsnudge` for the stencil layers)
is queued. **Cost:** to be measured (the same band loop, one more band).

## H41: a light through a wall (restoration, on by default; fixed)

**Evidence [E]:** at the hand test's spot (the same camera), the emulator draws the same green glow on the
wall. `nowidgetsprites` removes it (3,641 pixels differ, all in the glow's square); `nolightpass` changes
nothing there. So it is a widget sprite: a lens flare, drawn without the depth test, whose ray test
answered "visible". The bunker door's two green lamps in H42's shot shine through the wall the same way
once the door has slid into it.

**What:** the flare test's level-only ray now runs to the flare's own point (all but 2 cm). The full test's
ray stops 0.15 short so as not to meet the flare's own object (characters in a flare's way); the level-only
test shared that shortened ray, so a wall within 0.15 of a lamp on its far side never hid it. The Xbox's
test square is depth-tested at the flare's point, so any wall short of it hides it.

**Keep word:** `flaresshort` (the shortened level ray, as before); `flaresdebug` logs the first 40 rays
(point, distance, whether the level hides it).

**Verdict: fixed [E].** At the hand test's camera the glow is gone: new against `flaresshort` differs in
exactly the 3,641 pixels `nowidgetsprites` removed, dark where the old drew green (mean 9/23/22 against
10/56/31). Every other shot of the run, the beach's eight included, is pixel-identical, and the first 40
rays (all beach lamps) answer "level clear" both ways: no flare the level doesn't hide was lost. Over the
run, 295 fewer flare sprites drawn (1,048 against 1,343), 110 more answers "hidden by the level".
**Cost [E]:** none: the same 3,088 rays cast both ways; frame 29.47 against 29.48 ms over the run.

## H42: the bunker doorway

**Evidence [E]:** the emulator reaches the spot, but the door there is closed (the hand test saw it open),
so the doorway's own lighting can't be compared yet. Full-size lightmaps (no `assettrack`) against the
half-size ones differ only in detail at all four interior spots (mean absolute difference 1.2-3.9/255,
mean brightness within 0.4): the half-size lightmaps are not the fault. The green glows beside the open
door in the hand test's shot are H41's. Open: the dark wedge inside the open doorway, against the PC.

## H33, read again: the line is not the light pass

**Evidence [M][E]:** in the hand test's log the light pass drew nothing at H33's shots (0 triangles, every
stats line from 2,560 to 2,700 s), and the emulator draws the same dashed line at the same camera with
`nolightpass` (only the clock's pixels differ) and with every light-pass keep word. The line (8-pixel
dashes every ~31 rows, colour 115/149/148) is on a pillar's face seen edge-on. The light pass's unlifted
triangles stay (exact, `lightliftall` turns them off) but fix nothing seen.

**Round two [E]:** none of `clearmagenta`, `nomip`, `nodetail`, `guardwide` removes the line.
`clearmagenta` changes no pixel (so no crack shows the cleared screen), `guardwide` 19 pixels (the guard
band's cuts aren't it), `nodetail` leaves the dashes (44 of 46 pixels). `nomip` keeps the dashes in the
same places but brighter and whiter: they sample a mipped texture, so they are drawn surface, not a hole.
Candidates left: a see-through or decal surface along the corner, a crack showing a surface behind, or
depth precision between the corner's faces. Asked next: `nooverlays`, `nodecals`, `nocull`, `near4`.
Not yet compared with the PC: its runner can't place the camera at a given spot.

**The unlifted light triangles under the flashlight [E]:** with the flashlight on through the same
interior spots, new against `lightliftall` changes no level pixel (0-46 pixels, the clock); only the
first-person rifle differs (new slightly brighter, 42/51/53 against 40/47/50 where they differ). Cost:
frame 13.66 against 13.55 ms over game time 3,000-5,000 (+0.11 ms, +0.8%), tick unchanged. Whether to
keep it waits on H23's tunnel A/B (`lightliftall` is in it): if it shows nothing there either, it costs
time for nothing and should go off by default.

## H24

Waits on H6's A/B.

## Round three (one emulator batch, the same build)

**H33: the line is a level overlay [E].** At the b30 corner shot, `nooverlays` alone removes the dashes
(46 of 46 dash pixels gone; 257 pixels differ from the base in that shot); `nodecals`, `nocull` and `near4`
keep all 46. So it is a see-through surface on the level (a generic or chicago shader), drawn with the
overlay's nudge toward the eye, not the base pass, a decal, the level's culling or depth precision.

**Fix (restoration, on by default):** level see-through draws whose shader (generic or chicago) lacks the
two-sided flag (bit 2 of the flags byte at +41) now draw front faces only, with the level's own winding,
as the Xbox's environment passes cull (D3DCULL_CCW). The GE drew them both sides; with the nudge, a back
face at a silhouette corner passed the depth test over the front face beside it. Only the level's own
triangles and their cut pieces are culled; a bake's stencil layers keep both sides. **Keep word:**
`overlaysnocull` (both sides, as before). **Key lines:** `psp/src/psp_ge.c`, `SEETHROUGH_ONE_SIDED`
(set in `psp_ge_seethrough_shader`, used in `transparent_flush_body`); the "see-through depth" stats line
counts "level draws front faces only". **Evidence [E], proof run read: the fix does NOT remove the line.** At
b30's corner shots (011, 012) the dashes are the same 46 of 46 pixels with the fix and with `overlaysnocull`
(the two shots differ by 426 and 1,800 pixels, none of them the dashes). The cull is active: the stats line
counts 289 to 3,842 level draws front faces only per 300 frames in the fix run, 0 in `overlaysnocull`. So the
dashes are drawn by a surface that faces the eye (or by a shader with the two-sided flag), not by a back face.
**What `tdump` shows at shot 011:** the see-through draws that frame are the light strips (`light bright
overhead`, `light dim blue`, `light dim white`, `light bright white`, bitmap `lights small strips`) and `glow
tech` (`alpha grate`), all level draws with the one-sided flag (depth mode 512 or 528), so culled; and the
two-sided ones, `light fixture vol` (depth mode 16, `light fixture ceiling1`, 78 triangles, at about 13 from
the eye, in the camera's direction), `holo control` and `holo control warning`. `tdump` lists a frame's
draws, not where on the screen each lands, so the shader at the dashes is not named: candidates are the
two-sided `light fixture vol` (a volume's cone seen edge-on, both sides with the nudge) and the front faces of
a light strip or `glow tech` at the pillar's edge. **Status: H33 NOT fixed.** The cull stays (it is the
Xbox's rule and costs nothing) but is not the cause. **Next [U]:** a keep word that skips see-through draws by
shader-name substring (one per candidate) in one batch, or `tdump` with each draw's screen bounding box.
**Other overlays [E]:** across the 17 shots of the run the new build differs from `overlaysnocull` only in
moving things (the plasma bolt, foliage, the rifle's sway, water) and the clock: no overlay has disappeared
(shots 7 and 14 looked at; shots 0-4 identical). **Cost [E]:** frame 6.48 ms against 6.84 ms (game time
3,000-5,000, 7 and 6 heartbeats; tick 2.54 against 2.67): no cost, within the spread. Multiplayer build of
this code compiles (`PSP_MULTIPLAYER=1`, 5.94 MB, EBOOT d4fff1451e541484).

**H23 and the unlifted band [E].** At the hand test's tunnel camera (shot 031, the same position and
direction to two decimals), the emulator's flashlight never takes the split: 0 triangles split in all three
runs (new, `lightsplitold`, `nolightsplit`), where the hand test's log split about two triangles per cone
draw over the same seconds [M]. So the emulator can't show the split's seams here: new and `lightsplitold`
differ only by the clock and a ripple of one or two levels in the falloff's bands (the shot falls a tenth of
a second apart). `lightliftall` (every triangle lifted, as in the hand-tested build) draws a dashed bright
line along a diagonal seam (x 320-345, rows 162-188: 32 against 56-61) that new and `lightsplitold` don't:
the same signature as the hand test's shot, which shows bright dashes along the tunnel's seams (66-69
against 24-32 around them), not dark lines. **Verdict:** the unlifted band fixes the lift's share of H23 in
the emulator, so it stays on by default (its 0.11 ms a frame buys this). Split pieces are still lifted, so
on the PSP, where the tunnel does split, dashes may remain along split pieces: that needs the PSP (new
against `lightliftall` and `lightsplitold` at the tunnel). The per-edge split itself is [D] only.

**H9: the glass is in BSP 2, and the fix doesn't reach it [E].** Held at the hand test's camera (shot 018),
BSPs 0, 3 and 4 draw black; BSP 2 draws the hand test's scene: the floor and a black pit. Base and
`glassnoreflection` are pixel-identical there, and the frame counts 0 see-through draws (the hand test's log
too, at that shot [M]): the glass isn't queued at all, so the reflection path never runs. Open: why the
glass (type 8, no diffuse map) never reaches the see-through queue at that spot.

## Round four: H33 found and fixed (two emulator batches)

**Which draw [E].** At b30's corner shot, leaving out one see-through map at a time (`tskipN`, N the map's
address from `tdump`) names it: without `lights small strips` (the light strips' bitmap: `light bright
overhead`, `light dim white` and the rest) the dashes are gone (0 of 46 pixels); without `alpha grate` (glow
tech), `light fixture vol` or `holo control` all 46 stay. The nudge settles it: `overlaynoslope` (no slope
part) and `overlayslope1` (half of it) also remove all 46; `overlaynowall` keeps them. So the line is a light
strip on a surface behind the pillar, front-facing but seen nearly edge-on, drawn in front of the pillar by
the slope part of its nudge. That matches the hand note: a stencil showing through along its thinnest edge,
seen at 90 degrees.

**Why [D].** The slope part is two pixels' worth of the triangle's depth change across the screen. Edge-on
that change is thousands of depth steps a pixel, and the lift limit that would hold it (`nudge_percent`) is
off by default, so an edge-on strip moved most of the way to the eye (one strip draw went out at 1,521 steps
where its neighbours had 15 to 40). Each strip covers a pixel or so edge-on, which is why the line is dashed.
The Xbox draws the level's overlays with no depth bias at all (`D3DRS_ZBIAS` 0 in the environment passes), so
the original never shows one through what stands in front of it.

**Fix (restoration, on by default).** `slope_units_capped` drops the slope part for a triangle whose normal
is within a cosine of 0.10 of square to the view (about 84 degrees and beyond); the distance part and the
3-step floor stay. It covers the level's overlays and the decals (both callers). **Keep words:**
`nudgeedge0` keeps the slope part at every angle (as before); `nudgeedgeN` sets the cosine in hundredths.
The "overlays" stats line adds "dropped edge-on N".

**Evidence [E], one batch, the same build:**

| Run | Dash pixels (shots 011, 012) | Other shots against the fix |
|---|---|---|
| fix (cosine 0.10) | 0, 0 | - |
| `nudgeedge0` (as before) | 46, 46 | 0-9 scattered pixels, edge-on specks |
| `nudgeedge5` | 0, 0 | identical, all 17 shots |
| `nudgeedge20` | 0, 0 | 3-4 pixels in 5 shots |

a10's lights route (24 shots) against `nudgeedge0`: identical but for 11-13 scattered pixels along one
edge-on line (shots 8-14) and a HUD prompt's fade a tenth of a second apart; no symbol, stencil or decal
lost. The drop is common (in one 300-frame window on b30, 40,570 of 76,005 slope nudges were edge-on: the
beach's ground overlays at a distance), and the shots show no overlay sinking into its surface for it.
0.10 is the middle of three settings that all fix the spot; 0.05 and 0.10 give the same 17 shots.

**Cost [E]:** b30 game time 3,000-5,000, frame 6.83 ms against 6.84 (tick 2.66 against 2.67); a10's route
24.37 ms both. None.

**Status: H33 fixed [E].** The PSP check is the hand test's own spot on the next build. The one-sided cull
of round three stays: the Xbox's rule, free, and harmless across both maps' shots.
