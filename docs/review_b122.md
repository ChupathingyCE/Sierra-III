# Review: build 122, every change since build 121

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b122 replaces b121 for testing: it carries all of b121 (read `review_b121.md` for those eight changes) plus the six below. Changes 3–6 are off by default, built so one build can A/B them (your rule, 2026-10-14: nothing waits). The long versions are `review_b122_posing.md`, `_merays.md`, `_aihalf.md`, `_farrate.md`, `_look.md` and `_vehcam.md`. Labels: [M] measured on a PSP (here the PSP-3000 rig), [E] emulator, [D] read in code, [U] inference.

Branch `w122`. Start from b121 (`49d0938`).

| # | Change | Kind | Default | Off word |
|---|---|---|---|---|
| 1 | Perception's line-of-sight rays cast on the Media Engine | exact speed (ME phase 1) | on | `meraysoff` |
| 2 | Cheaper posing: the root's second check skipped, animation requests answered inline | exact speed | on | `posingold animlockcall` |
| 3 | Far AI perception at half rate | behaviour: your A/B | **off** | on: `aihalf` |
| 4 | Far, unseen moving machines at half rate; half-rate posing for far, unseen, resting objects (campaign only) | behaviour: your A/B | **off** | on: `machinesfar`, `posehalf` |
| 5 | c40's see-through parts on the VFPU; d20's light merging; foliage two-pass | look: your A/B | **off** | on: `seethroughvfpu`, `lightmerge`, `foliagetwopass` |
| 6 | Cut pieces lost to a full cut buffer counted; a bigger cut buffer | investigation (the Pelican wall) | count on; buffer **off** | on: `cutroom` |

---

## 1. Perception's rays on the Media Engine (`b8b7726`, `4bc4f00`)

**What.** b121 queues every actor's line-of-sight ray at the start of the AI's update. Now the main CPU casts the first 4 itself and hands the rest to the ME in jobs of up to 16, then updates its first actors while the ME casts them. Each answer is taken only if nothing moved since (b121's epoch count).

**Key lines.** `psp_fixups.py`, `FIXES_122` `#merays` pairs:
- an ME-side copy of the ray's top four functions (`ai_test_line_of_sight`, `collision_test_vector`, `object_test_vector`, `collision_model_test_vector`) that writes only its own stack and its answers;
- the five functions that change what a ray reads wait for any job still out.

`psp_me.c`: the job type `ME_JOB_PERC_RAYS`, `ME_RING_ALL_OF_RAM` (the main CPU writes back its whole cache before the push; the ME drops its whole cache), a 24 KB stack allowance for this job.

**Why.** The rays are ~40% of perception, and perception is the AI's largest part. The main CPU waits for each ray; the ME was idle.

**Proven.**
- Emulator [E]: 0 of 270,940 ME answers differing; play and seeds identical on d20 and a50; no system calls in the ME's work (lab 38560105583).
- **The rig, the real ME** [M] (lab 38567183309): 0 of 31,940 checked answers mismatched, 0 failed jobs, 0 fallbacks. **a50's tick 1.0–1.8 ms shorter (−3.8 to −5.7%).**
- **b30 on the rig** [M] (lab 38577715595, the b113 route): **the flight's tick 61.0 ms against 67.7 (−6.7 ms, −9.8%)**, the cinematic −3.3 ms (−5.5%); the landing and beach −2.0 to −2.3 ms, within that stretch's spread (b30's play varies there).

**Default.** On; exact and measured. `meraysoff` puts the rays back on the main CPU.

## 2. Cheaper posing (`8d04c19`, `c981570`, `0aa3f58`)

**What.**
- The root node's matrix was checked for validity twice in a row with nothing between; the second check is skipped.
- `psp_animation_data` answers a repeated request for the same animation range itself, instead of calling the animation cache.
- A root-frame memo was also built, exact but not faster, so it stays opt-in (`posingmemo`).

**Why.** Posing is 17.5% of c20's tick instructions (the Flood's streamed animations), and 96% of c20's animation requests ask for the range asked just before [E].

**Proven.** Play and seeds identical on c20, a50, d20 [E] (lab 38559154038, 38560000156). **The rig: c20's tick 0.2–0.3 ms shorter (−0.7 to −1%)** [M] (lab 38567141001).

## 3. Far AI perception at half rate (`aihalf`, off)

**What.** A far actor skips its one look at a known unit (prop) every other tick, staggered so half rest each tick. Never for an actor within `aihalfdistN` (15 units) of the player, with its target in weapon range, recently damaged, or in a zoomed player's view (`aihalfview`: in any view). **What you'd notice:** a far actor notices new things up to a round of its props later (~0.2 s for one that knows 6 units). **Proven** [E]: no crash or frozen AI on b30, a50, c20, d20; a50 −0.29 ms a tick in the cache model; on the PSP its saving is smaller now the rays are on the ME [U]. The ME-late variant (B5) wasn't built: the rig shows the main CPU waits only ~17 µs a job for the ME.

## 4. Far machines and half-rate posing (`machinesfar`, `posehalf`, off; campaign only)

**What.** `machinesfar`: an always-moving machine far (6+ units), outside the player's view cone and untouched updates fully every other tick; its position still steps every tick, so it stays on its path. `posehalf`: a far, unseen, resting family's node matrices are made every other tick. Both off in any network game, co-op or cinematic. **Proven** [E]: no crash; **but no gain yet**: in the windows measured almost nothing qualified, and the tests themselves cost +0.2 to +0.6% of the tick. Worth a rig look only after the test is made cheaper.

## 5. The look calls (off)

- **`seethroughvfpu`** (c40): multi-node see-through parts placed on the VFPU. Not one pixel differs over 24 shots; see-through models −3.2% of their time [E]. A speed question only.
- **`lightmerge`** (d20): the light pass's distance-band draws merged into one. The ceiling/wall light seam is thinner; the light pass costs +3.2% [E]. A look call, not a speed-up; and tell me whether this is the merge you meant.
- **`foliagetwopass`** (c10, b30): alpha-tested plants get a second blended pass: softer, anti-aliased edges; c10's frame +13% [E]. The biggest look change and the biggest cost.

## 6. The Pelican wall (`cutroom`, off)

**What.** Inside a cabin the walls are inches from the eye, so their triangles are cut on the CPU, into a 192-corner buffer. On the Pelican bench it overflows ~9 times a frame and the cut pieces were dropped silently [E] (a new log line counts them: "cut pieces lost"). `cutroom` gives a 768-corner buffer from the heap (33.8 KB); with it, 0 lost. The near plane isn't the cause: it stays at the game's own on the bench [E]. **Not yet proven to be your missing wall** [U]: no emulator shot faced the wall. **The test:** b30's Pelican bench with preset 13 against preset 1, looking at the wall behind you.

## The merged build

- **1 and 2 together are exact** against both off: play and both seeds identical at every tick on d20 (22,615), a50 (37,681), c20 (28,081); `perccheck posingcheck` on a50: 0 differing [E] (lab 38578895499).
- **The off words leave play as it was**: the merged build against 53d8070 (before 3–6), defaults: play and seeds identical at every tick on d20 (24,452), a50 (39,852), c20 (29,765) [E] (lab 38590465463).
- The multiplayer build compiles (the PGO batch's `mp-build` run). AI runs on the server only, so change 1 never runs on a client; change 2 is bit-identical [D].
