# The frame timeline (ledger 3)

One chart per heavy stretch: what each unit (the main CPU, the Media Engine, the GE) does with a frame, how long it waits, and on whom. Ledger 3 of `redesign_recon.md`; the design reads it beside `cpu_cost_map.md` (where the main CPU's time goes) and `capacity_model.md` (what each job costs per entity).

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or docs, [U] inference. Every table figure is [M] unless marked.

## The runs

The tock at 64647bb5 (tock162 with the GE's phase split), strict build with the campaign-w162b profile, layouts w161a_tgc90 and hotg161a, bake w157m. One run a stretch; keep words `nearauto gebusy gebusy2 assettrack geo15 nomouth hwprofile hwthreads frametrace fixedseed` plus each stretch's own. Checked first in the emulator: every word took and printed, and the routes played to their stops (lab sierra-batch 41937820432) [E].

| Map | Run (lab sierra-hw) | PSP | How it played | Stretches read (game time) |
|---|---|---|---|---|
| b30 | 41941407089 | PSP-3000 | route `b30_b139t2_timed`, `exitat150` (ended at 4,498) | opening 150–570, flight 600–2000, landing 2000–2700, beach 2700–3700 |
| c20 | 41942245562 | PSP-1000 | hands off, `exitat120`, `lanetrace phasemark` (ended at 3,474) | 1500–3474 (steady after the opening) |
| d20 | 41941380156 | PSP-3000 | route `d20_b139t3_swamp`, `flashlightat1400`, `exitat90` (ended at 2,685) | swamp walk 300–1350, flashlight on 1500–2685 |
| b40 | 41942251606 | PSP-3000 | hands off, `exitat120`, `lanetrace phasemark` (ended at 3,488) | 1500–3488 |

b40 is the light map: on tock159's ladder it ran 29.7 fps mean and 28.0 at its lowest 5 s, the steadiest of b40, c40, d40 and a10 [M, lab sierra-hw 41821725723], so it shows where a frame's room goes when nothing binds but the display. The two PSPs' figures agree within ~3% a map on every build ladder-run on both (the lab's findings) [M]; each figure here is from the PSP named above.

**How the figures are made.** Each 5 s heartbeat window logs the `budget`, `hardware by phase`, `ME`, `ME: ring`, `gebusy2` and `frameend` lines. A stretch's figure is the windows' mean weighted by their frames; an ME job's figure is its own time times its count, over the stretch's frames. "Asleep" is the main CPU halted inside a phase (the `hardware by phase, of the clock` line). Bars are composition, not order: each row is the frame's length, and its letters show how the unit's time divides. The order inside a frame is in the next section.

## How a frame runs today

lanetrace places the three units on one clock, frame by frame [M for the main CPU's phases, the GE's spans and each job's hand-over and collection; E for where inside its span the ME ran]. c20, frame 831, just after the opening cutscene (lab sierra-hw 41942245562, PSP-1000):

```
frame 831: 66.5 ms, 1 character = 1.01 ms
main CPU |OTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTOOOOOOODDDDDDDDDDDDDDDDO|
ME       |IIRRRRRRRRRRRRRRRRRAAAARRRRRRRRRRRRRRRR...AAAA.....|..............|
GE       |...GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG..G..............|
```

b40, frame 935 (lab sierra-hw 41942251606, PSP-3000):

```
frame 935: 33.9 ms, 1 character = 0.51 ms
main CPU |OOOOOOOOOOOOOOOOOOOOOOOOOOOOTTTTTTTTTTTOOODDDDDDDDDDDDDDDDDDDDDDDO|
ME       |RRAAAAARRRRRRRRRRRRRRRRRRRRRRIRRRRRRRRRRRR..|...AAAAA.............|
GE       |GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG.....G.......GGGGG...GGGG.|
```

Main CPU: T tick, O other, D drawing, E frame end. ME: R the replay of the frame before (its own time placed from its hand-over; `|` where the main CPU collected it), A audio, I the AI job. GE: G busy.

So the frame is a three-stage pipeline [M, the lanes above; D, `psp_ge.c` and `psp_me.c`]:
1. **The main CPU** runs the frame's ticks, then its drawing, which records the frame's GE work rather than building the lists itself.
2. **The ME replays that record** in the render slot, handed over at the frame's end and running beside the next frame's ticks. It writes the GE's lists chunk by chunk, so **the ME is the GE's producer**.
3. **The GE draws each chunk** as it lands, so its busy time falls inside the next frame's ticks.

The waits that join them:
- **The main CPU on the replay:** the next drawing can't start its record until the replay of the last one is collected. When the replay outlasts the ticks and "other", the main CPU sleeps in drawing for the difference.
- **The GE on the ME:** when the ME writes chunks slower than the GE draws them, the GE sits inside an unfinished list (gebusy2's "idle in a list"). This time is outside the GE's busy figure (`gebusy_add` in `psp_ge.c` counts it apart) [D].
- **The main CPU on the GE:** `frameend`'s wait for the GE, inside the drawing phase's sleep (drawing asleep ≈ the replay's wait + this one, within 1 ms on c20 and b30's flight) [M, U for the split].
- **The AI job** (`aime2a`) is handed over during drawing and collected at the next tick's start; the ring jobs (poses ahead, points, rays) are pushed in one phase and collected in the next, with waits under 0.5 ms a frame everywhere below.
- **Audio** runs on the ME for the audio thread, which waits for it; the game's thread doesn't.

## Each unit's frame, by stretch

| ms a frame [M] | b30 opening (150-570) | b30 flight (600-2000) | b30 landing (2000-2700) | b30 beach (2700-3700) | c20 (1500-3474) | d20 swamp walk (300-1350) | d20 flashlight on (1500-2685) | b40 (1500-3488) |
|---|---|---|---|---|---|---|---|---|
| **Frame** | 94.5 (10.6 fps) | 132.8 (7.5 fps) | 130.3 (7.7 fps) | 99.8 (10.0 fps) | 62.7 (15.9 fps) | 96.3 (10.4 fps) | 92.3 (10.8 fps) | 33.6 (29.7 fps) |
| *Main CPU* | | | | | | | | |
| tick (ticks a frame) | 72.1 (2.02) | 87.3 (2.00) | 79.3 (2.01) | 67.9 (2.00) | 35.5 (1.87) | 65.0 (1.92) | 42.2 (1.95) | 11.0 (1.01) |
| drawing, working | 17.9 | 37.1 | 42.9 | 24.3 | 17.2 | 20.1 | 26.6 | 13.4 |
| drawing, asleep (waiting for the replay or the GE) | 1.7 | 2.9 | 2.2 | 1.3 | 3.5 | 1.8 | 17.1 | 0.1 |
| other, working | 2.6 | 5.3 | 5.7 | 6.1 | 6.4 | 9.2 | 6.2 | 4.0 |
| other, asleep | 0.1 | 0.1 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 5.0 |
| frame end | 0.2 | 0.1 | 0.1 | 0.2 | 0.1 | 0.2 | 0.1 | 0.2 |
| tick waiting on memory, % of its cycles | 63% | 64% | 65% | 59% | 67% | 56% | 60% | 62% |
| *Media Engine* | | | | | | | | |
| the replay (render slot) | 19.3 | 69.8 | 73.1 | 27.5 | 39.9 | 24.2 | 76.5 | 17.3 |
| audio | 8.4 | 17.9 | 14.1 | 20.2 | 4.7 | 19.4 | 14.1 | 4.7 |
| ring jobs (poses, points, rays) | 2.1 | 3.9 | 3.2 | 2.2 | 2.6 | 3.0 | 1.4 | 0.7 |
| the AI job, by difference [U] | 18.8 | 17.2 | 21.4 | 13.1 | 3.7 | 19.5 | 3.7 | 2.5 |
| busy, all jobs | 46.5 | 101.9 | 105.6 | 61.1 | 48.0 | 64.2 | 94.2 | 24.6 |
| idle | 48.0 | 30.9 | 24.7 | 38.7 | 14.8 | 32.1 | 0.0 | 9.0 |
| *GE* | | | | | | | | |
| busy in the ticks | 35.8 | 75.6 | 62.9 | 31.8 | 31.8 | 22.9 | 16.9 | 8.9 |
| busy in drawing | 0.1 | 6.1 | 14.9 | 0.0 | 5.6 | 0.8 | 10.0 | 2.6 |
| busy in other and frame end | 0.2 | 1.8 | 2.8 | 0.1 | 4.7 | 0.3 | 1.8 | 5.2 |
| stalled in a list (the ME's next chunk) | 1.9 | 2.6 | 1.3 | 0.9 | 0.5 | 4.9 | 47.2 | 1.0 |
| idle | 56.6 | 46.7 | 48.4 | 66.9 | 20.1 | 67.5 | 16.3 | 16.0 |
| *Waits* | | | | | | | | |
| main CPU on the replay | 0.1 | 0.3 | 0.4 | 0.1 | 2.2 | 0.0 | 18.1 | 0.2 |
| main CPU on ring jobs | 0.41 | 0.17 | 0.11 | 0.17 | 0.03 | 0.21 | 0.22 | 0.01 |
| main CPU on the GE (frameend's wait) | 0.0 | 1.8 | 0.8 | 0.0 | 1.3 | 0.1 | 2.0 | 0.3 |
| the replay's longest job (one job, ms) | 22.8 | 45.8 | 14.9 | 21.7 | 19.6 | 10.6 | 55.7 | 9.5 |

The AI job's row is by difference [U]: the ME's report line is cut at 512 bytes (`platform_log`'s buffer) before its `ai` slot, so its figures never print. The row is the ME's busy time plus the work counted twice in yields, less the replay, audio and ring jobs.

```
b30 opening (150-570): 94.5 ms a frame, 1 character = 1.6 ms
main CPU |TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTDDDDDDDDDDDwOO|
ME       |RRRRRRRRRRRRAAAAAArIIIIIIIIIIII.............................|
GE       |GGGGGGGGGGGGGGGGGGGGGGGs....................................|

b30 flight (600-2000): 132.8 ms a frame, 1 character = 2.2 ms
main CPU |TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTDDDDDDDDDDDDDDDDDwwOO|
ME       |RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRAAAAAAAArIIIIIIII...........|
GE       |GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGggggs.....................|

b30 landing (2000-2700): 130.3 ms a frame, 1 character = 2.2 ms
main CPU |TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTDDDDDDDDDDDDDDDDDDDwOOO|
ME       |RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRAAAAAArrIIIIIIIII.........|
GE       |GGGGGGGGGGGGGGGGGGGGGGGGGGGGGggggggggs......................|

b30 beach (2700-3700): 99.8 ms a frame, 1 character = 1.7 ms
main CPU |TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTDDDDDDDDDDDDDDwOOOO|
ME       |RRRRRRRRRRRRRRRRRAAAAAAAAAAAArIIIIIIII......................|
GE       |GGGGGGGGGGGGGGGGGGGs........................................|

c20 (1500-3474): 62.7 ms a frame, 1 character = 1.0 ms
main CPU |TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTDDDDDDDDDDDDDDDDwwwwOOOOOO|
ME       |RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRAAAAArrIIII...........|
GE       |GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGggggggggggs...................|

d20 swamp walk (300-1350): 96.3 ms a frame, 1 character = 1.6 ms
main CPU |TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTDDDDDDDDDDDDwOOOOOO|
ME       |RRRRRRRRRRRRRRRAAAAAAAAAAAArrIIIIIIIIIIII...................|
GE       |GGGGGGGGGGGGGGgsss..........................................|

d20 flashlight on (1500-2685): 92.3 ms a frame, 1 character = 1.5 ms
main CPU |TTTTTTTTTTTTTTTTTTTTTTTTTTTDDDDDDDDDDDDDDDDDDwwwwwwwwwwwOOOO|
ME       |RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRAAAAAAAAAr|
GE       |GGGGGGGGGGGggggggggssssssssssssssssssssssssssssss...........|

b40 (1500-3488): 33.6 ms a frame, 1 character = 0.6 ms
main CPU |TTTTTTTTTTTTTTTTTTTTDDDDDDDDDDDDDDDDDDDDDDDDOOOOOOOvvvvvvvvv|
ME       |RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRAAAAAAAArIIIII...............|
GE       |GGGGGGGGGGGGGGGGggggggggggggggs.............................|
```

Main CPU: T tick, D drawing working, w drawing asleep (waiting for the replay or the GE), E frame end, O other working, v other asleep. ME: R replay, A audio, r ring jobs, I the AI job [U]. GE: G busy inside the ticks, g busy in drawing or other, s stalled inside a list waiting for the ME's next chunk; `.` idle.

## Who binds each stretch

| Stretch | Binds | Why [M] |
|---|---|---|
| b30, all four | **The main CPU's ticks** | The tick takes 68–87 ms of a 94–133 ms frame (two ticks a frame) and waits on memory for 59–65% of its cycles. The replay ends inside the ticks: the main CPU waits 0.1–0.4 ms a frame for it. The ME is 77–81% busy on the flight and landing; the GE is busy 63% of the flight's clock, 75.6 ms of it inside the ticks |
| c20 | **The main CPU**, with the ME close | Tick 35.5 + drawing 20.7 of a 62.7 ms frame. The replay (39.9 ms) nearly fills the ticks and "other" (42 ms), so the main CPU already waits 2.2 ms a frame for it. ME 77% busy; GE 67%, 31.8 ms inside the ticks (90% of the tick's time) |
| d20, swamp walk | **The main CPU's ticks** | Tick 65 of a 96 ms frame; the ME 67% busy, the GE 25% |
| d20, flashlight on | **The ME** | The ME is busy for the whole frame (94 ms of 92: replay 76.5, audio 14.1). The main CPU sleeps 17.1 ms a frame in drawing waiting for the replay; the GE stalls 47.2 ms a frame inside lists for the ME's next chunk and is busy only 28.8 |
| b40 | **The display's refresh** | 29.7 fps: tick 11.0, drawing 13.5, and 5.0 ms a frame asleep in "other" (the frame waiting for the refresh [U]) |

The GE binds none of these stretches. Where it does is c10's later stretch, saturated at 94–98% of the clock (`cpu_cost_map.md` §4) [M].

What it says for the design [U]:
- **The replay is a fixed stage between the main CPU and the GE.** On the heavy maps it takes 40–77 ms of the ME a frame (c20, b30's flight, d20's heavy view). It must finish inside the next frame's ticks, or the main CPU waits; and its chunk rate caps the GE. d20's heavy view fails both, which is why the main CPU there spends a fifth of its frame asleep.
- **The GE is busy through most of the tick:** GE busy time overlaps 35–90% of the tick's own time (b30's flight 87%, c20 90%, d20's walk 35%) [M]. Where those reads come from RAM, every tick miss costs more (`cpu_cost_map.md` §2 and §4) [U].
- **The ME's room is uneven:** 25–48 ms a frame idle on b30 and d20's walk, about 15 on c20, none on d20's heavy view. A job moved to the ME has to fit beside the replay where it is longest.
- **b40's ~5 ms a frame of sleep** is the light maps' headroom.

## Gaps this ledger leaves

- **The ME's report line is cut at 512 bytes,** so the AI slot job's own time, count and waits never print (every window of these runs). The fix is the report's, not the AI job's: split the line, or print the slots on lines of their own. Until then the AI job's row stays [U].
- **lanetrace opens a window only at a phase edge** (a map loaded, a cutscene's start or end, a BSP switch; keep word `phasemark`). So its frames come from the start of play, not from the stretches above: c20's window after its cutscene (game time ~1,069, 73.2 ms a frame) and b40's (35.1). A window opened at a game time (a `lanetraceatN`) would place d20's heavy view and c20's steady state frame by frame.
- **One run a stretch.** The figures carry each run's own spread: b30 and d20 by route, c20 and b40 hands off. A pair is for a lever, not for this map.
- **The earlier d20 reading of gebusy2, corrected:** sierra-hw 41893041680's "GE busy 41 ms a frame, 23–24 ms of it stalled" read the stall as part of busy; it is outside it [D, `gebusy_add`]. That view's GE was busy 41 ms and stalled 23–24 ms more.
- **`gebusy2`'s own cost** on b30 is unmeasured here: take its ms as the shape of the frame, and price levers from pairs (`cpu_cost_map.md` §4).

## Keeping it current

Re-run the four stretches when the frame's shape changes (a new ME job, the replay rebuilt, frames in flight), and update the table and the "who binds" rows in the same turn as the findings (the lab's `psp/sierra/findings/measurements.tsv`, rows with source "ledger 3").

Started 2026-11-04.
