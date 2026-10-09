# Stencil bake (build 87): wall decorations drawn on the wall's own triangles

2026-10-06. Labels: [M] measured, [E] estimated, [U] untested on hardware. Tools in `psp_port/tools/`, runtime in `psp_ge.c` (search "stencil bake").

## The problem it removes

A *stencil* is a see-through decoration modelled as its own surface just in front of a wall: Blood Gulch's base light strips ("bloodgulch light red/yellow/blue") and pillar emblems, the cave fungus (`cap_moss01b`). Measured on the BSPs, they sit **0.5 mm to 4 cm in front of the wall** [M, stencil_survey.py].

- The Xbox has 24-bit depth, so a few millimetres is many depth steps.
- The PSP has 16 bits. At 20 units from the eye one step is centimetres, so the stencil and the wall land on the *same* step, and which one wins depends on rounding.
- Builds 52–85 tried every kind of depth nudge (slope part, distance lift, floors, ceilings). Each setting fixed one distance or slant and broke another: the symbol sank into its wall, or showed through what stood in front of it. A nudge is a guess about depth error. It can't be right everywhere.

## The idea: make the depths equal, not close

The lightmap and detail passes already draw on top of the base pass with depth **EQUAL**, and they never fight on hardware. They work because they repeat the wall's own draw: the same vertex positions through the same matrices give bit-identical depths.

So the bake makes the stencil *part of the wall's draw*:

1. **Cut the wall** under each stencil along the stencil's outline (a constrained Delaunay triangulation of the wall triangle with the outline inside it). The wall now has its own triangles under the stencil.
2. **Draw the wall from those pieces** in every pass (base, lightmap, detail, shadows, lights). It looks the same, because the pieces lie in the wall's plane and carry its texture coordinates.
3. **Draw the stencil on the very same pieces**: same positions, the stencil's texture coordinates, depth EQUAL. No nudge at all.

**T-junctions** (a vertex lying in the middle of a neighbour's edge) would open hairline cracks, because the GE rasterises the two edges differently. Points added on a wall edge are registered once per edge and inserted into *every* triangle sharing that edge, across materials ("neighbours (edge points only)" in the stats).

## Tools

| File | Does |
|---|---|
| `bspread.py` | Reads a map + .pgeo pack into per-material vertex/triangle arrays (the .pgeo has a 64-byte header; the record key is at +4) |
| `stencilbake.py map pgeo out.pstn` | The bake. Matching rules: stencil corners within `PLANE_GAP` 0.06 units of the wall's plane and at most `BEHIND` 0.004 behind it, normals within ~11° (`ALIGN` 0.98); a stencil triangle is baked only if walls cover 98% of it (`COVER`). Uses `triangle` and `shapely` |
| `stencil_verify.py map pgeo` | Checks that the pieces cover each replaced triangle exactly, keep its winding, and add no T-junctions across all materials |
| `tj_compare.py` | T-junction count before vs after |
| `stencil_survey*.py` | The survey that measured the gaps |

Front face: the game culls `D3DCULL_CCW`, so a triangle's front is the −(cross product) side; the bake keeps every piece's winding. Near-coincident points crashed `triangle`, so points within 1e-5 units are merged (`add_vertex`), and edge points closer than 2e-5 units along an edge are one point.

## File format: `<map>.pstn` (next to the map)

Little-endian.
- **Header**, 64 bytes: `'PSTN'`, version 1, the pack's map CRC (a .pstn for another build of the map is refused), wall count, layer count, stencil count, body size.
- **Walls**, one per wall material: key, original vertex count, added vertex count, replaced-triangle count, piece count. Then:
  - added vertices as 7 floats + 1 word (position, base uv, lightmap uv, packed normal);
  - replaced triangles (the game's index triple, first piece, piece count);
  - pieces (3 × u16, indices into the material's vertices extended by the added ones).
- **Layers**, one per (wall, stencil) pair: keys, then vertices as 5 floats (stencil uv, position) and triangles as 4 × u16 (three vertices and the replaced wall triangle the triangle lies on).
- **Stencils**: per stencil material, the triangles now drawn from layers (index triples). Its other triangles keep the old nudge path.

## Runtime (psp_ge.c)

- `psp_ge_stencils_open` is called by `psp_geo.c` when the pack opens. It checks the CRC and builds per-wall hashes of the replaced triples.
- Level draws (body, shadow, light) substitute each replaced triangle with its pieces (`bake_substitute`) and stamp the triangle with the frame number.
- A stencil's layers are queued only for wall triangles drawn this frame. They are classified and cut at the near plane exactly as the pieces were (same corners), and flushed with the see-through draws.
- **Debug menu "Stencils (wall decorations)":**
  - **baked, EQUAL** (default);
  - **baked, +2**: GEQUAL with two depth steps of slack;
  - **old nudge**: the bake off.

  With "Level vertices: 16-bit" (levelcompact) the layers use +2 automatically: the wall then goes through a different vertex path from the layer's floats.

## Results

Bake statistics [M]:

| Map | Stencil triangles baked | Walls | Wall triangles replaced | Pieces | Layers | .pstn |
|---|---:|---:|---:|---:|---:|---:|
| Blood Gulch | 65 of 132 | 8 | 61 | 519 | 30 | 21.9 KB |
| Beaver Creek | 48 of 84 | 8 | 67 | 459 | 23 | 18.6 KB |
| Boarding Action | 34 of 296 | 13 | 37 | 201 | 11 | 8.6 KB |
| Carousel | 388 of 1,220 | 25 | 93 | 1,727 | 52 | 67.1 KB |
| Chill Out | 84 of 324 | 7 | 26 | 378 | 31 | 14.8 KB |
| Damnation | 44 of 1,440 | 4 | 11 | 213 | 22 | 8.3 KB |
| Hang 'Em High | 358 of 358 | 6 | 174 | 2,171 | 176 | 87.5 KB |
| Longest | 107 of 179 | 2 | 96 | 488 | 50 | 21.3 KB |
| Prisoner | 122 of 132 | 1 | 151 | 583 | 63 | 26.7 KB |
| Rat Race | 42 of 480 | 6 | 25 | 267 | 18 | 10.7 KB |
| Sidewinder | 80 of 158 | 16 | 104 | 702 | 47 | 29.1 KB |
| Wizard | 48 of 144 | 1 | 64 | 570 | 1 | 23.9 KB |

- **What isn't baked keeps the old path.** That covers stencils not on a wall (the teleporter's beam, free-standing glass) and stencils the walls cover less than 98%. Damnation's and Boarding Action's low shares are probably see-through surfaces that aren't decorations [E: not inspected surface by surface].
- **Verification:**
  - Blood Gulch: exact cover, windings kept, **0 T-junctions** [M, stencil_verify.py].
  - The other maps: at most 5 residual T-junctions each, all ≈2e-5 units (far below a pixel) [M].
  - The bake is deterministic: a re-run gives a byte-identical file [M].
- **Cost in the emulator** [M, spawn views, per frame]:
  - Blood Gulch: ~18 wall triangles become ~145 pieces (+127 triangles), plus 8 layer draws (50 triangles). Small.
  - **Hang 'Em High: ~150 wall triangles become ~1,850 pieces (+1,700 triangles), plus 96 layer draws (~400 triangles).** That is large next to Blood Gulch's ~3,000 level triangles a frame. Its walls are covered in stencils, and each wall triangle is cut into ~12 pieces.
  - Hardware cost: benchrun's "stencils old nudge" switch for Blood Gulch [U]. For Hang 'Em High, compare the debug menu's modes by the overlay's frame time.
  - If it's too costly there, the levers are fewer pieces (merge pieces outside the stencils back into fans), or baking only stencils the nudge gets wrong.

## Hardware results (b87)

**Looks (run 2, Blood Gulch, Sheepdog01's shots 000–019):**
- Baked EQUAL and baked +2 look the same everywhere tested, close and far, at the base walls and pillars. **EQUAL works on this path** when the layer repeats the pieces' exact vertices and cuts, as predicted. [M]
- The old nudge visibly lifts some symbols off the wall, and at an extreme angle shows a light decal through the pillar beside it. [M]
- The faults left in those shots weren't stencils: red base's large painted symbol cut into the wall (a scenario decal, fixed by b90's projection pass, renderer_debugging.md), and a decal missing on a wall at the left (not yet explained).

**Cost (run 5, Hang 'Em High, the worst map for the bake; standing still, one view, modes switched every 30 s)** [M]:

| Mode | Frame | fps | Renderer CPU | Level draws |
|---|---:|---:|---:|---:|
| Baked EQUAL | 36.0 ms | 22.9 | 21.8 ms | 13.0 ms |
| Baked +2 | 35.9 ms | 22.9 | 21.8 ms | 13.0 ms |
| Old nudge | 31.0 ms | 26.0 | 17.5 ms | 9.1 ms |

- EQUAL and +2 cost the same (same triangles; only the depth test differs).
- **The bake costs 5 ms a frame there.** 131 wall triangles a frame become 1,558 pieces (+1,427 triangles, half again the view's ~3,000), plus 106 layer draws (506 triangles).
- The GE isn't the limit (it waits 0.2–0.4 ms a frame). The cost is CPU, about 3 µs per added triangle:

| Step | Bake | Old nudge | Extra |
|---|---:|---:|---:|
| Finding the replaced triangles + index range | 1.88 ms | 0.39 | +1.5 |
| Angle-bucket sort | 3.48 | 2.22 | +1.3 |
| Classifying corners | 3.90 | 3.09 | +0.8 |
| Passes (binds, lists) | 6.25 | 4.65 | +1.6 |

## Making it cheaper (planned, not built)

The pieces go through every per-triangle step as if they were the level's own triangles. They needn't:
1. **One lookup pass with a fast reject.** `bake_substitute` hashes every triangle of an affected draw twice (once to size the list, once to fill it). A bitmap of the vertices that start a replaced triangle rejects most of them with one bit test. [E: most of the 1.5 ms]
2. **A replaced triangle's pieces as one block.** They lie in its plane and inside it: classify and bucket the parent once, then copy its pieces' indices. [E: most of the sort and classify cost]
3. **Fewer pieces at bake time:** merge the pieces outside every stencil back into larger triangles.

Target [E]: the bake under 1 ms a frame on Hang 'Em High, which would make it the default on every map without a cost argument.

## Open

- The cheaper path above, then re-measure with run 5's method (stand still, switch modes every 30 s).
- Grazing angles at 100+ units weren't covered separately; nothing in run 2 suggested a problem.
- Every map needs its .pstn rebuilt whenever the map or pack is rebuilt (the CRC check refuses a stale one, and the stencils fall back to the old path).
