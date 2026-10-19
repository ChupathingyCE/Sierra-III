# The next levers, ranked by payout (2026-10-17, after b137)

Sheepdog01, 08:34–08:42: rank the next levers by payout, batching and pipelining included; re-examine what was shelved; find new ones; assign only what pays. Read by a research delegate of the master session from `optimization_ranking.md`, `big_levers.md`, `shelf.md`, `recon_134.md`, `frames_in_flight.md`, `plan_memory_stalls.md`, the lab's findings and b136's and b137's PSP-1000 ladders. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or docs, [U] inference.

## Where a frame goes (b136's ladder, PSP-1000, lab 39019552090) [M]

The heartbeat's `budget:` line (`psp_platform.c` ~2668): clock = ticks + drawing + rest, in series on the main CPU. ms a frame, one run a map:

| map | clock | ticks (a frame) | drawing | GE layer CPU | engine walk | GE busy | CPU waits for GE |
|---|---|---|---|---|---|---|---|
| a10 | 51.0 | 8.0 (1.42) | 30.8 | 18.0 | 12.8 | 16.0 | 0.95 |
| a30 | 125.0 | 19.0 (2.00) | 100.3 | 56.0 | 44.3 | 62.8 | 0.33 |
| a50 | 89.2 | 42.1 (1.97) | 42.7 | 20.0 | 22.7 | 24.6 | 0.78 |
| b30 | 222.1 | 101.0 (2.00) | 113.6 | 57.8 | 55.8 | 71.2 | 0.37 |
| c10 | 111.8 | 41.0 (2.00) | 66.0 | 27.1 | 38.9 | 39.4 | 0.48 |
| c20 | 105.7 | 44.4 (1.96) | 54.8 | 32.6 | 22.2 | 34.0 | 0.21 |
| d20 | 130.8 | 51.2 (2.00) | 71.2 | 43.2 | 28.0 | 29.9 | 0.41 |
| d40 | 45.2 | 4.4 (1.35) | 30.8 | 18.0 | 12.8 | 21.0 | 4.40 |

- **The CPU almost never waits for the GE** (0.2–1.5 ms a frame); the GE is idle 50–77% of the clock. The frame is the main CPU's, in series.
- **The GE's reads from main RAM cost the CPU about one for one** [M, with caveats]: b137's first ladder (39034486402) had every model in main RAM by accident (`vrammodels600 left off`); ticks didn't move, but drawing rose by about the GE's rise (b30 +56.9 ms against +46, a30 +50.4 against +50, c10 +28.3 against +25), and b30's engine walk, which never touches VRAM, rose 31 ms. A miss costs 424 ns instead of 216 while the GE reads (`plan_memory_stalls.md`). One run a map, and b137 has other changes. **So taking the GE's bytes off the bus is a CPU lever.**

## The ranking

**The line:** worth a session's time if ≥ ~5% of a heavy map's frame or tick on the PSP-1000 (b30: ~11 ms a frame or ~2.5 ms a tick; c20, d20: ~5–6.5 ms a frame or ~1.1 ms a tick), or if it unlocks a bigger lever.

| # | Lever | Worth | Evidence | Cost / risk | Session |
|---|---|---|---|---|---|
| 1 | **The GE's bytes off main RAM:** levelvram (in b137), then the next places by `gereads` (a30 still reads ~327 KB of models a frame from RAM; textures in RAM) | 5–10 ms a frame on c20, d20, a30 [U] | PSP-3000 c20 −7%, d20 −5 to −7.6% [M]; the 1:1 tax above [M] | Exact; VRAM room is the limit | renderer |
| 2 | **Render-only tick work once a frame** (new; generalises LD1): lights re-linked once a frame, node matrices computed only when read | lights ~1.3 ms a tick on b30; matrices up to ~2 ms a tick on c20, d20 [U] | `object_connect_lights` 2.57 ms a tick on b30 [M]; 24–45% of nodes not read before the next compute [E, b132] | Play-exact only if lights are flushed before units update; a build switch for the accessor hooks | recon |
| 3 | **The engine walk's pass table** (measure first) | the walk is 12–56 ms a frame (b30 55.8, 25% of the clock) [M]; the lever table barely touches it | the budget table | none: an instrument | recon |
| 4 | **Frames in flight (L5)** (unpaused, Sheepdog01 08:54) | up to the whole main-CPU replay, 25–30% of the frame (c20 34 of 115 ms), if the ME's replay beside the ticks slows neither side [U] | mekinds [M, both PSPs]: the ME takes 0.45–0.92× the main CPU's time a record (replay a frame: b30 main 72.4 / ME 57.6 ms, c20 36.0 / 29.6); F1's 2.5–3.2× was the bus under overlap, not the ME (39047999180, 39048028093). Next: the rate when they overlap (`inflight`) | one frame more input lag (Sheepdog01's call); +64–128 KB against 84 KB of headroom | recon |
| 5 | **P1.2 leaf passes** | node matrices 1.39 ms a tick plus ~1.1 ms of small passes: ~5% of b30's tick [E] | p11b136a/b | exact | recon |
| 6 | **Change colour baked into the texture** (TB), with the empty glow passes (CP) | GE 25–28 ms a frame on b30's ride and landing [M], and CPU through the tax | 32 triples cover 93–100% on five maps [E] | exact; 350–900 KB of cache | renderer |
| 7 | **Lights merged per surface** (new as a lever) | d20 ~5–10 ms a frame [U] | d20's light pass 22.5 ms a frame [M]; 45–51% of its triangles redrawn by another light [E] | same look, not bit-exact (clamping) | renderer |
| 8 | **The Pelican's near layer** (PN) | 12.3 ms a frame on b30's cutscene and ride [M, PSP-3000] | `recon_134.md` | same look | renderer |
| 9 | **Vehicles' update split per pass**, then a pose cache for parked ones | ~3 ms a vehicle a tick [M]; 136 k instructions, ~1,240 D-misses an update [E] | p11b136types | exact for the parked cache | recon |

**In b138:** LD1 (attached lights linked once a frame; play-exact on a50, d20, c20, b30's parting is the emulator's sound clock [E]; gain [U] ~1.3 ms a tick on b30 until its PSP A/B, `- | nolightsframe`); DRAWCOLD (the debug vertex call off the part loop; 318 I-misses and 4.6 k instructions a tick on b30 [E]); `hotwords` (three hot statics in one cache line, ~860 D-misses a tick [E], ~0.18 ms [U]); BONUS (deterministic `location.bonus`; a fix, not a speed lever). b138's release check reports each one's misses removed at the final code (cache model, keepsets), the lesson of X8 and `nwbatch`. **LF** (light fixtures sleep, `fixturesleep`): play-exact, no gain on the emulator (Damnation 0.33 against 0.32 ms a tick [E]); a Damnation PSP A/B decides, b139.

**Ride-alongs** (below the line): TP (0.9–1.2 ms a tick, in b138); WS, the sleepers off the object walk (0.3–1.8 ms a tick [U], with #2 and #5); the scratchpad shared in time (15.1 of 16 KB is draw-only; the ticks use none); `vfpuskin` on a30 only; CP's empty glow passes (~0.45 ms).

## Batching

- **Draw and state batching are used up:** `statememo` no change on the PSP [M]; `gedefer` no gain on the 1000 [M]; decal merge 0 joinable [E]; `listcachedall` hung the GE.
- **Triangle runs paid a little and shipped** (NC, NR, SU; NR c20 drawing −1.4% [M]).
- **Tick batching:** `nwbatch` b30 −4.5 to −11% but c20 +5%, c10 +34% on the PSP-3000 [M] (each object's data arrived cold); shelved.
- **ME jobs:** the pose job −2.0% on b30's flight [M, 1000]; `meraysbatch` slower.
- **I/O paid best:** the log batched (b30's opening frame −11% [M]; start-up 7.4 → 1.8 s [M]); checkpoints in 4 KB pieces 3.1 → 0.18 s.
- **Where it can still pay:** the leaf passes (#5), the light merge (#7), render-only tick work (#2), and S1 (one thread owning the stick: frametrace shows up to 74 ms of stick reads in a frame).

## Pipelining

- **The CPU and GE already overlap** (the GE draws behind the list as it fills): overlapping more gains ≤ 1.5 ms. The GE costs the CPU through the bus, not through waits.
- **Moving the GE layer to the ME** is the big pipelining lever (#4), but on the PSP-3000 the ME took ~192 ms for what the main CPU does in ~60, so the CPU waited. Leads [D]: the replay skips the main CPU's memos (`psp_ge.c` ~12431–12469, ~7659); no VFPU on the ME (~9643); the ME shares the bus with a tick that misses heavily. Recording runs in series with the ME (two record buffers would fix it).
- **The ME is idle ~94%** without render jobs. Node matrices or biped updates there have ceilings of −5.4 and −2.4 ms a tick on b30 [E], and the ME's own rate is 0.45–0.92× the main CPU's [M, mekinds]; what slows ME work is the bus when both run at once (F1's 2.5–3.2×), so the cost to measure is the overlap.

## Shelved: retry or leave

| Item | Verdict | Why |
|---|---|---|
| `nwbatch` | retry narrowly | WT, b132's order and per-build profiles changed the loop; as the node-matrix pass of #2 or #5, on the PSP-1000 (measured on the 3000) |
| X8 (lights re-linked only when moved) | retry as LD1 | rejected on instruction counts; the cost is misses |
| L5 frames in flight | retry: lever #4 | the ME's rate is measured on both PSPs (0.45–0.92× [M]); the overlap's cost is next |
| `vfpuskin` | retry on a30 only | flat on b30 [M]; a30's corner loop is 21.9 ms a frame |
| L1 `gemeoverlap`, `mewake`, `mefinishslow`, `statememo`, `listcachedall`, DB, DC, LB, code-layout ideas, `sceneryhalf`, `machinesfar`, `posehalf`, `posingmemo` | leave | measured flat or worse, or superseded |
| `PSP_PGO_PARTIAL=0` | leave until the layout work | doesn't link (branch reach) |
| `geoasync` | only inside S1 | one reader of the stick |
| Vita `HALO_SKY_LATE` (proposal, recon 10-19) | leave until #4 | exact, but it saves only GE fill. The GE isn't the limit (CPU waits 0.3–0.4 ms a frame on a30/b30/d20 [M]), and our sky is already drawn at the far plane without depth (`research_vita.md` §12) |
| Vita `HALO_DECAL_STACK_CAP` (proposal, recon 10-19) | shelf, Sheepdog01's call | detracts (old marks under a pile go); GE fill only |

## Order

- **b138 (exact, pre-approved):** #1 confirmed on the PSP-1000 (b137's ladder rerun); LD1 (lights once a frame); the first leaf pass (#5); TP and WS riding along. Measured beside it: the engine-walk pass table (#3), `gereads` by place (#1's next candidates), `objtrace` on the node matrices (#2), the vehicles' split (#9).
- **b139:** lazy node matrices (#2), TB's cache (#6), the parked-vehicle cache (#9); the light merge (#7) and the near layer (#8) as same-look changes shown by side-by-sides.
- **Sheepdog01's A/B:** frames in flight (input lag) once the ME's rate makes it a win; `aihalf`; the light merge if its clamping shows.
