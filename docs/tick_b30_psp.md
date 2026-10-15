# Where b30's tick goes on the PSP-1000 (2026-10-15)

Three instruments, all on the rig's PSP-1000 and b30's b113 route (the beach fight): the step profile of a `PSP_TICK_STEPS` recon build (b126, lab 38713734855), the per-type object update lines (b126, lab 38706758115), and the collision log by user (b127 + `collisionlog127`, `PSP_DEBUG_TOOLS=1`, lab 38731689275). Labels: [M] measured on the PSP, [E] emulator, [U] inference. A beach tick is about 50–55 ms; the floor needs 2 ticks + drawing ≤ 66.7 ms.

## The tick by step [M]

Per tick, beach windows (1,787 ticks). A step's time runs until the next step, so each figure is that call plus whatever follows it in its caller.

| Step | ms a tick | What it is |
|---|---:|---|
| `object_compute_node_matrices` | **6.9** | Every animated object's skeleton: animation decode, interpolation, the units' aiming and looking overlays, the matrix chain |
| `object_postprocess_node_matrices` | 4.2 | Postprocess, then everything between objects: the sleep checks (their checksums), reconnects to the map |
| `biped_update_physics` | 3.8 | Biped movement: the pill sweeps against the level and objects |
| `unit_verify_vectors` | 2.7 | `unit_update`'s own code between its (skipped) checks: aiming, looking, seats |
| `object_connect_lights` | 2.6 | Lights attached to objects reconnected to the map every tick (render-only data) |
| `object_type_update` | 1.7 | |
| `actor_decision_loop` | 1.4 | |

Bipeds as a type: 19.8 ms a tick for 32 updates (17.6 live: type update 8.8, node matrices 4.8).

## Collision by user [M]

About 300 s of beach; the log's own clock reads add a little to each call.

| User | Share of real time | Calls, cost |
|---|---:|---|
| Bipeds' movement (`features-in-sphere`) | **4.2%** | 62,547 calls at 203 µs: its level BSP sphere 106 µs, objects ~30–60 µs, features ~20 µs |
| The AI's lines of sight (main CPU) | 2.5% | vector against objects 70 µs, against the level 36 µs; up to 2.3 s in one 30 s window |
| Vehicles' movement | 1.6% | 52 µs a call |
| The camera's obscurer ray | 0.6% | 31 µs a call; a debug display's only. **Removed in `obscurer128` (exact)** |
| Sounds, limp bodies, projectiles, point physics, lights, unknown | ~3% together | |

All collision: about 11% of real time, roughly a fifth of the tick [U: nested calls counted once].

## a50 for comparison [M]

a50's opening hands off (lab 38732215900; tick 28 ms, 10 fps): **biped movement is 11.5% of real time** (68,334 calls at 505 µs, the level BSP sphere 363 µs of it, against b30's 106: a denser level), bipeds 16.2 ms of the 28 ms tick. And **nearauto spent 3.9 ms a frame** deciding the near plane, with four sphere tests about the eye at 1.26 ms each, most of them touching the level. `anysphere` (`obscurer128` branch) answers them with a walk that stops at the first surface: no answer differs out of 74,705 checked, and play is exact; in the emulator the decision drops 79%.

## What it says

1. **Node matrices are the largest single item** (6.9 ms a tick). The code is math over animation data, the same for every object, and independent between objects once their animation state is set. Exact on the Media Engine (same FPU code), if the main CPU has other work while it runs. On the VFPU it would be faster but not bit-exact (plays the same; needs side-by-sides).
2. **Biped movement collision** (~4 ms a tick on b30, a quarter of a50's tick): the plan's next ME item. Each call is a walk of the level's BSP around the biped's sphere (~106 µs) and a gather of nearby objects. "Still" bipeds sweep too (1,900 of 3,000 sweeps a window), and the sphere memo answers only ~5% of level queries, so their spheres move slightly each tick.
3. **Exact, smaller, now:** the obscurer ray (0.6%, built); nearauto's early-out sphere test (a50's 3.9 ms a frame, built); attached lights reconnected once a frame instead of every tick (LD1, ~1.3 ms a tick at two ticks a frame; render-only data, but keeping the drawing identical needs care).
4. **Not where the time is:** the lookups and the code layout (inlining both ways, `-Os`): measured today in the cache models, all slower.

## Sources

Findings rows dated 2026-10-15 in the lab's `measurements.tsv`; the levers in `levers.tsv` (LD1, OB1, LW2).
