# The flight recorder (PSP_FLIGHTREC=1, keep word `flightrec`)

The game's events and the engine's cost on one time axis, recorded on a PSP over a held pad replay and read later on a bench (Sheepdog01, 2026-11-19 10:00-10:30; the lab's brief `coord/briefs/flightrec.md`). Code: `psp/src/psp_flightrec.c`, the hooks in `psp_platform.c` and `psp_lanetrace.c`, and `FIXES_FLIGHTREC` in `psp_fixups.py`. The reader is the lab's `psp/sierra/flightrec.py`. The viewer comes after the first run's JSON exists.

## What existed, and what the recorder takes from it

| Instrument | What it already gives | What the recorder takes |
|---|---|---|
| The step profile (`psp_step`, every build; `notimers` turns it off) | The time from one step to the next, charged to the first, in 512 slots. Timed one frame in four (`stepsall`: every frame). Written to `profile.txt` every 10 s | Each charge as it lands, split by the main CPU's phase, per 2 s slice. The slice carries its timed frames so the reader scales the untimed ones in, as `profile_report` does |
| lanetrace (`psp_lanetrace.c`, `lanetrace`) | The main CPU's phase changes, the GE's busy spans (with `gebusy`/`gebusy2`) and the ME's slot and ring jobs, all on one clock. 60 frames after each phase edge, as log lines | Every frame's notes, handed over at the frame's end. While the recorder runs, lanetrace opens no windows of its own |
| hwprofile (`hwprofile`, halo_profile.prx) | The CPU's 19 counters, as totals and by phase, added ten times a second. A 5 s heartbeat line | The totals and the by-phase totals, differenced per slice. A slice's edges are good to 0.1 s |
| The heartbeat, frametrace, metrics | Frame, tick, drawing and frame-end times, as 5 s, 300-frame or per-map figures | The same times, per frame and per slice, from `psp_frame_account` and `psp_tick_account` |
| phasemark | Map loaded, cutscene begun and ended, BSP switch, as log lines | The same edges, as events (the recorder's own test, so phasemark isn't needed) |
| aioutcome (`PSP_RECON`) | Encounters, script starts and ends, ai_place, kills, every 5 s as log lines | Nothing (it needs the recon build, whose heap fails on c20: B64/B79). The recorder's game hooks are its own, outside recon |
| framepic, KSAMPLE, pcsample | Box marks, kernel samples, PC samples. Each is a build switch or keep word of its own | Not folded in yet. They can run beside it, and their cost then shows only in the bare run's gap |

**New in the recorder:** the game hooks (objects made and deleted by type, script calls by function with the level script's beats as events, deaths with team and player cause, actors by action each slice, the player's seat each tick). Also the per-frame record with its job spans, and every instrument's own cost.

## The record

Every 2 s of game time (60 ticks), at the first frame end past the boundary, one record is written to `ms0:/halo/log/flightrec.flr`. A run of records follows, each a word (type << 24 | bytes) and its payload, little-endian. The types are: 1 the header (calibration), 2 a name (a step's string by its address, a script function by its index, a map), 3 a slice, 4 the end. A slice holds:

- **The head:** the slice's game ticks, its clock, frames and ticks, tick, drawing and frame-end µs, stick reads, timed frames, notes kept and dropped, average live actors, objects and effects, and the CPU's MHz.
- **S, the steps:** name, µs and calls, by phase (0 other, 1 tick, 2 drawing, 3 frame end).
- **H, the counters:** hwprofile's 19, total and by phase.
- **O and A:** objects made and deleted by type, and actors by action (none, sleep, alert, fight, flee, uncover, guard, search, wait, vehicle, charge, obey, converse, avoid).
- **C and E:** script calls by function, and events (tick, kind, two small fields, a word).
- **I, the instruments:** id, how measured, calls, µs.
- **F, the frames:** each with its number, end time, draw µs, end µs and ticks, then its lanetrace notes (12 bytes each: end, start, and kind, detail and the ME's own µs).

**Size [U, from lanetrace's counts]:** ~25 notes a frame (gebusy joins the GE's spans into ~5-8, plus phases and the ME's jobs). That is ~0.35 KB a frame, plus ~2-3 KB a slice of steps and counts, so ~15-25 KB a slice and ~1-2 MB for b30_leg2 to the Warthog. **Memory:** about 120 KB from the heap when the word is first seen: the slice's frames 56 KB, its head 16 KB, the state ~30 KB (the step cells 16 KB of it), and lanetrace's notes 18 KB. The log's `flightrec: on (N KB ...)` line gives the exact figure. Notes past 56 KB in a slice are dropped and counted. The c20 held run's heap low on the PSP was 848 KB free (sweepme2-held, 2026-11-19).

## Honest accounting

Each slice's `I` section gives one row per instrument:

- **Timed by the clock:** the recorder's tick hook, its frame hook (lanetrace's hand-over included), its slice end, and its stick writes (bytes too). A slice end's own time and its write land in the next slice's rows, because they happen after the record is made.
- **A count times a cost calibrated at the start:** the step timers (timed frames' calls), lanetrace's notes (the interrupt hold, a floor), the game hooks, hwprofile's phase calls, and the recorder's own clock reads. The calibration (a timed loop of each call, ns a call) is in the header and in the log's `flightrec: calibrated` line.
- **Named but not timed:** hwprofile's module sampling, gebusy's interrupt handlers beyond the note itself, and cache and bus disturbance.

**The bare run is the whole cost:** the same held replay with `notimers` and none of the instruments, beside the recorded run on the same PSP. Both repeat tick for tick (heldaa-b30). `flightrec.py --bare` puts the bare run's heartbeat windows on game time and gives three figures per slice: the recorder's whole cost (wall against bare), the part its own rows see, and the unseen gap.

## Rules kept

- **Read only.** The hooks read game state and write only the recorder's own memory.
- **The ME.** Every engine hook returns at once on the Media Engine (`PSP_ME_WORK()`). No hook reaches a system call or the VFPU on the ME path. The tick and frame hooks run on the game's thread only.
- **A default build is byte-identical.** The file, its defines and `FIXES_FLIGHTREC` exist only with the switch (proof below).
- **No USB in the game.** The record goes to the stick through the port's wrapped `sceIo` calls, as the log does. The time is in the writes row.

## Proofs

- **A default build is byte-identical [D, built]:** the tock 34be07fc and builder/flightrec at a6ec290c and 901ad22e both give EBOOT f7dcbef1b0e6f377 on the tock's recipe without the switch (the lab's sierra-build 44576621830, 44576656158, 44578587657). Later commits touch only `psp_flightrec.c`, which a default build doesn't compile.
- **Play is unchanged with the recording running [E]:** the lab's flightrec3 (sierra-batch 44585439212, ceedbc06, PSP_FLIGHTREC=1 + PSP_PADPATH=1 on the tock's recipe) ran held b30_leg2 seed1 to exitat80 and c20_leg2 seed1 to exitat60. Recorder off against recorder on, play and both seeds were identical on every tick (statecmp: 2,400/2,400 and 1,800/1,800), with strict 0 sites on all four runs. The recorder wrote 26 and 36 slices (179 and 253 KB, 0 write failures), and `flightrec.py` parses both into JSON.
- **The emulator's blind spots, found here [E]:**
  - **The step profile is empty in the emulator.** `profile.txt` has no rows, with or without the recorder (flightrec1's off runs too), and no frame there is a timed one. The recorder therefore calibrates after 300 ticks without one (the header's flag says so), and its emulator files have no tree. On the PSP the profile fills (sierra-hw 44565140172's `003_profile.txt`).
  - **gebusy2 trips strict in the emulator.** With `gebusy2` on, strict lists range loads and stores at 0x083ffexx from gebusy_add, gebusy_ge_finish, gebusy_signal and psp_lane_note. That is the GE interrupt's code on the emulator's interrupt stack, which it puts in kernel RAM (flightrec1, flightrec2; the same family as KSAMPLE v3's). The recorder alone is strict 0. The PSP ask keeps `gebusy2` for the GE's spans.
- **The first PSP pair:** the lab's `flightrec-b30held` (coord/psp_queue.md): bare against recorded, held b30_leg2 to the Warthog, on the PSP-3000.

## The first PSP pair (2026-11-19) [M]

The lab's sierra-hw 44680919272, PSP-3000, ref 7ab916d6, held b30_leg2 to exitat150, one run a side: bare (`notimers`) against recorded (`flightrec gebusy2 hwprofile`); the lab's measurements.tsv `flightrec_psp_b30_first`.

- **It runs on the PSP:** calibrated (clock read 562 ns, interrupt hold 121, step 113, game hook 152, hwprofile phase call 3,781), recording from game time 299, 71 slices, 723 KB written, 0 write failures. Heap free low 1,406 KB, no request failed. Both runs exited, crash.txt no. `flightrec.py` reads the file (71 slices, 54 events, frames).
- **The tree is empty:** `steps timed at the calibration: 0`, and every slice's tree has no phases. Not a fault: `notimers` is on by default in the tock (psp_platform.c's `keep_defaults`), so no step is timed unless the run asks for `timerson`. The recorded side has to carry `timerson`.
- **Its cost isn't read yet:** one run a side, so no verdict. The recorded side's frame was +2.0 / +6.3 / +6.3 ms (+2.9 / +7.8 / +6.1%) on the opening, flight and fight, and −3.1 ms on the beach (a stretch that varies ~10% run to run). The recorder's own instruments come to ~0.4–0.55 ms a frame. The rest of the gap is the recorded side's other words (`gebusy2`, `hwprofile`: 3.8 us a phase call) and what no instrument sees [U].
- **Next pair:** `notimers | timerson flightrec`, two runs a side, the same stretch. The recorder alone, with its steps timed, against bare: its own cost, and the tree.
