# Direction check at R's milestone (2026-11-09 17:50, master)

Sheepdog01 17:44: verify the shape of the direction, that we aren't chasing something that won't pay, and that nothing is left on the table. Read with `redesign_plan.md` §1-§1b, `redesign_tick_target.md` §4 and `redesign_r_slice5.md`. b30's flight is the worked case; the other heavy stretches have the same shape at smaller sizes.

## The frame equation, from the PSP [M on U]

On the main CPU a frame holds its ticks, its drawing and a few ms of the rest (waits, frame end, other). R's milestone pair 3 (sierra-hw 42718514224, PSP-3000, the bypass on) on the flight: tick 40.9, drawing 41.7, frame 129.1 = 2 × 40.9 + 41.7 + ~5.6.

| Goal | The frame | What it allows, rest at ~5 ms |
|---|---|---|
| Floor: 30 ticks a second, 15 fps | 66.7 ms = 2 ticks + drawing + rest | tick 25.8 needs drawing ~10; **with today's 41.7 the tick may cost only ~10** |
| Target: 30 fps, one tick a frame | 33.3 ms = tick + drawing + rest | tick 16.6 needs drawing ~12; tick + drawing ~28 |

**So the main CPU's drawing is on the critical path of both goals, as much as the tick.** The plan says so (§1: "drawing to ~10") and books it to R. §1a's and §1b's tables carry R at that design figure.

## What R delivers against it [M]

- **The bypass** (slice 5's 5a.1-5a.2, pair 3): drawing −0.7 to −4.6 ms (−2.9% to −8.3%), frame −1.4% to −4.9%. Real, and cheap to keep.
- **Slice 5's whole ceiling** is the per-part chain, ~21% of drawing, ~7.5 ms of the flight's (the PSP sampler, 42444519821). At its top the flight's drawing is ~34-36 ms, not ~10.
- **R's earlier slices** (3a-3e, drawing inside the ME's replay) moved the frame −0.7% to +1.0% (42435834901): the main CPU sets b30's frame, so ME-side savings don't show until the main CPU is near the ME.
- **R as a whole against the old path hasn't been priced in today's setup:** pair 3 compared the bypass inside R (R's lean words on both sides).

**The gap no part owns: ~24-26 ms of the flight's drawing a frame** (from ~34-36 at slice 5's top to ~10). In the sampler that's the walk R keeps (16%: `render_model` 8.9, the object list, LOD, the model begins) and ~59% spread over the level (4.1), visibility (3.3), copies (3.0), sprites and the HUD (3.0) and a long tail that has never been split further on the PSP.

## The other two walls, for completeness

- **The tick:** −25.8 ms a tick at the target; the known ceilings sum to ~10.4-14.2 (target page §4), a gap of ~11.5-15.5. Bottleneck owns it (`tick_map.md`). That sum predates Sheepdog01's 16:07 and 17:30 rulings: collision (25.6% of the tick) and the AI's main-CPU share carry no lever there and are now open under "plays the same"; and its by-system split rests on the emulator (the lab's handoff, dig 1).
- **The ME at 30 fps:** ~1,650 ms a second on the flight after R at R's *design* figure (×0.57 of the replay), which the PSP hasn't measured yet. **The GE:** ~1,800 → ~1,050-1,150 ms a second on paper, every row [U on E], and several are look changes (error-budget LODs, passes trimmed on small models) to be classed under 15:55 and 16:07 before they count.

## So

1. **Keep R's bypass and finish slice 5's measured chain** (5a.3 the locks' touch, then the pretouch and the record copy: ≤~9% of drawing): it pays, but it is lever-sized. It isn't the road to drawing at ~10.
2. **Open the drawing side as its own map**, like `tick_map.md` for the tick: what each piece of the main CPU's drawing computes and who reads it, its PSP share on the heavy stretches, every lever tried on it, and the algorithm question. First: split pair 3's drawing on the PSP (its runs already carry PC samples; they need the build's symbols, a lab job, not a new PSP run).
3. **R's own design is the obvious candidate to price before anything new:** the instance as GE words (`redesign_r.md`: ~10-20 words a rigid part instead of ~140-200, no per-part bind, lighting or geometry chain), extended upward to the walk (an object's visible parts, LOD and permutations carried from frame to frame, checked by stamp). That is the piece that would take the main CPU's drawing toward ~10; slices 1-5a built the forms and the arguments, not the walk.
4. **Price R whole against the old path once** (the redesign rule's milestone), ME time included, so §1a/§1b's R column is a measurement, not a design figure.
5. **Re-run §1a and §1b with measured figures** in place of R's design ones; the floor's margin on the flight changes sign if drawing stays near 35.
