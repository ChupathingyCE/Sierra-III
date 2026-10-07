# Halo CE on the PSP-1000: engine performance audit

2026-10-02. Read-only audit of `/tmp/halo-sp` (game and Linux port layer) plus the PSP changes (`psp_fixups.py`, `psp/src/*.c`), against the hardware numbers from builds 18 and 19.

**How the figures were made.** Every figure is labelled:
- **measured**: on hardware, by Sheepdog01, from build 18;
- **verified**: compiled or disassembled here (`psp-gcc 15.2` with the PSP build's flags, `psp-objdump` of `/tmp/psp_halo/halo.elf`, and the hot files recompiled with `-g` so each double-precision call maps to a source line);
- **estimated**: reasoned from the code plus the measured costs in the feasibility notes (cache miss ≈ 73 cycles, double `sqrt` 2,390 cycles, `sinf` 113 cycles). At 333 MHz, 1 ms is 333 k cycles.

No new hardware or emulator runs were made.

---

## 0. Summary: the findings that matter most

| # | Finding | Evidence | Effect (333 MHz) | Status |
|---|---|---|---|---|
| 1 | **Render interpolation, made expensive by double-precision `sqrt`, explains most of build 18's unexplained render costs.** First-person weapon: 9 double `sqrt` per node per frame. `render_objects`: about 5 per visible node per frame. Plus the 1.3 ms "gap" after each tick | `render_interpolation.c:151-154, 175-191, 194-231, 290, 381-470, 513-584, 793-835`; `objects.c:1244-1253` | FP weapon ~3 ms of its 3.6 ms (est.); a large part of `render_objects` 7.5 / 25 ms (est.); ~1 ms of the gap (est.) | **Build 19 already turns interpolation off.** Re-measure. If it ever comes back, use `sqrtf` (bit-identical) |
| 2 | **psp-gcc does not narrow `(float)sqrt(float)`, `floor`, `ceil` or `fmod` to the float versions**, unlike clang on x86. Each call stays a double library call with two conversions | Verified with a test compile: `(real)sqrt(x)` → `__extendsfdf2` + `sqrt` + `__truncdfsf2`. 28 `sqrt`, 52 `floor`, 46 `fmod` and 6 `ceil` call sites remain in the ELF | Hot sites: animation keyframe decode (`floor` per node per sample), vehicle wheel and tread `fmod`, function values. Estimated 0.3–1.5 ms per tick on Blood Gulch | A 6-line type-generic header fixes all of them **bit-exactly** (verified to emit `sqrt.s` and `floorf`/`fmodf`) |
| 3 | **About 259 static scenery objects get a full `object_update` every tick** (node matrices, function values, change colours, damage) and produce the same result each time | `objects.c:4235-4255`, `3606-3676`, `2344-2798`; `scenery.c:143-164` does nothing for non-animated scenery | Estimated 2–3 ms of the 8.2 ms on-foot tick | Put them to sleep with Bungie's own *active* bit (`objects.c:4200-4222`; inactive objects are still drawn and collide). Exact if the wake-up triggers are right |
| 4 | **Parked vehicles still pose fully every tick**: `unit_update`, about 10 overlay animations (steering, speed, wheel, suspensions), 20–40 node matrices and weapon updates. Only the rigid-body physics is skipped at rest. **Hovering vehicles (antigrav) probably never count as at rest** and raycast every mass point every tick | `vehicles.c:2405-2709` (physics gated at `2592`), `vehicles.c:1370-1480`; at-rest test `physics.c:1494-1523` needs ≥3 grounded points; antigrav probe `physics.c:897-931` | Estimated 60–170 k cycles per vehicle per tick, ×8 vehicles ≈ 1.5–4 ms | Measure at-rest counts by vehicle type first. Then cache the pose of parked, unoccupied vehicles (exact) |
| 5 | **GE level classification transforms every corner of every triangle** (about 6× per shared vertex), reading vertices in random order | `psp_ge.c:279-314` | Measured 1.5 / 6.5 ms. Classifying per vertex instead should give about 0.3–0.5 / 1.2–2 ms (est.) | Same output, so no fidelity risk |
| 6 | **Each controller poll makes about 20 `sceCtrlPeekBufferPositive` system calls plus `malloc`/`free`** | `xinput_sdl.c:404-440, 529-570`; `psp_sdl.c:175-258` (`read_controls` on every button and axis query; `SDL_GetGamepads` mallocs) | Most of the measured 0.8 ms `input_update` (est.) | Read the pad once per poll |
| 7 | **HUD 2 → 7 ms (build 18)** is most likely **blocking texture-cache loads**. HUD bitmaps get evicted by the null path's shadow and transparent textures, then reloaded from the Memory Stick in a busy loop | `rasterizer_xbox.c:1904` (blocking `_texture_cache_bitmap_get_hardware_format(..., TRUE, TRUE)`); spin at `xbox_texture_cache.c:946-965`; cache is 1.25 MB | Hypothesis, not measured | Build 19 skips textures and the HUD. When the GE draws the HUD, its textures must stay resident in VRAM |
| 8 | **Build 19's timing probes cost real time**: 2 `sceKernelGetSystemTimeLow` system calls per object per tick (~600 per tick), and they inflate `objects_update` | `psp_fixups.py` FIXES_19 for `objects.c` | Estimated 0.2–0.6 ms per tick | Subtract or sample (section 5.5) |
| 9 | **Build 19 changes the frame-timing path.** With interpolation off, the game uses the Xbox vblank throttle, which **spin-waits** on the emulated vblank counter | `main.c:2233` (path choice), `main.c:2482-2507` (spin, 1 s timeout) | Risk: if the vblank thread can't preempt the spinning main thread (the PSP has no time slicing), expect a one-off 1 s stall and "stuck waiting for VBLANK callback!" | Check build 19's log |
| 10 | Assertions are evaluated on the PSP, and `PSP_ASSERTS_COLD` **already exists in `cseries.h:128-136, 219-221` but the PSP build doesn't define it** | `psp_build.py` flags | Feasibility notes measured 7–9% smaller hot code with cold asserts plus `-freorder-blocks-and-partition` | One flag; no semantic change |

Build 19 already captures the largest wins (interpolation off, null-path passes skipped). Items 2–6 are the next tier. Each is exact or near-exact, so it keeps the game as close to the original as possible.

---

## 1. What build 18's numbers were really measuring

### 1.1 Render interpolation plus the double `sqrt`: FP weapon, `render_objects`, the gap

`display.interpolation` defaults to `true` (`port_config.c:78`), so builds up to 18 interpolated every frame:

- **Per object, per frame:** `object_get_node_matrices` returns the interpolated pose (`objects.c:1244-1253`, `1842`). `render_interpolation_object_node_matrices` (`render_interpolation.c:513-584`) blends every node of every rendered object.
  - Per node, per frame: `matrix_blend_rotations` normalises a quaternion with a double `sqrt` (line 290).
  - Per node, per tick: each new snapshot's rotations are rebuilt by `rotation_from_matrix`: three `vector_length` double `sqrt` in `basis_is_rotation` (lines 151-154, 175-191) plus one in `quaternion_from_basis` (194-231).
  - At about one frame per tick that is ~5 double `sqrt` per visible node per frame: ~12 k cycles. 150 visible nodes would be ~5 ms (estimated).
- **First person** (`render_interpolation_first_person`, lines 793-835, called from `first_person_weapons.c:1378` every frame): `matrix_blend` runs `rotation_from_matrix` on **both** snapshots for every node, every frame, so 9 double `sqrt` per node per frame.
  - At 2,390 cycles each, that's ~22–24 k cycles per node. With ~40–50 FP nodes: 0.9–1.2 M cycles, **~3 ms**.
  - That matches the measured, scene-independent 3.6 ms. The game's own FP posing (`first_person_weapons.c:1065-1376`) is the remainder.
- **Per tick** (the 1.3 ms gap after each tick): `render_interpolation_tick` (lines 381-470) walks every object, `memcpy`s all node matrices (~400 objects), and runs `correction_advance` with double adds (line 327).

**Verified:** the disassembly shows `rotation_from_matrix` making 7 double `sqrt` calls (`__extendsfdf2` / `sqrt` / `__truncdfsf2`).

**Build 19** turns interpolation off (`psp_keep("interpolation")`), so all of this should disappear. Expected (estimated): FP update 3.6 → ~0.5 ms, `render_objects` down sharply, gap 1.3 → ~0.3 ms on foot.

**If interpolation is ever wanted** (for example a 20 Hz tick with 30 fps display):
- change `sqrt` to `sqrtf` in `render_interpolation.c` (bit-identical, see section 3);
- blend only objects that moved (compare the two snapshots' root node first);
- for the FP weapon, store the rotation of the `previous` snapshot instead of recomputing it every frame.

That's about a 20× cut.

### 1.2 The gap after each tick: what runs there

`game_time.c:468-476`: `game_tick()` → `render_interpolation_tick()` → `network_distributed_tick()`, then once per frame `game_frame()` (`game.c:863-890`). The breadcrumb profile charges all of it to `profile_tick_end` (the last step inside `game_tick`).

- **`network_distributed_tick`** (`network_distributed.c:3056-3104`) is negligible in a local game. Neither branch runs; `distributed_batches_flush` loops over 5 empty batches. A few hundred cycles.
- **`render_interpolation_tick`**: ~1 ms (above). Off in build 19.
- **`game_frame`**:
  - `particles_update` (`particles.c:1004-1063`). Each live particle with point physics raycasts the BSP **every frame** (`point_physics.c:177`, `collision_test_vector`).
  - `particle_systems_update`, `contrails_update`, `widgets_update` (Warthog antenna: `antenna.c`, double `pow`, `sin`, `cos`).
  - Driving kicks up tyre dust, which is why the gap grows from 1.3 to 6 ms per tick. At 0.5 frames per tick, that's ~10 ms per frame of particle work (estimated).
- **A side effect of build 19 to know about:** skipping `render_particles` means `last_rendered_frame_index` is never updated. `particles_update` then **deletes any particle unrendered for 15 frames and 15 ticks** (`particles.c:102, 1035-1041`). Particles now live at most ~0.5 s, which hides their simulation cost.
  - It's cosmetic only.
  - The cost comes back once the GE draws particles. Plan particle knobs for then (section 6).
- Build 19's breadcrumbs in `game_time_update` will split this gap. The prediction: almost all of it is `game_frame` once interpolation is off.

### 1.3 HUD 2 → 7 ms

`interface_draw_screen` is null-path work: no pixels result. What changed between build 16 (2.0–2.6 ms) and build 18 (6.9 ms) is texture-cache pressure.

- HUD and text drawing go through `rasterizer_set_texture_bitmap_data`. It calls `_texture_cache_bitmap_get_hardware_format(bitmap, TRUE, TRUE)` (`rasterizer_xbox.c:1904`), a **blocking** load.
- A blocking miss spins on `SwitchToThread()` until the Memory Stick read lands (`xbox_texture_cache.c:946-965`). The doc measured a random 4 KB read at 1.75 ms.
- In build 18 the null path still ran:
  - shadows, 1.9 ms, and transparent geometry, which request model textures;
  - the 131 KB "vector normalization" lookup texture, which fails every time.
- All of this competes with HUD bitmaps for 1.25 MB, so HUD textures plausibly get evicted and reloaded every frame.

**Hypothesis, not measured.** Build 19 skips both textures and the HUD, so it can't confirm this.

**Design rule for the GE HUD:** HUD, font and reticle bitmaps live permanently in VRAM. Never take a blocking texture load in the frame.

### 1.4 Why ticks drop to 18/s when driving

`main.c:2207` and `2449`: `seconds_elapsed = CEILING(seconds_elapsed, 1/15 s)` for local games. That's at most **2 ticks per rendered frame**. At 9 fps the game runs at 18 ticks/s, so it slows down rather than skipping ticks. This is the Xbox's behaviour.

So once fps falls below 15, frame cost directly becomes game slow-down. At a 20 Hz tick the same 1/15 s cap gives an average of 1.33 ticks per frame. Check whether the tick-rate patch rescales this literal.

### 1.5 Build 19 measurement overhead

`objects_update` now brackets every top-level `object_update` with two `psp_microseconds()` calls (`psp_fixups.py`, FIXES_19 for `objects.c`). Each is a `sceKernelGetSystemTimeLow` system call.

With ~280 updates per tick that's ~560 system calls per tick (estimated 0.2–0.6 ms). Part of it lands inside the per-type sums.

Read build 19's per-type numbers with that in mind. Better: time only every 8th tick, or time the whole loop and count types. `psp_step` breadcrumbs (~150 per frame) add a further ~0.1–0.2 ms per frame (estimated).

---

## 2. The tick

### 2.1 Anatomy of `objects_update` (`objects.c:4167-4286`)

1. **Activation pass** (lines 4191-4232). Runs only when the players' combined PVS changes. On Blood Gulch the outdoor PVS covers the whole map, so **every object is active**.
2. **Update pass** (4235-4255). For each active top-level object, `object_update` (3606-3676) runs:
   - `object_type_update`: for each part definition, e.g. a vehicle runs `unit_update` and then `vehicle_update` (`object_types.c:760-778`);
   - `object_damage_update` if the object has a collision model;
   - export function values;
   - `object_compute_node_matrices`, unless `_object_do_not_recompute_node_matrices_bit` is set;
   - `object_compute_function_values`, `object_compute_change_colors`, light reconnection;
   - children, recursively (held weapons, vehicle seats and turrets);
   - `object_postprocess_node_matrices`.
3. **Creation and deletion pass**, then `objects_garbage_collection`.

**What each update costs regardless of type (estimated):**
- 10–15 cache misses: header, 3–7 datum lines, definition, model, tag instance entries, node array (~1 k cycles);
- several out-of-line accessor calls (2.6);
- at least one root node matrix: 2–3 `matrix4x3_multiply`, `matrix4x3_from_orientation`, **two** `valid_real_matrix4x3` checks of the root node (`objects.c:2639` and `2755`) and the `vassert` validity checks (2514-2542).

That's about 3–4 k cycles for a one-node object.

### 2.2 Scenery: why ~259 per tick, and what they do

Blood Gulch's trees, rocks and boulders are scenery objects. All are active, so all update.
- `scenery_update` (`scenery.c:143-164`) only does anything if the object animates (flag 0).
- Everything else in `object_update` reproduces the same node matrices, function values and colours as the previous tick.

**Estimated cost:** 259 × ~3.5 k cycles ≈ 0.9 M cycles, **~2.7 ms per tick** (about a third of the 8.2 ms on-foot tick). Build 19's per-type report will confirm or correct this; mind the probe overhead in 1.5.

**Proposal: static-object sleep, using the engine's own mechanism.**

`object_deactivate` / `object_activate` (`objects.c`) only clear and set the header's active bit. Inactive objects stay connected to the map: they are still rendered (`render_objects.c:571-574` even special-cases drawing inactive objects) and still collide. This is exactly how the Xbox treats objects outside the PVS.

1. After an object's first update, deactivate it on the PSP if all of these hold:
   - type is scenery (later also parked items, devices at rest);
   - no animation playing;
   - `functions.count == 0` and no change-colour functions;
   - no attachments or widgets;
   - not a child;
   - `interpolation_frame_count == 0`;
   - not flagged for dynamic lighting recompute.
2. Reactivate it from:
   - `object_set_position` / `object_translate`;
   - `object_cause_damage`;
   - `scenery_animation_start*`;
   - attach and detach;
   - and give the activation pass a PSP exception, so a PVS change doesn't wake it.

- **Gain:** estimated −2 to −2.5 ms per tick on Blood Gulch. In campaign levels it scales with scenery count.
- **Fidelity:** none, if the predicate is right. The outputs of a skipped update are identical by construction.
- **Effort:** small–medium. One PSP flag bit plus ~6 hook points.

### 2.3 Vehicles: is there an at-rest mechanism?

**Yes, but it only skips the rigid-body physics.**

- `vehicle_update` gates the physics on `!TEST_FLAG(object.flags, _object_at_rest_bit)` (`vehicles.c:2592-2653`).
- The flag is set by the physics step:
  - `physics.c:1494-1523` (new path), `2071-2080` (old path);
  - it requires **every mass point at rest and at least 3 on the ground** (`ground_depth > 0`, line 884), plus velocity and acceleration below thresholds.

**What still runs every tick for a parked, empty vehicle:**

| Step | Where | Notes |
|---|---|---|
| `unit_update` (whole) | `units.c:5181-6020` | Aiming and looking vectors with trig (`angular_accelerate_to_position`, `unit_euler_aiming_update`: double-wrapped `sin`, `cos`, `acos`); `weapon_owner_update` for the vehicle's weapon; seats; 12× `unit_verify_vectors`, each 6 normal checks (`units.c:5193...6019`, `10978-10991`) |
| `vehicle_update` light part | `vehicles.c:2434-2587, 2686-2706` | `arctangent` → `halo_atan2` with 4 float↔double conversions (`vehicles.c:2451`); speed seeks; `unit_update_animation` |
| `vehicle_preprocess_node_orientations` | `vehicles.c:1370-1480+` | `aiming_screen_apply` (2 double `fmod`) + up to 5 `overlay_animation_apply_continuous` (double `fmod` + `floor`, lines 1156-1157) + one overlay **per suspension**. Each overlay touches every node and decodes keyframes (double `floor` per compressed node, `model_animations.c:1992`) |
| Node matrices | `objects.c:2344-2798` | 20–40 nodes: `matrix4x3_from_orientation` + `matrix4x3_multiply` each |
| `unit_postprocess_node_matrices`, children | | Seat and weapon attachments |
| Wheel and tread `fmod` | `vehicles.c:1877, 1882, 2063` | Double `fmod` |

**Estimated** 60–170 k cycles per vehicle per tick. With 8 vehicles that's 1.5–4 ms, consistent with "vehicles dominate" in the emulator.

**Hover vehicles:**
- Antigrav mass points don't touch the ground (`ground_depth` is 0); the probe at `physics.c:897-931` raycasts down every tick instead.
- So a parked Ghost probably **never** passes the "≥3 points on ground" test, and runs full physics plus one `collision_test_vector` per antigrav point every tick.
- Banshees likewise if they idle in hover.

This is from the code, not measured. Verify by logging `TEST_FLAG(flags, _object_at_rest_bit)` per vehicle definition in the per-type report.

**Recommendations, in order:**
1. **Measure**: at-rest count per vehicle type, and the time split inside `object_update` (type update, preprocess, node matrices, postprocess). Two extra accounts in `psp_object_account`.
2. **Pose cache for parked, unoccupied vehicles** (exact).
   - Pose key: at rest, no driver or gunner, and `turn`, `speed`, `slide`, `wheel`, suspension bytes, aiming vectors, `outgoing_function_values`, position and orientation unchanged since the last tick.
   - While the key matches, set `_object_do_not_recompute_node_matrices_bit` and skip `vehicle_preprocess_node_orientations`.
   - Gain −1 to −3 ms per tick (est.). No fidelity cost.
3. For hover vehicles that never rest: a PSP "settled" rule (velocity and acceleration below the same thresholds for N ticks, unoccupied, no damage) that lets them sleep like ground vehicles. **Small fidelity risk**: a parked Ghost's idle bob, if any, would freeze. Check against the Xbox.

### 2.4 Bipeds and units

- `unit_update`'s trig all goes through the double wrappers (section 3). Most calls have float arguments.
- `unit_verify_vectors` runs 12× per unit update (~1–1.5 k cycles per unit, estimated). It's pure validation; see 5.3.
- Illumination sampling (a raycast) is already time-sliced: one unit per tick (`units.c:5197-5217`, `5852-5854`).
- With a single player and no AI, the biped is a small share on Blood Gulch. On `b30` the AI share (27% on x86) and its trig and raycasts take over. AI files also make many trig calls through the double wrappers (`actor_looking.c`, `actor_moving.c:2767-2825`, `actor_combat.c:1292-1325`).

### 2.5 Animation decoding

**Compressed animations:** `animation_get_keyframe_rotation`, `_translation` and `_scale` each do `fast_ftol(floor(real_frame_index))` with a **double `floor`** (`model_animations.c:1992`, `2095`, `2198`). That's once per animated node per sample, so overlays multiply it.

**Every overlay call:** `overlay_animation_apply_continuous(_scaled)` does a double `fmod` plus `floor` (1156-1157, 1413-1414). `aiming_screen_apply` does 2 double `fmod` (1630, 1664).

**Gain:** the narrowing header (section 3) turns each of these into `floorf`/`fmodf` (exact), saving ~150–250 cycles per call (estimated). Whether this matters depends on whether Blood Gulch's vehicle and FP animations are stored compressed. That's worth one check with Invader on the map.

### 2.6 Accessor overhead (all object and tag code)

- **`object_get(i)`** (`objects.h:196`) is two out-of-line calls: `object_get_and_verify_type` → `datum_get` (`data.c:143-166`).
  - ~40 instructions, an integer multiply, three dependent loads and two asserts.
  - **Verified:** 1,185 call sites, plus 1,914 direct `datum_get` sites.
  - `unit_update` alone has 21 sites.
- **`tag_get`** (`cache_files.c:881-910`) calls `cache_get_tag_instance` and checks three group tags. The tag instance table (1,806 × 32 bytes on Blood Gulch) is larger than the D-cache, so it often misses.
- **`TAG_BLOCK_GET_ELEMENT`** is an out-of-line `tag_block_get_element_with_size` with 5 asserts (`tag_groups.c:44-61`).

**Estimated** 0.3–0.8 ms per tick on Blood Gulch, more with AI. **Fix:** `HALO_PSP` inline fast paths in the headers, keeping the asserts as cold out-of-line checks. No semantic change. Effort small–medium.

### 2.7 Collision in the tick

- Vehicle mass points and the antigrav probes (above).
- Biped pill movement: `bsp3d_test_pill_recursive` has a double multiply and subtract from `fabs(inverse)` (`collision_bsp.c:2112`).
- Projectiles (`collision_test_vector`) and particles (per frame, in `game_frame`).

The feasibility notes' lever (cache-line layout, scratchpad for the top of the BSP) still applies. On Blood Gulch with no AI, collision is mostly vehicles and particles.

---

## 3. Double precision: verified behaviour, hot sites, cheapest fixes

**What the compiler does (verified with the PSP build's exact flags,** `-O2 -march=allegrex -ffp-contract=off -fno-math-errno -fsingle-precision-constant`):

| Source | Code generated | Cost (from the feasibility measurements) |
|---|---|---|
| `fabs(f) < c` | float `abs.s` (narrowed) | free |
| `(real)sqrt(f)` | `__extendsfdf2`, **`sqrt` (double)**, `__truncdfsf2` | ~2,500 cycles |
| `(real)floor(f)`, `ceil`, `fmod(f, 1.f)` | extend, double library call, truncate | ~150–600 cycles |
| `sine(x)` → `halo_sin` (`real_math.h:920`) | extend, call, (in `psp_libc.c:109`) truncate, `sinf`, extend; truncate at the call site | ~400 cycles vs 113 for `sinf` |
| `fabs(f) * x` with `x` float | double multiply (the `fabs` result is double) | ~150–300 cycles |

**Cheapest global fix (verified to compile to `sqrt.s`, `floorf`, `fmodf`).**

Add a PSP-only header after `<math.h>` (via `psp_fixups.py` into `port/linux/include/math.h` under `#ifdef HALO_PSP`):

```c
#define PSP_NARROW1(F, x) __builtin_choose_expr(__builtin_types_compatible_p(__typeof__(x), float), F##f(x), (F)(x))
#define PSP_NARROW2(F, x, y) __builtin_choose_expr(__builtin_types_compatible_p(__typeof__((x)+(y)), float), F##f(x, y), (F)(x, y))
#define sqrt(x)    PSP_NARROW1(sqrt, x)
#define floor(x)   PSP_NARROW1(floor, x)
#define ceil(x)    PSP_NARROW1(ceil, x)
#define fmod(x, y) PSP_NARROW2(fmod, x, y)
```

- **Exact:** `sqrt`, `floor`, `ceil` and `fmod` are exactly rounded, so the float version of a float argument gives the same bits.
- **Untouched:** true double arguments (libc, the profiler) keep the double functions.

**Trig wrappers (lever 1 in the feasibility notes).** Under `HALO_PSP`, redefine `sin`, `cos`, `atan2` and the rest in `port/include/halo_math.h` as inline float-in, float-out functions (`sinf`, or VFPU later). That removes 4 conversions per call; `psp_libc.c:109-120` already computes in float.

**Not exact:** results for double-typed arguments change slightly. Acceptable, since the PSP doesn't play system link with PCs.

**Explicit double sites the macros won't fix** (source edits, all `#ifdef HALO_PSP`):

| Site | What | Fix |
|---|---|---|
| `periodic_functions.c:245` | `(real)fmod((double)time, 1.0)`, once per object function per tick | `time - floorf(time)` (exact for time ≥ 0) |
| `collision_bsp.c:2112` | `fabs(inverse)*radius` in double, in biped collision | `fabsf` (changes rounding slightly) |
| `physics.c:837-838, 874-875, 1824-1825, 1876-1877` | `fabs(dot)*...` lift terms in double, every powered mass point every tick | `fabsf` |
| `vehicles.c:661` | `fabs(speed)/max` in double | `fabsf` |
| `input_abstraction.c:376-415` | `1.0 - fabs(...)*k` | `1.0f`, `fabsf` (per frame, minor) |
| `render_interpolation.c:153, 206-230, 290, 327` | if interpolation is re-enabled | `sqrtf` (exact) |

**What remains is cold**: error and printf formatting (`object_compute_node_matrices` 41 extends, `unit_verify_vectors` 30, all on failure paths), and colour packing in null-path rasterizer code (`rint` + `__muldf3` in `_rasterizer_*`, `real_*_to_pixel32`). The GE renderer replaces the latter.

---

## 4. The render side (per frame)

### 4.1 GE level classification (`psp_ge.c:270-324`)

Measured: 1.5 ms for ~1,150 triangles and 6.5 ms for up to 3,722, i.e. 430–600 cycles per triangle. Every triangle transforms its 3 corners from 32-byte vertices in index order. Shared vertices (~6 triangles each in a terrain mesh) are transformed and fetched up to 6 times, mostly as cache misses.

**Cheaper, identical output** (estimated 4–6× cheaper):
1. Scan each draw's index list once for min/max.
2. Stream through vertices `[min, max]` sequentially. That's 2 vertices per cache line, at the sequential cost (57 cycles per line instead of 72). Compute a 5-bit outcode per vertex into a byte array in the **scratchpad** (16 KB, `0x00010000`).
3. The triangle loop becomes 3 byte loads plus OR/AND. Transform again only the corners of triangles that need clipping.

Then 1.5 → ~0.3–0.5 ms and 6.5 → ~1.2–2 ms. The transforms are 3 dot products: one `vtfm3` per vertex on the VFPU, a further ~2×.

Later the bake removes this entirely (tessellated, GE-safe BSP), or it moves to the Media Engine: inputs are read-only tag vertices plus an index list; output goes to uncached RAM.

### 4.2 `render_objects` without interpolation

Per visible object (`render_objects.c:1099-1194`, `models.c:726-1036`):
- lighting from the cache, which refreshes statics only for moved objects (`render_objects.c:881-1033`): a raycast down for a moving vehicle every tick;
- LOD pixels;
- `render_model`:
  - relative node matrices (`models.c:787-795`): one `matrix4x3_multiply` per node. **The GE path needs this.**
  - `_rasterizer_model_begin` (`rasterizer_xbox_models.c:626-716`): skinning constants, lighting constants and fog-screen setup. **The GE path ignores all of it.**
  - parts: static parts go to the GE; dynamic or uncompressed parts fall back to `_rasterizer_model_draw`, 7.3 KB of null-path state and texture setup;
  - transparent parts are queued by `rasterizer_model_transparent_geometry_submit`, and the queue is then never drawn in build 19.

**Cheap trims (exact; estimated 0.2–0.6 ms per frame near the base):**
- skip `_rasterizer_model_begin` when the GE is active and the model has no fallback parts;
- skip the transparent submit when `!psp_keep("transparent")`.

### 4.3 LOD selection uses a 480-line screen; the PSP has 272 lines

On the PSP build, `halo_screen_width()` is at least 640 and `SCREEN_HEIGHT` is 480 (`d3d8_gl.c:75, 85-100`; the PSP build defines `HALO_ANDROID`). So `render_frustum_sphere_diameter_in_pixels` reports 480/272 = **1.76× more pixels** than the object really covers. Model LOD (`models.c:805-810`), shadow size thresholds and lighting refresh tiers all pick finer levels than Bungie's thresholds intend for that on-screen size.

**Fix:** scale `level_of_detail_pixels` by 272/480 in `object_get_level_of_detail_pixels` (`render_objects.c:448-463`). This is arguably *more* faithful: it picks the LOD the art was set up for at that pixel size. Gain: fewer parts and vertices per model, on both the CPU submission and the GE (estimated 10–30% of model parts in wide views).

**Related correctness note, not performance:**
- The game's frustum is 640×480 (4:3), but `psp_ge_window_begin` projects with aspect 480/272 (`psp_ge.c:131`) and the game's vertical FOV.
- So the screen's left and right edges show space the game culled. Expect missing BSP surfaces and objects popping at the sides.
- Setting `display.screen_width` (≈ 848) aligns the two. It costs a little more visible geometry.

### 4.4 First-person weapon without interpolation

Once interpolation is off (build 19), the remaining cost per frame is:
- the game's own FP posing: a state animation, up to ~7 overlay applications over ~40–50 nodes, compressed keyframe decode with double `floor`;
- `animation_graph_node_matrices_from_orientations`;
- `model_remap_node_matrices_to_match_animation_graph` at draw time.

Estimated 0.3–0.8 ms. Narrowing helps; a VFPU matrix path would roughly halve the matrix part.

### 4.5 GE submission and the shared bus

`psp_ge.c` builds with `GU_DIRECT` and `sceGum` (non-VFPU `libpspgum`):
- v8 measured 266 vs 62 cycles per draw. At ~60–120 draws that's only ~0.05 ms.
- The bigger effect: in `GU_DIRECT` the GE starts reading **vertices from main RAM** (level and models) while the CPU continues through `render_objects` and the rest of the frame. v8b measured +272% on CPU misses during GE vertex reads.

The frame ends with `sceKernelDcacheWritebackAll` + `sceGuFinish` + `sceGuSync` (`psp_ge.c:408-411`), so **the tick never overlaps the GE**. Keep it that way until vertices live in VRAM. A double-buffered GE overlapping the next tick would slow every tick miss.

**A/B worth doing:** build the frame into a `GU_CALL` list and kick it at frame end, then compare `render_objects` time. This measures how much bus contention costs the frame today.

### 4.6 Visibility and the rest

- `structure_visibility_compute` (0.4 / 3.5 ms) builds the per-frame surface list the GE path consumes (`structure_visibility.c:1682-1777`). It stays until baked per-cluster display lists replace it.
- `lights_preprocess_scene` and `structure_render_preprocess` are small in the measured profiles.

---

## 5. The port layer

### 5.1 `render_interpolation.c` and `network_distributed_tick`

See 1.1 and 1.2: interpolation is the expensive one, the netcode tick is negligible.

### 5.2 Null Direct3D path

- Draws exit early (`prepare_draw` returns at `!device.gl_ready`, `d3d8_gl.c:2472`).
- `SetVertexShaderConstant` does a 16-byte `memcmp` and copy per row (`d3d8_gl.c:1000-1014`).
- The real cost is on the **game** side, building parameters for it (4.2, and the passes build 17/19 skip).
- Still running in build 19, besides model begin and transparent submit (4.2): lens flare occlusion test submission, `game_engine_post_rasterize_objects`, `rasterizer_screen_flash`, `render_ui_widgets`, and the decal begin/end loops (`render.c:341-415`). All small.

### 5.3 Assertions

- Evaluated in release (`cseries.h:115-136`): 4,972 `release_assert_failed` call sites (verified).
- `PSP_ASSERTS_COLD` is already wired in `cseries.h:131-133, 219-221`. **Define it and add `-freorder-blocks-and-partition`** (with `-mno-explicit-relocs`, as the feasibility notes found). Measured there: hot code −7–9%. Zero semantic change.
- Further, and safe because they are pure: compile out the `valid_real_*` family checks (`unit_verify_vectors`, `match_assert_valid_real_*`, the double root-matrix check at `objects.c:2639/2755`). Estimated 1–3% of the tick.
- `PSP_ASSERTS_OFF` stays off until the side-effecting asserts are audited (`heap_insert`, `hs_parse_variable`).

### 5.4 Input (`input_update` 0.8 ms per frame)

Each `XInputGetState(port 0)` (`xinput_sdl.c:529-570`):
- pumps events and calls `sdl_gamepads` (`SDL_GetGamepads` **mallocs** each call, `psp_sdl.c:195-208`);
- reads the pad through ~8 + 6 `SDL_GetGamepadButton` and 6 `SDL_GetGamepadAxis` calls. **Each** does `read_controls` → `sceCtrlPeekBufferPositive` plus the script check (`psp_sdl.c:175-193`).

**Fix:** read once per `XInputGetState` (or cache by time) and use a static gamepad list. Estimated −0.5–0.7 ms per frame. Exact.

### 5.5 Debug, profiling and logging

- **Heartbeat thread priority 0x18 is higher than the game thread (0x20)** (`psp_platform.c:553`). Every 10 s it formats ~60 `%f` lines through soft-float `vfprintf`, sorts, and writes the Memory Stick. Every log line is `sceIoOpen` / write / `sceIoClose` (~7 ms per line on Sheepdog01's card; `psp_platform.c:114-141`) under a semaphore the game thread also takes.
  - The game thread logs synchronously too: the GE log every 300 frames (`psp_ge.c:413-416`), the "STABBED" error.
  - **Likely contributors to the 60–256 ms frame spikes** (estimated). Fix: heartbeat priority below the game (e.g. 0x30); log to a RAM ring buffer flushed by that thread; no file I/O on the game thread.
- Per-object timing probes (1.5) and breadcrumbs: keep them as a build option.
- **Frame pacing in build 19** (1.4, finding 9):
  - `main_rasterizer_throttle` spins on the emulated vblank counter (`main.c:2482-2507`). The counter advances only when the vblank pthread (`d3d8_gl.c:617-660`) gets to run.
  - On the PSP that thread must be **higher** priority than the game thread. Otherwise the spin starves it until the 1 s timeout.
  - Check the build 19 log for "stuck waiting for VBLANK callback!". Long term, pace with `sceDisplayWaitVblankStart` instead of a spin.

---

## 6. Knobs that trade cost for fidelity, ranked by visible impact

| Visible impact | Knob | Where | Cost it saves |
|---|---|---|---|
| **None (exact)** | Double narrowing; trig wrappers in float | Section 3 | Tick and frame, 0.3–1.5 ms per tick |
| **None (exact)** | Static-object sleep; pose cache for parked vehicles | 2.2, 2.3 | 2–5 ms per tick on Blood Gulch |
| **None (exact)** | Per-vertex GE classification; input once per poll; cold asserts; inline accessors | 4.1, 5.4, 5.3, 2.6 | 1–6 ms per frame, 0.5–1 ms per tick |
| **None (arguably more faithful)** | LOD pixel size from the real 272-line screen (×0.567) | `render_objects.c:448-463` | Model parts and vertices; GE time |
| Very low | Particle collision only near the camera (no structure raycast past ~N wu or for particles under ~2 px) | `point_physics.c:164-177` via `particles.c:1054-1055` | Particle raycasts per frame (the driving gap) |
| Very low | Half-rate pose refresh for distant, at-rest, unseen objects | `object_update` | Node matrices |
| Very low | Hover-vehicle "settled" sleep | 2.3 | Physics plus antigrav raycasts per parked Ghost |
| Low | Particle / particle-system caps for a 480×272 screen (`HALO_PORT_MAXIMUM_PARTICLES` 1024 → 512, `..._SYSTEM_PARTICLES` 512 → 256) | `psp_capacity.h` | Particle update and draw in heavy combat |
| Low | Decal cap and lifetime (`MAXIMUM_DECALS_PER_MAP` 2048) | `decals.c:245` | Decal build on impact (clips against the BSP) and drawing |
| Low–medium | Detail objects (grass) density or distance | `structure_detail_objects.c` | GE fill and submission once drawn |
| Medium | Dynamic object shadows off (`render_shadows`) | `render_objects.c:264` | 1.9 / 7.9 ms measured on the null path (build 18) |
| Medium | Lens flares, weather and contrails off (`render_*_enabled`, `render.c:148-151`) | | Small |
| Medium–high | Draw distance and fog (approved lever) | Camera z_far, `render.fog` | Visibility, GE |
| High | AI perception and update stagger | AI | Big on campaign, changes behaviour timing |
| High | 20 Hz tick | Tick-rate patch | −33% tick CPU per second |

---

## 7. Memory layout, cache, scratchpad, VFPU, Media Engine

**Object data:**
- Datums live in the 1 MB object pool, with node matrices in the same allocation (`object_header_block_get`), which is good per-object locality.
- Datum sizes: vehicle 0x47C, scenery 0x1A8, biped 0x480 (`object_types.c`). An update touches 4–8 of their 64-byte lines plus the node array.
- `real_matrix4x3` is 52 bytes (13 floats), so node arrays straddle lines and aren't 16-byte aligned. The layout is fixed by the game state.
- The header array (12 bytes × high-water count) is walked 3 times per tick: small.

**Tags:** definition → model → animation graph → tag-instance table; each is usually a miss per object. Sorting the update loop by definition (scenery clusters of the same tree or rock) would help, but is moot if scenery sleeps.

**Scratchpad (16 KB, one owner thread):**
- best first use is the GE classification outcode buffer (4.1);
- next, the top levels of the collision BSP (nodes and planes, ~2 KB) for raycasts and pill tests, and per-tick scratch for node orientations (already on the stack, which is warm).

**VFPU:**
- `object_compute_node_matrices` and `render_model` relative matrices: `vmmul`, 8× faster than the FPU (measured);
- GE classification transforms (`vtfm3`);
- quaternion nlerp and normalise in animation (`vrsq`).
- **Constraint:** `real_matrix4x3` isn't 16-byte aligned and the PSP-1000 corrupts unaligned `ulv.q`. Use `lv.s` scalar loads or copy to aligned temporaries.

**VRAM as CPU memory** (132 ns vs 220 ns, measured): a candidate home for the tag-instance table or the top of the collision BSP. It competes with textures.

**Media Engine:**
- particle point-physics raycasts and GE level classification are both read-only on tag data (BSP, vertices) with small outputs, which suits a non-coherent second core;
- the feasibility plan's line-of-sight raycasts are the AI-side equivalent.

---

## 8. Ranked action list

Gains are at 333 MHz on Blood Gulch unless noted. Est. = estimated, meas. = measured.

| Rank | Action | Gain | Effort | Fidelity risk |
|---|---|---|---|---|
| 1 | **Confirm build 19** on hardware: FP update, `render_objects`, gap, HUD; check the vblank-throttle spin (5.5); remove per-object probes from the comparison | Expected −8 to −12 ms per frame on foot vs build 18 (est.), mostly interpolation | None (run it) | None |
| 2 | **Narrowing header** for `sqrt`/`floor`/`ceil`/`fmod` + **float trig wrappers** (`halo_math.h` under `HALO_PSP`) + the explicit-double table in section 3 | −0.3 to −1.5 ms per tick on Blood Gulch, more on `b30` (est.) | Small | None (narrowing is exact); tiny for trig and `fabsf` |
| 3 | **Static-object sleep** (scenery first) using the active bit | −2 to −2.5 ms per tick (est.) | Small–medium | None if the predicate and wake-ups are complete |
| 4 | **Measure vehicles**: at-rest by type plus sub-phase timing; then **pose cache** for parked, unoccupied vehicles | −1 to −3 ms per tick (est.) | Medium | None |
| 5 | **Per-vertex GE classification** in the scratchpad (later VFPU) | −1.1 / −4.5 ms per frame on foot / near base (est. from 1.5 / 6.5 meas.) | Small–medium | None |
| 6 | **Input: one pad read per poll**, no malloc | −0.5 to −0.7 ms per frame (est.) | Small | None |
| 7 | **`PSP_ASSERTS_COLD` + `-freorder-blocks-and-partition`**; compile out pure `valid_real_*` checks | Several % of tick and frame (hot code −7–9% measured in the feasibility notes) | Small | None |
| 8 | **Logging and heartbeat** below game priority, ring buffer, no file I/O on the game thread | Removes periodic stalls (est.) | Small | None |
| 9 | **LOD at real pixel size** (×272/480) | 10–30% fewer model parts in wide views (est.) | Small | None (matches the intended LOD) |
| 10 | Skip null-path `_rasterizer_model_begin` and transparent submit when the GE draws | −0.2 to −0.6 ms per frame (est.) | Small | None |
| 11 | Inline `object_get` / `tag_get` / `TAG_BLOCK_GET_ELEMENT` fast paths | −0.3 to −0.8 ms per tick (est.) | Small–medium | None |
| 12 | Hover-vehicle settle rule | −0.3 to −1 ms per tick per hovering vehicle group (est.) | Small | Low (idle bob) |
| 13 | Particle collision LOD and caps, before the GE draws particles | Keeps the driving gap (6 ms per tick meas.) from returning | Small | Very low–low |
| 14 | VFPU node matrices and relative matrices | −0.3 to −1 ms per tick and per frame (est.) | Medium | None |
| 15 | `GU_CALL` list A/B to measure bus contention; later VRAM vertices | Unknown until measured | Small (A/B) | None |
| 16 | Media Engine: GE classification or particle raycasts | −1 to −2 ms per frame (est.) | High | None |
| 17 | Dynamic shadows, draw distance, AI stagger, 20 Hz | Section 6 | Varies | Medium–high, Sheepdog01's call |

---

## Appendix A: how the double-precision sites were mapped

- Each hot file was recompiled from the build mirror (`/tmp/psp_halo/tree`, read-only) with `psp_build.py`'s exact flags plus `-g`, into a scratch directory.
- `psp-objdump -dlr` then mapped every relocation to `__extendsfdf2`, `__truncdfsf2`, `__muldf3`, `__adddf3`, `__divdf3`, the double compares, `sqrt`, `floor`, `ceil`, `fmod`, `rint` or `halo_*` back to its source line.
- Whole-ELF counts (verified):

| Routine | Call sites |
|---|---|
| `__extendsfdf2` | 1,097 |
| `__truncdfsf2` | 463 |
| `__muldf3` | 268 |
| `rint` | 160 (null-path rasterizer colour packing) |
| `floor` | 52 |
| `fmod` | 46 |
| `sqrt` | 28 |
| `ceil` | 6 |

- Narrowing check: `(real)sqrt(x)` on `psp-gcc 15.2` → `jal sqrt` (double); with the `PSP_NARROW1` macro → `sqrt.s`.

## Appendix B: per-phase prediction for build 19, on foot (estimated)

| Phase | Build 18 (meas.) | Build 19 expectation |
|---|---|---|
| Tick (`objects_update`) | 8.2 ms | ~8 ms + probe overhead; scenery ~2.5, vehicles ~3–4, player and weapons ~1–1.5 |
| Gap after tick | 1.3 ms | ~0.2–0.4 ms (`game_frame` only) |
| `render_objects` | 7.5 ms | ~2–3 ms |
| FP weapon update | 3.6 ms | ~0.3–0.8 ms |
| Shadows, transparent, particles, decals, HUD | 9.7 ms | ~0 (skipped) |
| Level (classification + GE) | 1.5 ms | 1.5 ms (→ ~0.4 with 4.1) |
| `input_update` | 0.8 ms | 0.8 ms (→ ~0.1 with 5.4) |

---

## 9. Addendum, build 46 (2026-10-03): where the CPU's cycles go, by phase [measured, Sheepdog01's PSP-1000]

Build 46's profiler splits the hardware counters by what the game was doing (`halo_profile_phase`, a system call into `halo_profile.prx`; see the hardware reference §4). Blood Gulch, single player, steady play after the first 20 s (`b46_run1`, 15 windows of 5 s):

| Phase | Share of CPU cycles | Waiting on memory | I-cache misses / M cycles | D-cache misses / M cycles |
|---|---|---|---|---|
| Drawing (`main_game_render`) | **55–84%, typically ~70%** | 47–59% | 2,700–3,200 | 1,400–1,750 |
| Game tick (`game_tick`) | 14–30%, typically ~17% | 43–50% | 4,300–5,300 | 1,900–2,400 |
| Other (main loop, input, sound outside the two) | 8–20% | 44–51% | 4,600–5,500 | 1,300–2,000 |
| Frame end (GE finish, swap, texture cache) | 0.1–0.7% | 55–70% | ~6,000 | ~3,600 |

- **Drawing is the CPU's main job now, not the tick.** On Blood Gulch the tick is ~5–8 ms at 30 ticks/s (15–25% of a second); drawing is 25–50 ms a frame at 12–28 fps. The CPU is never idle ("asleep" 0.0%).
- **The GE isn't the limit.** The frame end waits 55–80 µs a frame for the GE (worst 0.3–3.6 ms), so the GE finishes each list well before the CPU has the next one. Anything that only speeds up the GE (texture placement in VRAM, fill) can't raise the frame rate until the CPU side shrinks. Build 46's `vramranked` A/B is expected to show little; it is still the first test of that lever.
- **The tick has the worse instruction-cache behaviour** (about 5,000 I-misses per million cycles against drawing's 3,000): more varied code per unit of work. Drawing's cost is mostly data (memory stalls 50–58%).
- The breadcrumb profile (`profile.txt`, same run) agrees on the large items: `objects_update` 21% (tick), `render_object_list` 19%, `structure_render_lightmaps` 8%, the HUD ~7% (unit + weapon interface), `structure_visibility_compute` 4%, `sound_render` 4%, shadows 3.5%.
- Caveat: the counters see the whole CPU, so the sound mixer thread's time lands in whichever phase it interrupted (mostly drawing, by share of time).
- Coprocessor 0 register 24 read `0xEC400000`: bit 0 clear, as expected on a PSP-1000 (no L2).

**Build note:** build 46 shipped with a stale `psp_tex.o` (the `PSP_REUSE` rebuild kept objects whatever their age). `psp_build.py` now reuses an object only when it is newer than its source.

**The `vramranked` A/B (b46 run 1 largest first, run 2 ranked by learned binds; same build, similar play) [measured]:**
- It works mechanically: the share of texture binds served from VRAM went from ~13% (the 12 most bound textures all in RAM) to ~88% (all 12 in VRAM).
- No measurable frame-rate change: GE wait 53–77 µs a frame in both, frame times overlap at matching activity.
- The drawing phase's memory stalls fell from 53.3% to 50.8% of its cycles at unchanged miss rates (each miss a little cheaper: less GE traffic on the bus?). Small and within activity noise, but in the expected direction. Sheepdog01 felt run 2 smoother.
- Verdict: exact and harmless; worth keeping as the default once the learned file is written at map end instead of mid-play (the write costs 40–56 ms). It is not where the frame time is.

## 10. Addendum, builds 47–48 (2026-10-03): inside the renderer's CPU time

**Measured on hardware (b47 run 1, Blood Gulch, heavy vehicle play):**
- One `sceKernelGetSystemTimeLow` reading costs **588 ns** (~196 cycles). Every timer and breadcrumb step pays this; build 47's extra marks add roughly 0.1–0.3 ms a frame.
- The renderer's own CPU time was 10–20 ms a frame. Level draws ~4.6 ms: classifying corners 1.1–3.3 ms; **the passes (texture binds and list commands) 1.4–3.1 ms, ~175 µs a draw** (about 18 µs in the emulator, a 10× gap no other part shows). Model parts ~4.4 ms over ~25 parts: binds, state and lighting ~40 µs a part; geometry 1.0–2.2 ms; second passes 1.0–2.1 ms. Decals ~20 µs a call. Shadows 1.2–3 ms.
- The game's side of drawing objects (`render_object_list`, ~11 ms a frame) splits into `rasterizer_model_draw` 5.7 ms (my GE code inside it), shadow model draws 1.1, model begin 1.0, widgets 0.8, transparent submit 0.7, per-object setup 0.6, relative node matrices 0.5, and `object_get_level_of_detail_pixels` 1.5 (before it).
- Two screenshots cost about 4 s of frame time between them (frame-end steps of 48 and 10 ms a frame in those windows): don't take screenshots in a run meant for timing.

**Found by reading pspgu's object code [verified]:** `sceGuStart` ORs the display list's address with `0x40000000`, so **every GE command word, and everything a draw copies into `sceGuGetMemory` space, is an uncached store** (the counters' 1,500–3,000 uncached stores per million cycles). And in `GU_DIRECT` every `sceGuDrawArray` (and `sceGuGetMemory`) makes the system call `sceGeListUpdateStallAddr`, which ends with `sync`, draining the CPU's write buffer (uofw `kd/ge/stall.S`). The firmware itself carries a "lazy" stall-update patch for one game (Genso Suikoden I&II, uofw `ge.c`) that called it too often. This is the likely reason the passes cost 10× the emulator's figure.

**Build 48 (exact, both with keep words to undo):**
- `list_take` regions of 256 bytes or more are written through the cached view on whole cache lines of their own (64 bytes of slack each side; lines claimed with `cache 0x18`, create dirty exclusive), then written back and invalidated (`cache 0x1B`) before the GE may read them. "listuncached" restores the old path.
- `sceGeListUpdateStallAddr` is wrapped (`--wrap`): the cached regions are flushed there, and only every 16th update reaches the GE (always the finish's). "stalleach" restores one update per draw. Emulator: 266 updates a frame, 17 passed on.
- The by-phase hardware line now counts uncached stores per phase, the test of the first change.
