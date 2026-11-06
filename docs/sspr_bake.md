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

## The proof

- **Exactness isn't asked** (it's a look change by design). Play and seeds must be identical to off, because only drawing changes. That's checked by statecrc's play and seeds fields.
- **Sheepdog01's eye decides *e*.**
  - Bakes at e = 0.5, 1 and 2 pixels for rigid models, plus the foliage column, all in one build.
  - Side-by-side screenshots against off on b30's flight and landing, b40's cliff and a cinematic close-up.
  - The GE's model vertices are counted in the same runs (lodcensus's count).
  - **So are the geometry cache's misses and resident KB.** The levels add blocks and b30's cache is already full, so a vertex win mustn't be paid back in reloads (bottleneck 14:20).
- **Step 1's GE figure is the Pelican's and the trees' share only** (bottleneck 14:12). The bipeds are skinned, step 2's, and carry the larger share (under 16 px: grunts, elites and marines ~14k of the flight's 55k vertices [E]).
- **Priced on the PSP once Sheepdog01 picks *e*:** one pair, `- | sspr`, on b30's flight (the redesign rule).

## Step 2, after Sheepdog01's look

Skinned models, with each vertex's bone weights kept valid across a collapse (no vertex is merged into one hung on other nodes). The skin plans are made again for each level by modelconvert's planner.

## Rules it keeps

- **The bakes stay on the X3D** (game data); only counts and shots reach the results branch.
- **The near-camera levels are untouched:** the top level, first person and anything drawn under the near plane.
- **Credit:** meshoptimizer is named in the commit, the build's review and the README's Credits.
