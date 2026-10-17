# Multiplayer

The multiplayer build plays Halo PC's online games from a PSP. It lists the public games that ChupathingyCE servers announce on the internet, and it joins one from its **ONLINE GAMES** screen. The PC players in those games see a PSP as one more player.

It is a separate build from the campaign: "Halo PSP multiplayer" in the XMB, beside "Halo PSP". The two share the maps on your Memory Stick.

**What it does today:**
- **Joins** public games on the thirteen multiplayer maps from the Xbox disc, with no password.
- **Doesn't host.** It can't create a game, and two PSPs can't play each other directly (ad hoc). Both come back later, with campaign co-op.
- **No custom maps.** A game on a map that isn't on the disc is listed but can't be joined.
- **Slow in busy games.** The PSP runs at 5–8 fps in a game with a few players (a PSP-1000 on Prisoner with 7 players started at 7.7 fps) and slower as fights grow. Expect a quiet server to play best.

If you haven't set up the campaign yet, do that first: `play.md` takes you from a stock PSP and your own disc to a playable game. This page adds only what multiplayer needs.

## 1. What you need

- **The campaign's setup working:** a PSP on custom firmware (6.61 with ARK-4) and the maps baked from your own disc. Any PSP model works.
- **The multiplayer maps in your bake:** `ui` and the thirteen multiplayer maps (`beavercreek bloodgulch boardingaction carousel chillout damnation hangemhigh longest prisoner putput ratrace sidewinder wizard`). A full bake has them; if you baked only some maps, bake these too: `--maps ui,beavercreek,bloodgulch,…` (`bake/README.md`).
- **The multiplayer zip:** the release's `halo_psp_bNNNmp.zip`, on the same release page as the campaign zip and the bake bundle, from the same release as your campaign build.
- **A Wi-Fi network with internet** that the PSP can join:
  - **WPA2-AES (CCMP) or open.** WPA3 and WPA2/WPA3 mixed modes don't work.
  - **2.4 GHz.** The PSP-1000 speaks only 802.11b, so on a router that turns 802.11b off (many do), it can't join. The PSP-2000 and 3000 also speak 802.11g.
  - A phone's hotspot set to WPA2 and 2.4 GHz works, and so does a PC's (§7).

## 2. Install

With the stick mounted at `$STICK`:

```bash
unzip -o halo_psp_bNNNmp.zip -d $STICK          # PSP/GAME/halomp/
```

The multiplayer build reads the maps from the campaign build's `halo/maps/`, so there is nothing more to copy. It keeps its own settings, saves and logs in `halomp/`, which it creates on its first launch.

| Path | What |
|---|---|
| `PSP/GAME/halomp/` | the multiplayer build (the zip) |
| `halo/maps/` | the bake, shared with the campaign build |
| `halomp/config.toml` | the game's settings, written at the first launch |
| `halomp/keep.txt` | keep words (optional, §8) |
| `halomp/log/`, `halomp/crash.txt` | what the game writes |

The stick rules are the campaign's: delete only inside `halo/`, `halomp/`, `PSP/GAME/halo/` and `PSP/GAME/halomp/`; leave ARK-4's files alone.

## 3. ARK-4 and the PSP's network settings

1. **ARK-4's WPA2 setting, for homebrew.** The PSP's own firmware knows only WPA1; ARK-4 patches in WPA2. Add this line to ARK-4's `SETTINGS.TXT` (one `runlevel, setting, on/off` a line; see the [ARK-4 wiki's settings page](https://github.com/PSP-Archive/ARK-4/wiki/settings)):

   ```
   always, wpa2, on
   ```

   - `always` covers homebrew, where the game runs. A setting for the XMB alone isn't enough.
   - ARK-4 reads its settings at boot: restart the PSP after changing it.
   - An open network doesn't need it.
2. **The WLAN switch** (the PSP-1000's left side): on.
3. **Settings → Network Settings → Infrastructure Mode:** add your network, security **WPA-PSK (AES)**, with its password. ARK-4 turns WPA into WPA2 when it joins.
   - **Save it as connection 1.** The game joins connection 1 without asking. To use another, put `netprofileN` in `halomp/keep.txt` (§8).
   - **Test Connection** in the same menu should succeed.
4. **Settings → Power Save Settings → WLAN Power Save: Off.** With it on, the radio dozes and games lag.

## 4. Play

1. Start **Halo PSP multiplayer**. It boots to the game's main menu; the WLAN light shows it joining your network.
2. **Multiplayer → ONLINE GAMES.** The list fills over a few seconds.
3. Each row is a game: its name, map, game type, players (in it / most), ping, and whether you can join it.
   - **Up/down** picks a game, **left/right** turns the page.
   - **Cross** joins, **square** refreshes, **circle** goes back (while a host is being reached, it stops waiting).
4. A game the PSP can't play is dimmed, with the reason:
   - `Custom map: not on the PSP yet`: a map that isn't on the disc.
   - `Co-op: not on the PSP yet`: a campaign game played together.
   - `Map not on this Memory Stick`: a disc map missing from your bake (§1).
   - `Password games: not yet`, `Not accepting players`, `Couldn't reach its host`.

   After you pick a game, the status line may say `That game can't be joined from this version.` (a host too new or too old, §6) or `The host didn't answer.`
5. After you join, the host's pregame lobby opens; the game starts when the host starts it.
6. **To quit:** HOME → ARK-4's Kommander → Exit.

Some servers turn the PSP away even when the list shows them joinable. `[C] Grifball`'s Open Pitch, for example, plays only between PC builds that have its Bomb mode, and refuses the PSP when it joins.

## 5. If it doesn't work

**First, the log.** The game rewrites `halomp/log/log.txt` at every launch and keeps the last 8 launches' as `log/NNN_log.txt`. Copy it off after a run that went wrong:

```bash
tar czf mp_test.tgz -C $STICK/halomp log crash.txt
```

(`crash.txt` is there only after a crash; drop it from the command if it's missing.) Attach that file to an issue.

| What you see | Look for in `log.txt` | Cause | Fix |
|---|---|---|---|
| `Internet play is off (network.online in config.toml).` | | `online = false` under `[network]` in `halomp/config.toml` | Set it to `true`, or delete the line |
| The list stays empty | `network: the access point refused the PSP or went out of range`, or no `network: on "<SSID>" as <address>` line | The PSP didn't join your Wi-Fi | §3: connection 1, WPA2-AES, 2.4 GHz with 802.11b allowed (PSP-1000), ARK-4's `wpa2` on, then restart the PSP |
| The WLAN light blinks once or twice, then nothing | `network: joining network connection N` with another network | Connection 1 is another network | Save yours as connection 1, or `netprofileN` |
| Joined Wi-Fi, but the list stays empty | `online games: lobby: browsing; heard 0` | No internet on that network, or a firewall blocking outgoing TCP | Try a phone's hotspot |
| Every game says it can't be joined from this version | `not joining a host of network version N` | ChupathingyCE moved to a network version this build doesn't know | A newer release; meanwhile a fresh `legacy.json` (§8) |
| Joined, then dropped back to the menu | `_transport_error_endpoint_io` or `links dead` above 0 | The connection to the host broke | A closer host (lower ping), a stronger signal, WLAN power save off |
| Very low frame rate, the game lags then rushes | `heartbeat: … fps` lines | The PSP is behind the host in a busy game | A quieter server; this is being worked on |

## 6. Network versions

ChupathingyCE's servers announce an OpenCE network version. This build announces version 29 and joins hosts from 11 to 30, the same as ChupathingyCE 0.7.5d. If ChupathingyCE moves to a newer version before a new release, the PSP can read a newer table from `halomp/legacy.json`:

```bash
curl -sL https://raw.githubusercontent.com/ChupathingyCE/chupathingyce/delta-table/legacy.json -o $STICK/halomp/legacy.json
```

Without the file, the built-in numbers apply (the log says `no legacy table; the built-in numbers: announcing 29, joining 11 to 30`).

## 7. Advanced: your own server, over a PC's hotspot

A Linux PC can run ChupathingyCE's dedicated server and its own Wi-Fi hotspot, and the PSP joins it from **Multiplayer → System Link**, with no internet. Useful when no public game is quiet enough, or to play against friends on desktop ChupathingyCE. The first whole PSP-versus-PC game was played this way (`milestone_first_netgame.md`).

```
PSP ── Wi-Fi, WPA2 ──▶ the PC's Wi-Fi card: hotspot, <hotspot-address> ──▶ dedicated server
                                                               ▲
                        desktop client (optional), on the PC's wired address
```

You need a Wi-Fi card that can be an access point (`iw list` shows `* AP` under "Supported interface modes"), NetworkManager, and ufw (or open the same ports in your firewall). Commands are bash; in fish, `set x (cmd)` where bash has `x=$(cmd)`. Below, `wlan0` is the Wi-Fi card and `enp6s0` the wired one (`ip link` shows yours).

**The hotspot** (once):

```bash
nmcli connection add type wifi ifname wlan0 con-name psp-ap autoconnect no ssid PSP-LAN \
  802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel 1 \
  802-11-wireless.cloned-mac-address permanent \
  wifi-sec.key-mgmt wpa-psk wifi-sec.proto rsn \
  wifi-sec.pairwise ccmp wifi-sec.group ccmp wifi-sec.pmf disable \
  wifi-sec.psk "<8 to 63 characters>" \
  ipv4.method shared ipv6.method disabled
sudo ufw allow in on wlan0 to any port 67 proto udp comment 'psp-ap: DHCP'
sudo ufw allow in on wlan0 to any port 53 comment 'psp-ap: DNS'
sudo ufw route allow in on wlan0 out on enp6s0 comment 'psp-ap: internet via NAT'
sudo ufw allow in on wlan0 to any port 5150 proto udp comment 'halo: system link'
sudo ufw allow in on wlan0 to any port 5150 proto tcp comment 'halo: game connection'
```

- `proto rsn` with `ccmp` is WPA2-AES, the only security ARK-4's patch speaks; `band bg` because the PSP-1000 is 802.11b only.
- `ipv4.method shared`: the PC runs DHCP and takes `<hotspot-address>`. The PSP gets a `10.42.0.x` address (Test Connection shows it).
- On and off: `nmcli connection up psp-ap` / `down`. Keep it up only while you play.

**The server**, from a ChupathingyCE checkout (the version in `UPSTREAM.md`), with the disc's own maps (not the PSP's bake) in its data folder:

```bash
cd <ChupathingyCE checkout> && ninja server-x64          # build/server-x64/chupathingyce-server
mkdir -p ~/halo-server/maps ~/halo-server/playlists
python3 -I <Sierra-III checkout>/bake/xiso.py <disc image> ~/halo-server/maps ui bloodgulch
echo "bloodgulch slayer" > ~/halo-server/playlists/psp.txt

PSP=<device-address>                                        # the PSP's address
LAN=$(ip -4 -o addr show enp6s0 | grep -oP 'inet \K[\d.]+')   # a desktop client's, if one plays
cd ~/halo-server
env HALO_DATA_ROOT=$PWD HALO_SAVE_ROOT=$PWD/save \
    HALO_DEDICATED=playlists/psp.txt HALO_DEDICATED_NAME=sierra-mp HALO_DEDICATED_PUBLIC=false \
    HALO_DEDICATED_MAXIMUM_PLAYERS=16 HALO_DEDICATED_MINIMUM_PLAYERS=1 \
    HALO_NET_ONLINE=false HALO_NET_BROWSER= HALO_NET_LIST_GAMES=false \
    HALO_NET_REPORT_GAMES=false HALO_NET_REPORT_EVENTS=false HALO_NET_ALLOW_UPNP=false \
    HALO_NET_ADDRESS=<hotspot-address> HALO_NET_BROADCAST=$PSP,$LAN \
    <ChupathingyCE checkout>/build/server-x64/chupathingyce-server
```

- `HALO_NET_BROADCAST`: the server answers searches to these addresses one by one. Under ARK-4's WPA2 patch the PSP hears no broadcasts.
- The `false`s and the empty `HALO_NET_BROWSER` keep the server off the public list.
- Its log is `debug.txt` in the data folder; Ctrl+C stops it.

**The PSP's searches** go to the server's address: launch the multiplayer build once, quit, and on the stick:

```bash
sed -i -E 's/^(# )?broadcast = .*/broadcast = "<hotspot-address>"/' $STICK/halomp/config.toml
grep '^broadcast =' $STICK/halomp/config.toml      # one line
```

(Linux delivers a broadcast only to sockets bound to `0.0.0.0`, and the server is bound to `<hotspot-address>`.)

**A desktop client** (optional) on the same PC:

```bash
cd <ChupathingyCE checkout> && ninja linux64             # build/linux64/halo
cfg=build/linux64/config.toml
LAN=$(ip -4 -o addr show enp6s0 | grep -oP 'inet \K[\d.]+')
sed -i -E -e "s/^(# )?address = .*/address = \"$LAN\"/" \
    -e 's/^(# )?broadcast = .*/broadcast = "<hotspot-address>"/' \
    -e 's/^(# )?online = .*/online = false/' $cfg
```

The server tells machines apart by address, so the client takes the PC's wired one.

**Play:** hotspot up, server started, then on the PSP **Multiplayer → System Link → `sierra-mp`** (and the same on the desktop client).

**When it doesn't work, on the PC:**
- Did the PSP join the hotspot? `iw dev wlan0 station dump` shows `authorized: yes`.
- A refused join logs nothing at the normal level: `sudo wpa_cli -p /run/wpa_supplicant -i wlan0 log_level debug`, then `journalctl -fu wpa_supplicant`. An EAPOL-Start from the PSP, then `reason 15`, means ARK-4's `wpa2` setting is off for homebrew.
- Joined the hotspot but no game in System Link: the two 5150 rules (`sudo ufw status numbered`), the PSP's address in `HALO_NET_BROADCAST`, `broadcast = "<hotspot-address>"` in the PSP's `config.toml`.
- Game traffic: `sudo tcpdump -ni any udp port 5150`. Collect the server's `debug.txt` with the PSP's log: `tar czf mp_test.tgz -C $STICK/halomp log -C ~/halo-server debug.txt`. The PSP's clock may be hours off the PC's: match the two logs by events.

## 8. Settings and keep words

Keep words go on one line in `halomp/keep.txt`. None is needed to play.

| Where | Name | What it does |
|---|---|---|
| `keep.txt` | `netprofileN` | Join the PSP's saved connection N (default 1) |
| `keep.txt` | `netpaceN` | Every N ms, give the Wi-Fi driver 1 ms of the CPU (default 5; `netpace0` off). Without it, busy games drop the PSP |
| `keep.txt` | `netpoolN` | The network stack's pool in KB (64–1024; default 256) |
| `keep.txt` | `netbufN` | A socket's buffers in KB at most (default 32) |
| `config.toml`, `[network]` | `broadcast` | Where System Link searches go (§7) |
| `config.toml`, `[network]` | `legacy_table` | A network-version table other than `halomp/legacy.json` (§6) |
| `halomp/legacy.json` | (the file) | ChupathingyCE's network-version table (§6) |

The reasons behind each step, for the curious: `psp_wifi_hotspot.md` (the network), `multiplayer_plan.md` (the engine), `mp_client_only.md` (why it doesn't host), and the build notes `build_mp1.md` to `build_mpb4.md` (what each failure looked like).
