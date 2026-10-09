# Tick-rate literals: classified sites

Base: cybersecurity/halo-ce-universal `8fb1647e`. Every edit compiles to identical assembly at 30 Hz (all 40 touched files checked with the port's own clang flags, LTO and debug info off).

Labels: `CONVERT` = edited to use TICKS_PER_SECOND / TICKS_FROM_30HZ; `KEEP` = left as-is, with the reason.


## CONVERT: float sites (37)

| Site | Was | Now |
|---|---|---|
| `ai/action_obey.c:2645` | `* 30.0f` | `* (real)TICKS_PER_SECOND` |
| `ai/actions.c:319` | `#define ACTOR_GRENADE_TICKS_PER_SECOND 30.0f` | `#define ACTOR_GRENADE_TICKS_PER_SECOND ((real)TICKS_PER_SECOND)` |
| `ai/actions.c:4043` | `< 0.033333335f` | `< (1.0f / TICKS_PER_SECOND)` |
| `ai/actions.c:4405` | `* 30.0f` | `* (real)TICKS_PER_SECOND` |
| `ai/actions.c:-` | `urgency *= 45.0f;` | `urgency *= REAL_TICKS_FROM_30HZ(45.0f);` |
| `ai/actions.c:4633` | `urgency < 30.0f` | `urgency < (real)TICKS_PER_SECOND` |
| `ai/actor_perception.c:2885` | `30.0f);` | `(real)TICKS_PER_SECOND);` |
| `ai/actor_perception.c:2963` | `30.0f);` | `(real)TICKS_PER_SECOND);` |
| `ai/actor_perception.c:3684` | `30.0f);` | `(real)TICKS_PER_SECOND);` |
| `ai/actor_perception.c:3700` | `30.0f);` | `(real)TICKS_PER_SECOND);` |
| `ai/actor_perception.c:4955` | `45.0f,` | `REAL_TICKS_FROM_30HZ(45.0f),` |
| `ai/actor_type_flood.c:404` | `MAX(change_time * 30.0f, 31.0f)` | `MAX(change_time * (real)TICKS_PER_SECOND, (real)(TICKS_PER_SECOND + 1))` |
| `ai/ai_communication.c:2015` | `* 30.0f +` | `* (real)TICKS_PER_SECOND +` |
| `ai/ai_communication.c:2688` | `delay * 30.0f` | `delay * (real)TICKS_PER_SECOND` |
| `ai/ai_communication.c:5293` | `delay_time * 30.0f` | `delay_time * (real)TICKS_PER_SECOND` |
| `effects/effects.c:2057` | `* 30.0f;` | `* (real)TICKS_PER_SECOND;` |
| `effects/effects.c:2058` | `* 30.0f;` | `* (real)TICKS_PER_SECOND;` |
| `effects/effects.c:2059` | `* 30.0f;` | `* (real)TICKS_PER_SECOND;` |
| `game/player_rumble.c:296` | `1.0f / 30.0f` | `1.0f / TICKS_PER_SECOND` |
| `interface/first_person_weapons.c:-` | `*30.0f;` | `*(real)TICKS_PER_SECOND;` |
| `interface/first_person_weapons.c:-` | `*-30.0f;` | `*-(real)TICKS_PER_SECOND;` |
| `interface/hud_draw.c:1043` | `* 30.0f` | `* (real)TICKS_PER_SECOND` |
| `interface/terminal.c:266` | `* 0.033333335f` | `* (1.0f / TICKS_PER_SECOND)` |
| `main/main.c:1636` | `1.0f / 30.0f` | `1.0f / TICKS_PER_SECOND` |
| `main/main.c:2205` | `0.03333333507180214f` | `(1.0f / TICKS_PER_SECOND)` |
| `main/main.c:2447` | `0.03333333507180214f` | `(1.0f / TICKS_PER_SECOND)` |
| `objects/damage.c:1029` | `0.033333335f;` | `(1.0f / TICKS_PER_SECOND);` |
| `objects/damage.c:1951` | `+ 0.033333335f` | `+ (1.0f / TICKS_PER_SECOND)` |
| `objects/objects.c:4810` | `* 0.033333335f` | `* (1.0f / TICKS_PER_SECOND)` |
| `objects/widgets/glow.c:1209` | `/ 30.0f` | `/ (real)TICKS_PER_SECOND` |
| `objects/widgets/glow.c:1219` | `* 30.0f` | `* (real)TICKS_PER_SECOND` |
| `objects/widgets/glow.c:1414` | `30.0f /` | `(real)TICKS_PER_SECOND /` |
| `units/units.c:1626` | `* 0.033333335f` | `* (1.0f / TICKS_PER_SECOND)` |
| `units/vehicles.c:838` | `(real)((1.0/30.0)*(1.0/30.0))` | `(real)((1.0/TICKS_PER_SECOND)*(1.0/TICKS_PER_SECOND))` |
| `units/vehicles.c:1112` | `(4.0f/30.0f)` | `(4.0f/TICKS_PER_SECOND)` |
| `units/vehicles.c:1581` | `1.0f/30;` | `1.0f/TICKS_PER_SECOND;` |
| `units/vehicles.c:2314` | `(1.0f/30)` | `(1.0f/TICKS_PER_SECOND)` |

## CONVERT: integer sites (111)

| Site | Was | Now |
|---|---|---|
| `ai/action_charge.c:739` | `> 15` | `> TICKS_FROM_30HZ(15)` |
| `ai/action_flee.c:377` | `= 45;` | `= TICKS_FROM_30HZ(45);` |
| `ai/action_guard.c:149` | `= 120;` | `= TICKS_FROM_30HZ(120);` |
| `ai/action_guard.c:165` | `= 120;` | `= TICKS_FROM_30HZ(120);` |
| `ai/action_obey.c:1283` | `= 15;` | `= TICKS_FROM_30HZ(15);` |
| `ai/action_obey.c:2552` | `= 10;` | `= TICKS_FROM_30HZ(10);` |
| `ai/action_vehicle.c:862` | `>= 30` | `>= TICKS_FROM_30HZ(30)` |
| `ai/action_wait.c:73` | `= 150;` | `= TICKS_FROM_30HZ(150);` |
| `ai/action_wait.c:167` | `= 120;` | `= TICKS_FROM_30HZ(120);` |
| `ai/actions.c:2982` | `= 90;` | `= TICKS_FROM_30HZ(90);` |
| `ai/actions.c:3875` | `< 60` | `< TICKS_FROM_30HZ(60)` |
| `ai/actions.c:4617` | `< 30` | `< TICKS_FROM_30HZ(30)` |
| `ai/actions.c:4626` | `< 20` | `< TICKS_FROM_30HZ(20)` |
| `ai/actor_combat.c:327` | `>= 75` | `>= TICKS_FROM_30HZ(75)` |
| `ai/actor_combat.c:2348` | `>= 45` | `>= TICKS_FROM_30HZ(45)` |
| `ai/actor_looking.c:402` | `< 210` | `< TICKS_FROM_30HZ(210)` |
| `ai/actor_moving.c:453` | `= 75;` | `= TICKS_FROM_30HZ(75);` |
| `ai/actor_moving.c:1691` | `< 90` | `< TICKS_FROM_30HZ(90)` |
| `ai/actor_perception.c:1214` | `> 150` | `> TICKS_FROM_30HZ(150)` |
| `ai/actor_perception.c:2048` | `>= 150` | `>= TICKS_FROM_30HZ(150)` |
| `ai/actor_perception.c:2866` | `= 22;` | `= TICKS_FROM_30HZ(22);` |
| `ai/actor_perception.c:2870` | `= 22;` | `= TICKS_FROM_30HZ(22);` |
| `ai/actor_perception.c:2892` | `>= 45` | `>= TICKS_FROM_30HZ(45)` |
| `ai/actor_perception.c:3601` | `= 22;` | `= TICKS_FROM_30HZ(22);` |
| `ai/actor_perception.c:3688` | `= 45;` | `= TICKS_FROM_30HZ(45);` |
| `ai/actor_perception.c:3704` | `= 45;` | `= TICKS_FROM_30HZ(45);` |
| `ai/actor_perception.c:4884` | `= 20;` | `= TICKS_FROM_30HZ(20);` |
| `ai/actor_perception.c:5447` | `= 30;` | `= TICKS_FROM_30HZ(30);` |
| `ai/actor_perception.c:6239` | `>= 60` | `>= TICKS_FROM_30HZ(60)` |
| `ai/actor_perception.c:6337` | `>= 45` | `>= TICKS_FROM_30HZ(45)` |
| `ai/actor_perception.c:6343` | `>= 60` | `>= TICKS_FROM_30HZ(60)` |
| `ai/actor_perception.c:6384` | `= 750;` | `= TICKS_FROM_30HZ(750);` |
| `ai/actor_perception.c:6930` | `>= 180` | `>= TICKS_FROM_30HZ(180)` |
| `ai/actor_perception.c:6933` | `>= 180` | `>= TICKS_FROM_30HZ(180)` |
| `ai/actor_stimulus.c:764` | `= 750;` | `= TICKS_FROM_30HZ(750);` |
| `ai/actor_type_elite.c:110` | `= 30;` | `= TICKS_FROM_30HZ(30);` |
| `ai/actor_type_jackal.c:105` | `= 30;` | `= TICKS_FROM_30HZ(30);` |
| `ai/actor_type_flood.c:405` | `= 30;` | `= TICKS_FROM_30HZ(30);` |
| `ai/actor_type_infection.c:517` | `> 45` | `> TICKS_FROM_30HZ(45)` |
| `ai/actor_type_infection.c:519` | `= 45;` | `= TICKS_FROM_30HZ(45);` |
| `ai/actor_type_infection.c:814` | `>= 45` | `>= TICKS_FROM_30HZ(45)` |
| `ai/actors.c:1409` | `> 15` | `> TICKS_FROM_30HZ(15)` |
| `ai/actors.c:1748` | `< 150` | `< TICKS_FROM_30HZ(150)` |
| `ai/actors.c:1751` | `== 150` | `== TICKS_FROM_30HZ(150)` |
| `ai/actors.c:4003` | `>= 60` | `>= TICKS_FROM_30HZ(60)` |
| `ai/ai.c:1632` | `< 90` | `< TICKS_FROM_30HZ(90)` |
| `ai/ai_communication.c:1026` | `= 60;` | `= TICKS_FROM_30HZ(60);` |
| `ai/ai_communication.c:1070` | `= 30;` | `= TICKS_FROM_30HZ(30);` |
| `ai/ai_communication.c:1071` | `= 45;` | `= TICKS_FROM_30HZ(45);` |
| `ai/ai_communication.c:1072` | `= 30;` | `= TICKS_FROM_30HZ(30);` |
| `ai/ai_communication.c:1073` | `= 900;` | `= TICKS_FROM_30HZ(900);` |
| `ai/ai_communication.c:4889` | `= 24;` | `= TICKS_FROM_30HZ(24);` |
| `ai/ai_communication.c:5916` | `>= 270` | `>= TICKS_FROM_30HZ(270)` |
| `ai/ai_communication.c:6045` | `>= 180` | `>= TICKS_FROM_30HZ(180)` |
| `ai/ai_communication.c:6049` | `>= 75` | `>= TICKS_FROM_30HZ(75)` |
| `ai/ai_communication.c:6064` | `>= 180` | `>= TICKS_FROM_30HZ(180)` |
| `ai/ai_communication.c:6069` | `>= 75` | `>= TICKS_FROM_30HZ(75)` |
| `ai/ai_communication.c:6074` | `>= 75` | `>= TICKS_FROM_30HZ(75)` |
| `ai/ai_communication.c:7123` | `= 24;` | `= TICKS_FROM_30HZ(24);` |
| `ai/ai_debug.c:2325` | `= 120-` | `= TICKS_FROM_30HZ(120)-` |
| `ai/ai_debug.c:5996` | `= 15;` | `= TICKS_FROM_30HZ(15);` |
| `ai/encounters.c:1129` | `= 90;` | `= TICKS_FROM_30HZ(90);` |
| `ai/encounters.c:2761` | `>= 210` | `>= TICKS_FROM_30HZ(210)` |
| `ai/encounters.c:2993` | `= 120;` | `= TICKS_FROM_30HZ(120);` |
| `game/game_engine.c:2794` | `= 900;` | `= TICKS_FROM_30HZ(900);` |
| `game/game_engine.c:3567` | `= 120;` | `= TICKS_FROM_30HZ(120);` |
| `game/game_engine.c:4744` | `= 150;` | `= TICKS_FROM_30HZ(150);` |
| `game/game_engine.c:4838` | `= 300;` | `= TICKS_FROM_30HZ(300);` |
| `game/game_engine.c:4905` | `= 150;` | `= TICKS_FROM_30HZ(150);` |
| `game/game_engine.c:4939` | `= 300;` | `= TICKS_FROM_30HZ(300);` |
| `game/game_engine.c:5011` | `= 150;` | `= TICKS_FROM_30HZ(150);` |
| `game/game_engine.c:5036` | `= 150;` | `= TICKS_FROM_30HZ(150);` |
| `game/game_engine.c:5090` | `= 300;` | `= TICKS_FROM_30HZ(300);` |
| `game/game_engine.c:5110` | `= 150;` | `= TICKS_FROM_30HZ(150);` |
| `game/game_engine.c:5147` | `= 150;` | `= TICKS_FROM_30HZ(150);` |
| `game/game_engine.c:5183` | `= 150;` | `= TICKS_FROM_30HZ(150);` |
| `game/game_engine.c:5219` | `= 150;` | `= TICKS_FROM_30HZ(150);` |
| `game/game_engine.c:5264` | `= 300;` | `= TICKS_FROM_30HZ(300);` |
| `game/game_engine.c:5297` | `= 300;` | `= TICKS_FROM_30HZ(300);` |
| `game/game_engine.c:5358` | `= 300;` | `= TICKS_FROM_30HZ(300);` |
| `game/game_engine.c:5394` | `= 300;` | `= TICKS_FROM_30HZ(300);` |
| `game/game_engine.c:5464` | `= 450;` | `= TICKS_FROM_30HZ(450);` |
| `game/game_engine.c:7768` | `== 90` | `== TICKS_FROM_30HZ(90)` |
| `game/game_engine.c:7768` | `== 60` | `== TICKS_FROM_30HZ(60)` |
| `game/game_engine.c:7768` | `== 30` | `== TICKS_FROM_30HZ(30)` |
| `game/game_engine.c:7819` | `== 90` | `== TICKS_FROM_30HZ(90)` |
| `game/game_engine.c:7824` | `== 60` | `== TICKS_FROM_30HZ(60)` |
| `game/game_engine.c:7829` | `== 30` | `== TICKS_FROM_30HZ(30)` |
| `game/game_engine_race.c:1476` | `= 30;` | `= TICKS_FROM_30HZ(30);` |
| `game/players.c:3875` | `>= 90` | `>= TICKS_FROM_30HZ(90)` |
| `hs/hs_runtime.c:2374` | `= 30;` | `= TICKS_PER_SECOND;` |
| `interface/hud_messaging.c:699` | `= 30 * (` | `= TICKS_PER_SECOND * (` |
| `interface/hud_unit.c:520` | `< 15` | `< TICKS_FROM_30HZ(15)` |
| `main/main.c:968` | `= 91;` | `= TICKS_FROM_30HZ(91);` |
| `main/main.c:1917` | `= 10;` | `= TICKS_FROM_30HZ(10);` |
| `units/bipeds.c:1695` | `>30 &&` | `>TICKS_FROM_30HZ(30) &&` |
| `units/bipeds.c:1748` | `>120)` | `>TICKS_FROM_30HZ(120))` |
| `units/bipeds.c:1752` | `>30 &&` | `>TICKS_FROM_30HZ(30) &&` |
| `units/bipeds.c:3840` | `< 22 &&` | `< TICKS_FROM_30HZ(22) &&` |
| `units/bipeds.c:3935` | `= 60;` | `= TICKS_FROM_30HZ(60);` |
| `units/unit_dialogue.c:665` | `= 45;` | `= TICKS_FROM_30HZ(45);` |
| `units/unit_dialogue.c:704` | `= 24;` | `= TICKS_FROM_30HZ(24);` |
| `units/unit_dialogue.c:864` | `= 60;` | `= TICKS_FROM_30HZ(60);` |
| `units/unit_dialogue.c:869` | `= 30;` | `= TICKS_FROM_30HZ(30);` |
| `units/unit_dialogue.c:870` | `= 22;` | `= TICKS_FROM_30HZ(22);` |
| `units/unit_dialogue.c:987` | `= 22;` | `= TICKS_FROM_30HZ(22);` |
| `units/units.c:6750` | `= 45;` | `= TICKS_FROM_30HZ(45);` |
| `units/units.c:8856` | `>15)` | `>TICKS_FROM_30HZ(15))` |
| `units/units.c:11337` | `= 10;` | `= TICKS_FROM_30HZ(10);` |
| `units/vehicles.c:2457` | `<30 &&` | `<TICKS_FROM_30HZ(30) &&` |
| `units/vehicles.c:2628` | `= 15;` | `= TICKS_FROM_30HZ(15);` |

## KEEP (13 entries)

| Site | Value | Why |
|---|---|---|
| `ai/actions.c:1797` | `target_hidden_ticks = 1000` | sentinel for never seen (comment added) |
| `game/game_engine.c:6906` | `hold_time >= 10` | 10 steps of 0.1 alpha; wrapping would break the fade (comment added) |
| `main/main.c:1589` | `ticks <= 15` | bound on a debug frame skip (comment added) |
| `ai/actor_firing_position.c:1305` | `30.0f * 30.0f` | distance squared |
| `ai/actor_perception.c:3977` | `distance < 30.0f` | distance |
| `ai/ai_communication.c:1074` | `absolute_range = 30.0f` | range |
| `interface/progress_bar.c:2284` | `y*0.033333335f` | UI vertex scale |
| `ai/ai_communication.c:1083-1350 (10)` | dialogue table floats | `weight`, and `repeat_delay` in seconds converted at :3211 with TICKS_PER_SECOND |
| `ai/ai_communication.c:1353-1433 (7)` | reply table floats | `repeat_delay` in seconds, converted at :3236 |
| `network_*, input_abstraction, xbox caches, progress_bar.c:1786, errors.c:245 (20)` | 1000/2000/10000/... | real-time milliseconds |
| `camera/*.c (11)` | `timer<=3600.f` | float seconds validation |
| `actor_type_infection.c:493/549, weapons.c:758/3052, bipeds.c:4100, units.c:8967/8969 (7)` | `255`, `127` | byte-sized counter caps |
| `action_flee.c:67, actions.c:268, device_machines.c:54, rasterizer_xbox_profile.c:198/200 (5)` | enum value, bit index, offsetof | not time |

## Not covered by this pass

- Single-digit tick literals (e.g. `unit_dialogue.c:857` `pause_time = 7`) and literals in variables not named like time.
- HaloScript in scenario tags: `(sleep 30)` and similar count ticks in map data, not code.
- Per-tick values stored in tags (the PAL list in `port/linux/game/pal_tags.c`).

The 25 Hz vs 30 Hz side-by-side test is the backstop for all three.
