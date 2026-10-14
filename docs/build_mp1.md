# mp1: the first multiplayer build for the PSP (2026-10-10)

The multiplayer build's first hardware test: a PSP joins ChupathingyCE's dedicated server on the desktop, over the desktop's hotspot, then a 16-player game. Background: `multiplayer_plan.md`. The hotspot: `psp_wifi_hotspot.md`. The whole setup as it stands, step by step: `multiplayer_setup.md`.

## The build

| | |
|---|---|
| Name | `mp1`: its own series, outside the performance builds' `bNNN` (Sheepdog01, 2026-10-10) |
| Source | Sierra-III `mp-recon` `43ab51f`, `PSP_MULTIPLAYER=1`, ChupathingyCE base |
| EBOOT | sha256 `abedd7aca624e1a3…`; `halo_psp_mp1.zip` (unzips to `PSP/GAME/halomp/`) |
| On the stick | `PSP/GAME/halomp`, beside the campaign build; the XMB calls it "Halo PSP multiplayer" |
| What's in it | the PC's limits (128 players, 128 machines), Wi-Fi and sockets (`psp_net.c`), the 6 MB tag window |
| What isn't | b112's speed-ups: `mp-recon` branched from `main` before them, so expect b111's frame rate |

**Its maps:** the 6 MB bake, `/bake/mp5` on the X3D (`~/psp-bake/mp5/halo/maps` for Sheepdog01). Blood Gulch needs `ui.*` and `bloodgulch.*` (87 MB without the `_track` pack). The campaign build loads these too, so they replace the old ones in `$psp/halo/maps`.

**Proved in the emulator** (`sierra-netplay`): joins the server, loads Blood Gulch, plays for minutes at 30 ticks a second, heap top 3,528 of 4,352 KB.

## Setup, once

### A. The desktop: firewall and server

ufw denies incoming on the hotspot (`wlan0`); the server needs port 5150 (searches and game datagrams over UDP, the game's connection over TCP):

```fish
sudo ufw allow in on wlan0 to any port 5150 proto udp comment 'halo: system link'
sudo ufw allow in on wlan0 to any port 5150 proto tcp comment 'halo: game connection'
```

The server, from the same checkout and commit as the desktop client (the PSP's network version, 24):

```fish
cd ~/games/chupathingyce
ninja server-x64
```

Its data folder: the client's maps (the disc's, which the client copied next to `halo` on its first start) and a playlist:

```fish
mkdir -p ~/games/halo-server/playlists
ln -s ~/games/chupathingyce/build/linux64/maps ~/games/halo-server/maps
echo "bloodgulch slayer" > ~/games/halo-server/playlists/mp1.txt
```

### B. The desktop client

The server and the client share one machine, so each gets its own address: the server the hotspot's (<hotspot-address>), the client the wired one (`enp6s0`). The server tells machines apart by address.

```fish
set lan (ip -4 -o addr show enp6s0 | string match -r '\d+\.\d+\.\d+\.\d+')
set cfg ~/games/chupathingyce/build/linux64/config.toml
sed -i -E -e "s/^(# )?address = .*/address = \"$lan\"/" \
    -e 's/^(# )?broadcast = .*/broadcast = "<hotspot-address>"/' \
    -e 's/^(# )?online = .*/online = false/' $cfg
grep -E '^(address|broadcast|online) =' $cfg    # three lines, your values
```

- `broadcast`: the client's searches go to the server.
- `online = false`: system link only, no invite links.

### C. The stick

```fish
pspmount
df -h $psp                                       # the maps need ~90 MB free (the stick was ~93% full)
unzip -o ~/Downloads/halo_psp_mp1.zip -d $psp     # -> $psp/PSP/GAME/halomp
set src ~/psp-bake/mp5/halo/maps
cp $src/ui.map $src/ui.pgeo $src/ui.ptex \
   $src/bloodgulch.map $src/bloodgulch.pgeo $src/bloodgulch.ptex $src/bloodgulch.pstn $psp/halo/maps/
umount $psp
```

On the PSP: the hotspot's connection (`PSP-LAN`) saved as network connection 1 (or keep word `netprofileN`), and WLAN Power Save off.

### D. Test mode on the stick (and back)

Both builds share `ms0:/halo` (`init.txt`, `keep.txt`, `config.toml`, `log/`). The launch menu (the text list of maps at start-up) always names a map in `init.txt`, but a network join starts from the game's main menu, with no map named. Found by Test 0's first try: its log stopped at the launch menu. So for these tests the stick skips the launch menu (`nomenu.txt`) and names no map:

```fish
pspmount
cd $psp/halo
cp init.txt init.txt.campaign
echo -n > init.txt          # no map named: the game starts on its main menu
touch nomenu.txt            # no launch menu
cd ~; umount $psp
```

Back to the campaign's own setup after the tests:

```fish
pspmount
cd $psp/halo
mv init.txt.campaign init.txt; rm nomenu.txt
cd ~; umount $psp
```

(For a later build: a "main menu" entry in the multiplayer build's launch menu, or a data folder of its own, would make this unnecessary.)

## Starting the server

The hotspot must be up first (`nmcli connection up psp-ap`): the server binds <hotspot-address>. `MIN` is the players the game waits for (1, 2 or 16 below).

```fish
set MIN 1
set lan (ip -4 -o addr show enp6s0 | string match -r '\d+\.\d+\.\d+\.\d+')
cd ~/games/halo-server
env HALO_DATA_ROOT=$PWD HALO_SAVE_ROOT=$PWD/save \
    HALO_DEDICATED=playlists/mp1.txt HALO_DEDICATED_NAME=sierra-mp1 HALO_DEDICATED_PUBLIC=false \
    HALO_DEDICATED_MAXIMUM_PLAYERS=16 HALO_DEDICATED_MINIMUM_PLAYERS=$MIN \
    HALO_NET_ONLINE=false HALO_NET_BROWSER= HALO_NET_LIST_GAMES=false \
    HALO_NET_REPORT_GAMES=false HALO_NET_REPORT_EVENTS=false HALO_NET_ALLOW_UPNP=false \
    HALO_NET_ADDRESS=<hotspot-address> HALO_NET_BROADCAST=<device-address>,$lan \
    HALO_LEGACY_TABLE=$HOME/games/halo-server/legacy-24.json \
    ~/games/chupathingyce/build/server-x64/chupathingyce-server
```

- Off every list: no game list, no invite links, no reports.
- `HALO_LEGACY_TABLE`: the OpenCE version number the server announces, from a local file. Once:
  ```fish
  printf '%s\n' '{"delta_legacy": 1, "serial": 1, "wires": {"chupa-24a": {"announce": 24, "minimum": 11, "maximum": 25}}}' > ~/games/halo-server/legacy-24.json
  ```
  Without it the server follows ChupathingyCE's signed table, which a desktop client hands it over Delta Peer (2026-10-10: announce 25, join 11–25). The PSP has neither Delta Peer nor the table, so it joins hosts of 11–24 only and refuses one announcing 25 (`build_mp3.md`). The desktop client joins 24 either way.
- `HALO_NET_BROADCAST`: a host answers a search by broadcast to each address on this list. The PSP's own address (<device-address>, the address the hotspot has given it each time) and the desktop client's. Not the hotspot's broadcast address: under ARK-4's WPA2 patch the PSP hears no broadcasts (`psp_wifi_hotspot.md` §6). If the PSP's log shows another address (`network: on "PSP-LAN" as …`), use that.
- Ctrl+C stops it.

## The tests

Pull the logs after each one: `log.txt` is wiped at every launch. The server's `debug.txt` goes in the same file:

```fish
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/mp1_test1.tgz -C $psp/halo log -C ~/games/halo-server debug.txt
umount $psp
```

**Test 0: boot and Wi-Fi** (no server; 2 minutes; test mode on, D). Start "Halo PSP multiplayer"; at the game's main menu (the HALO title, not the launch menu's text list), wait 30 s; quit (HOME → ARK-4 Kommander → Exit). Pull as `mp1_test0.tgz`.
- Proves: the build starts, the network modules load, the PSP joins the hotspot.
- The log's answers: `network: the modules and the stack started …; user memory free … was …` (**the modules' RAM**, the one number the emulator can't give), `network: on "PSP-LAN" as 10.42.0.…`.
- This first launch also adds the multiplayer settings to `$psp/halo/config.toml` (commented out), which Test 1's edit needs.

**Test 1: the PSP joins** (server `MIN 1`; ~8 minutes). Turn on the scripted join once:

```fish
pspmount
sed -i -E -e 's/^(# )?broadcast = .*/broadcast = "<hotspot-address>"/' \
    -e 's/^(# )?network_test = .*/network_test = "join"/' $psp/halo/config.toml
grep -E '^(broadcast|network_test) =' $psp/halo/config.toml    # two lines
umount $psp
```

Start the server, then the PSP build: it searches, joins `sierra-mp1` and plays when the lobby counts down. Play (or stand) 5 minutes; quit. Pull as `mp1_test1.tgz`.
- Stop early if the PSP shows no game after 2 minutes at the menu.
- Look for: `successfully joined a net game`, `heartbeat … ticks/s`, `network test: tick … sent … received …`, `heap top`.
- To stop the auto-join later: set `network_test = ""` in the same file.

**Test 2: PSP and desktop** (server `MIN 2`; ~10 minutes). Start the server, the desktop client (join `sierra-mp1` from its system link list), then the PSP. Play each other 5 minutes. Pull as `mp1_test2.tgz`.

**Test 3: 16 players** (server `MIN 16`; ~10 minutes). Start the server, the desktop client and the PSP, then 14 stand-in machines from ChupathingyCE's tool. They join over loopback addresses and stand at their spawns:

```fish
cd ~/games/chupathingyce
python tools/system_link_bots.py --host <hotspot-address> --machines 14
```

The game starts at 16. Play 5 minutes; Ctrl+C the bots; quit the PSP. Pull as `mp1_test3.tgz`.
- Look for: the tick with 15 other bipeds, the heap top, Wi-Fi traffic (`sent`/`received` a second).

## Test 0 on hardware (2026-10-10): passed

The PSP's clock runs 5 hours behind the desktop's: a log written at 16:46 on the PSP is 21:46 in the desktop's journal.

| Run | Keep words changed | What happened |
|---|---|---|
| 0 | | stopped at the launch menu (it names a map; a join starts from the main menu) |
| 0b | | START without X: `b30` loaded, which doesn't fit the 6 MB window; stuck in `main_setup_connection`. The join failed (below) |
| 0c | | Blood Gulch loaded and played as a local game; the join failed |
| 0e | `nomeaudio`, no `hwprofile`, `sound1024` | the same failure |
| 0f | PSP-LAN moved to connection 1 | the PSP reaches the hotspot; the handshake fails |
| 0g | 0f plus `nomeaudio` | the same as 0f |
| 0h | 0g, with ARK-4's WPA2 setting back on | **joined**: `on "PSP-LAN" as <device-address>`, up in 8.3 s |

**Measured** [M]:
- The network modules: 574 KB, started in 276 ms (`multiplayer_plan.md` §4).
- Blood Gulch with them loaded (0c, a local game, 10 minutes at the spawn): ~26 ticks/s, ~17 fps, heap top 3,160 of 4,352 KB, 212 KB of user memory free. No faults.

**0–0e: the wrong network.** PSP-LAN was the PSP's network connection 2; the game joins 1 unless `keep.txt` says `netprofileN`. Connection 1 (another network) turned the PSP away within ~2 s, and the hotspot logged nothing. Sheepdog01 moved PSP-LAN to connection 1.

**0f–0g: the handshake stalls** [M]. The hotspot's debug log, for each of the four tries in the game's 20 s:
1. The PSP associates with a 65-byte request: the RSN element is there, so ARK-4's WPA2 patch applied.
2. The hotspot sends message 1 of the 4-way handshake.
3. The PSP sends an EAPOL-Start (`version=1 type=1 length=0`), which a WPA2-PSK hotspot ignores, and never message 2.
4. The hotspot resends message 1 three times (0.1 s, then 1 s apart), then drops the PSP (reason 15, the handshake timed out).

The PSP's log shows the same as `joining` ↔ `exchanging keys` every ~3 s, then `no connection after 20 s`. The game carries on without the network. Not the ME (0g) [M]. In the Kommander trace (`psp_wifi_hotspot.md` §4) the PSP answered message 1 within the same second, so under Halo the PSP's network software doesn't get to the handshake in time, or doesn't see message 1 [U].

**The cause: ARK-4's WPA2 setting was off** [M], left so by an earlier test (Sheepdog01). With it back on (0h), the handshake completes at once and the PSP has an address 8.3 s after the join starts. The join itself took 32 KB of user memory. Blood Gulch then ran at 28–30 ticks/s.

Open [U]: 0g's join request already carried a 22-byte element, the size of the patch's RSN element (WPA1's would make it 67 bytes). So the setting also decides how the PSP answers the handshake, not only what it asks to join with.

**For the next build:**
- Log each saved connection's name and network before joining: the connection number cost five runs.
- Log the access point's events with their error codes (`sceNetApctlAddHandler`), not only the polled states.
- The hotspot logs a failed handshake only at debug level (`wpa_cli … log_level debug`, `psp_wifi_hotspot.md` §5): use it from the first try.

## Test 1 on hardware (2026-10-10): a crash, fixed for mp2

The PSP joined PSP-LAN (7.3 s), loaded the main menu and started the scripted join; a moment after `network test: searching for games` it stopped [M]:

```
CRASH: bad address (load) in thread user_main at epc 0894d6c0 (ra 0894f2bc, badvaddr 09fbe8a1)
```

- `epc` and `ra` less 0x08804000 (where the program loads), through `psp-addr2line -f -e halo.elf`: `byte_swap_memory`, called by `data_encode_integer`, from `_data_packet_encode`. The PSP was writing its first search packet.
- A packet is written one value after another into a byte buffer, so a 2-byte value can start at an odd address (0x09fbe8a1); the engine then swapped its bytes in place with a 2-byte load. The PSP faults on that; a PC doesn't, nor does the emulator's JIT, which every network test so far ran on. Reading values back does the same.
- Sheepdog01 had pressed down on the D-pad at about that moment; the search is the cause, not the button.

**The fix** (`psp_fixups.py`, `FIXES_PACKET_ALIGNMENT`): the bytes are swapped one at a time, and values are read back with unaligned-safe loads (`PSP_UNALIGNED_SHORT` and the others, `psp_limits.h`). The same bytes go on the wire. To find any others first, the network test now runs on the strict interpreter too (the lab's `sierra-netplay`, `strict=1`), which reports what the JIT forgives.

## What can stop it on the first try

- **The network connection:** the game joins connection 1 (Settings → Network Settings) unless `keep.txt` says `netprofileN`.

- **The modules' RAM:** if the log shows the sound cache taken from the heap (`sound cache` … `memalign`), the keep word `sound1024` gives back 512 KB.
- **The firmware's error numbers:** `network: a socket call failed with errno N, which this doesn't know` names one `psp_net.c` must map.
- **The firewall:** a PSP that joins the hotspot but never sees the game: `sudo ufw status numbered` for the two 5150 rules.
- **The tick at 16** [U]: ~0.8 ms a biped a tick (`big_levers.md`), so ~+12 ms a tick; it may drop below 30 ticks a second. A measurement, not a failure.
