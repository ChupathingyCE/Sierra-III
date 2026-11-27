# Box: the AI's think a tick old (s1-me Part A, builder2, from 2026-11-26)

The lab's `briefs/s1_me.md` Part A (the lead 19:5x, re-briefed 20:5x). Design only, nothing built. Cut from `builder2/ticktwo` 450298ad (box_tick_schedule.md §12.3-§12.4). The goal's frame is one tick a frame at 30 fps on both cores (Sheepdog01 2026-11-26 19:3x). The lever rules' bar: built only if the net clears ~0.5 ms a tick on b30's fight (1098-1712) at that frame (decisions.md 2026-11-26 18:3x). Every figure is labelled by how it was measured: [M] a PSP run, [E] the emulator, [D] read from the code or worked out, [U] not known; a figure from the archive or a two-tick frame is a lead, never the goal's.

- **§1, the cost sheet [D]:** below (written 2026-11-26 23:2x).
- **§2, the play faults [D]:** not yet written. It names which of the three causes (aitickoldself, aitickoldclock, aitickoldbuf) is the conversation fault and which the firing-position fault, against box_tick_schedule.md §12.3's R1-R3.
- §1-§2 go to the lead together, before any code (the brief's task 3).

## 1. The cost sheet [D] (2026-11-26 23:2x)

### 1.1 What there is to move at one tick a frame

**Today's AI already thinks a frame's last tick on the ME.** With `aime2a` (a campaign default; b30's log line 244 in drawcensus-b30, sierra-hw 45943027708 run 1: "the thinking ahead after a frame's last tick done on the Media Engine during the record, collected at the next tick's start"), the ME thinks for the tick after a frame's last tick while the main CPU draws. A frame's earlier ticks think on the main CPU ("passes inline"). AITICKOLD (aime_me.md, "AITICKOLD") moved that *second* pass of a two-tick frame onto the ME, into the same job. It was worth "tick −5.9 ms (−14.5%) [M, PSP-3000]" on b30's flight at two ticks a frame [archive, as_of 2026-11-10; findings/levers/levers.tsv AITICKOLD].

**At one tick a frame every tick is a frame's last**, so every pass is already the ME's and the main CPU thinks none inline [D, aime_me.md:129: "a frame of one tick wastes the second pass"]. The −5.9 ms has nothing to move at the goal's frame. (This is the lab's findings/twocore_read_2026-11-26.md item 7, here with figures.) What the main CPU still spends on the AI at that frame, and so all the saved side can hold, is the brief's three lines: the wait for the AI job's answer at the collection, the hand-over copies, and the answer applied.

### 1.2 The counts (b30_leg2, the PSP-1000, ticks 0-901) [M]

From drawcensus-b30's side A, run 1 (sierra-hw 45943027708, `run1/log.txt` lines 1197-1205, the build c5ff3114 on dbc76ba0, `aicad3` on). These are the only aime report lines the run has: they print every 900 ticks and the run ends at game time 1800 (`exitat60`), so **the fight (1098-1712) has no report of its own** and these ticks are the opening and the flight, at 1.3-2.2 ticks a frame.

| Count, ticks 0-901 | Figure | A tick |
|---|---|---|
| passes | 901 | 1 |
| jobs on the ME (collected 499, refused 0, no answer 0) | 499 | 0.55 |
| passes inline on the main CPU (a frame's earlier ticks) | 402 | 0.45 |
| actors thought ahead / of them on the ME | 17,166 / 9,744 | 19.1 / 10.8 |
| actors handed back and thought whole in the tick (`owed_node_block`, all of them) | 495 | 0.55 |
| late jobs waited for at the collection: 4, in all / the longest | 3,488 µs / 1,649 µs | **3.9 µs** |
| past the wait's bound (200 ms) / ended at the word | 0 / 0 | 0 |
| the pathfinding log applied | 27 | 0.03 |
| the job's longest stretch without a look at the word (a yield's ring job, type 14) | 17,877 µs | — |

### 1.3 The saved side (main-CPU ms a tick, b30's fight, one tick a frame)

| # | Line | Figure | Label |
|---|---|---|---|
| S1 | **the collection's wait** for a job not yet done at the next tick's `ai_update` | 0.004 ms a tick on ticks 0-901 at ~2 ticks a frame; the fight's figure and the goal frame's are not measured | [M] for 0-901; [U] at the goal |
| S2 | the passes inline on the main CPU | 0 at one tick a frame, by construction (§1.1); 0.45 passes a tick today are a two-tick frame's | [D] |
| S3 | the hand-over copies (`psp_aime_think_actor`'s `memcpy`, 1,252 misses a tick of b30's 2,073, 134 calls, 51.8 KB; cpu_cost_map.md:74) | **not main-CPU time at the goal.** The counts are an emulator's, where the ME's jobs run on the main CPU at their kicks and waits (box_biped_motion.md:111), and they mix the ME's passes with the inline ones (1b's copies are taken in both). At one tick a frame every pass is the ME's, so the copies are ME time. Reading them from tc-copy instead would shorten the ME's job, not the main CPU's tick | [E counts; D for the split; U for its ms] |
| S4 | the answer applied at the collection (the store log, the queue, the pathfinding log, 0.03 a tick) and the handed-back actors (0.55 a tick, thought whole in the tick) | stays in both forms: a think a tick old installs its record at the next H1 and hands back the same actors, for the same stop (`owed_node_block`) | [D]; none of it is saved |

**What the saved side can be at the goal: S1, the wait, alone.** It's 3.9 µs a tick today, because the job has the draw and the next tick's start to run in and mostly fits (4 late of 499). At one tick a frame its window shrinks to that frame's drawing and "other" (the main CPU's ~33 ms less the tick), while the ME's other jobs (the replay, audio, visibility, the tick's half) fill the same 30 ms room. A think a tick old (posted at H1 or H4 of tick N, its record installed at N+1's H1, box_tick_schedule.md §12.3) has a whole frame instead. So **Part A's whole case is the wait the AI job would cause at one tick a frame**, and no run has measured it: the job's own ME time never prints (job_graph.md:109, the ME's report line cut at 512 bytes [U]), and no run plays at one tick a frame.

### 1.4 The added side (main-CPU ms a tick, b30's fight, one tick a frame)

| # | Line | Figure | Label |
|---|---|---|---|
| A1 | **the copy, in full** (tc-copy commits 1-3, the refresh and its write-back): `tccopy_b30_price` | waits on the PSP pair (queued behind drawcensus-c20, PSP-1000; reader builder2) | [M] pending |
| A2 | commit 4's ≤ 0.10, in full (21:4x (1)) | ≤ 0.10 ms | [D] |
| A3 | **the think's buffer**, one of three (tc-copy §3 (a)-(c), the lead 21:4x (4)): **(a)** a second buffer: +~233 KB of the general heap at the 1024 pool (218.9 KB at 768, +14,336 B for the pool, the lead 22:4x (2)), so the heap bar's largest-block low goes from ~333 to ~566 KB (c20 clears at 906 [M, `copyheap_c20`]; c40 is heaplow-c40's, in flight); **(b)** one refresh at the tick's start, which moves wsold's point; **(c)** the think posted after the refresh, at H4, which changes AITICKOLD's rule. (b) and (c) are the lead's (21:4x (4)) | (a): the heap, not ms; (b), (c): [U] until ruled | [D] |
| A4 | the post (one ring job, no `kcall`) and the record's install at H1: AITICKOLD's `aitickoldbuf` merge, a field at a time, per actor (aime_me.md "Why it played differently"), and the fallbacks it refuses (4.5% of installs on b30, each thought whole in the tick) | not measured; the merge is per actor, ~10.8 actors a tick on the ME | [archive, as_of 2026-11-10, E for the 4.5%]; [U] in ms |
| A5 | the ME's side: the job reads the copy instead of its 1b copies (S3 moves here as a saving of ME time), plus the record's writes | the ME's room is the measured 30 ms a frame less its jobs; the AI job's share isn't printed (S1's gap) | [U] |

### 1.5 The net, and what it means

**Net = S1 − (A1 + A2 + A4).** A2 alone is 0.10 ms; A1 is ≥ 0 and A4 > 0. The only saving is S1, measured at 0.004 ms a tick today and unmeasured at the goal. **On today's figures the sheet doesn't reach 0.5 ms a tick: it's negative by at least 0.1 ms.** That's the brief's stop line ("a cost sheet under 0.5 ms a tick ... stops that part at its design. It goes to the lead, with no build").

**What could move it, and what would show it:** only S1 at one tick a frame. If the AI job outlasts its window at 30 fps by ≥ ~0.6 ms a tick plus A1 on b30's fight, the think a tick old pays; otherwise it doesn't. Two measurements decide it, both cheap, neither a build of Part A:
1. **The AI job's own ME time a job on b30's fight** (the ME's report line split, job_graph.md:109's gap), with the collection's wait on the fight alone: an aime report at the fight's end (the report's every-900 interval, or the run's exit past 1801).
2. **Its window at one tick a frame**: the frame's drawing plus "other" after the hand-over, from the same run's `budget:` lines with the tick a frame taken out [D, a projection until the build runs at one tick a frame].

If the job fits its window, Part A stops at its design with `revive_if` "the AI job's collection wait on b30's fight at one tick a frame ≥ 0.6 ms a tick plus the copy's [M]". If it doesn't, the case is the wait, and S3's copies are then the ME's share to cut (they shorten the job itself), which is "the AI's hand-over reads tc-copy", not "the think a tick old": the lead's call (twocore_read item 7).
