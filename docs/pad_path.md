# The pad path: a replay that holds Sheepdog01's route on any build

Design and feasibility read (the audit session, 2026-11-12; the lead's brief `drift_tolerant_replay` in the lab's `coord/briefs/`). Nothing here is built. Labels: [M] measured on a PSP, [E] emulator or count, [D] read in code or data, [U] inference.

## 0. The answer

**Buildable, and worth building, with one change to the sketch and one honest limit.**

- **The hold is three small pieces, not one corrector.** The view is *set* (it is control state, so setting it upsets nothing). The position on foot is *steered* through the stick and *snapped* only past a bound. The seat is *forced* when the vehicle isn't where it was. [D, §3]
- **The path is a second file beside the pad, made by replaying the pad once on its recording's build, in the emulator.** No PSP time and no re-recording: the emulator now replays Sheepdog01's pads as the PSP does (`vfpudot`, d20 whole and b40 to 12,922 against the live runs' checkpoints [E+M]). [§5, §8]
- **The limit: a held path holds the player, not the fight.** Once any number differs, the AI's dice fall differently, so every generation plays a *different sample of the same fight* around the same route. In fights most of the tick follows who is alive and awake, not the route (the beach: objects, posing, AI and effects are ~87% of the tick [M]). So a held replay makes the bench *valid* on every build; it does not make fight stretches *identical*. [§2]
- **So the bench changes how a pair is read:** the two repeats of each side run on two different seeds, which makes the repeats' spread include the fight's own sample noise at no extra PSP runs; each stretch prints a census; a fight stretch whose census parts is priced per unit of work, not by its total. [§2.3]
- **The first slice that settles the open numbers is cheap:** the path writer and a *watcher* that only measures error (no correction). It shows how fast and how far play parts between seeds and between builds, in the emulator, before any corrector exists. [§9, slice 2]
- **PSP cost of the whole plan: one job.** The corrector's own price, one A/B pair on d20's opening. [§6]

What the lead decides: yes or no to slices 1-2 (path + watcher) now, and to slices 3-5 on the watcher's figures.

## 1. What the hold is for, and what it is not

A pad replays inputs. Inputs give the same game only while every number the game computes is the same, so a pad is tied to its engine generation and, by design, to the old path (a box is proven by play and the outcome census, never by identity). Unheld, the parting is not small: b30_play2 on the rebased tock is equal to the recording through tick 3,837 and ~78 units off by 5,642 [M, the lab's bugs.md B73, sierra-hw 43361959799]. So "tolerate a little drift" is not an option; the route has to be held.

The hold keeps **Sheepdog01's route, view and seat**. It does not try to make the AI do what it did. That is the right cut for pricing: the rebuild's boxes are allowed to change what the AI's dice give, and a bench that forced the old outcome would be testing the old path's stream again.

It also leaves alone one existing property: a replay runs the *recording's* time steps ('F' records), so its ticks a frame are the recording's, not what the build under test would run at its own speed. The hold neither helps nor hurts that.

## 2. Question 1: how much of the cost follows the path

### 2.1 Three kinds of cost

| Kind | What sets it | Held by the path? |
|---|---|---|
| **Route and view** | the draw list, visibility, the BSP walk for the camera, effects and decals in view, level streaming, which trigger volumes fire and so which squads spawn and when | **Yes.** Trigger volumes test the player's position by containment (`scenario_trigger_volume_test_object` tests the bounding sphere's centre against the volume) [D], so a held position fires them at the recording's tick, within the tolerance. |
| **Population** | how many actors and objects are alive and awake near the player: object updates, posing, collision, the AI's think count | **At a fight's start, yes** (spawns are trigger-led). **Through the fight, no:** it falls as actors die, and who dies when is the fight's sample. |
| **Choices** | what the AI does this tick: projectiles, grenades, effects made, who is moving, AI-driven vehicles | **No.** |

### 2.2 The sizes

- **Outside fights the route is the cost.** b30_play2's opening, flight and landing, on two tock builds a day apart, one replay tracking and one not: frame 97.3 / 97.1, 115.4 / 115.4, 104.4 / 104.6 ms (within 0.3%) [M, PSP-3000, 43322377198 and 43361959799].
- **In a fight most of the tick is population and choices.** The beach fight, PSP-3000, instrumented: of 44.2 ms a tick, objects 21.6 (49%), posing 6.7 (15%), the AI on the main CPU 5.9 (13%), effects 4.5 (10%), sound 3.5 [M, the lab's `combat_steps_psp_vs_emu`]. Drawing, which the route does hold, is 22-32 ms of a 50-115 ms frame across the pad stretches [M, the lab's stretches.tsv].
- **One measured sample of "the same route, a parted fight":** the same two b30 runs on the beach, where the bare replay had parted: frame 63.4 against 65.8 ms (+3.8%), tick 35.6 against 37.4 (+5%) [M, same runs; the builds also differ, so this is a size, not a clean pair]. The emulator's older figure is the same order: beach fights vary ~10% between runs [E, this repo's CLAUDE.md].
- **Why the fight parts at all on a held path:** the game's dice are one stream. One different number changes one draw's count or order and every later draw differs, so the AI's choices decorrelate within seconds. They stay draws from the same distribution: the same squads, spawned at the same tick, facing a player on the same route firing along the same lines. [U on D]

So: **a held path removes the route's share of the noise (all of it outside fights) and leaves a fight's sample noise of a few percent of the tick.** A box's gain is often that size, so the read has to handle it.

### 2.3 How a pair is read under that noise

1. **Repeats run on different seeds.** A `sierra-hw` pair is two sides, `repeat=2`. Today both repeats use the one fixed seed, so on one build they are the same game and their spread is only timer noise: it *understates* the noise between two builds. With the hold, repeat 1 runs seed 1 and repeat 2 seed 2 on both sides. The repeats' spread then contains the fight's sample noise, and `tools/abreport.py`'s existing test (is the gap larger than the repeats' own spread) becomes the honest one. No extra runs. Needs: `fixedseed` taking a number (today it is on or off [D, `FIXES_113`]).
2. **Each stretch prints a census, both sides:** actor-ticks awake, actors alive at the stretch's end, objects updated, effects and projectiles made, and the hold's own line (§4). Counts, so the emulator can check them too.
3. **A stretch holds** when the hold's line is inside tolerance and the sides' actor-ticks differ by no more than the seeds do on one side. Its frame and tick are then compared as now.
4. **A stretch that doesn't hold is priced per unit:** the box's own ledger line over its own count (posing a posed biped, collision a query, the AI a think), with the census beside it. Its total is shown and not called a price.
5. **Stretches are classed once per pad** from the recording's census: *route-led* (no live actors in range) or *fight-led*. A box's standalone price and its first in-frame read come from route-led stretches where they exist; fight-led stretches are the second read.

If the watcher (slice 2) shows fights' census parting further than seeds on one build do, the next step is an **outcome hold** (the recording's deaths per squad kept to schedule). It is deliberately not in the first build: §9, slice 7.

## 3. The design

### 3.1 The path (made once per pad)

Replaying the pad on its recording's build with `pathrec` writes one record per game tick, in order:

| Field | Bytes | Notes |
|---|---|---|
| position | 12 | the player's unit; seated, the root object he rides (a seated unit's own position is its seat's offset [D, `psp_player_teleport`'s comment, B32]) |
| desired facing: yaw, pitch | 4 | `player_control`'s `desired_angles`, 16 bits each (0.005°) |
| seat | 1 | NONE on foot, else the seat index |
| flags | 1 | control active (no cinematic, facing not inhibited), airborne, crouched, zoomed |
| weapon | 2 | the held weapon's tag index (a number, not data), for the log only |
| orientation, seated only | 6 | the root's forward and up, 3 × 8 bits each |

About 20 bytes a tick, 26 seated: d20_play1's 29,234 ticks are ~600 KB, c10's 44,859 ~900 KB. Velocity isn't stored: it is the next record's position less this one's. A revert or restart inside a recording (the 'U' records) writes a marker and the tick count starts again.

Every ~30 ticks a **world mark**: the BSP index, the AI's living count, the object count, whether a cinematic runs. Marks are compared and logged, never corrected (§3.5).

**The path holds no game data:** it is where Sheepdog01 stood, where he looked and what he sat in.

### 3.2 The view: set

`player_control`'s `desired_angles` is absolute and is control state: the unit turns toward it by its own rules. A replay reaches it by summing the recording's per-frame look deltas, and that sum parts only through four things [D, `player_control_modify_desired_angles`, `handle_one_player_input`]: a seat's yaw limits (they follow the vehicle), the pitch autolevel (it follows speed), the pitch limits, and a script or a new unit setting the facing at a different tick.

So after the blob is applied, on a tick the path marks *control active*, the hold writes the path's angles into `desired_angles` when they differ by more than 0.25°. No physics is touched. On foot this makes the view exact. In a driver's seat it also holds the steering, since a Warthog steers toward the look.

### 3.3 The position on foot: steer, then snap

Each tick, the error is the path's position less the player's.

- **Under 0.05 units: nothing.** On the recording's own build this is every tick, and the hold must make no correction at all there (§6).
- **0.05 to 0.5 units: steer.** The horizontal error is turned into the player's own frame and added to the recorded throttle, scaled so 0.5 units asks for the full stick, and the sum is clipped to the stick's circle as the engine clips it. The player then gets there by the biped's own movement, so collision, slopes and contact with other bipeds all still apply.
- **Past 0.5 units, or 0.5 vertical, or no gain for 45 ticks: snap.** `object_set_position` to the path's point (it unhooks the object from the map, sets the position, poses it and hooks it back [D]) and the velocity set to the path's.

Why steer at all, and not snap small and often: a snap skips collision, so near a wall or another biped the game would push back every tick and the hold would fight it; and each snap re-poses the player. Steering costs a few float operations and plays as a player would.

What steer can't do, and so what the snap is for: catch up when the recorded stick is already full (the clip leaves no room), a fall, a missed jump, a lift or door that wasn't there yet.

The three bounds are [U]: slice 2's watcher gives the error's real distribution, and they are set from it.

### 3.4 Seats and vehicles: force the seat, then hold the root

Enter and exit are in the recording as the action button at a tick, and they work on replay only if a vehicle is in reach then. The vehicle's place is the world's, not the player's (an AI drove it, a Pelican dropped it, a fight pushed it). So the seat is held as *state*:

- **The path says seated and the replay isn't, 15 ticks on:** the nearest vehicle with that seat free within 3 units of the path's root is entered at once (`unit_enter_seat`, as the scripts' `vehicle_load_magic` does). None there: the nearest of that kind within 30 units is first set at the path's root pose (counted as *fetched*). None at all: logged as *vehicle lost*, the player is held on foot at the path's point, and the stretch is marked not held.
- **The path says on foot and the replay is seated:** the unit leaves the seat, then §3.3.
- **Seated, the root is held:** the view set does the steering; the along-track error bends the recorded throttle; past 2 units, or when the player isn't the driver, the root is snapped to the path's position *and orientation*, with its velocities. Setting the whole pose puts the mass points where they stood on the recording, over the same static ground.

### 3.5 Discrete events: at their tick

Fire, grenades, melee, jump, crouch, zoom and weapon swaps replay **at their tick, as now**. With the position held, "its tick" and "its place" are the same thing to within the tolerance, so there is nothing to choose between. Two are state, not events, and are handled as state: the seat (above), and the held weapon.

**The weapon is logged, not fixed, in the first build.** A pickup needs the dropped weapon to lie where it lay, which is the fight's sample. A mismatch (`hold: weapon differs from tick N`) changes the first-person model and the projectile kind, and an empty weapon fires nothing. If the census shows that matters, granting the recording's weapon is a small later slice.

**Health and shields:** every replay runs deathless already; logged only.

### 3.6 What the hold never does

No correction while the path marks control inactive (a cinematic owns the camera and the player). Nothing on the ME. Nothing in a build without `padhold`: the hooks are the pad recorder's two existing ones plus one at the tick's start, in `psp/src/psp_pad.c`.

## 4. Question 4: tolerance, and how it shows in the log

**"The same route" for cost means the same things in view and in range at the same tick.** Position within 0.5 units (about 1.5 m; the Chief is ~0.7 units tall) and the view set exactly. At that size:

- trigger volumes fire within a few ticks of the recording's (at running speed, 0.5 units is ~7 ticks) [D on the biped's speed];
- the draw list changes only at a portal's edge, and distance-led choices (levels of detail, the far forms) move by 0.5 units in tens;
- the AI sees a player in the same cover, on the same line.

**In the log,** one line per stretch and one per correction:

```
hold: ticks 2774-4041 err mean 0.03 p95 0.21 max 0.48 | steered 412 ticks | snaps 0 | view sets 3 | seat forced 0 fetched 0 | marks parted 2 of 42
hold: snap at tick 5120: 0.74 off (fell 0.61), on foot
hold: mark at tick 5130: living 14 against 11, objects 212 against 205
```

A stretch is **held** with no snap larger than 2 units, no lost vehicle and p95 under 0.5. The lab's stretch reader takes the line with the stretch's row.

## 5. Question 3: can the old pads get a path

**Yes, in the emulator, with no PSP time** [D on the builds, E+M on the emulator, U on the last step]:

- Both recording builds still exist and are ancestors of the tock: b3461c1 (2026-11-09) and race1's fdf856f (2026-11-10). Both pin the engine at d98982b (v0.7.5d, a public upstream commit) and both hold the replay code. [D]
- The emulator now gives the PSP's VFPU bits by default (`PSP_VFPU_DOT`, an emulator patch, so it applies to any build). With it d20_play1 replays whole against the live run's 23 checkpoints and matches the PSP's own replay at all 2,700 ticks compared; b40_play1 matches its 9 checkpoints to ~12,922. [E+M, the lab's `pads_follow_with_vfpudot`, `pads_replay_with_vfpudot`]
- So a path is made by an emulator replay on the recording's build. The path writer isn't in those commits: it goes onto a branch cut from each (it lives in `psp_pad.c` and reads state only, so it carries back cleanly [U]).
- **The proof that a path is Sheepdog01's play** is the one B73 used, as a tool: every `player: at` sample in Sheepdog01's own recording log equals the path at that tick to the log's 2 decimals. A path that fails it is not filed.
- **What is [U]:** that the four pre-rebase pads track in the emulator on their own builds to their ends. The `vfpudot` proof was made on the rebased build's pads. If one parts (a race the old build still had), its path is good to the parting tick and the pad stays A/B input past it. That costs one batch to learn.

c40_play1 (tock eb6d3a7), and b40, d20 and c10 (rebase090 1fbd2bd) are made the same way on builds the tock already tracks.

## 6. Question 5: the corrector's own cost, and its PSP ask

**What it does each tick:** reads one ~20-byte record from a 16 KB window (the pad's own reader, so one more Memory Stick read about every 800 ticks), reads the player's position and angles (lines the tick has warm), ~20 float operations, two compares. On a tracking replay nothing else. [D on the design; the price is the PSP's to give]

**Estimate:** under 0.05 ms a tick and two or three cache lines of its own [U]. It is the same on both sides of any pair, since both sides run it.

**The ask (one A/B pair, 4 runs, through the master):**

```
sierra-hw   pad=d20_play1 (with its path and its init)   build: the tock with padhold
keep="padplay fixedseed padend"   keepsets="- | padhold"   repeat=2
stretch: d20's opening, game ticks 300-3,000 (the shortest pad stretch with play in it)
```

- **Reads:** the frame and tick gap between the sides against the repeats' spread (`abreport.py`), and the `hold:` line.
- **Passes when:** the gap is inside the spread and the hold's line shows **0 steered ticks, 0 snaps, 0 view sets**: the tock tracks d20_play1 to ~6,000 [M, B73], so on this stretch a correct hold corrects nothing.
- **Not on the recording's build as the brief has it:** d20_play1's is 1fbd2bd, which has no hold. The tock tracks it over this stretch, which is the property the ask needs.
- **Why the PSP:** it is a time question, and the Memory Stick read is one the emulator doesn't price.

The path for this run is made in the emulator first (§5), so the job is the pair alone.

## 7. Question 2: does correcting break things

| Thing | Read | Risk |
|---|---|---|
| **The view set** | control state; the unit turns toward it as for any look input [D] | none |
| **Steer** | the game's own movement from a bent stick [D] | none to the game. It can lag or stall against an obstacle; the 45-tick rule snaps |
| **A snap on foot** | `object_set_position`: map links and the pose are redone; velocity is set by the hold; the biped's ground state is recomputed by its next physics step [D for the first two, U for the third] | a snap into another biped or a moved crate gets pushed out next tick. Each is logged with its size; a stretch with many is not held |
| **A snap seated** | the same on the root, with orientation and velocities | a spring kick if a *moving* thing sits where the path's vehicle sat [U]. Static ground is the same ground |
| **Trigger volumes and scripts** | containment, not crossing [D]: a snap that lands inside a volume fires it at that tick; nothing is skipped by jumping over an edge | none new |
| **Script gates on the fight** ("until this encounter is dead") | not held: a door or a cinematic may open later or sooner than on the recording | **the main limit.** The path then walks at a shut door: steer stalls, the snap carries the player through, and the world is behind him. The world marks show it (`living 14 against 11`) and the stretch is marked. The outcome hold (slice 7) is the fix if the watcher shows it happens |
| **Checkpoints** | a save waits for `game_safe_to_save` (no danger near), so its tick follows the fight [D] | a save's hitch can land in a different stretch on each side. The log already names every save; the stretch reader should report saves per stretch |
| **AI that targets the player** | a snap under 2 units is less than a second's run; the AI's tracking sees ordinary motion [U] | a large snap leaves stale last-seen points for a moment. Counted, not prevented |
| **Co-op, multiplayer** | the hold is a bench tool for player 0 in the campaign | none: not in a default build's play |

## 8. Question 7: the pad format

**Two files.** `<name>.pad` stays Sheepdog01's recording, never rewritten. `<name>.path` is derived and can be remade.

- **Why not one:** an old build reading a pad with records it doesn't know would stop at the first; the path is made after the recording, so one file means rewriting the original; and the path's format will change as the hold is tuned, which should never touch a recording.
- **The path's header:** a format number, the pad's name and the checksum of the pad's records (so a path can't be played against another pad), the recording build, the build and run that made it, the tick count, and the pad's difficulty and init copied from the pad's own header (the builder's in-flight work), so a replay can refuse a mismatch.
- **New recordings write both at once:** the recorder writes the path as Sheepdog01 plays, where it is exact by definition. From then on a recording never needs a path-making run.
- **`padfromlog.py`:** takes a third argument. `pathrec` logs its bytes as `path: data <hex>` lines beside the pad's `pad: data` lines (results take no binary files); the tool writes each stream to its file and prints the path's header. A second small tool, `pathcheck.py <recording log> <path>`, is §5's proof.
- **On the rig:** `sierra-hw pad=<name>` already copies the pad to `ms0:/halo/pad.pad`; with `padhold` it copies the path to `ms0:/halo/pad.path` and refuses the run if there is none. The lab's pads README gains a column: path made on, proven against, held to tick.

## 9. Question 6 and the build plan: slices, each with its proof

All proofs are emulator runs unless marked. Keep words: `pathrec`, `padwatch`, `padhold`.

| # | Slice | Done when |
|---|---|---|
| 1 | **The path writer and its tools.** `pathrec` in `psp_pad.c`; `padfromlog.py`'s third argument; `pathcheck.py`. | A replay with and without `pathrec` is identical (play and both seeds, statecrc). d20_play1's path passes `pathcheck` against Sheepdog01's recording log. |
| 2 | **The watcher: measure, correct nothing.** `padwatch` reads the path and prints §4's lines and the census; `fixedseed` takes a number. | On the path's own build and seed: error 0.00 at every tick. Then the figures the design waits on, from one batch: the error's growth and the census spread (a) across four seeds on one build, (b) with `vfpudotoff` (different maths, the same code), (c) on the posing box's new-only build. **The lead reads these before slice 3:** they size the bounds and say whether fights part further between builds than between seeds. |
| 3 | **The view set, steer and snap on foot.** | A test word that kicks the player 0.3 and 3 units at set ticks: steer recovers the first, a snap the second, each logged. On (a), (b), (c) above: held to the pad's end on a foot level (d20), correction counts reported. On the path's own build: no corrections, identical play. |
| 4 | **Seats and vehicles.** | b40_play1 (the Warthog) and c40_play1 (the Banshee) held to their ends on (a) and (c); every forced or fetched seat logged. |
| 5 | **The price [PSP].** §6's pair. Lab side: `lab.sh`'s `pad=` preflight wants the path; `abreport.py` prints the hold and census lines; seeds per repeat. | The pair passes §6. |
| 6 | **Paths for every pad.** One batch per recording build (§5), `pathcheck` on each. | The pads README's new column is filled; the pre-rebase pads' verdicts are known. |
| 7 | **Only if slices 2-4 call for it:** the outcome hold (per squad, the recording's living count kept to schedule) and the weapon grant. | Decided from the census, with its own page section. |

**What proves the whole thing works** is slice 3-4's test on a deliberately non-identical build: held inside tolerance to the pad's end, corrections counted, and each stretch's census inside the seeds' spread. The first real use is then a box's in-frame pair read by §2.3.

**Size:** slices 1-2 are small (one file, two tools, one batch) and decide the rest. Slices 3-4 are the work. Nothing here needs Sheepdog01's hands or a new recording.

## 10. Open, and who can close it

- The steer and snap bounds, and whether fights part further across builds than across seeds: slice 2's batch. [U until then]
- Whether a seated snap kicks the springs in practice: slice 4.
- Whether the pre-rebase pads track in the emulator on their own builds: slice 6's first batch.
- Whether replaying the recording's time steps is still the right bench once builds run faster than the recording did: not this page's question, raised for the lead (§1).
