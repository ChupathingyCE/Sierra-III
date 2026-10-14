# Review section for build 121: AI perception as a batch

For `docs/review_b121.md`. Same shape as `review_b120.md`: **what**, **key lines**, **why**, **proven**, **default**. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

Branch `aiperc121` (from `w121`, `274f08a`).

| Change | Kind | Default |
|---|---|---|
| AI perception as a batch: unit facts shared, line-of-sight rays queued | exact speed (ME phase 1, proposal C steps 1–3) | on (`percold` off) |

---

## AI perception as a batch (`761f2d4`)

**What.** Two exact changes to AI perception. Both last only for the length of `actors_update`, the loop that updates every actor once a tick.

- **Unit facts, shared.** A unit's head position and centre of mass (two marker lookups each time) and whether it is underwater are worked out once and then shared:
  - its own actor reads them in `actor_input_update`;
  - every actor that knows the unit reads them again in `prop_position_refresh`, and an actor's target is refreshed every tick.

  On b30's beach most actors target the player, so before this the player's head and body markers were looked up once per actor per tick.
- **The ray queue.** Each actor's perception casts one line-of-sight ray a tick, for the prop (a known unit) whose turn it is.
  - At the start of `actors_update`, the queue predicts that prop for every actor and casts all the rays in a row.
  - `prop_status_refresh` takes a queued answer only when every input of its own call matches the queued one bit for bit and nothing has moved since. Otherwise it casts its own ray, as before.

**Key lines.** All in `psp_fixups.py`, `FIXES_121`:

- `actor_perception.c`, before `prop_status_refresh`:
  - `psp_perception_unit_head`, `_center_of_mass` and `_underwater`: the facts, one slot per unit;
  - `psp_perception_queue`: the prediction, which copies `actor_perception_update`'s choice (the first prop whose timer, stepped, reaches last tick's highest);
  - `psp_perception_line_of_sight`: the bit-for-bit match;
  - `psp_perception_end`: the `perception:` log line.
- `actors.c`, `actors_update`: the queue's loop over the same actors the update loop takes. Also the two input calls in `actor_input_sample_position` and `actor_input_update`.
- `objects.c`: `psp_object_epoch++` at the top of five functions:
  - `object_disconnect_from_map`;
  - `object_reconnect_to_map`;
  - `object_detach`;
  - `object_attach_to_node`;
  - `object_compute_node_matrices`.

  Anything that places, moves, attaches, detaches or removes an object, or makes its node matrices, goes through one of these. Every kept fact and queued ray from before the count changed is dropped.

**Why.**
- Every level is bound by its tick, and AI is about 17 ms of b30's 57 ms tick on the rig [M].
- Perception is the AI's largest part. On b30's beach [E, JIT steps, lab 38510147369]:
  - perception is ~2.2 ms a tick in the emulator, as much as the whole rest of the actor update;
  - its line-of-sight ray is ~40% of that (14 rays a tick, ~63 µs each);
  - the object scan is ~26%.
- On a50, perception is ~1.4 ms a tick, of which the rays are 0.74 ms.
- The ray walks the collision BSP and the objects in each cluster it crosses. On the PSP that is mostly data misses, and before this change it ran once per actor, in between that actor's ~60 KB of other AI code.
- Cast in a row, the rays share:
  - the collision code in the instruction cache;
  - the beach clusters' object lists and spheres in the data cache.

  The rays from different actors cross the same few clusters.
- The queue is also the ME's groundwork: a list of fixed-format queries with all inputs known at the start, which is the shape an ME job needs (`research_b1_insomniac.md`, the batch-update recipe).

**Which of the recipe's steps, and why not all of step 1.**
- **Step 1 (group):** done for the rays. Grouping the whole of perception can't be exact: perception draws the global random seed (notice and dive chances, and the dialogue its stimuli start), and so do the stages after it. Running every actor's perception first would reorder those draws.
- **Step 2 (inputs and outputs):** the queue's record (`struct psp_perc_ray`). It holds the two points and their clusters, the mode, the vehicle to ignore and the answer, plus the actor and prop it is for.
- **Step 3 (cut sync points):** the per-actor wait on a ray becomes one batch of rays per tick. Sorting actors by AI state isn't needed for exactness, and it changes nothing here: the rays don't depend on state.

**Proven.**
- **Play exact.** Hands off, on against `percold`: the play hash and both random seeds are identical at every tick on d20 (25,411 ticks), a50 (41,911) and c20 (31,681) [E] (lab 38512442380). That is the bar `itemsawake` and b117–b119's exact levers were held to.
- **Every answer checked.** `perccheck` works out every kept fact and every queued ray again and compares them [E]:
  - b30's beach on the b113 route: 0 mismatched facts in 1,796,579 lookups (1,079,079 kept), and 0 mismatched rays in 158,108 taken (lab 38512442380);
  - d20 over 34,200 passes and a50 over 52,200 passes: 0 and 0 (lab 38515411714).
- **The prediction is nearly always right** [E]:
  - b30: 159,556 rays cast; 158,108 taken (99.1%), 1,274 refused (the call's inputs differed, so it cast its own), 23 dropped because something moved;
  - a50: 97.8% taken; c20: 99.8%; d20: 98.9%.

  Objects changed during `actors_update` in 2 of 52,200 passes on a50 and in none on the other maps.
- **The whole-state hash.** Besides the allocations already known to follow the emulator's speed (the render and collision marks in `objects`, particles, contrails, looping sounds, first-person weapons), the `actor` allocation also differs. Its 64-byte block hashes at d20 tick 120 (`stateblocks120`, lab 38515411714) narrow it to one block: actor 16's `control`, from `secondary_look_type` to `idle_minor_direction`.
  - It comes from the unit facts alone: `percnoqueue` gives the same block, and `percnomemo` (the queue alone) gives none.
  - It isn't a value. `perccheck`, whose answers are all worked out fresh, gives the same block.
  - A `direction_specification` is a 2-byte type, 2 bytes of padding and a 12-byte union. The game fills only the type and the 4-byte prop index before copying the whole struct into the actor (`prop_status_refresh`, "started moving" look: `direction.type = …; direction.prop_index = …; actor_look_secondary(…)`), so 10 bytes of stack come along. Nothing reads them for that type.
  - The facts' extra call level leaves different bytes on that stack [D].
  - **The bytes** (`stateblockraw479`, lab 38517993959). Of the block's 16 words, three differ, and all three are in actor 16's `secondary_look_direction`:
    - the padding after `type` (`094d` against `0000`);
    - the union's second and third words (`e3710010 094dc958` against `00000001 bdb41e74`).

    `type` (1, a prop) and `prop_index` (`f2ad0039`) are the same. `094d…` is a stack address: the bytes are stack.
- **Multiplayer.** The multiplayer build compiles, links and starts. On Blood Gulch it fails at the same place as `w121` itself: the GE's 768 KB display list doesn't fit the heap (`w121a`, lab 38509454132, the same log line). That failure is `w121`'s, not this change's.

**Measured: the cache models** [E] (lab 38512475945).

Setup:
- Hands off, `fixedticks fixedseed`, to game time 120 s (b30: 150 s on the b113 route, through the landing into the beach fight).
- Counts per tick of the tick phase. "Est." is a PSP-1000 estimate: one cycle an instruction plus 72 cycles (216 ns) a miss of either cache, at 333 MHz. The rig weighs it later.

| Map | Instructions | Data misses | Instr. misses | Est. tick, old → on | Change |
|---|---:|---:|---:|---|---:|
| a50 | +0.06% | −2.2% | −2.5% | 23.35 → 23.03 ms | **−0.32 ms (−1.35%)** |
| d20 | −0.29% | −0.9% | −1.1% | 21.87 → 21.71 ms | −0.16 ms (−0.73%) |
| c20 | −0.27% | +0.1% | −0.6% | 23.53 → 23.44 ms | −0.09 ms (−0.40%) |
| b30 beach (alike, not the same) | −1.5% | −3.8% | −3.3% | 46.37 → 45.08 ms | −1.28 ms (−2.8%), see below |

- a50, c20 and d20 play the same on both sides, so their counts compare exactly.
- **b30 can't be compared tick for tick:** its play parts across any change in speed (from tick ~1,326, the dialogue). Its whole-tick figure carries that, and so do its split runs (`percnoqueue` alone came out −7.4%, which is noise of play). The functions the change touches are the fair reading there.

Where the savings come from (b30's beach, per tick):
- `collision_bsp_test_vector_recursive` and `collision_test_vector`, the ray's code: −62,000 cycles. Instruction misses fall 873 → 638 and 679 → 438, data misses 2,827 → 2,599 and 908 → 759: the rays now run in a row.
- `model_get_marker_by_name` and `model_find_marker`, the markers: −39,000 cycles. Fetches fall 42,100 → 29,700: the facts are shared.
- `actor_perception_update`: −26,000 cycles.

Together −130,000 cycles, about **−0.39 ms a tick** (est.). On a50 the same functions give −0.17 ms.

Steps 1–3 pay little on the main CPU alone: about 1% of the tick where the AI is busy. The larger value is the queue itself. It is a fixed-format batch of 14–48 rays a tick with every input known when `actors_update` starts, and its answers are needed only later. That is an ME job's shape: the ME can cast the rays while the main CPU runs the first actors' updates. For scale: every vector test of b30's tick, perception's rays among them, spends ~1.8 ms a tick of the estimate above in those two functions alone [E], before the objects part of each ray.

**Default.** On; an exact speed-up is pre-approved. Keep words:
- `percold`: both parts off;
- `percnoqueue`: the unit facts alone;
- `percnomemo`: the ray queue alone;
- `perccheck`: every kept fact and queued ray is worked out again and compared, with mismatches counted in the log. The run plays with the fresh answers, as `percold` would.

**Campaign and multiplayer.** A client of the distributed netcode doesn't run `ai_update`: the server's actors drive it (`network_actors_drive`, `game.c`), so in a network game against ChupathingyCE's servers this code never runs and the simulation is untouched [D]. The multiplayer build carries it compiled in (above).

**Memory.** 6.7 KB more static data (64 unit slots, 48 queued rays). The heap is 8 KB smaller in a local build: 5,760 KB against `w121`'s 5,768.

## Instruments (recon only)

- `PSP_TICK_STEPS=1` now also splits perception:
  - `prop_status_refresh` by labelled steps (line of sight, visibility, audibility, the weights);
  - `prop_position_refresh` and the danger zone, by their calls.

  This is what sized the lever (lab 38510147369).
- `statecrc` gains `stateblocksN` (the actor allocation's hash by 64-byte block at tick N) and `stateblockrawB` (that block's words). Together they find the field two runs part in when the play hash holds. Both are silent unless asked for.
