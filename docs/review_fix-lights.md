# Review: fix-lights (b139's hand test: H6, H22, H9, H23, H33, H41, H42, H24)

Branch `fix-lights`, off `w139`. Every change has a keep word that turns it off. Evidence labels as in the
README: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.

Status: the code compiles file by file in both builds [D]; the full builds, the bake and the emulator
A/Bs are below as they come in. Costs are game-time comparisons on the same stretch in one batch [E].

## H6: relief streaks on a30's cliffs

**What:** relief (the bump map's shading baked into a second copy of the base texture) is now off by
default; keep word `relief` turns it on. The bake's relief is fixed too, so `relief` is worth testing again.

**Why [U]:** the cliffs' bump maps tile far finer than their base maps. The bake sampled one bump texel per
base texel, unfiltered, so the bumps aliased into stripes, and the cliffs' stretched UVs drew those as
streaks.

**Key lines:**
- `psp/src/psp_tex.c`, `psp_relief_key`: `on = psp_keep("relief") && !psp_keep("norelief")`.
- `tools/texpack.py`, `apply_relief`: the bump is box-filtered over each base texel's footprint before its
  shading is taken; relief is left out (logged "bump relief left out for ...") where a base texel covers
  more than `RELIEF_MAX_FOOTPRINT` (4) bump texels, since a filtered bump that fine is flat anyway.
  `--relief-unfiltered` bakes the old way, for A/Bs.
- `psp_fixups.py`, the level detail scale: a relief texture lives under its base's key `| 2`, so the
  base's size is read from `base_key & ~3u`, not from the relief key (it read the wrong header before).

**Cost:** off by default saves relief's texture fetches and memory [E: below]. **For the owner's call:** whether
the filtered relief goes back on (A/B: a30 cliffs, relief on the new bake against off and the old bake).

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

**Cost:** no extra pass; one division per textured part [E: below]. It restores the original's look, so
it is on by default. Tuning against the PC reference: below.

## H9: a10's floor glass a black pit

**What:** level glass with no diffuse map draws its reflection: the cube map's patch, added, tinted
between its perpendicular and parallel colours by the angle it's seen at. Keep word `glassnoreflection`.

**Why [D]:** that glass had no diffuse map, and a see-through shader with texture key 0 was never drawn,
so the dark pipes under it showed through untouched.

**Key lines:** `psp_fixups.py`, FIXES_118 `glasslit` (the type 8 branch); `psp/src/psp_ge.c`,
`psp_ge_seethrough_shader` (`SEETHROUGH_ANGLE_TINTED` for those shaders). Needs a bake with cube patches.
Not yet drawn: the glass's own tint pass (the frame times its tint colour).

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
**Evidence:** [D] so far; the emulator A/B (a10's maintenance corridor, flashlight on: new, `lightsplitold`,
both old) is queued. **Cost:** to be measured (piece counts are in the stats line as split pieces).

## H33: a bright line along a corner seen edge-on (restoration, on by default)

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

## H41: a light through a wall; H42: the bunker's lighting

Not yet decided: the A/Bs that tell the causes apart (`nowidgetsprites` and `nolightpass` for H41, a run
without `assettrack` for H42) are queued at the hand test's spots. The first attempt never reached them:
those spots are in b30's interior BSP, and a route that teleports there needs `switch_bsp 1` first.

## H24

Waits on H6's A/B.
