# Discussion, 2026-10-14 afternoon: findings and decisions

Sheepdog01 and Claude, while Sheepdog01 ran b122's tests. It covers the research applied, how the PSP's hardware is used, where the port stands against the frame-rate goals, the rebuild approach, multiplayer, the bake, stutter measurement and the frame-rate fallbacks. Labels: [M] measured on a PSP (the rig is the PSP-3000), [E] emulator or estimate, [D] read in code or docs, [U] inference.

## Decisions (Sheepdog01)

1. **Campaign first.** No multiplayer-specific work until the campaign holds a steady 30 ticks a second; multiplayer's 4-5 builds so far were proofs of concept and network tests. Shared code must still compile into the multiplayer build and stay compatible with the servers (CLAUDE.md, "Campaign first" and "Don't break multiplayer"). This puts the 2026-10-14 internet-play decision (`psp_browser_design_input.md`) on hold.
2. **The frame-rate ladder.** The tick stays at 30 a second: every timer counts in thirtieths, and multiplayer's clock must match the servers.
   - **Target:** 30 ticks and 30 fps.
   - **Conditional fallback:** 30 ticks and 20 fps, only with a cheap interpolation that passes Sheepdog01's feel test. Without it, 20 fps judders (1, 2, 1, 2 ticks a frame), and Halo's own interpolation cost ~14 ms a frame and rubber-banded on b87 [M].
   - **Floor:** 30 ticks and 15 fps, locked (2 ticks every frame, even steps). `vblankthrottle`, the default since b90, already locks to it.
3. **Rebuild around the PSP, not rewrite.** The decomp stays; how much is rebuilt depends on the layer.
   - **Drawing:** free to rebuild, as long as the picture matches.
   - **Render content:** rebuilt at bake time. Anything not lossless needs an emulator side-by-side.
   - **Simulation:** rebuilt only exactly: same answers, different layout, the Media Engine, reuse.
   - **No new engine:** months of work, multiplayer compatibility lost, and the exactness proof with it.
4. **The bake becomes a speed tool**, in this order:
   1. Bounding spheres per model so unseen objects are culled before posing (RD), and pixel-aware LOD tables.
   2. Ready-made display lists for model parts (X2).
   3. Static scenery merged into the level, once multiplayer's object slots are checked.
   4. The collision tree's layout (X9), alongside ME phase 2.

## Findings

### Research applied

Full list in `research_application.md`. In b123 on three branches (`mehyg123`, `flags123`, `lititems123`):
- audio served between ME ray jobs;
- one cache hand-over per batch of rays, and ranges instead of whole-cache flushes for audio;
- the output-range fix in `psp_me_push`;
- the PGO hot-set threshold and loop-duplication flags;
- the AI profile meter skipped;
- lit ground items allowed to sleep.

After b123: the GE hand-over by ownership (before ME phase 2), then character collision on the ME.

### Rig results today [M, PSP-3000, b122, 2 runs a side]

- **c40, b122's speed-ups on against off** (lab 38611163488):
  - In the opening cinematic, no difference.
  - In play they help: the bunker's tick is 0.4–0.6 ms shorter and its frame 1.5–2.7 ms.
  - So the ladder's cutscene creep against b121 isn't the rays or posing.
  - The ladders, opening by opening (`mehyg123`'s review): the audio waits, the ring waits and the mixer lock are flat. What moved is `objects_update`: +0.7 to +1.8 ms a frame.
  - a10 and a30 handed no rays to the ME and still crept. The likeliest cause is the code layout from the two builds' different PGO profiles [U].
  - The test that names it: b121 and b122 built with one profile, A/B'd on a10.
- **b30, the S9 sound bake against the disc's sounds** (38616912734 against 38616947512):
  - The disc's sounds are slower: +5.2% frame in the cinematic, +1.6% in the flight, +3.9% in the landing.
  - Sound reads from the stick: 36.7 MB against 11.6 MB by game time ~2,450.
  - **S9 keeps its speed gain.** Its fidelity costs are handled by the audio plan (`audio_fidelity.md`).
- **b30, the scratchpad on against off** (38618547090):
  - **`noscratch` is slightly faster**: the flight's frame −1.4% and tick −2.2%, both real; the cinematic in the noise.
  - The scratchpad's case was the PSP-1000's halo_bench v9. Either the 3000 differs or the pieces chosen don't pay [U].
  - **The scratchpad swap (tag globals, renderer globals, BSP top) is on hold** until a scratchpad latency bench on the 3000, and a `noscratch` run on Sheepdog01's 1000, settle it.
- **Pelican cut buffer:** "cut pieces lost" reaches ~60,000 by game time ~2,450 on b30 [M]. That backs `cutroom` as the lead for the missing Pelican wall (Sheepdog01's test 7).

### The hardware, by hand

- **No hardware coherence on the PSP.** The Xbox's x86 cache snoops the bus, so stale copies never happen. On the PSP, nothing keeps the caches of the main CPU, the Media Engine and the GE in step. So we write back and drop by hand at hand-over points, which is cheaper than snooping when done once per hand-over, and costly when done per job (the b123 fixes).
- **No look-ahead either.** The Allegrex runs in order and stalls on every miss (halo_bench v7 [M]).
  - **`cache 30` (the prefetch):** it hides 80–90% of a miss, but only if the ~70 cycles after it touch no memory (v7b/v7c [M]).
  - **Placement is by measurement:** the emulator models the prefetch's fill, and a window finder ranks where it fits (agent branch `pf123`, `docs/prefetch_map.md`).
  - **Gain:** about 0.3–1.2 ms a tick [U] (ceiling 0.6–2.4 [E]).
- **DMA can't write the scratchpad.** It reports success but no data arrives [M, v6], and psdevwiki says the scratchpad is off the system bus [D]. It wouldn't be a big lever even if it worked: DMA running beside the CPU makes the CPU's misses 2.3× slower [M].

### Where we stand (latest PSP-1000 figures are b120 [M]; b122's come from today's tests)

- **The floor:** a10, b40 and d40 meet 15 fps at full speed; c40 is at 13.7 fps.
- **Behind the floor:** a50, c10, c20, d20 and a30 sit at 7–9 fps. b30 runs 6.3 fps in the cinematic and 4.2 in the flight.
- **Below 15 fps every frame carries 2 ticks**, so a millisecond off the tick is two off the frame. Above 15 fps they count equally.
- **What holds it back, biggest first:**
  1. Everything runs in a line on one CPU, while the GE is busy 25–30% of the clock and the ME 8–15%.
  2. Memory waits, two-thirds of them instruction fetches.
  3. b30's tick at 53–63 ms, against a 33 ms wall for full speed.
  4. Drawing's CPU side: 49–112 ms a frame.
- **Offloading has a balance point.** A tick lasts as long as the slower of its CPU and ME shares, and a frame as long as the slowest of CPU, ME and GE. Two CPUs can at most halve the work. b30's flight needs ~3,570 ms of CPU work a second against the 2,000 two CPUs supply, so the work itself must shrink too.
- **The GE looks idle but is near its limit.** Its own time is 79 ms a frame in the flight and 46 on the beach (b113 [M]). Once the CPU work fits, the flight hits the GE. VRAM residency is its lever.
- **Odds [U]:**

  | Goal | Odds, exact only | With the full rebuild |
  |---|---|---|
  | Floor on the lighter maps | ~70% | ~70% |
  | Floor everywhere | 25–35% (~50% with Sheepdog01's behaviour calls) | 45–55% |
  | 30 fps on average | ~10% | 20–25% |
  | A solid 30 everywhere | <5% | <5% |

  What would move the odds most: L1 (the ME building the GE's lists) paying off on hardware once the hand-over flush is fixed.

### Multiplayer and the rebuild (recorded for when multiplayer work resumes)

- **The netcode is host-authoritative and not lockstep** (`multiplayer_plan.md`).
  - The host decides damage, deaths, spawns and scores, names objects by slot index and tag index, and corrects clients.
  - Clients predict their own player, report their hits, and draw everything themselves.
- **What a render rebuild must keep:** render models and drawing are free to rebuild in multiplayer too. But:
  - the tag indices must stay the disc's;
  - objects may not be removed in a way that shifts slot numbers [U: check before any scenery merge];
  - collision must give identical answers, because the host checks hit reports against its own.
- **Dead reckoning** (far remote players smoothed between host updates) is a possible multiplayer-only saving: a feel change, so Sheepdog01's A/B.

### Stutter: what we measure, and what's missing

- **Logged today [D]:**
  - the 5-second heartbeat: ticks a second, average and worst tick and frame, fps;
  - how many refreshes each frame stays on screen;
  - ticks per frame;
  - `iostats`' worst read per stream;
  - the profile windows.
- **Missing:**
  - a per-frame record;
  - the 1% and 0.1% lows, and the frame-to-frame variation;
  - the cause of each slow frame.

  Averages can't tell steady overruns from rare hitches, and A/B reports compare only averages.
- **Sheepdog01's campaign Blood Gulch at 26–29 ticks a second, never a steady 30.** Ticks are lost only on frames over 66.7 ms. Suspects [U]:
  - steady overruns just over 66.7 ms;
  - hitches, such as geometry reads from the stick (100–240 ms, b86 [M]);
  - the lock counting time in screen refreshes: a frame of 5 refreshes (83 ms) advances the game only 4 refreshes' worth.
- **Planned for b123: `frametrace`.**
  - **In the game:** a per-frame ring buffer (the frame's time, its ticks, game time lost, its slowest step, any stick wait), ~8 bytes a frame. Measurement only.
  - **`tools/frametrace.py`:** the timeline, the lows, and each hitch with its cause.
  - **`abreport`:** the lows reported beside the averages.

### Frame lock refinements (for b123's pacing work)

- Hold a rate for a few seconds before stepping up, and step down only after repeated misses. b87's remaining bounce came exactly when the lock changed rate [M].
- Wait by sleeping instead of spinning.

## Open and next

- Sheepdog01's b122 tests: the first PSP-1000 figures since b120; `gebusy` gives the GE's own time per stretch.
- The agents' branches for b123: `mehyg123`, `flags123`, `lititems123`, `pf123`.
- RD's first step, an emulator count: how much posing goes to objects that end up off screen.
- A scratchpad latency bench on the 3000, and `noscratch` on Sheepdog01's 1000.
- The remaining deep dives: software caching and staleness; the drawing side.
