# aimeaudiolook: audio mixed at a long job's looks near its bound (B60)

On by default; off word `aimeaudiolookoff`. Media Engine only.

## The bug

On c10, with six Covenant placed beside the player, the ME stopped for the session: "a job (type 1) not finished in 40 ms". The main CPU gives an audio block 40 ms and then stops the ME. The ME wasn't stuck. It was busy.

The ME's debug stash at the late block (`mestash`, recon build) [M]:
- the AI job (slot type 15, aime2a) was in hand;
- its place was step 11, the obstacle-avoidance search's step (`path_iterate`, aimeobslook's look);
- it had made no yield between actors for 42 ms. The last audio block had been mixed 44 ms before.

So one newly placed actor's think, its path build, held the ME past the audio's bound on its own. The AI job's looks inside an actor test the collection's word and, with `aimeringlook`, serve the ring, but never audio. That was deliberate: the first form of aimeringlook mixed audio at every look, and on d20 that pushed the word's answer past its 20 ms when the stretches between looks still ran up to ~24 ms. Audio was left to the yield between actors.

The replay wasn't the holder. Under `geinflight` it serves audio every record: the audio's worst wait there stayed 6.5-9.6 ms beside 31-54 ms render jobs [M].

## The fix (psp_me.c)

- `me_audio_due(now)`: the audio slot's request is noted the first time a look sees it. It is due once ~4 ms of the ME's clock (`AUDIO_LOOK_DUE_CYCLES`) have passed since then. The ME can't read when the main CPU pushed it, so the wait counts from the first look that saw it.
- `psp_me_yield_inner(ring)`: the AI job's look. A due block is mixed there (`me_audio_now`), timed by the census as the yield's audio (step 7, as between actors). The ring is served after it only when `ring` (aimeringlook) is on, so the audio look also works with `aimeringlookoff`.
- `psp_me_ring_look`: the light pass's look (lightringlook), the other ring-only look inside a long job. A due block is mixed there too, unless the AI job's word is up (a render job nested in the AI job's yields can't see the AI's mark, so it reads the word itself).
- The heartbeat logs `ME: audio blocks mixed at long jobs' looks, near their bound (aimeaudiolook): N in the AI job's actors, M in the light pass`.

psp_fixups.py: `psp_aime_ring_yield` calls the inner yield when either look is on. As before, it never calls it once the word is up. `psp_aime_audio_looks` is read at launch beside `psp_aime_ring_looks`.

## Why the bounds hold

- **The audio:** in the worst case a block is first seen one stretch after its push and mixed one stretch after it falls due: 2 stretches (≤~10 ms each on d20, ~6 on c10), plus 4 ms, plus the mix (2.5 ms on c10, 5-7 on d20). That's under 40 ms.
- **The word:** no mix starts once the word is up. The word is tested right after the look (at perception's props, at its next look, ~2 ms on), so a mix delays its answer by about the mix alone. That's inside its 20 ms.
- **Cost where nothing is late:** two uncached reads (the slot's request and done) at a look that runs at most once a ms.

## Proof

- **Exact [E]:** play and both seeds are identical to the tock at every tick on the b30 route (4,500 ticks), c20 (3,600), d20 with the flashlight (2,700) and d20 with aimelate3 (2,700). Strict mode found 0 sites. The emulator's simulated AI job runs on an eDRAM stack, so the look runs there. But the emulator's audio never sets a slot request, so nothing is mixed: this is an exactness and build proof.
- **PSP-3000 pair [M]:** c10 with the placement, on against `aimeaudiolookoff`, 2 runs a side. The ME survived on both sides in this pair; the off side's stop had come in 2 of 3 placed runs on the older build. The audio's worst wait was 10.5 and 11.4 ms on, against 34.2 and 32.6 ms off (the bound is 40 ms). The off side's worst came in c10's opening, before the placement. After the placement it was ≤8.6 ms on and up to 27.7 ms off. 9-11 blocks a run were mixed at the AI job's looks and none in the light pass. The frame is a tie: opening 36.05/36.02 ms on vs 36.42/36.12 off; placed 23.91/24.14 vs 23.83/24.29. The word's worst answer was 0.3-2.3 ms on and 2.5-3.0 off.

## Left

- One actor's think on the ME runs ≥42 ms when it is placed among others beside the player (its obstacle search). That is the engine's own work, the same on either CPU, so it is a capacity cost, not a fault.
