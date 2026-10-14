# Multiplayer on Sierra-III: where it stands and the plan (2026-10-10, evening)

Two goals, in order (Sheepdog01, 2026-10-10):
1. ChupathingyCE's multiplayer working on the PSP at all.
2. Custom maps, which means converting their tag data in the bake.

**The target is full parity with PC player counts and servers** (Sheepdog01, 2026-10-10): mirror how the PC builds work wherever memory allows. PSP ↔ PSP play is the fallback if PC compatibility can't be reached.

**ChupathingyCE compatibility comes first** (Sheepdog01, 2026-10-12): the PSP speaks ChupathingyCE's protocol as a PC does, and nothing PSP-only may change what goes on the wire. Player limits are the server's to set (ChupathingyCE's platform policy, upstream and planned, §7), not the PSP client's. Ad hoc play (mp5, keep word `adhoc`) is PSP to PSP only, a side path: the access point path to ChupathingyCE servers stays the main one, and every multiplayer build is checked against a ChupathingyCE server before it ships.

Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in code, **[U]** inference. The engine is ChupathingyCE at `b78e6cf` (0.7.1d). The PSP-side networking facts (Wi-Fi bring-up, sockets, emulator behaviour) are in `multiplayer_recon.md` (§1–§3); this doc doesn't repeat them. How to set it all up and play: `multiplayer_setup.md`.

## 0. The short version

- **A PSP has played on a PC server** [E]. In the emulator, the multiplayer build joined ChupathingyCE's Linux dedicated server from the main menu, loaded Blood Gulch, spawned and played for 4.5 minutes at 30 ticks a second: 28,501 messages sent, 16,749 received, the heap's top 3,528 of 4,352 KB (lab workflow `sierra-netplay`, §5).
- **The netcode suits the PSP.** It isn't lockstep: every machine runs on its own clock, the host decides everything, and clients are corrected toward the host. A slow or slightly different PSP doesn't stall or desync anyone [D].
- **The multiplayer build runs at the PC's limits** (128 players, 128 machines), so its game record matches a PC's byte for byte (§3.1). The campaign build is byte-identical throughout [D].
- **The baked maps keep the disc's tag handles**, salts included, on all 13 stock multiplayer maps (§3.2).
- **Memory fits** because the multiplayer maps are small: a 6 MB tag window instead of 9 gives 3 MB back (§4).
- **Wi-Fi and sockets work** in the emulator (`psp_net.c`, §2). Next: the same join on hardware (§5, step 7).
- **Custom maps are a bake job, not an engine job.** The desktop converts Custom Edition maps as they load (`ce_*.c`); the PSP should convert them once on the PC, with Invader's own tools plus the repairs ChupathingyCE already lists (§6).

## 1. What we inherit from ChupathingyCE

**Transport** [D]:
- System link over UDP. `xnet.c` maps the Xbox's Winsock and XNet onto BSD sockets: an Xbox address is just the IPv4 address. The game's server port is 5150.
- Clients find games by **broadcast**. `network.broadcast = "a.b.c.d,..."` (config.toml) sends them to a list of addresses instead. That replaces the relay the C&C emulator tests needed.
- Internet play (`p2p.c`: invites, a tunnel, encryption, Discord) is left out. The PSP already answers it as "no internet play" (`psp_libc.c`). LAN only.
- **Delta Peer** (port 5160) is optional. A host speaks it only to clients that answer its handshake; anyone else gets plain OpenCE behaviour. The PSP doesn't build it.

**The game protocol** [D]:
- Halo's star shape: clients talk only to the host.
- Messages are packed field by field (`data_packets.c`). A count's width comes from its maximum (one byte up to 255), so an array of 16 players and one of 128 encode the same way when ≤ 255.
- **The exception: the game settings record** (`struct network_game`) is sent as raw bytes in four pieces, and the client checks its total size against its own (§3.1).
- **Version:** 24. A client joins hosts of 11 to 24 (`HALO_PORT_NETWORK_VERSION*`).

**The distributed netcode** (`port/linux/NETCODE.md`) [D]:
- **Own clock.** Every machine ticks at 30 Hz on its own. A client takes the host's time once, then only ever jumps forward to it when behind.
- **Host authoritative.** The host alone decides damage, deaths, spawns, pickups and scores.
- **Prediction and correction.** A client drives its own player at once. The host corrects everyone's copies, gliding small errors and snapping large ones.
- **Shooter's hits.** A client reports what it hit; the host checks the report.
- **Objects by index.** The host's units, vehicles, weapons and equipment sit at the same datum index on every machine. Messages name them by that index and their type by **tag index** (§3.2).

**What that means for the PSP:**
- **No determinism needed** (C&C's lockstep did need it). The port's own small differences (a sleeping body, a packed collision tree) get corrected instead of desyncing.
- **A slow PSP hurts only itself.** If it can't hold 30 ticks a second, its clock jumps forward and its view gets jerky; the others don't wait [D, U].
- **So the tick rate is the PSP's own problem**, and player count is its main lever: every remote player is a biped the PSP simulates (§4).

**The dedicated server** (`server/`) [D]:
- The same game as a headless Linux program, hosting from a playlist.
- It plays the stock NTSC Xbox maps (`ui.map` + the multiplayer maps); Custom Edition maps go in `maps_ce/` with CE's resource maps.
- Its own machine takes a machine slot with no player.
- **This is the CSPSP pattern** the recon recommended: PSPs as clients, a PC host. No PSP has to host.

**A test harness** (`network_test.c`, compiled in the multiplayer build) [D]:
- `debug.network_test = "host:bloodgulch"` hosts and starts a game by itself; `"join"` joins the first game it finds.
- On the PSP it reads `ms0:/halo/save/config.toml` (`HALO_SAVE_ROOT`, `port_config.c`).
- This is the scripted join for the emulator and for a first hardware test, with no menus.

## 2. The PSP build today

`PSP_MULTIPLAYER=1`, ChupathingyCE base, branch `mp-recon` [D]:

| | Campaign | Multiplayer |
|---|---:|---:|
| Session limits | the Xbox's: 16 players, 4 machines | **the PC's: 128, 128** |
| Program | 4.55 MB | 6.16 MB |
| Heap | 5,952 KB | 4,352 KB |
| Tag window | 9 MB | **6 MB** (§4) |
| Symbols stubbed | 0 | 0 (was 10) |

The campaign EBOOT stayed byte-identical through all of this (`8104c527`): every change is behind `PSP_MULTIPLAYER` or in files only that build compiles.

**The ten stubs**, each answering 0. The five marked would have broken a game:

| Symbol | A 0 would have... | Now |
|---|---|---|
| **`map_family_wire_name`, `_from_wire_name`** | copied no map name: the advertisement and the settings name no map | `map_families.c` built in as it is |
| **`delta_legacy_announce`, `_minimum`, `_maximum`** | advertised version 0; joined only hosts of version 0 | 24, 11, 24 (delta.c with no table) |
| **`pc_menus_screen`** | failed to load the pregame and postgame screens | the screen it's given |
| `map_family_parse`, `_badge` | (Custom Edition paths only) | `map_families.c` |
| `log_address` | logged "(null)" for addresses | the real rule, PSP hash |
| `p2p_set_game_listing` | nothing (it's void) | with the other p2p answers |

**What the extra 1.6 MB is** (link maps, against the campaign) [D]:
- Code: +378 KB.
- Buffers: +1.3 MB. The largest: one send buffer per machine (`distributed_batches`, 155 KB), the update rings (`update_server_globals`, `update_client_globals`: 131 KB each at 128 players), the late joiners' copies of the players (64 KB), co-op's buffers (~250 KB: `host_sent_transforms`, `host_actors`, `client_actors`...), the host's damage records.
- Arrays only a host uses are sized for a PSP-hosted game of 16 machines (`PSP_HOST_MAXIMUM_NETWORK_*`, `psp_limits.h`); each was checked to be reached only through a machine or player index under 16.
- The rest are levers if memory gets tight again; they aren't needed now (§4).

**Wi-Fi and sockets** (`psp/src/psp_net.c`, multiplayer build only) [D, E]:
- **The network modules load once, at start-up**, right after the game state and the tag window take their fixed places (`psp_memory.c`). They live in user RAM outside the heap, so they go first: before the caches that also live there, and never where the fixed blocks must go.
- **The game joins the access point as it starts:** its transport asks for this machine's address. The connection is the PSP's saved one number 1, or keep word `netprofileN`. 6 s in the emulator.
- **Sockets:** the game's Winsock calls on the PSP's BSD sockets, answering as ChupathingyCE's Linux layer does (`posix_net.c`): addresses converted (BSD's length byte), errors mapped, a connect under way is "would block", a failed connect lands in the error set, Xbox-only options do nothing.
- **Linked:** `pspnet pspnet_apctl pspwlan`. Not `pspnet_inet` or `pspnet_resolver`: the toolchain adds those itself (the link-order trap, `multiplayer_recon.md` §1.3).

## 3. Cross-play with PCs: the blockers

PSP ↔ PSP avoids all three (same code, same bake). PSP ↔ PC met them in this order; the first two are fixed, the third is down to objects.

### 3.1 The game settings record's layout [D]: fixed

- The record holds an array per machine and per player, sized by `HALO_PORT_MAXIMUM_NETWORK_MACHINES` and `_PLAYERS`.
  - PC: 128 and 128 (13,120 bytes). The PSP had the Xbox's 4 and 16; the multiplayer build now has 128 and 128 too (`psp_limits.h`).
- It's sent raw, and the client checks the size (`network_game_client_receive_game_settings_piece`).
- **So a PSP joining a PC host fails cleanly** ("a piece for a different game layout"), and the reverse too. Nothing desyncs.
- **The fix (Sheepdog01, 2026-10-10: mirror the PC):** the PSP's limits become the PC's, 128 and 128, so the record matches with no translation. What that costs, and how to keep it affordable, is §4.
  - (The alternative was translating the record at the wire and keeping 16/4 inside. It's the fallback if §4's memory can't be found.)

### 3.2 Tag order and tag IDs in the baked maps [D, E]: fixed

- **The netcode names tags by their whole handle**, index and salt: an object's type, a damage effect, an effect, a sound. `tag_index_is_group` compares both [D].
- **The bake rebuilds every map with Invader**, which orders tags its own way and gives each the salt of its own formula [D].
- **Measured** (`tools/tagorder.py`, lab workflow `sierra-tagorder`; counts only, no names) [E]:
  - plain bake: Blood Gulch kept 1,378 of 1,806 tags at the same handle; 14 of its 70 object tags moved;
  - with an index (`invader-build -w`): every tag at the same index, but the last 114 salts were 1 or 2 more than the disc's. Among them 6 damage effects, 2 effects and 16 sounds, which the netcode sends. The disc's salts don't all follow Invader's formula.
- **The fix:** the bake writes each tag's ID into the index (`bake/lib/tagindex.py`), and the port's Invader fork builds with it (an index line may end in a tab and the ID).
- **Result:** all 13 stock multiplayer maps keep every tag's handle (bake `mp3`, 2026-10-10).

### 3.3 Players, machines and objects [D, U]

**Why the PSP is at 4/16** [D]:
- The Xbox game's record has room for 4 machines and 16 players (4 consoles, up to 4 split-screen players each). OpenCE raised it to 128/128 for the native builds and reworked what that broke (the record sent in pieces, longer message arrays). That rework is already in the PSP build; only the two constants differ.
- The PSP chose the Xbox's values on day one (`halo-psp-feasibility.md`, 2026-10-01): the campaign was the goal, multiplayer a PSP ↔ PSP proof of concept, and every capacity went to the Xbox's to save memory. The same doc named 128 players a stretch goal and said the limits should stay a build setting.
- So it's our memory choice, not a hardware or engine limit.

**Now 128/128** in the multiplayer build (§2). **What the 4/16 limits refused:**
- **Machines:** the PSP accepts at most 4, the server's own included. With a dedicated server that's **3 PSPs** [D: `network_game_client_game_settings_updated`].
- **Players:** the PSP refuses a game whose `maximum_players` is over 16 [D].
- **Objects:** a client's own objects (projectiles, effects) take the upper half of the object array; the host's stay below.
  - PC: 8,192 objects, the host's below 4,096. PSP: 2,048, the host's must stay **below 1,024** [D].
  - Stock multiplayer maps likely stay far under that [U]; the PSP should count them.
- At 128/128 the first two go away. Objects stay a PSP limit: a server could still refuse a PSP whose maps or game would pass it (Sheepdog01's enforcement idea).

### 3.4 Version drift [D]

- The PSP joins hosts of version 11–24. A newer ChupathingyCE raises it.
- Keeping up = moving `upstream/pins.env` and running `PSP_FIXUPS_CHECK=1`, as for every upstream change.

## 4. Cost on the PSP

**Tick rate:**
- Blood Gulch with one player held 30 ticks a second on hardware (b87, the donut test: 1–2 ticks a frame at 14–19 fps) [M, frame_rate_targets.md].
- Remote players add bipeds, and bipeds are what made b30's beach slow [M, campaign_recon_b30.md].
- So the first games should be small: **2 to 4 players**, then measure up.

**Memory: the builds** [D, link maps]:

| Multiplayer build | Program | Heap |
|---|---:|---:|
| Limits 16/4 (the first link) | 5.55 MB | 4,992 KB |
| Limits 128/128, every array sized for 128 | 7.63 MB | 2,816 KB |
| **128/128, the host's arrays sized for a PSP-hosted game of 16 (now)** | **6.16 MB** | **4,352 KB** |

- The same code is host and client, so every array a host needs is on every machine. The host-only ones are sized for 16 machines and players (§2), which keeps the wire at the PC's 128/128 for 1.5 MB less.
- The client's update queue holds 32 updates, not 128: the distributed netcode's client never reads it [D].
- The game's own script table (a stand-in on the PSP: maps bring their own) is 1,024 nodes, not 19,001 (371 KB → 20 KB) [D].

**Memory: the tag window** [D, E]:
- At 128/128 Blood Gulch didn't fit the 4,352 KB heap: the GE's copies' arena (1,056 KB) and display list (512 KB) found no room, and 155,000 materials went undrawn [E].
- **The stock multiplayer maps are small**: as baked, they take 2.3 to 3.3 MiB of tag space (Invader's count: the tags and the largest BSP) against the campaign's 9 MB window [E, bake of 2026-10-10]:

| Map | Tag space | Map | Tag space |
|---|---:|---|---:|
| sidewinder | 3.30 MiB | prisoner | 2.50 |
| bloodgulch | 3.10 | hangemhigh | 2.40 |
| boardingaction | 2.88 | ratrace | 2.40 |
| putput | 2.87 | carousel | 2.39 |
| damnation | 2.85 | chillout | 2.39 |
| beavercreek | 2.58 | wizard | 2.39 |
| longest | 2.29 | ui | 1.06 |

- **So the multiplayer build's window is 6 MB** (`psp_capacity.h`): the same base, ending 3 MB lower. It leaves 2.7 MB of room over the largest stock map for custom maps.
- **The bake builds the multiplayer maps and ui for 6 MB** (`bake.py`, `-T 0x600000`). A structure BSP loads at the end of the window it was built for, so these maps load in both builds; the campaign maps keep 9 MB. The engine refuses a BSP that would land outside the window, so a campaign map in the multiplayer build fails to load rather than overwriting anything [D].
- **The 3 MB given back** is user RAM outside the heap. The network modules, the sound cache (1.5 MB), the display list (512 KB) and the rasterizer's start-up buffers now go there, and the heap keeps its room for the rest.

**Memory: measured** [E, emulator]:

| | Heap top, of 4,352 KB | Ticks | Failed allocations |
|---|---:|---:|---:|
| Blood Gulch, offline | 3,164 KB | 30/s | 0 |
| Sidewinder (the largest), offline | 3,184 KB | 30/s | 0 |
| Blood Gulch, playing on a server for 4.5 minutes (§5) | 3,528 KB | 30/s | 0 |

- The campaign build loads the 6 MB-baked Blood Gulch as before (heap top 5,492 of 5,952 KB), and starts on the 6 MB-baked `ui.map` (the main menu, 30 fps), so one maps folder serves both builds.
- **The network modules on the PSP** [M, mp1 Test 0, 2026-10-10]: loaded and started in 277 ms; user RAM outside the heap 3,844 → 3,270 KB free (**574 KB**), its largest block 3,328 → 2,976 KB. With a 1,792 KB sound cache (keep word `sound1792`), the display list and the start-up buffers also outside the heap, 244 KB stayed free. So the 6 MB window holds on hardware.
- **Still to measure on the PSP:** the heap in a full game.

**Beyond memory, at 128 players** [U, estimates]:
- **The tick.** Every remote player is a biped the PSP simulates each tick. 128 is a CPU question for the performance work: "joins a 128-player server" and "holds 30 ticks/s in a full one" are separate goals.
- **Wi-Fi airtime.** Each client gets every player's input every tick and the units near it. A rough sum for a full game is 120–200 KB/s, 1–1.6 Mbit/s, per PSP. 802.11b moves maybe 4–6 Mbit/s in all, shared by every PSP on the access point. Measure with the echo test (`multiplayer_recon.md` §6).

## 5. Steps (ordered)

| # | Step | Status |
|---|---|---|
| 1 | The multiplayer build links clean | ✅ 0 stubs |
| 2 | **PC limits:** 128/128; the host's arrays sized for a PSP-hosted game of 16, each checked | ✅ (§2, §4) |
| 3 | **Wi-Fi and sockets** (`psp_net.c`); link `pspnet pspnet_apctl pspwlan` | ✅ in the emulator |
| 4 | **Tag order and IDs** in the bake (§3.2) | ✅ all 13 maps |
| 5 | **Memory:** the 6 MB tag window (§4) | ✅ in the emulator |
| 6 | **First join, emulator:** the dedicated server and the PSP on the X3D (`sierra-netplay`) | ✅ joined and played, at real speed |
| 7 | **Hardware:** Sheepdog01's PSP on the desktop's hotspot, joining the server there; `mp1`, `build_mp1.md`; `mp2`, `build_mp2.md` | mp1: Wi-Fi joins [M]; its first search faulted. mp2 built, to test |
| 8 | **Delta Peer, client side** (§7) | later |
| 9 | The Xbox's system link menus; PSP ↔ PSP (one hosts), the fallback | later |

**How the emulator join works** (`sierra-netplay`, in the lab) [E]:
- The server is ChupathingyCE's at our pin, built for the lab (its `bin` branch). It reads `ui.map` and the multiplayer maps from Sheepdog01's disc image into scratch space, and keeps off the game list and internet play.
- The PSP boots to the main menu (`tools/emu.sh`'s `MAP=none`) with `debug.network_test = "join"`: it searches, joins the first game, and plays.
- Both sit on the loopback: the server on 127.0.0.1, the PSP on 127.0.0.2 (the emulator's `PPSSPP_INSTANCE`). Each sends its system link announcements to the other's address (`network.broadcast`), since a loopback broadcast reaches neither.

**What the runs found:**
1. Run 1: found the server, then every connection failed. A non-blocking connect under way came back as `WSAEINPROGRESS`, which the game takes as a failure; Linux's layer answers `WSAEWOULDBLOCK`. Fixed in `psp_net.c`, with the other answers `posix_net.c` special-cases.
2. Run 2: joined the lobby, loaded Blood Gulch, then the server timed the PSP out. Its TCP connection left from 127.0.0.1 (never bound) and its datagrams from 127.0.0.2, and the server knows a machine by its connection's address. An emulator artifact (a PSP has one address), fixed in the emulator's local patch.
3. Run 3: played (§0). But headless emulation runs faster than real time, so the server saw the PSP's clock run 1.7× fast and refused its player's predictions ("runs 1.72 times as fast as this host's").
4. Run 4, the emulator held to real time (`PPSSPP_REALTIME`, the emulator's local patch): 4.25 minutes clean. No clock complaint; 15,302 messages sent and 15,964 received (two a tick each way); 30 ticks a second.

**What the emulator can't tell:** the network modules' RAM (its modules cost almost nothing, §4), Wi-Fi's latency and loss, and the PSP's real tick at full load.

## 6. Custom maps (after a stock map plays)

**How the desktop does it** [D]: it loads a Custom Edition map and converts it as it loads (`port/linux/game/ce_*.c`):
- `ce_resources.c`: CE keeps most bitmaps, sounds, fonts and strings in shared resource maps (`bitmaps.map`, `sounds.map`, `loc.map`); they're copied in.
- `ce_models.c`: PC models (`gbxmodel`) become Xbox models: part layout, local nodes, compressed vertices.
- `ce_bsp.c`: the level's uncompressed vertices are compressed.
- **Channels:** a model shader's multipurpose map and a HUD meter's bitmap keep their masks in different channels on PC (`ce_models.c`, `ce_hud.c`).
- **Sounds:** some are Ogg Vorbis (`ce_vorbis.c`).
- **Repairs** of what Halo PC let slide (`ce_repairs.c`, `ce_functions.c`), and **checks** of every offset and count, since a map can come from anywhere (`ce_map_checks.c`).

**The PSP's way:** do all of that once, on the PC, in the bake. A sketch:
1. `invader-extract` the CE map, with CE's resource maps beside it.
2. `invader-convert` each `gbxmodel` to a `model`, and repoint what references it (`invader-refactor`).
   - Invader refuses to build an Xbox map with a `gbxmodel` in it [D: `model.cpp`].
3. The repairs from `ce_repairs.c` and `ce_functions.c`, in Python, on the tags.
4. `invader-index` the CE map, rename `.gbxmodel` to `.model` in that list, and build with `-w`.
   - That keeps the CE map's tag order, which a PC host playing the original uses (§3.2).
5. The PSP bake as now (geometry pack, textures, sounds), with two additions:
   - the PC channel order for multipurpose maps and HUD meters;
   - Vorbis → VAG.
6. **The fit check:** the baked map against the multiplayer build's 6 MB tag window (Invader's `-T` refuses a map that doesn't fit; the bake log shows each map's tag space), the geometry cache and the heap, before it goes on the stick.

**Map transfer: there isn't any** [D]:
- ChupathingyCE sends no maps and points to no repository, at our pin or at upstream's 0.7.1e. A client missing the host's map is told which map and which folder it goes in (`cache_files_map_present`).
- The hooks for downloads exist: a downloaded map's scripts run under tighter rules (`map_is_downloaded`, "until the game downloads maps itself").
- **The check on connect already exists.** For a Halo PC map the host sends a version, the map file's header checksum (`cache_files_map_version`), and the client compares its own. If the bake writes the source map's checksum into the PSP map, a PSP answers that check like a PC, over the plain protocol.

**Baking: how long, and where** [D, M]:
- **Time:** 3–10 minutes a map on a desktop (`bake/README.md`); the X3D baked `ui` and the ten campaign levels in about 35 minutes. Most of it is the textures (k-means: Blood Gulch's took about 90 s on two cores, `asset_baking.md` §6).
- **On the PSP: no.** There's no Invader or Python on it, a source map (44 MB for Blood Gulch) is bigger than its RAM, and the texture fit alone would take hours at 333 MHz.
- **At connect, on a PC: no.** Minutes, against a join that waits seconds.
- **Once per map, ahead of time: yes.** Bake each custom map once, keyed by its checksum, and keep the result where PSP players get it (a repository). The texture fit is seeded (`random_state=0`), so the same bundle should make the same pack [U].
- Stock maps stay as now: every player bakes them from their own disc. Only community maps could be shared baked.

**On the network:**
- A CE map's wire name is `custom_maps\<name>` (OpenCE's), already handled by `map_families.c` [D].
- The PSP must open such a name from its own folder. The desktop's CE loader is off on the PSP (`HALO_CUSTOM_EDITION` dropped), so this is new engine work, a small one: the baked map is an Xbox map by then.
- Delta's `ce_maps` capability checks the file's size and hash too. The PSP has no Delta Peer; the checksum above already catches a stale bake. Later, optionally: carry the size and hash as well and answer Delta's check.

**Try first, before writing any of it:** does `invader-build -g xbox-ntsc` accept a converted CE map's tags at all? One map, by hand, on the X3D.

## 7. Delta on the PSP

**What Delta is** [D, `docs/delta.md` upstream]: everything ChupathingyCE machines say to each other and to its site beyond OpenCE's game protocol. Its rule: the player base stays whole, so every part falls back silently to plain OpenCE.

| Part | Between | For the PSP |
|---|---|---|
| **Delta Peer** (UDP 5160, beside the game's 5150) | machines in one game | **yes: the part that matters** |
| Delta List (HTTPS) | games and the site's game list | later: an internet server browser |
| Delta Stats, Control, Link (HTTPS) | servers, admins and the site | no: server-side |

**What Delta Peer gives a PSP** [D]:
- **The platform key and policy:** 8 bytes in the handshake: platform, most players it hosts, most it joins, memory class. **The host protects the weakest machine:** it takes no more players than the smallest join limit in the game, so the advertisement shows the game full. The original Xbox's row is 16/16, 64 MB, no co-op. This is the "PSP-friendly server" enforcement, already designed, on every ChupathingyCE server with no setup.
- **The co-op gate:** a host offers no network co-op while a platform without it is in the game.
- **The legacy table relay:** the host passes its signed table of compatible network versions (Ed25519, checked against built-in keys), so a PSP keeps joining newer hosts after additive version raises without an update (§3.4).
- **Map identity** (`ce_maps`): a Halo PC map's size and BLAKE2b hash. For the PSP, the bake would carry the source map's (§6); the PSP never hashes its own file.
- Platform icons on the scoreboard; profiles and moderation (not needed).

**What it doesn't change:** the record's layout (§3.1) and the tag order (§3.2). Delta caps how many play; the wire format still has to match.

**Cost** [D, compiled for the PSP]: `delta_peer.c` 20 KB and `delta_wire.c` 7 KB of code, Monocypher up to 65 KB (less after `--gc-sections`), plus `delta_peer_game.c` and `delta.c` (not sized yet). The session struct is 95 KB, 54 KB of it the host's 128 peers; sized for a PSP-hosted game it is much smaller. Roughly 100–150 KB in all.

**How it gets onto the PSP:**
1. **After a plain join works.** Delta falls back to OpenCE after 4 s, so it can't fix a broken join and mustn't be needed for one.
2. **The game's hooks** into Delta Peer are under `HALO_GAME_BROWSER` in the networking units, with the game browser, invites and stats; the PSP drops that flag. Either define it for the networking units and answer the browser's parts as "none", or (better) propose upstream a `HALO_DELTA_PEER` switch for the hooks alone. Warthog would want the same.
3. **Build** `delta_peer.c`, `delta_wire.c`, `delta_peer_game.c`, `delta.c` and Monocypher. `delta_peer.c` is a protocol core with no sockets or game code; `delta_peer_game.c` opens its socket through `xnet.c`, so it rides the same Wi-Fi.
4. **A PSP platform key.** The registry (`enum delta_platform`) and the policy table are upstream's: ask for a PSP number and row. Until then a key with platform "unknown" works: a host honours a key's own claim when it's lower than its platform's row.
5. **The PSP's own join check** ("Warthog's side"): at 128/128 the PSP no longer refuses big games by itself, so it skips advertised games above its join limit (overridable, as upstream's `network.platform_limits = "off"`).
6. **Take no tables at first** (TABLE_HAVE 0xFFFFFFFF), then turn the relay on once an Ed25519 check is timed on the PSP [U: expected tens of ms, once].
7. **Test** against the X3D's server: HELLO and WELCOME in both logs, the PSP in the roster, the server's advertisement full at the PSP's limit.

## 8. Co-op, and testing at size

**How co-op works** [D, `port/linux/NETCODE.md`, `server/src/dedicated.c`]:
- The same netcode and the same shape as multiplayer: one host, clients that talk only to it. "Peer to peer" in ChupathingyCE's docs means no central server is needed, not that clients talk to each other.
- **A player's game hosts it** (Create Game, Map screen). The dedicated server refuses campaign levels.
- **The host runs the AI and the level's scripts**, spawns players and decides damage. It sends clients the actors' controls, cinematics, devices, BSP switches and the scripts' effects. Clients drive their own player, and the host's units from what it sends.
- Everyone stays on the host's BSP; cutscenes are skipped by vote; dead players watch a teammate and come back beside one.
- The host can add enemies per extra player (`network.coop_enemies_mode`).

**What that means for a PSP** [D, U]:
- **As a client:** no AI and no scripts. On b30's flight the AI was 11.9 ms of a 46 ms tick [M, b112]. The objects (34 ms there) and drawing remain, plus every remote player.
- **Memory is the hard part.** b111 played b30 to its end with the heap peaking at 5,047 KB of 5,952 [M, `hardware_b111.md`]. The multiplayer build's heap is 4,352 KB, and its 6 MB tag window can't hold a campaign level, which is baked for 9 MB [D]. So co-op on the campaign would be a build of its own: the 9 MB window with the 128/128 netcode, about 1.6 MB to find (the levers in §2; co-op keeps its own buffers) [U].
- **As a host:** the whole campaign plus the netcode for everyone else. Not a near-term target.
- **A PC hosting without a player** has a route: a desktop copy with `debug.network_test = "host:<level path>:coop"` and `debug.null_renderer` hosts co-op by itself [D: `network_test.c`; U: run that way for long].
- Delta's co-op gate (§7) lets a host refuse co-op while a PSP is in the game, until the PSP's row says yes.
- **One unified build** (campaign and multiplayer in one EBOOT) is a different problem from co-op: the campaign only has to stop paying for the netcode while it isn't playing (its buffers allocated when a network game starts; the window's unused part lent per map). Co-op is a network game on a campaign map, so it pays for everything at once [U].

**Co-op at scale: speed against capacity** (2026-10-10)

The host adds enemies for each player past the first [D, `port_config.c`]:

| `network.coop_enemies_mode` | Each squad a level places becomes |
|---|---|
| `"none"` | as the Xbox placed it |
| `"per_player"` (the default; `coop_enemies` 50%, 25–200) | 1 + 50% × the players past the first: 1.5× at 2 players, 2.5× at 4, **8.5× at 16** |
| `"multiplier"` (`coop_enemies_multiplier`, 2–32) | that many times as large, at any number of players |

ChupathingyCE's menus default co-op to 16 players (`COOPERATIVE_DEFAULT_PLAYERS`, `menu_functions.c`) [D], the community's target too (Sheepdog01).

Two kinds of limit stand in the way, and only one moves with speed-ups:

| Kind | What | Today | Moves with |
|---|---|---|---|
| **Capacity** | AI units a client can drive (`MAXIMUM_NETWORK_ACTORS`: the actor table + 32 for cutscenes) | PSP 288 (the Xbox's 256 actors); PC 1,056 | memory only: the actor table is 457 KB of the game state for 256 [E, log], and the game state has ~42 KB spare, so more comes out of the heap |
| **Capacity** | the host's objects | PSP below 1,024 (of 2,048); PC below 4,096 | memory only (the game state's object pool) |
| **Speed** | the client's tick | b30's flight: 46 ms, objects 34, AI 12 [M, b112] | the performance work |

- **1,056 is a ceiling, not a game:** the PC's actor table (1,024) plus 32. A normal co-op fight is a fraction of it.
- **Speed, roughly** [U]: a biped costs ~0.8 ms a tick [M, `big_levers.md`]; a co-op client skips the AI; 30 ticks a second needs a tick under 33 ms, even after the full tick/draw split. b30's flight has 27 bipeds (21.6 ms). At 4 players and the default scaling that's ~67 bipeds, ~54 ms of bipeds alone. The big levers (L1–L3) mostly speed up drawing; a biped's own cost needs the structural work (data-oriented loops, collision walks), not measured yet.
- **So:** Xbox-sized encounters (`"none"`) look reachable; a light setting (+25% per player, 2 players) may be; the default at 4+ players isn't, even after the big levers. Extra enemies hit the capacity limits before the speed limit.
- **Direction (Sheepdog01, 2026-10-10): memory can be found** (the levers in §2 and §4); **compute is the next limit to tackle for co-op at scale.**
- **The tag window gives co-op little:** the campaign maps filled 94–99% of the 9 MB window on 2026-10-06 (`campaign_window_fit.md`); the grass and slim blocks taken out since free roughly 0.5–1 MB, so the unused part is likely 0.5–1.5 MB a map [U: to measure from a current bake].

**Testing player counts with two PSPs and two PCs** [D]:
- **`tools/system_link_bots.py`** (ChupathingyCE's): adds up to 127 machines to a game, one player each. They follow the protocol but neither simulate nor move. That fills a game for memory, decoding and join-limit tests, from one PC.
- **Moving players:** desktop copies with `debug.test_input = "bot:<seed>"` walk, strafe, turn, fire, jump and throw grenades on their own. With `debug.null_renderer` they draw nothing, so one PC runs several. Each copy needs its own address (loopback aliases on one machine; one address per copy on the LAN).
- **The PSPs themselves:** `xinput_sdl.c` is in the PSP build, so `bot:<seed>` should drive a PSP's player too, for unattended runs [U: not tried].
- A ladder: 2 → 4 → 8 → 16 idle bots, then the same with moving copies, reading the PSP's tick, heap and Wi-Fi traffic at each step.

## 9. Decisions

Made (Sheepdog01, 2026-10-10):
1. **Mirror the PC wherever memory allows:** 128/128 on the PSP, not a translation layer.
2. **First target: a PSP joins a PC server, on the stock multiplayer maps, in a small game (4, 8 or 16 players).** PSP ↔ PSP is the fallback. Multiplayer and co-op both, at a scale still to be found.
3. **Bake speed** is worth improving later; server owners can pre-bake meanwhile.
4. **Co-op at scale:** memory can be found; compute is the next thing to tackle (§8).

Open:
5. **A PSP platform number and policy row** in Delta's registry: upstream's call (§7). With it, ask for **co-op's extra enemies off while a PSP is in the game** (§8): the host has the setting, but nothing turns it off for a weaker machine by itself.
6. **Sharing PSP bakes of community maps.** The rule so far is that baked maps never leave the local network; that rule was written for maps made from the disc. A repository of baked community maps would be a deliberate exception, with the repository's owner.

## Building it

```
# the multiplayer build (any machine with the toolchain)
HALO_REPO=<engine> PSP_OUT=<out> PSP_MULTIPLAYER=1 python3 psp_build.py --jobs 8
```

- **Its maps:** a bake from 2026-10-10 on (`mp-recon`): the multiplayer maps and ui built for the 6 MB window, with the disc's tag IDs. Older bakes' multiplayer maps won't load in it (their BSPs sit past 6 MB); the campaign build loads both. On the X3D: `/bake/mp5` (all 13 and ui; tag handles checked).
- **In the lab:** `sierra-bake` makes the maps, `sierra-tagorder` checks their tag handles, `sierra-test` with `env=PSP_MULTIPLAYER=1` runs a map offline, `sierra-netplay` joins the dedicated server.
