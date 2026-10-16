# Build 135: b134 on ChupathingyCE v0.7.5d (2026-10-16)

`review_b135.md` lists every change and why. The bake is unchanged (`campaign121`).

`halo_psp_b135.zip` (SHA-256 `85f8091d544a8efb71a43070ad97ebad2fbb72220ef34f771733b60b885688f5`), EBOOT `f48305852a9c9442c2ae532d9cbe3b4cb2f69b7fffca9e3e3ad286a9051f1865`: built from commit `5950c5e` on `w135` (lab sierra-build 38925960715). Heap 5,644 KB (b134: 5,656). **Its own profile** (`campaign-b135`, 13 runs at `b384588`), checked against the code (`PSP_PGO_STRICT=1`: 0 functions without their counts), and b132's code order. Release check (lab 38925953425): play and both seeds identical to b134 at every tick on d20, a50 and c20 (12,000 each) [E]; b30 and c40 shots right; the multiplayer build plays Blood Gulch. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**What's new for you:** the engine is ChupathingyCE `v0.7.5d` (was `v0.7.4e`). Of upstream's changes, what reaches the campaign: checkpoints wait 240 game ticks for a safe moment, as on the Xbox (they counted frames); the weapon HUD's second magazine no longer replaces the first's overlays; no shield damage sound while an overshield drains. Campaign play is otherwise unchanged (the release check above). Multiplayer: upstream's network version is 29 (`review_b135.md` §1).

**Speed against b134** [M, the PSP-3000's ladder, lab 38925966052 against 38913265535]: within ~1–2% on every map but c10, +3.6% (20.0 against 19.3 ms). One run; the program's code grew 10 KB with upstream's changes, which moves where the hot code sits, and c10 is the map most sensitive to it. Not run on the PSP-1000 (another session had it).

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
set zip ~/Downloads/halo_psp_b135.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b135 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 135`.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b135_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests:**

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | Halo (a30), **1** | Play from the start into the first fights | After two checkpoints, or 10 min | Checkpoints come as on the Xbox (upstream now counts their wait in game ticks); nothing else plays differently |
| 2 | Halo (b30), **15** (`nomeaudio`) | Play a minute, sleep with the power switch for ~10 s, wake, play 30 s, then HOME → exit | The XMB is back, or the PSP hangs (pull the battery) | b133's test 2, if not done there: with `nomeaudio` the ME never starts. A clean exit points at the ME for the sleep hang; a hang here too points elsewhere |

Do 2 last: it may end in a battery pull. The log reaches the stick before a hang, so pull it after the battery's back in.

**Other presets** (no test asks for them): 2 the ladder (with `frametrace`), 3 the cinematic bench, 4 the speed-ups off (b121 to b135), 5 b125's fixes off, 6 and 7 the dither A/B, 8 the level as b124, 9 sleep with `mewake` (shelved), 10 level materials as b124, 11 b126's restorations off, 12 stick reads logged, 13 the showcase, 14 frames timed.
