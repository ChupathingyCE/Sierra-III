# REDOSEED: each actor's thinking draws from a seed of its own

`FIXES_REDOSEED` in `psp_fixups.py` (after `FIXES_AIME2A`), on by default. Keep word `redoseedold` puts back the old way. Campaign only: AIME is off in the multiplayer build, so the group does nothing there.

## The problem (B91)

AIME thinks every actor's next tick ahead, in one pass at the end of the tick before (`docs/aime_me.md`). Before this change the pass drew every actor's thinking from **one running seed** (`psp_aime_seed`), actor after actor. An actor the pass couldn't finish thought again in the tick, and drew from **the game's seed**. That covers an actor handed back at a world writer or an owed compute (1b's stop), the actors a late job never reached, and those after the job's stack guard.

Whether an actor is handed back can follow the clock. Examples: whether a frame-side reader settled an object's owed vislazy compute before the pass read it, or how long the ME's job ran. A hand-back then moved two streams at once:
- the game's seed, by the redo's draws, and with it every later draw in the game (a shot's spread at b30 tick 2,061 in the lab's B91 chain);
- the pass's own seed for every later actor, since the stop puts the seed back to where the handed-back actor started.

So two builds that differ only in timing (unreached code, a keep word that only adds instructions) could play two different games. [E: the lab's b91a-j]

## The change

An actor's thinking for a tick now draws from a seed made from three things: the map's root seed, that game time, and the actor's datum index (`psp_aime_actor_seed`, mixed so that neighbouring indices and ticks start far apart in the engine's sequence).
- **The root** is the pass's seed as the map's first pass (or a reverted game's) took it from the game's seed, as before.
- **In the pass** (on the ME, or the main CPU's pass), `psp_aime_seed` is set to the actor's seed at the start of its thinking.
- **In the tick** (a redo, or an actor the pass never reached), the thinking draws from `psp_aime_redo_seed`, set to the same value. This switch covers the thinking only: from `actor_update`'s thinking block to `actor_action_update`. It applies on the game's thread alone (`sceKernelGetThreadId`), never on the ME, so the sound's thread or other work in the meantime draws from the game's seed as before. The general update and everything after the thinking draw from the game's seed in both cases, as they did.
- **Before the map's first pass** (or a reverted game's), the tick's thinking uses the game's seed as before. The root there would still be the last map's or era's.

The thinking's draws are now the same wherever it runs. No other actor's stream, and not the game's, depends on whether an actor was handed back.

**What it doesn't do:** a redo still thinks a tick later than the pass would have, so after the tick's earlier steps. Where that world differs, the actor can decide differently, and play can still part. The seed is no longer the amplifier, but that path remains (below).

**Why not exact with the old way:** an actor's draws were tied to every actor before it in the pass. No form that keeps the old stream can stop a hand-back moving the actors after it. The change is "plays the same" (the AI's thinking draws other numbers from the first tick: play differs from the tock at tick 83 on b30_leg1). Its exactness check is the same build with `redoseedold`.

The tick's owed set (B91's v1/v2 on its own branch, `tickowedold`) is left out. It only lowers how often the frame's readers change a hand-back, it didn't close the split on its own, and it makes owed computes twice. The seed closes the channel for every trigger.

## Proofs (emulator [E]; the lab's findings `b91_redoseed_*`)

- **One build, the word sets that split it** (b91k, b91n; b30_leg1 to 4,500 ticks, `padplay fixedseed fixedticks statecrc`, legendary init):
  - With `redoseedold` the build plays two games, parting at tick 2,061: none, 2014 and 2030+920 against 2030, 2014+920 and 2040.
  - With the fix all six sets play one game: play and both seeds are identical at every tick.
  - d20_play1 to 9,000 ticks with `arrayfullgo` is identical with and without a word.
- **Across builds** (b91l): the fix on the tock's layout, on the text layout w091rb_tgc90, with `PSP_DEADDEBUG=0`, and with posing B off by keep word (`boxposeboff`) all play one game, with and without stateobjects2014. Without the fix, the tock and the fix's build with `redoseedold` part at 2,061 (B91 itself).
- **Forced late passes** (b91n, `aimelateN`, a test word): the hand-backs a late job makes still part play with the fix, but later.
  - `aimelate30`: play parts at 1,264 with the fix (the seed follows at 1,559), and at 794 the old way (the seed first, at 785).
  - `aimelate7`: play parts at 89 with the fix, 79 the old way.

  On the PSP a late job is waited for (`aimewait`), so these happen only past the wait's bound. What remains there is the redo's world, not its seed.
- **Sheepdog01's recordings no longer track with the fix** (b91m; replayed to their ends with their heads' init, `padplay fixedseed padend`; each checked against his own log, projects/sierra/tools/progresscheck.py):

  | Pad | Tock: tracks Sheepdog01 to | Fix: parts from Sheepdog01 at |
  |---|---:|---:|
  | b30_leg1 | 23,385 | 2,055 |
  | c20_leg1 | the end (63,556) | 3,236 |
  | c40_leg1 | the end (66,761) | 12,999 |
  | b40_leg1_part | 26,187 | 3,490 |

  He recorded them on builds with the old stream. The fix changes every actor's draws from the first tick, so the AI plays another fight and the replay leaves his path. With `redoseedold` they track as on the tock. So turning the fix on means re-recording his pads (or replaying them with `redoseedold`). That is his call: determinism against the recordings he has.
