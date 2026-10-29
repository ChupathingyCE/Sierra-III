# Prebuilt drawing commands (PDC): the census and the build plan

Sheepdog01, 2026-10-29 14:34: "Cheaper draw calls are a good idea to chase. That means more headroom on the ME." Sheepdog01's third restructuring direction (14:00) is each model's GE commands built once, its matrices patched each frame, and the model called as a sub-list. This is its census and its plan. Nothing is built yet.

Labels: [M] measured on the PSP, [E] emulator, [D] read in code, [U] inference.

## Where the work is now [M]

The campaign defaults (gedefer, geinflight, gemeon) split the work:
- the main CPU only records each GE-layer call;
- the ME carries the calls out, writing the frame's list beside the next frame's ticks.

So the per-draw command building is the ME's, and it is large:

| Map | The ME's replay a frame | The frame | Run |
|---|---|---|---|
| b30 (PSP-3000) | 70-78 ms | 115-170 ms | sierra-hw 40888171017 |
| c10 (PSP-1000) | 30-37 ms | 53-55 ms | 40889089669 |
| c20 (PSP-1000) | 21-42 ms | 93-138 ms | 40893621828 |
| d20 | no current PSP figure | | |

On c10 the ME is 57-62% busy, and the main CPU already waits 1-2 ms a frame (worst 9) for its render job. At a 33 ms frame the ME, not the main CPU, would be the limit on c10.

## The census [E]

Lab sierra-batch pdc1 40942209711 ran 90 s a map with the halo-local patch's item 25 (`pdccensus`) and the game's `pdcmark`. How it counts:
- Each word of a frame's list is counted once, at its first execution.
- Each draw is matched to last frame's draw with the same kind, vertex and index address, primitive, vertex type and ordinal.
- A word is **kept** if last frame's matching draw had the identical word.

In the emulator the frames ran at 1.1-1.6 ticks a frame, near the 30 fps target's one tick a frame. The PSP's frames today run 2-5 ticks, so they keep less.

| Map | Words a frame | Kept | Models' words | Models kept | Models kept, matrices left out | Model matrices kept | Level words, kept |
|---|---|---|---|---|---|---|---|
| b30 | 46,266 | 69% | 37,789 (82%) | 76.7% | **97.0%** | 42% of 14,053 | 2,484, 29% |
| c10 | 26,012 | 71% | 20,237 (78%) | 89.5% | **98.0%** | 69% of 5,849 | 2,899, 0.1% |
| c20 | 19,565 | 66% | 7,843 (40%) | 64.9% | **98.1%** | 28% of 3,721 | 7,274, 70% |
| d20 | 16,360 | 32% | 6,294 (38%) | 69.4% | **94.0%** | 30% of 2,436 | 3,981, 6% |

Per group, a model part's words repeat almost entirely, apart from its matrices:

| Group (models) | Kept |
|---|---|
| geometry | 98-99% |
| texture | 98-99% |
| render state | 97% |
| lighting and colour | 94-97% |

Almost every model draw finds last frame's match: 544 of 552 a frame on b30, 282 of 285 on c10.

**What it says:**
- **A model part's commands are the same as last frame's, apart from its bone matrices,** and those already sit in sub-lists of their own (the models' share line: 587 sub-list calls a frame on b30). So a part's command block can be kept and called again, with only its matrices written.
- This revises the first sizing (findings @c4493ed03: ~4-10 ms on b30), which took X2's narrower "fixed state" probe as the kept share. The census measures the same part on the same object frame to frame, and nearly all of it repeats.
- **The other kinds keep little as counted.**
  - The HUD, decals, shadows and the see-through queue build their vertices in the frame's own memory, so their vertex addresses change each frame and they never match. That undercounts them; they are smaller anyway.
  - The level depends on the map: c20's level keeps 70%, c10's 0.1% and d20's 6%, because its cut-up and index copies are made per frame. The level is B's, and its baked per-cluster lists are the form that keeps it.

## The ME saving it implies [U]

The saving is the ME's replay, times the models' share of it, times the share of the models' work that's removable, less the cost of checking that a part is unchanged:
- **Removable share:** the non-matrix words kept, against all the models' words: b30 61%, c10 70%, c20 52%, d20 58%.
- **The models' share of the replay** is between two measures:
  - their share of the words: b30 82%, c10 78%, c20 40%, d20 38%;
  - b30's older ME split by kind (mekinds, the render ownership table): models 61% of the GE replay against 82% of the words, because the level's work costs more a word.
- **The check is taken as 20%** of what it saves.

| Map | The ME's replay | Saving | What it means |
|---|---|---|---|
| b30 | 70-78 ms | **~22-30 ms a frame** | |
| c10 | 30-37 ms | **~11-14 ms a frame** | takes c10's ME from ~33 ms busy a frame (57-62% of a 55 ms frame) to ~20, well under a 33 ms frame |
| c20 | 21-42 ms | ~4-5 ms | |
| d20 | no PSP replay figure | ~58% of the models' share of whatever it is | |

Only a prototype measures it. The PSP's frames move more objects between frames than the census's, so the saving today is smaller than at the target. It grows as the frame rate rises.

## The build plan

**Step 1 [E]: retained part blocks on the ME (keep word `pdcreuse`, off by default).** In the replay, each model-part record keeps a block in a retained arena.
- **Key:** the record's own bytes, everything but its node matrices. That covers the part, its geometry and texture, its lighting and colours, the LOD and the passes, with the ordinal among equal keys. A record identical to last frame's then makes identical words.
- **On a match:** the frame's list calls the kept block, after a call to this frame's bone sub-list. The block's draws use the bones the sub-list loaded.
- **Call depth:** frame list, then block, then the batches' bone sub-lists is two deep, which the GE allows. Its third return address doesn't exist (halo_gecall).
- **On a miss:** the part is built as today, into a new block, and kept.
- **Lifetime:** a block is freed only after the GE has finished the frame that last called it. geinflight already tracks the frame the GE is on.
- **Room:** b30 ~95 KB of kept words a frame, c10 ~57 KB, plus the copied data blocks (indices, palettes: b30 68 KB). With replaced blocks waiting out their frame, that's ~200 KB on b30 at most.
  - The heap's largest block is 247-346 KB at its lowest (walkr2). The GE can't read eDRAM, and VRAM is full.
  - A cap on the arena keeps it inside what the heap has.
- **Proof:** gestream frames identical with and without the keep word (b30, c10, c20, d20), with play and seeds unchanged (it changes no state). Then pdccensus again, to show the words written a frame falling.

**Step 2 [M]: a c10 pair on the PSP-1000.** Keep sets `"- | pdcreuse"`, repeat 2, 4 runs. It measures the ME's replay a frame (gemeon's own time), the main CPU's wait for it, and the frame. c10 is where the ME is closest to binding.

**Step 3:** the matrices of objects that didn't move: 42% of b30's model matrices and 69% of c10's are identical. A node-matrix stamp from the tick lets those bone sub-lists be kept too. RSO's still-object test is the same idea on the main CPU's side, and together the two skip both the recording and the building of a still object.

**Step 4:** the kinds that build vertices per frame (decals, shadows, see-through, HUD), and the level through B's baked lists.

**Exactness:** identical records make identical words, so the frames are the same bit for bit. It changes nothing the server reads, so the multiplayer boundary isn't touched.
