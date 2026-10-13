# Build 118: four exact speed-ups, the map-file fix and the hallway line (2026-10-13)

Labels: **[M]** measured on a PSP (the PSP-3000 rig's figures are marked as such until Sheepdog01's PSP-1000 runs this build), **[E]** emulator, **[D]** read in the code, **[U]** inference.

`halo_psp_b118.zip` (SHA-256 `2814bd8d11f1bba4ae3732bda3ec97618c3a4c1d0395e33800b25a235df33cff`), EBOOT `e1ce6b85fced2389503d458d01f5af1376de748a27aea0a46f2e96e6f3b0260a`: built from commit `be07195` on `w118` (lab run 38398847837) with **b117's profile** (`campaign5-b117`; the code has moved a little since it was counted, so the layout is nearly but not wholly 118's own), `PSP_PGO=use PSP_PGO_PROFILE=campaign5-b117 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone`. Heap 5,840 KB (b117: 5,876). **No new maps:** the `campaign116f` maps are the ones to use.

**Why 118 now, with b117's tests not all run:** Sheepdog01 (2026-10-13) cleared folding confirmed wins into newer builds, so one session on the PSP-1000 completes both. b117's open tests are folded in below; test 6 is gone (the rig answered it).

## What's in it

### Speed (exact: the same picture and play; on by default)

| Change | What it does | Evidence | Off word |
|---|---|---|---|
| **R1** the restless back-off | An object whose update changed it on two sleep checks running (swaying vegetation) is updated without the checks for 30 ticks | c10: −15% of the tick's instructions, a30 −8%; play and seeds the same all 3,600 ticks [E]. Rig: c10's tick 22.6–22.8 ms against b117's 25.8 on the PSP-1000 [M, rig] | `restlesscheck` |
| **X9a** the sphere memo | The level sphere queries' answers kept under all their inputs: a standing biped asks the same thing every tick (a50: 53–59% of queries) | 0 differing answers on a50, b30, d20, c20; a50 −3.0% of the tick [E]. Rig: a50's tick 32.1 ms against 33.2 [M, rig] | `spherewalk` (`spherememocheck` verifies) |
| **NS** the near sphere test | A bending batch near the eye wholly inside the corner tests' planes is drawn whole without its corner loop | "all whole" on a30, b30, c40; a30 drawing −2.2% [E] | `nearspherefull` (`nearspherecheck`) |
| **M1** the see-through node scan | Whether a see-through part hangs from several nodes, remembered per part | 0 differing on c40, b30, a10; c40 data misses −1.5% [E] | `multiscan` (`multicheck`) |
| **T1** the see-through cut's sphere test | A see-through part wholly in view by its model's sphere skips the per-triangle cut | `tcutcheck` 0 mismatches on b30, a10, c40 [E] | `tcutfull` (`tcutcheck`) |
| **X10** the vector checks | The units' and bipeds' vector checks skipped (they act only on broken vectors) | exact; 0.3–1.3% of the tick [E] | `verifyvectors` |

### Fix and restoration

| Change | What it does | Off word |
|---|---|---|
| **F1** map files | One map file open besides the open map's: b117 ran out of file handles at the sixth map in a session (your test 2) | `mapfilesopen` |
| **O1** the hallway line | On-wall overlays nudged by their far corner: b30's bunker line gone at your spot [E]; very likely c40's glass through Chief's legs too [U] | `overlaywallnear` |

### Instruments

- **H1:** the 300-frame report counts frames by display refreshes shown (1 = 60 fps, 2 = 30, 3 = 20, 4 = 15, 5+) and the longest: the momentary drops below 30 fps that 5 s averages hide.
- `mebench` and `gemeoverlap` stay in, opt-in. On the rig the overlap was **3–6% slower** than the main CPU alone (`hardware_rig.md`, run 38359689577), so it has no preset.

### On the rig already (PSP-3000, the ten-level ladder, lab run 38395171093) [M, rig]

| Level | fps | Tick (ms) | Ticks a second | b117 on your PSP-1000: fps / tick |
|---|---:|---:|---:|---|
| The Pillar of Autumn (a10) | 14.1 | 6.6 | 24.1 | 14.5 / 6.9 |
| Halo (a30) | 6.5 | 11.3 | 12.8 | 6.6 / 11.9 |
| Truth and Reconciliation (a50) | 9.0 | 32.1 | 17.9 | 8.8 / 33.1 |
| The Silent Cartographer (b30) | 3.8 | 59.2 | 7.5 | 3.8 / 58.9 |
| Assault on the Control Room (b40) | 14.5 | 15.3 | 24.6 | (its log was lost) |
| 343 Guilty Spark (c10) | 8.8 | 22.8 | 17.2 | 8.5 / 25.8 |
| The Library (c20) | 7.7 | 26.5 | 15.2 | 7.8 / 26.7 |
| Two Betrayals (c40) | 12.9 | 16.0 | 25.2 | 12.6 / 16.7 |
| Keyes (d20) | 7.7 | 22.7 | 14.2 | 7.3 / 16.9 (a different stretch: cut short) |
| **The Maw (d40)** | **16.3** | 4.2 | **29.8** | (never measured) — **meets the floor (15 fps at 30 ticks)**: hands-off at its start, the opening only (lab run 38398689046) |

The rig ran b116 within ~2% of the PSP-1000 on the frame and 1% on the tick (`hardware_rig.md`); its GE is about 12% slower.

## On the PSP

**Install** (the EBOOT and the presets; the maps stay):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b118.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b118 is on the stick"
rm -f $psp/halo/shots/*
umount $psp
```
The log's first line should say `bring-up build 118`, and its next `pgo: the hot code laid out…`.

**After each test: pull the log** (change the name):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b118_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
rm -f $psp/halo/shots/*
umount $psp
```

| # | Map, preset | Play | Stop | Time | What it answers |
|---|---|---|---|---|---|
| 1 | b30, 3 (cinematic bench) | Hands off | It quits by itself at game time 70 s | ~5 min | The milestone number on the PSP-1000: 118's cinematic against b116's 194 ms (and 117's, never run) |
| 2 | b30, 6 (cinematic bench, 118's speed-ups off) | as 1 | as 1 | ~5 min | With test 1: what 118's exact speed-ups are worth on your PSP-1000 |
| 3 | b30, 4 (bench, model buffers in main RAM) | as 1 | as 1 | ~5 min | How the VRAM buffers stack with the rest (b117's test 3) |
| 4 | a10, 2 (the ladder) | Hands off: each map's opening for 90 s of game time | It quits by itself after d40 (F1: it should now reach it) | ~30–60 min | Every map's baseline on 118, any crash, and F1 (all ten maps in one session) |
| 5 | b30, 1 (play) | The landing, the beach, then the bunker hallway | When you've seen a fight and the hallway | ~10 min | By eye: no checkerboard, actors lit by muzzle flashes, **the hallway line gone** (O1); if you get to c40, Chief's legs past the glass |
| 6 | b30, 5 (ceiling bench) | Hands off from the start | It quits by itself at game time 300 s | ~15 min | What each lever is worth after this build (b117's test 5) |
| 7 | (the XMB) | Highlight Halo in the game list and wait a moment | When you've seen it | ~1 min | A photo of the wallpaper and tile on the PSP-1000's screen (b117's test 7) |

Presets 7 and 8 show b117's look and b115's look, for side-by-side shots.

**With one evening:** 7, then 1 and 2 back to back, then start 4 with the PSP on its charger and leave it; it quits to the XMB when it's done.
