# Review: a level material with no room for its copy (branch `streamoom`)

Sheepdog01's b124 test 3 rerun (a10, preset 1 = `nearauto assettrack geo2`, PSP-1000): from about game time 5,900, near the bridge, `heap: memalign of 109600 bytes FAILED` and `GE: no memory for a material's vertices (5480)` on every frame, later `166080 bytes` too; the frame rate fell from 21 to 9. The same build on the same map had no failures in his other runs (test 3 on preset 9, test 8's ladder).

Labels: [M] Sheepdog01's PSP-1000 log (`b124hw/test3rerun`), [E] emulator (lab batches below, `fixedticks fixedseed`, hands off), [D] read in the code or the b124 link map (lab 38670379459), [U] inference.

## The cause

**Where the bytes go.** A level material's copy is four pieces (`level_stream_get`): the float vertices (20 bytes each), the lightmap's float vertices (20), the normals (4) and the radiosity (4): 5,480 vertices take 263 KB, 8,304 take 398 KB [D]. The four `heap callers` addresses in the log, 0x8a77a9c, …afc, …b14, …b34, all fall inside `level_stream_get` in b124's link map (`copy_alloc` inlined), in that order: the float, lightmap, normals and radiosity pieces' heap fallback [D; the pairing by order, U]. On each failing frame the 107 KB float piece came from the heap and the 107 KB lightmap piece did not [M: one `memalign 107 KB from 0x8a77a9c`, then the failure].

**Why there was no room, though the heap said 418 KB free.**
1. **The arena held only what was on screen.** The copies' arena (1,056 KB) lets go the least recently drawn copies to fit a new one, never one drawn in the frame so far (`copy_alloc`). By the failures the arena was full at every frame's end (1,010 of 1,056 KB, largest free 45 KB [M, frame 3600]) after it had let go everything it could (let-gos for room 23 → 413 in 300 frames). So the free room wasn't split: the view's copies, about 1.0 MB in the arena plus the failing material's 263 KB, didn't fit [M, U].
2. **The heap was at its ceiling.** Its overflow went to the general heap, which had taken 5,588 of its 5,600 KB from the system, with 418 KB free in 119 pieces before the frame's 162 KB and 107 KB pieces, and no 107 KB piece left after them [M]. Earlier in the run the heap held up to 428 KB of overflow (four 107 KB pieces, lines 2325-2328) [M]; meanwhile the rest of the game's use grew (skinning plans 27 → 204 KB, the largest caller after the copies being `skin_plan` [M, D]). Which is why the same build on another path had room [U].
3. **The retry made it worse.** The failing material was tried again on every draw. Each try let go every copy not yet drawn in that frame, which the frame then built again from the geometry pack (let-gos 50 → 450 in 300 frames, 12-21 fps down to 9 [M]), and failed again.
4. **A counting bug.** `copy_alloc` counted a heap piece before the heap answered, so each refusal left `arena_heap_live` one too high: "pieces to the heap … (42 there now)" with 42 = the heap's 42 failures and no copy on the heap [M, D]. With it non-zero, `arena_migrate` walked all 512 copies every frame for good, and `arena_frame_end` took every failing frame as an overflow worth a compaction.

Not the cause: fragmentation of the arena (its free room was 45 KB) [M]; the stream budget (the copies, 1,008 KB, were under its 1,024) [M].

## The change (`psp/src/psp_ge.c`)

| Lines | What | Why |
|---|---|---|
| 3974-4007 | `stream_room`'s state, the waiting list (16) | The reason in one comment |
| 4023, 4041-4053 | `streamroomoff` and `streamsqueezeN` read at the arena's start; the squeeze holds the rest of the arena in one block with no owner (first, so compaction never moves it) | Test pressure in the emulator without starving the rest of the heap |
| 4086-4118 `copy_alloc` | Heap pieces counted when the heap gives them (the old count under `streamroomoff`); under the squeeze, no heap | Bug 4 |
| 4551-4570 `stream_room_ask`, 5058 | A material on the waiting list is skipped by its draws before any work (no let-go, no pack read) | Cause 3 |
| 4572-4594 `stream_room_wait`, 5119 | A failed build puts the material on the list (logged for the first 8) instead of logging every failure | |
| 4596-4609 `stream_room_need` | The arena bytes a copy takes, headers included | The frame end's room test |
| 5196-5283 `stream_room_frame_end`, 15652 | At the frame's end (GE done, blocks free to move): a material not drawn this frame leaves the list. One that was is built if the free room plus the copies not drawn this frame hold it: those let go oldest first, the arena compacted if its free room is still split. Otherwise the heap is asked once a second, by a probe through `__real_memalign` (a refusal there is a question, not one of the heap's failures). A failed build on the arena's room waits a second too. A line once a second while anything happens, every ten while a material only waits | Draw it as soon as room exists, without the per-frame churn |
| 9388 | The list emptied with the level's copies (a new BSP or map) | Its pointers belong to the BSP |

`psp_platform.c` 894: the keep words' line.

**Keep words.** `streamroomoff`: b124 as it was (every draw tries, the old count). `streamsqueezeN` (test): the copies held to N KB of the arena, no heap room.

**What it doesn't do.** It can't make room that isn't there: when the view's copies are larger than the arena plus what the heap can give, a material is still left out, as before; the difference is which one, and the cost. b124 left out whichever material came last in the frame's draw order, rebuilding the rest every frame; this leaves out the one that first found no room, until the view or the heap frees room for it. Drawing all of Sheepdog01's view needs smaller copies (the float lightmap copy repeats the positions; 16-bit copies are half the size) or a larger arena at the rest of the heap's expense: Sheepdog01's call, not part of this.

**Simulation.** Only drawing and the copies' memory change. Object lighting reads the copies' normals and radiosity (`psp_ge_level_vertex_lighting`), so a missing copy changes what objects are lit with, as it did before [D].

## Evidence

EVIDENCE_PLACEHOLDER

## For Sheepdog01 on the PSP

- Test 3 again (a10, preset 1, the flyby then up to Keyes). The log should show `GE: no room for a material's copy (… vertices): it waits` once per material and `GE: level copies short of room: …` at most once a second, never a run of `memalign of 109600 bytes FAILED`. `heap: … N requests failed` should stay near 0.
- The frame rate where b124 fell to 9 fps should hold near what it was before the failures (about 21).
- A missing piece of the level where b124 had one is expected while the line says a material is waiting; one that b124 drew there but this build doesn't is worth a shot and a note (the choice of which material is left out changed).
- `streamroomoff` gives b124's behaviour for an A/B.
