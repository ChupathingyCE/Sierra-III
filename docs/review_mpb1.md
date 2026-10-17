# Review: mpb1, the ONLINE GAMES screen (branch `mpbrowse`)

What changed since `w131` (b131's trunk), in the order to read it. Sheepdog01's call, 2026-10-15: the server browser goes ahead of "campaign first" as a showcase track; the campaign build is untouched.

## 1. mp5 merged onto the trunk

`mp5` (b117's engine, ad hoc) merged into `w131` (engine v0.7.4e, network version 28).

- Every mp5 fixup anchor still matched the newer engine.
- The trunk retired halo-ce-universal, so mp5's `Either(chupa=…)` wrappers became plain pairs and its `CHUPA` conditions went.
- `PSP_MULTIPLAYER=1` passes `-DPSP_MULTIPLAYER` again; the trunk had dropped it.
- **Why it matters:** the multiplayer build now carries every campaign speed-up through b131. Its heap is 4,008 KB before internet play (mp5's: 4,152).
- **Campaign check [D]:** the campaign EBOOT's code is byte-identical to w131's, except one string (mp5's crash screen names `ms0:/halo/crash.txt`), which shifts data offsets by 4 bytes.

## 1b. The network version moved to the pin's

`psp_limits.h` still announced version 24 and joined hosts of 11–24, though the PSP compiles v0.7.4e's netcode (version 28).

- **The effect:** the lobby hides listings outside the join range, so the PSP would have seen no current public game, and it would have refused today's servers.
- **The fix:** 28, joining 11–30, the pin's own numbers. Every other wire constant already matched the pin [D].
- **Campaign check [D]:** the campaign's code is unchanged.

## 2. The engine's p2p layer, compiled as it is

`psp_build.py`: `INTERNET_UNITS` adds `p2p.c`, `p2p_signal.c`, `p2p_lobby.c`, `p2p_crypto.c`, Monocypher and KCP to the multiplayer build, at `-Os`.

- **Why upstream's own code:** the desktop and the PSP then speak exactly the same lobby and tunnel protocol, and an engine-pin move brings their fixes along.
- `psp_libc.c`'s "no internet play" stubs stay for the campaign build only (`#ifndef PSP_MULTIPLAYER`).

## 3. `FIXES_INTERNET`: sized for a PSP that joins

As written, the p2p layer took 3.1 MB and left an 832 KB heap.

| Limit | Desktop | PSP | Why the PSP needs less |
|---|---:|---:|---|
| `MAXIMUM_STREAMS` (16 KB each) | 160 | 2 | One connection to its host |
| `MAXIMUM_PROXIES` | 512 | 16 | The host's 2–3 ports |
| `P2P_MAXIMUM_PEERS` | 127 | 7 | It tunnels to its host |
| Listeners, lobby games, tombstones, askers, used requests | 64 / 256 / 256 / 256 / 1024 | 8 / 64 / 64 / 16 / 64 | Host-side bookkeeping |
| `MAXIMUM_BROKERS` | 4 | 2 | Each is a TCP connection from the stack's one 256 KB pool |

- The 128 KB `proxy_by_port` table becomes a search of the 16 stand-ins.
- None of these change what goes on the wire.
- **Result:** the multiplayer heap is 3,728 KB, 280 KB less than before internet play. mp5's client peaked at 3,612 KB on Blood Gulch with 2 players [E]. **That margin is thin**; the emulator run's heap top settles it.

## 4. Joining, and the ONLINE GAMES item

- **The item:** `ONLINE_GAMES_UNITS` builds `ui_widget.c`, `ui_widget_event_handler_functions.c` and `cache_files.c` with `-DHALO_GAME_BROWSER`, and no other unit. That brings in the engine's own ONLINE GAMES item (a copy of System Link's in the Xbox Multiplayer menu) and its screen hooks, without the web list, stats or moderation.
- **The join:** a fixup exposes `network_game_client_join_invite_host` (`network_client_manager.c/.h`) to the PSP.
- **The description:** a fixup rewords ONLINE GAMES' text, since the PSP's list isn't the web game list.

## 5. `psp/src/psp_online.c`: the screen

**Why its own screen:** the desktop's `browser_screen.c` needs HTTPS and OpenGL fonts. This one reads `p2p_lobby_games()` and draws with `draw_quad` and the menus' fonts (`ui\small_ui`, `ui\large_ui`) in 640×480, as the virtual keyboard does.

- **`psp_join_blocker`** is the first form of the PSP rating. A game is joinable only if it is:
  - on one of the 13 stock maps,
  - with that map on this stick,
  - without a password,
  - open, and not already failed this run.

  Anything else is dimmed, with the reason shown.
- **`join_selected` / `wait_for_host`** follow `browser_screen.c`:
  1. Join the invite (`p2p_join_invite`).
  2. Wait for the host's advertisement through the tunnel.
  3. Join it.
  4. Open the pregame lobby.

  After 20 s the game is marked failed.
- **`psp_online_test_update`** (keep words `onlinelist`, `onlinetest`) is called from `network_test_update`, via a fixup. It opens the screen 5 s after the main menu, logs the list every 10 s, and with `onlinetest` joins the first joinable game after 30 s.

## 6. `psp/src/psp_internet.c` and `psp_net.c`

- **Random bytes:** the campaign's `posix_random_bytes` is a Mersenne Twister seeded from the 32-bit clock, so a key made from it could be found by trying every seed. The multiplayer build's version:
  1. Hashes the OpenPSID, the MAC, the time since boot and the real-time clock with BLAKE2b into a key.
  2. Takes ChaCha20's keystream under that key.
  3. Re-hashes the clock into the key on every call.

  The OpenPSID never goes on the wire, so the key can't be rebuilt from traffic. It is still not a hardware generator [U]; KIRK's is reachable only from kernel mode.
- **Discord:** stubbed.
- **`psp_net.c`:** `posix_resolve_ipv4` resolves names through `sceNetResolver`, started once (`pthread_once`), so the brokers and STUN servers can be names.
- **`psp_chupa_platform.c`:** `log_address_ipv4`, as upstream's.

## Open

- Heap headroom in a real game (section 3).
- Passwords: `p2p_listing_unlock` needs a text-entry prompt.
- Custom maps: the bake-on-demand plan in `multiplayer_plan.md`.
- `last_error` in `psp_net.c` is one variable shared by the p2p threads and the game thread. Fine while each reads it straight after its own call; worth a per-thread slot if errors ever look wrong.
