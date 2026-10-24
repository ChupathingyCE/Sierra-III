# Review: ocean (the planar fog seen from above)

One change, a restoration, on by default. Keep words: `oceanfogold` (as before; the pass is still counted, not drawn), `oceanfogseg` (distance counted only along the submerged part of the sight line).

## The change

**What the original does** [D]: the Xbox fogs every visible level surface in a pass of its own after the level's passes (`structure_render_fog` -> `_rasterizer_environment_fog_begin/_draw`, `rasterizer_xbox_environment_fog.c`): ZFUNC EQUAL, blend ONE / INVSRCALPHA, the planar colour weighted per vertex by a density from the vertex's depth below the window's fog plane and its distance, the atmospheric fog on top. The window's planar fog comes from the camera's cluster (`structures.c`, mode normal or fully fogged).

**What the port did**: `psp_ge_fog_planar` turns the planar fog into a GE distance fog only with the eye inside it (c10's swamp from inside, a fully fogged cluster). With the eye above the plane, nothing below the plane got the planar colour: b30's sea floor and shallows under the added water took the violet atmospheric fog alone.

**Now**:
- `psp_fixups.py` FIXES_OCEAN: after FIXES_116's planar fog call, each window hands `psp_ge_fog_submerged` the settled plane, planar colour, maximum density (0 unless mode normal), maximum depth and distance.
- `psp_ge.c` `submerged_pass`, at the end of each textured level draw: skipped unless the draw's box reaches below the plane and the eye is above it. Per vertex of the draw's range: alpha = max density x min(depth / max depth, 1) x min(distance / max distance, 1); a flat-coloured draw over the kept triangles, depth EQUAL, no depth write, SRC_ALPHA / ONE_MINUS_SRC_ALPHA, in the positions the draw's passes used (the 16-bit copy or the floats) so the depths agree. The GE's atmospheric fog stays on for it, so the planar colour is fogged as the surface was: planar first, atmospheric over it, the Xbox's order.
- Left out: the pieces cut at the near plane (at the eye: only the shallows underfoot), objects (the Xbox fogs models too), the sky.
- The density's exact shape on the Xbox is a lookup texture indexed by the fog vertex shader [U]; the product of the two ramps is the reading of the fog tag's fields. b30's values make the shape moot there: depth 0.15, distance 0.1, density 1, so anything a little under the water is fully the planar colour.

## Evidence

- b30 planar fog [D, from the GE log]: plane z = 0, colour (47, 66, 61), density 1, depth 0.15, distance 0.1. c10: plane z = 38.97, colour (172, 159, 238), depth 1, distance 16. a10: none.
- Shots [E] (b30 opening, the PC desktop build's frames at the same moments):
  - At the second moment (low over the beach), the open water past the surf goes from violet-lilac to a dark grey-green, as the desktop frame shows it.
  - At the first moment (high above the sea, beside the Pelicans) nothing changes, pixel for pixel. The far violet there is not sea floor under planar fog. It is the background below the horizon behind the added water: the clear colour (the atmospheric fog's), where the desktop has the sky's own water layer. That layer draws nothing in the port. **Still open**, see "Next".
  - `oceanfogseg` gives the same pixels on b30: with depth 0.15, everything saturates either way.
- c10: the swamp pools take the lavender planar colour over their floors with the eye above (24% of the opening's first shot changes). The Xbox fogs them the same way [D]. How murky they should look is for the eye to judge against the original.
- a10: unchanged, pixel for pixel. No planar fog.
- Play: statecrc identical on b30 and a10. On c10 only the `objects` record differs, from tick 90. Every per-object record agrees. [U] this is render-side bookkeeping following the frames drawn, not the simulation. An old-against-old repeat would settle it.

## Cost [E] (emulator, b30 opening, 300 frames)

- 2.1 submerged level draws a frame, 745 triangles, 1,752 vertices computed (each draw's whole index range).
- 0.19 ms a frame on the CPU.
- c10: 7.2 draws, 1,507 triangles, 3,350 vertices, 0.43 ms a frame.
- Plus one flat pass over those triangles on the GE. This needs a PSP point before anyone calls it free.

## Next

- The far ocean: why the sky's `water simple` draws nothing in the port. With the eye above the plane, the Xbox gives the sky's alpha-blended layers no planar fog (`rasterizer_xbox_transparent_geometry.c`: the density there is the eye's depth below the plane).
- Cheaper: compute only the vertices the kept triangles use, and skip the pass for draws whose submerged part is out of view.
