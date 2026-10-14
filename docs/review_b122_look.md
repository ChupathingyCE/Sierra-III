# Review: build 122's look calls (branch `look122`)

Four of Sheepdog01's open calls (`handoff_2026-10-13c.md` §3 row 5, "Look batch"), each behind its own keep word and **off by default**, so one build carries both sides of each A/B. Three are new code in `psp/src/psp_ge.c`; `sceneryhalf` is build 107's and only checked here. No fixup pairs (nothing in FIXES_122). The keep words are listed in the log's start-up lines (`psp_platform.c`) and in `optimization_ranking.md` appendix B.

Labels: [M] PSP, [E] emulator, [D] code, [U] inference.

| Keep word | What | Kind | Default |
|---|---|---|---|
| `seethroughvfpu` | c40's multi-node see-through parts placed on the VFPU | speed, not bit-exact | off |
| `lightmerge` | the light pass's distance bands and cut pieces as one draw | speed/look trade | off |
| `foliagetwopass` | alpha-tested model parts get a soft second pass | look, costs speed | off |
| `sceneryhalf` | sleeping scenery checked every other tick (b107) | speed/behaviour | off |

## 1. `seethroughvfpu`: see-through parts on several nodes placed by the VFPU

**What.** A see-through part whose vertices hang from several nodes (Cortana, Guilty Spark, a Warthog's lights) is placed on the CPU vertex by vertex, each by its own node (build 42). On c40 that placement is most of the see-through queue, which was 18% of its drawing's instructions (`b118_candidates.md` M1). With the word, each node's matrix goes into the VFPU once per run of vertices on that node, and a vertex is three `lv.s`, one `vhtfm4.q` and three `sv.s`.

**Key lines** (`psp_ge.c`):
- 9646–9661: `seethrough_vfpu`, `SEETHROUGH_VFPU_LOAD` (the matrix's rows into `R100..R103`, so column *a* of `M100` is row *a* with its offset) and `seethrough_vfpu_place`.
- 9730: `on_vfpu = seethrough_vfpu && !PSP_ME_WORK();` the Media Engine has no VFPU, so its share of the GE layer keeps the FPU path.
- 9750–9757: the matrix is reloaded only when the node changes (`loaded`), then the vertex is placed and the FPU lines skipped.
- `node_at` is now `aligned(16)`: `lv.q` needs it (an unaligned quad load corrupts on the PSP-1000).

**What the player sees.** The VFPU sums the products in its own order, so a vertex can land a last bit away from the FPU's. Nothing visible is expected [U]; the shots are the check.

**Cost/gain.** See "Evidence" below.

## 2. `lightmerge`: the light pass's bands drawn as one

**What.** No earlier write-up defines "d20's light-pass merging" beyond its name, so this is the reading taken from the code [D]: in mode 2 (the default) each light-and-surface draw sorts its kept triangles into up to six distance bands, makes one pass over every kept triangle per band to copy that band's triangles, and draws each band with one depth nudge sized for its nearest corner; the cut pieces are a seventh draw, already lifted corner by corner (b117). With the word, every corner of the kept triangles is lifted by its own distance the same way (`light_lift_corner`), and the kept triangles and cut pieces go to the GE as one draw. That is fewer draws, no band passes and one copy.

**Key lines** (`psp_ge.c`):
- 12643–12653: the comment and `light_merge`.
- 13172–13208: the merged draw; it zeroes `kept_count` and `cut_count`, so the band and cut-piece blocks after it do nothing.
- Only with `lightcutone` off (the default), since the per-corner lift is what `lightcutone` turns off.

**What the player sees.** A band's nudge can lift a corner up to four times what its own distance asks; the per-corner lift gives each corner exactly its own. So on most surfaces the light sits a little less far forward: where the light pass and the level fight (the hallway line class, b108–b117), it could show slightly more or slightly less. The shots at the same tick are the check.

**Cost/gain.** See "Evidence" below.

## 3. `foliagetwopass`: alpha-tested parts with soft edges

**What.** An alpha-tested model part (leaves, grass, fences, the vines) keeps only texels over half alpha (`GU_GREATER 0x7F`), so its edges are hard, and a far plant shimmers as its mips thin (LK8). With the word, such a part is drawn a second time right after its first pass: blended by its alpha, only where it is nearer than what is there (`GU_GREATER`: pass 1's own texels have equal depth, so they are left alone), and without writing depth. The texels pass 1 threw away fade the edge in. Two-sided parts get the second pass on both sides.

**Key lines** (`psp_ge.c`):
- 7064–7091: the comment, `foliage_soft_pass` (alpha test `> 0`, depth `GREATER`, depth writes off, `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`; then every state back as the model path expects it).
- 8619–8623 and 8644–8645: which parts (textured, the texture has alpha, the shader is alpha-tested, not camouflaged, not the sky) and the two calls.

**What the player sees.** Softer plant and grate edges. The known side effect: the faded edge is drawn in object order without depth, so something drawn later behind it covers the faded fringe [D]. Level surfaces (the BSP's own alpha-tested shaders) are unchanged.

**Cost/gain.** A second `model_geometry` (CPU and GE) for every alpha-tested part. See "Evidence".

## 4. `sceneryhalf` (build 107, unchanged)

Still in `psp_fixups.py` (3011–3031, 3160) on the w122 base: a sleeping scenery object is checked on alternate ticks, half of them each tick by index. Its heartbeat line ("sleeping scenery, checked every other tick") shows it working. On the PSP it gained nothing measurable in b112 (63.2 ms a tick with and without, `hardware_b112.md`) [M]. It changes when the simulation notices a change to scenery, so it stays off in the multiplayer build's play against a server unless Sheepdog01 decides otherwise [U].

## Multiplayer

The three new words are drawing-only (`psp_ge.c`, shared by both builds) and change nothing the server sees. The multiplayer build (`PSP_MULTIPLAYER=1`) compiles with them: see "Evidence".

## Evidence

(filled in below)
