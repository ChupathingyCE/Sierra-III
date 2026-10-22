# Review: aicad2 and lodpsp (two compromises, both off by default)

Both are compromises under the look rule (CLAUDE.md: approximate the original's look and feel; a compromise is priced like any lever, built off behind its keep word and shown to the owner as an A/B). Neither changes anything with its word off. Both are campaign-only in effect: `aicad2` is compiled out of action in the multiplayer build and ignored in networked games; `lodpsp` changes only what is drawn.

## 1. `aicad2`: each actor thinks on every other tick (lever AICAD)

**What it does.** In `actor_update` (actors.c, `FIXES_AICAD`), on an actor's resting tick (half the actors on even ticks, half on odd, by the actor's index) the thinking is skipped:

- `actor_get_timeslice` (the actor can't hold the timeslice: no firing-position search, no path re-plan, no perception refresh that tick), `actor_situation_update`, `actor_decision_loop`;
- perception's prop service: `psp_perception_rest` (actor_perception.c) answers TRUE for a resting actor, so no prop is serviced and the ray queue casts no ray for it.

Every tick still runs `actor_general_update`, `actor_input_update`, perception's decays and danger zone, `actor_emotion_update`, `actor_clear_orders`, `actor_action_update` (the action's own timers), then the control: `actor_action_control` rebuilds the orders from the last decision, and moving, looking, firing and `actor_unit_control` run as before. So a unit never stands still or stops shooting on a resting tick; it carries on with what it last decided.

**The timeslice at half rate too.** `actors_update` gives the tick's one timeslice only on two ticks in four (one even, one odd: `game_time & 2`), so both halves of the actors still compete for it and the costly search comes round half as often. The service timer counts thinking ticks only.

**Kept at full rate:** swarms, charging actors (melee and leaps are timed to the tick), dormant actors, and an actor's first update (a new actor, or a reused slot: the slot's identifier). The `aicad:` line (every 30 s of game time, cumulative) counts actor updates, those rested, and the resting turns these kept.

**Why it can work.** Halo's own reactions are slower than a tick: perception's acknowledgement timers, the 15-tick minimum between timeslices, `become_dormant_timer` of 60. One tick (33 ms) later is inside those delays [D]. Bungie's code already treats the timeslice and prop servicing as round-robin work that can wait [D: `actor_get_timeslice`, the prop timers in `actor_perception_update`].

**What a player could notice.** Enemies and marines react up to a tick later; fights play differently (not exact), so the outcome census below compares against the runs' own spread, not tick by tick.

**Price [U, before the read].** ai_update is 25.1-29.6 ms a frame on b30 on the PSP [M], of it thinking ~78% [E]; halving it is ~10-12 ms a frame on b30, ~7 on d20, ~2 on c20, if the halving is linear.

**Evidence:** emulator gain and outcome census: section 3 (to fill from the batches).

## 2. `lodpsp`: models' detail picked 1.76x nearer (lever LODS)

**Already in the port [D].** Since early builds the level-of-detail pixels are measured on the PSP's 272 lines (`object_get_level_of_detail_pixels`, `FIXES_19` `#lod`: the frustum's pixel count x 272/480; `fulllod` turns it off). So the port already picks lower levels 1.76x nearer than the Xbox, at the size models really appear.

**What `lodpsp` adds.** The same factor again (x (272/480)^2 in all), so lower levels come in 1.76x nearer than now (3.1x nearer than the Xbox) and the smallest objects drop out sooner. Shadows follow the same pixels. This is what the model census counted as "LODS" (it scaled the pixels the renderer had already scaled). It is a compromise for the GE-bound cinematics (b40's opening above all), not a correction: the owner's call after an A/B.

**Key line:** render_objects.c, `object_get_level_of_detail_pixels`: `* (PSP_KEEP_ONCE("lodpsp") ? (272/480)^2 : 272/480)`.

**Price [E/U].** b40 cinematic model vertices -15.7% [E] x the models' GE time there (~16 ms a frame [M]) = at most ~2.5 ms a frame where the GE binds; ~0 on play stretches (draws at the top level already).

## 3. Evidence

To fill: the emulator's ai_update per window off against `aicad2` (b30 flight, landing and beach; d20; c20), the outcome census against the off runs' own spread, the strict run's faults, and the b40 side-by-side for `lodpsp`.

## 4. The owner's A/B

- **`aicad2` on b30:** one PSP job, keepsets `- | aicad2`, repeat 2, b30's timed route, nomouth. Read the flight and the beach, not the opening cinematic (it has favoured one side in A/A pairs). Then play b30's beach and d20 by hand with the word on: do marines hold the beach, do enemies flank and take cover as usual.
- **`lodpsp` on b40's opening:** keepsets `- | lodpsp`, repeat 2, and the emulator's screenshots side by side.
