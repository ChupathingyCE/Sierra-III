# Review: texavg, a missing texture drawn as its 4x4 stand-in (branch `master/texavg`, on w142 a735763)

Lever STANDIN (lab `levers.tsv`). Sheepdog01, 2026-11-03: "I like the average color 4x4 most". A look change, so **off by default** behind `texavg` until Sheepdog01 has seen the side-by-side. Labels: [M] PSP, [E] emulator, [D] code or data, [U] inference.

## Why

When a bind finds its texture missing (the RAM and VRAM caches full and "waiting for room", or its load not in yet) the surface is drawn untextured. On b30's bunker approach (game time 5,400-7,200) the caches hold 2,632 KB and the heaviest frame needs ~2,540 KB, so dozens of frames draw something untextured there [M] (lab findings `b30_flicker_psp`, `b30_flicker_emu`). A stand-in in the texture's own colours makes the gap read as a blur that sharpens, not a flash.

## The bake (`tools/texpack.py`)

- `standin(e)`: the texture's top level cut into a 4x4 grid of blocks; each block's average becomes one colour. 16 colours a texture, row order.
- **Format: a 16-colour palette.** 5650 for an opaque texture, 4444 for one with alpha: 32 bytes a texture, the least the GE loads as a CLUT (one 32-byte block). Every stand-in shares one 4x4 CLUT4 index texture (indices 0-15), so drawing a stand-in is loading its 16 colours as the palette. A 4x4 direct texture at 32 bits would be 64 bytes, at 16 bits it would need a 16-byte row (8 texels), so the palette form is the smallest [D].
- **Alpha:** each block keeps its mean alpha and the alpha-weighted mean colour. Halo's cut-outs (foliage, grates) are near 0 or 255 and drawn with the alpha test at half (`sceGuAlphaFunc(GU_GREATER, 0x7F)`), so a block passes where at least half its texels do: a coarse cut-out, not a solid card. Weighting the colour keeps the see-through texels' colour (often black) out of it. Blended textures keep their mean opacity.
- **The table:** `'TSI1'`, a count, then 32 bytes a texture in directory order, right after `'TAV1'`; zeros for a texture in its own colours (the showcase's). A build that doesn't know it reads no further than `'TAV1'`, so the bake format stays 2.

## The game (`psp/src/psp_tex.c`, `psp_ge.c`)

- **Read:** in `psp_tex_map_opened`, after the averages, only when `texavg` (or `texavgall`) is on: `memalign(64, count * 32)`, read, written back from the data cache (the GE reads it). Off, nothing is allocated. A pack without the table logs "no stand-ins in this pack" and draws untextured, as before.
- **`tex_standin`** (cold, not inlined): the 16-colour CLUT load, `GU_PSM_T4` unswizzled, `sceGuTexImage(0, 4, 4, 32, standin_texels)` (a buffer of 32 texels is the GE's least 16-byte row), one level, linear filter. It forgets both bind memos (the GE's texture is no longer the one they name) and counts in the ME's own book when the ME runs it.
- **Where it's called:** the bind's two untextured returns, the ME's (after `want(index)`, so the texture is still read in) and the main CPU's (after the census note). The cost off is one load and test on those paths.
- **`psp_tex_bind_texels`:** the three draws in the GE's 2D mode (the zoom mask, the HUD's pictures, the sensor's blips) give texture coordinates in texels, which a 4-texel stand-in would repeat across; they bind through this and stay untextured when missing. The bind's body is now `tex_bind(key, has_alpha, standin_ok)`, inlined into both.
- **`texavgall`:** every bind draws its stand-in (`psp_tex_standin = 3`), to see what they look like. A look tool only.
- **Census:** every 300 frames, `textures: texavg: N frames drew a stand-in (longest run N), N stand-ins drawn`; frames that drew a stand-in no longer count as untextured.

## Cost and risks

- Heap: 32 bytes a texture, only with `texavg` on (see the lab's findings for b30's count).
- Exactness: drawing only; the simulation never reads it. Proven in the lab (texavg1).
- PGO: `psp_tex_bind`'s code changed, so the profile `campaign-w154a` no longer matches it and strict PGO refuses it; the lab's runs build without `PSP_PGO_STRICT`. The next profile cut covers it.
