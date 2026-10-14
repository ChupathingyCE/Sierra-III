# What the original engine already skips: activation, AI, posing, physics, lights (2026-10-13)

Code read: ChupathingyCE at the port's pin (paths below are relative to `source/`), and Sierra-III's `psp_fixups.py` and `docs/`. Nothing was built or run.

Labels: **[D]** read in code, **[U]** inference, **[M]/[E]** figures quoted from the port's docs. "Exact" means the same game state tick for tick. "Faithful" means it follows the Xbox game's own rules. **GAMEPLAY-VISIBLE** marks changes that alter behaviour.

---

## 1. Object activation and deactivation

### The rule: the players' combined PVS [D]
- `objects_update` (`objects/objects.c:4434`) copies `players_get_combined_pvs()` into `active_cluster_bits` every tick (`:4456-4457`). When the bits change (`csmemcmp`, `:4459`), it walks every object header:
  - It acts on an object only if `_object_header_automatically_deactivate_bit` and `_object_header_connected_to_map_bit` are both set (`:4464-4466`).
  - An active object whose **header cluster** (one cluster: the object's centre) is not in the PVS is deactivated. If it has `_object_deleted_when_deactivated_bit` (projectiles: `items/projectiles.c:610`; garbage: `items/garbage.c:54`), it is deleted instead (`:4468-4481`).
  - An inactive, unparented object whose cluster is in the PVS is activated (`:4483-4490`).
- **The combined PVS** (`game/players.c:1795-1850`): the OR of `structure_bsp_get_cluster_pvs(cluster)` for every player's cluster (the ultimate parent's cluster, so a Pelican's), plus one extra "activating cluster". Scripts set that cluster through `object_pvs_set_object` or `object_pvs_set_camera_point` (cutscenes, `objects.c:507-575`).
  - It's the map's baked PVS. There is **no distance term and no frustum**.
  - On open terrain it covers most of the outdoor clusters. `engine_performance.md` already found that on Blood Gulch every object is active [D/M]. b30's beach is probably the same [U].
- **Which objects take part:**
  - `object_new` sets the auto-deactivate bit on every object (`objects.c:3484-3485`).
  - AI-controlled units clear it (`ai/actors.c:3693`, `:3839`) and follow their actor's dormancy instead (§2).
  - Detaching an actor sets it again (`actors.c:765`, `:795`), and so does a recorded animation (`cutscene/recorded_animations.c:367`).
  - `object_disconnect_from_map` deactivates an object (`objects.c:1263-1266`). Reconnecting activates it, or deletes a deleted-when-deactivated object outside the PVS (`:1888-1901`).
  - `object_activate` refuses children and `_object_cannot_be_activated_bit` (`:1161-1163`). Nothing in the source sets that bit.
- **What an inactive object skips:** its whole `object_update` (`:4498-4520`: only `active && !being_created` objects update). That includes the type update, physics, node matrices, functions, colours, light reconnect and children.
  - It stays linked into the cluster partitions, so it still collides and is still drawn (`engine_performance.md` §2.2 cites `render_objects.c:571-574`).
  - It is the Xbox's own "sleep". It saves nothing for objects inside the PVS.
- **Scenery, props, weapons and garbage get no rule of their own.** They follow the same PVS test: every active one runs a full `object_update` each tick, seen or not. There is no visibility test, distance test or rate reduction for them.

### A built-in half-rate mode (precedent only) [D]
- `objects.c:4446`: `dont_update_object = ((game_time_get()&1)!=0) && game_players_are_double_speed();`. On odd ticks, every object except player-driven units skips `object_update` (`:4510-4515`).
- It belongs to the double-speed powerup (`game/players.c:4166-4169`). The game then steps effects at half the time step (`game/game.c:347-349`, `:927-930`).
- So the engine already tolerates alternate-tick updates of non-player objects, but only with the clock rescaled. It's a precedent for B3 and B4, not a faithful lever. **GAMEPLAY-VISIBLE** if borrowed.

### Garbage collection [D]
- **Trigger:** 50 active garbage objects (`GARBAGE_LIMIT_ACTIVE_GARBAGE_TRIGGER`). The collector then deletes down to 30 (`objects/objects.h:40-42`; `objects.c:4127`, `:4185-4188`).
  - A run "for space" starts when memory or object slots are low (`:4110-4123`).
  - The limits scale by `MAX(16, players)/16`, which keeps the Xbox's numbers in campaign (`:4079-4085`).
- **What it deletes:** only objects that `object_visible_to_any_player` rejects (`:4211-4214`). That test is "in the PVS, and inside a 45° cone around the player's aim, or inside the bounding sphere" (`:1688-1750`).
- **What the port already does:** sleeping corpses and wrecks still add to `active_garbage_object_count` (`psp_fixups.py` `psp_sleepy_update`, ~line 3020), so collection starts when it did on the Xbox. Faithful.

### What the port already uses
- The port's byte-identical sleep (`psp_fixups.py:2701-2870`, `psp_object_update`) runs **inside** the active set. It covers:
  - scenery (default);
  - parked vehicles at rest (`vehiclesawake`);
  - machines (`machinesleep`, off);
  - garbage corpses and placed-dead bodies.
- **It skips everything else.** The last line, `psp_sleepy_update(..., header->type == _object_type_scenery)` (`:2870`), means **weapons, equipment, device controls and light fixtures never sleep**. See opportunity 1.

---

## 2. AI: what already skips work

### Encounter activation, once a second [D]
- `encounters_update` (`ai/encounters.c:2300`) calls `encounters_test_activation` when `time % TICKS_PER_SECOND == 0`.
- **An encounter is active when** (`:4106-4140`):
  - one of its actors' clusters, or a cluster its active actors' firing positions or targets add (`encounter_compute_activation_cluster_bit_vector`, `:639-800`), is in the combined PVS;
  - or it is `force_active` (the script `ai_force_active`);
  - or it is respawning.
- **Once inactive, it stays active for a while:** `remain_active_timer = ENCOUNTER_REMAIN_ACTIVE_TIME` (150 ticks, `:334`), counted down 30 a second, so about 5 s. Linked encounters keep each other alive (`:4144-4160`).
- **An inactive encounter's actors are skipped entirely.** `actors_update` iterates with `active_only=TRUE` (`actors.c:4255`; the iterator at `encounters.c:1221-1260` skips inactive encounters and inactive actors).
  - `actor_set_active(FALSE)` deletes the actor's props, makes it dormant and deactivates its unit objects (`actors.c:3554-3605`).
- **Encounter-less actors** use the same test per actor, with a 90-tick linger (`encounters.c:1136-1147`, `:4030-4100`).

### Dormancy, inside an active encounter [D]
- **The wish:** `dormant_desire = !PVS(actor's cluster)`, set once a second (`encounters.c:4083`, `:776-782`). A squad's `disable_dormant` (the script `ai_allow_dormant`, `ai/ai_script.c:1385`) and `force_active` override it.
- **The switch:** `actor_general_update` (`actors.c:3980-4036`) wakes the actor at once. It makes it dormant only after **60 ticks** of wishing, and never while:
  - its action is transitory;
  - it has an acknowledged living player enemy;
  - it is pathing to a player, or guarding with a follow-players order.
- **What dormancy does:** `actor_set_dormant` deactivates the actor's unit or swarm units (`actors.c:2980-3047`), so their `object_update` stops: no posing, no biped physics.
- **The cost per state, per actor per tick** (`actor_update`, `actors.c:4202-4238`):

| State | Runs | Skips |
|---|---|---|
| Inactive encounter or inactive actor | nothing | everything; its units are deactivated |
| `mode <= braindead` (`ai_braindead`) | nothing (`actors.c:4262`) | everything |
| Active, dormant | general update, timeslice counter, input, perception (prop bookkeeping only; `prop->timer` reset, no rays, `actor_perception.c:6406-6409`), situation, emotion, `clear_orders`, decision loop, action update | danger zone, the object scan, prop service, all control (action control, communication, conversation, destination/path, move, look, combat, `unit_control`); the unit's whole object update |
| Active, not dormant | everything | n/a |

### Time-slicing Bungie already does [D]
1. **One "timeslice" a tick, across all actors** (`actors.c:1383-1420`).
   - Each actor's `service_timer` counts up (×3 while melee-charging). The first actor over last tick's highest and over 15 gets the slice; `time_given_this_frame` allows one a tick.
   - The slice gates the costly per-actor work:
     - the object scan `actor_perception_refresh` (`actor_perception.c:6221-6224`);
     - path refresh (`actor_moving.c:2108-2113`, `:2285`, …);
     - position searches in guard, flee, search, uncover, alert, charge, avoid, wait and converse (the `meta.timeslice` sites).
   - With 22 actors, each gets a slice about every 22 ticks [U].
   - A new move order still refreshes its path at once (`actor_moving.c:2283`).
2. **One prop serviced per actor per tick** (`actor_perception.c:6410-6438`).
   - Each prop's timer counts up. Non-enemies are shifted `>>3`, props at `quantized_distance >= 3` (10 wu or more, `:3971-3989`) `>>1`. The first prop at or above last tick's highest gets `prop_position_refresh` + `prop_status_refresh`, with one `ai_test_line_of_sight`.
   - An actor that knows an enemy pays one ray every tick. An actor that knows only friends pays one every 8–16 ticks.
   - **B1 in the docs is exactly the change to this rule.**
3. **Props "in use"** (the target, the interesting orphan, the retreat prop, look targets) also get `prop_position_refresh` every tick, with no ray (`:6455-6484`). That's head and body position, `scenario_location_underwater`, child-unit counts and the sense position.
4. **The ray's own early-out:** `ai_test_line_of_sight` returns "obstructed" without a ray when the two clusters fail `scenario_test_pvs` (`ai/ai.c:2321-2322`).
   - `ai_debug.fast_los` drops object occluders from the test (`:2331-2337`). **GAMEPLAY-VISIBLE**: the AI would see through vehicles and crates.
5. **The fire target's ray** runs every 10 ticks of `current_fire_target_timer` for manual-point targets (`ai/actor_combat.c:1873`). Prop targets reuse `prop->line_of_sight`.
6. **Encounters** are each updated once every `ENCOUNTER_UPDATE_INTERVAL` = 15 ticks, by phase (`encounters.c:335`, `:2316-2331`). Dirty status and activation run once a second.
7. **Swarms** (Flood infection forms): one actor drives up to `MAXIMUM_NUMBER_OF_UNITS_PER_SWARM` units through `actor_type_swarm_control` (`ai/actor_types.c:223`). That's Bungie's AI level of detail for crowds. b30 has none.
8. **Units, not AI:** `unit_refresh_illumination` round-robins one unit a tick (plus players) through `unit.timer` and `unit_globals->used_time` (`units/units.c:5376-5397`, `:6020`). The port's checksum already leaves this timer out for vehicles and bipeds.

### What doesn't exist [D]
There is no distance-based thinking rate, no `should_think`, no AI LOD global and no per-actor update period beyond the above. `ai_profile.disabled` and `ai_debug.force_all_active` are debug switches.

---

## 3. Posing and animation

- **No render-driven or distance-based posing** [D].
  - `object_update` always calls `object_compute_node_matrices` (`objects.c:3855-3858`) unless `_object_do_not_recompute_node_matrices_bit` is set. Only a limp-body ragdoll sets it (`units/bipeds.c:711`, cleared `:729`).
  - `object_compute_node_matrices` (`:2456`) decodes the animation frame, overlays, the type's preprocess (the aim blend) and every node. It never tests visibility or distance.
  - Models choose a level of detail only in drawing.
- **Bungie's render-gated simulation exists for cosmetic widgets only** [D]:
  - flags simulate only while drawn in the last 5 updates and catch up when drawn (`objects/widgets/flags.c:276-280`, `:302-308`);
  - antennas do the same (`objects/widgets/antenna.c:281-310`).
  - This is the precedent for B4, but node matrices feed play: markers for projectiles and weapons, AI head positions, hit tests. **Posing skips stay GAMEPLAY-VISIBLE (B4).**
- **Exact pose reuse** only applies where the inputs don't change: no animation advancing, same transform, no overlays or functions moving. In practice that means resting objects, which the sleep rules cover (§1, opportunity 1). Held weapons follow their parent's nodes every tick.
- **Markers and attachments:** looked up by name every tick (lights, footsteps, `item_update`'s "ground point"). The port caches them (b109, `psp_fixups.py:9920`); N3 covers the rest.

---

## 4. Physics and movement

- **The at-rest bit** (`_object_at_rest_bit`, `objects/objects.h:73`) [D]:
  - **Vehicles:** physics skipped while at rest (`units/vehicles.c:2639`). The port's vehicle sleep goes further (b44/b89).
  - **Items:** the drop and fall raycast is skipped at rest (`items/items.c:576`), but `item_update`, node matrices, functions and light reconnect still run every tick (§5).
    - At rest, the item also reads external state:
      - a breakable surface it rests on (`items.c:768-795`: wakes if the surface broke);
      - `bsp_index` against the current BSP;
      - an object it rests on (`_item_on_object_bit`: realigned every tick to that object's node 0, `:798-826`).
  - **Living bipeds:** `_object_at_rest_bit` is computed (`bipeds.c:4128-4133`) but never used to skip the pill sweep. Only the dead skip it (`bipeds.c:3522-3527`: dead + at rest + `ignore_translation` returns early from `biped_update_moving`). E8 in the docs is the faithful-adjacent idea.
- **Dead bodies** [D]:
  - The limp-noodle relax runs at most `biped_limp_noodle_get_max_relaxation_iterations` = **20** iterations (`units/biped_limp_noodle.c:103-107`; started at `bipeds.c:694-713`, applied `:1933-1942`).
  - After that, `biped_update_dead` only sets the dying state.
  - `idle_ticks` counts while dead and at rest (`bipeds.c:4400-4407`). The port's corpse sleep keys on it (more than 60).
- **Machines:** an automatic door looks for someone only every 4th tick (`devices/device_machines.c:221-223`). E4 uses this.

---

## 5. Object lights: what `object_connect_lights` actually is

- **Every tick, for every active object** with `_object_dynamic_lighting_recompute_bit`, unless invisible with a model (`objects.c:3863-3872`).
  - That bit is set at creation for every unit (`units/units.c:2605-2607`), item (`items/items.c:153`) and projectile (`projectiles.c:540`), and **never cleared** anywhere in the source [D].
  - So it isn't "on move": it's every tick.
- **It computes no lighting** (`objects.c:4569-4598`). For objects with `_object_has_attached_lights_bit`, it disconnects and reconnects each attached **point light** in the light cluster partition (`objects/object_lights.c:1427-1445`, `:1855-1918`). For each light, that's:
  - a marker lookup by name;
  - a transform;
  - `light_compute_bounding_sphere` with `maximum=TRUE`: the radius is the definition's **constant** maximum (`:1656-1658`);
  - `cluster_partition_reconnect` → `structure_clusters_in_sphere`, the portal flood (`structures/cluster_partitions.c:324-372`). That flood is N4's hot `sphere_intersects_cluster_portal`.
- **Objects without lights pay a flag test only.** So X8's 8% (17 µs × 84 [E]) is the few objects with lights: plasma bolts, Covenant weapons, any light-bearing equipment [U, which tags carry lights needs a look at the data].
- **Two docs need correcting:**
  - X8 ("cache lighting on unchanged inputs", `optimization_ranking.md:171`) is aimed at the wrong thing.
  - `campaign_recon_b30.md:40` says "every moving biped and its weapon". It is every unit, item and projectile, moving or not, and the cost is only where lights are attached.

---

## 6. Tunables, debug switches, Xbox-specific paths

| What | Where | Effect | Faithful? |
|---|---|---|---|
| `ai_profile_update` every tick | `ai/ai.c:1953`, `ai/ai_profile.c:252-282` | 28 meters with 60-tick histories; `ai_profile_sample_units` and `ai_profile_sample_swarm_actors` each walk **every actor of every encounter, inactive ones included** (`:366-406`). Read only by `ai_profile_render` (`render/render_debug.c:525`) | Exact to skip unless the AI profile display is on (opportunity 2) |
| `game_state_check_data_arrays` + `lights_port_recover`, **twice a tick** | `game/game.c:329-330`, `:374-375`; `saved games/game_state.c:904-935`; `objects/object_lights.c:743-757` | ChupathingyCE's diagnostics (not in the Xbox game): about 80 data-array headers checked each call | Exact to run once a tick or less (opportunity 4) |
| `ai_debug.fast_los` | `ai/ai.c:2331` | Rays test the level only | **GAMEPLAY-VISIBLE** |
| `ai_debug.force_all_active`, `ai_profile.disabled`, `ai_profile.move_actors_randomly` | `encounters.c:4089`, `ai.c:1945-1960` | Debug | Not levers |
| `debug_biped_skip_update`, `debug_biped_skip_collision`, `debug_biped_limp_body_disable`, `debug_collision_skip_objects`, `debug_collision_skip_vectors` | `units/bipeds.c:531-533`, `physics/collisions.c:197-198` | Debug | **GAMEPLAY-VISIBLE** |
| Scripts: `ai_allow_dormant`, `ai_force_active`, `ai_braindead`, `object_pvs_activate`, `object_pvs_set_camera_point` | `ai/ai_script.c:1385`, `:1599`; `objects.c:507-575` | The level's own dormancy and activation choices | Data, not ours to change. Worth reading which b30 encounters force themselves active |
| Double-speed half-rate objects | `objects.c:4446` | See §1 | Precedent only |
| `local_player_count() > 2` makes material effects always visible | `effects/material_effects.c:64-67` | Split screen | Not relevant |
| Tag fields such as `update_frequency` or `dont_update` | none | Searched the object, unit, item, device, AI and model headers: **none exist** [D] | n/a |

**A side lead for #25 (render feeding play) [U].** Tick code reads per-frame state:
- `material_effect_visible` (camera within 10 wu, `material_effects.c:58-85`) gates biped footsteps (`bipeds.c:1982`), item impact effects (`items.c:622`) and particle effects (`effects/particles.c:689`).
- The camera comes from `observer_update`, which runs once a **frame** (`main/main.c:3433`), not once a tick.
- So whether an effect is made depends on the frame rate. Effects draw the local seed, and effects can make objects.

---

## 7. Ranked: the most promising exact or faithful opportunities not in the lever docs

1. **Items sleep: weapons and equipment at rest on the level** (exact, by the existing byte-identical rule).
   - **Rule:** in `psp_object_update`, `may_sleep` for unparented items with `_object_at_rest_bit` and `_item_on_structure_bit`, no children, and the current BSP.
   - **Exclusions:**
     - `_item_on_object_bit`: it follows its support every tick;
     - a rested surface that is breakable: the break isn't in the item's bytes;
     - `item.bsp_index != global_structure_bsp_index_get()`.
     - A BSP switch should wake all sleepers, as for corpses.
   - The existing sleeper path already counts garbage (resting items are garbage, `items.c:672-675`).
   - **Target:** "of which weapons" is **1.5–4.7 ms a tick on the PSP** (`hardware_b114.md:92` [M]). That's top-level weapons only (`psp_fixups.py:3345` times per top-level object), so ground weapons, not held ones. It also takes their light reconnects off X8.
   - The same may extend to device controls and light fixtures (they never sleep today), smaller.
   - Keep word e.g. `itemsawake`.
2. **Skip `ai_profile_update`'s sampling unless the profile display is on** (exact: the meters are read only by the debug render).
   - **Size:** two walks over every actor on the map, plus about 28 history updates, each tick. About 0.05–0.2 ms a tick on b30 [U, scales with actors placed map-wide].
   - **Measure first:** log the meters once a second (actors updated against active, units active, line of sight). That tells how many of b30's actors are dormant or inactive by Bungie's own rules.
3. **Measure activation before designing more.**
   - The heartbeat could carry `objects_information_get` (`objects.c:480`: objects against active), the AI meters above, and which encounters are `force_active` or `disable_dormant`.
   - If b30's PVS keeps everything active (likely [U]), no faithful activation lever exists. Every further cut (B1, B3, B4) is then a behaviour change for Sheepdog01 to decide.
4. **The light partition re-linked only when the light's sphere centre or start cluster changed** (exact in which clusters hold each light).
   - Keep the position and orientation update; skip the disconnect and `cluster_partition_reconnect`.
   - The radius is constant (§5), so the flood's answer depends only on centre and cluster.
   - **Check:** skipping changes the order of a cluster's light list, which re-linking reorders every tick today. Verify that nothing depends on that order: the light pass, and the object point-light search (off by default since b106).
   - Gains mostly overlap opportunity 1 and sleeping vehicles; moving lights (plasma bolts, held weapons) still re-link.
   - Also correct X8's description (§5).
5. **ChupathingyCE's twice-a-tick diagnostics run once a tick** (or every N ticks).
   - `game_state_check_data_arrays` + `lights_port_recover` at `game.c:329-330` and `:374-375`.
   - Not Xbox behaviour; only the report's timing changes. Small: about 160 header reads a tick [U].

**Not new:** B1 (prop service rate), E4 (machine sleep), E8 (standing biped), N3/N4 and X9 already cover the rest of what the engine offers. Bungie's own levers (activation by PVS, dormancy, one timeslice a tick, one prop a tick, encounters every 15 ticks, illumination round robin, the 20-iteration limp cap) are all already active in the port, and none can be pushed further without changing behaviour.
