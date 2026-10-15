# Review: LK22b, the level given from an origin near the eye (branch `lk22b`)

Sheepdog01's PSP-1000, b124 test 2 [M]: c40's opening with b124's fix (`lk22`, preset 1) against `levelexactoff` (preset 9): "almost no discernible difference". The level still dances frame to frame, and the dark grey pillars on the back wall flicker. In the emulator `lk22` had stopped it (`review_lk22.md`).

Branch `lk22b` from `w125` (`d18a1d0`). One restoration, **on by default**; `levellocaloff` turns it off (the drawing before it: section 4). One instrument, opt-in: `geprecision`. No fixup pairs.

Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

## 1. Why lk22 wasn't enough

- [D] `lk22` made the level's *translation* exact in the GE's matrix words: the GE's world origin is `ge_eye`, the eye with its low 8 bits dropped, so `-ge_eye` arrives whole and the view carries the rest. That fixes the one loss the emulator models (PPSSPP decodes a matrix word as a float's top 24 bits).
- [M] On the PSP the dance stayed. So the GE loses precision somewhere the emulator doesn't model.
- [U] The likeliest place: the GE folds the model and view matrices (perhaps the projection too) into one, in its own limited precision, when a matrix changes. Then:
  - the folded translation is `R·(origin − eye)`, about 800 units for the level in c40's control room, however the two matrices were split;
  - the folded rotation's entries lose their low bits too, and they multiply vertex positions that are 700–800 units long.
  Either way a level vertex near the eye lands up to ~1/64 of a unit off, and the error changes as the camera turns or moves: the dance.
- [D] Models never danced because both their numbers are small: model-space vertices, and a translation made relative to the eye on the CPU (`relative()`). The level had world-space vertices and a world-sized translation.

So the fix had to make the level's **vertices** small, not just its translation.

## 2. The change

The level's copies (the 20-byte GE streams, `struct level_stream`) give their positions from **`level_origin`**, a point of a 32-unit lattice near the eye. Every draw of a copy loads a model matrix that moves its origin to `ge_eye`, worked out on the CPU in floats (`level_model`). What reaches the GE is then a few tens of units at most, the size of the scene around the eye, as for models.

`psp/src/psp_ge.c`:

- **2429–2540**, the core (the comment says why):
  - `LEVEL_ORIGIN_STEP` 32, `LEVEL_ORIGIN_SLACK` 24: `level_origin_follow()` re-centres the origin on the nearest lattice point once the eye is more than 24 units from it on any axis, as each frame begins. So it moves seldom (c20: twice in a 300-frame window while walking [E]).
  - `level_snap()`: positions snapped to 1/4096 of a unit (0.24 mm, about 1/20 of a pixel at one unit) as a copy is made. A lattice point minus a snapped position is exact in a float within `LEVEL_REACH` (4,000 units), so a copy moved from one origin to another **loses nothing, however often it moves**. Without the snap, a vertex near the world's origin would lose its low bits each time its copy moved away from it.
  - `level_matrix()` / `level_model()`: the model matrix for positions given from an origin. `world_model()` is `level_model(0)`, so with `levellocaloff` every draw gets the same matrix as before.
  - `level_frame()` and `level_eye`: the eye seen from a copy's origin, for the CPU's tests. `level_to_camera()` (3639) uses it; `to_camera()` stays for world positions. For an exact position and an exact `level_eye` the difference is one rounding of the same real number, so the CPU's classifications are the world's (for the snapped positions).
- **3540–3550**, `ge_eye`: with levellocal, `level_origin + ge_matrix_float(eye − level_origin)`. The level's translation (`origin − ge_eye`) is then exactly the rounded part, whole in a 24-bit word. With `ge_matrix_float(eye)` it was whole hundreds of units out but not near the world's origin: with the eye 8–16 units out, `origin − ge_eye` can need 17 significant bits.
- **5481–5510**, a copy is made: positions snapped, the box worked out from the snapped values, then given from `level_origin` (or, if the copy lies more than 4,000 units from it, from the lattice point nearest its own middle). The lightmap copy's positions are the base copy's.
- **5265–5330**, `level_layers_follow()` and `level_copy_follow()`: a copy (and the stencil bake's layers on it) moved to another origin by adding the difference of two lattice points: exact.
- **13144**, `level_copies_follow()`, called at the frame's end (15982), after the arena's compaction, when neither the GE nor the Media Engine reads the copies: the copies drawn this frame from another origin are moved to `level_origin`. A copy not drawn keeps its origin until it is; it draws correctly meanwhile, through its own matrix, with larger numbers. `level_copies_behind` (set in `level_frame()`) skips the scan when nothing is behind.
- **Every user of a copy's positions** now works from the copy's origin:
  - the level draw (6166 `level_frame`, 6372 `level_model`, `world_model()` after the passes): classification, cut corners, the slant sort (`angle_sort`, the ME's slant job, both get `level_eye`), the texture-centre probe;
  - see-through level surfaces (11876) and their angle-faded copies (11836): `draw->matrix` is the copy's (11982, 12013, 12043), which the flush already loads per draw;
  - the stencil bake's layers (11609–11682): given from their wall copy's origin, so the layers and the wall pieces go through the same numbers and depth EQUAL holds;
  - shadows (13182) and lights (13936): the shadow's and the light's positions are moved into the copy's frame for the draw (their wrappers put them back), so the blob's texture matrix, the light's reach, cone and splits all see copy-space positions; the lifts and bands use `level_eye`;
  - the walls' collision ray (11506) and the projected permanent decals (`decal_project_copy`, 14681) add the origin back where they need the world;
  - the ME's measure jobs (`meclassify`) get `level_eye`.
- **Decals** (the game gives them in world coordinates, written to the list each frame): their corners are written from `level_origin` and drawn through its matrix (`decal_origin()`, 14674), so they sit on the level's numbers.
- `levelcompact` (a dead test switch) keeps its own 16-bit lattice and matrix; its float copies are given from the copy's origin like the rest.
- **The log**: a start-up line names `levellocaloff` and `geprecision` (`psp_platform.c`); every 300 frames, "the level's origin (x y z), moved N times; copies moved to it M (V vertices, U us), left for being out of exact reach K".

**Not changed**: contrails (world-space strips, drawn through `relative()`'s identity) still carry world-sized numbers; they move anyway. Everything the game simulates is untouched: the copies are the renderer's.

**`levelexactoff` (b124's off word) still works** and isn't redundant: with levellocal on and `levelexactoff`, the level's translation is `origin − eye`, up to ~40 units, which a 24-bit word holds to about 1/1000 of a unit. The two words are independent; `levellocaloff levelexactoff` is b122.

## 3. The instrument: `geprecision`

`ge_precision_test()` (2689–2888), run once as the first frame begins when the keep word is set, then the game goes on as usual. It measures the GE itself, so Sheepdog01's PSP can say what the emulator can't.

- A white quad 2 units ahead of a camera 800 units from the world's origin (c40's control room, looking along an oblique direction slightly down), drawn alone 32 times with the camera moved 1/256 of a unit sideways each time (**move**), then 32 times turned 2 milliradians each time (**turn**).
- Its right edge leans across the screen's height, so the white pixels counted along every row (read back from VRAM) give where the edge lies to about 1/100 of a pixel.
- A GE exact to a float's precision moves the edge smoothly (a straight line against the step, a gentle curve for the turn). What it rounds shows as the steps' **scatter** about a fitted parabola: one log line per way of giving the quad to the GE (rms and largest scatter, pixels).
- The six ways: `world, eye in model` (b122's level), `world, exact origin` (b124's), `local 28 away` (lk22b's, the level's origin near its farthest from the eye), `local at the quad` (the best case), `local 28 away, eye in model` (lk22b with `levelexactoff`), `near the origin` (control: nothing large anywhere).
- About 400 pictures: a few seconds of black screen at the start on the PSP [U].

**In the emulator** [E] (lab 38683231439 and 38686280101, the same lines both times): only `world, eye in model` scatters on the move, **0.389 px rms (0.738 largest)**: the matrix words' rounding the emulator models. Everything else, 0.010–0.015 px rms: the measure's floor. `local 28 away, eye in model` 0.020 (a 28-unit translation's 24-bit rounding, 1/2048 of a unit). The turn sweep shows nothing anywhere: in the emulator the eye's rounding doesn't change when only the camera turns. The fitted step is −0.380 px on the move, as the geometry says (1/256 unit × 194 px per unit at 1 unit ÷ 2 units).

**How to read Sheepdog01's lines:**
- `world, exact origin` scattering well above `near the origin` (move or turn) confirms that the PSP loses precision on large positions or folded matrices: what LK22 is.
- `local 28 away` back at the control's level: lk22b's fix is enough as built.
- `local 28 away` between the two, with `local at the quad` at the floor: the origin's lattice is too coarse; `LEVEL_ORIGIN_STEP` and `LEVEL_ORIGIN_SLACK` come down (8 and 6, say), a one-line change; the copies move more often, ~1–4 ms each time [E].
- Everything at the floor, including `world, eye in model`: the GE isn't the cause and LK22 lies elsewhere (the CPU's matrices, the near plane after all).

## 4. Evidence (emulator)

Labs: lk22local-a 38683398565 (904e6ae), -b 38683231439 (9f27f33: the same code without the `ge_eye` rounding from the origin and the frame end's flag), -c 38686280101 and -d 38687238130 (e001c32, the final code). Every run JIT, hands off, `fixedticks fixedseed`, maps `/bake/campaign121`.

**The off word is w125** [E] (lk22local-d): c40's opening, 72 frames, `levellocaloff` against w125 as merged (d18a1d0): 48 frames identical to the pixel; in the rest, the stamp's clock digit and at most 167 pixels by at most 24 levels (one frame; the others under 30), none of them the level's edges.

**levellocal with `levelexactoff`** runs and draws as the default does (c40, 72 frames: the same jerk, 0.090 against 0.085 px; the level's translation then rounded in the words, by up to about 1/1000 of a unit).

**The pictures are the same** [E]. On against `levellocaloff`, shot for shot:
- c40's opening, 72 frames in runs of six: 195,201 pixels differ over all 72 (up to 9,300 a frame, t 21–24). Tile offsets between the sides: median 0.000–0.004 px, the largest 0.09 px. The differences are the Monitor's glowing shell (it differed between lk22's own two sides as much: run-to-run, not the level) and single pixels along edges. The level's edges, which moved by whole pixels between lk22's sides, don't move.
- a10's flyby, 72 frames: 4,124 pixels differ over all 72.
- d20 (9f27f33), 72 frames in runs of three: ~2,000 pixels a frame once the view stands still, single pixels along edges and the dithered lightmap's speckle: the 0.24-mm snap moves an edge's sub-pixel position now and then.
- b30: up to t 59, 52–1,180 pixels a frame; after that the enemies differ, because b30's play does (below).
- **The motion is the same** [E]: the median frame-to-frame jerk (how much each 64-px tile's motion changes from one frame step to the next) on against off: c40 0.085 / 0.090 px, a10 0.131 / 0.133 px. The emulator showed the level steady already with lk22; levellocal keeps it so. Whether it removes the PSP's dance only the PSP can say.

**Play exact** [E], hands off, `statecrc`, on against `levellocaloff` (`tools/statecmp.py`): the play hash and both random seeds the same at every tick on **d20 (17,734 ticks), a50 (29,266) and c20 (24,666)**. The whole state differs only where it follows the emulator's speed (`objects`, `contrail point`, `first person weapons`), as in every such pair.
- b30, not one of the exact maps: on against on again, **everything the same at all 6,952 ticks**; on against off, the play hash agrees through tick 1,325 and differs from 1,326. That is b30's known reach across anything that changes the emulator's speed (1,326–1,357, `big_levers.md`; findings, w118 `play_hash_reach`). lk22's own b30 pair parted the same way (its enemies differ from t 110).

**The multiplayer build** (`PSP_MULTIPLAYER=1`) compiles, starts on Blood Gulch and plays 9,028 ticks with no fault; its stencil bake draws 2,400 layer draws a window on the walls' pieces. The renderer's copies aren't simulation, so the server's game isn't touched.

**The origin's moves** [E], from the log's new line: c40's opening never moves it (352, −704, 64). c20 walking: 2 moves in a window, 110 copies, 36,958 vertices, 8.6 ms all told, ~4 ms a move. a10's flyby: 14 moves in a window, 707 copies, 180,564 vertices, 17 ms, ~1.2 ms a move. b30: 5 moves, 29,659 vertices, 5.0 ms. No copy was ever out of exact reach.

## 5. Cost

- **Frame time** [E]: c40 on against off, the same to 0% in every stretch; a10 0 to −3% (on not slower); b30's first stretch (before its play parts) +0%.
- **Display list** [E]: one model matrix (13 words, 52 bytes) per level draw. c40's level: 49.4 KB a frame with lk22, 50.5 with lk22b. Lights and shadows loaded the world's matrix before and load the copy's now: the same.
- **The origin's moves**: above, ~1–4 ms once per move, at the frame's end. On foot that is every 25–40 units walked; a fast flyby moved it about every 20 frames.
- **Memory**: the copies' origin, 12 bytes × 512 slots, and 16 bytes per stencil layer. Program text +8.8 KB (3,460,320 → 3,469,300 bytes; much of it the instrument), bss +6.2 KB; the heap 5,632 → 5,616 KB (sized in 16 KB steps from the program's end).
- CPU per draw: three subtractions (`level_frame`) and a matrix load.

## 6. What Sheepdog01's PSP must confirm

Hardware is the only judge here: the emulator showed no dance even before.

1. **`geprecision`** (one launch; keep.txt line `geprecision`; any map; quit once the game shows, ~20 s): send `log.txt`. Its 12 `geprecision:` lines say whether the PSP's GE loses precision on large positions (section 3) and whether lk22b's 32-unit lattice is fine enough.
2. **c40's opening**, the build as it is, then with **`levellocaloff`** (the drawing before lk22b): the terrain, bridge, doorway and pillars should hold still against the camera with the build as it is and dance as before with the off word. The dark grey pillars on the back wall should stop flickering (if they still do, and `geprecision` is clean, that flicker is depth, not position).
3. **a10's exterior flyby**: the frame round the bridge glass steady, on against off.
4. **Watch for, new with this change**: hairline cracks between level surfaces for a frame after the origin moves (copies briefly on different origins; none expected once they all move at the frame's end); a stutter when it moves (the log's "the level's origin" line gives the moves and their time each 300 frames); bullet holes and blood sitting on the walls as before; Blood Gulch's base light strips and symbols (the stencil bake's layers, now given from their wall's origin) whole.
