# Node-matrix readers: a census for the node-matrix pass

A code read, not a run: lab api-job 41033412317 (Sonnet, 2026-10-30, on w142), asked by the master for direction 1's node-matrix pass. Every row is labelled [D] (read in code) or [U]; costs and exactness come only from the emulator and PSP pairs. Line numbers are against w142 and the engine at the pin.

**What it means for the pass:** a plain pass after the walk is not exact (parent/child, seat, weapon-fire, IK and collision readers see last tick instead of this tick); a pass before the walk is not exact either. The working shape is the shelved `nwbatch` (a waiting bit per object, settled by its first reader), revived with its armed-biped failure fixed and ordered against FVP.

---


I read code only; nothing was built or run. Paths starting `engine/` are in `../engine/`. `psp_fixups.py` and `docs/` are in the Sierra-III checkout. "THIS" means this tick's matrices, because that object's compute already ran in the walk. "LAST" means last tick's.

## Findings

| # | Finding | Label |
|---|---|---|
| 1 | The walk goes in header-slot order over top-level objects. A parent's compute runs before its children's updates, so children and held weapons see THIS tick for their parent. | D |
| 2 | Matrices are 13 floats = **52 bytes**, not 48. | D |
| 3 | Pass **after** the walk (the FVP shape): every in-walk read of a parent, or of an earlier object, goes from THIS to LAST. These are the child's compute reading its parent, seat and owner markers, collision tests, IK grips and light connects. | D+U |
| 4 | Pass **before** the walk is not exact. The compute reads inputs that the same object's type update changes first. | D+U |
| 5 | Readers before `objects_update` (AI, scripts, effects, the ME's rays) see LAST either way. Readers after the walk and in the frame see THIS either way. | D |
| 6 | Collision tests read other objects' matrices through `collision_model_instance_new`, plus the bounding sphere the compute writes. Which version they get depends on walk order. | D |
| 7 | No kept copy of last tick's is needed, because the block itself holds LAST until recomputed. What readers need is an order rule: settle that object (and its parent chain) first. The port built exactly this as `nwbatch`, which is **shelved** (`psp_fixups.py:25675`, `:32421`). `vislazy` is the live version of the same idea. | D |
| 8 | The compute also reads `outgoing_function_values`, which FVP now makes after the walk. A matrices pass has to be ordered against FVP. I found no code that sequences the two. | D+U, CAN'T TELL |

## 1. How node matrices are built today

**Walk order [D]**
- `objects_update` loops over the object-header array in slot order. It calls `object_update` only for active objects that aren't being created: `engine/source/objects/objects.c:4509` `for (i = 0; i<object_header_data->count; ++object_header)`, `:4524` `object_update(object_index);`.
- Children are not in that loop. They are inactive and are reached from the parent's update: `:3881` `if (object->object.first_child_object_index!=NONE)`, `:3883` `object_update(object->object.first_child_object_index);`. Siblings are reached by `:3890` `object_update(object->object.next_object_index);`, but only when the object itself has a parent.
- Objects created during the walk are updated in a second loop at `:4538-4545` (`_object_header_being_created_bit`).
- On odd ticks at double speed only player units update (`:4452`, `:4520-4522`).

**Order inside one `object_update` [D]**
1. `:3853` `object_type_update(object_index);` (physics, animation advance, weapon fire).
2. `:3859` `object_type_export_function_values`.
3. `:3863` `object_compute_node_matrices(object_index);`
4. `:3866` `object_compute_function_values`, then `:3877` `object_connect_lights`.
5. `:3883` the children's updates.
6. `:3894` `object_postprocess_node_matrices(object_index);` (IK, in place).

**What the compute reads and writes [D]**
- It reads the parent's node (`:2507` `object_node_matrix = object_get_node_matrix(object->object.parent_object_index, object->object.parent_node_index);`), the object's own position, forward, up, scale and animation state, and its `node_orientations`.
- It writes the object's node block and the bounding sphere (`:2946` `matrix4x3_transform_point(object_nodes, &object_definition->object.bounding_offset, &object->object.bounding_sphere_center);`, `:2947` `…bounding_sphere_radius = …`).
- The port copies the sphere into `psp_object_spheres[]` in the same place (`psp_fixups.py:10483-10484`). Collision tests read that copy.

**Tick order [D]** (`engine/source/game/game.c`)
- Before the walk: `:337` `units_update();`, `:342` `ai_update();`, `:345` `players_update_before_game();`, `:350` `effects_update(`, `:356` `antennas_update(`, `:359` `first_person_weapons_update();`, `:361` `game_engine_update();`, `:367` `hs_update();`.
- The walk: `:369` `objects_update();`.
- After the walk: `:370` `players_update_after_game();`, `:371` `hud_update();`, `:372` `player_effect_update();`.
- Then `engine/source/game/game_time.c:476` `render_interpolation_tick();` snapshots every object (`engine/port/linux/game/render_interpolation.c:469`).
- Then the frame: `game.c:938-942` `particles_update`, `contrails_update`, `particle_systems_update`, `widgets_update`, `game_sound_update`.

**Computes outside the walk [D]**
- Writers, from the engine: `objects.c:3413` (`object_set_position`), `:3632` (`object_new`), `:3789` (attach), `:3813`, `:3907` (recursive), `:5028`.
- Also `projectiles.c:1067`, `:1418`, `:1426`; `bipeds.c:1310`; `units.c:3162`, `:3289`, `:4079`, `:4199`, `:4229`, `:4690`, `:3760`; `action_obey.c:2450`.
- The `units.c` sites are enter-seat, exit-seat and user-animation paths. I did not trace which of them run inside the walk.

**Where the port changes the picture [D]** (`psp_fixups.py`)
- `posehalf` and `machinesfar` skip a far or resting object's compute and postprocess (`:18943`, `:18955`, `:18981`). Its matrices then stay older than LAST.
- **VISLAZY is on by default** (`:29414`, `:29767`). It leaves out the compute of unseen scenery and its kin. An "owed" bit at the accessors makes the compute when something reads it (`:29542-29545`). Checkpoint saves settle all owed computes first (`:29458`), and the ME's rays fall back to the main CPU for an owed object (`docs/review_vislazy.md:22`).
- `nwbatch` and `nwbatchwrites` are filtered out unless `PSP_SHELF=1` (`:25675-25676`, `:32421`).
- `object_compute_node_matrices` first settles the ME's perception rays and bumps `psp_object_epoch` (`:18540-18556`, `:18561`).
- The pose job (poseahead, PSC, posefpu) feeds `node_orientations` into the compute (`:21107`, `:38759`, `:38613`). It doesn't read matrices.

## 2. Readers

I grepped `object_get_node_matrix(`, `object_get_node_matrices(`, `object_get_marker_by_name(`, `->object.node_matrices`, and the wrappers `object_get_origin`, `object_get_orientation`, `object_get_world_matrix` and `collision_model_instance_new`. I excluded `rasterizer/` from the first marker/accessor sweep and treated draw code as one group. The groups after B are by time, because they share a version.

### A. Inside the walk, on the object's own datum or on its parent or children

| Reader (file:line, quoted) | Function | Whose | When | Version today |
|---|---|---|---|---|
| `objects.c:2507` `object_get_node_matrix(object->object.parent_object_index, …)` | `object_compute_node_matrices` | parent | a child's compute runs in the parent's update, after the parent's compute (`:3863` before `:3883`) | **THIS** |
| `objects.c:2974` `object_type_postprocess_node_matrices(object_index, matrices);` | `object_postprocess_node_matrices` | own, in place | `:3894`, after the children's updates | THIS, then changed |
| `objects.c:2163-2164` `object_get_marker_by_name(hand_object_index, …)` / `(grip_object_index, …)`, called from `units.c:11172` and `:11204` | `object_inverse_kinematics` | own hand; grip is the parent vehicle or the held weapon | in the unit's postprocess | THIS for both. The vehicle's own postprocess runs after its children's. |
| `objects.c:3877` `object_connect_lights(object_index, TRUE, TRUE);` → `object_lights.c:1867`, `:1878` `object_get_node_matrix(light->object_index, …)` | `light_reconnect_to_map` | own | just after the own compute | THIS |
| `objects.c:2365` `node_matrix = object_get_node_matrix(object_index, 0);` | `object_export_function_values` (compass) | own | `:3859`, before the own compute | **LAST** |
| `units.c:7811` `object_get_marker_by_name(unit_index, "left hand", &marker, 1);` (from `:5846`) | `unit_throw_grenade_move_to_hand` | own | type update | LAST |
| `units.c:11453` `object_get_marker_by_name(unit->object.parent_object_index, seat->marker_name, …)` (from `:5944`) | `unit_seat_update` | parent | type update of a seated unit | **THIS** |
| `units.c:8745` `object_get_marker_by_name(unit_index, "melee", &marker, 1)==1` (from `:10215`, `:4532`) | `unit_cause_melee_damage` | own | type update | LAST |
| `units.c:8909` `object_get_marker_by_name(unit_index, "head", &marker, 1);` (from `bipeds.c:4387`) | `unit_cause_player_melee_damage` | own | type update | LAST |
| `units.c:7257`, `:7278`, `:7286` (own "head" and seat markers); `:7303` `object_get_marker_by_name(unit->object.parent_object_index, parent_seat->camera.marker_name, …)` | `unit_get_head_position`, `unit_get_camera_position` | own, or the parent's camera marker | see the callers in group C and below | own LAST; parent THIS in the walk |
| `weapons.c:2435` `object_get_marker_by_name(weapon_get_effect_object_index(weapon_index), …)` | `trigger_create_projectiles` | the weapon, or its parent when invisible (`:1931-1933`) | weapon type update | weapon LAST; parent THIS |
| `weapons.c:2485` `unit_adjust_projectile_ray(owner_object_index, …)` → `units.c:4894`, `:4925`; `weapons.c:2612` `unit_get_camera_position(owner_object_index, …)` | weapon fire | owner (the parent) | weapon update | **THIS** |
| `units.c:11375`, `:11421`, `:11585` `unit_get_camera_position(unit_index, …)` | `unit_throw_grenade_release`, `unit_drop_item` | own | type update | LAST |
| `units.c:11627` `collision_model_instance_new(&instance, unit->object.parent_object_index)` | `unit_cause_continuous_melee_damage` | parent vehicle | unit update | THIS |
| `bipeds.c:1990` `object_get_marker_by_name(biped_index, contact_point->marker_name, …)` | `biped_make_footstep` | own | biped update | LAST |
| `vehicles.c:1234`, `:1306`, `:1310` `object_get_marker_by_name(vehicle_index, "hover thrusters" / "jet thrusters", …)` | `create_ghost_effect`, `create_pelican_effect` | own | vehicle physics | LAST. [U] for the call site; I did not trace it. |
| `items.c:350`, `:401`, `:476`, `:762`, `:848` `object_get_marker_by_name(item_index, "ground point", …)` | `item_accelerate`, `item_align_to_normal_and_point`, `item_update` | own | item update | LAST |
| `device_machines.c:308` `object_get_node_matrix(machine_index, …elevator_node_index)` | machine update | own | machine update | LAST |
| `biped_limp_noodle.c:707` `&object->object.node_matrices`, `:784` | limp-noodle physics | own block, direct | biped update | [U] when it runs; I didn't trace it |

### B. Inside the walk, arbitrary other objects (EITHER, by walk order)

| Reader | Function | Whose | Version |
|---|---|---|---|
| `collision_models.c:58` `instance->matrices = object_get_node_matrices(object_index);` | `collision_model_instance_new` | any object a test sweeps | **EITHER**: THIS if that object's slot is earlier in the walk (or it is a parent whose update is running), LAST if later |
| Its callers: `collisions.c:825`, `:1638`, `:1721`, `:1866`; `physics.c:1304`; `bipeds.c:1193`, `:4069` (`biped->biped.impact_target_object_index`); `collision_debug.c:337` | collision tests: points, vectors, features in sphere, biped and vehicle physics, line of sight, melee | other objects | EITHER |
| Bounding sphere via `psp_object_spheres[]` (`psp_fixups.py:10645`, `:17996`, `:22829-22839`) and `objects.c:2946` | `psp_sphere_of`, `objects_in_sphere`, `damage.c`, `projectiles.c`, `items.c`, `devices` | other objects | EITHER |
| `items.c:695` `object_get_node_matrix(collision.object_index, 0);`, `:805` `…(item->item.item_on_rest_object_index, 0)` | `item_update` | the object the item rests on | EITHER |
| `projectiles.c:789` `unit_get_center_of_mass(projectile->projectile.target_object_index, …)` → `units.c:2911` | `projectile_update` | the homing target's "body" marker | EITHER |
| `physics.c:562-563` `object_get_origin(…)`, `object_get_orientation(…)` | `physics_instance_new` | the object, and its parent if it has one | the parent's THIS or LAST |

### C. Wrappers that read a parent's node only when the object has a parent

These are `object_get_origin` (`objects.c:2077-2085`), `object_get_orientation` (`:2097-2121`) and `object_get_world_matrix` (`:2139-2143`). Without a parent they read the datum and not a matrix.
- **In the walk:** `bipeds.c:660`, `:1001`, `:1474`, `:1530`, `:1735`, `:4154`; `units.c:3084`, `:3737`, `:3740`, `:3840-3841`, `:4926`, `:5747`, `:5799`, `:7371`, `:7461`, `:10295`, `:10996`, `:11063`, `:11348`, `:11455`, `:11629`; `vehicles.c:381`, `:546`, `:862`, `:876`; `projectiles.c:1413`, `:1433-1434`, `:1458-1459`; `damage.c:2276`; `objects.c:1728` (`unit_get_head_position(player->unit_index, …)`). Version is the parent's THIS when the reader is that parent's child, otherwise EITHER.
- **Before the walk (LAST):** `ai/` (`action_obey.c:1914`, `:2200`, `:2646`; `action_vehicle.c:238`, `:305`; `actions.c:3854`, `:3903`; `actor_firing_position.c:1302`; `actor_looking.c:1093`; `actor_moving.c:774`, `:792`, `:1444`; `actor_perception.c:4587`, `:4716`, `:4822`, `:4918`, `:5236`, `:5256`, `:5393`, `:6166`; `actors.c:889`, `:1512`, `:2904`, `:4095`; `ai.c:3069`; `ai_script.c:4523`; `encounters.c:3738`, `:3751`; `path_obstacles.c:251`, `:267`), `devices.c:677`, `hs_library_external.c:782`, `network_*.c` (`network_damage.c:666`, `:1321`, `:1890`, `:1914`; `network_distributed.c:2629`; `network_objects.c:1253`, `:1276`, `:1630`; `game_events.c:285`).
- **After the walk (THIS):** `player_effects.c:663`, `:1317`; `hud_weapon.c:1670`; `game_engine.c:3174`, `:4281`, `:7136`; `players.c:1981`, `:1984`; `cheats.c:387-388`.

### D. Markers and heads read by unit helpers (callers decide the timing)

`unit_get_head_position`, `unit_get_camera_position`, `unit_get_center_of_mass`, `unit_can_see_point` and `unit_estimate_position` are called from:
- **Before the walk (LAST):** `ai/` (`actors.c`, `actor_perception.c`, `ai_communication.c`, `action_*.c`, `actor_combat.c`, `actor_firing_position.c`, `actor_looking.c`), `hs_library_external.c:264`, `:268`, `:315`, `players.c:4138`, `player_control.c:536`, `player_control.c:1963`, `game_engine.c:2670`, `:5448`, `unit_dialogue.c:1021` (the call site is [U]), `network_actors.c:560`, `coop_enemies.c:395`.
- **After the walk (THIS):** `hud.c:915`, `:1124`; `hud_nav_points.c:722`, `:1016`; `motion_sensor.c:368`, `:534`, `:600`.
- **Camera code:** `first_person_camera.c:83`, `:106`, `:181`, `:203`, `bored_camera.c:130`, `following_camera.c:167`. [U] I did not trace which phase calls these.

### E. Before the walk: effects, widgets and the ME

| Reader | Function | Version |
|---|---|---|
| `effects.c:2676` `return object_get_node_matrix(effect->object_index, node_index);` and `effect_build_locations` (`:951`, `:1021` pass `object_get_marker_by_name`), run from `effects_update` (`game.c:350`) | `effect_get_node_matrix` | LAST |
| `effects.c:790` `object_get_node_matrix(object_index, marker_list.node_index);` and `:1356` | effect creation, `dangerous_effects_near_player` | EITHER for creation inside the walk, depending on the object and when it is created (the call sites are not traced); LAST for the AI check |
| `antenna.c:342` `object_get_marker_by_name(antenna->object_index, …)` from `antennas_update` (`game.c:356`) | `antenna_update_attachment` | LAST |
| `hs_library_external.c:728`, `:961` | script effect, unit exit seat | LAST |
| `game_sound.c:374` (looping sound created), `:548` | sound sources | at creation: EITHER, depending on the caller |
| `particle_systems.c:493` (attach at creation), `particles.c:849` (`particle_new`), `particles.c:328` | creation | at creation: EITHER, depending on the caller |
| ME perception rays: `psp_fixups.py:18303` `psp_perception_me_rays`, which tests matrices without the accessors | handed over in `ai_update`; settled at the first compute or move (`:18554-18556`) | LAST |
| `first_person_weapons.c:273`, `:530` | `first_person_weapon->node_matrices` | not an object's block. It is the first-person weapon's own struct, built before the walk. Out of scope. |

### F. After the tick, in the frame

All of these read through the accessors and see the finished tick; on the PSP interpolation is off (`docs/review_vislazy.md:26`), so they read the block.
- `render_interpolation.c:469` (the snapshot).
- Frame readers: `contrails.c:808`, `particle_systems.c:712`, `:1108`, `glow.c:1290`, `:1502`, `light_volumes.c:244`, `:355`, `lightning.c:278`, `:325`, `:503`, `flags.c:424`, `antenna.c:455`, `game_sound.c:1159`, `:1215`.
- Draw code: `render_objects.c:607`, `:629`; `render_particles.c:284`; the `rasterizer*` files; `object_lights.c:1200` (`lights_preprocess_scene`).
- The GE queue: `psp_ge.c:996` copies `nodes * NODE_FLOATS` floats when the call is queued, and `:11059` keeps the pointer (`model_nodes = node_matrices;`).

## 3. Which readers change under a single pass, and what keeps them exact

**Pass after the walk, or at the end of each top-level subtree**
- Readers that change from THIS to LAST, so they would be wrong:
  - the compute of a child or held weapon (`objects.c:2507`);
  - `unit_seat_update` (`units.c:11453`);
  - weapon fire reading its owner (`weapons.c:2612`, `units.c:4894`);
  - the IK grips (`objects.c:2163-2164`);
  - `object_connect_lights` (`:3877`);
  - the wrappers on parented objects;
  - collision tests, `items.c:695` and `:805`, melee targets and homing targets, whenever the other object comes earlier in the walk.
  - The bounding sphere has the same problem. The collision tests read `psp_object_spheres[]`, which only the compute writes, and the existing `nwbatch` patches settle spheres at many sites (`psp_fixups.py:22819-23022`).
- Unchanged: own-before-compute reads (`objects.c:2365`, `units.c:7811`, `:8745`, `:8909`, `bipeds.c:1990`), everything in groups C (before the walk), E and F, and the after-walk HUD and player-effect readers (group C).
- The postprocess changes the block in place after the attachments' updates (`:3894`). A pass has to keep each unit's compute, its attachments' computes and its postprocess in that order, or the IK reads unfinished matrices.

**Pass before the walk**
- [U] It computes from inputs the same object's type update is about to change (position, animation state at `:3853`), so the values differ from today's. It is not an exact pass.

**What readers need to stay exact**
- No kept copy of LAST. The block holds last tick's until the compute overwrites it.
- An order rule is needed for every THIS reader. The shelved `nwbatch` shows the mechanism [D]:
  - a waiting bit per object, tested in line in the accessors (`psp_fixups.py:22539-22544`, `:22696`, `:22704`);
  - a settle that runs the object's entries first (`:22522-22535`);
  - settles at the writers (`:22779-22805`) and at the sphere readers (`:22819-22932`).
- Inputs also need recording at the listing point. `psp_nwb_inputs` holds position, facing, scale, flags, animation and `outgoing_function_values` (`:22213-22221`). The docs say c20's Monitor parted at tick 991 until the function values were kept (`docs/plan_node_matrices_me.md:70`).
- Armed bipeds were not exact in `nwbatchheld` (`psp_fixups.py:22174`, `docs/plan_node_matrices_me.md:74`).
- **FVP interaction (CAN'T TELL).** The compute runs before `object_compute_function_values` (`objects.c:3863`, `:3866`), so today it reads LAST tick's function values. FVP now makes them after the walk (`psp_fixups.py:39308-39332`). A matrices pass that runs after FVP would read THIS tick's values and could differ. I searched `psp_fixups.py` for `nwb`/`fvpass` overlap and found no code that orders the two passes.

**How much of the work the readers leave to batch.** Measured in the emulator (`docs/plan_node_matrices_me.md:20-25`), the share of node computes whose first reader is inside `objects_update`: b30 72%, a50 78%, c20 12%, d20 24%. Weapons and vehicles are about 100%, and scenery and machines almost never. `docs/cache_work.md:84` already rules this pass "no: later objects' collision tests read them (NB, LEAF)".

## 4. Size

- `engine/source/math/real_math.h:477-479` `struct real_matrix4x3 { real scale; union { real n[4][3]; …` is 13 floats, **52 bytes** [D]. `psp_ge.c:241` `#define NODE_FLOATS 13`, and `docs/hardware_big_moves.md:117` uses "784 nodes x 52 B".
- `model_definitions.h:35` `MAXIMUM_NODES_PER_MODEL = 64`, so at most 3,328 bytes an object. The render interpolation cap is also 64 (`render_interpolation.c:57`).
- Typical: bipeds ~20–40 nodes (`docs/campaign_recon_b30.md:39`), so about 1–2 KB.
- Stated per map: b30 has 96 computes and c20 79 a tick (`docs/cache_work.md:84`). b30's drawn objects total 784 nodes, about 40 KB (`docs/hardware_big_moves.md:117`). I found no count per object type and no total for every map.

## Not traced

- The call phase of `objects.c:1728`'s caller and `unit_dialogue.c:1021`.
- The `units.c` computes at `:3162`, `:3289`, `:4079`, `:4199`, `:4229` and `:4690`.
- The vehicle effect functions' call sites.
- `biped_limp_noodle.c`'s timing.
- The camera code's phase.
- Whether the `psp_object_epoch++` in every compute matters if the compute is batched. Its readers are the perception cache in `ai_update`, before the walk, so I expect not, but I didn't check.

I took "one pass" to mean after the walk, which matches how `docs/cache_work.md:79-86` sizes it.
