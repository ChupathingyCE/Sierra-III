# The cache work: loop phasing, prefetch, data layout (draft, 2026-10-20)

The PSP-1000 has 16 KB instruction and 16 KB data caches and no L2; about half of play's cycles wait on memory [M]. This page collects the cache work after `plan_memory_stalls.md`, `prefetch_map.md`, `order_file.md` and `research/research_icache.md`. Labels: [M] measured on a PSP, [E] emulator or cache model, [D] read in code, [U] inference. **Status: the base measured (b30, c20, d20); loop phasing's first footprints in (c20, 30 s) and its score failed its check; `nodeprefetch` and `typehdr` scored in the model (exact both; `typehdr` gains a little, `nodeprefetch` loses) and A/B'd together on a PSP-3000 (no difference the PSP can see, as predicted; `nodeprefetch` off by default, `typehdr` stays on).**

## Where it starts (b30's beach, the cache model, game time 90–170 s) [E]

- Tick 29.7 ms in the model: instruction stall 9.7 (33%), data stall 7.6 (26%), work 12.5.
- The tick refetches its ~70 KB hot code 47–75 times a tick; the misses are capacity, not conflicts (a fully associative cache removes only 16–18%).
- Data misses by region: game state 37% (the object pool alone 16%), tag window 32%, program globals 19%, thread stacks 10%, heap 2%.
- The main thread's stack reaches 280 KB deep [E, the `stacks` line], so it can't move to the 16 KB scratchpad; its misses (~4,200 a tick) stay a layout question for the frames that cause them.

**The base on three maps** (w140's build, b139's profile; the cache model with timing; game time to 170 s on b30's route, 90 s on c20's and d20's openings; a tick's mean) [E]:

| Map | Tick | D-miss stall | D-misses | I-miss stall | I-misses | Prefetch at every site, best window | after its own cycles |
|---|---|---|---|---|---|---|---|
| b30 | 34.75 ms | 8.93 ms | 41,282 | 11.86 ms | 61,734 | 0.38 ms | 0.18 ms |
| c20 | 18.52 ms | 4.17 ms | 19,285 | 8.41 ms | 43,753 | 0.21 ms | 0.12 ms |
| d20 | 18.88 ms | 4.86 ms | 22,477 | 5.21 ms | 27,093 | 0.19 ms | 0.09 ms |

Placing prefetches in the code as it is hides 4–5% of the data stall at best, as `prefetch_map.md` found on b30 alone. Moving up to three memory operations out of each window (reordering, which is a code change per site, not a prefetch) would hide 2.5 ms on b30 and 1.2 ms on c20 and d20: the ceiling for reshaped loops.

On all three maps the node walk holds the top sites: `matrix4x3_rotation_from_quaternion`'s first stores into the node's matrix (both ends of its 52 bytes) and `object_compute_node_matrices`' read of the node's links in the model tag. Together 917 misses a tick on b30, 683 on c20, 430 on d20 (0.20, 0.15, 0.09 ms a tick at full hide). Next on c20 is the animation's keyframe read (`animation_get_keyframe_rotation`, 158 a tick).

Data misses in b30's tick by allocation: the object pool 15.6%, the main thread's stack 7.2%, object spheres 4.1%, cluster collideable object references 3.4%, props 3.1%, actors 2.6%; by function: `collision_bsp_test_vector_recursive` 6.6%, `object_get_and_verify_type` 4.9% (one load, its type check through the object header, 1,196 misses a tick with no window), `psp_bsp3d_test_sphere` 4.6%, `cluster_partition_get_next_datum` 3.1%, `object_compute_node_matrices` 2.5%.

## 1. Loop phasing's data side (P1.1)

Question (the review of icache1): for `unit_update`, `weapon_update`, `actor_look_update`, `biped_update` and `object_update`, how much data does each call touch, and does running one function's calls over every object, then the next (phasing), fit both caches?

Method: two new cache-model switches (the lab's emulator patch, its item 18):
- **Footprints:** for each call in the tick, the distinct code and data lines it touches, the data lines an earlier call in the same tick touched too (what a pass keeps warm), and a tick's calls together.
- **Each function a pass of its own:** each listed function's calls run on a pair of caches of their own, the rest on the main pair, with a clock each, so main + passes is the phased tick, scored against the same run as built.
- **The method's check:** the node-matrix pass on c10, where the real batch (nwbatch) lost 34% on the PSP-3000 and its lean rebuild lost in the model: the split must show a loss there too before its gains are trusted.

First results (c20's opening, 30 s; the switches' smoke run) [E]. A pass of one function's calls keeps its code warm only if the code all its calls run fits the cache's 256 lines:

| Function | Calls a tick | Code lines a call | Code lines a tick, all calls | Data lines a call (own) | Fits? |
|---|---|---|---|---|---|
| `weapon_update` | 1.4 | 51 | 52 | 22 (20) | yes |
| `actor_look_update` | 3.9 | 75 | 105 | 35 (22) | yes |
| `unit_update` | 29 | 68 | 384 | 41 (20) | no |
| `biped_update` | 29 | 299 | 543 | 120 (28) | no |
| `object_update` | 78 | 365 | 1,463 | 142 (38) | no |

(Callees included; "own": lines no earlier call in the tick had touched, which a pass of its own would fetch again.) `biped_update`'s single call already runs more code than the cache holds, and `unit_update`'s calls together run half again as much, because each kind of unit takes its own path. So phasing those three can't keep their code resident; only the small ones fit, and they are few calls a tick. The split with its own clock agrees with the old one-pair split to the cycle on the same function (the switch's own check).

**The method's check failed** (c10, to 90 s, 2,700 ticks) [E]. The node-matrix pass (`object_compute_node_matrices`, 305 calls a tick) run as a pass of its own scores 14.44 ms a tick against 16.31 as built: a gain of 1.87 ms (11.5%), tick I-misses down 11,689 and D-misses up 1,739. The real batch of those calls (nwbatch) lost 34% of c10's tick on a PSP-3000, and the model scored its lean rebuild a loss, so the check expects a loss here. The reason is the score's own definition: it is the gain if the calls' order could change freely, with the pass's caches taking every call in order, and it counts nothing for what the restructuring costs (the loop that gathers the objects, state carried between the passes, the extra loads and stores). That cost is what the real batch paid. Phasing scores therefore give an upper bound whose sign can't be trusted, and the tables for b30, c20 and d20 (all five updates at once, and each alone) were not read. Phasing stays unscored until the method is given the restructuring's cost, or until a candidate is built for real and A/B'd in the model; the footprints (above) are the part of this that stands.

## 2. Prefetch

`prefetch_map.md` stands: as the code is, prefetch is worth at most ~0.2–0.4 ms a tick, and only reshaped code or next-item loops open real windows. The one clean next-item case is the node matrices: each node's first store (`matrix4x3_rotation_from_quaternion`'s, the tick's top data-miss site, ~450 misses a tick on b30) waits for its line, and the walk's queue names the next node before this one is done.

Built and scored in the model: `nodeprefetch` (`FIXES_CACHE`). The walk is a queue (siblings and children appended as each node is done), so the node after next is often named already: at the end of each node, every queued node up to the one after next that hasn't been asked for gets three `cache 0x1E`s, for both ends of its matrix and its links in the model tag; the root's before the walk. Each queued node is asked for once, about one node's compute (more than the ~72 cycles a line takes) ahead. A tag block without data is skipped, so no prefetch names an address outside memory. On word `nodeprefetch` (off by default since it was scored; see below). Exact by construction. Scored [E] (cache model with timing, `statecrc` on every side, to 90 s on each map; the tick below includes the state hash `statecrc` computes after every tick, about half of the tick's data misses and the same on every side, so absolute ticks read two to three times the base's and only the differences carry over):

| Map | Play | Tick on / off | on − off | D-miss stall | Waiting on fills | I-miss stall | D-misses a tick |
|---|---|---|---|---|---|---|---|
| c20 (2,700 ticks) | identical | 52.435 / 52.345 ms | **+0.090 ms** | −0.159 ms | +0.152 ms | +0.041 ms | −734 |
| d20 (2,700 ticks) | identical | 54.188 / 54.134 ms | **+0.054 ms** | −0.102 ms | +0.096 ms | +0.020 ms | −473 |
| b30 (to 85 s) | parts after tick 1,325 | 74.541 / 74.282 ms | +0.260 ms | −0.244 ms | +0.239 ms | +0.108 ms | −1,126 |

Exact: play and both random seeds are identical at every tick on c20 and d20; b30's play is identical to tick 1,325 and then drifts, as it does on any change that moves the emulator's speed, so b30's differences are indicative only. The prefetch does what it was built for: the two node-walk sites nearly vanish from the miss list (`object_compute_node_matrices` 707 → 352 a tick and `matrix4x3_rotation_from_quaternion` 413 → none on c20; 99.7% of the fills are used, two a tick evicted unused). It still loses, because it lands too late: a demand load waits about 33 of a line's ~72 cycles for its fill (the prefetch is asked about one node's compute ahead, ~40 cycles), and each prefetch costs ~12 cycles of its own work (the compare and the three operations). On c20 the tick gains 53,000 cycles of D-miss stall and pays 50,600 of waiting, 19,000 of work and 13,600 of I-miss stall. **So `nodeprefetch` is a net loss in the model, 0.05–0.09 ms a tick on the maps where it can be proven exact.** The hardware A/B decides whether the model's fill timing holds (the same job also switches `typehdr` off, so a gain or loss there is the two together). Not built: asking two nodes ahead, which would give the fill time to land at the price of holding more lines in a 16 KB cache [U].

Not built: making the node block's lines with `cache 0x18` instead (no read at all, since each matrix is written whole). A line may hold parts of two nodes, and the walk's order is not the nodes' order, so a created line could wipe a node already written this tick; it would need a per-model plan of which lines are whole, for at most the same 0.2 ms.

## 3. Data layout

Misses a tick in the base, b30 to 170 s [E]: the object pool ~6,400; the main thread's stack ~2,960 (and a second stack ~1,150); object spheres ~1,710; cluster collideable object references ~1,400; props ~1,260; actors ~1,080; `psp_sphere_memo` ~290; `psp_surfaces_seen` ~260; `ai_profile` ~260.

Built and scored in the model: **`typehdr`** (`FIXES_CACHE`). `object_get_and_verify_type`, called from everywhere, checked the type in the object's own data, a line the caller often never touches otherwise: on b30 that load missed ~1,200 times a tick (18% of its runs), the tick's most-missed load, with no window for a prefetch (its address comes from the load before it). The object's header, whose line the lookup has just read for the data pointer, holds the same type, written with it when the object is made and never changed [D]: the check reads that instead. Exact: it passes and fails as before. Off word `notypehdr`. Scored [E] (as `nodeprefetch` above, the same runs): exact (play and both seeds identical at every tick of c20 and d20; b30 as above). The tick gains **0.032 ms on c20 and 0.044 ms on d20** (0.108 ms on b30, indicative), 0.06–0.08% of the hash-inflated tick and about 0.2% of a tick without the hash [U]. The lookup's own misses fall by half or more (c20 775 → 400 a tick, d20 836 → 326, b30 2,410 → 998), but 71%, 60% and 46% of the removed misses reappear in the callers, which then read the object's line themselves, so the whole tick loses only 107, 205 and 765 data misses. The ≤0.26 ms expected on b30 is not reached; the lever is exact and positive, and stays on.

**On a PSP-3000** [M] (b30's route to `exitat300`, two runs a side, both levers on against both off, all four runs to the end at game time ~8,990, no crash): where the two sides play the same game (300–1,300) the tick is 45.6 ms on both and the frame 192.2 against 192.7 ms; in the cinematic (150–570) the tick differs by +0.3 ms, on the beach (2,700–6,000) by −0.2 ms, in the bunker (6,000–8,100) by −0.1 ms (gaps are off minus on), every one inside the runs' own spread (±0.3 ms a tick on a 30–50 ms tick). The cache model's prediction for the pair is a net near zero (`typehdr` +0.03–0.1 ms, `nodeprefetch` −0.05–0.09 ms), well under what two runs a side can resolve, and the PSP agrees. The landing (2,000–2,700) reads as a real gain for the levers on (frame +15.4 ms, drawing +11.3 ms, tick +1.7 ms), but two-thirds of it is drawing, which these tick-side levers don't touch; b30's play drifts after tick 1,325 on any change, so that stretch compares different moments of the descent [U]; it is left as an open observation, not a result. The drawing side is 0.3–0.9 ms slower with the levers off in four of five stretches, which `typehdr`'s type check in drawing's callers could explain [U]. Decision: `nodeprefetch` is off by default (on word `nodeprefetch`) on the model's evidence; `typehdr` stays on; per-lever PSP sides are not worth the hardware time at these sizes.

Still to score: the cluster reference walk (`cluster_partition_get_next_datum`, 3.1% of the tick's misses) and the object spheres (`psp_sphere_memo`'s neighbours); the stack's misses follow the deep frames, a layout question for the frames that cause them.

## 4. Loop phasing itself

Section 1: the code of the big updates doesn't fit the cache, and the model's phasing score failed its check, so a candidate needs the restructuring's cost counted before it is built. A pass pays only when its own code, callees included, fits the cache: running the AI as two passes over the actors saved no instruction misses, as each pass is still far past 16 KB [E].

**Sizing objects_update's steps as passes** [E] (cache model, b30 route and c20 hands off to game time 2,100; each step's whole calls on a second 16 KB pair, the main CPU's instruction-miss drop less what the step still misses on its own pair, less the data misses of walking the objects again, all phases summed):

| Step as a pass | calls a tick (b30 / c20) | I-misses removed a tick | model ms a tick | exact as a pass |
|---|---|---|---|---|
| function values + change colours | 152 / 130 | 3,978 / 5,431 | 0.73 / 0.99 | yes, with the readers below |
| node matrices | 96 / 79 | 7,748 / 6,455 (5,285 / 1,837 left on its own pair) | 1.38 / 1.19 | no: later objects' collision tests read them (NB, LEAF) |
| attached lights' links | 20 / 8 | 1,643 / 769 | 0.40 / 0.15 | no: IK and later movers change their inputs |
| postprocess (IK), damage | 93, 76 / 68, 31 | under 900 | ~0 (the re-walk's data misses cancel it) | no |

The function values miss on nearly every call in the walk and not at all on their own pair, so they are the pass: **FVP** (`fvpass`, check `fvpasscheck`; FIXES_FISSION). The compute reads only the object's own inputs, flags and outputs, its definition and the game time [D]; in the walk the only readers of what it makes are `object_get_function_value` and an attached particle system's colour at its creation, which make a waiting object's values first, and the only writer of another object's inputs, a hidden weapon's export into its holder, makes the holder's first [D]. The proof found three more places the walk's order mattered, each now handled where it happens: an object exported a second time in the walk (a second update, a sleeper's functions) has its first values made first; the sleep check's checksum after an update holds the function values of every type but dead bipeds, weapons and machines, so a family it checks has its values made first; an object deactivated or deleted has them made first. And each object's values are made from its incoming values as its update left them (on b30 two weapons' are written in between by no export) [D, E]. Play and both seeds are identical on vs off on b30, c20, c10, a30 and d20 for 3,600 ticks, strict clean [E]; the whole state parts in the looping sounds, the objects and the first-person weapons, but a timing control parts the same way: `fvpasscheck`, which makes every value in place as off does and only spends the pass's time, parts from off at the same ticks on all five maps, in the same allocations at every checkpoint (all 120 on b30, c10, a30 and d20; 105 of 120 on c20, the rest in looping sounds alone), and every object's whole datum, compared off against `fvpass` at c20's ticks 110, 118 and 630 and b30's 740, 880 and 1,110, differs only in its magic number (+0x8, the stamp a render or collision pass leaves) [E]. So the parting follows the time each tick takes, through the frames drawn between ticks, not anything the pass makes; two runs of one side are identical. In the model, across all phases, b30's tick is 0.37 ms faster (the tick phase 1.2%) and c20's 0.67 ms (3.8%), with 1.8% and 5.0% fewer instruction misses [E]. On the PSP-3000 (b30 flight, sierra-hw 40989795526, one build, 2 runs a side) the tick is 0.5 ms faster (52.8 -> 52.3 ms a frame, -1.0%, real); the drawing is 0.5 ms slower (46.4 -> 46.8, +1.0%, real), and the frame 0.6 ms faster (156.7 -> 156.2, inside abreport's noise gate) [M]. So it's on by default (`fvpassoff` turns it off). Why the drawing rose is open: the walk no longer writes the function values, so the drawing may find more object lines cold [U].

## The model's own fix

The cache model counted the game's `cache 0x18` (create dirty exclusive, which the renderer uses on display-list lines) as a drop, so the stores after it missed in the model. It should allocate the line without a read (measured on a PSP-1000, halo_bench v7b). Now fixed in the emulator's patch: `0x18` makes the line without a read, `0x19` is the drop. Cache-model runs before and after the fix differ a little in drawing; compare them only within one batch.
