# Halo: Combat Evolved on the PSP-1000: the first network game against a PC

**Milestone, October 10, 2026, 23:42–23:58 US Central.** A PSP-1000 played Halo CE against a Linux desktop: the PSP and the desktop's ChupathingyCE client both in a game on a ChupathingyCE dedicated server running on the desktop, over Wi-Fi. Two games of Blood Gulch, about 13 minutes together.

## What played
- **The PSP:** a stock-memory PSP-1000 on custom firmware (6.61, ARK-4), at 333 MHz, no overclock. It ran **mp3**, the port's multiplayer build: build 111's engine and renderer, plus the multiplayer work (`build_mp3.md`).
- **The desktop:** Linux, running ChupathingyCE 0.7.1d-dev (commit `b78e6cfa`, OpenCE network version 24): its dedicated server, and its client as the second player.
- **The network:** the desktop's own Wi-Fi card as a hotspot, WPA2 through ARK-4's `wpa2` setting (`psp_wifi_hotspot.md`; the setup: `multiplayer_setup.md`).
- **The map:** Blood Gulch, slayer, from the disc's maps rebuilt for the PSP. No game files are distributed.

## What it took (October 10)
Each was found on the PSP or in the emulator that day:
1. **The same tags on both sides.** The netcode names objects by their tag handles, so the PSP's rebuilt maps keep each tag's index and handle as the PC's (`multiplayer_plan.md`).
2. **Room for multiplayer.** A 6 MB tag window and the PC's player limits; the network modules take 574 KB on the PSP [M].
3. **Wi-Fi and sockets** for the PSP (`psp_net.c`), and the right saved network and ARK-4's WPA2 setting (`build_mp1.md`, Test 0).
4. **Packets the PSP's CPU can read.** The engine writes a packet's values at any byte offset and read them back whole; the PSP faults on that where a PC doesn't (mp1 → mp2; the emulator's strict interpreter found the second site).
5. **A network stack that can't run dry.** The engine asks 1 MB a socket; the PSP's stack has one small pool for every socket, and a long Memory Stick read let it empty (mp2 → mp3).

## The match, from the server's log [M]

| Time | |
|---|---|
| 23:42:14 | The PSP joins the server's lobby; the game starts at 23:42:46 and every machine has loaded 4 s later |
| 23:43:47 | The desktop's client joins the game in progress (tick 1,713) |
| 23:50:09 | Game 1 ends: no score in 5 minutes (the server's rule); both machines go back to the lobby together |
| 23:51:09 | Game 2: both loaded in 2 s |
| 23:58:01 | Game 2 ends: the server timed the PSP out, at tick 12,417 |

- The PSP's log of the match was overwritten by the next launch, so the PSP's side (frame rate, the stack's pool) isn't known. In mp2's game: 30 ticks a second, 14–20 fps on Blood Gulch, heap top 3,536 of 4,352 KB [M].
- Joining the hotspot: 4.5–8.3 s from the game's start [M].

**Open from it:**
- **Game 2 ended with the PSP timed out** after about 7 minutes. Without the PSP's log, why is open; mp3 logs the stack's pool and the access point every heartbeat for the next one.
- **The server refused the PSP's predictions** four times in game 1: "machine #1's game runs 1.12–1.31 times as fast as this host's (16–22 ticks ahead)". The PSP's game clock ran ahead of the server's at times, perhaps catching up after long frames [U].
- **After the match the server announced a newer network number** (25), which the PSP refuses; the desktop client had handed it ChupathingyCE's signed legacy table (`build_mp3.md`).

## The first whole game (October 11, 00:23–00:34) [M]

An hour later, with the server's network number pinned: slayer to 15 on Blood Gulch, played to the end, the PSP's player winning 15–0 against the desktop's. Eleven minutes, 30 ticks a second, ~16 fps, the stack's pool never below 224 of 255 KB free (`build_mp3.md`).

## What's next
- The PSP follows ChupathingyCE's legacy table, so it can join hosts that announce a newer number (`build_mp3.md`).
- The 16-player test (`build_mp1.md`, Test 3), and the frame rate the network costs: b112's speed-ups aren't in this build yet.
