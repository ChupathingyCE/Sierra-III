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

**Cost.** None measured [E]. The same EBOOT on the same bake with and without composites (`nocomposite`), heartbeats on the same stretch by game time. b30's flight and beach to game time 3,000: tick 14.97 vs 14.98 ms and frame 21.90 vs 21.88 ms at game 240-1,000, frame 41.57 vs 41.54 ms at 1,000-3,000. a30's opening to 2,170: tick 3.54 vs 3.61 ms, frame 38.16 vs 38.15 ms. Past game time 3,000 b30's two sides play different fights (a grenade lands on one only), so their heartbeats differ by up to 3.5% in both directions (frame 20.36 vs 21.08 ms, then 19.52 vs 18.89 ms): noise, not cost. Memory: the bake's composites add to `ptex` a10 +22 KB, a30 +163 KB, b30 +90 KB (the menu's +0.03 MB); the menu's sky composite is 512 by 512 in 16-bit colour, 7 levels, 699,008 bytes.

**Evidence [E].** Same EBOOT, same camera, same game time, composites on vs off:
- **Barely anything changes in view.** b30 has composites for 3 shaders and a30 for 7. Ten of b30's 23 shots (the Pelican flight and the walk to the beach, game 27-99 s) differ in under 0.2% of their pixels, and all of a30's 24 in under 0.4%: the shaders that got composites are mostly off-screen there.
- **H18 (water layers): better, not fixed.** At the first beach shot the sea's white blotches with hard outlines, far out and along the shore, are gone with composites on, and the shore's foam is softer, nearer the PC build's. The jagged line where the sand meets the water stays on both sides.
- **H37 (the Jackal's shield): see-through now.** With composites on, the Elite and plants behind the shield show through its rim and its rainbow centre (b30, game ~108 s). Its definition is as before: the rim and the swirl are a few texels wide. The two sides' fights differ from there on, so there is no second pixel-for-pixel pair.
- **H38 (plasma, needler): no clean pair.** The fights diverge before the first plasma or needler shot. The green plasma bolt on the on side shows a soft halo round a bright core.
- **H19 (thrusters): unchanged.** The Pelican's thrusters are pale bars with a hard top edge on both sides; the off side adds a dark haze round them, which composites on lose.
- **H7, H14 (cut-outs as squares): unchanged.** a30's distant tree line is still a solid olive-grey card with faint pines at the cutscene's first shots, the same on both sides: not a see-through shader.
- **Menu sky (H16):** covered by the bake (the composite above); the menu's runs belong to the front-end branch.

The a10 sign (H7) is under "H14, H29" below.

**The menu's sky** (`sky_x10 ... space`: star mask, stars, and the low-resolution map the GE drew): covered when its maps tile over the drawn map's span (the bake log's `composite for ...space` line says which way it went); the stars then sit in a 512-wide texture, finer than one map but coarser than the Xbox's stars at their own scale.

## H14, H29

- **H7's a10 sign** is not answered. Six emulator runs aimed at the corridor sign never had the sign in view: the opening cutscenes run to about game time 8,500 and carry the camera, a teleport made before they end is undone when they do (the game puts the player back at the bay), and a route put at 305 s moved the player's feet but not the camera in the runs read [E]. Composites change a10's cutscene shots by under 0.05% of pixels and cost nothing (tick 6.12 vs 6.15 ms, frame 44.66 vs 44.56 ms at game 2,500-4,500). What would settle it: a hand-test-style route that walks the player from the bay to the sign (the walk's `player: at` lines are in the test's log), shots from game time 8,500 on (the emulator's cap is 24 shots, so a `shotfrom` word, or `shotevery` set past the cutscenes), `tdump` on each.
- **H14** (a30's distant tree line as olive cards): not a see-through shader and not the sky's `horizon land` (whose drawn map carries its alpha, 0.0002 of texels flipped) [E]. The cards sit along the cliff tops in the hand test's shot; which shader draws them is still open (the emulator's `tdump` lists only see-through shaders).
- **H29** (oily floors): the PC build's ground shows fine detail where the emulator's is smooth; the open thread is the GE's dither on the undithered detail passes (`review_dithershift.md`), a look question for the hardware. The hardware A/B (a `PSP_DITHERSHIFT=1` build, b30's Pelican flight and landing to game time 100 s, one run a side on the PSP-1000) [M]:

  | Side | Frame ms (avg) | GE renderer CPU ms | Game time reached |
  |---|---|---|---|
  | as now | 52.1 | 19.1 | 2,962 |
  | `dithershift` | 59.2 | 21.3 | 2,945 |
  | `dithershift ditherlift` | 54.9 | 20.9 | 2,974 |

  One run a side, inside the stretch's own spread (about 10%); the renderer's CPU looks ~2 ms higher with the shift, to be confirmed with `repeat=2`. The probe confirms the GE truncates to 5-6 bits (`ditherprobe` row 0: red and blue step at 8, green at 4), the drift `review_dithershift.md` describes. The look is Sheepdog01's call from the shots (`shotevery10`, same frames on each side); `dithershift` stays off by default and out of default builds (`PSP_DITHERSHIFT`).
