# Review: aimeobslook and sndtierback (B54, B55: the ME stopped on d20, and the sound tier silent after a stop)

**Branch** master/mestop, on master/tock159. **Keep words** `aimeobslook`, on by default (`keep_levers` in psp_platform.c; `aimeobslookoff` turns it off), and `sndtierback`, **on by default** (`sndtierbackoff` turns it off): the safety net for an ME stop, which is a bug to prevent (Sheepdog01, 2026-11-04 10:51), not a state to recover from. `mestopatN` is a test word: it stops the ME at game time N. **Touches** psp_fixups.py (FIXES_AIME2A: actors.c, path_obstacle_avoidance.c, actor_moving.c; FIXES_SNDEDRAM: xbox_sound_cache.c), psp/src/psp_dsound.c, psp/src/psp_platform.c.

## B54: the AI job's word unanswered for 20 ms

**The bug [M].** On the PSP-3000, d20 to exitat90 with the flashlight, one run of two: at game time 1051 the main CPU asked the AI job to end and waited out its 20 ms bound. The ME was stopped, and the main CPU ran every job after that. The log named the step in hand: 10, a path search's node. The census's worst for that step was 45 ms by the run's end, against 13-14 ms in the run that didn't stop.

**Why it isn't the audio [M].** The full-rate music made the ME's audio blocks longer on average (d20 3.1 -> 4.2 ms), but not at worst (7.3-8.7 ms against 7.3-8.2 before). The AI job mixes audio only in its yield between actors. The word is tested just before that yield and just after it, so a block adds at most its own length to a wait. The yield's worst was 6.6-8.3 ms in tock159 and 6.2-7.9 ms in tock158. Neither is near 20 ms.

**Where the 45 ms went [D, U].** A step's worst runs from its mark to the next mark of any step. Step 10 is marked at each node a path search pops. After the search's last node, nothing marks or looks until the actor's next look, or its next actor. That stretch holds:
- the path's build (`path_state_build_path`): its smoothing, and its obstacle-avoidance search (`path_obstacle_avoidance.c`'s `path_find`), which loops over a heap of steps with a planar ray test each and had no look;
- and the rest of the actor's update after its path, until a world writer hands it back.

The obstacle search is the unbounded part: its steps grow with the crowd near the actor, as in d20's swamp. The census below tells the parts apart on the PSP.

**The fix.**
- `path_find`'s loop calls `psp_aime_search_look(11)` before each step: a census mark, then the word's look, as `path_state_traverse` does before each node (`psp_aime_decision_look(10, 0)`).
- A step writes only the search's own state: the caller's local `obstacle_path`, or the debug storage. No collision walk is open between steps. So a stop there is put back by 1b's copies, as at the path search's nodes.
- Off (`aimeobslookoff`) the mark stays and the look doesn't, so both sides of an A/B carry the same census.
- The bound and the stop stay as they are: they're the safety net.

**The census.**
- Step 11 is an obstacle-avoidance step.
- Step 12 is marked once an actor's path is built (actor_moving.c): the rest of the actor's update after its path.
- A new line gives what bounds the wait directly: `the job's longest stretch between places it could answer the word`. A place is a look while the actor in hand can be handed back, or one of the word's tests between actors. The line gives the step it began and ended at, and a bit for each step marked inside it.

## B55: the tier's sounds silent after an ME stop

**The bug [M].** When the ME stops, the sound tier closes. Voices playing from the eDRAM are then mixed silent: the main CPU mixes, and it can't read the eDRAM. Each such sound is given up only once it stops playing. In B54's run that was 335 blocks with voices silent (~7.7 s) before all 23 tier sounds had been given up.

**The fix (v2: no eDRAM access after a stop).** Three fills near a busy or stopped ME have hung a PSP (sndedram round 1 twice, and this branch's first cut's DMACplus out of the eDRAM after a stop). So nothing here reads or writes the eDRAM once the ME has stopped. At the first poll that sees the stop:
- The cache's fence is closed first, so new blocks come from the main tier.
- Each tier sound still playing or loading is taken off the tier's books. Playing means the cache's counts, or a packet of it in the game's view of any voice's queue: a channel lets go of its sound while the mixer still has packets of it queued. Each such sound is held by a count of its own while the list is worked, because the allocation pushes out whatever nothing holds, tier blocks included.
- Each gets a main-tier block from the cache's own allocation, and the datum moves to it with its counts. It is then read again from the disc by `cache_file_read`, the main tier's normal load. The old block is let go as an unplayed one, and the tier's own load of it, if any, is dropped (`psp_sound_cache_alive`).
- The game's view of each queue is moved at once. The mixer's packets are moved by a command (`CMD_MOVE`) that the audio thread applies in order after any packet queued before it, since in the default mode the audio thread doesn't take the mixer's lock.
- Until a block's read lands (or 5 s pass), its voices play muted: a brief gap, counted ("voices played muted meanwhile in N blocks").
- Every other tier sound is given up, so its next play reads it into the main tier.
- A sound that finds no room stays silent as before and is given up when it stops (counted).

## Proofs

**B54.**
- Exact [E] (the lab's mestop1): play and both seeds the same every tick, tock159 vs this branch, on the b30 route (4,500 ticks), c20 (3,600) and d20 with the flashlight (2,700). With `aimelate3` on both builds, d20 is the same too: 182 forced endings at the word a side. The emulator's AI job runs on the simulated ME, so this proves the code around the looks and the stop path, not the PSP's timing.
- PSP-3000 pair [M]: d20 to exitat90, flashlight at 1400, `- | aimeobslookoff`, two runs a side, one build. Neither side stopped the ME; tock159's same stretch stopped it in 1 of 2 runs.
  - The longest stretch between places the word could be answered: on 8.9 and 9.8 ms, from a path search's last node to the first obstacle step (the path's smoothing and the obstacles' gathering). Off: 24.0 ms (from a candidate's rays, through the obstacle search, to the next actor) and 13.1 ms. The bound is 20 ms, so the off side's 24 ms would have stopped the ME had the word come at its start.
  - The word was answered within 7.2 / 8.4 ms on and 11.8 / 7.7 ms off. One obstacle step runs at most 1.1-2.8 ms. 0 audio blocks late on every run.
  - Time: from 1400 on (the flashlight), the tick is a tie (18.3 vs 17.9 ms, within the spread). Before 1400 the on side ran slower: tick 100-400 +0.9 ms, 560-1000 +4.5 ms. The on side's ring-late rest fell in the opening fight (game time ~405) on both its runs. [U] The cause is play, not the look's cost: on the PSP a handback at a new place changes the AI's play, much as `aimelate` changes it in the emulator. The sides' fights differ (unit screams queued by 901: 20 and 13 on against 87 and 127 off). Each look is a clock read and a few tests between steps of ~1 ms. A pair on b30, with its known floors, would price it apart from d20's play.

**B55.**
- Emulator [E], d20 with `mestopat900` (the lab's mestop3-7). Off, 632-636 blocks have a voice mixed silent after the stop. With the rescue's last emulator form (e5023838), 6 playing sounds (322 KB) were copied, 1 still-loading sound was read again, and 487 blocks were silent. The voice left silent is a sound that found no room in the main tier: it was full of sounds playing. Each earlier form left the count where it was. The cause each time, in order: the mixer's command ring held old packets; a channel had let go of a sound the mixer still played; the allocation pushed out sounds waiting to move (a fault); still-loading sounds read as loaded; the fence still open.
- **PSP-3000 [M]: the first cut (9b316db1) HUNG the PSP** on the rescue's run (sierra-hw 41827708700 r1, no log). It needs hands. The likeliest cause is the open fence, now fixed with the destination guard. A new block could come from the tier itself and become the DMA's main-RAM side. Other candidates [U]: DMACplus out of the eDRAM with the ME halted (bench15 proved it with the ME idle in its loop, not halted), or the mixer's lock held across the DMA. Not proven either way: so `sndtierback` is off by default.

**B55, v3 (recon/sndtierback3 bd6ae399: the no-room case; on by default).**
- **What changed.** The rescue places the largest sounds first, so the smaller ones fit the holes the larger leave. A sound with no room in the main tier stays in the tier, unheld, and is tried again at each sound poll as the main tier's sounds end, for at most 30 s after the stop (`tier_back_retry`). Its voices mix silent from the tier meanwhile. Still nothing reads or writes the eDRAM after a stop. The tier's idle 128 KB staging was weighed as the room and not used: it is outside the cache's books (a rescued sound needs a cache block for its datum), and d20's largest sound is 148 KB.
- **Emulator [E]** (the lab's mestop9 41860654557, d20): play and both seeds the same every tick, on against off, at both stops. Tier voices mixed silent: at `mestopat900` 629 off, 10 on (9 sounds read again, none left); at `mestopat1000` 400 off, 147 on (2 left without room: one found room on its 56th try at 5.2 s, the other ended first). Side cost, there in v2 too: the engine's own new loads fail for room for about 2 s after the stop (4 against 190 attempts at 900).
- **PSP-1000 [M]** (sierra-hw 41865182489, d20, `mestopat1000 exitat60`, `- | sndtierback`): both runs played to their end, with no hang and no copy job or DMA after the stop. Tier voices silent: 444 off, 352 on. The main tier is fragmented at the stop: 1,208 of its 1,392 KB is held by sounds playing or loading, and the longest run it can clear is 32-44 KB. The five sounds left without room (59-109 KB) found it at 0.13, 6.2, 6.7 and 7.4 s.
- **Why on by default as it is** (Sheepdog01, 2026-11-04 10:51): an ME stop is a bug like a crash. The work goes into preventing stops; the rescue is the net that keeps most of the sound if one happens.

