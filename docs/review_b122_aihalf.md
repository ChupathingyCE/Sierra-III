# Review: far AI perception at half rate (`aihalf122`)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). Same shape as `review_b122_merays.md`: **what**, **key lines**, **why**, **what changes for the player**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `aihalf122`, from `w122` (`8312219`).

| Change | Kind | Default | Words |
|---|---|---|---|
| B1: far actors service their props every other tick | behaviour (AI thinks less often), Sheepdog01's call after an A/B | **off** | `aihalf`; `aihalfdistN`, `aihalfview` |
| B5: the ME's ray answers used a tick late | not built (below) | | |

With the word off, the only new work is one load and branch per actor a tick (the word, read once).

---

## B1: far actors' perception at half rate (`9210d97`)

**What.** Bungie's perception already services one prop (a known unit) per actor a tick: the first whose timer, stepped, reaches last tick's highest, with non-enemies counting 8 times slower and far props 2 times slower [D]. With `aihalf`, an actor that qualifies (below) services no prop on every other tick. Its prop timers keep counting, so on its next tick the round carries on where it stopped: each prop is looked at half as often.

Everything else in perception still runs every tick: the danger zone (grenades, vehicles), the decays and timers, the target's position, awareness. Only the serviced prop's status (line of sight, visibility, hearing, the weights) and its position refresh wait.

An actor rests only on its turn (game time plus its index, odd or even, so half the far actors rest each tick), and only when **all** of these hold:
- it is farther than `aihalfdistN` world units (15 unless set; ~45 m) from every player's head;
- its target, if it has one, is farther than its weapon's maximum range;
- its unit has no recent damage (body or shield: it holds for 2 s after a hit, then decays, up to 2 s more) [D];
- it isn't inside a zoomed player's view cone (the cone narrowed by the zoom). With `aihalfview` it must also be outside every player's unzoomed view.

**Key lines.** `psp_fixups.py`, `FIXES_122["source/ai/actor_perception.c#aihalf"]`:
- `psp_perception_rest(actor_index, counted)`: the test above. The players' eyes, aim and zoom are read once a tick (`psp_aihalf_viewers_now`).
- `actor_perception_update`: `psp_rest` is worked out once per actor, and the "service this prop" test gains `!psp_rest &&`.
- `psp_perception_queue` (b121's ray queue): a resting actor asks for no ray, so the queue casts none for it. If the two ever disagree it costs only speed: a ray nobody takes, or an actor casting its own.
- The log gains `perception, half rate: R of T resting turns taken; kept awake by a player near …, a target in range …, damage …, in view …` every 1,800 passes.

**Why.** AI is about 17 ms of b30's tick on the rig [M], and perception is its largest part: ~2.2 ms a tick on b30's beach in the emulator, its ray ~40% of that, the serviced prop's status most of the rest [E, lab 38510147369]. Far actors are the ones whose reactions the player can't see or feel. `research_b1_insomniac.md` ("drones") is the pattern: less often when far, back to full rate on damage, zoom, or nearness.

**What changes for the player [U].** A far actor notices a new unit, or a change in a known one (it came into sight, went into cover, fired), up to one round of its props later than before. A round is one tick per prop it knows (more for non-enemies), so for an actor that knows 6 units it is ~0.2 s later at 30 ticks a second. Nothing changes for an actor near the player, one fighting in range, one just hit, or one the player is looking at through a scope. This is a change in play, so runs on and off part after the first far actor rests: compare them as alike, not the same.

**Proven so far.** (filled from lab 38581218511 and 38581266921 below)

**Default.** Off, until Sheepdog01's A/B on the rig. Words:
- `aihalf`: on;
- `aihalfdistN`: the distance in world units (default 15);
- `aihalfview`: also keep every actor in any player's view at full rate.

**Campaign and multiplayer.** A networked client never runs `ai_update` (the server's actors drive it), so against ChupathingyCE's servers this code never runs [D]. On a PSP hosting a game, the rest decision reads only game time and positions, the same on every machine, so nothing is drawn from the random seed. The multiplayer build compiles it.

---

## B5: the ME's answers used a tick late: not built

The rig already answers the question. On a50 with the rays on the ME (lab 38567183309, run 2), `ME: ring: 335 jobs (301 done before the wait) … wait 17 us a job (worst 881)` [M]. The ME's job is done before the first actor of its part asks in 9 of 10 jobs, and the average wait is 17 µs, so a tick-late answer would save ~0.02 ms a tick at most.

Against that, it would need:
- a second set of answers kept by actor and prop across ticks;
- a non-blocking look at the ring;
- and a ray from before last tick's moves, which the epoch fence was built to forbid.

The emulator runs each job inside the push, so it would never take the late path, and only the rig could test it. Not worth it; revisit only if a heavier map's `ME: ring:` line shows the wait large.
