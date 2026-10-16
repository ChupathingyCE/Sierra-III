# Halo: Combat Evolved on the PSP-1000: the first public internet game

**Milestone, October 16, 2026, about 09:52 US Central.** A PSP-1000 found a public Halo CE game on the internet from its own server list, tunnelled to it and joined it: HALO UNIVERSAL, a ChupathingyCE dedicated server, with 11 players already on Derelict (`carousel`). Its player spawned, and it played about 20 seconds before it fell too far behind the server and was dropped.

The first network game (`milestone_first_netgame.md`, October 10) was on Sheepdog01's own server, over his own hotspot. This one was a stranger's server, found through ChupathingyCE's public lobby and reached across the internet. Nothing about it was set up for the PSP.

## What played
- **The PSP:** the lab's PSP-1000 (custom firmware 6.61, ARK-4), at 333 MHz, no overclock, run by the rig hands-free (lab `sierra-hw` 38918999662). It ran **mpb3**, the ONLINE GAMES build: b131's engine and renderer, ChupathingyCE v0.7.4e's own internet play, and the PSP's own server-list screen (`build_mpb3.md`).
- **The network:** Sheepdog01's desktop hotspot, then the internet.
- **The host:** HALO UNIVERSAL, a dedicated server on ChupathingyCE network version 29 (the PSP speaks 28 and joins 11–30).
- **The maps:** the disc's maps, rebuilt for the PSP. No game files are distributed.

## What happened, from the PSP's log [M]

Times are from the launch, from the log's 5-second heartbeats (±5 s).

| Time | |
|---|---|
| 0:15 → 1:05 | Joins the hotspot (WPA2) and gets an address |
| 1:40 | Internet play starts: the two signalling brokers (`opence.milenko.org`, `broker.emqx.io`) |
| 1:45 | **The PSP's clock reads 1 January 1970.** Three hosts' listings agreed on the time, so the PSP set its offset from them (+1,792,162,321 s) and read the listings' times with it |
| 2:45 | Both brokers ready, none failed; 411 listings heard, **44 public games** shown |
| 2:45 | STUN gives the PSP's public address (Google's and Cloudflare's agree); the hands-free join picks the first joinable dedicated server, HALO UNIVERSAL |
| 2:50 | **The tunnel connects** to the host; the host's game shows up as a System Link game through it; "joining a host of network version 29"; **"successfully joined a net game; our machine is #8"** |
| 2:50 | Carousel loads (geometry, 466 textures, stencils from the bake); **"joined the game in progress at game tick #15317"**; the player spawns |
| 3:05 | "network client connection has been silent for a dangerously long amount of time" |
| 3:10 | "the game host went down": the client gives up; back to the main menu, the list still up (44 games) |

## What it took
Each found and fixed between October 15 and 16, most of it in the emulator first:
1. **ChupathingyCE's own internet play compiled for the PSP** (lobby, signalling, tunnel, Monocypher, KCP), sized for a PSP that only joins: from 3.1 MB of buffers to 280 KB (`review_mpb1.md` §3).
2. **The right network version.** The PSP still announced 24 and saw no current game; now 28, joining 11–30.
3. **Keys a PSP can make safely:** random bytes from BLAKE2b and ChaCha20 over the PSP's OpenPSID, MAC and clocks, not the game's 32-bit-seeded generator.
4. **A clock it can trust.** Listings are dropped when their time is far from the reader's. The PSP's clock was 56 years off; its offset now comes from three hosts that agree, so one host with a wrong clock can't move it (mpb3, after the emulator caught one 3 hours off).
5. **Names and sockets:** the firmware's resolver for the brokers' and STUN servers' names; each thread's last socket error kept apart.
6. **One row per game,** and a map check that asks the firmware (the engine's file layer found none of the maps).
7. **The lab:** an emulator variant that lets connections leave the machine (`halo-net`), a lobby check, and hardware runs of the multiplayer build (`data=halomp`).

## Why it was dropped [M, with the reasons labelled]

| After the join (4 heartbeats) | |
|---|---|
| Tick | 43 ms average (60, 43, 35, 36). The host runs 30 ticks a second: 33.3 ms for the whole frame |
| Game speed | 16–28 ticks a second against the host's 30: the PSP fell further behind every second |
| Frames | 0.4–1.0 a second, each running 27–39 ticks to catch up |
| The tick | bipeds 21 ms (30 biped updates a tick with 12 players), weapons on the ground 12 ms (49 a tick, 42 asleep) |
| Heap top | 3,612 of 3,744 KB |

- **The host's reliable messages stopped soon after the join.** The client warns at 5 s without them and gives up at 15 s, so the last came within about 10 s of joining [D: `network_connection_idle`].
- **Most likely why [U]:** the tunnel runs on a thread of its own (`p2p.c`), made with pspdev's default pthread priority, 0x3c [D: `pte_osThreadGetDefaultPriority`], below the game thread's 0x20. The PSP's scheduler runs a lower-priority thread only while every higher one waits, and the game thread runs its catch-up ticks for one to two and a half seconds at a time without waiting. So the tunnel likely got no time and couldn't answer the host. This is the next build's first fix: the tunnel's thread above the game thread, the way the cache file thread is already raised (`readerslow`).
- **Underneath it, the tick:** even with the tunnel served, a 12-player game costs more than 33 ms a tick on the PSP today. Every campaign tick speed-up shortens it too.
- **Not the cause:** the run's one failed heap request was the GE's 1,056 KB vertex-copies arena. It fell back to the general heap, as built.

## What's next
- **mpb4:** the tunnel's thread above the game thread, then the same join again.
- **Why 30 biped updates a tick with 12 players** (the emulator, against a local server): if the client updates remote players' bodies more than once a tick, that is a multiplayer-only lever.
- **The rebase onto b135** (ChupathingyCE 0.7.5d, network version 29), when the master session lands it.
- **ONLINE GAMES in upstream's Xbox-menus look,** with the PSP's buttons.

The log: lab results `sierra-hw/38918999662/log.txt`; Sheepdog01's copy `mpb3_first_public_game.tgz`.
