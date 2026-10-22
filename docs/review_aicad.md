# Review: aicad2 and aicad3 (AI thinking at half rate; compromises, off by default)

Both are compromises under the look rule (CLAUDE.md: approximate the original's look and feel; a compromise is priced like any lever, built off behind its keep word and shown to the owner as an A/B; AI thinking less often ships only after an A/B shows it breaks nothing). Neither changes anything with its word off. Both are campaign only: compiled out of action in the multiplayer build and ignored in networked games. `aicad3` is the one for the A/B; `aicad2` failed its outcome census and is kept for comparison. The emulator's `aioutcome` census (recon builds) comes with them.

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

**Evidence:** section 4. aicad2 is kept as it was built; `aicad3` (section 2b) is the one to test.

## 2. `aicad3`: the same rest, the timeslice at full rate

**What it changes from aicad2.** `actor_get_timeslice` runs every tick, before the rest test, exactly as without the word (no 2-in-4 gate), and the actor that wins the slot thinks whole that tick. So the firing-position search that sends an actor to cover, and the path re-plan, come round as often as in the original; only the other actors' decision loop, situation and prop service rest on alternate ticks. The ray queue asks before the slot is given, so for the holder it can queue a ray from last tick's answer; at worst that ray goes unused (the queued answer is taken only when it matches the call bit for bit). The census line adds `timeslice N`: resting turns kept because the actor held the slot.

**Why.** aicad2 sent marines to cover half as often, and the beach went worse for them (section 4). The search is the one part of thinking whose delay a fight feels: it decides where a unit stands.

## 3. `lodpsp`

In w140 on its own (`FIXES_LODPSP`, `docs/review_lodpsp.md`); this branch no longer carries it. Its look evidence (the b40 shots below) is that review's too.

## 4. Evidence

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

Everything scripted later (save_beach_5, the shaft and valley placements, shaftB at 13,084) comes at the same tick in all six. No far actor stuck, no crash. But the beach fight goes worse for the marines in every `aicad2` run: the covenant are never cleared, the beach's first save is never written, and one run loses every marine, where the off runs' worst is 6 marines and one covenant left. **That is outside the runs' own spread: `aicad2` as built breaks the beach fight, so it isn't ready for the owner's A/B** (but see "aicad2 looked again" below).

**Why, most likely [U].** On the beach the marines and the covenant both think half as often, but the timeslice (the firing-position search that sends a marine to cover) comes round half as often too, and a marine out of cover dies; the covenant, with more units, are hurt less by the same delay. The next build to try keeps the timeslice at its full rate and rests only the decision loop, situation and prop service (or exempts actors near a player, as farslice did); it removes less (the search is about half of the decisions' time [E]).

**Strict [E].** b30 with `aicad2` and `lodpsp` to game time 130 s on the strict interpreter: 3,762 ticks, 0 faults.

**`lodpsp`'s look [E].** b40's opening, screenshots every 3 s: at 4 s 387 pixels differ, at 7 s 2,293 (the nearest elite's armour edges and its weapon: a lower level of detail on a model about a third of the screen tall), at 10 s none. A player sees slightly coarser silhouettes on models in the cinematic's middle distance.

**Compiles:** campaign, recon and `PSP_METRICS=0` builds of this branch succeed.

### aicad3

**The gain [E, cache model as above, one run a side].**

| Map, stretch | Off | aicad3 | Removed | (aicad2) |
|---|---|---|---|---|
| b30, flight and landing | 4.79 | 4.09 | 0.70 (15%) | 34% |
| b30, beach (the fights part) | 2.57 | 1.88 | 0.69 (27%) | 42% |
| d20, swamp route | 1.74 | 1.57 | 0.17 (10%) | 33% |
| c20, route to 120 s | 1.35 | 1.40 | none | 18% |

Tick-phase cycles: b30 -6.4%, d20 -0.5%, c20 -3.1%. Rested: 47% of b30's actor updates (2,093 resting turns kept by the slot), 20% on d20 and 25% on c20, where swarms, dormant and charging actors think every tick. Keeping the search at full rate gives back about half of aicad2's gain on b30 and most of it elsewhere: the search is the costly part of thinking [E].

**What that could be on the PSP [U].** b30's ai_update is 25.1-29.6 ms a frame [M]; 15% of it on the flight is ~3.8-4.4 ms a frame, and 27% on the beach ~6.8-8.0, against the A/A floor (one side's own spread: flight 2.5, landing 3.4, beach 3.0 ms). The flight's gap should clear its floor narrowly; the landing's may not. The beach's would clear it easily, but the fights part there, so it is the less reliable stretch.

**The outcomes [E, the same census, three runs a side plus a fixed pair].**

| Run | Marines at gt 5,700 | Covenant left | beach_lz_marine cleared | save_beach_lz | save_beach_1 |
|---|---|---|---|---|---|
| off a | 0 | 5 | no | no | no |
| off b | 4 | 1 | no | 4,922 | no |
| off c | 5 | 1 | no | 4,712 | no |
| off, fixed seed | 6 | 1 | no | 4,352 | no |
| aicad3 a | 6 | 0 | 4,205 | 4,207 | 5,790 |
| aicad3 b | 7 | 0 | 4,239 | 4,242 | 5,790 |
| aicad3 c | 5 | 0 | 4,566 | 4,567 | 5,790 |
| aicad3, fixed seed | 5 | 0 | 4,437 | 4,442 | 5,790 |

Everything scripted later (save_beach_5 at 6,750, the valley at 6,660, shaftB at 10,775) comes at the same tick in all eight; no far actor stuck, no crash. With `aicad3` the beach is cleared in all four runs, and with the defaults in none of these four. Off's spread over both censuses is wide: 0-8 marines, 0-5 covenant left, beach cleared in 2 of 7. `aicad3` sits inside it, at its better end. The fixed pair parts at game time 45 s, in the first fight, as any change to the AI's timing would. **Verdict: `aicad3` breaks nothing the census can see; it goes to the owner's A/B.**

**aicad2 looked again.** This census's off runs did worse than the first's (one lost every marine, as one aicad2 run did). So aicad2's 7/0/3 is at the worse end of off's spread rather than clearly outside it, but it never cleared the beach or wrote save_beach_1 in three runs. It stays built and untested on the PSP.

## 5. The owner's A/B

- **`aicad3` on b30:** one PSP job, keepsets `- | aicad3`, repeat 2, b30's timed route, nomouth on both sides. Read the flight first (its gap should be ~4 ms against a floor of 2.5), then the landing and the beach; not the opening cinematic, which has favoured one side in A/A pairs. Then play b30's beach and d20 by hand with the word on: marines take cover and hold the beach, enemies flank and take cover as usual, nothing stands idle.
- **`aicad2`:** no PSP time; kept for comparison.
- **`lodpsp`:** the emulator side-by-sides (`docs/review_lodpsp.md`).
