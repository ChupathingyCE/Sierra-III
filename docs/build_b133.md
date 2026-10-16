# Build 133: b132 with the objects' node matrices made together, and every preset on the menu (2026-10-16)

`review_b133.md` lists every change and why. The bake is unchanged (`campaign121`).

`halo_psp_b133.zip` (SHA-256 `0253de6b6cf419c3704d6b18e345f1d2fd1ce0c70b0e1e1c27a5965cdce80159`), EBOOT `ed846f17132dd72f43664ce45cceaece06fae5ace56a5b4a332653510e7e7e5f`: built from commit `1fff864` on `w133` (lab sierra-build 38884092696). Heap 5,648 KB (b132: 5,656). **Its own profile** (`campaign-b133`, counted at `f91d810`, 13 runs). Release check (lab 38882219486): play and both seeds identical to b132 at every tick on d20, a50 and c20 (12,000 each) [E]; b30 and c40 shots right; the multiplayer build plays Blood Gulch. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**What's new for you:** nothing to see, and nothing should play differently. Scenery's node matrices (the skeletons' poses of trees, rocks, set dressing and the like) are now made together after the objects' loop instead of in each object's update, so their code is fetched once a tick, not once an object. In the emulator the game plays exactly as without it. `nwbatchoff` (preset 16) turns it off. The launch menu shows every preset again (one row each, the cursor's keep words below the list); b132's preset 15 was missing.

**What's faster than b132** [M, the PSP-3000, nwbatch on against off in one build]: b30's cinematic tick −4.4% and flight −3.3% (lab 38881473541). The PSP-3000's ladders of b133 and b132 back to back are running.

## On the PSP

**The PSP-1000 hung in the rig at 02:54** on the renderer session's run (lab 38868877402, silhouette shadows on). It needs your hands: power-cycle it, start PSP RIG AGENT, replug its USB, and tell the renderer session, which gets its log back (`psp-probe`) before anything else touches the stick. If you'd rather play on it first, pull that log yourself and clear the rig's last job off the stick (otherwise the game skips the launch menu and plays the rig's map):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/rig_hang_38868877402.tgz -C $psp/halo log
for f in init.txt keep.txt input.txt; rm -f $psp/halo/$f; end
umount $psp
```

**Install** (the EBOOT and the presets):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b133.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b133 is on the stick"
find $psp/halo/shots -type f -delete
umount $psp
```
The log's first line should say `bring-up build 133`.

**After each test, pull the log** (change the number each time):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b133_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
find $psp/halo/shots -type f -delete
umount $psp
```

**The tests, in this order** (preset numbers on the launch menu's third page):

| # | Map, preset | Play | Stop | What it answers |
|---|---|---|---|---|
| 1 | The launch menu's third page | Scroll down to 16 | When you've seen 16 | Every preset shows, one row each, with the cursor's words below the list (b132 lost 15) |
| 2 | Halo (b30), **1** | Play normally from the start, through the first fight on the beach | After the first fight (~10 min) | Anything that moves or looks wrong. Nothing should: the batch plays exactly as before in the emulator |
| 3 | b30, **15** (`nomeaudio`) | Play a minute, sleep with the power switch for ~10 s, wake, play 30 s, then HOME → exit | The XMB is back, or the PSP hangs (pull the battery) | b132's test 8, which the missing preset stopped: with `nomeaudio` the ME never starts. A clean exit here while b132's test 6 recovered badly and 7 hung points at the ME; a hang here too points elsewhere |

Do 3 last: it may end in a battery pull. The log reaches the stick before a hang, so pull it after the battery's back in.

**Other presets** (no test asks for them): 2 the ladder (with `frametrace`), 3 the cinematic bench, 4 the speed-ups off (b121 to b133, `nwbatchoff` with them), 5 b125's fixes off, 6 and 7 the dither A/B, 8 the level as b124, 9 sleep with `mewake`, 10 level materials as b124, 11 b126's restorations off, 12 stick reads logged, 13 the showcase, 14 frames timed, 16 node matrices in each update, as b132 (`nwbatchoff`).
