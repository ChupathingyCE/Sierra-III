# The parity budget: what 30 fps asks of each stretch (2026-11-07, master)

Sheepdog01's goal is a 30 fps average per level over interactive play, one frame per tick (`CLAUDE.md`, the goals). At parity each frame holds one tick, so **one tick + one frame's drawing + the frame's other work must fit in 33.3 ms on the main CPU**, and the Media Engine's replay of a frame must fit in the same 33.3 ms beside it. Sheepdog01 (2026-11-07 23:20) asked for headroom on top, since combat with vehicles and many AI is the game's normal state: this page budgets **28 ms** (~15% under 33.3).

## Where each stretch stands (b151, PSP-3000, lab `stretches.tsv`, ledger1) [M]

One tick is the stretch's tick time over its ticks a frame. The main CPU's work at parity is one tick + drawing + other; the ME's is its replay a frame.

| Stretch | Frame today, ms | One tick | Drawing | Other | **Main CPU at parity** | Over 28 ms | ME replay | Over 28 ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| b30 opening | 84.6 | 32.4 | 16.9 | 2.2 | **51.5** | 1.84x | 25.9 | 0.92x |
| b30 Pelican flight | 127.3 | 42.4 | 35.8 | 4.7 | **82.9** | 2.96x | 69.2 | 2.47x |
| b30 landing | 114.6 | 35.3 | 37.8 | 4.7 | **77.8** | 2.78x | 66.0 | 2.36x |
| **b30 beach fight** | 96.0 | 33.5 | 21.8 | 6.1 | **61.4** | 2.19x | 25.4 | 0.91x |
| c20 after the opening | 60.9 | 19.3 | 16.0 | 5.4 | **40.7** | 1.45x | 40.9 | 1.46x |
| d20 swamp walk | 84.8 | 30.7 | 19.2 | 7.6 | **57.5** | 2.05x | 29.1 | 1.04x |
| d20 flashlight on | 91.3 | 20.5 | 25.8 | 6.1 | **52.4** | 1.87x | **78.4** | **2.80x** |
| b40 opening | 57.9 | 19.4 | 20.1 | 4.5 | **44.0** | 1.57x | 36.2 | 1.29x |
| b40 cliff path | 104.9 | 35.9 | 26.4 | 5.3 | **67.5** | 2.41x | 43.0 | 1.54x |
| **b40 snowfield fight** | 103.0 | 37.6 | 23.4 | 4.5 | **65.5** | 2.34x | 26.1 | 0.93x |

**What it says:**
- **Every measured stretch needs the main CPU's frame cut 1.5-3x.** The combat stretches (b30's beach, b40's snowfield and cliff) need ~2.2-2.4x; the flight and landing, ~2.8-3x.
- **The tick is the larger half on the combat stretches** (beach: 33.5 of 61.4; snowfield 37.6 of 65.5); drawing is the larger half on the flight and landing.
- **The ME sets the frame in some stretches, not others.** It's under 28 ms on the beach and the snowfield, but 2.4-2.8x over on b30's flight and landing and **d20's flashlight**, where the main CPU sleeps 18.9 ms a frame waiting for it. Work that cuts the ME's replay (R's ME side: the memo, the carry, the replay bypassed) matters on those, and doesn't on the beach.
- These are b151's figures. The tock since (the RH5 bundle, `CLAUDE.md`'s `PSP_SMALLDATA`) measured b30 5-13% faster; the combat benches (below) re-measure them.

## What the parts reach, against the gap [U on M]

The redesign's parts with their ceilings where they're known per stretch (`redesign_plan.md`, `redesign_r.md`, `redesign_s.md`, lab `redesign.tsv`):

| Part | What it cuts | Ceiling | Where it's known |
|---|---|---|---|
| R, slice 5 (the per-part loop skipped) | Main CPU drawing | ~21% of drawing: ~7.5 ms of the flight's 35.8; ~4.6 ms of the beach's 21.8 if the share holds | PSP sampler, b30's flight (42444519821) |
| R, the replay bypassed (later slices) | The ME's replay | ~29-31 ms of the flight's 69.2 (plan) | estimate |
| S3, AITICKOLD | The tick's AI pass | -5.9 ms a tick on the flight [M], the beach in the noise; needs ME room; built for two ticks a frame | PSP-3000 pair 42041663929 |
| S4, the far-biped tier | The tick's far bipeds | ~2.3-4.6 ms a tick on the flight; ~2.75-5.5 on the snowfield | shares x the PSP figure |
| S1, S2, T5/T7 | The tick | ~0-1 ms each | measured or estimated |
| B (VRAM, the tag window) | The GE's toll on the tick | ~2-4 ms | estimate |
| A (audio around the VME) | The ME's replay | ~6-9 ms on d20 | the placement pair (42423884489) |

**Against the beach** (61.4 -> 28: -33.4 ms needed): R's slice 5 ~4.6, S4 (if its share holds there) ~2-4, the toll ~2-4, AITICKOLD in the noise there today, T5/T7 ~1: **~10-14 ms of the 33**. The parts as designed close about a third of the beach's gap. The rest needs work not on the plan yet: the tick's structure (collision 25% of the tick, node matrices 11%, `redesign_tick.md` T6), and whatever the combat benches show the beach spends that the flight didn't.

**Against the snowfield** (65.5 -> 28: -37.5 ms): `redesign_s.md` sums S at ~7.8-11.5 ms a tick there, and R's slice 5 ~5 ms: ~13-17 ms, under half.

## What has to change in how we measure

- **Combat benches, not the cinematic** (Sheepdog01 23:20). The flight stays only as a low-noise check that a change doesn't break anything; parts are priced and ranked on combat stretches.
- **Combat that repeats: done** (2026-11-08). The pad recorder (`pad_recorder.md`) replays a recorded fight's inputs and time steps. Once two races were fixed (edc2d39, 74cd771), a recorded b30 beach fight (lab `pads/b30_padrec2`, scripted in the emulator) replays exact on the PSP-3000 (play and seeds identical on all 3,900 ticks), and two runs agree within 0.2-0.4% a stretch without statecrc (lab sierra-hw 42493868743) [M]. Its beach on tock162 929afdaa: one tick 36.1 ms, drawing 26.2, other ~6.0: **68.3 ms at parity, 2.44x over 28** (with the bench guards `readswait aimewait`). Next: Sheepdog01's own recorded fights (`padrec fixedseed`) on the busy stretches.
- **Each part's ceiling per combat stretch**, then this page's table re-filled from the combat benches on the current tock.
