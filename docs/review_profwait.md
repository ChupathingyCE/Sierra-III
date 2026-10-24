# Review: profwait (a profile write waits for the last one by sleeping)

**What it fixes.** A freeze in a10's look tutorial at the look inversion: the game stops for good with HOME still answering, no crash.

**Cause [D], reproduced [E].** `player0_look_invert_pitch` saves the active profile. `player_profile_write` first waits for the previous write's thread in an empty loop (`while (!thread_has_exited(thread)) {}`); the playlist profiles' io has the same loop five times. The writer is a pthread at the default priority 0x3c, below the game thread (0x20). Windows shares the CPU between threads, so the original never noticed. The PSP runs only the highest-priority ready thread, so once the game thread spins the writer never runs again. Any profile write made while the last one's thread is still alive stops the game. A thread that `create_thread` failed to make leaves a reference with no handle, which the same loop would wait on for ever.

The emulator reproduces it from the front door (a profile is active; a map started directly has none, so nothing is written): two inversions by console in one tick, or one tick apart, stop game time at the second, with or without `hblowoff`; one second apart they don't. On the PSP the stick is slower and the game thread busier, so the window is wider [U].

**The change** (`psp_fixups.py`, `FIXES_PROFWAIT`):
- `thread_win32.c`: `psp_thread_has_exited_waiting` sleeps 1 ms (`sceKernelDelayThread`) each time the thread hasn't finished, so the writer gets the CPU; a reference with no handle counts as finished. Each case is logged once (`profile io: ...`).
- `player_profile.c`, `playlist_profile.c`: `thread_has_exited` is mapped onto it after `thread.h`; those loops are the files' only uses.
- `profwaitold` keeps the empty loop (the forced hazard, for the A/B).

**Proof [E]** (front door, a10, inversions by console): the fix plays on with both writes made (begin/end x2) where `profwaitold` and the unfixed build stop. Play without a profile write never reaches the change.

**Cost.** None outside a wait for profile io. The changed functions are cold; a strict PGO link needs the profile counted again.
