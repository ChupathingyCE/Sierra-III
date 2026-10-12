# Build 115 on the PSP-1000 (2026-10-11)

Sheepdog01's PSP-1000 at 333 MHz, b30, `halo_psp_b115.zip` (EBOOT `d22eb2eddda5139a`). The tests are listed in `build_b115.md`; the figures come from `tools/benchreport.py` and the log's `gebusy by kind` and `iostats` lines. Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

This page fills in as the tests come back.

## In short

- **b115 costs nothing on the cinematic** [M]: 197.0 ms a frame against b114's 195.6, inside the 1% that runs of one build vary by.
- **The models are almost all of the GE's time** [M]: 88% of it in the cinematic (33 of 37 ms a frame) and 89% in the flight after it (105 of 118 ms). The emulator's byte count said the same (83–88% of the GE's reads from main RAM, `build_b115.md`). Both of `big_levers.md` §0's conditions for "models first" now hold: the models dominate the GE's time, and a few buffers carry most of their reads.
- **Textures from main RAM cost the GE little** [M]: with VRAM's texture cache cut from 1,216 to 400 KB (test 3), the GE took 1.5 ms a frame more in the cinematic, all of it in the level's draws (2.5 → 4.1 ms); the models' time didn't move. The frame was within run-to-run noise (198.0 against 197.0 ms). So giving ~800 KB of VRAM to models costs about 1–1.5 ms of GE time, and the models' 105 ms in the flight is what it's for.
- **The map file's re-reads are real on the PSP** [M]: after loading, 95% of what it read had been read before. They cost only 1.0–1.5% of the stick's time on this stretch, and nobody waited behind them.

## Test 2: the cinematic, base

Keep words `nearauto gebusy assettrack geo2 iostats`. No fault; Sheepdog01 stopped in the Pelican at game time 1,151 (HOME).

**Against b114** (`benchreport.py`):

| | b114 T1 | b114 T4 (`lean`) | b115 T2 |
|---|---:|---:|---:|
| The cinematic (150–570): frame by the clock, ms | 195.6 | 190.5 | **197.0** |
| drawing, ms | 72.2 | 70.4 | 72.4 |
| tick, ms | 59.9 | 58.3 | 60.3 |
| GE busy, ms a frame (share of the clock) | 44 (20%) | 47 (22%) | 45 (20%) |
| Heap top, KB | 5,520 (whole bench) | 5,156 | 5,160 |

**The GE's time by kind of draw**, ms a frame, weighted by frames:

| Kind | The cinematic (150–570) | The flight after it (570–1,151) |
|---|---:|---:|
| models | **32.7** (88%) | **105.3** (89%) |
| level | 2.5 (7%) | 9.8 (8%) |
| see-through drawn | 1.5 (4%) | 2.4 (2%) |
| shadows | 0.3 | 0.5 |
| HUD, see-through queued, other | 0.3 | 0.2 |
| **All** | **37.2** | **118.3** |

- **What it means for the targets:** at 30 fps the GE has 33 ms a frame. The cinematic is just over that; the flight after it is 3.6 times over, and the models are 105 ms of it. Faster CPU work alone can't take the flight past ~8 fps [U, from the GE's time alone].
- **What it doesn't say yet:** whether the models' time is the GE reading vertices from main RAM or the GE transforming them. Tests 3 and 4 split that: 3 moves textures to main RAM, 4 moves near corners to the VFPU. Blood Gulch's A/B [M] found most of the models' cost was reading.
- `gebusy by kind` adds a chunk at each change of kind, so the total can differ a little from b114's; here it's 45 against 44 ms (benchreport's weighting).

**The stick by stream** (`iostats`, every ~30 s) [M]:

| Report (game time) | Map file: s in the stick (KB, re-read share) | Log: s (worst ms) | Textures: s | Geometry: s | Waits behind others |
|---|---|---|---:|---:|---|
| start to 299 (loading included) | 1.44 (16,088 KB, 58%; worst 332 ms) | **8.86** (47) | 0.55 | 0.67 | 0.5 s in all, mostly behind the log |
| 299–597 | 0.43 (3,885 KB, 92%) | 0.28 (46) | 0.02 | 0.05 | none |
| 597–826 | 0.30 (2,929 KB, 98%) | 0.28 (16) | 0.04 | 0.05 | none |
| 826–983 | 0.38 (3,210 KB, 97%) | 0.25 (21) | 0.06 | 0.00 | 3 ms |
| 983–1,151 | 0.44 (3,691 KB, 94%) | 0.68 (**280**) | 0.06 | — | 9 ms |

- **The re-reads:** 13.0 of the 13.7 MB the map file read after loading had been read before (95%) [M]. The emulator's guess was 83% [E]. At 1.0–1.5% of the stick's time they aren't a bottleneck here; the beach (test 1) is the busy case.
- **The log writes are the stick's biggest user while loading:** 8.9 s of the first report's 13.3 s. Most of that is the heartbeat thread (6.0 s); until the first frame, lines are written one at a time rather than batched [D]. The game thread waited only 0.1 s behind it, so loading barely felt it [M].
- **One 280 ms log write** in the last report, as a checkpoint was written ("other": 1.3 MB). Log writes of that length are the hitch candidates `streaming.md` §2 names; test 1's longer run will show how often.
- Sound: one voice ran dry once (75 ms), at the exit.

## Test 3: the cinematic, textures from RAM (`vramtex400`)

Keep words as test 2 plus `vramtex400`; the log confirms the cut (`the VRAM cache cut to 400 KB (of 1216)`). No fault; stopped at game time 1,182.

| | b115 T2 (base) | b115 T3 (`vramtex400`) |
|---|---:|---:|
| The cinematic (150–570): frame by the clock, ms | 197.0 | 198.0 |
| drawing, ms | 72.4 | 73.4 |
| tick, ms | 60.3 | 60.5 |
| GE, ms a frame: all | 37.2 | **38.7** |
| GE: level | 2.5 | **4.1** |
| GE: models | 32.7 | 32.4 |
| The flight after it (570–1,151), GE: all | 118.3 | 118.3 |
| GE: level | 9.8 | 10.9 |
| GE: models | 105.3 | 104.0 |
| Textures loaded over the run (KB) | 128 (2,188) | 199 (2,836) |
| Textures read from the stick after loading, KB a report | 109–209 | 109–346 |

- **The cost of texels in main RAM is the level's, ~1–1.5 ms a frame** [M]. The level's textures are the big, repeated ones; the models' time doesn't change, so their 105 ms isn't spent on texels.
- **That matches Blood Gulch's ±1 ms** (`big_levers.md` §0) and settles M4: the swap's first step is cheap.
- **Streaming barely moved:** the stick read the same textures except 137 KB more in the last report. In the real swap the RAM cache grows to take what VRAM gives up (`big_levers.md` §0), so even that goes.

