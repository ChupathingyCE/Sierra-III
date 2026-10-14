# Build 120: the foundation for the Media Engine work (2026-10-13)

Labels: **[M]** measured on a PSP (the PSP-3000 rig's figures are marked as such until Sheepdog01's PSP-1000 runs this build), **[E]** emulator, **[D]** read in the code, **[U]** inference.

**Read `docs/review_b120.md` first:** every change since b119, its key lines and why.

`halo_psp_b120.zip` (SHA-256 `60e85e62218d503cf39b195c3f8fcb850bac845af3472ec50a31b58b52ab7198`), EBOOT `ab2c0211ef9b52954ec818b922b09502c80c90e956b8d45365382775ae41d80f`: built from commit `84646c2` on `w120` (lab run 38486771413). Heap 5,836 KB (b119: 5,844); 0 symbols stubbed. Built with **its own profile** (`campaign-b120`: counted at `6c1fd25` on the new maps, 13 runs, 156 units), `PSP_PGO=use PSP_PGO_PROFILE=campaign-b120 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone`.

**New texture packs: `campaign120`.** The same as `campaign116f` except the HUD sheets: 17–21 per map stay at 8 bits instead of 16 colours. Only the `.ptex` files changed; the install below copies just those.

## What's in it

| Change | Kind | On? | Off word |
|---|---|---|---|
| Engine on ChupathingyCE `v0.7.4e` | upstream | yes | n/a |
| Resting weapons and equipment sleep | exact speed | yes | `itemsawake` |
| HUD sheets at 8 bits | restoration (bake) | yes | `--hud-clut4` at bake time |
| `hudsnap`, `hud177` | look: your A/B | no | n/a |
| `mehalt`, `mefinishslow` | ME groundwork | no | n/a |
| code layout (`hotgroup`) | exact speed | not yet (b121) | n/a |

**Settled on the rig already** [M, rig]:
- `mehalt` works on real hardware (halts = wakes, 0 failed jobs, 22 µs a wake) but gives no frame gain, so it ships off.
- The tick, broken down: a50's is mostly biped physics (8.7 ms a tick, 668 µs a call); c20's mostly posing and **53 machines at 6.5 ms a tick**. That's the input for the tick levers after the ME phases.

### On the rig already (PSP-3000, the ladder on this exact EBOOT, lab run 38486781171) [M, rig]

Every map from The Pillar of Autumn to Keyes in one session, no crash (the wall stopped it in d40's opening, as planned). Second half of each map's 90 s; `tools/ladderreport.py` makes this table.

| Level | fps | Tick (ms) | Ticks a second | b119 on the rig: fps / tick (ms) |
|---|---:|---:|---:|---|
| The Pillar of Autumn (a10) | 14.9 | 6.6 | 25.6 | 14.7 / 6.6 |
| Halo (a30) | 7.0 | 10.5 | 14.0 | 6.9 / 11.5 |
| Truth and Reconciliation (a50) | 9.3 | 31.3 | 18.3 | 9.1 / 32.6 |
| The Silent Cartographer (b30) | 4.0 | 56.7 | 8.1 | 3.9 / 59.1 |
| Assault on the Control Room (b40) | 14.9 | 16.1 | 28.1 | 14.9 / 16.5 |
| 343 Guilty Spark (c10) | 9.3 | 21.3 | 18.5 | 8.9 / 23.2 |
| The Library (c20) | 8.0 | 26.0 | 15.6 | 7.9 / 26.4 |
| Two Betrayals (c40) | 13.7 | 15.3 | 26.8 | 13.0 / 16.4 |
| Keyes (d20) | 8.2 | 16.3 | 16.3 | 7.6 / 16.8 |

The tick is 2–9% faster on every map but a10, and fps is up on 8 of 9: the rebase, the resting items and the fresh profile together. b30's flight on the rig: 231.7 ms a frame against 235.9 with b119's profile on the same code (lab runs 38482644668, 38472249947).

## On the PSP

**Install** (the EBOOT, the presets and the new maps):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b120.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b120 is on the stick"
set maps ~/psp-bake/campaign120/halo/maps
cp -v $maps/ui.ptex $psp/halo/maps/
for m in a10 a30 a50 b30 b40 c10 c20 c40 d20 d40
    cp -v $maps/$m.ptex $maps/$m"_track.ptex" $psp/halo/maps/
end
rm -f $psp/halo/shots/*
umount $psp
```
Only the texture packs changed in this bake (every other file is byte-identical to `campaign116f`'s), so the copy is 21 files, about 300 MB.

The log's first line should say `bring-up build 120`, its next `pgo: the hot code laid out…`.

**After each test, pull the log** (change the name):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b120_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
rm -f $psp/halo/shots/*
umount $psp
```

**The tests, in this order.** The preset number is what to pick on the launch menu's third page, not the order.

| # | Map, preset | Play | Stop | Shape | What it answers |
|---|---|---|---|---|---|
| 1 | The Silent Cartographer (b30), **3** (cinematic bench) | Hands off | It quits by itself at game time 70 s | Short | The milestone number: b120 against b119's 162.2 ms |
| 2 | b30, **1** (play) | The landing and the beach fight | When the fight's done | Medium | **By feel:** anything off? The weapons dropped on the beach now sleep: do they still get picked up, knocked around by blasts, and look right? |
| 3 | b30, **4** (`hudsnap`) | The landing; a screenshot with the HUD in view | Once you've looked | Short | HUD step 1: sharper at the same size? |
| 4 | b30, **5** (`hudsnap hud177`) | as 3 | as 3 | Short | HUD step 2: 1:1 and ~18% bigger. Worth the size? |
| 5 | The Pillar of Autumn (a10), **2** (the ladder) | Hands off: each opening for 90 s of game time | It quits by itself after The Maw (d40) | Long: start it on the charger and leave it | Every map on b120: the new baseline on your PSP-1000 |
| 6 | a10, **6** (the ladder, items awake) | as 5 | as 5 | Long | Optional: with test 5, what the items' sleep is worth on each map |

Test 2's screenshot (the usual button) also gives the plain HUD for comparing with 3 and 4.

**With one evening:** 1, then 2, 3 and 4 back to back, then start 5 on the charger and leave it. Test 6 on a later evening.

**Also worth a moment (optional):** put the PSP to sleep mid-game and wake it. The ME is held in reset over a sleep; nothing in b120 changes that path, but it's cheap to confirm.
