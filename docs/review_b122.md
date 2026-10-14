# Review: build 122, every change since build 121

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b122 replaces b121 for testing: it carries all of b121 (read `review_b121.md` for those eight changes) plus the two below. The long versions are `review_b122_posing.md` and `review_b122_merays.md`. Labels: [M] measured on a PSP (here the PSP-3000 rig), [E] emulator, [D] read in code, [U] inference.

Branch `w122`. Start from b121 (`49d0938`).

| # | Change | Kind | Default | Off word |
|---|---|---|---|---|
| 1 | Perception's line-of-sight rays cast on the Media Engine | exact speed (ME phase 1) | on | `meraysoff` |
| 2 | Cheaper posing: the root's second check skipped, animation requests answered inline | exact speed | on | `posingold animlockcall` |

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
- **The rig, the real ME** [M] (lab 38567183309): 0 of 31,940 checked answers mismatched, 0 failed jobs, 0 fallbacks. **a50's tick 1.0–1.8 ms shorter (−3.8 to −5.7%).** b30's beach A/B is running (lab 38577715595).

**Default.** On; exact and measured. `meraysoff` puts the rays back on the main CPU.

## 2. Cheaper posing (`8d04c19`, `c981570`, `0aa3f58`)

**What.**
- The root node's matrix was checked for validity twice in a row with nothing between; the second check is skipped.
- `psp_animation_data` answers a repeated request for the same animation range itself, instead of calling the animation cache.
- A root-frame memo was also built, exact but not faster, so it stays opt-in (`posingmemo`).

**Why.** Posing is 17.5% of c20's tick instructions (the Flood's streamed animations), and 96% of c20's animation requests ask for the range asked just before [E].

**Proven.** Play and seeds identical on c20, a50, d20 [E] (lab 38559154038, 38560000156). **The rig: c20's tick 0.2–0.3 ms shorter (−0.7 to −1%)** [M] (lab 38567141001).

## The merged build

- Exactness of both together against both off, on d20, a50, c20: lab batch `w122exact`.
- The multiplayer build compiles (the PGO batch's `mp-build` run). AI runs on the server only, so change 1 never runs on a client; change 2 is bit-identical [D].
