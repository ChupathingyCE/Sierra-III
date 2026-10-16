# Build 134: the cleanup, the pause point to pivot from (2026-10-16)

`review_b134.md` lists every change and why. The bake is unchanged (`campaign121`).

`halo_psp_b134.zip` (SHA-256 `31dcebd95da98e65cd96a2a1c2343707db0b7ca7e8a1379e90c9442895cf11c5`), EBOOT `17f4eaafc149c804541307fbb6616d540b5758f12fa3e1bc44311f9adc09fb34`: built from commit `2b9b41d` on `w134` (lab sierra-build 38915002139). Heap 5,656 KB (b133: 5,644; b132: 5,656). **Its own profile** (`campaign-b134`, 13 runs at `5e9b9f5`), checked against the code (`PSP_PGO_STRICT=1`: 0 functions without their counts), and b132's code order. Release check (lab 38913248313): play and both seeds identical to b133 at every tick on d20, a50 and c20 (12,000 each) [E]; b30 and c40 shots right; the multiplayer build plays Blood Gulch. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**What's new for you:** nothing to see. b134 is b133 with the experiments that didn't pay put on the shelf (`docs/shelf.md`): the node-matrix batch is out of the build (preset 16 is gone), and the rest stay opt-in. The build now refuses to ship with a profile that doesn't match its code.

**Speed against b133 and b132:** the PSPs' ladders are running (`3867f49`, the same code but the build number); this line follows when they land.

## On the PSP

**The PSP-1000 is the rig's** (its runs end in the game until its stick is set up again: see below). To play on it, clear the rig's last job off the stick first (otherwise the game skips the launch menu and plays the rig's map):
```fish
# Desktop
pspmount
for f in init.txt keep.txt input.txt; rm -f $psp/halo/$f; end
umount $psp
```

**Set the PSP-1000's stick up again** (the rig agent 4, and the watcher's plugin line switched back on), from Sheep-PSP-Tools:
```fish
# Desktop
pspmount
cd ~/Sheep-PSP-Tools/psprig/host      # wherever your checkout is
git pull
bash stick_setup.sh $psp psp1000
umount $psp
```

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b134.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b134 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 134`.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b134_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests:**

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | The launch menu's third page | Scroll the presets | When you've seen 15 | The list ends at 15 (16, the batch, is shelved) |
| 2 | Halo (b30), **15** (`nomeaudio`) | Play a minute, sleep with the power switch for ~10 s, wake, play 30 s, then HOME → exit | The XMB is back, or the PSP hangs (pull the battery) | b133's test 2, if not done there: with `nomeaudio` the ME never starts. A clean exit points at the ME for the sleep hang; a hang here too points elsewhere |

Do 2 last: it may end in a battery pull. The log reaches the stick before a hang, so pull it after the battery's back in.

**Other presets** (no test asks for them): 2 the ladder (with `frametrace`), 3 the cinematic bench, 4 the speed-ups off (b121 to b134), 5 b125's fixes off, 6 and 7 the dither A/B, 8 the level as b124, 9 sleep with `mewake` (shelved), 10 level materials as b124, 11 b126's restorations off, 12 stick reads logged, 13 the showcase, 14 frames timed.
