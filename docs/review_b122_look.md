# Review: build 122's look calls (branch `look122`)

Four of Sheepdog01's open calls (`handoff_2026-10-13c.md` §3 row 5, "Look batch"), each behind its own keep word and **off by default**, so one build carries both sides of each A/B. Three are new code in `psp/src/psp_ge.c`; `sceneryhalf` is build 107's and only checked here. No fixup pairs (nothing in FIXES_122). The words are in the log's start-up list (`psp_platform.c`) and in `optimization_ranking.md` appendix B.

Labels: [M] PSP, [E] emulator, [D] code, [U] inference. Emulator figures are JIT runs with `fixedticks fixedseed` (the same frames in both runs of a pair), lab runs named below; the emulator's GE and VFPU timings are its own, so only the PSP can say how many milliseconds.

| Keep word | What | Kind | Emulator [E] | Default |
|---|---|---|---|---|
| `seethroughvfpu` | c40's multi-node see-through parts placed on the VFPU | speed, not bit-exact | pictures identical; see-through models −3% | off |
| `lightmerge` | the light pass's bands and cut pieces as one draw | speed/look trade | light seams slightly thinner; cost below | off |
| `foliagetwopass` | alpha-tested model parts get a soft second pass | look, costs speed | softer leaf edges; frame +13% on c10 | off |
| `sceneryhalf` | sleeping scenery checked every other tick (b107) | speed/behaviour | see §4 | off |

## 1. `seethroughvfpu`: see-through parts on several nodes placed by the VFPU

**What.** A see-through part whose vertices hang from several nodes (Cortana, Guilty Spark, a Warthog's lights) is placed on the CPU vertex by vertex, each by its own node. On c40 that placement is most of the see-through queue, 18% of its drawing's instructions (`b118_candidates.md` M1). With the word, each node's matrix goes into the VFPU once per run of vertices on that node, and a vertex is three `lv.s`, one `vhtfm4.q` and three `sv.s`.

**Key lines** (`psp_ge.c`):
- 9639–9661: the comment, `seethrough_vfpu`, `SEETHROUGH_VFPU_LOAD` (the matrix's rows into `R100..R103`, so column *a* of `M100` is row *a* with its offset; `vhtfm4` with (p, 1) is then the FPU's three sums) and `seethrough_vfpu_place`.
- 9730: `on_vfpu = seethrough_vfpu && !PSP_ME_WORK();` the Media Engine has no VFPU, so its share of the GE layer keeps the FPU path.
- 9750–9757: the matrix is reloaded only when the node changes (`loaded`); then the vertex is placed and the FPU lines skipped.
- `node_at` is now `aligned(16)`: `lv.q` needs it.

**What the player sees.** Nothing: over c40's opening (24 shots at the same frames, Cortana and Guilty Spark in view, 200,960 vertices placed on the VFPU per 300 frames) **no pixel differs by more than 8 levels** [E]. The VFPU sums in its own order, so a vertex can still land a last bit away.

**Cost/gain** [E, lab 38580939224]: the see-through models' CPU 2,919 → 2,826 µs a frame (−3.2%); the GE renderer's CPU −0.5% (12.68 → 12.61 ms a frame). Small, as `vfpuskin` was; whether the PSP agrees is its A/B.

## 2. `lightmerge`: the light pass's bands drawn as one

**What.** No write-up defines "d20's light-pass merging" beyond its name, so this is the reading taken from the code [D]: in mode 2 (the default) each light-and-surface draw sorts its kept triangles into up to six distance bands, makes one pass over every kept triangle per band to copy that band's triangles, and draws each band with one depth nudge sized for its nearest corner; the cut pieces are a seventh draw, already lifted corner by corner (b117). With the word, every corner is lifted by its own distance, and kept triangles and cut pieces go to the GE as one draw. **If Sheepdog01 meant something else by "merging" (several lights on one surface in one pass, say), this is not it.**

**Key lines** (`psp_ge.c`):
- 12643–12684: the comment, `light_merge`, and `light_lift_corner_merged`: the lift as a share of the depth, min(1%, 0.2 units / depth), kept between 3 and 4096 depth steps and short of the near plane, one division a corner. It skips `light_lift_corner`'s rounding to whole depth steps (a sub-step difference).
- 13201–13233: the merged draw; it zeroes `kept_count` and `cut_count`, so the band and cut-piece blocks after it do nothing.
- Only with `lightcutone` off (the default), since the per-corner lift is what `lightcutone` turns off.

**What the player sees** [E, `b122_look_lightmerge.png`]: a band's nudge can lift a corner up to four times what its own distance asks; per corner, each gets its own. In d20's corridors the bright light seam along a ceiling/wall edge is **thinner** with the word (less light leaking past the edge); elsewhere the same. 2,185 pixels differ by more than 8 levels over 24 shots.

**Cost/gain** [E]: the first version (calling `light_lift_corner`, three divisions a corner) made d20's light pass **dearer**, 14.5 → 17.1 ms a frame (+18%, lab 38580939224). The committed version (one division) is measured in lab 38583595807: see "Still open" if that run hadn't finished.

## 3. `foliagetwopass`: alpha-tested parts with soft edges

**What.** An alpha-tested model part (leaves, grass, fences) keeps only texels over half alpha (`GU_GREATER 0x7F`), so its edges are hard, and a far plant shimmers as its mips thin (LK8). With the word, such a part is drawn a second time right after its first pass: blended by its alpha, only where it is nearer than what is there (`GU_GREATER`: pass 1's own texels have equal depth, so they are left alone), and without writing depth. Two-sided parts get it on both sides.

**Key lines** (`psp_ge.c`):
- 7064–7091: the comment and `foliage_soft_pass` (alpha test `> 0`, depth `GREATER`, depth writes off, `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`; then every state back as the model path expects it).
- 8619–8623 and 8644–8645: which parts (textured, the texture has alpha, the shader alpha-tested, not camouflaged, not the sky) and the two calls.

**What the player sees** [E, `b122_look_foliagetwopass_c10.png`, the 3x crop]: c10's ferns and palms get soft, anti-aliased edges instead of stair-stepped ones. Known side effect [D]: the faded fringe has no depth, so something drawn later behind it covers it. The level's own alpha-tested surfaces and c10's hanging vines (LK7, still solid bars) are not changed by it.

**Cost/gain** [E, lab 38580939224, c10's swamp]: model parts' CPU 3.1 → 3.5 ms a frame (+14%); the GE renderer's CPU +5%; the CPU now **waits for the GE 6.1 ms a frame** (0.01 without), so the frame by the clock is 45.7 → 51.6 ms (+13%). 47,239 second passes per 300 frames. It is the most expensive of the four; the PSP's fill rate decides it.

## 4. `sceneryhalf` (build 107, unchanged)

Still in `psp_fixups.py` (3011–3031, 3160) on w122 [D]: a sleeping scenery object is checked on alternate ticks, half of them each tick by index. Its heartbeat line ("sleeping scenery, checked every other tick") shows it working. On the PSP it gained nothing measurable in b112 (63.2 ms a tick with and without, `hardware_b112.md`) [M]. It changes when the simulation notices a change to scenery, so it stays off for multiplayer play against a server unless Sheepdog01 decides otherwise [U].

## Multiplayer

The three new words are drawing-only (`psp_ge.c` is shared) and change nothing the server sees. The multiplayer build (`PSP_MULTIPLAYER=1`) compiles and links with them (local build at `f1eeea7`: 490 of 490 units, heap 4,644 KB) [D].

## For Sheepdog01's A/B

- `seethroughvfpu`: c40's opening, the cinematic bench's way: a speed question only (the pictures match).
- `lightmerge`: d20's corridors by eye (the seams), and the light pass's time.
- `foliagetwopass`: c10's swamp or b30's beach by eye (plant edges, shimmer while moving), and the frame's cost.
- `sceneryhalf`: already measured as no gain [M]; it can stay off.

## Still open

(filled in at the end of the session)
