# Review: aimeringlook (B50, a late point-physics job rests the ring)

**Branch** master/ringlate, on master/light. **Keep word** `aimeringlook` (off by default until its PSP pair is read). **Touches** `psp/src/psp_me.c`, `psp/src/psp_me.h`, `psp/src/psp_ge.c` (PSP_RECON only), `psp_fixups.py` (FIXES_AIME2A).

## The bug

On d20's opening the frame's point physics (the mepoints ring job, type 12) sometimes wasn't done 10 ms after its push. The main CPU then made every point itself (correct), but the late job also made the ring rest for 1-2 s. All ring work (perception's rays, next tick's poses) went to the main CPU for that time, and the tick ran ~32 ms against ~20 [M].

## The cause [M]

The points job is pushed in `game_frame`, just after the AI's thinking-ahead job (aime2a) is handed to the ME. The AI job serves audio and the ring only between actors (`psp_me_yield_slots`), and one actor's thinking can hold the ME for more than 10 ms. mestash's report of the two late jobs in one PSP-3000 run:

- The ME looked at the ring just before the push, then entered an actor's step 1 (input and perception, with no look inside) and was still there 10 ms later.
- The ME hadn't looked at the ring for 9.1 ms (one actor) when it reached the yield between actors. That yield mixes an audio block (~3 ms) before the ring, so the job missed its deadline.

## The fix

The AI job already looks at the collection's word inside an actor: after perception, after its situation, before each pass of the decision loop, before each firing-position flood and candidate's rays, and between a path search's nodes. Each of these points was chosen because the work there holds nothing shared (no collision walk is open). With `aimeringlook`, the ring is served at those points too, and at the head of each prop in perception's loop, which is the step where the recon run caught the ME.

- `psp_me_yield_inner` (psp_me.c) drains the ring at most every ~1 ms of the ME's clock. It serves the ring only, not audio: the first version also mixed audio there, and on d20 an audio block (~5-7 ms) held the AI job's answer to the collection's word past its 20 ms bound, which stopped the ME in one run of the PSP pair [M]. Audio waits for the next actor, as it did before. A path search looks once a node, and the gap keeps that to one clock read per node. It yields only if a ring job's stack allowance (the points/rays one, 24 KB, plus 16 KB) fits above the AI job's guard. Otherwise it counts a "passed by" and skips.
- `psp_aime_ring_yield` (actors.c fixup) calls it only inside the ME's AI job, and never once the collection's word is up. The drain marks the census's step 7 (the ring jobs), and the actor's step is put back after it.
- The drain is `me_ring_drain`, as in `psp_me_yield`: the jobs run with the AI job's mark cleared, and the drain stops at the collection's word. Ring jobs touch only their own inputs and outputs, so this is exact anywhere a job may call it.
- The rest after a late job stays as the safety net. The deadline is unchanged.

Off the ME (the emulator's simulated job, the main CPU's pass alike) the yield does nothing.

## Instruments

- `mestash`'s report of a late ring job gains a second line: how long before the ME's latest mark it last looked at the ring, and, in PSP_RECON builds, where the long slot job was (the AI's step and tag, or the render record's kind). `psp_me_stash_place` is PSP_RECON only, so default builds don't pay its uncached reads.
- A line each second with `aimeringlook` on: the yields inside actors, the ring jobs run in them, and the looks passed by for the stack.

## Proofs

- Emulator [E]: play and both seeds are the same at every tick (b30 route 4,500 ticks, c20 3,600, d20 swamp 2,700) for the base, this branch, and this branch with `aimeringlook mestash`. This proves the code around the yield. The emulator can't exercise the ME's interleaving.
- PSP-3000, recon build [M], d20 to game time ~550, one run a side: off, 2 late ring jobs; on, 0. On side: 3-148 inner yields a second, 0-10 ring jobs run in them, none passed by for the stack.
- First PSP-3000 pair (audio in the inner yields too) [M]: off, 0 lates in 2 runs; on, 1 late rays job, and in one run the ME was stopped because the word went unanswered for 20.1 ms (the step in hand was a candidate's rays). That pair led to the ring-only version.
- The ring-only version's PSP-3000 pair: see the lab's findings (`aimeringlook` rows).

## Risk

The AI job takes on ring work that used to wait for the next actor, so its own run gets a little longer. After the word it adds at most the one ring job in hand: there is no yield once the word is up, and the drain checks the word before each job. The cache work around each served ring job (a whole-cache write-back and invalidate for the points and rays jobs) now also lands inside an actor, which costs refetches. That cost was already paid between actors.
