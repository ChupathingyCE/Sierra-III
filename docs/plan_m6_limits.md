# M6: the limits per map, and the levers re-ranked by them

The measurement round's last step (`plan_measurement_round.md`, branch `perfplan`). For b30, a30 and a10: what the frame would cost with memory waits gone, with drawing gone, with the tick gone, and with the work split across the main CPU and the ME, against 15/15 (≤ 66.7 ms a frame) and 30/30 (≤ 33.3 ms). Then every lever, shelved ones included, ranked by what those limits say can reach each map's target.

Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code or data, [U] inference or arithmetic. Every limit below is [U]: arithmetic on [M] inputs.

## The inputs

- **The frame at one tick a frame** (both targets run one tick a frame): tick + drawing + rest, from M1's clean replays on the PSP-1000 (b30 game time 90-450 s, a30 40-388 s, a10 229-780 s) [M]. The PSP-3000's figures are within 3% on every line [M].
- **Memory waits:** each phase's share of cycles spent waiting on memory (`hwprofile`, M3's stretches): tick 47-55%, drawing 44-55%, the rest 41-46% [M]. Memory waits are 44-51% of the whole frame on all three maps.
- **The ME beside the main CPU** (ME programme stage 0a, both PSPs): the ME busy on main RAM slows the main CPU's tick 4.5% (work like the main CPU's own) to 10-11% (scattered misses); on its own eDRAM 1-3%. The ME itself runs 11-15% slower while the main CPU works [M]. The ME runs the GE layer's records in 0.45-0.92× the main CPU's time [M], builds GE lists 1.25× [M]. It is 9-14% busy today (sound, slant sort, rays) [M].
- **The GE's own time** (`gebusy`, the same M1 runs): busy a frame, mean / 90th percentile of the logged windows: b30 23.6 / 36.6 ms, a30 39.0 / 69.3, a10 24.5 / 33.2 [M]. The CPU waits on it only 1.3-1.8 ms a frame today [M], so it isn't a limit yet; at 30/30 it is.
- **Split arithmetic:** the ideal, perfectly balanced split costs 0.54-0.63 of the one-CPU frame (main +4.5 to +11%, ME +11 to +15% and ×1 to ×1.25 its rate). The split the code is built for (`gemeoverlap`, frames in flight): the main CPU keeps the tick, the engine walk and the rest; the ME takes the GE layer's records.
- The cache model reads 0.62-0.84 of the PSP's time by phase (M3) [E vs M]: a lever's model-only saving in ms is roughly ×1.2-1.6 on the PSP [U].

## b30 (beach, play)

Now, one tick a frame: tick 32.4 + drawing 49.2 (engine walk 30.4, GE layer 18.7) + rest 7.5 = **89.1 ms** [M].

| Limit | ms a frame | 15/15 | 30/30 |
|---|---|---|---|
| Now | 89.1 | 1.34× over | 2.67× over |
| Memory waits removed | 45.2 | met | 1.36× |
| Drawing removed (tick + rest) | 39.9 | met | 1.20× |
| Tick removed (drawing + rest) | 56.7 | met | 1.70× |
| Ideal balanced split | 48-56 | met | 1.44-1.67× |
| Split as built for (GE layer on the ME) | main 73.5-78.0 (ME 9-20) | 1.10-1.17× | 2.2-2.3× |
| The GE alone | 23.6 mean, 36.6 p90 | met | met on the mean, p90 1.10× |

**What it says:** 15/15 needs 22 ms off one CPU (−25%), or the split plus 7-10 ms off the main CPU. 30/30 is out of reach of every single limit: even the perfect split needs the work cut 30-40%, and the split as built leaves the main CPU at 2.2× [U]. b30's plan is 15/15.

## a30 (cliffs, play)

Now: tick 25.7 + drawing 50.3 (walk 25.3, GE layer 24.6) + rest 7.5 = **83.5 ms** [M].

| Limit | ms a frame | 15/15 | 30/30 |
|---|---|---|---|
| Now | 83.5 | 1.25× | 2.51× |
| Memory waits removed | 40.7 | met | 1.22× |
| Drawing removed | 33.2 | met | at the line |
| Tick removed | 57.8 | met | 1.73× |
| Ideal balanced split | 45-52 | met | 1.35-1.57× |
| Split as built for | main 61.1-64.9 (ME 12-26) | met | 1.84-1.95× |
| The GE alone | 39.0 mean, 69.3 p90 | met on the mean, p90 1.04× | **1.17×; p90 2.1×** |

**What it says:** the split as built for reaches 15/15 by itself, if its bus cost stays at stage 0a's figures; as built it didn't (the GE drawing out of main RAM during the tick slowed the tick 73%, lever L5) [M]. 30/30 is walled by the GE first: its own time is over budget on the mean, so a30's 30/30 plan starts with the GE's bytes and passes, not the CPU [U].

## a10 (Pillar of Autumn, play)

Now: tick 6.8 + drawing 54.9 (walk 23.2, GE layer 31.6) + rest 5.6 = **67.3 ms** [M].

| Limit | ms a frame | 15/15 | 30/30 |
|---|---|---|---|
| Now | 67.3 | at the line (1.01×) | 2.02× |
| Memory waits removed | 37.4 | met | 1.12× |
| Drawing removed | 12.4 | met | met |
| Tick removed | 60.5 | met | 1.82× |
| Ideal balanced split | 36-42 | met | 1.09-1.26× |
| Split as built for | main 37.2-39.5 (ME 16-33) | met | 1.12-1.19× |
| The GE alone | 24.5 mean, 33.2 p90 | met | met (p90 at the line) |

**What it says:** a10 is the one map where 30/30 is in sight: the split as built for, plus 4-6 ms off the main CPU's drawing (the level pass is 21.1 ms a frame [M]). The tick can't help: removing it entirely leaves a10 1.8× over 30/30 [U].

## Across the three

1. **Drawing alone is over 30/30's budget on every map** (49-55 ms on one CPU) [M]. No set of drawing cuts on one CPU reaches 30/30; the GE layer has to move to the ME, and that only pays once the GE stops drawing out of main RAM during the tick (L5's finding).
2. **Memory waits are the largest limit** (44-51% of every frame) but the levers in hand reach a few percent of it: the misses are capacity, not conflict (the order file: −3.2% measured against an −18% bound) [E]. Only data-layout work (fewer bytes touched) can move this limit far, and none of it is built [U].
3. **The GE becomes a wall at 30/30** on a30 (39 ms) and in b30's and a10's worst windows [M]. It is not a limit at 15/15 anywhere.
4. **15/15 on all three is reachable** [U]: a10 now, a30 with the split, b30 with the split and ~10 ms off its tick (or ~22 ms of cuts without it).

## The levers, ranked against these limits

Reach is what the lever can move of the map's binding limit. Rows in the baseline (shipped in b139 or earlier) aren't re-ranked. Dropped for a map = it can't move that map's limit.

| # | Lever | b30 | a30 | a10 | Reach and gate |
|---|---|---|---|---|---|
| 1 | **The GE's bytes off main RAM** (L2b and lever #1: levels, models, hot textures into VRAM) | keep | **first** | keep | The gate for #2 (the GE's main-RAM drawing is what slowed the tick 73% under overlap) and a30's GE wall (39 ms). c20 −7%, d20 −5 to −7.6% [M] |
| 2 | **The split: GE layer on the ME, frames in flight** (L1, L5) | keep | **first** | **first** | The only lever that reaches a30's 15/15 alone and brings a10 within 4-6 ms of 30/30; b30 −11 to −16 ms. Slower as built (+3 to +25%) until #1 lands [M]; one frame of input lag (a look-and-feel call, A/B first) |
| 3 | **Tick work on the ME** (NW node matrices whole subtrees, then BW; MP) | keep | keep | drop | b30: model ceiling −5.4 ms a tick (≈ −7 on the PSP [U]); MP −1.15 ms in b30's flight, 0 on the beach [M]. Gets b30 from "split + 10 ms" toward 15/15 |
| 4 | **The engine walk's and level pass's CPU** (LM lights merged, LFL1 one bucket pass, SHQ shadow receivers, SM, SR) | keep (small) | keep | **first** | a10: level pass 21.1 ms a frame, the 4-6 ms it needs for 30/30 after #2 [M]; shadows up to 5 ms on a10 windows [M] |
| 5 | **GL1 + GL2** (geometry cache lookup, no Xbox constants) | keep | keep | keep | Drawing −2.4 to −6.1 ms on b30's cinematic/flight/landing, beach in the noise [M, PSP-3000]; in w140, not in M1's baseline |
| 6 | **Node and posing tick cuts** (NB, BP, TP, WS) | keep | keep | drop | NB b30 tick −4.5 to −6.7% [M] (not exact as first built); BP est −3.3% [E]; TP −2% ceiling [M] |
| 7 | **AI** (L3 AI on the ME; B1/RA far actors at half rate, deferred; RV3; RH) | keep | keep | drop | actors and AI 10.4 ms a tick on b30 [M, instrumented]: the second tick group. L3 adds to #3's ME share |
| 8 | **Vehicles** (VH1 then VH2) | keep (driving) | keep (driving) | drop | warthog moving 7.6 ms an update on b30, 4.3 on a30 [M]: only where vehicles move |
| 9 | **Model draws on a30** (rasterizer_model_draw 15.1 ms; scenery 14.5 ms a frame drawing, tree_pine 6.4) | small | keep | drop | [M, instrumented]; no lever row yet: the next renderer question for a30 |
| 10 | **GE passes** (TB baked change colour, CP empty glow masks) | keep (ride, landing) | keep | drop | GE −25-28 ms on b30's ride/landing [M], beach 8.5; moves the GE wall, not the CPU's. TB blocked on memory |
| 11 | **Memory waits by layout and prefetch** (NPF, TYP scoring now; T2, TG, PX) | small | small | small | the largest limit, the smallest reach: capacity misses [E]. Keep only what the scoring shows |
| 12 | **Memory waits by data layout** (TO, RB, SC1, RT1, RT2, RV8) | keep | keep | drop | the only route to a large share of #11's limit; unmeasured, research [U] |
| 13 | **Pelican near layer** (PN) | cutscene only | drop | drop | 12.3 ms on b30's ride [M]; outside the play stretch. Hangs on the plan's open question: do slow cutscenes count? |

**Dropped from every map's plan:**
- rejected or parked, unchanged by M6: HG, FH, CI1, CI2, RL1, LB, DC, AI, DB, WC, X8, LPH, WM;
- too small to move any limit: NP, OB1, NMS, UI, STK, SPX, THR, HUDX, LODX, AP, GL3, GL4, LR, PX (unless the scoring says otherwise), EM, MB, RR1, RR3, X2 (retired);
- not on these maps: MS (machines: c20, d20);
- not exact yet: PT (ME points), parked until its cause is found;
- in the baseline already: PQ, IS, AS1, FW1, LD1, WT, R1, SB, X9, X10, RV1, RV2, RV4, SL, NC, SU, NR, SS, T1, LK.

The rest of `levers.tsv` (sound, memory, checkpoints, the stick, correctness, instruments) isn't a speed lever and isn't ranked.

## Gaps this leaves

- **The thread split on the PSP** (M4): `hwthreads` read garbage on hardware; fixed on this branch (`review_metrics.md`), one short run pending. It would say how much of the rest and the tick is other threads.
- **The split's bus cost under real load:** the split rows use stage 0a's synthetic loads. The real figure needs #1 first, then an overlap run.
- **The model ran with sound off the ME** (`nomeaudio`): its absolute ms are low by more than the model's error alone on b30 (sound is 24% of its fetches there) [U].
- **The instrumented runs** (b30 PSP-1000, a30 PSP-3000) cost 15% of the frame; their pass and tick-step shares are used here, never their absolute times.
