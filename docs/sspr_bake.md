# SSPR at bake: levels of detail from a pixel-error budget (step 1, rigid models)

A build plan (master, 2026-11-06). Sheepdog01 asked on 14:03, through bottleneck, for SSPR's bake step to come forward as its own path, with A/B bakes for his eye. The design is the cost map's `redesign_tick_target.md` §3.6 (docs/cost-map 9fd38509). This page says how step 1 is built. The lab's `levers.tsv` row is SSPR. Labels: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.

## Why

- **The GE is vertex-bound in the flight** (~89%, C6), and the GE's reads of RAM take a toll on the tick (~7.5 ms a tick on b30's flight, `cpu_cost_map.md`). So fewer vertices help both the frame and the tick.
- **The tags' own levels are spent** (lodcensus1 [E]). 66% of b30 flight's model vertices belong to models with no lower level. LODS (models picked at 272 lines) has already taken what the tags' levels give.
- **SSPR was sized in 2026-10 under a 3% bar and stopped for want of a bake tool** (levers.tsv). The bar is now the 30 fps target (plan §1b), and it is being priced per level.

## What step 1 does

1. **At bake, each rigid model part with no lower level of its own is decimated** into levels 0-3 (the engine's super low to high; level 4 is the top, kept as it is).
   - The simplifier is **meshoptimizer's** `meshopt_simplify` (Arseny Kapoulkine, MIT). It is vendored at a pinned release under `tools/third_party/meshoptimizer/`, with its licence, and credited in the README.
   - It keeps UVs and normals as attributes (`meshopt_simplifyWithAttributes`), and locks the part's border vertices, so open borders between parts don't open.
   - **UV seams inside a part** are split vertices at one position, and they open unless the simplifier is told they share it. The pinned release must take a position remap (`meshopt_generatePositionRemap`, or its seam-aware path). That is checked when it's vendored; an older release is not pinned (bottleneck 14:20).
   - Each level records its **shape error ε in model units** (meshopt's relative error times `meshopt_simplifyScale`).
2. **Each level's cutoff follows from the error and one dial, *e* pixels at 272 lines.** A level is drawn once its error projects under *e* pixels.
   - The engine's pick compares the object's size in pixels with the model's cutoffs. That size is the bounding diameter at 480 lines, which LODS scales to 272. [D: `FIXES_LODPSP`]
   - `e × D / ε_L` (in the pick's own units, D the diameter the pick uses) is the largest size at which level L still projects under *e*.
   - The engine stores, per level, the size at which it moves *up* to that level. So the value goes in the next finer level's cutoff: **L+1's cutoff = e × D / ε_L** (bottleneck 14:20). A one-line test on a known model checks the sense before the bakes.
   - The bake writes these cutoffs over the tag's cutoff floats in the map: the gbxmodel's super high to super low cutoffs at 0x08-0x18 [D: Invader's gbxmodel definition, as bake/lib reads it]. This is the same in-place rewrite modelconvert already makes to the pad word, and the size doesn't change.
   - **The cutoffs are per model, not per part.** So step 1 rewrites only models where no region permutation has a lower level of its own. A model with its own levels on some parts keeps the tags' cutoffs and is listed (its count goes in the bake's log).
   - **The engine's pick is unchanged.** It has no hysteresis, so a model on the edge may switch level from frame to frame. Step 1 adds none, and the shots' notes say so (bottleneck 14:20: ~10% at each cutoff is the remedy if Sheepdog01 sees it).
3. **The decimated parts are PCV blocks**, written after modelconvert's region in the pack, in its format (24-byte records, strips where they pay). A directory maps each (part's block, level) to its block.
   - At run time, when the engine has picked a level whose geometry is the top's (the model has no level of its own there), the GE layer's model draw takes the level's block instead.
   - **R takes these as any part:** a form is keyed by its block, so a level is a new part.
   - The geometry cache reads them like any block.
4. **Foliage gets its own method** (bottleneck 14:12). The alpha-tested leaf-card trees would come apart under edge collapse.
   - For these, a level drops whole cards, smallest projected area first. It may also merge the cards of a level with `meshopt_simplifySloppy` and keep the alpha test.
   - They get their own *e* (a column in the first bakes).
   - A tree is told by its shader (alpha-tested, two-sided, many disjoint quads). [U: the rule is checked against the census's list of the flight's trees]
5. **Off by default behind `sspr`.** Without it the GE layer never looks the directory up, so one build plays both sides of an A/B. Each bake's *e* is in the pack's header and logged at the map's start.

## How it's built (the pieces)

- **`tools/sspr.py`** runs after modelconvert, on the map and the pack.
  - It finds rigid parts whose model has no lower level anywhere (every region permutation's five geometry indices are one).
  - It decimates each such part (meshoptimizer through a host library built from `tools/third_party/meshoptimizer`).
  - It writes the levels as PCV blocks with a new flag, **PCV_LEVEL (4): the block's own triangle list follows its records** (a rigid block today draws the part's index buffer). It also writes a directory (the part's block index × 4 levels → the level's block, 0 for none) and the bake's *e* in the pack header's next free words.
  - It writes the cutoffs into the map.
- **The engine's pick** (models.c, render_model [D]) starts at level 4 and steps down while the object's size in pixels is under `detail_cutoff_pixels[level]`. So the cutoff at array index L+1 is the size below which L+1 gives way to L: under `sspr` the runtime writes `detail_cutoff_pixels[L+1] = e × D / ε_L` into each listed model's tag at the map's load (index 0 is never read), kept non-increasing downwards. The pixels are LODS's (272 lines). This settles bottleneck's check 1 from the code; the test on a known model still runs before the bakes.
- **The level the engine picked reaches the GE layer** through a fixup after that loop: `FIXES_SSPR` sets `psp_model_level` (0-4). Only the number is carried; the pick is the engine's.
- **The level block is chosen where the part is recorded, on the main CPU** (psp_ge.c's part draw, before the record is made): its vertices, triangle list, count and strip flag replace the part's. So the record, the ME's replay and R see an ordinary part with its own list, and nothing on the ME looks the directory up. The block is read through the geometry cache like any baked block (its range added to the baked range).
- **psp_geo.c** reads the directory with the pack's header, and relocates a PCV_LEVEL block's list as it relocates a skinned block's.

## The proof

- **Exactness isn't asked** (it's a look change by design). Play and seeds must be identical to off, because only drawing changes. That's checked by statecrc's play and seeds fields.
- **Sheepdog01's eye decides *e*.**
  - Bakes at e = 0.5, 1 and 2 pixels for rigid models, plus the foliage column, all in one build.
  - Side-by-side screenshots against off on b30's flight and landing, b40's cliff and a cinematic close-up.
  - The GE's model vertices are counted in the same runs (lodcensus's count).
  - **So are the geometry cache's misses and resident KB.** The levels add blocks and b30's cache is already full, so a vertex win mustn't be paid back in reloads (bottleneck 14:20).
- **Step 1's GE figure is the Pelican's and the trees' share only** (bottleneck 14:12). The bipeds are skinned, step 2's, and carry the larger share (under 16 px: grunts, elites and marines ~14k of the flight's 55k vertices [E]).
- **Priced on the PSP once Sheepdog01 picks *e*:** one pair, `- | sspr`, on b30's flight (the redesign rule).

## The levels are drawn as lists (2026-11-10, chat1110)

**Superseded the same day (the master; the lab's findings `sspr_strip_count_b30`, `_b40`, `sspr5_figure`, `sspr_pair2_b30`):** the bake's count found the tags' strips run 2.09 indices a triangle, so the list levels were already shorter; the levels are now stripified where shorter (Sierra-III master/sspr2, its `docs/sspr_bake.md`), the strip word fixed, and Sheepdog01 approved 1 px (2026-11-10 14:35). The note below is kept as it was written.

[D] in code, its size [U]. `tools/sspr.py` writes each level as a triangle list, and `psp_ge.c` draws a level with `strip = 0`; the top draws in its source's form, a strip where the tag has one. The GE keeps no transformed vertex (`hardware_facts.md`, GE), so its work is the index count: a list pays 3 a triangle, a strip ~1. A level is kept when its list is shorter than the top's *list* (`len(out) < len(tri)`), ~~so a strip-sourced part's level above ~1/3 of the top's triangles sends more transforms than the top.~~ (superseded by the bake's count, master 2026-11-10: the tags' strips run 2.09 indices a triangle on b30's simplified parts, so the lists were already 13-18% shorter than the top; the strip fix, the shading term and their proof are on `master/sspr2`) The levels stop near half (`sspr_bake_census_fixed`), which is the leading explanation for step 1's −3.6% of indices.

The fix, exact and bake-only: stripify each level (`meshopt_stripify`, joined by repeated indices: the GE has no restart), drawn with the strip flag, and keep a level only when its strip is shorter than what the top draws. Count first: each rigid part's source form and its indices, top against each level as a list and as a strip. Rigid parts whose source is a list can be drawn as strips by the same rule (modelconvert's `strip_of`, as SKINSTRIP does for bending batches). The PSP pair waits for the stripified levels.

## Step 2, after Sheepdog01's look

Skinned models, with each vertex's bone weights kept valid across a collapse (no vertex is merged into one hung on other nodes). The skin plans are made again for each level by modelconvert's planner.

**Shading in the error, before step 2** (bottleneck 17:55, from sspr1's b30 shot 7 at 2 px [E]): a near boulder kept its outline but its lit face shifted (2,032 pixels), because the vertex lighting follows the decimated normals and ε measures shape only. Lit skin and armour would show it more. So step 2 weights normals in the simplifier's attributes, or adds a shading term to ε, and step 1's rigid levels are rebaked the same way.

## Rules it keeps

- **The bakes stay on the desktop** (game data); only counts and shots reach the results branch.
- **The near-camera levels are untouched:** the top level, first person and anything drawn under the near plane.
- **Credit:** meshoptimizer is named in the commit, the build's review and the README's Credits.
