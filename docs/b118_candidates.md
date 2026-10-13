# What 118 carries so far (branch `w118`)

Nothing here ships until b117's tests are done (`CLAUDE.md`). Labels: **[M]** PSP, **[E]** emulator, **[D]** code, **[U]** inference.

| # | Change | Kind | Off word | Evidence |
|---|---|---|---|---|
| O1 | On-wall overlays nudged by their far corner: the bunker hallway line, very likely c40's glass through Chief's legs (`6c3f857`) | restoration | `overlaywallnear` | `wallfar118` [E]: the line gone at Sheepdog01's spot |
| F1 | One map file open besides the open map's (`8697bbc`). b117 ran out of file handles at the sixth map in a session (`hardware_b117.md`, test 2) | fix | `mapfilesopen` | `ladderfd118` [E], with the PSP-1000's descriptor limit: all ten maps |
| L1 | `gemeoverlap` (with `gedefer gemeon`): the Media Engine carries out one 32 KB half of the GE layer's calls while the main CPU writes the next (`big_levers.md` §4, step 4) | exact speed-up, opt-in | (on only with the word) | Simulated [E]: 600–1,560 halves a run, play unchanged. **On the PSP-3000 rig: no gain** (below) |
| B1 | `mebench`: the GE layer's work on the main CPU, on the ME serially, and overlapped, in turns of 60 frames in one run, each way's frame time logged (frame pacing off) | instrument | — | built; the emulator run comes first |
| T1 | `transparent_cut` skips a see-through part whose model's bounding sphere is wholly in view (from `tcut118`) | exact speed-up | `tcutfull`; `tcutcheck` verifies | lab run 38353068304 (`w118c`) |
| X10 | The units' and bipeds' vector checks skipped (they act only on broken vectors) | exact speed-up | `verifyvectors` | Exact: play agrees to the cross-keep reach (`mebench118`, 38357172854) [E]. Worth small (0.3–1.3% of the tick's instructions, `tickmap118`): b114's "6%" was `PSP_TICK_STEPS` timing the inline work after the call too |
| R1 | **The restless back-off**: an object whose update changed it on two sleep checks running is updated without the checks for 30 ticks (`887a84b`) | exact speed-up | `restlesscheck` | c10: −15% of the tick's instructions, −14.5% instruction misses; a30: −8%; play and seeds agree all 3,600 ticks (`restless118`, 38364074267) [E]. Strict clean on c10, b30. Rig ladder: c10's tick 22.6 ms against b117's 25.8 on the PSP-1000 [M, cross-device] |
| X9a | **The level sphere queries' answers remembered** (`f5756af`): 32 entries under all their inputs (the BSP, centre and radius bit for bit, the breakable surfaces' flags), forgotten at each collision BSP set and each map's tags | exact speed-up | `spherewalk`; `spherememocheck` verifies | Probe (38368364688): a50 53–59% of queries repeat last tick's exactly. Memo (38369948767): 0 differing answers on a50, b30, d20, c20; a50 −3.0% of the tick's instructions; play and seeds agree all 3,600 ticks; strict clean [E]. Hits a50 26–32%, b30 63–69%, d20 ~5%, c20 7%: d20 and c20 lose repeats to the table's size, a50 to big answers (`spheresize118`, 38374265166). 64 entries tried (`memo64`, 38378057116): hits up (a50 13→21%) but data misses up on d20 (+1.3%) and c20 (+2.3%); 32 stays |
| NS | **The near path's sphere test** (`53a9ffb`): a bending batch near the eye whose sphere is wholly inside the corner tests' planes is drawn whole without its corner loop (each slot's image of the batch's sphere; a Gershgorin bound on the slot's stretch) | exact speed-up | `nearspherefull`; `nearspherecheck` verifies | `nearsphere118` (38370888358): "all whole" on a30, b30, c40; a30: 6 of 8 whole batches a frame passed by their sphere, drawing −2.2% instructions, model geometry 14.7 → 13.7 ms a frame [E]. transparent_cut's test now shares `sphere_inside` |
| H1 | Frames counted by display refreshes shown (1, 2, 3, 4, 5+) and the longest, in the 300-frame GE report: the momentary drops below 30 fps that 5 s averages hide | instrument | — | — |

## L1: what makes the overlap safe

Each CPU has its own write-back data cache with 64-byte lines and no coherence, so a line both write while a half is out loses one CPU's bytes.
- **The emulator's write trace** (the lab's keep word `mewrites` with `strict = 1`, `PSP_MEWRITES` in its emulator patch) lists every line both sides write; the lab's `mewrites.py` names what is on each. Lines on the game thread's stack (0x09fb…) are the simulation's: on the PSP the ME has its own stack in its eDRAM.
- **The rules it led to:**
  - `psp_build.py`'s link script puts the data of `psp_ge.o`, `psp_tex.o` and libpspgu in blocks aligned to 64 bytes at both ends.
  - Any variable of those files the main CPU writes while the ME works (recording, the heartbeat, record-time helpers) is marked `PSP_MAIN_SIDE` (`psp_me.h`), which gathers them on lines of their own. **A new variable of that kind needs the mark.**
  - Lookup tables both CPUs fill (`psp_geo.c`'s `at_slot`, `psp_tex.c`'s `recent`) have one copy per CPU.
  - Code the ME may run makes no system calls: no `platform_log` (use `ge_log`), no printf family (`psp_keep_number` builds its word by hand), no VFPU. `PSP_ME_WORK()` / `psp_on_me()` tell where code runs (the ME's stack is in eDRAM).
- **The ME-strict check** (`gemesim`) is clean after `464223f`; the write trace's last image lines were fixed in `090a5bf`.
- **On the PSP-3000 rig** (`sierra-hw` 38359689577, b30, `repeat=2`) [M]: in the steady windows, overlapped frames were 3–6% *slower* than the GE layer deferred on the main CPU, and the ME serially 20–25% slower.
  - **Why:** the deferral starts the GE late, so the frame end waits for the GE: 35 ms a frame, against 0.8 in the default build (38352811636). And the ME builds lists about 25% slower than the main CPU (memory-bound work on the shared bus).
  - **So L1 stays opt-in, with no preset.** An ME lever pays only for work that doesn't feed the GE late.

## What the cache map says about the tick (`tickmap118`, 38359927909) [E]

Six levels, 120 s each, by function (`cachemap.txt`). Share of the tick's instruction fetches / data misses:

| Group | a50 | b30 | c10 | c20 | a30 | d20 |
|---|---|---|---|---|---|---|
| Collision walks (X9) | 29 / 27% | 6 / 19% | ? / 5% | 9 / 15% | ? / 11% | 14 / 16% |
| Cluster re-links (N4) | 1 / 6% | 1 / 7% | ? / 4% | 2 / 6% | 0 / 5% | 3 / 7% |
| The port's sleep checks (now R1) | 1 / 7% | 1 / 5% | **23 / 22%** | 0 / 0% | 2 / 13% | 0 / 2% |
| Posing | 3 / 4% | 4 / 4% | 9 / 13% | 8 / 8% | 4 / 7% | 3 / 4% |
| X10, markers, the aim blend | ≈1% each | | | | | |
| X8 (lights) | ≈0 | ≈0 | ≈0 | ≈0 | ≈0 | ≈0 |

(? : below the report's top 40 by instruction misses.) **X8 is parked:** its "8%" was a step share. Drawing's biggest is `model_geometry` (19–28% of drawing's instructions on b30 and a30) and on d20 the light pass (15%).

## Next
1. ~~`w118c`~~ clean; ~~`mebench`~~ answered (L1: no gain); ~~X10~~ exact; R1, X9a and NS built, checked, merged. `w118d` (38375333273): all together, the ten-level ladder and four strict openings.
2. R1 on the rig against `restlesscheck`, same device, c10 (batched with the next hardware question).
3. The tick: X9 (collision walks, a50's biggest), posing on c10/c20; `model_geometry`'s near and far paths split by a probe.
4. The shadow pass memo; c40's restorations from Sheepdog01's notes (`b117_candidates.md`).
