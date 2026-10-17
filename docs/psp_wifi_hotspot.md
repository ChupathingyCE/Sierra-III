# PSP Wi-Fi via the desktop hotspot (Linux, NetworkManager)

The test network for PSP multiplayer, shared by the PSP ports (C&C first, then Halo): the desktop's spare Wi-Fi card runs a hotspot, and the desktop reaches the internet over Ethernet. `multiplayer_setup.md` is the step-by-step for Sierra-III; this is the reference behind it. `multiplayer_recon.md` covers the PSP's network API.

**Status (2026-10-11):**
- **Joining works.** WPA2-AES through ARK-4's patch: the PSP gets an address and answers pings (§4). C&C and Sierra-III (homebrew, `sceNetApctlConnect` on the Sony profile) join too.
- **Broadcasts don't reach the PSP under the WPA2 patch** (§6). A game's lobby search depends on them, so the host must answer the PSP's own address.

Labels: **[M]** measured on hardware or on the desktop; **[code]** ARK-4 source (`7c03c37`, 2026-08-02); **[U]** inference.

## 1. Hardware and constraints

| | | |
|---|---|---|
| Desktop Wi-Fi | Intel Wireless-AC 9260, `iwlwifi`, interface `wlan0`; Ethernet is `enp6s0` (I211) | [M] |
| AP limits | `#{ managed } <= 1, #{ AP } <= 1, #channels <= 1`. The card does nothing else, so the channel is free to choose | [M] |
| Intel AP mode | 2.4 GHz only (firmware policy); few clients; not a use case Intel supports | [web] |
| Backend | NetworkManager on `wpa_supplicant` (iwd inactive) | [M] |
| Firewall | ufw, default deny incoming and forwarded | [M] |
| Channel | 1. Crowded, but no overlapping neighbour; 6 is hit by a 40 MHz network on channel 8 | [M] |

Your interface names may differ: `ip link` lists them.

## 2. The hotspot profile

```bash
nmcli connection add type wifi ifname wlan0 con-name psp-ap autoconnect no ssid PSP-LAN \
  802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel 1 \
  802-11-wireless.cloned-mac-address permanent \
  wifi-sec.key-mgmt wpa-psk wifi-sec.proto rsn \
  wifi-sec.pairwise ccmp wifi-sec.group ccmp wifi-sec.pmf disable \
  wifi-sec.psk "<8+ characters>" \
  ipv4.method shared ipv6.method disabled
nmcli connection up psp-ap      # on
nmcli connection down psp-ap    # off
nmcli connection modify psp-ap wifi-sec.psk "<new>"   # then down/up to apply
```

- A simple password is fine, with the hotspot up only while it's needed.
- `ipv4.method shared`: NetworkManager runs dnsmasq for DHCP (<device-address>–254) and NATs out through Ethernet. **The desktop is always <hotspot-address>**: a fixed address for a dedicated server. dnsmasq has given the PSP the same address each time (<device-address> here).
- `proto rsn` + `ccmp` = WPA2-AES, for ARK-4's WPA2 patch (§3).
- **WPA1-TKIP (`proto wpa`, `pairwise tkip`, `group tkip`) doesn't work on this desktop** [M, 2026-10-10]: the PSP (patch off, profile WPA-PSK TKIP) reports "AP did not let us join". Probably the AP side refusing TKIP, which is deprecated [U]; not chased.
- WPA passwords need 8–63 characters.
- Duplicate connection names are allowed; NetworkManager picks one. Keep a single `psp-ap`.

**Open test profile** (no encryption; for short tests only, anyone nearby can join while it's up):

```bash
nmcli connection add type wifi ifname wlan0 con-name psp-open autoconnect no ssid PSP-OPEN \
  802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel 1 \
  ipv4.method shared ipv6.method disabled
```

ufw rules (hotspot interface only):

```bash
sudo ufw allow in on wlan0 to any port 67 proto udp comment 'psp-ap: DHCP'
sudo ufw allow in on wlan0 to any port 53 comment 'psp-ap: DNS'
sudo ufw route allow in on wlan0 out on enp6s0 comment 'psp-ap: internet via NAT'
sudo ufw allow in on wlan0 to any port 1234 proto udp comment 'C&C PSP: LAN game'
sudo ufw allow in on wlan0 to any port 5150 proto udp comment 'halo: system link'
sudo ufw allow in on wlan0 to any port 5150 proto tcp comment 'halo: game connection'
```

## 3. ARK-4's WPA2 patch [code]

- Setting `wpa2` in ARK-4's `SETTINGS.TXT`; for homebrew it must be on in that context: `always, wpa2, on` (§7).
- Settings are read once per boot of each context (XMB, homebrew, game), just before `sceImpose_Driver` starts (`core/systemctrl/src/loadercore.c`). `always` matches every context (`plugin.c`, `matchingRunlevel`). Holding SELECT at that moment skips the settings.
- The patch is applied by the module-start hook whenever `sceNetApctl_Library` starts (`core/compat/psp/syspatch.c`), whoever loads it. So nothing in ARK makes it XMB-only.
- `core/compat/psp/wpa2.c` rewrites the WPA code path into WPA2 at fixed offsets of the 6.61 module: the WPA element in the join request (vendor tag `0xDD`) becomes an RSN element (tag 48, 20 bytes). While it is on, WPA1 and WEP don't work.
- The patch only changes what happens once the firmware is told "WPA". An app that builds its own connection settings decides that itself.
- WPA2-AES only, 2.4 GHz only.

## 4. Findings (2026-10-10)

**XMB (Sony's settings): works** [M]. Join request 65 bytes: 43 + a 22-byte RSN element (2-byte header + the patch's 20-byte `rsn_info`).

**Kommander r1029 (installed 16 Sep): two bugs of its own, neither in ARK** [M]:
1. **With the Sony profile** (the XMB one): join request 43 bytes, SSID and 802.11b rates only, **no security element**, Privacy bit set (`capab_info=0x11`), the way a WEP client joins. hostapd: `STA did not include WPA/RSN IE ... possible WPS use`, EAP, `authorized: no`. So Kommander didn't hand the Sony profile's security type to the firmware.
2. **With a profile made in Kommander's own network setup:** the join request is right (65 bytes, RSN), but every handshake fails with `invalid MIC in msg 2/4`, then deauth reason 15 (handshake timeout) and a rejoin about every 5 s. Same with a lowercase-only password, so it isn't the characters.
   - `tools/psp_mic_check.py` recomputes the PSP's message 2 MIC from a `msgdump` capture: **the PSP signs with the all-zero key.** The correct key, `PBKDF2(password, SSID)`, doesn't match; neither do typos, other SSIDs or PBKDF2 parameter mistakes.
   - A chance match on the zero key is ~2⁻¹²⁸, which also validates the checker on real PSP data.
   - Confirmed end-to-end: with the hotspot's key set to 64 hex zeros, the handshake ran through messages 3/4 and 4/4 and the keys were installed. The PSP still dropped the link (reason 3) and never got an address.

**Fixed by updating Kommander to R1311-BETA (2026-10-01)** [M]: its network setup GUI has been reworked. With the password back to the normal one, the PSP connected and got **<device-address>**.

**First latency sample** [M], desktop → PSP, `ping`, idle, PSP next to the desktop: 25/25 replies, **2.1–4.6 ms steady**, the first two 12 and 14 ms (avg 3.7 ms, mdev 2.9 ms).

**For the ports: confirmed** [M, C&C r1002; Sierra-III mp1]. `sceNetApctlConnect(<Sony profile>)` joins the WPA2 hotspot like the XMB: `authorized: yes`, address <device-address>. Each port first needed memory for the network modules (Sierra-III measured 574 KB, `multiplayer_plan.md` §4).

**Earlier wrong turn:** this note first concluded "patch inactive in Kommander". The bytes showed no WPA element at all, which the patch can't cause.

**Side finding:** the Memory Stick had a FAT error from an interrupted write (a log file longer than its cluster chain, both FATs differing, dirty bit). Seen as `Input/output error` in `grep` and `fat_bmap_cluster: request beyond EOF` in `dmesg`. Repaired with `fsck.vfat -a`; check with `fsck.vfat -n` (report only) on the unmounted device.

## 5. Diagnostics that worked

| Question | Command |
|---|---|
| Joined and keyed? | `iw dev wlan0 station dump` → `associated` / `authorized` |
| Handshake events | `journalctl -fu wpa_supplicant` |
| What the PSP sent | `sudo wpa_cli -p /run/wpa_supplicant -i wlan0 log_level debug`, then grep the journal for `ASSOC_REQ`, `WPA/RSN IE`, `4-Way`, `MIC`, `EAPOL`. Set back to `info` after |
| Which key the PSP used | `log_level msgdump`; take `Send EAPOL-Key msg` (message 1) and the matching `RX EAPOL data` (message 2, same replay counter, byte 16); `python3 -I tools/psp_mic_check.py --msg1 … --msg2 … --ap … --sta … --ssid … --passphrase …` |
| DHCP traffic | `sudo tcpdump -ni wlan0 port 67 or port 68` (before the firewall) |
| Whole log of one attempt | `journalctl -u wpa_supplicant --since "-2min" > attempt.txt`: better than guessing grep patterns |
| Game traffic, both ways | `sudo tcpdump -ni any udp port 5150 -c 20`: the `any` capture names the interface and direction (`wlan0 In/Out/B`) |

How to read them:
- **At wpa_supplicant's normal level, a failed join logs nothing.** `AP-STA-CONNECTED` appears only once the handshake has finished. Use the debug level from the first try.
- `associated: yes`, `authorized: no`: the 4-way handshake never completed, so no data (DHCP included) passes. The PSP shows "obtaining IP address".
- `ASSOC_REQ len=43` from the PSP: no security element. `len=65`: RSN element present (patched WPA2).
- `invalid MIC in msg 2/4`: the PSP's key differs from the AP's. The key is PBKDF2(password, SSID, 4096), so the SSID counts too.
- `IEEE 802.1X: version=1 type=1 length=0` (an EAPOL-Start) from the PSP, then message 1 unanswered and `reason 15`: ARK-4's `wpa2` setting is off for that context (§7).
- `Unsupported authentication algorithm (1)`: Shared Key, a WEP profile.
- `SCAN-FAILED ret=-95`: the card can't scan in AP mode. Harmless.
- Deauth `reason_code=3`: the PSP leaving; `15`: the AP gave up on the handshake; `2` from the AP: previous authentication no longer valid.

A handshake capture lets anyone holding it test passwords offline: use throwaway passwords while debugging.

On the PSP: WLAN Power Save off (latency spikes otherwise). Homebrew often connects to profile 1 without asking: keep the working profile first (§7).

## 6. Broadcasts under the WPA2 patch (2026-10-10, C&C)

**Finding** [M]. With the PSP on the WPA2 hotspot (patch on) in C&C's Join list, the desktop answered its lobby queries with a small script:

| Answer sent to | PSP lists the game? |
|---|---|
| The PSP's own address (<device-address>, unicast) | **yes** |
| The hotspot's broadcast address (<broadcast-address>) | **no** |
| Same, on the **open** hotspot (`psp-open`) | **yes** |

- `tcpdump` showed every broadcast answer leaving on `wlan0`, so routing is not the cause.
- Traffic from the PSP is fine either way: a client's packets go to the AP one-to-one, broadcasts included. Only broadcasts **to** the PSP are lost.
- Power save was off throughout.

**Why** [U]. Under WPA2, unicast frames use the pair's own key, while broadcasts use a group key (GTK) that WPA2 delivers inside message 3 of the 4-way handshake. WPA1 delivers it in a separate group-key handshake. The patch turns the 6.61 firmware's WPA1 code into WPA2 at the join; if that code still waits for WPA1's separate group handshake, it never installs the GTK. It can then decrypt unicast and never broadcast. To confirm: a `msgdump` of the handshake (does message 3 carry the GTK, and does the PSP ever use it?), or ARK-4's source.

**What it means:**
- **Discovery breaks** wherever a PSP must hear a broadcast: a LAN game's search answers.
- **Once machines know each other's addresses, unicast works.**
- **Halo's answer:** the dedicated server answers searches to a list of addresses (`network.broadcast`, `HALO_NET_BROADCAST`), and the PSP's own address goes on it (`multiplayer_setup.md`).

**Other options:** an open hotspot while testing; a broadcast-to-unicast relay on the desktop (the desktop is the AP and sees every client); a fix upstream in ARK-4's `wpa2.c`, once the cause is confirmed.

**Also found** [M]: PPSSPP on the desktop reports its Ethernet address to the game, not the hotspot's, and sends its broadcasts to `255.255.255.255`, which leave through Ethernet. A temporary route makes them leave through the hotspot: `sudo ip route add 255.255.255.255/32 dev wlan0 src <hotspot-address>` (removed with `ip route del`, or by a reboot or the interface going down).

## 7. Sierra-III's first hardware runs (2026-10-10, mp1–mp4)

Three things that each cost several runs [M]:

1. **The connection number.** The game joins the PSP's saved connection 1 (or keep word `netprofileN`). The hotspot was connection 2; connection 1 (another network) turned the PSP away within ~2 s, with the WLAN light blinking once or twice, and the hotspot logged nothing. Check which one the game names: `network: joining network connection N`, then `network: on "<SSID>" as …` once joined.
2. **ARK-4's `wpa2` setting.** Left off for homebrew by an earlier test, the PSP still sent a 65-byte join request, then an EAPOL-Start (`version=1 type=1`) in place of message 2 of the handshake, never answered message 1, and the hotspot dropped it every ~3 s (`reason 15`). The PSP's log: `joining` ↔ `exchanging keys`, then `no connection after 20 s`. Turned on (`always, wpa2, on`), the PSP had an address 8.3 s after the game started.
3. **A server bound to one address hears no broadcasts.** Linux delivers a broadcast only to sockets bound to 0.0.0.0, and the dedicated server binds the hotspot's address (`network.address`). So the PSP sends its searches to the server's address (`network.broadcast = "<hotspot-address>"` in the PSP's `config.toml`), and the server answers the PSP's address (§6).
