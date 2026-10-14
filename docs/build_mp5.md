# mp5: build 117's engine, and ad hoc play PSP to PSP (2026-10-13)

mp4's multiplayer on b117's engine (b112–b117's speed-ups), plus a second way to play: **ad hoc**, PSP to PSP with no access point and no PC. The access point path to ChupathingyCE servers is unchanged and stays the main one (`multiplayer_plan.md`: ChupathingyCE compatibility first).

## The build

| | |
|---|---|
| Name | `mp5` |
| Source | Sierra-III `mp5` `090ee05`, `PSP_MULTIPLAYER=1`, ChupathingyCE base, no PGO (b117's profile is the campaign's) |
| EBOOT | sha256 `686cf51f774458fe…`; `halo_psp_mp5.zip` (unzips to `PSP/GAME/halomp/`, over mp4) |
| Kernel modules | mp1's (unchanged) |
| Heap | 4,152 KB (mp4: 4,352; b112–b117 took the difference) |

## What changed from mp4

1. **b117 merged in** (the `mp5` branch: the trunk `mp-recon` plus `b117`). Seven files conflicted; every one kept both sides.
2. **Ad hoc, keep word `adhoc`** (`psp/src/psp_net_adhoc.c`). The game's sockets go to the firmware's ad hoc sockets instead of the IP stack: UDP as PDP, the game's TCP connections as PTP. Every PSP joins ad hoc group `SIERRA3` (product code `SIER00003`) and gets a made-up address, 169.254.x.y from its MAC's last two bytes. Broadcasts work in ad hoc, so System Link finds games with no address lists. Without the keep word, nothing differs from mp4.
3. **The loopback.** A host plays through a client of its own, which joins it at 127.0.0.1, and ad hoc can't reach a PSP's own sockets: that traffic stays in memory (94 KB, outside the heap when there's room, only in ad hoc mode).
4. **A PSP can host.** The host's message handler counted machine slots to 128 while the host keeps 16 (`psp_fixups.py`, `network_server_message_handler.c#host`): the first message to every machine read slot 16 through NULL. Fixed. Nothing on the wire changes.
5. The crash screen names the build's own folder (`ms0:/halomp/crash.txt`).

## In the emulator [E] (the lab's `sierra-netplay`)

| Run | What | Result |
|---|---|---|
| 38347702719 | PSP joins a ChupathingyCE server (the compatibility check) | joined, 7,630 ticks at 30 a second, heap top 3,612 KB |
| 38350876668 | two PSPs over ad hoc, one hosting and playing | 5,735 ticks at 30 a second, no fault; host heap top 4,024 KB |
| 38347529254 | two PSPs over IP, one hosting | the host's emulator stops after accepting its own client: open, emulator side or not [U] |

- **Hosting costs about 2.5 times a client's frame** in the emulator (24 ms against 9.5 ms on Blood Gulch, two players). Where the time goes is the emulator's to say; how long it takes is the PSP's.
- PPSSPP fakes ad hoc over sockets, with a directory server (built into one emulator: `PPSSPP_ADHOC_SERVER`, the lab's emulator patch). A real PSP has no directory: the radio does it.

## On hardware: not yet

Multiplayer hardware time is Sheepdog01's to schedule (2026-10-13), PSP-1000 against the lab's PSP-3000. The questions only the PSPs answer:
1. Two consoles find each other and play over ad hoc.
2. How long a host's frame takes, against a client's.
3. The ad hoc modules' RAM (the IP ones took 574 KB).

## Install (two PSPs)

On each stick, as mp4 (`build_mp4.md`), plus the keep word:

```fish
pspmount
unzip -o ~/Downloads/halo_psp_mp5.zip -d $psp                 # PSP/GAME/halomp
echo "nearauto nomeaudio adhoc" > $psp/halomp/keep.txt
umount $psp
```

- No hotspot, server or `broadcast` setting is needed for ad hoc (a `broadcast` left in `config.toml` is sent to every PSP in the group instead).
- Both PSPs on the same Ad Hoc channel (Settings → Network Settings → Ad Hoc Mode; Automatic is fine on both). WLAN switch on.
- One PSP: Multiplayer → System Link → create a game. The other: System Link → join it. Hands-free: `network_test = "host:bloodgulch"` on one, `"join"` on the other (`[debug]` in `halomp/config.toml`).
- Logs as mp4's, per PSP (no server log in ad hoc).
