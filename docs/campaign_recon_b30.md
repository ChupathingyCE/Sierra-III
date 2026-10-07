# Campaign recon: The Silent Cartographer (b30) on the PSP port — 2026-10-03

First run of a campaign level on the port. Emulator only (PPSSPP headless, JIT, the build's own profiler); Sheepdog01's hardware runs come next. Labels: **M** measured here, **D** documented (source code), **E** estimate.

## How it was made

- Tags extracted from the Xbox `b30.map` with `invader-extract -n`, rebuilt with the patched Invader (`-g xbox-ntsc -X 0x09600000 -T 0x1600000 -U -l 0`), then decompressed (`dez.py`). Result: 3,325 tags, 14.77 MiB of tag data, two BSPs (6.29 and 3.83 MiB), 21.06 of 22.00 MiB tag space, 206 MB file (144 MB of it sound) [M].
- Textures baked with `tools/texpack.py`: 633 textures, 9.0 MiB (b30.ptex) [M]; cube, 3D and bump maps skipped as before.
- A **64 MB recon variant** of the game: `PSP_BIG_RAM=1 psp_build.py` sets `PSP_RECON_64MB`, which widens the tag window to the Xbox's 22 MB (`PSP_TAG_CACHE_SIZE 0x01600000`), lets user RAM run to `0x0C000000`, and writes `MEMSIZE=1` into the PARAM.SFO so the firmware (and PPSSPP) grow the user partition. **Recon only**; the PSP-1000 stays the target.
- `init.txt`: `map_name levels\b30\b30`. The input script skips the opening cinematic with Start, then walks the beach and fires.

The level runs end to end: Pelican ride, scripted drop, encounter AI on the beach, HUD, dialogue. Screenshots in the morning report. Emulator caveat: the beach sand draws in the sky colour (the ground shader; not looked into — a render question, not a recon one).

## The tick is the problem, not the frame

Blood Gulch in the same emulator: tick 1.8 ms, frame 9 ms. b30 on the beach (window 5 of the profile, 29 bipeds and 29 actors live):

| | per tick (M, emulator) | share |
|---|---:|---:|
| `biped_update_physics` (29 bipeds × 344 µs) | 9.8 ms | 27% |
| AI perception: `prop_position_refresh` + `prop_status_refresh` (one serviced prop per actor per tick, 277 µs each) | 7.9 ms | 22% |
| node matrices + postprocess (bipeds, weapons, projectiles: ~85 objects × 35 + 28 µs) | 5.1 ms | 14% |
| `object_connect_lights` (78 moved objects × 39 µs) | 3.0 ms | 8% |
| `actor_decision_loop` (29 × 97 µs) | 2.8 ms | 8% |
| `unit_update` remainder (charged to `unit_verify_vectors`) | 2.7 ms | 7% |
| the rest of `actor_update` (input, combat, emotion, look, situation, move…) | 2.0 ms | 6% |
| `object_type_update` preambles, footsteps, discard checks, translate | 2.5 ms | 7% |
| scenery sleep checks (221 asleep of 223 per tick: the checksum still runs) | 1.5 ms | 4% |
| **tick** | **≈36 ms** | |

Heartbeat over the run: tick 11–30 ms average depending on how many bipeds are active (the drop and the beach fight are the worst; it calms to ~12 ms once the beach is cleared), 12–27 ticks/s. Frame 15–45 ms (the frame includes the tick's time here because the game runs two ticks a frame when it falls behind; the render part alone is ≈15 ms, close to Blood Gulch's worst views).

Hardware has run the frame 2.5–4× slower than this emulator and **the tick about 4×** (b42 on foot: 7.4–8.1 ms against the emulator's 1.8; the tick is cache-miss-bound and PPSSPP models no cache). Scaled the same way, **b30's beach would tick at roughly 7 per second on the PSP-1000** [E]. The campaign needs something like a 5× cut in tick cost in fights of this size, before the memory problem is even touched.

### Where the cost comes from (D, from reading the code)

- **Biped physics** (`bipeds.c: biped_update_physics`): every biped, every tick, sweeps its pill against the collision BSP and nearby objects, idle or not. b30's BSP has 41,948 bsp3d nodes (the census), so each test is pointer-chasing through a 2.9 MB structure: cache-miss bound on the Allegrex (16 KB D-cache, ~73 cycles a miss).
- **Perception**: Bungie already timeslices it. Each actor services one prop (one known unit) per tick, the one whose timer is highest; non-enemies get serviced 8× less often and props at quantized distance ≥ 3 half as often (`actor_perception.c`, the `prop_timer` block). The service is a line-of-sight `collision_test_vector` through the same BSP, plus position bookkeeping. The `actor_perception_refresh` (object scan, 13 candidates per actor per tick at 2 µs) is cheap.
- **Node matrices**: compressed-animation decode and one `matrix4x3_multiply` per node for every moving object; bipeds have ~20–40 nodes.
- **`object_connect_lights`** runs for every object whose `_object_dynamic_lighting_recompute_bit` is set — every moving biped and its weapon, every tick.
- **Decision loop**: the actor state machine (`actor_decision_loop`), 97 µs per actor.

### What this says about the plan

1. **The collision BSP is the shared hot path**: biped physics, AI line of sight, vehicle mass points, projectiles and particles all walk it. Making one `collision_test_vector` / pill sweep cheaper (cache layout of the top levels in the scratchpad, 16-bit node fields where counts allow, VFPU plane tests, or a per-tick cache of ground results for bipeds standing still) pays everywhere. This is the first thing to attack, and it is fidelity-free.
2. **Bipeds standing still** turned out to be rare (see "The pill sweep, taken apart" below): a standing-biped shortcut is worth 1–2 ms a tick here, not the 3–6 first guessed. The sweep itself has to get cheaper.
3. **Perception rate** is already Bungie's knob. Halving the service rate for actors far from the player is a small fidelity change (reaction delay for enemies out of the fight), worth having behind a keep word, not on by default.
4. **The Media Engine** fits this picture: line-of-sight rays and ground probes are read-only on tag data with small answers — the "AI-side equivalent" job the feasibility notes named. The safe-task route (see the hardware reference) would let those run beside sceSas.
5. The frame side is not free either (render ≈15 ms in the emulator at the beach → 30–45 on hardware, E), but it's the same work Blood Gulch already measures.

### The pill sweep, taken apart (M, emulator, second pass)

Two more probes (in build 45 for hardware too) answer the "standing bipeds" question and split the shared hot path:

- **Few bipeds are ever still.** Per 3,000 sweeps on the beach: 12–64% "moving" (over 0.01 wu/tick), 35–75% "slow" (0.001–0.01 wu/tick: a walk, a shuffle, an AI adjusting its stance), only **7–20% "still"** (under 0.001 wu/tick ≈ 9 cm/s). Gravity pushes every standing biped into its ground plane each tick, so the vertical velocity is never zero and the sweep always finds its one collision. A "skip the sweep when standing" rule would save 7–20% of the pill cost (≈1–2 ms/tick here), not the half I hoped for; still bipeds also cost *more* per sweep (400–570 µs vs 250) because they stand in cluttered places.
- **Where a sweep's time goes.** `collision_move_pill` = `collision_get_features_in_sphere` (collect the BSP's surfaces/edges/vertices in the pill's sphere, then every collideable object's features in the touched clusters) + `collision_move_point` against those features. Per gather on the beach: **BSP phase 50–220 µs (4–18 leaves touched), objects phase 20–96 µs walking 17–60 objects** (the cluster lists are smaller than the 715 cluster-object references suggested; the beach is cut into several clusters). So roughly 45% BSP feature collection, 25% object walk, 30% the point move. The same gather serves vehicle mass points (build 44's "ground tests") and projectiles.

A third probe split the BSP phase: **the walk (`collision_bsp_test_sphere`) is 36–204 µs per gather and the shape-making only 4–19 µs**, for fewer than two features found per gather (0.5–2.2 surfaces, edges and vertices). The walk descends the 3D BSP to 4–18 leaves, and in each leaf loops over its bsp2d references, matches each against the stack of planes the sphere straddled, projects, and descends a 2D BSP to test surfaces, edges and vertices — every step through `TAG_BLOCK_GET_ELEMENT` (an out-of-line call with five asserts) and pointer chasing across b30's 2.9 MB collision data. The cost is in rejecting candidates, not in what is found.

What that points at, all fidelity-free:

1. **Make the walk cheap.** First the exact, small things the earlier audit already named: inline `TAG_BLOCK_GET_ELEMENT` (keep the asserts cold) and float `fabs` in `collision_bsp.c`; then the top of the BSP (nodes and planes) in the scratchpad; then a bake-time per-leaf packed table of each leaf's planes, edges and vertices so the per-leaf tests read sequentially instead of chasing pointers. On hardware the walk is cache-miss-bound (16 KB D-cache, 73-cycle misses), so this is where the 4× hardware/emulator tick ratio lives.
2. **A compact broad-phase for the object walk**: per cluster, a tight array of (centre, radius, index) so the sphere test is a linear scan of a few hundred bytes, not an `object_get` per candidate.
3. **Share gathers**: AI squads stand in each other's spheres; a per-tick cache keyed on leaf set would serve several bipeds from one collection. Exact if invalidated when any object in the set moves.

## Memory on b30 (M, emulator, 64 MB layout)

- Game state: 3,251 of 3,348 KB allocated, as on Blood Gulch (fixed-size pools). Peaks: `objects` pool 650 KB of 1,024 (Blood Gulch 53); `object` headers 967 of 2,048; `actor` 29 of 256; `prop` 220 of 768; `hs thread` 68 of 256; `lights` 129 of 896; cached object render states 256 of 256 (full); cluster/object reference arrays 715 of 2,048.
- Heap top 3,968 of 4,992 KB (Blood Gulch 3,404).
- Textures: 165 loaded for the beach (3,156 KB of the 5,312 KB of caches), 18 evictions, worst frame 3 ms of reading. The 9 MiB pack does not need to fit; what a view uses does.
- Sound: 8.2 voices on average, 15 at most, in the 16-voice software mixer; 6.3% of the CPU in the emulator.
- Tag window: 21.8 MB used (tags + largest BSP) against the PSP-1000's 9 MB — unchanged from the census; nothing here changes that arithmetic.

## Instrumentation added (build 45, both variants)

- `objects_update` by type now splits bipeds like vehicles: `(of the bipeds: type update, damage, node matrices)`.
- Breadcrumb steps inside `biped_update`, `biped_update_moving`, `unit_update`, `unit_update_animation`, `object_update`, `actor_update`, `actor_perception_update`, `actor_perception_refresh` (profile.txt shows each callee's "time until the next step"). **Recon builds only** (`PSP_TICK_STEPS=1`): a step costs ~1.3 µs and `object_update` runs for every object every tick.
- `bipeds: pill sweeps: moving N at N us (...), slow ..., still ...` every 3,000 sweeps, and `collision: features in sphere: N calls; BSP walk N us each (N leaves), features N us (N ... made into shapes), objects N us (N objects walked)` every 6,000 gathers — in every build, cheap. (Build 45 as shipped to Sheepdog01 logs the earlier two-way form, `BSP N us each` = walk + shapes together.)
- Game-state survey names are copied (some callers pass stack buffers; the b30 log showed garbage names).
- Assert-like macros (`match_assert_valid_*`, `vassert`) are excluded from breadcrumb insertion.

## Files

- Map and pack: `/home/claude/maps_psp/b30_dec.map`, `b30.ptex`; tags in `/home/claude/tags_b30`.
- Emulator stick: `/tmp/ms_big` (EBOOT from `/tmp/psp_halo_big`); logs `/tmp/ms_big/halo/log/`, shots `/tmp/ms_big/halo/shots/`.
- Build knob: `PSP_BIG_RAM=1` in `psp_build.py`; macro `PSP_RECON_64MB` in `psp_fixups.py` (PRELUDE, `physical_memory_map.c`, `platform.h`) and `psp/src/psp_memory.c`.
