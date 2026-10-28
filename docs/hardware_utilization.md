# Hardware utilization: does the port use the PSP well?

Draft, phase 1 (the survey) in progress. The hardware session's brief is in the lab (`coord/handoff/hardware_2026-10-28.md`). Phase 2 turns this into a ranked reshuffle for Sheepdog01 before anything is rebuilt. Labels: [M] PSP, [E] emulator or estimate, [D] code or data, [U] inference.

## 1. Which lane limits each map

The main CPU binds all seven campaign intros [M] (w142, PSP-3000, tm-plain 40566969386; lab findings `timemap_lanes`). The GE is busy 22-68% of the frame and the ME 34-69%.

- **Record dominates** (the main CPU's drawing) on a10 (30.1 vs 2 x 14.0 ms of ticks) and a30 (38.6 vs 2 x 11.5). Moving the draw walk off the main CPU (Plan C) pays most here.
- **The tick dominates** on b30 (2 x 44.4 vs 43.2), c10, c20 and d20. Faster tick maths pays most here.

## 2. Where the main CPU's cycles go

Medians over the hardware profiler's ~1 s windows, with loading windows dropped [M] (lab findings `cpu_cycles_by_kind`).

| Map | Waiting on memory | Internal stalls | VFPU | Coprocessors | Issuing |
|---|---|---|---|---|---|
| a10 (intro) | 57.8% | 6.8% | 0.4% | 3.1% | 31.8% |
| a30 (intro) | 57.2% | 7.1% | 0.3% | 3.8% | 31.6% |
| a50 (intro) | 61.8% | 6.4% | 0.4% | 3.3% | 28.1% |
| b30 (intro) | 62.8% | 6.1% | 0.5% | 4.2% | 26.4% |
| c10 (intro) | 56.2% | 7.2% | 0.6% | 4.9% | 31.1% |
| c20 (intro) | 62.6% | 6.4% | 0.5% | 3.0% | 27.5% |
| d20 (intro) | 53.7% | 7.6% | 0.4% | 5.6% | 32.7% |
| b30 (route, 40089996527) | 57.2% | 7.2% | 0.4% | 4.9% | 30.3% |
| a30 (route, 40096315033) | 53.3% | 8.1% | 0.4% | 4.4% | 33.8% |
| d20 (opening, PSP-1000, 40096390859) | 52.6% | 7.9% | 0.3% | 3.0% | 36.1% |

**What this means:**
- The VFPU does one job in the whole port (`matrix4x3_multiply`).
- Only about 30% of cycles do work at all. The VFPU can shrink only that ~30%; the memory wait (53-63%) is untouched by it.
- So a VFPU rewrite's ceiling is a fraction of its function's share, and the memory side (item 4) is the larger half.

## 3. The float inventory (api-job hwfloat1 40731074790, read and spot-checked)

Emulator instruction-fetch shares from the cache maps of b30, d20 (40640730484) and a30, a50, c10 (40599003550), crossed with the code at w142 [D]. Worth is the fetch share times the estimated float fraction of the body [U]. It is an upper bound on the compute half only: the PSP's memory wait isn't in it.

**Alignment:** nothing in the engine is 16-byte aligned by type [D]. Every float struct is 4-aligned, so on the PSP-1000 VFPU code needs `lv.s` loads or aligned temporaries (`ulv.q` corrupts a register, `halo-psp-feasibility.md`).

VFPU forms:
- **E:** element-wise add, mul and div in the FPU's order. Exact, measured (vfpuround).
- **D:** needs `vdot` or `vmmul` accumulation. Exactness not measured.
- **S:** needs `vsqrt` or `vrsq`. Not exact.
- **U:** unaligned data.
- **N:** no fit (branchy, mostly compares, one dot a step).

### The tick

| Function | Share of the tick's fetches | Form | Worth [U] |
|---|---|---|---|
| `psp_bsp3d_test_sphere`, `psp_bsp2d_test_sphere`, `collision_bsp_test_vector_recursive`, `bsp3d_test_point` | 1-12% each | N (one plane dot and compares a node: a memory-bound walk) | ≤ 0.5-2.8 each, but not VFPU material |
| Posing chain: `matrix4x3_rotation_from_quaternion`, `quaternions_multiply`, `quaternions_interpolate_and_normalize`, `quaternion_normalize`, aim blend | 0.6-1.6% each | E (the normalizes need sqrt on the FPU: VFPUQ); output matrices unaligned (U) | ≤ ~3.8 together |
| `object_compute_node_matrices_with`, `matrix4x3_transform_point`, `valid_real_vector3d_axes3` | 1.0-4.4% | D | ≤ ~3.3 together |
| `fast_vector_intersects_sphere`, `sphere_intersects_cluster_portal`, `path_state_traverse` | 0.6-2.5% | N, part S | ≤ ~2.5 together, early exits limit it |

### Drawing

| Function | Share of drawing's fetches | Form | Worth [U] |
|---|---|---|---|
| `render_frustum_cube_visible` | 1.7-5.2% | D, 4-wide bounds tests (lever RM). The frustum's planes sit at 8 mod 16 (`render_cameras.h`, checked), so it needs one aligned copy a frame, which is cheap | ≤ 4.4 |
| `planes_intersect_rectangle` (a50), `weather_particle_system_render` (c10) | 2.0-3.2% on one map each | D | ≤ 1.8, 1.6 |
| `build_sprite` (vertex loop), `convex_polygon2d/3d_clip_to_plane` | 1.3-4.9% | E in the loop, otherwise N | ≤ 1.3; the clips are N |
| `structure_visibility_*` | 1.0-5.3% | Not settled (inlined float work unread) | CAN'T TELL |

### What it says so far

1. **The tick's hottest float work is the collision BSP walks.** They are one dot product and two compares per node, chasing tag data through memory: a placement or memory question, not a VFPU one.
2. **The VFPU's clean candidates are posing and the frustum tests.**
   - Posing: the quaternion and matrix chain, ≤ ~7% of the tick's fetches together. Coordinate with the budget session's LBM, which removes ~42% of b30's biped matrices outright.
   - The frustum tests: RM, ≤ ~4% of drawing on the maps that show it.
   - Both are upper bounds on the compute half. On the PSP the gain is those shares times roughly the 2-2.6x VFPU speed-up, minus the memory wait those functions keep.
3. **The one VFPU job isn't bit-exact today.** `matrix4x3_multiply` runs on `vmmul` and is switched on by a tolerance check (`psp/src/psp_vfpu.c`: `worst < 1e-5f`; lab levers.tsv calls it "last-bit different"). So `vmmul`'s accumulation order already differs from the FPU's somewhere. Measuring `vdot`/`vmmul` against the FPU (the brief's first open question) decides the D rows. Whether node matrices reach multiplayer's shared state is item 6 (api-job hwmpbound1).
4. **Not settled:** the alignment of tag data (collision planes, portals, keyframes; a map-level check of block offsets mod 16 would settle it); `objects_update`, `effect_update` and `structure_visibility_*`'s inlined float work.

## 4-6. To come

- 4: the memory side.
- 5: the placement map.
- 6: the multiplayer boundary.
