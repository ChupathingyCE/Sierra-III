# Build 114 on the PSP-1000 (2026-10-11)

Sheepdog01's PSP-1000 at 333 MHz, b30, `halo_psp_b114.zip` (EBOOT `8ae1188134612709`). The tests are listed in `build_b114.md`, and the figures come from `tools/benchreport.py` and `tools/hwreport.py`. Labels: **[M]** measured on the PSP, **[E]** emulator, **[D]** read in the code, **[U]** inference.

## In short

- **b114 is a little faster than b113, and it draws everything b113 dropped** [M]:
  - The opening cinematic: 195.6 ms a frame, against b113's 197.5–199.0. Ticks were 3% cheaper, because the sleep's checksum (multiply by 33) gave back what b113's FNV-1a took.
  - The whole bench (game time 300–7,400): 170 ms a frame against 181 (−6%), 5.9 fps against 5.5.
  - Nothing dropped: 0 draws skipped with the list full (b113 Test 1: 18,033) and 0 see-through draws dropped (494). 3 sounds failed for room (108).
- **Test 2 (`listcachedall`) hung the GE** within 2 s. Shelved.
- **Test 3 (`statememo`) changed nothing** that the PSP can measure.
- **Test 4 (`lean`) saves 2–3% of the frame**, not the 8–12% the emulator showed.
- **Test 5 (`gemeon`): step 4b's fixes hold** (no failed jobs, no late audio). The ME's runs are still serial, so it costs 7% in the cinematic, and textures the ME can't load show as one flat-coloured frame (the red pistol).
- **Test 6 (`drawdist60`): about 3%**, and the fog at 60 looks acceptable (Sheepdog01). 40, 30 and 20 go to the emulator first.
- **Tests 7 and 8 (the looks):** the zoom overlay, the tracers and the plasma pistol need a reference to aim at (the PC build). The bake's half-size, 16-colour effect textures are a likely part of it. Preset 7, without `hwprofile` and `gebusy`, had smaller frame spikes.
- **Preset 9 (`drawdist40`): about where 60 is** (−6% in the approach). The beach's big jump was the player standing still at the landing spot.
- **The ocean's clipping is our depth push**, not the 16-bit depth buffer: it's fixed on the emulator (`overlayfreefar`), for b115.
- **On the PSP, only the opening cinematic repeats from run to run.** `benchreport.py` now reports it first, as the stretch to use for A/Bs.

## The cinematic is the PSP's fair A/B stretch [M]

Until game time 570 the camera is scripted. After that, the player looks around inside the Pelican.

| Frame by the clock, ms | b113 T1 | b113 T2 | b113 T3 | b114 T1 | b114 T3 | b114 T4 |
|---|---|---|---|---|---|---|
| The cinematic (150–570) | 199.0 | 198.0 | 197.5 | 195.6 | 195.6 | 190.5 |
| The rest of the approach (570–1,100) | 288.5 | 310.1 | 287.8 | 291.3 | 306.5 | 295.2 |

- **Three runs of one build fall within 1% in the cinematic, and within 7% after it.** The later spread is where the player looks, not the code [U].
- **Earlier notes called the whole flight "the fair A/B stretch".** That holds for the emulator, whose input is scripted. On the PSP, use 150–570; anything longer needs several runs.

## Test 1: b114 as it comes

Keep words `nearauto assettrack geo2 hwprofile gebusy`. No fault. The run went from the start through the beach and its Warthog into the first bunker (game time 7,408). Both runs took the same way down (player positions [M]).

| Frame by the clock, ms (drawing; tick) | b113 T1 | b113 T2 | b114 T1 |
|---|---|---|---|
| The cinematic (150–570) | 199.0 (71.2; 61.6) | 198.0 (72.1; 61.5) | **195.6** (72.2; 59.9) |
| The flight (300–2,000) | 273.7 | 270.7 | 279.4 |
| The landing (2,000–2,700) | 226.2 | 196.7 | 192.0 |
| The early beach (2,700–4,087) | 127.1 | 123.8 | 153.5 |
| The late beach (4,087–6,000) | 198.8 | (stopped at 4,087) | 174.1 |
| The bunker (6,000–7,400) | 77.6 | | 63.0 |
| **All (300–7,400)** | **180.5** | | **170.3** |

- **The sleep's checksum** [M]:
  - In the cinematic, scenery's update went from 10.0 to 8.4 ms a tick (−16%), for the same objects.
  - b113's notes put FNV-1a at about +1 ms a tick over b112's rotate-and-xor. A multiply by 33 is a shift and an add, with no multiply instruction.
- **The hand-played stretches vary with play** [U]:
  - The flight's +2% is inside the spread the same build shows after the cinematic.
  - The early beach was the one slower stretch. Its drawing rose (the GE renderer's CPU 41 ms a frame against 24–31), and so did the AI's update: the kind of change a bigger fight makes.
  - The late beach and the bunker were faster (−12%, −19%).
- **The list and the see-through table** [M]:
  - The list peaked at 601 KB of its 768.
  - No draw was skipped, and no see-through draw was dropped.
- **Sound** [M]: 3 failed for room, all in the approach (game time 1,407–1,559), with the cache full (1,792 of 1,792 KB).
- **The heap** [M]:
  - It peaked at 5,520 of 5,888 KB from game time 4,874 on (the Warthog fight), with 368 KB to spare.
  - b113's estimate for the whole level was about 5,680 KB [U]. Test 9 will settle it.
- **The render-state cache filled** at game time 4,557 (512 of 512) and stayed full, as b113 Test 1's did at 5,128 [M]. Test 9 (the cliffs) is its test.
- **Bodies** [M]: on the beach, 1.2 bodies a tick were held up by something other than the level and kept awake, with 7 ground tests in the window. b113's rule would have let them sleep where they hung.
- **Tracers: none, as expected.** `contrails` is off by default, and preset 1 doesn't set it; presets 7 and 8 do.
- **The ocean clipping into the seabed in the flight** (Sheepdog01): the cause is found, and a fix has been tested on the emulator. See "The ocean" below.

## Test 2: `listcachedall`: the GE hung [M]

- **The stop:** at game time 61 (about 2 s in), the game stopped. Heartbeats went on, with 0 frames and 0 ticks; the main thread sat in the frame end's phase, waiting for the GE.
- **Before the hang:**
  - The GE was busy 183–296 ms a frame, against Test 1's 133–167 at the same game times.
  - Drawing's uncached stores were down from 533 to 139 per million cycles, so the switch did what it was built for.
- **The cause** [U]: a line of the list, either command words or a region, was still in the CPU's cache when the GE read it. The stale words drew garbage, then something the GE couldn't get past.
  - The emulator has no caches: its GE always sees the CPU's latest writes, so it can't catch this.
- **What it was worth** [U]: Test 1's beach drew with 744 uncached stores per million cycles. At about 37 ns each, that's at most 1% of the drawing time.
- **Shelved.** Finding the missed line isn't worth that 1%.

## Test 3: `statememo`: no change [M]

- **The cinematic:** frame 195.6 ms, as Test 1; drawing 71.9 against 72.2; GE busy 44 ms against 44.
- **What the memo left out:** 20 of 237 texture binds a frame (8%), and 90 of 126 lighting sets (71%). That's about 1% of the list's words [U], too little to show.
- **The rest of the approach:** 306.5 against 291.3, inside the 288–310 that one build gives there.
- **Leave it off.** It isn't worth a default.

## Test 4: `lean`: 2–3% [M]

- **The cinematic:** frame 190.5 ms against 195.6 (−2.6%), drawing 70.4 against 72.2, tick 58.3 against 59.9.
- The preset also leaves out `hwprofile`, so the profiler module's own cost is part of the saving.
- **The emulator said 8–12%** [E]. Its timer calls must cost far more than the PSP's [U].
- No overlay, as expected.
- **Sheepdog01's call:** 2–3% of the frame, against the fps readout and the step timers.

## Test 5: `gedefer gemeon`: step 4b's fixes hold; serial, so it costs time [M]

The run went from the start to the beach (game time 3,568). No fault. Sheepdog01 saw no floating bodies, one flicker from a body, and a red flash on the pistol when he switched to it straight out of the Pelican.

- **b112's failures are gone** (`big_levers.md` §4, step 4a):
  - 0 render or audio jobs failed over the run. b112's ME stopped at job 49 on its 40 ms limit; the worst render job here was 64 ms, against the new 500 ms limit.
  - 0 of 16,764 audio blocks were late. In b112, 7 of 109 were late, waiting behind render jobs.
  - No wanted list overflowed: 660 textures, 299 model buffers and 127 level copies were wanted by the ME's work, all fitted, and all were done at the frame's end.
- **It costs 13 ms a frame in the cinematic:** 208.8 ms against Test 1's 195.6 (+7%).
  - In steps 4a and 4b the main CPU waits out each ME run: 14.9 ms waited for 14.6 ms of the ME's work, about 2.2 runs a frame.
  - So no speed-up was expected. Step 4, the overlap, is what would make it pay.
  - Where the rest of the 13 ms goes is open [U]. The audio jobs' longer waits (2.9 against 2.7 ms a job) account for about 2.5 ms of it.
  - The hand-played stretches came out −4% to −7%, inside their run-to-run spread.
- **The red pistol is an untextured draw** [D for the mechanism, U that it's what Sheepdog01 saw]:
  - Under `gemeon`, a texture bind on the ME can't read the texture from the stick. It asks for it at the frame's end and fails (`psp_tex_bind`).
  - An untextured model part is drawn in a flat warm colour, picked per shader (red 160–255, green 70–165, blue 40–103; `psp_fixups.py`).
  - The first frame after the switch, the pistol's textures weren't resident: one frame of a red-orange pistol. Without `gemeon` the main CPU reads the texture in the same frame, within the 96 KB budget.
- **The body's flicker** is likely the same kind of thing [U]: a model buffer wanted converted is skipped for that frame.
- **For step 4:** when the ME's bind fails, skip the part for that frame instead of drawing it flat, or read the textures the frame will want before the ME starts.

## Test 6: `drawdist60`: about 3% [M]

The opening bench to the bunker (game time 6,555). Sheepdog01: "the fog at 60 looks acceptable, it seems to push the lows up slightly, but it's far enough out that it doesn't really do much."

| Game time 570–6,000 | Test 1 | Test 6 (60) |
|---|---|---|
| Frame, ms (hwreport, by heartbeat) | 126.2 | 122.9 (−3%) |
| fps, mean | 4.90 | 5.14 (+5%) |
| fps, 10th percentile | 2.90 | 2.96 (+2%) |

- **It matches the emulator** (−5% of drawing at 60 [E]) and what Sheepdog01 felt. One run can't pin it down further: hand-played stretches vary by ±7%.
- **The cinematic was unchanged** (196.1 against 195.6), because the far clip is lifted during cutscenes. So the PSP's one repeatable stretch can't measure this switch.
- **Closer in, on the emulator** (lab batch `drawdist-near`, run 38156283151, b114 at `c238665149`; the bench's scripted route; the shares transfer to the PSP, the milliseconds don't) [E]:

| Frame by the clock (drawing) | None | 60 | 40 | 30 | 20 |
|---|---|---|---|---|---|
| The flight after the cinematic (570–2,000) | 94.0 (47.8) | −3% (−6%) | −6% (−13%) | −9% (−18%) | −12% (−23%) |
| The landing (2,000–2,700) | 81.0 (45.4) | −2% (−5%) | −6% (−15%) | −11% (−26%) | −24% (−39%) |
| The beach (2,700–6,000) | 48.2 (21.3) | −1% (+2%) | +3% (0%) | +5% (−4%) | −9% (−15%) |
| The bunker (6,000–8,100) | 40.9 (15.4) | −4% (−8%) | +1% (+1%) | +3% (−3%) | −6% (−10%) |

  - **The saving is in the open views,** the flight and the landing. The beach fight is mostly within 30 units, and its ticks (the AI), which the draw distance doesn't touch, moved by up to 13% between runs because play parted.
  - **The pictures** (a screenshot every 20 s):
    - At 40, the cliffs and trees past the beach fade into a pale haze.
    - At 30, they're nearly gone.
    - At 20, the beach is in thick fog, the Covenant are silhouettes at its edge, and a hard line shows where the far clip cuts the ground against the unfogged sky.
  - **At 20–30 the fog starts at 12–18 units,** inside the range Elites fire from on the beach.
  - **40 is the one worth a PSP run:** about −6% of the frame in the open stretches. Below that, the look and the fight change more than the frame does.

## Tests 7 and 8: the looks [M]

Preset 7 (`contrails zoommask`) ran from the start to the bunker (game time 7,436). Preset 8 (`contrails zoommaskfull`) ran to game time 1,511 and drew 24 zoom masks.

**What Sheepdog01 saw:**
- The pistol's zoom overlay looks offset and stretched.
- The plasma and rifle fire show long streaks rather than a projectile flying forward.
- The plasma pistol's bolts look like a low-quality cloud.
- Run 7 felt the smoothest of the night and looked the best.

- **Smoothness:**
  - The average frame was the same as Test 1's: 195.1 ms in the cinematic, against 195.6.
  - The spikes were a little smaller. A heartbeat's worst frame was 1.63 times its average at the 90th percentile, against Test 1's 1.98 and Test 6's 1.77.
  - Preset 7 is the only bench preset without `hwprofile` and `gebusy`. The profiler module and the GE's interrupt every 16 draws are the likely source of the hitches [U]. Preset 1 without those two words would test it.
  - Nothing in the log explains the look [U].
- **The zoom overlay** [D]:
  - The port draws the screen effect's convolution mask as a darkening layer, a stand-in for the blur (a third of its alpha, or all of it with `zoommaskfull`). On the Xbox, that mask marks where the screen is blurred; it isn't drawn as it is.
  - The texture is spread over the 4:3 square in the middle of the screen.
  - The mask's soft gradient, held in a palette, shows as rings.
  - What the real effect looks like has to come from a reference: the PC build at the same moment.
- **The tracers** [D]:
  - The contrails are drawn from the game's own vertices, with their per-point colours (Test 8: 221 contrails, about 11 segments each, in its last 300 frames), so their length is the tags'.
  - What reads as wrong is more likely what goes with them [U]: the bolt's head (its light volume, glow and flare are widget sprites), the effect textures, and the 5650 framebuffer with no dithering.
- **The bake as a cause** [D for the policy, U for these bitmaps]:
  - Every texture loses its top mip, so it's stored at half resolution.
  - A texture goes to 16 colours (CLUT4) when its PSNR is no worse than the 5650 framebuffer's own. That measure can pass a glow's soft alpha gradient while banding it.
  - To check: the format the bake chose for the plasma, contrail and zoom mask bitmaps, then a bake variant that keeps effect textures at CLUT8 and full size.

## Preset 9: `drawdist40` [M]

The opening bench to game time 5,118. Preset 9 was added on the stick after the emulator's runs (keep words: preset 1's plus `drawdist40`).

| Frame by the clock, ms (drawing) | Test 1 (none) | Test 6 (60) | Preset 9 (40) |
|---|---|---|---|
| The cinematic (150–570) | 195.6 | 196.1 | 196.1 |
| The approach (570–2,000) | 298.9 (156.7) | 278.8 (143.0) | 281.9 (139.9) |
| The landing (2,000–2,700) | 192.0 | 199.1 | 189.2 |
| The landing spot, same game times (2,700–3,400) | 134.5 (hwreport) | 115.1 | 115.9 |

- **40 is about where 60 is.**
  - The approach: −6% at 40 and −7% at 60, though the drawing's CPU was −8% at 40 against −3% at 60.
  - The landing spot: −14% for both, with the drawing's CPU −25% at 40 against −15%.
  - The extra 20 units buy a few milliseconds of drawing, inside the run-to-run spread.
- **The beach's +69% fps is not the draw distance.** From game time 2,700 to the end, the player stayed at the landing spot (−54, −19, 4), so the fight never moved and the view stayed put. Test 1 drove down the beach in the Warthog over the same game times.
- **Sounds failed for room: 18,** all in the approach (game time 1,565–2,163), with the cache full. Test 1 had 3, test 6 had 9, and preset 7 had 8. The approach's dialogue and music still overflow the 1,792 KB cache now and then.

## The ocean: Sheepdog01's "clips through the terrain under the water" [D + E]

**The cause** [D]:
- b30's ocean ("water simple") is one of the level's see-through surfaces. Since build 65, a see-through triangle that isn't painted on a wall is pushed away from the eye in the depth test, so solid edges win ties (it was added for the teleporter's shield).
- The water is never "on a wall": the collision has no surface there (water is a fog plane in it), so the wall test finds nothing.
- Each draw goes out at the largest push of its triangles. A water triangle with a corner at the near plane asks 0.005 units of lift there, which is 307/near depth steps: 2,457 at the near plane ×2, and the 4,096 cap at ×1.
- One depth offset is a lift that grows with the square of the distance. At 70 units off that's hundreds of units, so those draws lost to any seabed under them.
- Triangles wholly in view got the 3-step floor, about 2 units at 70 off at ×2.

**Why it varied:** the push depends on the nearest water corner and on the near plane, and `nearauto` switched between ×16 and ×2 as the Pelicans came close.

**The emulator A/B** (lab batch `water-depth`, run 38153180983, branch `water-depth` at `a20f63d991`) [E]:
- The flight ran three ways on the same frames: b114, `overlayfreefar`, and `nooverlays` (no see-through surfaces at all). Play was identical until tick 1,883, so the flight's shots compare the same moments.
- The water draws' pushes went from 3–4,096 steps (b114) to 1–9 steps (`overlayfreefar`) (`tdump`).
- **With no see-through surfaces at all, the ocean shows lavender**: the seabed under the water, in the fog. So b114's patchwork of white and lavender, with straight edges, was the water drawn on some triangles and pushed behind the seabed on others: Sheepdog01's "only partially loads".
- **With `overlayfreefar` the water covers the whole ocean,** and at the landing it covers the shallows up to the beach, with its ripples visible near the camera.
- **What it changes:**
  - Free-standing see-through surfaces are now pushed back by a lift measured at their farthest corner, with a 1-step floor, and a draw takes the smallest push of its triangles.
  - A small surface near the eye (the teleporter's shield) gets about the push it had before [U]. A check at a teleporter on Blood Gulch would confirm it.

**The white is a separate problem** (`b113_candidates.md` #5) [D]:
- On the Xbox, "water simple" takes its colour from a reflection cube map, which the GE can't draw. The port draws one ripple map, untinted, and from afar its mips average to near white.
- With the depth fixed, more of the ocean is that white.
- The fix is a tint per shader, baked from the cube map's mean colour and the shader's stage constants.

## Next

- Test 9 (optional, the cliffs) is still to run. Test 2 is skipped.
- b115: `overlayfreefar` for Sheepdog01's A/B, and the water's tint.
