# Hardware utilization: does the port use the PSP well?

Draft: phase 1 (the survey) done; phase 2's ranked list in section 7, for Sheepdog01 before any rebuild. The hardware session's brief is in the lab (`coord/handoff/hardware_2026-10-28.md`). Labels: [M] PSP, [E] emulator or estimate, [D] code or data, [U] inference.

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

## 4. The memory side (from the lab's findings; no new runs)

**Instruction misses outnumber data misses** [M]. Per million cycles, the intros show 3,456-4,916 I-misses against 2,470-3,121 D-misses (`cpu_cycles_by_kind`), about 1.2-1.8x. On a10 the tick alone has 5,035 against 2,814 (w139). Code size and layout is the largest single memory lever. It is already being worked (the `tgc` layouts, `tick_imiss_by_layout`). Denser code helps it, the VFPU's included.

**Where the data misses come from** [E] (cache model, lm1 39895433740; b30, a30, c20, d20):

| Source | Share of data misses |
|---|---|
| The tag window (tag data, mostly the collision BSPs and their planes) | 28-37% |
| The collision walks' own state | 15-30% |
| The object pool | 12-14% |
| Thread stacks | 9-10% |
| Node matrices and animation | 5-6% |
| Spheres and cluster references | 3-4% |

Collision is about half of it, which matches section 3: the tick's hottest float work is a memory walk, not maths.

**The scratchpad** (16 KB, as fast as a cache hit, off the bus) [M, w142 tm-plain log]: 15,104 of 16,384 bytes used:
- the models' near-cut corner table (8,448 B, drawing);
- the profiler's slots (6,144 B) and front table (512 B).

The split was set at build 108 by the cache model's misses per byte (`psp_scratch.c`, `overnight_b108.md`). The tick's top miss sources today (tag window, collision) get none of it. The feasibility notes' lever (the top of each BSP in the scratchpad) was never built. **Lead:**
- re-rank the 16 KB by misses per byte on w142's cache map;
- price the winner with a PSP pair, never the cache model (the lab's cache rule; `noscratch` is the off switch);
- ask whether the profiler's 6.6 KB belongs there in play builds.

**The ME's eDRAM** (lab `psp/sierra/findings/edram.md`):
- About 1,664 KB is claimable.
- The main CPU can't load from it; only the ME or DMA can. So it is the ME's memory, and it matters to placement: an ME job's tables can live there.
- The two big claims exclude each other: CPA, the checkpoint (1,089 KB), and R2, the sound cache (1,664 KB; rejected on the PSP as built).

**The bus: the GE's traffic is what costs the tick** [M]:
- When the ME and the main CPU both walked RAM, neither slowed (mewalk).
- The ME's loads cost the tick 1.4-6.7% (`tick_change_me_busload`, PSP-1000).
- When the GE pulled from main RAM under M6 #2, 77-94% of the tick's rise was contention: each miss stalled longer (`contention_tick_ms`, PSP-3000). With the models' vertices read from VRAM (mevram) it fell to 55-76% of a smaller rise.
- That answers the brief's open question: the ME's traffic is cheap; the GE's main-RAM reads are what the tick pays for. So data the GE reads belongs in VRAM first.
- DMA's own share of the bus budget is about 1 [D] (`bus_budget`).

**VRAM today** [M, w142 tm-plain 40566969386, a10's intro, PSP-3000 log]: of the 2 MB, the display takes 832 KB, the texture cache 616 KB and the model buffers (`vrammodels`) 616 KB.
- **Textures:** the cache is filled by binds per byte; its best third holds 189-270 KB. Several of the most-bound textures still live in RAM (a 128x128 at 4,896 binds in 900 frames).
- **Model buffers:** they run full. In different 300-frame windows they were full 0-274 times and copied up to 6 MB into VRAM.
- **The room is spoken for** (the master's 12:55 note; levers.tsv M6B, CTNG, VRSP, VRBTEX). The dashboard's `vram_plan` is b115's and stale.
- **What's left is choosing what earns the room,** not finding more of it. The copies into the model buffers are bus traffic of their own: a lead for phase 2 is to price their churn against what they save the GE.

### The bus census: no cheap room left

The emulator's bus census (`buscensus`, the halo-local patch's item 23; read with the lab's `bus.py`) counts the bytes each frame pulls from main RAM, by source and by who reads them: the main CPU's and the ME's cache fills and write-backs, and the GE's lists, vertices and textures [E].

**b30, 90 s on its route** (lab bus7 40884427736, bus8 40894428250): about 12.9 MB a frame.
- By reader: the main CPU fills 5.6 MB and writes back 1.2; the ME fills 2.0 and writes back 0.75; the GE reads 1.4 of vertices, 1.4 of texels and 0.25 of lists.
- By source:
  - the program's `.bss`, 2.5 MB: a long tail; its largest symbol is the geometry cache's entry table, which the ME's scans read at 148 KB a frame;
  - the heap, 2.2 MB: mostly the GE's level copies (their arena ~0.9 MB, with the level carve ~1.4 MB, ~1.0 of it the GE's vertex reads) and gedefer's buffer (~0.34);
  - the texture cache, 1.4 MB;
  - the objects' game state, 1.05 MB;
  - tag data, 1.0 MB, the level collision BSP 0.42 of it;
  - the display list, 0.9 MB.
- c10 is the same kinds at about half the size (6.5 MB a frame).

**Three cuts, each priced on the PSP:**

| Cut | Bytes saved [E] | PSP |
|---|---|---|
| BSPDFS: the collision BSP renumbered depth-first | 59 KB a frame on b30 (420 to 361) | even on b30 (40862518281) and a10 (40875906608): shelved |
| MEPLACE: the ME's geometry lookups through the place keys | ~260 KB a frame on b30 (2.1%; the ME's entry-table fills 148 to 33) | null on b30, PSP-3000 (40894645109: flight 158.2 to 158.0 ms, tick 53.1 to 53.0): rejected |
| levelcompact (b87): the level's vertices 20 to 10 bytes | about half of the GE's ~1 MB a frame of level vertex reads | no gain three times (b87, b93, b102) and ground cracks: settled |

**What it says** [M for the prices, U for the reason]:
- Cuts of 0.5-4% of the bus, on any reader, don't move the frame or the tick.
- What the tick pays for is contention: the GE reading main RAM while the tick misses (the bus subsection above: 55-94% of the tick's rise under overlap). That depends on when the GE reads, not only on how many bytes it reads [U]; levelcompact halved the bytes and gained nothing.
- What's left of the census are things already priced or spread thin:
  - the GE's texels and vertices (mevram and the texture cache already hold what VRAM has room for);
  - the main CPU's own misses (no symbol over ~0.25 MB a frame);
  - the ME's traffic (cheap to the tick: mewalk, `tick_change_me_busload`).
- So "after the bus has room" can't be C step 3's gate: no cheap cut makes room.

**C step 3's gate, proposed instead:** price the snapshot itself, since its copy is the bus traffic C adds.
1. **[E] Count it:** the snapshot's bytes a frame on b30, a10 and c10: the objects' render state, node matrices, lights, effects and decals (walkwrites lists what the walk writes). One emulator run with a counting keep word.
2. **[M] Price it:** one PSP pair on c10 (`keepsets "- | snapdry"`, repeat=2). `snapdry` writes that snapshot at each frame's end, and nothing reads it. The pair prices the copy's own cost, the tick's contention included.
3. **Go** if snapdry costs the frame less than a third of what C takes off the main CPU on the same map. The engine's whole walk is 12-56 ms a frame (b30 55.8 at b138, levers_next_b138.md); its visibility part alone is 8.4 on b30 (visspan, PSP-1000) [M]. Otherwise the copy eats C's gain, and C stays parked.

## 5. The placement map

Each unit's strengths, measured:
- **The main CPU** has the VFPU: 1.9-2.6x its own FPU on the node product [M].
- **The ME** has an FPU exactly as fast as the main CPU's and bit-exact with it. It has no VFPU. Its traffic costs the tick 1.4-6.7% [M], and it runs branchy scalar work at 0.88-0.96x the main CPU's time (mevis [M]).
- **The GE** has spare fill on the main-CPU-bound maps [M]. Its main-RAM reads are what slow the tick (section 4).

So:
- matrix and vector work goes where the VFPU is;
- branchy scalar work goes to the ME;
- bytes the GE reads go to VRAM.

Per-pass figures are research's (lab `coord/handoff/render_ownership_2026-10-27.md` 6b, b30) unless marked.

| Job | Runs on today | Best unit | Why |
|---|---|---|---|
| Visibility (find_clusters, subclusters, find_rendered_objects): 8.0-8.8 ms a frame | main CPU | **ME** | Scalar and branchy, no VFPU, static inputs; measured 0.88-0.93x on the ME, exact, never late (mevis [M]); its one tie to the tick (the marker stamp) is cut by walkapart |
| Level pass: 2.1-3.5 ms | main CPU walk, ME replay | **ME** (B's first job) | No VFPU; self-contained |
| Model walk and node matrices: ~20 ms main, 27.9 ME replay | main CPU walk (VFPU product), ME replay | **Main CPU, more of it on the VFPU** | Holds every VFPU use in the walk; on the ME the node products would take ~2.5-5 ms instead of 1.3-1.9 [M]. Part draws can balance either way (B) |
| Decals, HUD, screen quads: ~0.6 ms | main CPU | ME jobs | Cheap, no VFPU |
| Particles, contrails: 1.3-2.9 ms | main CPU | Main CPU for now | Reach firmware paths that need stand-ins |
| Tick: posing (quaternions, rotation matrices, node walk; ~10% of b30's tick [E]) | main CPU's FPU | **Main CPU's VFPU** (E forms; section 3) | Element-wise forms are exact; LBM first removes the unneeded matrices |
| Tick: collision BSP walks (about half the data misses) | main CPU | **Main CPU, with the scratchpad**; batched queries to the ME later (L3, BW) | One dot a node: memory, not maths. The ME's own cache and cheap bus traffic make batched line-of-sight a fit, once AI's share of the ME is settled against drawing's |
| AI rays | ME ring jobs | ME (as now) | 1-3% of the ME [M] |
| Sound | ME | ME (as now) | |
| Geometry and textures the GE reads | VRAM where they fit | VRAM | 9.6 µs of tick a KB of geometry from main RAM [M]; the VRAM route is nearly used up (the master's second opinion) |

**Plan C re-weighed with the VFPU:**
- Full C, where the ME owns all drawing, gives up the VFPU on exactly the pass that uses it (models). It also keeps the GE's main-RAM reads beside the tick.
- What the hardware argues for is a split: the ME takes visibility, the level pass and the small passes; the main CPU keeps the model walk and does more of its maths on the VFPU.
- That is B plus mevis, the order research and the master's second opinion already reached. The VFPU findings add a reason to keep models on the main CPU, rather than a reason to revisit C.

**A conflict to settle:** RM (VFPU frustum tests, `render_frustum_cube_visible`, ≤ 4.4% of drawing's fetches) runs inside visibility. If visibility moves to the ME, RM has no VFPU to use. Visibility on the ME (8 ms off the main CPU) outranks RM.

## 6. The multiplayer boundary (api-job hwmpbound1 40731381973, read and spot-checked)

The PSP is only ever a client of ChupathingyCE's desktop host (network version 29; `FIXES_MPCLIENT`). **Halo's netcode isn't lockstep: nothing a client computes has to stay bit-exact with the host** [D]. What crosses the wire is either the client's own word (input, aim: taken as given, clamped) or the host's word, which the client accepts and snaps or glides to.

| System | Verdict | Why [D] (engine at the pin d98982b) |
|---|---|---|
| Input, aim | LOCAL (the client's word) | sent as floats, the host clamps them (`network_distributed.c`), nothing recomputed to compare |
| Own biped movement | PREDICTED, bounds-checked | the host takes the client's position within 3.5 units (`HOST_ACCEPT_TOLERANCE`); the client snaps to the host's past 3.0 (`LOCAL_CORRECTION_TOLERANCE`) |
| Other players, vehicles | PREDICTED | host states in 16-bit vectors; under 1.0 unit the copy glides half the gap a tick, past it jumps (`network_objects.c`) |
| Own vehicle | PREDICTED, bounds-checked | 4.0 / 5.0 units (`LOCAL_VEHICLE_TOLERANCE`, `HOST_VEHICLE_ACCEPT_TOLERANCE`) |
| Damage, death | host's | the client deals no damage; its hit reports (floats, node, region) pass the host's bounds (3-6 units, finite), not a bit match (`network_damage.c`) |
| Node matrices, animation, collision | LOCAL | never on the wire; reach the host only through movement and hit reports, both bounds-checked |
| Projectiles, particles, decals, contrails, lights, sound, HUD, drawing | LOCAL | no message types for them (`network_distributed.h`'s enum); projectiles are client-only objects |

**Checks across machines:** none per tick. The engine's checksums (FNV-1a in `network_distributed.c`) are the host deciding whether a state changed since its last send, not a desync test (checked). At join: a token mixing the build time and `tag_groups_checksum()` in non-`DEBUG` builds, and the map version.

**What does have to match** is discrete, not float: objects placed at map load must take the same datum indices as the host's (`network_objects.c`). Whether a Sierra-III bake keeps that order is unchecked.

**So for the bit-exact rule** (the brief's rewording 2): VFPU maths that differs in the last bits is safe for multiplayer, on the tick as well as in drawing. What it could change is behaviour well inside the tolerances: a biped drifting past 3.0 units would snap visibly, and hit reports choose region and material on the client. Neither needs bit equality; both need the play to stay the same, which the campaign's side-by-sides already test. Two things keep statecrc useful in the lab anyway: exactness proofs for A/Bs, and the repeatability of fixed-seed runs. A lever that changes the last bits needs its new maths on both sides of its A/B.

**Riskiest calls** [U]:
1. Own biped prediction: the PSP's collision differs from the PC's (the packed collision tree), so a large drift shows as snaps, independently of any VFPU work.
2. Hit reports: a systematic PSP bias in node matrices would go unnoticed (by design: what the shooter saw hit, hits).
3. Map-load object indices against the bake: a hard failure if wrong; not float.

**Side notes for the multiplayer session:**
- The engine's `port/linux/NETCODE.md` still says version 10.
- The join token's size comes from `sizeof` on an array parameter, so it is pointer-sized: 8 bytes on PC, 4 on the PSP, in non-`DEBUG` builds. Unchecked which the PSP's multiplayer build is.

## 7. Phase 2: the ranked list (draft, for Sheepdog01 before any rebuild)

| # | Lever | Worth | Risk | Exactness | Priced by |
|---|---|---|---|---|---|
| 1 | **Visibility on the ME** (mevis; research's) | 8.0-8.8 ms of the main CPU's frame on b30 [M] | Low: measured exact, never late | Exact (statecrc [E]) | Research's PSP pair; this session endorses its placement |
| 2 | **Posing on the VFPU** (merged with 4 after the count below): the quaternion product, blends and normalizes, and the rotation matrices, in the VFPU's own instructions (`vqmul`, `vdot`, `vrsq`) where they pay | Exact forms alone: ~0.7% of b30's tick fetches [E count], under the 1% gate. The VFPU's own forms: per-call times from `vfpubench` [M, pending] | Medium: unaligned data, so `lv.s` loads | Not bit-exact; posing is LOCAL (section 6), so the A/B carries the new maths on both sides and side-by-sides judge it | `vfpubench`'s PSP run first (times and rounding), then one A/B pair on b30's flight |
| 3 | **The scratchpad re-ranked** by misses per byte on w142 (candidates: the top of each map's collision BSP, the planes it touches first) | Unsized: the tick's top miss sources (tag window, collision) hold none of it today | Low: `noscratch` is the off switch; one owner thread | Exact (only placement) | Cache rule: a PSP pair, never the cache model |
| 4 | (merged into 2) | | | | |
| 5 | **Small passes as ME jobs** (decals, HUD) | ~0.6 ms main [M] | Low | Exact | Part of B |
| 6 | **Collision queries batched to the ME** (L3, BW) | Large but unsized; needs AI's and drawing's split of the ME first | High (XL) | Exact if the ME's FPU is (it is [M]) | Later |
| - | RM (VFPU frustum tests) | ≤ 4.4% of drawing's fetches | | | **Dropped** if 1 lands |

**Sheepdog01's 12:27 ruling:** both rule rewordings are in force; the list goes ahead in this order.

**#2's removal count** (b30's flight, cache map 40640730484 [E]): the five targets are 787 M of 20.0 G tick fetches (3.9%).
- Element-wise forms can only pack the products. `quaternions_multiply` packs well (16 multiplies and 12 adds into 4 swizzled `vmul.q` and 3 `vadd.q`).
- `quaternion_normalize` gains nothing: its sum stays sequential, and the sqrt and divide stay on the FPU.
- The rotation matrix's nine sums each need lanes from different registers, so they stay scalar (~15% of the function).
- That totals ~133 M, about 0.7%: under the gate. So the exact forms alone don't pay.
- The VFPU's own instructions do much more: `vqmul.q` is the whole product in one instruction, and `vdot.q` a whole dot. Since posing is local to the client (section 6), #2 and #4 become one lever, priced by `vfpubench`.
