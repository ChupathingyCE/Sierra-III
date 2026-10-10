# Build 112: candidates (2026-10-10)

What comes after b111, found the night b111 was made. Both are on the `b112` branch, built on b111. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code or the link map, **[U]** inference. Nothing here ships until b111's hardware run is in.

Both are meant to be exact: the same game and the same picture. Item 1 has a keep word to turn it off; item 2 is a build change, so its A/B is b112 with `placeddeadawake` against b111.

| # | What | Off switch | Aimed at |
|---|---|---|---|
| 1 | Bodies the level placed dead sleep like other corpses | `placeddeadawake` | tick (bipeds) |
| 2 | Cold asserts and float `fast_ftol` on ChupathingyCE | b111 | instruction cache, tick and drawing |

A third candidate, the converted-buffer table swept sooner, moved into b111 the same night: it fixes the same heap failure as the arena (`build_b111.md`).

Heap 5,952 KB, as b111: the two add about 12 KB of code, most of it item 2's hot/cold split, without crossing another of the heap's 64 KB steps.

## 1. Bodies the level placed dead

- **Why:** b105's corpse sleep lets a dead body that is garbage, at rest and idle for 2 s skip its update while its data stays byte-identical. Bodies the level places dead (set dressing) never qualified: `units.c` gives them `_unit_placed_here_dead_bit` and `_object_cannot_be_garbage_bit`, so the garbage collector never takes them and they update every tick for the whole level [D].
- **Found in b108 run 1** (b30's security room, game time 19,500–22,000) [M]: 20 dead bodies a tick, 13 garbage (all 13 asleep) and 7 not garbage, updated every tick. The dead count jumped from 4 to 20 in about 200 ticks as the room was entered, as placed bodies do [U].
- **What:** a placed-dead body goes on to the same stages as a garbage one (at rest, idle 2 s), and the byte-identical rule still decides; a push or damage wakes it. Placed dead means dead from the start, so a Flood form feigning death never qualifies.
- **Log:** the corpse line gains "N placed dead".
- **Where it should matter most:** levels dressed with bodies (343 Guilty Spark, The Library, the Pillar of Autumn) [U].
- **The emulator** (run 1's route, `sierra-test` 38027512336, built with the sweep as well) [E]: in the security room stretch, 4 bodies a tick were placed dead, all at rest and offered to sleep, none holding anything; the route played to its end with no fault. The replay fights differently from Sheepdog01's run (13 garbage bodies there, none here), so the PSP gives the cost.
- **Status:** built; awaiting the PSP.

## 2. Cold asserts and float `fast_ftol` (a regression from the rebase)

- **Why:** halo-ce-universal's upstream patch 0002 never reached ChupathingyCE [D]:
  - `psp_build.py` passes `-DPSP_ASSERTS_COLD=1` and `-freorder-blocks-and-partition`, but ChupathingyCE's `cseries.h` had no branch for them. b110's and b111's link maps hold 3 `.text.unlikely` sections against 2,981 on the old base, so every failed assert's call sat in the middle of the hot code it guards (`optimization_ranking.md`, E1).
  - `fast_ftol` rounded through a double. The PSP's FPU has no double operations, so each call was three library calls (to double, `rint`, to long) on animation frame indices, AI perception, weapons and the HUD; libm's `rint` was linked for it.
- **What:** one `FIXES_CHUPA` entry ports the patch's `cseries.h` half: `PSP_ASSERTS_OFF`/`PSP_ASSERTS_COLD` branches, `__attribute__((cold))` on `release_assert_failed`, and `rintf` in `fast_ftol`. A float's nearest integer is exact in a float, so `rintf` gives the same bits.
- **The link map** [D]: 2,951 `.text.unlikely` sections (3 before), about 500 KB of code moved out of the hot sections, and `rint` is no longer linked.
- **Why it should help:** two thirds of the memory waits are instruction fetches, and the tick's hot code is about 59 KB against a 16 KB instruction cache (`optimization_ranking.md` §1). b108's `-Os` test showed layout matters more than size (`hardware_b108.md`). The ranking's ceiling for E1: about 1.1 ms a tick and 1.4 ms a frame [U].
- **The emulator** (the same replay) [E]: the whole route with no fault. PPSSPP doesn't model the caches by default, so only the PSP shows the gain.
- **Status:** built; awaiting the PSP.

## On the PSP, after b111's run

- **Item 2:** the A/B stretch (the Pelican flight to the Warthog drop) on b112 with `placeddeadawake`, against b111's.
- **Item 1:** b30's security room on b112, against b111: the bipeds' time a tick and the corpse line's "placed dead".
