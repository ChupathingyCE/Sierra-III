# Rebuilding the engine around the PSP: the plan (2026-10-09)

What Sheepdog01 and Claude agreed in the 2026-10-09 discussion. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code, **[U]** inference.

## The targets (Sheepdog01)

- **30 fps on average**, chased aggressively.
- **A steady 30 ticks a second at every point in the campaign.** The lowest acceptable frame rate is whatever keeps that: the engine runs at most 2 ticks a frame (`main.c` caps a frame's game time at 1/15 s), so below 15 fps the game itself slows down.
- **The original look and feel**, kept as far as possible.
- **Structural change is accepted**: the game still runs as if on an Xbox, minus what has moved to the GE. Getting there means rebuilding it around the PSP.
- **Changes that alter behaviour or looks are decided one at a time**: tested, compared, and kept only if they don't take away from the experience.

## Why the game is slow on the PSP

The PSP-1000's CPU mostly waits [M] (`psp_hardware_reference.md`, `memory_traffic.md`):
- **54% of its cycles wait on memory.** About 60% of the misses are instruction misses by count, 68% on c20 [M] (`hardware_facts.md`'s miss-ratio bullet; corrected 2026-11-16 from "two thirds").
- The caches are 16 KB each (instructions, data), 2-way, 64-byte lines. A miss costs about 72 cycles (216 ns), and about 424 ns while the GE is reading RAM.
- The tick's hot code is about 59 KB, four times the instruction cache. Its misses have a long tail: half are in 46 functions (97 KB), 90% in 303 functions (294 KB) [E].
- The Media Engine, a second 333 MHz CPU, is idle about 94% of the time [M].

So the work is less "do less arithmetic" than **"touch less memory, in a better order, on both CPUs."**

Where the time goes, b30's beach on b107 (run `b107_b30_hw`, heartbeats and profile windows) [M]:
- tick 55–67 ms, 2.4–6.2 fps;
- in the fight, a tick spends 34–60 ms in `objects_update` (bipeds 32 ms for ~30 of them, b106) and 12–23 ms in `ai_update`;
- each frame spends 20–95 ms in `rasterizer_model_draw`.

(An earlier version of this line gave the AI 8.5 ms a tick; that was The Library's figure.)

The ranked list of every lever is `optimization_ranking.md`.

## The pillars

1. **Drawing rebuilt around the GE.**
   - Display lists built ahead (precompiled per model part and per level cluster), so a frame mostly links lists rather than writing commands.
   - Static scenery merged into the level's lists at bake time.
   - VRAM for what the GE reads most.
2. **The tick and the drawing split, with the Media Engine as a second core.**
   - The tick writes a snapshot of what the frame needs; the frame draws from it.
   - The ME takes self-contained jobs first: the AI's line-of-sight rays against the level (read-only BSP data), sound mixing (already there), skinning.
3. **Data-oriented hot loops.** The per-object walks (sleep checks, sphere tests, cluster lists) read compact arrays instead of chasing each object's datum. b107-b109's sphere copies, sleep checks and marker memo are the first of these.
4. **The compiler and the memory system, used fully.**
   - Profile-guided and link-time optimisation; hot and cold code laid out apart (`PSP_TEXT_LAYOUT`).
   - Prefetch (`cache 30`, non-blocking [M]) ahead of the walks; create-dirty-exclusive (`cache 24`, halves whole-line writes [M]) for buffers written whole.
   - Hot tables in the scratchpad (16 KB) or VRAM (CPU misses there cost 132 ns, not 220 [M]).
   - An audit of asserts in hot paths; a lean play build; frame pointers only where the profiler needs them.
5. **Measurement on the PSP itself.**
   - A sampler on the PSP (where the time goes, as the emulator's PC samples show where the instructions go).
   - A worst-case sweep of the campaign: every mission's heaviest moments, so "steady 30 ticks everywhere" is measured, not assumed.

## How the work is measured

- **The emulator answers "what and where"**: instruction and miss counts per function from the cache model, the same moment replayed from a saved state, runs stopped on emulated time so two builds cover the same stretch.
- **The PSP answers "how long"**: only hardware runs give real milliseconds.
- Every change is A/B'd: a keep word turns it off, so the same build tests both sides.

## Order

1. Finish the move to ChupathingyCE (`chupa_rebase.md`): the base everything below is built on.
2. The b109 candidates already measured in the emulator (`b109_candidates.md`): hardware A/Bs.
3. The ME's first job (the AI's rays), designed and measured one batch at a time.
4. The GE rebuild: precompiled lists for models first (the 21% in `rasterizer_model_draw`).
5. The tick/draw split.

Each step ships as its own build, with its hardware run before the next.
