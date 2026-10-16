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

## Open

- **The drop** (`build_mpb4.md`): the host's packets stop reaching the tunnel within seconds of the game starting. Next: a public game hosted on Sheepdog01's own network.
- The tick in a 12-player game, and why 30 biped updates a tick.
- **Heap:** the top reached 3,716 of 3,720 KB in a join on the PSP-3000 (38950938673).
