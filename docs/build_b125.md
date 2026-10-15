# Build 125: the level from a shared origin, room for a10's materials, b30's water, a safer sleep, the GL path out (2026-10-15)

`review_b125.md` lists every change and why. The bake is unchanged (`campaign121`); the PGO profile is still b122's.

The rig measures what needs no eyes (`geprecision`, the timing of `levellocaloff`, `mewaketest`). Your tests are the ones that need eyes, feel and hands.

## On the PSP

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b125.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b125 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 125`, with about 100 KB more heap than b124's (the GL path left the program).

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b125_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests, in this order** (preset numbers on the launch menu's third page):

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | Two Betrayals (c40), **1**, then **8** | The opening cutscene and the bridge; look at the terrain, the doorway and the dark grey pillars on the back wall | After the bridge, each time (~5 min each) | The dance: the level should hold still against the camera on 1 and dance as before on 8. If the pillars still flicker on 1, it's depth, not position |
| 2 | The Pillar of Autumn (a10), **1** | The flyby (watch the window frame), then walk up to Captain Keyes | After Keyes (~10 min) | The window frame steady. The log should show `GE: no room for a material's copy … it waits` lines, not runs of `memalign of 109600 bytes FAILED`; the frame rate shouldn't drop to 9 near the bridge |
| 3 | a10, **8** | The flyby only | After the flyby (~4 min) | The window frame as b124 drew it, against test 2 |
| 4 | Keyes (d20) dark corridor, then b30's sky: **1**, **6**, **7** | Stand in the same spot on each preset | When you've compared (~10 min) | The dither: full (1), half (6), off (7). Which looks best is your call |
| 5 | a10, **13** (the showcase), on the charger | Hands off | When d40 is playing on, HOME → exit (about half an hour [U]) | Every opening's cutscene, one after another, for the video |
| 6 | b30, **1** | Play a minute, sleep with the power switch for ~10 s, wake, play 30 s, then HOME → exit | The XMB is back, or the PSP hangs (pull the battery) | The default's exit after a real sleep. If it hangs, the firmware's own ME boot fails after a sleep (b122 would too) |
| 7 | b30, **9** (`mewake`) | The same as test 6 | The XMB is back, or the PSP hangs (pull the battery) | Whether the new restart works: the log says `ME: restarted after the wake` or how far the ME got (`review_mewake2.md` has the table) |

**First sitting:** 1 to 4. Then 5 on the charger. Do 6 and 7 last: each may end in a battery pull. The log reaches the stick before a hang, so pull it after the battery's back in.

**Other presets** (no test asks for them): 2 the ladder, 3 the cinematic bench, 4 the speed-ups off, 5 b125's fixes off, 10 level materials as b124 (`streamroomoff`), 11 `geprecision`, 12 stick reads logged.
