# Box: the AI's think a tick old (s1-me Part A, builder2, from 2026-11-26)

The lab's `briefs/s1_me.md` Part A (the lead 19:5x, re-briefed 20:5x). Design only, nothing built. Cut from `builder2/ticktwo` 450298ad (box_tick_schedule.md §12.3-§12.4). The goal's frame is one tick a frame at 30 fps on both cores (Sheepdog01 2026-11-26 19:3x). The lever rules' bar: built only if the net clears ~0.5 ms a tick on b30's fight (1098-1712) at that frame (decisions.md 2026-11-26 18:3x). Every figure is labelled by how it was measured: [M] a PSP run, [E] the emulator, [D] read from the code or worked out, [U] not known; a figure from the archive or a two-tick frame is a lead, never the goal's.

- **§1, the cost sheet [D]:** below (written 2026-11-26 23:2x).
- **§2, the play faults [D]:** below (written 2026-11-26 23:3x). The conversation fault is aitickoldbuf's cause (R3), and the copy can't answer it. The firing-position fault is aitickoldself's (R1), answered by the input block and the copy's biped and fact-block fields. aitickoldclock is neither fault's (R2 stays as a rule).
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

## 2. The play faults [D] (2026-11-26 23:3x)

The brief's task 2: "each fault names the read that causes it and the field of tc-copy that answers it". Read from Sierra-III `docs/aime_me.md` "AITICKOLD" (lines 125-150, rounds 2-14 on b30 and b40) and the lab's findings/levers/grounds.tsv AITICKOLD (row 507), against box_tick_schedule.md §12.3's R1-R3 (lines 282-286). Each round is an emulator batch of 2026-11-10/11 in the old lab, so every finding below is **[archive, as_of 2026-11-11, E]**: a lead, not today's build. The copy's fields are tc-copy's (box_world_store.md's tc-copy section on `builder2/storecopy` 34147323, §1c(ii), §1d, §1e(ii), §3; the record layout in `psp/include/box_ws_old.h` on `builder2/tccopy` 01208196).

### 2.1 Which cause is which fault

| Fault | Its symptom (rounds) | The read that faults | The cause, among the three | R row |
|---|---|---|---|---|
| **The conversation fault** | b40: 2-3 of 4 on runs fault near game time 15,330 on a 2-byte load at 0x2, and conversations are lost (rounds 2-12, aime_me.md:133-148). Off is clean | The main CPU's actor reads its `external_orders.conversation_index` and finds the datum gone: in `actor_action_can_stop_conversing` (round 2's naming, :134), then in `ai_conversation_finish` +0x64 from `actor_action_change` (round 5's, :140) | **aitickoldbuf's: the think's writes landing live.** Round 12 confirmed it (:148): the conversation was "ended on the ME in the thinking of one of its own participants", and at the main CPU's next tick start "that same actor still named it, gone". The delete went into the live pool. The think's other writes went into aitickoldbuf's record, which covers "the actor's words, its props' words, the props it made or deleted" (:132) but not conversations. So the record as built had a hole, not a wrong rule. `aitickoldconvdefer` (7bc6472c) closed it: "an end made in a thinking pass is kept and made by the main CPU at its next tick start if the datum is the same; a gone one is passed by". Result: 0 of 8 faulted, against 3 of 8 before (round 13, :149) | **R3** |
| **The firing-position fault** | b30's beach: the player is hit 3-4x as often (250-369 against 52-118 off), and the landing zone's encounter never clears. It's a stalemate from ~4,500 to 5,850 that no off run reaches (rounds 2-8, :133-144). On b40 a milder form shows: the players are hit 12-20% less, and the marines kill 0-1 covenant against 1-4 off (round 14, :150) | Each actor's firing-position search reads its own place and its targets' places. At the stalemate's tick starts every search has "found outside range", every path is at its destination, and the control holds `moving` 0 and throttle 0 (rounds 7-8, :143-144). Perception is sound: line of sight and fresh sightings are there (round 6, :141). "The decision is made and the act is refused" | **aitickoldself's: a decision made from a world one tick older than the tick that acts on it.** The search ran for the second tick on the first tick's world. aitickoldself refreshes the actor's own position. That's the input block's job in the tick (`actor_input_sample_position` actors.c:1828-1829), and aitickoldbuf still needs it "because the record's own position is one tick old" (:132). With buf, self and convdefer all on, round 14 had no fault and no stuck actors. It still had the lean ("~1 in 30 by chance"). `aitickoldfresh` refreshed 0 fire targets and 25,243 own positions. So the own-position half is fixed. The **targets' one-tick-old places are still open [U]**: round 8's way to tell them apart (count the decision reads that differ between the one-tick-old world and the live one, :144) was never run | **R1** |
| neither | — | the clock read one tick on (`game_time_get` in the second pass's own context, :127) | **aitickoldclock**: by line 132, a symptom of the order ("the first tick acting on the next tick's clock"), which aitickoldbuf removes: "aitickoldclock doesn't go with it". It isn't either fault's cause once the record holds the think's writes | **R2**, kept as a rule: in forms (a)-(c) the serial tick moves the clock while the think runs |

**s3o5's split isn't read here.** grounds.tsv's row has "b30's beach early 2 of 4, b40 stuck 2 of 4" with "aitickoldclock + aitickoldself each narrowed one". That round isn't among aime_me.md's rounds 2-14, so its symptom-to-switch pairing is [U]. The pairing above comes from the rounds that name the reads.

### 2.2 The tc-copy field that answers each

- **The conversation fault: no copy field. The record answers it.** The fault comes from a write, and the conversation pool is one of the AI's own tables. Those tables are "Not a copy: the record" (tc-copy 1c(ii), the "other actors' and props' datums" row), and 1e(ii) has no conversation row. So R3's record must carry **conversation ends** as aitickoldconvdefer did. R3 already lists "conversations ended". The install makes an end only if the datum is still the same one, and passes a gone one by. One gap is left **[U]**: the think also *reads* the conversation pool. Round 11's run g was "a thinking that ended a conversation already gone". In forms (a)-(c) the main CPU runs beside the think, so if any main-CPU writer of the pool runs between the post and the join, that read is of a table written beside it. The tc-copy holds no conversation field, and adding one is a field the copy doesn't have today. Whether such a writer exists is unread. H1's `actor_update` (where `actor_action_change` ends conversations) comes before every post point, but the damage and noise handlers (`ai_handle_damage` ai.c:1416, `ai_handle_unit_effect` :1562) run from P1 and S1, beside the think.
- **The firing-position fault: three sources, each already named.**
  - **The actor's own place:** the input block, tc-copy 1d's row ("not copied"; it "holds in the parallel phase, **if the tick's refresh is done before the think is posted**"). That is R1: the think reads its own unit from the block H1's refresh made.
  - **The targets' places:** the copy's biped record, `box_ws_old_biped.position[3]` and `.velocity[3]` (box_ws_old.h:39-40, "held, needed", 1e(ii)).
  - **The rest of each target's points:** forward, location, head, centre of mass and aim. That's the **fact block, "needed, missing"** (1e(ii), 68 B a unit, 44 B of it beyond what's held for a biped).

  The copy makes the targets one consistent tick old, which is the same age aitickoldbuf's world had. It doesn't make them fresh. That's the half round 14 left open.

### 2.3 What this says about Part A's form at the goal's frame

- **In the record's window, today's `aime2a` at one tick a frame, neither fault can arise.** It thinks for tick N+1 on the world tick N left, during the record, while "the world holds still" (:127). That's the world N+1's `ai_update` would read first. So nothing it reads is a tick old, and its writes come in at the collection. aime2a's plain runs are exact against the tock on b30 and c20 (:121) [archive, E]. Both faults belong to a think that is a tick older than the tick acting on it. That was AITICKOLD's second pass, and it would be forms (a)-(c) of tc-copy §3 run beside the serial tick.
- **Forms (a)-(c) take on both faults' fixes in full.** They need R1 (the refresh before the post, at H1), R3 with conversation ends in the record and the install's same-datum check, and R2. They also need the copy's missing think rows (the fact block, the noncollideable partition, vitality, team and seat links: 1e(ii)), and the open target half of the firing-position fault. Clearing that half needs an outcome census on the six maps, with b30's beach and b40's stretch held 4 of 4 (§12.3's proof, s1-me task 4).
- **The emulator weighs this case differently from AITICKOLD.** In round 14 only 891-1,895 of ~13,500 ticks used a second pass, because "the emulator's frames are mostly one tick" (:150). In forms (a)-(c) *every* tick's think is a tick old, so an emulator census would weigh the faults fully. That makes it a stronger test than AITICKOLD's, not a weaker one.
- **So §2 adds a cost and no saving.** With §1's net under 0.5 ms, §2 supports stopping Part A at its design. Its revive_if is §1.5's. If the lead's measurement there (the AI job's wait at one tick a frame) revives it, the first form to price is still the record's window, which carries neither fault. Forms (a)-(c) come after it, with R1-R3 as their requirements.
