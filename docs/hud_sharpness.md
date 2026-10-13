# HUD sharpness (2026-10-13, for build 120)

Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

**What Sheepdog01 sees:** the HUD looks soft on the PSP-1000.

## Why it's soft

Sprite art is sharpest when one texel lands on one pixel, at a whole-pixel position: then the bilinear filter has nothing to blend. The HUD breaks that in three places.

| Stage | What happens [D] | On screen |
|---|---|---|
| **Scale** | The game lays the HUD out on 848 × 480; the PSP shows 480 × 272 (× 0.566), then the HUD zoom (1.5 since build 40) gives **× 0.85** | Each texel covers 0.85 of a pixel, so every edge is resampled: soft, with uneven stroke widths |
| **Position** | `hud_draw` (psp_ge.c) places corners at the game's coordinates times that scale, never rounded | An edge lands part-way across a pixel and spreads over two; some elements look sharper than others |
| **Format** | HUD sheets could take 16 colours (`texpack.py`) when their error is within 1 dB of the 16-bit frame buffer's own | A glyph's anti-aliased edge or a soft ramp can pass the test while its steps show [U for which sheets] |

Already in place before 120: HUD sheets at full size with no mip levels (build 39), and the 1.5 zoom (build 40).

## The levers (build 120)

Each is separate, so they can be judged one at a time. Cheapest first.

| Keep word / switch | What it does | Cost |
|---|---|---|
| `hudsnap` | Each HUD quad's corners rounded to whole pixels (`hud_pixel` in psp_ge.c) | None |
| `hud177` | Zoom 848 / 480 = 1.767: one HUD texel is one PSP pixel. The HUD is ~18% bigger than at 1.5. With `hudsnap`, it's drawn 1:1 | None |
| bake: HUD at 8 bits (on by default) | HUD sheets never take 16 colours. `--hud-clut4` (texpack.py) restores the old rule | A little texture memory: the bake's `HUD sheets:` line names the sheets it changed, if any |

**How to read the bake's line:** `HUD sheets: 0 kept at 8 bits…` means the 16-colour rule never touched the HUD, and the format wasn't part of the softness.

**The A/B order:**
1. `hudsnap` alone: is the 1.5 HUD comparably sharper? (Sheepdog01 prefers cheap if it's close.)
2. `hudsnap hud177`: the fully crisp HUD. Is the bigger size acceptable?
3. If 1:1 is clearly sharper but 1.767 is too big, the next lever is below.

## Next, if needed: the HUD baked at its drawn size

Shrink each HUD sheet offline to the drawn scale (× 0.85 at 1.5) with a high-quality filter (Lanczos), then draw it 1:1 with `hudsnap`. An offline filter weighs many texels and keeps strokes crisp; the GE's bilinear sees four. The costs: the bake is tied to one zoom (the other `hud1xx` words fall back to scaling), and PSP textures are powers of two, so each sheet sits in a padded canvas with its UVs scaled to match.
