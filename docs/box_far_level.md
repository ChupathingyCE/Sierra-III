# The far level as one part (the GE lists and forms box, its first far form)

The builder's page, 2026-11-12. It covers the lab's `research/opportunities/drawing_sizing.md`, "The order" item 2, and §1-§5 of "The far tiers fleshed out". The lead's ready list (`coord/sessions/lead.md` §3b) puts it first for the builder, then D1's sprites. Labels: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.

## What it is

A model drawn under a few tens of pixels is drawn as **one part**, baked once per model:

- its lowest level's parts merged;
- unlit, with the object's light as one tint;
- each vertex on one bone;
- its base maps packed into one small CLUT8 atlas;
- drawn as a strip.

It replaces the model's parts in the frame's records, so the ME's replay and R both see one ordinary part. Under Sheepdog01's ruling of 2026-11-10 13:46 it is **efficiency, on by default** (keep word `farlevel`, its threshold `farlevelN` in pixels, overshooting). Its proof is play and both seeds identical (drawing only) plus shots for Sheepdog01's look, not "looks the same". Explosions, fire, projectiles and tracers keep their own forms; first person, the main menu and anything near keep everything.

## Why it pays (the census, [E] counts)

- **Most model parts are drawn small.** farcensus3 (2026-11-12, the lab's sierra-batch 43176406033, row `farcensus3_bands_and_level_far`) counted model parts by size band:

  | Stretch | Share of parts under 32 px |
  |---|---|
  | b30's route | 74% |
  | b40 to the snowfield | 52% |
  | a30 | 49% |
  | d20 | 41% |
  | c10 | 39% |

  Under 96 px the share is 76% of the snowfield's model indices (farcensus2).
- **A part costs the ME ~150 µs to replay, whatever its size** [M, `me_steps_flight_old_vs_r`]. That is bind, state and lighting ~17-29 µs, then draw calls, bones and near tests ~10-15 each, then the record's decode. A far part skips nearly all of it: no lights, no near tests, one bone set, one bind a run. A model of ~2 parts a run (`stage3_chain_hotset` [M]) becomes one.
- **The models are ~37-43 ms of the ME's render job on b30's flight** [M, meparts4]. The job's allocation in REBUILD.md §3 is 12 ms, so the far tiers carry most of that gap. **The size-band price** is the PSP run that sizes this: meparts by band, Sierra-III recon/mepartsband 8f48d4e (row `mepartsband_counts_b30_flight` for its counts). It turns these shares into milliseconds before anything is built past the bake.

## The contract

| | |
|---|---|
| **Reads** | the object's pixel diameter (the engine's level pick, at 272 lines under LODS); its tier byte (hysteresis); its cached lighting (`object_get_cached_render_lighting`); its node matrices; the pack's far directory |
| **Writes** | one record a far instance in place of its parts' records (main CPU, where the part is recorded today) |
| **Read by** | the ME's replay (one part, lighting off, the tint as the material colour, the atlas bound, fog on) and R's instance list (keyed by its block, a new key: (model, far)) |
| **When** | each frame, at the object's draw, after the engine's level pick |
| **Never** | first person, the sky, ui.map, see-through parts (dropped under the threshold, as the ruling allows, or kept as they are; a column of the A/B), effects, decals, particles |

## The data (bake: `tools/farlevel.py`, after modelconvert and sspr)

Per model with any part drawable far:

1. **Source level:** SSPR's level 0 where it baked one (rigid parts), else the tag's super-low level.
2. **Merge:** each part's vertices are taken into the model's space through its node at rest. Each vertex keeps one node: the heaviest weight for skinned parts, the part's own node for rigid ones. Parts are welded where positions and uvs agree.
3. **Atlas:** the parts' base maps, mip-reduced to the size they reach at the threshold, are packed into a **64×64 CLUT8** sheet. The uvs are remapped into each map's cell. Tiled uvs are baked into the mesh's cells, since the far level is coarse enough to cut at tile seams. Change-colour texels get palette entries of their own, so one atlas serves every colour: the instance loads its palette row. That is the CLUT trick of §2.
4. **Shape term:** each vertex's colour carries the baked sky and ground term (hemisphere occlusion from the merged mesh, coarse). The run-time tint multiplies it.
5. **Strip:** `meshopt_stripify`, joined by repeated indices, since the GE has no restart.
6. **Format:** a PCV block with a new flag, **PCV_FAR (8)**. Vertices are 12 bytes: 16-bit position, 16-bit uv, 16-bit colour, and a weight byte where the model has more than one node. Then the strip and the node list follow. The atlas is a CLUT8 texture in the texture pack, its palette 256 × 16 bits.
7. **Directory:** after SSPR's region, keyed by tag id: the far block's pack address and bytes, the atlas's texture key, the diameter, and the merged error ε. The trailer is `FAR1`.

Sizes (§1's table [U]): ~1-5 KB a model for the far block, ~4.5 KB for its atlas, ~16 bytes of directory. **What a frame keeps resident** is the distinct far models of that frame. b30's window 5 drew ~13 biped and ~18 scenery definitions under 32 px (row `farcensus3_bands_and_level_far`): ~30 models, so ~100-300 KB of the geometry and texture caches. **That is the first check, before the draw path is built** (slice 0).

## Slice 1's form, simplified (2026-11-12 01:5x)

The atlas needs the texture bake's plumbing: decoding and requantising each base map, and a new texture key. It also has a hard case, tiled uvs, which a single cell can't wrap. So slice 1 drops it, and the atlas becomes a look column (slice 1c) only if Sheepdog01's shots ask for texture under the threshold:

- **Vertex colours carry the texture.** Each far vertex's colour is its base map sampled at its uv, from a mip about the size the part covers at the threshold, times the shape term. At 32 px a far part covers a few dozen pixels, so colour at each vertex of a ~50-300-vertex mesh holds about what the texture showed. That's the overshoot the ruling asks for, tuned by `farlevelN`.
- **No texture is bound,** so a far instance costs no texture-cache room and no bind. Only its block passes through the geometry cache.
- **The tint multiplies through the GE's ambient alone:** lighting stays on with no lights enabled, the ambient set to the tint, and the material's ambient taken from the vertex colour (`sceGuColorMaterial(GU_AMBIENT)`). So the GE works out tint × vertex colour with no per-light work, and the change colours come out of the bake per permutation.
- **The far block** is a PCV block flagged PCV_FAR (8): 12-byte vertices (16-bit position, 16-bit colour, a node byte and padding), then the strip, then the node list.

**The first bake (w162far1, 2026-11-12; the lab's row `farlevel_bake_census_w162far1`) [D]:**
- On b30, 28 of 86 models qualify, in a 66 KB region. The rest are left for:
  - a non-soso shader: 29;
  - more than one permutation: 12;
  - more than one node: 11;
  - skinning: 6.
- On b40, 22 of 82 qualify, in 33 KB.
- Slice 1 alone brings in under a third of what b30's small parts stop reading (slice 0).
- The merged meshes' strips ran 4.7 indices a triangle, so a list is kept where the strip isn't shorter.
- The next slices are sized by the groups left: other shaders and permutations first.

## The run-time path

- **The tier:** after the engine's level pick, the object's diameter is compared with `farlevelN`. It drops to far below N and comes back near above 1.15 N, using its tier byte. No cross-fade.
- **The record:** where the main CPU records a model's parts (psp_ge.c, the part draw), a far object records one far part instead: its block, the atlas key, the palette row, the tint, and its bone matrices (one per node in the block's list). The parts' records aren't made, so the ME never sees them.
- **The tint** (§4): the cached lighting's ambient plus its two distant lights at a half-facing average, as one RGB, with the point lights in reach added at the centre with Halo's falloff, once per object on the main CPU. Under the ruling it is refreshed every 4 frames, staggered.
- **The ME's replay:** the GE's lighting is off, the material colour is the tint, one bind (the atlas and its palette row), the bones (1-8), and one strip draw. Fog stays the models' own.

## Budget

| Unit | Today, a model's parts (b30's flight, per far instance) | The far level's allocation |
|---|---|---|
| ME, replay | ~2 parts × ~150 µs [M] | **≤ 15 µs** (one bind, ≤ 8 bones, one draw, no lights) [U] |
| GE | the parts' vertices at their level, ~2 weights a vertex [E] | ≤ 300 vertices, 1 bone [U] |
| Main CPU, record | ~50 µs a part (drawing_sizing.md §2) [M] | ≤ 10 µs (one record, the tint once in 4 frames) [U] |
| Heap | — | 0 fixed (pack data through the existing caches); the tier byte a live object |

The slices are priced alone on the PSP before integration, per REBUILD.md §5: a capture of the far instances of one pad bench, replayed through the far path. The in-frame price comes from two builds (old-only, far-only) on the same bench.

## Slices

0. **The cache check** `[enables]`. The caches have no free room to price against: the geometry cache runs 99-100% full in play, and so do VRAM's shares (`memory_census.md` §1 [M]). So the check is the **swap** instead: the far blocks a window brings in, against the top-level blocks it stops reading.
   - On b30's route, window 5, the distinct parts drawn under 32 px read **~339 KB** of top-level geometry: <4 px 10 KB, 4-8 68, 8-16 113, 16-32 148 [E, farcensus3].
   - Their ~30 far models would take ~30-150 KB of far blocks plus ~135 KB of atlases [U]. So geometry comes out ahead by up to ~200-300 KB, and the texture cache takes on ~135 KB.
   - That holds only where *every* instance of a model is far, so its top blocks age out. **Counted (farcensus4, the lab's sierra-batch 43178226282, row `farcensus4_only_small`) [E]:** the top-level KB a 300-frame window reads only for parts drawn under 32 px, never larger in that window.
     - b30: 127-199 KB of the 197-238 drawn there (60-90%).
     - a30: 18-106 KB.
     - c10: 14-31 KB.
     - d20: 1-12 KB.
     - b40 to the snowfield: 0-99 KB; most of its far models are also drawn large within the same window.
     - So the far form frees geometry room where b30 is tightest. What it brings in is the bake's figure (`tools/farlevel.py`'s census).
   - If the texture side can't take its share, §1's fallback applies: ~64 KB of the copies' VRAM as a resident far region.
1. **Rigid, one node, opaque** (rocks, trees, crates, most of b30's ~8 repeated scenery models): the bake (merge, atlas, shape term, strip, PCV_FAR), the directory, the tier, the record, the replay. Proven in the emulator by pictures at each band's nearest distance and play + seeds identical. Shots go to Sheepdog01.
2. **Rigid, many nodes** (vehicles, machines): the bones in the block.
3. **Skinned, one bone a vertex** (bipeds), with change colours by palette row. Under 16 px, D1/D2's sprites take over.
4. **R's runs:** far instances of one model back to back, one bind and one palette a run (R's key).

## Proof

- **Play and both seeds identical** against `farlevel` off (it draws only): statecrc's fields, b30_play2 and c20_play1.
- **Pictures:** shots at each band's nearest distance on b30's flight and landing, b40's cliff and c10. The session judges "a few scattered pixels". Anything with a shape a player could see goes to Sheepdog01 as its own question.
- **The PSP:** the slice alone over a capture; then the pair of builds.

## Open

- Whether the merged mesh should carry the shape term or take the GE's lighting with one directional light. The term is cheaper on the ME; the light is closer on big flat parts. That's a column in slice 1's shots.
- Alpha-tested foliage: cards merge into one alpha-tested strip (the atlas keeps alpha), per SSPR's leaf rule.
- See-through parts under the threshold: dropped (the ruling) or kept as parts. It's an A/B column.

## Sources

- The lab's `research/opportunities/drawing_sizing.md`: "The order", "The far tiers fleshed out" §1-§5, and the census rows named.
- `docs/sspr_bake.md` (the bake's pattern, meshoptimizer: Arseny Kapoulkine, MIT, vendored and credited).
- REBUILD.md §3 and §5 (budgets, standalone prices).
