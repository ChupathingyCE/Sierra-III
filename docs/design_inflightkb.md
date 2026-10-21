# Design: inflightkb, M6 #2's record buffer sized so a frame never waits on the ME mid-walk

Branch `inflightkb` (w140 38490a8 + this). Keep word `inflightkbN`, default 128 (as before). Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code, [U] inference or arithmetic.

## What GEBUS found

GEBUS (sierra-hw 39540669642, PSP-1000, b30 route to 210 s, four sides × 2: plain | `gedefer gemeon geinflight` | + `mevram` | + `mevram geserial`) was run to split M6 #2's tick rise into the GE's bus traffic and the rest. It did that (the read is in the lab's GEBUS handoff), and it also showed where most of the split's lost time goes on b30, which is neither:

- **The main CPU is idle or in the kernel 12-22% of the time under the split** (the threads line's remainder), against 0.3-2.2% on the plain side [M]. On the plain side every thread's CPU share adds up to 97.5-99.7% of the wall.
- **It's asleep in the GE layer's drain at a full buffer** [D, M]. Under `geinflight` a frame's records go into one 128 KB buffer (`DEFER_INFLIGHT_WORDS`, sized for "~100 KB a frame" in recon_134.md). When the walk outgrows it, `defer_take` drains it there and then (`defer_drain` → `defer_on_me` → `psp_me_run`): the main CPU sleeps while the ME replays what's written so far. The PSP's logs show fills in 24-100% of the frames of each 300-frame window from game time 600 on (the same counts in the emulator's gebus1: fills come from what is drawn, not from timing), and none on a10 or c20.
- **What the sleep costs** [M, `GE: gemeon: N runs … the main CPU waited X us a run`, every side with `gemeon`]: 0.1 ms a frame in the cinematic, **26 ms a frame in the flight, 36 in the landing, 10.5 on the beach**; per fill 16-62 ms (the ME replays the first 128 KB of the frame, ~70% of its records). The profile shows it where the buffer fills: `rasterizer_decals_draw` + `render_particles` take 45-57 ms a frame in the windows with the most fills, against 10-13 on the plain side.

So on b30 the split's frame carries ~10-36 ms a frame of main-CPU sleep that has nothing to do with the bus.

## The change (built, off by default)

`inflightkbN`: the buffer is N KB (64-1024; 128 when the word is absent), taken from the heap once, as now. A frame that fits goes out whole at the next frame's first call, after its ticks, as every frame under 128 KB already does. The geinflight line gains the whole frame's size: `GE: geinflight: whole frames N KB on average, M KB at most` (the parts drained at a fill included), so a run says what N each map needs.

**Size:** b30's frames under `mevram` are at most ~190 KB by the PSP's counts (128 drained + up to 63 left at the hand-over in the all-fill window) [M]; one fill a frame at most in every window, so 256 KB holds every b30 frame on this route [U]. inflightkb1 measures the whole frame directly.

**RAM:** +128 KB of heap at 256. b30's heap top is 5,464 of 5,900 KB under M6 #2 + mevram (GEBUS side 3) [M], so 256 fits there. a10 peaks at 5,836 of 5,900 [E, gebus1] and doesn't fill: a10 doesn't need it, and the buffer can't be sized per map today (it is taken once, at the first frame). M6 #2's heap is already one of its default-on gates (a10's failed requests 2 → 15), so this word's default stays 128 until that gate is settled; the follow-up below needs no heap.

## Exactness

Exact [D]: the records and their order are the same; only when the first part is carried out moves (after the next ticks, with the rest, instead of in the walk). Two things could tell the difference, and both already happen on every frame that doesn't fill:
- **Queries the walk makes of the GE layer's state** (the converted-buffer table, the level copies wanted, the walls asked for): under `geinflight` they are answered as on the ME (`defer_me_semantics`), from what is built, whether or not a part was drained. A drained part answers them earlier; without the drain they are answered as on every frame that doesn't fill.
- **The GE's caches** (VRAM copies, the texture cache): a drained part's binds and copies run in the walk; whole, after the ticks. Which bytes are resident can differ for a frame; what is drawn can't (the list holds the same commands).

The emulator check (below) compares `statecrc` (play) and `framecrc` (pictures) against a repeat of the default, the project's bar for exact.

## Multiplayer

`psp_ge.c` only, behind a keep word, no simulation change: the multiplayer build compiles it (PSP_MULTIPLAYER=1 build on build2 with this commit) and plays as before with the word off.

## What it should give (b30, PSP-1000, side 3 = M6 #2 + mevram) [U on M]

| Stretch | Frame now | Sleep removed | Moved into the ticks | Net | Frame after | Plain build |
|---|---:|---:|---:|---:|---:|---:|
| cinematic | 132.7 | 0.1 | 0 | ~0 | ~133 | 143.7 |
| flight | 184.3 | 26 | +5-10 | −16 to −21 | ~165 | 205.5 |
| landing | 196.5 | 36 | +8-15 | −21 to −28 | ~172 | 206.2 |
| beach | 100.5 | 10.5 | +2-4 | −7 to −9 | ~92 | 96.4 |

"Moved into the ticks": the drained part's ME replay and its GE reads now run beside the next ticks instead of beside an idle CPU: the GE's reads at GEBUS's price (geometry ~9.6 µs a KB a frame, textures ~1.7), and the ME's replay at stage 0a's +4.5-11% of the time it overlaps. The ME's lane stays inside the ticks: its whole frame is ~60 ms of ME time in the flight against ~118 ms of ticks, and the GE alone is ~69 ms [M]. On these numbers M6 #2 beats the plain build on every b30 stretch, the beach included (now +4% in the noise).

## The emulator check (dispatched: sierra-batch inflightkb1, emupool)

b30 route to 210 s: default, default again (the noise control), `inflightkb256`; a30 route to 390 s: default, `inflightkb256`. All with `gedefer gemeon gemesim geinflight mevram fixedticks fixedseed statecrc` (b30 also `framecrc`).
- **Pass:** b30-256 fills 0 (`GE: gedefer: … (0 when the buffer filled)`), whole frames ≤ 256 KB; `tools/statecmp.py` b30-def vs b30-256 identical; `tools/framecmp.py` b30-def vs b30-256 no more frames apart than b30-def vs b30-def2; heap top at 256 on b30 and a30.
- a30: does it fill (the census never ran a30 with `geinflight`), and does 256 fit.

## The PSP pair (not dispatched; the master queues it)

6 runs × (730 + 90) s ≈ 82 min, PSP-1000 (either PSP; label it):

```
timeout 120 tools/lab.sh run sierra-hw ref=<inflightkb head> map='levels\b30\b30' route=b30_b139t2_timed maps=/bake/b139a/halo/maps init="cheat_deathless_player 1" wall=730 keep="nearauto mekinds gebusy assettrack geo2 exitat210" keepsets="- | gedefer gemeon geinflight mevram | gedefer gemeon geinflight mevram inflightkb256" repeat=2 allowmismatch=yes env="PSP_PGO=use PSP_PGO_PROFILE=campaign-w140 PSP_PGO_EXTRA=-fno-unroll-loops,-fno-peel-loops,-fno-tracer,-fno-ipa-cp-clone PSP_TEXT_LAYOUT=b132"
```

Read: `tools/abreport.py` per stretch (frame, drawing, tick); the `gemeon` line's wait a run × runs ÷ 300 (expect ~0 at 256); the threads line's idle share (expect the plain side's ~1-2%). **Pass:** the frame falls on the flight and the landing beyond the repeats' spread and doesn't rise on the beach.

## Follow-up if the heap can't give 128 KB: the part to the ME's eDRAM

The ME has ~1.7 MB of its eDRAM untouched (`me_programme.md` §4b: 256-1,984 KB). At a fill, instead of replaying the part while the main CPU sleeps, an ME job copies the part's records into eDRAM (~1-3 ms for 128 KB at the ME's streaming rate [U]) and the main CPU goes on writing from the buffer's start; at the hand-over the ME replays the eDRAM part, then the rest. No heap, the same order, and the ME reads those records from its own memory rather than over the bus beside the ticks. It needs a copy job, a second record base in the replay and the eDRAM map's room (R2's trial wants the same room), so it waits on this pair's answer.
