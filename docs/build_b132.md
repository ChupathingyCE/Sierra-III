# Build 132: b131 with the ME pose job on, a safer ME start, and a profile that places the code well (2026-10-15)

`review_b132.md` lists every change and why. The bake is unchanged (`campaign121`).

`halo_psp_b132.zip` (SHA-256 `f92cc5a3bb9772050525849ffb1b4475226a4d43b9e9117473b33222e4b7c719`), EBOOT `886244ecc25434872ca41c83da525c439d3a59a35134d95cbf4bf9471d659523`: built from commit `1fc417f` on `w132` (code as at `65150f5`; lab sierra-build 38834087925). Heap 5,656 KB (b131: 5,660). **Its own profile** (`campaign-b132`, counted at `4218888`, 13 runs). Release check (lab 38834044173): play and both seeds identical to b131 at every common tick on d20 (15,380), a50 (25,957) and c20 (19,615) [E], with the pose job on; b30 and c40 shots right; the multiplayer build plays Blood Gulch. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**What's new for you:** nothing to see. The Media Engine makes next tick's base poses during the frame's drawing, on by default now (`poseaheadoff` turns it off; preset 4 has it off with the other speed-ups). The ME's start no longer reads the ME's own RAM while it runs (the PSP-3000's one start-up hang came there; `memarksatlaunch` reads it as before).

**What's faster than b131** [M, the PSP-1000's ladder of b132 before release, lab 38827551482]: b131's slower ticks are gone, and every map ticks under b130's too: a50 28.4 → 26.8 ms, c10 20.7 → 18.9, c20 24.6 → 23.1, b30 52.5 → 51.4 (b130 → b132). fps: a50 10.8, c10 9.9, c20 8.7, b40 16.5, c40 15.2.

## On the PSP

**Tests 1–4 carry over from b131** (they check the picture, which b132 doesn't change): if you've done them on b131, don't repeat them. **Tests 6–8, the sleep tests, belong on b132**: its ME starts differently and the pose job keeps the ME busy during play.

**If the PSP was the rig's**, first clear its last job off the stick (otherwise the game skips the launch menu and plays the rig's map):
```fish
# Desktop
pspmount
for f in init.txt keep.txt input.txt; rm -f $psp/halo/$f; end
umount $psp
```

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b132.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b132 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 132`.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b132_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests, in this order** (preset numbers on the launch menu's third page):

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1–4 | as `build_b131.md` | | | Done on b131: they carry over |
| 5 | a10, **13** (the showcase), on the charger | Hands off | When d40 is playing on, HOME → exit (about half an hour [U]) | Every opening's cutscene, one after another, for the video |
| 6 | b30, **1** | Play a minute, sleep with the power switch for ~10 s, wake, play 30 s, then HOME → exit | The XMB is back, or the PSP hangs (pull the battery) | The default's exit after a real sleep, now with the pose job keeping the ME busy |
| 7 | b30, **9** (`mewake`) | The same as test 6 | The XMB is back, or the PSP hangs (pull the battery) | Whether the restart works after a real sleep: the log says `ME: restarted after the wake` |
| 8 | b30, **15** (`nomeaudio`) | The same as test 6 | The XMB is back, or the PSP hangs (pull the battery) | Whether the ME is the cause at all: with `nomeaudio` the ME is never started. A clean exit here while 6 hangs points at the ME; a hang here too points elsewhere (the volatile partition first) |

Do 6, 7 and 8 last: each may end in a battery pull. The log reaches the stick before a hang, so pull it after the battery's back in.

**Other presets** (no test asks for them): 2 the ladder (with `frametrace`), 3 the cinematic bench, 4 the speed-ups off (b121 to b132), 10 level materials as b124, 11 b126's restorations off, 12 stick reads logged, 14 frames timed (`frametrace`).
