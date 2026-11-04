# Review: a found body's shout taken up only while its prop lives (FIXES_PROPCRASH)

A bug fix, on by default; `shoutnone` turns it off. It touches two engine files through `psp_fixups.py` (`source/ai/action_guard.c`, `source/ai/ai.c`) and nothing else.

## The crash

b30, in a hand run on the PSP-1000 (the tock, game time 27,037, at the map room bunker): `datum_get` on the AI's prop array was asked for index NONE, then a bus error at the next load. The fatal asserts are compiled out, so the lookup returned an address past the array and the read faulted.

Where it was [D]: the crash address is in `action_guard_control`, not `unit_start_user_animation`. That function is 0x218 bytes long and ends before the crash address; reading the nearest symbol in the link map's listing gave the wrong name. The disassembly at the crash:

```
datum_get(prop_data, state->shout_dead_friend_prop_index)   (index NONE)
state->shout_about_dead_friend = FALSE; ...NONE
actor_stimulus_suspicion(actor, 2, 600)
lw  a2, 0x18(prop)      <- prop->unit_index: the fault
jal ai_communication_event
```

That is the guard's shout about a dead friend, once its path ends (`action_guard.c`, `action_guard_control`).

## How the guard gets a shout about NONE [D]

1. Perception sees a friend's body: `actor_stimulus_enter_combat_found_body` sets a combat transition of type 2 that names the body's prop.
2. The decision takes it up: `actor_action_handle_combat_transition` -> `action_guard_setup_from_combat_transition`, which sets `shout_about_dead_friend` with that prop. The engine asserts the prop isn't NONE there (`action_guard.c` #90).
3. In the engine, a stimulus lives only from perception to the decision of the same actor update: `actor_decision_loop` ends it with `actor_stimulus_clear`. So the prop is always alive at step 2.
4. **With `aicad3`** (on by default in the tock), an actor on its resting tick runs perception but no decision loop, so the stimulus waits a tick. If the body's unit is deleted in that tick (garbage collection: `ai_handle_unit_deleted` -> `actor_switch_props(..., NONE)`), the stimulus's prop becomes NONE and the transition stays. The next decision sets up the shout about NONE, and the guard's control reads it.

A guard that already has the shout when its prop goes is handled by the engine: `action_guard_replace_prop` sets the prop to NONE and drops the shout. The stimulus waiting for its decision had no such rule, because in the engine it never waits.

AIME (the thinking a tick ahead) doesn't need to be on for this: it moves when the decision runs, not whether a stimulus waits a tick.

## The change

`action_guard_setup_from_combat_transition`: the shout is set up only when the transition's prop is alive. Everything else about the guard is set up as before (the transition, the point where the body lay, the timer), which is what `action_guard_replace_prop` leaves a guard with when its prop goes.

```c
	if (actor->stimuli.combat_transition == 2
#ifdef HALO_PSP
		&& (actor->stimuli.combat_transition_prop_index != NONE ||
			(psp_guard_shouts_gone++, psp_guard_shoutnone))
#endif
		)
```

- The branch is reached only with a prop of NONE, which the engine never gives it, so play is the same wherever the case doesn't come up.
- `psp_guard_shouts_gone` counts the case on both sides (the ME's pass may count it, so it sits on the ME's lines, `PSP_AIME_ME_SIDE`). `psp_guard_report`, at the start of each `ai_update` on the main CPU, logs a line when the count moves: `ai: game time N | a found body's prop gone before the decision took it up: K so far`.
- `shoutnone` is read in `ai_initialize_for_new_map`, on the main CPU, before any pass ahead can reach the decision on the ME (a keep word lookup can't run on the ME).

## Keep words

- `shoutnone`: the fix off. The shout is set up about NONE as before.
- `shoutcensus` (recon): at the end of each `ai_update` (main CPU, before `objects_update`), every found-body transition still waiting for its decision is logged, with whether the actor rested this tick under aicad3. The engine has none.
- `shoutforce` (a test, with the census): each such body's unit is deleted there, as garbage collection may before the next `ai_update`, so the next decision meets the transition's prop gone. This is the crash's repro.

The census and the test sit in `ai.c` (`psp_guard_tick_end`, one flag test a tick when off).

## Proof [E]

- Repro: on the hand run's path through b30, the asserts-on build's play makes a found body wait over a tick at game time ~3,790, on an aicad3 resting tick. With `shoutforce shoutnone` the game crashes at the next decision's control, at the instruction of the hand run's crash (the shout's load of `prop->unit_index`), every time. With `shoutforce` and the fix on, the log says the guard was set up without the shout and play goes on to the run's end. With `aicad3off` no found body waits and nothing crashes.
- Exactness: play and both seeds the same at every tick as the tock before the fix, on the b30 route (4,500 ticks), c20 and c10 (3,600), where the fix doesn't fire.
