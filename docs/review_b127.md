# Review: build 127

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b127 is b126 (`review_b126.md`) plus the branch below, and **its own profile** (`campaign-b127`).

## Release check

Lab 38726145459, against b126 as built: play and both seeds identical at every common tick on d20 (15,091), a50 (25,200) and c20 (18,946) [E]; the whole state differs only where builds always do (the decal vertex cache, the speed-following allocations). The logs read whole, none dropped. b30 and c40 shots right; the multiplayer build plays Blood Gulch. Built from `25f7c80` (lab sierra-build 38726220580). b126 never reached your PSP, so its tests carry over (`build_b127.md`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch | What | Kind | Default | Words | Review |
|---|---|---|---|---|---|
| `logbatch127` | The log batches its lines from the game's start; a line written at once goes in one write; the stick's slow calls named; removing and renaming through the stick's lock | exact speed-up, instrument | on | `logeachline` (off) | below |

## Why a fresh profile matters [M]

The same code (b126) built with its own profile against b122's, two runs each on the PSP-1000, b30 hands off (lab 38711610007 against 38711662972): **beach frame −5.7%, tick −7.9%, drawing −3.9%, flight drawing −3.1%**, all real. A profile goes stale as the code moves, so every build now gets its own, counted at its number's commit.

## `logbatch127`: the log off the stick's critical path (`psp_platform.c`, `psp_trace.c`)

**Why.** On the PSP-1000 (lab 38706758115, b30, four runs) the log's thread held the Memory Stick 12.7–14.4 s a run, 5.0–6.5 s of it before game time ~375: until 20 s after launch every log line was written at once, an open, the line, its newline and a close, four stick calls a line, while the map streams its first views. The stick serves one call at a time, so every reader waits its turn behind them. The log's worst single call took 1.46 s, and an animation read waited ~770 ms behind it: the run's worst frame.

**What.**
1. Once the game runs (game time ≥ 0), lines wait in the batch for the heartbeat's write (each second for the first 20 beats, then every 5 s), as they already did after 20 s. A crash still writes the batch (`psp_crash.c`); a hang loses at most a beat's lines. While the map loads, lines are still written at once (hangs there need them), but each line and its newline go in one write.
2. The stick's report names its slow calls: every call of 250 ms or more is counted, and the slowest four of each 30 s window are logged with their thread, call, file and game time (`stick: N calls of 250 ms or more since the last report; the slowest: …`). It answers which call the 1.46 s one was.
3. `sceIoRemove` and `sceIoRename` (the learned binds' save, the log's rotation) go through the stick's lock like every other stick call, and are timed (`psp_build.py`'s link wraps them).

**Key lines.**
- `psp_platform.c`, the heartbeat: `if (ticks >= 0 && (beat > 20 || !log_each_line())) log_batching = 1;`
- `psp_platform.c`, `psp_log_write`: the line and its newline copied into `log_line_joined` (only under the log's lock) and written once.
- `psp_trace.c`: `stick_take_op` names the outermost call and its file; `stick_give` hands calls of 250 ms and more to `stick_slow_note`, which keeps the slowest four; `psp_stick_report` prints them and starts a new window.

**Word.** `logeachline`: each line written at once for the first 20 s, its newline apart, as b126. `stickparallel` still turns the stick's lock off for every call, the two new ones included.

**Proven** [M] (lab 38713180516, the PSP-1000, b30 on the b113 route, on/off/on/off):
- **The opening (game time 0–400): frame −11%** (204 against 229 ms), **tick −15%**, drawing −9%, all real. The game thread's own lines were written one by one too, inside its tick.
- The flight −1.2% and the landing −2.9% (frame, real); the beach in the noise.
- frametrace: 99% 326 against 351 ms, **99.9% 450 against 547 ms**.
- The log's thread held the stick 1.1 s up to tick ~400, against 5.8 s; the game thread waited for the stick 0.1 s a run against 0.5 s.

## Tried and rejected today (cache models, exact pairs) [E]

Each played identically to b126 at every tick on d20 (statecrc); each made the tick slower, so none is in b127:
- **Lookups inline** (`lookups127`, RL1): 0.5–2.7% fewer instructions, 9–18% more instruction misses; the tick +0.8% to +4.9%.
- **Less inlining** (CI1: `-fno-inline-functions`, and tighter limits): tick +0.6% to +2.4%, drawing +1.3% to +4.0%.
- **Twelve once-per-object tick functions at `-Os`** (CI2, `sizefuncs127`): 20–60% smaller, yet the tick's instruction misses rose 4.6–6.0%; tick +0.5% to +3.0%.

Together: the build's inlining sits near its best for the 16 KB instruction cache, in both directions.

## Found along the way [M]

- **The levers' worth on the PSP** (b126's RV2, RV1, RV4): about 1% of b30's tick (the flight −0.9% with ME audio, real; with `nomeaudio` all stretches in the noise). The cache models' −7.7% compared fights that differed.
- **The PSP's play parts between runs of one side**, `nomeaudio` or not (dead bodies differ from ~tick 1,600): b30's beach is judged by repeats only.
- **b30's first checkpoint** writes 768 KB in 227 ms: frametrace's one ~230 ms "rest" stall in every run.
- **b30's beach on the PSP** (profile lines, `nomeaudio`): bipeds are 39 ms of the tick for 31 of them: their type update 20 ms, node matrices 8.8 ms, the rest of their object update ~10 ms. The scenery's 277 updates a tick (most asleep, their checksums) 4.5 ms.
