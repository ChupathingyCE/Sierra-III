# The pad recorder: Sheepdog01's fights as repeatable benches (design, 2026-11-08, master)

**Why** (Sheepdog01 2026-11-07 23:20-23:30): the benches were mostly cinematics and routes, and a route replays the player's positions, not his inputs, so his own fire, grenades and vehicles never happen in it. Combat with vehicles and many AI is the game's normal state, and the parity budget (`parity_budget.md`) has to be priced there.

**What makes it possible:** the simulation is deterministic given the same start, the seed and the inputs each tick. In the emulator that's proven every batch (statecrc). On the PSP it held only once two races were fixed (tock162 edc2d39 and 74cd771: the AI's pass on the ME read tables the record rewrote while it ran): two b30 runs then play the same through the beach fight (lab sierra-hw 42485968207).

## Recording (keep word `padrec`)

- **Where:** `handle_one_player_input` (`game/player_control.c`), right after `get_local_player_input_blob`: the `input_blob` the game acts on, after the pad, the sensitivity and the inhibitions, so the replay doesn't depend on the stick's dead zones or the control settings.
- **The time step too:** each frame's `seconds_elapsed` (`main.c`), which decides how many ticks the frame runs. Recorded with the blob, so a replay runs the same ticks with the same inputs however fast its build draws. No `fixedticks` needed to record: Sheepdog01 plays at real speed.
- **What's written:** a header (the map, the build's commit, the seed word, the keep words that change play), then per frame its `seconds_elapsed` and per `player_control_update` call each local player's blob. Kept in a RAM buffer, written to `ms0:/halo/log/pad_NNN.pad` in 64 KB pieces by the I/O thread, so the main thread never waits on the stick.
- **The start:** a recording starts at a map's load (a checkpoint revert or a new map ends it and starts the next file), so a replay starts from the same state. `fixedseed` is on for both.

## Replay (keep word `padplay`, `padplayN` for file N)

- The file is read whole at the map's load; each frame takes its `seconds_elapsed` from it and each call its blob in place of the pad's.
- **Checked as it goes:** the replay logs statecrc's play and seed hashes; a recording made with `statecrc` on carries them, and the replay names the first tick that parts (a build that changes play, or a race not yet found).
- **The bench:** a lab route-like file (`psp/sierra/pads/`, inputs only: no game data) and `sierra-hw ... keep="padplay ..."`; the stretch's window picked from the recording's own heaviest seconds (its metrics lines).

## Proof

1. The emulator: record a run driven by an input script, replay it, statecrc identical (play and seeds) to the end.
2. The PSP: Sheepdog01 records a fight; the rig replays it twice; play and seeds identical; then the same recording replayed on a build with a play-changing switch parts where expected.
