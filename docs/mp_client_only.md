# The multiplayer build as a client

Sheepdog01, 2026-10-17 (09:48): the multiplayer build drops hosting, and ad hoc with it (ad hoc play needs a PSP that hosts). Both come back after the campaign reaches its performance targets on every level, since hosting is a prerequisite for campaign co-op. Until then the multiplayer build joins games only, and what only a host or co-op needs leaves its heap.

## What changes (branch `mpcut`, for b138)

| Change | Where | Heap gained [D] |
|---|---|---:|
| Co-op left out: `network_coop.c`, `network_actors.c`, `coop_*.c`; their answers in `psp/src/psp_nocoop.c` (both builds compile it; the campaign's moved there from `psp_campaign.c`) | `psp_build.py` (`COOP_UNITS`) | the units' code and tables (~276 KB, the earlier census) |
| Actor and prop pools 8 and 32 (the multiplayer maps' peak is 0 of 256 and 768); the game state's CPU part 640 KB shorter, starting 640 KB higher, so the heap's ceiling rises | `psp_capacity.h`, `psp_build.py` (`GAME_STATE_BASE`), `tools/cachemap.py` (note) | 640 KB |
| No server is ever created (`create_global_network_game_server` answers FALSE) | `FIXES_MPCLIENT`, network_game_globals.c | — |
| The host's update ring 1 deep (a local game takes each update as it builds it) | `FIXES_MPCLIENT`, player_queues_new.c | ~127 KB |
| One sender slot (a client hears only the host) | network_distributed.c `MAXIMUM_SENDERS`/`HOST_SENDER` | ~195 KB |
| The host's records one slot each: late joiners, `objects_host_*`, the distributed host tables | `psp_limits.h` (`PSP_HOST_MAXIMUM_NETWORK_MACHINES` 1), `FIXES_MPCLIENT` | ~64 + ~97 + ~86 KB |
| Ad hoc off (`adhoc_mode` a constant 0; the linker drops `psp_net_adhoc.c`'s code) | `psp/src/psp_net.c` | its code |

The heap figures are read from b137mp's link map and log [D]; the measured gain is mpcut1's (the lab's findings).

**Switches:** `PSP_MP_HOST=1` builds hosting and ad hoc back in (every `FIXES_MPCLIENT` change is under `PSP_MP_CLIENT_ONLY`, which it unsets). `PSP_MP_COOP=1` builds co-op back in, with the campaign's pools and game state, and implies `PSP_MP_HOST`. These are build switches, not keep words: they change what is compiled and how big the tables are.

**Safety.** Shrinking a table is safe only if nothing a client runs indexes it past its first slot. The use map below traced every use [D]. A client's new game (`network_objects_new_game`, `network_distributed_new_game`, every machine at every map) clears some host records in loops to the old counts: those loops now stop at each table's own size, split from the client's records in the same loops. Every other use needs a server, and the host's four entry points in network_objects.c also return at once. The host's per-machine sends and a machine's load are bounded by the sender slots.

**Not changed:** `update_client_globals` (the client's ring, already 32), `client_damage_animations`, `objects_client_*`, `distributed_deaths`/`_player_pings`/`_own_positions`, `machine_to_player_table` (a client needs all 128 machine slots), network_damage.c (its own audit first: one define covers the client's reports too), the server struct (17.5 KB, never touched without a server).

## Use map (2026-10-17, ChupathingyCE d98982b, line numbers before the fixups)

Engine: ChupathingyCE at d98982b (line numbers are the engine checkout's, before fixups; FIXES_23 inserts lines into network_objects.c, so the mirror's numbers there are later by ~15-40).
Sizes: [D] from the b137mp link map (lab results `sierra-build/39037200812/halo.linkmap.txt`, Sierra-III 7e52cd8, `PSP_MULTIPLAYER=1`, built with -fdata-sections so each static has its own section). Cross-checked against the struct definitions; every figure matched.

## 0. Can a server exist, and what game_connection() can a client have?

**The only writer of the server pointer** is `create_global_network_game_server` (source/networking/network_game_globals.c:800-815, `global_network_game_server = network_game_server_create()`; the macro at :133 is `bss_004566dc.server`; :452 only clears it). `network_game_server_create` (network_server_manager.c:1191) is called from nowhere else, and it is the only creator of a listening game connection (`FLAG(_connection_create_server_bit)`, network_server_manager.c:1208 → network_connection.c:1171-1179 `listen_endpoint`).

Callers of `create_global_network_game_server` in units the multiplayer build compiles:
| Site | Path |
|---|---|
| source/interface/player_ui.c:710 | `player_ui_fast_setup_network_server` (hs command, hs.c:14736; and network_test.c:1081, `debug.network_test = host`) |
| source/interface/ui_widget_event_handler_functions.c:2676 | `split_screen_game_initialize` (split screen = a local server, Xbox style) |
| source/interface/ui_widget_event_handler_functions.c:3606 | `network_game_start_new_server` (System Link's Y "create", :2757; `ui_online_games_start_server` :6033 → ui_widget.c:3466 `ui_widget_online_games_create_game`, whose only caller is browser_screen.c, a desktop unit the PSP leaves out; `ui_widget_port_host` :6209, called only from menu_functions.c, also left out) |

So `create_global_network_game_server` returning FALSE removes System Link create, split screen, the ad hoc host (psp_net_adhoc.c is only a socket shim under xnet.c: it has no server of its own; its PTP listen happens only when the game's server connection listens), network_test host mode, and hosting through the online games screen. psp/src has no other route (grep: no `game_connection_set(2)`, no server creation; psp_campaign.c:59 already stubs it FALSE for the campaign build).

**p2p "listen" path:** p2p.c:1765-1767 marks `hosting_socket` when any stream socket listens (xnet.c:277). The game's only listeners are the server connection and the telnet console (telnet_console.c:116, off unless `debug.telnet_console`; loopback only). No server, no p2p hosting.

**game_connection() values without a server:** every `game_connection_set(2)` / `_game_connection_network_server` is behind a successful create or `global_network_game_server_get()` (player_ui.c:713, ui_widget_event_handler_functions.c:2680 and 3611, network_game_manager.c:587-588). `main_globals.connection` is otherwise set to 0 (local: main menu), 1 (client: ui_widget_event_handler_functions.c:1978/2353/6269, psp_online.c:354) or 3 (film playback, main.c:2766). So a client-only build sees 0, 1, 3, never 2.

**Host code entry points**, all closed without a server:
- `network_distributed_tick` host branch: `connection == _game_connection_network_server` (network_distributed.c:3331).
- `network_distributed_handle_message`: client-sent kinds need `machine_index != NONE && game_connection() == server` (network_distributed.c:4223-4234); machine_index != NONE comes only from the server's handlers (network_server_message_handler.c:1398, 1599, both inside server functions); the client handler passes NONE (network_client_message_handler.c:720).
- `network_game_server_*` functions in network_server_manager.c/network_server_message_handler.c take a server or get it from `global_network_game_server_get()` and return when NULL (e.g. network_distributed_server_machines, network_server_message_handler.c:828).
- `network_objects_damage_animation` host branch: `connection == _game_connection_network_server` (network_objects.c:909).
- `update_server_add_player`: network_client_manager.c:2057 `if (global_network_game_server_get())`.

---

## 1. `update_server_globals` (source/game/player_queues_new.c) — NOT host-only, but safe to shrink its ring to 1

- **Definition:** :286 `static struct update_server_globals update_server_globals`; struct :190-196 `{ boolean initialized; long next_update_number_to_build; struct data_array *queues; struct update updates[MAXIMUM_SERVER_UPDATES]; }`; `MAXIMUM_SERVER_UPDATES = 32` (:127); `struct update` = 4 + `struct server_update` (4 + 128 × 0x20) = 4,104 B.
- **Size:** 0xC + 32 × 4,104 = **131,340 B** [D]. At 1: 4,116 B (saves **127,224 B**).
- **Who uses it:** every **local** game, not only a host. `game_time_update` → `update_client_local_ticks` for `_game_connection_local` (game_time.c:448-449): the main menu (ui.map) ticks through `update_server_next_update` every tick. `update_server_new` runs at every map (game.c:664) and in main.c:1120/1266, so it is initialized on clients too.

| Site | Function | Guard / runs on |
|---|---|---|
| :298 memset whole struct; :305 memset `updates` (sizeof) | update_server_new | all machines, every map (game.c:664; main.c:1120, 1266) — sizeof, fine at any size |
| :318-324 | update_server_delete | all — no `updates` index |
| :347-352 | update_server_start | local and host (game_time.c:357-359), reset_and_fill (:1030) — queues only |
| :391-393 | update_server_add_player | host only (network_client_manager.c:2057) — queues only |
| :405-471 | update_server_next_update | **local** (via update_client_local_ticks :858) and host (network_server_manager.c:2481) — `update_server_get_update(update_number)` for the number it just built (:419) |
| :487-516 | update_server_build_server_update | local (:859) and host — `get_update(queue->next_update_number)`, NULL-tolerant (:509) |
| :876-889 | update_server_handle_client_update | local; returns at once on a host (:880) — queues and `update_server_pending_control_flags` only |
| :993-996 memset (sizeof); :1024-1032 | update_queues_reset_and_fill_with_lies | all (game_state.c:198 callback) — sizeof, fine |
| :1047 | player_new_queue | queues only |
| :1069 | update_server_take_local_actions | host only (:415) |
| :1125-1141 | update_server_handle_distributed_input | host only (network_distributed.c:2395, handle_inputs, server gate) |
| :1147 | update_server_ticked_update_number | host (network_distributed.c:2589) |
| :1155 | update_server_update_actions | host (network_distributed.c:2593, history 0..3) |
| :1205-1219 | update_server_get_update | every index is `update_number & (MAXIMUM_SERVER_UPDATES-1)` after a window check `[next-N, next)` |

- **Why a ring of 1 is memory-safe everywhere:** the only index into `updates` is `& (MAXIMUM_SERVER_UPDATES-1)` (:1212), which is 0 at N=1 (1 is a power of two); every other touch is a memset by sizeof. **Behaviour at N=1:** a local tick builds update n and reads n back at once (:419, :471): unchanged. `update_server_build_server_update` in a local game reads `queue->next_update_number`, which may lag; at N=1 an older number returns NULL and the copy into the caller's local is skipped, but `update_client_local_ticks` (:846-860) discards that local anyway. The only reader that needs depth is the host's `update_server_update_actions` history (DISTRIBUTED_INPUT_HISTORY = 4), which never runs without a server.
- **Dimension/sizeof dependents:** :222-223 size assert uses `MAXIMUM_SERVER_UPDATES` (follows). No other file defines or sizes it (grep). `struct server_update`'s player dimension (128) must stay: it is shared with `update_client_globals` and the networking units' copies (:211-216).
- **Verdict:** safe to shrink to 1 under "no server ever exists" (and actually safe for local play with or without a server's absence). **1 fixup line**, multiplayer-only because the campaign compiles this file:
  ```
  ("""	MAXIMUM_SERVER_UPDATES = 32,""",
   """#if defined(HALO_PSP) && defined(PSP_MULTIPLAYER)
  	MAXIMUM_SERVER_UPDATES = 1, /* no host: a local game reads back only the update it just built */
  #else
  	MAXIMUM_SERVER_UPDATES = 32,
  #endif""")
  ```
  It can go into the existing `FIXES_MULTIPLAYER["source/game/player_queues_new.c#client"]` list (psp_fixups.py:10575). No client-path site needs changing.
- Neighbours, for completeness: `update_server_pending_control_flags[128]` (512 B) and `update_server_distributed_inputs[128]` (1,536 B) are read on the local path at `[queue_index]` (:442, :450, :457): **not** host-only; leave them.

## 2. `late_joiner_players` (source/networking/network_server_manager.c) — host-only

- **Definition:** :2701 `static struct network_player late_joiner_players[MAXIMUM_NETWORK_MACHINE_COUNT][NUMBEROF(((struct network_game *)0)->players)]`; `MAXIMUM_NETWORK_MACHINE_COUNT` is 16 after `FIXES_MULTIPLAYER[...network_server_manager.c#host]` (PSP_HOST_MAXIMUM_NETWORK_MACHINES); inner dimension 128 (HALO_PORT_MAXIMUM_NETWORK_PLAYERS); `struct network_player` 0x20.
- **Size:** 16 × 128 × 32 = **65,536 B** [D]. At [1][128]: 4,096 B (saves **61,440 B**).
- **Every use:**
  | Site | Function | Guard |
  |---|---|---|
  | :2751-2755 write (memcpy a row, `sizeof(late_joiner_players[i])` from `server->game.players`) | network_game_server_start_late_joiner (static) | callers :2583, :2598 inside the server's in-game add-player handler (takes `server`); bound `machine_index < MAXIMUM_NETWORK_MACHINE_COUNT` |
  | :2813-2815 read row, loop `index < NUMBEROF(server->game.players)` | network_game_server_late_joiner_loaded | caller network_server_message_handler.c:2639 (server's client-message handler); bound `machine_index < MAXIMUM_NETWORK_MACHINE_COUNT` |
  No initializer loop or memset anywhere (static zero only). Nothing on a client.
- **Dimension dependents:** the inner dimension must stay 128 (memcpy by row sizeof from `server->game.players`, and the loaded loop counts `NUMBEROF(server->game.players)`). The outer dimension is used only by the two bounds.
- **Verdict:** safe to shrink the outer dimension to 1. **3 fixup lines**: the declaration (`[1]` under `#ifdef HALO_PSP`; the file is multiplayer-only), and the two bounds at :2751 and :2813 changed from `MAXIMUM_NETWORK_MACHINE_COUNT` to `(long)NUMBEROF(late_joiner_players)`. The two bounds are latent (unreachable without a server) but keep it memory-safe if hosting ever comes back.
- Alternative: setting `PSP_HOST_MAXIMUM_NETWORK_MACHINES` to 1 (psp_limits.h) shrinks this and every other per-slot host array in network_server_manager.c / network_server_message_handler.c consistently, since every loop and bound there already uses the enum. Check its other users first (only those two FIXES_MULTIPLAYER entries use it today).

## 3. port/linux/game/network_objects.c host statics — host-only, one client-run function to fix

| Table | Def | Element / dims | Bytes [D] |
|---|---|---|---:|
| `objects_host_told` | :324 | long [MAXIMUM_TRACKED_OBJECTS=2048] | 8,192 |
| `objects_host_resting_cursor`, `objects_host_told_count` | :326, :328 | long | 4 + 4 |
| `objects_host_state_moving` | :332 | boolean(byte) [2048] | 2,048 |
| `objects_host_rest_times` | :333 | long [2048] | 8,192 |
| `objects_host_inventories` | :337-344 | 20-B struct [2048] | 40,960 |
| `objects_host_viewers` (holds `player_unit_origins`) | :349-364 | indices[128 machines], player_unit_origins[128 players] (12 B), machines[128] × 96 B | 14,344 |
| `objects_host_machines` | :367-371 | 20-B struct [128 machines] | 2,560 |
| `objects_host_vehicle_predictions` | :380-405 | 152-B struct [128 players] | 19,456 |
| `seen` (function static) | :1044 in distributed_host_update_objects | boolean [2048] | 2,048 |
| (already malloc'd on the PSP: send_states' `states`/`kinds`, send_inventories' `inventories`/`kinds`/`origins`, FIXES_23 psp_fixups.py:3663) | | | 4 each |
| `host_damage_animations` | :868-872 | [32] | 384 (not sized by players; leave) |

Total of the sized ones: **97,800 B**; at 1 element each about 400 B.

`player_unit_origins` is a member of `objects_host_viewers` (:354), written only in distributed_host_find_viewers (:1253-1254) and read in distributed_host_object_period (:1305-1306): host-only.

**Every use, by function (all host-only except `network_objects_new_game`):**
| Function | Sites | Reached from | Guard |
|---|---|---|---|
| distributed_host_update_objects | :1044, :1052-1097 (`seen[abs]`, `told[abs]` with abs from the object iterator, **no bound**) | network_objects_host_tick :1982; network_objects_client_asked :1173 | host tick: network_distributed.c:3353 (server branch :3331); client_asked: network_distributed.c:4403 (client_ready kind, server gate :4227-4231) |
| network_objects_client_asked | :1154 bound `HALO_PORT_MAXIMUM_NETWORK_MACHINES`; :1158-1165 machines[mi]; :1174-1184 loops `< told_count` | network_distributed.c:4403 | server gate |
| distributed_host_placed_object | :1212 told[abs] (abs < told_count) | send_states | host tick |
| distributed_host_find_viewers | :1242-1281 (`distributed_client_machines(..., HALO_PORT_MAXIMUM_NETWORK_MACHINES)`, `player_unit_count < MAXIMUM_TRACKED_PLAYERS`) | host_tick :1983 | host tick |
| distributed_host_object_period | :1298-1306 | send_states, send_inventories, network_objects_send_period | host |
| network_objects_send_period | :1346-1348 | only network_actors.c:776 (co-op, not built) | — |
| distributed_host_rest_state_due | :1368-1370 | send_states | host |
| distributed_host_send_states | :1388-1474 (loops `< told_count`; vehicle_predictions[pi] bound `MAXIMUM_TRACKED_PLAYERS` :1467) | host_tick :1984 | host tick |
| distributed_host_send_inventories | :1589-1628, :1663 (`inventories[abs]`, abs from the unit iterator, **no bound**) | host_tick :1986 | host tick |
| network_objects_handle_vehicle_prediction | :1708 bound `MAXIMUM_TRACKED_PLAYERS`; :1713-1715 | network_distributed.c:4421 (vehicle_prediction kind) | server gate |
| network_objects_note_vehicle_ground / _vehicle_below_ceiling | :1725-1748, :1771-1786 | apply_vehicle_predictions | host |
| network_objects_apply_vehicle_predictions | :1811 loop `< MAXIMUM_TRACKED_PLAYERS`; :1813-1974 | network_distributed.c:3352 | server branch |
| **network_objects_new_game** | **:3141 memset (sizeof: fine); :3144-3150 loop `< MAXIMUM_TRACKED_OBJECTS` writing told/state_moving/rest_times/inventories (and the client's objects_client_has/dead in the same loop); :3154-3156 scalars; :3157-3158 loop `< HALO_PORT_MAXIMUM_NETWORK_MACHINES` writing objects_host_machines; :3159 memset (fine); :3160-3165 loop `< MAXIMUM_TRACKED_PLAYERS` writing vehicle_predictions** | network_distributed_new_game :3315 ← game.c:645 (game_initialize_for_new_map) | **NONE: runs on every machine, every map, the main menu included** |

**Client-path sites that must change (they write past element 0):**
1. :3144-3150: split the loop. Keep `MAXIMUM_TRACKED_OBJECTS` for `objects_client_has` / `objects_client_dead`; loop the host ones (`objects_host_told`, `_state_moving`, `_rest_times`, `_inventories[].carried_time/.ammunition_time`) to `NUMBEROF(objects_host_told)`.
2. :3157: `machine_index < (short)NUMBEROF(objects_host_machines)`.
3. :3160: `index < (short)NUMBEROF(objects_host_vehicle_predictions)`.

**Dimension dependents:** declarations :324, :332, :333, :344, :352 (indices), :354 (player_unit_origins), :363 (machines), :371, :405, :1044 (seen). `objects_host_viewers.indices` and `.machines` must keep the same dimension (count ≤ indices, machines[count]). No wire format or size assert depends on any of them. The FIXES_23 mallocs size by `MAXIMUM_TRACKED_OBJECTS`; they are only made on the first host send, so they cost nothing on a client and need no change.

**Latent host-only sites** (unreachable without a server; they would overflow a shrunk table if a server existed): :1059/:1060/:1063/:1077 and :1594-:1628/:1663 index by object absolute index with no bound; :1154, :1242-1243, :1247, :1467, :1708, :1811 bound by the old constants. If hosting might return, either bound these by NUMBEROF, or (fewer lines) make the four host entry points return at once in this build: network_objects_host_tick, network_objects_client_asked, network_objects_handle_vehicle_prediction, network_objects_apply_vehicle_predictions.

**Verdict:** safe to shrink every `objects_host_*` table (and `seen`) to 1 under "no server ever exists", with **3 client-path changes** in `network_objects_new_game` plus the ~10 declaration lines. `client_damage_animations` (65,536 B, :878-884) is the **client's** (network_objects_handle_damage_animations, :921-929): leave it.

## 4. port/linux/game/network_distributed.c host statics — host-only, three client-run loops to fix

| Table | Def | Element / dims | Bytes [D] | Host-only? |
|---|---|---|---:|---|
| `distributed_host_states` | :585 | `struct distributed_unit_state` (64 B) [128 players] | 8,192 | yes |
| `distributed_host_actions` | :586 | `struct distributed_relayed_action` (20 B) [128] | 2,560 | yes |
| `distributed_sends` | :588 | byte [128 machines][128 players] | 16,384 | yes |
| `distributed_seen` | :563 | boolean [128][128] | 16,384 | yes |
| `distributed_viewers` | :566-572 | 28-B struct [128 machines][4] | 14,336 | yes |
| `distributed_predictions` | :493-500 | 80-B struct [128 players] | 10,240 | yes (client read is behind a server check, below) |
| `distributed_accepted` | :505-514 | 44-B struct [128] | 5,632 | yes |
| `distributed_host_speeds` | :524-538 | 60-B struct [128] | 7,680 | yes |
| `distributed_client_identities` | :620 | 64-B struct [128 machines] | 8,192 | yes |
| `distributed_client_clocks` | :624-634 | 20-B struct [128 machines] | 2,560 | yes |
| `distributed_round_trips` | :641-646 | 12-B struct [128 machines] | 1,536 | yes |
| `distributed_machine_players` | :550 | long [128 machines][4] | 2,048 | yes |
| `distributed_sent_units` | :553-559 | 16-B struct [128 players] | 2,048 | yes |
| `distributed_input_facings` | :2333-2337 | 12-B struct [128] | 1,536 | yes (handle_inputs only, :2393) |
| `distributed_sent_statistics` | :592 | ulong [128] | 512 | yes |
| `distributed_player_machines` | :589 | long [128] | 512 | yes in effect; leave (tiny, and `distributed_player_machine()` is exported) |
| `distributed_machines` | :652-658 | indices[128] | 516 | yes; leave (tiny) |
| plan_players' statics `present, changed, has_action, action_changed, quit, placed` / `origins` / `clusters` | :2570-2577 | [128 players] | 6×128 + 1,536 + 256 | yes |

Host-only sum ≈ **102,400 B**. `distributed_deaths`, `distributed_seat_disagreements`, `distributed_player_pings`, `distributed_own_positions` are the client's too (distributed_handle_unit_state :2169-2225, distributed_player_ping :1312): **not** host-only.

**Uses, by function:**
| Function | Tables | Reached from / guard |
|---|---|---|
| distributed_host_plan_players :2567-2781 | sends (memset :2595, :2694), player_machines :2598/:2610, host_states :2616, host_actions :2647, sent_units :2635-2642, viewers[mi][i] :2667, seen[mi][pi] :2705/:2735-2737/:2770; loops `< MAXIMUM_TRACKED_PLAYERS`, mi from distributed_client_machines | network_distributed_tick server branch :3354 |
| distributed_host_send_players :2785-2824 | sends, host_actions, host_states, player_machines | server branch :3368 |
| distributed_apply_predictions :2097-2146 → distributed_on_foot_bound :1815 (host_speeds), distributed_take_prediction :2021-2082 (accepted, host_speeds, predictions), distributed_on_foot_ceiling :1976 | predictions, accepted, host_speeds; loop `< MAXIMUM_TRACKED_PLAYERS` | server branch :3351 |
| distributed_handle_predictions :1752-1779 | predictions[state.player_index] bound `MAXIMUM_TRACKED_PLAYERS` :1769 | player_prediction kind, server gate :4227-4231 |
| distributed_handle_inputs :2364-2422 | round_trips[mi] bound `HALO_PORT_MAXIMUM_NETWORK_MACHINES` :2400; input_facings via :2393 | player_inputs kind, server gate |
| distributed_machine_loaded :1246-1274 | machine_players, received_times, sender_times, round_trips, viewers, client_clocks, client_identities, seen (loop :1271 to `MAXIMUM_TRACKED_PLAYERS`); bound `HALO_PORT_MAXIMUM_NETWORK_MACHINES` :1253 | :4399 client_ready kind, server gate |
| distributed_note_client_clock :3998-4091, distributed_machine_clock_fast :4102 | client_clocks, client_identities; bound HALO_PORT :4006, :4105 | :4260-4265, only when machine_index != NONE (server gate passed) |
| network_distributed_handle_message :4364-4374 | client_identities; bound HALO_PORT | client_identity kind, server gate |
| distributed_write_player_record :3639, network_distributed_ban_until :3818 | client_identities; bound HALO_PORT | distributed_log_cheater (host), network_server_manager.c:1075 (ban, needs a server) |
| distributed_machine_sees_player :1295, distributed_player_machine :1288, distributed_machine_round_trip_ticks :1327, distributed_player_ping :1304 | sends, player_machines, round_trips; bounds HALO_PORT / MAXIMUM_TRACKED_PLAYERS | callers: network_damage.c:688 (inside `connection == server` :682), :1507/:1873 (hit-report checks, server gate), :2005/:2066-2068/:2135 (network_damage_host_tick, server branch :3355); network_distributed.c:1983 (host), :2990-3005 (send_pickups, server branch), :3531 (host names); network_objects.c:1777 (host). distributed_player_ping reads round_trips only after `connection == server` (:1313-1316); its other callers are game_engine.c:2146 / game_events.c:823 (scoreboard, any machine): safe, the client branch returns first (:1311-1312) |
| distributed_send_statistics :3135-3142 | sent_statistics | server branch :3357 |
| **distributed_state_from_player :1389-1395** | predictions[pi] | **also called on a client** (distributed_client_send_predictions :1744), but the read is behind `game_connection() == _game_connection_network_server &&` first (short-circuit): safe on a client. Keep that order. |
| **network_distributed_new_game :3242-3319** | memsets by sizeof :3250-3258, :3266-3268, :3270, :3306-3307 (fine); **loops below** | game.c:645: **every machine, every map** |

**Client-path sites that must change (in network_distributed_new_game):**
1. :3263-3264 `machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES` writing `distributed_client_clocks[].window_tick` → `< NUMBEROF(distributed_client_clocks)`.
2. :3276-3283 loop `< MAXIMUM_TRACKED_PLAYERS` writes `distributed_deaths[].dead_since` (client's, keep 128) **and** `distributed_sent_units[]` (host) → split; sent_units to `NUMBEROF(distributed_sent_units)`.
3. :3291-3296 loop `< MAXIMUM_TRACKED_PLAYERS` writes `distributed_predictions[].taken_host_time`, `distributed_host_speeds[].unit_index`, `distributed_player_machines[]` → each to its own NUMBEROF (or split).
(:3284-3290 loops `< MAXIMUM_SENDERS`: see 5a.)

**Dimension dependents:** declarations above; `distributed_client_machines(..., HALO_PORT_MAXIMUM_NETWORK_MACHINES)` maxima (:1018, :2579, :2789; network_objects.c:1242; network_damage.c:2135) only matter if `distributed_machines.indices` shrinks; no wire format or size assert depends on these tables.

**Latent host-only bounds** (old constants; unreachable without a server): :1253, :1271, :1291, :1299-1300, :1319, :1330, :1389, :1769, :2400, :2597-2610, :2613, :2691, :2799, :2811, :3135-3142, :3660-3664, :3837, :4006, :4105, :4364; and the plan/apply/send loops. These have exact bounds today, so if the tables are shrunk they must be re-bounded or their functions made unreachable; the natural single guard is the existing `connection == _game_connection_network_server` (which `create == FALSE` makes impossible). For belt and braces, an early return in the multiplayer build at the top of the host branch of network_distributed_tick (:3331) and of the server-gated message kinds (:4231) covers all of them, plus network_damage's host paths.

**Verdict:** safe to shrink all the tables marked host-only (≈100 KB) to 1 under "no server ever exists", with **3 client-path loop changes** in network_distributed_new_game.

## 5. Other 128-machine / 128-player tables

**5a. The per-sender tables (the biggest; NOT host-only as written, but a client uses one slot):**
| Table | Def | Bytes [D] |
|---|---|---:|
| `distributed_batches[MAXIMUM_SENDERS]` | network_distributed.c:662 | 155,832 (129 × 1,208) |
| `distributed_received_times[MAXIMUM_SENDERS][78]` | :602 | 40,248 |
| `distributed_sender_times[MAXIMUM_SENDERS]` | :610 | 516 |
`MAXIMUM_SENDERS = HALO_PORT_MAXIMUM_NETWORK_MACHINES + 1`, `HOST_SENDER = HALO_PORT_MAXIMUM_NETWORK_MACHINES` (:178-179). A client uses **slot 128** (`HOST_SENDER`: :1027, :1730, :3397, :3450), not slot 0, so these cannot simply be sized to 1. They can if the two enum lines become `MAXIMUM_SENDERS = 1, HOST_SENDER = 0` in the multiplayer build:
- client paths then all hit slot 0: distributed_send `_distributed_to_host` :1027, packer :1730, batch room/append/flush :860-905 (flush sends to the host when `sender == HOST_SENDER` :870), batches_flush :970-974 (`% MAXIMUM_SENDERS`), time_bounded :3397-3404 and stale :3450-3484 (both bounded by MAXIMUM_SENDERS), new_game :3284-3290 (loop to MAXIMUM_SENDERS);
- host-only writers index by machine index with the old bound: distributed_send_to_machine :1033 (`< HALO_PORT_MAXIMUM_NETWORK_MACHINES`, then batch[machine_index]), distributed_send `_to_clients` :1016-1020 (machine indices from distributed_client_machines, 0 without a server), host packers :2797/:2810, distributed_machine_loaded :1264-1265. All need a server.
- Saves ≈ **195 KB**. Fixup: the two enum lines (:178-179), plus re-bounding :1033 and :1253-1265 if hosting may return.

**5b. "viewers" and "machines" tables** (the earlier plan's 128 → 16 candidates):
- `objects_host_viewers` (network_objects.c:349-364, 14,344 B) and `objects_host_machines` (:367-371, 2,560 B): host-only (section 3).
- `distributed_viewers` (network_distributed.c:566-572, 14,336 B), `distributed_machine_players` (:550, 2,048 B), `distributed_client_clocks`, `distributed_round_trips`, `distributed_client_identities`, `distributed_machines`: host-only (section 4).
- `machine_to_player_table[128][4]` (source/game/players.c:514, 2,048 B): **the client's too** (machine_get_player_list :645; players.c:1175, 1219). A client needs all 128: the host numbers machines up to 127 in a 128-machine game. Leave it.

**5c. More host-only memory in network_server_manager.c** (everything there needs a server; the machine-sized ones are already 16):
- `network_game_server_memory_do_not_use_directly` (:793; COMMON, **17,548 B** [D]): the server itself (struct network_game 13,120 B, client_machines[16], waiting_players[128] 4,096 B). Only `network_game_server_create` (:1195) takes it. It could be malloc'd in create, or left in place (create never runs, so it is never touched, but it still costs bss).
- `network_game_server_start_players[128]` (:800, 4,096 B) and `network_game_server_ingame_additions[128]` (:880, 1,024 B): server functions only.

**5d. Not host-only, for the record:** `update_client_globals` (131,476 B; the multiplayer build's ring is already 32: psp_fixups.py:10575), `client_damage_animations` (65,536 B, network_objects.c:878), `objects_client_dead` / `objects_client_has` (16,384 / 8,192 B), `distributed_deaths`, `distributed_player_pings`, `update_client_relayed_actions` (5,632 B).

**5e. Out of scope but adjacent:** network_damage.c already redefines `MAXIMUM_TRACKED_PLAYERS` as `PSP_HOST_MAXIMUM_NETWORK_PLAYERS` (16) for the whole file. Taking that to 1 would need its own audit: the define covers the client's `damage_reports` too.

## 6. Summary of the fixups that keep a client safe

| Table(s) | Bytes now | After | Client-path fixups (must) | Other lines |
|---|---:|---:|---|---|
| update_server_globals ring | 131,340 | 4,116 | none | 1 (MAXIMUM_SERVER_UPDATES → 1, `HALO_PSP && PSP_MULTIPLAYER` only) |
| late_joiner_players | 65,536 | 4,096 | none | decl + 2 latent bounds (:2751, :2813) |
| objects_host_* + seen | 97,800 | ~0.4 K | network_objects_new_game :3144 (split), :3157, :3160 | ~10 decls; latent bounds or 4 early returns |
| distributed host tables | ~102,400 | ~1 K | network_distributed_new_game :3263, :3276 (split), :3291 (split) | ~18 decls; latent bounds or one host-branch guard |
| per-sender batches/times | 196,596 | ~1.5 K | none if MAXIMUM_SENDERS = 1 and HOST_SENDER = 0 together | 2 enum lines; latent :1033, :1253-1265 |

Plus `create_global_network_game_server` returning FALSE (network_game_globals.c:800), which makes every host path above unreachable (section 0).
