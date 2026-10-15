# Build 129: b128 with the flares' walls, the fractions in single precision and the collision log's unused copies dropped, its own profile (2026-10-15)

`review_b129.md` lists every change and why. b127 and b128 never reached your PSP, so their tests are tonight's, on b129 (the same seven; b129 changes nothing you can see). The bake is unchanged (`campaign121`).

`halo_psp_b129.zip` (SHA-256 `cb9c88b9d92011595e14973a9304e9b673ecb432130f367114913d8ef759b05b`), EBOOT `41d9fd11f62444944bcf550728fd796c82f864d0a5dec7c5dcc928a0646d0744`: built from commit `d855672` on `w129` (lab sierra-build 38762103752). Heap 5,676 KB (b128's 5,680). **Its own profile** (`campaign-b129`, counted at `4fd9377`, 13 runs). Release check (lab 38761997380): play and both seeds identical to b128 at every tick on d20, a50 and c20 [E]; b30 and c40 shots right; the multiplayer build plays Blood Gulch. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**What's faster** [M, PSP-1000, c20 once the Flood are about]: the frames −1.9% from the flares' walls (drawing −3.5%) and −1.7% from the fractions (the tick −3.4%). On top of b128's: a50's frames −3.1% to −3.4% (nearauto's spheres); b127's: b30's opening −11%, the 99.9% frame 450 against 547 ms.

## On the PSP

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b129.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b129 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 129`.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b129_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
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

**Other presets** (no test asks for them): 2 the ladder (now with `frametrace`), 3 the cinematic bench, 4 the speed-ups off (b121 to b129), 5 b125's fixes off, 10 level materials as b124, 11 b126's restorations off, 12 stick reads logged, 14 frames timed (`frametrace`).

**Reading a `frametrace` log** (on the Desktop, in the Sierra-III checkout): `python3 tools/frametrace.py log.txt` prints the run's 99th and 99.9th percentile frame times, how many frames lost game time, and what took most of each slow frame.
