# Multiplayer: setting it up

How to play the PSP build against a PC: a PSP-1000 joins ChupathingyCE's dedicated server on a Linux PC, over that PC's own Wi-Fi hotspot, and a desktop ChupathingyCE client can join the same game. This setup played the first whole PSP-vs-PC game on 2026-10-11 (`milestone_first_netgame.md`).

The reasons behind each step are in:
- `psp_wifi_hotspot.md`: the network;
- `multiplayer_plan.md`: the engine;
- `build_mp1.md` to `build_mp4.md`: what each failure looked like.

Commands are bash. In fish, write `set x (cmd)` where bash has `x=$(cmd)`.

## The picture

```
PSP-1000 ── Wi-Fi, WPA2 ──▶ the PC's Wi-Fi card: hotspot, <hotspot-address> ──▶ dedicated server
                                                                  ▲
                            desktop client, on the PC's wired address
```

| Machine | Address | Why it matters |
|---|---|---|
| The PC, hotspot side | always `<hotspot-address>` | NetworkManager's shared mode gives itself this address. The server binds it |
| The PSP | `10.42.0.x`, from the hotspot's DHCP (`<device-address>` so far, every time) | The server must name it: under ARK-4's WPA2 patch, the PSP hears no broadcasts |
| Desktop client (optional) | the PC's wired address | The server tells machines apart by address, so the client can't share `<hotspot-address>` |

## What you need

- **A PSP-1000** on 6.61 with ARK-4. Others are untested.
- **ARK-4's `wpa2` setting on for homebrew.** Without it, the PSP can't finish joining a WPA2 network (§4).
- **A Linux PC with a Wi-Fi card that can be an access point.** Check with `iw list`: `* AP` must appear under "Supported interface modes". The card runs the hotspot and nothing else, so the PC stays online over Ethernet.
- NetworkManager and ufw. With another firewall, open the same ports.
- **Your own Halo Xbox disc image.** It is the source of the PSP's bake and of the server's maps. No game data is in any repository.
- Toolchains: pspdev, this repository's **`mp-recon` branch**, and ChupathingyCE at the pin in `UPSTREAM.md`.

## 1. The PSP build

```bash
git checkout mp-recon
HALO_REPO=<engine checkout> PSP_OUT=<out> PSP_MULTIPLAYER=1 python3 psp_build.py --jobs 8
make -C psp/crash && make -C psp/profile
```

What `PSP_MULTIPLAYER=1` changes:
- builds the netcode back in, with the PC's limits;
- shrinks the tag window to 6 MB, which makes room for the network modules;
- keeps the build's files in `ms0:/halomp`;
- names the build "Halo PSP multiplayer" in the XMB. It sits beside the campaign build.

**Pack it.** The game finds its two helper modules beside its `EBOOT.PBP`:

```bash
mkdir -p pkg/PSP/GAME/halomp
cp <out>/EBOOT.PBP psp/crash/halo_crash.prx psp/profile/halo_profile.prx pkg/PSP/GAME/halomp/
cp <me-core-mapper checkout>/LICENSE.md pkg/PSP/GAME/halomp/LICENSE-mecore.md
(cd pkg && zip -r ../halo_psp_mpN.zip PSP)
```

## 2. The maps

Bake from the `mp-recon` checkout. Its bake does two things the multiplayer build needs:
- **It keeps the disc's tag IDs** (`multiplayer_plan.md` §3.2). The netcode names object types by tag handle, so the PSP's maps must match the PC's.
- **It builds the multiplayer maps and `ui` for the 6 MB window.** Bakes from `main`, or from before 2026-10-10, won't load in the multiplayer build.

```bash
bake/make_bundle.sh <bundle>            # once; needs the Invader fork built (bake/INVADER.md)
<bundle>/halo-psp-bake <disc image> <maps out> --maps ui,bloodgulch
```

- Always include `ui`.
- Blood Gulch with `ui` comes to ~90 MB.
- The campaign build loads these maps too, so one `halo/maps` folder serves both builds.

## 3. The hotspot (on the PC)

**Find your interface names** with `ip link`. Below, `wlan0` is the Wi-Fi card and `enp6s0` is Ethernet.

**Make the profile once:**

```bash
nmcli connection add type wifi ifname wlan0 con-name psp-ap autoconnect no ssid PSP-LAN \
  802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel 1 \
  802-11-wireless.cloned-mac-address permanent \
  wifi-sec.key-mgmt wpa-psk wifi-sec.proto rsn \
  wifi-sec.pairwise ccmp wifi-sec.group ccmp wifi-sec.pmf disable \
  wifi-sec.psk "<8 to 63 characters>" \
  ipv4.method shared ipv6.method disabled
```

- `proto rsn` with `ccmp` is WPA2-AES, the only security ARK-4's patch speaks. The PC refused WPA1-TKIP.
- `band bg`: the PSP-1000 is 802.11b only.
- `ipv4.method shared`: NetworkManager runs DHCP on the hotspot, and the PC takes `<hotspot-address>`.

**Open the firewall on the hotspot only:**

```bash
sudo ufw allow in on wlan0 to any port 67 proto udp comment 'psp-ap: DHCP'
sudo ufw allow in on wlan0 to any port 53 comment 'psp-ap: DNS'
sudo ufw route allow in on wlan0 out on enp6s0 comment 'psp-ap: internet via NAT'
sudo ufw allow in on wlan0 to any port 5150 proto udp comment 'halo: system link'
sudo ufw allow in on wlan0 to any port 5150 proto tcp comment 'halo: game connection'
```

**Turn it on or off:** `nmcli connection up psp-ap` (or `down`). Keep it up only while you play.

## 4. The PSP

1. **ARK-4:** add `always, wpa2, on` to ARK-4's `SETTINGS.TXT`. The format is one `runlevel, setting, on/off` a line; see the [ARK-4 wiki's settings page](https://github.com/PSP-Archive/ARK-4/wiki/settings).
   - `always` covers homebrew, where the game runs. A setting for the XMB alone isn't enough.
   - Settings are read at boot, so restart the PSP after changing it.
2. **The WLAN switch** on the PSP-1000's left side: on.
3. **Settings → Network Settings → Infrastructure Mode:** save `PSP-LAN` with security WPA-PSK (AES) and its password. ARK-4's patch turns WPA into WPA2 at the join.
   - **Save it as connection 1.** The game joins connection 1 without asking (keep word `netprofileN` picks another).
4. **Test Connection** in the same menu. It shows the PSP's address, which the server needs (§5).
5. **Settings → Power Save Settings → WLAN Power Save: Off.** With power save on, the radio dozes and latency spikes.

## 5. The server (on the PC)

**Build** it from the same ChupathingyCE checkout:

```bash
cd <engine checkout>
ninja server-x64          # build/server-x64/chupathingyce-server
```

**Its data folder:** the disc's own maps (not the PSP's bake) and a playlist. `bake/xiso.py` copies maps out of the disc image:

```bash
mkdir -p ~/halo-server/maps ~/halo-server/playlists
python3 -I bake/xiso.py <disc image> ~/halo-server/maps ui bloodgulch   # run from this checkout
echo "bloodgulch slayer" > ~/halo-server/playlists/psp.txt
```

**Start it.** The hotspot must be up first, because the server binds `<hotspot-address>`:

```bash
PSP=<device-address>                                    # the PSP's address (§4, step 4)
LAN=$(ip -4 -o addr show enp6s0 | grep -oP 'inet \K[\d.]+')   # the desktop client's, if one plays
cd ~/halo-server
env HALO_DATA_ROOT=$PWD HALO_SAVE_ROOT=$PWD/save \
    HALO_DEDICATED=playlists/psp.txt HALO_DEDICATED_NAME=sierra-mp HALO_DEDICATED_PUBLIC=false \
    HALO_DEDICATED_MAXIMUM_PLAYERS=16 HALO_DEDICATED_MINIMUM_PLAYERS=1 \
    HALO_NET_ONLINE=false HALO_NET_BROWSER= HALO_NET_LIST_GAMES=false \
    HALO_NET_REPORT_GAMES=false HALO_NET_REPORT_EVENTS=false HALO_NET_ALLOW_UPNP=false \
    HALO_NET_ADDRESS=<hotspot-address> HALO_NET_BROADCAST=$PSP,$LAN \
    <engine checkout>/build/server-x64/chupathingyce-server
```

What the settings do:
- **`HALO_NET_ADDRESS`**: system link runs on the hotspot.
- **`HALO_NET_BROADCAST`**: the server answers searches to these addresses one by one, instead of by broadcast. The PSP hears no broadcasts under ARK-4's WPA2 patch (`psp_wifi_hotspot.md` §6).
- **`HALO_DEDICATED_MINIMUM_PLAYERS`**: how many players the lobby waits for before the game starts.
- **The `false`s and the empty `HALO_NET_BROWSER`** keep the server off every list. No invite links, no reports.
- The server's log is `debug.txt` in the data folder. Ctrl+C stops it.

**The legacy table.** ChupathingyCE's machines agree on which OpenCE network version to announce. They take it from a signed table passed between them (Delta Peer).
- The PSP has no Delta Peer, so it reads the same table from a file on the stick (§6).
- If a PC hands the server a newer table than the stick's, the PSP refuses the host. The PSP's log then says `not joining a host of network version N`.
- To pin the server instead, give it a local table: `HALO_LEGACY_TABLE=<file>`, or `legacy_table` under `[network]` in the server's `config.toml`. The file's format is in `build_mp1.md`.

## 6. The stick

| Path | What |
|---|---|
| `PSP/GAME/halomp/` | the build's zip (§1) |
| `halo/maps/` | the bake (§2), shared with the campaign build |
| `halomp/config.toml` | the game's settings. The first launch writes it, with the network settings commented out |
| `halomp/legacy.json` | ChupathingyCE's legacy table (§5) |
| `halomp/keep.txt` | keep words (optional, below) |
| `halomp/log/`, `halomp/crash.txt` | what the game writes |

**Install**, with the stick mounted at `$STICK`:

```bash
unzip -o halo_psp_mpN.zip -d $STICK
mkdir -p $STICK/halo/maps $STICK/halomp
cp <maps out>/halo/maps/* $STICK/halo/maps/
curl -sL https://raw.githubusercontent.com/ChupathingyCE/chupathingyce/delta-table/legacy.json -o $STICK/halomp/legacy.json
```

- The table is the one the PCs use. Refresh it when the PSP refuses a host's version (§9).

**Launch once,** quit, and mount the stick again. Then point the PSP's searches at the server:

```bash
sed -i -E 's/^(# )?broadcast = .*/broadcast = "<hotspot-address>"/' $STICK/halomp/config.toml
grep '^broadcast =' $STICK/halomp/config.toml      # one line
```

- Why: Linux delivers a broadcast only to sockets bound to `0.0.0.0`, and the server is bound to `<hotspot-address>`. So the PSP's searches go straight to the server's address.

**Optional keep words** (`halomp/keep.txt`, one line): `nearauto assettrack geo2 sound1024`. The first whole game also had `nomeaudio`, which is no longer needed: the Media Engine and Wi-Fi run together under game traffic (2026-10-17, two `[C] Slayer` joins on the PSP-1000), and the word costs the main CPU the audio mixing, the slant sort and the pose job.

## 7. The desktop client (optional)

```bash
cd <engine checkout>
ninja linux64             # build/linux64/halo; its first start sets up its maps
cfg=build/linux64/config.toml
LAN=$(ip -4 -o addr show enp6s0 | grep -oP 'inet \K[\d.]+')
sed -i -E -e "s/^(# )?address = .*/address = \"$LAN\"/" \
    -e 's/^(# )?broadcast = .*/broadcast = "<hotspot-address>"/' \
    -e 's/^(# )?online = .*/online = false/' $cfg
grep -E '^(address|broadcast|online) =' $cfg      # three lines
```

- `address`: the PC's wired address, so it isn't the server's.
- `broadcast`: its searches go to the server.
- `online = false`: system link only.
- To play in a window: F11, or `mode = "windowed"` under `[display]`.

## 8. Play

1. Hotspot up: `nmcli connection up psp-ap`.
2. Start the server (§5).
3. Start the desktop client, if one plays: Multiplayer → System Link → `sierra-mp`.
4. Start "Halo PSP multiplayer" on the PSP. It starts on the game's main menu, and the WLAN light shows it joining.
5. On the PSP: Multiplayer → System Link → `sierra-mp`.
6. To quit on the PSP: HOME → Exit (with ARK-4's Kommander: HOME → Kommander → Exit).

**Hands-free join (for tests):** with `network_test = "join"` under `[debug]` in `halomp/config.toml`, the PSP searches and joins by itself from the main menu. Set it to `""` to stop.

## 9. Logs, and when it doesn't work

**Collect after every run.** `log.txt` is wiped at each launch.

```bash
tar czf mpN_test.tgz -C $STICK/halomp log crash.txt -C ~/halo-server debug.txt
```

- `crash.txt` is only there after a crash. Drop it from the command if it's missing.
- The PSP's clock may differ from the PC's by whole hours (Sheepdog01's: 5 h behind). Match the two logs by events, not by time.

**The useful lines** in the PSP's `log.txt`:
- `network: joining network connection N`
- `network: on "<SSID>" as <address>`: joined the hotspot
- `successfully joined a net game`
- `network: the stack's pool …`: every heartbeat while joined

**On the PC:**
- Did the PSP join the hotspot? `iw dev wlan0 station dump` shows `authorized: yes`.
- The hotspot's own log: `sudo wpa_cli -p /run/wpa_supplicant -i wlan0 log_level debug`, then `journalctl -fu wpa_supplicant`. At the normal level, a failed join logs nothing.
- Game traffic: `sudo tcpdump -ni any udp port 5150`.

| What you see | Cause | Fix |
|---|---|---|
| The WLAN light blinks once or twice; the hotspot logs nothing | The PSP is joining another saved connection | Save the hotspot as connection 1, or keep word `netprofileN` (§4) |
| PSP log: `joining` ↔ `exchanging keys`, then `no connection after 20 s`. Hotspot (debug level): an EAPOL-Start from the PSP, then `reason 15` | ARK-4's `wpa2` setting is off for homebrew | `always, wpa2, on`, then restart the PSP (§4) |
| PSP joined the hotspot, but System Link shows no game | The firewall, or the server isn't answering the PSP's address | The two 5150 rules (`sudo ufw status numbered`); the PSP's address in `HALO_NET_BROADCAST`; `broadcast = "<hotspot-address>"` in the PSP's `config.toml` |
| "net game handler failed" in red. PSP log: `not joining a host of network version N` | The server announces a version outside the stick's legacy table | A fresh `halomp/legacy.json` (§6), or pin the server (§5) |
| The main menu stalls with music and no animation | The hands-free join retrying a host it refuses | As the row above; or `network_test = ""` |
| Joined, then nothing sent or received after a few seconds | The network stack's memory pool ran dry (fixed in mp3) | The `network: the stack's pool …` lines; keep words `netpoolN`, `netbufN` |
| `CRASH: bad address (load)` in packet code | A value read at an odd address (fixed in mp2) | `build_mp1.md`, Test 1, has how to resolve the address |
| Server log: predictions refused; the PSP lags, then rushes | The PSP's long frames leave it behind, and it catches up too fast | Open (`build_mp3.md`); frame time is the fix |

## Settings and keep words (multiplayer build)

| Where | Name | What it does |
|---|---|---|
| `keep.txt` | `netprofileN` | Join the PSP's saved connection N (default 1) |
| `keep.txt` | `netpoolN` | The network stack's pool in KB (64–1024; default 256) |
| `keep.txt` | `netbufN` | A socket's buffers in KB at most (default 32; `netbuf0`: what the engine asks, 1 MB) |
| `config.toml`, `[network]` | `broadcast` | Where the PSP's searches go: `"<hotspot-address>"` |
| `config.toml`, `[network]` | `legacy_table` | A legacy table file other than `ms0:/halomp/legacy.json` |
| `config.toml`, `[debug]` | `network_test` | `"join"`: search and join by itself |
| `ms0:/halomp/menu.txt` | (the file) | Start on the launch menu instead of the game's main menu |

## Ad hoc: PSP to PSP, no PC (from mp5)

Two or more PSPs play each other directly: no hotspot, no server, no PC. One PSP hosts and plays.
- On every stick: the multiplayer build, the bake, and `adhoc` in `halomp/keep.txt`.
- The same Ad Hoc channel on every PSP (Settings → Network Settings → Ad Hoc Mode).
- One PSP creates the game from System Link; the others join it.

PCs can't join an ad hoc game; the hotspot and ChupathingyCE servers above stay the main way to play. Details: `build_mp5.md`.

## Without hardware

The emulator can run the same join. `tools/emu.sh` with `DATA=halomp` uses the multiplayer build's folder.

The lab's `sierra-netplay` workflow runs it all on one machine:
- the dedicated server on `127.0.0.1`;
- the PSP build in PPSSPP on `127.0.0.2`.

Its `strict=1` input runs the strict interpreter. That interpreter reports the unaligned accesses the PSP faults on and the JIT forgives, so run network changes under it before hardware.
