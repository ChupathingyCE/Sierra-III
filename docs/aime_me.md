# AIME step 2a: what the ME would have to run (reach census)

2026-10-29, hardware session. AIME step 1 (`aime` f0f799e9: the AI's thinking done ahead at the end of the tick before, on the main CPU, with its own seed) is proven [E]. Step 2a moves the pass after a frame's last tick to the ME, during the record (`docs/hardware_surveys.md`: b30 ~10-12 ms a frame [U on M x E]). This note sizes what the ME job would have to run, from the lab's `mewalk.py` over f0f799e9's disassembly. Nothing is built yet: the two censuses below are recon keep words on `aime`.

**How it was read.** mewalk follows `jal`/`j`/`bal` from the roots and flags what the ME can't run: firmware calls, VFPU instructions, indirect calls. The walk is static: it counts every path the code has, including ones play never takes. The thinking's steps were the roots: `actor_input_update`, `actor_perception_update`, `actor_situation_update`, `actor_emotion_update`, `actor_decision_loop`, plus each actor type's `decide_action` and each action's `begin`, `end`, `perform`, `replace_prop` and `flush` handlers, which the decision loop reaches through tables the walk can't follow. The walks stopped at the fatal-error paths (`error`, `system_exit`, `halt_and_catch_fire`, `display_assert`, `__assert_func`, `release_assert_failed`, `psp_datum_get_failed`) and the log (`platform_psp_log`), since a job would hand those back to the main CPU.

| sierra-build | Roots | Reached |
|---|---|---|
| 40974801421 | `psp_aime_think` (nothing stopped) | 2,082 functions, 1,033 KB: every path ends in the fatal error's whole-screen report |
| 40975134912 | each step on its own, error paths stopped | input 134 KB, perception 238 KB, situation 10 KB, emotion 219 KB, decision loop 46 KB (before its handler tables) |
| 40975705724 | the steps and every handler together | 2,115 functions, 1,100 KB: the handlers reach the world |

## What the thinking reaches that the ME can't run

| # | Path | What it is | The way round | Label |
|---|---|---|---|---|
| 1 | `elite_decide_action` -> `actor_action_handle_vehicle_exit` -> `unit_try_and_exit_seat` -> `unit_exit_seat_end` -> animation, impulse sounds (the sound cache, the ME's own start-up), weapon readying (the texture cache, DMA) | **the decision loop writes the world**: actors leave vehicles from inside their thinking. 59 of the flagged chains go this way | queue world writes for the hand-back, or hand the actor back to the main CPU before any write. Which one depends on how often it happens (the census below) | D |
| 2 | `object_get_origin` / `object_get_node_matrix` -> `psp_nms_settle` -> node matrices (VFPU) and animation reads from the pack (file I/O) | reading a deferred object's position computes its owed node matrices | settle every owed object before the push (NMSKIP's end-of-walk loop already does at objects_update's end [D]); on the ME, a settle still owed fails the job | D |
| 3 | `prop_status_refresh` -> `psp_perception_line_of_sight` -> `psp_me_wait` | the perception ray queue waits on the ME itself | rays inline on the ME (the survey's point) | D |
| 4 | `unit_get_head_position` -> `object_get_marker_by_name` -> `matrix4x3_multiply` (VFPU) | shared maths in VFPU form | FPU copies for the job. For the ME and its main-CPU fallback to give identical bits, the thinking pass uses the FPU form on both sides, as posefpu did for the poses | D |
| 5 | `tag_get` -> `psp_keep` -> `sceKernelWaitSema` | a keep word read inside a tag lookup | the keep word read once before the push | D |
| 6 | `actor_perception_update` -> `qsort` (25 indirect calls); `actor_select_firing_position` -> `qsort_4byte` | library sorts with comparators through pointers | a port-own sort, the same order for equal keys | D |
| 7 | `props.c:406` `datum_new(prop_data)` (survey) | perception makes props | a prop pool reserved before the push | D |
| 8 | `ai_communication_event` -> `unit_speak`, `csprintf` (survey) | speech from inside the thinking | an event queue drained at the hand-back | D |
| 9 | `actor_decision_loop` -> `halo_linux_sprintf`; `ai_debug_describe_actor` | debug strings | stopped, or compiled out of the job | D |

**What the AI writes in the world, by name** (calls in `ai/*.c`, any step, not only the thinking) [D]: unit_try_and_exit_seat, unit_enter_seat, unit_start_user_animation, unit_start_animation_impulse, unit_start_running_blindly, unit_speak, unit_scream, unit_melee_attack_begin, unit_leap_begin, unit_detach_from_parent, unit_add_weapon_to_inventory, unit_add_grenade_type_to_inventory, unit_set_actively_controlled, object_new, object_delete, object_activate, object_deactivate, object_set_automatic_deactivation, weapon_set_total_rounds, game_allegiance_provoke, game_allegiance_incident, hs_wake_by_name, biped_accelerate. Which of them the thinking steps reach in play is CAN'T TELL from the static walk.

## The census: what the pass ahead writes (aimew1, aimew2)
Two emulator batches on `aime` with recon keep words: `aimewrites` hashes every game-state allocation before and after each pass ahead; `aimewritesobj` copies the objects pool before each pass and names every word changed after it, by object type and datum offset. Each ran b30, c20, d20 and a30's routes and c10 hands off, 8,100 passes a map [E].

| Write | Where | How often (of 8,100 passes, or word-passes) | What it is | On the ME |
|---|---|---|---|---|
| the AI's own state | actor, prop, ai globals, swarm, encounter, squad, pursuit, dialogue | most passes | the thinking's output | the ME writes them; the main CPU drops its copies before the next tick |
| **objects' `magic_number` (+0x8)** | every type: b30's bipeds 179k, scenery 128k word-passes | most passes | the collision walks' visit marks (`object_mark_function`) | **already solved:** the ME's ray walk keeps its marks as bit sets on its stack (FIXES_122); the whole walk does the same |
| **bipeds' pathfinding fields (+0x434-0x448)** | `pathfinding_surface_index`, `pathfinding_point`, `last_pathfinding_attempt_time`, `last_pathfinding_surface_index` | b30 ~4.6k, c20 ~2.1k, d20 ~1.9k, a30 ~2.6k, c10 28 | `biped_find_pathfinding_surface_index`'s lazy fill: a memo of the world as it stands | a store log on the ME, applied at collection (nothing in the record reads those fields [U: to check]) |
| **node blocks settled on read** | b30 scenery's node matrices 12.2k, bipeds' original orientations 4.6k; d20 13.1k | many passes | NMSKIP's deferred node matrices computed when the AI reads a position | settle every owed object before the push; a settle still owed on the ME fails the job (item 2 above) |
| **world writes proper** | `aimewho` (aimew3) counted 24 world writers at their entry: only six are called. `unit_speak` (b30 83 calls), `unit_scream` (d20 241, from action_flee), `unit_melee_attack_begin` (d20 74, c20 31, b30 8), `object_activate`/`deactivate` (up to 17), `unit_start_animation_impulse` (up to 7), `unit_enter_seat` (2), `unit_start_running_blindly` (2) | actors that reach one: b30 83 of 135,675 thought ahead (0.06%), c20 31 of 94,472, d20 335 of 50,621 (0.66%), a30 18 of 65,152, c10 4 of 3,780 | the decision handlers acting on units | **the void ones go in a queue** applied at collection (speech, scream, activation: the AI ignores their results); **the rest hand the actor back**: melee begin (its result picks the charge's next step), seat entry, animation impulse, running blindly. That's at most 0.15% of actors (d20), under 0.03% elsewhere |

## The design this gives
1. **Before the push** (the record's start, after the main CPU's whole-cache write-back): settle every owed node block; read the keep words the job needs; reserve a prop pool.
2. **On the ME, per actor:** copy the actor's datum and its props' first, then think it with its own marks, a port-own sort, its rays inline, FPU maths (the same form as the main CPU's fallback, so both give the same bits), and stores to the pathfinding memo logged. Speech, screams and activations go in a queue. At any other world writer, stop that actor: put its copy back, mark it "think in the tick", go on to the next. The census says that happens to at most 0.15% of actors. What the copy can't put back is a write to a record the actor shares (its encounter, squad, the AI globals) made before the stop. That's a small behaviour difference in a rare case, and the outcome census covers it.
3. **At collection** (the next `ai_update`, with a bounded wait): apply the store log, drain the speech and event queue, and think the handed-back actors inline, in actor order.
4. **What it changes:** a handed-back actor thinks at its turn in the tick, as without AIME; the rest think one record ahead, as AIME step 1 does. Behaviour: step 1's outcome census again, on the ME build. Speed: one c20 pair, `keepsets="- | aime"` on both sides of the ME switch.

Items 2-9 in the first table are the job's other prerequisites. The next step is building step 1's hand-back on the main CPU first (the copy, the stop at a world writer, the redo in the tick), which proves the mechanism with no ME risk; then the job.
