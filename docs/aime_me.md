# AIME step 2a: what the ME would have to run (reach census)

2026-10-29, hardware session. AIME step 1 (`aime` f0f799e9: the AI's thinking done ahead at the end of the tick before, on the main CPU, with its own seed) is proven [E]. Step 2a moves the pass after a frame's last tick to the ME, during the record (`docs/hardware_surveys.md`: b30 ~10-12 ms a frame [U on M x E]). This note sizes what the ME job would have to run, from the lab's `mewalk.py` over f0f799e9's disassembly. Nothing is built.

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

## Next: a census before any ME code
Item 1 decides the design: a queue changes when a seat exit or a provocation lands (a few ms of game time), while a hand-back costs the actor its ME time. The step-1 build can count both cheaply:
- **The census:** in a recon build of `aime`, a counter per world-writing call made while `psp_aime_think_only` is set, and per actor whether it made any. Run it in the emulator on b30's flight and landing, c20's swarm, d20 and a30 (the exactness batches' routes).
- **If world writes are rare** (a few actors a minute): the ME thinks every actor, and an actor whose thinking reaches a world writer is handed back. Its own writes so far are undone from a copy of its actor and prop datums taken before it started.
- **If they're common:** a write queue, with the census's list as its contents, and an outcome census like step 1's to show the timing change plays the same.
- Items 2-9 are needed either way.
