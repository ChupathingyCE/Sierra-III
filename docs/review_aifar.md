# Review: aifar (far actors out of the fight think 1 tick in 4) and RENDERTICK's count

**Status (2026-11-22): on by default** (Sheepdog01 2026-11-22 01:06, from the 2026-11-18 ruling: running less often where a player can't tell is efficiency, proven by its encounter census; `aifaroff` turns it off). The paragraph below is the earlier status, kept for its reasons.

`aifar` is a compromise under the look rule (CLAUDE.md: AI thinking less often ships only after an A/B shows it breaks nothing), built off by default behind its keep word. With the word off nothing changes: `psp_aicad_rest` answers exactly as before. Campaign only, like aicad3: `psp_aifar_on` is 0 in the multiplayer build, and the rest test sits behind `psp_aicad_active()` (a local game, no network co-op). RENDERTICK was counted and not built (section 3).

## 1. `aifar` (lever AIFAR; `FIXES_AIFAR` in psp_fixups.py, actors.c)

**What it does.** aicad3 (on by default) rests each actor's thinking (situation, decision loop, prop service) on alternate ticks, by `(game time + actor index) & 1`. aifar takes the actor's four-tick phase, `(game time + actor index) & 3`: phases 1 and 3 rest as before, phase 0 thinks as before, and on phase 2 an actor rests too when `psp_aifar_resting` says it is far and out of the fight. So such an actor thinks on one tick in four, and the four phases spread them evenly over the ticks.

- **Far:** 20 world units or more from every player unit's bounding-sphere centre (the actor's `input.position.body_position`). With no player unit, nobody is far.
- **Out of the fight:** none of these: an action with a fight or its own timing in it (fight, flee, uncover, charge, avoid, search, vehicle, obey, converse); a command list; `combat_status` at "definite" or above; known or visible reachable enemies; a target prop; a target seen in the last 10 s (`since_any_target_visible_timer`); a danger zone; recent body or shield damage.
- aicad3's exemptions come first and still hold: the timeslice holder thinks whole, swarms, dormant and charging actors and an actor's first update never rest.
- **One answer a tick.** Perception's ray queue asks `psp_aicad_rest` before the actor's `actor_input_update` refreshes its position, and the update asks again; the far answer is kept per slot for the tick (game time + 1 and the identifier), so both agree. The kept answers are cleared when game time goes back (a revert, a new map). The player positions are read once a tick.
- **Only with aicad3.** With `aicad2` or `aicad3off` the word does nothing, and the log says so.
- **Census:** `aifar: game time N | asked A: near N, in the fight E, rested far R` every 30 s of game time, cumulative; `asked` counts phase-2 asks that reached the far test.

**Key lines.** `psp_aicad_rest`: `phase = (game_time_get() + absolute_index) & 3;` then `if ((phase & 1) == 0 && !(phase == 2 && psp_aifar_enabled())) return FALSE;` and, after aicad3's exemptions, `return (phase & 1) || psp_aifar_resting(actor, absolute_index);`. With the word off this reduces to aicad3's own test.

**What a player could notice.** A far actor out of the fight notices something up to 4 ticks (133 ms) late instead of 2 (67 ms). Halo's own perception delays are hundreds of milliseconds. Nothing near the player or in a fight changes.

**Evidence [E].** Emulator runs of this branch (w142 + FIXES_AIFAR), bake as the tock's:
- **Strict:** b30's route to 100 s, c20 and d20 hands off to 100 s, each with `aifar`: 0 distinct sites, no crash.
- **Play (b30, the outcome census, `aioutcome`):** seven runs a side whose play varies (the clock seeds effects and sound), to 210 s through the beach. Marines alive at game time 5,700: default 7, 7, 0, 8, 0, 0, 0; aifar 0, 0, 7, 6, 7, 7, 3. Beach cleared (and its save written): default 2 of 7, aifar 0 of 7, mostly with one covenant left; at seven a side that can't be told apart from the default's own spread. A fixed-seed pair parts at game time ~1,500 (which covenant the marines kill first); both sides clear the beach. No far actor stuck in any run.
- **Where it acts:** on b30 every run rests exactly 6,370 actor turns, all before game time 2,762: the opening and the Pelican's flight, while the beach's covenant wait out of the fight. From the landing on, every actor asked is near a player or in the fight, so the beach plays differently only through what the flight changed. c20 (hands off, 90 s): 1,045 rests; d20: 363, all in the first 30 s.
- **What a player could notice:** nothing near them. Far fights are untouched by design (an actor in a fight never rests more than aicad3's).

## 2. Price

The cache model's whole-run totals can't price it: the sides part at game time ~1,500, so the rest of the run is a different fight (b30 +3.5% tick fetches, d20 +3.0%, c20 -0.6%, against off-vs-off2 identical). Priced instead from the counts [E x U]: aicad3 removes ~14.7 thousand tick fetches per rested actor turn on b30 (aicad3 against aicad3off), ~12.8 thousand on c20. aifar's rests are then ~0.6% of b30's tick over the route to 150 s, ~1% of the flight's tick where they all fall, ~0.3% of c20's, ~0.06% of d20's. That's an upper bound: a far actor out of the fight thinks for less than the average actor (the band census: 150-290 us an update at 40+ units against 530-840 near). On the PSP-1000 the flight's tick is ~58 ms a frame, so up to ~0.3-0.6 ms a frame [U]: above that stretch's A/A floor (0.1 ms frame, 0.2 tick), so a PSP pair on the flight can see it.

## 3. RENDERTICK: counted, not built

The idea was to run render-only tick work (the HUD's updates, the first-person weapon, the antennas) once a frame instead of every tick. The count says it can't pay:

- **What the tick spends on it [E]** (the emulator's step profile, w142 at b30's route to 150 s and c20 hands off to 120 s; a step runs until the next call in `game_tick`): `hud_update` (weapon, unit, nav points; the motion sensor's state) 11.7 us a tick on b30, 10.5 on c20; `first_person_weapons_update` 25 us in the one b30 window that listed it, and under the table's cut-off (about 5-10 us a tick) everywhere else; `antennas_update` under the cut-off on both maps. b30's tick is 15-25 emulator ms, c20's at least 6.7. So the three are at most ~0.2% of b30's tick and ~0.4% of c20's (both bounds take the unlisted steps at the cut-off). None of their functions reaches the cache model's top list of tick fetches (each under ~0.5%).
- **Only one tick a frame of it could go** (two ticks a frame on the heavy stretches), which halves that again: under ~0.1% of b30's tick, ~0.05 ms a PSP tick [U].
- **And most of it isn't pure drawing state** [D]. The first person weapon's tick update is its animation state machine, whose keyframes fire sounds and effects on exact frame indices; its node matrices are already built once a frame, in `first_person_weapon_render_update`. The HUD's tick update carries its own timers (messages, flashing, the motion sensor's sweep). The antennas' chain is stepped a tick at a time on purpose: stepped a frame at a time, its springs act against the frames' movement (the comment in `game_tick`).

So RENDERTICK stays a catalogued idea, under the bar.
