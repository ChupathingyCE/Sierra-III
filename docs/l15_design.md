# L15: the campaign at 15 ticks a second, design note

2026-10-29, hardware session (Sheepdog01 11:59 lifted L15 from the compromise freeze, off by default behind a toggle; the master's order 17:10Z: design note first). The sizing this builds on is the lab's `findings/rank_pipelined.md` §10.2. Labels: [M] measured on a PSP, [E] emulator, [D] read in the code (ChupathingyCE d98982b), [U] inference.

## What it buys, and why it's one tick a frame

- On every heavy stretch a frame already runs two 30 Hz ticks, because the frame clock gives a frame at most 1/15 s (`main_update_time`). game_time_update allows up to 7 ticks for a local game, but the clock cap makes it 2 [D].
- So the game is already shown at 15 Hz of game time there. L15 runs **one 15 Hz tick in that frame instead of two 30 Hz ticks**.
- A tick's cost is per object, per actor and per particle, not per second, so it stays about what a 30 Hz tick costs today [U, §10.2].
- Worth: b30's flight ~157 -> ~95-100 ms a frame, game speed 42% -> ~65% of real time; c20's swarm ~116 -> ~70; d20's fly-through ~108 -> ~70 [U on M] (docs/compromise_table.md).
- Below 67 ms a frame (light maps, menus) the game would draw faster than it ticks. There the existing interpolation draws the frames between ticks (below).

## The toggle

The engine has no dt. Its tick rate is the enum `TICKS_PER_SECOND = 30` (cseries.h), with ~360 uses in 82 files, and every per-tick literal (gravity, decays, countdowns) is compiled in [D]. A keep word can't double a dt that doesn't exist, and making the rate a variable in every build would put its cost in the default build, where L15 is off. So two layers:

- **A build switch, `PSP_L15=1`** (campaign only; refused with `PSP_MULTIPLAYER=1`). It turns `TICKS_PER_SECOND` into a variable (`psp_ticks_per_second`, 30 or 15, fixed for the session at launch), and the constant contexts that use it into run-time values:
  - `seconds_per_tick` in particle_systems.c and projectiles.c;
  - the `#define`s built on it (garbage retry, the co-op timers, unused in the campaign);
  - `TICKS_PER_MINUTE` and the rest.

  The default build doesn't change at all.
- **A keep word, `l15`, inside that build** (off by default). One EBOOT carries both sides, so every A/B is a one-build keepset with no layout noise, and Sheepdog01 can switch it from keep.txt or the launch menu.
- **The cost of the switch itself** (a variable in place of a constant) is the PSP_L15 build with `l15` off against the default build: one pair before anything else is read. The proof is play and both seeds identical every tick (statecrc) between the default build and PSP_L15 with `l15` off.

## The simulation: the hybrid first

Two forms were sized:
- **full** (everything at 15 Hz): 907 ms saved a game second on b30's flight [U];
- **hybrid** (physics and collision kept at 30 Hz as two substeps, scripts on a 30 Hz clock): 677.

The hybrid goes first: it keeps the parts that are hardest to re-time and easiest to break (vehicles' springs, falls, tunnelling, scripted timing) as they are. The full form is a later step if the hybrid plays right.

With `l15` on:

| Area | How it runs at 15 Hz | Change |
|---|---|---|
| Rates through `TICKS_PER_SECOND` (fire rates, heat, recharge, arming, AI rates, acceleration) | scale by themselves | none: the variable is 15 |
| Physics and collision (vehicles' mass points, bipeds' physics, projectiles' flight) | **two substeps a tick, each a 30 Hz step**, with the tick rate reading 30 inside them | a loop of two around the physics calls, with the variable set to 30 for its span |
| Gravity and per-tick decays outside the substeps | per tick² and per tick | gravity x4, decays squared (`*= k` -> `*= k*k`), by hand: 37 decays, audited one by one |
| Countdowns and literals in ticks (49 non-zero literals in ai, units, items, objects; 20 `game_time +/- N`; UNARMED_MELEE_TICKS 16; ACTOR_GRENADE_TICKS_PER_SECOND 30) | half as many ticks pass | through the variable, or halved, by hand, one by one |
| Animation | one frame a tick, events (footsteps, fire, melee, throw) on exact frame indices | **two advances a tick, events OR'd** (~8 call sites); node matrices made once a tick, which is the gain |
| Scripts | sleep N, sleep_until (every 30 ticks), camera_set over N, sound and fade counts | **a 30 Hz script clock**: hs_update twice a tick, the scripts' game_time read as 30 Hz; camera and fade counts halved where scripts hand them on |
| Recorded animations (the Pelicans' flights, cutscene units) | one 30 Hz entry a tick | two playback steps a tick, presses OR'd |
| Weapons | at most one shot a tick: rates above 15 a second capped at 15 | accepted for the first build, counted (which weapons' tags pass 15 a second) |
| Game time and saves | ticks at 15 Hz | saves carry the rate; a save from the other rate refuses to load |
| Multiplayer | the netcode ticks at 30 on every machine | out: the switch is refused in the multiplayer build, and `l15` does nothing in a networked game (the gate FIXES_AICAD uses) |

`aicad3` (AI on alternate 30 Hz ticks) is subsumed: at 15 Hz every actor thinks every tick, which is the same rate in seconds, and the cover search keeps its rate. So `aicad3` is off under `l15`.

## What's drawn: interpolation

- **At or above 67 ms a frame** (every heavy stretch), one tick a frame: nothing to interpolate. What's drawn is the tick's state, as today.
- **Below 67 ms a frame,** frames between ticks are drawn by the engine's own interpolation (`port/linux/game/render_interpolation.c`; on the PSP: compiled in, keep word `interpolation`, off by default). After each tick it keeps each object's node matrices, the camera and the first-person weapon's pose. Each frame blends the last two by the tick fraction (quaternions nlerped, positions lerped); teleports, pose swaps and camera cuts snap. Antennae, flags and the motion sensor already read it. Under `l15` it's on whenever a frame is shorter than a tick, with two changes:
  - **snap limits doubled:** they're sized for 33 ms (NODE_SNAP_DISTANCE 0.4 wu, the object and camera-cut limits), so fast limbs would snap at 67 ms;
  - **snapshots for drawn objects only, in an arena of its own:** today it keeps every object and ran the heap dry on b30 (l15i1 39895555245: 176,327 failed requests [E]).
- **Not interpolated:** particles and effects (they step at 15 Hz, visible as stepped motion only above 15 fps), the HUD (its timers count real time), decals.
- **Input:** player control and the look still run every frame, and the camera is drawn where the player aims now (the direct camera). Fire and movement land on the next tick, up to 67 ms later where frames outrun ticks, plus one tick of interpolation lag. On the heavy stretches, where frames are longer than a tick, the response gets quicker because the frame does: b30's flight from ~157 ms to ~100.

## Proof

It's a different simulation, so the proof is that it plays the same, never statecrc:
1. **The switch alone:** PSP_L15 with `l15` off against the default build, play and seeds identical every tick (b30, c10, c20, d20). Then the off-vs-default PSP pair: the switch's own cost.
2. **An outcome census** with `l15` on against off and an off-vs-off2 control, as aicad3's was (aicad3c 39761384998), on these:
   - b30's beach (cleared, save_beach_1, marines alive);
   - a30's fights (first_jeep_marine);
   - c20's swarm;
   - d20;
   - the route completing;
   - every scripted beat's game second;
   - falls, jumps and vehicle handling (a fixed input route, positions compared in seconds of game time).
3. **Side-by-sides for Sheepdog01:** shots at the same game second on both sides (b30 flight and beach, c20, d20), and a short emulator capture of motion with interpolation on a light map.
4. **One PSP pair** on b30's flight (keepsets `- | l15`, repeat 2), read by game time.

## Build order

1. The switch (`PSP_L15=1`, the variable, its constant contexts) and the keep word, the hybrid's two physics substeps, gravity, the two animation advances, the 30 Hz script clock and the recorded-animation steps. Proof 1, then a first look at proof 2 on b30.
2. The audit of the literals and decays (by hand, each with its line in the review), and the weapons count.
3. The interpolation's changes (snap limits, drawn objects only, its own arena).
4. Proof 2 in full, proof 3, then proof 4.

Each step is one commit on branch `l15`, merged into the tock only after proof 1 holds (the default build identical) and the first census doesn't break b30's beach.

## For the master and Sheepdog01

- **The toggle's form:** a build switch with a keep word inside it. The default build carries nothing, and the L15 build A/Bs in one EBOOT. Say if a plain keep word in every build is wanted instead; its cost lands on the default build.
- **The hybrid first,** the full form later and only if the hybrid plays right.
- **Weapons above 15 shots a second are capped** in the first build; the count says which ones.
