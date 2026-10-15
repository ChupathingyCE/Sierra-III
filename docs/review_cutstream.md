# Review: cutstream (no cut piece dropped; where the cutting goes)

Branch `cutstream` (from `w123`). Sheepdog01 (2026-10-14): a bigger cut buffer is duct tape; chase the root cause of the Pelican wall.

Files: `psp/src/psp_ge.c` (the change), `psp/src/psp_platform.c` (the keep-word line), `psp/presets.txt` (preset 13 uses `cutstreamoff`). Lab batches `cutstream1` to `cutstream11` on lab main; results on the results branch under `sierra-batch/<run>`.

## In short

| | Default | Word |
|---|---|---|
| **The fix:** cut pieces past the fixed buffers go on into display-list blocks, so none is ever dropped | **on** | `cutstreamoff` = b122's fixed buffers |
| **Off-screen skip:** triangles and pieces wholly past a side of the screen aren't cut or stored. −1.1 to −1.4 ms a frame of renderer CPU [E] | off | `cutless` (Sheepdog01's A/B) |
| **Wider guard planes** (1920 px, the GE's limit with margin) | off | `guardwide` (Sheepdog01's A/B) |
| Counts by kind in the 300-frame log | off | `cutcount`; recon `cutlessmaskN` |

Removed: `cutroom`, `cutroomoff` and b123's heap buffer grown on demand.

## The root cause, measured

**1. A fixed staging buffer with a silent drop [D], losing pieces on every map tested [E].** Every CPU cut path wrote its pieces into a fixed buffer (the models' 192 corners in the scratchpad, the level's 384) and dropped what didn't fit. `cutstreamoff` with `cutcount`, hands off from the start, `fixedticks fixedseed` (lab 38653693106):

| Map (frames) | Pieces lost | A frame | Where (the worst one draw needed, against its room) |
|---|---|---|---|
| b30 (2,101) | 92,884 | 44 | bending parts ~77,700 (621 corners a batch vs 192; over the room in 1,965 of 2,101 frames), rigid 15,011 (507 vs 192) |
| c40 (4,201) | 21,483 | 5 | see-through parts 11,164 (1,134 vs 192), bending ~10,300 (561) |
| a10 (4,201) | 190,193 | 45 | rigid 133,376 (654), bending ~35,100 (1,152), see-through 11,658 (1,365), the level 10,058 (735 vs 384) |
| d20 (2,701) | 2,374 | 0.9 | bending ~1,500 (1,062), rigid 840 (564) |

(Bending parts' losses are whole triangles, counted only in the total. "Worst" is from the stream's runs, where nothing is lost.) So the Pelican's wall wasn't a one-off: the buffers are too small for many views on every map, and the level's own 384 overflows too (a10).

**2. Most of the cutting is for nothing on screen [E].** Of the triangles sent to be cut, 68–94% lie wholly past one side of the screen, and 70–95% of all pieces made land wholly off it:

| a frame | triangles cut (near / guard planes only) | wholly off the screen | pieces | pieces off the screen |
|---|---|---|---|---|
| b30 | 284 (101 / 183) | 268 | 444 | 420 (95%) |
| c40 | 231 (70 / 161) | 215 | 361 | 336 (93%) |
| a10 | 294 (149 / 145) | 271 | 460 | 428 (93%) |
| d20 | 211 (181 / 30) | 143 | 342 | 239 (70%) |

Why: the guard planes sit 6 screens out, so a triangle beside the eye that crosses the near plane, or reaches past the screen's side, goes through `clip_triangle` even when none of it is on screen. These off-screen pieces are also what overflowed the buffers. With them left out (`cutless`), b30's worst bending batch needs 120 corners, inside the 192.

**3. The guard planes' width is not the waste [E].** At 1920 px (`guardwide`), triangles cut only at a guard plane fall (b30 150 → 117 a frame), but triangles cut at the near plane rise (97 → 145). A triangle beside the eye that was wholly past a 6-screen plane used to be dropped; at the GE's limit it isn't, so it gets cut, into off-screen pieces. In all: b30 247 → 263 a frame, c40 229 → 213, a10 287 → 265, d20 211 → 209.

## The change

### 1. The cut store (on)

Each path now cuts into a `struct cut_store`:
- the first corners go into the same fixed buffer as before (the scratchpad stays the common case);
- past it, 96-corner blocks are taken from the display list (`list_take`) and linked;
- each path's conversion loop reads corner `i` through `cut_at(store, i)`, and writes its GE vertices as before.

Every path with a fixed room goes through it: rigid, bending and see-through parts; the level; its see-through draws; stencil layers; lights; shadows.

```c
static inline float (*cut_piece(struct cut_store *store))[CORNER_FLOATS]
{
	if (store->count + 3 <= store->head_room)   /* the fixed buffer, as before */
	...
	return cut_piece_spilled(store);            /* past it: a block of the display list */
}
static inline const float *cut_at(struct cut_store *store, int index)
{
	return index < store->head_room ? store->head[index] : cut_at_spilled(store, index);
}
```

- **Why the display list:** it is already where the pieces end up, sized for the whole frame (768 KB), and it costs nothing until a draw overflows. The GE never reads a block, because `sceGuGetMemory` jumps over it, and the next frame reuses the memory. b30 averaged 3.2 blocks a frame (13.6 KB); a10 averaged 2.1 [E].
- **Why keep the float staging rather than write GE vertices straight into the list:** a list region can't grow once taken, and the count is known only after cutting. Writing directly means reserving the worst case (18 corners a triangle), or chains of regions that eight consumers would each have to walk: the passes, the decals, the see-through queue, the light and shadow draws. Staging keeps every path's conversion and draws unchanged.
- **The cache rule [D]:** a block is filled over many pieces, so lines can be written after `list_take`'s own write-back has passed. A dirty line left in the data cache would later be written back over the next frame's commands. `cut_spill_settle` writes back and drops (`cache 0x1B`) each line a spilled piece or a block's link touched, right after the write. The emulator has no caches, so the PSP must confirm it: any run that spills (the Pelican bench) would show a GE hang or a garbled frame if a line were missed.
- **`cutstreamoff`:** the fixed buffers alone, pieces past them lost, with the bending path's old whole-triangle room check kept exactly.

### 2. `cutless` (opt-in)

A triangle whose three corners lie past one of the screen's side planes (8-pixel margin) is not cut, and a piece wholly past one is not stored (`piece_off_screen`, at the top of `clip_triangle` and in its fan loop). The planes pass through the eye, so such a triangle lies wholly in the outer half-space at any depth, and every piece of it too. Decisions that asked "did this draw have cut pieces?" also count what was left out (`store->dropped`): the level's near detail passes (`angle_near_any`), and the early returns of the level, rigid parts, lights and shadows.

### 3. `guardwide` (opt-in)

`guard_x = guard_y = 1920 * pixel` (a pixel's width at depth 1), against 6 screens (1440 across, 816 up and down). The same planes drive every "all inside" shortcut (box, sphere and run tests, the ME's classification).

## Evidence

**Pixel diffs**, shots every 5 s from the start, hands off, `fixedticks fixedseed` (24 a run; the bottom rows hold a time readout and are left out):

- **The default (the stream), against `cutstreamoff` (lab 38661970116):** c40 96 pixels over 5 shots, a10 129 over 12, d20 4 over 2, at most 17 [E]. That is the restored pieces, inside the same-code noise (below). Lost pieces in those runs: 92,884 / 21,483 / 190,190 / 2,374 with `cutstreamoff`, 0 with the stream, on b30 / c40 / a10 / d20.
  - b30 shot 2 (in the Pelican, 15 s): the old path shows sky through the top-right of the cabin where the Pelican's frame lost its pieces; the stream draws the frame (1,633 pixels, all in that corner). Shot 6 is the same, with 3,103 pixels.
  - From shot 11 (the beach) b30's play diverges, the known b30 divergence (marines in other places), set off by the change in CPU time. Those shots can't be compared. No shot faces the wall behind the bench: hands off, the seated view doesn't turn.

- **`guardwide`, against the 6-screen planes (lab 38653693106):** c40 8,248 pixels over 21 of 24 shots, a10 1,666, d20 14,302. Mostly one colour step, with a few edge pixels up to 124 on c40 [E]. A triangle drawn whole instead of as pieces interpolates its colours and edges a little differently. Not identical, so the planes stay at 6 screens. The difference is invisible at a glance, and per `CLAUDE.md` a speed-up that looks the same can ship after Sheepdog01's side-by-side. But it doesn't cut less (above), so it isn't worth his time on its own.
- **`cutless`, against the stream alone (labs 38657862012, 38659654507, 38660503519):** c40 1,174–1,248 pixels over 21 of 24 shots, a10 300–1,205 over 16, d20 279–287 over 22. One or two colour steps in scattered pixels, a few up to 57 [E].
  - Larger and steadier than the noise: the same rendering on two builds differs by 430–480 pixels in 2–3 shots on c40 (0–3 on a10), and changing CPU time alone (`cutcount` on or off) by 2–52 pixels in 1–5 shots.
  - Every kind contributes (`cutlessmaskN`, one kind at a time: on c40 the level 1,917, see-through 1,101, the lights' group 1,274, bending 514, rigid 24). So it isn't one path's decision.
  - On a static d20 view the same 11 pixels differ in every shot, by one step [E]. No piece wholly off the screen should draw a pixel; why the frames move is **not found** [U].
  - So `cutless` is opt-in. To settle: a screenshot of one frame drawn both ways, or a GE trace of one frame, to see which draw's pixels move.

**Play (state CRC pairs, hands off, `statecmp`), the stream with `cutless` against `cutstreamoff` (lab 38656632179):** the play hash and both random seeds are the same at every common tick on d20 (14,354), a50 (23,688) and c20 (29,100) [E]. The whole state differs only in the allocations that follow the emulator's speed (`objects`, `contrail point`, `object looping sounds`, `first person weapons`), as in earlier exactness checks. c20's `lights` also differs at a few checkpoints, which earlier reviews didn't see; it is [U] whether that is speed-following too. The default draws a subset of what that run drew (the stream without `cutless`).

**Strict interpreter**, b30 with `cutless guardwide cutcount` (lab 38656632179): 27 spill blocks taken, no bad access, nothing lost [E]. **Multiplayer:** the `PSP_MULTIPLAYER=1` build compiles and plays Blood Gulch (24 shots) [E].

**CPU time** (renderer CPU a frame, the GE entry timers, emulator [E]; windows 2–10 or 2–12 of 300 frames, no `cutcount`):

| Renderer CPU, ms a frame | `cutstreamoff` | default (stream) | `cutless` | `cutless guardwide` |
|---|---|---|---|---|
| b30 (lab 38661970116/38662008898) | 28.16 | 28.26 | **26.80** (−1.36, −4.8%) | 26.56 (−1.60) |
| d20 (lab 38662008898) | 22.94 | 22.89 | **21.80** (−1.14, −5.0%) | |
| c40, the stream against `cutless` (lab 38659654507) | | 12.99 | **11.65** (−1.34, −10%) | |
| a10, the same | | 17.68 | **16.27** (−1.41, −8%) | |

Where `cutless` saves on b30: the level 0.19 ms, model parts 0.73, see-through models 0.25, see-through level 0.14, shadows 0.24. On d20, the level 0.57 and the light pass 0.49. The stream alone costs nothing measurable (b30 +0.1 ms for the 44 pieces a frame it draws again, d20 −0.05).

The models' figure rises a little with the stream: b30's old path lost 44 pieces a frame and skipped their work.

## What's left

- **The PSP:** the spill blocks' cache write-back (a Pelican bench run; the wall should be there, and no hang), and whether `cutless` saves on the PSP what the emulator says.
- **Why `cutless` moves pixels.** Until that is found, it stays opt-in.
- **For the wide-frustum VFPU test (research item RS, `docs/research/research_b15_b17_sources.md`):**
  - The measurements change its premise. The waste isn't the cutting itself (`clip_triangle`'s planes) but the triangles sent to it: 68–94% are wholly off the screen. The cheap reject belongs earlier, in the classification. Give `outside()` four more bits for the screen's side planes, so a triangle wholly past one is dropped before its corners are built (`to_camera_corner`, 11 floats each). That is where a VFPU version, testing a vertex against all nine planes in a few instructions, would pay.
  - Moving the guard planes outward (dQuakePlus's "wide frustum") doesn't help here (cause 3).
  - Everything depends on the same exactness question as `cutless`.
