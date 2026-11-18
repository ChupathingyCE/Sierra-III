# TGME: the code layout coloured for both caches, the ME weighted as it runs

The ME's own code layout (Sheepdog01, 2026-11-18 09:10: "it does matter, and it's something I have been pushing for for a while"; the lab's decisions.md 09:13). It's recipe rung D's layout part (the lab's `coord/briefs/recipe_refresh.md`), worked now on the tock without waiting on rung C. It retires L0's zero ME weight. Labels: **[M]** PSP, **[E]** emulator or estimate, **[D]** code or data, **[U]** inference.

## What was there, and how current (read 2026-11-18)

- **TGME itself** (the lab's `levers.tsv` row TGME): `codeorder --me-runs --me-weight`, two-cache colouring, built 2026-11-02 on w142. It scored the ME replay's misses at -7.7 / -9.1 / -10.6% on b30 / c10 / c20 [E], and was priced neutral on the PSP-1000 (b30, the ME's replay -0.1 to -0.4 ms a line [M]) when the ME was off the critical path. Its revive_if was met on 11-10: the ME is b30 flight's furthest-over unit, and code misses are 29-32% of its job [M] (`hardware_facts.md`). **That price predates MEXEC**, so it no longer describes the tock's ME.
- **The adopted layout, w_rc1_tgc90** (2026-11-16, Sierra-III 338ef9f1): its graph is the lab's ltotrc1, three routes (b30's October route to 170 s, c10 to 120 s, c20 to 90 s), with `nomeaudio` and `--me-weight 0`. That is L0: the ME's graph was collected but given no weight. No pad and no fight window went into it, and the ME's audio was off.
- **MEXEC is on in the tock** (keep word `mexec`, list rb: 95 functions, 158 KB; `mexec.md`). The PSP's ME runs the replay's hot code from a copy in its eDRAM, so the text layout no longer places that code for the ME. The copy's sets come from the list's order. The text layout still places everything else the ME runs: the AI job (19.3 ms a frame of the ME on b30's flight, b156 [M], stretches.tsv), the audio mix (9.1 ms), the ring jobs (3.4 ms), and the replay's code that isn't copied.
- **The cache model didn't know this:** the emulator runs the ME's jobs from the originals in main RAM, so before 2026-11-18 a `meside` run modelled the replay at its main-RAM addresses. The simulated audio job ran without `psp_me_context`, so with the ME's audio on its fetches counted as the main CPU's. The model also had no windows: a graph counted from the map's load.
- **MEHOTG** (`mehotg.md`): its re-derived block landed on the ME's hot stack sets (mehotgr1). It's this work's second step.
- **On the PSP, the tock has no ME miss counter.** meprofile was on R's milestone branch. The ME's own figures are hwprofile's job times by kind (replay, audio, AI, ring), the ME's own clock.

## What was built (2026-11-18)

- **The model sees the ME as the PSP runs it** (the lab's halo-local.patch item 35, emu.sh, batch.py; lab 1a331c4):
  - the ME's fetches made in MEXEC's copy go through the ME's cache at the copy's eDRAM places. Copy mode is tracked through jal/j/jr/jalr and a shadow return stack, as the copy's patches make it, so a function pointer or a call from the AI job reaches the original, as on the PSP;
  - the ME's fetches are counted by job (`ICACHE ME JOB replay|ai|ring|classify|audio|other`), and with the ME's audio on the simulated mix counts as the ME's;
  - `cachewin = "<from>-<to>"`: the instruction counts and graphs over a game window, with the caches warm before it.
- **codeorder `--me-copy`** (lab `tools/codeorder.py`, check 4 of `test_codeorder_me.py`): the copy's functions are fixed nodes at their eDRAM sets, so a main-RAM function the ME runs is placed against the copy lines it runs beside.

- **A gap the model keeps:** 49 of the copy's 106 jump-table words stay in main RAM (sierra-build 44337295012's mexec line), so on the PSP those switches leave the copy. The model keeps a `jr $reg` inside its function in the copy. A small share, and in the copy's favour.

## The graphs (the lab's tgme2g, sierra-batch 44352925716) [E]

The tock 3989e308 at its recipe plus `PSP_PADPATH=1`, in w_rc1_tgc90. Four held windows (seed1), with the ME's audio and `mexec` on: b30_leg2 20-67 s (the flight) and 67-125 s (the landing and beach), c20_leg2 60-150 s, d20_play1 160-190 s. Findings `tgme2_me_jobs_baseline`. The ME's I-misses on the flight, in millions (fully associative in brackets):

- **replay 17.25 (13.01).** 7.99 (8.24) of it is in MEXEC's copy, which is capacity-bound: the copy's order has little to gain. 9.26 (4.77) is the replay's main-RAM code, about half of it conflicts, and that is TGME's ground.
- **ai 6.64 (6.43):** capacity-bound, so placement can't move it.
- ring 0.37 (0.17); audio 0.07, so the ME's audio code is negligible; other 0.94.

The other windows have the same shape (c20: replay 36.3, half of it in the copy; ai 7.6).

## The candidates and their score (the lab's tgme2s0/1/2) [E]

`codeorder --color --link --graph trg --hot-share 90 --line-heat --me-runs` on the four windows, link map and alignments from sierra-build 44337295012. I-misses against w_rc1_tgc90 (findings `tgme2_cache_scores`), flight / beach / c20 / d20:

| Layout | The ME, all jobs | The ME's main-RAM code | The ME's ring jobs | The main CPU |
|---|---|---|---|---|
| m0: the windows alone, ME weight 0 (the control) | +5.3 / +5.3 / +7.1 / +4.1% | +4.9 / +6.7 / +8.4 / +4.0% | +74 / +33 / +6 / +9% | -9.3 / -9.9 / -9.9 / -13.1% |
| **m1: ME weight 1, the copy fixed** | **-8.5 / -7.3 / -6.9 / -9.5%** | -11.4 / -8.8 / -5.7 / -10.7% | -49 / -35 / -63 / -50% | **-6.1 / -7.0 / -8.9 / -9.7%** |
| m2: ME weight 2, the copy fixed | -7.6 / -6.2 / -6.3 / -7.1% | -11.2 / -9.8 / -9.1 / -10.2% | -49 / -35 / -64 / -37% | -4.8 / -6.4 / -7.3 / -9.2% |

- The windows alone help the main CPU and cost the ME. Giving the ME its weight, with the copy fixed, turns the ME's side 12-15 points for about 3 points of the main CPU's gain. m1 is the candidate.
- Exact by construction: only addresses move. Every window counted the same ticks as the baseline.

## The PSP pair (queued)

w_rc1_tgc90 against w_tgme2m1_tgc90 on b30_leg2 held (seeds 1 and 2, to `exitat130`, where the ME is furthest over), with hwprofile's ME job times by kind as the ME's own clock. The lab's `coord/psp_queue.md` has the command.
