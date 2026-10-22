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

**The gain [E, cache model, hands-off fixedticks fixedseed nomouth, one run a side].** ai_update in ms a tick of the emulator's clock (profile windows), off against `aicad2`:

| Map, stretch | Off | aicad2 | Removed |
|---|---|---|---|
| b30, flight and landing (ticks 0-2,515) | 4.82 | 3.17 | 1.65 (34%) |
| b30, beach (2,515 on; the fights part) | 3.04 | 1.77 | 1.27 (42%) |
| d20, swamp route to 88 s | 1.80 | 1.20 | 0.60 (33%) |
| c20, route to 120 s | 1.35 | 1.11 | 0.24 (18%) |

The tick phase's cycles (fetches + 72 D-misses + 64 I-misses) fall 12.9% on b30, 2.3% on d20, 4.0% on c20. The census line: on b30 half the actor updates rest (42,882 of 86,352; charging kept 278), and on d20 and c20 only 30% (swarms keep their full rate: 2,593 and 4,283 turns). Scaled to the PSP's ai_update (25.1-29.6 ms a frame on b30 [M]) at 34-42%, that is about 9-12 ms a frame on b30 [U], well above the A/A floor of 2.5-4.4 ms a stretch, but only if play holds.

**The outcomes [E, recon build, `aioutcome`, b30's timed route to game time 450 s, play left to vary: three runs a side].**

| Run | Marines alive at the beach's end (gt 5,700) | Covenant left on the beach | beach_lz_marine cleared | save_beach_lz | save_beach_1 |
|---|---|---|---|---|---|
| off a | 8 | 0 | 4,143 | 4,147 | 5,790 |
| off b | 8 | 0 | 4,120 | 4,122 | 5,790 |
| off c | 6 | 1 | no | 4,397 | no |
| aicad2 a | 7 | 1 | no | 4,497 | no |
| aicad2 b | **0** | **6** | no | no | no |
| aicad2 c | 3 | 1 | no | 9,597 | no |

Everything scripted later (save_beach_5, the shaft and valley placements, shaftB at 13,084) comes at the same tick in all six. No far actor stuck, no crash. But the beach fight goes worse for the marines in every `aicad2` run: the covenant are never cleared, the beach's first save is never written, and one run loses every marine, where the off runs' worst is 6 marines and one covenant left. **That is outside the runs' own spread: `aicad2` as built breaks the beach fight, so it isn't ready for the owner's A/B.**

**Why, most likely [U].** On the beach the marines and the covenant both think half as often, but the timeslice (the firing-position search that sends a marine to cover) comes round half as often too, and a marine out of cover dies; the covenant, with more units, are hurt less by the same delay. The next build to try keeps the timeslice at its full rate and rests only the decision loop, situation and prop service (or exempts actors near a player, as farslice did); it removes less (the search is about half of the decisions' time [E]).

**Strict [E].** b30 with `aicad2` and `lodpsp` to game time 130 s on the strict interpreter: 3,762 ticks, 0 faults.

**`lodpsp`'s look [E].** b40's opening, screenshots every 3 s: at 4 s 387 pixels differ, at 7 s 2,293 (the nearest elite's armour edges and its weapon: a lower level of detail on a model about a third of the screen tall), at 10 s none. A player sees slightly coarser silhouettes on models in the cinematic's middle distance.

**Compiles:** campaign, recon and `PSP_METRICS=0` builds of this branch succeed.

## 4. The owner's A/B

- **`aicad2` on b30:** not yet: it fails the outcome census above. A narrower build first (timeslice at full rate), its census, then the PSP pair.
- **`lodpsp` on b40's opening:** keepsets `- | lodpsp`, repeat 2, and the emulator's screenshots side by side.
