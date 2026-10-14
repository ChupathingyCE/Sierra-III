# Build 123: b122 plus faster code layout, the Pelican wall fixed, and A/B words (2026-10-14)

`review_b123.md` lists every change and why. In short:
- functions and loops aligned, and four loop-duplicating passes off: −1.8 to −3.7% a frame on the PSP-3000 [M];
- the Pelican wall restored (on by default);
- the AI profile meter skipped;
- lit items may sleep;
- the ME's cache hand-overs and the ground-weapon sleep lever built but off.

The PGO profile is b122's. The bake is unchanged (`campaign121`), so b122's texture packs stay on the stick.

## On the PSP

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b123.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b123 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 123`, and the launch menu's header `Halo PSP build 123  preset none` until you apply one. The zip brings b123's `presets.txt`: preset 6 logs stick reads, 13 is the old small cut buffer, and 14 pins the near plane.

**After each test, pull the log** (change the name):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b123_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests, in this order.** The preset number is what to pick on the launch menu's third page.

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | The Silent Cartographer (b30), **3** (cinematic bench) | Hands off | It quits by itself at game time 70 s | The new code layout on your 1000: against b122's 160.0 ms and b120's 158.7 |
| 2 | b30, **1** | Sit on the Pelican bench and look at the wall behind you; a screenshot | When you've looked | The wall is there without any preset word (the buffer grows by itself) |
| 3 | Two Betrayals (c40), **14** | The opening cutscene and the bridge; a screenshot or two | After the bridge | Does the level geometry stop bouncing with the near plane pinned? (LK22) |
| 4 | The Pillar of Autumn (a10), **14** | The exterior flyby: the frame around the bridge glass | After the flyby | The same question for a10's window frame |
| 5 | b30, **14** | The intro: the far water on the horizon | When you're in the Pelican | Does the far water stop flickering? (LK13) |
| 6 | b30, **6** | The beach fight. Mid-fight, sleep the PSP, wake it, play another minute | When the fight's done | Every stick read logged through the hitches. What feels or sounds wrong after the wake (the ME stays off after a wake in b123; its restart is being built) |
| 7 | 343 Guilty Spark (c10), **8**, then **1** | The swamp's plants, standing and moving | When you've looked | Foliage two-pass: softer plant edges, worth +13% frame time? |
| 8 | b30, **7**, then **1** | The beach fight | When the fight's done | Far AI at half rate: do the far enemies react slower? |
| 9 | Keyes (d20), **9**, then **1** | The corridors' ceiling and wall edges | When you've looked | Light merging: the seam, and is this the merge you meant? |
| 10 | a10, **2** (the ladder) | Hands off, on the charger | It quits by itself after The Maw (d40) | Every map on b123 on your 1000 |

**First sitting:** 1 to 6. **Later:** 7 to 9 are your A/B calls, any order. Then start 10 on the charger and leave it.
