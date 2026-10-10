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

## 3. Instruments for the big levers (added later on 2026-10-10)

Sheepdog01 asked for the levers that move the tick rate and frame rate by a lot, tested, instead of more small gains. `big_levers.md` sizes them. b112 carries the instruments and the first step; none changes the game unless its keep word is set.

| Keep word | What it does | For |
|---|---|---|
| `ceilingbench` | b30 from 90 s of game time, standing still: each heartbeat window turns one big part off (AI, objects, effects, models, level, the GE layer), three cycles; benchrun's `bench: RESULT` lines (`tools/benchparse.py --by-switch`) | the most each structural lever could save, on the PSP |
| `gedefer` | the model path's GE calls written down and carried out in runs, in order (`psp_ge.c`) | step 1 of moving the GE layer to the Media Engine; and a possible instruction-cache gain of its own |
| `fixedticks` | every frame two ticks and a fifteenth of a second, whatever the clock says (`main.c`) | runs that play the same way on any build (tests only: game time stops following the clock) |
| `statecrc` | a hash of the game state after every tick, and of each allocation every 30 ticks | proving a change leaves the game exactly as it was |

Tools: `tools/mewalk.py` (what a Media Engine job started at given functions could reach: firmware calls, VFPU code, calls through pointers; the lab's `sierra-build` `reach` input runs it); `benchparse.py --by-switch` (a tick's own cost per switch). Lab: `sierra-test` `cache=1` runs the emulator's cache models and reports each phase's misses.

Heap still 5,952 KB with all of this in.

## On the PSP, after b111's run

- **Item 2:** the A/B stretch (the Pelican flight to the Warthog drop) on b112 with `placeddeadawake`, against b111's.
- **Item 1:** b30's security room on b112, against b111: the bipeds' time a tick and the corpse line's "placed dead".
- **The ceiling bench** on b30 (`ceilingbench`, about 10 minutes, hands off), and **benchrun on Blood Gulch** for switch 8 against switch 3 (`optimization_ranking.md` M3).

### Results (Sheepdog01's PSP-1000, 2026-10-10) [M]

The scripted Pelican flight (game time 150–2,000), averaged over the heartbeat's `budget:` lines, the same keep words as b111's long run (`nearauto assettrack hwprofile geo2`):

| Run | Clock, ms a frame | Ticks, ms a frame | Frame, ms | The GE layer's CPU | The rest (frame end) |
|---|---|---|---|---|---|
| b111 (the long run) | 267.8 | 127.9 | 132.8 | 63.4 | 7.1 |
| b112 run 1 (`placeddeadawake`: item 2 alone) | 260.6 | 124.6 | 129.1 | 63.2 | 6.9 |
| b112 run 2 (+ `gedefer`) | 260.0 | 128.4 | 91.7 | 65.0 | 40.0 |

- **Item 2 (cold asserts, float `fast_ftol`): −2.7% by the clock, −2.6% on the ticks.** Small, and close to the run-to-run spread on this stretch (b108's two runs differed by 2%), but in the expected direction. It stays.
- **`gedefer` on its own: no change** (260.0 against 260.6). The replay moves about 38 ms a frame from drawing to the frame end ("frame" falls, "the rest" rises), and the recording adds about 2 ms to the GE layer's CPU. The emulator's cache model had predicted about 3 ms a frame saved (`big_levers.md` §7); the PSP doesn't show it. So `gedefer` is neutral on the main CPU, and its value is as the step before the ME takes the replay (L1).
- Both runs played the beach to the Warthog drop with no failed heap requests (run 1: 0 failed, 4,938 KB taken).
- Item 1 (the security room) is still to run.
- **Run 3, `nosizefloor` (test B):** worse. Whole groups of beach objects vanished and came back (Sheepdog01): with the display list full, whatever was submitted last is dropped, 83 KB of model draws a frame against 46 KB with the floor (game time 570–1,200). The floor stays; the list's room is `b113_candidates.md` #19.
- **Run 4, `sceneryhalf` (test C):** no measurable gain. The flight's tick is 63.2 ms in both runs 1 and 4, with 58 sleeping scenery objects a tick left unchecked; the beach's first stretch (game time 2,600–3,700) spans 117–145 ms a frame across four hand-played runs, and run 4 sits inside that. It stays off: what it trades (a change to scenery noticed a tick late) buys nothing visible.
