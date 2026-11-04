# Review: aimeobslook and sndtierback (B54, B55: the ME stopped on d20, and the sound tier silent after a stop)

**Branch** master/mestop, on master/tock159. **Keep words** `aimeobslook` and `sndtierback`, both on by default (`keep_levers` in psp_platform.c); `aimeobslookoff` and `sndtierbackoff` turn them off. `mestopatN` is a test word: it stops the ME at game time N. **Touches** psp_fixups.py (FIXES_AIME2A: actors.c, path_obstacle_avoidance.c, actor_moving.c; FIXES_SNDEDRAM: xbox_sound_cache.c), psp/src/psp_dsound.c, psp/src/psp_me.c, psp/src/psp_platform.c.

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

**The fix.** At the first poll that sees the stop, before the tier closes:
- Only if the ME halted itself, and not for a sleep. bench15 read the eDRAM by DMACplus with the ME idle. A sleep loses the eDRAM, and a held reset is unproven.
- Every tier sound that nothing plays is given up at once (`psp_sound_tier_drain`), so its next play reads it again into the main tier.
- Each tier sound still playing gets a block of the main tier from the cache's own allocation. The closed fence keeps the allocation out of the tier, and it pushes out what nothing plays. Its bytes come out of the eDRAM by DMACplus, in pieces of up to 65,520 bytes. The main CPU's lines for the block are written back and dropped first, and dropped again after. The cache's datum moves to the new block with its play counts. The old block is let go as an unplayed one, and the voices' packets are pointed at the new place.
- All of it runs under the mixer's lock, so no block is mixed halfway through. The whole tier is 656 KB: ~2.6 ms of DMA at the measured 256 MB/s, once a session.
- A sound that finds no room, or whose copy fails, stays silent as before and is given up when it stops. A log line counts both.
- In the emulator the tier is main RAM, and the copy is a memcpy.

`psp_me_dma_edram` now also reads the eDRAM after such a halt (`psp_me_halted_clean`). A start or a sleep clears that.

## Proofs

(filled in from mestop1 and the PSP-3000 runs)
