# Build 111: the level's vertex copies in an arena of their own (2026-10-10)

Built on b110 after b108's hardware runs (`hardware_b108.md`): Sheepdog01 chose b111 = b110 + the level-stream arena. Nothing else changed. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator (PPSSPP with the local patches, PSP-1000 memory), **[D]** read in the code, **[U]** inference.

## What changed

- **The copies' arena** (`psp_ge.c`, "the copies' arena"). Every piece of a level vertex copy (the float copies, the normals, the radiosity, the 16-bit copies) comes from one block carved out of the heap at the first copy and kept for the program's life: the copies' 1 MB budget plus 32 KB, 1,056 KB.
  - When a piece doesn't fit, the least recently drawn copies are let go until it does, never one drawn this frame (the trim's rule; the GE may still be reading those). The trim and the arena share one function for that choice.
  - If this frame's own copies fill the arena, the piece comes from the general heap, as before b111.
  - Inside the arena, blocks lie end to end behind 16-byte headers; a request takes the smallest free block that holds it, and a freed block merges with free neighbours.
- **Nothing drawn changes.** A copy let go mid-frame is one the frame-end trim would have let go anyway, and it's rebuilt the next time it's drawn, as before.
- **Keep word `noarena`:** the general heap only, as in b110, for the A/B.
- **Log line every 300 frames:** `GE: the copies' arena: N KB in use of 1056, largest free N KB; N copies let go to make room, N pieces from the general heap instead`.
- Program +1.3 KB; heap 6,016 KB, as b110.

## Why this fixes b108 run 1's failure

What happened on the PSP [M] (`hardware_b108.md`; b108's heap was 6,080 KB):
- The copies (about 1 MB at their budget) and everything else shared the general heap. Copies were let go and rebuilt as the view moved, each rebuild asking for blocks of up to 106 KB again.
- By the hunter fight the heap had grown to its end. At the first failure (game time 27,907) it held 5,854 KB in use with 224 KB free in 140 pieces, about 1.6 KB each: even a 232-vertex material (4.6 KB) found no room.
- From then on 5,290 materials went undrawn, and later frees left up to 1.4 MB free in 289 pieces, none large enough.
- Every large request in the run came at start-up; during play the rest of the game asks for small blocks only.

With the arena [D, U]:
- The copies can't fail for room: they have 1,056 KB of their own, and the churn stays inside it.
- The rest of the game gets 6,016 − 1,056 = 4,960 KB. In run 1 it needed about 4,520 KB at the start and about 4,850 KB at the peak (the heap's use less the copies'), so it fits with about 110 KB to spare at that moment. Its own allocations are small, so its holes serve them.
- Why 32 KB of slack, not 128: the first build of b111 had 128 KB, which would have left the rest about 4,864 KB against the 4,876 KB it needed at run 1's peak.

## What the emulator showed [E]

**The emulator can't reproduce the failure.** Run 1's own path, replayed on b110 (Sierra-III's `tools/route_from_log.py --timed --from 2600`, the lab's route `b30_b108run1_timed`), played the whole route with 0 failed requests:
- The copies' churn matches the PSP's: 726 copies let go to the route's end, against 731 [M].
- The rest of the heap doesn't: 700–950 KB less in use than the PSP at the same game time. The replay faces the way it moves and fights differently, so it meets far fewer models: at game time 20,000, 511 model buffers converted against 1,180 on the PSP, and 473 KB of bending parts' skin data made against 1,160 KB [M]. Skin data stays on the heap until the converted-buffer table is three-quarters full.
- So the emulator shows the arena working (no failures, its own counts), not the heap at the PSP's pressure. The PSP run is the test.

**A frame can draw more than the budget.** a10's opening draws about 1.6 MB of copies a frame [E]. The budget was never a cap there: b110 held all 1.6 MB in the general heap. b111 holds what fits in the arena and takes the rest from the general heap, the same total.

**b111 on run 1's route** (`sierra-test` 38027507275, the same route, keep words and bake as b110's replay 38024398390):

| At the route's end (game time ~40,000 on) | b110 | b111 |
|---|---|---|
| Failed requests, materials undrawn | 0, 0 | 0, 0 |
| Heap in use (b111: the arena's 1,056 KB included) | 5,287 KB | 5,410 KB |
| Free inside the heap | 346 KB in 112 pieces | 34 KB in 51 pieces |
| Taken from the system (heap top) | 5,634 KB | 5,444 KB |
| Copies let go | 726 (the trim) | 839 (to make room in the arena) |

- b111 took 190 KB less from the system: the copies' churn no longer leaves holes in the general heap, so it wastes less, even with the whole arena reserved from the start.
- 103 pieces went to the general heap, 99 of them between game time 20,085 and 30,141: the BSP switch into the facility (game time 25,189), where every copy is let go and the new BSP's appear at once. The arena afterwards: 822 KB in use, largest free 106 KB.

**Every map's opening** on the campaign bake, 300 s each, deathless, no input (`sierra-test`, b111 at `ae7b223`; a10 at `5d1e41d`, the first build with 128 KB of slack, rerun pending):

| Map | Game time | Heap peak in use, KB (with the arena) | Heap top, KB | Copies' peak, KB | Arena at the end | Let go to make room | Pieces from the heap | Failed |
|---|---|---|---|---|---|---|---|---|
| a10 | 13,301 | 5,122 | 5,156 | 1,641 | 1,150 of 1,152, largest free 0 | 109 | 130 | 0 |
| a30 | 13,259 | 4,700 | 4,760 | 723 | 726 of 1,056, largest free 329 | 0 | 0 | 0 |
| a50 | 13,249 | 4,377 | 4,596 | 611 | 612, largest free 443 | 0 | 0 | 0 |
| b30 | 13,472 | 4,600 | 4,620 | 1,002 | 987, largest free 56 | 0 | 2 | 0 |
| b40 | 13,109 | 4,474 | 4,596 | 410 | 413, largest free 642 | 0 | 0 | 0 |
| c10 | 10,516 | 4,506 | 4,632 | 801 | 805, largest free 250 | 0 | 0 | 0 |
| c20 | 11,894 | 4,681 | 4,704 | 1,107 | 841, largest free 97 | 77 | 112 | 0 |
| c40 | 10,843 | 4,502 | 4,596 | 482 | 486, largest free 570 | 0 | 0 | 0 |
| d20 | 15,335 | 4,567 | 4,640 | 1,117 | 1,056, largest free 0 | 31 | 45 | 0 |
| d40 | 18,376 | 4,377 | 4,596 | 794 | 797, largest free 258 | 0 | 0 | 0 |

- No map failed a request or left a material undrawn. Every heap peak, the arena included, is at least 890 KB under 6,016 KB here; the PSP carries more (above).
- **Where pieces went to the heap:** the three openings whose copies pass the 1 MB budget (a10, c20, d20). Pieces go to the heap when nothing is left to let go: every copy left in the arena was drawn that frame, which happens as a level first appears (everything is new at once) and in views that draw more than the arena holds. They stay in the heap until let go, as all of them did before b111. In c20, 841 of about 1,003 KB of copies sat in the arena afterwards.
- **What would close that gap:** compacting the arena at frame end (the GE has finished, and nothing keeps a pointer into a copy across frames [D]), then moving the heap's copies in. A b112-or-later candidate, not in b111.

## On the PSP

The question: does b111 play b30 past the hunter fight, where b108 began to fail, with no `GE: no memory for a material's vertices` lines?
- **Run:** b30 from the start, as far as possible, ideally to the end; deathless; keep words `nearauto assettrack geo2 hwprofile`; no USB capture.
- **In the log:** the arena line (let-goes, pieces from the general heap), the heap line (`requests failed` should stay 0), and no "no memory" lines.
- `noarena` gives b110's behaviour in the same build, if a comparison is wanted.
