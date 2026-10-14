# Review section for build 121: the machines' tick

For `docs/review_b121.md`. Same shape as `review_b120.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `machines121` (from `w121`, `5015ea3`).

| Change | Kind | Default |
|---|---|---|
| The relink memo: a reconnect's two lookups remembered | exact speed | on (`relinkfresh` off) |
| `machinetime`: a machine's update in its parts | recon instrument | off (opt-in) |
| `machinesleep` (b102) re-judged, and a sleeping machine's lights reconnected | exact speed, not yet proven whole | off (opt-in); on once `machines121m` holds |

---

## Where c20's machines spend their tick

On the rig, c20's 53 machines cost 6.5 ms a tick (lab 38472506973, a `PSP_TICK_STEPS` ladder, so the steps' own cost is in it) [M]. `machinetime` splits each machine's update; c20's opening in the cache models, hands off, profile window 1 (lab 38526124924) [E]:

| Part | ms a tick | Share | Per machine |
|---|---|---|---|
| Node matrices (`object_compute_node_matrices`) | 0.554 | 31% | 10.5 µs (53 a tick) |
| Outside `object_update` (the sleep wrapper, the b102 count, the timing) | 0.29 | 16% | 5.4 µs |
| The relink (`object_translate` in `machine_update`) | 0.244 | 14% | 7.6 µs (32 a tick: the gears) |
| Function values and change colours | 0.228 | 13% | 4.3 µs |
| Attached lights (`object_connect_lights`) | 0.175 | 10% | 17.5 µs (10 machines have lights) |
| The rest of the type update (`device_update`, `machine_update`) | 0.137 | 8% | 2.6 µs |
| Damage and exported functions | 0.097 | 6% | 1.8 µs |
| Children and post-processing | 0.040 | 2% | 0.8 µs |
| **All machines** | **1.76** | | **33 µs** |

- The cache models' 1.76 ms against the rig's 6.5 ms: about 3.7 to 1, as for the rest of the tick. Shares carry over; absolute times don't.
- c20's machine updates by type (b102's count): 60% gears (type 2: they turn every tick), 35% platforms (type 1), 5% doors [E].
- d20 is different: no gears, 70% doors; its machines cost 1.44 ms a tick in its busiest window, and 23% of that is the lights of the 20 machines that carry them [E].

So no single part dominates. The gears' node matrices are real work (their pose changes every tick). The relink and the lights repeat the same lookups every tick, which the memo (1) takes out, for little. The larger lever is the 40% that don't move: `machinesleep` (3) takes 12% off c20's machines and about half off d20's, 2.6–2.7% of each tick in the estimate.

## 1. The relink memo (`ed42ef0`)

**What.** Every tick, `machine_update` moves each gear to where it already is (`object_translate` with its own position). That disconnects it from the level's cluster lists and reconnects it, and a reconnect makes two lookups:

- which leaf and cluster its bounding sphere's centre is in (`scenario_location_from_point`, a walk down the level's BSP);
- which clusters the sphere touches (`structure_clusters_in_sphere`, a walk through the cluster portals).

A light on a machine is reconnected with it every tick (`object_connect_lights`) and makes the second lookup too, with a larger sphere.

Both lookups read only their inputs and the level's BSP. The memo remembers each answer by its inputs, bit for bit, and gives it back when the same inputs come again. The disconnect and reconnect still run in full, so the lists come out in the same order, with the same references, as before.

**Key lines.** `psp_fixups.py`, `FIXES_121`:
- `cluster_partitions.c#relinkmemo`:
  - `psp_relink_clusters_in_sphere`: 256 slots picked by a hash of the inputs; a slot answers only when the cluster, the sphere's four floats (as bits) and the generation all match;
  - `psp_relink_location_from_point`: the leaf and cluster, likewise. It writes the two fields `scenario_location_from_point` writes and nothing else;
  - the generation: `psp_relink_generation++` in `cluster_partition_port_forget` (the lists are made anew: a new map, a loaded game) and in `psp_relink_start` when the level's BSP (pointer or index) is another, so nothing remembered from before answers.
- `objects.c#relinkmemo`: `object_reconnect_to_map` asks `psp_relink_location_from_point` for the bounding sphere's centre.

**Why.** The relink is 14% of c20's machine time and the lights 10%; both are mostly these two lookups, repeated with the same inputs.

**Measured** [E, cache models, hands off to game time 60 s, lab 38526124924; "est." is `review_b121_aiperc.md`'s PSP-1000 estimate: one cycle an instruction plus 72 a miss]:

| | c20 | d20 |
|---|---|---|
| Lookups a tick remembered / worked out | 363 / 586 | 21 / 17 (busiest window) |
| Machines, profile window (memo / `relinkfresh`) | 1.76 / 1.84 ms | 1.44 / 1.65 ms |
| of which the relink | 0.244 / 0.293 ms | (no gears) |
| of which attached lights | 0.175 / 0.200 ms | 0.329 / 0.577 ms |
| Tick phase: instructions, data misses, instruction misses | −0.8%, −0.8%, +0.6% | −0.9%, −0.7%, +0.8% |
| Tick, est. | 23.17 / 23.17 ms (−0.03%) | 28.68 / 28.76 ms (−0.28%) |

The machines get cheaper by 4% (c20) and 13% (d20), but on c20 the whole tick barely moves: the memo's own code, run for every reconnect in the level (~950 lookups a tick on c20, most of them bipeds and projectiles that moved), costs about as many instruction misses as the lookups it saves. A smaller gain than the machines' own figures promised; it is exact and doesn't make any tick slower in the estimate.

**Proven.**
- **Play exact.** Hands off, on against `relinkfresh` (lab 38526377504): the play hash and both random seeds are identical at every tick on c20 (19,438 ticks), d20 (16,531, across its BSP switch at tick 1,122) and a50 (26,431) [E]. The bar `itemsawake` and b117–b119's exact levers were held to.
- **Every remembered answer checked.** `relinkcheck` on c20: 0 of 1,282,075 remembered lookups differed from a fresh one [E] (same run).
- **The whole-state hash** differs only in allocations that follow the emulator's speed: `objects` (the render mark), `contrail point` and `actor` (as `review_b121_aiperc.md` found) on d20 and a50; on c20 `objects` and, at one checkpoint (tick 13,260), `lights`. `lights` is speed too: c20 against itself with `relinkcheck` (the same answers, only slower) differs in the same two. Each run's last checkpoint is left out: the wall ends the runs mid-line, so one log holds only part of it.
- **The BSP switch** (`eb527ef`). A switch reconnects every object to the new BSP without making the cluster lists anew, so the first build's generation didn't change across it, and a lookup remembered in the old BSP could have answered in the new one. d20's play stayed identical across its switch, but nothing ruled that out, so the memo now notes the BSP (pointer and index) and starts a new generation when it changes. That can only turn a remembered answer into a fresh one [D]. Its own runs (d20's pair and `relinkcheck` across the switch, a50, the MP compile) are batch `machines121v`, lab 38531934682.

**Multiplayer.** The memo changes no value the game computes: it returns what the lookup would have returned. So nothing the simulation or the network sees changes. The `PSP_MULTIPLAYER=1` build compiles (lab 38526124924, build 2: EBOOT `be64e5449d3ac333`; its run fails at start, as every w121 MP run in the harness does, `mpcheck121`). The desktop servers don't run this code.

**Default.** On. Off word `relinkfresh`; `relinkcheck` does every remembered lookup again and counts any that differs (the `machine parts` line's `wrong`).

## 2. `machinetime` (`ed42ef0`)

**What.** With the keep word, each machine's update is timed in its parts, on a `machine parts` line after the object report's `objects_update by type` line: the type update (`device_update` and `machine_update`), damage and exported functions, node matrices, the relink alone (`object_translate` in `machine_update`), function values and change colours, attached lights (counted only for machines that have them), and children and the rest. The same line gives the memo's lookups a tick.

**Key lines.** `psp_fixups.py` `FIXES_121` `objects.c#machinetime` and `device_machines.c#machinetime`; `psp/src/psp_platform.c` `object_report`.

**Default.** Off; recon only.

## 3. `machinesleep` re-judged (`f6d2617`)

**What.** b102's rule, unchanged: a machine whose update leaves it byte-identical twice running sleeps, with its power and position groups in the checksum; gears never sleep, a door wakes on its look-around ticks, a machine carrying anything stays awake. New: asleep, a machine still reconnects its lights, as its update would.

**Key lines.** `psp_fixups.py`: the machine branch of `psp_object_update` (who may sleep, unchanged) and, at the end of `psp_sleepy_update`'s asleep branch, `object_connect_lights` for a sleeping machine.

**Why.** b102 shelved it on the emulator's clock without the cache models: "no gain". In the cache models it is the largest exact lever for machines found [E, lab 38526541668 against 38526124924, hands off to game time 60 s, `machinetime`]:

| | c20 | d20 |
|---|---|---|
| Machines asleep a tick | 11 of 53 | 17 of 29 (busiest window) |
| Machines, profile window (default / `machinesleep`) | 1.76 / 1.55 ms | 1.44 / 0.69 ms |
| Tick, est. | 23.17 / 22.56 ms (−2.6%) | 28.68 / 27.91 ms (−2.7%) |

**Proven so far.** `ed42ef0`'s `machinesleep` against the default, hands off (lab 38526377504): the play hash and both seeds are identical at every tick on c20 (19,438), d20 (16,531) and a50 (26,431) [E]. But on d20 the whole state also differed in the light lists (`lights`, `light cluster reference`, `cluster light reference`, `light`) and `cached object render states`, from tick 1,140 on. An awake machine reconnects its lights every tick, which puts them first in their clusters' lists, and a sleeping one's stayed where they were. The lists decide which lights light an object, so that could be seen. `f6d2617` reconnects a sleeping machine's lights as its update would. Its runs: batch `machines121m`, lab 38533467924 (the state pair on c20, d20 and a50, and the cache models on c20 and d20), queued on the emu runner when this was written.

**Multiplayer.** Machines are in multiplayer maps; sleeping changes nothing the simulation reads only if the whole state stays the same, which is what `machines121m` checks. Until it does, the word stays off in both builds.

**Default.** Off (`machinesleep`). Turn it on when `machines121m` gives the same whole state as the default apart from the allocations that follow the emulator's speed, and say so in the build notes.
