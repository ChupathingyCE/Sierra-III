# Review: fix-tracers (b139's hand test: H28, tracers drawn as lines)

Branch `fix-tracers` off `w139`. Labels: [M] measured on the PSP, [E] emulator, [D] read in code or logs, [U] inference.

| Finding | Change | Default | Keep word | Evidence |
|---|---|---|---|---|
| H28 | A contrail's corners take their alpha into their colour where the blend has no alpha of its own; the draw's map wrap from the shader's clamp flags | on (restoration) | `contrailsflat` (as before) | [D] the Xbox's effect combiner; [E] a30 A/B below |

## H28: the trail's fade

**Cause [D].** `render_contrail` gives each point's corners a colour and an alpha from the contrail's states (the point's age through its states, scaled by density), times the fade toward the eye, and sets the alpha of the first and last pair to zero unless the definition says otherwise. On the Xbox, contrails are effect shaders, and the effect's last combiner (`rasterizer_xbox_transparent_geometry.c`, the effect case) multiplies the colour by the vertex alpha for the added, subtracted and max blends, and the alpha for the alpha blend. The tracers use added blends. The GE's added blend (`hud_blend` case 3: ONE, ONE) never reads alpha, so on the PSP every segment drew at its full colour from muzzle to projectile: an even line, with no fade along its length and no tapered ends.

**The change** (`psp_ge.c`, `contrail_faded_color`; `psp_fixups.py`, render_contrails.c's call): each corner's colour is scaled by its alpha for blends 3, 4, 6 and 7 (add, subtract, max, alpha-multiply-add), and moved toward white (grey for double multiply) by it for multiply and min, as the Xbox's combiner does. Alpha blend is unchanged (the GE reads alpha itself). The engine side now also passes the shader's map flags (u and v clamp) in the blend's high byte, so the draw clamps where the shader says and repeats otherwise. Before, the draw slot kept the last user's `depth_mode`, so the wrap was whatever the last draw in that slot used.

```c
case 3: case 4: case 6: case 7: /* add, subtract, max, alpha-multiply-add */
	r = r * a / 255; g = g * a / 255; b = b * a / 255;
```

**Evidence [E]** (a30 fight on the b139 hand test's route, shots every 2 s from 330 s; `contrailsflat` / default / `nocontrails`, two runs a side): where tracers are on screen (360-368 s), the flat side has 87-133 pixels in each shot brighter by more than 40 levels than the faded side. These are the strips' tails and ends, which now fade out. The faded strips are still there against `nocontrails` (80-223 pixels brighter a shot). The log's contrail line counts every queued contrail as faded (e.g. 114 of 114, 758 segments). Not compared with the PC reference: the Xbox's combiner settles the look [D].

**Cost [E].** A few integer multiplies per corner (on the order of 1,000 corners a frame at most). The frame pairs at equal game time differ by -0.21 ms (se 0.20) over the whole run and +0.64 ms (se 0.40) over the fight's 6 windows. Both sit within the pairs' own spread: the sides' play drifts apart (drawing feeds play), so the windows don't hold the same fire. Free in frame time, so it's on by default. Drawing contrails at all is the existing cost (`nocontrails`, about 1.2 ms a frame in a30's heaviest stretch [E]); this change doesn't alter it.
