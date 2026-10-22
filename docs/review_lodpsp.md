# Review: models' level of detail picked nearer (keep word `lodpsp`; a compromise, off by default)

**What it does.** Models already pick their level of detail at the size they appear on the PSP's 272 lines (FIXES_19; `fulllod` restores the game's 480). With `lodpsp` the pick is made as if the screen were 272/480 as tall again: the pixels a model covers are scaled by (272/480)² instead of 272/480, so each lower level of detail comes in 1.76x nearer (3.1x the area) and the smallest objects drop out sooner. Shadows follow the same pixels. One line, in `render_objects.c`'s pixel size (psp_fixups.py, FIXES_LODPSP).

**Why it's here.** Sheepdog01, 2026-10-22: approximate the original's look and feel; compromises are priced like any lever, built off by default behind a keep word, and shown as an A/B. compro priced LODS on the GE-bound stretches (rank_pipelined.md §8, C2): b40's cinematic is the one stretch bound on model vertices; vertices there -15.7%, at most ~2.5 ms a frame [U]. That is under the PSP's A/A noise floor (2.5-4.4 ms a frame on b30's stretches), so it is a look call more than a measured gain, and there is no PSP pair: the side-by-sides below are the A/B.

**Look.** On b40, up to 2,293 pixels of a frame differ (the near elite's edges at a lower level of detail; aicad2c's shots, lab 39750884770). Side-by-sides for Sheepdog01: the shots of this build's proof batch, the defaults against `lodpsp` at the same game times (below).

**Off by default, exact when off.** The default path is the w140 build's own; this build's proof is play and both random seeds identical with the defaults against w140 13a0194.

## Proof [E]

(filled in by the merge request's proof batch)
