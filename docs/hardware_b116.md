# Build 116 on the PSP-1000 (2026-10-12)

Sheepdog01's PSP-1000 at 333 MHz, `halo_psp_b116.zip` (EBOOT `e313c587…482e`) with the `campaign116f` maps. The tests are listed in `build_b116.md`. Figures come from `tools/benchreport.py` and the log's `gebusy by kind` lines. Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

All seven tests are in.

## In short

- **The restorations show on the PSP** (test 1, Sheepdog01's eye) [M]:
  - the Pelicans' thrusters;
  - the ocean;
  - the seams at the base of the rocks, gone;
  - more effects visible;
  - the Warthog's meters;
  - the bunker's Forerunner door, textured.
- **They cost nothing on the cinematic** [M]: 193.2–196.1 ms a frame in tests 1 and 2, against b115's 197.0 and b114's 195.6.
- **Model buffers in VRAM win** (test 2) [M]. With 600 KB:
  - the models' GE time drops 31% in the cinematic and 44% in the flight after it;
  - the flight runs at 4.28 fps against 3.67.
  - 400 KB gains a little less. Sheepdog01's call; the recommendation is 600 KB on by default in 117.
- **The ordered dither (b113 #6) builds a checkerboard** on shadows and particles [M]. My mistake: blended passes round the frame again with the same 4x4 offset each time. Fixed on the `hudbars` branch (below).
- **c40 no longer crashes** (test 3) [M]: b115 stopped at game time 1,424; b116 played to 11,146.
- **c10's swamp fog isn't drawn** (test 4) [M]: the planar fog only covers the eye inside the fog, and in the swamp the eye is above it. My error in b116's notes, now corrected.
- **The restorations cost under 1%** on the cinematic (test 6 against test 1) [M].
- **The profile-guided build is about 6% faster** on the cinematic (test 7) [M], all of it in the tick.
- **Sheepdog01's calls:** keep S9; 600 KB of model buffers in VRAM by default in 117; the glows and light cones are acceptable for now.
- **The compact checkpoints are slower on the PSP than b115's whole pages** [M]: 3,102 ms against 219 for b30's first. The likely cause is writes that end mid-sector [U]. Fixed on `hudbars` (below).

## Test 1: b30, preset 1 (play)

Keep words `nearauto assettrack geo2`. No fault; Sheepdog01 played to game time 6,708, past the map room bunker's entrance. 7 screenshots.

**What Sheepdog01 saw**, and what each is:

| Shots | Seen | What it is | Status |
|---|---|---|---|
| — | Pelican thrusters, the ocean, no seams at the rocks' base, more effects, the Warthog's meters, the bunker door textured | b116's restorations (R1, R2, R2b, #16, R3) | confirmed [M] |
| 000–004 | A checkerboard on shadows and particles | **The dither** (b113 #6, below) | fixed on `hudbars`, checked in the emulator |
| 004 | Objects vanishing approaching the bunker's landing pad | **R4: geometry not read in time.** Once a frame has read its 192 KB share of geometry, a draw whose vertices aren't resident is left out for that frame. The counter went 45 → 48 in the report holding the shot [M] | fixed on `hudbars` (below) |
| 005–006 | The hallway floor's light through the wall | **b113 #13:** the light pass nudged its pieces cut at the near plane by one depth offset, sized for the corner nearest the eye, about 1 unit of lift at 8 units [D] | fixed on `hudbars` (below) |
| 000, 002 | No lighting on actors | Characters get the level's light under them and the sun. The flashlight and other moving lights don't light them yet (b113 #11) [D] | open |

**The checkerboard, measured.** The size of a fixed 4x4 pattern in each shot was measured as each pixel's difference from its 4x4 cell's mean, averaged by place in the cell. Spread from the brightest place to the darkest, in 0–255:

| Shot | Spread (R, G, B) |
|---|---|
| 000–002, 004 (beach, Warthog) | 12–25 |
| 003 (under the Pelican's dust) | 54–64 over the shot; **137–146** in the dust |
| 005 (inside the bunker) | 9–12 |

- **Why it builds:** a blended draw reads the frame back and rounds it again, and the GE adds the same offset at the same place each pass [D].
- **Where a pass changes a pixel little, the rounding goes the same way every time.** That covers a layer of dust, a shadow's quad outside the shadow, and a detail map near its neutral. Pass after pass, the error stacks [U, from the GE's rounding].
- **The emulator's shots show the same pattern** (b116's final tour: 23–36 on b30), so the fix can be checked there.

**The bench, by stretch** (`benchreport.py`):

| Stretch (game time) | Frame, ms | Drawing, ms | Tick, ms | fps |
|---|---:|---:|---:|---:|
| cinematic (150–570) | 193.2 | 71.7 | 59.7 | 5.17 |
| flight (300–2000) | 271.9 | 137.6 | 63.4 | 3.68 |
| landing (2000–2700) | 172.7 | 77.7 | 43.9 | 5.79 |
| beach (2700–6000) | 172.4 | 90.4 | 35.0 | 5.80 |
| bunker (6000–6708) | 94.6 | 49.8 | 14.6 | 10.57 |

- No draws skipped with the display list full; no sound failed for room; heap peak 5,512 KB of 5,852.

**Checkpoints and sound gaps** [M]:

| Game time | b116 checkpoint |
|---:|---|
| 1,108 | 330 pages, 768 KB, **3,102 ms** |
| 2,356 | 364 pages, 891 KB, 657 ms |
| 3,627 | 420 pages, 1,018 KB, 2,244 ms |
| 5,738 | 476 pages, 1,278 KB, 1,884 ms |
| 6,547 | 400 pages, 1,103 KB, 818 ms |

- **b114 and b115 on b30** wrote the same first checkpoint (330 pages as whole pages, 1,320 KB) in 219–225 ms in every run on file, and later ones in 153–1,161 ms [M].
- **Slower despite fewer bytes.** b116 writes the compact format in 32 KB stages of any length, so every write but the first starts mid-sector [D]. That the stick then rewrites sectors is [U].
- **The worst sound gaps follow the long checkpoint and the landing's loads.** Voices ran dry for 23.8 s in all (10 gaps, the longest 3.0 s) in the report after the 3.1 s checkpoint. 14.4 s (longest 2.5 s) during the landing, where the main thread used the stick for 3.6 s of a 5 s report.
- **R11 held the log's writes** around each checkpoint, as designed.

## Test 2: the cinematic (presets 2, 3, 4, 5)

b30, hands off through the opening cinematic, stopped in the Pelican (game time 1,125–1,221). Preset 2 (run by mistake) is preset 3 plus `iostats`, so it serves as a second base run.

| | Base (2) | Base (3) | 600 KB (4) | 400 KB (5) |
|---|---:|---:|---:|---:|
| Cinematic (150–570): frame, ms | 196.1 | 194.2 | **181.4** | 186.0 |
| drawing, ms | 72.4 | 72.3 | **61.3** | 64.9 |
| tick, ms | 59.7 | 59.7 | 60.1 | 60.2 |
| fps | 5.10 | 5.15 | **5.51** | 5.38 |
| Flight (300–2000): frame, ms | 263.7 | 272.2 | **233.9** | 242.2 |
| drawing, ms | 142.0 | 147.6 | **110.3** | 118.3 |
| fps | 3.79 | 3.67 | **4.28** | 4.13 |
| GE busy, cinematic / flight, ms a frame | 45 / 83 | 45 / 88 | **31 / 54** | 32 / 58 |
| Heap top, KB | 5,196 | 5,184 | 5,220 | 5,220 |

**The GE's time by kind of draw**, ms a frame, weighted by frames:

| | Base (3) | 600 KB (4) | 400 KB (5) |
|---|---:|---:|---:|
| Cinematic: models | 31.4 | **21.8** | 23.6 |
| level | 2.3 | 3.7 | 3.6 |
| Flight (570–1,300): models | 106.3 | **59.0** | 63.8 |
| level | 10.0 | 10.6 | 10.5 |

- **The two base runs agree within 1%** on the cinematic, so the differences are real.
- **The models' GE time falls by a third to a half.** The level's rises 1.4 ms in the cinematic, as textures lose that VRAM (b115's test 3 predicted 1–1.5 ms).
- With 600 KB, 76 buffers sat in 545 KB of VRAM, and 96% of model draws read a copy (63,893 of 66,285).
- No draws skipped, no sound failed for room; the tick is unchanged.

## Test 3: c40, preset 1

No fault: Sheepdog01 played to game time 11,146, past b115's crash at 1,424. 13 screenshots.

| Stretch (game time) | Frame, ms | Drawing, ms | Tick, ms |
|---|---:|---:|---:|
| the opening (0–1,800) | 104.2 | 42.7 | 21.8 |
| the control room (1,800–6,000) | 98.9 | 58.4 | 18.2 |
| after (6,000–11,146) | 128.6 | 74.0 | 23.0 |

- **On the shots:**
  - Guilty Spark draws with his glow and a soft halo (000–004).
  - The room's pale discs are still there (000, 002–004): the hologram's `static blue band vert`, identified in the emulator (`b117_candidates.md`).
- **Meter parts drawn:** up to 34 in a report; none left out.
- **Checkpoints were slow here too:** 190 pages (368 KB) in 699 ms, against b115's 171 ms for the same 190 pages; later ones 392–566 ms.
- 2 draws left out for missing geometry over the run; heap peak 5,632 KB.

## Test 4: c10, preset 1

No fault: Sheepdog01 played to game time 6,141, from the swamp into the first building. 9 screenshots.

| Stretch (game time) | Frame, ms | Drawing, ms | Tick, ms |
|---|---:|---:|---:|
| the swamp (150–4,000) | 172.1 | 87.6 | 39.5 |
| inside (4,000–6,141) | 108.7 | 58.5 | 21.2 |

(b115's run: 183.0 and 117.6 ms over longer stretches, so not a like-for-like comparison.)

- **The swamp water** is dark and see-through (shot 000) [M]: R2b restored.
- **The hanging vines** are still solid green bars (001–003): open.
- **The bunker's light cones** are still bright and solid (004–005): open, the chicago family.
- **The swamp fog isn't drawn: b116's planar fog never engaged** [M]. It logged 0 windows in planar fog in every report, and so did the emulator's final tour [E].
  - b116 applies planar fog only while the eye is inside it.
  - In the swamp the eye is above the fog's plane. There the Xbox fogs what lies below the plane by its depth, as in b30's deep water [D].
  - `build_b116.md` is corrected.
- **Checkpoints** took 439–2,046 ms (599–913 KB), against b115's 153–201 ms for the same map [M].
- 553 draws left out for missing geometry, 536 of them in the first frames: the emulator left 536 out at the same place.

## Test 5: b30, preset 1 (the meters)

No fault: Sheepdog01 played to game time 3,111. 2 screenshots.

- **The plasma rifle's heat gauge draws** [M]: blue when cool (shot 000), then green to red as it heats (shot 001), beside the HUD's heat bar. Meter parts drawn: up to 245 in a report, none left out.
- **Checkpoints:** 520 ms and 1,522 ms (768 and 945 KB).

## Test 6: b30, preset 6 (the b115 look)

Stopped at game time 1,258, in the flight; no screenshots.

| | Test 1 (every restoration on) | Test 6 (every restoration off) |
|---|---:|---:|
| Cinematic: frame, ms | 193.2 | 191.4 |
| drawing, ms | 71.7 | 70.3 |
| Flight: frame, ms | 271.9 | 271.1 |

- **The restorations cost under 1% on the cinematic** [M], inside the run-to-run spread.

## Test 7: the profile-guided EBOOT (b30, preset 3)

`halo_psp_b116_pgo.zip`, b116 built with the profile `pgo/campaign5.tgz` (the log's second line says so). No fault; stopped in the Pelican at game time 1,230.

| | Base (presets 2 and 3) | PGO |
|---|---:|---:|
| Cinematic: frame, ms (fps) | 194.2–196.1 (5.10–5.15) | **182.6 (5.47)** |
| drawing, ms | 72.3–72.4 | 72.2 |
| tick, ms | 59.7 | **55.6** |
| Flight: frame, ms (fps) | 263.7–272.2 (3.67–3.79) | **255.6 (3.91)** |
| tick, ms | 60.0–60.6 | **56.1** |
| GE busy, cinematic / flight, ms | 45 / 83–88 | 43 / 86 |

- **About 6% faster on the cinematic** [M], from the tick (−7%). The emulator predicted −6.1% on the tick and −9.2% on drawing [E]; the PSP shows the first, not the second.
- **It should stack with the VRAM model buffers**, which cut the drawing and the GE's time instead [U].

## Bonus: Blood Gulch on the PGO EBOOT with 600 KB of VRAM model buffers

Sheepdog01's own play, after test 7, with the PGO EBOOT still installed and preset 4's keep words (`vrammodels600`): effectively 117's speed setup. An older Blood Gulch bake (no see-through tints). No fault; game time 8,064; no screenshots [M].

- **The whole run:** median 18.6 fps; 30 ticks a second in 30 of 67 reports; 15 fps or more in 50.
- **From game time 4,200 to the end:** 29–30 ticks a second and 19–29 fps, the last reports at 27–29 fps with 30 ticks a second: the primary target.
- **For scale:** b46 drew 12–28 fps by view, and the first net game (mp3) ~16 fps. Different views and play, so this is a trend, not an A/B.

## Sheepdog01's notes and calls after the tests (2026-10-12)

1. **S9 sounds: keep.** "More or less the same" on the PSP's speakers. No dropouts heard in test 1. The sounds missing in b115's test were easier to hear.
2. **Model buffers in VRAM: 600 KB by default in 117.** "The single most noticeable performance improvement so far."
3. **Glows and light cones: acceptable for now.** "Transparent gradients rather than solid light cones."
4. **c40, "by far the most broken map so far":**
   - broken textures on the bridge;
   - the bridge glass's edges shine through Chief's legs, and his legs look see-through;
   - the stencils and lights in the walls flicker as he moves and phase through the geometry, as on Blood Gulch before;
   - the glass in the trenches across the great hallway isn't drawn;
   - objects flicker outside, on the pyramid's top level.
5. **c10:**
   - "a vast improvement";
   - almost no water effects when something goes into the water;
   - the vines still broken;
   - the elevator's textures somewhat flat;
   - "every ms helped at the floor".

## Fixed on the `hudbars` branch for 117, after tests 1–3

| Fix | How | Keep word (as b116) | Check |
|---|---|---|---|
| The checkerboard | The dither only on draws that don't blend, and on the level's lightmap pass (which sets the level's brightness). Every blend switch goes through `ge_blend()` | `ditherall` | `dither118` (38326421791) [E]: the same frames under each rule; the pattern's spread on b30 28.2 (max 40.3) with b116's rule, **7.7 (max 8.3)** with the new one, 1.4 with none; a10 13.4, **6.2**, 0.7. What's left is one pass's dither, about one colour step. b116's rule also darkened the picture by up to 5.9 of 255 levels (b30: mean brightness 67.5 against 70.9 without dither); the new rule is within 0.6 |
| The floor's light through the hallway wall (#13) | Each corner of a cut piece moves toward the eye along its own line of sight, by the lift its own depth needs, never past the near plane; no depth offset | `lightcutone` | `lightcut118` [E], at Sheepdog01's spot (route `b30_bunker_hallway`) |
| Objects vanishing (R4) | A draw whose geometry isn't resident reads it at once, past the frame's share, while the frame has read under 384 KB. Up to ~110 ms more in such a frame at the 1.7 MB/s the PSP read geometry in test 1 [M, U] | `geoshare` | `geoshare118` [E]: the draws left out, and the strict interpreter |
| Slow checkpoints | The compact format written in whole 4 KB pieces; the file's bytes are unchanged | — (exact) | the PSP's next checkpoint times |
