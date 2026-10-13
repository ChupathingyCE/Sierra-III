# What 118 carries so far (branch `w118`)

Nothing here ships until b117's tests are done (`CLAUDE.md`). Labels: **[M]** PSP, **[E]** emulator, **[D]** code, **[U]** inference.

| # | Change | Kind | Off word | Evidence |
|---|---|---|---|---|
| O1 | On-wall overlays nudged by their far corner: the bunker hallway line, very likely c40's glass through Chief's legs (`6c3f857`) | restoration | `overlaywallnear` | `wallfar118` [E]: the line gone at Sheepdog01's spot |
| F1 | One map file open besides the open map's (`8697bbc`). b117 ran out of file handles at the sixth map in a session (`hardware_b117.md`, test 2) | fix | `mapfilesopen` | `ladderfd118` [E], with the PSP-1000's descriptor limit: all ten maps |
| L1 | `gemeoverlap` (with `gedefer gemeon`): the Media Engine carries out one 32 KB half of the GE layer's calls while the main CPU writes the next (`big_levers.md` §4, step 4) | exact speed-up, opt-in | (on only with the word) | Simulated [E]: 600–1,560 halves a run, play unchanged. See below |
| B1 | `mebench`: the GE layer's work on the main CPU, on the ME serially, and overlapped, in turns of 60 frames in one run, each way's frame time logged (frame pacing off) | instrument | — | built; the emulator run comes first |
| T1 | `transparent_cut` skips a see-through part whose model's bounding sphere is wholly in view (from `tcut118`) | exact speed-up | `tcutfull`; `tcutcheck` verifies | lab run 38353068304 (`w118c`) |
| X10 | The units' and bipeds' vector checks skipped (they act only on broken vectors) | exact speed-up | `verifyvectors` | 6% of the emulator's tick (b114, `big_levers.md` §8) [E]; not yet measured here |
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
- **Not yet known:** what it is worth on a real PSP. `mebench` answers that in one hardware run.

## Next
1. Read `w118c` (lab run 38353068304): the write trace with T1 folded in (only stack lines expected), and `tcutcheck`'s mismatches.
2. `mebench` in the emulator (does it rotate and log), then one `sierra-hw` run with `repeat=2`.
3. The tick: X8 (lights re-linked every update), X9 (collision walks: a50's sphere queries take 219–570 µs each on the PSP [M]).
4. The shadow pass memo; c40's restorations from Sheepdog01's notes (`b117_candidates.md`).
