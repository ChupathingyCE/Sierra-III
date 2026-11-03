# MEHOTG: the ME's hottest variables packed by heat

`PSP_ME_DATA_LAYOUT=mehotg` (psp_build.py) places the inputs listed in `psp/layout/medata_mehotg.txt`, hottest per byte first: the `.data` ones at the start of the ME's `.data` block, the `.bss` ones at the start of its `.bss` block. Only the ME's own objects (`psp_ge.o`, `psp_tex.o`) may be listed, so each block keeps only the ME's variables and no line is shared with the main CPU's. Off unless the switch is set. Labels: [M] measured on the PSP, [E] emulator, [D] read in code or data, [U] inference.

## Why

- The ME's caches are the main CPU's shape: 16 KB, 2-way, 64-byte lines, LRU-like, for code and data alike. The ME measured that from its own side (`mecache`, lab sierra-hw 41574321225) [M].
- On b30 the ME misses its data ~25k times a frame (meprofile, lab sierra-hw 41567767938) [M].
- A code layout that only moved data addresses cut those misses 13% and the ME's job 2.9% (`me_hw_layout`) [M]. So where the ME's data sits matters.
- The replay reads dozens of 4-byte flags and words of `psp_ge.c` on every call: `ge_serial`, `ge_kind_now`, `part_k14`, the stall and gebusy words, the model state. They sit in each file's input order, one or two to a line among cold neighbours, and lose their lines between calls to the replay's data streams [E].

## How the list was made

1. **The ME's data in the cache model.** The lab's `halo-local.patch` item 27 (`PSP_DCACHE_ME`, batch key `medcache`) gives the ME's loads and stores a data cache of their own. On b30's route to 5,400 ticks it counts ~25.7k ME D-misses a frame, against the PSP's ~25k (medc1 41575408940) [E against M].
2. **A spread build.** `PSP_ME_DATA_LAYOUT=mespread PSP_ME_DATA_SPREAD=1` put each of the ME's 1,011 small inputs (< 2 KB each, 36.8 KB in all) on lines of its own. The model's ME map (`cachemap_me.txt`) then gave each input's own misses on b30's flight and c20's opening (medc2 41581386618) [E].
3. **The pick.** 160 inputs took misses. 156 of them hold 98.5%: every input with at least 20,000 misses and 1,000 a byte, up to 1 KB, as HOTG's rule. That is 2,150 B: 43 `.data` inputs (180 B, 3 lines) and 113 `.bss` inputs (1,970 B, 31 lines) [E].

## What it moves

- Each kind stays in its own block, so the blocks keep their size, except the padding the new order brings: one line each (`.data` +64 B, `.bss` +64 B).
- Everything linked after them moves 64-128 B. HOTG's run moves one cache set and stays clear of the main thread's stack top [D, the link maps of sierra-builds 41580785753 (cb5d4f5, without the switch) and 41583714965 (d7138cc, mehotg)].
- Code and `.rodata` don't move.

## Proof [E]

- **Play** (medx1 41583974382; controls medx2 41586237955):
  - Play and both random seeds are the same every tick on b30, c20, d20, a30 and c10.
  - Strict mode finds 0 problems on b30.
- **The whole state** differs only in allocations outside the play hash: actor, objects, object looping sounds. Two plain runs are identical there. So the difference follows the layout: addresses, or fields the drawing writes [U].
- **Pictures** are within the run-to-run spread:
  - b30: 132-385 of 1,048 frames differ, against 131-334 between plain runs.
  - c10: 6-8, against 6 between plain runs.
  - c20 1, d20 2, a30 2.

## Score [E] (medc3 41583978233, both sides the same commit, the switch on or off)

- **b30's route to 180 s:**
  - The ME's D-misses fall 12.0% (71.85 to 63.21 M). The replay's phase falls 16.4%; the tick phase's jobs rise 20% (3.28 to 3.94 M).
  - The ME's write-backs fall 20.8%. Its I-misses are unchanged.
  - The main CPU's D-misses fall 0.5% (tick flat, drawing -1.4%).
- **c20's opening:**
  - The ME's D-misses fall 5.7% (the replay's phase -6.5%).
  - The main CPU's D-misses fall 0.5% (tick -2.3%).
- **What that's worth [U]:**
  - b30 sheds ~3.2k ME misses and ~1.9k write-backs a frame. At ~60-64 cycles a miss (`me_miss_cost` [M]) that is ~0.6-1.0 ms of the ME's ~30 ms a frame (2-3%).
  - That matches me_hw_layout's -13% misses giving -2.9% of the job.
  - Whether the frame gains depends on the ME's replay setting it. The PSP pair below answers that.

## The PSP pair

- **Two builds of d7138cc**, both with the tock's switches (`PSP_PGO=use PSP_PGO_PROFILE=campaign-w152s PSP_PGO_EXTRA=... PSP_TEXT_LAYOUT=w152L0_tgc90`). One adds `PSP_ME_DATA_LAYOUT=mehotg`.
- **Each run:** keep words `nearauto gebusy assettrack geo2 hwprofile nomouth mekinds meprofile`, route b30_b139t2_timed, bake w148s, wall 600.
- **One PSP for both jobs, back to back, repeat=2 each:** 4 runs.
- **Read:**
  - the ME's own counts (the `meprofile` lines): job time, D-misses, write-backs;
  - the frame, the tick and the drawing (abreport);
  - and whether the replay finishes earlier on the frame's lane.

## Not done here

- The ME also misses lines it doesn't own: pspgu's `gu_settings`/`gu_contexts` line, HOTG's first line (the main CPU's keep flags it reads), `at_slot_cpu`, a pte/gum line, and `psp_ge.o`'s main-side words. Packing those is a placement across the CPUs' blocks: each needs a check that the main CPU doesn't write it while the ME runs.
- The tick phase's rise on b30 (the ring jobs) is unexplained. The next ranking can include the tick phase's own weight.
