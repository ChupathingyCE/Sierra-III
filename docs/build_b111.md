# Build 111: the heap held, the level copies in an arena of their own (2026-10-10)

Built on b110 after b108's hardware runs (`hardware_b108.md`). Sheepdog01 chose b111 = b110 + the level-stream arena; the same night the arena gained compaction, and the converted-buffer table's sooner sweep joined it, because b108 run 1's log showed the dead skinning plans filling the same heap. All three are exact (nothing seen or played changes), all three are memory, and each has a keep word. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator (PPSSPP with the local patches, PSP-1000 memory), **[D]** read in the code, **[U]** inference.

## What changed

- **The copies' arena** (`psp_ge.c`, "the copies' arena"). Every piece of a level vertex copy (the float copies, the normals, the radiosity, the 16-bit copies) comes from one block carved out of the heap at the first copy and kept for the program's life: the copies' 1 MB budget plus 32 KB, 1,056 KB.
  - When a piece doesn't fit, the least recently drawn copies are let go until it does, never one drawn this frame (the trim's rule; the GE may still be reading those). The trim and the arena share one function for that choice.
  - If this frame's own copies fill the arena, the piece comes from the general heap, as before b111.
  - Inside the arena, blocks lie end to end behind 16-byte headers; a request takes the smallest free block that holds it, and a freed block merges with free neighbours.
- **Nothing drawn changes.** A copy let go mid-frame is one the frame-end trim would have let go anyway, and it's rebuilt the next time it's drawn, as before.
- **Keep word `noarena`:** the general heap only, as in b110, for the A/B.
- **The arena kept whole between frames** (added after the first route replay, below). At the frame's end, after `sceGuSync` and the trim, the GE is done and no copy is being built:
  - **Compaction:** the used blocks slide to the arena's start, so its free space is one block. Each block records its owner (the copy's field that points at it), which is repointed; the moved GE arrays are written back from the CPU's cache. Nothing outside the copies' table keeps a copy's address past the frame [D].
  - It runs only when the free space is badly split (its largest block under half of it, at least 64 KB free), at most every 30 frames, or after a frame that sent pieces to the general heap.
  - **Migration:** then copies the general heap holds come back into the arena, up to 128 KB a frame.
  - **Keep word `nocompact`:** the arena without either.
- **Log line every 300 frames:** `GE: the copies' arena: N of 1056 KB used, largest free N KB; let go for room N; pieces to the heap N (N there now); compacted N times, N KB; brought back N`.
- **The converted-buffer table swept sooner** (`psp_ge.c`, "sweep sooner"). A model part bending on several nodes gets a skinning plan on the heap; when the geometry cache reuses the part's block, the plan is dead but stayed until the table was three quarters full. The next sweep now comes 256 entries after what the last one kept (a quarter to three quarters of the table; the mark moves past the live set). A part that comes back is converted and planned afresh, as after any sweep. **Keep word `sweeplate`:** three quarters only, as before.
- **The converted-buffer table's log line,** now printed every 300 frames with the plans' live and peak KB: `GE: converted-buffer table: N sweeps, N dead entries forgotten, N in use of 2048 (next sweep at N); skinning plans N KB on the heap (peak N KB)`.
- **Program +3 KB; heap 5,952 KB, 64 KB less than b110.** `psp_build.py` rounds the heap down to 64 KB, and b110 sat 2.6 KB above a step: the arena with compaction fit under it once its housekeeping was compiled for size (`cold`), and the sweep crossed it. The sweep gives back far more than 64 KB in a long level (below).

## Why this fixes b108 run 1's failure

What happened on the PSP [M] (`hardware_b108.md`; b108's heap was 6,080 KB):
- The copies (about 1 MB at their budget) and everything else shared the general heap. Copies were let go and rebuilt as the view moved, each rebuild asking for blocks of up to 106 KB again.
- By the hunter fight the heap had grown to its end. At the first failure (game time 27,907) it held 5,854 KB in use with 224 KB free in 140 pieces, about 1.6 KB each: even a 232-vertex material (4.6 KB) found no room.
- From then on 5,290 materials went undrawn, and later frees left up to 1.4 MB free in 289 pieces, none large enough.
- Every large request in the run came at start-up; during play the rest of the game asks for small blocks only.

With b111 [D, U]:
- The copies have 1,056 KB of their own, and their churn stays inside it. Compaction keeps that space whole between frames, and copies the general heap had to take come back.
- The rest of the game gets 5,952 − 1,056 = 4,896 KB. In run 1 it needed about 4,520 KB at the start and about 4,850 KB at the peak (the heap's use less the copies'), and much of that growth was dead skinning plans: run 1 had made 1,160 KB of plans by game time 20,000 [M], and the replay below shows the sooner sweep keeping them to a few hundred KB. Its own allocations are small, so its holes serve them.
- Why 32 KB of the arena's slack, not 128: the first build of b111 had 128 KB, which would have left the rest about 4,864 KB (on a 6,016 KB heap) against the 4,876 KB run 1 needed at its peak.

## What the emulator showed [E]

**The emulator can't reproduce the failure.** Run 1's own path, replayed on b110 (Sierra-III's `tools/route_from_log.py --timed --from 2600`, the lab's route `b30_b108run1_timed`), played the whole route with 0 failed requests:
- The copies' churn matches the PSP's: 726 copies let go to the route's end, against 731 [M].
- The rest of the heap doesn't: 700–950 KB less in use than the PSP at the same game time. The replay faces the way it moves and fights differently, so it meets far fewer models: at game time 20,000, 511 model buffers converted against 1,180 on the PSP, and 473 KB of bending parts' skin data made against 1,160 KB [M]. Skin data stays on the heap until the converted-buffer table is three-quarters full.
- So the emulator shows the arena working (no failures, its own counts), not the heap at the PSP's pressure. The PSP run is the test.

**A frame can draw more than the budget.** a10's opening draws about 1.6 MB of copies a frame [E]. The budget was never a cap there: b110 held all 1.6 MB in the general heap. b111 holds what fits in the arena and takes the rest from the general heap, the same total.

**b111 on run 1's route** (the same route, keep words and bake as b110's replay, `sierra-test` 38024398390). Two b111 runs: the arena alone (38027507275), and the final build with compaction and the sooner sweep (38036725008):

| At the route's end (game time ~40,000 on) | b110 | b111, the arena alone | b111 final |
|---|---|---|---|
| Failed requests, materials undrawn | 0, 0 | 0, 0 | 0, 0 |
| Heap in use (b111: the arena's 1,056 KB included) | 5,287 KB | 5,410 KB | 4,460 KB |
| Free inside the heap | 346 KB in 112 pieces | 34 KB in 51 pieces | 354 KB in 63 pieces |
| **Taken from the system (heap top)** | **5,634 KB** | **5,444 KB** | **4,815 KB** |
| Copies let go | 726 (the trim) | 839 (to make room in the arena) | 862 |
| Copies' pieces the general heap holds at the end | all of them | 103 | 0 (165 went there over the run; 33 copies brought back) |
| Compactions | — | — | 19, 15.7 MB moved in all |
| Skinning plans on the heap | not counted | not counted | 151 KB (peak 441 KB) |

- **The arena alone** took 190 KB less from the system than b110: the copies' churn no longer leaves holes in the general heap. 103 pieces still went to the general heap, 99 of them after the BSP switch into the facility (game time 25,189), when the frame's copies, pinned while drawn, had split the arena's free space.
- **The final build** took 820 KB less than b110. Compaction kept the arena whole and brought the heap's copies back (none left at the end), and the sooner sweep kept the skinning plans under 450 KB where the arena-alone run made 994 KB of plans and freed none.
- **Compaction's cost:** 19 runs over the level, about 830 KB moved each. A memory copy that size is a few milliseconds on the PSP [U]: a hitch shorter than one frame at today's frame rates, a handful of times a level.

**Every map's opening** on the campaign bake, 300 s each, deathless, no input (`sierra-test`, b111 at `ae7b223`, the arena alone; a10 at `5d1e41d`, the first build with 128 KB of slack, and rerun with compaction in the next table):

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
- **Compaction closes most of that gap** (added after these runs). The same openings with compaction and migration (`b111-compact` at `07ba673`, without the sweep), pieces the general heap still held at the end:

  | Map | Before compaction | With it | Compactions (KB moved) | Copies brought back |
  |---|---|---|---|---|
  | a10 | 130 | 33 | 2 (1,235) | 9 |
  | c20 | 112 | 0 | 2 (1,123) | 14 |
  | d20 | 45 | 32 | 0 | 0 |

  What's left is the copies a view draws beyond the arena's 1,056 KB: a10's opening draws about 1.6 MB a frame and d20's about 1.1 MB, so their arenas are full and nothing can be compacted or brought back. Those copies live in the general heap, as all copies did in b110.

## On the PSP

The question: does b111 play b30 past the hunter fight, where b108 began to fail, with no `GE: no memory for a material's vertices` lines?
- **Run:** b30 from the start, as far as possible, ideally to the end; deathless; keep words `nearauto assettrack geo2 hwprofile`; no USB capture.
- **In the log:**
  - no `GE: no memory for a material's vertices` lines;
  - the heap line: `requests failed` should stay 0, and `taken from the system` shows how close the heap came to its end;
  - the arena line: let-goes, pieces in the general heap now, compactions;
  - the converted-buffer table's line: the skinning plans' KB on the heap and their peak.
- Pull the log right after the run: `log.txt` is removed at every launch.
- To compare in the same build: `noarena` (b110's copies), `nocompact` (the arena as first built), `sweeplate` (b110's sweep).
