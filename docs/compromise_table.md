# What lifting the compromise freeze would buy (b30, c10, c20, d20)

2026-10-29, hardware session, for Sheepdog01 (the master's ask, 16:15Z). Nothing here is built for this doc: it gathers the compromises already catalogued (the lab's `findings/levers.tsv`, `rank_pipelined.md` §8, §10.2 and §11.5) and prices each against the tock as it plays today. Labels: [M] measured on a PSP, [E] emulator, [U] inference (usually a PSP measure scaled by an emulator share).

## How to read the tables

- **The main CPU binds all four maps.** On these stretches a frame runs two ticks and then records the drawing, one after the other (`rank_pipelined.md` §9: the tick lane is 54-75% of the frame; the GE and the ME have slack). So only work taken off the main CPU shortens the frame. A cut on the GE side (fewer polygons, textures) buys nothing here, which is why SSPR, CP and the texture pickers aren't in the tables.
- **Worth is ms a frame off the main CPU.** A per-tick saving counts twice, because a frame runs two ticks.
- **Game speed** is how fast the game runs against real time: two 30 Hz ticks a frame means 2 x 1000 / frame / 30. b30's flight at 157 ms a frame runs at 42%.
- **Already in, and in every figure below:**
  - `aicad3` (AI thinking on alternate ticks, on since b140h1, waiting for your hands-on review);
  - `lodpsp` (model LOD at 272 lines);
  - `cutless`;
  - `decalfade`.
- **The bar, 30/30, is 33 ms a frame with one tick a frame.**

## b30 (the flight: PSP-3000, tock w149c, frame 155-158 ms, tick 48.6-53 ms, game speed 42%)

| Compromise | What a player would notice | Worth (ms a frame) | Label | Where it stands |
|---|---|---|---|---|
| **L15**: the simulation at 15 Hz, frames in between interpolated | the world moves in 15 steps a second (drawn smooth); controls answer up to 33 ms later; the multiplayer build can't take it | **-48 to -53** (one of the two ticks) | U on M | declined by you 2026-10-22 23:23 |
| **aicad2**: `aicad3` plus the timeslice (cover search, re-plan) at half rate | marines take twice as long to be sent to cover | ~-5 | U on E (aicnt1: -4.7% of the tick's work past aicad3) | built, off; **lost the beach** in the emulator (never cleared, save_beach_1 never written) |
| **UNSEENPOSE**: undrawn units posed at the root only | nothing drawn changes; AI aims from a pose up to 0.1-0.2 wu off; a30's marines ended differently | landing up to ~-10, flight ~0, beach worse (+4%) | E | built, shelved (fights lose the saving) |
| **RA** (far actors skip their AI), gated to the flight and landing | none expected in the dropship | ~-1 to -2 | U | built; ungated it lost the beach |
| **FX15**: effects and particles updated once for the frame's two ticks | a few sparks and puffs spawn a tick late | ~-1.5 | U on M | idea; needs a census by effect |
| **RENDERTICK**: the HUD, first-person weapon and antennae once a frame | none on screen (they're drawn once a frame anyway) | under -0.1 (counted: the three are ~0.2% of b30's tick, ~0.4% of c20's, and only one tick a frame of it could go) | E | counted, not built: under the bar (review_aifar.md) |
| **AIFAR**: far actors not fighting think 1 tick in 4 | far actors out of the fight react up to 4 ticks late instead of 2 | up to -0.7 | U on E | built, off by default (`aifar`; review_aifar.md) |
| **STILLSWEEP**: a standing biped skips its collision sweep | a still biped nudged by something moving reacts a tick late | -0.2 to -0.7 | U | parked |
| **ANIM2**: far bipeds posed only on the drawn tick | nothing drawn | -0.2 to -0.4 | U | under the bar |

**Best set without L15:** about -8 to -12 ms on the flight (157 -> ~146 ms, -6%; game speed 42 -> ~46%), more on the landing if UNSEENPOSE holds there. aicad2 is most of it, and it's the one that lost the beach.
**With L15:** ~157 -> ~95-100 ms (the per-tick items halve with the ticks). A 15 Hz tick a frame then runs the game at ~65-70% of real time. That's still 3x the 33 ms bar.

## c20 (PSP-1000, tock: opening frame 56 ms, tick 21.6; swarm 116 ms, tick ~46)

| Compromise | What a player would notice | Worth (ms a frame) | Label | Where it stands |
|---|---|---|---|---|
| **L15** | as on b30 | opening -22, swarm **-46** | U on M | declined |
| **UNSEENPOSE** | as on b30; c20's outcome differed a little | swarm up to ~-5 (the runs drift); before play parts -11% of the tick | E | shelved |
| **FX15** | as on b30 | swarm ~-2.1 | U on M | idea |
| **aicad2** | as on b30 (c20's swarms keep the full rate anyway) | ~-0.6 | U on E | built, off |
| **AIFAR** | as on b30 | up to -0.3 | U on E | not built |

**Best set without L15:** swarm ~-5 to -8 (116 -> ~110 ms, -5%), opening ~-2 to -3. **With L15:** swarm ~116 -> ~68 ms, opening ~56 -> ~35.

## d20 (PSP-1000 split from w140, framesplit 39798186814: fly-through frame 108 ms with ticks 74; swamp fight 70.5 with ticks 38; b145's ladder 12.5 fps on the PSP-3000)

d20 is the effects map: effects and particles are 13-22% of its tick.

| Compromise | What a player would notice | Worth (ms a frame) | Label | Where it stands |
|---|---|---|---|---|
| **L15** | as on b30 | fly-through -26 to -37, swamp ~-19 | U on M | declined |
| **FX15** | as on b30 | fly-through ~-5.4, swamp ~-4.2 | U on M | idea |
| **aicad2** | as on b30 | fly-through ~-4, swamp ~-0.7 | U on E x M | built, off |
| **PRIO** (`particleprio60`) | far smoke and debris thinner in busy fights (22% fewer particles) | ~-1.5 | M (PSP-1000, one run a side) | merged, off |
| **UNSEENPOSE** | as on b30 | fly-through **worse** (+6% of the tick) | E | shelved |

**Best set without L15:** fly-through ~-11 (108 -> ~97 ms, -10%), swamp ~-6 (70 -> ~64). **With L15:** fly-through ~-40, swamp ~-22. (LM, lights merged per surface, would add ~-2 in the swamp, but it plays and looks the same, so it isn't frozen: it waits on its build.)

## c10 (PSP-1000, tock w149c: frame 53-91 ms, of it drawing 30-37 ms; tick 13-25 ms)

c10 is the drawing map: the main CPU's drawing is a third to a half of its frame, and no compromise in the catalogue is sized on c10's drawing.

| Compromise | What a player would notice | Worth (ms a frame) | Label | Where it stands |
|---|---|---|---|---|
| **L15** | as on b30 | -13 to -25 (the cinematic most) | U on M | declined |
| **aicad2**, **FX15** | as on b30 | ~-1 to -2 together | U | as above |

**Best set without L15:** ~-1 to -2 ms (~2%). **With L15:** cinematic ~91 -> ~67, hands off ~55 -> ~42.

## The answer

- **No set of catalogued compromises reaches 30/30 on any of the four maps.** Without L15, lifting the freeze buys 2-10% a map, and the largest piece on b30 (aicad2) is the one that broke the beach.
- **L15 is the only multi-x lever.** It takes a third to a half off every tick-bound frame, and b30's game speed goes from 42% to about two-thirds of real time. It's also the one you declined as too drastic, and it can't be shared with the multiplayer build.
- **What isn't sized yet:**
  - loop fission (TYPEORDER's strong form, aimed at the instruction-cache misses: grouping by type was priced on the PSP at no gain);
  - a root-only decode for UNSEENPOSE;
  - anything that thins c10's drawing (draw distance, objects drawn).

  Each would need a prototype before it could join a table.
- **If you lift the freeze partly,** the order by worth over risk is: FX15 (d20, c20; a census first), RENDERTICK and AIFAR (small, low-risk), PRIO at 60 (d20, measured), then aicad2 only with a fix for the beach.

## Sources

- **PSP frames:**
  - b30: sierra-hw 40888171017, 40892599951;
  - c20: 40893621828, 40892667725;
  - c10: 40889089669;
  - d20: framesplit 39798186814, b145 ladder 40544405192.
- **AI:** aicnt1 sierra-batch 40899343318; aicad 39750884770 and 39761384998 (outcomes).
- **The other levers:**
  - UNSEENPOSE: up7 39878434540, uf1 39886129505;
  - PRIO: 40124814316;
  - L15: `rank_pipelined.md` §10.2;
  - the rest: `levers.tsv` rows of the same names.
