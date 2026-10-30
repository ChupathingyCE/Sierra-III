# Review: the stick turns the view by real time (`lookreal`)

**What it fixes.** The look stick felt slow and uneven in hand play. [D] The engine caps a local frame's time at 1/15 s (`main_update_time`: `CEILING(seconds_elapsed, 0.0667)`) and `player_control_update` turned the view by that capped time once a frame. Below 15 fps the game world runs slow, as it did on the Xbox when it dropped frames. But the turn slowed with it, so in real terms a held stick turned at (fps / 15) of its rate: two thirds at 10 fps, half at 7.5. As the frame rate moved between stretches, the turn sped up and slowed down with it. On the Xbox, at its usual 30 fps, the cap almost never applied, so a held stick turned at the tag's rate in real time. This change brings that back: it restores the original, so it is **on by default**, and `lookrealoff` turns it off.

**The change** (`psp_fixups.py`, `FIXES_LOOKREAL`, two files):

- `main.c`: both `main_update_time` and `main_update_time_unthrottled` keep the frame's real length, after the existing pin to [0, 1] s and before the cap, in `psp_look_real_seconds`. In a film it is 0, which means "use the game's time".
- `player_control.c`, `psp_look_time_delta`: the stick's time is that real length, at most 0.25 s, so a hitch or a checkpoint save can't spin the view. It falls back to the capped time when the keep word is off, when time is halted (`time_delta_sec <= 0`), or when the game isn't local: a network client's time isn't capped, so nothing changes there.
- `get_local_player_input_blob`: the turn is split into two parts.
  - The **stick's part** (the look function, the zoom, the stun, the acceleration, then magnetism's friction `input_scale`) is turned by the real time.
  - **Magnetism's part** (`target_angular_velocity * magnetism_scale`) keeps the capped time, because it follows a target that moves in game time.
  - The look acceleration's hold timer (`look_acceleration_time`) counts real time, so the ramp to full speed takes the same time in your hand at any frame rate.
  - Unchanged and still on the capped time: `player_control_angle_step_ticks` (the pitch limits' easing and autolevel, which step by game ticks) and autolevel's tick count.
- When the two times are equal (any frame at 15 fps or faster, or the keep word off), the code is the original expression, untouched.

**Why it's exact with the stick at rest.** At rest the stick's part is ±0. Unmagnetised, the turn is `stick_scale * ±0` = ±0, the same signed zero the original made. Magnetised, `facing_scale * M + stick_scale * (±0)` equals `facing_scale * (M + ±0)` bit for bit, including the sign of a zero result: fp-contract is off and floats are evaluated as floats. So a hands-off route plays the same with it on or off. The exactness pair exercises the new path: under `fixedticks` the game's time is 1/15 s every frame while the real time isn't, so the two differ and the split runs on every frame.

**Multiplayer.** `player_control.c` and `main.c` are shared with the multiplayer build. On a network client `game_connection()` isn't local, so `psp_look_time_delta` returns the game's time and the turn is the old one. The client's frame time isn't capped anyway. A multiplayer review should check:

- that the PSP is never the local-connection host of a networked game (it isn't supported);
- that `facing_delta` still reaches the client's action queue unchanged (`update_client_queue`). The view angles are player input, so a faster turn in real terms is only what the player asked for, never a simulation difference.

**Test words** (no cost when absent):

- `lookstat`: one line per second of real time in play: real and game seconds, frames, yaw turned and yaw per real second, the stick's yaw, and the last look time.
- `frameslowN`: N ms more on every frame, to get slow frames on demand.

**Cost.** One call and a compare per frame on the look path. `main_update_time` gains a store and a cached keep-number test. Both functions change shape, so the campaign profile doesn't match them until it is next regenerated (they are listed in `pgo_mismatch.txt`; neither is hot).

**Evidence** [E] (emulator, a30 route; nub held fully right for 60 s of game time, then partly right; yaw per real second, the ramp left out):

| Frame rate | Off: full stick | On: full stick | Off: part stick | On: part stick |
|---|---|---|---|---|
| 28 fps | 6.12 rad/s | 6.14 rad/s | 0.208 rad/s | 0.208 rad/s |
| 14.5 fps (`frameslow40`) | | 6.13 rad/s | | 0.207 rad/s |
| 7.7 fps (`frameslow100`) | 3.14 rad/s | 6.13 rad/s | 0.099 rad/s | 0.208 rad/s |

- **Rate.** With it off, at 7.7 fps the view turns at about half its rate. With it on, the rate is the same at every frame rate.
- **Exactness.** With the look stick at rest, on and off play the same: the play hash and both seeds are equal at all 6,000 ticks on b30's route (hands off) and on a30's route (forward held, so magnetism can engage). The whole state differs only in `objects` and `hud unit interface`, the parts that follow timing.
- **Strict interpreter.** a30 with the stick held, forward held and slow frames: no bad accesses.
- **Compiles.** The campaign PGO build and a `PSP_METRICS=0` build both compile, and neither changed function is in `pgo_mismatch.txt`.

**By hand.** On b30's beach, hold the nub fully right with the keep word on, then with `lookrealoff`, in the busy stretch (the first fight, where frames drop below 15). With it on, a full turn should take the same time there as on the quiet sand. With it off, it should take visibly longer in the fight. Then try short flicks: with it on, a flick of the same length should turn the same angle wherever you are.
