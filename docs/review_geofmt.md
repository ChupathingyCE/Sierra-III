# Review: geofmt (the geometry pack's formats)

## 1. The level's rendered vertices without binormal and tangent (bake `--slim-level`)

**What.** A rendered level vertex in the geometry pack is the Xbox's 32 bytes: position (12), packed normal (4), packed binormal (4), packed tangent (4), uv (8). The port never reads the binormal or tangent, so `--slim-level` packs each rendered vertex as 24 bytes (position, normal, uv); the 8-byte lightmap vertices are unchanged. Exact: the level's copies are built from the same bytes.

**Who reads the pack's level bytes** [D]. Only `level_stream_get` (`psp_ge.c`), which builds the copies (base uv + position, lightmap uv + position, the packed normal, the lightmap's radiosity) and reads offsets 0-15 and 24-31 of each rendered vertex and the whole lightmap vertex. Everything else reads those copies or the BSP's 6-byte position copy:
- level draw, dynamic lights, shadows, see-through level: the hooks pass the pack address, and the copies are drawn (`psp_fixups.py`, `psp_environment_*`);
- the engine's CPU readers: `structure_visibility.c` (the surface frustum test) and `structures.c` (the surface under a point) read the 6-byte position copy (`psp_structure_vertex_stride`); `object_lights.c` reads uvs, normals and radiosity from the copies (`psp_ge_level_vertex_texcoords`, `psp_ge_level_vertex_lighting`);
- collision has its own packed BSP; decals take their frame from collision surfaces; `structure_lens_flares.c` reads `uncompressed_vertex_data`, empty in an Xbox map; the engine's `environment_vertex_compressed_get_*` are reached only through the paths above;
- no debug path reads the pack's level bytes.

**The bake** (`tools/levelslim.py`, run after the map is built and before the texture and decal steps, which key walls by pack address). It rewrites the pack and moves every pointer into it:
- the blocks come from the BSPs' materials (`bspread.py`), each its rendered then its lightmap vertices; each block keeps its 64-byte alignment, so every later struct moves by a multiple of 64;
- every word of the map that points into the pack (models' vertices and indices, animation frame data, the level's descriptors) goes through the same table;
- it refuses, leaving both files as they were, on any pointer into dropped bytes or padding, any not 16-byte aligned, padding that isn't zero, a level block with no pointer, or a material whose lightmap count is neither 0 nor its own;
- header word 3 (data size) and words 5-6 (the animation range) are rewritten; word 10 gains bit 4.
- Its log line: `levelslim: N level blocks, N rendered vertices: pack A -> B KB (-C KB); N pointers into the pack, N moved, N of them the level's`.

**The runtime** reads the format from the pack: `psp_geo_level_stride()` (`psp_geo.c`, bit 4 of header word 10) gives 24 or 32; `level_stream_get` locks `count x (stride + 8)` bytes and reads the uv after the normal; the level draw hook's check that the lightmap block follows the material's (`lightmap_address == base + stride x count`) uses the same stride. An older pack, or a map that keeps its geometry in its tags, reads as before. No keep word: the binormal and tangent aren't in the pack to switch back to; a bake without `--slim-level` is the other side.

**Gains** [D, census of bake b140x]: pack -743 KB b30, -2,459 c20, -4,711 a10, -635 a30, -2,184 d20 (20% of the level's render data; the bake's log gives the exact figures). Heap: 0 (the geometry cache is a fixed 1 MB carved from the texture cache, and the copies are unchanged). Each copy build reads 32 instead of 40 bytes a vertex from the stick through that cache: 20% fewer bytes read and held while copies are built or rebuilt. GE: 0 (the copies don't change).

**Before it is the default:** bump `BAKE_FORMAT` in `bake/bake.py` with it, so a build from before this change refuses a slim bake rather than drawing its level from the wrong offsets.

**Proof:** framecrc, play and both seeds identical under `nomouth` on a10, b30, c20 and d20, a slim bake against the same bake without it (the lab's findings).
