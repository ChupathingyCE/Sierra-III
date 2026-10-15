# Build 126: b125 plus the first research levers, two restorations, frame timing (2026-10-15)

`review_b126.md` lists every change and why (the levers in detail: `review_levers126.md`). b125 never reached your PSP, so its tests are tonight's, on b126. The bake is unchanged (`campaign121`).

`halo_psp_b126.zip` (SHA-256 `4b3071710bb09efb74b047a65ebef2716beecbd289aeeee30f13a7f986b36bd5`), EBOOT `f1150b2f200ccb62ad1bd1210e0b5f7b5dd7d1a5c16979fc2acf70c5e1def713`: built from commit `e59706b` on `w126` (lab sierra-build 38709588486). Heap 5,684 KB (b125: 5,680); program text 4.9 KB smaller; 0 symbols stubbed. **Its own profile** (`campaign-b126`, counted at `c3d9a81`, 13 runs), the first fresh one since b122: 0 coverage mismatches. Release check (lab 38709536698): play and both seeds identical to b125 at every common tick on d20, a50 and c20; b30 and c40 shots right; Blood Gulch plays. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

The rig answers what needs no eyes: the GE's precision on the PSP-1000 (this morning: b122's way scatters 0.44–0.49 px, b125's local origin 0.025 px, so it's enough as built), the ME restart under `mewaketest` (restarted in 47 ms), the levers' timing, `cutless`'s timing. Your tests are the ones that need eyes, feel and hands.

## On the PSP

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b126.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b126 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 126`.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b126_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests, in this order** (preset numbers on the launch menu's third page):

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | Two Betrayals (c40), **1**, then **8** | The opening cutscene and the bridge; look at the terrain, the doorway and the dark grey pillars on the back wall | After the bridge, each time (~5 min each) | The dance: the level should hold still against the camera on 1 and dance as before on 8. If the pillars still flicker on 1, it's depth, not position |
| 2 | The Pillar of Autumn (a10), **1** | The flyby (watch the window frame), then walk up to Captain Keyes. Glance at the HUD for stray warnings (an empty-secondary beep, "NO FUEL") | After Keyes (~10 min) | The window frame steady; the frame rate shouldn't drop to 9 near the bridge (the log shows `GE: no room for a material's copy … it waits`, not runs of `memalign … FAILED`) |
| 3 | a10, **8** | The flyby only | After the flyby (~4 min) | The window frame as b124 drew it, against test 2 |
| 4 | Keyes (d20) dark corridor, then b30's sky: **1**, **6**, **7** | Stand in the same spot on each preset | When you've compared (~10 min) | The dither: full (1), half (6), off (7). Which looks best is your call |
| 5 | a10, **13** (the showcase), on the charger | Hands off | When d40 is playing on, HOME → exit (about half an hour [U]) | Every opening's cutscene, one after another, for the video |
| 6 | b30, **1** | Play a minute, sleep with the power switch for ~10 s, wake, play 30 s, then HOME → exit | The XMB is back, or the PSP hangs (pull the battery) | The default's exit after a real sleep. If it hangs, the firmware's own ME boot fails after a sleep (b122 would too) |
| 7 | b30, **9** (`mewake`) | The same as test 6 | The XMB is back, or the PSP hangs (pull the battery) | Whether the restart works after a real sleep: the log says `ME: restarted after the wake` (the rig's simulated sleep restarted in 47 ms) |

**First sitting:** 1 to 4. Then 5 on the charger. Do 6 and 7 last: each may end in a battery pull. The log reaches the stick before a hang, so pull it after the battery's back in.

**Other presets** (no test asks for them): 2 the ladder (now with `frametrace`), 3 the cinematic bench, 4 the speed-ups off (b121 to b126), 5 b125's fixes off, 10 level materials as b124, 11 b126's restorations off, 12 stick reads logged, 14 frames timed (`frametrace`).

**Reading a `frametrace` log** (on the Desktop, in the Sierra-III checkout): `python3 tools/frametrace.py log.txt` prints the run's 99th and 99.9th percentile frame times, how many frames lost game time, and what took most of each slow frame.
