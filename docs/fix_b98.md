# FIXES_B98: a conversation that has ended is read as none

A fix (the lab's bugs.md B98), not a lever: always on, no keep word.

**The fault.** An actor's `external_orders.conversation_index` can still name
a conversation whose datum is gone. Two readers then load through NULL:

- the actor's decision, `actor_action_can_stop_conversing`, reads the
  conversation's scenario index;
- the converse action's end (`actor_action_change` -> `action_converse_end`)
  calls `ai_conversation_finish` on it.

In the emulator, b40 seed 40 with aifar faulted this way on some builds and not
on others: 22f9c288 and 256976cc faulted 7 of 7 runs. The builds that faulted
and those that didn't had identical game state up to the tick before
[E, the lab's findings b98part1, b98watch3]. The writers of the index are the
engine's own: actor creation, ai_conversation_update and
ai_conversation_finish. No stray write was found. What leaves the name behind
in those builds wasn't seen, because the fault takes the log with it.

**The fix.** Two guards in `psp_fixups.py`, group FIXES_B98:

- `actor_action_can_stop_conversing` reads a gone conversation as none and
  clears it from the actor's orders (the index and the attention unit);
- `ai_conversation_finish` does nothing for a gone conversation.

Both act only when the datum is gone, so play is otherwise unchanged. They are
safe in every role: a multiplayer client runs no AI and makes no
conversations.

**Proof** (the lab's findings b98fix_proof) [E]:

- **Made on purpose.** In b40 seed 40's conversation, at game time 16,350, the
  conversation's datum is deleted with no end (FIXES_B98TEST, recon only).
  Marine 14 is left conversing and naming it. Without the fix the run faults,
  plain and strict. With it, the marine's own update clears the name and it
  leaves the converse action; the run goes on to 16,725, strict 0 sites (the
  lab's sierra-batch 44395409250).
- **The faulting builds with the fix.** They ran clean past 16,290 to
  16,725-16,738, plain and strict (44365756478).
- **Nothing else moves.** The tock against the tock + FIXES_B98: play and both
  seeds identical on held b30_leg2 seeds 1 and 2 (strict) and c20_leg1. The
  whole state is identical too, ignoring the decal vertex cache on b30, and on
  c20 the non-play records of B91's class (objects' non-play words, particle,
  hud unit interface, and object looping sounds, which is excluded until
  B91's determinism question is answered).
