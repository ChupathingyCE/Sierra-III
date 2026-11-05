# What the game recomputes that a bake could carry (direction 4's census)

Sheepdog01's fourth restructuring direction (2026-10-29 14:00) is "more done at bake time". This is its first step: what the game recomputes each frame, tick or load from data that never changes, what each costs now, and what a bake could hold instead. Nothing here is built.

Labels: [M] measured on the PSP, [E] emulator, [D] read in code, [U] inference.

**Sources:**
- The PSP figures are one build on two maps: w142 be460974 with its PGO profile (w149c), b30's route on the PSP-3000 (lab sierra-hw 40888171017) and c10 hands off on the PSP-1000 (40889089669). Each is 2 runs, using the steady report windows.
- Fetch shares are from the cache model on b30 (aicnt1 40899343318).

**Loads aren't the target.** c10 loads in 1.45 s [M], and the big load-time conversion (model vertex buffers) is already baked (PRECONVERT, Sheepdog01 07:42). What's left to bake is per-frame and per-tick work.

## The table

Largest first, by the map where each is largest.

| # | Recomputed | When | Cost now | What a bake could carry | Lever, status | First step |
|---|---|---|---|---|---|---|
| 1 | **Scenery's updates on c10**: its looping animations and node matrices | each tick | c10: 553 scenery updates a tick, **7.0-7.5 ms a tick** (~12 ms of a 55 ms frame, at 1.65 ticks a frame); 105 a tick have an animated root (vislazy) [M]. b30: 277 a tick, 1.5-2.1 ms [M] | **Pose tables**: for a scenery definition whose only input is a looping animation, its node matrices for each animation frame, baked once per definition. An instance then needs its frame index and its placement, and the GE can take the baked matrices as bone matrices [U] | new (catalogued as PSC). Earlier levers cut computes (NB, NMS, LEAF, vislazy) rather than the animation | [E] count c10's scenery updates by what drives them (root animation only, functions, other), the time each kind takes, and the tables' KB |
| 2 | **Model parts' GE commands** | each frame, on the ME | the ME's replay: b30 70-78 ms a frame, c10 30-37 [M] | Prebuilt lists per part, with patch slots (direction 3) | PDC, sized: the fixed words are ~12-28% of the model parts' time [U] | [E] a word-repeat census (fixed, patched, per-frame decision) |
| 3 | **Weather particles on c10**: each particle is tested against the visible weather polyhedra's planes (the "no weather" volumes), clipped to the boxes and built as a sprite [D] | each frame | c10: **4.7-5.0 ms a frame** [M]; b30 has no weather | A coarse occupancy grid of the weather polyhedra (a bit a cell), so the per-particle plane loop becomes one lookup. The particles' motion isn't static | new (catalogued as WXGRID). The whole loop is self-contained, so it is also an ME job candidate | [E] split the 4.8 ms: polyhedra test, box clip, build_sprite |
| 4 | **Dynamic lights on the level**: each light's reach over the level's surfaces and its lit triangles | each frame | lights_render_diffuse c10 2.35, b30 0.45; lights_preprocess_scene b30 1.47, c10 0.40 ms a frame [M] | For lights that never move (fixtures), their reach and lit-triangle lists | LMC (sized, not started) keeps the same thing at runtime. The bake form has no first frame and no cache to manage | [E] count c10's lights by moving and static |
| 5 | **Visibility**: the portal walk and its 2D clipping | each frame | main CPU (collect) b30 3.12, c10 2.13 ms [M]; the walk on the ME 5.4-7.3 ms on b30 (mevis [M]); the clip is 5.3% of b30's drawing fetches [E] | A per-cluster potentially visible set, so the walk stops at clusters no point in its cluster can see [U] | new (catalogued as PVS). Halo's outdoor maps may see most clusters, which would leave little to prune [U] | [E] for each cluster, how many clusters the walk ever reaches, against all of them |
| 6 | **The level's per-frame lists**: surface order, lightmap and transparent passes | each frame | structure_render_preprocess b30 0.94, c10 0.68; lightmaps 0.60 / 0.65; transparent 0.72 / 0.35 ms [M], plus the ME's level replay | Per-cluster draw lists in their final order | B (step 1 done; step 2 next): its sub-list is the baked form's runtime half | with B |
| 7 | **AI line of sight against the level**: the cover search's rays | each tick | ai_update b30 24.4 ms a frame [M]; the cover search is ~75% of the decision loop [E aitick2]; c10 0.14 | Visibility between firing positions against the level alone. Objects still need rays | catalogued idea. UNCR's runtime memo hit 0.55% [E]; AIME moves the search to the ME | [E] the share of AI rays the level alone blocks or passes |
| 8 | **Sound obstruction rays** | each frame | b30 0.57, c10 0.04 ms [M] | A cluster-to-cluster obstruction table [U] | under any bar | none |
| 9 | **Lookups in the walk**: definitions, tags, textures | each frame | object_definition_get b30 0.23, c10 0.13 ms [M]; tag_get 1.7% and the texture key 0.3% of b30's drawing fetches [E] | Indices resolved at load or bake (MARKERTAB's kind) | small | none |
| 10 | **Sound decoding**: Xbox ADPCM, decoded on the ME each time a sound plays | as sounds play | 3.0-7.9% of the ME [M] | The PSP's own ADPCM, if the firmware's decoder is cheaper [U]. It also runs on the ME, so it may save nothing | small | none |
| - | **LOD pixels** (each object's size on screen) | each frame | b30 3.77, c10 2.38 ms [M] | Nothing: it depends on the view | not a bake item | - |

**Already baked or settled:**
- model vertex buffers in the GE's form (PRECONVERT; B8 closed);
- the level's vertices slimmed (LVLSLIM, proved);
- the collision BSP depth-first (BSPDFS: exact, even on the PSP, shelved);
- textures from the finest level the GE samples (TEXFIT1, keep word `texfit`, off by default: plain entries; TEXFIT1b, keep word `texfitb`: the composite and glow entries, key & 3, which nothing on the CPU reads; lightmaps under their own key stay whole, since object lighting reads their top level). The bake lists both slices in one TFT1 table, so one pack prices each; the trims are in the lab's findings (levers.tsv) [D];
- polygon reduction (SSPR, under the bar).

## What it says

- **c10's two largest are new: its scenery (~12 ms a frame) and its weather (~4.8 ms).** Both come from data that never changes (animations, polyhedra), and neither has a lever yet. Each needs a count before any design: rows 1 and 3, both emulator only.
- **b30's largest is the AI** (row 7), and the bake can carry only its static half. AIME already moves the search to the ME.
- **The ME's replay** (row 2) is direction 3's. On c10 it's about a 30 fps frame on its own, so whatever shrinks it (prebuilt lists, B's level lists, an emitter of our own) counts there first.
- Rows 4-6 have levers or plans already: LMC, B and the visibility walk. A bake is their cheaper form, not a new lever.

## Counted and built (rows 1 and 3)

- **Row 1, PSC (built, on by default; `docs/review_psc.md`).** c10 hands off has 237 awake scenery updates a tick: the 553 the per-type line counts include 316 asleep, which are only counted. Of those, 156 are node computes from a looping base animation alone, and the base pose is about a quarter of the scenery update [E]. Baking isn't needed: the table is filled at first use at run time by the same decode, so it's exact. It keeps the decoded orientations and, for scenery, the children's local matrices. Its gain is less than this table's first guess: in the cache model, c10's tick -4.8% and its scenery -11% (~1.1-1.3 ms a frame on the PSP [U on M x E]). Elsewhere it gives its memory back: it keeps a pose only at its second decode within 16 ticks, and is released under the heap's floors (256 KB largest block, 320 KB free) or under 2 poses a tick served. The Media Engine's pose job already made ~115 of the 156 poses a tick, and in the emulator its decodes differ from the main CPU's in the last bit on c10, which an exact table can't share.
- **Row 3, WXGRID (counted, can't pay).** c10's one weather polyhedron (12 planes) is never within reach of the camera hands off: 0 plane tests a frame. c10's weather is the particles' update (52%), the sprite build (28%), the box tests (14%) and the set-up (5%) [E]. It is a self-contained loop, so an ME job is its candidate, not a bake. c40 is the only other map with polyhedra (11), and it is unmeasured.
