# Review: build 123, every change since build 122

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b123 replaces b122 for testing: it carries all of b122 (read `review_b122.md`) plus the changes below. The long versions are `review_b123_flags.md`, `review_b123_mehyg.md` and `review_b123_lititems.md`. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `w123`: `w122` plus `docs-import`, `flags123`, `pf123`, `mehyg123` (`f62eee9`) and `lititems123` (`9ee93a7`).

| # | Change | Kind | Default | Word |
|---|---|---|---|---|
| 1 | Functions aligned to 64 bytes, loops to 32; four loop-duplicating passes off | exact speed (compiler) | on | a build setting (below) |
| 2 | The larger cut buffer, taken on the first lost piece | restoration (the Pelican wall) | on | off: `cutroomoff` |
| 3 | The AI profile meter's sampling skipped unless shown | exact speed | on | off: `aiprofilefull` |
| 4 | Lit items (Covenant weapons) may sleep | exact speed | on | off: `litemsawake` |
| 5 | The ME's cache hand-overs (six changes) | speed, unproven | **off** | on: `meaudiofirst meraysbatch meaudioranges mepushdrop mestash meraysminN` |
| 6 | Resting ground weapons may sleep through their tracking test | speed, **not exact** | **off** | on: `weapontracksleep` |
| 7 | `activecensus`: what is awake, every 5 s | instrument | off | on: `activecensus` |
| 8 | The prefetch map tool (`tools/prefetchmap.py`) | tooling | n/a | no game code |

**The PGO profile is b122's** (`campaign-b122`), not one counted at b123's commit, so b123 ships quickly. The code it doesn't cover is laid out without a profile [D]. A fresh profile comes with the next build.

---

## 1. Alignment and loop flags (`flags123`)

**What.** Every `PSP_PGO=use` build gets `-falign-functions=64 -falign-loops=32` plus `-fno-unswitch-loops -fno-split-loops -fno-version-loops-for-strides -fno-predictive-commoning`. Key line: `psp_build.py`'s PGO flag list.

**Why.** At `-O2`, GCC places our functions on 4-byte boundaries, so hot functions straddle the 64-byte cache lines, and instruction fetches are two-thirds of our misses. The loop flags stop GCC from duplicating loops, which makes code bigger. The research's other idea, a smaller "hot" set, made things slower: warm code built for size runs 3–9% more instructions [E].

**Proven.**
- Play and both seeds identical at every tick on d20, a50 and c20 [E].
- **The PSP-3000, b30, same commit, new flags against b122's** [M] (lab 38631182315 against 38631231323):
  - cinematic frame 162.8 against 165.7 ms (−1.8%);
  - flight 230.1 against 238.7 (−3.7%);
  - drawing −2.9 to −5.6%.

**The off side.** `PSP_PGO_ALIGN_FUNCTIONS=0 PSP_PGO_ALIGN_LOOPS=0 PSP_PGO_LOOP_TRANSFORMS=1` rebuilds b122's flags byte for byte.

## 2. The Pelican wall (on by default)

**What.** b122's `cutroom` brought the wall back on your PSP (test 7). Now the 768-corner buffer is taken from the heap only after a frame first loses cut pieces. The switch happens at the frame's end, where no cut corners are held. Views that never overflow keep the scratchpad's 192 corners and pay nothing, and so does the multiplayer build's tighter heap.

**Key lines.** `psp_ge.c`:
- `model_cut_take_large`, `model_cut_grow_if_lost`;
- the call after `sceGuSwapBuffers()`.

`cutroom` still takes it at start; `cutroomoff` never grows it.

**Proven.** Your test 7 [M] for the larger buffer. The on-demand switch is being checked in the emulator (lab 38647204643, 38649944643).

## 3. The AI profile meter

`ai_profile_update` walked every actor twice a tick to fill a debug display nobody sees. Now it only zeroes its accumulators unless the display is on.
- Exact on d20, a50, c20 [E].
- Small: ≤0.1 ms a tick [E].

## 4. Lit items sleep

A resting weapon with attached lights now sleeps like any other item. While asleep, it reconnects its lights at its own turn, as sleeping machines do (b121).
- Play, seeds and light lists identical on d20, a50, c20 [E].
- **It gains nothing yet:** every resting ground weapon fails an earlier test (6 below), so none reaches this one.

## 5. The ME's cache hand-overs: all off

On the PSP-3000 they made c40's opening cinematic 6.9% slower [M] (lab 38631087543), through occasional long ticks the logs don't explain, and showed no gain elsewhere. So every one is opt-in, and b123 runs b122's ME code by default. The test that names the cost is c40 with `keepsets = "- | meaudiofirst meraysbatch | meaudioranges mestash | mepushdrop"`, when a test PSP is free.

## 6. Ground weapons and their tracking test: off

`weapon_new` never sets `tracked_object_index`. It reads 0, which names whatever object holds slot 0, so every ground weapon fails the sleep rule's tracking test and stays awake. On your PSP, ground weapons cost 5.1 ms a tick in b30's beach fight [M].

The lever lets them sleep while the tracked object exists, but its proof failed. A sleeping needler holds an old function value that its shaders read, because the game computes those functions from game time. Play isn't affected [E], but the look can be, so it's opt-in.

The exact version, where sleepers keep computing their functions, is being built, along with a check of every kind of sleeper for the same problem.

## The merged build

- The emulator checks (lab 38649944643):
  - exact against b122 as released on d20, a50 and c20;
  - b30 with shots: the wall, and no crash;
  - the multiplayer build starts.
- Campaign and multiplayer compile.
- `psp_fixups.py`'s merge kept both FIXES_123 and FIXES_123L, applied in that order.
