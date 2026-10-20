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

## H23, H33: bright seams along edges; H41: a light through a wall; H42: the bunker's lighting

Under test; A/B keep words added so the emulator can tell the causes apart without hands:
`stencilsnudge`, `stencilsslack` (the stencil layers' depth modes), `nolightpass` (the dynamic light pass
not drawn), `nowidgetsprites` (lens flares and glows left out). H42 is tested with and without
`assettrack` (half-size lightmaps).

## H24

Waits on H6's A/B.
