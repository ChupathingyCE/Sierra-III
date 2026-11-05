# The redesign: the tick

Sheepdog01, 2026-11-05 07:42: "we're working on a redesign, not patches." This page is the design's tick section: what binds the main CPU's tick, what can change that, and the order to build it in. It rests on the recon's ledgers (`redesign_recon.md`) and the cost map (`cpu_cost_map.md`), and it cites them rather than restating them. The fact sheet behind it is lab or-job 42038302650 (128 of 130 quotes checked).

**The goals** (Sheepdog01, 2026-11-05 13:30, rewording 08:33's; even motion is the end goal):
- **The floor: real time.** 30 ticks a second held in every gameplay stretch, tried or not. That means at least 15 fps: Halo caps a frame's elapsed time at 1/15 s (`main.c`, `psp_hardware_reference.md`), so below 15 fps it still runs two ticks a frame and the game slows down instead of skipping ticks.
- **The target: tick parity.** 30 fps, one frame per tick, as a per-level average over interactive play, measured as frames ÷ seconds (cutscenes excluded). Per level, so a heavy level can't hide behind a light one; frames ÷ seconds, so a slow stretch counts for the time it takes.
- **The stretch: parity everywhere.** 30 fps minimum in every stretch.
- **Not pursued: above 30 fps.** At 30 ticks a second a frame past 30 either repeats a tick or interpolates between two (an extra draw a frame and a tick of latency). 30 fps is the end state.
- **Even motion: the pacing's 20 fps step is deferred** (Sheepdog01, 13:26). At 30 ticks only 30 fps (1 tick a frame) and 15 fps (2) move evenly; 20 fps alternates 2 and 1 and judders. Halo's own pacing is already on by default (`vblankthrottle`, b90; `psp_fixups.py` FIXES_36 `main.c#throttle`) [D]: it pads each frame to whole vertical blanks (2 = 30 fps, 3 = 20, 4 = 15) and steps the interval after repeated misses, so it can settle on the 20 fps step. Pacing that holds only 30 or 15 is a small change to it, made later: behind a keep word, decided by Sheepdog01's hand A/B once the heavy maps near the floor. Benches already run the pacing off (`benchrun`, `ceilingbench`, `mebench`), because padding hides what a change saves; keep it that way for any change to it.

In frame time: the floor 66.7 ms (two ticks a frame), the target 33.3 ms (one).

Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or docs, [U] inference.

## 1. What binds

- **The tick sets the pace.** On b30, c20 and both of b40's late zones the frame waits on the main CPU's ticks (`frame_timeline.md` §1) [M]. b30's beach: frame 97.2 ms, tick 65.4 a frame [M, PSP-1000 42000782600].
- **Most of the tick is waiting, not working.** It waits on memory for 54-68% of its own cycles (b30 63%, c20 68%, d20 56-60%, b40 62%) [M, `cpu_cost_map.md` §1]. The core is in order: one miss in flight, no hardware prefetcher, a random line ~73 cycles [M].
- **The misses are spread thin.** Instruction misses are capacity: ~70 KB of hot code fetched again 47-75 times a tick [E]. Data misses run across the object pool (16%), the tag window (32%, half of it the level BSP), globals, stacks and the cluster walk [E, `cache_work.md`]. The top 30 functions hold 44-46% of b40's tick [M].
- **The GE makes every miss dearer.** A main-CPU miss takes +65-96% longer while the GE reads textures from RAM, and +272% while it reads vertices [M, `cpu_cost_map.md` §2]. Its reads inside the ticks cost b30 11.7-15.1 ms a frame of tick [U on E x M, lead A].
- **The leaf work is done.** The exact passes, the layouts, the profile, the asserts out, the data placements, the scratchpad, BSPLAYOUT (neutral on the PSP) and every subsystem's stage-major ceiling (none with an exact cut worth building) are in `cpu_cost_map.md` §4-4a with their prices. What's left at the leaves is under the floors (collcensus, 2026-11-05).

So the tick doesn't get faster by shaving functions. It gets faster when **the main CPU does less of it, waits less for each miss, or does it less often**. Those are the three parts below.

## 2. The parts

| Part | What it changes | Ceiling | Exact? | Where it stands |
|---|---|---|---|---|
| **T1. Work off the main CPU** (B1) | Whole jobs placed on the ME where the main CPU binds | ~5-8 ms a frame on b30's beach, ~2-3 on c20 [U on M]; AITICKOLD (the AI's mid-frame think on the ME from a world a tick older, a compromise): b30's flight frame -9.7 ms (-7.8%, real), the beach in the noise because the ME cuts 30% of its second passes there [M, PSP-3000 42041663929] | Yes, job by job; AITICKOLD no (Sheepdog01 07:30: "try it and see") | Sized (`frame_timeline.md`, "Which tick and frame jobs could move"). MEVISEARLY slice 1 gave nothing because the main CPU waited for the answer. **Slice 2 is built:** MEVISPUSH + RINGSLOTDONE (recon/mevispush 3942f862), exact (mvp1 42034882521), its PSP pair 42036870690 in flight; the push stays where it is and `sound_render` moves after it, so the wait is hidden (`mevis_slice2.md` §1); ~1.2-2.3 ms, which already holds RINGREFUSE's jobs and the sound manager's hiding work. Then DRAWRAYSME (~2.5). AITICKOLD is being built (recon's aitickold) |
| **T2. The shared pool** | Divisible work (points, AI thinks in batches, decoding, posing batches) taken by whichever unit is free within the frame | Grows with load: what T1's fixed homes can't place | Results identical either way where `exact_state.md` says so | mepointsclaim (B56) is its first form. The placement rule: fixed homes, overloads fixed where they happen, a pool for the rest; no balancer (Sheepdog01 2026-11-04 22:53) |
| **T3. Parts of the tick run less often** | Work a player wouldn't feel run on alternate ticks | AIFAR: tick -1.2 to -2.7% [M]. FX15: d20 tick -0.6 to -1.4% [M] | No: compromises, each behind its word, each Sheepdog01's A/B | AIFAR, FX15 built and off. **Not L15** (the whole simulation at 15 Hz): the goals are 30 ticks a second, and Sheepdog01 holds it in reserve like overclocking (2026-11-05 08:33) |
| **T4. The GE kept off the tick's misses** | When the GE reads main RAM, and from where | The GE wholly out of the tick: tick -2.6 to -7.6 ms, but frame +14 to +52 ms [M]: serializing loses. Room did pay once: CTNG's +177 KB of copies, frame -2.8 ms (`frame_timeline.md`) [M] | Yes | **Measured, and closed as an ordering design** [M, PSP-3000 sierra-hw 42088765228, gebusy2's tick share by kind, Sierra-III master/t4kinds 79ddb7d4]: with the ME replaying, the GE draws the last frame during the next frame's ticks, so 96-100% of its busy time falls inside the tick (b30 flight 56.6 of 58.7 ms a frame, landing 61.6 of 62.9, beach 24.3 of 24.3), models ~70% of it and the level ~23%. There's no time outside the tick to move vertex-heavy kinds into, so reordering the replay changes nothing. What's left on this ground is less read per model (the models' vertex diet: MV14, MVDIET, PRECONVERT, SKINW2, SKINSTRIP, LISTBYTES in levers.tsv) and room where a bake frees it (VRAM; VMFILL didn't pay at the margin) |
| **T5. Walks that overlap their misses** | The collision, cluster and node walks reshaped so each miss has ~70+ cycles of work to hide behind (next-item prefetch) | 0.8-2.6 ms a tick [E], ceilings modelled with I-misses waiting for fills, as the PSP now shows [M, PSP-3000 bench-hw 42086540153, halo_bench16]; as the code is, 0.2-0.4 [E]; hand placements gave microseconds | Yes | **The hardware answer is in:** an instruction miss waits for a data fill in flight [M, PSP-3000 bench-hw 42086540153], so as the code is the ceiling is 0.26 ms a tick, not 1.24 (`prefetch_map.md`). The reshaped walks need their windows in hot code: after the code layout's work |
| **T6. A tick-start world** (WORLDVIEW) | Collision and node matrices read a copy of the world as the tick began, so each runs as one pass | Collision ~0.75 / ~0.54 ms, node matrices ~0.7-1.2 a tick [U on E], less the copy's price (uncounted) | No: it changes campaign play, inside every client bound | Catalogued. Sheepdog01's call, and the copy's price is counted first (`stage_state_note.md`) |
| **T7. Data laid out for the tick** | Fewer data lines a tick: each object's hot fields (position, sphere, cluster links, flags) apart from its cold ones; the BSP walk's records packed in the order it reads them | Unsized. The ground: the tag window 32% of the tick's data misses (half of it the level BSP), the object pool 16% [E, `cache_work.md`]; data misses are about half the tick's miss cost (`cpu_cost_map.md` §1) | Yes: addresses move, slot numbers and tag indices don't | **Unscored** (`cpu_cost_map.md` §4, "Data density and layout"). Size it as §4a sized the subsystems: the hot fields' lines a tick against the whole records'. A BSP repack is a bake change, so its price includes testers re-baking (Sheepdog01 10-28); BSPLAYOUT (neutral) is the try to read first |

**Why this shape.** T1-T3 attack the time the main CPU spends; T4, T5 and T7 attack what each miss costs and how many there are; T6 is the only way the two biggest subsystems (collision 25% of the beach's tick, node matrices 10%) can be restructured at all, and it costs exactness. The exact parts: T1 ~5-8 ms a frame on b30's beach; T5 is 0.26 ms a tick as the code is (the fill bench), more only once its walks' windows sit in hot code; T7 unsized. AITICKOLD, which Sheepdog01 takes as imperceptible, carries the next large number.

## 3. The build order

Each step proves itself in the lab and rides the tick/tock cadence.

1. **MEVISEARLY slice 2** (T1, recon): MEVISPUSH + RINGSLOTDONE, built and exact; `sound_render` moved after the push so the main CPU doesn't wait for the views. Its PSP pair on b30's beach is in flight.
2. **AITICKOLD** (T1, recon): built off by default and priced (the flight -7.8% frame, the beach in the noise). Before a default: why the beach's second passes are cut (the job overruns the AI word's bound: fix it where it runs) and what a cut pass does to the actors it didn't reach.
3. **The fill bench** (T5): **done** [M, PSP-3000 bench-hw 42086540153, halo_bench16]: an instruction miss waits for a data fill in flight, so prefetch dropped into today's code is the 0.3 ms part (0.12 net of its own cost: not worth building). T5's reshaped walks keep their ceiling only with each window in hot code, so step 6 follows the code layout's work, not before it.
4. **The replay's reads by tick phase** (T4): **done** [M, PSP-3000 sierra-hw 42088765228, gebusy2's tick share by kind, Sierra-III master/t4kinds 79ddb7d4]: the GE runs inside the tick almost all the time (96-100%), models ~70% of it, so there's no outside-the-tick window to reorder into; T4 closes as an ordering design (its row says what's left).
5. **DRAWRAYSME** (T1), then **the pool's next jobs** (T2: AI think batches, posing batches).
6. **T5's reshaped walks**, if step 3 says the ceiling is the higher one: the collision cluster walk first (its code fits as one pass).
7. **T6**, only with Sheepdog01's OK and the copy counted.

## 4. The targets

From the goals above (Sheepdog01, 2026-11-05): every stretch must reach the floor first; the design is done when the target holds.

| Stretch (PSP-1000) | Today (tock162) | The floor (66.7 ms, 30 ticks) | The target (33.3 ms average) |
|---|---|---|---|
| b30's beach | frame 97.2 ms (tick 65.4) [M] | -30.5 ms: T1 (~5-8) and AITICKOLD (the flight's -9.7 once the beach's cuts are fixed) leave ~15-20 to find [U] | -64 ms: past every exact ceiling on this page |
| c20 | frame 64.7 (tick 38.0) [M] | met | -31 ms |
| b40's snowfield | 7.43 fps (~135 ms) [M, 41999004922] | -68 ms | -101 ms |

**What it says.** The exact parts and AITICKOLD reach the floor on c20 and come close on b30's beach; b40's snowfield is twice the floor away and is the hardest stretch we know. The target is out of reach of everything on this page: it needs the tick's shape changed (T5, T6) and drawing and the ME's work cut too. So the first aim is the floor on every stretch, b40 first.

### 4a. The budget for one tick

The goals as a limit on one tick, with D the frame's work outside the ticks (drawing working and asleep, "other"):

- **The floor** (two ticks a frame): one tick ≤ (66.7 − D) ÷ 2.
- **The target** (one tick a frame): one tick ≤ 33.3 − D.

| Stretch [M, PSP-1000, tock162] | One tick now | D now | Floor: one tick ≤ | Target: one tick ≤ |
|---|---|---|---|---|
| c20 | ~19 ms [U: 38.0 a frame ÷ ~2] | 26.7 | 20.0: **met** | 6.6 |
| b30's beach | ~33 ms [U: 65.4 ÷ ~2] | 31.8 | 17.5 | 1.5 |
| b40's snowfield (`frame_timeline.md`, b40 late) | 42.2 (1.91 a frame: the game in slow motion) | 55.3 | 5.7 | none: D alone is past 33.3 |

- **The target needs both halves cut, about in half each, on the heavy maps.** On b30's beach even a free tick misses 30 fps by its drawing. The tick's own half: ~33 → ~17 ms a tick, more than every exact part above adds up to (~5-8 a tick at best [U]).
- **That's the scale of the tick's memory wait.** The tick waits on memory for 63% of its cycles on b30 (§1): all of it gone would make it ~2.7× faster. Halving the tick means removing most of the wait, which is what T4, T5 and T7 attack, and why T7 is worth sizing.
- **b40's snowfield: its D falls first.** 14.9 ms a frame of its D is the main CPU asleep on drained render jobs (the snow's oversized sprite records); DRAINFLIGHT (exact [E], its PSP pair pending; `cpu_cost_map.md` §4) takes up to ~15 of it. With that, the floor needs a tick ≤ ~13 ms.
- **b40's route overstates the fight.** It replays positions without shooting, so the snowfield holds 30 bipeds a tick against ~21 in Sheepdog01's hand play (tick 42.2 against 35.0 ms), and its collision is mostly the bipeds' and actors' own rays. Read its gap as an upper bound until a route kills as a player does.
- **Where the ME's room comes from.** T1 and T2 place work on the ME, and its room limits them: audio is 10-24 ms a frame of it [M]. Sheepdog01's aim of audio under 10% of the ME, or wholly on the VME (2026-11-05), frees the room these parts spend.

Started 2026-11-05 (master), after the overnight round. Revised 2026-11-05 08:50 (Sheepdog01 08:33: the goals as targets, L15 held in reserve). Revised 08:10 from recon's check (DeepSeek's or-job redtick 42039706086, judged by recon): slice 2 built, T1's double count, the per-tick sum, L15 without interpolation, VMFILL's results, AITICKOLD under T1. Revised 13:30 with Sheepdog01 (chat session): the goals reworded (real time as the floor, tick parity per level as the target, nothing past 30 fps, the pacing's 20 fps step deferred), T7 added, §4a's budget for one tick. Revised as each step reports.
