# Review: branch `metrics` (the measurement round's instruments)

The plan is `docs/plan_measurement_round.md`. Labels: [M] measured on the PSP, [E] emulator, [D] read in code or data, [U] inference.

## The `objcost` and `particlecensus` counters printed no rows

**What was wrong** [E, D]. With both keep words on, an emulator run on b30 and a30 printed each counter's header and nothing under it (`objcost by type` empty, `particlecensus ... made 0`), while the per-type objects line and the particles line beside them were full. The fixup anchors all match (`PSP_RECON=1 PSP_FIXUPS_CHECK=1` reports none missing), so the calls were being made. They were refused at the door: `psp_objcost` dropped any `definition < 0` and `psp_particle_census` any `tag < 0`. A tag index is `index | salt << 16` (`DATUM_INDEX_NEW`), and a map's tag salts are 0x8000 and over (an Xbox map's start at 0xE174), so every real tag index is negative as a `long`. Every call was refused.

**The fix** (`psp/src/psp_platform.c`): NONE (-1) is the only "no tag", as the draw census in `psp_ge.c` already tests it.

```c
const char *name = tag != -1 ? tag_get_name(tag) : NULL;          /* tag_short_name */
if (!objcost_on || definition == -1)                               /* psp_objcost */
long tag = effect != -1 ? effect : particle;                       /* psp_particle_census */
if (!particle_census_on || tag == -1 || outcome < 0 ...)
particle_census[k].from_effect = effect != -1;
```

`tag_short_name` had the same test, so even a filled row would have been named "?".

**Cost:** none outside recon builds. Both counters exist only with `PSP_RECON=1` and their keep words; nothing a play build runs changed.

**Proof** [M]: a 450 s b30 replay on the PSP-1000 with `objcost particlecensus geocluster` prints rows per definition (named, not "?"; the by-type line too) and per effect in every ten-second window; the figures are in the lab's findings (measurements.tsv). Three things the run showed: the instrumented build costs 15% of the frame (+18 ms of 121, 16 of them in the tick), so its absolute times are an upper bound; `tools/passtable.py` mistook the census rows for steps (see below); and `hwthreads` gave nonsense on hardware (below).

## Found reading the first instrumented hardware run

- **passtable.py read the census rows as steps** (fixed, `tools/passtable.py`). `profile.txt` holds `objcost` and `particlecensus` rows inside each window; the parser skipped a row only when a colon sat in its first 12 characters, which held for `objcost:` and not for `particlecensus:`, so every effect row became a step (172 ms a frame of "unclassified"). Both prefixes are now skipped outright; on the instrumented b30 run the unclassified pass falls to 10.5 ms a frame [M]. What is left in it is mostly `object_type_update` (4.2 ms), which no pass pattern claims.
- **`hwthreads` is unusable on the PSP-1000.** In 223 of 226 windows the shares exceed 100% (others 1,000-40,000%, idle up to a wrapped 4.8e15%, no switches counted after load) [M]. Only the loading line is sane. Fixed below.

## `hwthreads`: the phase calls ran the refer calls as a user-mode caller

**Cause** [M][D]: `halo_profile_phase` is a system call, so it runs with k1 marking a user-mode caller. The firmware then refuses `sceKernelReferThreadRunStatus` and `sceKernelReferSystemStatus` (their buffers sit on the call's kernel stack, and kernel threads' status isn't a user's to read). The module took each refusal as a reading of 0: every phase change zeroed the run clocks, the idle clock and the switch count, and the sampler's next (good) reading added each thread's whole lifetime. The run shows it: a window's switch count is the running total (576,363, then 1,177,113 later), and idle "wraps" when a good reading is followed by a zero. The loading lines are sane because no phase calls come then, only the sampler, a kernel thread. The emulator doesn't check k1, so it never showed.

**Fix** (`psp/profile/main.c`):
- `halo_profile_phase` clears k1 around its work (`pspSdkSetK1(0)`, restored on the way out), as the kernel's own system calls do.
- `run_clock` and `system_clocks` return -1 on a refusal, and `threads_account` keeps the last reading then (and when the idle clock would go backwards), so a refusal costs a window's detail, never a lifetime's worth of time.

**Proof:** the module compiles (`make` in `psp/profile/`) [D]; on hardware, pending one short b30 run with `hwthreads` (each window's game + others + idle should come to about 100%, switches in every phase).

## M3: the cache model against the PSP, by phase

**What was compared** [M, E]. The cache model with its timing (`cache = 1`, `pfmap = 1`; the model as corrected before this batch, which counts `lines created (0x18)`) over three stretches of play, each as the difference of two emulator runs that stop at the stretch's end and start (`exitatN`, `fixedticks fixedseed`, so the second run plays the first's play exactly up to the first's end: 2,400 ticks on b30, 2,700 on a30 and a10, two ticks a frame). The PSP's side is the M1 hardware runs of the same routes (`hwprofile` heartbeats whose game time falls in the stretch, summed), on both PSPs, read with `tools/modelcheck.py`. Same code on both sides (the b139 release flags and profile, the b132 layout); the same keep words except that the model's runs also carry `nomeaudio fixedticks fixedseed`. The "ms" of a phase is its active cycles over 333 MHz: a tick for the tick phase, a frame for the others. Memory % is the share of the phase's cycles spent waiting on memory (the PSP's counter, the model's I- and D-miss stalls); I and D are cache misses per million cycles. Model / PSP below 1 means the model is shorter.

**b30, game time 90-170 s (route b30_b139t2_timed)**

| phase | per | PSP-1000 ms | PSP-3000 ms | model ms | model / 1000 | model / 3000 | memory % 1000 / 3000 / model | I per M cycles 1000 / 3000 / model | D per M cycles 1000 / 3000 / model |
|---|---|---:|---:|---:|---:|---:|---|---|---|
| other | a frame | 6.47 | 5.27 | 4.45 | 0.69 | 0.84 | 45.8 / 48.0 / 48.3 | 3385 / 3534 / 3414 | 3284 / 3410 / 3674 |
| tick | a tick | 29.09 | 29.60 | 21.35 | 0.73 | 0.72 | 54.7 / 54.9 / 57.5 | 4702 / 4758 / 5081 | 3110 / 3078 / 3467 |
| drawing | a frame | 45.22 | 44.15 | 29.72 | 0.66 | 0.67 | 46.3 / 48.8 / 44.7 | 3376 / 3410 / 3439 | 2658 / 2574 / 3145 |
| frame end | a frame | 1.28 | 1.25 | 0.82 | 0.64 | 0.65 | 36.6 / 36.7 / 29.4 | 3256 / 3261 / 2456 | 1945 / 1874 / 1899 |
| **frame total** | a frame | 111.3 (2.01 tpf) | 110.0 (2.01 tpf) | 77.7 (2 tpf) | 0.70 | 0.71 | | | |

**a30, game time 60-150 s (route a30_b139t1_timed)**

| phase | per | PSP-1000 ms | PSP-3000 ms | model ms | model / 1000 | model / 3000 | memory % 1000 / 3000 / model | I per M cycles 1000 / 3000 / model | D per M cycles 1000 / 3000 / model |
|---|---|---:|---:|---:|---:|---:|---|---|---|
| other | a frame | 7.80 | 7.93 | 5.87 | 0.75 | 0.74 | 43.8 / 44.3 / 51.6 | 3150 / 3217 / 3447 | 3372 / 3383 / 4105 |
| tick | a tick | 35.88 | 37.45 | 29.63 | 0.83 | 0.79 | 46.8 / 46.4 / 53.2 | 3520 / 3467 / 3977 | 3237 / 3239 / 3850 |
| drawing | a frame | 40.25 | 41.86 | 26.21 | 0.65 | 0.63 | 54.6 / 54.6 / 54.3 | 4312 / 4190 / 4883 | 2518 / 2551 / 3200 |
| frame end | a frame | 1.18 | 1.20 | 0.89 | 0.76 | 0.75 | 34.7 / 35.2 / 26.8 | 3287 / 3378 / 2322 | 1712 / 1691 / 1656 |
| **frame total** | a frame | 121.0 (2.00 tpf) | 125.5 (1.99 tpf) | 92.2 (2 tpf) | 0.76 | 0.74 | | | |

**a10, game time 240-330 s (route a10_b139t1_timed)**

| phase | per | PSP-1000 ms | PSP-3000 ms | model ms | model / 1000 | model / 3000 | memory % 1000 / 3000 / model | I per M cycles 1000 / 3000 / model | D per M cycles 1000 / 3000 / model |
|---|---|---:|---:|---:|---:|---:|---|---|---|
| other | a frame | 4.05 | 4.06 | 2.90 | 0.72 | 0.72 | 40.9 / 40.9 / 49.3 | 3338 / 3378 / 4069 | 2634 / 2578 / 3229 |
| tick | a tick | 7.00 | 6.80 | 5.25 | 0.75 | 0.77 | 54.0 / 54.0 / 61.6 | 4996 / 5035 / 5890 | 2825 / 2814 / 3314 |
| drawing | a frame | 51.32 | 52.84 | 32.53 | 0.63 | 0.62 | 43.6 / 45.5 / 43.0 | 2561 / 2486 / 2814 | 2399 / 2304 / 3471 |
| frame end | a frame | 1.76 | 1.82 | 1.37 | 0.78 | 0.75 | 42.5 / 43.2 / 58.3 | 2036 / 2084 / 1441 | 5090 / 5089 / 6818 |
| **frame total** | a frame | 70.4 (1.89 tpf) | 72.2 (1.98 tpf) | 47.3 (2 tpf) | 0.67 | 0.66 | | | |

**The model's error, model over PSP** (the range over the three maps and both PSPs; the six per-map figures are in the tables above):

| phase | ms | memory % (points, model minus PSP) | I per M cycles | D per M cycles |
|---|---|---|---|---|
| other | 0.69-0.84 | +0.3 to +8.4 | 0.97-1.22 | 1.08-1.25 |
| tick | 0.72-0.83 | +2.6 to +7.6 | 1.07-1.18 | 1.11-1.19 |
| drawing | 0.62-0.67 | -4.1 to -0.3 | 1.01-1.17 | 1.18-1.51 |
| frame end | 0.64-0.78 | -8.4 to +15.8 | 0.69-0.75 | 0.97-1.34 |
| frame total | 0.66-0.76 | | | |

By map, the frame total is 0.70 / 0.71 (b30, against the PSP-1000 / PSP-3000), 0.76 / 0.74 (a30) and 0.67 / 0.66 (a10).

**Caveats the table carries.**
- The model's runs mix sound, sort slants and cast perception rays on the main CPU (`nomeaudio`: the emulator has no Media Engine); the PSP does all three on the ME by default, and its counters are the main CPU's alone. The model's functions-by-pass table (M5) puts the sound pass at 23.8% of b30's fetches (10.8% on a30, 2.2% on a10), all of it in the model's main CPU and none in the PSP's.
- The PSP's stretches are free-running (not `fixedticks`), so their ticks a frame are 2.01 / 2.01 (b30), 2.00 / 1.99 (a30) and 1.89 / 1.98 (a10, PSP-1000 / PSP-3000); the frame total uses each side's own. The model's play is the route's exact play; the PSP's drifts with its timing, so the two see the same route but not the same moments.
- The two PSPs agree within 1.6 ms on every phase of every map.

## M5: the cache model's functions by pass

`tools/passtable.py - --funcfetches TO_A,TO_B --frames N` groups every function in `funcfetches.txt` (the cache model with `fabound`, the whole run, all phases) into the passes of a frame and takes the difference of the two runs. Fetches a frame in thousands (one cycle each, at least) and I-misses a frame (about 64 cycles each in the model); percentages are of the column's total. The pass list is the one the tool prints: the tick's work is split into its own passes (AI, units and bipeds, collision and physics, object updates, animation, scripts); `the rest` is what no pattern claims.

| pass | b30 fetches (k) | % | I-misses | % | a30 fetches (k) | % | I-misses | % | a10 fetches (k) | % | I-misses | % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sound (the ME's on the PSP) | 2884 | 23.8 | 1681 | 1.5 | 1512 | 10.8 | 1124 | 0.9 | 177 | 2.2 | 423 | 0.8 |
| collision and physics | 1594 | 13.2 | 9720 | 8.7 | 3572 | 25.6 | 11163 | 8.8 | 645 | 8.1 | 2166 | 3.9 |
| helpers (libc, maths, tags, data arrays) | 1547 | 12.8 | 13924 | 12.5 | 1643 | 11.8 | 14664 | 11.5 | 852 | 10.7 | 6515 | 11.8 |
| objects | 1373 | 11.3 | 22227 | 19.9 | 1850 | 13.2 | 29929 | 23.5 | 817 | 10.2 | 9712 | 17.6 |
| GE layer | 929 | 7.7 | 8832 | 7.9 | 1220 | 8.7 | 13527 | 10.6 | 2276 | 28.5 | 9093 | 16.4 |
| visibility | 832 | 6.9 | 3559 | 3.2 | 781 | 5.6 | 2661 | 2.1 | 562 | 7.0 | 2022 | 3.7 |
| decals | 411 | 3.4 | 529 | 0.5 | 35 | 0.3 | 170 | 0.1 | 197 | 2.5 | 528 | 1.0 |
| tick: AI | 367 | 3.0 | 9811 | 8.8 | 761 | 5.5 | 9732 | 7.6 | 35 | 0.4 | 1316 | 2.4 |
| the rest (functions outside the lists' top) | 350 | 2.9 | 11480 | 10.3 | 488 | 3.5 | 12147 | 9.5 | 475 | 5.9 | 8046 | 14.5 |
| system | 320 | 2.6 | 6099 | 5.5 | 443 | 3.2 | 8758 | 6.9 | 262 | 3.3 | 4832 | 8.7 |
| level | 245 | 2.0 | 2070 | 1.9 | 493 | 3.5 | 2123 | 1.7 | 511 | 6.4 | 1117 | 2.0 |
| tick: units and bipeds | 222 | 1.8 | 10295 | 9.2 | 212 | 1.5 | 10424 | 8.2 | 49 | 0.6 | 2704 | 4.9 |
| tick: object updates and sleep | 214 | 1.8 | 3511 | 3.2 | 210 | 1.5 | 3094 | 2.4 | 101 | 1.3 | 1606 | 2.9 |
| effects | 176 | 1.5 | 2150 | 1.9 | 212 | 1.5 | 2337 | 1.8 | 196 | 2.5 | 1524 | 2.8 |
| animation | 159 | 1.3 | 1075 | 1.0 | 133 | 1.0 | 1415 | 1.1 | 32 | 0.4 | 500 | 0.9 |
| HUD and menus | 116 | 1.0 | 1376 | 1.2 | 94 | 0.7 | 1169 | 0.9 | 66 | 0.8 | 702 | 1.3 |
| soft double (the FPU is single) | 110 | 0.9 | 2004 | 1.8 | 91 | 0.7 | 1680 | 1.3 | 85 | 1.1 | 1043 | 1.9 |
| caches (lruv) | 89 | 0.7 | 57 | 0.1 | 4 | 0.0 | 18 | 0.0 | 1 | 0.0 | 8 | 0.0 |
| logging, debug text | 87 | 0.7 | 529 | 0.5 | 88 | 0.6 | 635 | 0.5 | 85 | 1.1 | 431 | 0.8 |
| scripts | 60 | 0.5 | 398 | 0.4 | 80 | 0.6 | 420 | 0.3 | 98 | 1.2 | 418 | 0.8 |
| shadows | 32 | 0.3 | 88 | 0.1 | 46 | 0.3 | 107 | 0.1 | 467 | 5.8 | 595 | 1.1 |
| saves | 0 | 0.0 | -0 | -0.0 | 0 | 0.0 | 0 | 0.0 | 0 | 0.0 | 0 | 0.0 |
| **total** | 12117 |  | 111415 |  | 13969 |  | 127297 |  | 7987 |  | 55301 |  |

Sound runs on the main CPU here (`nomeaudio`), so its row is what the ME takes off the PSP's main CPU; I-misses are small for it (1-2% of the total) and its fetches large. `funcfetches.txt` counts instructions, not data, so a pass's data cost (M3's D-miss rates, `cachemap.txt`) is not in this table.

## M6: the limits per map

`docs/plan_m6_limits.md`: per map (b30, a30, a10), the frame with memory waits, drawing or the tick removed, and split across the main CPU and the ME, against 15/15 and 30/30; then the levers re-ranked. In short [U on M inputs]: 15/15 is reachable on all three (a10 now, a30 with the GE layer on the ME, b30 with that and ~10 ms off its tick); 30/30 is in sight only on a10; drawing alone is over 30/30's budget on every map, and the GE's own time (a30 39 ms a frame mean [M]) walls a30's 30/30. The GE's bytes off main RAM rank first, as the gate for the split.
