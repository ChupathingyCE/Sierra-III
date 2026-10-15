# Build 131: b130 with surfaces out of reach passed over, the ME pose job (opt-in), its own profile (2026-10-15)

`review_b131.md` lists every change and why. b127–b130 never reached your PSP, so their tests are tonight's, on b131 (the same eight). The bake is unchanged (`campaign121`).

ZIPLINE

**What's new for you:** nothing to see: b131 plays and looks as b130 does. Bipeds' movement passes over level surfaces that can't touch them (`surfaceboundoff`, or preset 4, tests them all as before). WORTHLINE The Media Engine pose job is in but off: it hung the PSP-3000 once today, so no preset uses it tonight.

**What's faster from b130, still in:** `cutless` (a10 14.7 → 15.9 fps, c40 13.9 → 14.6 on the PSP-1000); c20's flares' walls and fractions; b30's flight tick −3.7%.

## On the PSP

**First, the lab's PSP-3000** (it hung on the ME pose job today, lab 38784036485): hold POWER until it switches off, switch it on, and before starting PSP RIG AGENT pull its log in USB mode (the next rig run deletes `log.txt`):
```fish
# Desktop (X3D), the PSP-3000 in USB mode
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/mepose131_psp3000_hang.tgz -C $psp/halo log
umount $psp
```
Then leave USB mode, start PSP RIG AGENT, and replug its USB cable.

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b131.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b131 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 131`.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b131_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
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
| 8 | b30, **15** (`nomeaudio`) | The same as test 6 | The XMB is back, or the PSP hangs (pull the battery) | Whether the ME is the cause at all: with `nomeaudio` the ME library is never started, so the firmware's ME handler is untouched. A clean exit here while 6 hangs points at the ME; a hang here too points elsewhere (the volatile partition first) |

**First sitting:** 1 to 4. Then 5 on the charger. Do 6, 7 and 8 last: each may end in a battery pull. The log reaches the stick before a hang, so pull it after the battery's back in.


**Other presets** (no test asks for them): 2 the ladder (now with `frametrace`), 3 the cinematic bench, 4 the speed-ups off (b121 to b131), 5 b125's fixes off, 10 level materials as b124, 11 b126's restorations off, 12 stick reads logged, 14 frames timed (`frametrace`).

**Reading a `frametrace` log** (on the Desktop, in the Sierra-III checkout): `python3 tools/frametrace.py log.txt` prints the run's 99th and 99.9th percentile frame times, how many frames lost game time, and what took most of each slow frame.
