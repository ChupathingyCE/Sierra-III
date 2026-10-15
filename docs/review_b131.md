# Review: build 131

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b131 is b130 (`review_b130.md`) plus the branches below, and its own profile (`campaign-b131`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch | What | Kind | Default | Words | Review |
|---|---|---|---|---|---|
| `spherebound132` | The level sphere walk passes over surfaces whose bounding sphere is out of reach | exact speed-up | on | `surfaceboundoff` (off); `surfacetestcheck` (check) | §1 |
| `mepose131` | Next tick's base poses made ahead, on the main CPU or the Media Engine: the first slice of phase D | exact speed-up, opt-in while the PSP-3000's hang is open | off | `poseahead`, `poseaheadme`, `poseaheadcheck` | §2 |
| `spherebound132` | The pose job's parts timed on the PSP | instrument | off | `posetime` | §3 |

## Release check

Lab 38803195090, against b130 as built: play and both seeds identical at every common tick on d20 (14,856), a50 (25,231) and c20 (19,285) [E]; the whole state differs from the first tick, as it does whenever a build's allocations change (surface bounds' 40 KB table). The logs read whole. b30 and c40 shots right; the multiplayer build plays Blood Gulch. Built from `646c3f6` (lab sierra-build 38803284593).

## §1 Surfaces out of reach passed over (`psp_fixups.py` FIXES_132 `collision_bsp.c#surfacebound`)

**What.** When a biped moves, the game asks which level surfaces touch a sphere around it (`collision_get_features_in_sphere`). The walk finds the BSP leaves the sphere reaches, then tests every surface in them: it walks each surface's ring of edges to see whether a vertex, an edge or the face itself is inside the sphere. Now each surface gets a bounding sphere the first time it's tested (the mean of its ring's vertices, then the farthest vertex from it), kept in a 2,048-entry table. A surface whose bounding sphere is farther from the query sphere than both radii, plus a margin, is passed over without walking its ring.

```c
reach = bound->radius + data->radius;
reach += reach * 0.001f + 0.0001f;
...
if (delta_x * delta_x + delta_y * delta_y + delta_z * delta_z <= reach * reach)
	return FALSE;        /* close enough: test it as before */
psp_sb_passed++;
return TRUE;             /* out of reach: the test would add nothing */
```

**Why.** A cache-miss lever: the sphere walk is the tick's largest source of data misses on a50 (17% of them), and most of its cost is walking rings of edges and vertices scattered through memory. **Filler, not the headline:** under today's lever rule its ceiling is under a millisecond (the cache model: −0.6 ms a tick on a50, −0.85 ms on b30); it was built and proven before the rule, and ships because it's free. The recon (lab 38790928679 [E], every test counted): **85% of a50's surface tests are out of reach**, 67% of b30's, 61% of d20's, 33% of c20's.

**Exact** [D, E]. A surface's vertices and edges lie inside its bounding sphere, and so does the point the sphere's centre projects onto when the test finds it inside the face (that point is less than the sphere's radius from the centre, since the sphere straddles the face's plane). So a sphere farther away than both radii touches none of them. The margin (a thousandth of the reach, plus 0.0001 of a world unit) covers the rounding of these sums at the level's coordinates. In the recon, none of 17 million out-of-reach surfaces would have been added or hit. Checks (lab 38793200211 [E]): every walk compared with the engine's own (`surfacetestcheck`), 0 of 197,649 differ on a50 and 0 of 372,128 on b30's route; play and both seeds identical to `surfaceboundoff` at every tick on a50 (19,291) and d20 (11,530).

**Memory.** 40 KB, allocated at the first walk; if there's no room the log says so and the walk tests every surface as before. Only the level's collision BSP uses the table (objects' BSPs would empty it); it's emptied when a map loads or the BSP changes (`psp_sphere_memo_forget`).

**Worth** [E, cache model, to game time 120 s; lab 38793200211]: a50's tick 20.46 → 19.89 ms (−2.8%: 5.2% fewer instructions, 2.6% fewer data misses), b30's 42.25 → 41.40 ms (−2.0%). In play, 88% of a50's surfaces about to be tested are passed over; the table makes a bounding sphere for about one test in six. The PSP's figure comes from tonight's ladder.

## §2 Next tick's base poses made ahead (`psp_fixups.py` FIXES_131 `objects.c#poseahead`, `model_animation_definitions.h#poseahead`; `psp_me.c` `ME_JOB_POSES`; `psp_anim.c`)

**What.** `docs/me_poses.md` has the plan and the results. As each tick poses an object, the guess for its next tick (the same animation, the frame after) is listed. After the tick's objects, the poses are made ahead: on the main CPU (`poseahead`) or as one Media Engine job (`poseaheadme`) while the frame is drawn. The next tick copies a pose whose guess held instead of decoding it.

**Exact** [E]: 0 of ~1.3 million poses checked differ (c20, a50, d20, b30); play and both seeds identical on a50, d20 and c20 (lab 38782815918); no system call in the ME's work, no line shared with the main CPU (lab 38786384396).

**On the PSP.** The PSP-1000 on b30, eight runs (lab 38784132264, 38789615962 [M]): the ME made 99.997% of the poses with no job failing; the flight's tick fell 1.15 ms (−2.0%, four runs a side), the cinematic's 0.7–1.8 ms, and the beach's and bunker's not at all (in the noise). Less than the ~2.2 ms the decodes cost: the main CPU still copies each pose back cold and flushes its cache twice a tick, and the ME uses the shared bus while the frame is drawn (your question today; §3 measures it). The PSP-3000, on c20 with the check word, stopped answering after the launch (lab 38784036485). The emulator doesn't show it; its log is on the 3000's stick. **So it stays off by default.**

## §3 `posetime` (`psp_fixups.py` FIXES_133 `objects.c#posetime`)

**What.** With the pose job on, times the main CPU's parts a tick: the collect (the wait and the cache drop), the takes (the copies), the listing, the making and the push alone; logged every 900 ticks. Off unless asked for; the clock reads add a little to each part.
