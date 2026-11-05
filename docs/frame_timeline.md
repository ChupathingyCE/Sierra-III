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

## The GE's main-RAM reads inside the tick (lead A, 2026-11-04)

The GE draws through most of the tick (above), and while it reads main RAM every tick miss costs more (`cpu_cost_map.md` §2). This section turns that into a figure: what the GE reads from main RAM, what share of it lands inside the ticks, and what that costs the tick.

**How it's counted** [U on E and M]:
- **Bytes:** `gereadskind` on tock162 with B58's emulator fix (lab sierra-batch 41964943382, Sierra-III `geoff/gecensus` 482d95b2) [E]. It counts the GE's main-RAM bytes by kind of draw and by part: vertices (v), indices (i), the list's own bytes with the data inline in it (l), textures once a frame (o), and palettes (p). It also places each byte by where in the frame's list it comes (quarters).
- **Overlap:** the share of each quarter of the GE's busy time that falls inside the ticks, on the PSP [M]. b30's flight uses lane-hw2's quarters (96 / 96 / 94 / 51%, sierra-hw 40718998656). c20 uses lanetrace on tock162 (77 / 90 / 87 / 56%, 41942245562). The other stretches use ledger 3's GE-in-tick share, spread evenly.
- **Price:** GEBUS's slopes [M on E]: 9.6 µs of tick for each KB a frame of geometry (v, i, l), 1.7 µs for each KB of textures.
- **Where the GE can read from:** the GE reads only main RAM and VRAM [D]. The ME's eDRAM is private to the ME.

| ms a frame of tick | b30 cinematic | b30 flight end (1802–2402) | c20 steady | d20 swamp walk | d20 flashlight on |
|---|---:|---:|---:|---:|---:|
| GE main-RAM KB a frame [E] | 1,788 | 3,447 | 1,454–1,883 | 948–1,157 | 1,090–1,281 |
| Inside the ticks | 99% | 84% | 80% | 95% | 59% |
| Vertices | 6.5 | 4.2 | 1.0 | 1.0 | 0.5 |
| Indices | 2.1 | 3.8 | 1.0 | 0.5 | 0.3 |
| **The list's own bytes** | 2.0 | **4.4** | **1.9** | **1.7** | **1.8** |
| Textures + palettes | 1.1 | 2.8 | 1.6 | 1.1 | 0.7 |
| **The toll** | **11.7** | **15.1** | **5.5** | **4.4** | **3.4** |
| Of the tick (ledger 3) | 16% | 17% | 15% | 7% | 8% |

- **b30 is the map where it matters.** The toll is ~7.5 ms a tick on the flight, a sixth of the tick. GEBUS measured 13.0 ms a frame on the flight directly (the GE held wholly outside the ticks, PSP-1000 [M]), within 16% of this count. c20's toll is 2.9 ms a tick and d20's is under 2.5.
- **By kind:** on b30, models carry 60% of it (9.0 of 15.1 ms) and come first in the list (quarters 1–2, 96% inside the tick). The level comes second half (4.8 ms). On c20 the level leads (2.5 of 5.5). With the flashlight on d20, the light pass's list is the largest part (1.2 ms).
- **The flight report is partial.** The flight's own reports (1202, 1802) lost their kind lines to the log; their vertex fit curves survived. The 2402 report covers the flight's end and the landing's start. w142's flight report (gereads1, 3,172 KB a frame) agrees within 9%.

**What VRAM would take, and what a KB is worth** (b30's flight; [U on M]): the fit curve of the 1202 report. 128 main-RAM vertex buffers are read 832 KB a frame. Each step below holds the best buffers in VRAM, priced at 90% inside the tick and 9.6 µs a KB:

| VRAM given to vertex buffers | KB a frame taken off main RAM | Tick saved, ms a frame | Per KB of VRAM |
|---:|---:|---:|---:|
| 200 KB | 408 (49%) | 3.5 | 17.6 µs |
| 400 KB | 632 (76%) | 5.5 | 9.8 µs |
| 600 KB | 765 (92%) | 6.6 | 5.7 µs |
| 800 KB | 832 (100%) | 7.2 | 2.9 µs |

- **The curve agrees with a measurement.** CTNG's +177 KB of copies' room measured −2.8 ms a frame on the flight, 15.8 µs a KB [M, VRSP-784 39661123709].
- **Index lists are about as dense.** VRAMIDX's 153–234 KB of live lists carry ~410 KB a frame, worth up to ~3.8 ms.
- **On the other maps the room is worth less.** On c20, 200 KB takes all 128 KB a frame of vertex reads: ~1.0 ms. On d20, 200 KB takes 43–47% of 108–148 KB: ~0.5 ms.

**Where the room would come from:** nowhere, today. Ledger 4 (`memory_ledger.md` §0.4) has 13–48 KB of VRAM free in all: 2–16 in the textures' share and 11–32 in the copies' share, with the frame and depth buffers' 816 KB fixed. The eDRAM's 544 KB at 256–800 KB is the machine's largest free block, but the GE can't read it. The ME, which writes the list, can't see VRAM [D]. So keeping the overlap and moving the bytes needs VRAM freed first:
- **Trading the textures' share:** closed on b30 (VRSP: untextured frames ×10, frame in the noise).
- **Shrinking the copies:** MV14 measured a loss; MVDIET was dropped.
- **Scanning the display out of main RAM:** rejected on arithmetic [U]. It would put ~16 MB/s of steady reads on the bus, more than the GE's geometry reads (~11 MB/s on the flight).

The curve is the price to rank any future source of VRAM against: ~17 µs of b30 tick a frame for each KB of the first 200.

**What the two other options cost** (step 2):
- **The GE wholly out of the tick** (geserial, GEBUS, PSP-1000 [M]): tick −2.6 to −7.6 ms a tick, but frame +14 to +52 ms. The GE's work then runs in series, and on tock162 it is larger (83.6 ms a frame on b30's flight against GEBUS's 69). Not run again: tock162 differs only in the direction that strengthens that verdict.
- **Gating the GE between ticks** (CTNT): parked as it was.
- **Keeping the overlap:** what stays in the tick whatever VRAM gets is the list's own bytes (4.4 ms a frame on b30's flight; 1.7–1.9 on c20 and d20, the largest part there) and the textures (2.8 ms). The list is the next count: lever LISTBYTES (the lab's levers.tsv) splits it by command and looks for words the GE already holds.

## The ME per job, and what moving it would buy (lead B)

Lead B (the master, 2026-11-04 23:20): the ME's scheduling sized before any rebuild. Two PSP-3000 runs on tock162 d76ff14b (6fcf5520 + B58's emulator fix, strict, profile campaign-w162d, 0 functions without counts), the ledger 3 keep set plus `lanetraceatN` (one window of 60 frames inside the stretch): d20's swamp route with the flashlight (lab sierra-hw 41967367512, `lanetraceat2100`, exited at 2,669) and b30's timed route (41967373964, `lanetraceat3200`, exited at 4,384). One run a stretch: a sizing, not an A/B. The ME heartbeat's slots now print whole (instr4's continued lines), so **the AI job's row is measured**, no longer by difference. The emulator gave the shape first (lab sierra-batch 41965518688, recon/mesched2 = tock162 + B58's series: 0 heap requests failed, strict 0 sites): one replay and one AI job a frame, a sound copy every 7-11 frames; its ME is simulated, so no ME times [E].

| ms a frame [M] | d20 swamp walk (300-1350) | d20 flashlight on (1500-2669) | b30 flight (600-2000) | b30 beach (2700-3700) |
|---|---|---|---|---|
| **Frame** | 99.4 | 93.8 | 135.5 | 95.0 |
| Main CPU working (tick + drawing working + other) | 97.3 (tick 67.9) | 76.1 (tick 41.0) | 132.6 (tick 89.1) | 94.2 (tick 64.2) |
| Main CPU asleep in drawing | 2.0 | **17.7** | 2.9 | 0.8 |
| *The ME's jobs (own time)* | | | | |
| the replay (1.0 a frame; longest one) | 26.6 (62 ms) | **80.8** (147 ms) | 71.7 (96 ms) | 27.7 (74 ms) |
| audio (blocks a frame) | 20.9 (4.3) | 15.6 (4.0) | 17.6 (5.8) | 20.7 (4.1) |
| the AI job, measured (longest one) | 12.0 (41 ms) | 3.6 (27 ms) | 18.9 (35 ms) | 10.6 (30 ms) |
| ring jobs: rays, poses, points (jobs a frame) | 3.6 (0.67, 1.53, 0.78) | 1.5 (0.17, 1.65, 0.75) | 3.7 (0.85, 1.69, 0.62) | 2.1 (0.70, 1.47, 0.66) |
| visibility (`mevisland`) [U] | ~1.4 | ~0.5 | ~3.6 | ~1-3.5 |
| sound tier copies | 0.1 | 0.1 | 0.1 | 0.1 |
| busy (the ME line, visibility left out) | 61.2 | 99.9 (101-104% of the clock) | 105.5 | 59.3 |
| *Deadlines and waits* | | | | |
| the replay: by the next record's first GE call; the main CPU's wait | 0.1 | **19.9** | 0.3 | 0.0 |
| audio: a block each 23.2 ms (bound 40); the audio thread's wait a block, mean (worst) | 5.2 (11.2) | 4.8 (13.4) | 3.6 (13.9) | 5.1 (11.6) |
| the AI job: by the next frame's first tick (bound 20 ms after its word); the main CPU's wait | 0.00 | 0.00 | 0.00 | 0.00 |
| ring jobs: within 10 ms; the main CPU's wait (refused, ME busy, a frame) | 0.23 (0.91) | 0.16 (0.59) | 0.15 (0.74) | 0.10 (1.20) |
| the GE stalled in a list for the ME's next chunk | 4.2 | **49.7** | 2.6 | 1.2 |

How to read it [M unless marked]:
- **Where the ME is saturated its own times sum past the clock** (d20's heavy view reads 101-104% busy): take its job figures as ~3% high there.
- **Visibility isn't in the ME's figures at all** [D, `psp_me.c`]: its job is collected by polling (`psp_me_ring_done`), and only `psp_me_wait` counts a ring job, so neither the ring line, the busy % nor lanetrace sees it. Its row is [U]: the views it answered (b30 217-262 of each 300 on the flight, 227 and 40 on the beach; d20 32-160 on the walk, 32 with the flashlight, the rest refused or not offered) times ~4.5 ms, the longest visibility job the AI job's drains saw on d20 (4.4-4.6 ms; the main CPU's own walk on the beach 4.8 ms a view, its wait for the ME's answer 2.1). On b30's beach that puts the ME at ~60-63 ms busy, not 59.
- **The AI job never makes the main CPU wait** on these stretches (worst 157 µs): handed over after the frame's last tick, it runs 6-12 ms of its own inside a ~25-45 ms span and is done before the next frame's first tick (lanetrace: b30 frames 1619-1622 handed at −25 to −32 ms, collected at +1.3 to +1.7). Its ledger 3 row by difference was close on d20's heavy view (3.7) and b30's flight (17.2), and high on d20's walk (19.5 [U] against 12.0 measured) and b30's beach (13.1 against 10.6).
- **The replay is one job a frame**, handed over at the frame's end (lanetrace: −80 to −136 µs) and collected at the next record's first GE call. On d20's heavy view it is 63-66 ms of its own in a 67-71 ms lanetrace frame (window at tick 2100), against 26.6 ms on the walk: **the flashlight adds ~54 ms of replay a frame**, the light pass the ME carries.
- On the beach the timed route keeps the ME at ~65% of the clock. Capacity4's hands-off stand at the beach (lab sierra-hw 41950823554) is another view: replay 91 ms a frame with its AI, 66-68 with it erased. There the beach is ME-bound too, so a design must hold both views.

### The ceilings [U on M]

A frame in flight lasts about as long as its busiest unit's work, since the units overlap: frame ≈ max(main CPU working, ME busy), with the GE below both here. A job moved from the ME to the main CPU at a cost of c ms of the main CPU for each ms of the ME makes it max(ME − X, main CPU + c·X). c from the record: the ME builds the GE's lists ~25% slower than the main CPU (L1), so c ≈ 0.8 for the replay; the AI job is the engine's own code, which ran on the main CPU before AIME, c ≈ 1; audio's mix is float work, c ≈ 1 as written and ~0.3-0.5 if put on the VFPU (a matrix product is ~3x faster on the VFPU than on the ME's FPU, `hw_capabilities.md`) [U].

- **The main CPU's wait if the replay started earlier, or ran in parts beside the ticks:** d20's heavy view: **0 ms**. The ME is busy the whole frame, so an earlier start or a split in time gives it no room: the replay already runs beside the next frame's ticks (handed over at the frame's end). Only work leaving the ME shortens it. b30's beach: **0 ms**: the main CPU waits 0.0 ms for the replay now. b30's flight: 0.3.
- **d20's heavy view, ME-bound by ~18 ms** (ME ~94, main CPU 76.1), balance at ~85 ms:
  1. **The replay split across the units** (part of the lists made on the main CPU again, the light pass's first): move ~10 ms, c 0.8 → **frame ~84, −9.8 ms (−10%)**, the most of the three.
  2. **Audio moved to the main CPU**: 15.6 ms. As written (c 1): max(78.2, 91.7) → **−2 ms**. On the VFPU (c 0.3-0.5): **−8 to −10 ms**, and the ME's audio bound (40 ms, B60) leaves with it.
  3. **The AI job moved back**: only 3.6 ms here → **−3.6 ms**; it costs b30 and d20's walk 10-19 ms of the main CPU, where the main CPU binds (AIME's own pair: b30 frame −4.3 to −8.5% with it on the ME).
  - Any mix can't pass the balance: (76.1 + 93.8) / 2 ≈ 85 ms, **−9 ms**. Beyond it, only the replay doing less (the light pass's work itself) pays.
- **b30's beach (route) and flight, and d20's walk, are main-CPU-bound** (ME ~65% busy on the beach, ~80% on the flight, ~63% on the walk). None of the three designs buys anything there: each moves work onto the unit that binds (audio +20.7, the AI job +10.6 ms on the beach). What pays is the other direction: **work moved onto the ME's ~32 ms of room** a frame on the beach (~36 on the walk), up to the balance, (94.2 + ~62) / 2 ≈ 78 ms, **−17 ms at most** at c 1, less for float work (the ME's FPU ~3x the VFPU) [U].

So the ME scheduler's rebuild buys on one kind of stretch only: where a heavy light pass puts the replay past the ticks (d20's flashlight, capacity4's hands-off beach). Its best single move there is a replay split by measured fit, worth up to ~10 ms on d20's heavy view; audio on the VFPU is next. On the main-CPU-bound stretches the ME's room is the prize, and it goes to work moved onto the ME, not off it.

## b40 late: the cliff path and the snowfield fight (2026-11-04)

b150's hand test on the PSP-1000 slowed to 5-10 fps late in b40: on the cliff path to the bridge (level 0, z ~48) and in the snowfield fight below it (level 0, z ~0) [M, its logs]. This section makes both stretches reproducible and reads their frames as the stretches above.

**The recipe.** The lab's route `b40_b150hand_timed` replays the hand test's first try through the fight, position by position on its clock (`route_from_log.py --timed --map b40`, the log cut before its revert). It reaches levels 0, 4 and 0 by itself, and the fight spawns as it did in the hand test. Forcing the BSP switches by command makes them flap. A shortcut that starts at the elevator's foot reaches level 0 with no enemies (1 biped a tick): the encounters follow the level's script, which needs the whole path [E, lab sierra-batch 41970036866]. On the PSP: `init = "cheat_deathless_player 1"`, `exitat518`, wall 1200 s; it takes ~13 minutes. The route replays positions, not shooting, so no enemy dies: the snowfield has 30 bipeds a tick against ~21 in the hand test's play.

**The run.** Tock162 d76ff14b, strict on campaign-w162d, layouts w161a_tgc90 and hotg161a, bake w157m: the build of the lead B rows above. The ledger 3 keep set with `geo2` (the hand test's) and `lanetraceat14500`. One run (lab sierra-hw 41971570306, PSP-3000, exited at game time 15,475).

| ms a frame [M] | upper: cliff path (5,100-7,350) | lower: snowfield fight (14,350-15,500) | lower, the hand test's play (b150, PSP-1000) |
|---|---|---|---|
| **Frame** | 118.9 (8.4 fps) | 136.5 (7.3 fps) | 133.0 (7.5 fps) |
| tick (one tick; ticks a frame) | 78.4 (39.4; 2.00) | 81.2 (42.2; 1.91) | 60.0 (35.0; 1.7) |
| drawing, working | 31.9 | 32.9 | 44.4 |
| drawing, asleep | 1.8 | **14.9** | 19.8 |
| other | 6.6 | 6.7 | 8.5 |
| tick waiting on memory, % of its cycles | 68% | 64% | |
| tick I / D misses, writebacks a million cycles | 3,782 / 2,488, 855 | 4,287 / 2,934, 957 | |
| *ME* (busy %) | 69% | 37% | 34% |
| the replay (render jobs) | 46.1 | 21.7 (two a frame) | 24.5 |
| audio | 23.7 | 10.2 | 9.1 |
| the AI job | 11.8 | 12.6 | |
| ring jobs | 2.8 | 3.7 | 2.8 |
| main CPU's wait for the render jobs | 0.6 | **15.6** | 12.2 |
| *GE* busy (inside the ticks) | 46.3 (45.5) | 36.7 (27.8) | |
| GE idle in a list | 3.1 | 0.1 | |
| *What the tick carries, a tick* | | | |
| actors updated (thinking, aicad3) | ~18.5 (~10) | ~23 (~12) | |
| bipeds, vehicles, projectiles | 16.9, 7.0, 19.8 | 30.0, 9.9, 0.1 | 20.7, 2.6, - (at the switch) |
| scenery updates | 126 | 116 | |

The hand test's column comes from its own log (lab psp-probe 41967536333, `014_log.txt`, 14,363-15,305, before the ME stopped there: B62, the master's). Its build is b150 (tock161's code). Its drawing's sleep was the replay's wait 12.2 ms plus texture reads, ~6 ms a frame. Its texture cache was more starved than the route's run: 4,632 requests waited for room in the stretch, against 105 [M]. A whole evening's play had filled it.

**What binds both: the main CPU's ticks.** The tick takes 66% of the upper frame and 60% of the lower. One tick costs 39-42 ms, more than a whole 30 fps frame, and two run in every frame. The ME has room on both (31% idle upper, 63% lower). The GE is busy 31-39% of the clock and almost never stalls on the ME. These are b30's beach and d20's walk again, with a heavier tick: b30's beach ticks 32 ms a tick on the same build [M, the ME per job above].

**The lower zone's second cost: a synchronous render job inside the drawing.** lanetrace's window at 14,500 shows two render jobs each frame [M]:
- frame 9055, 132.7 ms: tick 1.6-40.8, tick 41.0-84.1, drawing 87.7-132.1;
- the first render job, handed over at the frame's end and collected as the drawing starts, has 1.8 ms of its own: the frame in flight;
- the second is handed over at 108.1 ms, inside the drawing, and collected at 126.8 ms. It has 17.4 ms of its own, and the main CPU sleeps through it.

The main CPU's wait for the render slot is 9.7-13.6 ms a job, two jobs a frame: about one whole job each frame. In the code, the render slot has one path that the main CPU waits for at once: `defer_drain`. A call that must run at once, or a full buffer, sends everything recorded so far to the ME through `psp_me_run`, and the main CPU waits for the result (`defer_on_me`, `psp_ge.c`) [D]. The upper zone has no such job (its wait is 0.6 ms). **What sets it off is a record too large to keep** [D, the run's log]. In the snowfield's 300 frames to game time 14,994, 252 records were over `DEFER_LARGEST` (4,096 words, 16 KB), and `defer_take`'s too-large path drains everything recorded so far before running the call at once. That made 552 render jobs in 300 frames, with the main CPU waiting 9,970 µs a run. None came from a full buffer (the frame's records were 91 KB at most, of 224) and none from text. On the cliff path the same path fired 0-51 times a block, with waits of 0-1.7 ms a run. The record is a sprite group of ~244 sprites: 5,898 words, `DEFER_SPRITES`, the snow's weather particles [E, lab sierra-batch 41978913719]. The bound was set for the plain 64 KB buffer and never grew with the in-flight one. **DRAINFLIGHT** (branch `recon/drainflight`, off word `drainflightoff`) lets an in-flight buffer keep records up to a quarter of itself, as the plain buffer does at 16 KB, so the sprites replay in flight with the rest. It is exact: play and both seeds are identical every tick on b40's whole route and on b30's [E]. If the drain ran in flight like the rest of the record, the lower frame would lose up to ~15 ms (11%) [U].

**Gaps.**
- **The tick by function** [M, PSP-1000, lab sierra-hw 41979909751: the PC sampler's counts written at game ticks 5,100, 7,350, 14,350 and 15,400 (keep words `pcsnapN`, branch `recon/b40tick-pcsnap`), each zone the difference of two snapshots]:
  - Cliff path (40,365 tick samples): collision and BSP 18.6%, object nodes and animation 14.2, actors and AI 13.7, objects 10.4, units, bipeds and weapons 9.5. The top functions are `object_compute_node_matrices_with` 4.8, `object_update` 3.2, `game_tick` 2.9 (objects_update's walks inlined), `collision_bsp_test_vector_recursive` 2.3, `unit_update` 2.2 and `cluster_partition_reconnect` 2.0.
  - Snowfield (44,756): **collision and BSP 26.6%**, actors and AI 13.9, object nodes and animation 12.4, objects 9.2, units 8.8. The top functions are `object_compute_node_matrices_with` 3.7, `collision_test_vector` 3.7, `psp_bsp3d_test_sphere` 3.0, `collision_bsp_test_vector_recursive` 2.8 and `collision_get_features_in_sphere` 2.7. Sphere tests take 7.3% against 4.0 on the cliff path. The extra collision is the actors' and bipeds' own rays and spheres: the route doesn't shoot (0.1 projectiles a tick).
  - Like b30 and c20, the time is spread thin: the top 30 functions hold 44-46%. So in `cpu_cost_map.md` §4 it falls under "Doing less work" (collision's passes) and "Phasing" (the object update and its node matrices), not under a single hot spot.
  - The drawing's CPU: on the cliff path the snow (`weather_particle_system_render` 8.1%, `build_sprite` 4.9, the weather palette 2.7) is ~16%. On the snowfield, allocation (`fits`, `_malloc_r`, `allocate` ~11%) and `object_new_from_scenario` 6.7% show in the drawing phase, unexplained [U].
  - Of an earlier lever sweep's twelve leads (lab or-job 41974123240), none clears ~1% of the tick that isn't already on a ground's row: the sleepers' walk and the header passes (WS, HEO) sit inside `game_tick`'s 1.2-2.9%; vehicles' probe 0.6%, items 0.1-0.2, pills 0.2, markers 1.1-1.4 (the searched share far less); the actors' inline thinking is AIME's ground.
- The cache model can't run this route: 15,500 ticks under the interpreter don't fit a job. The per-phase misses above come from the PSP's counters.
- lanetrace opened one window (the lower zone). The upper zone's order inside a frame is inferred from its waits.

## Gaps this ledger leaves

- **The ME's report line was cut at 512 bytes,** so the AI job's own time never printed in ledger 3's runs: its column above is [U]. Fixed in tock162 (instr4's continued lines); measured from d76ff14b in the ME-per-job section.
- **Visibility's ME time isn't counted** (collected by polling, never through `psp_me_wait`): the ring line, the busy % and lanetrace all leave it out. Counting it at the poll that sees it done would close it.
- **lanetrace opens a window only at a phase edge** (a map loaded, a cutscene's start or end, a BSP switch; keep word `phasemark`). So its frames come from the start of play, not from the stretches above: c20's window after its cutscene (game time ~1,069, 73.2 ms a frame) and b40's (35.1). A window opened at a game time (a `lanetraceatN`) would place d20's heavy view and c20's steady state frame by frame.
- **One run a stretch.** The figures carry each run's own spread: b30 and d20 by route, c20 and b40 hands off. A pair is for a lever, not for this map.
- **The earlier d20 reading of gebusy2, corrected:** sierra-hw 41893041680's "GE busy 41 ms a frame, 23–24 ms of it stalled" read the stall as part of busy; it is outside it [D, `gebusy_add`]. That view's GE was busy 41 ms and stalled 23–24 ms more.
- **`gebusy2`'s own cost** on b30 is unmeasured here: take its ms as the shape of the frame, and price levers from pairs (`cpu_cost_map.md` §4).

## Keeping it current

Re-run the four stretches when the frame's shape changes (a new ME job, the replay rebuilt, frames in flight), and update the table and the "who binds" rows in the same turn as the findings (the lab's `psp/sierra/findings/measurements.tsv`, rows with source "ledger 3").

Started 2026-11-04.
