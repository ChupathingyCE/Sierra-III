# Review: models' level of detail picked nearer, on by default (keep word `lodpspoff`; a compromise)

**What it does.** Models already pick their level of detail at the size they appear on the PSP's 272 lines (FIXES_19; `fulllod` restores the game's 480). With this change the pick is made as if the screen were 272/480 as tall again: the pixels a model covers are scaled by (272/480)² instead of 272/480, so each lower level of detail comes in 1.76x nearer (3.1x the area) and the smallest objects drop out sooner. Shadows follow the same pixels. On by default; `lodpspoff` gives the previous pick; `lodpsp` is still accepted (it was the opt-in word). ui.map is untouched: FIXES_19's showcase returns the top level there before any of this runs.

**The floor (the glass fix).** Some models' lower levels lose a part instead of simplifying it. With the nearer pick, the Pelican's cockpit window went black in b40's opening cinematic: its glass (a see-through shader) exists at the top level only. So each model gets a floor, read from its own tag data the first time it is drawn in a map: the lowest level at which every region permutation still draws every see-through shader its top level draws. The nearer pick never takes a model below its floor unless the game's own pick (the previous default) would already be there. Opaque materials that a lower level merges into one don't count: that is the simplification itself. A first version that counted any lost shader floored 19 of b40's 82 models (every biped and weapon) and left the nearer pick removing almost nothing [E].

Models the floor holds (`lodpspscan`, which logs every model's floor at a map's first draw; b40 13 of 82): Pelican and lifepod_entry at the top level (glass, shader type 8); Warthog at the top level (type 6); plasma pistol (top) and plasma rifle (level 3+) (type 9); assault rifle (top), sniper rifle, needler and frag grenade (level 3+); elite, elite_special and marine_armored (level 2+); grunt and cyborg (level 1+). The Ghost, Banshee, Wraith and the dropship have no floor: their lower levels keep every see-through part. `lodpspall` is the nearer pick with no floor, for an A/B.

**Cost.** One hash lookup a call of `object_get_level_of_detail_pixels` (a table of 1,024 models, cleared at each map's tags), plus the tag walk once per model a map. In the cache model the engine walk is ~0.1 ms a frame higher with it on (below).

## Evidence [E]

All on the emulator (the gain is under the PSP's A/A floor, so there is no PSP pair). This build carries RSO v4b on by default: RSO keys its kept records by the level of detail's pixels, so these runs measure the nearer pick with RSO live.

**The window.** b40's opening cinematic, a shot every 3 s, at t8.9 (the shot where the window went black): in a 60x60 box over the window, the default against `lodpspoff` differs at 0 pixels, `lodpspall` (no floor) at 1,371. Whole frames, the default against `lodpspoff`: 0, 16, 0, 8,230, 7,976, 18, 0 pixels of 130,560 (the elites' edges at t5 and t7).

**What it removes**, the default against `lodpspoff`, the same route both sides (gereads, every 300 frames; model vertex KB the GE reads a frame):

| stretch | model vertex KB a frame | change | models' display list KB | part draws |
|---|---|---|---|---|
| b40 cinematic (0-604 ticks) | 757.9 -> 675.8 | **-10.8%** (no floor: -25.3%) | 34.7 -> 34.9 | |
| b30 flight (570-2000) | 4,984.6 -> 4,949.9 | -0.7% | 312.4 -> 312.3 | 289.0 = |
| b30 landing (2000-2700) | 5,850.1 -> 5,850.1 | 0 | 167.9 -> 167.8 | 169.0 = |
| b30 beach (2700-5400) | 1,912.0 -> 1,888.6 | -1.2% | 38.1 = | 36.7 = |
| a30 fight (300-6000) | 2,568.4 -> 2,562.0 | -0.2% | 71.5 = | 83.9 = |
| c20 swarm (300-3600) | 905.6 -> 901.6 | -0.4% | 50.9 -> 50.8 | 27.8 = |

The busy stretches draw their models near or as cinematic stars (top level whatever the pick), so the nearer pick reaches few of them. Pictures on those stretches differ little: at most 5,890 pixels of 130,560 in one shot on b30 (most shots under 200), 3,006 on a30, 974 on c20.

**Record lane** (cache model, drawing phase, the same play both sides): b30 22.2 -> 22.2 ms a frame, a30 12.7 -> 12.7, c20 11.8 -> 11.8; the objects pass 4.17 -> 4.17, 2.72 -> 2.76, 1.36 -> 1.43; the engine walk +0.1 ms on each. No record-lane gain; a small cost from the lookup.

**Exact in play.** Play and both random seeds identical tick for tick, the default against `lodpspoff`: b30 5,400 ticks, a30 6,000, c20 3,600 (statecrc, fixedticks fixedseed nomouth). The whole-state hash differs from tick 2, as it must: the cached object render states, which are game state, hold the level of detail's pixels. Strict 0 sites on b40 (with `lodpspscan`) and a10. Front door: menu into a10 with the default, 780 s played, no fault.

**Verdict.** A look compromise worth ~11% of model vertex reads on b40's cinematic, the one stretch bound on model vertices (at most ~1.7 ms a frame there, scaling the earlier pricing of models' GE time [U]), and under 1.2% on the busy stretches, where it does not pay. Every figure is under the PSP's A/A floor, so it is judged in the emulator: exact in play, the glass kept.
