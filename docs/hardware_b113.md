# Build 113 on the PSP-1000 (2026-10-11)

Sheepdog01's PSP-1000 at 333 MHz, b30, `halo_psp_b113.zip` (EBOOT `f0114760…`). The tests are in `build_b113.md`. Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

## Test 1: b113 as it comes

Keep words `nearauto assettrack geo2 hwprofile gebusy`, deathless. The run went from the start to the first bunker on the beach (game time 8,109, about 13 minutes) and stopped there, short of the cliffs. No fault; the log ends after a normal heartbeat.

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

## Next

- Test 2 (`list768 sound1792 soundplace`) and Test 3 (`contrails zoommask`), as in `build_b113.md`.
- The cliffs with b113's defaults, to judge the render-state cache.
- b114: a cheaper checksum that still can't cancel the flip's writes: the same scheme with a multiply by 33, done as a shift and an add instead of the full multiply. As with FNV-1a, one change always shows, and two cancel only if the second equals what the multiply made of the first [U, to measure in the emulator's cache model and on the PSP].
