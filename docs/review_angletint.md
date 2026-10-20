# Review: angletint (see-through tints by the angle, Cortana)

**What it restores:** Cortana's hologram (and any see-through generic shader whose combiners read the angle a surface is seen at) dims where it faces the camera and brightens toward its outline, as on the Xbox. Before, she drew evenly bright and magenta. On by default; `noangletint` turns it off.

**Why she was wrong** [E]: the Xbox's generic combiners take vertex colour 1 as the fades by angle: fade(perpendicular) in red, green and alpha, fade(parallel) in blue (the stage inputs' names). The GE can't run the combiners, so the bake works out one tint for the map the GE draws (`seethrough.py`), and it set those fades to their middle (0.5). Face-on, where most of her is, the Xbox has perpendicular 0; the PSP drew her at 0.5. ChupathingyCE's desktop build draws her violet and see-through, about (88,34,157) on her pixels; the PSP drew (130,24,199).

**The change:**

- **Bake** (`bake/lib/seethrough.py`): `generic_result` takes the angle (`VIEWS`: face on, edge on, the middle). For a generic shader drawn by modulate, `_angle_tints` runs the shader face on and edge on; when the two differ by more than 2/255 in a channel, it keeps both tints (with one doubling between them and the middle tint, which stays for packs and builds without the table).
- **Pack** (`tools/texpack.py`): a new table after the drawn maps, `'TNA1'`, count, then (shader address | flags, face-on ABGR, edge-on ABGR). A `'TND1'` header (count 0 allowed) always comes before it. Builds that don't know the table never read it (the textures are read by offset); this build reads packs without it as before.
- **Reader** (`psp/src/psp_tex.c`): `psp_tex_shader_angle_tints`, a binary search like the tints'.
- **GE** (`psp/src/psp_ge.c`): `psp_ge_seethrough_shader` asks for them and passes them through `seethrough_tint_set` (deferred with the tint). A model part with them takes per-vertex colours (`seethrough_vertex_colors`, as the fade by angle already does), each vertex's tint blended between edge on and face on by |cos| of the angle between its normal and the view:

```c
a = mode == 0 ? 1.0f : mode == 1 ? 1.0f - c : c;
... seethrough_tinted(seethrough_color(fade * a ...), draw->angle_tinted ? seethrough_angle_tint(draw, c) : draw->tint);
```

**Limits:** the blend between the two tints is linear; the Xbox's combiners may not be. Level surfaces (no normals in their GE copies) keep the middle tint. A part cut at the near plane rebuilds its cut corners' colours from the middle tint.

**Cost:** per part with angle tints, one normal transform and blend per vertex (as a part faded by angle already costs); nothing for other parts. The pack grows by 12 bytes per such shader.

**Format:** the texture pack gains the optional `'TNA1'` table: a re-bake is needed for the fix to show.
