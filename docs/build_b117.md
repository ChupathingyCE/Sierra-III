# Build 117: the performance milestone (2026-10-12)

Sheepdog01 named this build 117, the "something special" he had saved the number for. It puts the two speed-ups measured on the PSP in b116's tests together (model buffers in VRAM, the profile-guided build) with the review's exact speed-ups, and the restorations from b116's tests. It also carries the XMB menu's wallpaper from the menu work on `main`. Labels: **[M]** measured on Sheepdog01's PSP-1000, **[E]** emulator, **[D]** read in the code, **[U]** inference.

`halo_psp_b117.zip` (SHA-256 `7f44a0f8271bb173472e2178f82e387b089b7c8d0269cb4072284ab373bf995e`), EBOOT `83f3037a9cd5acab6b1a514c916c14f20e582c66fa58f1924c9b0850d918765b`: built from commit `b7d0562` on the `b117` branch with **its own profile** (`pgo/campaign5-b117.tgz`, counted at 117's code in five emulator stretches, lab run 38337151402), `PSP_PGO=use PSP_PGO_PROFILE=campaign5-b117 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone`. Heap 5,876 KB (b116: 5,852). **No new maps:** the `campaign116f` maps from b116 are the ones to use.

(An rc1 built with b116's profile, `8561020`, was made for a session that didn't happen; it was never handed out.)

## What's in it

### Speed (on by default)

| Change | What it does | On the PSP so far | Off word |
|---|---|---|---|
| Model buffers in VRAM, 600 KB | The model buffers the GE reads most per byte, copied into VRAM; the same pixels | b116 test 2 [M]: the models' GE time −31% (cinematic), −44% (flight); the flight 3.67 → 4.28 fps | `vrammodels0` |
| The profile-guided build | GCC lays out and optimises the hot code from counts of where the game runs | b116 test 7 [M]: the cinematic 194 → 182.6 ms a frame, the tick −7% | (the binary) |
| The level light pass's vertex memo | Each shared vertex worked out once a draw | −2.4 to −3.8% of drawing [E]; checks itself (`lightmemocheck`) | — |
| Three small exact speed-ups | The overlay flags remembered per list; the texture lookup's recent places; `-fno-math-errno` for the port's units | −1.2 to −2.3% of drawing [E] | — |
| Checkpoints written in whole 4 KB pieces | The same bytes; b116's pieces of any length made the stick rewrite sectors [U] | b116 [M]: 3–4× slower than b115's checkpoints | — |

Together [U]: about 194 → 170 ms a frame on the cinematic and 272 → 220 on the flight, if the GE-side and tick-side gains stack.

### Restorations (on by default; a word turns each off)

| Change | Restores | Off word |
|---|---|---|
| The dither only on draws that don't blend (and the lightmap pass) | b116's checkerboard on shadows and particles gone; b116's slight darkening gone [E] | `ditherall` |
| R4: a draw's geometry read at once past the frame's share (up to 384 KB a frame) | No objects left out as they come into view: 0–2 left out in play in the emulator, against b116's hundreds over the openings [E] | `geoshare` |
| Models lit by their point lights (muzzle flashes, plasma, the flashlight) | Actors and weapons lit by moving lights, as on the Xbox [E: 37,000 lit parts over b30's beach] | `nopointlights` |
| The sniper rifle's angle ticks | a50's two white bars become the scope's blue ticks [E] | `hudonemap` |
| Glass on models lit by the model's light | not full-bright | `glassflat` |
| The light pass's near-plane pieces lifted per corner | Correct, but **not** the hallway line (below) | `lightcutone` |

### The XMB menu (from `main`, `docs/xmb_menu.md`)

- **The wallpaper (`PIC1.PNG`)**: the space backdrop, the ring and the Pillar of Autumn from Sheepdog01's PC captures, the game title and the Sierra-III mark baked in (162 KB, packed in the EBOOT). Under Microsoft's Game Content Usage Rules; the README carries their notice.
- **The tile (`ICON0.PNG`)** is still the placeholder; no ring loop (`ICON1`) or menu sound (`SND0`) yet: the bake adds the sound from the user's own disc (`--eboot`), not the release.

### Tests built in

| Keep word | What it does |
|---|---|
| `ladderN` | The campaign's openings one after another, N seconds of game time each, unattended; exits after the last map on the stick |
| `exitatN` | Quits at N seconds of game time, so a bench stops by itself |

## Found, not fixed yet

- **The hallway line in b30's bunker (b113 #13) is a see-through level surface, not the light pass** [E].
  - With the level's see-through surfaces left out (`nooverlays`) it's gone; with decals left out it stays.
  - At Sheepdog01's spot one draw of `levels\b30\shaders\glass floor`, its pieces cut at the near plane, carries a 4,096-step depth nudge: the largest one any of its triangles asked for, applied to them all.
  - Very likely the same cause as c40's glass edges through Chief's legs [U].
  - The fix, for 118: on-wall overlays nudged by their far corner (O1 in `b117_candidates.md`). In the emulator the line is gone at Sheepdog01's spot (`wallfar118`) [E].
- **c10's swamp fog:** the Xbox fogs what lies below the fog's plane by its depth, as b30's deep water needs.
- c40's other items and c10's water effects (`b117_candidates.md`).

## On the PSP

**Install** (the EBOOT and the presets; the maps stay):
```fish
# Desktop
pspmount
set zip ~/Downloads/halo_psp_b117.zip
ls -l $zip                      # must list the file; fish carries on after a failed step
unzip -o $zip -d $psp
unzip -p $zip PSP/GAME/halo/EBOOT.PBP | cmp - $psp/PSP/GAME/halo/EBOOT.PBP; and echo "b117 is on the stick"
rm -f $psp/halo/shots/*
umount $psp
```
The log's first line should say `bring-up build 117`, and its next `pgo: the hot code laid out…`.

**After each test: pull the log** (change the name):
```fish
# Desktop
pspmount
mkdir -p $HOME/psp/logs
tar czf $HOME/psp/logs/b117_test1.tgz --exclude='log/[0-9][0-9][0-9]_*' -C $psp/halo log shots
rm -f $psp/halo/shots/*
umount $psp
```

| # | Map, preset | Play | Stop | Time | What it answers |
|---|---|---|---|---|---|
| 1 | b30, 3 (cinematic bench) | Hands off | It quits by itself at game time 70 s | ~5 min | The milestone number: 117's cinematic and flight against b116's 194 and 272 ms |
| 2 | a10, 2 (the ladder) | Hands off: it plays each map's opening for 90 s of game time and loads the next | It quits by itself after d40 | ~30–60 min (the PSP plays openings at a quarter to half speed) | Every map's baseline, any crash, R4 on the PSP, checkpoint times |
| 3 | b30, 4 (bench, model buffers in main RAM) | as 1 | as 1 | ~5 min | How the two speed-ups stack (with test 1) |
| 4 | b30, 1 (play) | The landing and the beach | When you've seen a fight | ~10 min | By eye: no checkerboard, actors lit by muzzle flashes, nothing vanishing near the landing pad |
| 5 | b30, 5 (ceiling bench) | Hands off | It quits by itself at game time 300 s | ~15 min | What each lever is worth after this build |
| 6 | b30, 6 (GE work on the ME, serial) | Hands off | It quits by itself at game time 70 s | ~5 min | The ME's time on 117's work: sizes L1 step 4 |
| 7 | (the XMB) | Highlight Halo in the game list and wait a moment | When you've seen it | ~1 min | The wallpaper and tile on the PSP's screen: a photo of the XMB with Halo highlighted, to check the dead zones (`psp/xmb/deadzones.py`) and how the art reads on the PSP-1000's LCD |

Presets 7 and 8 show b116's look and b115's look, for side-by-side shots.

**With one evening:** 7, then 1, then start 2 with the PSP on its charger and leave it; it quits to the XMB when it's done.
