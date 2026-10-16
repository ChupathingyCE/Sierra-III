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

**Ruled out:** falling behind the host (it drops when it keeps up too); the tunnel thread's priority; streams running out of room, closing or dying; WLAN power save on the 3000; the desktop's firewall (ufw and NetworkManager's hotspot table pass the flow; nothing logged).

## The capture that found it [M] (sierra-hw 38958361231, Sheepdog01's `tcpdump` on the hotspot)

| On the hotspot, 14:56:47–14:57:11 | |
|---|---|
| The host's packets to the PSP | 50–80 a second, 1,442 in all, all to the tunnel's port (61839) |
| Read by the PSP's tunnel (its log) | about 190 |
| The PSP's packets out | in bursts (37, 25, 27, 42, 20) between up to 5 s of none; then 300 at once after the host had stopped |

**The packets reached the PSP, and its tunnel didn't read them**, in both directions. That is also why the stack's pool filled ~8 KB a second, and why round trips swung to seconds.

**The likely reason [D, U]:** the tunnel's thread asks select about 10–25 sockets at once. libcglue's `select` (`select.o`) asks the firmware one socket at a time, each in a set of which it clears only newlib's 64 bits while setting the socket's bit anywhere up to 256 (from bits 5–7 of the firmware's number). For a firmware number of 64 or more, the bit lands in stale words and in libcglue's own result sets, and the firmware is asked about the wrong sockets. The game alone (one socket a call) plays a local server well.

## Two replacements for select (keep words, both off by default)

| Keep word | What | Emulator [E] | PSP-3000 [M] |
|---|---|---|---|
| `inetpoll` | `sceNetInetPoll` over all the sockets | plays a local server; the emulator's poll ignores its timeout (the rest is waited out) | **hung the PSP** (38964850877; no log) |
| `inetselect` | libcglue's way, one socket at a time, each set 1,024 bits cleared whole | plays a local server (38975717470); selects take their timeout | not yet tried |

The select report now gives the highest firmware socket number asked about. If it reads 64 or more on a PSP, the theory holds.

## The next tests (both PSPs need hands first)

1. **Recover both PSPs** (hold POWER, switch on, start PSP RIG AGENT, replug USB), then `psp-probe recover=` for 38964850877's log.
2. **The PSP-3000, `keepsets="- | inetselect"`**, with the hotspot capture running: the firmware numbers, and whether the tunnel keeps up.
