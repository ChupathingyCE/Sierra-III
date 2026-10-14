# mp3: mp2 with a network stack that can't run dry (2026-10-10)

mp2 joined the server on hardware and played, then a few seconds in nothing was sent or received again (`build_mp2.md`, Test 1). mp3 is mp2 with the stack's buffers bounded. Its tests are mp1's (`build_mp1.md`, from Test 1).

## The build

| | |
|---|---|
| Name | `mp3` |
| Source | Sierra-III `mp-recon` `33eee4b`, `PSP_MULTIPLAYER=1`, ChupathingyCE base |
| EBOOT | sha256 `825030c8b847fdb0…`; `halo_psp_mp3.zip` (unzips to `PSP/GAME/halomp/`, over mp2) |
| Kernel modules | mp1's (unchanged) |

**What changed from mp2** (`psp/src/psp_net.c`):
1. **The stack's pool**, sceNetInit's: 256 KB (was 128, pspsdk's sample size). Keep word `netpoolN` (KB, 64 to 1024). It comes out of the RAM outside the heap: in mp2's play, ~900 KB of it was free.
2. **A socket's buffers**: at most 32 KB, where the engine asks 1 MB. A full socket drops what arrives (UDP may) instead of holding it in the shared pool. Keep word `netbufN` (KB; `netbuf0`: as the engine asks, mp2's behaviour).
3. **A network line every heartbeat**, once joined: `network: the stack's pool F of P KB free (M KB at most in use)`, and the access point's state when it changes. A game that goes quiet shows which it was.

**In the emulator** [E] (`sierra-netplay` 38112037574, the JIT): joined, played 5,863 ticks; the cap logged once (`32 KB, not the 1024 KB asked`). The emulator's sockets are the desktop's, so its pool figures aren't the PSP's, and it can't show the fix itself.

## Install and test

```fish
pspmount
unzip -o ~/Downloads/halo_psp_mp3.zip -d $psp     # replaces PSP/GAME/halomp
umount $psp
```

Then `build_mp1.md`'s Test 1, as for mp2 (stick in test mode, server with `HALO_NET_BROADCAST=<device-address>,$lan`). Look for:
- `network: the stack's pool …` lines: how low the free figure goes, above all after a long frame;
- `received` in the `network test: tick` lines going on rising;
- the server's `debug.txt` without "timed out".

## On hardware (2026-10-10/11)

**Milestone: the first match between a PSP and a Linux desktop**, 23:42–23:58: mp3 and the desktop's ChupathingyCE client in two games of Blood Gulch on the dedicated server there, over the hotspot (`milestone_first_netgame.md`, from the server's log). Game 1 ended on the server's no-score rule; game 2 when the server timed the PSP out, about 7 minutes in. The PSP's log was overwritten by the next launch.

**The next try couldn't join** [M]: `not joining a host of network version 25 (this machine's is 24)`, shown in the menu as "net game handler failed". ChupathingyCE's machines pass a signed legacy table between them over Delta Peer; the current one (serial 5, 2026-10-10) has wire `chupa-24a` announce 25 and join 11–25. The desktop client had it and handed it to the server during the match; the server cached it and announced 25 from then on. The PSP's numbers are built in (24, 11 to 24; `psp_chupa_platform.c`), so it turned the host down. The scripted join retried every few seconds and held the main menu meanwhile (a minute or two of music and no animation).

- **For the tests:** the server announces 24 from a local table (`HALO_LEGACY_TABLE`, `build_mp1.md`).
- **For mp4:** the PSP reads a table file too (`network.legacy_table`, the PCs' setting), so it can follow the table the PCs use, until it has Delta Peer of its own.

## A whole game (2026-10-11, 00:22–00:35) [M]

With the server announcing 24 (`HALO_LEGACY_TABLE`), mp3 joined at 00:22:52, the desktop client joined the game in progress, and they played **slayer to 15 on Blood Gulch to the end**: the PSP's player 15 kills, 0 deaths (00:23:29–00:34:29, 11 minutes). Then both went back to the lobby; Sheepdog01 quit. The log came back whole (`mp3_test3.tgz`, with the server's `debug.txt`).

| | |
|---|---|
| Ticks | 30 a second on most heartbeats (21–40 at the extremes) |
| Frames | 15.9 fps on average over the heartbeats |
| The stack's pool | never below 224 of 255 KB free; 84 KB at most in use |
| Messages | 39,132 sent, 55,155 received, 93 corrections, 75 hits reported |
| Heap top | 4,092 of 4,352 KB; user RAM outside the heap at least 754 KB free |
| Worst frame | 3,050 ms reading textures (1,491 KB/s) |

**Open: the PSP falls behind, then rushes** [M, the cause U]. Nine times the PSP found "the host's clock is 32–40 ticks ahead of this machine's" and jumped to the host's tick; fifteen times the server refused the PSP's predictions, its game running 1.10–1.73 times as fast and up to 55 ticks ahead. The PSP's long frames (Memory Stick reads, 16 fps) leave it behind; catching up, it runs ticks faster than the server takes. What it costs in play (corrections the player sees) is Sheepdog01's to say; the fix is frame time, and b112's speed-ups aren't in this build.
