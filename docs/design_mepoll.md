# Design: mepoll, fewer wake-ups of the threads that wait on the Media Engine

Not built. Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code, [U] inference or arithmetic.

## What GEBUS's threads line shows

Two threads above the game's priority wake many times a second to see whether the ME is done, and each wake costs the game thread 25-60 µs of CPU (sierra-hw 39540669642, PSP-1000, b30 route; the 30 s `threads` line: CPU share and times woken) [M]:

| Thread (priority) | What it waits for | Wakes a second | µs a wake | Share of the wall: plain / M6 #2 + mevram | ms a frame on M6 #2 (flight, landing, beach) |
|---|---|---:|---:|---|---|
| `ge forward` (0x10) | the ME's newest stall address under `geinflight`, every 1 ms while a frame is out (`inflight_forwarder`) | 600-720 | 25-32 | — / 1.8-2.1% | 3.4, 3.8, 2.1 |
| `halo audio` (0x12) | the ME's audio mix: a 200 µs first look, then every 100 µs (`me_run_slot`, first look 200 from `psp_meaudio_run`) | 450-650 | 30-60 | 1.8-2.5% / 1.7-2.1% | 3.1, 3.6, 2.1 (plain: 3.7, 4.0, 2.4) |

- The audio job is ~0.7 ms of ME time, but the thread waits 0.9-2.4 ms a block for it (the ME busy with the replay, the sleep's granularity) and looks ~10 times a block; its own mixing is 120-150 µs a block (0.5-0.6% of the CPU) [M]. Most of its share is the looking.
- The forwarder runs only while a frame is out with the ME, i.e. beside the ticks, so its share lands on the tick [D]. Under `geserial` it still wakes (1.0-1.6%) though it has nothing to pass on.
- Neither is in the profile's steps (other threads), and both preempt the game thread, so their time is the tick's and the walk's [U]. What the wakes cost the game thread's caches comes on top and isn't counted here.

## The change

Two keep numbers, off by default (as now), one flag test each:

1. **`gefwdN`: the forwarder's period, N ms (default 1).** At 4 ms the wakes fall by ~75%. The GE gets each new stretch of the list up to N ms later; its lane has room: under M6 #2 on b30 the GE alone takes ~69 ms of the flight's ~118 ms of ticks and ~12-42 ms of the beach's 46-62 [M], and the frame's last stall address is passed on at the sync anyway (`defer_wait_me`), so the list is never left short. The frame end's wait for the GE (`waited for the GE`, now 0.03-0.8 ms a frame) says if the GE starts to lag.
2. **`audiolookN`: the audio job's first look at N µs (default 200), then every 100 µs.** `audiolook0`, adaptive (the one to test): the first look at the last block's wait less 300 µs (at least 200), so one or two looks a block instead of ~10. A block is 23 ms and the wait 1-2.4 ms, so a late look can't starve the output (`late_blocks` counts it if it ever does).

## Exactness and multiplayer

Exact [D]: the same data is passed on and the same samples are mixed; only when a thread notices changes. No simulation change. The audio thread is the same in the multiplayer build (`psp_dsound.c`, `psp_me.c`), so it gains there too; the forwarder exists only under `geinflight`.

## What it should give [U on M]

| | flight | landing | beach |
|---|---:|---:|---:|
| `gefwd4` on M6 #2 (75% of the forwarder) | −2.5 | −2.8 | −1.6 |
| adaptive audio look (80% of the looking) | −1.7 | −2.0 | −1.2 |
| both, M6 #2 + mevram | **~−4.2 ms a frame** | **~−4.8** | **~−2.8** |
| audio only, the plain build | ~−2.2 | ~−2.4 | ~−1.6 |

Small next to `inflightkb` (10-36 ms a frame on b30), but cheap, exact, and the audio half helps every build and multiplayer.

## Checks

- **Emulator:** it runs the ME's jobs synchronously (no ME, no waiting), so neither path runs there: it only shows the build plays the same with both words (`statecrc` against off) and that the multiplayer build compiles.
- **PSP:** one job, `- | gefwd4 audiolook0` on M6 #2 + mevram (b30 route, repeat 2): the threads line's two shares and wakes; `abreport.py` tick and frame; `waited for the GE` stays under ~1 ms a frame; `audio:` lines' late blocks stay 0.
