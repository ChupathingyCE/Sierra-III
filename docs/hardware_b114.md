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
- **The ocean clipping into the seabed in the flight** (Sheepdog01): the cause is read in the code and the emulator's log [D + E].
  - A see-through surface that isn't on a wall is pushed away from the eye in the depth test, one push per draw. The water's push came from a corner at the near plane: 2,457–4,096 depth steps, hundreds of units at 70 off.
  - The fix is on the emulator first (branch `water-depth`, keep word `overlayfreefar`).

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

## Next

- Tests 5–9 as in `build_b114.md`. Test 2 is skipped.
- The water A/B on the emulator. If it holds, `overlayfreefar` goes into b115 for Sheepdog01's A/B.
