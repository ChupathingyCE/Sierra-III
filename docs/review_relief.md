# Review: the baked bump relief (`relief`)

Sierra-III `relief` on `w139` (786ad08). Sheepdog01's decision (2026-10-17 16:17): the flat walls get their bump maps' relief baked into the base maps, no runtime cost; a restoration, on by default. Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## Why the walls were flat [D]

The Xbox lights a level's bump map per pixel: each texel's normal against the lightmap's incident direction. The GE has no per-pixel lighting, and the bake dropped every bump map (`bitmap_bake.target`: class `bump`), so ship walls that take their look from the bump came out as smooth lightmap gradients (Sheepdog01's b138 hand tests: a10's tunnels, a30's lifepod ramp).

## What the bake does now (`tools/texpack.py`)

- `relief_looks`: every senv shader with a bump map. Offsets from `rasterizer_xbox_environment.c`'s `shader_environment_diffuse_properties` (at +108): bump scale +292, bump map +296 (index +308), runtime scale (u, v) +312.
- `apply_relief`: the base map at full size times `dot(normal, light) / light.z`, the bump sampled where each base texel falls on the tiled bump map (bilinear, wrapped), clamped to 0.35–1.65. A flat normal leaves the colour as it was. The light is one fixed direction (`RELIEF_LIGHT`, up and to the side, an emboss): a texture shared by many surfaces can't take each surface's own lightmap direction [U: the look's judge is the side-by-side].
- P8 bump maps (format 17) decode through the engine's vector palette (`bake/lib/vector_palette.py`, copied from ChupathingyCE's `bitmaps.c`, `global_vector_palette`): `bitmap_bake.decode_bump`.
- The relief version is packed beside the plain one, under the base's key | 2 (`(with its bump relief)`); the plain one stays for anything else that draws that bitmap. `--no-relief` bakes without; the log says how many (`bump relief: N level base maps`).

## What the PSP does (`psp_tex.c`, `psp_relief_key`; `psp_fixups.py`)

The three places that pick a senv shader's base map (the level's draws, its dynamic lights, a senv shader on an object) ask `psp_relief_key(key)`: key | 2 when the pack has it and `norelief` isn't kept, else the key. Hot code: one `psp_tex_has` lookup (the pack's binary search) per level draw call and light draw. Memory: none at run time (one version is loaded); the pack on the stick grows by the relief versions (a10: 28 maps, a30: 14).

## Proof

- relief1 (39073284455) [E]: a10's opening, the Pillar's hull: panel lines where `norelief` is smooth; against the PC (pc-chupa 39063014041, tick 1080) a clear step toward it, though the PC's is crisper (its base maps are full size and lit per pixel). Strict 0 sites on a30; the multiplayer build plays Blood Gulch (its packs have no relief: plain maps, as before).
- relief2 [E]: a30's lifepod ramp at Sheepdog01's camera (pending).

## Open

- One light direction for all: panels lit from the "wrong" side on some walls are possible; per-surface direction would need the lightmap's direction per texture use (a later measure).
- The measure of relief, full-size base maps and specular/reflection waits until after the release (Sheepdog01).
