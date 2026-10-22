# Review: a late ring job rests the ring instead of turning it off (keep words `ringgiveup`, `ringrestN`; recon `ringlateN`, `ringhang`)

**What happened.** On a PSP-3000 ladder, at b30 game time ~1,170, a perception-ray job (`ME_JOB_PERC_RAYS`) was not done 10 ms after its push. The ME was in the middle of a 77-97 ms render job (geshow3's replay), with the audio effects' mixing beside it, about 64-67% busy. The ring's give-up then turned the ring off **for the session**. Every later map (b40 to d40) ran its slant, ray, pose and point jobs on the main CPU. Against an earlier ladder whose ring stayed on, the tick rose by +3.3 ms on c10, +1.1 on c20 and +2.4 on d20 [M]. The cache model shows no rise of the main CPU's own work between those builds [E], so the ring loss carries most of it.

**Why the give-up was built that way [D].** `psp_me_wait` stops waiting at `RING_DEADLINE_US` (10 ms) and the main CPU does the job's work itself. The ME may still finish that job later and write its output, which by then is the caller's again (the rays' answers, the pose arena). Turning the ring off made sure no later push could put a job behind a stuck or slow ME, and no late write could land on an output that had been reused. The write itself was already harmless for the late job: its answers go unread, and every caller does its own work when the wait fails. The danger was the *next* jobs. A truly hung ME would make every later push wait out 10 ms.

**The fix (psp_me.c).**
- A late job now **rests** the ring (`ring_dead = 2`) instead of ending it (`1`). `psp_me_ring_ready` is false while it rests, so every producer falls back to the main CPU exactly as when it was off.
- At each frame end (`psp_me_wake_poll`, where the ring's one producer is between frames) `ring_revive` looks at a resting ring. It comes back only when all three hold:
  - the rest is over (1 s, or `ringrestN`);
  - the ME runs and isn't being stopped;
  - **the ME has popped every job ever pushed** (`RING_POPPED == ring_next`).
  
  The last condition is the safety. Once the ME has popped the late job, none of its writes is still to come, so no output can be written late after the ring returns. Pushes come only from the game thread, which is in the frame end at that moment, so none can slip in between the look and the return.
- **A hung ME never catches up, so the ring stays off**, as before. The slot jobs' own timeouts (40 ms audio, 500 ms render) still stop a hung ME outright, as before.
- An ME that is late again within 5 s of coming back rests twice as long each time, up to 16 s. So an ME that is often late costs the main CPU at most one 10 ms deadline a rest, not one a frame.
- A job that comes back with a bad answer (past its stack allowance, an unknown type, another job's ticket) still turns the ring off for the session. That is a bug, not a load spike.
- `ringgiveup` keeps the old rule: the first late job ends the ring for the session.

The fix is on by default, because it restores the intended state: the ring on whenever the ME can serve it.

**Not done, and why.** I considered ordering ray jobs so they don't sit behind a long render job, or letting the ring preempt it. The render job already serves the ring every record while it runs under geinflight (`inflight_serve` → `psp_me_yield` → `me_ring_drain`). So a ray job waits only for the longest stretch between two records, or for a see-through flush or a geshow3 room wait, plus the audio mixed there. Finding which stretch held this job 10 ms needs the hardware (`mestash` on a PSP run). The rest-and-revive fix makes the cost of one such stretch one tick's rays on the main CPU, not the rest of the session.

**Recon test words (PSP_RECON builds only).** `ringlateN` treats every ticket that is a multiple of N as late. The wait spins out the 10 ms and fails the job. On the emulator, the simulated ME also stays "behind" (popped held) until the revival look lets it catch up. `ringhang` (with `ringlateN`) never lets it catch up, as a hung ME. In a default build neither exists.

## Proof [E]

Lab batch ringfix1 39771186920 (ringfix 4987f8a on w140 dab585b; bake b140z; campaign PGO campaign-w140c; fixedticks fixedseed statecrc nomouth to game time 125 s; ME audio simulated, so slant, rays, poses and points all go through the ring). All 10 runs played to their end.

- **Play is unchanged.** Play and both random seeds are identical at all 3,750 ticks between w140 and ringfix on b30's timed route and on c10. Every recon run also matches the fix's play and seeds, including the forced-late ones. The whole state differs only in the parts that follow the frame's speed (objects' and particles' drawing state, sounds, first person weapons), as between any two builds.
- **Nothing changes until a job is late.** Ring jobs: b30 10,240 on w140 against 10,234 with the fix, c10 4,978 against 4,979; 0 failed on all four runs.
- **The ring rests and comes back** (`ringlate500`, b30). Each forced late job rests the ring, and it comes back 1,042-1,068 ms later: 19 late stretches, 18 returns, with 9,471 ring jobs done over the run.
- **Late again soon after coming back doubles the rest** (`ringlate40`, c10). The rests go 1 s, 2 s, 4 s and on: 13 late stretches, 12 returns.
- **The old rule and a hung ME both stay off.** With `ringgiveup` the first late job ends the ring for good ("off from here (ringgiveup)"; 500 ring jobs, then none). With `ringhang` (the ME never catches up) it rests and never returns (500 ring jobs, then none).
- **Strict** (`ringlate100`, interpreter, gemesim mewrites): the ring rested and came back four times to game time 866. The 10 range sites are all at 0x083ffef0-0x083ffefc. That is the kernel's interrupt stack, written by the GE's signal and finish callbacks under gebusy, the same thing gualias2 (39719244350) read; none of them is in psp_me.c.
- **Compiles:** campaign PGO 39770408093 ok, PSP_RECON=1 39770152866 ok, PSP_METRICS=0 (PGO) 39771440254 ok.

**On the PSP (not yet run).** The question is whether the c10, c20 and d20 ticks return to the ladder where the ring stayed on, and how often the ring rests on a real ME. The ladder spec is in the merge request.

## Risk

Low. The ring's off state and its fallbacks are unchanged. Only the way back is new, and it runs only after a late job, only between frames, and only on an ME with nothing outstanding. Play can't move: every ring job's answer is exact either way (its checks: perccheck, poseaheadcheck, mepointscheck, meslantcheck), and the main CPU makes the same answers while the ring rests.
