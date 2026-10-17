# Review: the self-illumination glow maps and the main menu showcase (branch `glowmaps`)

Item 1 of `main_menu_restoration.md`: the main menu's white band across the ring, and every self-illuminated model part drawn in its flat colour. A restoration, so on by default; keep word **`glowflat`** draws the old pass. Labels: [M] PSP, [E] emulator, [D] code or data, [U] inference.

## What the Xbox does, and what the port did

- **Xbox** [D: `rasterizer_xbox_models.c`, `set_environment_shader_pixel_shader`]: combiner stage 0 takes the multipurpose map's green (the self-illumination mask), stage 1 adds it × the shader's colour to the lighting, and the later stages multiply the lighting by the base map. A lit-up area shows **its own base texture** at full brightness.
- **The port until now**: the colour × the mask, added straight onto the frame (`multipurpose_pass(1, ...)`). With the ring's white colour, a white band; elsewhere a flat wash of colour.
- **Now**: the bake writes a **glow map** per self-illuminated model shader (the base × the green), and the pass draws that map × the colour, added. The sum is base × lighting + base × green × colour = base × (lighting + green × colour), the Xbox's formula, short of the clamp.

## The changes

### `tools/texpack.py` (the bake)

- `glow_looks(m, B)`: every soso shader with a multipurpose map, a period and a colour at either end (the same test the runtime uses to give it a colour). Keyed by **the shader's tag address | 2**: bitmap keys are 4-aligned (`| 1` is the detail-folded base), so `| 2` can't collide with any.
- `apply_glow(img, multipurpose)`: base RGB × green, opaque. The green is resized **on its own**: PIL resizes RGBA premultiplied, so a green under the detail mask's zero alpha would come out black.
- The glow map is baked by `bake_one` from the base, at the base's size and class, with the shader's detail map folded in when the GE draws the folded base (`| 1`).
- Shaders with the same base and multipurpose map **share one copy of the texels** (two directory entries, one offset).
- The census goes to the bake's log: a `glow map for <shader>: …` line each, and a `glow maps: N shaders, M maps, B bytes` total (`bake.py` echoes lines starting `glow `).
- `--no-glow-maps` bakes the pack as before.

### `psp_fixups.py` (the model hook)

- `psp_ge_model_multipurpose` gains the glow map's key, `(unsigned int)definition | 2`, for any model shader with a multipurpose map.

### `psp/src/psp_ge.c`

- The deferred call (`defer_model_multipurpose` and its replay) carries the extra word.
- `multipurpose_pass(1, ...)`: when the pack has the glow map and `glowflat` isn't kept, `glow_map_pass` draws it: `GU_TFX_MODULATE` by the colour, added (`GU_FIX` 1/1), depth-equal over pass 1, fogged to black, as the old pass.
- A glow map not yet in memory (the Media Engine's binds only ask for it) leaves the part unlit for that frame, rather than flashing the flat colour.
- The GE's frame line counts `(N with glow maps)` beside the self-illumination passes.
- A pack without glow maps (every bake before this one) draws exactly as before.

## Cost

- **Frame**: none expected; the same pass with a different texture, and no palette worked out per draw [U; checked in the emulator].
- **Memory**: one texture per distinct (base, multipurpose) pair, at the base's baked size (soso: 128 px at most, 4 or 8 bits). The bake log's total says how much per map.

## The detail masks read on their own (a restoration found on the way)

- `apply_model_look` (the detail map folded into a model's base at bake) resized the multipurpose map as RGBA with PIL when its size differed from the base's. PIL resizes RGBA premultiplied, so a mask in the red, green or blue channel came out **0 wherever the map's alpha is 0**, and a few percent dark where it's low: the alpha is the map's own detail mask, not a transparency [D, and a test of PIL].
- Where that happened, a plain mask left the base without its detail (too bright, under a darkening detail map), and an inverse mask put the detail on in full.
- Now `channel_at` resizes the one channel it needs, for the detail mask and the glow maps alike. It's a restoration at no frame cost (the bake only), so it is on by default. `texpack.py --premultiplied-masks` bakes the masks as before.
- The bake's log names each shader the fix changed, with the share of its texels (`detail mask read on its own for …`), and a total (`detail masks: N shaders`).

## The main menu as the hardware's showcase (Sheepdog01, 2026-10-16)

The rule (Sierra-III `CLAUDE.md`): ui.map is a high-fidelity showcase, exempt from the frame-time limits on restorations, as long as nothing it does costs the other maps. So everything below takes effect on ui.map alone, and **`noshowcase`** turns all of it off.

- **Its own pack, `ui_showcase.ptex`** (`texpack.py --showcase`; `bake.py` writes it beside `ui.ptex` for ui only):
  - every map at its own size, up to the GE's 512 a side (no halving, no 128 px object cap, no 16-colour downgrade);
  - objects, sky and level maps in **32-bit colour** (`GU_PSM_8888`, no palette), their levels swizzled as the 8-bit ones are. The glow maps and the detail-folded bases too.
  - Kept on palettes: multipurpose maps (the change colour's mask reads their palette), lightmaps (the object lighting samples them), the HUD and menu sheets (meters), detail maps (the detail pass's grey). Their size still rises to their own.
  - The bake's log lists every 32-bit texture with its size (`showcase: …`).
- **`psp_tex.c`**: a pack candidate before the others, `<map>_showcase.ptex`, tried only when the map is `ui` and `noshowcase` isn't kept. A stick without it falls back to `ui.ptex`.
  - `psp_tex_bind` draws a texture in its own colours: no palette loaded, the buffer width in pixels from 2 or 4 bytes a texel (`DIRECT`, `TEXEL_BYTES`).
  - The palette readers (`psp_tex_palette`, `psp_tex_palette16`, `psp_tex_average`) pass such a texture by (its `clut` byte is 0xFF), so the meters, masks and the flashlight's average fall back as they do for a texture with no palette of the kind they want. The lightmap samplers already took only 8- and 4-bit textures.
- **`psp_platform.c`**: `psp_showcase_map`, set as each map opens (ui and not `noshowcase`).
- **`psp_fixups.py`** (`render_objects.c#lod`): on the showcase map every model takes its most detailed level, as the game already does for a cinematic's stars (`REAL_MAX`).

Not yet: a 32-bit frame buffer (the screen is 16-bit everywhere; the space backdrop's gradients band), the hull's detail pass, the reflection pass.

## Checks

| Check | What it shows | Result |
|---|---|---|
| Bake `glow135` (ui, a10, b30; lab 38988879171) | the census; the packs build | ok. ui: 3 shaders, 3 maps, 29 KB (both ring shaders, the plasma grenade's button). a10: 43 shaders, 36 maps, 420 KB. b30: 54 shaders, 45 maps, 555 KB. Detail masks changed by the fix: 0 shaders on these maps [D] |
| `glow1` menu (lab 38991916734) | the ring | the inner ring shows its land, sea and cloud, brighter than with no pass; no white band. The hull stays dark metal (its base is dark, so base × green adds little). 9000 passes, all with glow maps [E] |
| `glow1` a10, b30 | the levels | differences are small and local: the ring in a10's opening is textured, not a white arc; the assault rifle's display adds a little over its base where glowflat washed it flat blue [E] |
| Frame time, same runs | cost | menu 2.2 ms all three ways; a10 and b30 within the runs' own spread (a10's noglowpass run was as slow as the glow-map run) [E] |
| `PSP_MULTIPLAYER=1` (glow1, mp-bloodgulch) | shared code builds and plays | ok: built, ran clean on bloodgulch with mp134's packs (no glow maps: the old pass, 0 with maps) [E] |

## Still open

- **The ring's textures are at the object cap:** the inner ring reaches the PSP at 128 × 8 px, the hull 128 × 64. Item 2 of `main_menu_restoration.md` (the carve-out) is what gives the ring its detail back.
- **The Xbox clamps lighting + self-illumination at 1 before it multiplies the base;** the port's sum can go past the base where a lit part is also in strong light. Not seen in the shots [U].
- Judged against the formula, not a reference frame: the PC rig's frames (item 5) would settle it.
