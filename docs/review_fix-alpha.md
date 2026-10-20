# Review: fix-alpha (see-through and cut-out surfaces)

Branch `fix-alpha`, off w139. The hand-test findings it answers: cut-outs drawn as squares (H7, H14), hard-edged water layers (H18), hard-edged thrusters (H19), oily floors (H29), the Jackal's shield (H37), solid plasma glows (H38). Three commits on the bake and one on the bake and the runtime.

## What it found first [E] [D]

- **The new palettes are not the cause.** The same EBOOT on the older bake and on b139's draws every audited spot the same [E]. The bake's alpha audit (`--alpha-audit`) measures every alpha-tested map against its packed palette: at most 1.4% of texels change side of the alpha test on any map, by any method; the pines change none [E].
- **62 of 195 see-through shaders draw a map that doesn't give them their shape** [D]: the GE draws one map of a shader and a constant tint for the rest of it (`seethrough.py`), and for these the map drawn isn't the one that carries the alpha. Among them: the Jackal's shield (a mask times two noise maps, added: drawn as the mask alone it is a solid disc, H37, against the PC build's see-through shield), the Pelican's thrusters (the cone is map 0's alpha, map 1 drawn, H19), b30's shore `waves` (alpha from map 0, map 2 drawn, H18), the needler's core and ammo glow (H38).

## 1. Palettes fitted each side of the alpha test; effects at 8 bits (c873a06)

`texpack.py` fits an alpha-tested map's palette separately for the texels the test keeps and those it drops (`ALPHA_SPLIT`; `--no-alpha-split` goes back to one fit), so no palette entry straddles the threshold. Effects maps the 16-colour test would have halved stay at 8 bits: +157 KB (b30) to +257 KB (a10) at their top levels per map [E]. Memory only; no frame time.

## 2. `--drawn-by-shape` (32f6dd6, off)

A see-through shader blended by its alpha draws the map that carries most of what the frame shows over a dark and a light background alike, so a mask counts for the background it hides. Off by default: it keeps one map and a tint, so it trades the colour pattern for the shape. Superseded by 3 where 3 applies; kept as the bake option it is for the A/B.

## 3. See-through composites (on; `--no-composites`, keep word `nocomposite`)

One map and a tint can't stand for a shader whose look comes from two maps. Such a shader now gets a texture of its own: the shader worked out texel by texel with every map in place (the combiners as `seethrough.py` already runs them), laid out on one of its maps, whose scale, offset, clamping and animation carry it.

- `bake/lib/seethrough.py`, `composite()`: a shader gets one when a map other than the drawn one carries at least 10% (`COMPOSITE_SHARE`) of the output, or, for a blend that lets the background through (alpha blend, alpha-multiply add), of the alpha. It is laid out on the map that shapes it when shape is what's lost (the cone's mask stays still while its colour scrolls), else on the drawn map. Every other map must repeat a whole number of times over that map's span, unless that map is clamped, or the texture would seam where it tiles; a shader where no map qualifies keeps its tint and the log says why. Size: the largest the maps need at their scales, a power of two, up to 256 a side (512 on the menu's pack, `--showcase`).
- `tools/texpack.py`: each composite is baked like any map of its class, under the key `shader address | 1` (no bitmap's key can be odd), and listed in a new table `TNC1` (shader address, the map it stands in for) after `TNA1`; `TNA1` is now written, count 0, whenever `TNC1` follows it.
- `psp/src/psp_tex.c`: `composite_map()` finds a shader in `TNC1`; `psp_tex_shader_map` returns its map, `psp_tex_shader_tint` returns white for it (the composite is the shader's whole colour), and `psp_tex_composite_key` hands back the composite's key. `nocomposite` (or `drawnmapscale`, since a composite goes with the bake's choice of map) leaves every shader as before.
- `psp_fixups.py`, `psp_transparent_map`: after the map's key, a non-numeric shader takes its composite's key.

What a still texture can't hold: the other maps' scrolling is frozen where it stood at time 0 (the shield's noise no longer drifts across it; the thrusters' colour no longer runs along the cone), and the angle a surface is seen at takes its middle value (shaders whose colour turns with the angle are left to their angle tints). Both are what the Xbox shows at one instant: the shape is right where before it was lost.

**Cost.** Frame time: none expected [U]: the same one draw a shader, one texture each; only the texture differs (a composite may be larger than the map it replaces, at most 256 a side). Memory: the composites' bytes, printed by the bake (`composites: N see-through shaders ... bytes`). Measured: pending (below).

**Evidence:** pending. The bake and the A/B (b30 Pelican flight and beach, a10, a30; the same EBOOT on a bake with and without composites, game time on the same stretch) are in the lab under this branch's name.

**The menu's sky** (`sky_x10 ... space`: star mask, stars, and the low-resolution map the GE drew): covered when its maps tile over the drawn map's span (the bake log's `composite for ...space` line says which way it went); the stars then sit in a 512-wide texture, finer than one map but coarser than the Xbox's stars at their own scale.

## H14, H29

- **H14** (a30's distant tree line as olive cards): not a see-through shader and not the sky's `horizon land` (whose drawn map carries its alpha, 0.0002 of texels flipped) [E]. The cards sit along the cliff tops in the hand test's shot; which shader draws them is still open (the emulator's `tdump` lists only see-through shaders).
- **H29** (oily floors): the PC build's ground shows fine detail where the emulator's is smooth; the open thread is the GE's dither on the undithered detail passes (`review_dithershift.md`), a look question for the hardware. The hardware A/B with `PSP_DITHERSHIFT=1` (`dithershift`, `dithershift ditherlift`) is in the lab under this branch's name.
