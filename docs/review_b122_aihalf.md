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

**Proven so far** [E], batch `aihalf122a`, lab 38581218511, on `9210d97`, `maps=/bake/campaign121`. Every run ended on its wall (or its `exitat`): no crash, no fault, 0 strict sites.

- **Nothing breaks with the word on** (600 s each, 10 at once):
  - b30 on the b113 route: game time 10,178, against 9,890 off;
  - a50 hands off: 10,828 (10,827 off);
  - c20: 8,267; d20: 8,186;
  - a50 under the strict interpreter: 0 bad accesses.
- **The queue and the ME stay exact with actors resting.** b30's route with `aihalf meraysperc perccheck`: 0 mismatched of 95,879 rays taken, 0 of 62,295 ME answers checked, 0 failed jobs, 0 of 743,200 kept unit facts.
- **How much rests** (the log's `half rate` line at the run's end):

  | Map | Resting turns taken | Kept: player near | target in range | damage | a swarm | rays cast, off → on |
  |---|---:|---:|---:|---:|---:|---|
  | b30 route | 31,068 of 73,278 (42%) | 6,845 | 29,120 | 6,245 | 0 | 117,915 → 99,821 (−15%) |
  | a50 | 43,821 of 70,746 (62%) | 26,925 | 0 | 0 | 0 | 139,226 → 95,497 (−31%) |
  | c20 | 17,385 of 31,228 (56%) | 3,164 | 0 | 0 | 10,679 | 33,869 → 19,400 (−43%) |
  | d20 | 1,902 of 9,735 (20%) | 0 | 2,608 | 1,649 | 3,576 | 11,921 → 8,362 (−30%) |

  On b30 most kept turns are actors fighting the marines in range: those keep full rate by design. A swarm (the Flood's infection forms) has no single unit, so it never rests; the log counts it only in the total.
- **No AI frozen in the shots** (`shotevery5`, on and off side by side at the same game time). On b30 the beach fight runs on both sides: an elite meleeing the player at 117.8 s with the word on, grunts and marines firing at 133 and 144 s. a50's marines walk the same paths on both sides. c20 and d20 show no enemies on screen hands off.
- **The cache models, a50's opening to 60 s** (hands off, `fixedticks fixedseed`; play parts, so alike, not the same). Estimate: one cycle an instruction plus 72 cycles a miss at 333 MHz:

  | | Instructions a tick | D misses | I misses | Est. tick |
  |---|---:|---:|---:|---:|
  | off | 2,681,612 | 22,257 | 32,915 | 19.98 ms |
  | `aihalf` | 2,645,209 | 21,771 | 32,581 | 19.70 ms |
  | | −1.4% | −2.2% | −1.0% | **−0.29 ms (−1.4%)** |

  That is with 1.8 resting turns a tick. Later in the same map it is 4.1 a tick.
  Rays are counted at the same pass for c20 and d20 (7,200); their off runs are in `aihalf122b`.
- **At 10 world units** (`aihalfdist10`, batch `aihalf122b`, lab 38581266921): a50 rests 109,285 of 121,146 turns (90%), b30's route 39,596 of 104,278 (38%: the beach fight keeps its actors awake by their targets). Neither crashed (600 s, game time 18,060 and 15,342).
- **Multiplayer** (same batch): the `PSP_MULTIPLAYER=1` build compiles, links and plays Blood Gulch with `aihalf` for 120 s, game time 3,768, no crash. Its perception lines read 0 rays: no AI there.

**The expected gain [U].**
- About 0.08–0.15 ms of the estimate per resting turn (a50's figure).
- b30's beach takes ~3.5 resting turns a tick, which comes to **~0.3–0.5 ms a tick**.
- a50 later in the map takes ~4 a tick: ~0.3–0.6 ms.
- On the PSP with b122's defaults, the ME casts most rays already, so the main CPU saves less: the skipped status work (visibility, hearing, the weights), not the whole ray. Only the rig can say how much.

**Default.** Off, until Sheepdog01's A/B on the rig. Words:
- `aihalf`: on;
- `aihalfdistN`: the distance in world units (default 15);
- `aihalfview`: also keep every actor in any player's view at full rate.

**Campaign and multiplayer.** A networked client never runs `ai_update` (the server's actors drive it), so against ChupathingyCE's servers this code never runs [D]. On a PSP hosting a game, the rest decision reads only game time and positions, the same on every machine, so nothing is drawn from the random seed. The multiplayer build compiles it.

**Rig runs wanted** (`sierra-hw`, after b122's own queue). Both use `ref=aihalf122`, `maps=/bake/campaign121/halo/maps` and `keep="nearauto assettrack geo2 gebusy"`, so the ME's rays are on as in b122.
1. **b30, the beach** (4 runs, ~45 min of the PSP): `map=levels\b30\b30`, `route=b30_b113test1_timed`, `keepsets="- | aihalf"`, `repeat=2`, `wall=660`. Read `abreport.py` for the beach's tick (game time 2,700 to the end) and AI, and the `half rate` line.
2. **a50, far actors** (6 runs, ~30 min): `map=levels\a50\a50`, `keepsets="- | aihalf | aihalf aihalfdist10"`, `repeat=2`, `wall=300`.

Sheepdog01's call after those: whether a far actor reacting up to a round later is acceptable for the tick it gives.

---

## B5: the ME's answers used a tick late: not built

The rig already answers the question. On a50 with the rays on the ME (lab 38567183309, run 2), `ME: ring: 335 jobs (301 done before the wait) … wait 17 us a job (worst 881)` [M]. The ME's job is done before the first actor of its part asks in 9 of 10 jobs, and the average wait is 17 µs, so a tick-late answer would save ~0.02 ms a tick at most.

Against that, it would need:
- a second set of answers kept by actor and prop across ticks;
- a non-blocking look at the ring;
- and a ray from before last tick's moves, which the epoch fence was built to forbid.

The emulator runs each job inside the push, so it would never take the late path, and only the rig could test it. Not worth it; revisit only if a heavier map's `ME: ring:` line shows the wait large.
