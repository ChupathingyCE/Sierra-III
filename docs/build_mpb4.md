# mpb4: the multiplayer build on b135 (2026-10-16)

The ONLINE GAMES build moved onto b135 (ChupathingyCE `v0.7.5d`, network version 29), plus what the first public games taught (`milestone_first_public_game.md`). It lists and joins public games on the PSP-1000 and the PSP-3000. **Every public game it joins still drops it within about 30 s**; this note records what has been ruled out, and the next test.

## The build

| | |
|---|---|
| Name | `mpb4` |
| Source | Sierra-III `mp135` (b135 + the multiplayer work; latest at the time of writing, with the diagnostics below); `PSP_MULTIPLAYER=1`, no PGO |
| Engine | ChupathingyCE `v0.7.5d`, network version 29, joining 11–30 |
| Heap | 3,720 KB |
| Stubbed | `map_download_get`, `map_download_start`, `map_family_find`, `browser_screen_open_map_offer`, `text_hires_enabled` (`review_mpb4.md` §2) |

## What changed since mpb3 (`review_mpb4.md`)

1. **b135's engine:** mp134's changes applied onto `w135`; the campaign's code is unchanged but for mp5's crash-screen string [D]. Network version 29.
2. **Upstream's one-row-per-game** (`p2p_copies.c`) in place of ours.
3. **The downloaded-maps list** from 512 names to 8: 64 KB of heap back.
4. **Internet play starts again after a failed Wi-Fi join.** On the PSP-1000 the hotspot refused the first join once, and internet play stayed off all run.
5. **The tunnel's thread at 0x1f from the moment a join starts,** at 0x5c in the menus (`tunnelslow` keeps it low). Raised from the start, it starved the menus' start-up thread, and the menus never ran the screen.
6. **Diagnostics** in the hands-free test's log, every 10 s: the screen's state, the tunnel's peers and streams, its packets and why any were turned away, select's timing, the thread's priority. Unknown socket errors logged each the first time. WLAN power save logged at the join. `tunnelpoll` reads every tunnel socket each pass.

## The public games [M]

| Run | PSP | Game | Result |
|---|---|---|---|
| 38936073185 | 1000 | (none) | Hotspot refused the first join; internet play off (fixed: 4) |
| 38938090605 | 1000 | (none) | Thread at 0x1f from the start: the menus never ran the screen (fixed: 5) |
| 38939812478 r2 | 1000 | Wizard, 7 players, in progress | **Kept up** (30 ticks/s, 22 ms a tick, ~7 fps); dropped after ~30 s |
| 38941941105 r1 | 1000 | Beaver Creek, 10 players, at its start | Kept up (29.6 ticks/s, 30 ms a tick, ~1 fps); dropped after ~25 s. Run 2 hung the PSP |
| 38945184202 | 3000 | Longest, in progress | Fell behind; host last heard 4.2 s before the drop |
| 38947199244 | 3000 | Longest | Thread at 0x5c: ~5 passes a second; the join failed |
| 38950938673 | 3000 | Hang 'Em High, in progress | ~100 passes a second; no packet of the host's for 10 s; the stack's pool filling ~8 KB/s |
| 38952663446 | 3000 | Rat Race | `tunnelpoll` on: the join failed, round trips up to 4 s. Off: joined, dropped |
| 38954892605 | 3000 | (menu) | WLAN power save off |

**Ruled out:** falling behind the host (it drops when it keeps up too); the tunnel thread's priority; select (on time, descriptors ≤12, reading every socket each pass changes nothing); streams running out of room, closing or dying; WLAN power save on the 3000.

**What remains:** within seconds of the game starting, the host's packets stop reaching the tunnel, and round trips swing from 50 ms to 4 s. That points at the path, the hotspot's and the routers' NATs or queues, or at the host [U].

## The next test (needs Sheepdog01's desktop)

A public game on the same network as the PSP. On Sheepdog01's machine the desktop's ChupathingyCE dedicated server hosts publicly with internet play on, and the PSP joins it from ONLINE GAMES through the tunnel, over the hotspot.
- **If it plays for minutes:** the PSP's tunnel works, and the internet path is the problem.
- **If it drops the same way:** the problem is on the PSP's side.
