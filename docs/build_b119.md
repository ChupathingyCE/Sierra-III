# Build 119: four exact speed-ups, the clock fix and the probes (2026-10-13)

Labels: **[M]** measured on a PSP (the PSP-3000 rig's figures are marked as such until Sheepdog01's PSP-1000 runs this build), **[E]** emulator, **[D]** read in the code, **[U]** inference.

`halo_psp_b119.zip` (SHA-256 `1fdadb15c1e32a61681a2583e950f43c42503680de31181a619627bf6ba8979c`), EBOOT `05c4b263d8f2e845c528409fe74f8d7e0593d439c509082462b1bd80a635715f`: built from commit `bb8b02d` on `w119` (lab run 38450581801) with **its own profile** (`campaign-b119`, merged with the new `tools/pgo_merge.sh`; GCC took all 156 count files without a mismatch; counted at 119's code: the ten openings plus b30's three routes, lab runs 38447019289 and 38447077130), `PSP_PGO=use PSP_PGO_PROFILE=campaign-b119 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone`. Heap 5,844 KB (b118: 5,840). **No new maps:** the `campaign116f` maps are the ones to use.

**Read this first: the fps numbers drop on some levels, and that is the fix working.** Until 118, the game's clock lost ticks whenever a frame ran long (below). The game ran slower than real time and drew more frames per tick. 119 plays at real speed, so where the clock was losing ticks, each frame now carries more of them. Compare builds by **ticks a second** first.

## What's in it

### The clock fix (a restoration; on by default)

The port's vertical blank thread counts the screen's refreshes, and the game's clock follows that count. It ran at a lower priority than the game thread (0x3c against 0x20), so on a long frame it was starved and counted the missed refreshes in a burst. The engine caps a frame's step at a fifteenth of a second, so the burst beyond the cap was thrown away: on Assault on the Control Room (b40) the game ran at 77% of real time. The thread now runs just above the game thread (0x1e). Rig, b40: 2.00 ticks a frame (was 1.43), at 13.1–13.4 fps (was ~16) [M, rig]. Off word: `vblankslow`.

### Speed (exact: the same picture and play; on by default)

| Change | What it does | Evidence | Off word |
|---|---|---|---|
| **NC** the near corner loop in runs | A bending model near the eye is checked in runs of 16 triangles, each run kept or dropped whole by its sphere, instead of corner by corner | `nearclustercheck` agreed on a30, b30, c40, d20 [E]. Rig, with SU, one build by off words: Halo (a30) model parts 46.6 → 39.6 ms, b30 48.7 → 42.3 [M, rig] | `nearclusterfull` (`nearclustercheck`) |
| **SU** the corner sums written out | The near corners' three axis sums as straight-line code the PSP's FPU schedules well | with NC above | `skinaxisloop` (`skinaxischeck`) |
| **NR** the rigid near path in runs | The same runs for rigid models near the eye; also forgets M1's answers with the map | exact on c20, b30, a30 and the ladder; The Library (c20) drawing −3.8% a tick [E]. Rig, one build by off words: c20 drawing 58.0 against 58.8 ms, model parts 7.4 against 8.3 [M, rig] | `rigidrunsfull` (`rigidrunscheck`) |
| **SS** the surface bits | The light pass's surface lists put in order through a bit per surface instead of a sort | exact on d20, c40, a10; Keyes (d20) drawing −6.0% a tick [E]. Rig, one build by off words: d20 drawing 99.8 against 105.4 ms (−5.4%) [M, rig] | `surfaceqsort` (`surfacesortcheck`) |

### Instruments

- **`probes`** (opt-in): splits the hottest loops from their calls on the PSP (the near corner loop and bone slots, the level's passes by piece, the light draw before, in and after its triangle loop, the decal calls by piece). Off, it reads no clock and logs nothing. Preset 6 runs the ladder with it: the PSP-1000's own ranking, for the Media Engine work that comes next.

### Settled since 118

- **b118's test 2** (118's speed-ups off, ~2% faster on b30's cinematic): not a cost. On the rig each of the three speed-ups off left b30's drawing inside the default's own repeat spread, which is wider (4%) than the 2% seen (lab runs 38440755848, -773806, -791589, -817029) [M, rig].

### On the rig already (PSP-3000, the ladder on this exact EBOOT, lab run 38450861752) [M, rig]

Every map from The Pillar of Autumn to The Maw in one session, no crash (the wall stopped it in d40's opening, so d40 has no row). Second half of each map's 90 s; `tools/ladderreport.py` makes this table.

| Level | fps | Tick (ms) | Ticks a second | b118 on the rig (slow clock): fps / ticks a second |
|---|---:|---:|---:|---|
| The Pillar of Autumn (a10) | 14.7 | 6.6 | 25.3 | 14.1 / 24.1 |
| Halo (a30) | 6.9 | 11.5 | 13.8 | 6.5 / 12.8 |
| Truth and Reconciliation (a50) | 9.1 | 32.6 | 18.0 | 9.0 / 17.9 |
| The Silent Cartographer (b30) | 3.9 | 59.1 | 7.8 | 3.8 / 7.5 |
| Assault on the Control Room (b40) | 14.9 | 16.5 | **28.1** | 14.5 / 24.6 |
| 343 Guilty Spark (c10) | 8.9 | 23.2 | 17.8 | 8.8 / 17.2 |
| The Library (c20) | 7.9 | 26.4 | 15.5 | 7.7 / 15.2 |
| Two Betrayals (c40) | 13.0 | 16.4 | 25.8 | 12.9 / 25.2 |
| Keyes (d20) | 7.6 | 16.8 | 15.0 | 7.7 / 14.2 |

**How to read ticks a second:** the engine advances the game by at most a fifteenth of a second (2 ticks) a frame. Below 15 fps that cap binds, so ticks a second is 2 × fps (Halo 6.9 → 13.8; b30 3.9 → 7.8) and the game runs slower than real time by design; the clock fix can't change that. It recovers the ticks only on maps whose frames come in under the cap, like b40 (24.6 → 28.1). That cap is why the floor is 15 fps.

## On the PSP

**Install** (the EBOOT and the presets; the maps stay):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b119.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b119 is on the stick"
rm -f $psp/halo/shots/*
umount $psp
```
The log's first line should say `bring-up build 119`, and its next `pgo: the hot code laid out…`.

**After each test: pull the log** (change the name):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b119_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
rm -f $psp/halo/shots/*
umount $psp
```

| # | Map, preset | Play | Stop | Shape | What it answers |
|---|---|---|---|---|---|
| 1 | The Pillar of Autumn (a10), 2 (the ladder) | Hands off: each map's opening for 90 s of game time | It quits by itself after The Maw (d40) | Long: start it on the charger and leave it | Every map on 119 at real speed: the new baseline |
| 2 | a10, 3 (the ladder, the old slow clock) | as 1 | as 1 | as 1 | With test 1: how many ticks the clock was losing on each map (the fix's size on your PSP-1000) |
| 3 | The Silent Cartographer (b30), 4 (cinematic bench) | Hands off | It quits by itself at game time 70 s | Short | The milestone number: 119's cinematic against 118's 170.3 ms |
| 4 | b30, 5 (cinematic bench, 119's speed-ups off) | as 3 | as 3 | Short | With test 3: what 119's exact speed-ups are worth on your PSP-1000 |
| 5 | b30, 1 (play) | The landing and the beach fight | When the fight's done | Medium | **By feel:** does the game run at the right speed now? (Enemies, the Pelican, your walk) |
| 6 | a10, 6 (the ladder with the probes) | as 1 | as 1 | Long | The PSP-1000's own ranking of the hot loops: which go to the Media Engine first |

Presets 7 and 8 show b117's look and b115's look, for side-by-side shots.

**With one evening:** 3 and 4 back to back, then 5, then start 1 on the charger and leave it. Tests 2 and 6 can run on later evenings, one each.
