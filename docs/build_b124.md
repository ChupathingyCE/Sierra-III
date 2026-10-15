# Build 124: the ME back after a sleep, the level's wobble, no lost cut pieces, sleeping weapons (2026-10-14)

`review_b124.md` lists every change and why. The bake is unchanged (`campaign121`); the PGO profile is still b122's.

## On the PSP

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b124.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b124 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 124`.

**After each test, pull the log:**
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b124_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests, in this order** (preset numbers on the launch menu's third page):

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | The Silent Cartographer (b30), **1** | The landing and the beach fight. Mid-fight, sleep the PSP for ~10 s, wake it, play a minute | When the fight's done | The sleep fix: sound and speed as before the sleep. The log should say "ME: restarted after wake" |
| 2 | Two Betrayals (c40), **1**, then **9** | The opening cutscene and the bridge | After the bridge | LK22: steady level on 1, the old wobble on 9. Chief's armour whole |
| 3 | The Pillar of Autumn (a10), **1** | The flyby, then walk up to Captain Keyes | After Keyes | The window frame steady; Keyes whole up close |
| 4 | b30, **1** | Sit on the Pelican bench and look at the wall behind you | When you've looked | The wall there (now by the new path) |
| 5 | Keyes (d20), **1**, then **5** | The corridors' doors | When you've looked | Doors at their right brightness on 1, the old brightness on 5 (sleeping machines' functions) |
| 6 | b30, **3** (cinematic bench) | Hands off | It quits by itself at game time 70 s | Speed against b123 |
| 7 | b30, **8** | The landing and the beach | When you've looked | `cutless`: anything look different? (it's −1 ms a frame in the emulator) |
| 8 | a10, **2** (the ladder), on the charger | Hands off | It quits by itself after d40 | Every map on b124 |

**First sitting:** 1 to 5. Then 6 and 7, and leave 8 on the charger.
