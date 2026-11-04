# Review: aimeobslook and sndtierback (B54, B55: the ME stopped on d20, and the sound tier silent after a stop)

**Branch** master/mestop, on master/tock159. **Keep words** `aimeobslook`, on by default (`keep_levers` in psp_platform.c; `aimeobslookoff` turns it off), and `sndtierback`, **off by default**: it turns the rescue on. It stays off until it runs on a PSP without a hang (see B55's proofs). `mestopatN` is a test word: it stops the ME at game time N. **Touches** psp_fixups.py (FIXES_AIME2A: actors.c, path_obstacle_avoidance.c, actor_moving.c; FIXES_SNDEDRAM: xbox_sound_cache.c), psp/src/psp_dsound.c, psp/src/psp_me.c, psp/src/psp_platform.c.

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

**The fix.** At the first poll that sees the stop, with the cache's fence closed first, before the rest of the tier's close:
- Only if the ME halted itself, and not for a sleep. bench15 read the eDRAM by DMACplus with the ME idle. A sleep loses the eDRAM, and a held reset is unproven.
- Every tier sound that nothing plays is given up at once (`psp_sound_tier_drain`), so its next play reads it again into the main tier.
- Playing means the cache's counts, or a packet of the sound in the game's view of any voice's queue: a channel lets go of its sound while the mixer still has packets of it queued. Each such sound is held by a count of its own while the list is worked, because the allocation pushes out whatever nothing holds, tier blocks included.
- Each tier sound still playing gets a block of the main tier from the cache's own allocation. The closed fence keeps the allocation out of the tier, and it pushes out what nothing plays. Its bytes come out of the eDRAM by DMACplus, in pieces of up to 65,520 bytes. The main CPU's lines for the block are written back and dropped first, and dropped again after. The cache's datum moves to the new block with its play counts. The old block is let go as an unplayed one. The game's view of each queue is moved at once. The mixer's packets are moved by a command (`CMD_MOVE`) that the audio thread applies in order, after any packet queued before it. In the default mode the audio thread doesn't take the mixer's lock, so moving the packets from the game thread would miss those still in the command ring.
- A sound still loading into the tier is moved the same way and read again from the disc into its new block. Its load would fail at the loss, and the cache's flag holds 2 for a failed read, which the engine reads as loaded.
- The DMA refuses any block outside main RAM's user memory, and any source outside the tier.
- The whole tier is 656 KB: ~2.6 ms of DMA at the measured 256 MB/s, once a session.
- A sound that finds no room, or whose copy fails, stays silent as before and is given up when it stops. A log line counts both.
- In the emulator the tier is main RAM, and the copy is a memcpy.

`psp_me_dma_edram` now also reads the eDRAM after such a halt (`psp_me_halted_clean`). A start or a sleep clears that.

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
