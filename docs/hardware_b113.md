# Build 113 on the PSP-1000 (2026-10-11)

Sheepdog01's PSP-1000 at 333 MHz, b30, `halo_psp_b113.zip` (EBOOT `f0114760…`). The tests are in `build_b113.md`. Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

## Test 1: b113 as it comes

Keep words `nearauto assettrack geo2 hwprofile gebusy`, deathless. No fault. The run went from the start to the first bunker on the beach (game time 8,109, about 13 minutes).

**The opening bench** (Sheepdog01, 2026-10-11): start to the first bunker, the furthest point before the way to the security room. It covers the scripted flight (the fair A/B stretch), the landing, the beach fight, and the walk down into the bunker, a light-drawing finish. b113's tests use it as their common stretch from here.

### The GE's own time, for the first time [M]

`gebusy` times the GE by its own interrupts. Weighted by frames:

| Stretch (game time) | Frame by the clock | GE busy a frame | Share of the frame |
|---|---|---|---|
| The flight (300–2,000) | 274 ms | 79 ms | 29% |
| The landing (2,000–2,700) | 226 ms | 52 ms | 23% |
| The beach (2,700–6,000) | 170 ms | 46 ms | 27% |
| The bunker and after (6,000–8,100) | 85 ms | 25 ms | 30% |

- **The GE works a quarter to a third of each frame. The main CPU is the limit**, as the frame end's 0.1–0.2 ms wait for the GE already suggested.
- **The GE's own floor** [U]: if the main CPU's work vanished, the GE as it draws today would allow about 12 fps in the flight (79 ms), 21 on the beach (46 ms) and 40 in the bunker (25 ms). Reaching 30 fps in the heavy stretches needs the GE's work cut as well as the CPU's: fewer passes, less fill, fewer vertices.
- The GE's time includes its waits for memory, which the CPU's traffic makes longer; with the CPU quieter it may need less [U].
- The profiler's 19th counter ("local bus") reads 0 on this PSP: it counts nothing here.

### The flight against b112 [M]

| The scripted flight (150–2,000) | b112 runs 1, 4, 5, 6 | b113 |
|---|---|---|
| Tick, ms a frame | 63.2, 63.2, 62.2, 66.0 | 67.0 |
| Frame, ms a frame | 129.1, 133.3, 116.7, 140.5 | 143.1 |

- b113 is at the slow end of b112's spread.
- **The sleep's new checksum costs about 1 ms a tick** [M]: scenery's update (almost all of it the sleep's checks) went from 4.4–4.5 to 5.5 ms a tick, for the same 275–278 objects checked. The bipeds' update rose by about 1.6 ms a tick (it includes the bodies' checks; the bipeds' figure also varies run to run). The multiply in FNV-1a is the likely cost [U].
- The rest (the AI +1.7 ms a frame, model drawing +6 ms) is within b112's own spread or the memory layout's shift (the game state moved 64 KB) [U].
- **The render-state cache did no work here**: 0.0–0.8 objects worked out whole a view, none pushing out the oldest. Its test is the cliffs, which this run didn't reach.

### The sound drops named [M]

The first 24 drops came in the flight (game time 1,338–1,607). They were sounds of 28–130 KB. Sounds playing or loading held 788–1,380 KB of the 1,536, so 156–748 KB was free or could be cleared, but the longest run that could be cleared was only 28–120 KB.
- At the worst moments it's size: with 1,380 KB held, a 130 KB line can't fit.
- The rest is fragmentation: with 748 KB clearable, no run longer than 120 KB.
- So the placement (`soundplace`) and the size (`sound1792`) each have something to fix; Test 2 runs both.
- 108 drops by game time 8,100, against run 5's 85 by 6,000: the same order.

## Test 2: the bigger list, and the sound cache's best set-up

Keep words: Test 1's plus `list768 sound1792 soundplace`. From the start to the Warthog drop (game time 4,087, about 8 minutes), as the first version of the notes said. No fault.

| | Test 1 (b113's defaults) | Test 2 | b112 |
|---|---|---|---|
| Draws skipped with the display list full, by game time 1,200 (the approach) | 13,518 | **0** | 14,206 (run 1) |
| Sounds dropped ("failed for room") by game time 2,600 | 105 | **0** | 16 (run 6, `sound1792` alone) |
| Sounds dropped by game time ~4,000 | 108 | **0** | 16 (run 6) |
| Sound loads from the stick by game time ~2,600 | 2,477 | 1,785 | 1,962 (run 6) |
| Heap taken from the system (peak in the run) | 5,007 KB | 5,363 KB | |

[M] for every figure.
- **`list768` ends the approach's skipped draws.** All of b112's and Test 1's skips fell in game time 570–1,200, where the fullest frames want about 400 KB of list. With 768 KB none were skipped; the fullest frame's models took 246 KB.
- **The cost:** the CPU now draws what it used to skip. Over the approach the frame took 198 ms against Test 1's 171 (+16%); the ticks were the same (62.8 against 62.7 ms). Over the rest of the run the frame was faster or the same (−9% and −10% in the next two stretches, −21% on the beach, all within the run-to-run spread).
- **`sound1792` with `soundplace`: no sound dropped.** `sound1792` alone dropped 16 by the landing in run 6; with the placement too, none, and fewer loads from the stick. One run each [M]; the emulator's single runs were mixed, so the PSP's figure is the one to trust.
- **The heap:** 5,363 of 5,888 KB taken by game time 4,000. b112 run 5's whole level peaked 312 KB above its game-time-4,000 figure (5,154 against 4,842), so the whole level would take about 5,680 KB [U]: it fits, with about 200 KB to spare.

## Next

- Test 2 (`list768 sound1792 soundplace`) and Test 3 (`contrails zoommask`), as in `build_b113.md`.
- The cliffs with b113's defaults, to judge the render-state cache.
- b114: a cheaper checksum that still can't cancel the flip's writes: the same scheme with a multiply by 33, done as a shift and an add instead of the full multiply. As with FNV-1a, one change always shows, and two cancel only if the second equals what the multiply made of the first [U, to measure in the emulator's cache model and on the PSP].
