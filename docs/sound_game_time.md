# Sound game time: game logic that waits on sound playback

The lab's B98 (b) and B91, design first (the lead, 2026-11-18 12:0x; the
lab's decisions.md 12:0x). Status: built (FIXES_SOUNDGAMETIME) and on by default since its proof (the lab's findings sgtproof, 2026-11-18): keep word `nosoundgametime` turns it off.

## The question

The game state must not depend on how far the mixer has played a sound. The
mixer runs on the ME, and its progress follows real time on the PSP and the
emulator's instruction clock in the emulator. If the game state does depend on
it, two builds with different code layout or timing play differently even
with the same seed. The lab saw two cases:

- B98 (b40 seed 40): some builds fault and others don't. Their game state is
  identical through the tick before the fault, except objects' non-play words.
- B91's looping-sounds record (b30 seed 1, tick 532): the record parts between
  builds whose code differs only in layout. Play and both seeds stay
  identical.

Findings: b98part1, b98watch3, loopwatch1, loopwatch4.

## What each check waits on [D]

### Conversation lines (ai_communication.c, ai_conversation_line_perform)

These already run on game time. **No change is needed for them.**

| Check | What it reads | Set from |
|---|---|---|
| `sound_scripted_dialog_is_playing()` (a line waits while another scripted dialog plays) | `game_time < sound_manager_globals.game_time_when_no_scripted_dialog_will_be_playing` | `sound_impulse_new` for the scripted-dialog classes: `game_time + 30 * longest_permutation_length / 1000 + 10`, taken from the sound's tag |
| `scripted_sound_time(line sound) == 0` (an unspatialized line has finished) | `definition->scripting_time - game_time` | `scripted_sound_new`: `game_time + longest_permutation_length * TICKS_PER_SECOND / 1000`, from the tag |
| `unit->unit.speech.current.priority != yell` (a spoken line has finished) | the unit's speech state | `unit_speak`: `sound_timer = longest_permutation_length * TICKS_PER_SECOND / 1000`, from the tag; counted down per tick in unit_dialogue.c |

All three take the line's length in game time from the sound tag's own
duration (`longest_permutation_length`) and count it on the game clock. The
original's line timing is therefore already independent of playback. One
caveat: `game_time_when_no_scripted_dialog_will_be_playing` sits in
`sound_manager_globals`. That is not game state: it isn't saved, reverted or
hashed by statecrc. Even so, it is set only from game time and tag data.

**Correction to the lab's B98 notes (b98watch3, 11:4x):** they said a
conversation's end follows sound playback. Read further, it doesn't. B98's
build-dependent trigger is therefore not named by this. Fix (a), FIXES_B98,
keeps the fault from happening whatever the trigger is.

### Looping sounds (game_sound.c, update_potentially_audible_looping_sound)

These do follow playback. The game state takes `sound_refresh_looping`'s
answer (sound_manager.c):

- **Stopping** (not audible, or `_looping_sound_refresh_stop`): the answer is
  TRUE once the sound manager's loop has `component_sound_count == 0`, that
  is, once its last component sound has finished in the mixer. The count drops
  in the sound's end callback (sound_manager.c, `component_sound_count--`).
  On TRUE the game state sets the looping sound `inactive`, or deletes it if
  it is unattached. Until then it stays `deactivating`.
- **Playing a fake impulse sound** (`_looping_sound_fake_impulse_sound_bit`):
  TRUE once `ordered_sounds_finished` is set and the components have drained.
  The game state then goes `inactive` or deletes the sound.
- With the sound system off (`!sound_is_active()`), the answer is fixed:
  TRUE when stopping, FALSE otherwise. That case is already deterministic.

So the record's `state` field, and whether a slot is still alive, change on
the frame the mixer finishes a fade or a one-shot. That is the field loopwatch4
pointed at [U: the values themselves are still unseen].

## The design: soundgametime

The keep word is `soundgametime`. The group is `FIXES_SOUNDGAMETIME`. It
starts off by default, and the default flips once its proof holds (the lab's
call).

- The **sound side is unchanged.** `sound_refresh_looping` is still called
  exactly as now, so sounds start, fade and stop as before. When sounds start
  is not touched.
- The **game-state side stops taking the mixer's answer.** It takes a
  game-time answer instead, from the tag's own durations:
  - **Stop:** at the tick the game state first asks to stop a looping sound,
    it records a game-time deadline: now plus the stop's fade in ticks. That
    is the `fade_time` asked for, or else the longest
    `track->fade_out_duration` over the definition's tracks, or 0 when no
    track fades (a stop sound or a cut). The answer is TRUE when game time
    reaches the deadline. That is when the original's fade ends by its own
    numbers.
  - **Fake impulse:** a deadline is recorded at the start, from the start
    sound's `longest_permutation_length`. TRUE comes when game time reaches
    it.
  - The deadline lives in the looping sound's own datum, so it is game state:
    saved, reverted and hashed with it. For that it needs a field. The record's
    `last_audible_frame_index` sits beside `state` and is 4 bytes, but it is
    in use. So either the datum grows by one long (`game_looping_sound_datum`,
    0x34 to 0x38; MAXIMUM_GAME_LOOPING_SOUNDS x 4 bytes of game state), or the
    deadline goes in a parallel game-state array allocated with the record.
    The choice is made at build time; growing the datum is the simpler proof.
- With the word off, the original's answer is used, byte for byte.

**Why the original's timing is kept:** the deadline is the same length the
mixer plays, the tag's fade or permutation length. A sound therefore goes
inactive at about the frame it would have (to within the mixer's buffering).
It does so on the same tick in every build. Nothing a player hears changes,
because the sound side is untouched.

**Multiplayer:** looping sounds aren't on the wire. The client runs the same
game-state logic on its own, so this is safe in every role.

## Proof

- **statecrc identical between two builds of different timing, both with the
  change on, with the looping-sounds record no longer excluded:**
  - fc70e7ff against 966d2903, the record's own parting at tick 532, each with
    FIXES_SOUNDGAMETIME;
  - the B98 pair, 256976cc against 709bdb84, each with the change.
- **The outcome census:** the lab's aioutcome census batches on b30 and b40,
  off against on, as AIFAR's were.
- **Strict:** 0 sites.

## Result (2026-11-18) [E]

The lab's sierra-batch 44427343964 (sgtproof1) and 44431025474 (sgtproof2), findings sgtproof:

- **The B98 pair, 256976cc and 709bdb84, each with the group (b40 seed 40, statecrc).**
  - Word on: the looping sounds record and game sound globals are identical at all 544 checkpoints to 16,290.
  - Word off: the record parts at 8,340, 12,150, 12,240, 12,930, 13,770 and later.
  - Play and both seeds are identical in every run. What still differs is B91's other class: objects' non-play words, particle and hud unit interface.
- **On against off in one build.** Play and both seeds are identical at all 16,800 ticks (b40) and all 600 ticks (b30). Only the sound records (the looping sounds record and game sound globals) and objects' non-play words differ.
- **handb94aa's pair, fc70e7ff and 966d2903, with the group added.** It didn't part even with the word off. Adding code to both sides erases its parting, as before (loopwatch1). So it proves nothing either way.
- **Strict:** 0 sites on b30 (four pair runs and the tock's build).
