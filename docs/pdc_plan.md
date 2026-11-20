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

## Stage A on the PSP, and the route it sets (2026-10-29)

**The share holds on the PSP** [M]. Lab sierra-hw 40954191918, 40956643111 and 40961270594 (pdcprobe; c10, PSP-1000; the ME's own cycle counter):
- 143-155 model parts a frame, of which 62-71% hit (keyed as last frame);
- the hits' build is 8.4-11.6 ms of the models' 13.6-18.9 on the ME, settling at 8.8 of 14.2.

**Where the ME's model time goes** [M] (meparts, 40961270594), ms a frame on c10:

| Step | ms a frame |
|---|---|
| bind + state + lighting | 5.0 |
| geometry | 3.8 |
| two-sided and change/glow passes | 3.1 |
| convert | 0.9 |
| node matrix | 0.4 |

The emulator's split of the same steps matches the PSP's proportions (mepf2 40966334820: 41/25/23/7/3% against 38/29/24/6.5/3%), so its finer split is taken as where the time goes [E]. Of bind + state + lighting:
- texture bind 27%;
- lighting 22%;
- reflection and ambient 18%;
- material state 14%;
- the rest 19%.

**What that rules out** [D, U]:
- **A memo of the decisions alone** (the same words re-sent through pspgu) takes ~0.5-1 ms. Only reflection and ambient is pure decision; the rest is pspgu calls writing words.
- **A plain copy through the list.** The command words are uncached stores, on the ME too: pspgu's sceGuStart sets bit 30, and `listcachedall` is off with gemeon. A copy of a hit's ~140 words is still ~140 bus stores, unless they go into a region taken through `list_take`'s cached path (lines made with `cache 0x18`, written back at the stall update).
- **The CALL form, for now.** Lab api-job pdcdesign 40973501562 (Opus; quotes spot-checked) found:
  - the bone sub-lists' CALLs change each frame inside a part (one a batch);
  - a rigid part's matrix is written into the part's own words (`sceGumLoadMatrix`);
  - light, fog and dither words change inside the block;
  - a block the ME wrote would need a write-back of its own, which the emulator can't check.

**The route: the copy form, into cached takes, rigid parts first** [U]. A hit's words are kept in a store of their own (not last frame's list, which geshow3 may reuse), and copied into a cached take. For the frame to stay bit-identical, a hit must also:
- re-take each sub-region and rewrite its addresses at recorded positions. A take of 256 bytes or more is aligned to a line, so one delta can't relocate a part;
- patch the matrix words, the point lights' places (0x63-0x6E) and the visor pass's lights;
- set the statics a build leaves: `dither_now`, `blend_now`, `lit_points_maybe_on`, `model_ambient`, `world_model()`, `model_draws_this_frame`;
- replay the texture marks (`me_book`), the bind counts and the VRAM copies' tallies (`vm_me_tally`), or a texture in use can be evicted;
- replay the stall updates, so gebusy's SIGNALs and the GE's feed stay as built;
- check the list's room for the whole part first, and build as now when it doesn't fit;
- hit only on the ME (`psp_on_me()`): the main CPU's replay has the lit_ and texture memos live.

The key gains the dither and blend state a part starts from, and fog.

**Steps:**
1. **`pdcreusecheck`:** each hit's copy is built into scratch and compared word by word with what the build made, BASE and the view and projection words included (`pdcprobecheck` skips both), plus the marks and tallies. The gate is 0 differences on b30, c10, c20 and d20. b30 and d20 still show 3-23% of hits differing in palette and index data.
2. **Rigid parts** (about 60 of c10's 143): gestream frames identical with and without the keep word, play and seeds. About 2.5-3 ms of c10's ME [U].
3. **Skinned parts:** the bone sub-lists rebuilt into fresh takes and their CALLs rewritten.
4. **The point lights patched.**
5. **The c10 pair on the PSP-1000:** `"- | pdcreuse"` x2, reading the ME's replay, the main CPU's wait and the key's own cost (logged since projects/sierra/research/pdcreuse bc3513e5).

In full, about 5.5-6.5 ms a frame of c10's ME [U]: the hits' 8.8, less the matrices, the bone rebuild, the copy, the replays and the key.

**The bind step has a cheaper first lever** (survey texgeoperf, `docs/frame_side_surveys.md`): the bind blocks and the bind memo are both off on the ME (`psp_tex.c` `if (bind_blocks_on && !PSP_ME_WORK())`). Blocks made by the main CPU at pretouch and only CALLed by the ME would take part of the texture bind's ~1.4 ms on every part, hit or not. The GE allows two levels of call: the frame's list calls the block.

The plan below is stage A's, kept for its reasoning. Where it differs from this route, this route holds:
- its key is narrower than the code's;
- it has one bone sub-list a part, where the code has one a batch;
- it says the matrices sit in sub-lists, which holds only for skinned parts.

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
