# Review: mpb4, the multiplayer work on b135 (branch `mp135`)

What changed since mpb3 (`7f0dc04`, on w131), in the order to read it. mpb4 is `mp134` (b134 + the multiplayer work) moved onto b135 (ChupathingyCE `v0.7.5d`), plus one fix from the PSP-1000's first public game (`milestone_first_public_game.md`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

## 1. Onto b135

`mp134`'s own changes (`w134..mp134`) applied onto `w135` as one three-way patch.
- **Conflicts:** two. `psp_sdl.c` takes both includes (b135's `strings.h` for `SDL_strncasecmp`, the multiplayer build's `psp_paths.h`). The fixups' registry line is b135's, with `FIXES_MULTIPLAYER`, `FIXES_PACKET_ALIGNMENT` and `FIXES_INTERNET` after `FIXES_CHUPA` as before.
- **Anchors:** every multiplayer fixup still matches `v0.7.5d`.
- **The campaign [D]:** built from `mp135` and from b135's `5950c5e` (no profile, same engine), the code is the same instruction for instruction. The one difference is mp5's crash-screen string (`ms0:/halo/crash.txt`), which moves later data by 4 bytes, as in every multiplayer branch since mp5.

## 2. What the new engine needed (`FIXES_INTERNET`, `psp_build.py`, `psp_limits.h`)

- **Network version 29**, the pin's, joining 11–30 (`psp_limits.h`). Today's public servers announce 29.
- **`p2p_copies.c`** joins `INTERNET_UNITS`. Upstream's lobby now shows a game heard under several network versions once, at the newest (`p2p_listing_older_copy`). The ONLINE GAMES screen's own pass over the list (mpb3, change 1) goes.
- **`#joinedhost`:** `network_game_client_joined_host_identifier` stays inside `HALO_GAME_BROWSER`. Its record of the joined host is written only by the web game list (to join again after a map download), and the PSP's `#invite` fixup had brought the function into the PSP's build without it.
- **`#downloaded`:** the downloaded Custom Edition maps' list goes from 512 names to 8 (`map_families.c`). The PSP downloads none, and the list and its text copy took 64 KB of heap.
- **Heap:** 3,724 KB (mpb3: 3,744).
- **Stubbed:** `map_download_get`, `map_download_start`, `map_family_find`, `browser_screen_open_map_offer`, `text_hires_enabled`. Each caller reads 0 as "no" [D]: Custom Edition maps aren't found, no download starts, no map offer shows, no high-res text.

## 3. The tunnel's thread above the game thread, from a join (`#priority`, `psp_internet.c`, `psp_online.c`)

**Why:** in the first public game (sierra-hw 38918999662) the host's messages stopped within about 10 s of the join.
- Internet play does all its network work on one thread (`p2p.c`). It starts at priority 0x5c [E], far below the game thread's 0x20.
- The PSP's scheduler runs a thread only while every higher one waits [D].

**The change:** the thread records itself as it starts. The screen raises it to 0x1f (above the game thread, below the vertical blank's 0x1e) when a join starts, and lowers it again when the screen opens.
- **Not from the start [M]:** raised from the start (38938090605, 38939812478 run 1), it starved the menus' start-up thread (profiles and playlists, slow on the stick), and the menus never ran the screen.
- **What it changed [M]:** in a join the thread passes through its loop ~100 times a second, where it managed ~5 at 0x5c (38950938673, 38947199244).
- **What it didn't:** the drop. Raised, the PSP was still dropped (38941941105 run 1, 38950938673).

**Keep word:** `tunnelslow` keeps it at 0x5c.

## 4. Internet play started again after a failed join (`psp_net.c`, `xnet.c` `#retry`)

The hotspot refused the PSP-1000's first Wi-Fi join once (38936073185). Internet play's socket failed with it at `WSAStartup`, and it stayed off while the next join worked. `psp_net.c` now notes any failed join, and the next one that succeeds calls `p2p_initialize` again, which does nothing once internet play runs.

## 5. Diagnostics (`psp_online.c`, `p2p.c` `#tunnelreport`, `psp_net.c`)

Every 10 s the hands-free test logs:
- **The screen:** open or closed, how often processed, its games.
- **The tunnel:**
  - the thread's loop passes and its priority;
  - the packets read, and which were turned away (not the tunnel's, no peer, stale, unsealed);
  - streams refused, closed on the PSP's side (with the socket error) or dead;
  - each peer's last packet and round trip;
  - each stream's queues.
- **Select:** calls, time, the longest call, overruns, the highest descriptor.

Also:
- Unknown socket errors are logged each the first time.
- WLAN power save is logged at the join.
- **`tunnelpoll`** reads every tunnel socket each pass, ready or not. As an A/B it ruled out select missing ready sockets.

## 6. The stack's own threads above the game thread (`psp_net.c`, `457b118`)

**Why:** `sceNetInit` started the stack's timer thread and its network interrupt thread (what moves arrived packets from the Wi-Fi driver into the sockets) at 0x20, the game thread's own priority. The PSP shares no time between equal priorities [D], and in a public game the game thread catches up for 0.5–2.5 s without waiting, so the stack's pool drained in every game [M].
- **The change:** `NET_THREAD_PRIORITY` 0x1d, through `psp_net_stack_priority()`, for both `sceNetInit` calls (internet and ad hoc): above the tunnel (0x1f in a game) and the game thread, under the cache file thread (0x1c).
- **Keep word:** `netprioN` (decimal; `netprio32` puts back 0x20). The start-up log names the stack's threads as the firmware has them (`psp_net_log_threads`, `sceKernelGetThreadmanIdList`).
- **What it changed [M]:** the pool no longer drains in a game (38987358496). It didn't stop the drop.

## 7. Test switches and diagnostics (`795a0c2`, `b12a9f1`, `1073e50`)

Off unless named; none changes play.
- **`netyieldN`** (`psp_net_tick`, called after each tick from `psp_tick_account` in the multiplayer build only): while joined, the game thread waits 1 ms every N ms of ticks. `netyield10` didn't help [M].
- **`apctlprioN`:** the access point thread's priority (0x42 otherwise).
- **Unknown errnos:** `fail()` and `succeed()` became macros over `fail_in` and `succeed_in` that carry the calling function's name (`__func__`), and every unknown errno is logged with the call and the thread, the first 32. It named errno 2 as the tunnel's `accept` with nothing waiting (harmless).
- **The tunnel's packets by size** (`#tunnelreport`: `psp_streams.sizes`, `psp_tunnel_sizes`), in tcpdump's units, on a log line of their own every 10 s.

## 8. The Wi-Fi driver gets time while online (`psp_net.c`, `dfa082e` and its follow-up)

**Why:** the Wi-Fi driver's threads (`SceWlanMac`, `SceWlanHal`) sit at 0x27, under the game thread's 0x20, and the PSP gives no time to a lower priority while a higher one runs. A heavy public game's catch-up ticks run 100–500 ms without waiting, the driver's receive buffer overflows, and acknowledged packets are lost: the drop (`build_mpb4.md`).
- **The change:** `pace_thread`, at 0x18, sleeps N ms, then (only while `wifi.connected`) moves the game thread to 0x70 for 1 ms and back. Its start is in `psp_net_tick`, the first tick on a network, which reads the game thread's own priority to restore.
- **Keep word:** `netpaceN`; on by default at 5 (`NET_PACE_DEFAULT_MS`), `netpace0` off.
- **What it changed [M]:** a public game played to the wall with the host's stream never behind (39009339059 run 2), where every run without it dropped within ~30 s.
- **Also in `dfa082e`:** every thread the firmware lists, with its priority, logged once on a network (`log_all_threads`).
- **Hot code:** none. `psp_net.c` is compiled only into the multiplayer build, and its per-tick call is inside `#ifdef PSP_MULTIPLAYER`.

## 9. The launch menu's own check for the multiplayer build removed (`psp_menu.c`)

The front door (b136) decides the first screen for both builds, so `#ifdef PSP_MULTIPLAYER` / `menu.txt` in `psp_menu()` became redundant (`review_frontdoor.md`). Until the front door is merged, `mp135` alone shows the launch menu at start unless `nomenu.txt` or `input.txt` is on the stick.

## Open

- **`_transport_error_endpoint_io`** on the client's reliable endpoint (three times; once with a healthy stream).
- netpace on the PSP-1000, and its cost in game time.
- **KCP and a full heap:** `ikcp_malloc`'s NULL is written through (a crash in a 13-player game). Its allocations need a check or a reserve.
- **A refused Wi-Fi join** isn't tried again until a socket is asked for.
- The tick in a 12-player game, and why 30 biped updates a tick.
- **Heap:** the top reached 3,716 of 3,720 KB in a join on the PSP-3000 (38950938673).
