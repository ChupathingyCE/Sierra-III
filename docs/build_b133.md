# Build 133: b132 with every preset on the menu, and the node-matrix batch to try (2026-10-16)

`review_b133.md` lists every change and why. The bake is unchanged (`campaign121`).

`halo_psp_b133.zip` (SHA-256 `0d4f41e41c7d0a2de5f736a7ad0f040fe3250a0eae48703b5a0d68de243ea768`), EBOOT `203742294e80bd1dfb832a837d12025b65101bc3a23cdef657d26ab1b0e2ef09`: built from commit `405ca12` on `w133` (lab sierra-build 38903119127). Heap 5,644 KB (b132: 5,656). **Its own profile** (`campaign-b133`, 13 runs counted at `d9e7cf3` with the batch off) **and b132's code order** (`PSP_TEXT_LAYOUT=b132`): review §4 says why. Release check (lab 38899909814): play and both seeds identical to b132 at every tick on d20, a50 and c20 (12,000 each) [E]; b30 and c40 shots right; the multiplayer build plays Blood Gulch. For the archive: `releases.tsv` has the line; `publish-releases.sh tags` and `archive` on your machine.

**What's new for you:** the launch menu shows every preset again (one row each, the cursor's keep words below the list); b132's preset 15 was missing. Otherwise b133 plays and looks as b132 did. Preset 16 tries the node-matrix batch (`nwbatch`): play exact, faster on b30, slower on c10 and c20, so it's off unless asked.

**Speed against b132** [M, the ladder's tick, both PSPs agree]:
- **Within ~1% on most maps:** a50, b30, c20, c40, d20.
- **Slower on c10 (+6.9%) and a30 (+3–4%).** The batch's switched-off code makes each object's update ~2.5 KB bigger, and the update no longer fits the instruction cache: the same instructions, 19–26% more instruction misses (the cache model, lab 38903261679). b134 takes that code out of the build.
- Labs: PSP-1000 38905744440 against b132's 38827551482; PSP-3000 38899916249 against 38884473198.

Two earlier b133s were never posted:
- `1fff864` had the batch on and a profile counted with it on. The PSP-3000's ladder had it slower than b132 on most maps (c10 +34%).
- `d9e7cf3` had the batch off and b132's profile, which doesn't match b133's object loop. Both PSPs' ladders had it 3–20% slower than b132 (review §4).

## On the PSP

**The PSP-1000 is the rig's again** (in rig mode since 06:54; its b133 ladder ran after). To play on it, clear the rig's last job off the stick first (otherwise the game skips the launch menu and plays the rig's map):
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
| 2 | Halo (b30), **15** (`nomeaudio`) | Play a minute, sleep with the power switch for ~10 s, wake, play 30 s, then HOME → exit | The XMB is back, or the PSP hangs (pull the battery) | b132's test 8, which the missing preset stopped: with `nomeaudio` the ME never starts. A clean exit here, where b132's test 6 recovered roughly and 7 hung, points at the ME; a hang here too points elsewhere |

Do 2 last: it may end in a battery pull. The log reaches the stick before a hang, so pull it after the battery's back in.

**Other presets** (no test asks for them): 2 the ladder (with `frametrace`), 3 the cinematic bench, 4 the speed-ups off (b121 to b133), 5 b125's fixes off, 6 and 7 the dither A/B, 8 the level as b124, 9 sleep with `mewake`, 10 level materials as b124, 11 b126's restorations off, 12 stick reads logged, 13 the showcase, 14 frames timed, 16 scenery's node matrices together (`nwbatch`).
