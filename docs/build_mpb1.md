# mpb1: the ONLINE GAMES screen (2026-10-15)

The first multiplayer build that lists the internet's public Halo games on the PSP and joins one from a screen of its own. It is a showcase track, by Sheepdog01's call of 2026-10-15, ahead of "campaign first"; the campaign build is untouched (`review_mpb1.md` §1).

## The build

| | |
|---|---|
| Name | `mpb1` (multiplayer series: mp1–mp5 before it) |
| Source | Sierra-III `mpbrowse` `b39d693`: `w131` + `mp5` + internet play; `PSP_MULTIPLAYER=1`, no PGO |
| Engine | ChupathingyCE v0.7.4e, network version 28, joining hosts of 11–30 (`psp_limits.h` had stayed at 24, so the PSP saw no current game) |
| EBOOT | sha256 `dac7c3fdba1c3e57…` (`tools/build_release.sh`, pinned me-core) |
| Zip | `halo_psp_mpb1_halomp.zip`: `PSP/GAME/halomp/` (EBOOT, both PRX modules) and `halomp/presets.txt` |
| Heap | 3,748 KB (mp5: 4,152; the trunk merge took 144 KB, internet play 280 KB) |

## What it does

1. **ONLINE GAMES:** the Xbox Multiplayer menu gets a fourth item, below System Link. It opens the PSP's screen (`psp_online.c`).
2. **The list:** every public game, read from internet play's MQTT brokers (`opence.milenko.org`, `broker.emqx.io`: plain TCP, the same listings the desktop's Server Browser reads). Each row shows server, map, type, players and ping.
3. **The PSP rating, first form:** a game is joinable only when it is on one of the 13 Xbox maps, that map is on this stick, it has no password, and it is open. Any other game is dimmed, with the reason shown under the list.
4. **Joining:** cross joins.
   1. The PSP joins the game's invite (the encrypted UDP tunnel to its host, through STUN and hole punching).
   2. It waits for the host's game to appear through the tunnel.
   3. It joins that game and opens the pregame lobby.
   4. If 20 s pass with no answer, it says so and marks the game failed for this run.
5. **Controls:** up and down pick, left and right turn the page, square refreshes, circle backs out (or stops waiting).

## In the emulator [E]

Lab `sierra-netplay` with `host=online` (one PSP, no server, keep word `onlinelist` or `onlinetest`):

| Run | What | Result |
|---|---|---|
| 38841260151 | `onlinelist`, campaign121's ui | queued behind other sessions' batches |

## On hardware: Sheepdog01's PSP-1000

**Questions only the PSP answers:**
1. Does the list fill over real Wi-Fi?
2. How long does a join take?
3. What is the heap top in a real public game? The margin is thin (`review_mpb1.md` §3).

**Install (fish):**

```fish
pspmount
unzip -o ~/Downloads/halo_psp_mpb1_halomp.zip -d $psp      # PSP/GAME/halomp, halomp/presets.txt
umount $psp
```

**What it needs:**
- A Wi-Fi connection that reaches the internet, saved as connection 1. The PC hotspot from `multiplayer_setup.md` does (its NAT rule), and so does a router that still allows 802.11b, with ARK-4's `wpa2` setting on.
- The stock maps in `ms0:/halo/maps`, baked for this engine (the lab's `mp131` bake: ui and Blood Gulch so far). Without them the list still fills; every game just reads "Map not on this Memory Stick".

**Test 1 (the list):**
1. Launch Halo MP.
2. Go to Multiplayer → ONLINE GAMES and wait about 30 s.
3. Pass: rows appear, and their counts change on square.

**Test 2 (a join):**
1. Pick a "Ready to join" game on Blood Gulch.
2. Press cross.
3. Pass: the pregame lobby opens, and the game plays.

**Logs:**

```fish
tar czf $HOME/psp/logs/mpb1_test1.tgz -C $psp/halomp log
```
