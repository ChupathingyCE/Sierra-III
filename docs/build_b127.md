# Build 127: b126 with the log off the stick's critical path, and its own profile (2026-10-15)

`review_b127.md` lists every change and why. b126 never reached your PSP, so its tests are tonight's, on b127 (the same seven; b127 changes nothing you can see). The bake is unchanged (`campaign121`).

`halo_psp_b127.zip` (SHA-256 `36d1d6980c1db01bd97f444631556bbb4e8f7697622049ba97647620fad6dc3f`), EBOOT `54d7eb7ad80a915b6fbc9bd2cc63535a4a09880f8837c7c813623dbcf8b85d3e`: built from commit `25f7c80` on `w127` (lab sierra-build 38726220580). Heap 5,684 KB (as b126); program text 1.8 KB larger; 0 symbols stubbed. **Its own profile** (`campaign-b127`, counted at `8431f5e`, 13 runs). Release check (lab 38726145459): play and both seeds identical to b126 at every common tick on d20 (15,091), a50 (25,200) and c20 (18,946); the logs read whole (no lines dropped); b30 and c40 shots right; Blood Gulch plays. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**What's faster** [M, PSP-1000, b30]: the opening (game time 0–400) −11% a frame, the flight −1.2%, the landing −2.9%; the 99.9% frame time 450 against 547 ms (the log batched from the game's start). And b126's own profile was already 5.7% faster than b122's on the beach; b127 keeps that by counting its own.

## On the PSP

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b127.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b127 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 127`.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b127_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
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

**Other presets** (no test asks for them): 2 the ladder (now with `frametrace`), 3 the cinematic bench, 4 the speed-ups off (b121 to b127), 5 b125's fixes off, 10 level materials as b124, 11 b126's restorations off, 12 stick reads logged, 14 frames timed (`frametrace`).

**Reading a `frametrace` log** (on the Desktop, in the Sierra-III checkout): `python3 tools/frametrace.py log.txt` prints the run's 99th and 99.9th percentile frame times, how many frames lost game time, and what took most of each slow frame.
