# The redesign plan: the frame budget, the parts, and how they're built

**From 2026-11-11 the rebuild's spine is the lab's `research/opportunities/REBUILD.md`** (Sheepdog01 09:37, 09:40): the frame budgeted forward from zero as boxes, each priced alone on the PSP before integration. This page stays as the frame-budget arithmetic and the parts' history; R, S, B and A map onto the boxes there (its §3.3).

Sheepdog01, 2026-11-05 15:41-16:24 (the bottleneck session): the redesign had turned back into a build and test of small fixes. "The whole point of the redesign was to attack the time costs directly." Patching over replacing "has killed the project timeline". This page is the plan that replaces the lever order as the redesign's spine. `redesign_tick.md` (T1-T7) and `redesign_recon.md` stay as its inputs. The rules for building it are in this repo's CLAUDE.md and the lab's ("Redesign work").

Revised 2026-11-05 17:05 after a second-opinion review (lab or-job 42123727478, each point checked against the sources): the GE enters the floor; one bare baseline; S's target derived, and shared with R and B; R's saving tied to its frame rate; criteria, owners and a fallback per part.

Labels: [M] measured on the PSP, [E] emulator or estimate, [D] read in code or data, [U] inference. Lab paths are the lab repository's.

## Where it stands, and the two goals (2026-11-09, Sheepdog01 15:37-15:55)

**The goals** (Sierra-III `CLAUDE.md`; this page's §1, §1b):
- **The floor, hard:** 30 ticks a second at 15 fps or more: a frame of 66.7 ms holds two ticks, so a tick may cost ~25.8 ms on b30's flight (today's bare 42.4: **-39%**).
- **The target, what to attack** (Sheepdog01 15:37: "chasing marginal levers to scrape by to 15/30 is the wrong approach when we want 30/30 as the end goal"): 30 fps, one tick a frame, each level's own average: a frame of 33.3 ms, so a tick may cost ~13-17 ms on the heavy stretches and **16.6 on b30's flight (-61%)**; at 30 fps the main CPU, the ME and on the heaviest stretches the GE are all over their second after R (§1b).

**Today, in the units seen on the PSP** (b151's bare bench, PSP-3000, `stretches.tsv`; ms a frame, then fps) [M]:

| Stretch | Frame | fps | Ticks a frame |
|---|---:|---:|---:|
| b30 Pelican flight | 127 | 7.9 | 2 |
| b30 landing | 115 | 8.7 | 2 |
| b30 beach fight | 96 | 10.4 | 2 |
| d20 flashlight on | 91 | 11.0 | 2 |
| d20 swamp walk | 85 | 11.8 | 2 |
| b30 opening | 85 | 11.8 | 2 |
| c20 after the opening | 61 | 16.4 | 2 |

The tock since b151 is faster on b30 (the RH5 bundle: -5 to -13% [M]), and the worst moments by hand are 70-200 ms a frame. **So the floor is ~1.3-1.9x away on the heavy stretches, the target ~2.6-3.8x.**

**How it's attacked** (Sheepdog01 15:55: "compromises are a no go. we have to find ways to either be more efficient, or leverage other hardware we haven't tapped into"): structural parts, not levers; efficiency and untapped hardware first; a rule in the way goes to Sheepdog01 as a question; compromises are found and priced, never built.

**Where each part's live detail is, and its state:**
- **The tick (S and beyond)**: `redesign_tick_target.md` (the ceilings against the 50-60% cut, the tiny-object stand-in §3.5, SSPR's design §3.6) and `redesign_s.md`; the **tick30** session owns it from 2026-11-09 (the lab's `coord/handoff/tick30_2026-11-09.md`). S4's far tier is a flight-and-landing lever (30% / 13% of biped time); the in-fight bipeds are the beach's cost.
- **R (drawing and the ME's replay)**: `redesign_r.md`, `redesign_r_slice5.md`. Slices 1-4 and 5's step 3 and 5a.1-5a.2 proven exact in the emulator (a kept key's parts drawn from what was kept, records word-identical); the milestone pair is on the PSP (lab sierra-hw 42706253796, 2026-11-09).
- **B (the memory map)**: the lab's `redesign.tsv`; its VRAM gains are spent, what's left is fewer bytes in R's forms.
- **The GE's vertices**: SSPR step 1 built (`sspr_bake.md`); the tiny-object stand-in designed, not built (`redesign_tick_target.md` §3.5, ~13-14 ms a frame of GE on the flight [U]).

## 0. What the plan delivered at the floor (2026-11-05)

- **The floor first: real time (30 ticks a second, at least 15 fps) on every benched stretch** (the goals, `redesign_tick.md`). The target (30 fps per level) follows on the lighter levels; on the heaviest it isn't promised (§5).
- **Viability, on b151's bare bench** (2026-11-05 21:40; §1) [U on M]:
  - **Most stretches reach the floor with R and S:** b30's opening and beach, c20, d20's walk and flashlight (with R's light pass), b40's opening, and b30's landing (its tick needs -27%, and its bodies are a quarter of its biped time, which S1 sleeps exactly).
  - **b30's flight is the hardest:** its bare tick is 42.4 ms (-39% to 25.8), and S's pieces reach ~23.6-29.8 (§3 S): the floor needs the upper half of their ranges. **S's milestone pair (2026-11-06, sierra-hw 42169096020) found no gain from S1 and S2 on the flight** [M, one run]: without them S reaches ~26.7-31.5, the floor only with S3, S4 in its ME form and the toll at their tops, so the margin there grows (`redesign_s.md` §5, "What the milestone says"). R takes ~29-31 ms of the ME's ~34 needed from the models alone; the level's share is unverified (`redesign_r.md` §2.6).
  - **b40's snowfield** needs -31% on the tick; its far, out-of-the-fight bipeds are a third of its biped time, so S4 is worth most there. Its timed run (recon) prices it.
  - **So the margin sits on b30's flight, and on b40's snowfield** (S sums to ~7.8-11.5 of its -11.8 there before R's toll, S2 shelved, the top only with S4's ME form; `redesign_s.md` §2). Sheepdog01's ruling 3 covers it: "plays the same" on the moving bipeds first (the in-fight 20-40 band is the next pool after S4), the compromise freeze only if that's hopeless.

## 1. The frame budget

Every unit has 1000 ms a second. One second of real time at 15 fps asks each unit for:
- **main CPU:** 30 ticks + 15 × (drawing, other and frame end, working);
- **ME:** 15 × the replay + audio at its real-time rate + 30 × (the AI job and ring jobs a tick);
- **GE:** 15 × its own work (busy), with the time it sits stalled for the ME's next chunk shown apart (that's the ME's lateness, not the GE's work).

**Where the formula's assumptions fail** [U]:
- The work a frame is held as measured, at today's 7-15 fps. At higher frame rates less changes between frames (R's saving grows, §3), and contention is held as measured.
- The tick's cost is taken as independent of the frame rate. That's true of the game's work, not of contention: the toll depends on what the GE reads beside the ticks.

**The baseline: b151's bare bench** (ledger1, PSP-3000, the figures runs without instruments; the lab's findings @f076f1770, stretches.tsv; read 2026-11-05 21:35). It replaces ledger 3, which carried the instruments (~7% on the tick) and predated SKINW. The GE's split is scaled from the instrumented runs [U on M].

| At 15 fps, ms a second | 30 ticks | One tick | Main CPU | ME | GE work | GE stalled |
|---|---:|---:|---:|---:|---:|---:|
| b30 opening | 970 | 32.4 | 1258 | 743 | 368 | 100 |
| **b30 flight** | **1270** | **42.4** | **1880** | **1511** | 898 | 171 |
| **b30 landing** | **1059** | **35.3** | **1698** | **1387** | 844 | 153 |
| b30 beach | 1005 | 33.5 | 1425 | 874 | 357 | 42 |
| c20 | 579 | 19.3 | 901 | 788 | 420 | 141 |
| d20 swamp walk | 922 | 30.7 | 1325 | 851 | 284 | 144 |
| d20 flashlight on | 615 | 20.5 | 1095 | **1414** | 345 | 796 |
| b40 opening (route-played; not comparable with ledger 3) | 581 | 19.4 | 952 | 876 | 330 | 206 |
| **b40 cliff path** (re-run 42156503649, 18.7 bipeds a tick) | **1076** | **35.9** | **1551** | 1045 | 537 | 95 |
| **b40 snowfield** | **1127** | **37.6** | **1547** | 676 | 396 | 3 |

What it says:
- **The ticks alone are over the second on b30's flight (1270) and landing (1059), b40's snowfield (1127) and b40's cliff (1076)**, and at the line on b30's beach (1005).
- **What one tick may cost:** with drawing, other and frame end at ~15 ms a frame, a tick has 25.8 ms. Today's bare ticks need: **b30's flight -39%** (42.4), the snowfield -31% (37.6), the cliff -28% (35.9), the landing -27% (35.3), the beach -23% (33.5), b30's opening -20%, d20's walk -16%.
- **Drawing, other and frame end** run 19-43 ms a frame on the heavy stretches (the flight 40.6, the landing 42.6): they have to come to ~15, and 'other' alone is ~5 on b30's flight and ~6.4 on b40's snowfield (recon's splits, rows other_split_*: per-frame particles and effects lead, then the game loop, tags, sound, input), so drawing to ~10.
- **The ME is over on b30's flight (1511, the replay must lose ~34 ms a frame), the landing (1387, ~26) and d20's flashlight (1414, ~28; its replay is 78.4 ms, the main CPU asleep 18.9 ms in drawing waiting for it).**
- **The GE's own work fits the floor everywhere** (at most 898 ms a second, the flight). d20's flashlight is the ME's lateness (796 stalled), not the GE's work. At the target (30 fps) the GE is ~1.8× on the flight: fewer vertices (C6).

### 1a. Both units together (2026-11-06 04:20, bottleneck) [U on M]

S's tick sum counted AITICKOLD (S3) and S4's ME form as cuts to the main CPU's tick. They are, but the same work lands on the ME, so they are **placements, not reductions**. The floor needs both units at or under 1000 ms a second, so the test that decides viability is **the two units' total against 2,000**, then the balance between them. On b151's bare bench, with R at its design figures (the ME's replay ×0.57, i.e. b30's flight's −30 of 69 ms a frame; the main CPU's drawing to ~10 ms a frame; 'other' and frame end as they are):

| Stretch | Main CPU, ME today | After R | Total after R | Against 2,000 |
|---|---|---|---:|---:|
| b30 flight | 1910, 1511 | 1522, 1064 | 2587 | **+587** |
| b30 landing | 1719, 1387 | 1302, 962 | 2264 | **+264** |
| b40 cliff | 1574, 1045 | 1328, 768 | 2095 | **+95** |
| d20 flashlight | 1376, 1414 | 1139, 908 | 2046 | **+46** |
| b30 beach | 1440, 874 | 1263, 710 | 1973 | −27 |
| d20 walk | 1360, 851 | 1222, 663 | 1884 | −116 |
| b40 snowfield | 1579, 676 | 1378, 507 | 1885 | −115 |
| b30 opening | 1269, 743 | 1166, 576 | 1741 | −259 |
| b40 opening | 988, 876 | 836, 642 | 1478 | −522 |
| c20 | 966, 788 | 876, 524 | 1400 | −600 |

What it says:
- **Where the total fits** (the beach, d20's walk, the snowfield, the openings, c20), the floor is a matter of balance: S3 and S4's ME form move ~4.5 ms a tick each (~135 ms a second each) from the main CPU to the ME, which is exactly what those stretches need. The snowfield: 1378 + 507 → about 1110 + 775 with both, then the toll and T5/T7 finish it.
- **Where it doesn't** (b30's flight, its landing, b40's cliff, d20's flashlight), only work that's removed counts: S4's half-rate form (~70 ms a second on the flight), the GE toll R and B cut (~60-120), T5 and T7 (~15-30), part A (~60-70, the ME), MECODE (~30-50, the ME), and 'other' (75 ms a second on the flight, owned by no part yet). Together ~235-340 of the flight's 587 [U]: **the flight stays ~250-350 ms a second over** with every piece at its ceiling, the landing about level, the cliff and the flashlight under. That's ruling 3's margin, sized: the in-fight bipeds ("plays the same") first, then compromises.
- **So S's pieces divide in two:** reductions (S4 half rate, T5/T7, the toll) help everywhere; placements (S3, S4 on the ME) help only where the ME has room after R, and on the flight it doesn't (1064 after R). There S4 takes its half-rate form, and S3 waits for the ME room part A and MECODE make.
- **'Other', sized from recon's splits** (other_split_b30_flight, _b40_snowfield [M]): ~5.0-5.3 ms a frame on the flight, ~6.4 on the snowfield, 12-18% of it in the kernel, the rest spread thin: particles and effects 24-27% (particles_update, bsp3d_test_point, point physics: ~1.2-1.7 ms a frame), the game loop and state ~11, input and aim 6-8, collision 5-8, sound 6-8, tags 5-9, AI ~4. Only the particles are a chunk, and moving their point physics to the ME is B1's "points' remainder", a placement. Removable work there is ~20-40 ms a second [U]: it doesn't change the flight's margin.

### 1b. At the target: 30 fps, each level's own average (2026-11-06 11:35, master) [U on M]

The goal is each level's own average at 30 fps, one frame a tick (Sheepdog01 2026-11-06 11:31: per level, not across the ladder, so a heavy level's stretches must reach 30 themselves; the floor stays hard). The same sums as §1a with the frame terms doubled (main CPU: 30 ticks + 30 × the frame side; ME: 30 × the replay + audio at real time + 30 × AI and ring a tick; GE: 30 × its work), from §1a's after-R totals:

| Stretch | Tick today (ms) | Total after R at 30 fps | Against 2,000 | GE at 30 fps | The tick may cost | Cut |
|---|---:|---:|---:|---:|---:|---:|
| b30 flight | 42.4 | 3429 | **+1429** | **1796** | 16.6 | 61% |
| b30 landing | 35.3 | 3071 | **+1071** | **1688** | 17.1 | 51% |
| b40 cliff | 35.9 | 2716 | **+716** | **1074** | 16.5 | 54% |
| d20 flashlight | 20.5 | 3241 | **+1241** | 690 | (the ME first: its main CPU waits on the replay) | |
| b30 beach | 33.5 | 2448 | +448 | 714 | 16.1 | 52% |
| d20 walk | 30.7 | 2434 | +434 | 568 | 13.3 | 57% |
| b40 snowfield | 37.6 | 2359 | +359 | 792 | 16.6 | 56% |
| b30 opening | 32.4 | 2159 | +159 | 736 | 20.3 | 37% |
| b40 opening | 19.4 | 2042 | +42 | 660 | 16.3 | 16% |
| c20 | 19.3 | 2047 | +47 | 840 | 13.5 | 30% |

**The GE column, checked on the PSP** (the design review's G6, lab sierra-hw 42736528894, PSP-1000, the tock without R, `gebusy2` only; findings `ge_busy_plain_g6`) [M]: the GE is busy 48.8 ms a frame on the flight and 53.7 on the landing, so ~1,460-1,610 ms a second at 30 fps. The column's 1,796 (the ledger's scaled share) is near that; the unscaled 83.5 (2,505) is not. The GE stays over its second at 30 fps on both. **R's columns are design figures** (the replay ×0.57, the drawing ~10 ms): R priced whole on the PSP left the ME's replay at ×1.02 and took 1.6-7.3 ms of drawing (`draw_map.md` §2.1).

What it says:
- **Every benched stretch is over at the target, after R.** The plan's parts at their ceilings (§1a: ~235-340 ms a second removed on the flight) close the floor on most stretches, not the target.
- **The tick is the core.** After R a frame's non-tick work on the main CPU is ~16-20 ms, so a tick may cost ~13-17 ms: the heavy stretches' ticks must lose **50-60%** (the flight 42.4 → ~16.6). The tick costs the same a tick at any frame rate, so this figure isn't softened by R's saving growing with the frame rate.
- **The ME is over too** (the flight ~1,650 ms a second): the replay runs twice as often. R's saving should grow at higher frame rates (less changes between frames), so this column is pessimistic.
- **The GE is over on b30's flight and landing and b40's cliff** (1,074-1,796): fewer vertices drawn (C6), not faster CPUs.
- **So the tick side needs a redesign of what the tick does, not levers** (Sheepdog01 11:33: "if it means a fundamental redesign we want to explore it"). The work: size where the flight's and the heavy stretches' tick goes by system (cpu_cost_map.md §4), then design for a ~55-60% cut; candidates to weigh include lower-rate or lower-fidelity simulation away from the player (plays the same), the AI and collision rebuilt around change, and whole tick systems as designed ME jobs (placements help only where the ME has room, which §1b says is little).

## 2. Why the time is what it is

- **The tick** waits on memory for 56-67% of its cycles on every stretch [M, ledger 3]: a PC engine's data, scattered behind pointers, against a 16 KB data cache. On b30's flight: computing 36%, its own misses 47%, and the GE's contention toll 17% (15.1 ms a frame [superseded 2026-11-16: the GE's toll in play, measured: b30 flight's tick 5.8 ms (15%) [M]; hardware_facts.md, Bus (the toll in play)], `frame_timeline.md` lead A [U on E and M]). The flight's tick has never been split by subsystem.
- **The drawing and the replay do the per-part work twice.** The engine's immediate-mode renderer runs on the main CPU and records every part's draw (37 ms a frame on b30's flight). The ME decodes each record and re-derives its GE state (70 ms). Both are limited by instruction misses: the drawing's hot code is ~45 KB and the replay's ~29 KB, against a 16 KB I-cache (`draw_rebuild.md`, cachecensus1 [E]).
- **Most of it repeats.** A model part's GE words are 97-98% unchanged frame to frame apart from its matrices, and 544 of 552 model draws a frame on b30 match last frame's (`pdc_plan.md`, pdc1 [E]; the share held on the PSP, pdcprobe [M]). That census ran at 1.1-1.6 ticks a frame; at today's two ticks a frame less repeats.
- **Every part's commands cross main RAM every frame**, ~500 KB a frame of list on b30's flight. The GE reads them beside the tick: ~~the list's own bytes are 4.4 ms of b30's toll~~ (broken, corrected 2026-11-16: a list word costs 10.4 ns, so the list is ~1.1 ms of bus time; the lab's `tick_buyback.md` §7).

## 3. The parts

Each part: what it changes, its ceiling, its milestone (when the PSP prices it), its stop rule, its fallback, and who does it (proposed; Sheepdog01 assigns).

### S. The tick, by how often its work changes

Today every object's work runs every tick. The redesigned tick computes what's read, when it's read, and keeps results until their inputs change. T3, T5, T6 and T7 are its pieces.

- **Target, derived (§1):** a bare tick of ≤ 25.8 ms with drawing at ~15 ms a frame. That is b30's flight −36%, b40's snowfield −35%, b30's landing −30%, b40's cliff −28%; the lighter stretches −18 to −22%.
- **The target is shared.** Part of the tick is the GE's toll, which R and B shrink (the list's own bytes; vertices and indices moved to VRAM where room is freed). What's left after them is S's.
- **What's already in the measured figures:** VISLAZY and aicad3 are on by default (the lab's `levers.tsv`), so their gains are in ledger 3.
- **What's known on top, on b30's flight** [U on M]:
  - AITICKOLD, the AI's think on the ME from a world one tick older: tick −14.5% [M, PSP-3000 sierra-hw 42041663929]. It needs ME room, which R frees.
  - The toll R and B can remove: ~2-4 ms a tick.
  - T5 and T7: ~0.5-1.
  - Together: 40.6 → ~30-32 ms against ≤ 25.8. **A gap of ~4-6 ms a tick has no known source.** C3 has to find it.
- **Where the tick goes (C3a, 2026-11-05 17:46, recon)** [M]. b30's flight on tock162 (PSP-3000, the ledger bench's sampler set, sierra-hw 42114568213): collision 25.6%, AI 16.5, node matrices and animation 11.3, units and vehicles 9.3, other objects 7.7, libm and memcpy 5.3, effects 3.4, tags 2.9, audio on the main CPU 2.4, scripts 1.6, the rest 14.5 spread thin. b40 (PSP-1000, b40tick 41979909751): the snowfield collision 26.6%, AI 13.9, node matrices 12.4; the cliff collision 18.6, node matrices 14.2, AI 13.7.
  - **So collision is S's first piece** [U]: a quarter of the heavy ticks, more than AI, and the likeliest home of the missing 4-6 ms. On the flight: sphere tests (the 3-D BSP sphere test 6.0%, the 2-D one 2.5, gathering features in a sphere 2.5), the vector walk, cluster reconnects 2.0.
  - **Exact by choice, not by rule** (Sheepdog01 2026-11-09 17:30/17:52 retired 10-30's "collision is exact in every mode, one path": collision is held to plays-the-same and the host's bounds on hit reports, CLAUDE.md). The change-rate form is exact by construction: a query whose object and world didn't change since the last tick gives the same answer. C3b counts how often that holds.
  - b30's landing has no sampler window yet (the bench stops at game time 2,000). It's left out unless S's design needs it: the flight and both b40 stretches agree.
- **How much of the tick's work repeats (C3b, 2026-11-05 17:52, recon)** [E, sierra-batch 42131939482, fixedticks; each object's play hash compared tick to tick over 48 ticks]:

  | Stretch | Objects a tick | Unchanged since the last tick | Bipeds unchanged |
  |---|---:|---:|---:|
  | b30 flight | 658 | 69.0% | 58.4% (15) |
  | b30 landing | 687 | 73.1% | 65.5% (16) |
  | b40 cliff | 504 | 97.1% | 71.9% (21) |
  | b40 snowfield | 476 | 96.5% | 80.0% (45) |

  - **What it means for S** [U]: on b40 almost nothing but bipeds changes from one tick to the next. On b30 the scenery that changes (36-43%) does so by its animation frame (swaying), not its place. So object updates and node matrices for unchanged objects are repeated work in the shares above, and collision is mostly queries by moving callers (bipeds, projectiles, the Pelican) against unchanged objects and an unchanged level.
- **Collision's repeat share by query (C3b, 2026-11-05 18:30, recon)** [E; rows c3_query_repeats_*]. A query repeats when its caller's inputs and every object it reaches match the last tick:

  | Repeats | Vectors | Features in a sphere | Cluster reconnects |
  |---|---:|---:|---:|
  | b30 flight | 2.7% (of 112k) | 19.0% (of 45k) | 3.5% (of 115k) |
  | b40 snowfield | 6.1% | 16.3% | 4.9% |
  | b40 cliff | 3.1% | 39.4% | 4.8% |

  Bipeds carry nearly all of it (their vectors repeat 30-64%, their feature queries 25-46%); vehicles', projectiles' and callerless queries ~0%.
  - **So the exact change-rate form doesn't close S's gap** [U]. Reuse keyed on unchanged inputs pays on the feature queries (a fifth to two fifths), little on the rest: of collision's ~25% of b30's flight tick, roughly 1 ms a tick, against the 4-6 ms missing. The objects mostly don't change (C3b by object), but the queries are made by the ones that do.
- **The unchanged objects' update time (C3, 2026-11-05 19:18, recon)** [M, PSP-1000, sierra-hw 42141971439, game time 600-1799; row c3_objcost_ms_b30_flight; a census build with a mismatched profile, its tick ~47-60 ms, wall clock per update]. Object updates take 34.9 ms a tick; **6.8 (19%) is in updates that left the object unchanged**: parked or idle vehicles 2.4 (2.1 a tick at ~1.1 ms), the 53 awake scenery 2.3 (the ~220 asleep cost only the check), half the weapons 0.45, the rest small. The moving bipeds hold 22.9 ms (23.6 a tick at ~970 µs each). **Corrected 2026-11-16 (audit, after Qwen4 Max's read):** these ms come from a census build whose tick was ~47-60 ms, not the shipped 39.9 ms; as shares of that tick (object updates 58-74%), bipeds are ~16-21 ms of the shipped tick [U], so a pot sized as a share of 24.3 ms runs 15-70% high.
  - An exact skip has to know beforehand that an update won't change its object, so 6.8 is a ceiling. Scaled to the bare tick (~40 against the run's ~47-60) it's ~5 ms, and a skip that predicts well takes perhaps half to most of it: **~2.5-5 ms a tick** [U].
  - **Where S stands** (b30's flight, b151's bare tick 42.4 ms) [U on M]: less AITICKOLD (-14.5%, ~6.1, needs ME room), the toll R and B remove (~2-4), T5 and T7 (~0.5-1), S2's level half (~0.75-1.1), S1's sleeping bodies (~1-2), S4's far-biped tier (~2.3-4.6): **~23.6-29.8 ms against 25.8**; S1 and S2 measured nothing on the PSP (S2 shelved after its v2 pair, 42179121901), so ~26.7-31.5. The floor's tick on the flight needs the upper half of those ranges (`redesign_s.md` §2).
  - **What's left beyond exact work:** the moving bipeds' 23 ms a tick (~970 µs each). Only "plays the same" work reaches them (the AI already thinks at half rate: aicad3).
- **S's "plays the same" piece, the far-biped tier** (Sheepdog01 19:28, ruling 3; 19:34: agreed). Three forms, each stopped before only because it had to be exact:
  - **far bipeds at half rate:** a biped far from the player and out of combat updated every other tick with twice the time step, as the AI already thinks. If about half the bipeds are far, ~5-6 ms a tick on b30's flight [U], enough for S's margin;
  - **far bipeds on the ME, a tick behind** (BW; the AITICKOLD pattern), on the room R frees;
  - **the bipeds' update in stages that fit the I-cache** (PHASEB: its revive_if was "Sheepdog01's OK for the plays-the-same form"): ~1.1 ms a tick on b30 and ~2.6 on c20 on tock162's LTO build [U on E].
  - Collision runs the same code against the same world in all three (a choice that keeps the proof cheap; since 17:30 not a rule). Proof: side-by-sides and the outcome census. First count: bipeds a tick by distance from the player and by combat state (recon, emulator).
- **Milestone:** priced as two builds on Sheepdog01's pad benches (b30_play2 and c20_play1): old-only (S's words off) against new-only (S's slices on), each with the whole heap (Sheepdog01 2026-11-10 01:16). Its trigger and due point are in the lab's plan (`research/opportunities/PLAN.md`, "Milestones"; Sheepdog01 2026-11-10 10:07): no part goes more than three proven slices or two days without a hardware number.
- **Stop rule:** if C3 shows the pieces can't reach the target exactly or "plays the same", Sheepdog01 decides (§5.1).
- **Fallback:** the old tick path behind S's keep word; it stays the safety net for a failure, per CLAUDE.md's failure rule.
- **Who:** recon (C3); the master builds.

### R. Retained rendering

The GE was built for retained mode: lists that CALL sub-lists, 8 bone matrix and 4 light registers, a stall address. Native PSP games build a part's commands once and write only what changes.

| Changes | What | Built by | Lives in |
|---|---|---|---|
| once a map | each model part's commands per level of detail and shader pass; each level cluster's static geometry | the bake | main RAM, VRAM where room allows |
| when it happens | a texture moved in VRAM: the words that point at it patched | the unit that moves it | the prebuilt lists |
| every frame, per instance | world matrix, bone matrices, lights, tint, then CALLs | a short emit pass | a small frame list |
| every frame, dynamic | particles, decals, contrails, sorted see-through, the HUD, first person | immediate mode, as now | the frame list |

- **The units' jobs:** the main CPU runs the tick and the visibility, and hands over an instance list, not a record of every call. The ME writes the frame list from the instances and makes the dynamic geometry: the replay as a stage goes. The GE draws as now.
- **What it must deliver at the floor (§1):** the ME's replay −33 ms a frame on b30's flight (−38 landing, −25 d20's flashlight); drawing, other and frame end 42.5 → ~15-20 ms on the flight. At the target, the GE's work too: fewer vertices and bytes (C6: the flight's GE work is ~89% vertex-side).
- **What's known of its worth** [U on E and M]: PDC's census puts the models' removable share at 22-30 ms a frame of the replay on b30, measured near one tick a frame. At the floor's two ticks a frame it's smaller (`pdc_plan.md`: "the saving today is smaller than at the target. It grows as the frame rate rises"). The level's per-cluster lists add to it, unsized. **So R as known meets the floor's ME need on d20's flashlight and falls ~5-15 ms short on b30's flight and landing.** C4 and C2 size the rest.
- **R on b30's flight and landing (C4, 2026-11-05 17:54, recon)** [M, PSP-3000, sierra-hw 42132063553, bare mekinds, tock162 88d3e33c; rows c4_replay_by_kind_b30_*]. The flight's replay is 67.9 ms a frame: model part draws 37.8 (277 a frame, 56%), audio and the ring served inside it 12.6, level draws 6.8, see-through 3.0 + 0.65, screen quads 1.5, model begin 1.0, multipurpose 0.9, shadows 0.8. No light pass, decals under 0.5. The landing's is 77.4: models 37.6, decals 6.8.
  - **What R takes** [U]: the models' part draws down to their per-instance words (matrices, bones, lights, the CALL; ~40 of a part's words are state that repeats, C4's word count): 37.8 → ~8-12 ms, about -26 to -30. The level's draws down to ranges indexed in place: 6.8 → ~2, about -5. Together **~-31 to -35 ms a frame against the floor's need of -33 on the flight**: met, just, **if** the level's ~5 holds. The code review of R's page (2026-11-05) found the level's draw is rebuilt per frame and split by slope buckets, so its saving is unverified and probably smaller (`redesign_r.md` §2.6): from the models alone (~29-31 ms, measured by step) R is ~2-4 ms short on the flight until slice 6 sizes the level. On the landing (need -38) it's ~3-7 short, the decals' 6.8 being dynamic.
  - Audio and the ring (12.6) stay on the ME whatever R does.
- **The light pass is R's too (C4 on d20, 2026-11-05 18:08, recon)** [M, PSP-3000, sierra-hw 42132069463, bare mekinds]. With the flashlight on, the replay is mostly the world's lights: light draws 28.2 and 47.9 ms of 49.3 and 71.8 a frame (57-67%), 94-108 draws a frame at 260-511 µs each, the worst single record 11-15 ms. The models are only 3.0-3.6 ms, the level 6.4-7.7, decals 1-2. Before the flashlight: 32.5 ms in all, models 8.1, lights 1.3.
  - So on d20's heavy view R's ceiling is the light pass, not the model parts. Each light redraws the surfaces it reaches, with a projected cone texture for a spotlight (`psp_ge.c`), and its per-draw work (each vertex's reach, the near cut, the bands) is immediate mode, redone every frame.
  - **Its pieces under R, already sized and never started** (the lab's levers.tsv): **LMC**, a static light's reach and lit-triangle list kept across frames while the light doesn't move (d20's machine lights); and **LM**, one draw with up to four GE lights in place of one draw per light. The flashlight moves every frame, so LMC doesn't take it: its form (the cone kept as a projected texture, or the GE's own spotlight, which is a look question) is for R's design page.
  - **The flashlight's form, as built and as R could have it** [D, `psp_ge.c`, tock162]. The PSP doesn't use the GE's own spotlights (no `GU_SPOTLIGHT` anywhere; models get directional and point lights). The flashlight is drawn as a pass of its own, its cone a 32×32 texture projected from the light so the cone's edge is per pixel (build 36: per-vertex cones showed as lit triangles). Its **gel, the pattern the Xbox projects, is only sampled at each lit vertex** (GELC's 16-a-side cube), so on large level triangles the pattern blurs away. Working out each vertex's reach and gel on the ME, and copying the vertices with those colours, is the light pass's cost: the 191 KB a frame of lit copies (C2) and most of the 28-48 ms (C4).
    - **Option 1, the GE's spotlight in the surface's own pass:** no pass, no copies, no ME work, but per vertex: the lit triangles of b35 come back and there's still no pattern.
    - **Option 2, the gel projected as the pass's texture** [U]: the cone texture replaced by the cone times the gel's face the light points through, and the falloff with it, projected by the GE's texture matrix from the vertices' positions (`sceGuTexMapMode` with the texture matrix), so the ME computes no per-vertex light. That gives the pattern per pixel, and the 191 KB of copies and most of the ME's light work go. Its cost is the GE's one texture a pass: the surface's own texture ~~still can't be multiplied in, as today~~ **can't be multiplied in, so option 2 gives up B7 (today the pool is the surface's own texture times the per-corner light, `light_spot_base()`, on by default) and the surface comes in as its average colour, the old `lightconetex` look. The 5650 frame buffer has no destination alpha to keep both (corrected 2026-11-10, research; the lab's `research/opportunities/light_pass.md`: option 3 there keeps both in a buffer of its own, ~278 KB of VRAM).** That's lever PROJLIGHT (catalogued as a compromise, 2026-11-04); with the pattern restored, the look question is whether it reads closer to the Xbox than today's, which is Sheepdog01's side-by-side.
    - R's design page settles which, after an emulator side-by-side against the PC reference.
- **R's dynamic floor (C2, 2026-11-05 18:14, recon)** [E, sierra-batch 42134306096, Sierra-III recon/c2listsplit de95a013; rows c2_inline_split]. The frame list's inline data by writer, KB a frame:

  | | b30 flight | d20 flashlight | b40 snowfield |
  |---|---:|---:|---:|
  | inline data in all | 214.9 | 256.4 | 116.2 |
  | kept index copies (models + level) | 62.7 + 28.1 | 2.1 + 22.0 | 3.0 + 13.8 |
  | planar fog's per-vertex colour | 45.2 | 0 | 36.2 |
  | the light pass's lit copies | 1.4 | **191.3** | 0.5 |
  | see-through, particles, decals, HUD | 32.3 + 22.7 + 11.3 + 6.9 | 11.4 + 8.6 + 8.0 + 5.4 | 5.2 + 47.3 + 6.1 + 2.7 |
  | near-plane cut pieces | ~1-6 | ~1-6 | ~1-6 |

  - **The cutting is small** (C5's worry): 1-6 KB a frame.
  - **The index copies go with R:** a part CALLed whole, or a level range indexed in place, needs no copy. That's 91 KB a frame on b30's flight.
  - **Fog and lights are per frame as built,** unless they move to the GE's own fog and to option 2 above: 45 KB of fog on b30's flight and 36 on b40's snowfield, and the light pass's 191 KB on d20's flashlight.
  - **The truly dynamic floor** (see-through, particles, decals, HUD) is ~73 KB a frame on b30's flight, ~33 on d20 and ~61 on b40, against today's 768 KB list.
- **Why the earlier tries didn't pay** (the lab's `levers.tsv` and `grounds.tsv`, rows PDC, PDCCALL, X2):
  - PDC copied prebuilt words into the old per-frame list through the old layers, bit for bit, from an eDRAM store the GE can't read. Its pairs ran even to +1.6 ms a frame.
  - PDCCALL CALLed retained blocks, but kept the key, the hash, the replay and the built parts. It measured +1.2-2.3% frame on c10 [M, PSP-1000 sierra-hw 41216890230], and its store didn't fit b30's heap.
  - Neither is R: R is bake-built per level of detail and pass, with no per-frame record or replay. They showed that half a retained mode costs about what it saves, not that retained mode loses.
- **Fewer vertices at bake time** (SSPR; step 1, rigid models, built and in the benches' bake w162s: `sspr_bake.md`, b30 48 models with baked levels; lodcensus1 [E]: 66% of b30 flight's model vertices belong to models with no lower level of their own, which is why it's needed): each lower level of detail simplified to what can be seen at the distance it's drawn. C6 found the GE ~89% vertex-bound on b30's flight, so this is the target's GE lever; R's bake already rebuilds every part, so it goes there.
- **What could sink it:**
  - the shader forms a map needs (C1);
  - the store's room, which has to be GE-readable: main RAM or VRAM, not eDRAM. The room is the frame list it replaces, 768 KB of list plus the record buffer, 128 KB by default and 224 with the default keep set [D, `psp_ge.c` DEFER_INFLIGHT_KB; IFKB];
  - the dynamic floor (C2);
- **The words a form takes (C4, 2026-11-05 18:30, recon)** [M, PSP-3000, b30's flight, words only: the probes inflate the times]: a model part's bind, state and lighting is ~40 words (~160 bytes; 272-282 a frame), a geometry draw call ~12.7 words, the near-plane filter 5.9-6.6k words a frame, two-sided passes ~3k, the level's passes 16.9-20.2k words in 13-15 calls; the whole list 173-211 KB a frame. So a prebuilt form is ~40 words plus ~13 a draw call, nearer 200-300 bytes than C1's 560, and the store below roughly halves [U].
- **C1, the store (2026-11-05 17:53, the master; bake w162s, sierra-formcensus 42132247128 and 42132545265)** [D for the counts, U for the bytes at 560 a form]:

  | | Forms, every level of detail | Store | Super high alone |
  |---|---:|---:|---:|
  | b30 | 1,462 | 799 KB | 652 / 356 KB |
  | c10 | 1,679 | 918 KB | 800 / 437 KB |
  | the ten maps | 667-1,679 | 364-918 KB | 407-800 / 222-437 KB |

  - **It fits the room R frees** [U]. Super high is drawn only for objects taller than the screen (the cinematics' close-ups, `memory_ledger.md`), so a store of the other four levels of detail, ~250-480 KB on most maps, fits inside today's 768 KB frame list, which R cuts to tens of KB. Super high can stay immediate, or stream in for a cinematic. The 560 bytes a form and the passes per feature are estimates, so the master's next read of psp_ge.c's model passes reprices it.
  - **The level needs no store of its own** [U]: its triangles are 0.35-2.1 MB of 16-bit indices a map (a10 365k triangles, 2.1 MB), too much to copy, but they're already in main RAM as the BSP's surfaces. A bake that orders each material's surfaces by cluster makes each (cluster, material) a contiguous range the GE can index in place. That's for R's design page to settle.
  - near the camera, geometry still has to be cut each frame (C5: the GE can't draw it uncut), so what's near the eye stays per-frame work.
- **Milestone:** two builds on Sheepdog01's pad benches (b30_play2 and c20_play1): old-only (R off) against new-only (R with its proven slices, R top-down's included), each with the whole heap (Sheepdog01 2026-11-10 01:16); its trigger and due point are in the lab's plan (`research/opportunities/PLAN.md`, "Milestones"). **Switch-over:** every kind drawn by R; the same two builds again.
- **Stop rule:** if C1 shows the store can't fit, or C4 that R's ME saving can't reach the floor's need with S and B, Sheepdog01 decides before any build.
- **Fallback:** immediate mode stays in the build for what R can't hold: parts whose forms didn't fit the store, and any failure. The old record and replay go at the switch-over; immediate drawing doesn't.
- **Who:** the bottleneck session (C1, C2, C5); the master (C4, the build).

### B. The memory map by design (with R)

R decides what must be GE-readable, and S's layout what the tick reads hot. B places them: VRAM, main RAM and the ME's eDRAM by design, not job by job. T4 joins it.

- **Known:** VRAM has 13-48 KB free behind 816 KB of fixed buffers. Freed VRAM is worth ~17.6 µs of b30's tick a frame a KB for the first 200 KB (lead A [U on M]). The eDRAM's 544 KB is the largest free block, readable by the ME only.
- **The scratchpad by design** (Sheepdog01 19:34). 16 KB, always a hit, off the bus, reachable by the main CPU and the GE (cpu_cost_map.md §2). Today: the level collision BSP's top 304 nodes (SCRATCHBSP, a small win [M]) and the profiler's slots and table (6.6 KB; moving them out gained nothing, PROFHEAP). It was last set by misses per byte in the old shape (build 108, then SCRATCHBSP). In the new shape the candidates change: S's change-rate keys (each object's version, the feature queries' reuse table), the far-biped tier's per-pass arrays, the hot tag fields (the tag window is 32% of the tick's data misses, TAGHOT), and R's per-frame instance list on the main CPU side. B re-ranks them by misses per byte on the new shape's cache map once S's and R's design pages fix their tables, and the winner is priced by a PSP pair (the cache rule).
- **Promoted to a design-first part beside R** (Sheepdog01's go, 2026-11-07; the review and the sizing are the lab's `docs/research/design_review_2026-11-07.md` and `gap_sizing_2026-11-07.md`). Contention is the measured limit: the tick waits on memory 56-68% of its cycles [M]; with the GE reading VRAM only, the wait fell 56% → 40% and a D-miss's cost 358 → 192 cycles (b102 [M]); M6 #2's replay costs the tick ~2 ms with the miss count flat (TICKI [M]). There's no bus-priority setting (§9.1 closed), so placement is the only lever. B's scope is three things:
  - **The GE's traffic onto VRAM:** vertices and indices (up to ~8 ms of b30's flight). The display list stays in main RAM, because the ME writes it and can't see VRAM. Gated on freeing VRAM (13-48 KB free today). First step: one PSP pair moving vertices and indices to VRAM (vrammodels' room), checked against lead A's ~17.6 µs a KB.
  - **The hot tag window as a packed, typed layout,** read through accessors: 32% of the tick's D-misses, half of them in a 16 KB hot set. TAGHOT's retrofit is blocked by raw-offset readers; B lays the hot fields out once instead. Exact; ~2-4 ms a tick [U], priced by a PSP pair (the cache rule). First step: the readers' clustering audit (are the hot fields read in a few functions or across the tick).
  - **Freeing VRAM,** which the first of these needs.
- **Not B's:** the tick restructured around the I-cache (82-84% of its I-misses are capacity) needs WORLDVIEW, a tick-start view of the world that changes play. It's Sheepdog01's call as a compromise, ~1.5 ms a tick ceiling [U].
- **Milestone and stop:** priced at B's own milestone (two builds, old-only and new-only, on the pad benches; Sheepdog01 2026-11-10 01:16) or inside R's and S's, not piece by piece; the trigger goes in the lab's plan ("Milestones") when B has its next go.
- **Who:** the master.

### A. Audio built around the VME (candidate, Sheepdog01 19:34)

- **Today's audio was priced as it stands, on the ME's CPU** [M]: ~1.7 ms + ~0.12 ms a voice per 23 ms block on b30 (VMEMIX's pair), ~13-20% of the ME's second on the heavy stretches (b30's beach: 20.2 ms a frame at 10 fps), and it scales with real time, not frames.
- **The VME bolted onto today's mix lost:** VMEMIX was exact in game and 22 µs a voice slower, because feeding the VME cost more than its compute saved. Its study (`vme_capability.md`) found the mix's tail fits (gain, accumulate), but not the interpolation at the pitch step (a gather) or the one-pole lowpass (recursive), and not the ADPCM decode (sequential, table-driven).
- **What a built-for-the-VME audio path would change** [U]: voices kept resident in the VME across blocks; the interpolation and lowpass reshaped so they need no gather and no recursion (a sound question for the audiocheck meter, a "plays the same" change); the decode done ahead in bulk, or sounds baked to a form the VME reads. Its ceiling is unsized.
- **The census** (recon, 2026-11-05, PSP-3000 sierra-hw 42152480586, audiokinds; row audio_by_stage_b30) [M]: every heard voice is mono ADPCM. Per voice-frame: plain 73 cycles (decode 28 + mix 45), pitched 87, low-passed 85, both ~98. Over b30's run: **decode ~32%, the mix's multiply-accumulate ~53%, pitch interpolation ~13%, the low-pass ~2%**, plus ~81-85 µs a block zeroing and writing out. Pitched voices carry 63% of the mix time.
- **A's shape, from it** [U]: the ME's CPU keeps what the VME can't do and does it in one pass: decode each voice and step its pitch (the gather) straight into a PCM buffer in eDRAM. The DMAC moves those buffers into the VME beside the CPU (2.96 cycles a word from eDRAM, ~0 of the CPU), where every voice's gain and accumulate run as one resident pass a block, double-buffered. The low-pass (2%) stays on the CPU or becomes a short FIR (a sound question for the audiocheck meter). VMEMIX lost because the CPU fed the VME word by word per voice; here the CPU writes the decoded samples once, where it writes them today anyway, and the DMAC does the moving.
- **Its ceiling** [U]: the mix's ~53% less its write-out, so at most about half of audio's ME time: on b30's flight ~17.9 ms a frame of audio, so ~6-9 ms a frame at today's 7.5 fps, ~60-70 ms of the ME's second (~6-7%). That matters for the target more than the floor: at 30 fps audio stays per real second while the frame halves, so its share of a frame doubles.
- **Open for Sheepdog01** (he's away; no build until he says): whether A becomes a part. Its first step would be a bench on one PSP, the DMAC feed and a resident VME accumulate of 12 voices a block, before any game code.

### The old parts

T1 and T2 (work onto the ME) wait for the ME room R frees. T4 is B's. T3, T5, T6 and T7 are S's pieces. The lever list goes on beside the plan (§6), not on its parts' paths.

## 4. The counts

Sent to the master 2026-11-05 17:20 (the lab's coord/inbox/master.md) for those that need a build; the rest done here. Results go into the lab's findings and this page's §1 and §3.

| | Count | Answers | Where | Who | State |
|---|---|---|---|---|---|
| C1 | Each map's prebuilt forms (parts × levels of detail × shader passes) and their bytes | whether R fits | a counter in the baker, run as a sierra-bake job that prints counts only | this session's counter, run by the master | **done** (§3 R) |
| C2 | The frame list's inline data (b30's flight 254 of 501 KB) by kind: model and level cut pieces, decals, kept index copies, effects, see-through, HUD | R's floor: what stays per frame | emulator, `listcensus` (Sierra-III recon/listbytes2; lab coord/handoff/listbytes_2026-11-05.md) | recon | **done** (§3 R) |
| C3 | The tick by subsystem on the four heavy stretches, and its change rate: the share of each subsystem's per-object work whose inputs didn't change since the last tick | where S's last 4-6 ms a tick comes from | emulator: the PC sampler, `PSP_TICK_STEPS=1`, `nmstudy` extended (lab coord/handoff/nmstudy.md) | recon, through the master | **done** (§3 S): S falls ~3-5 ms a tick short on b30's flight |
| C4 | The replay by record kind, and the models' time by step | R's ceiling on the ME | emulator for the shape; one bare PSP run for the ms (meparts' ME cycle counter) | recon | **done** (§3 R): R meets b30 flight's floor need just, ~3-7 short on the landing |
| C5 | What the GE does at the near plane; how the replay lights a model | two of R's unknowns | code | this session | **done** (below) |
| C6 | The GE's own time on b30's flight and landing by what limits it | whether the GE's work can lose ~15 ms a frame for the same picture | existing measures, then one PSP pair | this session; the pair through the master | **done** (below) |

C3, C4 and C6 decide the heavy stretches; C1 decides whether R is built at all.

**Gaps still open after Gate 1** (asked 2026-11-05 19:45):
- **The bare ledger bench on the current tock:** the frame budget's rows are ledger 3's, instrumented (~7% on the tick) and stale on the GE (master, at the tock's finalization).
- **The main CPU's "other"** (5-9 ms a frame on the heavy stretches) has never been split, and the floor's tick budget assumes drawing and other fit in ~15 ms together (recon, the PC sampler's `pcsampleother`).
- **b40, timed:** the unchanged-object skip and AITICKOLD are priced on b30 only (recon, one bare PSP run of b40's snowfield).
- **Bipeds by distance and combat state** for the far-biped tier (recon, emulator).
- **Audio by stage** for part A (recon, emulator for the shape).

### What the reads found

- **C5, the near plane** [D, `psp_ge.c`'s header, tock162 70bc508]: the GE "drops a whole triangle when a corner falls behind the camera or lands too far off the screen (its coordinates only reach 2048 pixels either side)". So triangles near the eye are checked and cut on the CPU each frame, and the pieces drawn at the frame's end. For R: a prebuilt part can be CALLed only when it's wholly and safely in front; the rest (the ground under the player, models beside the camera) stays per-frame work. That's part of R's dynamic floor, and C2 sizes it.
- **C5, model lighting** [D, `psp_ge.c` model_lighting]: already the GE's own lights. Two directional lights and two point lights, with the ambient, are set per part through `sceGuLight` and `sceGuLightColor` (the light pass of the world's lights is separate). For R that's a few per-instance words before a part's CALL. Lighting is not a blocker.
- **C6, what doesn't limit the GE** [M on E]: moving its reads off main RAM barely changes its own time. mevram's measured rate is 1.63 µs of GE time a KB moved to VRAM, so M6B's 294-551 KB a frame was worth 0.3-0.9 ms of GE time (the lab's levers.tsv, M6B, GEV). On b30's flight (83.5 ms busy) the GE is bound by its own vertex and pixel work, not by memory. Its reads still matter to the tick (the toll), not to itself.
- **C6, vertices or pixels (2026-11-05 18:01, the master)** [M, PSP-1000, sierra-hw 42131785138, gebusy + gebusy2, master/gescissor c7f24063; findings rows c6_ge_fill_split]: with every draw clipped to one pixel, the GE's busy time on b30 went 26.6 -> 18.5 ms a frame on the opening (fill ~30%) and **55.6 -> 49.6 on the flight (fill ~11%)**. So the flight's GE work is **~89% vertex-side**: fetching, transforming, reading main RAM. Cutting it means fewer vertices or bytes (R's prebuilt forms, the levels of detail, B's placement), not less fill. With fill gone the tick fell too (-4.5% opening, -3.5% flight): the fill's texture reads tax the bus [U].
- **C1, the order of magnitude** [E]: one frame's model commands on b30 are ~37,800 words (~150 KB, `pdc_plan.md`'s census), ~560 bytes a hit (pdcprobe's ~140 words). A map's whole store is a multiple of one frame's visible set, by how many parts, levels of detail and passes it holds [U]. The frame list R shrinks is 768 KB. Whether the store fits is C1's question.
- **C4, what's known** [E]: on b30 the models were 61% of the replay's ME time in the older split by kind (mekinds, in `pdc_plan.md`); on c10 the models' steps are bind + state + lighting 38%, geometry 29%, two-sided and glow passes 24%, convert 6.5%, node matrix 3% [M, meparts 40961270594]. On b30's flight that puts the models at ~43 of the replay's 70 ms and the level and the rest at ~27 [U]. R's need there (−33 ms) takes most of the models' removable share plus part of the level's: C4 settles it.

## 5. Decision points for Sheepdog01 (Gate 1, 2026-11-05)

**Sheepdog01's rulings, 2026-11-05 19:28:** (1) R: go. (2) S: go. (3) The margin: "plays the same" work on the moving bipeds first; lifting the compromise freeze only if that's hopeless. So R and S move to Phase 1 (their design pages), and the levers on their grounds now wait for them (the redesign rule), unless one pays before its part lands and Sheepdog01 says so.

1. **R's go:** build retained rendering as a separate path. Its store fits (C1, with C4's words a form), its dynamic floor is small (C2), it meets b30 flight's ME need and carries d20's light pass. It frees the ME room AITICKOLD needs, so S waits on it. The flashlight's form (the GE's spotlight per vertex, or the cone times the gel projected per pixel) is settled on R's design page by a side-by-side against the PC reference (Sheepdog01, 18:15: worth considering if it improves performance for a similar benefit; the gel's pattern is what's missing today).
2. **S's go:** build the tick by change rate with its pieces: AITICKOLD on the room R frees, the unchanged-object skip (exact), collision's reuse on the repeating feature queries (exact), T5 and T7. Its target is a bare tick of 25.8 ms on b30's flight.
3. **The margin on the heaviest stretches:** if R and S land below their ceilings, b30's landing and the b40 stretches stay a few ms over the floor. What may cover it:
   - "plays the same" work on the moving bipeds' 23 ms a tick (the simulation rule of 2026-10-30; collision included since Sheepdog01 2026-11-09 17:30/17:52, and running less often is efficiency when a player can't tell, 18:16): for example, a far biped's update at a lower rate, as the AI already is; or
   - lifting the compromise freeze for those stretches (frozen apart from L15, FX15, RENDERTICK, AIFAR and PRIO60; L15 held in reserve).

   This can wait until R's and S's milestones measure what they really give. Deciding now only sets whether that work is designed beside them.
4. **The target:** 30 fps per level looks reachable with R and S on the lighter levels: a10 and c10 at 20.7 fps today, c20 17.2, c40 27.5 (tock161's ladder, PSP-1000 [M]). It isn't promised on b30, a30 or d20 (×2.2-3.7). Whether to chase it there comes after the floor.

## 6. How it's built

The redesign rule (Sierra-III CLAUDE.md and the lab's, "Redesign work", Sheepdog01 2026-11-05 16:24):
- **Budget first.** A part starts as its ceiling on each unit and stretch against §1's gap, and Sheepdog01's go.
- **A separate path, switched whole.** A part is built beside the old path, not inside it, with its own machinery, under one keep word. The old path stays as the A/B side until the switch-over. A failure fallback stays after it (§3's fallbacks), per CLAUDE.md's failure rule.
- **Proven for correctness, priced once.** Slices are proven in the emulator for correctness: pictures for drawing, play + seeds or the outcome census for the simulation. GE-stream identity with the old path isn't asked of a part that replaces it.
- **The multiplayer boundary is the netcode's** (corrected 2026-11-09, Sheepdog01 17:30; CLAUDE.md). No simulation state has to stay bit-exact for multiplayer: the host bounds-checks the client's movement (3.5 units) and hit reports (3-6 units), and only discrete values (datum indices at map load, the join's version and checksums) must match. A part that changes the simulation is proven by play + seeds when it is exact, otherwise by side-by-sides and the outcome census; multiplayer builds stay left out for now (CLAUDE.md, 2026-10-21).
- **A slower slice is not a rejection.** The PSP prices a part at its milestone and at its switch-over.
- **Levers stay off a part's ground.** Once a part has Sheepdog01's go, a lever on a ground it replaces isn't started (the lab's levercheck names the part), unless it pays before the part lands and Sheepdog01 says so. Before the go, a lever there says how it fits the part. A lever that failed through the old layers is marked revivable under the part (PDC, PDCCALL and X2 are, under R).
- **Progress is the budget.** A milestone updates §1's table. A lever reports its effect in its terms.

**Traps when proving a new path against the old one** (learned on R's slices):
- A form exists, so the ME never hands its part back again (rslice3): compare R's ME counts with an m6off run's before trusting a widening's reach.
- A check that runs the old path after the new one sees the state the new one left (the point-light flag): restore every CPU-side shadow of GE state first.
- A list swapped for a scratch list must be invisible to the display list's machinery (stall updates, the ring's room, the pending write-back act on `gu_list->current`): suspend them for the scratch run (3be5e21).
- A run that never fills a cache proves nothing about its eviction (R's 11 KB set in a 32 KB ring): check the let-go counts are > 0, and cut the store below the set for the proof.

## 7. The road

**Phase 0, the counts (now; no part has Sheepdog01's go).**
- C1-C6 above, in parallel: the master and recon on the builds, this session on the reads.
- PSP time: C4's one bare run and C6's one pair. Nothing else.
- Each count's result goes into the lab's findings and this page's §1 and §3, and the dashboard's Redesign tab follows (`redesign.tsv`).
- **Ends when** C1, C3, C4 and C6 are in.

**Gate 1 (Sheepdog01): the go per part.** Passed 2026-11-05 19:28: R and S have their go (§5).
- For each heavy stretch, the parts' ceilings from Phase 0 against §1's gap.
- Sheepdog01 rules on §5.1 (what fills any gap: "plays the same" or a lifted compromise) and §5.2 (R's go).
- A part without its go stays a plan; its levers carry on, saying how they fit it.

**Phase 1, the design pages.**
- One page per part with its go, in `docs/`: the data it keeps, its interfaces with the old path, its slices in order, each slice's proof, its keep word, its fallback, and its milestone's stretch.
- R's names the instance list's format, the store and its placement (with B), patch-on-change for textures, and the dynamic path (cut pieces, decals, effects).
- S's comes from C3: which subsystems become computed-on-read, and what each keeps.
- Reviewed before building: a second-opinion read of the page against the code, as this page had.

**Phase 2, the build.**
- Each part on its own branch, as a separate path under its keep word. The master builds; recon builds S's census-driven pieces.
- Slices are proven in the emulator for correctness only (§6).
- The milestone's price: two builds (old-only, new-only) on the pad benches, triggered as the lab's plan says (`research/opportunities/PLAN.md`, "Milestones"). A slice is never priced on the PSP by itself.
- **R's one early pair** (Sheepdog01's go, 2026-11-07): after slice 2c's ring, one PSP pair logs what R's drawing in the ME's replay really costs the ME and the tick (the emulator charges the ME's work ~0). It's for information, not a gate, and isn't a frame A/B of R (the old layers are still there). It runs with the sound stream on both sides, which gives R's ~440 KB its heap.
- **LMC under R** (Sheepdog01, 2026-11-07: "sounds like a good idea"): static lights' light-pass work kept across frames goes before R lands only if it pays first, sized in the eDRAM (an `edram.md` row: in main RAM the ME's reads of it would add the contention it removes), and built as the mechanism R adopts for static lights.
- **The stick is scheduled** for every streaming part: fewer, larger reads; the game thread's reads first; no long writes in play.
- After each milestone, §1's table is redone with the measured figures. If a part falls short of its share, Sheepdog01 decides before the next slice.

**Phase 3, the switch-over.**
- One PSP pair per part at the switch-over. The old record and replay go; immediate drawing stays as R's fallback (§3).
- With the ME room R frees, T1's remaining jobs and AITICKOLD move onto it, priced as placements (CLAUDE.md).
- B's layout is set from what R and S hold.
- The ladder and the ledger bench run on the first tick that carries them (the lab's bench rule).

**Beside it all, the lever lane.** Levers go on in the tock as now, off a part's ground once it has its go. They report in §1's terms.

**Who keeps it true.** This page is the redesign's record. It's updated in the same turn as a result that changes it, like the cost map. The lab's `redesign.tsv` and the dashboard follow.

## 8. Why the habits changed

On the dashboard's lever table (2026-11-05): 513 levers, 145 in the build, 93 rejected, 54 shelved; `levers.tsv` changed in 510 commits since the tick-and-tock rule (2026-10-24). The large wins were sized before they were built. PDC's census put the replay's saving at 22-30 ms a frame on b30. Its build, held to the old path's GE stream through the old layers, ran even, and PDCCALL measured +1.2-2.3%. Both were judged slice by slice. The rule above is so that a design whose payoff comes when the old layer goes isn't judged on its first slice.

## 9. The rebuild's rules (moved from CLAUDE.md, 2026-11-18)

The rules sessions follow when building a box (Sheepdog01 2026-11-05, 2026-11-11). CLAUDE.md keeps who calls what; the full rules are here.

The lab's `research/opportunities/REBUILD.md` is the spine: the frame budgeted from zero as **boxes**, built forward from the spec, systematic and complete, not patches over what's built.
- **A box** is one subsystem the game needs every frame, written as the port's own code in `psp/src/` against its own data, with a contract (what it reads, writes, and who reads it when) and a budget on every unit, per entity as well as per tick or frame. Its design page is `docs/box_<name>.md`. It may start from the engine's own code, adapted, with ChupathingyCE named in the file's header, UPSTREAM.md and the README's Credits (Sheepdog01 2026-11-18). The engine's version is switched out whole at the box's entry points when it's proven; the engine checkout stays unedited and `psp_fixups.py` holds only the cut-over.
- **Three means make a box cheaper** than what it replaces: its data owned and packed for its walk; work done on change or on need; both cores on separate data. A change that uses none of them is a lever.
- **Budget first:** a part starts as its ceiling on each unit and stretch against the plan's frame budget (§1), and Sheepdog01's go.
- **Priced in the frame** (Sheepdog01 2026-11-16): a box is priced by its effect in the frame, alone or with a codependent group, and broken down only where a group misses its budget. Its standalone price (over a capture of one of his pad benches, on the PSP, warm and with the caches emptied, the GE reading beside it) is a required sanity check, not a gate.
- **Cut in under its own build switch, off by default, on the lead's call**, with no go from Sheepdog01 (Sheepdog01 2026-11-11). It's proven against the old path on vs off in one build (Sheepdog01 2026-11-16); in two builds (old-only and new-only, each with the whole heap, on the same pad bench) only when both paths don't fit the heap together (Sheepdog01 2026-11-10). The old path goes at the switch-over; the switch meets "every change behind a keep word".
- **Proven for correctness, priced once:** slices are proven in the emulator (pictures for drawing; play + seeds, or the outcome census, for the simulation), never by identity with the old path's GE stream. The multiplayer boundary still holds. A slice slower on its own isn't a rejection: the PSP prices the part at its milestone (the old layer bypassed) and at the switch-over, one pair of builds each.
- **Levers aren't filler** (Sheepdog01 2026-11-14): work on the old path runs on a PSP only as an ask with a question, a reader and the decision it feeds, or under the next rule; an idle PSP with no such ask stays idle. Lever work is tagged `[lever]` in the lab's session files, doesn't set a session's order of work, and isn't reported as rebuild progress.
- **Levers stay off a part's ground:** once a part has Sheepdog01's go, a lever on a ground it replaces isn't started unless it pays before the part lands and he says so (the lab's `tools/levercheck.py` names the part). Before the go, a lever there says how it fits the part.
