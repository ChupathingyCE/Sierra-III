# Review section for build 122: far machines and resting poses at half rate

For `docs/review_b122.md`. Same shape as `review_b122_posing.md`: **what**, **key lines**, **why**, **what a player could notice**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `farrate122` (from `w122`, `8312219`).

| Change | Kind | Default |
|---|---|---|
| 1. `machinesfar`: far, unseen always-moving machines updated every other tick | behaviour (`optimization_ranking.md` B3): Sheepdog01's call after an A/B | off (opt-in) |
| 2. `posehalf`: far, unseen objects at rest posed every other tick | behaviour (B4): Sheepdog01's call after an A/B | off (opt-in) |

Both are campaign only and change what the game simulates, so neither is exact.

---

## Who counts as "far and unseen"

`psp_far_unseen` (`objects.c`). One test for both words:

- **Far.** The object's bounding sphere is more than 6 world units (about 18 m) from the player's eye.
- **Unseen.** The object is outside a cone of about 70 degrees each side of the player's aim (`unit.aiming_vector`), with 4 world units of slack for a vehicle's camera that sits behind the player. Halo's widest view is narrower than that cone.
- **Judged from the player's unit**, not the camera. The unit is simulation state, so the answer doesn't follow the frame rate.

The test answers "no" (everything updates every tick) in all of these cases:

- a networked game. The checks are `game_connection() != _game_connection_local`, `game_engine_running()` (a multiplayer game engine) and `network_coop_active()` (a co-op client or host);
- a cutscene (`cinematic_in_progress()`): its camera can be anywhere;
- the player has no unit;
- there is more than one local player.

Half of the eligible objects go each tick, chosen by object index. Under the double-speed powerup objects update on even ticks only, so the half is taken by update count rather than by tick, as `sceneryhalf` does.

## 1. `machinesfar`

**What.** This applies to an always-moving machine (type 2: c20's gears and moving platforms, 60% of its machine updates [E, `review_b121_machines.md`]) that is far, unseen, has no parent, carries nothing, has no lights, and has no unit or item inside its bounding sphere. On its off tick its update is left out, and only the position step from `machine_update`'s first lines runs. Its relink, pose, function values, change colours and damage update catch up at its next update.

**Key lines.** `psp_fixups.py`, `FIXES_122`:

- **`devices/device_machines.c#farrate`, `psp_machine_step_alone`.** Runs `position += (1 − power)·depowered velocity + power·powered velocity`, then the wrap at 1, the `_device_position_changed_bit` flag and the group's `actual_value`. These are the same operations on the same values as `machine_update`, so the position and its group stay bit for bit where every-tick updates would put them [D]. It steps only while `device_update` would leave the machine alone: power steady (velocity 0, equal to its power group's value) and its position group asking nothing new. Otherwise it returns 0 and changes nothing.
- **`objects/objects.c#farrate`, `psp_machine_far_skips`.** Checks the type and power first, then the far/unseen test, then `objects_in_sphere` for units and items. It is called in `psp_object_update`'s machine branch, before `psp_sleepy_update`.

**Why.** c20's 53 machines cost 6.5 ms a tick on the rig [M, lab 38472506973]. `machinesleep` can't help the type-2 machines, because they never stop. A gear's update costs about 30 µs in the cache models: 10.5 µs of node matrices, 7.6 µs of relink, 4.3 µs of functions, and the rest [E].

**What a player could notice.** Only if they could see the machine, and the test makes sure they can't. The worst case is a one-tick lag in something that touches a far machine during its off tick:

- a projectile or ray that hits a far gear meets its pose from one tick before;
- an AI riding a far platform would see the same lag, but the sphere test keeps any machine with a unit or item on it at every tick.

The machine's position never drifts, because it is stepped every tick.

## 2. `posehalf`

**What.** This applies to a family of objects (a root and everything attached to it, such as an AI and the gun it holds) whose root is at rest (`_object_at_rest_bit`), far and unseen, and isn't a machine (those are part 1's). Nothing in the family may be interpolating. On the family's off tick, `object_compute_node_matrices` and `object_postprocess_node_matrices` are left out for every member:

- the whole family takes the same ticks, because a child's pose is made from its parent's;
- the post-processing works on the pose in place, so it only runs on a pose that was just made;
- the animation, physics, functions and lights still run every tick, so the next pose made is exactly the one it would have had [D].

The first version (`c4d0458`) left out anything with a parent or children. That excluded every armed AI and every held weapon, and it skipped 0 of 30,000 chances on c20 (lab 38582477085). `1dd6302` decides by the family's root.

**Key lines.** `objects/objects.c#farrate`:
- `psp_pose_half_skips` walks up to the root and is decided once at the top of `object_update` (`psp_pose_skipped`);
- the node-matrix call and the post-processing call are each gated on it.

**Why.** Posing is 17.5% of c20's tick, 10% of b30's and 5.8% of a50's [E, `review_b122_posing.md`]. What rests and stays awake (an idle AI, a corpse not yet asleep, a parked vehicle someone touched) is still posed every tick.

**What a player could notice.** Nothing on screen. A far object behind the player keeps its last pose for one extra tick, and it is at rest, so the pose barely changes. Game logic that reads its markers on the off tick sees the pose from one tick before, for example where a far, idle AI's gun muzzle is.

## Proven

**Builds** (local, `9aa2fb7`; `1dd6302` again for the campaign):
- campaign: 448 of 448 units, nothing stubbed;
- `PSP_MULTIPLAYER=1`: 490 of 490 units, linked. Its 10 stubbed symbols are the multiplayer build's usual ones (`delta_legacy_*`, `map_family_*` and the like), none of them from this change.

The batch below compiles the multiplayer build again.

**Lab batch `farrate122a`, run 38582477085** [E], `9aa2fb7`, both words on (posehalf's first rule):

- **No crash, and play as normal, with both words on.** c20, d20 and a50 hands off, and b30 on the `b113test1_timed` route, each 480 s of wall with `shotevery5`. They reached game time 9,922, 9,979, 13,389 and 11,880 with segv 0, no strict-mode sites and no `crash.txt`, 24 shots each. The `PSP_MULTIPLAYER=1` build compiled (EBOOT `ba3e5e4876893702`).
- **`machinesfar` acts on c20** (hands off): about 15 always-moving machines a tick are offered on their off tick. Of those, 28% were skipped in the first 30,000 chances and 53% in every later count. That is roughly 8 of c20's ~30 gear updates a tick left out. The other three maps have no always-moving machines in reach.
- **Cache models, c20 hands off to game time 40 s** (both words on against off, `fixedticks fixedseed`): the estimated tick went from 22.555 to 22.601 ms (+0.2%). Fetches fell 0.13%, but instruction misses rose 0.4%. In this opening window few gears qualify (the 28% above), so the test's own code costs about what it saves.
- **b30 on the route, same window:** 47.882 against 47.954 ms (+0.15%). b30 has no gears, so this is the cost of the test alone, while posehalf's first rule skipped nothing.
- **Screenshots** of the machines and distant objects look as they do without the words. They are on the results branch, not in this repo (they show game content).

**Lab batch `farrate122b`, run 38588163787** [E], `1dd6302` (posehalf by family): the cache-model pair again on b30's route and on c20 to game time 40 s, and the counts on a50 and d20. *Its figures go into the build notes when it reports.*

## Multiplayer

Neither word acts in a networked game (the guard above). The words are read in both builds, but in a multiplayer game `game_engine_running()` is true, so nothing changes what a client simulates against ChupathingyCE's servers.

## Default

Both off. Each needs Sheepdog01's call after an A/B, because it changes the simulation for things the player can't see. Turn them on with `machinesfar` and `posehalf`.

## For the rig

One `sierra-hw` run each, with keep sets (on, off, on, off), read with `tools/abreport.py`:

1. **c20**, where the gears are: `ref=farrate122 maps=/bake/campaign121/halo/maps map=levels\c20\c20 init="cheat_deathless_player 1" keep="nearauto assettrack geo2 hwprofile" keepsets="machinesfar posehalf | -" repeat=2 wall=420`
2. **b30's opening**, the fair A/B stretch, for `posehalf`: the same with `map=levels\b30\b30 route=b30_b113test1_timed keepsets="posehalf | -"`.
