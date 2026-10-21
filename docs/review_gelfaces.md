# Review: gelfaces (a light's gel by its six cube faces), for b140

Branch `gelfaces`, off w139 (it needs fix-cortana's gel average, `nogel`, `lightdump` and `flycam`). It does not ship in b139: it goes into b140 through the w139 -> w140 merge. Evidence labels as in CLAUDE.md.

## What the Xbox does [D]

`_rasterizer_environment_diffuse_light_begin` (rasterizer_xbox_environment.c) binds the light's gel, a cube map, on stage 1 and turns its frame each frame: R = rotation from yaw, pitch and roll, each `periodic_function_evaluate(function, game_time / period) * 2pi`; the gel's forward = R * the light's forward, up = R * the light's up, left = forward x up. The environment light vertex shader (decoded with the engine's own nv2a translator) looks the gel up at (dot(P - L, forward), dot(P - L, left), dot(P - L, up)): the cube's +X face is the gel's forward, +Y its left, +Z its up, in D3D face order (+X, -X, +Y, -Y, +Z, -Z), the order the bake already reads the faces in.

fix-cortana lit a gel light by the mean of all six faces: right on average, but every wall around the light got the same share, whichever faces its direction actually sees.

## The change (keep word `gelfaces`; off: the single average, as now)

**The bake (`tools/texpack.py`).** `gel_faces` reads each face's top level and takes its mean. `cube_patch` puts the six in palette entries 1-6 of the gel's 8 x 8 patch, with `0xFACE` in entry 15 marking them. Every texel still points at entry 0, the six-face mean, so the patch draws and averages exactly as before, and the old reader (`psp_tex_average_known`) and `gelfaces` off see no difference. `--no-gel-faces` leaves the faces out. Only gel bitmaps (the ones lights alone use) change.

**The reader (`psp/src/psp_tex.c`, `psp_tex_gel_faces`).** It returns the six face colours (5650 -> 0-1) when entry 15 carries the mark. It keeps 16 gels by key, so a gel that has left memory isn't brought back every frame.

**The light begin (`psp_fixups.py`, `FIXES_MODELS` `#lightgel` and `#gelframe`).** For a cone-less gel light with `gelfaces` on and faces in the pack, `psp_light_gel_frame` works out the gel's frame the way the Xbox's begin does, with the engine's own `periodic_function_evaluate`, `matrix4x3_rotation_from_angles`, `matrix4x3_transform_normal`, `cross_product3d` and `normalize3d`. It reads the functions and periods from the definition (+0x8e/+0x90 yaw, +0x96/+0x98 roll, +0x9e/+0xa0 pitch, after the gel's map and secondary map references: the engine's `point_light_gel_parameters`). The light then keeps its full colour, and `psp_ge_light_gel` hands the GE the six faces and the frame. Any period that isn't set falls back to the single average. `lightdump` also logs the faces and the frame.

**The GE (`psp/src/psp_ge.c`).** `psp_ge_light_gel` is a setter after `psp_ge_light_begin` with its own deferred record (`DEFER_LIGHT_GEL`, 27 floats), so the begin's 15-word record stays as it is. The begin clears `light.gel`, so a light without a gel never carries one over. For a gel light, each lit corner's colour comes from `light_gel_at`: d = vertex - light in the gel's frame, each axis weighted by d_i^2 / |d|^2 on the face of d_i's sign (the weights add up to 1). The reach per channel goes in the cut corner's floats 5, 6 and 10, which the clipper already interpolates, and is stored apart from `light_memo` (`light_memo_gel`, by the same draw-and-vertex tag) so other lights don't carry it. The whole-triangle path (light mode 1) takes the mean of the corners' colours, flat. A gel light is never a spotlight (spotlights have cones), so the surface tint path is unchanged.

The weighting is the blend that gives a face's own colour straight along its axis and splits evenly at the cube's diagonals. A cube map sampled per pixel would show each face's pattern. This is the per-vertex light pass's resolution: the beams' level per direction, turning with the gel. It is not the beams' edges.

## Hot code

Changed functions the w140 profile counts: `psp_ge_light_draw_body` (one test of `light.gel` per triangle; the gel block only for gel lights), `light_emit` (one test per kept corner), and the light colour loops (one test per draw). `light_reach`, `light_split` and `light_emit_spot` are unchanged. The new `light_gel_at` and `light_color_gel` only run for gel lights. Expect the profile to miss counts for the three changed functions until it is regenerated.

## Proof

See the findings rows for this branch (gelf1 batch). It covers the excess red on the alert hallway's walls with `gelfaces` on, off and `nogel` against the PC's 4.19 levels a pixel, at Cortana's spot (the PC: ~0), and the light pass's cost on a10 and b30 under the cache model.
