# Visual quality pass: preliminary recon (2026-10-04)

What the GE renderer leaves out against the Xbox, with where it is in the code, a rough cost, and a fidelity gain. For the pass after tuning. Costs are estimates unless marked.

## Level

| Omission | Xbox | PSP now | Cost to add | Gain |
|---|---|---|---|---|
| Specular lightmaps and reflections (`structure_render_specular_lightmaps`, `_reflections`, `_reflection_lightmap_masks`: skipped passes) | Cube-map reflections on metal and glass, specular highlights | None | Cube maps aren't baked (texpack skips `cube`); the GE has no cube-map texturing, so it would be an approximation (a 2D environment map per view) at one more pass per reflective surface | Medium indoors (bases, the Library), low outdoors |
| Detail objects (`structure_render_detail_objects`: skipped) | Grass tufts and shrubs billboarded over the ground | None | Sprites from the detail-object tags: cheap GE work, CPU placement per cluster; a density knob for 272 lines | High on Blood Gulch's grass |
| Fog screen (`structure_render_fog_screen`: skipped) | A screen-space fog layer for planar fog volumes | Only the GE's per-vertex fog | One screen quad per fog plane crossed | Low on Blood Gulch, matters in water levels |
| Bump maps (texpack skips `bump`) | Bumped shaders (`senv` with bump) | Flat | The GE has no bump mapping; could be baked into the lightmap direction (fake) | Low |
| Lightmap mips | None on the Xbox (build 49 matched this) | Matched | – | – |

## Models

| Omission | Xbox | PSP now | Cost to add | Gain |
|---|---|---|---|---|
| Model lighting | Per-pixel-ish: ambient + two directional from the cluster's lights, plus a reflection cube map and specular by the multipurpose map's red | GE lights: ambient + directional diffuse (`model_lighting`), Gouraud since b62 | Specular: the GE has a specular light type; one more `sceGuLight` and a material setting | Medium: the Master Chief's armour reads flat |
| Model detail maps (`soso` detail) | Multiplied in per pixel | Folded into the base texture at bake (average colour) since build 36 | A second pass per part (as the level's detail passes): +1 pass × parts | Low–medium up close |
| Flashlight gel and falloff textures | Cube-map gel, falloff by distance texture, lit through the surface's own texture | Plain cone, additive, with the texture's *average* colour (`psp_ge_light_*`) | One extra textured pass per lit surface under the cone (multiply instead of add): the GE has one texture a pass, so two passes per lit draw. Also "never darkens" (open) | Medium in caves and the Library |
| Lens flare white-hot core (`psp_ge.c` ~2890: left out) | Texture^6 × tint | None | One more sprite pass with a higher-power texture | Low |
| Skinned models at the near plane | Full | Triangles dropped (not cut) when a corner reaches past the guard band | CPU skinning for parts near the eye | Low (rare) |
| Active camouflage | Refraction of what is behind | Faint see-through model (build 30) | A copy of the frame to a texture, sampled with an offset: expensive on the PSP (readback) | Medium in multiplayer |

## Particles, effects, weather

| Omission | PSP now | Note |
|---|---|---|
| Particle caps for 272 lines | `fullparticles` keep word restores the full counts | A fidelity knob, left as is |
| Contrails, weather | Drawn? (`weather_particle_systems_render` appears in the profile) | Not inspected this pass |
| Decals under 2 pixels | Left out (b57) | Deliberate, against blinking |

## Texture bake

| Item | Now | Possible |
|---|---|---|
| CLUT 256 / 16 colours per texture | 8-bit for level and 4-bit where it is within 1 dB of the frame buffer | Keep |
| Resolution: top mip dropped for ≥ 64 px, 512 max | | A per-class knob; the PSP's 480×272 rarely shows 512 texels |
| Cube, 3D, bump skipped | | See above |
| Detail mips from the Xbox chain | Since pack 59 | Keep |

## Order suggested for the pass

1. Detail objects (grass): the biggest visible difference on Blood Gulch, cheap on the GE.
2. Flashlight: multiply through the surface texture, and fix "never darkens".
3. Specular on models.
4. Model detail maps as a pass, behind a keep word (cost).
5. Reflections as a 2D environment map, indoors only.

Each needs a cost measurement on the PSP first; the frame budget is the constraint (see `engine_performance_b66.md`).
