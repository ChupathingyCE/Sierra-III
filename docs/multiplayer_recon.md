# Multiplayer on the PSP: recon (C&C now, Halo later)

Started 2026-10-09. Two read-only surveys:
- the PSP's networking: pspsdk, PPSSPP's source, the web;
- Vanilla-Conquer's network code, with `sizeof` probes compiled for both x86-64 and the PSP.

The PSP part (§1–§3) applies to every port, Halo included. §4 is C&C's own code; §5 is the Halo notes.

*This copy (2026-10-11) is here because Sierra-III's docs point to its sections. The original is with the C&C port's notes in Sheepdog01's claude.ai project. Paths in §2 and §4 (`tools/net/`, `tools/ppsspp-headless.patch`, `cnc_toolchain.md`) are the C&C port's. Sierra-III's own setup is `multiplayer_setup.md`.*

Labels:
- **[SDK]** pspsdk headers or source
- **[PPSSPP]** emulator source at `79cf859`
- **[code]** Vanilla-Conquer source
- **[probe]** measured: compiled and run
- **[web]** a web page (Sources at the end)
- **[U]** inference

## 0. Decisions so far

1. **Infrastructure mode (Wi-Fi through a router) first; ad-hoc later.**
   - Vanilla-Conquer already speaks UDP. The PSP gives homebrew BSD sockets.
   - The emulator turns a PSP socket into a real Linux socket, so it can be tested here.
   - One PSP plus a PC can test it on hardware.
2. **Skirmish (against the AI) is the cheap first "multiplayer".** It is already compiled into the PSP build and needs no network [code].
3. **Cross-play PSP ↔ PC needed a fix in TD, now made** (r27). Some of TD's network structs laid out differently on MIPS (§4.4). Red Alert's were already identical [probe]. A PSP-against-Linux game has since been played in the emulator (§4.7).
4. **Halo: client-server over infrastructure, with a PC dedicated server** (the CSPSP pattern, §5).

## 1. The PSP's two network modes

| | Infrastructure | Ad-hoc |
|---|---|---|
| What | Wi-Fi through an access point, normal IP | PSP to PSP directly, no router |
| Addresses | IPv4 + ports; BSD sockets | MAC addresses + "ports" |
| API | `sceNetInet*` (BSD sockets via libcglue), `sceNetApctl*` to connect | `sceNetAdhoc*` PDP (datagrams) / PTP (streams), `sceNetAdhocctl*` groups, optional `sceNetAdhocMatching*` |
| Talks to a PC | yes | not directly (§2.5 tunnels) |
| Emulator | real host sockets [PPSSPP] | needs an ad-hoc server, two instances [PPSSPP] |

**Both work from a user-mode EBOOT on 6.61, including under ARK-4** [SDK]. The modules are loaded with `sceUtilityLoadNetModule`. The old `pspSdkLoadInetModules()` loads from flash0 and needs kernel mode: don't use it.

**One radio:** you can't be on ad-hoc and on an access point at the same time [U].

### 1.1 Infrastructure bring-up (pspsdk samples `net/simple`, `utility/netdialog`)

1. `sceWlanGetSwitchState()` must be 1. This is the WLAN switch on the PSP-1000's side.
2. `sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON)`, then `(PSP_NET_MODULE_INET)`.
3. `pspSdkInetInit()`. This is the same as calling, in order:
   - `sceNetInit(0x20000, 0x20, 0x1000, 0x20, 0x1000)`
   - `sceNetInetInit()`
   - `sceNetResolverInit()`
   - `sceNetApctlInit(0x1600, 0x42)`
4. Connect, one of two ways:
   - **Silent:** `sceNetApctlConnect(profile)`. Profiles start at 1; `sceUtilityCheckNetParam(i) == 0` means profile i exists. Then poll `sceNetApctlGetState()` until it returns 4 (GOT_IP).
   - **Sony's dialog:** `sceUtilityNetconfInitStart` (CONNECTAP), with `sceUtilityNetconfUpdate(1)` called every frame while drawing.
   - Connecting takes seconds, so it runs as a state machine or on its own thread.
5. Own IP and subnet mask: `sceNetApctlGetInfo(8 / 9, &info)`.
6. Teardown: close the sockets, `sceNetApctlDisconnect`, `pspSdkInetTerm()`, then unload the modules in reverse order.

### 1.2 Ad-hoc bring-up (pspsdk sample `utility/gamesharing`; no PDP sample exists)

1. Load the COMMON module, then ADHOC.
2. Initialise:
   - `sceNetInit(128 KB, 42, 4 KB, 42, 4 KB)`
   - `sceNetAdhocInit()`
   - `sceNetAdhocctlInit(0x2000, 0x30, &product)`. `product` is `{int type; char[9] code}`.
3. Join a group:
   - `sceNetAdhocctlConnect("CNCTD001")` (at most 8 alphanumeric characters), or create/scan/join, or the netconf dialog's ad-hoc action.
   - Poll `sceNetAdhocctlGetState()` until it returns 1.
4. Send and receive with PDP:
   - `sceNetAdhocPdpCreate(mac, port, bufsize, 0)`
   - `PdpSend` / `PdpRecv`, each with a non-blocking flag
   - Broadcast = destination MAC `FF:FF:FF:FF:FF:FF`.
5. Skip GameMode (Sony's replicated buffer); PDP is simpler.
6. A MAC is 6 bytes, so it fits C&C's IPX node address [U].

### 1.3 Sockets in pspsdk: what works and the traps

- **Works:** `socket`, `bind`, `sendto`, `recvfrom`, `setsockopt`, `getsockname`, `close`, `gethostbyname` (via the resolver), `inet_ntop`.
  - libcglue maps these onto sceNetInet, and socket fds share newlib's fd table [SDK].
- **Non-blocking:** `fcntl(fd, F_SETFL, O_NONBLOCK)` maps to `setsockopt(SOL_SOCKET, SO_NONBLOCK 0x1009)`.
  - `ioctl(FIONBIO)` does nothing: `sys/ioctl.h` is empty [SDK].
- **`select()` is a polling loop:** a zero-timeout `sceNetInetSelect` per fd, with 100 µs sleeps between rounds [SDK]. Instead, drain non-blocking `recvfrom` once per frame.
- **No `getifaddrs`:** take the IP and subnet mask from `sceNetApctlGetInfo` [SDK].
- **Link-order trap:** don't put `pspnet_inet` or `pspnet_resolver` in `target_link_libraries`.
  - The compiler's specs already add them after libcglue.
  - Listing them again splits the import table. In the emulator, `sendto` became a different function ("Bad socket", "Unknown syscall from known HLE module 'sceNetInet'") [probe].
  - Link only `pspnet pspnet_apctl pspwlan` (+ `pspnet_adhoc*`).

### 1.4 Hardware facts (PSP-1000)

| | | |
|---|---|---|
| Radio | 802.11b only, 2.4 GHz: the router must accept b clients | [web] |
| Security | WEP; WPA-PSK TKIP (FW 2.00+); WPA-PSK AES (2.50+). WPA2-only routers are reported not to work: use WPA/WPA2 mixed. No WPA3 | [web], [SDK] |
| Ad-hoc | channel 1, 6, 11 or auto; up to 16 PSPs; group name ≤ 8 characters | [SDK] |
| Packet size | keep datagrams ≤ ~1400 bytes (PDP fragment 1444; UDP 1472 avoids IP fragments) | [PPSSPP], [U] |
| WLAN power save | system setting. On = the radio dozes between beacons: latency spikes. Turn it off for play | [web], [U] |
| Latency, throughput | **no reliable figures found: measure** (expect a few ms ad-hoc, more through a router; a few Mbit/s) | [U] |
| RAM | the network modules load into the game's 24 MB, plus `sceNetInit`'s 128 KB pool. **Measure on hardware** (free memory before/after); the emulator's modules cost nothing | [U] |

### 1.5 Internet play for ad-hoc games

- **aemu** (the open successor of PRO Online) tunnels ad-hoc over infrastructure Wi-Fi to an ad-hoc server.
  - On ARK-4 it is a plugin (`atpro.prx` in `plugins.txt`, the SSID in `hotspot.txt`).
  - It plays with PPSSPP.
  - Caveat: on a PSP-1000 some games fail (those using partition 5) [web].
  - An ad-hoc homebrew would get internet play with no code changes [U].
- **XLink Kai:** a PC with a second Wi-Fi adapter joins the ad-hoc network and tunnels it [web].
- **PPSSPP:** ad-hoc servers (socom.cc by default), relay mode [web].
- ARK-4 itself has no network feature beyond loading plugins [web].

### 1.6 Homebrew to learn from

- pspsdk samples (source, not installed): `net/simple`, `utility/netdialog`, `utility/gamesharing`.
- **CSPSP**, a Counter-Strike-like PSP shooter: PSP clients talk UDP to a PC dedicated server, with a master server for the list.
  - `Wlan.cpp`: profiles, apctl, non-blocking sockets.
  - `UdpManager.cpp`: ACKs and ordered delivery over UDP.
  - **This is the Halo pattern** [web].
- Kurok / PSP Quake: infrastructure and ad-hoc. NJ's CPS1/CPS2 emulators: ad-hoc input netplay, close to lockstep [web].

## 2. Testing networking in the emulator

**Infrastructure sockets are real host sockets** [PPSSPP, probe]:
- The PSP's `socket()` becomes a Linux UDP socket, non-blocking on the host side, with SO_REUSEADDR and SO_REUSEPORT set.
- **A fake access point:** profile "NetConf1", SSID "Wifi". Apctl goes through states 0 → 2 → 3 → 4 in ~6 emulated seconds.
- **Addresses:** the instance's IP is 127.0.0.N, mask 255.255.255.0.
  - A bind to `INADDR_ANY` is rewritten to 127.0.0.N.
  - PortOffset is *not* applied to inet sockets.

| Probe (headless instance ↔ a Linux program) | Result |
|---|---|
| PSP → Linux, unicast | works; the reply to the source address comes back |
| PSP → 255.255.255.255 | received by a Linux socket bound to 0.0.0.0 |
| Linux → PSP at 127.0.0.1 | not received: the PSP is on 127.0.0.N |
| Linux → broadcast | not received: Linux only delivers broadcasts to sockets bound to 0.0.0.0 |
| `recvfrom` on an empty socket | -1/EAGAIN after ~0.5 ms of emulated time |

**Timing caveats:**
- Every `recvfrom`, `sendto` or `select` costs ~500 µs of emulated time.
- `select` with a NULL timeout waits 5.5 s.
- So use non-blocking sockets and poll once per frame [PPSSPP].

**Headless gaps, fixed by a local patch** (`tools/ppsspp-headless.patch`, 2026-10-09):
- Headless never loaded `--appendconfig`. `strict.ini`'s `PSPModel=0` (PSP-1000) was ignored, and every run was a PSP-2000 model.
  - In practice this changed nothing measurable: homebrew gets the same 24 MB user partition either way, and the interpreter and fault-on-bad-access flags were already on the command line.
  - Now it is applied.
- **Every headless instance was 127.0.0.0** with the same MAC. With `PPSSPP_INSTANCE=N` in the environment, an instance gets 127.0.0.N and (for N > 1) MAC N:N:N:N:N:N, like the GUI's Nth instance.

**Two emulated PSPs on one machine:**
- Run them as instances 1 and 2.
- Broadcasts need a small **reflector** on Linux: a UDP socket bound to 0.0.0.0 that re-sends each broadcast to the other instances by unicast.
- Ad-hoc between headless instances would also need the built-in ad-hoc server started [PPSSPP].

## 3. Bandwidth needs (both games)

- **C&C lockstep:**
  - TD, 15 ticks a second, one packet a tick: ~0.9 KB/s each way per peer when idle; a 4-player game ≈ 2.6 KB/s per machine.
  - RA: one packet every 3 ticks.
  - **Packet rate matters more than bytes** [code, U].
- **A shooter at 20–30 updates a second:** tens of KB/s.
- Both are far below 802.11b. Latency and loss are what to measure [U].

## 4. Vanilla-Conquer's networking (TD and RA)

### 4.1 What exists

- **One live mode, "Network":** the 1995 IPX protocol code over **UDP broadcast and unicast, port 1234**.
  - Modem, null-modem and Westwood Chat are compiled out.
  - **Skirmish is always built** [code].
- **CMake:**
  - `NETWORKING` (default ON) adds `WINSOCK_IPX NETWORKING` and the net sources.
  - The PSP block in the top `CMakeLists.txt` forces it OFF.
  - `build-linux` here was configured with it OFF too.

### 4.2 Transport

**The stack:**
1. Game code: `queue.cpp`, `netdlg.cpp`.
2. `IPXManagerClass` (`ipxmgr.cpp`): one global channel (lobby, chat) plus up to 7 private channels, one per peer, a full mesh.
3. `ConnectionClass` (`common/connect.cpp`): ACKs, retries, ordering.
4. `UDPInterfaceClass` (`common/wspudp.cpp`), built on `WinsockInterfaceClass` (`wsproto.cpp`).

**Socket calls:** `socket(AF_INET, SOCK_DGRAM)`, SO_BROADCAST, non-blocking via `FIONBIO`, `bind(INADDR_ANY:1234)`, `getifaddrs` (local and broadcast addresses), 128 KB buffers, then a zero-timeout `select` + `recvfrom`/`sendto` each poll.

**Discovery:**
- A joiner broadcasts "query game" to 255.255.255.255 and to each interface's broadcast address.
- The host answers by broadcast (seen in the 2026-10-09 PSP ↔ Linux run, §4.7; the code read first suggested unicast).
- **Packets from any local address are dropped** (the self-filter), so two copies on one machine can't see each other unmodified.

**Packets:**
- Private channel: 8-byte header (magic, code DATA_ACK / DATA_NOACK / ACK, packet id).
- Global channel: TD 20-byte header, RA 12-byte header.
- TD and RA use different magic numbers, so they can share a LAN.

**No thread:** the network is pumped from the keyboard poll (`Process_Network()` in `wwkeyboard_sdl2.cpp`). On the PSP it would go in `WWKeyboardClassPSP::Fill_Buffer_From_System`.

### 4.3 The game protocol: deterministic lockstep

- Every machine runs the whole simulation. Only commands travel.
- A command made on frame F runs on every machine at frame F + MaxAhead.
- Each packet starts with FRAMEINFO: the frame, a CRC of the game state, a command count.
- **A machine waits** until it has every peer's commands for the frame. After 3 s it shows the reconnect dialog; after 25 s it drops the peer.
- **MaxAhead** comes from the measured round trip (minimum 2 frames). On a LAN that is ~133 ms of input delay [U].
- **Sync check:**
  - The CRC covers unit, infantry and building positions and facings, the map and the random seed.
  - A mismatch brings up "Out of sync" [code].
- **RA can slow everyone to the slowest machine** (frame-rate negotiation, MULTI_E_COMP).
  - TD has the code but enables it only under `FORCE_WINSOCK`.
  - So in TD **the slowest machine (the PSP) stalls everyone** [code, U].
- **Packet rate:** TD sends a packet every frame; RA every 3rd frame.
- **Packet loss:**
  - A lost FRAMEINFO-only packet is harmless.
  - Command packets are resent until acknowledged.
  - A command arriving after its frame is fatal ("Packet received too late").

### 4.4 Cross-play hazards (PSP MIPS vs PC x86-64)

**The blocker (TD only):** `BITFIELD_STRUCT` is `__attribute__((ms_struct))`. x86 GCC honours it; MIPS GCC ignores it. A 1-bit field takes 4 bytes on x86 and 1 byte on the PSP [probe]:

| TD struct | x86-64 | PSP |
|---|---|---|
| `EventClass` (every command) | 22 | **19** |
| `SpecialClass` | 16 | **13** |
| `GlobalPacketType` (lobby) | 48 | **46** |

**Effects:**
- A PC's lobby packets are 2 bytes too big for the PSP's queue, so **the PSP never sees a PC's game**.
- The other way, the seed and options are read from the wrong offsets, so the game desyncs at once.
- **PSP ↔ PSP is unaffected:** same compiler.
- **Nothing catches it:** TD's `Version_Number()` is always 1.

**Fix:**
- Pad the bitfields to whole 32-bit units with unnamed fields. This leaves the x86 layout unchanged and makes the PSP's match.
- Add `static_assert`s on the sizes.
- Fold a layout or build fingerprint into the version.
- Note: `SpecialClass` is also in save games, so PSP saves change format [U].

**RA's structs** use explicit bitfield unions and come out the same on both [probe].

**Checked and fine:**
- no pointers or `long` in TD's wire structs;
- enums are 1 byte on both;
- both builds use `-fsigned-char`, and both machines are little-endian;
- the synced random generator is a 32-bit LCG;
- `rand()` only makes the host's seed;
- simulation timers count frames, not the clock [code].

**Small risks:**
- One float in the AI's build slowdown (`techno.cpp:318`): make it integer.
- The PSP builds at -Og and Linux at -O2, so code with undefined behaviour could diverge.
- The PSP's test-only "W wins" key changes game state outside the command queue: it must be off in network games.

### 4.5 Lobby flow and UI needs

**TD:** Multiplayer → Skirmish / Network.
- **Network → Join dialog:** name entry, side, game list, chat line.
- **Network → New-game dialog:** scenario list, credits, units, tech level, bases, tiberium, crates.
- The host is authoritative: it pushes the options, pings, sets MaxAhead and sends GO.

**Scenario agreement:**
- TD sends only a scenario number. Each machine needs the same maps; a mismatch is caught by a scenario CRC at start.
- RA can send the scenario file to a joiner, and adds version and `rules.ini` checks, AI players in network games, and a chat lobby.

**On the PSP:**
- Text entry (name, chat): take the name from the PSP's nickname (`sceUtilitySystemParam`) or the on-screen keyboard. Chat can wait.
- The lists and buttons already work with the cursor [U].

**Record/playback:**
- `-XX` records the command stream to `RECORD.BIN`; `-XY` replays it.
- This is a ready-made determinism test: record on one machine, replay on another, compare CRCs [code].

### 4.6 Port plan for C&C (ordered)

| # | Step | Size |
|---|---|---|
| 1 | ✅ Skirmish on the PSP: dialog, map, AI game run (r27). The script "win" hook only exists with an input script loaded | small |
| 2 | ✅ **PSP transport** (r27): NETWORKING on; `wspudp.cpp` PSP branch (non-blocking via `fcntl`, own address from apctl, 16 KB buffers); `Process_Network()` in the PSP input poll; only `pspnet pspnet_apctl pspwlan` linked by name | small–medium |
| 3 | ✅ Wi-Fi bring-up (`common/net_psp.cpp`, r27): modules, apctl, profile 1 (`[PSP] NetProfile=`), switch check, teardown; joined when the socket opens, not at start-up; left when the player leaves network play (kept between the lobby and a game, which TD recreates its transport for); "Connecting to the network..." while joining, a message when it fails. The Sony dialog is not used | medium |
| 4 | ✅ Two emulated PSPs (r27): instances 2 and 3, `tools/net/psp_relay.py`, scripted host and join (`cnc_toolchain.md` §9) | medium |
| 5 | ✅ TD layout fix + `static_assert`s; PSP ↔ Linux game played in the emulator (2026-10-09, §4.7). Not done: a version fingerprint (TD's `Version_Number()` is always the same, so builds that differ in game code can still join each other and desync) | small–medium |
| 6 | Hardware: one PSP against the Linux build, then two PSPs | — |
| 7 | Optional: TD on RA's protocol (fewer packets, slows to the slowest machine) | medium |
| 8 | Optional: an ad-hoc transport (PDP) for router-free play | medium–high |

### 4.7 Results (2026-10-09, r27, emulator)

**Wi-Fi bring-up.** In the emulator, joining takes 6.0 s: states joining → getting an IP → connected. The Join dialog then appears.
- Joining used to happen at start-up. TD's global `IPXManagerClass` asks the transport whether network play is possible by starting it. On the PSP that would join the Wi-Fi at every boot, so the join waits for the socket's opening.

**Two PSPs, one game.**
- The guest's Join dialog lists "Host's game"; the host sees the guest (Nod) in its player list.
- After OK, both start the same two-player game on Green Acres.
- A move order on the host travels as a 74-byte packet (8 + 3 × 22: the frame's FRAMEINFO plus two command events) and is acknowledged. Both games carry on with no "out of sync".
- Ticks run at 7–15 a second because the two emulators run at different speeds, not because of the game.

**Packet sizes after the layout fix:**
- lobby: 68 bytes, the same as x86 (66 before);
- frames: 30 bytes;
- ACKs: 8 bytes.
- Measured from the relay's log.

**Memory.** The heap after joining was 13,648 KB in use, 7,408 KB largest free block, at the multiplayer menu. The network modules' own RAM, outside the heap, still needs measuring on hardware.

**PSP against the Linux build** (same commit, desktop SDL2 build with `-DNETWORKING=ON`):
- **Set-up:**
  - The desktop game runs under Xvfb in DOS mode, driven by `xdotool` (`tools/net/xclick.sh`).
  - The `reuseaddr.so` shim binds it to 127.0.0.5.
  - The relay sends to it from 127.0.0.9: the desktop game drops packets from its own addresses, 127.0.0.1 among them.
- **The lobby, decoded both ways with the x86 layout:**
  1. The PSP's `QUERY_GAME`.
  2. Linux's `ANSWER_GAME` ("Linux", version 2, open).
  3. `QUERY_PLAYER` / `ANSWER_PLAYER`.
  4. The PSP's `QUERY_JOIN` ("Guest", Nod, color 1) and Linux's `CONFIRM_JOIN`.
  5. `GAME_OPTIONS`: Green Acres, 3000 credits, tech 3, 6 units, game speed 4, bases.
  6. `GO`.
- **The game:**
  - Green Acres started on both.
  - A move order given on Linux crossed as a 74-byte packet (three 22-byte x86 events).
  - Neither side reported "out of sync" over the PSP's next ~500 ticks.
  - The PSP advanced only as Linux's frames arrived (~1.5 ticks per emulated second, Linux running in real time and the emulator about 5× faster): lockstep pacing working across the two.
  - The run ended when the PSP's emulator reached its time limit; Linux then showed "Reconnecting to Guest", as expected.
- **Finding:** the host answers queries by **broadcast**, not unicast. On a real LAN that reaches everyone. On one machine it only reaches sockets bound to 0.0.0.0, which is why every local test needs the relay.

**Not yet done:**
- **Hardware.** The natural first test is one PSP against PPSSPP on Sheepdog01's laptop, running the same `CNC` folder.
  - PPSSPP's desktop app with networking on, and an ad-hoc server that isn't localhost, binds the game's socket to all addresses. Its fake access point reports the laptop's real LAN IP [PPSSPP: `NetApctl_InitInfo`].
  - So it should behave like a second PSP on the LAN [U].
  - The Linux build would also do, but needs building on the laptop.
- RA's network game (needs data);
- ad-hoc.

## 5. Halo notes

- **Halo PC/Xbox is client-server**, unlike C&C:
  - the server simulates;
  - clients send input and receive snapshots of the world.
  - So there is no lockstep and no determinism requirement, but more bytes (tens of KB/s) and a need for prediction and interpolation [U].
- **Pattern to copy: CSPSP.**
  - PSP clients, a PC dedicated server, UDP with its own ACK/ordering layer.
  - A **Linux dedicated server** built from the existing desktop port keeps the server's simulation off the PSP's CPU [U].
- **Infrastructure first:**
  - It is testable in the emulator against Linux programs (§2).
  - It gives PSP ↔ PC play.
  - Ad-hoc (PDP) later, for router-free play; aemu then adds internet play for free.
- **Memory:** the campaign build cut multiplayer code (`dead_code_inventory.md`, group 1). A multiplayer build is its own build with its own budget. The network modules' RAM cost must be measured first.
- **Reusable from C&C's work:**
  - the Wi-Fi bring-up code;
  - the PSP socket notes (§1.3);
  - the emulator set-up (§2);
  - the reflector.

## 6. To measure on hardware

1. Connect to Sheepdog01's router: profile 1, WPA/WPA2 mixed, 802.11b allowed.
2. Note the IP, and the free RAM before and after loading the modules.
3. Echo test against the laptop: round-trip times at 15 Hz for a few minutes, WLAN power save on vs off; broadcast both ways.
4. Then C&C: PSP against the Linux build on the LAN.

## Sources

- pspsdk: https://github.com/pspdev/pspsdk (samples/net, samples/utility, src/libcglue, src/sdk/inethelper.c)
- PSP firmware changelog: https://playstationdev.wiki/pspdevwiki/Official_Firmware
- PPSSPP multiplayer: https://github.com/hrydgard/ppsspp/wiki/How-to-play-multiplayer-games-with-PPSSPP
- aemu: https://github.com/Kethen/aemu
- ARK-4: https://github.com/PSP-Archive/ARK-4
- XLink Kai: https://teamxlink.co.uk/wiki/PlayStation_Portable_XLink_Kai_Setup
- CSPSP: https://github.com/kevinbchen/cspsp, https://github.com/kevinbchen/cspspserver
