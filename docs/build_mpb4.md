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

**First suspect, ruled out [D, M]:** libcglue's `select` (`select.o`) asks the firmware one socket at a time, each in a set of which it clears only newlib's 64 bits while setting the socket's bit anywhere up to 256. A firmware socket number of 64 or more would be asked about wrongly. On the PSP-3000 the highest number asked about was 22–23 (38984376111), so libcglue's select was right, and replacing it changed nothing (below).

## Two replacements for select (keep words, both off by default)

| Keep word | What | Emulator [E] | PSP-3000 [M] |
|---|---|---|---|
| `inetpoll` | `sceNetInetPoll` over all the sockets | plays a local server; the emulator's poll ignores its timeout (the rest is waited out) | **hung the PSP** (38964850877; no log) |
| `inetselect` | libcglue's way, one socket at a time, each set 1,024 bits cleared whole | plays a local server (38975717470); selects take their timeout | dropped the same way (38984376111) |

## The select A/B [M] (sierra-hw 38984376111, PSP-3000, Beaver Creek in progress, 7 machines)

| Keep set | Tunnel's packets in the game | The stack's pool, 10 s apart (KB free) | Result |
|---|---|---|---|
| (none) | 58 → 118 → 163 | 216, 169, 124 of 255 | dropped |
| `inetselect` | 50 → 141 → 143 | 188, 148, 108 of 255 | dropped |
| `inetselect netpool512` | 201 → 207 | 475, 469, 438 of 511 | dropped |

The hotspot capture of the same runs (Sheepdog01's, minute by minute) had the host sending 745–1,125 packets in each game's minute, none of them fragmented: about 25–35 a second against the tunnel's 6 at best. Once each game ended, the host's keep-alives came through promptly (round trip ~50 ms) and the pool came back.

## The next suspect: the stack's own threads [D, U]

`psp_net.c` started the stack with `sceNetInit(pool, 0x20, …, 0x20, …)`: its timer thread and its network interrupt thread (what takes arrived packets from the Wi-Fi driver into the sockets) at **0x20, the game thread's own priority**.
- The PSP shares no time between threads of one priority: a thread runs until it waits [D].
- In a public game the game thread catches up for 0.5–2.5 s at a time without waiting (`budget:` lines: 15–32 ticks a frame), and the tunnel's thread (0x1f in a game) sits above the stack's.
- So arrived packets wait in the driver's queue (the pool draining) until it overflows, and sent ones leave whenever the game thread pauses (the capture's 5 s gaps, then 300 at once) [U].
- In the menus the game waits on every vertical blank, so the stack runs: lobbies, joins and keep-alives all work. That is also why a quiet local game plays.

**The change (`457b118`):** the stack's two threads start at 0x1d (`NET_THREAD_PRIORITY`): above the tunnel and the game thread, under the cache file thread (0x1c). Keep word `netprioN` (decimal; `netprio32` puts back 0x20). The start-up log names the stack's threads as the firmware has them. Emulator: plays a local server as before (sierra-netplay 38986865640, game time 4,292); it has no such threads to show.

## The stack-priority A/B [M] (sierra-hw 38987358496, PSP-3000, `netprio32 | -` twice)

The firmware's own list confirms the threads: `SceNetCallout 0x20, SceNetNetintr 0x20, SceNetApctl 0x42` before; `0x1d, 0x1d, 0x42` with the change.

| Run | Stack | Game | Tunnel's packets in the game | Pool (KB free) | Result |
|---|---|---|---|---|---|
| 1 | 0x20 | 13 players | 123, then the crash | 212 → 159 | **crashed** (below) |
| 2 | 0x1d | 13 players | 197 → 198 in 10 s | 169 → 172 | dropped |
| 3 | 0x20 | 6 players | (the host left before the map loaded) | | |
| 4 | 0x1d | 10 players | 150 → 186 in ~14 s | 179 → 217 → 204 | dropped |

**At 0x1d the pool no longer drains,** so what reaches the stack is read: the change is right and stays on. **The drop stays**, so the host's packets are lost before the stack, in the Wi-Fi driver or in the air.

**Run 1's crash [M, D]:** a bus error on the tunnel's thread in `ikcp_input`, storing at 0x18: `ikcp_malloc` returned NULL (the heap was full in a 13-player game), and KCP doesn't check. Every KCP allocation needs a check or a reserve of its own; open.

## Under the stack: netyield, the access point thread, the errnos [M] (sierra-hw 38989808944, 38992107950)

`netyield10` (`795a0c2`: the game thread waits 1 ms every 10 ms of ticks while joined) and `apctlprio29` (the access point thread at 0x1d, not 0x42):

| Run | Keep set | Game | Result |
|---|---|---|---|
| 38989808944 r1 | (none) | 14 players | tunnel 147 → 191 in 10 s; dropped |
| 38989808944 r2 | `netyield10` | 9 players | ended at the reliable endpoint (`_transport_error_endpoint_io`) as the game began |
| 38989808944 r3 | `apctlprio29` | 1 player | ended at the reliable endpoint **in the pregame**, the game idle at 30 fps |
| 38992107950 r1 | (none) | 4 players | rejected: `_rejection_code_version_too_old` (a server newer than 29) |
| 38992107950 r2 | `netyield10` | 16 players | ~10 packets a second read; dropped |

**The errnos [M, D]:** each of 38989808944's endpoint failures came just after a socket call failed with errno 2 or 0, which `psp_net.c` maps to `WSAEINVAL`. Logged each time with its call and thread (`b12a9f1`), errno 2 is the tunnel thread's `accept` once nothing more is waiting, every pass for half a second around the join; `p2p.c` just returns from it. errno 0 was once the game thread's `recvfrom`, before the join. Neither explains the drop. **Open:** what ends the reliable endpoint in the pregame.

**What the drop is [M]:** in 38992107950 run 2 the host's stream read `kcp receive 0+10`, `0+56`, `0+156` 10 s apart: no segment in order, a growing pile waiting behind one gap. The host was heard every ~70 ms all the while. KCP gives the game nothing past a missing segment, so the game hears silence and gives up.

**Why the gap never fills [U]:** the host resends the missing segment, and each resend is lost as the first was. If the host's large packets never reach the PSP, a full-size segment (KCP's 1,200 bytes, ~1,250 with the tunnel's header) is lost every time, while small packets (pings, acknowledgements, the pregame's messages) arrive. That fits everything so far: lobbies, joins and pregames work; the first large state update of a game stalls the stream for good.

## The packets by size [M] (sierra-hw 38993451550, PSP-3000; Sheepdog01's capture A for the hotspot's side)

| Size (UDP payload) | The hotspot sent (capture A, earlier runs) | The tunnel read (run 2, to the drop) |
|---|---|---|
| under 600 | 10,281 | 90 |
| 600–999 | 2,219 | 46 |
| 1,000–1,199 | 2,386 | 94 |
| 1,200–1,236 | 1,204 | 10 |

Full-size packets do reach the tunnel, but at about 1 in 7 of the mid-size ones' rate, where the hotspot sent them at half. The host's stream: `0+2`, `0+4`, `0+9`, `0+114` 10 s apart. Run 1 never got online: the hotspot refused the PSP's Wi-Fi join (the second time today), and nothing retried it (**open:** the join is retried only when a socket is asked for).

**What it points at [U]:** a weak or noisy radio link, where the longer a frame the likelier it is lost once the access point's own retries run out. Sheepdog01 saw the game connect for about 30 s and degrade.

## The next test

The access point's own count (`iw dev wlan0 station dump` every 5 s on the desktop: signal, rate, retries, failures) during two more runs, and where the PSP-3000 sits against the desktop's antenna and its USB 3 ports.
