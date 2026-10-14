# mp2: mp1 with network packets the PSP can read (2026-10-10)

mp1's Test 1 on hardware stopped at the PSP's first search for games: a packet's value at an odd address, which the PSP's CPU can't load whole (`build_mp1.md`). mp2 is mp1 with that fixed, and with the one more found by running the network test on the emulator's strict interpreter. Its tests are mp1's (`build_mp1.md`, from Test 1).

## The build

| | |
|---|---|
| Name | `mp2` |
| Source | Sierra-III `mp-recon` `29b2c87`, `PSP_MULTIPLAYER=1`, ChupathingyCE base |
| EBOOT | sha256 `135fc7c8fbd51963…`; `halo_psp_mp2.zip` (unzips to `PSP/GAME/halomp/`, over mp1) |
| Kernel modules | `halo_crash.prx`, `halo_profile.prx`: mp1's (their sources haven't changed) |

**What changed from mp1** (`psp_fixups.py`; neither changes what goes on the wire):
1. **Packet values** (`FIXES_PACKET_ALIGNMENT`): the engine writes a packet's values one after another into a byte buffer, swaps each value's bytes in place, and reads values back by loading them whole. The bytes now move one at a time (`psp_reverse_bytes`), and values are read with unaligned-safe loads (`PSP_UNALIGNED_SHORT`, `_ULONG`, `_INT64`, `psp_limits.h`).
2. **ChupathingyCE's batched messages** (`FIXES_MULTIPLAYER`, `network_distributed.c`): a tick's messages to one machine go in one datagram, whose bytes started 2 past a multiple of 4, and whose header was written through a pointer. Those bytes are now 8-aligned, and a received message that isn't 8-aligned is read from an aligned copy (some handlers read entries through structure pointers).

## Proved in the emulator [E]

The lab's `sierra-netplay` with `strict=1` (the interpreter, which reports each unaligned load or store the JIT forgives; the run now stops at the first one):

| Run | Source | Result |
|---|---|---|
| 38108362569 | the packet fix | joined; stopped at the first batch: a 4-byte store at `0x08cf82c2` from `distributed_batch_flush` |
| 38109908076 | both fixes (`29b2c87`) | joined, loaded Blood Gulch, played 1,688 ticks; **0 strict findings**, no fault |

Not covered: the player stood at the spawn, so shooting, damage, deaths, pickups and vehicles sent nothing. Those messages are read by the same code as the rest (entries copied out, or read from the aligned message), but they haven't run under the strict interpreter. The network test can drive them (`debug.network_test_shoot`, `_kill`, `_pickup`, `_vehicle`): a strict run with those is the next sweep.

Also found by a scan with GCC's `-Wcast-align=strict` on the network code: `message_encryption.c`'s TEA would load 4 bytes 2 past a multiple of 4, but nothing calls it. The other casts it lists are into the engine's data arrays, which are aligned.

## Install

The stick stays in mp1's test mode (`build_mp1.md`, D: no launch menu, no map named, the scripted join in `config.toml`):

```fish
pspmount
unzip -o ~/Downloads/halo_psp_mp2.zip -d $psp     # replaces PSP/GAME/halomp
umount $psp
```

Then mp1's Test 1, with the server started as `build_mp1.md` gives it (its broadcast list names the PSP's address, <device-address>).

## Test 1 on hardware (2026-10-10): joined and played, then went quiet

Two tries, both mp2 [M]:
1. **23:23** (the PSP named "King"): joined; the server dropped it 60 s into loading Blood Gulch, "timeout while loading for game". Its PSP log was overwritten by the second try.
2. **23:26**: joined, loaded in 4 s, played at 30 ticks a second while Sheepdog01 moved. About 5 s in, the PSP received nothing more (`received 176` from then on) while it went on sending; the server heard nothing either, and dropped it 17 s after the game began ("client machine 1 timed out"). The PSP's side: "silent for a dangerously long amount of time", then "the game host went down".

What else the PSP's log shows:
- Before the search, `winsock error #10055: WSAENOBUFS`: the engine sets each UDP socket's buffers to 1 MB (`transport_endpoint_winsock.c`, a PC's size), and the PSP's stack refused one.
- The frame at the game's start spent **2,964 ms** reading textures from the Memory Stick, at 1,623 KB/s. Blood Gulch with Wi-Fi joined but no traffic (Test 0h) read at 3,914 KB/s, its worst frame 1,291 ms. With the network busy, the stick read about 2.4 times slower.
- The hotspot's journal for 23:25–23:30 has nothing: the PSP didn't leave the access point.

**Reading** [U]: the stack takes every socket's buffers from one pool (sceNetInit's, 128 KB here). What arrives during a long stall waits in the socket; once the pool is empty, the stack can neither send nor receive, which is what both tries look like (the first during the load's long reads). mp3 (`build_mp3.md`) caps the sockets and doubles the pool, and logs the pool every heartbeat to show it.

