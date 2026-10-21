# Review: fix-revert (sleep tables across a game state load)

Branch `fix-revert140`, off w140; the same change is `fix-revert`, off w139. Evidence labels as in CLAUDE.md.

## The question

`psp_sleep` (each slot's identifier, quiet count, ground answer and checksum), `psp_restless` and wake-on-write's `psp_wake_marked` live outside the game state. Nothing emptied them when a game state loads (`game_state.c`'s `before_load_procs`: a checkpoint revert, a saved game, a core load) or when a map starts [D]. A load puts back every object's bytes without marking any, under the identifiers they were saved with. With wake-on-write on (the default in a local game), an unmarked sleeper whose identifier matches is not summed: it keeps the checksum it slept with and skips its update, whatever its restored bytes are [D].

## Why it held in practice [D], [E]

- **A sleeper restored at rest is safe.** An object is asleep only when its bytes at the start of a tick equal those at the start of the last one (`psp_sleepy_update` stores the checksum taken before each update): its update was a no-op on them. An object asleep when the checkpoint was saved comes back as such a fixed point, so skipping its update leaves the same game as running it.
- **Units, corpses, vehicles and items re-read their restored state.** Their `may_sleep` (`psp_resting`, the corpse settle test, `psp_item_may_sleep`) reads the restored at-rest bits before any checksum, so one moving at the checkpoint is updated. A corpse's `ground` answer is reset by `psp_corpse_unsettled` while it moves.
- **What's left is narrow:** an object that was changing at the checkpoint, asleep by the time of the revert under the same identifier, and offered to sleep whatever its state. Only scenery qualifies (`may_sleep` is its type alone): scenery moving or animating at the checkpoint (a scripted scenery animation) would come back frozen at that point until something marks it.
- **The emulator found none** [E]: `wakecheck` (every unmarked sleeper summed anyway) across a `game_revert` on c10 (hands off, and through the fight driver), on b30's beach route, and across a `map_reset` on c10: 0 unmarked sleepers changed, against 1.9 to 7.1 million sums a run, the same as a run with no revert.

## The change (on by default; keep word `sleepforgetoff`)

`FIXES_WAKE` `objects.c#wake_forget` and `game_state.c#wake_forget`: `psp_sleep_forget` empties the three tables; it is one of `before_load_procs` (after `game_sound_clear`) and runs at the top of `game_state_initialize_for_new_map` (every map start, before the objects are made; a unit the profile doesn't count, so a release profile still matches). Every object is then summed afresh as a new one: identifier 0 never matches a live object, so every object is updated in the first two ticks after a load and may sleep from the third, as at a level's start. Exact by construction: it only makes checks run that the tables would have skipped. `sleepforgetoff`: the tables kept across a load, as before.

**Cost:** about 20 KB of `memset` a load or map start, and two ticks with every object awake after it: what a level's first ticks cost.

**Multiplayer:** the tables are the campaign's and the server's simulation is its own; the multiplayer build compiles with it.
