# mpb3: the public list proven in the emulator (2026-10-16)

mpb2 plus four fixes found by running the list against the live lobby in the emulator. mpb2 was never tested on a PSP; mpb3 replaces it, and the test below is mpb3's.

## The build

| | |
|---|---|
| Name | `mpb3` |
| Source | Sierra-III `mpbrowse` `7f0dc04` (on w131); `PSP_MULTIPLAYER=1`, no PGO |
| EBOOT | sha256 `a7af65591c369cdc…` (`tools/build_release.sh`, pinned me-core) |
| Zip | `halo_psp_mpb3_halomp.zip`: `PSP/GAME/halomp/` and `halomp/presets.txt` |
| Heap | 3,724 KB; heap top at the menu with the list up: 1,524 KB [E] |

## What changed since mpb2 (`review_mpb2.md` covers mpb2 itself)

1. **One row per game:** a host lists its game once per network version it speaks. One server took ten rows; now it takes one.
2. **Co-op games named as such:** "Co-op: not on the PSP yet", where mpb2 said "Custom map".
3. **The clock offset needs three agreeing listings:** in the emulator one host three hours off moved the offset by itself. Now three live signed listings must agree, and only while none has yet been in time.
4. **The map check asks the firmware** (`sceIoGetstat`, `psp_internet.c`). A game unit's stdio goes through the engine's file layer, which found no maps.
5. **The hands-free join tries a dedicated server first,** and waits 30 s for the tunnel, not 20.

## In the emulator [E] (lab `sierra-netplay host=online`, `halo-net` variant)

| Run | What | Result |
|---|---|---|
| 38857365803 | mpb2, the batches' emulator | every broker connection EINVAL: its patch binds them to 127.0.0.0 |
| 38860509066 | mpb2, `halo-net` (connections leave from the host) | **the list works:** brokers ready, 216 listings heard, none turned away, 46–50 public games; duplicates and a co-op label wrong (fixed) |
| 38862487031 | mpb3's fixes | 39–41 games, one row each; Blood Gulch on the stick; 4 games joinable; joined one: brokers carried the JOIN, STUN resolved, the tunnel's UDP stuck on 127.0.0.0 |
| 38863306976 | mpb3, `halo-net` binding where the game asks | joined "[C] Blood Gulch", a dedicated server: STUN went out, the tunnel never connected. Rootless Podman's NAT on the runner is the suspect [U], not the PSP |

**What it proves:** the PSP side of the list, end to end against the live lobby. Joining reaches the tunnel; only a real network can show the tunnel itself.

## On the PSP-1000 [M] (lab `sierra-hw` 38918999662, `data=halomp`)

The rig ran mpb3 (`dff6bd4`, its code as at `7f0dc04`) with `nearauto nomeaudio onlinetest onlinedone150`, the hotspot as its Wi-Fi, all 13 maps on the stick (bake `mp131`).

**What worked, start to finish:**
1. The PSP joined the hotspot; both brokers ready, none failed.
2. The PSP's clock reads 1970-01-01. Three agreeing hosts set the offset (+1,792,162,321 s), so the three-agreement rule (change 3) is what let the list fill.
3. 418 listings heard, 44 public games shown.
4. The hands-free join picked HALO UNIVERSAL (dedicated, carousel, 11 players, network version 29). The tunnel connected; the PSP joined as machine #8, loaded carousel and joined the game in progress at tick 15,317. Its player spawned.

**Where it stopped: the PSP can't keep up with a 12-player game.**

| After the join (4 heartbeats) | |
|---|---|
| Tick | 43 ms average (60, 43, 35, 36); the host's 30 ticks a second leave 33.3 ms for the whole frame |
| Game speed | 16–28 ticks a second, against the host's 30 |
| Frames | 0.4–1.0 a second, each running 27–39 ticks to catch up |
| Where the tick goes | bipeds 21 ms (30 biped updates a tick), weapons 12 ms (49 a tick, 42 asleep) |
| Heap top | 3,612 of 3,744 KB |

About 20 s after the join the client gave up (`timeout in network_connection_idle`, "the game host went down") and went back to the main menu. It gives up after 15 s without the host's reliable messages, so they stopped within about 10 s of the join. The likeliest reason [U]: the tunnel's thread runs at 0x5c, far below the game thread's 0x20 (mpb4's log [E]), and the game thread's catch-up frames ran 1–2.5 s without waiting, so the tunnel got no time (`milestone_first_public_game.md`).

**Not the cause:** the run's one failed heap request was the GE's 1,056 KB vertex-copies arena, which fell back to the general heap as built.

**Open from this run:**
- Why 30 biped updates a tick with 12 players [U]. If the client updates remote players' bipeds more than once a tick, that is the multiplayer lever.
- Weapons on the ground cost 0.25 ms each a tick though most are asleep.
- The campaign's speed work lands here too: every tick lever shared with the campaign shortens this one.

## Test 1 (PSP-1000): the list, and a join

**Install (fish).** The second line puts Blood Gulch on the stick, from this machine (it never touches GitHub); skip it if it's already there from last night.

```fish
pspmount
sudo cp ~runner/psp-bake/mp131/halo/maps/bloodgulch* $psp/halo/maps/
unzip -o ~/Downloads/halo_psp_mpb3_halomp.zip -d $psp
echo "nearauto nomeaudio onlinetest" > $psp/halomp/keep.txt
umount $psp
```

**Run it:**
1. Launch Halo MP and leave it on the main menu.
2. After 5 s ONLINE GAMES opens by itself, and the list should fill.
3. After 30 s it joins the first joinable Blood Gulch game, a dedicated server if there is one.
4. If the pregame lobby opens, play a minute.
5. Quit with HOME.

**Pull the log:**

```fish
pspmount
tar czf $HOME/psp/logs/mpb3_test1.tgz -C $psp/halomp log
umount $psp
```

**Pass:**
- **The list:** rows appear, with `online games: brokers:` showing "ready".
- **The join:** the lobby opens. "The host didn't answer" means the tunnel couldn't get through the hotspot's and the router's NATs.
