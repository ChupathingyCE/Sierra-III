# The pad recorder: Sheepdog01's fights as repeatable benches (design, 2026-11-08, master)

**Why** (Sheepdog01 2026-11-07 23:20-23:30): the benches were mostly cinematics and routes, and a route replays the player's positions, not his inputs, so his own fire, grenades and vehicles never happen in it. Combat with vehicles and many AI is the game's normal state, and the parity budget (`parity_budget.md`) has to be priced there.

**What makes it possible:** the simulation is deterministic given the same start, the seed and the inputs each tick. In the emulator that's proven every batch (statecrc). On the PSP it held only once two races were fixed (tock162 edc2d39 and 74cd771: the AI's pass on the ME read tables the record rewrote while it ran): two b30 runs then play the same through the beach fight (lab sierra-hw 42485968207).

That run had the bench guards `nomouth aimewait readswait`; Sheepdog01's recordings run without them, and their replays parted again (b30 at the landing). Two more paths followed the clock, both fixed so a recording replays without guards [E]:
- **The lip sync** (`FIXES_MOUTHCLOCK`): a speaker's mouth aperture, which moves its nodes, was read at the mixer's play time; a line slowed by its read (`readswait`) or left silent (`nomouth`) moved play. Now each line's mouth data is read from the tick it started, by the frames' time steps (the ones recorded here), its draws from a speech seed under `fixedseed`. `mouthold`: the mixer's clock.
- **A late AI job on the ME** was asked to end at once, and what it handed back thought in the tick from the game's seed (forced late passes, `aimelate1`, part play at once). The collection now waits for the whole job, up to 200 ms, and asks it to end only past that. `aimelateold`: asked to end at once.

## Recording (keep word `padrec`)

- **Where:** `handle_one_player_input` (`game/player_control.c`), right after `get_local_player_input_blob`: the `input_blob` the game acts on, after the pad, the sensitivity and the inhibitions, so the replay doesn't depend on the stick's dead zones or the control settings.
- **The time step too:** each frame's `seconds_elapsed` (`main.c`), which decides how many ticks the frame runs. Recorded with the blob, so a replay runs the same ticks with the same inputs however fast its build draws. No `fixedticks` needed to record: Sheepdog01 plays at real speed.
- **What's written** (v1, `psp/src/psp_pad.c`): per frame its `seconds_elapsed` ('F' and 4 bytes) and per `player_control_update` call each local player's blob ('B', the player, 32 bytes), kept in a 64 KB buffer and written to `ms0:/halo/log/pad_<map load>.pad` when it fills (~1 KB a second of play, so a write about once a minute, on the main thread in v1). `padlog` also logs the bytes in hex, so an emulator run's recording comes back with its log (the results take no binary files). Not yet: a header naming the map, the build and the play-changing keep words.
- **The menus** ('U' and a byte, 2026-11-12): the pause menu's work isn't in the blobs, so a hand run that paused or restarted the level couldn't replay (c10_play1: the replay ran the paused frames' ticks and never restarted). Each frame right after `process_ui_widgets`, `main.c` hands `psp_pad_menu` the state (bit 0 paused, bit 1 a revert asked, bit 2 a restart asked); a change is written between the frame's 'F' and its 'B's, and a replay reads it at the same point and sets the pause and the flags again, which `main.c` acts on at the next frame's start as it did live. Flags the tick sets (a scripted revert, `revertatN`) are cleared before this point and replay by themselves. A level restart stays in the same file (the map load count doesn't move). Older recordings have no 'U' and replay as before. Test word `padmenutestN`: while recording, paused at frame N, let go at N+45, the level restarted at N+90.
- **The head** ('H' and 64 bytes, 2026-11-13; Sheepdog01 2026-11-12 23:02, the lead 2026-11-13 13:16): the file's first record: a version byte (1), the game's difficulty (`game_difficulty_level_get`, 0 easy to 3 legendary), and init.txt's lines but its `map_name`, ';' between them, zeros after. A replay reads it and logs whether it plays at the recording's difficulty (a different one parts: `pads/README.md` in the lab). Older files start with 'F' and replay as before, logged as having no head. A hand load chooses the difficulty on the launch menu's maps page (square cycles it, heroic first; select toggles deathless), written to init.txt as `game_difficulty_set` and `cheat_deathless_player 1` before `map_name`; a direct load gave no choice and fell back to normal.
- **The start:** a recording starts at a map's load (a checkpoint revert or a new map ends it and starts the next file), so a replay starts from the same state. `fixedseed` is on for both.

## Replay (keep word `padplay`)

- `ms0:/halo/pad.pad` is read whole at the first map load; each frame takes its `seconds_elapsed` from it and each call its blob in place of the pad's.
- **Checked as it goes:** the replay logs statecrc's play and seed hashes; a recording made with `statecrc` on carries them, and the replay names the first tick that parts (a build that changes play, or a race not yet found).
- **The bench:** a lab route-like file (`psp/sierra/pads/`, inputs only: no game data) and `sierra-hw ... keep="padplay ..."`; the stretch's window picked from the recording's own heaviest seconds (its metrics lines).

## Proof

1. The emulator: record a run driven by an input script, replay it, statecrc identical (play and seeds) to the end.
2. The PSP: Sheepdog01 records a fight; the rig replays it twice; play and seeds identical; then the same recording replayed on a build with a play-changing switch parts where expected.
